import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

// Toolbar indicator showing SAR operation status
Item {
    id: sarIndicator
    width:      sarRow.width
    height:     parent.height

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    RowLayout {
        id: sarRow
        anchors.verticalCenter: parent.verticalCenter
        spacing: ScreenTools.defaultFontPixelWidth * 0.25

        Rectangle {
            width:  sarIcon.width + 6
            height: sarIcon.height + 4
            radius: 3
            color:  sarMissionManager && sarMissionManager.missionActive ? "#e67e22" : "#555"

            QGCLabel {
                id: sarIcon
                anchors.centerIn: parent
                text:   "SAR"
                color:  "white"
                font.pointSize: ScreenTools.smallFontPointSize
                font.bold: true
            }
        }

        QGCLabel {
            text: {
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
            color:  qgcPal.text
            font.pointSize: ScreenTools.smallFontPointSize
        }

        // Coverage percentage
        QGCLabel {
            text:       sarCoverageTracker ? sarCoverageTracker.coveragePercent.toFixed(0) + "%" : ""
            color:      "#2ecc71"
            font.pointSize: ScreenTools.smallFontPointSize
            visible:    sarMissionManager && sarMissionManager.missionActive
        }

        // Target count
        QGCLabel {
            text:       sarTargetManager && sarTargetManager.totalTargets > 0 ? sarTargetManager.totalTargets + "T" : ""
            color:      "#f39c12"
            font.pointSize: ScreenTools.smallFontPointSize
            visible:    sarTargetManager && sarTargetManager.totalTargets > 0
        }
    }
}
