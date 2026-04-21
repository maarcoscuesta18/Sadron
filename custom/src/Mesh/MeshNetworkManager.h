#pragma once

#include <QtCore/QObject>
#include <QtCore/QLoggingCategory>
#include <QtCore/QHash>
#include <QtCore/QTimer>
#include <QtPositioning/QGeoCoordinate>
#include <QtQmlIntegration/QtQmlIntegration>

typedef struct __mavlink_message mavlink_message_t;

Q_DECLARE_LOGGING_CATEGORY(MeshNetworkManagerLog)

class Vehicle;

#include "QmlObjectListModel.h"

// Represents a single node in the mesh network
class MeshNode : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")

    Q_PROPERTY(int              vehicleId       READ vehicleId      CONSTANT)
    Q_PROPERTY(QGeoCoordinate   position        READ position       NOTIFY positionChanged)
    Q_PROPERTY(NodeStatus       status          READ status         NOTIFY statusChanged)
    Q_PROPERTY(int              signalStrength  READ signalStrength NOTIFY signalStrengthChanged)
    Q_PROPERTY(double           batteryPercent  READ batteryPercent NOTIFY batteryChanged)
    Q_PROPERTY(int              linkCount       READ linkCount      NOTIFY linksChanged)
    Q_PROPERTY(QVariantList     connectedNodes  READ connectedNodes NOTIFY linksChanged)
    Q_PROPERTY(double           latencyMs       READ latencyMs      NOTIFY latencyChanged)

public:
    enum NodeStatus {
        Online = 0,
        Degraded,       // Partial connectivity
        Offline,        // No communication
        Returning,      // RTL in progress
    };
    Q_ENUM(NodeStatus)

    explicit MeshNode(int vehicleId, QObject *parent = nullptr);

    int             vehicleId() const { return _vehicleId; }
    QGeoCoordinate  position() const { return _position; }
    NodeStatus      status() const { return _status; }
    int             signalStrength() const { return _signalStrength; }
    double          batteryPercent() const { return _batteryPercent; }
    int             linkCount() const { return _connectedNodeIds.size(); }
    QVariantList    connectedNodes() const;
    double          latencyMs() const { return _latencyMs; }

    void setPosition(const QGeoCoordinate &pos);
    void setStatus(NodeStatus status);
    void setSignalStrength(int strength);
    void setBatteryPercent(double percent);
    void addConnectedNode(int nodeId);
    void removeConnectedNode(int nodeId);
    void clearConnectedNodes();
    void setLatencyMs(double ms);

    void updateLastSeen();
    qint64 msSinceLastSeen() const;

signals:
    void positionChanged();
    void statusChanged();
    void signalStrengthChanged();
    void batteryChanged();
    void linksChanged();
    void latencyChanged();

private:
    void _invalidateConnectedNodesCache();

    int             _vehicleId;
    QGeoCoordinate  _position;
    NodeStatus      _status = Online;
    int             _signalStrength = 100;  // 0-100
    double          _batteryPercent = 100.0;
    QList<int>      _connectedNodeIds;
    mutable QVariantList _connectedNodesCache;  // 5B: cached QVariantList
    mutable bool    _connectedNodesCacheDirty = true;
    double          _latencyMs = 0.0;
    qint64          _lastSeenMs = 0;
};

/*===========================================================================*/

// Manages the mesh network topology and inter-drone communication
class MeshNetworkManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")

    Q_PROPERTY(QmlObjectListModel  *nodes           READ nodes          CONSTANT)
    Q_PROPERTY(int                  nodeCount       READ nodeCount      NOTIFY topologyChanged)
    Q_PROPERTY(int                  onlineCount     READ onlineCount    NOTIFY topologyChanged)
    Q_PROPERTY(MeshHealth           meshHealth      READ meshHealth     NOTIFY healthChanged)
    Q_PROPERTY(bool                 gcsConnected    READ gcsConnected   NOTIFY gcsConnectionChanged)
    Q_PROPERTY(double               maxRangeMeters  READ maxRangeMeters WRITE setMaxRangeMeters NOTIFY maxRangeChanged)
    Q_PROPERTY(int                  nodeTimeoutMs   READ nodeTimeoutMs  WRITE setNodeTimeoutMs  NOTIFY timeoutChanged)
    Q_PROPERTY(int                  nodeDegradedMs  READ nodeDegradedMs WRITE setNodeDegradedMs NOTIFY timeoutChanged)

public:
    enum MeshHealth {
        Healthy = 0,        // All nodes connected
        Partial,            // Some nodes degraded
        Critical,           // Multiple nodes offline
        Disconnected,       // No mesh connectivity
    };
    Q_ENUM(MeshHealth)

    explicit MeshNetworkManager(QObject *parent = nullptr);
    ~MeshNetworkManager();

    QmlObjectListModel *nodes() const { return _nodes; }
    int nodeCount() const;
    int onlineCount() const;
    MeshHealth meshHealth() const;
    bool gcsConnected() const { return _gcsConnected; }
    double maxRangeMeters() const { return _maxRangeMeters; }
    int    nodeTimeoutMs() const  { return _nodeTimeoutMs; }
    int    nodeDegradedMs() const { return _nodeDegradedMs; }

    void setMaxRangeMeters(double range);
    void setNodeTimeoutMs(int ms);
    void setNodeDegradedMs(int ms);

    // Node management
    Q_INVOKABLE MeshNode *getNode(int vehicleId) const;
    Q_INVOKABLE void removeNode(int vehicleId);

    // Topology queries
    Q_INVOKABLE bool areNodesConnected(int vehicleId1, int vehicleId2) const;
    Q_INVOKABLE double distanceBetweenNodes(int vehicleId1, int vehicleId2) const;
    Q_INVOKABLE QVariantList getMeshTopology() const;

    // Broadcast a message to all mesh nodes
    Q_INVOKABLE void broadcastTargetSpotted(const QGeoCoordinate &coordinate, int spottedByVehicle);
    Q_INVOKABLE void broadcastZoneUpdate(int zoneId, double progress);

    // Process incoming MAVLink messages for mesh state
    void processMavlinkMessage(Vehicle *vehicle, const mavlink_message_t &message);

signals:
    void topologyChanged();
    void healthChanged();
    void gcsConnectionChanged();
    void maxRangeChanged();
    void timeoutChanged();
    void nodeAdded(int vehicleId);
    void nodeRemoved(int vehicleId);
    void nodeLost(int vehicleId);
    void targetBroadcastReceived(const QGeoCoordinate &coordinate, int spottedByVehicle);
    void zoneUpdateReceived(int zoneId, double progress);

private slots:
    void _checkNodeHealth();

private:
    MeshNode *_getOrCreateNode(int vehicleId);
    void _updateTopology();
    void _evaluateHealth();

    QmlObjectListModel *_nodes = nullptr;
    QHash<int, MeshNode *> _nodeById;   // O(1) lookup by vehicleId (2B optimization)
    QTimer *_healthCheckTimer = nullptr;
    QTimer *_topologyTimer = nullptr;    // Debounce topology recalculation (1A optimization)
    bool _topologyDirty = false;
    bool _gcsConnected = false;
    double _maxRangeMeters = 2000.0;    // Default max mesh range

    static constexpr int kHealthCheckIntervalMs = 2000;
    static constexpr int kTopologyDebounceMs = 2000;  // Recalc topology at most every 2s
    static constexpr double kNodeMoveThresholdM = 1.0; // Min movement to trigger update (1D)
    int _nodeTimeoutMs = 10000;         // Node considered offline after 10s (configurable)
    int _nodeDegradedMs = 5000;         // Node considered degraded after 5s (configurable)
};
