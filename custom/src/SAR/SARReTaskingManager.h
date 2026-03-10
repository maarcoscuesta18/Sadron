#pragma once

#include <QtCore/QObject>
#include <QtCore/QLoggingCategory>
#include <QtCore/QTimer>
#include <QtCore/QJsonObject>
#include <QtPositioning/QGeoCoordinate>
#include <QtQmlIntegration/QtQmlIntegration>

#include "QmlObjectListModel.h"

Q_DECLARE_LOGGING_CATEGORY(SARReTaskingManagerLog)

class SARMissionManager;
class SARZoneManager;
class SARTargetManager;
class SARTarget;
class SARZone;
class Vehicle;

// ============================================================================
// Represents one active or proposed drone re-tasking event
// ============================================================================

class ReTaskEntry : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")

    Q_PROPERTY(int          vehicleId       READ vehicleId      CONSTANT)
    Q_PROPERTY(int          targetId        READ targetId       CONSTANT)
    Q_PROPERTY(int          originalZoneId  READ originalZoneId CONSTANT)
    Q_PROPERTY(ReTaskState  state           READ state          NOTIFY stateChanged)
    Q_PROPERTY(HandoffType  handoffType     READ handoffType    CONSTANT)
    Q_PROPERTY(int          countdownSec    READ countdownSec   NOTIFY countdownChanged)
    Q_PROPERTY(QGeoCoordinate targetCoord   READ targetCoord    CONSTANT)
    Q_PROPERTY(int          targetPriority  READ targetPriority CONSTANT)
    Q_PROPERTY(QString      targetDescription READ targetDescription CONSTANT)

public:
    enum ReTaskState {
        Proposed = 0,       // Awaiting operator confirmation / auto-confirm timer
        Active,             // Drone is investigating the target
        Completed,          // Investigation finished — drone returning to zone
        Cancelled           // Operator cancelled the proposed re-task
    };
    Q_ENUM(ReTaskState)

    enum HandoffType {
        Investigation = 0,      // Standard re-task: drone goes to investigate a target
        ConfirmationPass,       // Secondary pass: another drone confirms a detection
    };
    Q_ENUM(HandoffType)

    explicit ReTaskEntry(int vehicleId, int targetId, int originalZoneId,
                         const QGeoCoordinate &targetCoord, int targetPriority,
                         const QString &targetDescription,
                         HandoffType handoffType = Investigation,
                         QObject *parent = nullptr);

    int             vehicleId() const       { return _vehicleId; }
    int             targetId() const        { return _targetId; }
    int             originalZoneId() const  { return _originalZoneId; }
    ReTaskState     state() const           { return _state; }
    HandoffType     handoffType() const     { return _handoffType; }
    int             countdownSec() const    { return _countdownSec; }
    QGeoCoordinate  targetCoord() const     { return _targetCoord; }
    int             targetPriority() const  { return _targetPriority; }
    QString         targetDescription() const { return _targetDescription; }

    void setState(ReTaskState state);
    void setCountdownSec(int sec);

signals:
    void stateChanged();
    void countdownChanged();

private:
    int             _vehicleId;
    int             _targetId;
    int             _originalZoneId;
    ReTaskState     _state = Proposed;
    HandoffType     _handoffType = Investigation;
    int             _countdownSec = 0;
    QGeoCoordinate  _targetCoord;
    int             _targetPriority;
    QString         _targetDescription;
};

// ============================================================================
// Manages dynamic re-tasking: reassign drones mid-mission when a sector
// yields a detection signal (target added)
// ============================================================================

class SARReTaskingManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")

    // ── Settings ──
    Q_PROPERTY(bool                 enabled             READ enabled            WRITE setEnabled            NOTIFY settingsChanged)
    Q_PROPERTY(SelectionStrategy    selectionStrategy   READ selectionStrategy  WRITE setSelectionStrategy  NOTIFY settingsChanged)
    Q_PROPERTY(int                  minimumPriority     READ minimumPriority    WRITE setMinimumPriority    NOTIFY settingsChanged)
    Q_PROPERTY(int                  autoConfirmTimeoutSec READ autoConfirmTimeoutSec WRITE setAutoConfirmTimeoutSec NOTIFY settingsChanged)

    // ── Runtime state ──
    Q_PROPERTY(ReTaskEntry*         pendingReTask       READ pendingReTask      NOTIFY pendingReTaskChanged)
    Q_PROPERTY(QmlObjectListModel*  activeReTasks       READ activeReTasks      CONSTANT)
    Q_PROPERTY(int                  activeReTaskCount   READ activeReTaskCount  NOTIFY activeReTasksChanged)

public:
    enum SelectionStrategy {
        NearestAvailable = 0,   // Pick closest eligible drone by distance
        PriorityWeighted,       // Score based on distance + battery + zone progress
    };
    Q_ENUM(SelectionStrategy)

    explicit SARReTaskingManager(SARMissionManager *missionMgr,
                                 SARZoneManager *zoneMgr,
                                 SARTargetManager *targetMgr,
                                 QObject *parent = nullptr);
    ~SARReTaskingManager();

    // ── Settings accessors ──
    bool                enabled() const             { return _enabled; }
    SelectionStrategy   selectionStrategy() const   { return _selectionStrategy; }
    int                 minimumPriority() const     { return _minimumPriority; }
    int                 autoConfirmTimeoutSec() const { return _autoConfirmTimeoutSec; }

    void setEnabled(bool enabled);
    void setSelectionStrategy(SelectionStrategy strategy);
    void setMinimumPriority(int priority);
    void setAutoConfirmTimeoutSec(int sec);

    // ── Runtime state accessors ──
    ReTaskEntry*        pendingReTask() const        { return _pendingReTask; }
    QmlObjectListModel* activeReTasks() const        { return _activeReTasks; }
    int                 activeReTaskCount() const;

    // ── Operator actions (Q_INVOKABLE for QML) ──
    Q_INVOKABLE void confirmReTask();                       // Accept proposed re-task now
    Q_INVOKABLE void cancelReTask();                        // Reject proposed re-task
    Q_INVOKABLE void modifyReTask(int newVehicleId);        // Change which drone is selected
    Q_INVOKABLE void completeReTask(int vehicleId);         // End investigation, resume zone
    Q_INVOKABLE void abandonReTask(int vehicleId);          // Cancel active re-task, restore zone status
    Q_INVOKABLE void completeAllReTasks();                  // End all active investigations

    // ── Confirmation pass (handoff protocol) ──
    Q_INVOKABLE void requestConfirmationPass(int targetId, int excludeVehicleId);

    // ── Serialization ──
    QJsonObject settingsToJson() const;
    void settingsFromJson(const QJsonObject &json);

signals:
    // Settings
    void settingsChanged();

    // Lifecycle
    void reTaskProposed(int vehicleId, int targetId, int timeoutSec);
    void reTaskConfirmed(int vehicleId, int targetId);
    void reTaskCancelled(int targetId);
    void reTaskCompleted(int vehicleId, int originalZoneId);
    void confirmationPassProposed(int vehicleId, int targetId);  // Handoff-specific signal

    // State
    void pendingReTaskChanged();
    void activeReTasksChanged();

private slots:
    void _onTargetAdded(int targetId, const QGeoCoordinate &coordinate);
    void _onTargetStatusChanged(int targetId, int newStatus);
    void _onAutoConfirmTick();

private:
    // Drone selection algorithms
    int _selectNearestAvailable(const QGeoCoordinate &targetCoord, int excludeVehicleId) const;
    int _selectPriorityWeighted(const QGeoCoordinate &targetCoord, int excludeVehicleId, int targetPriority) const;
    int _selectBestDrone(const QGeoCoordinate &targetCoord, int excludeVehicleId, int targetPriority) const;

    // Helpers
    bool _isVehicleEligible(int vehicleId) const;
    bool _isVehicleInvestigating(int vehicleId) const;
    void _activateReTask(ReTaskEntry *entry);
    void _cleanupPending();
    ReTaskEntry *_findActiveReTask(int vehicleId) const;
    ReTaskEntry *_findActiveReTaskForTarget(int targetId) const;

    // Sub-system references
    SARMissionManager   *_missionMgr = nullptr;
    SARZoneManager      *_zoneMgr = nullptr;
    SARTargetManager    *_targetMgr = nullptr;

    // Settings
    bool                _enabled = true;
    SelectionStrategy   _selectionStrategy = NearestAvailable;
    int                 _minimumPriority = 1;           // Default: Medium and above
    int                 _autoConfirmTimeoutSec = 10;    // Seconds before auto-confirm

    // Runtime state
    ReTaskEntry         *_pendingReTask = nullptr;      // At most one pending proposal
    QmlObjectListModel  *_activeReTasks = nullptr;      // Currently active investigations
    QTimer              *_autoConfirmTimer = nullptr;    // Countdown timer for pending proposal
};
