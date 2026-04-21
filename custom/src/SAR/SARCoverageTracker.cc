#include "SARCoverageTracker.h"
#include "SARGeoUtils.h"
#include "QGCLoggingCategory.h"
#include "Vehicle.h"

#include <QGCMAVLink.h>

#include <QtCore/QStringList>

#include <algorithm>
#include <cmath>
#include <QtCore/QThread>

QGC_LOGGING_CATEGORY(SARCoverageTrackerLog, "Sadron.SARCoverageTracker")

namespace {

QString _polygonCacheKey(const QVariantList &polygon)
{
    QStringList parts;
    parts.reserve(polygon.size());

    for (const QVariant &value : polygon) {
        const QGeoCoordinate coordinate = value.value<QGeoCoordinate>();
        parts.append(QStringLiteral("%1:%2")
                         .arg(coordinate.latitude(), 0, 'f', 7)
                         .arg(coordinate.longitude(), 0, 'f', 7));
    }

    return parts.join(QLatin1Char('|'));
}

} // namespace

SARCoverageTracker::SARCoverageTracker(QObject *parent)
    : QObject(parent)
{
}

SARCoverageTracker::~SARCoverageTracker()
{
}

double SARCoverageTracker::coveragePercent() const
{
    if (_totalCells == 0) return 0.0;

    int coveredValidCells = 0;
    if (_searchAreaCells.isEmpty()) {
        coveredValidCells = _coveredCells.size();
    } else {
        for (const auto &cell : _coveredCells) {
            if (_searchAreaCells.contains(cell)) {
                coveredValidCells++;
            }
        }
    }

    return (static_cast<double>(coveredValidCells) / _totalCells) * 100.0;
}

QVariantList SARCoverageTracker::coveredCells() const
{
    // 1B: Return cached list; rebuild only when dirty
    if (_coveredCellsCacheDirty) {
        _coveredCellsCache.clear();
        _coveredCellsCache.reserve(_coveredCells.size());
        for (const auto &cell : _coveredCells) {
            QGeoCoordinate coord = _cellToCoord(cell.first, cell.second);
            _coveredCellsCache.append(QVariant::fromValue(coord));
        }
        _coveredCellsCacheDirty = false;
    }
    return _coveredCellsCache;
}

void SARCoverageTracker::setCellSizeMeters(double size)
{
    if (!qFuzzyCompare(_cellSizeMeters, size) && size > 0) {
        _cellSizeMeters = size;
        emit gridChanged();
    }
}

void SARCoverageTracker::setCameraFovMeters(double fov)
{
    if (!qFuzzyCompare(_cameraFovMeters, fov) && fov > 0) {
        _cameraFovMeters = fov;
        emit cameraFovChanged();
    }
}

void SARCoverageTracker::initializeGrid(const QGeoCoordinate &origin, double widthMeters, double heightMeters)
{
    _gridOrigin = origin;
    _gridWidth = qBound(0, static_cast<int>(std::ceil(widthMeters / _cellSizeMeters)), 10000);
    _gridHeight = qBound(0, static_cast<int>(std::ceil(heightMeters / _cellSizeMeters)), 10000);
    _totalCells = _gridWidth * _gridHeight;
    _coveredCells.clear();
    _searchAreaCells.clear();
    _zoneCellCache.clear();
    _lastVehiclePositions.clear();

    // 1C: Pre-compute cos(refLat) for flat-earth approximation in _coordToCell
    _cosRefLat = std::cos(_gridOrigin.latitude() * M_PI / 180.0);

    _invalidateCoveredCellsCache();

    qCDebug(SARCoverageTrackerLog) << "Grid initialized:" << _gridWidth << "x" << _gridHeight
                                    << "cells (" << _totalCells << "total)";
    emit gridChanged();
    emit coverageChanged();
}

void SARCoverageTracker::initializeGridFromPolygon(const QVariantList &polygon)
{
    if (polygon.size() < 3) return;

    // Find bounding box
    double minLat = 90, maxLat = -90, minLon = 180, maxLon = -180;
    for (const QVariant &v : polygon) {
        QGeoCoordinate c = v.value<QGeoCoordinate>();
        minLat = qMin(minLat, c.latitude());
        maxLat = qMax(maxLat, c.latitude());
        minLon = qMin(minLon, c.longitude());
        maxLon = qMax(maxLon, c.longitude());
    }

    QGeoCoordinate topLeft(maxLat, minLon);
    QGeoCoordinate topRight(maxLat, maxLon);
    QGeoCoordinate bottomLeft(minLat, minLon);

    double widthMeters = topLeft.distanceTo(topRight);
    double heightMeters = topLeft.distanceTo(bottomLeft);

    initializeGrid(topLeft, widthMeters, heightMeters);

    for (int x = 0; x < _gridWidth; ++x) {
        for (int y = 0; y < _gridHeight; ++y) {
            const auto cell = qMakePair(x, y);
            if (_isPointInPolygon(_cellToCoord(x, y), polygon)) {
                _searchAreaCells.insert(cell);
            }
        }
    }

    _totalCells = _searchAreaCells.size();
    _zoneCellCache.clear();
    emit gridChanged();
    emit coverageChanged();
}

void SARCoverageTracker::markCellCovered(int gridX, int gridY)
{
    if (gridX >= 0 && gridX < _gridWidth && gridY >= 0 && gridY < _gridHeight) {
        auto cell = qMakePair(gridX, gridY);
        if (!_searchAreaCells.isEmpty() && !_searchAreaCells.contains(cell)) {
            return;
        }
        if (!_coveredCells.contains(cell)) {
            _coveredCells.insert(cell);
            _invalidateCoveredCellsCache();
            emit coverageChanged();
        }
    }
}

bool SARCoverageTracker::isCellCovered(int gridX, int gridY) const
{
    return _coveredCells.contains(qMakePair(gridX, gridY));
}

void SARCoverageTracker::resetCoverage()
{
    _coveredCells.clear();
    _invalidateCoveredCellsCache();
    emit coverageChanged();
}

void SARCoverageTracker::processMavlinkMessage(Vehicle *vehicle, const mavlink_message_t &message)
{
    if (!vehicle) return;

    // Process GLOBAL_POSITION_INT messages for coverage tracking
    if (message.msgid == MAVLINK_MSG_ID_GLOBAL_POSITION_INT) {
        mavlink_global_position_int_t pos;
        mavlink_msg_global_position_int_decode(&message, &pos);

        QGeoCoordinate coord(pos.lat / 1e7, pos.lon / 1e7, pos.relative_alt / 1000.0);
        int vehicleId = vehicle->id();

        // Only update if vehicle has moved significantly (> half cell size)
        if (_lastVehiclePositions.contains(vehicleId)) {
            double dist = _lastVehiclePositions[vehicleId].distanceTo(coord);
            if (dist < _cellSizeMeters / 2.0) return;
        }

        _lastVehiclePositions[vehicleId] = coord;
        _markPositionCovered(coord);
    }
}

void SARCoverageTracker::_markPositionCovered(const QGeoCoordinate &position)
{
    if (_totalCells == 0) return;

    // Calculate how many cells the camera FOV covers
    int cellRadius = static_cast<int>(std::ceil(_cameraFovMeters / (2.0 * _cellSizeMeters)));
    QPair<int, int> centerCell = _coordToCell(position);
    if (centerCell.first < 0 || centerCell.second < 0) {
        return;
    }

    bool changed = false;
    for (int dx = -cellRadius; dx <= cellRadius; dx++) {
        for (int dy = -cellRadius; dy <= cellRadius; dy++) {
            int cx = centerCell.first + dx;
            int cy = centerCell.second + dy;
            if (cx >= 0 && cx < _gridWidth && cy >= 0 && cy < _gridHeight) {
                auto cell = qMakePair(cx, cy);
                if (!_searchAreaCells.isEmpty() && !_searchAreaCells.contains(cell)) {
                    continue;
                }
                if (!_coveredCells.contains(cell)) {
                    _coveredCells.insert(cell);
                    changed = true;
                }
            }
        }
    }

    if (changed) {
        _invalidateCoveredCellsCache();
        emit coverageChanged();
    }
}

QPair<int, int> SARCoverageTracker::_coordToCell(const QGeoCoordinate &coord) const
{
    if (!_gridOrigin.isValid()) return qMakePair(-1, -1);

    // 1C: Flat-earth approximation (~50x faster than Vincenty/Haversine)
    // Accurate to <0.1% for SAR grid distances (typically <10km)
    static constexpr double kMetersPerDegLat = 111320.0;
    double eastDist  = (coord.longitude() - _gridOrigin.longitude()) * _cosRefLat * kMetersPerDegLat;
    double southDist = (_gridOrigin.latitude() - coord.latitude()) * kMetersPerDegLat;

    int gridX = static_cast<int>(std::floor(eastDist / _cellSizeMeters));
    int gridY = static_cast<int>(std::floor(southDist / _cellSizeMeters));

    if (gridX < 0 || gridX >= _gridWidth || gridY < 0 || gridY >= _gridHeight) {
        return qMakePair(-1, -1);
    }

    return qMakePair(gridX, gridY);
}

QGeoCoordinate SARCoverageTracker::_cellToCoord(int gridX, int gridY) const
{
    if (!_gridOrigin.isValid()) return QGeoCoordinate();

    // Approximate conversion back to geo coordinates
    double metersPerDegreeLat = 111320.0;
    double metersPerDegreeLon = 111320.0 * std::cos(_gridOrigin.latitude() * M_PI / 180.0);

    double lat = _gridOrigin.latitude() - (gridY * _cellSizeMeters / metersPerDegreeLat);
    double lon = _gridOrigin.longitude() + (gridX * _cellSizeMeters / metersPerDegreeLon);

    return QGeoCoordinate(lat, lon);
}

double SARCoverageTracker::coverageForZone(const QVariantList &zonePolygon) const
{
    // 4D: Assert main-thread access — _zoneCellCache is mutable and not mutex-protected
    Q_ASSERT(QThread::currentThread() == thread());

    if (_totalCells == 0 || zonePolygon.size() < 3) return 0.0;

    const QString cacheKey = _polygonCacheKey(zonePolygon);
    if (!_zoneCellCache.contains(cacheKey)) {
        QVector<QPair<int, int>> zoneCells;
        if (_searchAreaCells.isEmpty()) {
            zoneCells.reserve(_gridWidth * _gridHeight);
            for (int x = 0; x < _gridWidth; ++x) {
                for (int y = 0; y < _gridHeight; ++y) {
                    if (_isPointInPolygon(_cellToCoord(x, y), zonePolygon)) {
                        zoneCells.append(qMakePair(x, y));
                    }
                }
            }
        } else {
            zoneCells.reserve(_searchAreaCells.size());
            for (const auto &cell : _searchAreaCells) {
                if (_isPointInPolygon(_cellToCoord(cell.first, cell.second), zonePolygon)) {
                    zoneCells.append(cell);
                }
            }
        }

        _zoneCellCache.insert(cacheKey, zoneCells);
    }

    const QVector<QPair<int, int>> zoneCells = _zoneCellCache.value(cacheKey);
    if (zoneCells.isEmpty()) return 0.0;

    int coveredInZone = 0;
    for (const auto &cell : zoneCells) {
        if (_coveredCells.contains(cell)) {
            coveredInZone++;
        }
    }

    return static_cast<double>(coveredInZone) / zoneCells.size();
}

// 5A: Delegates to shared utility — single source of truth
bool SARCoverageTracker::_isPointInPolygon(const QGeoCoordinate &point, const QVariantList &polygon) const
{
    return SARGeoUtils::isPointInPolygon(point, polygon);
}

void SARCoverageTracker::_invalidateCoveredCellsCache()
{
    _coveredCellsCacheDirty = true;
}
