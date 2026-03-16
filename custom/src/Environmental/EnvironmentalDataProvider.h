#pragma once

#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtCore/QVariantList>
#include <QtPositioning/QGeoCoordinate>

class QNetworkAccessManager;
class QNetworkReply;

Q_DECLARE_LOGGING_CATEGORY(EnvironmentalDataLog)

/// Centralized provider for environmental data overlays:
///  - Real-time weather (Open-Meteo): wind vectors, precipitation, conditions
///  - Terrain slope analysis (Copernicus elevation → Sobel gradient)
///  - Hydrological data (OpenStreetMap Overpass): water features
class EnvironmentalDataProvider : public QObject
{
    Q_OBJECT

    // ── Weather properties ──
    Q_PROPERTY(bool    weatherAvailable      READ weatherAvailable      NOTIFY weatherChanged)
    Q_PROPERTY(bool    weatherLoading        READ weatherLoading        NOTIFY weatherLoadingChanged)
    Q_PROPERTY(double  windSpeed             READ windSpeed             NOTIFY weatherChanged)
    Q_PROPERTY(double  windDirection         READ windDirection         NOTIFY weatherChanged)
    Q_PROPERTY(double  windGust              READ windGust              NOTIFY weatherChanged)
    Q_PROPERTY(double  precipitation         READ precipitation         NOTIFY weatherChanged)
    Q_PROPERTY(double  temperature           READ temperature           NOTIFY weatherChanged)
    Q_PROPERTY(double  visibility            READ visibility            NOTIFY weatherChanged)
    Q_PROPERTY(int     humidity              READ humidity              NOTIFY weatherChanged)
    Q_PROPERTY(int     cloudCover            READ cloudCover            NOTIFY weatherChanged)
    Q_PROPERTY(QString weatherDescription    READ weatherDescription    NOTIFY weatherChanged)
    Q_PROPERTY(QString weatherIcon           READ weatherIcon           NOTIFY weatherChanged)
    Q_PROPERTY(QVariantList windVectors      READ windVectors           NOTIFY windVectorsChanged)
    Q_PROPERTY(int     weatherRefreshSecs    READ weatherRefreshSecs    WRITE setWeatherRefreshSecs NOTIFY weatherRefreshSecsChanged)

    // ── Terrain slope properties ──
    Q_PROPERTY(bool         slopeDataAvailable READ slopeDataAvailable NOTIFY slopeDataChanged)
    Q_PROPERTY(bool         slopeLoading       READ slopeLoading       NOTIFY slopeLoadingChanged)
    Q_PROPERTY(QVariantList slopeGrid          READ slopeGrid          NOTIFY slopeDataChanged)
    Q_PROPERTY(double       minSlope           READ minSlope            NOTIFY slopeDataChanged)
    Q_PROPERTY(double       maxSlope           READ maxSlope            NOTIFY slopeDataChanged)
    Q_PROPERTY(double       avgSlope           READ avgSlope            NOTIFY slopeDataChanged)
    Q_PROPERTY(int          slopeRows          READ slopeRows          NOTIFY slopeDataChanged)
    Q_PROPERTY(int          slopeCols          READ slopeCols          NOTIFY slopeDataChanged)

    // ── Hydrological properties ──
    Q_PROPERTY(bool         hydroDataAvailable READ hydroDataAvailable NOTIFY hydroDataChanged)
    Q_PROPERTY(bool         hydroLoading       READ hydroLoading       NOTIFY hydroLoadingChanged)
    Q_PROPERTY(QVariantList waterPolygons      READ waterPolygons      NOTIFY hydroDataChanged)
    Q_PROPERTY(QVariantList waterways          READ waterways          NOTIFY hydroDataChanged)
    Q_PROPERTY(int          totalWaterFeatures READ totalWaterFeatures NOTIFY hydroDataChanged)

public:
    explicit EnvironmentalDataProvider(QObject *parent = nullptr);
    ~EnvironmentalDataProvider();

    // ── Weather getters ──
    bool    weatherAvailable() const { return _weatherAvailable; }
    bool    weatherLoading() const { return _weatherLoading; }
    double  windSpeed() const { return _windSpeed; }
    double  windDirection() const { return _windDirection; }
    double  windGust() const { return _windGust; }
    double  precipitation() const { return _precipitation; }
    double  temperature() const { return _temperature; }
    double  visibility() const { return _visibility; }
    int     humidity() const { return _humidity; }
    int     cloudCover() const { return _cloudCover; }
    QString weatherDescription() const { return _weatherDescription; }
    QString weatherIcon() const { return _weatherIcon; }
    QVariantList windVectors() const { return _windVectors; }
    int     weatherRefreshSecs() const { return _weatherRefreshSecs; }

    void setWeatherRefreshSecs(int secs);

    // ── Terrain slope getters ──
    bool         slopeDataAvailable() const { return _slopeDataAvailable; }
    bool         slopeLoading() const { return _slopeLoading; }
    QVariantList slopeGrid() const { return _slopeGrid; }
    double       minSlope() const { return _minSlope; }
    double       maxSlope() const { return _maxSlope; }
    double       avgSlope() const { return _avgSlope; }
    int          slopeRows() const { return _slopeRows; }
    int          slopeCols() const { return _slopeCols; }

    // ── Hydrological getters ──
    bool         hydroDataAvailable() const { return _hydroDataAvailable; }
    bool         hydroLoading() const { return _hydroLoading; }
    QVariantList waterPolygons() const { return _waterPolygons; }
    QVariantList waterways() const { return _waterways; }
    int          totalWaterFeatures() const { return _waterPolygons.size() + _waterways.size(); }

    // ── Invokable methods (called from QML) ──
    Q_INVOKABLE void fetchWeather(double lat, double lon);
    Q_INVOKABLE void fetchWeatherForArea(double swLat, double swLon, double neLat, double neLon);
    Q_INVOKABLE void computeSlopeForArea(double swLat, double swLon, double neLat, double neLon);
    Q_INVOKABLE void fetchWaterFeatures(double swLat, double swLon, double neLat, double neLon);
    Q_INVOKABLE void refreshAll(double swLat, double swLon, double neLat, double neLon);
    Q_INVOKABLE void stopAutoRefresh();

signals:
    void weatherChanged();
    void weatherLoadingChanged();
    void windVectorsChanged();
    void weatherRefreshSecsChanged();
    void slopeDataChanged();
    void slopeLoadingChanged();
    void hydroDataChanged();
    void hydroLoadingChanged();
    void errorOccurred(const QString &source, const QString &message);

private slots:
    void _onWeatherReply();
    void _onWindGridReply();
    void _onHydroReply();
    void _onAutoRefresh();

private:
    // ── Weather internals ──
    void _parseWeatherResponse(const QByteArray &data);
    void _generateWindVectorGrid(double swLat, double swLon, double neLat, double neLon);
    void _parseWindGridResponse(const QByteArray &data, double swLat, double swLon, double neLat, double neLon);

    // ── Terrain slope internals ──
    void _requestElevationCarpet(const QGeoCoordinate &sw, const QGeoCoordinate &ne);
    void _computeSlopeFromElevation(const QList<QList<double>> &carpet,
                                    const QGeoCoordinate &sw, const QGeoCoordinate &ne,
                                    int rows, int cols);

    // ── Hydro internals ──
    void _parseOverpassResponse(const QByteArray &data);

    // ── Network ──
    QNetworkAccessManager *_nam = nullptr;

    // ── Weather state ──
    bool    _weatherAvailable = false;
    bool    _weatherLoading = false;
    double  _windSpeed = 0.0;
    double  _windDirection = 0.0;
    double  _windGust = 0.0;
    double  _precipitation = 0.0;
    double  _temperature = 0.0;
    double  _visibility = 0.0;
    int     _humidity = 0;
    int     _cloudCover = 0;
    QString _weatherDescription;
    QString _weatherIcon;
    QVariantList _windVectors;
    int     _weatherRefreshSecs = 300; // 5 min default

    // ── Auto-refresh ──
    QTimer *_refreshTimer = nullptr;
    double  _lastLat = 0.0;
    double  _lastLon = 0.0;

    // ── Terrain slope state ──
    bool         _slopeDataAvailable = false;
    bool         _slopeLoading = false;
    QVariantList _slopeGrid;
    double       _minSlope = 0.0;
    double       _maxSlope = 0.0;
    double       _avgSlope = 0.0;
    int          _slopeRows = 0;
    int          _slopeCols = 0;

    // ── Hydro state ──
    bool         _hydroDataAvailable = false;
    bool         _hydroLoading = false;
    QVariantList _waterPolygons;
    QVariantList _waterways;

    // ── Hydro cache (avoid re-fetching same area) ──
    QGeoCoordinate _hydroCacheSW;
    QGeoCoordinate _hydroCacheNE;

    static constexpr int    _windGridResolution = 5;   // 5x5 grid of wind vectors
    static constexpr double _cacheThresholdDeg  = 0.001; // ~100m — cache invalidation threshold
};
