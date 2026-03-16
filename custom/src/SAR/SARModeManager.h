#pragma once

#include <QtCore/QObject>
#include <QtCore/QVector>
#include <QtCore/QStringList>
#include <QtCore/QJsonObject>
#include <QtCore/QLoggingCategory>
#include <QtQmlIntegration/QtQmlIntegration>

Q_DECLARE_LOGGING_CATEGORY(SARModeManagerLog)

class SARMissionManager;
class SARZoneManager;
class SARCoverageTracker;
class SARReTaskingManager;
class VehicleCoordinator;

// Orchestrates disaster-specific parameter profiles across all SAR subsystems.
// Each DisasterMode provides a complete set of tuned defaults for search pattern,
// altitude, speed, spacing, coverage cell size, safety bubbles, comms timeout, etc.
class SARModeManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")

    Q_PROPERTY(DisasterMode     currentMode         READ currentMode        NOTIFY modeChanged)
    Q_PROPERTY(QString          currentModeName     READ currentModeName    NOTIFY modeChanged)
    Q_PROPERTY(QString          description         READ description        NOTIFY modeChanged)
    Q_PROPERTY(QString          modeColor           READ modeColor          NOTIFY modeChanged)
    Q_PROPERTY(QString          modeIcon            READ modeIcon           NOTIFY modeChanged)
    Q_PROPERTY(bool             isCustomized        READ isCustomized       NOTIFY customizedChanged)
    Q_PROPERTY(QStringList      recommendedOverlays READ recommendedOverlays NOTIFY modeChanged)
    Q_PROPERTY(QStringList      availableModeNames  READ availableModeNames CONSTANT)
    Q_PROPERTY(int              recommendedPartitionStrategy READ recommendedPartitionStrategy NOTIFY modeChanged)

public:
    enum DisasterMode {
        StandardSAR = 0,
        Maritime,
        Wildfire,
        Flood,
        Earthquake,
        Avalanche,
        ModeCount
    };
    Q_ENUM(DisasterMode)

    struct DisasterProfile {
        QString     name;
        QString     description;
        QString     color;
        QString     icon;

        // Search parameters
        int         searchPattern;      // SARMissionManager::SearchPattern
        double      searchAltitude;     // meters AGL
        double      searchSpeed;        // m/s
        double      trackSpacing;       // meters

        // Coverage tracker
        double      cellSizeMeters;
        double      cameraFovMeters;

        // Re-tasking
        int         selectionStrategy;  // SARReTaskingManager::SelectionStrategy
        int         minimumPriority;
        int         autoConfirmTimeoutSec;

        // Vehicle coordination
        double      safetyBubbleHorizontalM;
        double      safetyBubbleVerticalM;
        int         commsLossTimeoutSec;

        // Partition recommendation (0=Grid, 2=Strip — matches SARZoneManager enum)
        int         partitionStrategy;

        // Recommended overlays
        QStringList recommendedOverlays;
    };

    explicit SARModeManager(SARMissionManager   *missionMgr,
                            SARZoneManager      *zoneMgr,
                            SARCoverageTracker  *coverageMgr,
                            SARReTaskingManager *reTaskMgr,
                            VehicleCoordinator  *coordinator,
                            QObject *parent = nullptr);
    ~SARModeManager() override = default;

    // Property getters
    DisasterMode    currentMode() const         { return _currentMode; }
    QString         currentModeName() const;
    QString         description() const;
    QString         modeColor() const;
    QString         modeIcon() const;
    bool            isCustomized() const        { return _isCustomized; }
    QStringList     recommendedOverlays() const;
    QStringList     availableModeNames() const;
    int             recommendedPartitionStrategy() const;

    // Actions
    Q_INVOKABLE void applyMode(int mode);
    Q_INVOKABLE void resetToMode();
    Q_INVOKABLE void markCustomized();
    Q_INVOKABLE int  modeCount() const          { return static_cast<int>(ModeCount); }
    Q_INVOKABLE QString modeNameForIndex(int index) const;
    Q_INVOKABLE QString modeDescriptionForIndex(int index) const;
    Q_INVOKABLE QString modeColorForIndex(int index) const;

    // Serialization
    QJsonObject settingsToJson() const;
    void settingsFromJson(const QJsonObject &json);

signals:
    void modeChanged();
    void modeApplied(int mode);
    void customizedChanged();

private:
    void _initProfiles();

    DisasterMode    _currentMode = StandardSAR;
    bool            _isCustomized = false;

    QVector<DisasterProfile> _profiles;

    // Sub-system references (non-owning)
    SARMissionManager   *_missionMgr = nullptr;
    SARZoneManager      *_zoneMgr = nullptr;
    SARCoverageTracker  *_coverageMgr = nullptr;
    SARReTaskingManager *_reTaskMgr = nullptr;
    VehicleCoordinator  *_coordinator = nullptr;
};
