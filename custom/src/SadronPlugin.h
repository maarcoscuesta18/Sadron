#pragma once

#include <QtCore/QTimer>
#include <QtCore/QTranslator>
#include <QtQml/QQmlAbstractUrlInterceptor>

#include "QGCCorePlugin.h"
#include "QGCOptions.h"

class SadronOptions;
class SadronPlugin;
class QQmlApplicationEngine;

Q_DECLARE_LOGGING_CATEGORY(SadronLog)

// Forward declarations for SAR subsystems
class SARMissionManager;
class SARZoneManager;
class SARTargetManager;
class SARCoverageTracker;
class SARReTaskingManager;
class MeshNetworkManager;
class EnvironmentalDataProvider;
class VehicleCoordinator;

/*===========================================================================*/

class SadronFlyViewOptions : public QGCFlyViewOptions
{
    Q_OBJECT

public:
    explicit SadronFlyViewOptions(SadronOptions *options, QObject *parent = nullptr);

    // SAR operations require multi-vehicle coordination
    bool showMultiVehicleList() const final { return true; }
    // Show instrument panel for telemetry during SAR ops
    bool showInstrumentPanel() const final { return true; }
    // Show emergency stop for safety-critical SAR operations
    bool guidedBarShowEmergencyStop() const final { return true; }
    // Show orbit for area-of-interest inspection
    bool guidedBarShowOrbit() const final { return true; }
    // Show ROI for target marking
    bool guidedBarShowROI() const final { return true; }
};

/*===========================================================================*/

class SadronOptions : public QGCOptions
{
    Q_OBJECT

public:
    explicit SadronOptions(SadronPlugin *plugin, QObject *parent = nullptr);

    // SAR requires multi-vehicle support
    bool multiVehicleEnabled() const final { return true; }
    // Show firmware upgrade in advanced mode only
    bool showFirmwareUpgrade() const final { return _plugin->showAdvancedUI(); }
    // Allow all mission item types for SAR flexibility
    bool missionWaypointsOnly() const final { return false; }
    // Show mission status for SAR progress tracking
    bool showMissionStatus() const final { return true; }
    // Support offline maps for field operations
    bool showOfflineMapExport() const final { return true; }
    bool showOfflineMapImport() const final { return true; }

    QGCFlyViewOptions *flyViewOptions() const final { return _flyViewOptions; }

    // SAR-specific built-in survey presets
    QStringList surveyBuiltInPresetNames() const final;

private:
    QGCCorePlugin *_plugin = nullptr;
    SadronFlyViewOptions *_flyViewOptions = nullptr;
};

/*===========================================================================*/

class SadronPlugin : public QGCCorePlugin
{
    Q_OBJECT

public:
    explicit SadronPlugin(QObject *parent = nullptr);
    ~SadronPlugin();

    static QGCCorePlugin *instance();

    // Overrides from QGCCorePlugin
    void init() final;
    void cleanup() final;
    QGCOptions *options() final { return _options; }
    QString brandImageIndoor() const final { return QStringLiteral("/custom/img/sadron-logo-white.svg"); }
    QString brandImageOutdoor() const final { return QStringLiteral("/custom/img/sadron-logo-dark.svg"); }
    bool overrideSettingsGroupVisibility(const QString &name) final;
    void adjustSettingMetaData(const QString &settingsGroup, FactMetaData &metaData, bool &visible) final;
    void paletteOverride(const QString &colorName, QGCPalette::PaletteColorInfo_t &colorInfo) final;
    QQmlApplicationEngine *createQmlApplicationEngine(QObject *parent) final;
    const QVariantList &toolBarIndicators() final;

    // MAVLink interception for mesh coordination messages
    bool mavlinkMessage(Vehicle *vehicle, LinkInterface *link, const mavlink_message_t &message) final;

    // Plan file hooks for SAR metadata
    void postSaveToJson(PlanMasterController *pController, QJsonObject &json) final;
    void postLoadFromJson(PlanMasterController *pController, QJsonObject &json) final;

    // SAR subsystem accessors
    SARMissionManager   *sarMissionManager() const { return _sarMissionManager; }
    SARZoneManager      *sarZoneManager() const { return _sarZoneManager; }
    SARTargetManager    *sarTargetManager() const { return _sarTargetManager; }
    SARCoverageTracker  *sarCoverageTracker() const { return _sarCoverageTracker; }
    SARReTaskingManager *sarReTaskingManager() const { return _sarReTaskingManager; }
    MeshNetworkManager  *meshNetworkManager() const { return _meshNetworkManager; }
    EnvironmentalDataProvider *environmentalDataProvider() const { return _environmentalDataProvider; }
    VehicleCoordinator *vehicleCoordinator() const { return _vehicleCoordinator; }

private slots:
    void _advancedChanged(bool advanced);
    void _onSearchAreaChanged();
    void _onCoverageChanged();
    void _updateZoneProgress();

private:
    void _addSettingsEntry(const QString &title, const char *qmlFile, const char *iconFile = nullptr);

    SadronOptions *_options = nullptr;
    QQmlApplicationEngine *_qmlEngine = nullptr;
    class SadronOverrideInterceptor *_selector = nullptr;
    QVariantList _customSettingsList;
    QVariantList _toolBarIndicatorList;

    // Coverage update debounce timer
    QTimer *_coverageUpdateTimer = nullptr;

    // SAR subsystems
    SARMissionManager   *_sarMissionManager = nullptr;
    SARZoneManager      *_sarZoneManager = nullptr;
    SARTargetManager    *_sarTargetManager = nullptr;
    SARCoverageTracker  *_sarCoverageTracker = nullptr;
    SARReTaskingManager *_sarReTaskingManager = nullptr;
    MeshNetworkManager  *_meshNetworkManager = nullptr;
    EnvironmentalDataProvider *_environmentalDataProvider = nullptr;
    VehicleCoordinator *_vehicleCoordinator = nullptr;
};

/*===========================================================================*/

class SadronOverrideInterceptor : public QQmlAbstractUrlInterceptor
{
public:
    SadronOverrideInterceptor();

    QUrl intercept(const QUrl &url, QQmlAbstractUrlInterceptor::DataType type) final;
};
