import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import "qrc:/custom/Sadron" as Sadron

// Toolbar indicator showing mesh network health
Item {
    id: meshIndicator
    width:      indicatorPill.width
    height:     parent.height

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    function meshHealthColor() {
        if (!meshNetworkManager) return Qt.rgba(1, 1, 1, 0.45)
        switch (meshNetworkManager.meshHealth) {
        case 0: return qgcPal.colorGreen
        case 1: return qgcPal.colorOrange
        case 2: return qgcPal.colorRed
        default: return Qt.rgba(1, 1, 1, 0.45)
        }
    }

    Component {
        id: meshIndicatorPageComponent

        MeshStatusPanel {
            width: ScreenTools.defaultFontPixelWidth * 32
            height: ScreenTools.defaultFontPixelHeight * 18
        }
    }

    Sadron.SadronGlassPanel {
        id: indicatorPill
        anchors.verticalCenter: parent.verticalCenter
        height: indicatorRow.implicitHeight + ScreenTools.defaultFontPixelHeight * 0.4
        width: indicatorRow.implicitWidth + ScreenTools.defaultFontPixelWidth
        padding: ScreenTools.defaultFontPixelWidth * 0.5
        radius: height / 2
        elevated: false
        panelColor: qgcPal.windowShadeDark
        panelOpacity: qgcPal.globalTheme === QGCPalette.Light ? 0.86 : 0.76
        borderColor: Qt.rgba(1, 1, 1, 0.08)

        RowLayout {
            id: indicatorRow
            anchors.centerIn: parent
            spacing: ScreenTools.defaultFontPixelWidth * 0.5

            Rectangle {
                width: ScreenTools.defaultFontPixelHeight * 0.55
                height: width
                radius: width / 2
                color: meshIndicator.meshHealthColor()

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
                text:   "MESH"
                color:  qgcPal.text
                font.pointSize: ScreenTools.smallFontPointSize
                font.bold: true
            }

            QGCLabel {
                text:   meshNetworkManager ? meshNetworkManager.onlineCount + "/" + meshNetworkManager.nodeCount : "0/0"
                color:  qgcPal.text
                font.pointSize: ScreenTools.smallFontPointSize
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: mainWindow.showIndicatorDrawer(meshIndicatorPageComponent, meshIndicator)
    }
}
