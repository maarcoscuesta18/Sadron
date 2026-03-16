#include "SARTargetManager.h"
#include "QmlObjectListModel.h"
#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(SARTargetManagerLog, "Sadron.SARTargetManager")

namespace {

void _connectTargetStatusRelay(SARTargetManager *manager, SARTarget *target)
{
    if (!manager || !target) {
        return;
    }

    QObject::connect(target, &SARTarget::statusChanged, manager,
                     [manager, target]() {
                         emit manager->targetStatusChanged(target->targetId(), static_cast<int>(target->status()));
                     });
}

} // namespace

/*===========================================================================*/
// SARTarget
/*===========================================================================*/

SARTarget::SARTarget(int targetId, QObject *parent)
    : QObject(parent)
    , _targetId(targetId)
    , _timestamp(QDateTime::currentDateTimeUtc())
{
}

void SARTarget::setCoordinate(const QGeoCoordinate &coord) { if (_coordinate != coord) { _coordinate = coord; emit coordinateChanged(); } }
void SARTarget::setDescription(const QString &desc) { if (_description != desc) { _description = desc; emit descriptionChanged(); } }
void SARTarget::setPriority(TargetPriority priority) { if (_priority != priority) { _priority = priority; emit priorityChanged(); } }
void SARTarget::setStatus(TargetStatus status) { if (_status != status) { _status = status; _timestamp = QDateTime::currentDateTimeUtc(); emit statusChanged(); emit timestampChanged(); } }
void SARTarget::setSpottedByVehicle(int vehicleId) { if (_spottedByVehicle != vehicleId) { _spottedByVehicle = vehicleId; emit spottedByVehicleChanged(); } }
void SARTarget::setImagePath(const QString &path) { if (_imagePath != path) { _imagePath = path; emit imagePathChanged(); } }

QJsonObject SARTarget::toJson() const
{
    QJsonObject json;
    json["id"] = _targetId;
    json["lat"] = _coordinate.latitude();
    json["lon"] = _coordinate.longitude();
    json["alt"] = _coordinate.altitude();
    json["description"] = _description;
    json["priority"] = static_cast<int>(_priority);
    json["status"] = static_cast<int>(_status);
    json["spottedByVehicle"] = _spottedByVehicle;
    json["timestamp"] = _timestamp.toString(Qt::ISODate);
    json["imagePath"] = _imagePath;
    return json;
}

SARTarget *SARTarget::fromJson(const QJsonObject &json, QObject *parent)
{
    auto *target = new SARTarget(json["id"].toInt(), parent);
    target->_coordinate = QGeoCoordinate(json["lat"].toDouble(), json["lon"].toDouble(), json["alt"].toDouble(0));
    target->_description = json["description"].toString();
    int priorityInt = json["priority"].toInt(1);
    target->_priority = (priorityInt >= 0 && priorityInt <= SARTarget::Critical)
        ? static_cast<TargetPriority>(priorityInt) : SARTarget::Medium;
    int statusInt = json["status"].toInt(0);
    target->_status = (statusInt >= 0 && statusInt <= SARTarget::Rescued)
        ? static_cast<TargetStatus>(statusInt) : SARTarget::Unconfirmed;
    target->_spottedByVehicle = json["spottedByVehicle"].toInt(-1);
    target->_timestamp = QDateTime::fromString(json["timestamp"].toString(), Qt::ISODate);
    target->_imagePath = json["imagePath"].toString();
    return target;
}

/*===========================================================================*/
// SARTargetManager
/*===========================================================================*/

SARTargetManager::SARTargetManager(QObject *parent)
    : QObject(parent)
    , _targets(new QmlObjectListModel(this))
{
}

SARTargetManager::~SARTargetManager()
{
}

int SARTargetManager::totalTargets() const
{
    return _targets->count();
}

int SARTargetManager::confirmedCount() const
{
    int count = 0;
    for (int i = 0; i < _targets->count(); i++) {
        auto *target = qobject_cast<SARTarget *>(_targets->get(i));
        if (target && target->status() == SARTarget::Confirmed) {
            count++;
        }
    }
    return count;
}

SARTarget *SARTargetManager::addTarget(const QGeoCoordinate &coordinate, const QString &description, int vehicleId)
{
    auto *target = new SARTarget(_nextTargetId++, this);
    target->setCoordinate(coordinate);
    if (!description.isEmpty()) target->setDescription(description);
    if (vehicleId >= 0) target->setSpottedByVehicle(vehicleId);
    _connectTargetStatusRelay(this, target);
    _targets->append(target);

    qCDebug(SARTargetManagerLog) << "Target added:" << target->targetId() << "at" << coordinate;
    emit targetsChanged();
    emit targetAdded(target->targetId(), coordinate);
    return target;
}

void SARTargetManager::removeTarget(int targetId)
{
    for (int i = 0; i < _targets->count(); i++) {
        auto *target = qobject_cast<SARTarget *>(_targets->get(i));
        if (target && target->targetId() == targetId) {
            _targets->removeAt(i);
            target->deleteLater();
            emit targetsChanged();
            return;
        }
    }
}

void SARTargetManager::clearAllTargets()
{
    _targets->clearAndDeleteContents();
    _nextTargetId = 1;
    emit targetsChanged();
}

SARTarget *SARTargetManager::getTarget(int targetId) const
{
    for (int i = 0; i < _targets->count(); i++) {
        auto *target = qobject_cast<SARTarget *>(_targets->get(i));
        if (target && target->targetId() == targetId) {
            return target;
        }
    }
    return nullptr;
}

SARTarget *SARTargetManager::nearestTarget(const QGeoCoordinate &coordinate) const
{
    SARTarget *nearest = nullptr;
    double minDist = std::numeric_limits<double>::max();

    for (int i = 0; i < _targets->count(); i++) {
        auto *target = qobject_cast<SARTarget *>(_targets->get(i));
        if (target) {
            double dist = coordinate.distanceTo(target->coordinate());
            if (dist < minDist) {
                minDist = dist;
                nearest = target;
            }
        }
    }
    return nearest;
}

QJsonArray SARTargetManager::toJson() const
{
    QJsonArray arr;
    for (int i = 0; i < _targets->count(); i++) {
        auto *target = qobject_cast<SARTarget *>(_targets->get(i));
        if (target) arr.append(target->toJson());
    }
    return arr;
}

void SARTargetManager::fromJson(const QJsonArray &json)
{
    clearAllTargets();
    for (const QJsonValue &v : json) {
        auto *target = SARTarget::fromJson(v.toObject(), this);
        if (target) {
            _connectTargetStatusRelay(this, target);
            _targets->append(target);
            if (target->targetId() >= _nextTargetId) {
                _nextTargetId = target->targetId() + 1;
            }
        }
    }
    emit targetsChanged();
}
