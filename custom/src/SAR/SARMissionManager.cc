#include "SARMissionManager.h"
#include "SARZoneManager.h"
#include "SARCoverageTracker.h"
#include "SARTargetManager.h"
#include "SARReTaskingManager.h"
#include "QGCLoggingCategory.h"
#include "QmlObjectListModel.h"
#include "Vehicle.h"
#include "MissionManager.h"
#include "PlanManager.h"
#include "MissionItem.h"
#include "QGCMAVLink.h"

#include <cmath>
#include <limits>

QGC_LOGGING_CATEGORY(SARMissionManagerLog, "Sadron.SARMissionManager")

SARMissionManager::SARMissionManager(SARZoneManager *zoneManager, SARCoverageTracker *coverageTracker, QObject *parent)
    : QObject(parent)
    , _zoneManager(zoneManager)
    , _coverageTracker(coverageTracker)
{
    // Auto-regenerate transects when search parameters change
    connect(this, &SARMissionManager::patternChanged, this, &SARMissionManager::_onSearchParamsChanged);
    connect(this, &SARMissionManager::altitudeChanged, this, &SARMissionManager::_onSearchParamsChanged);
    connect(this, &SARMissionManager::spacingChanged, this, &SARMissionManager::_onSearchParamsChanged);

    // Track vehicle fleet changes for readiness
    auto *mvm = MultiVehicleManager::instance();
    connect(mvm, &MultiVehicleManager::vehicleAdded,   this, &SARMissionManager::_onVehicleAdded);
    connect(mvm, &MultiVehicleManager::vehicleRemoved,  this, &SARMissionManager::_onVehicleRemoved);

    // Wire existing vehicles
    QmlObjectListModel *vehicles = mvm->vehicles();
    for (int i = 0; i < vehicles->count(); i++) {
        auto *v = qobject_cast<Vehicle *>(vehicles->get(i));
        if (v) {
            connect(v, &Vehicle::armedChanged,  this, &SARMissionManager::_updateReadiness);
            connect(v, &Vehicle::flyingChanged,  this, &SARMissionManager::_updateReadiness);
        }
    }

    // Track zone changes for readiness
    if (_zoneManager) {
        connect(_zoneManager, &SARZoneManager::zonesChanged,  this, &SARMissionManager::_updateReadiness);
        connect(_zoneManager, &SARZoneManager::zoneAssigned,  this, [this]() { _updateReadiness(); });
    }

    // Abort safety timer — forces phase advancement if vehicles don't respond within 30s
    _abortSafetyTimer = new QTimer(this);
    _abortSafetyTimer->setSingleShot(true);
    _abortSafetyTimer->setInterval(30000);
    connect(_abortSafetyTimer, &QTimer::timeout, this, &SARMissionManager::_abortSafetyTimeout);
}

SARMissionManager::~SARMissionManager()
{
}

void SARMissionManager::setCurrentPattern(SearchPattern pattern) { if (_currentPattern != pattern) { _currentPattern = pattern; emit patternChanged(); } }
void SARMissionManager::setSearchAltitude(double altitude) { if (!qFuzzyCompare(_searchAltitude, altitude)) { _searchAltitude = altitude; emit altitudeChanged(); } }
void SARMissionManager::setSearchSpeed(double speed) { if (!qFuzzyCompare(_searchSpeed, speed)) { _searchSpeed = speed; emit speedChanged(); } }
void SARMissionManager::setTrackSpacing(double spacing) { if (!qFuzzyCompare(_trackSpacing, spacing)) { _trackSpacing = spacing; emit spacingChanged(); } }

void SARMissionManager::setShowFlightPaths(bool show)
{
    if (_showFlightPaths != show) {
        _showFlightPaths = show;
        emit showFlightPathsChanged();
    }
}

void SARMissionManager::startOperation()
{
    if (!canStartMission()) {
        qCWarning(SARMissionManagerLog) << "Cannot start operation:" << startBlockedReason();
        return;
    }

    // Regenerate all transects with current pattern/params before uploading
    // to guarantee the missions match the user's latest selections
    generateAllTransects();
    qCDebug(SARMissionManagerLog) << "Regenerated transects with pattern" << _currentPattern
                                   << "spacing" << _trackSpacing << "alt" << _searchAltitude;

    _missionActive = true;
    _phase = Deploying;
    emit missionActiveChanged();
    emit phaseChanged();
    emit operationStarted();
    qCDebug(SARMissionManagerLog) << "SAR operation started — deploying missions";

    // ── Build and upload missions for each assigned zone ──
    _pendingUploads.clear();
    _uploadSuccessCount = 0;
    _uploadFailCount = 0;

    QmlObjectListModel *zones = _zoneManager->zones();
    for (int i = 0; i < zones->count(); i++) {
        auto *zone = qobject_cast<SARZone *>(zones->get(i));
        if (!zone || zone->assignedVehicle() < 0 || zone->transectPath().isEmpty())
            continue;

        Vehicle *vehicle = MultiVehicleManager::instance()->getVehicleById(zone->assignedVehicle());
        if (!vehicle) {
            qCWarning(SARMissionManagerLog) << "Vehicle" << zone->assignedVehicle() << "not found — skipping zone" << zone->zoneId();
            continue;
        }

        _pendingUploads.insert(vehicle->id(), false);
        _uploadMissionToVehicle(vehicle, zone);
    }

    if (_pendingUploads.isEmpty()) {
        qCWarning(SARMissionManagerLog) << "No missions to upload — aborting deployment";
        _missionActive = false;
        _phase = Planning;
        emit missionActiveChanged();
        emit phaseChanged();
        emit deploymentFailed(QStringLiteral("No valid zone/vehicle assignments found"));
    }
}

void SARMissionManager::pauseOperation()
{
    if (!_missionActive || _paused) return;

    _paused = true;
    emit pausedChanged();

    // Pause all assigned vehicles
    auto *mvm = MultiVehicleManager::instance();
    QmlObjectListModel *vehicles = mvm->vehicles();
    for (int i = 0; i < vehicles->count(); i++) {
        auto *v = qobject_cast<Vehicle *>(vehicles->get(i));
        if (v) {
            v->pauseVehicle();
        }
    }

    qCDebug(SARMissionManagerLog) << "SAR operation paused";
}

void SARMissionManager::resumeOperation()
{
    if (!_missionActive || !_paused) return;

    _paused = false;
    emit pausedChanged();

    // Resume all assigned vehicles
    auto *mvm = MultiVehicleManager::instance();
    QmlObjectListModel *vehicles = mvm->vehicles();
    for (int i = 0; i < vehicles->count(); i++) {
        auto *v = qobject_cast<Vehicle *>(vehicles->get(i));
        if (v) {
            v->startMission();
        }
    }

    qCDebug(SARMissionManagerLog) << "SAR operation resumed";
}

void SARMissionManager::abortOperation()
{
    if (_abortPhase != AbortIdle) {
        qCWarning(SARMissionManagerLog) << "Abort already in progress";
        return;
    }

    _missionActive = false;
    if (_paused) {
        _paused = false;
        emit pausedChanged();
    }
    _phase = Recovery;
    emit missionActiveChanged();
    emit phaseChanged();

    qCDebug(SARMissionManagerLog) << "SAR operation abort initiated — landing all vehicles";
    _abortPhase_landVehicles();
}

void SARMissionManager::advancePhase()
{
    switch (_phase) {
    case Planning:      _phase = Briefing; break;
    case Briefing:      _phase = Deploying; break;
    case Deploying:     _phase = Searching; break;
    case Searching:     _phase = Investigating; break;
    case Investigating: _phase = Recovery; break;
    case Recovery:      _phase = Debriefing; break;
    case Debriefing:
        _phase = Planning;
        _missionActive = false;
        if (_paused) {
            _paused = false;
            emit pausedChanged();
        }
        emit missionActiveChanged();
        emit operationCompleted();
        break;
    }
    emit phaseChanged();
    qCDebug(SARMissionManagerLog) << "Phase advanced to:" << _phase;
}

void SARMissionManager::generateMissions()
{
    if (!_zoneManager) return;

    QmlObjectListModel *zones = _zoneManager->zones();
    for (int i = 0; i < zones->count(); i++) {
        auto *zone = qobject_cast<SARZone *>(zones->get(i));
        if (zone && zone->assignedVehicle() >= 0) {
            QVariantList waypoints = generatePatternWaypoints(zone->polygon());
            zone->setTransectPath(waypoints);
            qCDebug(SARMissionManagerLog) << "Generated" << waypoints.size() << "waypoints for vehicle" << zone->assignedVehicle();
            emit missionGenerated(zone->assignedVehicle());
        }
    }
}

QVariantList SARMissionManager::generatePatternWaypoints(const QVariantList &polygon, int pattern) const
{
    SearchPattern pat = (pattern >= 0) ? static_cast<SearchPattern>(pattern) : _currentPattern;

    switch (pat) {
    case ParallelTrack:
        return _generateParallelTrack(polygon, _trackSpacing, _searchAltitude);
    case CreepingLine:
        return _generateCreepingLine(polygon, _trackSpacing, _searchAltitude);
    case ExpandingSquare:
        if (!polygon.isEmpty()) {
            // Use centroid as center
            double latSum = 0, lonSum = 0;
            for (const QVariant &v : polygon) {
                QGeoCoordinate c = v.value<QGeoCoordinate>();
                latSum += c.latitude();
                lonSum += c.longitude();
            }
            QGeoCoordinate center(latSum / polygon.size(), lonSum / polygon.size());
            // Estimate radius from bounding box
            double maxDist = 0;
            for (const QVariant &v : polygon) {
                double d = center.distanceTo(v.value<QGeoCoordinate>());
                if (d > maxDist) maxDist = d;
            }
            return _generateExpandingSquare(center, maxDist, _trackSpacing, _searchAltitude);
        }
        return QVariantList();
    case SectorSearch:
        if (!polygon.isEmpty()) {
            QGeoCoordinate datum = polygon[0].value<QGeoCoordinate>();
            double maxDist = 0;
            for (const QVariant &v : polygon) {
                double d = datum.distanceTo(v.value<QGeoCoordinate>());
                if (d > maxDist) maxDist = d;
            }
            return _generateSectorSearch(datum, maxDist, _searchAltitude);
        }
        return QVariantList();
    }

    return QVariantList();
}

QVariantList SARMissionManager::generateZoneTransect(int zoneId)
{
    if (!_zoneManager) return QVariantList();

    SARZone *zone = _zoneManager->getZone(zoneId);
    if (!zone) return QVariantList();

    // Resolve per-zone vs global params
    double spacing = (zone->useGlobalParams() || zone->trackSpacing() < 0) ? _trackSpacing : zone->trackSpacing();
    double altitude = (zone->useGlobalParams() || zone->searchAltitude() < 0) ? _searchAltitude : zone->searchAltitude();
    double speed = (zone->useGlobalParams() || zone->searchSpeed() < 0) ? _searchSpeed : zone->searchSpeed();
    int patternInt = (zone->useGlobalParams() || zone->searchPattern() < 0) ? static_cast<int>(_currentPattern) : zone->searchPattern();
    SearchPattern pat = static_cast<SearchPattern>(patternInt);

    QVariantList polygon = zone->polygon();
    if (polygon.isEmpty()) return QVariantList();

    QVariantList waypoints;

    switch (pat) {
    case ParallelTrack:
        waypoints = _generateParallelTrack(polygon, spacing, altitude);
        break;
    case CreepingLine:
        waypoints = _generateCreepingLine(polygon, spacing, altitude);
        break;
    case ExpandingSquare:
        if (!polygon.isEmpty()) {
            double latSum = 0, lonSum = 0;
            for (const QVariant &v : polygon) {
                QGeoCoordinate c = v.value<QGeoCoordinate>();
                latSum += c.latitude();
                lonSum += c.longitude();
            }
            QGeoCoordinate center(latSum / polygon.size(), lonSum / polygon.size());
            double maxDist = 0;
            for (const QVariant &v : polygon) {
                double d = center.distanceTo(v.value<QGeoCoordinate>());
                if (d > maxDist) maxDist = d;
            }
            waypoints = _generateExpandingSquare(center, maxDist, spacing, altitude);
        }
        break;
    case SectorSearch:
        if (!polygon.isEmpty()) {
            QGeoCoordinate datum = polygon[0].value<QGeoCoordinate>();
            double maxDist = 0;
            for (const QVariant &v : polygon) {
                double d = datum.distanceTo(v.value<QGeoCoordinate>());
                if (d > maxDist) maxDist = d;
            }
            waypoints = _generateSectorSearch(datum, maxDist, altitude);
        }
        break;
    }

    zone->setTransectPath(waypoints);
    // Store the resolved speed on the zone for mission upload
    if (!zone->useGlobalParams() && zone->searchSpeed() < 0) {
        zone->setSearchSpeed(speed);
    }
    qCDebug(SARMissionManagerLog) << "Generated" << waypoints.size() << "transect waypoints for zone" << zoneId
                                   << "(speed:" << speed << "m/s)";

    return waypoints;
}

void SARMissionManager::generateAllTransects()
{
    if (!_zoneManager) return;

    QmlObjectListModel *zones = _zoneManager->zones();
    for (int i = 0; i < zones->count(); i++) {
        auto *zone = qobject_cast<SARZone *>(zones->get(i));
        if (zone) {
            generateZoneTransect(zone->zoneId());
        }
    }

    emit transectsGenerated();
    qCDebug(SARMissionManagerLog) << "Generated transects for all" << zones->count() << "zones";
}

void SARMissionManager::clearAllTransects()
{
    if (!_zoneManager) return;

    QmlObjectListModel *zones = _zoneManager->zones();
    for (int i = 0; i < zones->count(); i++) {
        auto *zone = qobject_cast<SARZone *>(zones->get(i));
        if (zone) {
            zone->setTransectPath(QVariantList());
        }
    }
}

void SARMissionManager::autoAssignZones()
{
    if (!_zoneManager) return;

    // Collect connected vehicle IDs
    QList<int> vehicleIds;
    auto *mvm = MultiVehicleManager::instance();
    QmlObjectListModel *vehicles = mvm->vehicles();
    for (int i = 0; i < vehicles->count(); i++) {
        auto *v = qobject_cast<Vehicle *>(vehicles->get(i));
        if (v) {
            vehicleIds.append(v->id());
        }
    }

    if (vehicleIds.isEmpty()) {
        qCWarning(SARMissionManagerLog) << "No connected vehicles — cannot auto-assign zones";
        return;
    }

    // Round-robin assignment over connected vehicles
    QmlObjectListModel *zones = _zoneManager->zones();
    int vIdx = 0;
    for (int i = 0; i < zones->count(); i++) {
        auto *zone = qobject_cast<SARZone *>(zones->get(i));
        if (zone && zone->assignedVehicle() < 0) {
            _zoneManager->assignZoneToVehicle(zone->zoneId(), vehicleIds[vIdx % vehicleIds.size()]);
            vIdx++;
        }
    }
}

void SARMissionManager::handleVehicleRTL(int vehicleId)
{
    if (!_zoneManager) return;

    SARZone *zone = _zoneManager->zoneForVehicle(vehicleId);
    if (zone && zone->status() == SARZone::Active) {
        zone->setStatus(SARZone::Reassigning);
        qCDebug(SARMissionManagerLog) << "Vehicle" << vehicleId << "RTL — zone" << zone->zoneId() << "needs reassignment";
    }
}

QVariantList SARMissionManager::_generateParallelTrack(const QVariantList &polygon, double spacing, double altitude) const
{
    QVariantList waypoints;
    if (polygon.size() < 3) return waypoints;

    // Find bounding box
    double minLat = 90, maxLat = -90, minLon = 180, maxLon = -180;
    for (const QVariant &v : polygon) {
        QGeoCoordinate c = v.value<QGeoCoordinate>();
        minLat = qMin(minLat, c.latitude());
        maxLat = qMax(maxLat, c.latitude());
        minLon = qMin(minLon, c.longitude());
        maxLon = qMax(maxLon, c.longitude());
    }

    // Convert track spacing to degrees (approximate)
    double metersPerDegreeLat = 111320.0;
    double latStep = spacing / metersPerDegreeLat;

    bool leftToRight = true;
    for (double lat = minLat; lat <= maxLat; lat += latStep) {
        if (leftToRight) {
            waypoints.append(QVariant::fromValue(QGeoCoordinate(lat, minLon, altitude)));
            waypoints.append(QVariant::fromValue(QGeoCoordinate(lat, maxLon, altitude)));
        } else {
            waypoints.append(QVariant::fromValue(QGeoCoordinate(lat, maxLon, altitude)));
            waypoints.append(QVariant::fromValue(QGeoCoordinate(lat, minLon, altitude)));
        }
        leftToRight = !leftToRight;
    }

    return waypoints;
}

QVariantList SARMissionManager::_generateCreepingLine(const QVariantList &polygon, double spacing, double altitude) const
{
    QVariantList waypoints;
    if (polygon.size() < 3) return waypoints;

    double minLat = 90, maxLat = -90, minLon = 180, maxLon = -180;
    for (const QVariant &v : polygon) {
        QGeoCoordinate c = v.value<QGeoCoordinate>();
        minLat = qMin(minLat, c.latitude());
        maxLat = qMax(maxLat, c.latitude());
        minLon = qMin(minLon, c.longitude());
        maxLon = qMax(maxLon, c.longitude());
    }

    // Similar to parallel track but oriented perpendicular (along longitude)
    double metersPerDegreeLon = 111320.0 * std::cos(((minLat + maxLat) / 2.0) * M_PI / 180.0);
    double lonStep = spacing / metersPerDegreeLon;

    bool bottomToTop = true;
    for (double lon = minLon; lon <= maxLon; lon += lonStep) {
        if (bottomToTop) {
            waypoints.append(QVariant::fromValue(QGeoCoordinate(minLat, lon, altitude)));
            waypoints.append(QVariant::fromValue(QGeoCoordinate(maxLat, lon, altitude)));
        } else {
            waypoints.append(QVariant::fromValue(QGeoCoordinate(maxLat, lon, altitude)));
            waypoints.append(QVariant::fromValue(QGeoCoordinate(minLat, lon, altitude)));
        }
        bottomToTop = !bottomToTop;
    }

    return waypoints;
}

QVariantList SARMissionManager::_generateExpandingSquare(const QGeoCoordinate &center, double sideLength, double spacing, double altitude) const
{
    QVariantList waypoints;

    double metersPerDegreeLat = 111320.0;
    double metersPerDegreeLon = 111320.0 * std::cos(center.latitude() * M_PI / 180.0);

    // Start at center
    waypoints.append(QVariant::fromValue(QGeoCoordinate(center.latitude(), center.longitude(), altitude)));

    double legLength = spacing;
    int direction = 0; // 0=N, 1=E, 2=S, 3=W
    double curLat = center.latitude();
    double curLon = center.longitude();

    while (legLength <= sideLength * 2) {
        // Each pair of legs has the same length, then length increases
        for (int pair = 0; pair < 2 && legLength <= sideLength * 2; pair++) {
            switch (direction % 4) {
            case 0: // North
                curLat += legLength / metersPerDegreeLat;
                break;
            case 1: // East
                curLon += legLength / metersPerDegreeLon;
                break;
            case 2: // South
                curLat -= legLength / metersPerDegreeLat;
                break;
            case 3: // West
                curLon -= legLength / metersPerDegreeLon;
                break;
            }

            waypoints.append(QVariant::fromValue(QGeoCoordinate(curLat, curLon, altitude)));
            direction++;

            if (pair == 1) {
                legLength += spacing;
            }
        }
    }

    return waypoints;
}

QVariantList SARMissionManager::_generateSectorSearch(const QGeoCoordinate &datum, double radius, double altitude) const
{
    QVariantList waypoints;

    double metersPerDegreeLat = 111320.0;
    double metersPerDegreeLon = 111320.0 * std::cos(datum.latitude() * M_PI / 180.0);

    // Generate 3 sectors (120 degrees each) from datum
    const int numSectors = 3;
    const double sectorAngle = 2.0 * M_PI / numSectors;

    for (int s = 0; s < numSectors; s++) {
        double angle1 = s * sectorAngle;
        double angle2 = angle1 + sectorAngle;

        // Datum point
        waypoints.append(QVariant::fromValue(QGeoCoordinate(datum.latitude(), datum.longitude(), altitude)));

        // Point along first radial
        double lat1 = datum.latitude() + (radius * std::cos(angle1)) / metersPerDegreeLat;
        double lon1 = datum.longitude() + (radius * std::sin(angle1)) / metersPerDegreeLon;
        waypoints.append(QVariant::fromValue(QGeoCoordinate(lat1, lon1, altitude)));

        // Arc points along the sector
        const int arcPoints = 5;
        for (int a = 1; a <= arcPoints; a++) {
            double angle = angle1 + (angle2 - angle1) * a / arcPoints;
            double lat = datum.latitude() + (radius * std::cos(angle)) / metersPerDegreeLat;
            double lon = datum.longitude() + (radius * std::sin(angle)) / metersPerDegreeLon;
            waypoints.append(QVariant::fromValue(QGeoCoordinate(lat, lon, altitude)));
        }
    }

    // Return to datum
    waypoints.append(QVariant::fromValue(QGeoCoordinate(datum.latitude(), datum.longitude(), altitude)));

    return waypoints;
}

// ── Readiness methods ──

int SARMissionManager::connectedVehicleCount() const
{
    auto *mvm = MultiVehicleManager::instance();
    return mvm->vehicles()->count();
}

int SARMissionManager::armedVehicleCount() const
{
    int count = 0;
    auto *mvm = MultiVehicleManager::instance();
    QmlObjectListModel *vehicles = mvm->vehicles();
    for (int i = 0; i < vehicles->count(); i++) {
        auto *v = qobject_cast<Vehicle *>(vehicles->get(i));
        if (v && v->armed()) {
            count++;
        }
    }
    return count;
}

bool SARMissionManager::hasConnectedVehicles() const
{
    return connectedVehicleCount() > 0;
}

bool SARMissionManager::canStartMission() const
{
    return startBlockedReason().isEmpty();
}

QString SARMissionManager::startBlockedReason() const
{
    if (!_zoneManager || _zoneManager->totalZones() == 0) {
        return QStringLiteral("No search zones defined");
    }

    if (connectedVehicleCount() == 0) {
        return QStringLiteral("No drones connected");
    }

    // Check that at least one zone has an assigned vehicle
    QmlObjectListModel *zones = _zoneManager->zones();
    bool hasAssignment = false;
    for (int i = 0; i < zones->count(); i++) {
        auto *zone = qobject_cast<SARZone *>(zones->get(i));
        if (zone && zone->assignedVehicle() >= 0) {
            hasAssignment = true;
            break;
        }
    }
    if (!hasAssignment) {
        return QStringLiteral("No drones assigned to zones");
    }

    // Check that at least one assigned vehicle is still connected
    bool hasConnectedAssigned = false;
    for (int i = 0; i < zones->count(); i++) {
        auto *zone = qobject_cast<SARZone *>(zones->get(i));
        if (zone && zone->assignedVehicle() >= 0) {
            Vehicle *v = MultiVehicleManager::instance()->getVehicleById(zone->assignedVehicle());
            if (v) {
                hasConnectedAssigned = true;
                break;
            }
        }
    }
    if (!hasConnectedAssigned) {
        return QStringLiteral("Assigned drones are not connected");
    }

    // Verify that assigned zones have transect paths generated
    bool hasTransects = false;
    for (int i = 0; i < zones->count(); i++) {
        auto *zone = qobject_cast<SARZone *>(zones->get(i));
        if (zone && zone->assignedVehicle() >= 0 && !zone->transectPath().isEmpty()) {
            hasTransects = true;
            break;
        }
    }
    if (!hasTransects) {
        return QStringLiteral("No flight paths generated — run Partition & Assign first");
    }

    return QString();
}

bool SARMissionManager::isVehicleConnected(int vehicleId) const
{
    Vehicle *v = MultiVehicleManager::instance()->getVehicleById(vehicleId);
    return v != nullptr;
}

void SARMissionManager::_onVehicleAdded(Vehicle *vehicle)
{
    if (vehicle) {
        connect(vehicle, &Vehicle::armedChanged,  this, &SARMissionManager::_updateReadiness);
        connect(vehicle, &Vehicle::flyingChanged,  this, &SARMissionManager::_updateReadiness);
    }
    _updateReadiness();
}

void SARMissionManager::_onVehicleRemoved(Vehicle *vehicle)
{
    if (vehicle) {
        disconnect(vehicle, &Vehicle::armedChanged,  this, &SARMissionManager::_updateReadiness);
        disconnect(vehicle, &Vehicle::flyingChanged,  this, &SARMissionManager::_updateReadiness);
    }
    _updateReadiness();
}

void SARMissionManager::_updateReadiness()
{
    emit readinessChanged();
}

void SARMissionManager::_onSearchParamsChanged()
{
    if (!_zoneManager) return;

    // Only auto-regenerate if we have zones with transects already generated
    QmlObjectListModel *zones = _zoneManager->zones();
    bool hasTransects = false;
    for (int i = 0; i < zones->count(); i++) {
        auto *zone = qobject_cast<SARZone *>(zones->get(i));
        if (zone && zone->transectPath().size() > 0) {
            hasTransects = true;
            break;
        }
    }

    if (hasTransects) {
        generateAllTransects();
        qCDebug(SARMissionManagerLog) << "Auto-regenerated transects after parameter change";
    }
}

// ── Mission upload pipeline ──

void SARMissionManager::_uploadMissionToVehicle(Vehicle *vehicle, SARZone *zone)
{
    QList<MissionItem *> missionItems;
    int seqNum = 0;

    // Item 0: Home position (required as sequence 0 by MAVLink mission protocol)
    QGeoCoordinate home = vehicle->homePosition();
    if (!home.isValid()) {
        // Fallback: use first transect waypoint as approximate home
        home = zone->transectPath().first().value<QGeoCoordinate>();
        home.setAltitude(0);
    }
    missionItems.append(new MissionItem(
        seqNum++,
        MAV_CMD_NAV_WAYPOINT,
        MAV_FRAME_GLOBAL,
        0, 0, 0, 0,
        home.latitude(),
        home.longitude(),
        home.altitude(),
        true,   // autoContinue
        false,  // isCurrentItem
        this));

    // Item 1: Takeoff to the search altitude
    double altitude = (zone->useGlobalParams() || zone->searchAltitude() < 0)
                          ? _searchAltitude
                          : zone->searchAltitude();
    missionItems.append(new MissionItem(
        seqNum++,
        MAV_CMD_NAV_TAKEOFF,
        MAV_FRAME_GLOBAL_RELATIVE_ALT,
        0,                                              // param1: min pitch (FW only)
        0,                                              // param2: empty
        0,                                              // param3: empty
        std::numeric_limits<double>::quiet_NaN(),        // param4: yaw (NaN = default)
        home.latitude(),
        home.longitude(),
        altitude,
        true,
        false,
        this));

    // Item 2: Set search speed
    double speed = (zone->useGlobalParams() || zone->searchSpeed() < 0)
                       ? _searchSpeed
                       : zone->searchSpeed();
    missionItems.append(new MissionItem(
        seqNum++,
        MAV_CMD_DO_CHANGE_SPEED,
        MAV_FRAME_MISSION,
        vehicle->multiRotor() ? 1 : 0,     // 1 = groundspeed (copter), 0 = airspeed (FW)
        static_cast<float>(speed),
        -1,                                 // throttle: no change
        0,                                  // absolute
        0, 0, 0,
        true,
        false,
        this));

    // Items 3..N: Transect waypoints
    QVariantList transect = zone->transectPath();
    for (const QVariant &wp : transect) {
        QGeoCoordinate coord = wp.value<QGeoCoordinate>();
        double wpAlt = coord.altitude() > 0 ? coord.altitude() : altitude;
        missionItems.append(new MissionItem(
            seqNum++,
            MAV_CMD_NAV_WAYPOINT,
            MAV_FRAME_GLOBAL_RELATIVE_ALT,
            0,                                              // hold time
            0,                                              // acceptance radius
            0,                                              // pass through
            std::numeric_limits<double>::quiet_NaN(),        // yaw
            coord.latitude(),
            coord.longitude(),
            wpAlt,
            true,
            false,
            this));
    }

    // Final item: RTL after search is complete
    missionItems.append(new MissionItem(
        seqNum++,
        MAV_CMD_NAV_RETURN_TO_LAUNCH,
        MAV_FRAME_MISSION,
        0, 0, 0, 0,
        0, 0, 0,
        true,
        false,
        this));

    qCDebug(SARMissionManagerLog) << "Uploading" << missionItems.size() << "mission items to Vehicle"
                                   << vehicle->id() << "for zone" << zone->zoneId();

    // Connect upload progress and completion signals
    PlanManager *pm = vehicle->missionManager();

    connect(pm, &PlanManager::progressPctChanged, this, [this, vehicle](double pct) {
        emit missionUploadProgress(vehicle->id(), pct);
    });

    connect(pm, &PlanManager::sendComplete, this, &SARMissionManager::_onUploadComplete);

    // PlanManager takes ownership of the MissionItem objects
    pm->writeMissionItems(missionItems);
}

void SARMissionManager::_onUploadComplete(bool error)
{
    // Identify which vehicle's upload just completed from the sender
    auto *pm = qobject_cast<PlanManager *>(sender());
    if (!pm) return;

    // Find the vehicle that owns this PlanManager
    int vehicleId = -1;
    auto *mvm = MultiVehicleManager::instance();
    QmlObjectListModel *vehicles = mvm->vehicles();
    for (int i = 0; i < vehicles->count(); i++) {
        auto *v = qobject_cast<Vehicle *>(vehicles->get(i));
        if (v && v->missionManager() == pm) {
            vehicleId = v->id();
            break;
        }
    }

    // Disconnect so we don't fire again on subsequent uploads
    disconnect(pm, &PlanManager::sendComplete, this, &SARMissionManager::_onUploadComplete);
    disconnect(pm, &PlanManager::progressPctChanged, this, nullptr);

    if (vehicleId < 0 || !_pendingUploads.contains(vehicleId)) {
        qCWarning(SARMissionManagerLog) << "Upload complete for unknown vehicle";
        return;
    }

    _pendingUploads[vehicleId] = true;

    if (error) {
        _uploadFailCount++;
        qCWarning(SARMissionManagerLog) << "Mission upload FAILED for Vehicle" << vehicleId;
        emit missionUploadComplete(vehicleId, false);
    } else {
        _uploadSuccessCount++;
        qCDebug(SARMissionManagerLog) << "Mission upload succeeded for Vehicle" << vehicleId;
        emit missionUploadComplete(vehicleId, true);
    }

    // Check if all uploads are done
    bool allDone = true;
    for (auto it = _pendingUploads.constBegin(); it != _pendingUploads.constEnd(); ++it) {
        if (!it.value()) {
            allDone = false;
            break;
        }
    }

    if (allDone) {
        if (_uploadFailCount > 0) {
            qCWarning(SARMissionManagerLog) << _uploadFailCount << "of" << _pendingUploads.size() << "uploads failed";
            emit deploymentFailed(QStringLiteral("%1 of %2 mission uploads failed")
                                      .arg(_uploadFailCount)
                                      .arg(_pendingUploads.size()));
            // Don't start any vehicles if some uploads failed — leave in Deploying for retry
        } else {
            qCDebug(SARMissionManagerLog) << "All" << _uploadSuccessCount << "missions uploaded — starting vehicles";
            _startAllUploadedVehicles();
        }
    }
}

void SARMissionManager::_startAllUploadedVehicles()
{
    auto *mvm = MultiVehicleManager::instance();

    for (auto it = _pendingUploads.constBegin(); it != _pendingUploads.constEnd(); ++it) {
        Vehicle *v = mvm->getVehicleById(it.key());
        if (v) {
            // startMission() handles: set Mission mode → arm → auto-takeoff (PX4)
            // or: set Auto mode → arm → MAV_CMD_MISSION_START (ArduPilot)
            v->startMission();
            qCDebug(SARMissionManagerLog) << "Started mission on Vehicle" << v->id();

            // Mark the zone as Active
            if (_zoneManager) {
                SARZone *zone = _zoneManager->zoneForVehicle(v->id());
                if (zone) {
                    zone->setStatus(SARZone::Active);
                }
            }
        }
    }

    _phase = Searching;
    emit phaseChanged();
    emit deploymentComplete();
    qCDebug(SARMissionManagerLog) << "Deployment complete — entering Searching phase";
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Abort state machine ──
// ══════════════════════════════════════════════════════════════════════════════

void SARMissionManager::setTargetManager(SARTargetManager *mgr)
{
    _targetManager = mgr;
}

void SARMissionManager::setReTaskingManager(SARReTaskingManager *mgr)
{
    _reTaskingManager = mgr;
}

QString SARMissionManager::abortStatusText() const
{
    switch (_abortPhase) {
    case AbortIdle:          return QString();
    case LandingVehicles:    return QStringLiteral("Landing all vehicles...");
    case DisarmingVehicles:  return QStringLiteral("Disarming vehicles...");
    case ClearingMissions:   return QStringLiteral("Clearing missions...");
    case ResettingState:     return QStringLiteral("Resetting SAR state...");
    }
    return QString();
}

void SARMissionManager::_abortPhase_landVehicles()
{
    _abortPhase = LandingVehicles;
    _abortPendingVehicles.clear();
    emit abortProgressChanged();

    auto *mvm = MultiVehicleManager::instance();
    QmlObjectListModel *vehicles = mvm->vehicles();

    for (int i = 0; i < vehicles->count(); i++) {
        auto *v = qobject_cast<Vehicle *>(vehicles->get(i));
        if (v && v->flying()) {
            _abortPendingVehicles.insert(v->id());
            connect(v, &Vehicle::flyingChanged, this, &SARMissionManager::_onVehicleLandedForAbort);
            v->guidedModeLand();
            qCDebug(SARMissionManagerLog) << "Abort: commanding Vehicle" << v->id() << "to land";
        }
    }

    if (_abortPendingVehicles.isEmpty()) {
        qCDebug(SARMissionManagerLog) << "Abort: no flying vehicles — skipping land phase";
        _advanceAbortPhase();
    } else {
        _abortSafetyTimer->start();
    }
}

void SARMissionManager::_onVehicleLandedForAbort()
{
    auto *v = qobject_cast<Vehicle *>(sender());
    if (!v || _abortPhase != LandingVehicles) return;

    if (!v->flying()) {
        _abortPendingVehicles.remove(v->id());
        disconnect(v, &Vehicle::flyingChanged, this, &SARMissionManager::_onVehicleLandedForAbort);
        qCDebug(SARMissionManagerLog) << "Abort: Vehicle" << v->id() << "has landed ("
                                       << _abortPendingVehicles.size() << "remaining)";

        if (_abortPendingVehicles.isEmpty()) {
            _abortSafetyTimer->stop();
            _advanceAbortPhase();
        }
    }
}

void SARMissionManager::_abortPhase_disarmVehicles()
{
    _abortPhase = DisarmingVehicles;
    _abortPendingVehicles.clear();
    emit abortProgressChanged();

    auto *mvm = MultiVehicleManager::instance();
    QmlObjectListModel *vehicles = mvm->vehicles();

    for (int i = 0; i < vehicles->count(); i++) {
        auto *v = qobject_cast<Vehicle *>(vehicles->get(i));
        if (v && v->armed() && !v->flying()) {
            _abortPendingVehicles.insert(v->id());
            connect(v, &Vehicle::armedChanged, this, &SARMissionManager::_onVehicleDisarmedForAbort);
            v->setArmed(false, false);
            qCDebug(SARMissionManagerLog) << "Abort: disarming Vehicle" << v->id();
        }
    }

    if (_abortPendingVehicles.isEmpty()) {
        qCDebug(SARMissionManagerLog) << "Abort: no armed vehicles — skipping disarm phase";
        _advanceAbortPhase();
    } else {
        _abortSafetyTimer->start();
    }
}

void SARMissionManager::_onVehicleDisarmedForAbort()
{
    auto *v = qobject_cast<Vehicle *>(sender());
    if (!v || _abortPhase != DisarmingVehicles) return;

    if (!v->armed()) {
        _abortPendingVehicles.remove(v->id());
        disconnect(v, &Vehicle::armedChanged, this, &SARMissionManager::_onVehicleDisarmedForAbort);
        qCDebug(SARMissionManagerLog) << "Abort: Vehicle" << v->id() << "disarmed ("
                                       << _abortPendingVehicles.size() << "remaining)";

        if (_abortPendingVehicles.isEmpty()) {
            _abortSafetyTimer->stop();
            _advanceAbortPhase();
        }
    }
}

void SARMissionManager::_abortPhase_clearMissions()
{
    _abortPhase = ClearingMissions;
    emit abortProgressChanged();

    auto *mvm = MultiVehicleManager::instance();
    QmlObjectListModel *vehicles = mvm->vehicles();

    for (int i = 0; i < vehicles->count(); i++) {
        auto *v = qobject_cast<Vehicle *>(vehicles->get(i));
        if (v) {
            v->missionManager()->removeAll();
            qCDebug(SARMissionManagerLog) << "Abort: cleared mission on Vehicle" << v->id();
        }
    }

    // Clear local upload tracking
    _pendingUploads.clear();
    _uploadSuccessCount = 0;
    _uploadFailCount = 0;

    // Fire-and-forget — vehicles are on the ground, advance immediately
    _advanceAbortPhase();
}

void SARMissionManager::_abortPhase_resetState()
{
    _abortPhase = ResettingState;
    emit abortProgressChanged();

    // 1. Clear transects
    clearAllTransects();

    // 2. Reset all zones: status → Pending, unassign vehicles, progress → 0
    if (_zoneManager) {
        QmlObjectListModel *zones = _zoneManager->zones();
        for (int i = 0; i < zones->count(); i++) {
            auto *zone = qobject_cast<SARZone *>(zones->get(i));
            if (zone) {
                zone->setStatus(SARZone::Pending);
                zone->setAssignedVehicle(-1);
                zone->setProgress(0.0);
            }
        }
    }

    // 3. Clear targets
    if (_targetManager) {
        _targetManager->clearAllTargets();
    }

    // 4. Reset coverage
    if (_coverageTracker) {
        _coverageTracker->resetCoverage();
    }

    // 5. Cancel pending re-task and complete all active re-tasks
    if (_reTaskingManager) {
        _reTaskingManager->cancelReTask();
        _reTaskingManager->completeAllReTasks();
    }

    // 6. Reset phase to Planning
    _phase = Planning;
    emit phaseChanged();
    emit operationCompleted();

    qCDebug(SARMissionManagerLog) << "Abort: SAR state fully reset to Planning";

    // Finalize abort
    _abortPhase = AbortIdle;
    emit abortProgressChanged();
    emit abortCompleted();
}

void SARMissionManager::_advanceAbortPhase()
{
    switch (_abortPhase) {
    case AbortIdle:
        // Should not be called in idle state
        break;
    case LandingVehicles:
        _abortPhase_disarmVehicles();
        break;
    case DisarmingVehicles:
        _abortPhase_clearMissions();
        break;
    case ClearingMissions:
        _abortPhase_resetState();
        break;
    case ResettingState:
        // Already final phase — should not advance further
        break;
    }
}

void SARMissionManager::_abortSafetyTimeout()
{
    qCWarning(SARMissionManagerLog) << "Abort safety timeout in phase" << _abortPhase
                                     << "— forcing advancement with" << _abortPendingVehicles.size()
                                     << "vehicles still pending";

    // Disconnect any remaining vehicle signals
    auto *mvm = MultiVehicleManager::instance();
    QmlObjectListModel *vehicles = mvm->vehicles();
    for (int i = 0; i < vehicles->count(); i++) {
        auto *v = qobject_cast<Vehicle *>(vehicles->get(i));
        if (v && _abortPendingVehicles.contains(v->id())) {
            if (_abortPhase == LandingVehicles) {
                disconnect(v, &Vehicle::flyingChanged, this, &SARMissionManager::_onVehicleLandedForAbort);
            } else if (_abortPhase == DisarmingVehicles) {
                disconnect(v, &Vehicle::armedChanged, this, &SARMissionManager::_onVehicleDisarmedForAbort);
            }
        }
    }

    _abortPendingVehicles.clear();
    _advanceAbortPhase();
}
