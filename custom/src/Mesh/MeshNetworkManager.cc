#include "MeshNetworkManager.h"
#include "QmlObjectListModel.h"
#include "QGCLoggingCategory.h"
#include "Vehicle.h"

#include <QGCMAVLink.h>
#include <QtCore/QDateTime>

QGC_LOGGING_CATEGORY(MeshNetworkManagerLog, "Sadron.MeshNetworkManager")

/*===========================================================================*/
// MeshNode
/*===========================================================================*/

MeshNode::MeshNode(int vehicleId, QObject *parent)
    : QObject(parent)
    , _vehicleId(vehicleId)
    , _lastSeenMs(QDateTime::currentMSecsSinceEpoch())
{
}

QVariantList MeshNode::connectedNodes() const
{
    QVariantList list;
    for (int id : _connectedNodeIds) {
        list.append(id);
    }
    return list;
}

void MeshNode::setPosition(const QGeoCoordinate &pos) { if (_position != pos) { _position = pos; emit positionChanged(); } }
void MeshNode::setStatus(NodeStatus status) { if (_status != status) { _status = status; emit statusChanged(); } }
void MeshNode::setSignalStrength(int strength) { strength = qBound(0, strength, 100); if (_signalStrength != strength) { _signalStrength = strength; emit signalStrengthChanged(); } }
void MeshNode::setBatteryPercent(double percent) { if (!qFuzzyCompare(_batteryPercent, percent)) { _batteryPercent = qBound(0.0, percent, 100.0); emit batteryChanged(); } }
void MeshNode::setLatencyMs(double ms) { if (!qFuzzyCompare(_latencyMs, ms)) { _latencyMs = ms; emit latencyChanged(); } }

void MeshNode::addConnectedNode(int nodeId)
{
    if (!_connectedNodeIds.contains(nodeId)) {
        _connectedNodeIds.append(nodeId);
        emit linksChanged();
    }
}

void MeshNode::removeConnectedNode(int nodeId)
{
    if (_connectedNodeIds.removeOne(nodeId)) {
        emit linksChanged();
    }
}

void MeshNode::clearConnectedNodes()
{
    if (!_connectedNodeIds.isEmpty()) {
        _connectedNodeIds.clear();
        emit linksChanged();
    }
}

void MeshNode::updateLastSeen()
{
    _lastSeenMs = QDateTime::currentMSecsSinceEpoch();
}

qint64 MeshNode::msSinceLastSeen() const
{
    return QDateTime::currentMSecsSinceEpoch() - _lastSeenMs;
}

/*===========================================================================*/
// MeshNetworkManager
/*===========================================================================*/

MeshNetworkManager::MeshNetworkManager(QObject *parent)
    : QObject(parent)
    , _nodes(new QmlObjectListModel(this))
    , _healthCheckTimer(new QTimer(this))
{
    connect(_healthCheckTimer, &QTimer::timeout, this, &MeshNetworkManager::_checkNodeHealth);
    _healthCheckTimer->start(kHealthCheckIntervalMs);
}

MeshNetworkManager::~MeshNetworkManager()
{
}

int MeshNetworkManager::nodeCount() const
{
    return _nodes->count();
}

int MeshNetworkManager::onlineCount() const
{
    int count = 0;
    for (int i = 0; i < _nodes->count(); i++) {
        auto *node = qobject_cast<MeshNode *>(_nodes->get(i));
        if (node && node->status() == MeshNode::Online) count++;
    }
    return count;
}

MeshNetworkManager::MeshHealth MeshNetworkManager::meshHealth() const
{
    if (_nodes->count() == 0) return Disconnected;

    int online = 0, degraded = 0, offline = 0;
    for (int i = 0; i < _nodes->count(); i++) {
        auto *node = qobject_cast<MeshNode *>(_nodes->get(i));
        if (!node) continue;
        switch (node->status()) {
        case MeshNode::Online:   online++; break;
        case MeshNode::Degraded: degraded++; break;
        case MeshNode::Offline:
        case MeshNode::Returning: offline++; break;
        }
    }

    if (online == _nodes->count()) return Healthy;
    if (offline > _nodes->count() / 2) return Critical;
    if (offline == _nodes->count()) return Disconnected;
    return Partial;
}

void MeshNetworkManager::setMaxRangeMeters(double range)
{
    if (!qFuzzyCompare(_maxRangeMeters, range) && range > 0) {
        _maxRangeMeters = range;
        _updateTopology();
        emit maxRangeChanged();
    }
}

void MeshNetworkManager::setNodeTimeoutMs(int ms)
{
    ms = qBound(1000, ms, 120000);
    if (_nodeTimeoutMs != ms) {
        _nodeTimeoutMs = ms;
        // Ensure degraded threshold stays below timeout
        if (_nodeDegradedMs >= _nodeTimeoutMs) {
            _nodeDegradedMs = _nodeTimeoutMs / 2;
        }
        emit timeoutChanged();
    }
}

void MeshNetworkManager::setNodeDegradedMs(int ms)
{
    ms = qBound(500, ms, 60000);
    if (_nodeDegradedMs != ms) {
        _nodeDegradedMs = ms;
        // Ensure degraded threshold stays below timeout
        if (_nodeDegradedMs >= _nodeTimeoutMs) {
            _nodeTimeoutMs = _nodeDegradedMs * 2;
        }
        emit timeoutChanged();
    }
}

MeshNode *MeshNetworkManager::getNode(int vehicleId) const
{
    for (int i = 0; i < _nodes->count(); i++) {
        auto *node = qobject_cast<MeshNode *>(_nodes->get(i));
        if (node && node->vehicleId() == vehicleId) return node;
    }
    return nullptr;
}

void MeshNetworkManager::removeNode(int vehicleId)
{
    for (int i = 0; i < _nodes->count(); i++) {
        auto *node = qobject_cast<MeshNode *>(_nodes->get(i));
        if (node && node->vehicleId() == vehicleId) {
            // Remove this node from all other nodes' connection lists
            for (int j = 0; j < _nodes->count(); j++) {
                auto *other = qobject_cast<MeshNode *>(_nodes->get(j));
                if (other) other->removeConnectedNode(vehicleId);
            }
            _nodes->removeAt(i);
            node->deleteLater();
            emit nodeRemoved(vehicleId);
            emit topologyChanged();
            _evaluateHealth();
            return;
        }
    }
}

bool MeshNetworkManager::areNodesConnected(int vehicleId1, int vehicleId2) const
{
    MeshNode *node1 = getNode(vehicleId1);
    if (!node1) return false;
    return node1->connectedNodes().contains(vehicleId2);
}

double MeshNetworkManager::distanceBetweenNodes(int vehicleId1, int vehicleId2) const
{
    MeshNode *node1 = getNode(vehicleId1);
    MeshNode *node2 = getNode(vehicleId2);
    if (!node1 || !node2) return -1.0;
    return node1->position().distanceTo(node2->position());
}

QVariantList MeshNetworkManager::getMeshTopology() const
{
    // Returns a list of edges: [{from: vehicleId, to: vehicleId, distance: meters}]
    QVariantList edges;
    QSet<QPair<int, int>> seen;

    for (int i = 0; i < _nodes->count(); i++) {
        auto *node = qobject_cast<MeshNode *>(_nodes->get(i));
        if (!node) continue;

        const QVariantList connected = node->connectedNodes();
        for (const QVariant &v : connected) {
            int otherId = v.toInt();
            auto edge = qMakePair(qMin(node->vehicleId(), otherId), qMax(node->vehicleId(), otherId));
            if (!seen.contains(edge)) {
                seen.insert(edge);
                QVariantMap edgeMap;
                edgeMap["from"] = node->vehicleId();
                edgeMap["to"] = otherId;
                edgeMap["distance"] = distanceBetweenNodes(node->vehicleId(), otherId);
                edges.append(edgeMap);
            }
        }
    }
    return edges;
}

void MeshNetworkManager::broadcastTargetSpotted(const QGeoCoordinate &coordinate, int spottedByVehicle)
{
    qCDebug(MeshNetworkManagerLog) << "Broadcasting target spotted at" << coordinate << "by vehicle" << spottedByVehicle;
    emit targetBroadcastReceived(coordinate, spottedByVehicle);
}

void MeshNetworkManager::broadcastZoneUpdate(int zoneId, double progress)
{
    qCDebug(MeshNetworkManagerLog) << "Broadcasting zone" << zoneId << "progress:" << progress;
    emit zoneUpdateReceived(zoneId, progress);
}

void MeshNetworkManager::processMavlinkMessage(Vehicle *vehicle, const mavlink_message_t &message)
{
    if (!vehicle) return;

    int vehicleId = vehicle->id();
    MeshNode *node = _getOrCreateNode(vehicleId);

    switch (message.msgid) {
    case MAVLINK_MSG_ID_GLOBAL_POSITION_INT: {
        mavlink_global_position_int_t pos;
        mavlink_msg_global_position_int_decode(&message, &pos);
        node->setPosition(QGeoCoordinate(pos.lat / 1e7, pos.lon / 1e7, pos.relative_alt / 1000.0));
        node->updateLastSeen();
        _updateTopology();
        break;
    }
    case MAVLINK_MSG_ID_SYS_STATUS: {
        mavlink_sys_status_t sysStatus;
        mavlink_msg_sys_status_decode(&message, &sysStatus);
        node->setBatteryPercent(sysStatus.battery_remaining);
        node->updateLastSeen();
        break;
    }
    case MAVLINK_MSG_ID_HEARTBEAT: {
        node->updateLastSeen();
        if (node->status() == MeshNode::Offline || node->status() == MeshNode::Degraded) {
            node->setStatus(MeshNode::Online);
            _evaluateHealth();
        }
        _gcsConnected = true;
        emit gcsConnectionChanged();
        break;
    }
    default:
        break;
    }
}

MeshNode *MeshNetworkManager::_getOrCreateNode(int vehicleId)
{
    MeshNode *node = getNode(vehicleId);
    if (!node) {
        node = new MeshNode(vehicleId, this);
        _nodes->append(node);
        qCDebug(MeshNetworkManagerLog) << "New mesh node added: vehicle" << vehicleId;
        emit nodeAdded(vehicleId);
        emit topologyChanged();
    }
    return node;
}

void MeshNetworkManager::_updateTopology()
{
    // Reset signal strength so it can recover when nodes move closer
    for (int i = 0; i < _nodes->count(); i++) {
        auto *node = qobject_cast<MeshNode *>(_nodes->get(i));
        if (node) node->setSignalStrength(100);
    }

    // Recalculate which nodes can communicate based on distance
    for (int i = 0; i < _nodes->count(); i++) {
        auto *nodeA = qobject_cast<MeshNode *>(_nodes->get(i));
        if (!nodeA || !nodeA->position().isValid()) continue;

        for (int j = i + 1; j < _nodes->count(); j++) {
            auto *nodeB = qobject_cast<MeshNode *>(_nodes->get(j));
            if (!nodeB || !nodeB->position().isValid()) continue;

            double dist = nodeA->position().distanceTo(nodeB->position());
            if (dist <= _maxRangeMeters) {
                nodeA->addConnectedNode(nodeB->vehicleId());
                nodeB->addConnectedNode(nodeA->vehicleId());

                // Signal strength decreases with distance
                int strength = static_cast<int>(100.0 * (1.0 - dist / _maxRangeMeters));
                nodeA->setSignalStrength(qMin(nodeA->signalStrength(), strength));
                nodeB->setSignalStrength(qMin(nodeB->signalStrength(), strength));
            } else {
                nodeA->removeConnectedNode(nodeB->vehicleId());
                nodeB->removeConnectedNode(nodeA->vehicleId());
            }
        }
    }

    emit topologyChanged();
}

void MeshNetworkManager::_checkNodeHealth()
{
    bool changed = false;
    for (int i = 0; i < _nodes->count(); i++) {
        auto *node = qobject_cast<MeshNode *>(_nodes->get(i));
        if (!node) continue;

        qint64 elapsed = node->msSinceLastSeen();
        MeshNode::NodeStatus newStatus = node->status();

        if (elapsed > _nodeTimeoutMs) {
            newStatus = MeshNode::Offline;
        } else if (elapsed > _nodeDegradedMs) {
            newStatus = MeshNode::Degraded;
        } else {
            newStatus = MeshNode::Online;
        }

        if (newStatus != node->status()) {
            node->setStatus(newStatus);
            changed = true;

            if (newStatus == MeshNode::Offline) {
                qCWarning(MeshNetworkManagerLog) << "Node lost: vehicle" << node->vehicleId();
                emit nodeLost(node->vehicleId());
            }
        }
    }

    if (changed) {
        _evaluateHealth();
    }
}

void MeshNetworkManager::_evaluateHealth()
{
    emit healthChanged();
}
