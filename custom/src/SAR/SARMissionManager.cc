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
#include "FirmwarePlugin.h"
#include "ParameterManager.h"
#include "QGCMAVLink.h"

#include <QtCore/QPointF>
#include <QtCore/QRectF>

#include <algorithm>
#include <cmath>
#include <limits>

QGC_LOGGING_CATEGORY(SARMissionManagerLog, "Sadron.SARMissionManager")

namespace {

constexpr double kGeometryEpsilon = 1e-6;
constexpr double kMinimumMetersPerDegreeLon = 1.0;

struct LocalFrame {
    QGeoCoordinate origin;
    double metersPerDegreeLat = 111320.0;
    double metersPerDegreeLon = 111320.0;
};

struct SegmentSpan {
    double start = 0.0;
    double end = 0.0;
};

double _crossProduct(const QPointF &a, const QPointF &b)
{
    return a.x() * b.y() - a.y() * b.x();
}

bool _isSearchPatternValueValid(int pattern)
{
    return pattern >= static_cast<int>(SARMissionManager::ParallelTrack)
        && pattern <= static_cast<int>(SARMissionManager::SectorSearch);
}

int _resolvedSearchPatternValue(int requestedPattern, int fallbackPattern)
{
    return _isSearchPatternValueValid(requestedPattern) ? requestedPattern : fallbackPattern;
}

LocalFrame _buildLocalFrame(const QVariantList &polygon)
{
    LocalFrame frame;
    if (polygon.isEmpty()) {
        return frame;
    }

    double latSum = 0.0;
    double lonSum = 0.0;
    for (const QVariant &value : polygon) {
        const QGeoCoordinate coord = value.value<QGeoCoordinate>();
        latSum += coord.latitude();
        lonSum += coord.longitude();
    }

    frame.origin = QGeoCoordinate(latSum / polygon.size(), lonSum / polygon.size());
    const double cosLat = std::abs(std::cos(frame.origin.latitude() * M_PI / 180.0));
    frame.metersPerDegreeLon = qMax(kMinimumMetersPerDegreeLon, 111320.0 * cosLat);
    return frame;
}

QPointF _toLocalPoint(const QGeoCoordinate &coord, const LocalFrame &frame)
{
    return QPointF((coord.longitude() - frame.origin.longitude()) * frame.metersPerDegreeLon,
                   (coord.latitude() - frame.origin.latitude()) * frame.metersPerDegreeLat);
}

QGeoCoordinate _toGeoCoordinate(const QPointF &point, const LocalFrame &frame, double altitude)
{
    return QGeoCoordinate(frame.origin.latitude() + (point.y() / frame.metersPerDegreeLat),
                          frame.origin.longitude() + (point.x() / frame.metersPerDegreeLon),
                          altitude);
}

QVector<QPointF> _toLocalPolygon(const QVariantList &polygon, const LocalFrame &frame)
{
    QVector<QPointF> localPolygon;
    localPolygon.reserve(polygon.size());

    for (const QVariant &value : polygon) {
        localPolygon.append(_toLocalPoint(value.value<QGeoCoordinate>(), frame));
    }

    return localPolygon;
}

bool _pointsEqual(const QPointF &first, const QPointF &second)
{
    return std::abs(first.x() - second.x()) <= kGeometryEpsilon
        && std::abs(first.y() - second.y()) <= kGeometryEpsilon;
}

bool _pointOnSegment(const QPointF &point, const QPointF &segmentStart, const QPointF &segmentEnd)
{
    const QPointF segment = segmentEnd - segmentStart;
    const QPointF offset = point - segmentStart;
    if (std::abs(_crossProduct(segment, offset)) > kGeometryEpsilon) {
        return false;
    }

    const double minX = qMin(segmentStart.x(), segmentEnd.x()) - kGeometryEpsilon;
    const double maxX = qMax(segmentStart.x(), segmentEnd.x()) + kGeometryEpsilon;
    const double minY = qMin(segmentStart.y(), segmentEnd.y()) - kGeometryEpsilon;
    const double maxY = qMax(segmentStart.y(), segmentEnd.y()) + kGeometryEpsilon;

    return point.x() >= minX && point.x() <= maxX && point.y() >= minY && point.y() <= maxY;
}

bool _pointOnPolygon(const QPointF &point, const QVector<QPointF> &polygon)
{
    for (int index = 0; index < polygon.size(); ++index) {
        const QPointF &start = polygon[index];
        const QPointF &end = polygon[(index + 1) % polygon.size()];
        if (_pointOnSegment(point, start, end)) {
            return true;
        }
    }

    return false;
}

bool _pointInPolygon(const QPointF &point, const QVector<QPointF> &polygon)
{
    if (polygon.size() < 3) {
        return false;
    }

    if (_pointOnPolygon(point, polygon)) {
        return true;
    }

    bool inside = false;
    for (int index = 0, previous = polygon.size() - 1; index < polygon.size(); previous = index++) {
        const QPointF &currentPoint = polygon[index];
        const QPointF &previousPoint = polygon[previous];
        const bool crosses = ((currentPoint.y() > point.y()) != (previousPoint.y() > point.y()));
        if (!crosses) {
            continue;
        }

        const double intersectionX =
            ((previousPoint.x() - currentPoint.x()) * (point.y() - currentPoint.y())
             / (previousPoint.y() - currentPoint.y()))
            + currentPoint.x();
        if (point.x() < intersectionX) {
            inside = !inside;
        }
    }

    return inside;
}

bool _segmentIntersectionParameter(const QPointF &segmentStart,
                                   const QPointF &segmentEnd,
                                   const QPointF &edgeStart,
                                   const QPointF &edgeEnd,
                                   double *segmentT)
{
    const QPointF segment = segmentEnd - segmentStart;
    const QPointF edge = edgeEnd - edgeStart;
    const double denominator = _crossProduct(segment, edge);
    if (std::abs(denominator) <= kGeometryEpsilon) {
        return false;
    }

    const QPointF delta = edgeStart - segmentStart;
    const double t = _crossProduct(delta, edge) / denominator;
    const double u = _crossProduct(delta, segment) / denominator;
    if (t < -kGeometryEpsilon || t > 1.0 + kGeometryEpsilon || u < -kGeometryEpsilon || u > 1.0 + kGeometryEpsilon) {
        return false;
    }

    *segmentT = qBound(0.0, t, 1.0);
    return true;
}

QList<double> _segmentIntersectionParameters(const QPointF &segmentStart,
                                             const QPointF &segmentEnd,
                                             const QVector<QPointF> &polygon)
{
    QList<double> intersections { 0.0, 1.0 };

    for (int index = 0; index < polygon.size(); ++index) {
        double segmentT = 0.0;
        if (_segmentIntersectionParameter(segmentStart,
                                          segmentEnd,
                                          polygon[index],
                                          polygon[(index + 1) % polygon.size()],
                                          &segmentT)) {
            intersections.append(segmentT);
        }
    }

    std::sort(intersections.begin(), intersections.end());

    QList<double> uniqueIntersections;
    for (double value : intersections) {
        if (uniqueIntersections.isEmpty() || std::abs(value - uniqueIntersections.last()) > kGeometryEpsilon) {
            uniqueIntersections.append(value);
        }
    }

    return uniqueIntersections;
}

QPointF _interpolatePoint(const QPointF &start, const QPointF &end, double t)
{
    return start + (end - start) * t;
}

bool _clipSegmentFromStart(const QPointF &start,
                           const QPointF &end,
                           const QVector<QPointF> &polygon,
                           QPointF *clippedEnd)
{
    if (!_pointInPolygon(start, polygon)) {
        return false;
    }

    const QList<double> intersections = _segmentIntersectionParameters(start, end, polygon);
    for (int index = 0; index + 1 < intersections.size(); ++index) {
        const double spanStart = intersections[index];
        const double spanEnd = intersections[index + 1];
        const double midpointT = (spanStart + spanEnd) / 2.0;
        const QPointF midpoint = _interpolatePoint(start, end, midpointT);
        if (_pointInPolygon(midpoint, polygon) && spanStart <= kGeometryEpsilon) {
            *clippedEnd = _interpolatePoint(start, end, spanEnd);
            return true;
        }
    }

    return false;
}

QList<SegmentSpan> _horizontalSpans(const QVector<QPointF> &polygon, double y)
{
    QList<double> intersections;
    for (int index = 0; index < polygon.size(); ++index) {
        const QPointF &start = polygon[index];
        const QPointF &end = polygon[(index + 1) % polygon.size()];
        if (std::abs(start.y() - end.y()) <= kGeometryEpsilon) {
            continue;
        }

        const bool crosses = ((start.y() <= y) && (end.y() > y)) || ((end.y() <= y) && (start.y() > y));
        if (!crosses) {
            continue;
        }

        const double t = (y - start.y()) / (end.y() - start.y());
        intersections.append(start.x() + (t * (end.x() - start.x())));
    }

    std::sort(intersections.begin(), intersections.end());

    QList<SegmentSpan> spans;
    for (int index = 0; index + 1 < intersections.size(); index += 2) {
        const double startX = intersections[index];
        const double endX = intersections[index + 1];
        if ((endX - startX) > kGeometryEpsilon) {
            spans.append({ startX, endX });
        }
    }

    return spans;
}

QList<SegmentSpan> _verticalSpans(const QVector<QPointF> &polygon, double x)
{
    QList<double> intersections;
    for (int index = 0; index < polygon.size(); ++index) {
        const QPointF &start = polygon[index];
        const QPointF &end = polygon[(index + 1) % polygon.size()];
        if (std::abs(start.x() - end.x()) <= kGeometryEpsilon) {
            continue;
        }

        const bool crosses = ((start.x() <= x) && (end.x() > x)) || ((end.x() <= x) && (start.x() > x));
        if (!crosses) {
            continue;
        }

        const double t = (x - start.x()) / (end.x() - start.x());
        intersections.append(start.y() + (t * (end.y() - start.y())));
    }

    std::sort(intersections.begin(), intersections.end());

    QList<SegmentSpan> spans;
    for (int index = 0; index + 1 < intersections.size(); index += 2) {
        const double startY = intersections[index];
        const double endY = intersections[index + 1];
        if ((endY - startY) > kGeometryEpsilon) {
            spans.append({ startY, endY });
        }
    }

    return spans;
}

bool _polygonCentroid(const QVector<QPointF> &polygon, QPointF *centroid)
{
    double twiceArea = 0.0;
    double centroidX = 0.0;
    double centroidY = 0.0;

    for (int index = 0; index < polygon.size(); ++index) {
        const QPointF &current = polygon[index];
        const QPointF &next = polygon[(index + 1) % polygon.size()];
        const double cross = (current.x() * next.y()) - (next.x() * current.y());
        twiceArea += cross;
        centroidX += (current.x() + next.x()) * cross;
        centroidY += (current.y() + next.y()) * cross;
    }

    if (std::abs(twiceArea) <= kGeometryEpsilon) {
        return false;
    }

    *centroid = QPointF(centroidX / (3.0 * twiceArea), centroidY / (3.0 * twiceArea));
    return true;
}

QGeoCoordinate _findInteriorPoint(const QVariantList &polygon)
{
    if (polygon.isEmpty()) {
        return QGeoCoordinate();
    }

    const LocalFrame frame = _buildLocalFrame(polygon);
    const QVector<QPointF> localPolygon = _toLocalPolygon(polygon, frame);
    QVector<QPointF> candidates;

    QPointF centroid;
    if (_polygonCentroid(localPolygon, &centroid)) {
        candidates.append(centroid);
    }

    QPointF average;
    for (const QPointF &point : localPolygon) {
        average += point;
    }
    average /= localPolygon.size();
    candidates.append(average);

    QRectF bounds;
    for (const QPointF &point : localPolygon) {
        bounds |= QRectF(point, QSizeF(0.0, 0.0));
    }
    candidates.append(bounds.center());

    for (int index = 0; index < localPolygon.size(); ++index) {
        candidates.append((localPolygon[index] + localPolygon[(index + 1) % localPolygon.size()]) / 2.0);
    }

    for (const QPointF &candidate : candidates) {
        if (_pointInPolygon(candidate, localPolygon)) {
            return _toGeoCoordinate(candidate, frame, 0.0);
        }
    }

    return polygon.first().value<QGeoCoordinate>();
}

double _polygonMaxRadius(const QVector<QPointF> &polygon)
{
    double maxRadius = 0.0;
    for (const QPointF &point : polygon) {
        maxRadius = qMax(maxRadius, std::hypot(point.x(), point.y()));
    }

    return maxRadius;
}

void _appendWaypoint(QVariantList &waypoints, const QGeoCoordinate &coordinate)
{
    if (waypoints.isEmpty()) {
        waypoints.append(QVariant::fromValue(coordinate));
        return;
    }

    const QGeoCoordinate lastCoordinate = waypoints.last().value<QGeoCoordinate>();
    if (lastCoordinate.isValid() && lastCoordinate.distanceTo(coordinate) <= kGeometryEpsilon) {
        return;
    }

    waypoints.append(QVariant::fromValue(coordinate));
}

QList<SARZone *> _zonesForVehicle(SARZoneManager *zoneManager, int vehicleId)
{
    QList<SARZone *> matchingZones;
    if (!zoneManager || vehicleId < 0) {
        return matchingZones;
    }

    QmlObjectListModel *zones = zoneManager->zones();
    for (int index = 0; index < zones->count(); ++index) {
        auto *zone = qobject_cast<SARZone *>(zones->get(index));
        if (zone && zone->assignedVehicle() == vehicleId) {
            matchingZones.append(zone);
        }
    }

    return matchingZones;
}

QList<Vehicle *> _vehiclesForIds(const QSet<int> &vehicleIds)
{
    QList<Vehicle *> vehicles;
    auto *vehicleManager = MultiVehicleManager::instance();
    for (int vehicleId : vehicleIds) {
        if (Vehicle *vehicle = vehicleManager->getVehicleById(vehicleId)) {
            vehicles.append(vehicle);
        }
    }

    return vehicles;
}

QList<Vehicle *> _assignedVehicles(SARZoneManager *zoneManager)
{
    QSet<int> vehicleIds;
    if (!zoneManager) {
        return {};
    }

    QmlObjectListModel *zones = zoneManager->zones();
    for (int index = 0; index < zones->count(); ++index) {
        auto *zone = qobject_cast<SARZone *>(zones->get(index));
        if (zone && zone->assignedVehicle() >= 0) {
            vehicleIds.insert(zone->assignedVehicle());
        }
    }

    return _vehiclesForIds(vehicleIds);
}

QGeoCoordinate _firstTransectPoint(const QList<SARZone *> &zones)
{
    for (SARZone *zone : zones) {
        if (zone && !zone->transectPath().isEmpty()) {
            return zone->transectPath().first().value<QGeoCoordinate>();
        }
    }

    return QGeoCoordinate();
}

} // namespace

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

    // Recovery safety timer — forces advancement if vehicles don't land/disarm within 30s
    _recoverySafetyTimer = new QTimer(this);
    _recoverySafetyTimer->setSingleShot(true);
    _recoverySafetyTimer->setInterval(30000);
    connect(_recoverySafetyTimer, &QTimer::timeout, this, &SARMissionManager::_recoverySafetyTimeout);
}

SARMissionManager::~SARMissionManager()
{
}

void SARMissionManager::setCurrentPattern(SearchPattern pattern) { if (_currentPattern != pattern) { _currentPattern = pattern; emit patternChanged(); } }
void SARMissionManager::setSearchAltitude(double altitude) { if (!qFuzzyCompare(_searchAltitude, altitude)) { _searchAltitude = altitude; emit altitudeChanged(); } }
void SARMissionManager::setSearchSpeed(double speed)
{
    if (qFuzzyCompare(_searchSpeed, speed)) {
        return;
    }

    _searchSpeed = speed;
    emit speedChanged();

    if (_missionActive) {
        qCWarning(SARMissionManagerLog) << "Ignoring live SAR speed change during active mission; new speed will apply to future mission uploads:" << speed << "m/s";
    }
}
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
    _pendingUploadManagers.clear();
    _uploadSuccessCount = 0;
    _uploadFailCount = 0;

    QHash<int, QList<SARZone *>> vehicleZones;
    QmlObjectListModel *zones = _zoneManager->zones();
    for (int i = 0; i < zones->count(); i++) {
        auto *zone = qobject_cast<SARZone *>(zones->get(i));
        if (!zone || zone->assignedVehicle() < 0 || zone->transectPath().isEmpty()) {
            continue;
        }

        Vehicle *vehicle = MultiVehicleManager::instance()->getVehicleById(zone->assignedVehicle());
        if (!vehicle) {
            qCWarning(SARMissionManagerLog) << "Vehicle" << zone->assignedVehicle() << "not found — skipping zone" << zone->zoneId();
            continue;
        }

        vehicleZones[vehicle->id()].append(zone);
    }

    for (auto it = vehicleZones.constBegin(); it != vehicleZones.constEnd(); ++it) {
        Vehicle *vehicle = MultiVehicleManager::instance()->getVehicleById(it.key());
        if (!vehicle || it.value().isEmpty()) {
            continue;
        }

        _pendingUploads.insert(vehicle->id(), false);
        _uploadMissionToVehicle(vehicle, it.value());
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

    const QList<Vehicle *> vehicles =
        _missionInProgressVehicles.isEmpty() ? _assignedVehicles(_zoneManager) : _vehiclesForIds(_missionInProgressVehicles);

    for (Vehicle *vehicle : vehicles) {
        if (vehicle) {
            vehicle->pauseVehicle();
        }
    }

    qCDebug(SARMissionManagerLog) << "SAR operation paused";
}

void SARMissionManager::resumeOperation()
{
    if (!_missionActive || !_paused) return;

    _paused = false;
    emit pausedChanged();

    const QList<Vehicle *> vehicles =
        _missionInProgressVehicles.isEmpty() ? _assignedVehicles(_zoneManager) : _vehiclesForIds(_missionInProgressVehicles);

    for (Vehicle *vehicle : vehicles) {
        if (!vehicle) {
            continue;
        }

        MissionManager *missionManager = vehicle->missionManager();
        const int resumeIndex = missionManager ? qMax(missionManager->currentIndex(), missionManager->lastCurrentIndex()) : -1;
        if (resumeIndex > 0) {
            vehicle->setCurrentMissionSequence(resumeIndex);
        }

        vehicle->startMission();
    }

    qCDebug(SARMissionManagerLog) << "SAR operation resumed";
}

void SARMissionManager::abortOperation()
{
    if (_abortPhase != AbortIdle) {
        qCWarning(SARMissionManagerLog) << "Abort already in progress";
        return;
    }

    // Disconnect mission-completion monitoring — we're aborting, not completing
    _disconnectMissionCompleteSignals();

    _missionActive = false;
    if (_paused) {
        _paused = false;
        emit pausedChanged();
    }
    _phase = Recovery;
    emit missionActiveChanged();
    emit phaseChanged();

    _suppressMissionCompleteDialog = true;
    emit suppressMissionCompleteDialogChanged();

    qCDebug(SARMissionManagerLog) << "SAR operation abort initiated — RTL all vehicles";
    _abortPhase_landVehicles();
}

void SARMissionManager::advancePhase()
{
    switch (_phase) {
    case Planning:      _phase = Briefing; break;
    case Briefing:
    case Deploying:
        qCWarning(SARMissionManagerLog) << "Operational deployment transitions must be driven by mission upload/start flow";
        return;
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
    const int resolvedPattern = (pattern >= 0)
        ? _resolvedSearchPatternValue(pattern, static_cast<int>(_currentPattern))
        : static_cast<int>(_currentPattern);
    const SearchPattern pat = static_cast<SearchPattern>(resolvedPattern);

    switch (pat) {
    case ParallelTrack:
        return _generateParallelTrack(polygon, _trackSpacing, _searchAltitude);
    case CreepingLine:
        return _generateCreepingLine(polygon, _trackSpacing, _searchAltitude);
    case ExpandingSquare:
        if (!polygon.isEmpty()) {
            const QGeoCoordinate center = _findInteriorPoint(polygon);
            double maxDist = 0;
            for (const QVariant &v : polygon) {
                double d = center.distanceTo(v.value<QGeoCoordinate>());
                if (d > maxDist) maxDist = d;
            }
            return _generateExpandingSquare(polygon, center, maxDist, _trackSpacing, _searchAltitude);
        }
        return QVariantList();
    case SectorSearch:
        return _generateSectorSearch(polygon, _trackSpacing, _searchAltitude);
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
    patternInt = _resolvedSearchPatternValue(patternInt, static_cast<int>(_currentPattern));
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
            QGeoCoordinate center = _findInteriorPoint(polygon);
            double maxDist = 0;
            for (const QVariant &v : polygon) {
                double d = center.distanceTo(v.value<QGeoCoordinate>());
                if (d > maxDist) maxDist = d;
            }
            waypoints = _generateExpandingSquare(polygon, center, maxDist, spacing, altitude);
        }
        break;
    case SectorSearch:
        waypoints = _generateSectorSearch(polygon, spacing, altitude);
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
    emit readinessChanged();
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

void SARMissionManager::clearAllVehicleMissions()
{
    auto *mvm = MultiVehicleManager::instance();
    QmlObjectListModel *vehicles = mvm->vehicles();

    for (int i = 0; i < vehicles->count(); i++) {
        auto *v = qobject_cast<Vehicle *>(vehicles->get(i));
        if (v) {
            v->missionManager()->removeAll();
            qCDebug(SARMissionManagerLog) << "Cleared mission on Vehicle" << v->id();
        }
    }
}

bool SARMissionManager::deployZoneMission(int zoneId)
{
    if (!_zoneManager) {
        return false;
    }

    SARZone *zone = _zoneManager->getZone(zoneId);
    if (!zone || zone->assignedVehicle() < 0) {
        return false;
    }

    if (zone->transectPath().isEmpty()) {
        generateZoneTransect(zoneId);
    }
    if (zone->transectPath().isEmpty()) {
        qCWarning(SARMissionManagerLog) << "Cannot deploy empty zone mission for zone" << zoneId;
        return false;
    }

    Vehicle *vehicle = MultiVehicleManager::instance()->getVehicleById(zone->assignedVehicle());
    MissionManager *missionManager = vehicle ? vehicle->missionManager() : nullptr;
    if (!vehicle || !missionManager || missionManager->inProgress()) {
        qCWarning(SARMissionManagerLog) << "Cannot deploy zone mission for zone" << zoneId << "- vehicle unavailable or busy";
        return false;
    }

    connect(missionManager, &PlanManager::sendComplete, this,
            [this, zoneId, vehicleId = vehicle->id()](bool error) {
                emit missionUploadComplete(vehicleId, !error);
                if (error) {
                    return;
                }

                Vehicle *updatedVehicle = MultiVehicleManager::instance()->getVehicleById(vehicleId);
                if (!updatedVehicle) {
                    return;
                }

                _missionInProgressVehicles.insert(vehicleId);
                connect(updatedVehicle, &Vehicle::flightModeChanged, this, &SARMissionManager::_onVehicleFlightModeChanged, Qt::UniqueConnection);
                _configureRCLossExemption(updatedVehicle);
                updatedVehicle->startMission();

                if (_zoneManager) {
                    if (SARZone *updatedZone = _zoneManager->getZone(zoneId)) {
                        updatedZone->setStatus(SARZone::Active);
                    }
                }
            },
            Qt::SingleShotConnection);

    _uploadMissionToVehicle(vehicle, { zone });
    return true;
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

    // ── Spatially-aware greedy assignment ──
    // Assign each vehicle to the nearest unassigned zone, minimising transit
    // distance.  When there are more zones than vehicles, the extra zones are
    // assigned to the nearest already-assigned vehicle (load balancing).

    QmlObjectListModel *zones = _zoneManager->zones();

    // Collect unassigned zone IDs and their centroids
    struct ZoneInfo { int zoneId; QGeoCoordinate centroid; };
    QList<ZoneInfo> unassigned;
    for (int i = 0; i < zones->count(); i++) {
        auto *zone = qobject_cast<SARZone *>(zones->get(i));
        if (!zone || zone->assignedVehicle() >= 0)
            continue;

        QVariantList poly = zone->polygon();
        if (poly.isEmpty()) continue;

        double latSum = 0.0, lonSum = 0.0;
        for (const QVariant &v : poly) {
            QGeoCoordinate c = v.value<QGeoCoordinate>();
            latSum += c.latitude();
            lonSum += c.longitude();
        }
        unassigned.append({ zone->zoneId(),
                            QGeoCoordinate(latSum / poly.size(), lonSum / poly.size()) });
    }

    if (unassigned.isEmpty()) {
        qCDebug(SARMissionManagerLog) << "autoAssignZones: no unassigned zones";
        return;
    }

    // Collect vehicle positions
    struct VehicleInfo { int id; QGeoCoordinate pos; };
    QList<VehicleInfo> vehiclePool;
    vehiclePool.reserve(vehicleIds.size());
    for (int vid : vehicleIds) {
        Vehicle *v = mvm->getVehicleById(vid);
        QGeoCoordinate pos;
        if (v) pos = v->coordinate();
        if (!pos.isValid()) {
            // Fallback: use home position or a default
            if (v) pos = v->homePosition();
        }
        vehiclePool.append({ vid, pos });
    }

    // Phase 1: Greedy nearest-centroid matching (one zone per vehicle)
    QSet<int> assignedVehicleIds;
    QSet<int> assignedZoneIdxs;

    int assignRounds = qMin(vehiclePool.size(), unassigned.size());
    for (int round = 0; round < assignRounds; round++) {
        double bestDist = std::numeric_limits<double>::max();
        int bestVIdx = -1, bestZIdx = -1;

        for (int vi = 0; vi < vehiclePool.size(); vi++) {
            if (assignedVehicleIds.contains(vi)) continue;
            for (int zi = 0; zi < unassigned.size(); zi++) {
                if (assignedZoneIdxs.contains(zi)) continue;

                double dist;
                if (vehiclePool[vi].pos.isValid()) {
                    dist = vehiclePool[vi].pos.distanceTo(unassigned[zi].centroid);
                } else {
                    // No position info — use index-based ordering (fallback)
                    dist = static_cast<double>(std::abs(vi - zi)) * 1e6;
                }

                if (dist < bestDist) {
                    bestDist = dist;
                    bestVIdx = vi;
                    bestZIdx = zi;
                }
            }
        }

        if (bestVIdx >= 0 && bestZIdx >= 0) {
            _zoneManager->assignZoneToVehicle(unassigned[bestZIdx].zoneId,
                                               vehiclePool[bestVIdx].id);
            assignedVehicleIds.insert(bestVIdx);
            assignedZoneIdxs.insert(bestZIdx);
            qCDebug(SARMissionManagerLog) << "Assigned zone" << unassigned[bestZIdx].zoneId
                                           << "to vehicle" << vehiclePool[bestVIdx].id
                                           << "(dist:" << bestDist << "m)";
        }
    }

    // Phase 2: If more zones than vehicles, assign remaining zones to the
    // nearest already-assigned vehicle (load balancing).
    for (int zi = 0; zi < unassigned.size(); zi++) {
        if (assignedZoneIdxs.contains(zi)) continue;

        double bestDist = std::numeric_limits<double>::max();
        int bestVehicleId = vehicleIds[0]; // fallback

        for (int vi = 0; vi < vehiclePool.size(); vi++) {
            double dist;
            if (vehiclePool[vi].pos.isValid()) {
                dist = vehiclePool[vi].pos.distanceTo(unassigned[zi].centroid);
            } else {
                dist = static_cast<double>(vi) * 1e6;
            }
            if (dist < bestDist) {
                bestDist = dist;
                bestVehicleId = vehiclePool[vi].id;
            }
        }

        _zoneManager->assignZoneToVehicle(unassigned[zi].zoneId, bestVehicleId);
        qCDebug(SARMissionManagerLog) << "Assigned extra zone" << unassigned[zi].zoneId
                                       << "to vehicle" << bestVehicleId
                                       << "(dist:" << bestDist << "m)";
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

    const LocalFrame frame = _buildLocalFrame(polygon);
    const QVector<QPointF> localPolygon = _toLocalPolygon(polygon, frame);

    double minY = std::numeric_limits<double>::max();
    double maxY = std::numeric_limits<double>::lowest();
    for (const QPointF &point : localPolygon) {
        minY = qMin(minY, point.y());
        maxY = qMax(maxY, point.y());
    }

    const double lineSpacing = qMax(1.0, spacing);
    bool leftToRight = true;
    for (double y = maxY; y >= minY - kGeometryEpsilon; y -= lineSpacing) {
        QList<SegmentSpan> spans = _horizontalSpans(localPolygon, y);
        if (spans.isEmpty()) {
            continue;
        }

        if (!leftToRight) {
            std::reverse(spans.begin(), spans.end());
        }

        for (const SegmentSpan &span : spans) {
            const QPointF start = leftToRight ? QPointF(span.start, y) : QPointF(span.end, y);
            const QPointF end = leftToRight ? QPointF(span.end, y) : QPointF(span.start, y);
            _appendWaypoint(waypoints, _toGeoCoordinate(start, frame, altitude));
            _appendWaypoint(waypoints, _toGeoCoordinate(end, frame, altitude));
        }

        leftToRight = !leftToRight;
    }

    return waypoints;
}

QVariantList SARMissionManager::_generateCreepingLine(const QVariantList &polygon, double spacing, double altitude) const
{
    QVariantList waypoints;
    if (polygon.size() < 3) return waypoints;

    const LocalFrame frame = _buildLocalFrame(polygon);
    const QVector<QPointF> localPolygon = _toLocalPolygon(polygon, frame);

    double minX = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    for (const QPointF &point : localPolygon) {
        minX = qMin(minX, point.x());
        maxX = qMax(maxX, point.x());
    }

    const double lineSpacing = qMax(1.0, spacing);
    bool bottomToTop = true;
    for (double x = minX; x <= maxX + kGeometryEpsilon; x += lineSpacing) {
        QList<SegmentSpan> spans = _verticalSpans(localPolygon, x);
        if (spans.isEmpty()) {
            continue;
        }

        if (!bottomToTop) {
            std::reverse(spans.begin(), spans.end());
        }

        for (const SegmentSpan &span : spans) {
            const QPointF start = bottomToTop ? QPointF(x, span.start) : QPointF(x, span.end);
            const QPointF end = bottomToTop ? QPointF(x, span.end) : QPointF(x, span.start);
            _appendWaypoint(waypoints, _toGeoCoordinate(start, frame, altitude));
            _appendWaypoint(waypoints, _toGeoCoordinate(end, frame, altitude));
        }

        bottomToTop = !bottomToTop;
    }

    return waypoints;
}

QVariantList SARMissionManager::_generateExpandingSquare(const QVariantList &polygon, const QGeoCoordinate &center, double maxRadius, double spacing, double altitude) const
{
    QVariantList waypoints;
    if (polygon.size() < 3 || !center.isValid()) {
        return waypoints;
    }
    const LocalFrame polygonFrame = _buildLocalFrame(polygon);
    const QVector<QPointF> polygonLocal = _toLocalPolygon(polygon, polygonFrame);
    const QPointF centerLocal = _toLocalPoint(center, polygonFrame);
    if (!_pointInPolygon(centerLocal, polygonLocal)) {
        return waypoints;
    }

    const double boundedRadius = qMax(qMax(1.0, spacing), maxRadius);
    _appendWaypoint(waypoints, QGeoCoordinate(center.latitude(), center.longitude(), altitude));

    double legLength = qMax(1.0, spacing);
    int direction = 0; // 0=N, 1=E, 2=S, 3=W
    QPointF currentPoint = centerLocal;

    while (legLength <= boundedRadius + kGeometryEpsilon) {
        for (int pair = 0; pair < 2 && legLength <= boundedRadius + kGeometryEpsilon; pair++) {
            QPointF target = currentPoint;
            switch (direction % 4) {
            case 0: target.ry() += legLength; break;
            case 1: target.rx() += legLength; break;
            case 2: target.ry() -= legLength; break;
            case 3: target.rx() -= legLength; break;
            }

            QPointF clippedEnd;
            if (_clipSegmentFromStart(currentPoint, target, polygonLocal, &clippedEnd) && !_pointsEqual(currentPoint, clippedEnd)) {
                _appendWaypoint(waypoints, _toGeoCoordinate(clippedEnd, polygonFrame, altitude));
                currentPoint = clippedEnd;
            }

            direction++;
            if (pair == 1) {
                legLength += qMax(1.0, spacing);
            }
        }
    }

    return waypoints;
}

QVariantList SARMissionManager::_generateSectorSearch(const QVariantList &polygon, double spacing, double altitude) const
{
    QVariantList waypoints;
    if (polygon.size() < 3) return waypoints;

    // ── Helper: 2D ray–segment intersection ──
    // Returns the parameter t along the ray (origin + t*dir) at which it
    // hits the segment (p1→p2), or -1 if no hit.  We work in a local
    // (dlat, dlon) frame so the maths stays simple.
    struct Vec2 { double x, y; };
    auto raySegIntersect = [](Vec2 o, Vec2 d, Vec2 p1, Vec2 p2, double &t) -> bool {
        Vec2 s = { p2.x - p1.x, p2.y - p1.y };
        double denom = d.x * s.y - d.y * s.x;
        if (std::abs(denom) < 1e-12) return false;          // parallel
        double u = ((p1.x - o.x) * s.y - (p1.y - o.y) * s.x) / denom;
        double v = ((p1.x - o.x) * d.y - (p1.y - o.y) * d.x) / denom;
        if (u > 1e-9 && v >= 0.0 && v <= 1.0) {             // t > 0 (forward ray)
            t = u;
            return true;
        }
        return false;
    };

    const QGeoCoordinate center = _findInteriorPoint(polygon);
    const double cLat = center.latitude();
    const double cLon = center.longitude();

    const double metersPerDegreeLat = 111320.0;
    const double metersPerDegreeLon = 111320.0 * std::cos(cLat * M_PI / 180.0);

    // Build polygon vertices in a local metre-offset frame centred on the centroid
    QVector<Vec2> polyPts;
    polyPts.reserve(polygon.size());
    for (const QVariant &v : polygon) {
        QGeoCoordinate c = v.value<QGeoCoordinate>();
        polyPts.append({ (c.latitude()  - cLat) * metersPerDegreeLat,
                         (c.longitude() - cLon) * metersPerDegreeLon });
    }

    // Helper: cast a ray from the centroid (0,0) at a given angle and return
    // the distance to the nearest polygon edge intersection.
    auto rayDistToBoundary = [&](double angle) -> double {
        Vec2 origin = { 0, 0 };
        Vec2 dir    = { std::cos(angle), std::sin(angle) };
        double bestT = std::numeric_limits<double>::max();
        for (int i = 0; i < polyPts.size(); i++) {
            int j = (i + 1) % polyPts.size();
            double t;
            if (raySegIntersect(origin, dir, polyPts[i], polyPts[j], t)) {
                if (t < bestT) bestT = t;
            }
        }
        return (bestT < std::numeric_limits<double>::max()) ? bestT : 0.0;
    };

    // ── Generate 3 sectors (120 deg each) from centroid, clipped to polygon ──
    const int numSectors = 3;
    const double sectorAngle = 2.0 * M_PI / numSectors;
    double maxRadius = 0.0;
    for (const Vec2 &point : polyPts) {
        maxRadius = qMax(maxRadius, std::hypot(point.x, point.y));
    }
    const int arcPoints = qMax(4, static_cast<int>(std::ceil(maxRadius / qMax(1.0, spacing))));

    for (int s = 0; s < numSectors; s++) {
        double angle1 = s * sectorAngle;
        double angle2 = angle1 + sectorAngle;

        // Back to centroid
        _appendWaypoint(waypoints, QGeoCoordinate(cLat, cLon, altitude));

        // First radial – clipped to polygon boundary
        double r1 = rayDistToBoundary(angle1);
        double lat1 = cLat + (r1 * std::cos(angle1)) / metersPerDegreeLat;
        double lon1 = cLon + (r1 * std::sin(angle1)) / metersPerDegreeLon;
        _appendWaypoint(waypoints, QGeoCoordinate(lat1, lon1, altitude));

        // Arc points along the sector edge, each clipped individually
        for (int a = 1; a <= arcPoints; a++) {
            double angle = angle1 + (angle2 - angle1) * a / arcPoints;
            double r = rayDistToBoundary(angle);
            double lat = cLat + (r * std::cos(angle)) / metersPerDegreeLat;
            double lon = cLon + (r * std::sin(angle)) / metersPerDegreeLon;
            _appendWaypoint(waypoints, QGeoCoordinate(lat, lon, altitude));
        }
    }

    // Return to centroid
    _appendWaypoint(waypoints, QGeoCoordinate(cLat, cLon, altitude));

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
    if (!v) {
        return false;
    }

    VehicleLinkManager *linkManager = v->vehicleLinkManager();
    return linkManager && !linkManager->communicationLost() && !linkManager->primaryLink().expired();
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

void SARMissionManager::_uploadMissionToVehicle(Vehicle *vehicle, const QList<SARZone *> &zones)
{
    QList<MissionItem *> missionItems;
    int seqNum = 0;
    if (!vehicle || zones.isEmpty()) {
        return;
    }

    // Item 0: Home position (required as sequence 0 by MAVLink mission protocol)
    QGeoCoordinate home = vehicle->homePosition();
    if (!home.isValid()) {
        // Fallback: use first transect waypoint as approximate home
        home = _firstTransectPoint(zones);
        if (!home.isValid()) {
            qCWarning(SARMissionManagerLog) << "Cannot upload mission for Vehicle" << vehicle->id() << "- no valid home or transect point";
            return;
        }
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
        nullptr));

    // Item 1: Takeoff to the search altitude
    double takeoffAltitude = _searchAltitude;
    for (SARZone *zone : zones) {
        if (!zone) {
            continue;
        }

        const double zoneAltitude = (zone->useGlobalParams() || zone->searchAltitude() < 0)
            ? _searchAltitude
            : zone->searchAltitude();
        takeoffAltitude = qMax(takeoffAltitude, zoneAltitude);
    }
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
        takeoffAltitude,
        true,
        false,
        nullptr));

    bool hasTransectWaypoint = false;
    for (SARZone *zone : zones) {
        if (!zone) {
            continue;
        }

        const double zoneAltitude = (zone->useGlobalParams() || zone->searchAltitude() < 0)
            ? _searchAltitude
            : zone->searchAltitude();
        const double zoneSpeed = (zone->useGlobalParams() || zone->searchSpeed() < 0)
            ? _searchSpeed
            : zone->searchSpeed();

        missionItems.append(new MissionItem(
            seqNum++,
            MAV_CMD_DO_CHANGE_SPEED,
            MAV_FRAME_MISSION,
            vehicle->multiRotor() ? 1 : 0,
            static_cast<float>(zoneSpeed),
            -1,
            0,
            0, 0, 0,
            true,
            false,
            nullptr));

        for (const QVariant &wp : zone->transectPath()) {
            const QGeoCoordinate coord = wp.value<QGeoCoordinate>();
            const double wpAlt = coord.altitude() > 0 ? coord.altitude() : zoneAltitude;
            missionItems.append(new MissionItem(
                seqNum++,
                MAV_CMD_NAV_WAYPOINT,
                MAV_FRAME_GLOBAL_RELATIVE_ALT,
                0,
                0,
                0,
                std::numeric_limits<double>::quiet_NaN(),
                coord.latitude(),
                coord.longitude(),
                wpAlt,
                true,
                false,
                nullptr));
            hasTransectWaypoint = true;
        }
    }

    if (!hasTransectWaypoint) {
        qCWarning(SARMissionManagerLog) << "Skipping mission upload for Vehicle" << vehicle->id() << "- no transect waypoints";
        qDeleteAll(missionItems);
        return;
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
        nullptr));

    qCDebug(SARMissionManagerLog) << "Uploading" << missionItems.size() << "mission items to Vehicle"
                                   << vehicle->id() << "for" << zones.size() << "zone(s)";

    // Connect upload progress and completion signals.
    // Disconnect first to prevent stacked connections if retrying the same vehicle.
    PlanManager *pm = vehicle->missionManager();

    disconnect(pm, &PlanManager::progressPctChanged, this, nullptr);
    disconnect(pm, &PlanManager::sendComplete, this, &SARMissionManager::_onUploadComplete);

    connect(pm, &PlanManager::progressPctChanged, this, [this, vehicle](double pct) {
        emit missionUploadProgress(vehicle->id(), pct);
    });

    if (_pendingUploads.contains(vehicle->id())) {
        _pendingUploadManagers.insert(pm, vehicle->id());
        connect(pm, &PlanManager::sendComplete, this, &SARMissionManager::_onUploadComplete);
    }

    // PlanManager takes ownership of the MissionItem objects
    pm->writeMissionItems(missionItems);
}

void SARMissionManager::_onUploadComplete(bool error)
{
    auto *pm = qobject_cast<PlanManager *>(sender());
    if (!pm) return;

    const int vehicleId = _pendingUploadManagers.contains(pm) ? _pendingUploadManagers.take(pm) : -1;

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
    _missionInProgressVehicles.clear();

    for (auto it = _pendingUploads.constBegin(); it != _pendingUploads.constEnd(); ++it) {
        Vehicle *v = MultiVehicleManager::instance()->getVehicleById(it.key());
        if (v) {
            // Track this vehicle for mission-completion monitoring
            _missionInProgressVehicles.insert(v->id());

            // Monitor flight mode changes to detect when mission ends
            connect(v, &Vehicle::flightModeChanged, this, &SARMissionManager::_onVehicleFlightModeChanged, Qt::UniqueConnection);

            // For PX4 multi-vehicle: exempt Mission mode from RC loss failsafe.
            // QGC sends MANUAL_CONTROL only to the active vehicle, so non-active
            // vehicles trigger RC loss failsafe and go to Hold/RTL.
            // COM_RCL_EXCEPT bit 0 = Mission mode exempt from RC loss.
            _configureRCLossExemption(v);

            // startMission() handles: set Mission mode → arm → auto-takeoff (PX4)
            // or: set Auto mode → arm → MAV_CMD_MISSION_START (ArduPilot)
            v->startMission();
            qCDebug(SARMissionManagerLog) << "Started mission on Vehicle" << v->id();

            // Mark the zone as Active
            if (_zoneManager) {
                const QList<SARZone *> zones = _zonesForVehicle(_zoneManager, v->id());
                for (SARZone *zone : zones) {
                    if (zone && zone->status() != SARZone::Completed) {
                        zone->setStatus(SARZone::Active);
                    }
                }
            }
        }
    }

    _phase = Searching;
    _pendingUploads.clear();
    _pendingUploadManagers.clear();
    _uploadSuccessCount = 0;
    _uploadFailCount = 0;
    emit phaseChanged();
    emit deploymentComplete();
    qCDebug(SARMissionManagerLog) << "Deployment complete — entering Searching phase ("
                                   << _missionInProgressVehicles.size() << "vehicles tracked)";
}

void SARMissionManager::_configureRCLossExemption(Vehicle *v)
{
    if (!v || !v->parameterManager()) return;

    // PX4: COM_RCL_EXCEPT — bitmask of modes exempt from RC loss failsafe
    //   bit 0 (1) = Mission, bit 1 (2) = Hold, bit 2 (4) = Offboard
    const QString paramName = QStringLiteral("COM_RCL_EXCEPT");

    if (v->parameterManager()->parameterExists(-1, paramName)) {
        Fact *param = v->parameterManager()->getParameter(-1, paramName);
        if (param) {
            int current = param->rawValue().toInt();
            if (!(current & 0x01)) {
                param->setRawValue(current | 0x01);  // Set bit 0: Mission mode exempt
                qCDebug(SARMissionManagerLog) << "V" << v->id()
                    << "COM_RCL_EXCEPT set to" << (current | 0x01)
                    << "(Mission mode exempt from RC loss)";
            }
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Mission-completion monitoring ──
// ══════════════════════════════════════════════════════════════════════════════

void SARMissionManager::_onVehicleFlightModeChanged(const QString &flightMode)
{
    if (_phase != Searching && _phase != Investigating) return;

    auto *v = qobject_cast<Vehicle *>(sender());
    if (!v || !_missionInProgressVehicles.contains(v->id())) return;

    // Get the firmware-specific mode names for comparison
    FirmwarePlugin *fw = v->firmwarePlugin();
    if (!fw) return;

    QString missionMode = fw->missionFlightMode();
    QString rtlMode     = fw->rtlFlightMode();
    QString pauseMode   = fw->pauseFlightMode();

    // A vehicle leaving Mission mode can mean:
    //  - RTL: mission reached the RTL item at the end → expected completion path
    //  - Pause/Hold: operator paused, or PX4 transitioned after mission finished + RTL landed
    //  - Other: unexpected mode change
    //
    // We treat exiting Mission mode as "mission complete" ONLY if the new mode
    // is RTL (reaching the final mission item) or pause/hold (PX4 auto-loiter
    // after the mission sequence is exhausted).
    if (flightMode == missionMode) {
        // Still in Mission mode — not done yet
        return;
    }

    if (_paused && flightMode == pauseMode) {
        qCDebug(SARMissionManagerLog) << "Vehicle" << v->id() << "entered pause mode while SAR operation is paused";
        return;
    }

    if (flightMode == rtlMode || (!_paused && flightMode == pauseMode)) {
        _missionInProgressVehicles.remove(v->id());
        disconnect(v, &Vehicle::flightModeChanged, this, &SARMissionManager::_onVehicleFlightModeChanged);

        // Mark the zone as Completed
        if (_zoneManager) {
            const QList<SARZone *> zones = _zonesForVehicle(_zoneManager, v->id());
            for (SARZone *zone : zones) {
                if (zone && zone->status() == SARZone::Active) {
                    zone->setStatus(SARZone::Completed);
                    zone->setProgress(1.0);
                }
            }
        }

        qCDebug(SARMissionManagerLog) << "Vehicle" << v->id() << "mission completed (mode:" << flightMode
                                       << ") —" << _missionInProgressVehicles.size() << "still in progress";

        _checkAllMissionsComplete();
    } else {
        qCWarning(SARMissionManagerLog) << "Vehicle" << v->id()
                                         << "unexpectedly switched to mode:" << flightMode
                                         << "during active SAR mission";
    }
}

void SARMissionManager::_checkAllMissionsComplete()
{
    if (!_missionInProgressVehicles.isEmpty()) return;

    qCDebug(SARMissionManagerLog) << "All vehicle missions completed — landing and disarming all vehicles";

    _phase = Recovery;
    emit phaseChanged();

    // Trigger the recovery sequence: land → disarm all vehicles
    _recoveryPhase_landVehicles();
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Recovery (normal completion) land & disarm sequence ──
// ══════════════════════════════════════════════════════════════════════════════

void SARMissionManager::_recoveryPhase_landVehicles()
{
    _suppressMissionCompleteDialog = true;
    emit suppressMissionCompleteDialogChanged();

    _recoveryPendingVehicles.clear();

    for (Vehicle *vehicle : _assignedVehicles(_zoneManager)) {
        if (vehicle && vehicle->flying()) {
            _recoveryPendingVehicles.insert(vehicle->id());
            connect(vehicle, &Vehicle::flyingChanged, this, &SARMissionManager::_onVehicleLandedForRecovery, Qt::UniqueConnection);
            vehicle->guidedModeRTL(false);
            qCDebug(SARMissionManagerLog) << "Recovery: commanding Vehicle" << vehicle->id() << "to RTL";
        }
    }

    if (_recoveryPendingVehicles.isEmpty()) {
        qCDebug(SARMissionManagerLog) << "Recovery: no flying vehicles — proceeding to disarm";
        _recoveryPhase_disarmVehicles();
    } else {
        _recoverySafetyTimer->start();
    }
}

void SARMissionManager::_onVehicleLandedForRecovery()
{
    auto *v = qobject_cast<Vehicle *>(sender());
    if (!v) return;

    if (!v->flying()) {
        _recoveryPendingVehicles.remove(v->id());
        disconnect(v, &Vehicle::flyingChanged, this, &SARMissionManager::_onVehicleLandedForRecovery);
        qCDebug(SARMissionManagerLog) << "Recovery: Vehicle" << v->id() << "has landed ("
                                       << _recoveryPendingVehicles.size() << "remaining)";

        if (_recoveryPendingVehicles.isEmpty()) {
            _recoverySafetyTimer->stop();
            _recoveryPhase_disarmVehicles();
        }
    }
}

void SARMissionManager::_recoveryPhase_disarmVehicles()
{
    _recoveryPendingVehicles.clear();

    for (Vehicle *vehicle : _assignedVehicles(_zoneManager)) {
        if (vehicle && vehicle->armed() && !vehicle->flying()) {
            _recoveryPendingVehicles.insert(vehicle->id());
            connect(vehicle, &Vehicle::armedChanged, this, &SARMissionManager::_onVehicleDisarmedForRecovery, Qt::UniqueConnection);
            vehicle->setArmed(false, false);
            qCDebug(SARMissionManagerLog) << "Recovery: disarming Vehicle" << vehicle->id();
        }
    }

    if (_recoveryPendingVehicles.isEmpty()) {
        qCDebug(SARMissionManagerLog) << "Recovery: no armed vehicles — recovery complete";
        _recoveryComplete();
    } else {
        _recoverySafetyTimer->start();
    }
}

void SARMissionManager::_onVehicleDisarmedForRecovery()
{
    auto *v = qobject_cast<Vehicle *>(sender());
    if (!v) return;

    if (!v->armed()) {
        _recoveryPendingVehicles.remove(v->id());
        disconnect(v, &Vehicle::armedChanged, this, &SARMissionManager::_onVehicleDisarmedForRecovery);
        qCDebug(SARMissionManagerLog) << "Recovery: Vehicle" << v->id() << "disarmed ("
                                       << _recoveryPendingVehicles.size() << "remaining)";

        if (_recoveryPendingVehicles.isEmpty()) {
            _recoverySafetyTimer->stop();
            _recoveryComplete();
        }
    }
}

void SARMissionManager::_recoveryComplete()
{
    _phase = Debriefing;
    _missionActive = false;
    emit phaseChanged();
    emit missionActiveChanged();

    _suppressMissionCompleteDialog = false;
    emit suppressMissionCompleteDialogChanged();

    qCDebug(SARMissionManagerLog) << "Recovery complete — all vehicles landed and disarmed";
    emit operationCompleted();
    emit recoveryMissionClearPrompt();
}

void SARMissionManager::_recoverySafetyTimeout()
{
    qCWarning(SARMissionManagerLog) << "Recovery safety timeout — forcing advancement with"
                                     << _recoveryPendingVehicles.size() << "vehicles still pending";

    // Disconnect any remaining vehicle signals
    const QList<Vehicle *> vehicles = _assignedVehicles(_zoneManager);
    for (Vehicle *vehicle : vehicles) {
        if (vehicle && _recoveryPendingVehicles.contains(vehicle->id())) {
            disconnect(vehicle, &Vehicle::flyingChanged, this, &SARMissionManager::_onVehicleLandedForRecovery);
            disconnect(vehicle, &Vehicle::armedChanged, this, &SARMissionManager::_onVehicleDisarmedForRecovery);
        }
    }

    _recoveryPendingVehicles.clear();

    // Determine which phase timed out and advance accordingly
    // Check if any vehicles are still flying — if so, skip to disarm; otherwise complete
    bool anyFlying = false;
    for (Vehicle *vehicle : vehicles) {
        if (vehicle && vehicle->flying()) {
            anyFlying = true;
            break;
        }
    }

    if (anyFlying) {
        // Timed out during land phase — try disarm anyway
        _recoveryPhase_disarmVehicles();
    } else {
        // Timed out during disarm phase — just complete
        _recoveryComplete();
    }
}

void SARMissionManager::_disconnectMissionCompleteSignals()
{
    auto *mvm = MultiVehicleManager::instance();
    QmlObjectListModel *vehicles = mvm->vehicles();

    for (int i = 0; i < vehicles->count(); i++) {
        auto *v = qobject_cast<Vehicle *>(vehicles->get(i));
        if (v) {
            disconnect(v, &Vehicle::flightModeChanged, this, &SARMissionManager::_onVehicleFlightModeChanged);
        }
    }

    _missionInProgressVehicles.clear();
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
            v->guidedModeRTL(false);
            qCDebug(SARMissionManagerLog) << "Abort: commanding Vehicle" << v->id() << "to RTL";
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
    _pendingUploadManagers.clear();
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
    _suppressMissionCompleteDialog = false;
    emit suppressMissionCompleteDialogChanged();

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
