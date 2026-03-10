#include "SARReTaskingManager.h"
#include "SARMissionManager.h"
#include "SARZoneManager.h"
#include "SARTargetManager.h"
#include "QmlObjectListModel.h"
#include "QGCLoggingCategory.h"
#include "MultiVehicleManager.h"
#include "Vehicle.h"

#include <cmath>
#include <limits>

QGC_LOGGING_CATEGORY(SARReTaskingManagerLog, "Sadron.SARReTaskingManager")

// ============================================================================
// ReTaskEntry
// ============================================================================

ReTaskEntry::ReTaskEntry(int vehicleId, int targetId, int originalZoneId,
                         const QGeoCoordinate &targetCoord, int targetPriority,
                         const QString &targetDescription,
                         HandoffType handoffType, QObject *parent)
    : QObject(parent)
    , _vehicleId(vehicleId)
    , _targetId(targetId)
    , _originalZoneId(originalZoneId)
    , _handoffType(handoffType)
    , _targetCoord(targetCoord)
    , _targetPriority(targetPriority)
    , _targetDescription(targetDescription)
{
}

void ReTaskEntry::setState(ReTaskState state)
{
    if (_state != state) {
        _state = state;
        emit stateChanged();
    }
}

void ReTaskEntry::setCountdownSec(int sec)
{
    if (_countdownSec != sec) {
        _countdownSec = sec;
        emit countdownChanged();
    }
}

// ============================================================================
// SARReTaskingManager
// ============================================================================

SARReTaskingManager::SARReTaskingManager(SARMissionManager *missionMgr,
                                         SARZoneManager *zoneMgr,
                                         SARTargetManager *targetMgr,
                                         QObject *parent)
    : QObject(parent)
    , _missionMgr(missionMgr)
    , _zoneMgr(zoneMgr)
    , _targetMgr(targetMgr)
    , _activeReTasks(new QmlObjectListModel(this))
    , _autoConfirmTimer(new QTimer(this))
{
    // Wire target detection → re-tasking evaluation
    if (_targetMgr) {
        connect(_targetMgr, &SARTargetManager::targetAdded,
                this, &SARReTaskingManager::_onTargetAdded);
        connect(_targetMgr, &SARTargetManager::targetStatusChanged,
                this, &SARReTaskingManager::_onTargetStatusChanged);
    }

    // Auto-confirm countdown ticks every second
    _autoConfirmTimer->setInterval(1000);
    connect(_autoConfirmTimer, &QTimer::timeout, this, &SARReTaskingManager::_onAutoConfirmTick);
}

SARReTaskingManager::~SARReTaskingManager()
{
    _cleanupPending();
}

// ── Settings setters ──

void SARReTaskingManager::setEnabled(bool enabled)
{
    if (_enabled != enabled) {
        _enabled = enabled;
        if (!_enabled) {
            _cleanupPending();
        }
        emit settingsChanged();
        qCDebug(SARReTaskingManagerLog) << "Dynamic re-tasking" << (_enabled ? "enabled" : "disabled");
    }
}

void SARReTaskingManager::setSelectionStrategy(SelectionStrategy strategy)
{
    if (_selectionStrategy != strategy) {
        _selectionStrategy = strategy;
        emit settingsChanged();
        qCDebug(SARReTaskingManagerLog) << "Selection strategy changed to:" << strategy;
    }
}

void SARReTaskingManager::setMinimumPriority(int priority)
{
    priority = qBound(0, priority, 3);  // Low=0, Medium=1, High=2, Critical=3
    if (_minimumPriority != priority) {
        _minimumPriority = priority;
        emit settingsChanged();
        qCDebug(SARReTaskingManagerLog) << "Minimum priority threshold:" << priority;
    }
}

void SARReTaskingManager::setAutoConfirmTimeoutSec(int sec)
{
    sec = qBound(3, sec, 60);
    if (_autoConfirmTimeoutSec != sec) {
        _autoConfirmTimeoutSec = sec;
        emit settingsChanged();
        qCDebug(SARReTaskingManagerLog) << "Auto-confirm timeout:" << sec << "seconds";
    }
}

int SARReTaskingManager::activeReTaskCount() const
{
    return _activeReTasks->count();
}

// ── Core detection handler ──

void SARReTaskingManager::_onTargetAdded(int targetId, const QGeoCoordinate &coordinate)
{
    if (!_enabled) return;

    SARTarget *target = _targetMgr ? _targetMgr->getTarget(targetId) : nullptr;
    if (!target) return;

    // Check priority threshold
    int priority = static_cast<int>(target->priority());
    if (priority < _minimumPriority) {
        qCDebug(SARReTaskingManagerLog) << "Target" << targetId
            << "priority" << priority << "below threshold" << _minimumPriority << "— skipping re-task";
        return;
    }

    // Don't create a new proposal if one is already pending
    if (_pendingReTask) {
        qCDebug(SARReTaskingManagerLog) << "Already have a pending re-task — skipping for target" << targetId;
        return;
    }

    // Select the best drone
    int excludeVehicle = target->spottedByVehicle();
    int selectedVehicle = _selectBestDrone(coordinate, excludeVehicle, priority);

    if (selectedVehicle < 0) {
        qCWarning(SARReTaskingManagerLog) << "No eligible drone for re-task to target" << targetId;
        return;
    }

    // Find the drone's current zone
    SARZone *zone = _zoneMgr ? _zoneMgr->zoneForVehicle(selectedVehicle) : nullptr;
    int originalZoneId = zone ? zone->zoneId() : -1;

    // Create proposed re-task entry
    _pendingReTask = new ReTaskEntry(
        selectedVehicle, targetId, originalZoneId,
        coordinate, priority,
        target->description().isEmpty() ? QStringLiteral("Target %1").arg(targetId) : target->description(),
        ReTaskEntry::Investigation,
        this
    );
    _pendingReTask->setCountdownSec(_autoConfirmTimeoutSec);

    emit pendingReTaskChanged();
    emit reTaskProposed(selectedVehicle, targetId, _autoConfirmTimeoutSec);

    // Start the auto-confirm countdown
    _autoConfirmTimer->start();

    qCDebug(SARReTaskingManagerLog) << "Re-task proposed: vehicle" << selectedVehicle
        << "→ target" << targetId << "(zone" << originalZoneId << "), auto-confirm in"
        << _autoConfirmTimeoutSec << "sec";
}

void SARReTaskingManager::_onTargetStatusChanged(int targetId, int newStatus)
{
    // When a target being investigated is resolved, auto-complete the re-task
    SARTarget::TargetStatus status = static_cast<SARTarget::TargetStatus>(newStatus);

    bool isResolved = (status == SARTarget::Confirmed ||
                       status == SARTarget::FalseAlarm ||
                       status == SARTarget::Rescued);

    if (!isResolved) return;

    ReTaskEntry *entry = _findActiveReTaskForTarget(targetId);
    if (entry) {
        qCDebug(SARReTaskingManagerLog) << "Target" << targetId << "resolved (status"
            << newStatus << ") — completing re-task for vehicle" << entry->vehicleId();
        completeReTask(entry->vehicleId());
    }
}

// ── Operator actions ──

void SARReTaskingManager::confirmReTask()
{
    if (!_pendingReTask) return;

    _autoConfirmTimer->stop();
    _activateReTask(_pendingReTask);
}

void SARReTaskingManager::cancelReTask()
{
    if (!_pendingReTask) return;

    _autoConfirmTimer->stop();

    int targetId = _pendingReTask->targetId();
    _pendingReTask->setState(ReTaskEntry::Cancelled);

    qCDebug(SARReTaskingManagerLog) << "Re-task cancelled for target" << targetId;

    _cleanupPending();
    emit reTaskCancelled(targetId);
}

void SARReTaskingManager::modifyReTask(int newVehicleId)
{
    if (!_pendingReTask) return;

    // Validate the new vehicle is eligible
    if (!_isVehicleEligible(newVehicleId)) {
        qCWarning(SARReTaskingManagerLog) << "Vehicle" << newVehicleId << "is not eligible for re-task";
        return;
    }

    // Stop the countdown, recreate with new vehicle
    _autoConfirmTimer->stop();

    int targetId = _pendingReTask->targetId();
    QGeoCoordinate coord = _pendingReTask->targetCoord();
    int priority = _pendingReTask->targetPriority();
    QString desc = _pendingReTask->targetDescription();

    // Find new vehicle's zone
    SARZone *zone = _zoneMgr ? _zoneMgr->zoneForVehicle(newVehicleId) : nullptr;
    int newZoneId = zone ? zone->zoneId() : -1;

    // Replace pending entry
    _cleanupPending();

    _pendingReTask = new ReTaskEntry(
        newVehicleId, targetId, newZoneId,
        coord, priority, desc,
        ReTaskEntry::Investigation, this
    );
    _pendingReTask->setCountdownSec(_autoConfirmTimeoutSec);

    emit pendingReTaskChanged();
    emit reTaskProposed(newVehicleId, targetId, _autoConfirmTimeoutSec);

    _autoConfirmTimer->start();

    qCDebug(SARReTaskingManagerLog) << "Re-task modified: now vehicle" << newVehicleId
        << "→ target" << targetId;
}

void SARReTaskingManager::completeReTask(int vehicleId)
{
    ReTaskEntry *entry = _findActiveReTask(vehicleId);
    if (!entry) {
        qCWarning(SARReTaskingManagerLog) << "No active re-task for vehicle" << vehicleId;
        return;
    }

    int originalZoneId = entry->originalZoneId();
    entry->setState(ReTaskEntry::Completed);

    // Resume the original zone
    if (_zoneMgr && originalZoneId >= 0) {
        SARZone *zone = _zoneMgr->getZone(originalZoneId);
        if (zone && zone->status() == SARZone::Investigating) {
            zone->setStatus(SARZone::Active);
            qCDebug(SARReTaskingManagerLog) << "Zone" << originalZoneId << "resumed (status → Active)";
        }
    }

    // Regenerate the transect for the zone so the drone can continue
    if (_missionMgr && originalZoneId >= 0) {
        _missionMgr->generateZoneTransect(originalZoneId);
    }

    qCDebug(SARReTaskingManagerLog) << "Re-task completed: vehicle" << vehicleId
        << "returning to zone" << originalZoneId;

    // Remove from active list
    for (int i = 0; i < _activeReTasks->count(); i++) {
        auto *e = qobject_cast<ReTaskEntry *>(_activeReTasks->get(i));
        if (e && e->vehicleId() == vehicleId) {
            _activeReTasks->removeAt(i);
            e->deleteLater();
            break;
        }
    }

    emit activeReTasksChanged();
    emit reTaskCompleted(vehicleId, originalZoneId);
}

void SARReTaskingManager::abandonReTask(int vehicleId)
{
    ReTaskEntry *entry = _findActiveReTask(vehicleId);
    if (!entry) {
        qCWarning(SARReTaskingManagerLog) << "No active re-task to abandon for vehicle" << vehicleId;
        return;
    }

    int originalZoneId = entry->originalZoneId();
    entry->setState(ReTaskEntry::Cancelled);

    // Restore the original zone status so it's not stuck in Investigating
    if (_zoneMgr && originalZoneId >= 0) {
        SARZone *zone = _zoneMgr->getZone(originalZoneId);
        if (zone && zone->status() == SARZone::Investigating) {
            zone->setStatus(SARZone::Active);
            qCDebug(SARReTaskingManagerLog) << "Zone" << originalZoneId << "restored to Active (re-task abandoned)";
        }
    }

    qCDebug(SARReTaskingManagerLog) << "Re-task abandoned: vehicle" << vehicleId
        << "(zone" << originalZoneId << "restored)";

    // Remove from active list
    for (int i = 0; i < _activeReTasks->count(); i++) {
        auto *e = qobject_cast<ReTaskEntry *>(_activeReTasks->get(i));
        if (e && e->vehicleId() == vehicleId) {
            _activeReTasks->removeAt(i);
            e->deleteLater();
            break;
        }
    }

    emit activeReTasksChanged();
}

void SARReTaskingManager::completeAllReTasks()
{
    // Collect vehicle IDs first (completing modifies the list)
    QList<int> vehicleIds;
    for (int i = 0; i < _activeReTasks->count(); i++) {
        auto *entry = qobject_cast<ReTaskEntry *>(_activeReTasks->get(i));
        if (entry) vehicleIds.append(entry->vehicleId());
    }

    for (int vId : vehicleIds) {
        completeReTask(vId);
    }
}

// ── Auto-confirm countdown ──

void SARReTaskingManager::_onAutoConfirmTick()
{
    if (!_pendingReTask) {
        _autoConfirmTimer->stop();
        return;
    }

    int remaining = _pendingReTask->countdownSec() - 1;
    _pendingReTask->setCountdownSec(remaining);

    if (remaining <= 0) {
        qCDebug(SARReTaskingManagerLog) << "Auto-confirm timeout reached — confirming re-task";
        confirmReTask();
    }
}

// ── Activation ──

void SARReTaskingManager::_activateReTask(ReTaskEntry *entry)
{
    if (!entry) return;

    int vehicleId = entry->vehicleId();
    int targetId = entry->targetId();
    int originalZoneId = entry->originalZoneId();

    // Pause the original zone → Investigating status
    if (_zoneMgr && originalZoneId >= 0) {
        SARZone *zone = _zoneMgr->getZone(originalZoneId);
        if (zone && (zone->status() == SARZone::Active || zone->status() == SARZone::Pending)) {
            zone->setStatus(SARZone::Investigating);
            qCDebug(SARReTaskingManagerLog) << "Zone" << originalZoneId << "paused (status → Investigating)";
        }
    }

    // Mark the target as Investigating
    if (_targetMgr) {
        SARTarget *target = _targetMgr->getTarget(targetId);
        if (target && target->status() == SARTarget::Unconfirmed) {
            target->setStatus(SARTarget::Investigating);
        }
    }

    // Move entry from pending → active
    entry->setState(ReTaskEntry::Active);
    _pendingReTask = nullptr;       // Detach from pending slot (ownership transfers to active list)
    _activeReTasks->append(entry);

    emit pendingReTaskChanged();
    emit activeReTasksChanged();
    emit reTaskConfirmed(vehicleId, targetId);

    qCDebug(SARReTaskingManagerLog) << "Re-task ACTIVATED: vehicle" << vehicleId
        << "→ target" << targetId << "(paused zone" << originalZoneId << ")";
}

// ── Drone selection algorithms ──

int SARReTaskingManager::_selectBestDrone(const QGeoCoordinate &targetCoord, int excludeVehicleId, int targetPriority) const
{
    switch (_selectionStrategy) {
    case NearestAvailable:
        return _selectNearestAvailable(targetCoord, excludeVehicleId);
    case PriorityWeighted:
        return _selectPriorityWeighted(targetCoord, excludeVehicleId, targetPriority);
    }
    return _selectNearestAvailable(targetCoord, excludeVehicleId);
}

int SARReTaskingManager::_selectNearestAvailable(const QGeoCoordinate &targetCoord, int excludeVehicleId) const
{
    auto *mvm = MultiVehicleManager::instance();
    QmlObjectListModel *vehicles = mvm->vehicles();

    int bestVehicleId = -1;
    double bestDistance = std::numeric_limits<double>::max();

    for (int i = 0; i < vehicles->count(); i++) {
        auto *v = qobject_cast<Vehicle *>(vehicles->get(i));
        if (!v) continue;

        int vId = v->id();

        // Skip the spotter drone
        if (vId == excludeVehicleId) continue;

        // Skip if not eligible
        if (!_isVehicleEligible(vId)) continue;

        // Skip if already investigating another target
        if (_isVehicleInvestigating(vId)) continue;

        double dist = v->coordinate().distanceTo(targetCoord);
        if (dist < bestDistance) {
            bestDistance = dist;
            bestVehicleId = vId;
        }
    }

    return bestVehicleId;
}

int SARReTaskingManager::_selectPriorityWeighted(const QGeoCoordinate &targetCoord, int excludeVehicleId, int targetPriority) const
{
    Q_UNUSED(targetPriority);

    auto *mvm = MultiVehicleManager::instance();
    QmlObjectListModel *vehicles = mvm->vehicles();

    int bestVehicleId = -1;
    double bestScore = -1.0;

    // Weight factors
    constexpr double wDistance = 0.50;      // Proximity is most important
    constexpr double wBattery  = 0.25;      // Prefer drones with more battery
    constexpr double wProgress = 0.25;      // Prefer drones with less zone progress (less wasted work)

    // First pass: find max distance for normalization
    double maxDist = 1.0;
    for (int i = 0; i < vehicles->count(); i++) {
        auto *v = qobject_cast<Vehicle *>(vehicles->get(i));
        if (v) {
            double d = v->coordinate().distanceTo(targetCoord);
            if (d > maxDist) maxDist = d;
        }
    }

    for (int i = 0; i < vehicles->count(); i++) {
        auto *v = qobject_cast<Vehicle *>(vehicles->get(i));
        if (!v) continue;

        int vId = v->id();
        if (vId == excludeVehicleId) continue;
        if (!_isVehicleEligible(vId)) continue;
        if (_isVehicleInvestigating(vId)) continue;

        // Distance score: closer = higher (inverted, normalized)
        double dist = v->coordinate().distanceTo(targetCoord);
        double distScore = 1.0 - (dist / maxDist);

        // Battery score: higher battery = higher score (normalized 0-1)
        double batteryScore = 0.5;  // default if unavailable
        if (v->batteries() && v->batteries()->count() > 0) {
            // Battery percentage from vehicle fact
            batteryScore = qBound(0.0, v->batteries()->get(0)->property("percentRemaining").toDouble() / 100.0, 1.0);
        }

        // Zone progress score: less progress = higher score (less work wasted)
        double progressScore = 1.0;  // default if no zone
        if (_zoneMgr) {
            SARZone *zone = _zoneMgr->zoneForVehicle(vId);
            if (zone) {
                progressScore = 1.0 - zone->progress();
            }
        }

        double totalScore = wDistance * distScore + wBattery * batteryScore + wProgress * progressScore;

        if (totalScore > bestScore) {
            bestScore = totalScore;
            bestVehicleId = vId;
        }
    }

    return bestVehicleId;
}

// ── Helpers ──

bool SARReTaskingManager::_isVehicleEligible(int vehicleId) const
{
    // Vehicle must be connected
    Vehicle *v = MultiVehicleManager::instance()->getVehicleById(vehicleId);
    if (!v) return false;

    // Vehicle must be flying (actively searching)
    if (!v->flying()) return false;

    // Vehicle should have an assigned zone that is Active
    if (_zoneMgr) {
        SARZone *zone = _zoneMgr->zoneForVehicle(vehicleId);
        if (!zone) return false;
        if (zone->status() != SARZone::Active) return false;
    }

    return true;
}

bool SARReTaskingManager::_isVehicleInvestigating(int vehicleId) const
{
    return _findActiveReTask(vehicleId) != nullptr;
}

ReTaskEntry *SARReTaskingManager::_findActiveReTask(int vehicleId) const
{
    for (int i = 0; i < _activeReTasks->count(); i++) {
        auto *entry = qobject_cast<ReTaskEntry *>(_activeReTasks->get(i));
        if (entry && entry->vehicleId() == vehicleId) return entry;
    }
    return nullptr;
}

ReTaskEntry *SARReTaskingManager::_findActiveReTaskForTarget(int targetId) const
{
    for (int i = 0; i < _activeReTasks->count(); i++) {
        auto *entry = qobject_cast<ReTaskEntry *>(_activeReTasks->get(i));
        if (entry && entry->targetId() == targetId) return entry;
    }
    return nullptr;
}

void SARReTaskingManager::_cleanupPending()
{
    if (_pendingReTask) {
        _autoConfirmTimer->stop();
        _pendingReTask->deleteLater();
        _pendingReTask = nullptr;
        emit pendingReTaskChanged();
    }
}

// ── Confirmation pass (handoff protocol) ──

void SARReTaskingManager::requestConfirmationPass(int targetId, int excludeVehicleId)
{
    if (!_enabled) return;

    // Can't create a confirmation pass if a proposal is already pending
    if (_pendingReTask) {
        qCDebug(SARReTaskingManagerLog) << "Cannot request confirmation pass — pending re-task exists";
        return;
    }

    // Get the target
    SARTarget *target = _targetMgr ? _targetMgr->getTarget(targetId) : nullptr;
    if (!target) {
        qCWarning(SARReTaskingManagerLog) << "Confirmation pass: target" << targetId << "not found";
        return;
    }

    QGeoCoordinate coord = target->coordinate();
    int priority = static_cast<int>(target->priority());
    QString desc = target->description().isEmpty()
        ? QStringLiteral("Confirm T%1").arg(targetId)
        : QStringLiteral("Confirm: %1").arg(target->description());

    // Select a drone (excluding the one that made the original detection)
    int selectedVehicle = _selectBestDrone(coord, excludeVehicleId, priority);
    if (selectedVehicle < 0) {
        qCWarning(SARReTaskingManagerLog) << "No eligible drone for confirmation pass on target" << targetId;
        return;
    }

    // Find the drone's current zone
    SARZone *zone = _zoneMgr ? _zoneMgr->zoneForVehicle(selectedVehicle) : nullptr;
    int originalZoneId = zone ? zone->zoneId() : -1;

    // Create proposed re-task entry with ConfirmationPass type
    _pendingReTask = new ReTaskEntry(
        selectedVehicle, targetId, originalZoneId,
        coord, priority, desc,
        ReTaskEntry::ConfirmationPass,
        this
    );
    _pendingReTask->setCountdownSec(_autoConfirmTimeoutSec);

    emit pendingReTaskChanged();
    emit reTaskProposed(selectedVehicle, targetId, _autoConfirmTimeoutSec);
    emit confirmationPassProposed(selectedVehicle, targetId);

    // Start the auto-confirm countdown
    _autoConfirmTimer->start();

    qCDebug(SARReTaskingManagerLog) << "Confirmation pass proposed: vehicle" << selectedVehicle
        << "→ target" << targetId << "(zone" << originalZoneId << "), auto-confirm in"
        << _autoConfirmTimeoutSec << "sec";
}

// ── Serialization ──

QJsonObject SARReTaskingManager::settingsToJson() const
{
    QJsonObject json;
    json["enabled"]                 = _enabled;
    json["selectionStrategy"]       = static_cast<int>(_selectionStrategy);
    json["minimumPriority"]         = _minimumPriority;
    json["autoConfirmTimeoutSec"]   = _autoConfirmTimeoutSec;
    return json;
}

void SARReTaskingManager::settingsFromJson(const QJsonObject &json)
{
    _enabled = json["enabled"].toBool(true);
    _selectionStrategy = static_cast<SelectionStrategy>(json["selectionStrategy"].toInt(0));
    _minimumPriority = json["minimumPriority"].toInt(1);
    _autoConfirmTimeoutSec = json["autoConfirmTimeoutSec"].toInt(10);

    emit settingsChanged();
}
