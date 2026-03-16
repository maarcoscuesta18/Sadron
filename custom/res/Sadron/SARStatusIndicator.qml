import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import "qrc:/custom/Sadron" as Sadron

// Toolbar indicator showing SAR operation status
Item {
    id: sarIndicator
    width:      indicatorPill.width
    height:     parent.height

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    function phaseText() {
        if (!sarMissionManager) return "OFF"
        if (!sarMissionManager.missionActive) return "STANDBY"
        switch (sarMissionManager.phase) {
        case 0: return "PLAN"
        case 1: return "BRIEF"
        case 2: return "DEPLOY"
        case 3: return "SEARCH"
        case 4: return "INVEST"
        case 5: return "RECOV"
        case 6: return "DEBRIEF"
        default: return "?"
        }
    }

    function phaseColor() {
        if (!sarMissionManager || !sarMissionManager.missionActive) return qgcPal.brandingPurple
        switch (sarMissionManager.phase) {
        case 3: return qgcPal.colorGreen
        case 4: return qgcPal.colorOrange
        case 5: return qgcPal.colorRed
        default: return qgcPal.brandingPurple
        }
    }

    Component {
        id: sarIndicatorPageComponent

        Item {
            implicitWidth: ScreenTools.defaultFontPixelWidth * 28
            implicitHeight: contentColumn.implicitHeight

            ColumnLayout {
                id: contentColumn
                anchors.fill: parent
                spacing: ScreenTools.defaultFontPixelHeight * 0.5

                QGCLabel {
                    text: qsTr("SAR Status")
                    color: qgcPal.brandingPurple
                    font.bold: true
                    font.pointSize: ScreenTools.mediumFontPointSize
                }

                QGCLabel {
                    text: qsTr("Phase: %1").arg(sarIndicator.phaseText())
                    color: sarIndicator.phaseColor()
                }

                QGCLabel {
                    text: qsTr("Coverage: %1%").arg(sarCoverageTracker ? sarCoverageTracker.coveragePercent.toFixed(1) : "0.0")
                    color: qgcPal.colorGreen
                }

                QGCLabel {
                    text: qsTr("Targets: %1").arg(sarTargetManager ? sarTargetManager.totalTargets : 0)
                    color: qgcPal.colorOrange
                }

                QGCLabel {
                    text: qsTr("Zones: %1").arg(sarZoneManager ? sarZoneManager.totalZones : 0)
                    color: qgcPal.text
                }
            }
        }
    }

    Sadron.SadronGlassPanel {
        id: indicatorPill
        anchors.verticalCenter: parent.verticalCenter
        height: statusRow.implicitHeight + ScreenTools.defaultFontPixelHeight * 0.4
        width: statusRow.implicitWidth + ScreenTools.defaultFontPixelWidth
        padding: ScreenTools.defaultFontPixelWidth * 0.5
        radius: height / 2
        elevated: false
        panelColor: qgcPal.windowShadeDark
        panelOpacity: qgcPal.globalTheme === QGCPalette.Light ? 0.86 : 0.76
        borderColor: Qt.rgba(1, 1, 1, 0.08)

        RowLayout {
            id: statusRow
            anchors.centerIn: parent
            spacing: ScreenTools.defaultFontPixelWidth * 0.5

            Rectangle {
                width: ScreenTools.defaultFontPixelHeight * 0.55
                height: width
                radius: width / 2
                color: sarIndicator.phaseColor()

                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width * 1.8
                    height: width
                    radius: width / 2
                    color: parent.color
                    opacity: 0.18
                }
            }

            QGCLabel {
                text: "SAR"
                color: qgcPal.text
                font.bold: true
                font.pointSize: ScreenTools.smallFontPointSize
            }

            QGCLabel {
                text: sarIndicator.phaseText()
                color: qgcPal.text
                font.pointSize: ScreenTools.smallFontPointSize
            }

            QGCLabel {
                text: sarCoverageTracker ? sarCoverageTracker.coveragePercent.toFixed(0) + "%" : ""
                color: qgcPal.colorGreen
                font.pointSize: ScreenTools.smallFontPointSize
                visible: sarMissionManager && sarMissionManager.missionActive
            }

            QGCLabel {
                text: sarTargetManager && sarTargetManager.totalTargets > 0 ? sarTargetManager.totalTargets + "T" : ""
                color: qgcPal.colorOrange
                font.pointSize: ScreenTools.smallFontPointSize
                visible: sarTargetManager && sarTargetManager.totalTargets > 0
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: mainWindow.showIndicatorDrawer(sarIndicatorPageComponent, sarIndicator)
    }
}
