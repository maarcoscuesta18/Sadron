#include "VehicleCoordinator.h"
#include "SARGeoUtils.h"
#include "SARMissionManager.h"
#include "SARZoneManager.h"
#include "SARTargetManager.h"
#include "SARReTaskingManager.h"
#include "SARCoverageTracker.h"
#include "MeshNetworkManager.h"
#include "QGCLoggingCategory.h"
#include "MultiVehicleManager.h"
#include "Vehicle.h"
#include "VehicleSupports.h"
#include "ParameterManager.h"

#include <cmath>
#include <limits>

QGC_LOGGING_CATEGORY(VehicleCoordinatorLog, "Sadron.VehicleCoordinator")

// ============================================================================
// ProximityConflict
// ============================================================================

ProximityConflict::ProximityConflict(int vehicleIdA, int vehicleIdB, QObject *parent)
    : QObject(parent)
    , _vehicleIdA(vehicleIdA)
    , _vehicleIdB(vehicleIdB)
{
}

void ProximityConflict::update(double hDist, double vDist, Severity severity, const QString &resolution)
{
    bool changed = false;
    if (!qFuzzyCompare(_horizontalDistM, hDist)) { _horizontalDistM = hDist; changed = true; }
    if (!qFuzzyCompare(_verticalDistM, vDist))   { _verticalDistM = vDist; changed = true; }
    if (_severity != severity)                    { _severity = severity; changed = true; }
    if (_resolution != resolution)                { _resolution = resolution; changed = true; }
    if (changed) emit updated();
}

bool ProximityConflict::matches(int idA, int idB) const
{
    return (_vehicleIdA == idA && _vehicleIdB == idB) ||
           (_vehicleIdA == idB && _vehicleIdB == idA);
}

// ============================================================================
// ZoneOverlap
// ============================================================================

ZoneOverlap::ZoneOverlap(int zoneIdA, int zoneIdB, double overlapPercent, QObject *parent)
    : QObject(parent)
    , _zoneIdA(zoneIdA)
    , _zoneIdB(zoneIdB)
    , _overlapPercent(overlapPercent)
{
}

void ZoneOverlap::setOverlapPercent(double percent)
{
    if (!qFuzzyCompare(_overlapPercent, percent)) {
        _overlapPercent = percent;
        emit updated();
    }
}

// ============================================================================
// CommsLossEvent
// ============================================================================

CommsLossEvent::CommsLossEvent(int vehicleId, int zoneId, const QGeoCoordinate &lastPos, QObject *parent)
    : QObject(parent)
    , _vehicleId(vehicleId)
    , _zoneId(zoneId)
    , _lastKnownPosition(lastPos)
{
}

void CommsLossEvent::setState(State state)
{
    if (_state != state) {
        _state = state;
        emit stateChanged();
    }
}

void CommsLossEvent::setElapsedSec(int sec)
{
    if (_elapsedSec != sec) {
        _elapsedSec = sec;
        emit elapsedChanged();
    }
}

// ============================================================================
// VehicleCoordinator
// ============================================================================

VehicleCoordinator::VehicleCoordinator(SARMissionManager   *missionMgr,
                                       SARZoneManager      *zoneMgr,
                                       SARTargetManager    *targetMgr,
                                       SARReTaskingManager *reTaskMgr,
                                       SARCoverageTracker  *coverageMgr,
                                       MeshNetworkManager  *meshMgr,
                                       QObject *parent)
    : QObject(parent)
    , _missionMgr(missionMgr)
    , _zoneMgr(zoneMgr)
    , _targetMgr(targetMgr)
    , _reTaskMgr(reTaskMgr)
    , _coverageMgr(coverageMgr)
    , _meshMgr(meshMgr)
    , _tickTimer(new QTimer(this))
    , _overlaps(new QmlObjectListModel(this))
    , _conflicts(new QmlObjectListModel(this))
    , _commsLossEvents(new QmlObjectListModel(this))
{
    // Wire periodic coordination tick
    connect(_tickTimer, &QTimer::timeout, this, &VehicleCoordinator::_tick);
    _tickTimer->start(kTickIntervalMs);

    // React to zone changes for deconfliction
    if (_zoneMgr) {
        connect(_zoneMgr, &SARZoneManager::zonesChanged, this, &VehicleCoordinator::_onZonesChanged);
        connect(_zoneMgr, &SARZoneManager::zoneAssigned, this, [this](int, int) { _checkZoneOverlaps(); });
    }

    // React to mesh node loss/recovery for comms monitoring
    if (_meshMgr) {
        connect(_meshMgr, &MeshNetworkManager::nodeLost,  this, &VehicleCoordinator::_onNodeLost);
        connect(_meshMgr, &MeshNetworkManager::nodeAdded,  this, &VehicleCoordinator::_onNodeAdded);
    }

    qCDebug(VehicleCoordinatorLog) << "VehicleCoordinator initialized";
}

VehicleCoordinator::~VehicleCoordinator()
{
    // 4B: Stop timer FIRST — prevents callbacks while disconnecting
    if (_tickTimer) {
        _tickTimer->stop();
    }

    // Disconnect all cross-subsystem signals to prevent stale callbacks
    if (_zoneMgr) disconnect(_zoneMgr, nullptr, this, nullptr);
    if (_meshMgr) disconnect(_meshMgr, nullptr, this, nullptr);
}

// ============================================================================
// Master enable
// ============================================================================

void VehicleCoordinator::setEnabled(bool enabled)
{
    if (_enabled != enabled) {
        _enabled = enabled;
        if (_enabled) {
            _tickTimer->start(kTickIntervalMs);
        } else {
            _tickTimer->stop();
        }
        emit settingsChanged();
        qCDebug(VehicleCoordinatorLog) << "Coordination" << (_enabled ? "enabled" : "disabled");
    }
}

// ============================================================================
// Sector Deconfliction — settings
// ============================================================================

void VehicleCoordinator::setDeconflictionEnabled(bool enabled)
{
    if (_deconflictionEnabled != enabled) {
        _deconflictionEnabled = enabled;
        if (_deconflictionEnabled) {
            _checkZoneOverlaps();
        }
        emit settingsChanged();
    }
}

bool VehicleCoordinator::hasOverlap() const
{
    return _overlaps->count() > 0;
}

int VehicleCoordinator::overlapCount() const
{
    return _overlaps->count();
}

bool VehicleCoordinator::validateZoneOverlaps()
{
    _zoneOverlapsDirty = true;  // Force recompute on explicit validation
    _checkZoneOverlaps();
    return !hasOverlap();
}

bool VehicleCoordinator::isVehicleInAssignedZone(int vehicleId)
{
    if (!_zoneMgr || !_meshMgr) return true;  // Assume OK if we can't check

    SARZone *zone = _zoneMgr->zoneForVehicle(vehicleId);
    if (!zone) return true;  // No zone assigned, can't violate

    MeshNode *node = _meshMgr->getNode(vehicleId);
    if (!node || !node->position().isValid()) return true;  // No position data

    return _isPointInPolygon(node->position(), zone->polygon());
}

// ============================================================================
// Altitude Separation — settings
// ============================================================================

void VehicleCoordinator::setAltitudeSeparationEnabled(bool enabled)
{
    if (_altitudeSeparationEnabled != enabled) {
        _altitudeSeparationEnabled = enabled;
        emit settingsChanged();
    }
}

void VehicleCoordinator::setSafetyBubbleHorizontalM(double meters)
{
    meters = qBound(kMinSafetyBubbleH, meters, kMaxSafetyBubbleH);
    if (!qFuzzyCompare(_safetyBubbleHorizontalM, meters)) {
        _safetyBubbleHorizontalM = meters;
        emit settingsChanged();
    }
}

void VehicleCoordinator::setSafetyBubbleVerticalM(double meters)
{
    meters = qBound(kMinSafetyBubbleV, meters, kMaxSafetyBubbleV);
    if (!qFuzzyCompare(_safetyBubbleVerticalM, meters)) {
        _safetyBubbleVerticalM = meters;
        emit settingsChanged();
    }
}

int VehicleCoordinator::activeConflictCount() const
{
    return _conflicts->count();
}

// ============================================================================
// Loss of Comms — settings
// ============================================================================

void VehicleCoordinator::setCommsMonitorEnabled(bool enabled)
{
    if (_commsMonitorEnabled != enabled) {
        _commsMonitorEnabled = enabled;
        emit settingsChanged();
    }
}

void VehicleCoordinator::setCommsLossTimeoutSec(int seconds)
{
    seconds = qBound(kMinCommsTimeout, seconds, kMaxCommsTimeout);
    if (_commsLossTimeoutSec != seconds) {
        _commsLossTimeoutSec = seconds;
        emit settingsChanged();
        qCDebug(VehicleCoordinatorLog) << "Comms loss timeout set to" << seconds << "seconds";
    }
}

int VehicleCoordinator::dronesInCommsLoss() const
{
    int count = 0;
    for (int i = 0; i < _commsLossEvents->count(); i++) {
        auto *evt = qobject_cast<CommsLossEvent *>(_commsLossEvents->get(i));
        if (evt && (evt->state() == CommsLossEvent::Detected || evt->state() == CommsLossEvent::RTLTriggered)) {
            count++;
        }
    }
    return count;
}

// ============================================================================
// Handoff — settings
// ============================================================================

void VehicleCoordinator::setHandoffDeconflictionEnabled(bool enabled)
{
    if (_handoffDeconflictionEnabled != enabled) {
        _handoffDeconflictionEnabled = enabled;
        emit settingsChanged();
    }
}

// ============================================================================
// TICK — main coordination loop (every 500ms)
// ============================================================================

void VehicleCoordinator::_tick()
{
    if (!_enabled) return;

    // 4B: Guard against stale subsystem pointers
    if (!_zoneMgr || !_meshMgr) return;

    // 2C: Stagger checks across phases to spread CPU load
    // Phase 0: boundary violations, Phase 1: proximity, Phase 2: comms
    switch (_tickPhase) {
    case 0:
        if (_deconflictionEnabled) {
            _checkBoundaryViolations();
        }
        break;
    case 1:
        if (_altitudeSeparationEnabled) {
            _checkProximityConflicts();
        }
        break;
    case 2:
        if (_commsMonitorEnabled) {
            _checkCommsStatus();
        }
        break;
    }

    _tickPhase = (_tickPhase + 1) % kTickPhases;
}

// ============================================================================
// Sector Deconfliction — zone overlap detection
// ============================================================================

void VehicleCoordinator::_onZonesChanged()
{
    // 2D: Mark overlaps dirty; actual recompute happens on next explicit call
    _zoneOverlapsDirty = true;
    if (_deconflictionEnabled) {
        _checkZoneOverlaps();
    }
}

void VehicleCoordinator::_checkZoneOverlaps()
{
    if (!_zoneMgr) return;

    // 2D: Skip if zones haven't changed since last computation
    if (!_zoneOverlapsDirty) return;
    _zoneOverlapsDirty = false;

    // 2A: Clear existing overlaps efficiently (reverse iterate avoids O(n²) shifts)
    for (int i = _overlaps->count() - 1; i >= 0; --i) {
        auto *obj = _overlaps->get(i);
        _overlaps->removeAt(i);
        obj->deleteLater();
    }

    QmlObjectListModel *zones = _zoneMgr->zones();
    if (!zones) return;

    // Pairwise comparison of all zone polygons
    for (int i = 0; i < zones->count(); i++) {
        auto *zoneA = qobject_cast<SARZone *>(zones->get(i));
        if (!zoneA) continue;

        for (int j = i + 1; j < zones->count(); j++) {
            auto *zoneB = qobject_cast<SARZone *>(zones->get(j));
            if (!zoneB) continue;

            QVariantList polyA = zoneA->polygon();
            QVariantList polyB = zoneB->polygon();

            if (polyA.size() < 3 || polyB.size() < 3) continue;

            if (_polygonsOverlap(polyA, polyB)) {
                double overlapPct = _estimateOverlapPercent(polyA, polyB);

                // Skip near-zero overlap (zones sharing edges/vertices but not actually overlapping)
                if (overlapPct < 1.0) continue;

                auto *overlap = new ZoneOverlap(zoneA->zoneId(), zoneB->zoneId(), overlapPct, this);
                _overlaps->append(overlap);

                qCWarning(VehicleCoordinatorLog) << "Zone overlap detected: Zone"
                    << zoneA->zoneId() << "and Zone" << zoneB->zoneId()
                    << "(" << overlapPct << "% overlap)";

                emit zoneOverlapDetected(zoneA->zoneId(), zoneB->zoneId());
            }
        }
    }

    emit deconflictionChanged();

    if (_overlaps->count() == 0) {
        emit deconflictionClean();
        qCDebug(VehicleCoordinatorLog) << "Deconfliction clean: no zone overlaps";
    }
}

bool VehicleCoordinator::_polygonsOverlap(const QVariantList &polyA, const QVariantList &polyB) const
{
    // Method: vertex-in-polygon test + edge intersection test
    // If any vertex of A is inside B, they overlap
    for (const QVariant &v : polyA) {
        QGeoCoordinate pt = v.value<QGeoCoordinate>();
        if (_isPointInPolygon(pt, polyB)) return true;
    }

    // If any vertex of B is inside A, they overlap
    for (const QVariant &v : polyB) {
        QGeoCoordinate pt = v.value<QGeoCoordinate>();
        if (_isPointInPolygon(pt, polyA)) return true;
    }

    // Check edge intersections (handles cross-shaped overlaps where no vertex is inside)
    int nA = polyA.size();
    int nB = polyB.size();
    for (int i = 0; i < nA; i++) {
        QGeoCoordinate a1 = polyA[i].value<QGeoCoordinate>();
        QGeoCoordinate a2 = polyA[(i + 1) % nA].value<QGeoCoordinate>();
        for (int j = 0; j < nB; j++) {
            QGeoCoordinate b1 = polyB[j].value<QGeoCoordinate>();
            QGeoCoordinate b2 = polyB[(j + 1) % nB].value<QGeoCoordinate>();
            if (_edgesIntersect(a1, a2, b1, b2)) return true;
        }
    }

    return false;
}

// 5A: Delegates to shared utility — single source of truth
bool VehicleCoordinator::_isPointInPolygon(const QGeoCoordinate &point, const QVariantList &polygon) const
{
    return SARGeoUtils::isPointInPolygon(point, polygon);
}

bool VehicleCoordinator::_edgesIntersect(const QGeoCoordinate &a1, const QGeoCoordinate &a2,
                                          const QGeoCoordinate &b1, const QGeoCoordinate &b2) const
{
    // 2D line segment intersection using cross products (in lat/lon space)
    double d1x = a2.longitude() - a1.longitude();
    double d1y = a2.latitude()  - a1.latitude();
    double d2x = b2.longitude() - b1.longitude();
    double d2y = b2.latitude()  - b1.latitude();

    double denom = d1x * d2y - d1y * d2x;
    if (std::abs(denom) < 1e-9) return false;  // Parallel (tolerance for lat/lon degrees)

    double dx = b1.longitude() - a1.longitude();
    double dy = b1.latitude()  - a1.latitude();

    double t = (dx * d2y - dy * d2x) / denom;
    double u = (dx * d1y - dy * d1x) / denom;

    return (t >= 0.0 && t <= 1.0 && u >= 0.0 && u <= 1.0);
}

double VehicleCoordinator::_estimateOverlapPercent(const QVariantList &polyA, const QVariantList &polyB) const
{
    // Estimate overlap by sampling points from polyA's bounding box
    // and counting how many fall inside both polygons
    if (polyA.size() < 3 || polyB.size() < 3) return 0.0;

    // Find bounding box of polyA
    double minLat = 90, maxLat = -90, minLon = 180, maxLon = -180;
    for (const QVariant &v : polyA) {
        QGeoCoordinate c = v.value<QGeoCoordinate>();
        minLat = qMin(minLat, c.latitude());
        maxLat = qMax(maxLat, c.latitude());
        minLon = qMin(minLon, c.longitude());
        maxLon = qMax(maxLon, c.longitude());
    }

    constexpr int kSamples = 20;  // 20x20 grid = 400 sample points
    double latStep = (maxLat - minLat) / kSamples;
    double lonStep = (maxLon - minLon) / kSamples;

    if (latStep <= 0 || lonStep <= 0) return 0.0;

    int inA = 0, inBoth = 0;
    for (int i = 0; i < kSamples; i++) {
        for (int j = 0; j < kSamples; j++) {
            QGeoCoordinate pt(minLat + i * latStep, minLon + j * lonStep);
            if (_isPointInPolygon(pt, polyA)) {
                inA++;
                if (_isPointInPolygon(pt, polyB)) {
                    inBoth++;
                }
            }
        }
    }

    if (inA == 0) return 0.0;
    return (static_cast<double>(inBoth) / inA) * 100.0;
}

// ============================================================================
// Sector Deconfliction — boundary violation monitoring
// ============================================================================

void VehicleCoordinator::_checkBoundaryViolations()
{
    if (!_zoneMgr || !_meshMgr) return;

    QmlObjectListModel *zones = _zoneMgr->zones();
    if (!zones) return;

    int prevCount = _boundaryViolationCount;
    _boundaryViolationCount = 0;

    // For each active zone with an assigned vehicle, check if the vehicle is inside
    for (int i = 0; i < zones->count(); i++) {
        auto *zone = qobject_cast<SARZone *>(zones->get(i));
        if (!zone || zone->assignedVehicle() < 0) continue;
        if (zone->status() != SARZone::Active) continue;

        int vehicleId = zone->assignedVehicle();

        // Skip boundary check for vehicles that aren't flying yet (e.g. during takeoff)
        Vehicle *vehicle = MultiVehicleManager::instance()->getVehicleById(vehicleId);
        if (vehicle && !vehicle->flying()) continue;

        MeshNode *node = _meshMgr->getNode(vehicleId);
        if (!node || !node->position().isValid()) continue;

        QVariantList poly = zone->polygon();
        if (poly.size() < 3) continue;

        bool inside = _isPointInPolygon(node->position(), poly);

        int prevViolation = _vehicleBoundaryViolations.value(vehicleId, 0);

        if (!inside) {
            _boundaryViolationCount++;
            _vehicleBoundaryViolations[vehicleId] = zone->zoneId();

            // Only emit if this is a new violation (not a repeated one)
            if (prevViolation != zone->zoneId()) {
                qCWarning(VehicleCoordinatorLog) << "Boundary violation: Vehicle" << vehicleId
                    << "is outside its assigned Zone" << zone->zoneId();
                emit boundaryViolation(vehicleId, zone->zoneId());
            }
        } else {
            _vehicleBoundaryViolations[vehicleId] = 0;
        }
    }

    if (_boundaryViolationCount != prevCount) {
        emit boundaryViolationsChanged();
    }
}

// ============================================================================
// Altitude Separation — proximity conflict detection
// ============================================================================

void VehicleCoordinator::_checkProximityConflicts()
{
    if (!_meshMgr) return;

    QmlObjectListModel *nodes = _meshMgr->nodes();
    if (!nodes || nodes->count() < 2) return;

    // Track which conflicts are still active this tick
    QSet<QPair<int, int>> activeConflictPairs;

    for (int i = 0; i < nodes->count(); i++) {
        auto *nodeA = qobject_cast<MeshNode *>(nodes->get(i));
        if (!nodeA || !nodeA->position().isValid()) continue;
        if (nodeA->status() == MeshNode::Offline) continue;

        for (int j = i + 1; j < nodes->count(); j++) {
            auto *nodeB = qobject_cast<MeshNode *>(nodes->get(j));
            if (!nodeB || !nodeB->position().isValid()) continue;
            if (nodeB->status() == MeshNode::Offline) continue;

            double hDist = _horizontalDistance(nodeA->position(), nodeB->position());
            double vDist = _verticalDistance(nodeA->position(), nodeB->position());

            if (hDist < _safetyBubbleHorizontalM) {
                int idA = nodeA->vehicleId();
                int idB = nodeB->vehicleId();
                auto pair = qMakePair(qMin(idA, idB), qMax(idA, idB));
                activeConflictPairs.insert(pair);

                ProximityConflict::Severity severity;
                QString resolution;

                if (vDist < _safetyBubbleVerticalM) {
                    // CRITICAL: both horizontal and vertical separation violated
                    severity = ProximityConflict::Critical;

                    // Command the higher-ID drone to climb
                    int climbVehicle = qMax(idA, idB);
                    double neededAlt = _safetyBubbleVerticalM - vDist + 2.0;  // +2m margin

                    resolution = QStringLiteral("V%1 climbing %2m")
                        .arg(climbVehicle)
                        .arg(neededAlt, 0, 'f', 1);

                    // Only issue command if we haven't already adjusted this drone recently
                    double prevAdj = _altitudeAdjustments.value(climbVehicle, 0.0);
                    if (prevAdj < neededAlt) {
                        _commandAltitudeAdjust(climbVehicle, neededAlt);
                        _altitudeAdjustments[climbVehicle] = neededAlt;
                    }
                } else {
                    // WARNING: horizontal proximity but vertically separated
                    severity = ProximityConflict::Warning;
                    resolution = QStringLiteral("Vertically separated (%1m)").arg(vDist, 0, 'f', 1);

                    // Clear altitude adjustment if separation is now adequate
                    _restoreAltitudeAdjust(qMax(idA, idB));
                }

                // Update or create conflict entry
                ProximityConflict *conflict = _findConflict(idA, idB);
                if (conflict) {
                    conflict->update(hDist, vDist, severity, resolution);
                } else {
                    conflict = new ProximityConflict(idA, idB, this);
                    conflict->update(hDist, vDist, severity, resolution);
                    _conflicts->append(conflict);

                    qCWarning(VehicleCoordinatorLog) << "Proximity alert: V" << idA << "and V" << idB
                        << "at" << hDist << "m horizontal," << vDist << "m vertical";
                    emit proximityAlert(idA, idB, hDist);
                }
            }
        }
    }

    // Remove conflicts that are no longer active
    bool changed = false;
    for (int i = _conflicts->count() - 1; i >= 0; i--) {
        auto *conflict = qobject_cast<ProximityConflict *>(_conflicts->get(i));
        if (!conflict) continue;

        auto pair = qMakePair(qMin(conflict->vehicleIdA(), conflict->vehicleIdB()),
                              qMax(conflict->vehicleIdA(), conflict->vehicleIdB()));
        if (!activeConflictPairs.contains(pair)) {
            // Clear altitude adjustments for resolved conflicts
            _restoreAltitudeAdjust(qMax(conflict->vehicleIdA(), conflict->vehicleIdB()));

            _conflicts->removeAt(i);
            conflict->deleteLater();
            changed = true;
        }
    }

    if (changed || !activeConflictPairs.isEmpty()) {
        emit conflictsChanged();
    }
}

double VehicleCoordinator::_horizontalDistance(const QGeoCoordinate &a, const QGeoCoordinate &b) const
{
    // 2D distance ignoring altitude
    QGeoCoordinate a2d(a.latitude(), a.longitude(), 0);
    QGeoCoordinate b2d(b.latitude(), b.longitude(), 0);
    return a2d.distanceTo(b2d);
}

double VehicleCoordinator::_verticalDistance(const QGeoCoordinate &a, const QGeoCoordinate &b) const
{
    return std::abs(a.altitude() - b.altitude());
}

void VehicleCoordinator::_commandAltitudeAdjust(int vehicleId, double deltaAltM)
{
    Vehicle *vehicle = MultiVehicleManager::instance()->getVehicleById(vehicleId);
    if (!vehicle) {
        qCWarning(VehicleCoordinatorLog) << "Cannot adjust altitude: vehicle" << vehicleId << "not found";
        return;
    }

    // Only issue temporary deconfliction changes while the vehicle is already
    // in a guided/hold-style state that accepts altitude nudges safely.
    if (!vehicle->flying()
        || !vehicle->supports()->guidedMode()
        || !vehicle->guidedMode()
        || !vehicle->firmwarePlugin()) {
        qCDebug(VehicleCoordinatorLog) << "Skipping altitude adjust for V" << vehicleId
            << "— vehicle not in a safe guided state";
        return;
    }

    double currentAlt = vehicle->altitudeRelative()->rawValue().toDouble();
    double newAlt = currentAlt + deltaAltM;

    qCInfo(VehicleCoordinatorLog) << "Altitude separation: commanding V" << vehicleId
        << "from" << currentAlt << "m to" << newAlt << "m (+" << deltaAltM << "m)";

    // guidedModeChangeAltitude takes a delta (altitude change), not absolute altitude
    vehicle->guidedModeChangeAltitude(deltaAltM, false /* pauseVehicle */);

    emit altitudeAdjusted(vehicleId, newAlt);
}

void VehicleCoordinator::_restoreAltitudeAdjust(int vehicleId)
{
    const double previousAdjustment = _altitudeAdjustments.value(vehicleId, 0.0);
    if (qFuzzyIsNull(previousAdjustment)) {
        _altitudeAdjustments.remove(vehicleId);
        return;
    }

    Vehicle *vehicle = MultiVehicleManager::instance()->getVehicleById(vehicleId);
    if (!vehicle) {
        _altitudeAdjustments.remove(vehicleId);
        return;
    }

    if (!vehicle->flying()
        || !vehicle->supports()->guidedMode()
        || !vehicle->guidedMode()
        || !vehicle->firmwarePlugin()) {
        _altitudeAdjustments.remove(vehicleId);
        return;
    }

    vehicle->guidedModeChangeAltitude(-previousAdjustment, false /* pauseVehicle */);
    _altitudeAdjustments.remove(vehicleId);

    emit altitudeAdjusted(vehicleId, vehicle->altitudeRelative()->rawValue().toDouble() - previousAdjustment);
}

ProximityConflict *VehicleCoordinator::_findConflict(int idA, int idB) const
{
    for (int i = 0; i < _conflicts->count(); i++) {
        auto *conflict = qobject_cast<ProximityConflict *>(_conflicts->get(i));
        if (conflict && conflict->matches(idA, idB)) return conflict;
    }
    return nullptr;
}

// ============================================================================
// Loss of Comms — monitoring and response
// ============================================================================

void VehicleCoordinator::_checkCommsStatus()
{
    if (!_meshMgr) return;

    QmlObjectListModel *nodes = _meshMgr->nodes();
    if (!nodes) return;

    for (int i = 0; i < nodes->count(); i++) {
        auto *node = qobject_cast<MeshNode *>(nodes->get(i));
        if (!node) continue;

        int vehicleId = node->vehicleId();
        CommsLossEvent *evt = _findCommsLossEvent(vehicleId);

        if (node->status() == MeshNode::Offline || node->status() == MeshNode::Degraded) {
            qint64 elapsedMs = node->msSinceLastSeen();
            int elapsedSec = static_cast<int>(elapsedMs / 1000);

            if (!evt && node->status() == MeshNode::Offline) {
                // New comms loss detected
                _handleCommsLoss(vehicleId);
            } else if (evt && evt->state() == CommsLossEvent::Detected) {
                // Update elapsed time
                evt->setElapsedSec(elapsedSec);

                // Check if we've exceeded the RTL timeout
                if (elapsedSec >= _commsLossTimeoutSec) {
                    evt->setState(CommsLossEvent::RTLTriggered);

                    qCWarning(VehicleCoordinatorLog) << "Comms loss RTL timeout for V" << vehicleId
                        << "after" << elapsedSec << "sec — drone failsafe expected";

                    // Mark zone for reassignment
                    if (_missionMgr) {
                        _missionMgr->handleVehicleRTL(vehicleId);
                    }

                    emit commsLossRTL(vehicleId);
                    emit commsLossChanged();
                }
            }
        } else if (node->status() == MeshNode::Online && evt) {
            // Comms restored
            if (evt->state() == CommsLossEvent::Detected || evt->state() == CommsLossEvent::RTLTriggered) {
                _handleCommsRestored(vehicleId);
            }
        }
    }
}

void VehicleCoordinator::_handleCommsLoss(int vehicleId)
{
    if (_findCommsLossEvent(vehicleId)) return;  // Already tracked

    // Find the drone's zone and last known position
    int zoneId = -1;
    QGeoCoordinate lastPos;

    if (_zoneMgr) {
        SARZone *zone = _zoneMgr->zoneForVehicle(vehicleId);
        if (zone) zoneId = zone->zoneId();
    }

    if (_meshMgr) {
        MeshNode *node = _meshMgr->getNode(vehicleId);
        if (node) lastPos = node->position();
    }

    auto *evt = new CommsLossEvent(vehicleId, zoneId, lastPos, this);
    _commsLossEvents->append(evt);

    qCWarning(VehicleCoordinatorLog) << "Comms loss detected: V" << vehicleId
        << "(Zone" << zoneId << "), timeout in" << _commsLossTimeoutSec << "sec";

    emit commsLossDetected(vehicleId);
    emit commsLossChanged();
}

void VehicleCoordinator::_handleCommsRestored(int vehicleId)
{
    CommsLossEvent *evt = _findCommsLossEvent(vehicleId);
    if (!evt) return;

    qCInfo(VehicleCoordinatorLog) << "Comms restored: V" << vehicleId
        << "after" << evt->elapsedSec() << "sec";

    // If zone was reassigned, don't auto-resume — operator needs to decide
    // If zone was not reassigned, resume the mission
    bool wasReassigned = (evt->state() == CommsLossEvent::ZoneReassigned);

    evt->setState(CommsLossEvent::CommsRestored);

    emit commsRestored(vehicleId);

    if (!wasReassigned && _zoneMgr) {
        SARZone *zone = _zoneMgr->zoneForVehicle(vehicleId);
        if (zone && zone->status() == SARZone::Reassigning) {
            zone->setStatus(SARZone::Active);
            qCInfo(VehicleCoordinatorLog) << "Zone" << zone->zoneId() << "resumed for V" << vehicleId;
        }
    }

    _removeCommsLossEvent(vehicleId);
}

void VehicleCoordinator::_onNodeLost(int vehicleId)
{
    if (_commsMonitorEnabled) {
        _handleCommsLoss(vehicleId);
    }
}

void VehicleCoordinator::_onNodeAdded(int vehicleId)
{
    // Check if this was a node that was in comms loss
    CommsLossEvent *evt = _findCommsLossEvent(vehicleId);
    if (evt && (evt->state() == CommsLossEvent::Detected || evt->state() == CommsLossEvent::RTLTriggered)) {
        _handleCommsRestored(vehicleId);
    }
}

void VehicleCoordinator::_removeCommsLossEvent(int vehicleId)
{
    bool removed = false;
    for (int i = _commsLossEvents->count() - 1; i >= 0; --i) {
        auto *evt = qobject_cast<CommsLossEvent *>(_commsLossEvents->get(i));
        if (evt && evt->vehicleId() == vehicleId) {
            _commsLossEvents->removeAt(i);
            evt->deleteLater();
            removed = true;
        }
    }

    if (removed) {
        emit commsLossChanged();
    }
}

CommsLossEvent *VehicleCoordinator::_findCommsLossEvent(int vehicleId) const
{
    for (int i = 0; i < _commsLossEvents->count(); i++) {
        auto *evt = qobject_cast<CommsLossEvent *>(_commsLossEvents->get(i));
        if (evt && evt->vehicleId() == vehicleId &&
            (evt->state() == CommsLossEvent::Detected || evt->state() == CommsLossEvent::RTLTriggered)) {
            return evt;
        }
    }
    return nullptr;
}

void VehicleCoordinator::reassignLostVehicleZone(int vehicleId)
{
    if (!_zoneMgr || !_missionMgr) return;

    SARZone *zone = _zoneMgr->zoneForVehicle(vehicleId);
    if (!zone) {
        qCWarning(VehicleCoordinatorLog) << "No zone to reassign for V" << vehicleId;
        return;
    }

    // Find the zone center for proximity-based selection
    QVariantList poly = zone->polygon();
    QGeoCoordinate center;
    if (!poly.isEmpty()) {
        double latSum = 0, lonSum = 0;
        for (const QVariant &v : poly) {
            QGeoCoordinate c = v.value<QGeoCoordinate>();
            latSum += c.latitude();
            lonSum += c.longitude();
        }
        center = QGeoCoordinate(latSum / poly.size(), lonSum / poly.size());
    }

    int newVehicleId = _findBestReassignmentTarget(vehicleId, center);
    if (newVehicleId < 0) {
        qCWarning(VehicleCoordinatorLog) << "No eligible drone to reassign Zone" << zone->zoneId();
        return;
    }

    int zoneId = zone->zoneId();
    _zoneMgr->reassignZone(zoneId, newVehicleId);

    // Regenerate transect for the new drone
    _missionMgr->generateZoneTransect(zoneId);

    // Update comms loss event
    CommsLossEvent *evt = _findCommsLossEvent(vehicleId);
    if (evt) {
        evt->setState(CommsLossEvent::ZoneReassigned);
    }

    qCInfo(VehicleCoordinatorLog) << "Zone" << zoneId << "reassigned from V" << vehicleId
        << "to V" << newVehicleId;

    emit zoneReassigned(zoneId, vehicleId, newVehicleId);

    _removeCommsLossEvent(vehicleId);
}

int VehicleCoordinator::_findBestReassignmentTarget(int excludeVehicleId, const QGeoCoordinate &zoneCenter) const
{
    auto *mvm = MultiVehicleManager::instance();
    QmlObjectListModel *vehicles = mvm->vehicles();

    int bestId = -1;
    double bestDist = std::numeric_limits<double>::max();

    for (int i = 0; i < vehicles->count(); i++) {
        auto *v = qobject_cast<Vehicle *>(vehicles->get(i));
        if (!v || v->id() == excludeVehicleId) continue;
        if (!v->flying()) continue;

        // Skip drones already investigating a target
        if (_reTaskMgr) {
            // Check the active re-tasks list
            QmlObjectListModel *reTasks = _reTaskMgr->activeReTasks();
            bool isInvestigating = false;
            for (int j = 0; j < reTasks->count(); j++) {
                auto *entry = qobject_cast<ReTaskEntry *>(reTasks->get(j));
                if (entry && entry->vehicleId() == v->id()) {
                    isInvestigating = true;
                    break;
                }
            }
            if (isInvestigating) continue;
        }

        // Skip drones that are in comms loss themselves
        CommsLossEvent *evt = _findCommsLossEvent(v->id());
        if (evt) continue;

        if (zoneCenter.isValid()) {
            double dist = v->coordinate().distanceTo(zoneCenter);
            if (dist < bestDist) {
                bestDist = dist;
                bestId = v->id();
            }
        } else {
            bestId = v->id();
            break;
        }
    }

    return bestId;
}

// ============================================================================
// Failsafe verification
// ============================================================================

void VehicleCoordinator::verifyFailsafeConfig(int vehicleId)
{
    Vehicle *vehicle = MultiVehicleManager::instance()->getVehicleById(vehicleId);
    if (!vehicle) {
        qCWarning(VehicleCoordinatorLog) << "Cannot verify failsafe: vehicle" << vehicleId << "not found";
        emit failsafeVerified(vehicleId, false);
        return;
    }

    // Check ArduPilot GCS failsafe parameters
    // FS_GCS_ENABLE: 1 = enabled (RTL on GCS loss)
    // The actual parameter read is asynchronous via the Fact system.
    // We check the known parameter values that QGC has already received.
    bool gcsFailsafeOk = false;

    // Try to read FS_GCS_ENABLE parameter
    if (vehicle->parameterManager() && vehicle->parameterManager()->parameterExists(-1, QStringLiteral("FS_GCS_ENABLE"))) {
        Fact *fsGcs = vehicle->parameterManager()->getParameter(-1, QStringLiteral("FS_GCS_ENABLE"));
        if (fsGcs) {
            int value = fsGcs->rawValue().toInt();
            // 0 = disabled, 1 = enabled (RTL), 2 = enabled (continue mission)
            gcsFailsafeOk = (value >= 1);
            qCDebug(VehicleCoordinatorLog) << "V" << vehicleId << "FS_GCS_ENABLE =" << value
                << (gcsFailsafeOk ? "(OK)" : "(DISABLED — needs to be >= 1)");
        }
    } else {
        // PX4 uses COM_DL_LOSS_T (data link loss timeout) and NAV_DLL_ACT (action)
        if (vehicle->parameterManager() && vehicle->parameterManager()->parameterExists(-1, QStringLiteral("NAV_DLL_ACT"))) {
            Fact *navDll = vehicle->parameterManager()->getParameter(-1, QStringLiteral("NAV_DLL_ACT"));
            if (navDll) {
                int value = navDll->rawValue().toInt();
                // 0 = disabled, 1 = loiter, 2 = RTL, 3 = land
                gcsFailsafeOk = (value >= 1);
                qCDebug(VehicleCoordinatorLog) << "V" << vehicleId << "NAV_DLL_ACT =" << value
                    << (gcsFailsafeOk ? "(OK)" : "(DISABLED)");
            }
        } else {
            qCWarning(VehicleCoordinatorLog) << "V" << vehicleId << "— failsafe parameters not available";
        }
    }

    _failsafeStatus[vehicleId] = gcsFailsafeOk;

    // Recalculate overall status
    _allFailsafesVerified = true;
    for (auto it = _failsafeStatus.constBegin(); it != _failsafeStatus.constEnd(); ++it) {
        if (!it.value()) {
            _allFailsafesVerified = false;
            break;
        }
    }

    emit failsafeVerified(vehicleId, gcsFailsafeOk);
    emit failsafeStatusChanged();
}

void VehicleCoordinator::verifyAllFailsafes()
{
    auto *mvm = MultiVehicleManager::instance();
    QmlObjectListModel *vehicles = mvm->vehicles();

    _failsafeStatus.clear();

    for (int i = 0; i < vehicles->count(); i++) {
        auto *v = qobject_cast<Vehicle *>(vehicles->get(i));
        if (v) {
            verifyFailsafeConfig(v->id());
        }
    }
}

// ============================================================================
// Handoff — deconfliction-aware drone selection
// ============================================================================

int VehicleCoordinator::selectHandoffDrone(const QGeoCoordinate &targetCoord, int excludeVehicleId)
{
    if (!_handoffDeconflictionEnabled) {
        // If deconfliction is disabled, fall through to ReTaskingManager's own selection
        return -1;
    }

    auto *mvm = MultiVehicleManager::instance();
    QmlObjectListModel *vehicles = mvm->vehicles();

    int bestId = -1;
    double bestDist = std::numeric_limits<double>::max();

    for (int i = 0; i < vehicles->count(); i++) {
        auto *v = qobject_cast<Vehicle *>(vehicles->get(i));
        if (!v || v->id() == excludeVehicleId) continue;
        if (!v->flying()) continue;

        // Skip drones in comms loss
        if (_findCommsLossEvent(v->id())) continue;

        // Check if the handoff would cause a proximity conflict
        if (_wouldCauseProximityConflict(v->id(), targetCoord)) {
            qCDebug(VehicleCoordinatorLog) << "Handoff: V" << v->id()
                << "skipped — would cause proximity conflict at target location";
            emit handoffBlocked(v->id(), QStringLiteral("Proximity conflict at target location"));
            continue;
        }

        double dist = v->coordinate().distanceTo(targetCoord);
        if (dist < bestDist) {
            bestDist = dist;
            bestId = v->id();
        }
    }

    return bestId;
}

bool VehicleCoordinator::isHandoffSafe(int vehicleId, const QGeoCoordinate &targetCoord)
{
    // Check if this vehicle can safely go to the target without conflicting
    if (_findCommsLossEvent(vehicleId)) return false;
    if (_wouldCauseProximityConflict(vehicleId, targetCoord)) return false;
    return true;
}

bool VehicleCoordinator::_wouldCauseProximityConflict(int vehicleId, const QGeoCoordinate &targetCoord) const
{
    if (!_meshMgr) return false;

    QmlObjectListModel *nodes = _meshMgr->nodes();
    if (!nodes) return false;

    for (int i = 0; i < nodes->count(); i++) {
        auto *node = qobject_cast<MeshNode *>(nodes->get(i));
        if (!node || node->vehicleId() == vehicleId) continue;
        if (!node->position().isValid()) continue;
        if (node->status() == MeshNode::Offline) continue;

        double hDist = _horizontalDistance(targetCoord, node->position());
        if (hDist < _safetyBubbleHorizontalM) {
            return true;
        }
    }

    return false;
}

// ============================================================================
// Serialization
// ============================================================================

QJsonObject VehicleCoordinator::settingsToJson() const
{
    QJsonObject json;
    json[QStringLiteral("enabled")]                     = _enabled;
    json[QStringLiteral("deconflictionEnabled")]        = _deconflictionEnabled;
    json[QStringLiteral("altitudeSeparationEnabled")]   = _altitudeSeparationEnabled;
    json[QStringLiteral("safetyBubbleHorizontalM")]     = _safetyBubbleHorizontalM;
    json[QStringLiteral("safetyBubbleVerticalM")]       = _safetyBubbleVerticalM;
    json[QStringLiteral("commsMonitorEnabled")]         = _commsMonitorEnabled;
    json[QStringLiteral("commsLossTimeoutSec")]         = _commsLossTimeoutSec;
    json[QStringLiteral("handoffDeconflictionEnabled")] = _handoffDeconflictionEnabled;
    return json;
}

void VehicleCoordinator::settingsFromJson(const QJsonObject &json)
{
    _enabled                    = json[QStringLiteral("enabled")].toBool(true);
    _deconflictionEnabled       = json[QStringLiteral("deconflictionEnabled")].toBool(true);
    _altitudeSeparationEnabled  = json[QStringLiteral("altitudeSeparationEnabled")].toBool(true);
    _safetyBubbleHorizontalM   = json[QStringLiteral("safetyBubbleHorizontalM")].toDouble(50.0);
    _safetyBubbleVerticalM     = json[QStringLiteral("safetyBubbleVerticalM")].toDouble(10.0);
    _commsMonitorEnabled        = json[QStringLiteral("commsMonitorEnabled")].toBool(true);
    _commsLossTimeoutSec        = json[QStringLiteral("commsLossTimeoutSec")].toInt(30);
    _handoffDeconflictionEnabled = json[QStringLiteral("handoffDeconflictionEnabled")].toBool(true);

    if (_enabled) {
        _tickTimer->start(kTickIntervalMs);
    }

    emit settingsChanged();
}
