#include "SARZoneManager.h"
#include "QmlObjectListModel.h"
#include "QGCLoggingCategory.h"
#include "QGCMapPolygon.h"
#include "TransectStyleComplexItem.h"
#include "StructureScanComplexItem.h"
#include "SimpleMissionItem.h"
#include "VisualMissionItem.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtGui/QColor>
#include <QtPositioning/QGeoCoordinate>

#include <cmath>
#include <algorithm>

QGC_LOGGING_CATEGORY(SARZoneManagerLog, "Sadron.SARZoneManager")

/*===========================================================================*/
// SARZone
/*===========================================================================*/

SARZone::SARZone(int zoneId, QObject *parent)
    : QObject(parent)
    , _zoneId(zoneId)
    , _name(QStringLiteral("Zone %1").arg(zoneId))
    , _mapPolygon(new QGCMapPolygon(this))
{
    connect(_mapPolygon, &QGCMapPolygon::pathChanged, this, &SARZone::polygonChanged);
}

QVariantList SARZone::polygon() const
{
    return _mapPolygon->path();
}

double SARZone::areaSqM() const
{
    const QVariantList poly = _mapPolygon->path();
    if (poly.size() < 3) return 0.0;

    // Shoelace formula on projected coordinates
    double area = 0.0;
    const int n = poly.size();
    for (int i = 0; i < n; i++) {
        const QGeoCoordinate c1 = poly[i].value<QGeoCoordinate>();
        const QGeoCoordinate c2 = poly[(i + 1) % n].value<QGeoCoordinate>();

        // Convert to meters relative to first point
        const QGeoCoordinate origin = poly[0].value<QGeoCoordinate>();
        const double x1 = origin.distanceTo(QGeoCoordinate(origin.latitude(), c1.longitude()));
        const double y1 = origin.distanceTo(QGeoCoordinate(c1.latitude(), origin.longitude()));
        const double x2 = origin.distanceTo(QGeoCoordinate(origin.latitude(), c2.longitude()));
        const double y2 = origin.distanceTo(QGeoCoordinate(c2.latitude(), origin.longitude()));

        area += (x1 * y2 - x2 * y1);
    }
    return std::abs(area) / 2.0;
}

QColor SARZone::displayColor() const
{
    switch (_status) {
    case Pending:       return QColor("#f1c40f");   // Yellow
    case Active:        return QColor("#2ecc71");   // Green
    case Paused:        return QColor("#e67e22");   // Orange
    case Completed:     return QColor("#3498db");   // Blue
    case Reassigning:   return QColor("#9b59b6");   // Purple
    case Investigating: return QColor("#e74c3c");   // Red — attention: drone diverted
    }
    return QColor("#888888");
}

void SARZone::setName(const QString &name) { if (_name != name) { _name = name; emit nameChanged(); } }
void SARZone::setAssignedVehicle(int vehicleId) { if (_assignedVehicle != vehicleId) { _assignedVehicle = vehicleId; emit assignedVehicleChanged(); } }
void SARZone::setProgress(double progress) { if (!qFuzzyCompare(_progress, progress)) { _progress = qBound(0.0, progress, 1.0); emit progressChanged(); } }
void SARZone::setStatus(ZoneStatus status) { if (_status != status) { _status = status; emit statusChanged(); } }

void SARZone::setPolygon(const QVariantList &polygon)
{
    _mapPolygon->setPath(polygon);
}

void SARZone::setUseGlobalParams(bool useGlobal)
{
    if (_useGlobalParams != useGlobal) {
        _useGlobalParams = useGlobal;
        emit useGlobalParamsChanged();
    }
}

void SARZone::setSearchPattern(int pattern)
{
    if (_searchPattern != pattern) {
        _searchPattern = pattern;
        emit searchPatternChanged();
    }
}

void SARZone::setSearchAltitude(double altitude)
{
    if (!qFuzzyCompare(_searchAltitude, altitude)) {
        _searchAltitude = altitude;
        emit searchAltitudeChanged();
    }
}

void SARZone::setSearchSpeed(double speed)
{
    if (!qFuzzyCompare(_searchSpeed, speed)) {
        _searchSpeed = speed;
        emit searchSpeedChanged();
    }
}

void SARZone::setTrackSpacing(double spacing)
{
    if (!qFuzzyCompare(_trackSpacing, spacing)) {
        _trackSpacing = spacing;
        emit trackSpacingChanged();
    }
}

void SARZone::setTransectPath(const QVariantList &path)
{
    _transectPath = path;
    emit transectPathChanged();
}

void SARZone::setSelected(bool selected)
{
    if (_selected != selected) {
        _selected = selected;
        emit selectedChanged();
    }
}

QJsonObject SARZone::toJson() const
{
    QJsonObject json;
    json["id"] = _zoneId;
    json["name"] = _name;
    json["assignedVehicle"] = _assignedVehicle;
    json["progress"] = _progress;
    json["status"] = static_cast<int>(_status);

    QJsonObject polyJson;
    _mapPolygon->saveToJson(polyJson);
    json["mapPolygon"] = polyJson;

    // Per-zone overrides
    json["useGlobalParams"] = _useGlobalParams;
    json["searchPattern"] = _searchPattern;
    json["searchAltitude"] = _searchAltitude;
    json["searchSpeed"] = _searchSpeed;
    json["trackSpacing"] = _trackSpacing;

    return json;
}

SARZone *SARZone::fromJson(const QJsonObject &json, QObject *parent)
{
    auto *zone = new SARZone(json["id"].toInt(), parent);
    zone->_name = json["name"].toString();
    zone->_assignedVehicle = json["assignedVehicle"].toInt(-1);
    zone->_progress = json["progress"].toDouble(0.0);
    int statusInt = json["status"].toInt(0);
    zone->_status = (statusInt >= 0 && statusInt <= SARZone::Investigating)
        ? static_cast<ZoneStatus>(statusInt) : SARZone::Pending;

    // Per-zone overrides
    zone->_useGlobalParams = json["useGlobalParams"].toBool(true);
    zone->_searchPattern = json["searchPattern"].toInt(-1);
    zone->_searchAltitude = json["searchAltitude"].toDouble(-1.0);
    zone->_searchSpeed = json["searchSpeed"].toDouble(-1.0);
    zone->_trackSpacing = json["trackSpacing"].toDouble(-1.0);

    // Load polygon from QGCMapPolygon JSON format
    if (json.contains("mapPolygon")) {
        QString errorString;
        zone->_mapPolygon->loadFromJson(json["mapPolygon"].toObject(), false, errorString);
    } else if (json.contains("polygon")) {
        // Legacy format: array of {lat, lon} objects
        QVariantList polygon;
        const QJsonArray polyArray = json["polygon"].toArray();
        for (const QJsonValue &v : polyArray) {
            QJsonObject point = v.toObject();
            polygon.append(QVariant::fromValue(QGeoCoordinate(point["lat"].toDouble(), point["lon"].toDouble())));
        }
        zone->_mapPolygon->setPath(polygon);
    }

    return zone;
}

/*===========================================================================*/
// SARZoneManager
/*===========================================================================*/

SARZoneManager::SARZoneManager(QObject *parent)
    : QObject(parent)
    , _zones(new QmlObjectListModel(this))
    , _searchAreaPolygon(new QGCMapPolygon(this))
{
    connect(_searchAreaPolygon, &QGCMapPolygon::pathChanged, this, &SARZoneManager::searchAreaChanged);
}

SARZoneManager::~SARZoneManager()
{
}

int SARZoneManager::totalZones() const
{
    return _zones->count();
}

double SARZoneManager::overallProgress() const
{
    if (_zones->count() == 0) return 0.0;

    double total = 0.0;
    for (int i = 0; i < _zones->count(); i++) {
        auto *zone = qobject_cast<SARZone *>(_zones->get(i));
        if (zone) total += zone->progress();
    }
    return total / _zones->count();
}

bool SARZoneManager::hasSearchArea() const
{
    return _searchAreaPolygon->path().size() >= 3;
}

void SARZoneManager::selectZone(int zoneId)
{
    SARZone *target = nullptr;
    for (int i = 0; i < _zones->count(); i++) {
        auto *zone = qobject_cast<SARZone *>(_zones->get(i));
        if (zone) {
            if (zone->zoneId() == zoneId) {
                target = zone;
                zone->setSelected(true);
            } else {
                zone->setSelected(false);
            }
        }
    }

    if (_selectedZone != target) {
        _selectedZone = target;
        emit selectedZoneChanged();
    }
}

void SARZoneManager::clearSelection()
{
    for (int i = 0; i < _zones->count(); i++) {
        auto *zone = qobject_cast<SARZone *>(_zones->get(i));
        if (zone) zone->setSelected(false);
    }

    if (_selectedZone) {
        _selectedZone = nullptr;
        emit selectedZoneChanged();
    }
}

SARZone *SARZoneManager::getZone(int zoneId) const
{
    for (int i = 0; i < _zones->count(); i++) {
        auto *zone = qobject_cast<SARZone *>(_zones->get(i));
        if (zone && zone->zoneId() == zoneId) {
            return zone;
        }
    }
    return nullptr;
}

void SARZoneManager::partitionArea(const QVariantList &boundaryPolygon, int droneCount, int strategy)
{
    clearAllZones();

    switch (static_cast<PartitionStrategy>(strategy)) {
    case StripPartition:
        _partitionStrips(boundaryPolygon, droneCount);
        break;
    case GridPartition:
    default:
        _partitionGrid(boundaryPolygon, droneCount);
        break;
    }

    emit zonesChanged();
}

void SARZoneManager::partitionSearchArea(int droneCount, int strategy)
{
    if (!hasSearchArea()) {
        qCWarning(SARZoneManagerLog) << "Cannot partition: search area polygon has fewer than 3 vertices";
        return;
    }

    partitionArea(_searchAreaPolygon->path(), droneCount, strategy);
}

SARZone *SARZoneManager::addZone(const QVariantList &polygon, const QString &name)
{
    auto *zone = new SARZone(_nextZoneId++, this);
    zone->setPolygon(polygon);
    if (!name.isEmpty()) zone->setName(name);
    _zones->append(zone);
    emit zonesChanged();
    return zone;
}

void SARZoneManager::removeZone(int zoneId)
{
    for (int i = 0; i < _zones->count(); i++) {
        auto *zone = qobject_cast<SARZone *>(_zones->get(i));
        if (zone && zone->zoneId() == zoneId) {
            if (_selectedZone == zone) {
                _selectedZone = nullptr;
                emit selectedZoneChanged();
            }
            _zones->removeAt(i);
            zone->deleteLater();
            emit zonesChanged();
            return;
        }
    }
}

void SARZoneManager::clearAllZones()
{
    if (_selectedZone) {
        _selectedZone = nullptr;
        emit selectedZoneChanged();
    }
    _zones->clearAndDeleteContents();
    _nextZoneId = 1;
    emit zonesChanged();
}

void SARZoneManager::assignZoneToVehicle(int zoneId, int vehicleId)
{
    for (int i = 0; i < _zones->count(); i++) {
        auto *zone = qobject_cast<SARZone *>(_zones->get(i));
        if (zone && zone->zoneId() == zoneId) {
            zone->setAssignedVehicle(vehicleId);
            zone->setStatus(SARZone::Active);
            _zoneAssignmentGen++;
            emit zoneAssignmentChanged();
            emit zoneAssigned(zoneId, vehicleId);
            return;
        }
    }
}

void SARZoneManager::reassignZone(int zoneId, int newVehicleId)
{
    for (int i = 0; i < _zones->count(); i++) {
        auto *zone = qobject_cast<SARZone *>(_zones->get(i));
        if (zone && zone->zoneId() == zoneId) {
            zone->setStatus(SARZone::Reassigning);
            zone->setAssignedVehicle(newVehicleId);
            zone->setStatus(SARZone::Active);
            _zoneAssignmentGen++;
            emit zoneAssignmentChanged();
            emit zoneAssigned(zoneId, newVehicleId);
            return;
        }
    }
}

SARZone *SARZoneManager::zoneForVehicle(int vehicleId) const
{
    for (int i = 0; i < _zones->count(); i++) {
        auto *zone = qobject_cast<SARZone *>(_zones->get(i));
        if (zone && zone->assignedVehicle() == vehicleId) {
            return zone;
        }
    }
    return nullptr;
}

QGeoRectangle SARZoneManager::_boundingRect(const QVariantList &polygon) const
{
    if (polygon.isEmpty()) return QGeoRectangle();

    double minLat = 90, maxLat = -90, minLon = 180, maxLon = -180;
    for (const QVariant &v : polygon) {
        QGeoCoordinate c = v.value<QGeoCoordinate>();
        minLat = qMin(minLat, c.latitude());
        maxLat = qMax(maxLat, c.latitude());
        minLon = qMin(minLon, c.longitude());
        maxLon = qMax(maxLon, c.longitude());
    }
    return QGeoRectangle(QGeoCoordinate(maxLat, minLon), QGeoCoordinate(minLat, maxLon));
}

void SARZoneManager::_partitionGrid(const QVariantList &boundary, int count)
{
    QGeoRectangle bounds = _boundingRect(boundary);

    // Determine grid dimensions: try to make roughly square cells
    int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count))));
    int rows = static_cast<int>(std::ceil(static_cast<double>(count) / cols));

    double latStep = (bounds.topLeft().latitude() - bounds.bottomRight().latitude()) / rows;
    double lonStep = (bounds.bottomRight().longitude() - bounds.topLeft().longitude()) / cols;

    int created = 0;
    for (int r = 0; r < rows && created < count; r++) {
        for (int c = 0; c < cols && created < count; c++) {
            double topLat = bounds.topLeft().latitude() - (r * latStep);
            double botLat = topLat - latStep;
            double leftLon = bounds.topLeft().longitude() + (c * lonStep);
            double rightLon = leftLon + lonStep;

            QVariantList cellPoly;
            cellPoly.append(QVariant::fromValue(QGeoCoordinate(topLat, leftLon)));
            cellPoly.append(QVariant::fromValue(QGeoCoordinate(topLat, rightLon)));
            cellPoly.append(QVariant::fromValue(QGeoCoordinate(botLat, rightLon)));
            cellPoly.append(QVariant::fromValue(QGeoCoordinate(botLat, leftLon)));

            addZone(cellPoly, QStringLiteral("Grid %1-%2").arg(r + 1).arg(c + 1));
            created++;
        }
    }
}

void SARZoneManager::_partitionStrips(const QVariantList &boundary, int count)
{
    QGeoRectangle bounds = _boundingRect(boundary);

    double lonStep = (bounds.bottomRight().longitude() - bounds.topLeft().longitude()) / count;

    for (int i = 0; i < count; i++) {
        double leftLon = bounds.topLeft().longitude() + (i * lonStep);
        double rightLon = leftLon + lonStep;

        QVariantList stripPoly;
        stripPoly.append(QVariant::fromValue(QGeoCoordinate(bounds.topLeft().latitude(), leftLon)));
        stripPoly.append(QVariant::fromValue(QGeoCoordinate(bounds.topLeft().latitude(), rightLon)));
        stripPoly.append(QVariant::fromValue(QGeoCoordinate(bounds.bottomRight().latitude(), rightLon)));
        stripPoly.append(QVariant::fromValue(QGeoCoordinate(bounds.bottomRight().latitude(), leftLon)));

        addZone(stripPoly, QStringLiteral("Strip %1").arg(i + 1));
    }
}

QJsonArray SARZoneManager::toJson() const
{
    QJsonArray arr;
    for (int i = 0; i < _zones->count(); i++) {
        auto *zone = qobject_cast<SARZone *>(_zones->get(i));
        if (zone) arr.append(zone->toJson());
    }
    return arr;
}

void SARZoneManager::fromJson(const QJsonArray &json)
{
    clearAllZones();
    for (const QJsonValue &v : json) {
        auto *zone = SARZone::fromJson(v.toObject(), this);
        if (zone) {
            _zones->append(zone);
            if (zone->zoneId() >= _nextZoneId) {
                _nextZoneId = zone->zoneId() + 1;
            }
        }
    }
    emit zonesChanged();
}

/*===========================================================================*/
// Plan-to-SAR import
/*===========================================================================*/

QList<QGeoCoordinate> SARZoneManager::_computeConvexHull(const QList<QGeoCoordinate> &points)
{
    // Andrew's monotone chain algorithm operating on (latitude, longitude)
    if (points.size() < 3) return points;

    // Sort by latitude then longitude
    QList<QGeoCoordinate> sorted = points;
    std::sort(sorted.begin(), sorted.end(), [](const QGeoCoordinate &a, const QGeoCoordinate &b) {
        if (!qFuzzyCompare(a.latitude(), b.latitude()))
            return a.latitude() < b.latitude();
        return a.longitude() < b.longitude();
    });

    // Remove duplicates (within ~1 m tolerance ≈ 0.00001 degrees)
    QList<QGeoCoordinate> unique;
    for (const auto &c : sorted) {
        if (unique.isEmpty() ||
            !qFuzzyCompare(c.latitude(), unique.last().latitude()) ||
            !qFuzzyCompare(c.longitude(), unique.last().longitude())) {
            unique.append(c);
        }
    }
    if (unique.size() < 3) return unique;

    // Cross product of vectors OA and OB where O=o, A=a, B=b
    auto cross = [](const QGeoCoordinate &o, const QGeoCoordinate &a, const QGeoCoordinate &b) -> double {
        return (a.latitude() - o.latitude()) * (b.longitude() - o.longitude())
             - (a.longitude() - o.longitude()) * (b.latitude() - o.latitude());
    };

    const int n = unique.size();
    QList<QGeoCoordinate> hull;
    hull.reserve(2 * n);

    // Lower hull
    for (int i = 0; i < n; i++) {
        while (hull.size() >= 2 && cross(hull[hull.size() - 2], hull[hull.size() - 1], unique[i]) <= 0)
            hull.removeLast();
        hull.append(unique[i]);
    }

    // Upper hull
    const int lowerSize = hull.size() + 1;
    for (int i = n - 2; i >= 0; i--) {
        while (hull.size() >= lowerSize && cross(hull[hull.size() - 2], hull[hull.size() - 1], unique[i]) <= 0)
            hull.removeLast();
        hull.append(unique[i]);
    }

    hull.removeLast(); // Remove the last point because it's the same as the first
    return hull;
}

QString SARZoneManager::importFromPlanItems(QmlObjectListModel *visualItems)
{
    if (!visualItems || visualItems->count() == 0) {
        qCWarning(SARZoneManagerLog) << "importFromPlanItems: no items to import";
        return QString();
    }

    QList<QGeoCoordinate> allVertices;   // Every polygon vertex and waypoint coordinate
    int polygonCount = 0;
    int waypointCount = 0;

    for (int i = 0; i < visualItems->count(); i++) {
        VisualMissionItem *vi = qobject_cast<VisualMissionItem *>(visualItems->get(i));
        if (!vi) continue;

        // Check for Survey or CorridorScan (both inherit TransectStyleComplexItem)
        TransectStyleComplexItem *transectItem = qobject_cast<TransectStyleComplexItem *>(vi);
        if (transectItem) {
            QGCMapPolygon *poly = transectItem->surveyAreaPolygon();
            if (poly && poly->isValid()) {
                const QList<QGeoCoordinate> coords = poly->coordinateList();
                allVertices.append(coords);
                polygonCount++;
            }
            continue;
        }

        // Check for StructureScan
        StructureScanComplexItem *structItem = qobject_cast<StructureScanComplexItem *>(vi);
        if (structItem) {
            QGCMapPolygon *poly = structItem->structurePolygon();
            if (poly && poly->isValid()) {
                const QList<QGeoCoordinate> coords = poly->coordinateList();
                allVertices.append(coords);
                polygonCount++;
            }
            continue;
        }

        // Check for simple waypoint items
        SimpleMissionItem *simpleItem = qobject_cast<SimpleMissionItem *>(vi);
        if (simpleItem) {
            const QGeoCoordinate coord = simpleItem->coordinate();
            if (coord.isValid()) {
                allVertices.append(coord);
                waypointCount++;
            }
            continue;
        }
    }

    if (allVertices.isEmpty()) {
        qCWarning(SARZoneManagerLog) << "importFromPlanItems: no usable coordinates found in plan items";
        return QString();
    }

    // Compute the search area polygon
    QList<QGeoCoordinate> resultPolygon;

    if (allVertices.size() < 3) {
        // Not enough points for a polygon — create a buffered bounding box
        // Use a ~50 m buffer around the point(s)
        const double bufferDeg = 50.0 / 111320.0;  // ~50 meters in degrees latitude
        double minLat = 90, maxLat = -90, minLon = 180, maxLon = -180;
        for (const auto &c : allVertices) {
            minLat = qMin(minLat, c.latitude());
            maxLat = qMax(maxLat, c.latitude());
            minLon = qMin(minLon, c.longitude());
            maxLon = qMax(maxLon, c.longitude());
        }
        resultPolygon.append(QGeoCoordinate(maxLat + bufferDeg, minLon - bufferDeg));
        resultPolygon.append(QGeoCoordinate(maxLat + bufferDeg, maxLon + bufferDeg));
        resultPolygon.append(QGeoCoordinate(minLat - bufferDeg, maxLon + bufferDeg));
        resultPolygon.append(QGeoCoordinate(minLat - bufferDeg, minLon - bufferDeg));
    } else if (polygonCount == 1 && waypointCount == 0) {
        // Single polygon, no waypoints — use it directly (already in allVertices)
        resultPolygon = allVertices;
    } else {
        // Multiple polygons or mix of polygons + waypoints — compute convex hull
        resultPolygon = _computeConvexHull(allVertices);
    }

    // Clear any existing zones (they were based on the old search area)
    clearAllZones();

    // Set the search area polygon
    _searchAreaPolygon->setPath(resultPolygon);

    // Build a summary string for the UI
    QString summary;
    if (polygonCount > 0 && waypointCount > 0) {
        summary = tr("Imported %1 polygon(s) and %2 waypoint(s) as SAR search area")
                      .arg(polygonCount).arg(waypointCount);
    } else if (polygonCount > 0) {
        summary = tr("Imported %1 polygon(s) as SAR search area").arg(polygonCount);
    } else {
        summary = tr("Imported %1 waypoint(s) as SAR search area").arg(waypointCount);
    }

    qCDebug(SARZoneManagerLog) << summary;
    return summary;
}
