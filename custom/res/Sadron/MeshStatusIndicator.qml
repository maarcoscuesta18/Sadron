import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

// Toolbar indicator showing mesh network health
Item {
    id: meshIndicator
    width:      indicatorRow.width
    height:     parent.height

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    RowLayout {
        id: indicatorRow
        anchors.verticalCenter: parent.verticalCenter
        spacing: ScreenTools.defaultFontPixelWidth * 0.25

        // Mesh icon (simplified as text)
        Rectangle {
            width:  meshIcon.width + 6
            height: meshIcon.height + 4
            radius: 3
            color: {
                if (!meshNetworkManager) return "#555"
                switch (meshNetworkManager.meshHealth) {
                case 0: return "#2ecc71"
                case 1: return "#f39c12"
                case 2: return "#e74c3c"
                default: return "#555"
                }
            }

            QGCLabel {
                id: meshIcon
                anchors.centerIn: parent
                text:   "MESH"
                color:  "white"
                font.pointSize: ScreenTools.smallFontPointSize
                font.bold: true
            }
        }

        QGCLabel {
            text:   meshNetworkManager ? meshNetworkManager.onlineCount + "/" + meshNetworkManager.nodeCount : "0/0"
            color:  qgcPal.text
            font.pointSize: ScreenTools.smallFontPointSize
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: {
            // Could open mesh detail panel
        }
    }
}
