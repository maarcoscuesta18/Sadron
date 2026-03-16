import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import "qrc:/custom/Sadron" as Sadron

// ============================================================================
// CoordinationStatusPanel — Multi-vehicle coordination status and settings
// Shows deconfliction status, proximity conflicts, comms loss events,
// failsafe verification, and coordination settings.
// ============================================================================
Item {
    id: coordPanel

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

            // ── Header with master toggle ──
            RowLayout {
                Layout.fillWidth: true

                QGCLabel {
                    text:               "Coordination"
                    color:              QGroundControl.globalPalette.brandingPurple
                    font.bold:          true
                    Layout.fillWidth:   true
                }

                Rectangle {
                    width:      statusLabel.width + 12
                    height:     statusLabel.height + 6
                    radius:     height / 2
                    color: {
                        if (!vehicleCoordinator || !vehicleCoordinator.enabled) return "#888"
                        var conflicts = vehicleCoordinator.activeConflictCount
                        var commsLoss = vehicleCoordinator.dronesInCommsLoss
                        var overlaps  = vehicleCoordinator.overlapCount
                        if (conflicts > 0 || commsLoss > 0) return "#e74c3c"
                        if (overlaps > 0) return "#f39c12"
                        return "#2ecc71"
                    }

                    QGCLabel {
                        id: statusLabel
                        anchors.centerIn: parent
                        text: {
                            if (!vehicleCoordinator || !vehicleCoordinator.enabled) return "OFF"
                            var conflicts = vehicleCoordinator.activeConflictCount
                            var commsLoss = vehicleCoordinator.dronesInCommsLoss
                            if (conflicts > 0 || commsLoss > 0) return "ALERT"
                            if (vehicleCoordinator.overlapCount > 0) return "WARN"
                            return "OK"
                        }
                        color:          "white"
                        font.pointSize: ScreenTools.smallFontPointSize
                        font.bold:      true
                    }
                }
            }

            // ── Enable toggle ──
            RowLayout {
                Layout.fillWidth: true

                QGCLabel {
                    text:   "Enable Coordination"
                    color:  "white"
                    font.pointSize: ScreenTools.smallFontPointSize
                    Layout.fillWidth: true
                }

                Switch {
                    checked: vehicleCoordinator ? vehicleCoordinator.enabled : false
                    onToggled: { if (vehicleCoordinator) vehicleCoordinator.enabled = checked }
                }
            }

            // ── Separator ──
            Rectangle { Layout.fillWidth: true; height: 1; color: Qt.rgba(1, 1, 1, 0.15) }

            // ── Deconfliction section ──
            QGCLabel {
                text:   "Sector Deconfliction"
                color:  "#3498db"
                font.bold: true
                font.pointSize: ScreenTools.smallFontPointSize
                visible: vehicleCoordinator && vehicleCoordinator.enabled
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: _margin * 3
                visible: vehicleCoordinator && vehicleCoordinator.enabled

                QGCLabel {
                    text:   "Overlaps: " + (vehicleCoordinator ? vehicleCoordinator.overlapCount : 0)
                    color:  vehicleCoordinator && vehicleCoordinator.overlapCount > 0 ? "#f39c12" : "#2ecc71"
                    font.pointSize: ScreenTools.smallFontPointSize
                }

                QGCLabel {
                    text:   "Violations: " + (vehicleCoordinator ? vehicleCoordinator.boundaryViolationCount : 0)
                    color:  vehicleCoordinator && vehicleCoordinator.boundaryViolationCount > 0 ? "#e74c3c" : "#2ecc71"
                    font.pointSize: ScreenTools.smallFontPointSize
                }
            }

            // ── Overlap list ──
            ListView {
                Layout.fillWidth: true
                height:           Math.min(contentHeight, ScreenTools.defaultFontPixelHeight * 4)
                clip:             true
                spacing:          2
                visible:          vehicleCoordinator && vehicleCoordinator.overlapCount > 0
                model:            vehicleCoordinator ? vehicleCoordinator.overlaps : null

                delegate: Rectangle {
                    width:  ListView.view.width
                    height: overlapRow.height + 4
                    radius: 3
                    color:  Qt.rgba(0.95, 0.6, 0.07, 0.15)

                    RowLayout {
                        id: overlapRow
                        anchors.left:   parent.left
                        anchors.right:  parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: 4
                        spacing: _margin

                        QGCLabel {
                            text:   "Z" + object.zoneIdA + " / Z" + object.zoneIdB
                            color:  "#f39c12"
                            font.pointSize: ScreenTools.smallFontPointSize
                            font.bold: true
                        }

                        QGCLabel {
                            text:   object.overlapPercent.toFixed(1) + "% overlap"
                            color:  "#aaa"
                            font.pointSize: ScreenTools.smallFontPointSize
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }
            }

            // ── Separator ──
            Rectangle {
                Layout.fillWidth: true; height: 1; color: Qt.rgba(1, 1, 1, 0.15)
                visible: vehicleCoordinator && vehicleCoordinator.enabled
            }

            // ── Proximity / Altitude Separation ──
            QGCLabel {
                text:   "Altitude Separation"
                color:  "#3498db"
                font.bold: true
                font.pointSize: ScreenTools.smallFontPointSize
                visible: vehicleCoordinator && vehicleCoordinator.enabled
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: _margin * 3
                visible: vehicleCoordinator && vehicleCoordinator.enabled

                QGCLabel {
                    text:   "Conflicts: " + (vehicleCoordinator ? vehicleCoordinator.activeConflictCount : 0)
                    color:  vehicleCoordinator && vehicleCoordinator.activeConflictCount > 0 ? "#e74c3c" : "#2ecc71"
                    font.pointSize: ScreenTools.smallFontPointSize
                }

                QGCLabel {
                    text:   "Bubble: " + (vehicleCoordinator ? vehicleCoordinator.safetyBubbleHorizontalM.toFixed(0) + "m H / " + vehicleCoordinator.safetyBubbleVerticalM.toFixed(0) + "m V" : "N/A")
                    color:  "#aaa"
                    font.pointSize: ScreenTools.smallFontPointSize
                }
            }

            // ── Conflict list ──
            ListView {
                Layout.fillWidth: true
                height:           Math.min(contentHeight, ScreenTools.defaultFontPixelHeight * 6)
                clip:             true
                spacing:          _margin
                visible:          vehicleCoordinator && vehicleCoordinator.activeConflictCount > 0
                model:            vehicleCoordinator ? vehicleCoordinator.conflicts : null

                delegate: Rectangle {
                    width:  ListView.view.width
                    height: conflictCol.height + _margin * 2
                    radius: _margin
                    color:  object.severity === 1 ? Qt.rgba(0.9, 0.3, 0.24, 0.2) : Qt.rgba(0.95, 0.6, 0.07, 0.15)
                    border.color: object.severity === 1 ? "#e74c3c" : "#f39c12"
                    border.width: 1

                    ColumnLayout {
                        id: conflictCol
                        anchors.left:   parent.left
                        anchors.right:  parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: _margin
                        spacing: 2

                        RowLayout {
                            spacing: _margin

                            Rectangle {
                                width:  sevLabel.width + 8
                                height: sevLabel.height + 4
                                radius: 3
                                color:  object.severity === 1 ? "#e74c3c" : "#f39c12"

                                QGCLabel {
                                    id: sevLabel
                                    anchors.centerIn: parent
                                    text:   object.severity === 1 ? "CRITICAL" : "WARNING"
                                    color:  "white"
                                    font.pointSize: ScreenTools.smallFontPointSize * 0.85
                                    font.bold: true
                                }
                            }

                            QGCLabel {
                                text:   "V" + object.vehicleIdA + " / V" + object.vehicleIdB
                                color:  "white"
                                font.pointSize: ScreenTools.smallFontPointSize
                                font.bold: true
                            }
                        }

                        RowLayout {
                            spacing: _margin * 2

                            QGCLabel {
                                text:   "H: " + object.horizontalDistM.toFixed(1) + "m"
                                color:  "#aaa"
                                font.pointSize: ScreenTools.smallFontPointSize
                            }
                            QGCLabel {
                                text:   "V: " + object.verticalDistM.toFixed(1) + "m"
                                color:  "#aaa"
                                font.pointSize: ScreenTools.smallFontPointSize
                            }
                        }

                        QGCLabel {
                            text:   object.resolution
                            color:  "#2ecc71"
                            font.pointSize: ScreenTools.smallFontPointSize * 0.9
                            visible: object.resolution !== ""
                        }
                    }
                }
            }

            // ── Separator ──
            Rectangle {
                Layout.fillWidth: true; height: 1; color: Qt.rgba(1, 1, 1, 0.15)
                visible: vehicleCoordinator && vehicleCoordinator.enabled
            }

            // ── Comms Loss section ──
            QGCLabel {
                text:   "Communications"
                color:  "#3498db"
                font.bold: true
                font.pointSize: ScreenTools.smallFontPointSize
                visible: vehicleCoordinator && vehicleCoordinator.enabled
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: _margin * 3
                visible: vehicleCoordinator && vehicleCoordinator.enabled

                QGCLabel {
                    text:   "Lost: " + (vehicleCoordinator ? vehicleCoordinator.dronesInCommsLoss : 0)
                    color:  vehicleCoordinator && vehicleCoordinator.dronesInCommsLoss > 0 ? "#e74c3c" : "#2ecc71"
                    font.pointSize: ScreenTools.smallFontPointSize
                }

                QGCLabel {
                    text:   "Timeout: " + (vehicleCoordinator ? vehicleCoordinator.commsLossTimeoutSec + "s" : "N/A")
                    color:  "#aaa"
                    font.pointSize: ScreenTools.smallFontPointSize
                }

                QGCLabel {
                    text:   "Failsafes: " + (vehicleCoordinator && vehicleCoordinator.allFailsafesVerified ? "OK" : "?")
                    color:  vehicleCoordinator && vehicleCoordinator.allFailsafesVerified ? "#2ecc71" : "#f39c12"
                    font.pointSize: ScreenTools.smallFontPointSize
                }
            }

            // ── Comms loss events list ──
            ListView {
                Layout.fillWidth: true
                height:           Math.min(contentHeight, ScreenTools.defaultFontPixelHeight * 6)
                clip:             true
                spacing:          _margin
                visible:          vehicleCoordinator && vehicleCoordinator.dronesInCommsLoss > 0
                model:            vehicleCoordinator ? vehicleCoordinator.commsLossEvents : null

                delegate: Rectangle {
                    width:  ListView.view.width
                    height: commsCol.height + _margin * 2
                    radius: _margin
                    color:  Qt.rgba(0.9, 0.3, 0.24, 0.15)
                    border.color: "#e74c3c"
                    border.width: 1

                    ColumnLayout {
                        id: commsCol
                        anchors.left:   parent.left
                        anchors.right:  parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: _margin
                        spacing: 2

                        RowLayout {
                            spacing: _margin

                            Rectangle {
                                width:  commsStateLabel.width + 8
                                height: commsStateLabel.height + 4
                                radius: 3
                                color: {
                                    switch (object.state) {
                                    case 0: return "#e74c3c"    // Detected
                                    case 1: return "#c0392b"    // RTLTriggered
                                    case 2: return "#2ecc71"    // CommsRestored
                                    case 3: return "#3498db"    // ZoneReassigned
                                    default: return "#888"
                                    }
                                }

                                QGCLabel {
                                    id: commsStateLabel
                                    anchors.centerIn: parent
                                    text: {
                                        switch (object.state) {
                                        case 0: return "LOST"
                                        case 1: return "RTL"
                                        case 2: return "RESTORED"
                                        case 3: return "REASSIGNED"
                                        default: return "?"
                                        }
                                    }
                                    color:  "white"
                                    font.pointSize: ScreenTools.smallFontPointSize * 0.85
                                    font.bold: true
                                }
                            }

                            QGCLabel {
                                text:   "V" + object.vehicleId + " (Zone " + object.zoneId + ")"
                                color:  "white"
                                font.pointSize: ScreenTools.smallFontPointSize
                                font.bold: true
                            }

                            QGCLabel {
                                text:   object.elapsedSec + "s"
                                color:  "#aaa"
                                font.pointSize: ScreenTools.smallFontPointSize
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignRight
                            }
                        }
                    }
                }
            }

            // Spacer
            Item { Layout.fillHeight: true }

            // ── Action buttons ──
            RowLayout {
                Layout.fillWidth: true
                spacing: _margin
                visible: vehicleCoordinator && vehicleCoordinator.enabled

                // Verify all failsafes
                Rectangle {
                    Layout.fillWidth: true
                    height:         verifyLabel.height + ScreenTools.defaultFontPixelHeight * 0.5
                    radius:         ScreenTools.defaultFontPixelHeight * 0.2
                    color:          verifyMa.containsMouse ? "#2980b9" : "#3498db"

                    QGCLabel {
                        id:             verifyLabel
                        anchors.centerIn: parent
                        text:           "Verify Failsafes"
                        color:          "white"
                        font.bold:      true
                        font.pointSize: ScreenTools.smallFontPointSize
                    }
                    MouseArea {
                        id:             verifyMa
                        anchors.fill:   parent
                        hoverEnabled:   true
                        onClicked:      { if (vehicleCoordinator) vehicleCoordinator.verifyAllFailsafes() }
                    }
                }

                // Check overlaps
                Rectangle {
                    Layout.fillWidth: true
                    height:         checkLabel.height + ScreenTools.defaultFontPixelHeight * 0.5
                    radius:         ScreenTools.defaultFontPixelHeight * 0.2
                    color:          checkMa.containsMouse ? "#7d3c98" : "#9b59b6"

                    QGCLabel {
                        id:             checkLabel
                        anchors.centerIn: parent
                        text:           "Check Overlaps"
                        color:          "white"
                        font.bold:      true
                        font.pointSize: ScreenTools.smallFontPointSize
                    }
                    MouseArea {
                        id:             checkMa
                        anchors.fill:   parent
                        hoverEnabled:   true
                        onClicked:      { if (vehicleCoordinator) vehicleCoordinator.validateZoneOverlaps() }
                    }
                }
            }
        }
    }
}
