#include "SARZoneManager.h"
#include "QmlObjectListModel.h"
#include "QGCLoggingCategory.h"
#include "QGCMapPolygon.h"
#include "TransectStyleComplexItem.h"
#include "StructureScanComplexItem.h"
#include "SimpleMissionItem.h"
#include "VisualMissionItem.h"
#include "MultiVehicleManager.h"
#include "Vehicle.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtGui/QColor>
#include <QtPositioning/QGeoCoordinate>

#include <cmath>
#include <algorithm>
#include <numeric>
#include <limits>

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
    case VoronoiPartition:
        _partitionVoronoi(boundaryPolygon, droneCount);
        break;
    case GridPartition:
    default:
        _partitionGrid(boundaryPolygon, droneCount);
        break;
    }

    // Validate that the partition covers the search area
    _validateCoverage(boundaryPolygon);

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

/*---------------------------------------------------------------------------*/
// Geometry helpers
/*---------------------------------------------------------------------------*/

double SARZoneManager::_signedArea(const QList<QGeoCoordinate> &poly)
{
    // Signed area using Shoelace formula in lat/lon space.
    // Positive = counter-clockwise winding.
    double area = 0.0;
    const int n = poly.size();
    for (int i = 0; i < n; i++) {
        const QGeoCoordinate &a = poly[i];
        const QGeoCoordinate &b = poly[(i + 1) % n];
        area += a.longitude() * b.latitude() - b.longitude() * a.latitude();
    }
    return area * 0.5;
}

bool SARZoneManager::_pointInPolygon(const QGeoCoordinate &point, const QVariantList &polygon)
{
    // Ray-casting algorithm: cast a ray from point eastward, count crossings.
    const int n = polygon.size();
    if (n < 3) return false;

    bool inside = false;
    double px = point.longitude(), py = point.latitude();

    for (int i = 0, j = n - 1; i < n; j = i++) {
        const QGeoCoordinate ci = polygon[i].value<QGeoCoordinate>();
        const QGeoCoordinate cj = polygon[j].value<QGeoCoordinate>();
        double xi = ci.longitude(), yi = ci.latitude();
        double xj = cj.longitude(), yj = cj.latitude();

        if (((yi > py) != (yj > py)) &&
            (px < (xj - xi) * (py - yi) / (yj - yi) + xi)) {
            inside = !inside;
        }
    }
    return inside;
}

QGeoCoordinate SARZoneManager::_centroid(const QVariantList &polygon)
{
    if (polygon.isEmpty()) return QGeoCoordinate();

    double latSum = 0.0, lonSum = 0.0;
    for (const QVariant &v : polygon) {
        QGeoCoordinate c = v.value<QGeoCoordinate>();
        latSum += c.latitude();
        lonSum += c.longitude();
    }
    return QGeoCoordinate(latSum / polygon.size(), lonSum / polygon.size());
}

double SARZoneManager::_approxAreaSqM(const QVariantList &polygon)
{
    // Shoelace on projected metres relative to first vertex.
    if (polygon.size() < 3) return 0.0;

    const QGeoCoordinate origin = polygon[0].value<QGeoCoordinate>();
    const double cosLat = std::cos(origin.latitude() * M_PI / 180.0);
    const double mPerDegLat = 111320.0;
    const double mPerDegLon = 111320.0 * cosLat;

    double area = 0.0;
    const int n = polygon.size();
    for (int i = 0; i < n; i++) {
        const QGeoCoordinate c1 = polygon[i].value<QGeoCoordinate>();
        const QGeoCoordinate c2 = polygon[(i + 1) % n].value<QGeoCoordinate>();
        double x1 = (c1.longitude() - origin.longitude()) * mPerDegLon;
        double y1 = (c1.latitude()  - origin.latitude())  * mPerDegLat;
        double x2 = (c2.longitude() - origin.longitude()) * mPerDegLon;
        double y2 = (c2.latitude()  - origin.latitude())  * mPerDegLat;
        area += (x1 * y2 - x2 * y1);
    }
    return std::abs(area) * 0.5;
}

/*---------------------------------------------------------------------------*/
// Sutherland-Hodgman polygon clipping
/*---------------------------------------------------------------------------*/

/*
 * Clips the SUBJECT polygon against every edge of the CLIP polygon using
 * the Sutherland-Hodgman algorithm.  Each clip edge defines a half-plane;
 * vertices on the "inside" side are kept, new intersection vertices are
 * inserted at edge crossings.
 *
 * This works perfectly when the subject (grid cell / strip) is convex.
 * For concave clip polygons (search area boundary) the result may have
 * minor artefacts in pathological concavities, but for practical SAR
 * search areas the output is correct and robust.
 *
 * Returns a list of fragments; usually one, but the caller can handle
 * multiple.
 */

QList<QVariantList> SARZoneManager::_clipPolygonWA(const QVariantList &subject, const QVariantList &clip)
{
    QList<QVariantList> results;
    if (subject.size() < 3 || clip.size() < 3)
        return results;

    // ── Helper: line-segment intersection parameter ──
    auto lerp = [](const QGeoCoordinate &a, const QGeoCoordinate &b, double t) -> QGeoCoordinate {
        return QGeoCoordinate(
            a.latitude()  + t * (b.latitude()  - a.latitude()),
            a.longitude() + t * (b.longitude() - a.longitude()));
    };

    // Build clip-edge list
    struct Edge {
        QGeoCoordinate a, b;          // edge from a -> b
        double nx, ny;                // inward-pointing normal (left of a->b)
    };

    QList<QGeoCoordinate> clipCoords;
    clipCoords.reserve(clip.size());
    for (const QVariant &v : clip)
        clipCoords.append(v.value<QGeoCoordinate>());

    // Ensure clip polygon is counter-clockwise so "left of edge" = inside
    if (_signedArea(clipCoords) < 0.0)
        std::reverse(clipCoords.begin(), clipCoords.end());

    const int cn = clipCoords.size();
    QList<Edge> edges;
    edges.reserve(cn);
    for (int i = 0; i < cn; i++) {
        Edge e;
        e.a = clipCoords[i];
        e.b = clipCoords[(i + 1) % cn];
        // Edge direction d = b - a;  inward normal = (-dy, dx)
        double dx = e.b.longitude() - e.a.longitude();
        double dy = e.b.latitude()  - e.a.latitude();
        e.nx = -dy;
        e.ny =  dx;
        edges.append(e);
    }

    // ── Inside test for a single edge ──
    auto insideEdge = [](const QGeoCoordinate &pt, const Edge &e) -> double {
        // Dot product of (pt - e.a) with inward normal.
        // Positive  => inside, negative => outside, zero => on edge.
        double dx = pt.longitude() - e.a.longitude();
        double dy = pt.latitude()  - e.a.latitude();
        return e.nx * dx + e.ny * dy;   // return the signed distance
    };

    // ── Sutherland-Hodgman core ──
    // Start with the subject polygon and clip against each edge in turn.
    QList<QGeoCoordinate> output;
    output.reserve(subject.size());
    for (const QVariant &v : subject)
        output.append(v.value<QGeoCoordinate>());

    for (const Edge &edge : edges) {
        if (output.size() < 3) break;

        QList<QGeoCoordinate> input = output;
        output.clear();
        const int in = input.size();

        for (int i = 0; i < in; i++) {
            const QGeoCoordinate &cur  = input[i];
            const QGeoCoordinate &prev = input[(i + in - 1) % in];

            double dCur  = insideEdge(cur,  edge);
            double dPrev = insideEdge(prev, edge);
            bool curInside  = (dCur  >= -1e-12);
            bool prevInside = (dPrev >= -1e-12);

            if (curInside) {
                if (!prevInside) {
                    // Crossing from outside to inside — add intersection
                    double t = dPrev / (dPrev - dCur);
                    output.append(lerp(prev, cur, t));
                }
                output.append(cur);
            } else if (prevInside) {
                // Crossing from inside to outside — add intersection
                double t = dPrev / (dPrev - dCur);
                output.append(lerp(prev, cur, t));
            }
            // else both outside: emit nothing
        }
    }

    if (output.size() >= 3) {
        QVariantList poly;
        poly.reserve(output.size());
        for (const QGeoCoordinate &c : output)
            poly.append(QVariant::fromValue(c));
        results.append(poly);
    }

    // ── Fallback: if S-H produced nothing, check full containment ──
    if (results.isEmpty()) {
        // Subject entirely inside clip?
        bool allInside = true;
        for (const QVariant &v : subject) {
            if (!_pointInPolygon(v.value<QGeoCoordinate>(), clip)) {
                allInside = false;
                break;
            }
        }
        if (allInside) {
            results.append(subject);
        } else {
            // Clip entirely inside subject? (rare for grid cells, but possible)
            bool clipInsideSubject = true;
            for (const QGeoCoordinate &cc : clipCoords) {
                if (!_pointInPolygon(cc, subject)) {
                    clipInsideSubject = false;
                    break;
                }
            }
            if (clipInsideSubject) {
                results.append(clip);
            }
        }
    }

    return results;
}

QVariantList SARZoneManager::_clipPolygon(const QVariantList &subject, const QVariantList &clip)
{
    // Convenience wrapper: returns the largest clipped fragment (or empty).
    QList<QVariantList> fragments = _clipPolygonWA(subject, clip);
    if (fragments.isEmpty())
        return QVariantList();

    if (fragments.size() == 1)
        return fragments[0];

    // Return the fragment with the largest area
    int bestIdx = 0;
    double bestArea = 0.0;
    for (int i = 0; i < fragments.size(); i++) {
        double a = _approxAreaSqM(fragments[i]);
        if (a > bestArea) {
            bestArea = a;
            bestIdx = i;
        }
    }
    return fragments[bestIdx];
}

/*---------------------------------------------------------------------------*/
// Subdivision helper: split a polygon along a latitude or longitude line
/*---------------------------------------------------------------------------*/

// Split a polygon into two halves at the given latitude line.
// Returns the portion ABOVE (>= splitLat) in `above` and BELOW (< splitLat) in `below`.
static void _splitAtLat(const QVariantList &poly, double splitLat,
                        QVariantList &above, QVariantList &below)
{
    above.clear();
    below.clear();
    const int n = poly.size();
    if (n < 3) return;

    for (int i = 0; i < n; i++) {
        const QGeoCoordinate cur  = poly[i].value<QGeoCoordinate>();
        const QGeoCoordinate next = poly[(i + 1) % n].value<QGeoCoordinate>();

        bool curAbove  = (cur.latitude()  >= splitLat);
        bool nextAbove = (next.latitude() >= splitLat);

        if (curAbove) above.append(QVariant::fromValue(cur));
        else          below.append(QVariant::fromValue(cur));

        // If the edge crosses the split line, insert intersection in both
        if (curAbove != nextAbove) {
            double t = (splitLat - cur.latitude()) / (next.latitude() - cur.latitude());
            QGeoCoordinate ix(splitLat,
                              cur.longitude() + t * (next.longitude() - cur.longitude()));
            above.append(QVariant::fromValue(ix));
            below.append(QVariant::fromValue(ix));
        }
    }
}

// Split a polygon into two halves at the given longitude line.
// Returns LEFT (<= splitLon) in `left` and RIGHT (> splitLon) in `right`.
static void _splitAtLon(const QVariantList &poly, double splitLon,
                        QVariantList &left, QVariantList &right)
{
    left.clear();
    right.clear();
    const int n = poly.size();
    if (n < 3) return;

    for (int i = 0; i < n; i++) {
        const QGeoCoordinate cur  = poly[i].value<QGeoCoordinate>();
        const QGeoCoordinate next = poly[(i + 1) % n].value<QGeoCoordinate>();

        bool curLeft  = (cur.longitude()  <= splitLon);
        bool nextLeft = (next.longitude() <= splitLon);

        if (curLeft) left.append(QVariant::fromValue(cur));
        else         right.append(QVariant::fromValue(cur));

        if (curLeft != nextLeft) {
            double t = (splitLon - cur.longitude()) / (next.longitude() - cur.longitude());
            QGeoCoordinate ix(cur.latitude() + t * (next.latitude() - cur.latitude()),
                              splitLon);
            left.append(QVariant::fromValue(ix));
            right.append(QVariant::fromValue(ix));
        }
    }
}

/*---------------------------------------------------------------------------*/
// Grid partition — recursive binary split, no merging, no overlap
/*---------------------------------------------------------------------------*/

void SARZoneManager::_partitionGrid(const QVariantList &boundary, int count)
{
    if (count <= 0) return;
    if (boundary.size() < 3) return;

    // ── Recursive binary-split approach ──
    // Start with the full boundary as a single zone.
    // Repeatedly split the largest zone (alternating lat/lon cuts through
    // its centroid) until we have `count` zones.
    // Because every split is a planar cut on the actual polygon, zones are
    // always non-overlapping and gap-free by construction.

    struct SplitZone {
        QVariantList polygon;
        double       areaSqM;
        bool         lastSplitWasLat;  // alternate split direction
    };

    QList<SplitZone> zones;
    SplitZone initial;
    initial.polygon = boundary;
    initial.areaSqM = _approxAreaSqM(boundary);
    initial.lastSplitWasLat = false;  // first split will be lat
    zones.append(initial);

    while (zones.size() < count) {
        // Find the largest zone to split
        int largestIdx = 0;
        double largestArea = zones[0].areaSqM;
        for (int i = 1; i < zones.size(); i++) {
            if (zones[i].areaSqM > largestArea) {
                largestArea = zones[i].areaSqM;
                largestIdx = i;
            }
        }

        SplitZone &toSplit = zones[largestIdx];

        // Compute centroid for the split line
        QGeoCoordinate c = _centroid(toSplit.polygon);

        QVariantList halfA, halfB;
        bool splitLat = !toSplit.lastSplitWasLat;  // alternate

        if (splitLat) {
            _splitAtLat(toSplit.polygon, c.latitude(), halfA, halfB);
        } else {
            _splitAtLon(toSplit.polygon, c.longitude(), halfA, halfB);
        }

        // If the split failed (one half is degenerate), try the other direction
        if (halfA.size() < 3 || halfB.size() < 3) {
            splitLat = !splitLat;
            if (splitLat) {
                _splitAtLat(toSplit.polygon, c.latitude(), halfA, halfB);
            } else {
                _splitAtLon(toSplit.polygon, c.longitude(), halfA, halfB);
            }
        }

        // If still degenerate, this zone can't be split — stop
        if (halfA.size() < 3 || halfB.size() < 3) {
            qCWarning(SARZoneManagerLog) << "Cannot split zone further at"
                                          << zones.size() << "zones (requested" << count << ")";
            break;
        }

        // Replace the largest zone with the two halves
        SplitZone zA, zB;
        zA.polygon = halfA;
        zA.areaSqM = _approxAreaSqM(halfA);
        zA.lastSplitWasLat = splitLat;
        zB.polygon = halfB;
        zB.areaSqM = _approxAreaSqM(halfB);
        zB.lastSplitWasLat = splitLat;

        zones[largestIdx] = zA;
        zones.append(zB);
    }

    // Create the zone objects
    for (int i = 0; i < zones.size(); i++) {
        addZone(zones[i].polygon, QStringLiteral("Grid %1").arg(i + 1));
    }

    qCDebug(SARZoneManagerLog) << "Grid partition created" << zones.size()
                                << "zones (requested:" << count << ")";
}

/*---------------------------------------------------------------------------*/
// Strip partition — longitude-only binary split, no overlap
/*---------------------------------------------------------------------------*/

void SARZoneManager::_partitionStrips(const QVariantList &boundary, int count)
{
    if (count <= 0) return;
    if (boundary.size() < 3) return;

    // Recursive binary-split approach, but only splitting along longitude
    // (vertical cuts) to produce parallel strips.

    struct SplitZone {
        QVariantList polygon;
        double       areaSqM;
    };

    QList<SplitZone> zones;
    SplitZone initial;
    initial.polygon = boundary;
    initial.areaSqM = _approxAreaSqM(boundary);
    zones.append(initial);

    while (zones.size() < count) {
        // Find the largest zone to split
        int largestIdx = 0;
        double largestArea = zones[0].areaSqM;
        for (int i = 1; i < zones.size(); i++) {
            if (zones[i].areaSqM > largestArea) {
                largestArea = zones[i].areaSqM;
                largestIdx = i;
            }
        }

        // Split at the centroid longitude
        QGeoCoordinate c = _centroid(zones[largestIdx].polygon);

        QVariantList halfA, halfB;
        _splitAtLon(zones[largestIdx].polygon, c.longitude(), halfA, halfB);

        if (halfA.size() < 3 || halfB.size() < 3) {
            qCWarning(SARZoneManagerLog) << "Cannot split strip further at"
                                          << zones.size() << "zones (requested" << count << ")";
            break;
        }

        SplitZone zA, zB;
        zA.polygon = halfA;
        zA.areaSqM = _approxAreaSqM(halfA);
        zB.polygon = halfB;
        zB.areaSqM = _approxAreaSqM(halfB);

        zones[largestIdx] = zA;
        zones.append(zB);
    }

    // Sort strips from left to right by centroid longitude for nice naming
    std::sort(zones.begin(), zones.end(), [this](const SplitZone &a, const SplitZone &b) {
        return _centroid(a.polygon).longitude() < _centroid(b.polygon).longitude();
    });

    for (int i = 0; i < zones.size(); i++) {
        addZone(zones[i].polygon, QStringLiteral("Strip %1").arg(i + 1));
    }

    qCDebug(SARZoneManagerLog) << "Strip partition created" << zones.size()
                                << "zones (requested:" << count << ")";
}

/*---------------------------------------------------------------------------*/
// Voronoi partition — uses vehicle positions as seeds
/*---------------------------------------------------------------------------*/

void SARZoneManager::_partitionVoronoi(const QVariantList &boundary, int count)
{
    if (count <= 0) return;

    // ── Collect seed points ──
    // Prefer actual vehicle positions if available.
    QList<QGeoCoordinate> seeds;
    auto *mvm = MultiVehicleManager::instance();
    if (mvm) {
        QmlObjectListModel *vehicles = mvm->vehicles();
        for (int i = 0; i < vehicles->count() && seeds.size() < count; i++) {
            auto *v = qobject_cast<Vehicle *>(vehicles->get(i));
            if (v && v->coordinate().isValid()) {
                seeds.append(v->coordinate());
            }
        }
    }

    // If we don't have enough vehicle positions, fill with evenly-spaced points
    // distributed along the search area boundary.
    if (seeds.size() < count) {
        // Compute centroid and distribute remaining seeds radially
        QGeoCoordinate center = _centroid(boundary);
        int remaining = count - seeds.size();

        if (seeds.isEmpty()) {
            // No vehicles — distribute seeds evenly within the bounding rect
            QGeoRectangle bounds = _boundingRect(boundary);
            int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(remaining))));
            int rows = static_cast<int>(std::ceil(static_cast<double>(remaining) / cols));

            double latStep = (bounds.topLeft().latitude() - bounds.bottomRight().latitude()) / (rows + 1);
            double lonStep = (bounds.bottomRight().longitude() - bounds.topLeft().longitude()) / (cols + 1);

            for (int r = 1; r <= rows && seeds.size() < count; r++) {
                for (int c = 1; c <= cols && seeds.size() < count; c++) {
                    QGeoCoordinate pt(
                        bounds.topLeft().latitude() - r * latStep,
                        bounds.topLeft().longitude() + c * lonStep);
                    // Only use points inside the search area
                    if (_pointInPolygon(pt, boundary)) {
                        seeds.append(pt);
                    }
                }
            }
        }

        // If still not enough, add jittered points near the centroid
        double jitter = 0.001; // ~100m
        while (seeds.size() < count) {
            int idx = seeds.size();
            double angle = (2.0 * M_PI * idx) / remaining;
            QGeoCoordinate pt(
                center.latitude()  + jitter * std::cos(angle),
                center.longitude() + jitter * std::sin(angle));
            seeds.append(pt);
            jitter += 0.0005;
        }
    }

    qCDebug(SARZoneManagerLog) << "Voronoi partition with" << seeds.size() << "seeds";

    // ── Compute Voronoi cells ──
    // For each point in the search area, assign it to the nearest seed.
    // We discretize the search area on a fine grid and then build polygonal
    // boundaries for each cell.
    //
    // Approach: for each seed, find the set of boundary polygon vertices and
    // intersection points that belong to its Voronoi cell, then build the cell
    // polygon from the Voronoi half-plane intersections.
    //
    // Simplified Voronoi construction: for each seed, the Voronoi cell is the
    // intersection of all half-planes defined by the perpendicular bisectors
    // between this seed and every other seed, clipped to the search area.

    QGeoRectangle bounds = _boundingRect(boundary);
    const double mPerDegLat = 111320.0;
    const double mPerDegLon = 111320.0 * std::cos(
        ((bounds.topLeft().latitude() + bounds.bottomRight().latitude()) / 2.0) * M_PI / 180.0);

    for (int s = 0; s < seeds.size(); s++) {
        // Start with a large bounding rectangle as the initial cell
        double margin = 0.01; // ~1km margin
        QVariantList cell;
        cell.append(QVariant::fromValue(QGeoCoordinate(bounds.topLeft().latitude()     + margin, bounds.topLeft().longitude()     - margin)));
        cell.append(QVariant::fromValue(QGeoCoordinate(bounds.topLeft().latitude()     + margin, bounds.bottomRight().longitude() + margin)));
        cell.append(QVariant::fromValue(QGeoCoordinate(bounds.bottomRight().latitude() - margin, bounds.bottomRight().longitude() + margin)));
        cell.append(QVariant::fromValue(QGeoCoordinate(bounds.bottomRight().latitude() - margin, bounds.topLeft().longitude()     - margin)));

        // Clip by half-plane of each other seed
        for (int o = 0; o < seeds.size(); o++) {
            if (o == s) continue;
            if (cell.size() < 3) break;

            // Perpendicular bisector between seed s and seed o
            // Midpoint
            double midLat = (seeds[s].latitude()  + seeds[o].latitude())  / 2.0;
            double midLon = (seeds[s].longitude() + seeds[o].longitude()) / 2.0;

            // Direction from s to o (in projected metres for proper perpendicular)
            double dx = (seeds[o].longitude() - seeds[s].longitude()) * mPerDegLon;
            double dy = (seeds[o].latitude()  - seeds[s].latitude())  * mPerDegLat;
            double len = std::sqrt(dx * dx + dy * dy);
            if (len < 0.001) continue; // seeds too close

            // Perpendicular direction (rotated 90 degrees)
            double perpDx = -dy;
            double perpDy =  dx;

            // Create a clip line through midpoint perpendicular to s->o.
            // We want to keep the side closer to seed s.
            // Build a half-plane clip polygon (a very large polygon on the s-side).
            double extent = (qMax(bounds.topLeft().latitude() - bounds.bottomRight().latitude(),
                                   bounds.bottomRight().longitude() - bounds.topLeft().longitude())) * 2.0;

            // Two points along the bisector line
            double p1Lat = midLat + (perpDy / len) * extent / mPerDegLat;
            double p1Lon = midLon + (perpDx / len) * extent / mPerDegLon;
            double p2Lat = midLat - (perpDy / len) * extent / mPerDegLat;
            double p2Lon = midLon - (perpDx / len) * extent / mPerDegLon;

            // Two points on the s-side (offset in direction from o to s)
            double sDirLat = -dy / len;
            double sDirLon = -dx / len;
            double p3Lat = p2Lat + sDirLat * extent / mPerDegLat;
            double p3Lon = p2Lon + sDirLon * extent / mPerDegLon;
            double p4Lat = p1Lat + sDirLat * extent / mPerDegLat;
            double p4Lon = p1Lon + sDirLon * extent / mPerDegLon;

            QVariantList halfPlane;
            halfPlane.append(QVariant::fromValue(QGeoCoordinate(p1Lat, p1Lon)));
            halfPlane.append(QVariant::fromValue(QGeoCoordinate(p2Lat, p2Lon)));
            halfPlane.append(QVariant::fromValue(QGeoCoordinate(p3Lat, p3Lon)));
            halfPlane.append(QVariant::fromValue(QGeoCoordinate(p4Lat, p4Lon)));

            // Clip the cell by this half-plane (half-planes are convex, so
            // Sutherland-Hodgman style clipping works here — use _clipPolygon
            // which picks the largest fragment)
            cell = _clipPolygon(cell, halfPlane);
        }

        if (cell.size() < 3) continue;

        // Clip the Voronoi cell against the search area boundary
        cell = _clipPolygon(cell, boundary);
        if (cell.size() < 3) continue;

        addZone(cell, QStringLiteral("Voronoi %1").arg(s + 1));
    }

    qCDebug(SARZoneManagerLog) << "Voronoi partition created" << _zones->count()
                                << "zones (requested:" << count << ")";
}

/*---------------------------------------------------------------------------*/
// Coverage validation
/*---------------------------------------------------------------------------*/

void SARZoneManager::_validateCoverage(const QVariantList &boundary)
{
    if (_zones->count() == 0 || boundary.size() < 3) {
        _lastCoverageRatio = 0.0;
        emit coverageValidated(0.0);
        return;
    }

    // Sample points on a grid across the bounding rectangle, check how many
    // that are inside the search area are also inside at least one zone.
    QGeoRectangle bounds = _boundingRect(boundary);

    const int sampleRows = 50;
    const int sampleCols = 50;
    double latStep = (bounds.topLeft().latitude()  - bounds.bottomRight().latitude())  / sampleRows;
    double lonStep = (bounds.bottomRight().longitude() - bounds.topLeft().longitude()) / sampleCols;

    int totalInside = 0;
    int coveredCount = 0;

    // Pre-collect zone polygons for efficiency
    QList<QVariantList> zonePolygons;
    for (int z = 0; z < _zones->count(); z++) {
        auto *zone = qobject_cast<SARZone *>(_zones->get(z));
        if (zone) {
            zonePolygons.append(zone->polygon());
        }
    }

    for (int r = 0; r < sampleRows; r++) {
        for (int c = 0; c < sampleCols; c++) {
            QGeoCoordinate pt(
                bounds.topLeft().latitude()  - (r + 0.5) * latStep,
                bounds.topLeft().longitude() + (c + 0.5) * lonStep);

            if (!_pointInPolygon(pt, boundary))
                continue;

            totalInside++;

            // Check if this point falls inside any zone
            bool covered = false;
            for (const QVariantList &zonePoly : zonePolygons) {
                if (_pointInPolygon(pt, zonePoly)) {
                    covered = true;
                    break;
                }
            }
            if (covered)
                coveredCount++;
        }
    }

    _lastCoverageRatio = (totalInside > 0) ? (static_cast<double>(coveredCount) / totalInside) : 1.0;
    emit coverageValidated(_lastCoverageRatio);

    if (_lastCoverageRatio < 0.95) {
        QString msg = QStringLiteral("Partition coverage is only %1% — some areas may not be searched")
                          .arg(static_cast<int>(_lastCoverageRatio * 100));
        qCWarning(SARZoneManagerLog) << msg;
        emit partitionWarning(msg);
    } else {
        qCDebug(SARZoneManagerLog) << "Partition coverage:" << static_cast<int>(_lastCoverageRatio * 100) << "%";
    }
}

double SARZoneManager::coverageRatio() const
{
    return _lastCoverageRatio;
}

/*===========================================================================*/
// JSON serialization
/*===========================================================================*/

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

void SARZoneManager::notifyProgressChanged()
{
    emit progressChanged();
}
