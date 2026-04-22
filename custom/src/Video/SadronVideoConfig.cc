#include "Video/SadronVideoConfig.h"

#include "Fact.h"
#include "SettingsManager.h"
#include "VideoSettings.h"

#include <QtCore/QUrl>
#include <QtCore/QStringList>

QGC_LOGGING_CATEGORY(SadronVideoConfigLog, "Sadron.VideoConfig")

namespace {
constexpr int kRgbPortBase = 5600;
constexpr int kAiPortBase = 5700;
constexpr int kThermalPortBase = 5800;

QString normalizedStreamType(const QString &streamType)
{
    const QString value = streamType.trimmed().toLower();
    if (value == QStringLiteral("ai") || value == QStringLiteral("overlay")) {
        return QStringLiteral("ai");
    }
    if (value == QStringLiteral("thermal") || value == QStringLiteral("ir")) {
        return QStringLiteral("thermal");
    }
    return QStringLiteral("rgb");
}

int portBaseForStreamType(const QString &streamType)
{
    const QString normalized = normalizedStreamType(streamType);
    if (normalized == QStringLiteral("ai")) {
        return kAiPortBase;
    }
    if (normalized == QStringLiteral("thermal")) {
        return kThermalPortBase;
    }
    return kRgbPortBase;
}

QString labelForStreamType(const QString &streamType)
{
    const QString normalized = normalizedStreamType(streamType);
    if (normalized == QStringLiteral("ai")) {
        return QObject::tr("AI Overlay");
    }
    if (normalized == QStringLiteral("thermal")) {
        return QObject::tr("Thermal");
    }
    return QObject::tr("RGB");
}

QString protocolFromFeed(const QVariantMap &feed)
{
    const QString explicitProtocol = feed.value(QStringLiteral("protocol")).toString().trimmed().toLower();
    if (!explicitProtocol.isEmpty()) {
        return explicitProtocol;
    }

    const QUrl uri(feed.value(QStringLiteral("uri")).toString());
    return uri.scheme().trimmed().toLower();
}
}

SadronVideoConfig::SadronVideoConfig(QObject *parent)
    : QObject(parent)
{
}

QVariantList SadronVideoConfig::feedOptionsForVehicle(int vehicleId) const
{
    QVariantList feeds;
    feeds.append(_buildFeed(vehicleId, QStringLiteral("rgb")));
    feeds.append(_buildFeed(vehicleId, QStringLiteral("ai")));
    feeds.append(_buildFeed(vehicleId, QStringLiteral("thermal")));
    return feeds;
}

QVariantMap SadronVideoConfig::defaultFeedForVehicle(int vehicleId) const
{
    return _buildFeed(vehicleId, QStringLiteral("rgb"));
}

QVariantMap SadronVideoConfig::feedForType(int vehicleId, const QString &streamType) const
{
    return _buildFeed(vehicleId, streamType);
}

bool SadronVideoConfig::applyFeed(const QVariantMap &feed) const
{
    const QString protocol = protocolFromFeed(feed);
    const QString source = qgcVideoSourceForProtocol(protocol);
    const QString uri = feed.value(QStringLiteral("uri")).toString().trimmed();

    if (source.isEmpty() || uri.isEmpty()) {
        qCWarning(SadronVideoConfigLog) << "Ignoring invalid Sadron video feed" << feed;
        return false;
    }

    VideoSettings *videoSettings = SettingsManager::instance()->videoSettings();
    if (!videoSettings) {
        qCWarning(SadronVideoConfigLog) << "Video settings are unavailable";
        return false;
    }

    if (protocol == QStringLiteral("rtsp")) {
        videoSettings->rtspUrl()->setRawValue(uri);
    } else if (protocol == QStringLiteral("tcp")) {
        videoSettings->tcpUrl()->setRawValue(hostPortFromUri(uri));
    } else {
        videoSettings->udpUrl()->setRawValue(hostPortFromUri(uri));
    }

    videoSettings->videoSource()->setRawValue(source);
    videoSettings->streamEnabled()->setRawValue(true);

    qCInfo(SadronVideoConfigLog) << "Selected Sadron video feed"
                                 << feed.value(QStringLiteral("streamType")).toString()
                                 << uri << source;
    return true;
}

QString SadronVideoConfig::hostPortFromUri(const QString &uri) const
{
    const QString trimmed = uri.trimmed();
    const QUrl parsed(trimmed);

    if (parsed.isValid() && !parsed.scheme().isEmpty()) {
        QString hostPort = parsed.host();
        if (parsed.port() > 0) {
            hostPort += QStringLiteral(":%1").arg(parsed.port());
        }
        return hostPort.isEmpty() ? trimmed : hostPort;
    }

    return trimmed;
}

QString SadronVideoConfig::qgcVideoSourceForProtocol(const QString &protocol) const
{
    const QString normalized = protocol.trimmed().toLower();
    if (normalized == QStringLiteral("mpegts") || normalized == QStringLiteral("mpeg-ts") || normalized == QStringLiteral("mpeg_ts")) {
        return VideoSettings::videoSourceMPEGTS;
    }
    if (normalized == QStringLiteral("rtsp")) {
        return VideoSettings::videoSourceRTSP;
    }
    if (normalized == QStringLiteral("udp") || normalized == QStringLiteral("udp264") || normalized == QStringLiteral("h264")) {
        return VideoSettings::videoSourceUDPH264;
    }
    if (normalized == QStringLiteral("udp265") || normalized == QStringLiteral("h265")) {
        return VideoSettings::videoSourceUDPH265;
    }
    if (normalized == QStringLiteral("tcp")) {
        return VideoSettings::videoSourceTCP;
    }
    return QString();
}

QVariantMap SadronVideoConfig::_buildFeed(int vehicleId, const QString &streamType) const
{
    const QString normalized = normalizedStreamType(streamType);
    const int port = portBaseForStreamType(normalized) + qMax(0, vehicleId);

    QVariantMap feed;
    feed.insert(QStringLiteral("key"), QStringLiteral("sadron:%1").arg(normalized));
    feed.insert(QStringLiteral("label"), labelForStreamType(normalized));
    feed.insert(QStringLiteral("mode"), QStringLiteral("sadron"));
    feed.insert(QStringLiteral("cameraIndex"), -1);
    feed.insert(QStringLiteral("streamIndex"), -1);
    feed.insert(QStringLiteral("streamType"), normalized);
    feed.insert(QStringLiteral("protocol"), QStringLiteral("mpegts"));
    feed.insert(QStringLiteral("uri"), QStringLiteral("mpegts://0.0.0.0:%1").arg(port));
    return feed;
}