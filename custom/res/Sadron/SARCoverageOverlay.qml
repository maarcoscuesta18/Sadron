import QtQuick
import QtQuick.Controls

import QGroundControl
import QGroundControl.Controls

// Coverage percentage HUD overlay (bottom-right corner)
Item {
    id: coverageOverlay

    property var mapControl
    property bool showOverlay: true

    visible: showOverlay && sarCoverageTracker && sarCoverageTracker.totalCells > 0

    // Coverage summary overlay in corner
    Rectangle {
        anchors.bottom:         parent.bottom
        anchors.right:          parent.right
        anchors.margins:        ScreenTools.defaultFontPixelWidth * 0.5
        width:                  coverageInfo.width + 16
        height:                 coverageInfo.height + 12
        radius:                 4
        color:                  Qt.rgba(0, 0, 0, 0.7)
        visible:                showOverlay

        Column {
            id: coverageInfo
            anchors.centerIn: parent
            spacing: 2

            QGCLabel {
                text:       "Coverage"
                color:      "#e67e22"
                font.bold:  true
                font.pointSize: ScreenTools.smallFontPointSize
            }

            QGCLabel {
                text:       sarCoverageTracker ? sarCoverageTracker.coveragePercent.toFixed(1) + "%" : "0%"
                color:      "#2ecc71"
                font.pointSize: ScreenTools.mediumFontPointSize
                font.bold:  true
            }

            QGCLabel {
                text: {
                    if (!sarCoverageTracker) return ""
                    return sarCoverageTracker.cellsSearched + " / " + sarCoverageTracker.totalCells + " cells"
                }
                color:      "#aaa"
                font.pointSize: ScreenTools.smallFontPointSize
            }

            // Per-zone coverage
            Repeater {
                model: sarZoneManager ? sarZoneManager.zones : null

                QGCLabel {
                    text: object.name + ": " + (object.progress * 100).toFixed(0) + "%"
                    color: object.progress > 0.8 ? "#2ecc71" :
                           object.progress > 0.4 ? "#f1c40f" : "#e74c3c"
                    font.pointSize: ScreenTools.smallFontPointSize
                }
            }
        }
    }
}
