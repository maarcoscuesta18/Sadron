#pragma once

#include <QtCore/QObject>
#include <QtCore/QLoggingCategory>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QDateTime>
#include <QtPositioning/QGeoCoordinate>
#include <QtQmlIntegration/QtQmlIntegration>

Q_DECLARE_LOGGING_CATEGORY(SARTargetManagerLog)

#include "QmlObjectListModel.h"

// Represents a potential target/point of interest spotted during SAR
class SARTarget : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")

    Q_PROPERTY(int              targetId        READ targetId       CONSTANT)
    Q_PROPERTY(QGeoCoordinate   coordinate      READ coordinate     WRITE setCoordinate     NOTIFY coordinateChanged)
    Q_PROPERTY(QString          description     READ description    WRITE setDescription    NOTIFY descriptionChanged)
    Q_PROPERTY(TargetPriority   priority        READ priority       WRITE setPriority       NOTIFY priorityChanged)
    Q_PROPERTY(TargetStatus     status          READ status         WRITE setStatus         NOTIFY statusChanged)
    Q_PROPERTY(int              spottedByVehicle READ spottedByVehicle WRITE setSpottedByVehicle NOTIFY spottedByVehicleChanged)
    Q_PROPERTY(QDateTime        timestamp       READ timestamp      NOTIFY timestampChanged)
    Q_PROPERTY(QString          imagePath       READ imagePath      WRITE setImagePath      NOTIFY imagePathChanged)

public:
    enum TargetPriority {
        Low = 0,
        Medium,
        High,
        Critical
    };
    Q_ENUM(TargetPriority)

    enum TargetStatus {
        Unconfirmed = 0,
        Investigating,
        Confirmed,
        FalseAlarm,
        Rescued
    };
    Q_ENUM(TargetStatus)

    explicit SARTarget(int targetId, QObject *parent = nullptr);

    int             targetId() const { return _targetId; }
    QGeoCoordinate  coordinate() const { return _coordinate; }
    QString         description() const { return _description; }
    TargetPriority  priority() const { return _priority; }
    TargetStatus    status() const { return _status; }
    int             spottedByVehicle() const { return _spottedByVehicle; }
    QDateTime       timestamp() const { return _timestamp; }
    QString         imagePath() const { return _imagePath; }

    void setCoordinate(const QGeoCoordinate &coord);
    void setDescription(const QString &desc);
    void setPriority(TargetPriority priority);
    void setStatus(TargetStatus status);
    void setSpottedByVehicle(int vehicleId);
    void setImagePath(const QString &path);

    QJsonObject toJson() const;
    static SARTarget *fromJson(const QJsonObject &json, QObject *parent);

signals:
    void coordinateChanged();
    void descriptionChanged();
    void priorityChanged();
    void statusChanged();
    void spottedByVehicleChanged();
    void imagePathChanged();
    void timestampChanged();

private:
    int             _targetId;
    QGeoCoordinate  _coordinate;
    QString         _description;
    TargetPriority  _priority = Medium;
    TargetStatus    _status = Unconfirmed;
    int             _spottedByVehicle = -1;
    QDateTime       _timestamp;
    QString         _imagePath;
};

/*===========================================================================*/

class SARTargetManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")

    Q_PROPERTY(QmlObjectListModel *targets          READ targets        CONSTANT)
    Q_PROPERTY(int totalTargets                     READ totalTargets   NOTIFY targetsChanged)
    Q_PROPERTY(int confirmedCount                   READ confirmedCount NOTIFY targetsChanged)

public:
    explicit SARTargetManager(QObject *parent = nullptr);
    ~SARTargetManager();

    QmlObjectListModel *targets() const { return _targets; }
    int totalTargets() const;
    int confirmedCount() const;

    Q_INVOKABLE SARTarget *addTarget(const QGeoCoordinate &coordinate, const QString &description = QString(), int vehicleId = -1);
    Q_INVOKABLE void removeTarget(int targetId);
    Q_INVOKABLE void clearAllTargets();
    Q_INVOKABLE SARTarget *getTarget(int targetId) const;

    // Find nearest target to a coordinate
    Q_INVOKABLE SARTarget *nearestTarget(const QGeoCoordinate &coordinate) const;

    QJsonArray toJson() const;
    void fromJson(const QJsonArray &json);

signals:
    void targetsChanged();
    void targetAdded(int targetId, const QGeoCoordinate &coordinate);
    void targetStatusChanged(int targetId, int newStatus);

private:
    int _nextTargetId = 1;
    QmlObjectListModel *_targets = nullptr;
};
