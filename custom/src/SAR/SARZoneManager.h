#pragma once

#include <QtCore/QObject>
#include <QtCore/QLoggingCategory>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QPair>
#include <QtPositioning/QGeoCoordinate>
#include <QtPositioning/QGeoRectangle>
#include <QtQmlIntegration/QtQmlIntegration>

Q_DECLARE_LOGGING_CATEGORY(SARZoneManagerLog)

#include "QmlObjectListModel.h"
#include "QGCMapPolygon.h"

// Represents a single SAR search zone assigned to a vehicle
class SARZone : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")

    Q_PROPERTY(int              zoneId          READ zoneId         CONSTANT)
    Q_PROPERTY(QString          name            READ name           WRITE setName           NOTIFY nameChanged)
    Q_PROPERTY(int              assignedVehicle READ assignedVehicle WRITE setAssignedVehicle NOTIFY assignedVehicleChanged)
    Q_PROPERTY(double           progress        READ progress       WRITE setProgress       NOTIFY progressChanged)
    Q_PROPERTY(ZoneStatus       status          READ status         WRITE setStatus         NOTIFY statusChanged)
    Q_PROPERTY(QVariantList     polygon         READ polygon        WRITE setPolygon        NOTIFY polygonChanged)
    Q_PROPERTY(double           areaSqM         READ areaSqM        NOTIFY polygonChanged)
    Q_PROPERTY(QGCMapPolygon*   mapPolygon      READ mapPolygon     CONSTANT)
    Q_PROPERTY(QColor           displayColor    READ displayColor   NOTIFY statusChanged)

    // Per-zone override properties
    Q_PROPERTY(bool             useGlobalParams READ useGlobalParams WRITE setUseGlobalParams NOTIFY useGlobalParamsChanged)
    Q_PROPERTY(int              searchPattern   READ searchPattern   WRITE setSearchPattern   NOTIFY searchPatternChanged)
    Q_PROPERTY(double           searchAltitude  READ searchAltitude  WRITE setSearchAltitude  NOTIFY searchAltitudeChanged)
    Q_PROPERTY(double           searchSpeed     READ searchSpeed     WRITE setSearchSpeed     NOTIFY searchSpeedChanged)
    Q_PROPERTY(double           trackSpacing    READ trackSpacing    WRITE setTrackSpacing    NOTIFY trackSpacingChanged)
    Q_PROPERTY(QVariantList     transectPath    READ transectPath    NOTIFY transectPathChanged)
    Q_PROPERTY(bool             selected        READ selected        NOTIFY selectedChanged)

public:
    enum ZoneStatus {
        Pending = 0,
        Active,
        Paused,
        Completed,
        Reassigning,
        Investigating       // Drone diverted to investigate a target — zone paused awaiting return
    };
    Q_ENUM(ZoneStatus)

    explicit SARZone(int zoneId, QObject *parent = nullptr);

    int             zoneId() const { return _zoneId; }
    QString         name() const { return _name; }
    int             assignedVehicle() const { return _assignedVehicle; }
    double          progress() const { return _progress; }
    ZoneStatus      status() const { return _status; }
    QVariantList    polygon() const;
    double          areaSqM() const;
    QGCMapPolygon*  mapPolygon() const { return _mapPolygon; }
    QColor          displayColor() const;

    // Per-zone overrides
    bool            useGlobalParams() const { return _useGlobalParams; }
    int             searchPattern() const { return _searchPattern; }
    double          searchAltitude() const { return _searchAltitude; }
    double          searchSpeed() const { return _searchSpeed; }
    double          trackSpacing() const { return _trackSpacing; }
    QVariantList    transectPath() const { return _transectPath; }
    bool            selected() const { return _selected; }

    void setName(const QString &name);
    void setAssignedVehicle(int vehicleId);
    void setProgress(double progress);
    void setStatus(ZoneStatus status);
    void setPolygon(const QVariantList &polygon);

    // Per-zone override setters
    void setUseGlobalParams(bool useGlobal);
    void setSearchPattern(int pattern);
    void setSearchAltitude(double altitude);
    void setSearchSpeed(double speed);
    void setTrackSpacing(double spacing);
    void setTransectPath(const QVariantList &path);
    void setSelected(bool selected);

    QJsonObject toJson() const;
    static SARZone *fromJson(const QJsonObject &json, QObject *parent);

signals:
    void nameChanged();
    void assignedVehicleChanged();
    void progressChanged();
    void statusChanged();
    void polygonChanged();
    void useGlobalParamsChanged();
    void searchPatternChanged();
    void searchAltitudeChanged();
    void searchSpeedChanged();
    void trackSpacingChanged();
    void transectPathChanged();
    void selectedChanged();

private:
    int             _zoneId;
    QString         _name;
    int             _assignedVehicle = -1;
    double          _progress = 0.0;
    ZoneStatus      _status = Pending;
    QGCMapPolygon*  _mapPolygon = nullptr;

    // Per-zone overrides (-1 = use global)
    bool            _useGlobalParams = true;
    int             _searchPattern = -1;
    double          _searchAltitude = -1.0;
    double          _searchSpeed = -1.0;
    double          _trackSpacing = -1.0;
    QVariantList    _transectPath;
    bool            _selected = false;
};

/*===========================================================================*/

// Manages SAR search zones: creation, partitioning, assignment, and tracking
class SARZoneManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")

    Q_PROPERTY(QmlObjectListModel *zones            READ zones              CONSTANT)
    Q_PROPERTY(int totalZones                       READ totalZones         NOTIFY zonesChanged)
    Q_PROPERTY(double overallProgress               READ overallProgress    NOTIFY progressChanged)
    Q_PROPERTY(QGCMapPolygon* searchAreaPolygon     READ searchAreaPolygon  CONSTANT)
    Q_PROPERTY(bool hasSearchArea                   READ hasSearchArea      NOTIFY searchAreaChanged)
    Q_PROPERTY(SARZone* selectedZone                READ selectedZone       NOTIFY selectedZoneChanged)
    Q_PROPERTY(int zoneAssignmentGeneration         READ zoneAssignmentGeneration NOTIFY zoneAssignmentChanged)
    Q_PROPERTY(double lastCoverageRatio             READ lastCoverageRatio  NOTIFY coverageValidated)

public:
    enum PartitionStrategy {
        GridPartition = 0,      // Adaptive grid subdivision with oversample + merge
        VoronoiPartition,       // Voronoi-based around drone positions
        StripPartition,         // Parallel strips (good for parallel track)
    };
    Q_ENUM(PartitionStrategy)

    explicit SARZoneManager(QObject *parent = nullptr);
    ~SARZoneManager();

    QmlObjectListModel *zones() const { return _zones; }
    int totalZones() const;
    double overallProgress() const;
    QGCMapPolygon* searchAreaPolygon() const { return _searchAreaPolygon; }
    bool hasSearchArea() const;
    SARZone* selectedZone() const { return _selectedZone; }
    int zoneAssignmentGeneration() const { return _zoneAssignmentGen; }
    double lastCoverageRatio() const { return _lastCoverageRatio; }

    // Zone creation and partitioning
    Q_INVOKABLE void partitionArea(const QVariantList &boundaryPolygon, int droneCount, int strategy = GridPartition);
    Q_INVOKABLE void partitionSearchArea(int droneCount, int strategy = GridPartition);
    Q_INVOKABLE SARZone *addZone(const QVariantList &polygon, const QString &name = QString());
    Q_INVOKABLE void removeZone(int zoneId);
    Q_INVOKABLE void clearAllZones();

    /// Import plan items (polygons from Survey/CorridorScan/StructureScan and
    /// waypoints from SimpleMissionItems) as the SAR search area polygon.
    /// Multiple polygons are merged into a single convex hull.
    /// @return description string summarising what was imported (for UI feedback)
    Q_INVOKABLE QString importFromPlanItems(QmlObjectListModel *visualItems);

    /// Compute the fraction of the search area covered by all zones (0.0 to 1.0).
    /// Useful for verifying partition completeness.
    Q_INVOKABLE double coverageRatio() const;

    // Zone selection
    Q_INVOKABLE void selectZone(int zoneId);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE SARZone *getZone(int zoneId) const;

    // Zone assignment
    Q_INVOKABLE void assignZoneToVehicle(int zoneId, int vehicleId);
    Q_INVOKABLE void reassignZone(int zoneId, int newVehicleId);
    Q_INVOKABLE SARZone *zoneForVehicle(int vehicleId) const;

    // Notify helpers (avoids emitting signals from outside the class)
    void notifyProgressChanged();

    // Serialization
    QJsonArray toJson() const;
    void fromJson(const QJsonArray &json);

signals:
    void zonesChanged();
    void progressChanged();
    void zoneAssigned(int zoneId, int vehicleId);
    void zoneCompleted(int zoneId);
    void searchAreaChanged();
    void selectedZoneChanged();
    void zoneAssignmentChanged();
    void coverageValidated(double ratio);
    void partitionWarning(const QString &message);

private:
    // ── Partition strategies ──
    void _partitionGrid(const QVariantList &boundary, int count);
    void _partitionStrips(const QVariantList &boundary, int count);
    void _partitionVoronoi(const QVariantList &boundary, int count);

    // ── Geometry helpers ──
    QGeoRectangle _boundingRect(const QVariantList &polygon) const;
    static QList<QGeoCoordinate> _computeConvexHull(const QList<QGeoCoordinate> &points);

    /// Weiler-Atherton polygon clipping — handles both convex AND concave polygons.
    /// Returns a list of clipped polygon fragments (concave clipping can produce
    /// multiple disjoint result polygons).
    static QList<QVariantList> _clipPolygonWA(const QVariantList &subject, const QVariantList &clip);

    /// Legacy single-result clipper (returns the largest fragment or empty).
    static QVariantList _clipPolygon(const QVariantList &subject, const QVariantList &clip);

    /// Point-in-polygon test (ray-casting algorithm).
    static bool _pointInPolygon(const QGeoCoordinate &point, const QVariantList &polygon);

    /// Compute signed area of a polygon in degrees² (positive = CCW).
    static double _signedArea(const QList<QGeoCoordinate> &poly);

    /// Compute the centroid of a polygon.
    static QGeoCoordinate _centroid(const QVariantList &polygon);

    /// Compute approximate area of a polygon in square metres.
    static double _approxAreaSqM(const QVariantList &polygon);

    // ── Coverage validation ──
    void _validateCoverage(const QVariantList &boundary);

    int _nextZoneId = 1;

    QmlObjectListModel *_zones = nullptr;
    QGCMapPolygon      *_searchAreaPolygon = nullptr;
    SARZone            *_selectedZone = nullptr;
    int                 _zoneAssignmentGen = 0;
    double              _lastCoverageRatio = 0.0;
};
