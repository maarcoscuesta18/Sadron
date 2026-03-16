#include "SARModeManager.h"
#include "QGCLoggingCategory.h"
#include "SARMissionManager.h"
#include "SARZoneManager.h"
#include "SARCoverageTracker.h"
#include "SARReTaskingManager.h"
#include "VehicleCoordinator.h"

#include <QtCore/QJsonArray>

QGC_LOGGING_CATEGORY(SARModeManagerLog, "Sadron.SARModeManager")

SARModeManager::SARModeManager(SARMissionManager   *missionMgr,
                               SARZoneManager      *zoneMgr,
                               SARCoverageTracker  *coverageMgr,
                               SARReTaskingManager *reTaskMgr,
                               VehicleCoordinator  *coordinator,
                               QObject *parent)
    : QObject(parent)
    , _missionMgr(missionMgr)
    , _zoneMgr(zoneMgr)
    , _coverageMgr(coverageMgr)
    , _reTaskMgr(reTaskMgr)
    , _coordinator(coordinator)
{
    _initProfiles();
    qCDebug(SARModeManagerLog) << "SARModeManager created with" << _profiles.size() << "disaster profiles";
}

void SARModeManager::_initProfiles()
{
    _profiles.resize(static_cast<int>(ModeCount));

    // ── Standard SAR ──
    _profiles[StandardSAR] = {
        QStringLiteral("Standard SAR"),
        QStringLiteral("General-purpose search and rescue. Balanced parameters for most land-based scenarios."),
        QStringLiteral("#3B82F6"),       // blue
        QStringLiteral("\u2695"),         // rescue symbol
        0,      // ParallelTrack
        30.0,   // altitude
        5.0,    // speed
        20.0,   // spacing
        10.0,   // cell size
        20.0,   // camera FOV
        0,      // NearestAvailable
        1,      // Medium priority
        10,     // auto-confirm timeout
        50.0,   // horizontal bubble
        10.0,   // vertical bubble
        30,     // comms timeout
        0,      // Grid partition
        {}      // no recommended overlays
    };

    // ── Maritime ──
    _profiles[Maritime] = {
        QStringLiteral("Maritime"),
        QStringLiteral("Ocean and coastal rescue. Wide sweeps at higher altitude for maximum coverage over open water."),
        QStringLiteral("#0EA5E9"),       // ocean blue
        QStringLiteral("\u2693"),         // anchor
        1,      // CreepingLine
        50.0,   // altitude
        8.0,    // speed
        40.0,   // spacing
        20.0,   // cell size
        35.0,   // camera FOV
        0,      // NearestAvailable
        1,      // Medium priority
        15,     // auto-confirm timeout
        80.0,   // horizontal bubble
        15.0,   // vertical bubble
        45,     // comms timeout
        2,      // Strip partition
        {QStringLiteral("weather"), QStringLiteral("hydro")}
    };

    // ── Wildfire ──
    _profiles[Wildfire] = {
        QStringLiteral("Wildfire"),
        QStringLiteral("Wildfire monitoring and survivor search. High altitude for thermal safety with rapid re-tasking."),
        QStringLiteral("#EF4444"),       // red
        QStringLiteral("\u2668"),         // flame/hot springs
        0,      // ParallelTrack
        80.0,   // altitude
        10.0,   // speed
        35.0,   // spacing
        15.0,   // cell size
        30.0,   // camera FOV
        1,      // PriorityWeighted
        2,      // High priority
        5,      // auto-confirm timeout (fast re-task)
        100.0,  // horizontal bubble
        20.0,   // vertical bubble
        20,     // comms timeout (shorter — fire can block signals)
        2,      // Strip partition
        {QStringLiteral("weather")}
    };

    // ── Flood ──
    _profiles[Flood] = {
        QStringLiteral("Flood"),
        QStringLiteral("Flood response and water rescue. Low altitude, tight spacing for detecting people in debris and water."),
        QStringLiteral("#06B6D4"),       // cyan
        QStringLiteral("\u2248"),         // wave
        0,      // ParallelTrack
        25.0,   // altitude
        4.0,    // speed
        15.0,   // spacing
        8.0,    // cell size
        18.0,   // camera FOV
        1,      // PriorityWeighted
        0,      // Low priority (catch everything)
        15,     // auto-confirm timeout
        40.0,   // horizontal bubble
        8.0,    // vertical bubble
        30,     // comms timeout
        0,      // Grid partition
        {QStringLiteral("weather"), QStringLiteral("hydro"), QStringLiteral("slope")}
    };

    // ── Earthquake ──
    _profiles[Earthquake] = {
        QStringLiteral("Earthquake"),
        QStringLiteral("Earthquake response. Expanding square from epicenter with tight coverage for structural collapse zones."),
        QStringLiteral("#F97316"),       // orange
        QStringLiteral("\u26A0"),         // warning triangle
        2,      // ExpandingSquare
        40.0,   // altitude
        3.0,    // speed (slow for detail)
        12.0,   // spacing
        6.0,    // cell size
        15.0,   // camera FOV
        1,      // PriorityWeighted
        0,      // Low priority
        8,      // auto-confirm timeout
        30.0,   // horizontal bubble
        8.0,    // vertical bubble
        25,     // comms timeout
        0,      // Grid partition
        {QStringLiteral("slope")}
    };

    // ── Avalanche ──
    _profiles[Avalanche] = {
        QStringLiteral("Avalanche"),
        QStringLiteral("Avalanche rescue. Low altitude creeping line with very tight spacing for locating buried victims."),
        QStringLiteral("#A855F7"),       // purple
        QStringLiteral("\u26F0"),         // mountain
        1,      // CreepingLine
        25.0,   // altitude
        3.0,    // speed
        10.0,   // spacing
        5.0,    // cell size
        12.0,   // camera FOV
        0,      // NearestAvailable
        0,      // Low priority
        8,      // auto-confirm timeout
        35.0,   // horizontal bubble
        10.0,   // vertical bubble
        25,     // comms timeout
        0,      // Grid partition
        {QStringLiteral("slope"), QStringLiteral("weather")}
    };
}

void SARModeManager::applyMode(int mode)
{
    if (mode < 0 || mode >= static_cast<int>(ModeCount)) {
        qCWarning(SARModeManagerLog) << "Invalid disaster mode index:" << mode;
        return;
    }

    _currentMode = static_cast<DisasterMode>(mode);
    const DisasterProfile &p = _profiles[mode];

    qCDebug(SARModeManagerLog) << "Applying disaster mode:" << p.name;

    // Push profile values into each subsystem
    if (_missionMgr) {
        _missionMgr->setCurrentPattern(static_cast<SARMissionManager::SearchPattern>(p.searchPattern));
        _missionMgr->setSearchAltitude(p.searchAltitude);
        _missionMgr->setSearchSpeed(p.searchSpeed);
        _missionMgr->setTrackSpacing(p.trackSpacing);
    }

    if (_coverageMgr) {
        _coverageMgr->setCellSizeMeters(p.cellSizeMeters);
        _coverageMgr->setCameraFovMeters(p.cameraFovMeters);
    }

    if (_reTaskMgr) {
        _reTaskMgr->setSelectionStrategy(static_cast<SARReTaskingManager::SelectionStrategy>(p.selectionStrategy));
        _reTaskMgr->setMinimumPriority(p.minimumPriority);
        _reTaskMgr->setAutoConfirmTimeoutSec(p.autoConfirmTimeoutSec);
    }

    if (_coordinator) {
        _coordinator->setSafetyBubbleHorizontalM(p.safetyBubbleHorizontalM);
        _coordinator->setSafetyBubbleVerticalM(p.safetyBubbleVerticalM);
        _coordinator->setCommsLossTimeoutSec(p.commsLossTimeoutSec);
    }

    _isCustomized = false;
    emit customizedChanged();
    emit modeChanged();
    emit modeApplied(mode);
}

void SARModeManager::resetToMode()
{
    applyMode(static_cast<int>(_currentMode));
}

void SARModeManager::markCustomized()
{
    if (!_isCustomized) {
        _isCustomized = true;
        emit customizedChanged();
    }
}

QString SARModeManager::currentModeName() const
{
    if (_currentMode >= 0 && _currentMode < _profiles.size()) {
        return _profiles[_currentMode].name;
    }
    return QString();
}

QString SARModeManager::description() const
{
    if (_currentMode >= 0 && _currentMode < _profiles.size()) {
        return _profiles[_currentMode].description;
    }
    return QString();
}

QString SARModeManager::modeColor() const
{
    if (_currentMode >= 0 && _currentMode < _profiles.size()) {
        return _profiles[_currentMode].color;
    }
    return QStringLiteral("#3B82F6");
}

QString SARModeManager::modeIcon() const
{
    if (_currentMode >= 0 && _currentMode < _profiles.size()) {
        return _profiles[_currentMode].icon;
    }
    return QString();
}

QStringList SARModeManager::recommendedOverlays() const
{
    if (_currentMode >= 0 && _currentMode < _profiles.size()) {
        return _profiles[_currentMode].recommendedOverlays;
    }
    return {};
}

int SARModeManager::recommendedPartitionStrategy() const
{
    if (_currentMode >= 0 && _currentMode < _profiles.size()) {
        return _profiles[_currentMode].partitionStrategy;
    }
    return 0;
}

QStringList SARModeManager::availableModeNames() const
{
    QStringList names;
    names.reserve(_profiles.size());
    for (const auto &p : _profiles) {
        names.append(p.name);
    }
    return names;
}

QString SARModeManager::modeNameForIndex(int index) const
{
    if (index >= 0 && index < _profiles.size()) {
        return _profiles[index].name;
    }
    return QString();
}

QString SARModeManager::modeDescriptionForIndex(int index) const
{
    if (index >= 0 && index < _profiles.size()) {
        return _profiles[index].description;
    }
    return QString();
}

QString SARModeManager::modeColorForIndex(int index) const
{
    if (index >= 0 && index < _profiles.size()) {
        return _profiles[index].color;
    }
    return QStringLiteral("#3B82F6");
}

QJsonObject SARModeManager::settingsToJson() const
{
    QJsonObject json;
    json[QStringLiteral("currentMode")] = static_cast<int>(_currentMode);
    json[QStringLiteral("isCustomized")] = _isCustomized;
    return json;
}

void SARModeManager::settingsFromJson(const QJsonObject &json)
{
    int mode = json.value(QStringLiteral("currentMode")).toInt(0);
    bool customized = json.value(QStringLiteral("isCustomized")).toBool(false);

    if (mode >= 0 && mode < static_cast<int>(ModeCount)) {
        _currentMode = static_cast<DisasterMode>(mode);
        _isCustomized = customized;

        // If not customized, re-apply the profile to ensure subsystem values match
        if (!customized) {
            applyMode(mode);
        } else {
            // Just update our state — subsystem values were already loaded from their own JSON
            emit modeChanged();
            emit customizedChanged();
        }
    }
}
