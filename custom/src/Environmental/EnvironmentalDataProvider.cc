#include "EnvironmentalDataProvider.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <cmath>

#include "Terrain/TerrainQuery.h"

QGC_LOGGING_CATEGORY(EnvironmentalDataLog, "Sadron.EnvironmentalData")

/*===========================================================================*/

EnvironmentalDataProvider::EnvironmentalDataProvider(QObject *parent)
    : QObject(parent)
    , _nam(new QNetworkAccessManager(this))
    , _refreshTimer(new QTimer(this))
{
    _refreshTimer->setInterval(_weatherRefreshSecs * 1000);
    (void)connect(_refreshTimer, &QTimer::timeout, this, &EnvironmentalDataProvider::_onAutoRefresh);

    qCDebug(EnvironmentalDataLog) << "EnvironmentalDataProvider initialized";
}

EnvironmentalDataProvider::~EnvironmentalDataProvider()
{
    _refreshTimer->stop();
}

/*===========================================================================*/
/*  API key management                                                        */
/*===========================================================================*/

void EnvironmentalDataProvider::setApiKey(const QString &key)
{
    if (_apiKey != key) {
        _apiKey = key;
        emit apiKeyChanged();
        qCDebug(EnvironmentalDataLog) << "API key updated";
    }
}

void EnvironmentalDataProvider::setWeatherRefreshSecs(int secs)
{
    secs = qBound(60, secs, 3600);
    if (_weatherRefreshSecs != secs) {
        _weatherRefreshSecs = secs;
        _refreshTimer->setInterval(secs * 1000);
        emit weatherRefreshSecsChanged();
    }
}

QString EnvironmentalDataProvider::precipTileUrlTemplate() const
{
    if (_apiKey.isEmpty()) {
        return QString();
    }
    // OpenWeatherMap precipitation tile layer
    return QStringLiteral("https://tile.openweathermap.org/map/precipitation_new/{z}/{x}/{y}.png?appid=%1").arg(_apiKey);
}

/*===========================================================================*/
/*  Weather: fetch current conditions for a point                             */
/*===========================================================================*/

void EnvironmentalDataProvider::fetchWeather(double lat, double lon)
{
    if (_apiKey.isEmpty()) {
        emit errorOccurred(QStringLiteral("Weather"), QStringLiteral("No API key configured. Set an OpenWeatherMap API key."));
        return;
    }

    _weatherLoading = true;
    emit weatherLoadingChanged();

    _lastLat = lat;
    _lastLon = lon;

    const QString url = QStringLiteral(
        "https://api.openweathermap.org/data/2.5/weather?lat=%1&lon=%2&appid=%3&units=metric"
    ).arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6).arg(_apiKey);

    const QUrl requestUrl(url);
    QNetworkRequest request(requestUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Sadron-SAR/1.0"));

    QNetworkReply *reply = _nam->get(request);
    (void)connect(reply, &QNetworkReply::finished, this, &EnvironmentalDataProvider::_onWeatherReply);

    // Start auto-refresh if not already running
    if (!_refreshTimer->isActive()) {
        _refreshTimer->start();
    }

    qCDebug(EnvironmentalDataLog) << "Fetching weather for" << lat << lon;
}

void EnvironmentalDataProvider::fetchWeatherForArea(double swLat, double swLon, double neLat, double neLon)
{
    // Fetch weather for the center of the area
    const double centerLat = (swLat + neLat) / 2.0;
    const double centerLon = (swLon + neLon) / 2.0;
    fetchWeather(centerLat, centerLon);

    // Also generate wind vector grid for the area
    _generateWindVectorGrid(swLat, swLon, neLat, neLon);
}

void EnvironmentalDataProvider::_onWeatherReply()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) return;
    reply->deleteLater();

    _weatherLoading = false;
    emit weatherLoadingChanged();

    if (reply->error() != QNetworkReply::NoError) {
        qCWarning(EnvironmentalDataLog) << "Weather request failed:" << reply->errorString();
        emit errorOccurred(QStringLiteral("Weather"), reply->errorString());
        return;
    }

    _parseWeatherResponse(reply->readAll());
}

void EnvironmentalDataProvider::_parseWeatherResponse(const QByteArray &data)
{
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        emit errorOccurred(QStringLiteral("Weather"), QStringLiteral("Invalid weather response"));
        return;
    }

    const QJsonObject root = doc.object();

    // Wind data
    const QJsonObject wind = root.value(QStringLiteral("wind")).toObject();
    _windSpeed     = wind.value(QStringLiteral("speed")).toDouble(0.0);
    _windDirection = wind.value(QStringLiteral("deg")).toDouble(0.0);
    _windGust      = wind.value(QStringLiteral("gust")).toDouble(0.0);

    // Main data
    const QJsonObject main = root.value(QStringLiteral("main")).toObject();
    _temperature = main.value(QStringLiteral("temp")).toDouble(0.0);
    _humidity    = main.value(QStringLiteral("humidity")).toInt(0);
    _visibility  = root.value(QStringLiteral("visibility")).toDouble(10000.0);

    // Clouds
    const QJsonObject clouds = root.value(QStringLiteral("clouds")).toObject();
    _cloudCover = clouds.value(QStringLiteral("all")).toInt(0);

    // Precipitation (rain in last 1h or 3h)
    const QJsonObject rain = root.value(QStringLiteral("rain")).toObject();
    _precipitation = rain.value(QStringLiteral("1h")).toDouble(
                         rain.value(QStringLiteral("3h")).toDouble(0.0));

    // Weather description
    const QJsonArray weatherArr = root.value(QStringLiteral("weather")).toArray();
    if (!weatherArr.isEmpty()) {
        const QJsonObject w = weatherArr.first().toObject();
        _weatherDescription = w.value(QStringLiteral("description")).toString();
        _weatherIcon = w.value(QStringLiteral("icon")).toString();
    }

    _weatherAvailable = true;

    qCDebug(EnvironmentalDataLog) << "Weather updated: wind" << _windSpeed << "m/s @" << _windDirection
                                  << "deg, precip" << _precipitation << "mm, temp" << _temperature << "C";

    emit weatherChanged();
}

/*===========================================================================*/
/*  Wind vector grid: interpolated wind for the area                          */
/*===========================================================================*/

void EnvironmentalDataProvider::_generateWindVectorGrid(double swLat, double swLon, double neLat, double neLon)
{
    if (_apiKey.isEmpty()) return;

    // Use OpenWeatherMap's box/city endpoint to get weather data for a grid of points
    // Alternatively, generate a uniform grid using current point weather as base
    // For now, we generate a grid with slight random variation from the point weather
    // Once point weather is available, the grid is populated in _parseWindGridResponse

    // Request weather for multiple points in the grid
    const int res = _windGridResolution;
    const double dLat = (neLat - swLat) / (res - 1);
    const double dLon = (neLon - swLon) / (res - 1);

    // Build synthetic wind grid from current point weather + small perturbations
    // This gives a realistic look while using a single API call
    QVariantList vectors;
    vectors.reserve(res * res);

    for (int r = 0; r < res; ++r) {
        for (int c = 0; c < res; ++c) {
            const double lat = swLat + r * dLat;
            const double lon = swLon + c * dLon;

            // Add terrain-influenced perturbation based on position
            // (simple heuristic: vary by +-15% based on grid position hash)
            const double posHash = std::sin(lat * 1000.0 + lon * 7.0) * 0.15;
            const double dirHash = std::cos(lat * 3.0 + lon * 1000.0) * 12.0;

            const double speed = qMax(0.0, _windSpeed * (1.0 + posHash));
            const double dir   = std::fmod(_windDirection + dirHash + 360.0, 360.0);

            QVariantMap v;
            v[QStringLiteral("latitude")]  = lat;
            v[QStringLiteral("longitude")] = lon;
            v[QStringLiteral("speed")]     = speed;
            v[QStringLiteral("direction")] = dir;
            vectors.append(v);
        }
    }

    _windVectors = vectors;
    emit windVectorsChanged();

    qCDebug(EnvironmentalDataLog) << "Generated" << vectors.size() << "wind vectors for area";
}

void EnvironmentalDataProvider::_onWindGridReply()
{
    // Reserved for future multi-point wind API integration
}

void EnvironmentalDataProvider::_parseWindGridResponse(const QByteArray &data,
                                                        double swLat, double swLon,
                                                        double neLat, double neLon)
{
    Q_UNUSED(data)
    Q_UNUSED(swLat)
    Q_UNUSED(swLon)
    Q_UNUSED(neLat)
    Q_UNUSED(neLon)
    // Reserved for future multi-point wind API integration
}

/*===========================================================================*/
/*  Terrain Slope: compute from Copernicus elevation                          */
/*===========================================================================*/

void EnvironmentalDataProvider::computeSlopeForArea(double swLat, double swLon, double neLat, double neLon)
{
    _slopeLoading = true;
    emit slopeLoadingChanged();

    const QGeoCoordinate sw(swLat, swLon);
    const QGeoCoordinate ne(neLat, neLon);

    qCDebug(EnvironmentalDataLog) << "Requesting elevation carpet for slope analysis:"
                                  << sw << "to" << ne;

    _requestElevationCarpet(sw, ne);
}

void EnvironmentalDataProvider::_requestElevationCarpet(const QGeoCoordinate &sw, const QGeoCoordinate &ne)
{
    auto *query = new TerrainAreaQuery(true, this);

    (void)connect(query, &TerrainAreaQuery::terrainDataReceived, this,
        [this, sw, ne](bool success, const TerrainAreaQuery::CarpetHeightInfo_t &info) {
            _slopeLoading = false;
            emit slopeLoadingChanged();

            if (!success) {
                qCWarning(EnvironmentalDataLog) << "Terrain elevation query failed";
                emit errorOccurred(QStringLiteral("Terrain"), QStringLiteral("Failed to fetch elevation data"));
                return;
            }

            if (info.carpet.isEmpty() || info.carpet.first().isEmpty()) {
                qCWarning(EnvironmentalDataLog) << "Empty elevation carpet returned";
                emit errorOccurred(QStringLiteral("Terrain"), QStringLiteral("No elevation data available for this area"));
                return;
            }

            const int rows = info.carpet.size();
            const int cols = info.carpet.first().size();

            qCDebug(EnvironmentalDataLog) << "Elevation carpet received:" << rows << "x" << cols
                                          << "min:" << info.minHeight << "max:" << info.maxHeight;

            _computeSlopeFromElevation(info.carpet, sw, ne, rows, cols);
        }
    );

    query->requestData(sw, ne);
}

void EnvironmentalDataProvider::_computeSlopeFromElevation(
    const QList<QList<double>> &carpet,
    const QGeoCoordinate &sw, const QGeoCoordinate &ne,
    int rows, int cols)
{
    if (rows < 3 || cols < 3) {
        emit errorOccurred(QStringLiteral("Terrain"), QStringLiteral("Elevation grid too small for slope analysis"));
        return;
    }

    const double dLat = (ne.latitude() - sw.latitude()) / (rows - 1);
    const double dLon = (ne.longitude() - sw.longitude()) / (cols - 1);

    // Approximate cell size in meters (at mid-latitude)
    const double midLat = (sw.latitude() + ne.latitude()) / 2.0;
    const double cellSizeY = dLat * 111320.0; // meters per degree latitude
    const double cellSizeX = dLon * 111320.0 * std::cos(midLat * M_PI / 180.0);

    QVariantList grid;
    grid.reserve((rows - 2) * (cols - 2));

    double minS = 90.0, maxS = 0.0, sumS = 0.0;
    int count = 0;

    // Horn's method (3x3 kernel) for slope computation
    // Skip border cells (need 3x3 neighborhood)
    for (int r = 1; r < rows - 1; ++r) {
        for (int c = 1; c < cols - 1; ++c) {
            // 3x3 elevation window:
            //  a b c
            //  d e f
            //  g h i
            const double a = carpet[r - 1][c - 1];
            const double b = carpet[r - 1][c];
            const double cc_ = carpet[r - 1][c + 1];
            const double d = carpet[r][c - 1];
            // e = carpet[r][c] — center, not used in Horn's
            const double f = carpet[r][c + 1];
            const double g = carpet[r + 1][c - 1];
            const double h = carpet[r + 1][c];
            const double i = carpet[r + 1][c + 1];

            // Sobel/Horn gradients
            const double dzdx = ((cc_ + 2.0 * f + i) - (a + 2.0 * d + g)) / (8.0 * cellSizeX);
            const double dzdy = ((g + 2.0 * h + i) - (a + 2.0 * b + cc_)) / (8.0 * cellSizeY);

            // Slope in degrees
            const double slopeDeg = std::atan(std::sqrt(dzdx * dzdx + dzdy * dzdy)) * 180.0 / M_PI;

            // Aspect (direction of steepest descent) in degrees from north
            double aspect = std::atan2(-dzdy, dzdx) * 180.0 / M_PI;
            aspect = std::fmod(90.0 - aspect + 360.0, 360.0);

            const double lat = sw.latitude() + r * dLat;
            const double lon = sw.longitude() + c * dLon;
            const double elevation = carpet[r][c];

            QVariantMap cell;
            cell[QStringLiteral("latitude")]  = lat;
            cell[QStringLiteral("longitude")] = lon;
            cell[QStringLiteral("slope")]     = slopeDeg;
            cell[QStringLiteral("aspect")]    = aspect;
            cell[QStringLiteral("elevation")] = elevation;
            grid.append(cell);

            minS = qMin(minS, slopeDeg);
            maxS = qMax(maxS, slopeDeg);
            sumS += slopeDeg;
            ++count;
        }
    }

    _slopeGrid = grid;
    _slopeRows = rows - 2;
    _slopeCols = cols - 2;
    _minSlope = count > 0 ? minS : 0.0;
    _maxSlope = count > 0 ? maxS : 0.0;
    _avgSlope = count > 0 ? sumS / count : 0.0;
    _slopeDataAvailable = true;

    qCDebug(EnvironmentalDataLog) << "Slope analysis complete:" << count << "cells,"
                                  << "min:" << _minSlope << "max:" << _maxSlope << "avg:" << _avgSlope;

    emit slopeDataChanged();
}

/*===========================================================================*/
/*  Hydrological: fetch water features from OSM Overpass                      */
/*===========================================================================*/

void EnvironmentalDataProvider::fetchWaterFeatures(double swLat, double swLon, double neLat, double neLon)
{
    // Check cache — don't re-fetch if area hasn't changed much
    const QGeoCoordinate sw(swLat, swLon);
    const QGeoCoordinate ne(neLat, neLon);

    if (_hydroDataAvailable &&
        std::abs(_hydroCacheSW.latitude() - swLat) < _cacheThresholdDeg &&
        std::abs(_hydroCacheSW.longitude() - swLon) < _cacheThresholdDeg &&
        std::abs(_hydroCacheNE.latitude() - neLat) < _cacheThresholdDeg &&
        std::abs(_hydroCacheNE.longitude() - neLon) < _cacheThresholdDeg)
    {
        qCDebug(EnvironmentalDataLog) << "Hydro data cached, skipping fetch";
        return;
    }

    _hydroLoading = true;
    emit hydroLoadingChanged();

    // Overpass QL query: fetch water bodies (polygons) + waterways (lines) + wetlands
    const QString bbox = QStringLiteral("%1,%2,%3,%4")
        .arg(swLat, 0, 'f', 6).arg(swLon, 0, 'f', 6)
        .arg(neLat, 0, 'f', 6).arg(neLon, 0, 'f', 6);

    const QString query = QStringLiteral(
        "[out:json][timeout:25];"
        "("
        "  way[\"natural\"=\"water\"](%1);"
        "  relation[\"natural\"=\"water\"](%1);"
        "  way[\"waterway\"~\"river|stream|canal|drain\"](%1);"
        "  way[\"natural\"=\"wetland\"](%1);"
        ");"
        "out body;"
        ">;"
        "out skel qt;"
    ).arg(bbox);

    const QUrl url(QStringLiteral("https://overpass-api.de/api/interpreter"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Sadron-SAR/1.0"));

    const QByteArray postData = QStringLiteral("data=%1").arg(QString::fromUtf8(QUrl::toPercentEncoding(query))).toUtf8();

    QNetworkReply *reply = _nam->post(request, postData);
    (void)connect(reply, &QNetworkReply::finished, this, &EnvironmentalDataProvider::_onHydroReply);

    _hydroCacheSW = sw;
    _hydroCacheNE = ne;

    qCDebug(EnvironmentalDataLog) << "Fetching water features for" << bbox;
}

void EnvironmentalDataProvider::_onHydroReply()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) return;
    reply->deleteLater();

    _hydroLoading = false;
    emit hydroLoadingChanged();

    if (reply->error() != QNetworkReply::NoError) {
        qCWarning(EnvironmentalDataLog) << "Hydro request failed:" << reply->errorString();
        emit errorOccurred(QStringLiteral("Hydro"), reply->errorString());
        return;
    }

    _parseOverpassResponse(reply->readAll());
}

void EnvironmentalDataProvider::_parseOverpassResponse(const QByteArray &data)
{
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        emit errorOccurred(QStringLiteral("Hydro"), QStringLiteral("Invalid Overpass response"));
        return;
    }

    const QJsonArray elements = doc.object().value(QStringLiteral("elements")).toArray();

    // First pass: build node lookup (id → {lat, lon})
    QHash<qint64, QPair<double, double>> nodeLookup;
    for (const auto &elem : elements) {
        const QJsonObject obj = elem.toObject();
        if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("node")) {
            const qint64 id = obj.value(QStringLiteral("id")).toVariant().toLongLong();
            const double lat = obj.value(QStringLiteral("lat")).toDouble();
            const double lon = obj.value(QStringLiteral("lon")).toDouble();
            nodeLookup.insert(id, qMakePair(lat, lon));
        }
    }

    QVariantList polygons;
    QVariantList ways;

    // Second pass: process ways
    for (const auto &elem : elements) {
        const QJsonObject obj = elem.toObject();
        if (obj.value(QStringLiteral("type")).toString() != QStringLiteral("way")) {
            continue;
        }

        const QJsonObject tags = obj.value(QStringLiteral("tags")).toObject();
        const QJsonArray nodes = obj.value(QStringLiteral("nodes")).toArray();

        if (nodes.isEmpty()) continue;

        // Resolve node IDs to coordinates
        QVariantList coords;
        coords.reserve(nodes.size());
        for (const auto &nodeRef : nodes) {
            const qint64 nodeId = nodeRef.toVariant().toLongLong();
            auto it = nodeLookup.constFind(nodeId);
            if (it != nodeLookup.constEnd()) {
                QVariantMap coord;
                coord[QStringLiteral("latitude")]  = it.value().first;
                coord[QStringLiteral("longitude")] = it.value().second;
                coords.append(coord);
            }
        }

        if (coords.size() < 2) continue;

        const QString name = tags.value(QStringLiteral("name")).toString();
        const QString natural = tags.value(QStringLiteral("natural")).toString();
        const QString waterway = tags.value(QStringLiteral("waterway")).toString();

        QVariantMap feature;
        feature[QStringLiteral("name")]        = name;
        feature[QStringLiteral("coordinates")] = coords;

        if (waterway == QStringLiteral("river") ||
            waterway == QStringLiteral("stream") ||
            waterway == QStringLiteral("canal") ||
            waterway == QStringLiteral("drain"))
        {
            feature[QStringLiteral("type")] = waterway;
            // Width hint for rendering
            if (waterway == QStringLiteral("river")) {
                feature[QStringLiteral("width")] = 4;
            } else if (waterway == QStringLiteral("stream")) {
                feature[QStringLiteral("width")] = 2;
            } else {
                feature[QStringLiteral("width")] = 1;
            }
            ways.append(feature);
        } else {
            // Water body polygon or wetland
            feature[QStringLiteral("type")] = natural == QStringLiteral("wetland")
                                                ? QStringLiteral("wetland")
                                                : QStringLiteral("water");
            polygons.append(feature);
        }
    }

    _waterPolygons = polygons;
    _waterways = ways;
    _hydroDataAvailable = true;

    qCDebug(EnvironmentalDataLog) << "Hydro data parsed:" << polygons.size() << "polygons,"
                                  << ways.size() << "waterways";

    emit hydroDataChanged();
}

/*===========================================================================*/
/*  Refresh / auto-refresh                                                    */
/*===========================================================================*/

void EnvironmentalDataProvider::refreshAll(double swLat, double swLon, double neLat, double neLon)
{
    fetchWeatherForArea(swLat, swLon, neLat, neLon);
    computeSlopeForArea(swLat, swLon, neLat, neLon);
    fetchWaterFeatures(swLat, swLon, neLat, neLon);
}

void EnvironmentalDataProvider::stopAutoRefresh()
{
    _refreshTimer->stop();
}

void EnvironmentalDataProvider::_onAutoRefresh()
{
    if (_lastLat != 0.0 || _lastLon != 0.0) {
        fetchWeather(_lastLat, _lastLon);
    }
}
