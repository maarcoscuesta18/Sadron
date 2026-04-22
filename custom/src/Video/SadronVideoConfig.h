#pragma once

#include <QtCore/QObject>
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>
#include <QtQmlIntegration/QtQmlIntegration>

#include "QGCLoggingCategory.h"

Q_DECLARE_LOGGING_CATEGORY(SadronVideoConfigLog)

class SadronVideoConfig : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")

public:
    explicit SadronVideoConfig(QObject *parent = nullptr);

    Q_INVOKABLE QVariantList feedOptionsForVehicle(int vehicleId) const;
    Q_INVOKABLE QVariantMap defaultFeedForVehicle(int vehicleId) const;
    Q_INVOKABLE QVariantMap feedForType(int vehicleId, const QString &streamType) const;
    Q_INVOKABLE bool applyFeed(const QVariantMap &feed) const;
    Q_INVOKABLE QString hostPortFromUri(const QString &uri) const;
    Q_INVOKABLE QString qgcVideoSourceForProtocol(const QString &protocol) const;

private:
    QVariantMap _buildFeed(int vehicleId, const QString &streamType) const;
};