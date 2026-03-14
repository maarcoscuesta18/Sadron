#pragma once

#include <QtCore/QObject>
#include <QtCore/QHash>
#include <QtCore/QSet>
#include <QtCore/QTimer>
#include <QtCore/QLoggingCategory>
#include <QtPositioning/QGeoCoordinate>
#include <QtQmlIntegration/QtQmlIntegration>

#include "MultiVehicleManager.h"

class PlanManager;

Q_DECLARE_LOGGING_CATEGORY(SARMissionManagerLog)

class SARZoneManager;
class SARCoverageTracker;
class SARTargetManager;
class SARReTaskingManager;
class SARZone;
class Vehicle;

// Coordinates SAR missions across multiple vehicles
// Generates search patterns and distributes them to drones
class SARMissionManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")

    Q_PROPERTY(SearchPattern    currentPattern  READ currentPattern WRITE setCurrentPattern NOTIFY patternChanged)
    Q_PROPERTY(double           searchAltitude  READ searchAltitude WRITE setSearchAltitude NOTIFY altitudeChanged)
    Q_PROPERTY(double           searchSpeed     READ searchSpeed    WRITE setSearchSpeed    NOTIFY speedChanged)
    Q_PROPERTY(double           trackSpacing    READ trackSpacing   WRITE setTrackSpacing   NOTIFY spacingChanged)
    Q_PROPERTY(bool             missionActive   READ missionActive  NOTIFY missionActiveChanged)
    Q_PROPERTY(bool             paused          READ paused         NOTIFY pausedChanged)
    Q_PROPERTY(OperationPhase   phase           READ phase          NOTIFY phaseChanged)
    Q_PROPERTY(bool             showFlightPaths READ showFlightPaths WRITE setShowFlightPaths NOTIFY showFlightPathsChanged)

    // Abort state properties
    Q_PROPERTY(bool             abortInProgress READ abortInProgress NOTIFY abortProgressChanged)
    Q_PROPERTY(AbortPhase       abortPhase      READ abortPhase      NOTIFY abortProgressChanged)
    Q_PROPERTY(QString          abortStatusText READ abortStatusText NOTIFY abortProgressChanged)

    // Drone readiness properties
    Q_PROPERTY(int     connectedVehicleCount READ connectedVehicleCount NOTIFY readinessChanged)
    Q_PROPERTY(int     armedVehicleCount     READ armedVehicleCount     NOTIFY readinessChanged)
    Q_PROPERTY(bool    hasConnectedVehicles  READ hasConnectedVehicles  NOTIFY readinessChanged)
    Q_PROPERTY(bool    canStartMission       READ canStartMission       NOTIFY readinessChanged)
    Q_PROPERTY(QString startBlockedReason    READ startBlockedReason    NOTIFY readinessChanged)

public:
    enum SearchPattern {
        ParallelTrack = 0,      // Standard parallel lines (maps to Survey)
        CreepingLine,           // Parallel lines with offset start (maps to Survey with angle)
        ExpandingSquare,        // Spiral out from POI
        SectorSearch,           // Pie-slice sectors from datum point
    };
    Q_ENUM(SearchPattern)

    enum OperationPhase {
        Planning = 0,           // Setting up zones and parameters
        Briefing,               // Review before launch
        Deploying,              // Uploading missions, arming
        Searching,              // Active search in progress
        Investigating,          // Target found, investigating
        Recovery,               // Recovering/RTL
        Debriefing,             // Post-operation review
    };
    Q_ENUM(OperationPhase)

    enum AbortPhase {
        AbortIdle = 0,
        LandingVehicles,
        DisarmingVehicles,
        ClearingMissions,
        ResettingState,
    };
    Q_ENUM(AbortPhase)

    explicit SARMissionManager(SARZoneManager *zoneManager, SARCoverageTracker *coverageTracker, QObject *parent = nullptr);
    ~SARMissionManager();

    SearchPattern currentPattern() const { return _currentPattern; }
    double searchAltitude() const { return _searchAltitude; }
    double searchSpeed() const { return _searchSpeed; }
    double trackSpacing() const { return _trackSpacing; }
    bool missionActive() const { return _missionActive; }
    bool paused() const { return _paused; }
    OperationPhase phase() const { return _phase; }
    bool showFlightPaths() const { return _showFlightPaths; }
    bool abortInProgress() const { return _abortPhase != AbortIdle; }
    AbortPhase abortPhase() const { return _abortPhase; }
    QString abortStatusText() const;

    // Manager injection (called after construction when all subsystems exist)
    void setTargetManager(SARTargetManager *mgr);
    void setReTaskingManager(SARReTaskingManager *mgr);

    // Readiness getters
    int     connectedVehicleCount() const;
    int     armedVehicleCount() const;
    bool    hasConnectedVehicles() const;
    bool    canStartMission() const;
    QString startBlockedReason() const;

    Q_INVOKABLE bool isVehicleConnected(int vehicleId) const;

    void setCurrentPattern(SearchPattern pattern);
    void setSearchAltitude(double altitude);
    void setSearchSpeed(double speed);
    void setTrackSpacing(double spacing);
    void setShowFlightPaths(bool show);

    // Mission lifecycle
    Q_INVOKABLE void startOperation();
    Q_INVOKABLE void pauseOperation();
    Q_INVOKABLE void resumeOperation();
    Q_INVOKABLE void abortOperation();
    Q_INVOKABLE void advancePhase();

    // Generate missions for all assigned zones
    Q_INVOKABLE void generateMissions();

    // Generate waypoints for a specific search pattern within a polygon
    Q_INVOKABLE QVariantList generatePatternWaypoints(const QVariantList &polygon, int pattern = -1) const;

    // Per-zone transect generation (resolves per-zone vs global params)
    Q_INVOKABLE QVariantList generateZoneTransect(int zoneId);
    Q_INVOKABLE void generateAllTransects();
    Q_INVOKABLE void clearAllTransects();

    // Auto-assign zones to available vehicles
    Q_INVOKABLE void autoAssignZones();

    // Handle vehicle RTL (redistribute its remaining zone)
    Q_INVOKABLE void handleVehicleRTL(int vehicleId);

signals:
    void patternChanged();
    void altitudeChanged();
    void speedChanged();
    void spacingChanged();
    void missionActiveChanged();
    void pausedChanged();
    void phaseChanged();
    void showFlightPathsChanged();
    void missionGenerated(int vehicleId);
    void operationStarted();
    void operationCompleted();
    void transectsGenerated();
    void readinessChanged();

    // Abort signals
    void abortProgressChanged();
    void abortCompleted();

    // Deployment signals
    void missionUploadProgress(int vehicleId, double pct);
    void missionUploadComplete(int vehicleId, bool success);
    void deploymentComplete();
    void deploymentFailed(const QString &reason);

private:
    QVariantList _generateParallelTrack(const QVariantList &polygon, double spacing, double altitude) const;
    QVariantList _generateCreepingLine(const QVariantList &polygon, double spacing, double altitude) const;
    QVariantList _generateExpandingSquare(const QGeoCoordinate &center, double sideLength, double spacing, double altitude) const;
    QVariantList _generateSectorSearch(const QVariantList &polygon, double altitude) const;

    void _onSearchParamsChanged();
    void _onVehicleAdded(Vehicle *vehicle);
    void _onVehicleRemoved(Vehicle *vehicle);
    void _updateReadiness();

    // Mission upload helpers
    void _uploadMissionToVehicle(Vehicle *vehicle, SARZone *zone);
    void _onUploadComplete(bool error);
    void _startAllUploadedVehicles();
    void _configureRCLossExemption(Vehicle *v);

    // Mission completion monitoring
    void _onVehicleFlightModeChanged(const QString &flightMode);
    void _disconnectMissionCompleteSignals();
    void _checkAllMissionsComplete();

    // Abort state machine
    void _abortPhase_landVehicles();
    void _abortPhase_disarmVehicles();
    void _abortPhase_clearMissions();
    void _abortPhase_resetState();
    void _advanceAbortPhase();
    void _onVehicleLandedForAbort();
    void _onVehicleDisarmedForAbort();
    void _abortSafetyTimeout();

    // Recovery (normal completion) land & disarm sequence
    void _recoveryPhase_landVehicles();
    void _recoveryPhase_disarmVehicles();
    void _onVehicleLandedForRecovery();
    void _onVehicleDisarmedForRecovery();
    void _recoveryComplete();
    void _recoverySafetyTimeout();

    SARZoneManager      *_zoneManager = nullptr;
    SARCoverageTracker  *_coverageTracker = nullptr;
    SARTargetManager    *_targetManager = nullptr;
    SARReTaskingManager *_reTaskingManager = nullptr;

    SearchPattern   _currentPattern = ParallelTrack;
    double          _searchAltitude = 30.0;     // meters AGL
    double          _searchSpeed = 5.0;         // m/s
    double          _trackSpacing = 20.0;       // meters between parallel tracks
    bool            _missionActive = false;
    bool            _paused = false;
    OperationPhase  _phase = Planning;
    bool            _showFlightPaths = true;

    // Mission upload tracking: vehicleId → upload completed
    QHash<int, bool>    _pendingUploads;
    int                 _uploadSuccessCount = 0;
    int                 _uploadFailCount = 0;

    // Abort state machine
    AbortPhase          _abortPhase = AbortIdle;
    QSet<int>           _abortPendingVehicles;
    QTimer             *_abortSafetyTimer = nullptr;

    // Recovery (normal completion) land & disarm tracking
    QSet<int>           _recoveryPendingVehicles;
    QTimer             *_recoverySafetyTimer = nullptr;

    // Mission-completion tracking: vehicleIds whose mission is still in progress
    QSet<int>           _missionInProgressVehicles;
};
