#include "SadronPlugin.h"
#include "QmlComponentInfo.h"
#include "QGCLoggingCategory.h"
#include "QGCPalette.h"
#include "QGCMAVLink.h"
#include "AppSettings.h"
#include "BrandImageSettings.h"
#include "Vehicle.h"
#include "MultiVehicleManager.h"
#include "LinkInterface.h"

#include "SAR/SARMissionManager.h"
#include "SAR/SARZoneManager.h"
#include "SAR/SARTargetManager.h"
#include "SAR/SARCoverageTracker.h"
#include "SAR/SARReTaskingManager.h"
#include "SAR/VehicleCoordinator.h"
#include "Mesh/MeshNetworkManager.h"
#include "Environmental/EnvironmentalDataProvider.h"

#include <QtCore/QApplicationStatic>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlFile>
#include <QtQml/QQmlContext>

QGC_LOGGING_CATEGORY(SadronLog, "Sadron.SadronPlugin")

Q_APPLICATION_STATIC(SadronPlugin, _sadronPluginInstance);

/*===========================================================================*/

SadronFlyViewOptions::SadronFlyViewOptions(SadronOptions *options, QObject *parent)
    : QGCFlyViewOptions(options, parent)
{
    qCDebug(SadronLog) << "SadronFlyViewOptions created";
}

/*===========================================================================*/

SadronOptions::SadronOptions(SadronPlugin *plugin, QObject *parent)
    : QGCOptions(parent)
    , _plugin(plugin)
    , _flyViewOptions(new SadronFlyViewOptions(this, this))
{
    Q_CHECK_PTR(_plugin);
}

QStringList SadronOptions::surveyBuiltInPresetNames() const
{
    return QStringList({
        QStringLiteral("SAR Parallel Track"),
        QStringLiteral("SAR Expanding Square"),
        QStringLiteral("SAR Sector Search"),
        QStringLiteral("SAR Creeping Line"),
    });
}

/*===========================================================================*/

SadronPlugin::SadronPlugin(QObject *parent)
    : QGCCorePlugin(parent)
    , _options(new SadronOptions(this, this))
{
    qCDebug(SadronLog) << "Sadron SAR Plugin initializing";

    // SAR system starts in advanced mode - operators need full control
    _showAdvancedUI = true;
    (void)connect(this, &QGCCorePlugin::showAdvancedUIChanged, this, &SadronPlugin::_advancedChanged);
}

SadronPlugin::~SadronPlugin()
{
    delete _vehicleCoordinator;
    delete _sarReTaskingManager;
    delete _sarMissionManager;
    delete _sarZoneManager;
    delete _sarTargetManager;
    delete _sarCoverageTracker;
    delete _meshNetworkManager;
    delete _environmentalDataProvider;
}

QGCCorePlugin *SadronPlugin::instance()
{
    return _sadronPluginInstance();
}

void SadronPlugin::init()
{
    qCDebug(SadronLog) << "Initializing SAR subsystems";

    _sarZoneManager = new SARZoneManager(this);
    _sarTargetManager = new SARTargetManager(this);
    _sarCoverageTracker = new SARCoverageTracker(this);
    _sarMissionManager = new SARMissionManager(_sarZoneManager, _sarCoverageTracker, this);
    _sarReTaskingManager = new SARReTaskingManager(_sarMissionManager, _sarZoneManager, _sarTargetManager, this);

    // Inject cross-references needed for abort full-reset
    _sarMissionManager->setTargetManager(_sarTargetManager);
    _sarMissionManager->setReTaskingManager(_sarReTaskingManager);

    _meshNetworkManager = new MeshNetworkManager(this);
    _environmentalDataProvider = new EnvironmentalDataProvider(this);

    // VehicleCoordinator must be created last — it references all other subsystems
    _vehicleCoordinator = new VehicleCoordinator(
        _sarMissionManager, _sarZoneManager, _sarTargetManager,
        _sarReTaskingManager, _sarCoverageTracker, _meshNetworkManager, this);

    // Coverage update debounce timer — avoids per-cell QML thrashing
    _coverageUpdateTimer = new QTimer(this);
    _coverageUpdateTimer->setSingleShot(true);
    _coverageUpdateTimer->setInterval(1000);

    // Initialize coverage grid when search area is drawn or zones repartitioned
    (void)connect(_sarZoneManager, &SARZoneManager::searchAreaChanged, this, &SadronPlugin::_onSearchAreaChanged);
    (void)connect(_sarZoneManager, &SARZoneManager::zonesChanged,     this, &SadronPlugin::_onSearchAreaChanged);

    // Debounce rapid coverage updates into a single zone-progress sweep
    (void)connect(_sarCoverageTracker, &SARCoverageTracker::coverageChanged, this, &SadronPlugin::_onCoverageChanged);
    (void)connect(_coverageUpdateTimer, &QTimer::timeout, this, &SadronPlugin::_updateZoneProgress);
}

void SadronPlugin::cleanup()
{
    if (_qmlEngine) {
        _qmlEngine->removeUrlInterceptor(_selector);
    }

    delete _selector;
    _selector = nullptr;
}

void SadronPlugin::_advancedChanged(bool changed)
{
    emit _options->showFirmwareUpgradeChanged(changed);
}

void SadronPlugin::_onSearchAreaChanged()
{
    if (!_sarZoneManager || !_sarCoverageTracker) return;

    if (_sarZoneManager->hasSearchArea()) {
        _sarCoverageTracker->initializeGridFromPolygon(
            _sarZoneManager->searchAreaPolygon()->path());
        qCDebug(SadronLog) << "Coverage grid initialized from search area polygon";
    }
}

void SadronPlugin::_onCoverageChanged()
{
    if (_coverageUpdateTimer) {
        _coverageUpdateTimer->start(); // (re)start 1-sec debounce
    }
}

void SadronPlugin::_updateZoneProgress()
{
    if (!_sarZoneManager || !_sarCoverageTracker) return;

    QmlObjectListModel *zones = _sarZoneManager->zones();
    if (!zones) return;

    for (int i = 0; i < zones->count(); ++i) {
        SARZone *zone = qobject_cast<SARZone *>(zones->get(i));
        if (!zone) continue;

        double pct = _sarCoverageTracker->coverageForZone(zone->polygon());
        zone->setProgress(pct / 100.0);
    }

    emit _sarZoneManager->progressChanged();
}

void SadronPlugin::_addSettingsEntry(const QString &title, const char *qmlFile, const char *iconFile)
{
    Q_CHECK_PTR(qmlFile);
    _customSettingsList.append(QVariant::fromValue(
        new QmlComponentInfo(
            title,
            QUrl::fromUserInput(qmlFile),
            !iconFile ? QUrl() : QUrl::fromUserInput(iconFile),
            this)
    ));
}

bool SadronPlugin::overrideSettingsGroupVisibility(const QString &name)
{
    if (name == BrandImageSettings::name) {
        return false;
    }
    return true;
}

void SadronPlugin::adjustSettingMetaData(const QString &settingsGroup, FactMetaData &metaData, bool &visible)
{
    QGCCorePlugin::adjustSettingMetaData(settingsGroup, metaData, visible);

    if (settingsGroup == AppSettings::settingsGroup) {
        // SAR operations default to multi-rotor for hover capability
        if (metaData.name() == AppSettings::offlineEditingFirmwareClassName) {
            metaData.setRawDefaultValue(QGCMAVLink::FirmwareClassArduPilot);
            return;
        } else if (metaData.name() == AppSettings::offlineEditingVehicleClassName) {
            metaData.setRawDefaultValue(QGCMAVLink::VehicleClassMultiRotor);
            return;
        }
    }
}

const QVariantList &SadronPlugin::toolBarIndicators()
{
    if (_toolBarIndicatorList.isEmpty()) {
        // Start with default indicators
        _toolBarIndicatorList = QGCCorePlugin::toolBarIndicators();
        // Add mesh status indicator
        _toolBarIndicatorList.append(QVariant::fromValue(QUrl::fromUserInput(QStringLiteral("qrc:/custom/Sadron/MeshStatusIndicator.qml"))));
        // Add SAR status indicator
        _toolBarIndicatorList.append(QVariant::fromValue(QUrl::fromUserInput(QStringLiteral("qrc:/custom/Sadron/SARStatusIndicator.qml"))));
    }
    return _toolBarIndicatorList;
}

bool SadronPlugin::mavlinkMessage(Vehicle *vehicle, LinkInterface *link, const mavlink_message_t &message)
{
    Q_UNUSED(link);

    // Let mesh network manager process coordination messages
    if (_meshNetworkManager) {
        _meshNetworkManager->processMavlinkMessage(vehicle, message);
    }

    // Let coverage tracker update from position messages
    if (_sarCoverageTracker) {
        _sarCoverageTracker->processMavlinkMessage(vehicle, message);
    }

    return true; // Allow normal processing to continue
}

void SadronPlugin::postSaveToJson(PlanMasterController *pController, QJsonObject &json)
{
    Q_UNUSED(pController);

    // Embed SAR metadata in plan files
    QJsonObject sarData;
    if (_sarZoneManager) {
        sarData["zones"] = _sarZoneManager->toJson();
    }
    if (_sarTargetManager) {
        sarData["targets"] = _sarTargetManager->toJson();
    }
    if (_sarReTaskingManager) {
        sarData["reTasking"] = _sarReTaskingManager->settingsToJson();
    }
    if (_vehicleCoordinator) {
        sarData["coordination"] = _vehicleCoordinator->settingsToJson();
    }
    json["sadronSAR"] = sarData;
}

void SadronPlugin::postLoadFromJson(PlanMasterController *pController, QJsonObject &json)
{
    Q_UNUSED(pController);

    if (json.contains("sadronSAR")) {
        QJsonObject sarData = json["sadronSAR"].toObject();
        if (_sarZoneManager && sarData.contains("zones")) {
            _sarZoneManager->fromJson(sarData["zones"].toArray());
        }
        if (_sarTargetManager && sarData.contains("targets")) {
            _sarTargetManager->fromJson(sarData["targets"].toArray());
        }
        if (_sarReTaskingManager && sarData.contains("reTasking")) {
            _sarReTaskingManager->settingsFromJson(sarData["reTasking"].toObject());
        }
        if (_vehicleCoordinator && sarData.contains("coordination")) {
            _vehicleCoordinator->settingsFromJson(sarData["coordination"].toObject());
        }
    }
}

// SAR-specific color palette: high-contrast for field operations
void SadronPlugin::paletteOverride(const QString &colorName, QGCPalette::PaletteColorInfo_t &colorInfo)
{
    // Dark theme optimized for outdoor/field use with high contrast
    if (colorName == QStringLiteral("window")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = QColor("#1a1a2e");
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = QColor("#1a1a2e");
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupEnabled]  = QColor("#f0f0f5");
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupDisabled] = QColor("#e8e8ed");
    } else if (colorName == QStringLiteral("windowShade")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = QColor("#16213e");
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = QColor("#16213e");
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupEnabled]  = QColor("#e4e4eb");
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupDisabled] = QColor("#d0d0d8");
    } else if (colorName == QStringLiteral("windowShadeDark")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = QColor("#0f0f23");
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = QColor("#0f0f23");
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupEnabled]  = QColor("#d8d8e0");
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupDisabled] = QColor("#b8b8c0");
    } else if (colorName == QStringLiteral("text")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = QColor("#e8e8e8");
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = QColor("#686878");
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupEnabled]  = QColor("#1a1a2e");
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupDisabled] = QColor("#909098");
    } else if (colorName == QStringLiteral("warningText")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = QColor("#ff4444");
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = QColor("#ff4444");
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupEnabled]  = QColor("#cc0000");
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupDisabled] = QColor("#cc0000");
    } else if (colorName == QStringLiteral("button")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = QColor("#2a2a4a");
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = QColor("#2a2a4a");
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupEnabled]  = QColor("#ffffff");
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupDisabled] = QColor("#f0f0f0");
    } else if (colorName == QStringLiteral("buttonHighlight")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = QColor("#e67e22");
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = QColor("#2a2a4a");
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupEnabled]  = QColor("#f39c12");
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupDisabled] = QColor("#d0d0d0");
    } else if (colorName == QStringLiteral("primaryButton")) {
        // SAR orange - high visibility, standard SAR color
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = QColor("#e67e22");
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = QColor("#2a2a4a");
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupEnabled]  = QColor("#e67e22");
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupDisabled] = QColor("#888888");
    } else if (colorName == QStringLiteral("primaryButtonText")) {
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = QColor("#ffffff");
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = QColor("#aaaaaa");
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupEnabled]  = QColor("#ffffff");
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupDisabled] = QColor("#cccccc");
    } else if (colorName == QStringLiteral("colorGreen")) {
        // Searched/clear areas
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = QColor("#2ecc71");
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = QColor("#27ae60");
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupEnabled]  = QColor("#27ae60");
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupDisabled] = QColor("#27ae60");
    } else if (colorName == QStringLiteral("colorOrange")) {
        // SAR branding accent
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = QColor("#f39c12");
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = QColor("#e67e22");
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupEnabled]  = QColor("#e67e22");
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupDisabled] = QColor("#e67e22");
    } else if (colorName == QStringLiteral("colorRed")) {
        // Alerts / unsearched areas
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = QColor("#e74c3c");
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = QColor("#c0392b");
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupEnabled]  = QColor("#e74c3c");
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupDisabled] = QColor("#e74c3c");
    } else if (colorName == QStringLiteral("mapIndicator")) {
        // High-visibility SAR orange for map markers
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupEnabled]   = QColor("#e67e22");
        colorInfo[QGCPalette::Dark][QGCPalette::ColorGroupDisabled]  = QColor("#888888");
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupEnabled]  = QColor("#d35400");
        colorInfo[QGCPalette::Light][QGCPalette::ColorGroupDisabled] = QColor("#888888");
    }
}

QQmlApplicationEngine *SadronPlugin::createQmlApplicationEngine(QObject *parent)
{
    _qmlEngine = QGCCorePlugin::createQmlApplicationEngine(parent);
    _qmlEngine->addImportPath("qrc:/qml/Custom/Widgets");

    // Register SAR subsystems with QML
    QQmlContext *context = _qmlEngine->rootContext();
    context->setContextProperty("sarMissionManager", _sarMissionManager);
    context->setContextProperty("sarZoneManager", _sarZoneManager);
    context->setContextProperty("sarTargetManager", _sarTargetManager);
    context->setContextProperty("sarCoverageTracker", _sarCoverageTracker);
    context->setContextProperty("sarReTaskingManager", _sarReTaskingManager);
    context->setContextProperty("meshNetworkManager", _meshNetworkManager);
    context->setContextProperty("environmentalDataProvider", _environmentalDataProvider);
    context->setContextProperty("vehicleCoordinator", _vehicleCoordinator);

    _selector = new SadronOverrideInterceptor();
    _qmlEngine->addUrlInterceptor(_selector);

    return _qmlEngine;
}

/*===========================================================================*/

SadronOverrideInterceptor::SadronOverrideInterceptor()
    : QQmlAbstractUrlInterceptor()
{
}

QUrl SadronOverrideInterceptor::intercept(const QUrl &url, QQmlAbstractUrlInterceptor::DataType type)
{
    switch (type) {
    case QQmlAbstractUrlInterceptor::QmlFile:
    case QQmlAbstractUrlInterceptor::UrlString:
        if (url.scheme() == QStringLiteral("qrc")) {
            const QString origPath = url.path();
            // First try Sadron-specific override
            const QString sadronRes = QStringLiteral(":/Sadron%1").arg(origPath);
            if (QFile::exists(sadronRes)) {
                const QString relPath = sadronRes.mid(2);
                QUrl result;
                result.setScheme(QStringLiteral("qrc"));
                result.setPath('/' + relPath);
                return result;
            }
            // Then try Custom override (for widgets)
            const QString customRes = QStringLiteral(":/Custom%1").arg(origPath);
            if (QFile::exists(customRes)) {
                const QString relPath = customRes.mid(2);
                QUrl result;
                result.setScheme(QStringLiteral("qrc"));
                result.setPath('/' + relPath);
                return result;
            }
        }
        break;
    default:
        break;
    }

    return url;
}
