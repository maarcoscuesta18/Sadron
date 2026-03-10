#pragma once

#include <QtCore/QObject>
#include <QtCore/QLoggingCategory>
#include <QtCore/QHash>
#include <QtCore/QSet>
#include <QtCore/QPair>
#include <QtPositioning/QGeoCoordinate>
#include <QtQmlIntegration/QtQmlIntegration>

typedef struct __mavlink_message mavlink_message_t;

Q_DECLARE_LOGGING_CATEGORY(SARCoverageTrackerLog)

class Vehicle;

// Tracks which areas have been searched by recording vehicle trajectories
// and computing coverage based on camera FOV
class SARCoverageTracker : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")

    Q_PROPERTY(double       coveragePercent     READ coveragePercent    NOTIFY coverageChanged)
    Q_PROPERTY(int          cellsSearched       READ cellsSearched     NOTIFY coverageChanged)
    Q_PROPERTY(int          totalCells          READ totalCells        NOTIFY gridChanged)
    Q_PROPERTY(double       cellSizeMeters      READ cellSizeMeters    WRITE setCellSizeMeters NOTIFY gridChanged)
    Q_PROPERTY(double       cameraFovMeters     READ cameraFovMeters   WRITE setCameraFovMeters NOTIFY cameraFovChanged)
    Q_PROPERTY(QVariantList coveredCells        READ coveredCells      NOTIFY coverageChanged)

public:
    explicit SARCoverageTracker(QObject *parent = nullptr);
    ~SARCoverageTracker();

    double coveragePercent() const;
    int cellsSearched() const { return _coveredCells.size(); }
    int totalCells() const { return _totalCells; }
    double cellSizeMeters() const { return _cellSizeMeters; }
    double cameraFovMeters() const { return _cameraFovMeters; }
    QVariantList coveredCells() const;

    void setCellSizeMeters(double size);
    void setCameraFovMeters(double fov);

    // Define the search area grid
    Q_INVOKABLE void initializeGrid(const QGeoCoordinate &origin, double widthMeters, double heightMeters);
    Q_INVOKABLE void initializeGridFromPolygon(const QVariantList &polygon);

    // Manual coverage marking
    Q_INVOKABLE void markCellCovered(int gridX, int gridY);
    Q_INVOKABLE bool isCellCovered(int gridX, int gridY) const;

    // Reset coverage data
    Q_INVOKABLE void resetCoverage();

    // Process MAVLink position messages to auto-track coverage
    void processMavlinkMessage(Vehicle *vehicle, const mavlink_message_t &message);

    // Get coverage data for a specific zone polygon
    Q_INVOKABLE double coverageForZone(const QVariantList &zonePolygon) const;

signals:
    void coverageChanged();
    void gridChanged();
    void cameraFovChanged();

private:
    void _markPositionCovered(const QGeoCoordinate &position);
    QPair<int, int> _coordToCell(const QGeoCoordinate &coord) const;
    QGeoCoordinate _cellToCoord(int gridX, int gridY) const;
    bool _isPointInPolygon(const QGeoCoordinate &point, const QVariantList &polygon) const;

    QGeoCoordinate _gridOrigin;
    double _cellSizeMeters = 10.0;      // Each grid cell is 10m x 10m
    double _cameraFovMeters = 20.0;     // Camera FOV footprint at flight altitude
    int _gridWidth = 0;                 // Number of cells wide
    int _gridHeight = 0;                // Number of cells tall
    int _totalCells = 0;

    // Set of covered cell coordinates (gridX, gridY)
    QSet<QPair<int, int>> _coveredCells;

    // Track last known position per vehicle to avoid redundant updates
    QHash<int, QGeoCoordinate> _lastVehiclePositions;
};
