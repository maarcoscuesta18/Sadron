import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import "qrc:/custom/Sadron" as Sadron

// Mesh network topology and health panel
Item {
    id: meshPanel

    property real _margin: ScreenTools.defaultFontPixelWidth * 0.5

    Sadron.SadronGlassPanel {
        anchors.fill:   parent
        padding:        0
        radius:         ScreenTools.defaultFontPixelHeight * 0.5
        panelColor:     QGroundControl.globalPalette.windowShadeDark
        panelOpacity:   QGroundControl.globalPalette.globalTheme === QGCPalette.Light ? 0.92 : 0.86
        borderColor:    Qt.rgba(1, 1, 1, 0.08)

        ColumnLayout {
            anchors.fill:       parent
            anchors.margins:    _margin
            spacing:            _margin

            // Header
            RowLayout {
                Layout.fillWidth: true

                QGCLabel {
                    text:               "Mesh Network"
                    color:              QGroundControl.globalPalette.brandingPurple
                    font.bold:          true
                    Layout.fillWidth:   true
                }

                // Health indicator
                Rectangle {
                    width:      healthLabel.width + 12
                    height:     healthLabel.height + 6
                    radius:     height / 2
                    color: {
                        if (!meshNetworkManager) return "#888"
                        switch (meshNetworkManager.meshHealth) {
                        case 0: return "#2ecc71"    // Healthy
                        case 1: return "#f39c12"    // Partial
                        case 2: return "#e74c3c"    // Critical
                        default: return "#888"      // Disconnected
                        }
                    }

                    QGCLabel {
                        id: healthLabel
                        anchors.centerIn: parent
                        text: {
                            if (!meshNetworkManager) return "N/A"
                            switch (meshNetworkManager.meshHealth) {
                            case 0: return "HEALTHY"
                            case 1: return "PARTIAL"
                            case 2: return "CRITICAL"
                            default: return "OFFLINE"
                            }
                        }
                        color:      "white"
                        font.pointSize: ScreenTools.smallFontPointSize
                        font.bold:  true
                    }
                }
            }

            // Stats row
            RowLayout {
                Layout.fillWidth: true
                spacing: _margin * 3

                QGCLabel {
                    text:   "Nodes: " + (meshNetworkManager ? meshNetworkManager.nodeCount : 0)
                    color:  "white"
                    font.pointSize: ScreenTools.smallFontPointSize
                }

                QGCLabel {
                    text:   "Online: " + (meshNetworkManager ? meshNetworkManager.onlineCount : 0)
                    color:  "#2ecc71"
                    font.pointSize: ScreenTools.smallFontPointSize
                }

                QGCLabel {
                    text:   "Range: " + (meshNetworkManager ? meshNetworkManager.maxRangeMeters.toFixed(0) + "m" : "N/A")
                    color:  "#aaa"
                    font.pointSize: ScreenTools.smallFontPointSize
                }
            }

            // Node list
            ListView {
                Layout.fillWidth:   true
                Layout.fillHeight:  true
                clip:               true
                spacing:            _margin
                model:              meshNetworkManager ? meshNetworkManager.nodes : null

                delegate: Rectangle {
                    width:      ListView.view.width
                    height:     nodeRow.height + _margin * 2
                    radius:     _margin
                    color:      Qt.rgba(1, 1, 1, 0.1)

                    RowLayout {
                        id: nodeRow
                        anchors.left:       parent.left
                        anchors.right:      parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins:    _margin
                        spacing:            _margin

                        // Vehicle ID
                        Rectangle {
                            width:      idLabel.width + 8
                            height:     idLabel.height + 4
                            radius:     4
                            color: {
                                switch (object.status) {
                                case 0: return "#2ecc71"    // Online
                                case 1: return "#f39c12"    // Degraded
                                case 2: return "#e74c3c"    // Offline
                                default: return "#3498db"   // Returning
                                }
                            }

                            QGCLabel {
                                id: idLabel
                                anchors.centerIn: parent
                                text:   "V" + object.vehicleId
                                color:  "white"
                                font.bold: true
                                font.pointSize: ScreenTools.smallFontPointSize
                            }
                        }

                        // Signal strength bar
                        Rectangle {
                            width:                  60
                            height:                 6
                            radius:                 3
                            color:                  Qt.rgba(1, 1, 1, 0.2)
                            Layout.alignment:       Qt.AlignVCenter

                            Rectangle {
                                width:  parent.width * object.signalStrength / 100
                                height: parent.height
                                radius: 3
                                color:  object.signalStrength > 70 ? "#2ecc71" :
                                        object.signalStrength > 30 ? "#f39c12" : "#e74c3c"
                            }
                        }

                        // Battery
                        QGCLabel {
                            text:   object.batteryPercent.toFixed(0) + "%"
                            color:  object.batteryPercent > 30 ? "#2ecc71" :
                                    object.batteryPercent > 15 ? "#f39c12" : "#e74c3c"
                            font.pointSize: ScreenTools.smallFontPointSize
                        }

                        // Links count
                        QGCLabel {
                            text:   object.linkCount + " links"
                            color:  "#aaa"
                            font.pointSize: ScreenTools.smallFontPointSize
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }
            }
        }
    }
}
