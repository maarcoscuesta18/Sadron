#pragma once

#include <QtCore/QObject>
#include <QtCore/QLoggingCategory>
#include <QtCore/QTimer>
#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtPositioning/QGeoCoordinate>
#include <QtQmlIntegration/QtQmlIntegration>

#include "QmlObjectListModel.h"

Q_DECLARE_LOGGING_CATEGORY(VehicleCoordinatorLog)

class SARMissionManager;
class SARZoneManager;
class SARTargetManager;
class SARReTaskingManager;
class SARCoverageTracker;
class MeshNetworkManager;
class SARZone;
class MeshNode;
class Vehicle;

// ============================================================================
// ProximityConflict — represents a pair of drones that are too close
// ============================================================================

class ProximityConflict : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")

    Q_PROPERTY(int      vehicleIdA      READ vehicleIdA     CONSTANT)
    Q_PROPERTY(int      vehicleIdB      READ vehicleIdB     CONSTANT)
    Q_PROPERTY(double   horizontalDistM READ horizontalDistM NOTIFY updated)
    Q_PROPERTY(double   verticalDistM   READ verticalDistM  NOTIFY updated)
    Q_PROPERTY(Severity severity        READ severity       NOTIFY updated)
    Q_PROPERTY(QString  resolution      READ resolution     NOTIFY updated)

public:
    enum Severity {
        Warning = 0,    // Inside horizontal bubble but vertically separated
        Critical,       // Inside both horizontal AND vertical bubble
    };
    Q_ENUM(Severity)

    explicit ProximityConflict(int vehicleIdA, int vehicleIdB, QObject *parent = nullptr);

    int     vehicleIdA() const      { return _vehicleIdA; }
    int     vehicleIdB() const      { return _vehicleIdB; }
    double  horizontalDistM() const { return _horizontalDistM; }
    double  verticalDistM() const   { return _verticalDistM; }
    Severity severity() const       { return _severity; }
    QString resolution() const      { return _resolution; }

    void update(double hDist, double vDist, Severity severity, const QString &resolution);

    // Check if this conflict involves the given pair (order-independent)
    bool matches(int idA, int idB) const;

signals:
    void updated();

private:
    int     _vehicleIdA;
    int     _vehicleIdB;
    double  _horizontalDistM = 0.0;
    double  _verticalDistM = 0.0;
    Severity _severity = Warning;
    QString _resolution;
};

// ============================================================================
// ZoneOverlap — describes an overlap between two zones
// ============================================================================

class ZoneOverlap : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")

    Q_PROPERTY(int zoneIdA READ zoneIdA CONSTANT)
    Q_PROPERTY(int zoneIdB READ zoneIdB CONSTANT)
    Q_PROPERTY(double overlapPercent READ overlapPercent NOTIFY updated)

public:
    explicit ZoneOverlap(int zoneIdA, int zoneIdB, double overlapPercent, QObject *parent = nullptr);

    int    zoneIdA() const         { return _zoneIdA; }
    int    zoneIdB() const         { return _zoneIdB; }
    double overlapPercent() const  { return _overlapPercent; }

    void setOverlapPercent(double percent);

signals:
    void updated();

private:
    int    _zoneIdA;
    int    _zoneIdB;
    double _overlapPercent;
};

// ============================================================================
// CommsLossEvent — tracks a drone that has lost communication
// ============================================================================

class CommsLossEvent : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")

    Q_PROPERTY(int      vehicleId       READ vehicleId      CONSTANT)
    Q_PROPERTY(int      zoneId          READ zoneId         CONSTANT)
    Q_PROPERTY(State    state           READ state          NOTIFY stateChanged)
    Q_PROPERTY(int      elapsedSec      READ elapsedSec     NOTIFY elapsedChanged)
    Q_PROPERTY(QGeoCoordinate lastKnownPosition READ lastKnownPosition CONSTANT)

public:
    enum State {
        Detected = 0,       // Comms loss detected, waiting for timeout
        RTLTriggered,       // Timeout expired, RTL assumed (drone failsafe)
        CommsRestored,      // Drone came back online
        ZoneReassigned,     // Zone has been reassigned to another drone
    };
    Q_ENUM(State)

    explicit CommsLossEvent(int vehicleId, int zoneId, const QGeoCoordinate &lastPos, QObject *parent = nullptr);

    int             vehicleId() const           { return _vehicleId; }
    int             zoneId() const              { return _zoneId; }
    State           state() const               { return _state; }
    int             elapsedSec() const          { return _elapsedSec; }
    QGeoCoordinate  lastKnownPosition() const   { return _lastKnownPosition; }

    void setState(State state);
    void setElapsedSec(int sec);

signals:
    void stateChanged();
    void elapsedChanged();

private:
    int             _vehicleId;
    int             _zoneId;
    State           _state = Detected;
    int             _elapsedSec = 0;
    QGeoCoordinate  _lastKnownPosition;
};

// ============================================================================
// VehicleCoordinator — central multi-vehicle coordination engine
//
// Responsibilities:
//   1. Sector deconfliction — zone overlap detection + boundary monitoring
//   2. Altitude separation  — dynamic proximity-based with safety bubble
//   3. Loss-of-comms        — detect, react (zone reassign), preflight verify
//   4. Handoff orchestration — deconfliction-aware target handoff
// ============================================================================

class VehicleCoordinator : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")

    // ── Master enable ──
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY settingsChanged)

    // ── Sector deconfliction ──
    Q_PROPERTY(bool                 deconflictionEnabled    READ deconflictionEnabled   WRITE setDeconflictionEnabled   NOTIFY settingsChanged)
    Q_PROPERTY(bool                 hasOverlap              READ hasOverlap             NOTIFY deconflictionChanged)
    Q_PROPERTY(int                  overlapCount            READ overlapCount           NOTIFY deconflictionChanged)
    Q_PROPERTY(QmlObjectListModel*  overlaps                READ overlaps               CONSTANT)
    Q_PROPERTY(int                  boundaryViolationCount  READ boundaryViolationCount NOTIFY boundaryViolationsChanged)

    // ── Altitude separation ──
    Q_PROPERTY(bool                 altitudeSeparationEnabled   READ altitudeSeparationEnabled  WRITE setAltitudeSeparationEnabled  NOTIFY settingsChanged)
    Q_PROPERTY(double               safetyBubbleHorizontalM    READ safetyBubbleHorizontalM    WRITE setSafetyBubbleHorizontalM    NOTIFY settingsChanged)
    Q_PROPERTY(double               safetyBubbleVerticalM      READ safetyBubbleVerticalM      WRITE setSafetyBubbleVerticalM      NOTIFY settingsChanged)
    Q_PROPERTY(int                  activeConflictCount         READ activeConflictCount         NOTIFY conflictsChanged)
    Q_PROPERTY(QmlObjectListModel*  conflicts                  READ conflicts                   CONSTANT)

    // ── Loss of comms ──
    Q_PROPERTY(bool                 commsMonitorEnabled     READ commsMonitorEnabled    WRITE setCommsMonitorEnabled    NOTIFY settingsChanged)
    Q_PROPERTY(int                  commsLossTimeoutSec     READ commsLossTimeoutSec    WRITE setCommsLossTimeoutSec    NOTIFY settingsChanged)
    Q_PROPERTY(int                  dronesInCommsLoss       READ dronesInCommsLoss      NOTIFY commsLossChanged)
    Q_PROPERTY(QmlObjectListModel*  commsLossEvents         READ commsLossEvents        CONSTANT)
    Q_PROPERTY(bool                 allFailsafesVerified    READ allFailsafesVerified   NOTIFY failsafeStatusChanged)

    // ── Handoff ──
    Q_PROPERTY(bool handoffDeconflictionEnabled READ handoffDeconflictionEnabled WRITE setHandoffDeconflictionEnabled NOTIFY settingsChanged)

public:
    explicit VehicleCoordinator(SARMissionManager   *missionMgr,
                                SARZoneManager      *zoneMgr,
                                SARTargetManager    *targetMgr,
                                SARReTaskingManager *reTaskMgr,
                                SARCoverageTracker  *coverageMgr,
                                MeshNetworkManager  *meshMgr,
                                QObject *parent = nullptr);
    ~VehicleCoordinator();

    // ── Master enable ──
    bool enabled() const { return _enabled; }
    void setEnabled(bool enabled);

    // ── Sector deconfliction ──
    bool deconflictionEnabled() const { return _deconflictionEnabled; }
    bool hasOverlap() const;
    int  overlapCount() const;
    QmlObjectListModel *overlaps() const { return _overlaps; }
    int  boundaryViolationCount() const { return _boundaryViolationCount; }

    void setDeconflictionEnabled(bool enabled);

    Q_INVOKABLE bool validateZoneOverlaps();
    Q_INVOKABLE bool isVehicleInAssignedZone(int vehicleId);

    // ── Altitude separation ──
    bool   altitudeSeparationEnabled() const { return _altitudeSeparationEnabled; }
    double safetyBubbleHorizontalM() const   { return _safetyBubbleHorizontalM; }
    double safetyBubbleVerticalM() const     { return _safetyBubbleVerticalM; }
    int    activeConflictCount() const;
    QmlObjectListModel *conflicts() const    { return _conflicts; }

    void setAltitudeSeparationEnabled(bool enabled);
    void setSafetyBubbleHorizontalM(double meters);
    void setSafetyBubbleVerticalM(double meters);

    // ── Loss of comms ──
    bool commsMonitorEnabled() const    { return _commsMonitorEnabled; }
    int  commsLossTimeoutSec() const    { return _commsLossTimeoutSec; }
    int  dronesInCommsLoss() const;
    QmlObjectListModel *commsLossEvents() const { return _commsLossEvents; }
    bool allFailsafesVerified() const   { return _allFailsafesVerified; }

    void setCommsMonitorEnabled(bool enabled);
    void setCommsLossTimeoutSec(int seconds);

    Q_INVOKABLE void verifyFailsafeConfig(int vehicleId);
    Q_INVOKABLE void verifyAllFailsafes();
    Q_INVOKABLE void reassignLostVehicleZone(int vehicleId);

    // ── Handoff ──
    bool handoffDeconflictionEnabled() const { return _handoffDeconflictionEnabled; }
    void setHandoffDeconflictionEnabled(bool enabled);

    Q_INVOKABLE int  selectHandoffDrone(const QGeoCoordinate &targetCoord, int excludeVehicleId);
    Q_INVOKABLE bool isHandoffSafe(int vehicleId, const QGeoCoordinate &targetCoord);

    // ── Serialization ──
    QJsonObject settingsToJson() const;
    void settingsFromJson(const QJsonObject &json);

signals:
    void settingsChanged();

    // Sector deconfliction
    void deconflictionChanged();
    void zoneOverlapDetected(int zoneIdA, int zoneIdB);
    void deconflictionClean();
    void boundaryViolation(int vehicleId, int violatedZoneId);
    void boundaryViolationsChanged();

    // Altitude separation
    void conflictsChanged();
    void proximityAlert(int vehicleIdA, int vehicleIdB, double distanceM);
    void altitudeAdjusted(int vehicleId, double newAltitudeM);

    // Loss of comms
    void commsLossChanged();
    void commsLossDetected(int vehicleId);
    void commsRestored(int vehicleId);
    void commsLossRTL(int vehicleId);
    void zoneReassigned(int zoneId, int oldVehicleId, int newVehicleId);
    void failsafeStatusChanged();
    void failsafeVerified(int vehicleId, bool correct);

    // Handoff
    void handoffBlocked(int vehicleId, const QString &reason);

private slots:
    void _tick();
    void _onZonesChanged();
    void _onNodeLost(int vehicleId);
    void _onNodeAdded(int vehicleId);

private:
    // ── Sector deconfliction internals ──
    void _checkZoneOverlaps();
    void _checkBoundaryViolations();
    bool _polygonsOverlap(const QVariantList &polyA, const QVariantList &polyB) const;
    bool _isPointInPolygon(const QGeoCoordinate &point, const QVariantList &polygon) const;
    bool _edgesIntersect(const QGeoCoordinate &a1, const QGeoCoordinate &a2,
                         const QGeoCoordinate &b1, const QGeoCoordinate &b2) const;
    double _estimateOverlapPercent(const QVariantList &polyA, const QVariantList &polyB) const;

    // ── Altitude separation internals ──
    void _checkProximityConflicts();
    double _horizontalDistance(const QGeoCoordinate &a, const QGeoCoordinate &b) const;
    double _verticalDistance(const QGeoCoordinate &a, const QGeoCoordinate &b) const;
    void _commandAltitudeAdjust(int vehicleId, double deltaAltM);
    void _restoreAltitudeAdjust(int vehicleId);
    ProximityConflict *_findConflict(int idA, int idB) const;

    // ── Loss of comms internals ──
    void _checkCommsStatus();
    void _handleCommsLoss(int vehicleId);
    void _handleCommsRestored(int vehicleId);
    void _removeCommsLossEvent(int vehicleId);
    CommsLossEvent *_findCommsLossEvent(int vehicleId) const;
    int _findBestReassignmentTarget(int excludeVehicleId, const QGeoCoordinate &zoneCenter) const;

    // ── Handoff internals ──
    bool _wouldCauseProximityConflict(int vehicleId, const QGeoCoordinate &targetCoord) const;

    // ── Sub-system references (QPointer guards against external destruction) ──
    QPointer<SARMissionManager>   _missionMgr;
    QPointer<SARZoneManager>      _zoneMgr;
    QPointer<SARTargetManager>    _targetMgr;
    QPointer<SARReTaskingManager> _reTaskMgr;
    QPointer<SARCoverageTracker>  _coverageMgr;
    QPointer<MeshNetworkManager>  _meshMgr;

    // ── Tick timer ──
    QTimer *_tickTimer = nullptr;

    // ── Master enable ──
    bool _enabled = true;

    // ── Sector deconfliction state ──
    bool _deconflictionEnabled = true;
    QmlObjectListModel *_overlaps = nullptr;
    int _boundaryViolationCount = 0;
    QHash<int, int> _vehicleBoundaryViolations;  // vehicleId → violatedZoneId (0 = none)

    // ── Altitude separation state ──
    bool   _altitudeSeparationEnabled = true;
    double _safetyBubbleHorizontalM = 50.0;
    double _safetyBubbleVerticalM = 10.0;
    QmlObjectListModel *_conflicts = nullptr;
    QHash<int, double> _altitudeAdjustments;  // vehicleId → cumulative altitude offset applied

    // ── Loss of comms state ──
    bool _commsMonitorEnabled = true;
    int  _commsLossTimeoutSec = 30;
    QmlObjectListModel *_commsLossEvents = nullptr;
    bool _allFailsafesVerified = false;
    QHash<int, bool> _failsafeStatus;  // vehicleId → verified

    // ── Handoff state ──
    bool _handoffDeconflictionEnabled = true;

    // ── Staggered tick counter (2C) ──
    int _tickPhase = 0;

    // ── Zone overlap dirty flag (2D) ──
    bool _zoneOverlapsDirty = true;

    // ── Constants ──
    static constexpr int kTickIntervalMs = 500;
    static constexpr int kTickPhases = 3;             // 3-phase stagger: boundary, proximity, comms
    static constexpr double kMinSafetyBubbleH = 10.0;
    static constexpr double kMaxSafetyBubbleH = 500.0;
    static constexpr double kMinSafetyBubbleV = 3.0;
    static constexpr double kMaxSafetyBubbleV = 100.0;
    static constexpr int kMinCommsTimeout = 5;
    static constexpr int kMaxCommsTimeout = 300;
};
