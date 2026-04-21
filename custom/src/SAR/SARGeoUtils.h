#pragma once

#include <QtPositioning/QGeoCoordinate>
#include <QtCore/QVariantList>

// Shared SAR geometry utilities — single source of truth for
// algorithms duplicated across SARCoverageTracker, VehicleCoordinator, SARZoneManager.

namespace SARGeoUtils {

// Ray-casting point-in-polygon test using lat/lon coordinates.
// Returns true if the point falls inside the polygon defined by
// a QVariantList of QGeoCoordinate values.
inline bool isPointInPolygon(const QGeoCoordinate &point, const QVariantList &polygon)
{
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

} // namespace SARGeoUtils
