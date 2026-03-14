import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

// Right-side per-zone editor panel for overriding search parameters
Rectangle {
    id: _root

    property var zone
    property var mapControl

    signal closeEditor()

    width:  ScreenTools.defaultFontPixelWidth * 28
    color:  qgcPal.window
    radius: ScreenTools.defaultFontPixelHeight * 0.25

    property real _margin: ScreenTools.defaultFontPixelWidth * 0.75

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    Flickable {
        anchors.fill:       parent
        anchors.margins:    _margin
        contentHeight:      mainCol.height
        clip:               true
        flickableDirection:  Flickable.VerticalFlick

        ColumnLayout {
            id:     mainCol
            width:  parent.width
            spacing: _margin

            // ── Header ──
            RowLayout {
                Layout.fillWidth: true

                // Color dot
                Rectangle {
                    width:  ScreenTools.defaultFontPixelHeight * 0.8
                    height: width
                    radius: width / 2
                    color:  _root.zone ? _root.zone.displayColor : "#888"
                }

                QGCLabel {
                    text:               _root.zone ? _root.zone.name : ""
                    font.bold:          true
                    font.pointSize:     ScreenTools.mediumFontPointSize
                    color:              "#e67e22"
                    Layout.fillWidth:   true
                }

                // Close button
                Rectangle {
                    width:  ScreenTools.defaultFontPixelHeight * 1.2
                    height: width
                    radius: width / 2
                    color:  closeMa.containsMouse ? Qt.rgba(1, 1, 1, 0.2) : "transparent"

                    QGCLabel {
                        anchors.centerIn:   parent
                        text:               "X"
                        color:              qgcPal.text
                        font.bold:          true
                        font.pointSize:     ScreenTools.smallFontPointSize
                    }

                    MouseArea {
                        id:             closeMa
                        anchors.fill:   parent
                        hoverEnabled:   true
                        onClicked:      _root.closeEditor()
                    }
                }
            }

            // Area display
            QGCLabel {
                visible: _root.zone !== null
                text: {
                    if (!_root.zone) return ""
                    var area = _root.zone.areaSqM
                    if (area > 1e6) return (area / 1e6).toFixed(2) + " km\u00B2"
                    return area.toFixed(0) + " m\u00B2"
                }
                color: qgcPal.text
                font.pointSize: ScreenTools.smallFontPointSize
            }

            // ── Grid Settings Section ──
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: gridSettingsCol.height + _margin * 2
                radius: _margin
                color:  qgcPal.windowShade

                ColumnLayout {
                    id: gridSettingsCol
                    anchors.left:       parent.left
                    anchors.right:      parent.right
                    anchors.top:        parent.top
                    anchors.margins:    _margin
                    spacing:            _margin

                    QGCLabel {
                        text:       qsTr("Grid Settings")
                        font.bold:  true
                        color:      "#e67e22"
                    }

                    // Use global params toggle
                    RowLayout {
                        Layout.fillWidth: true

                        QGCLabel {
                            text: qsTr("Use Global:")
                            Layout.fillWidth: true
                        }

                        Switch {
                            id: globalToggle
                            checked: _root.zone ? _root.zone.useGlobalParams : true
                            onToggled: {
                                if (_root.zone) _root.zone.useGlobalParams = checked
                            }
                        }
                    }

                    GridLayout {
                        columns:            2
                        Layout.fillWidth:   true
                        columnSpacing:      _margin * 2
                        rowSpacing:         _margin

                        QGCLabel { text: qsTr("Pattern:") }
                        ComboBox {
                            id:                 zonePatternCombo
                            Layout.fillWidth:   true
                            model:              [qsTr("Parallel Track"), qsTr("Creeping Line"), qsTr("Expanding Square"), qsTr("Sector")]
                            currentIndex:       _root.zone && _root.zone.searchPattern >= 0 ? Math.max(0, Math.min(3, _root.zone.searchPattern)) : (sarMissionManager ? sarMissionManager.currentPattern : 0)
                            enabled:            !globalToggle.checked
                            onActivated: {
                                if (_root.zone) _root.zone.searchPattern = currentIndex
                            }
                        }

                        QGCLabel { text: qsTr("Spacing (m):") }
                        SpinBox {
                            id:                 zoneSpacingSpin
                            from:               5
                            to:                 100
                            value:              _root.zone && _root.zone.trackSpacing > 0 ? _root.zone.trackSpacing : (sarMissionManager ? sarMissionManager.trackSpacing : 20)
                            Layout.fillWidth:   true
                            enabled:            !globalToggle.checked
                            onValueModified: {
                                if (_root.zone) _root.zone.trackSpacing = value
                            }
                        }
                    }
                }
            }

            // ── Flight Parameters Section ──
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: flightParamsCol.height + _margin * 2
                radius: _margin
                color:  qgcPal.windowShade

                ColumnLayout {
                    id: flightParamsCol
                    anchors.left:       parent.left
                    anchors.right:      parent.right
                    anchors.top:        parent.top
                    anchors.margins:    _margin
                    spacing:            _margin

                    QGCLabel {
                        text:       qsTr("Flight Parameters")
                        font.bold:  true
                        color:      "#e67e22"
                    }

                    GridLayout {
                        columns:            2
                        Layout.fillWidth:   true
                        columnSpacing:      _margin * 2
                        rowSpacing:         _margin

                        QGCLabel { text: qsTr("Altitude (m):") }
                        SpinBox {
                            id:                 zoneAltSpin
                            from:               10
                            to:                 120
                            value:              _root.zone && _root.zone.searchAltitude > 0 ? _root.zone.searchAltitude : (sarMissionManager ? sarMissionManager.searchAltitude : 30)
                            Layout.fillWidth:   true
                            enabled:            !globalToggle.checked
                            onValueModified: {
                                if (_root.zone) _root.zone.searchAltitude = value
                            }
                        }

                        QGCLabel { text: qsTr("Speed (m/s):") }
                        SpinBox {
                            id:                 zoneSpeedSpin
                            from:               1
                            to:                 15
                            value:              _root.zone && _root.zone.searchSpeed > 0 ? _root.zone.searchSpeed : (sarMissionManager ? sarMissionManager.searchSpeed : 5)
                            Layout.fillWidth:   true
                            enabled:            !globalToggle.checked
                            onValueModified: {
                                if (_root.zone) _root.zone.searchSpeed = value
                            }
                        }
                    }
                }
            }

            // ── Vehicle Assignment Section ──
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: vehicleAssignCol.height + _margin * 2
                radius: _margin
                color:  qgcPal.windowShade

                ColumnLayout {
                    id: vehicleAssignCol
                    anchors.left:       parent.left
                    anchors.right:      parent.right
                    anchors.top:        parent.top
                    anchors.margins:    _margin
                    spacing:            _margin

                    QGCLabel {
                        text:       qsTr("Vehicle Assignment")
                        font.bold:  true
                        color:      "#e67e22"
                    }

                    QGCLabel {
                        text: _root.zone && _root.zone.assignedVehicle >= 0
                              ? qsTr("Assigned: V%1").arg(_root.zone.assignedVehicle)
                              : qsTr("Unassigned")
                        color: _root.zone && _root.zone.assignedVehicle >= 0 ? "#2ecc71" : "#e74c3c"
                        font.pointSize: ScreenTools.smallFontPointSize
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: _margin

                        QGCLabel { text: qsTr("Vehicle ID:") }

                        SpinBox {
                            id:                 vehicleIdSpin
                            from:               1
                            to:                 20
                            value:              _root.zone && _root.zone.assignedVehicle > 0 ? _root.zone.assignedVehicle : 1
                            Layout.fillWidth:   true
                        }

                        QGCButton {
                            text: qsTr("Assign")
                            enabled: _root.zone && sarZoneManager && sarMissionManager
                                     && sarMissionManager.connectedVehicleCount >= 0
                                     && sarMissionManager.isVehicleConnected(vehicleIdSpin.value)
                            onClicked: {
                                if (_root.zone && sarZoneManager) {
                                    sarZoneManager.assignZoneToVehicle(_root.zone.zoneId, vehicleIdSpin.value)
                                }
                            }
                        }
                    }

                    // Warning when selected vehicle isn't connected
                    QGCLabel {
                        visible:    sarMissionManager
                                    && sarMissionManager.connectedVehicleCount >= 0
                                    && !sarMissionManager.isVehicleConnected(vehicleIdSpin.value)
                        text:       qsTr("Vehicle V%1 is not connected").arg(vehicleIdSpin.value)
                        color:      "#e74c3c"
                        font.italic: true
                        font.pointSize: ScreenTools.smallFontPointSize
                    }
                }
            }

            // ── Actions ──
            QGCButton {
                text:               qsTr("Regenerate Flight Path")
                Layout.fillWidth:   true
                onClicked: {
                    if (_root.zone && sarMissionManager) {
                        sarMissionManager.generateZoneTransect(_root.zone.zoneId)
                    }
                }
            }

            QGCButton {
                text:               qsTr("Center on Zone")
                Layout.fillWidth:   true
                onClicked: {
                    if (_root.zone && _root.zone.mapPolygon && _root.mapControl) {
                        _root.mapControl.center = _root.zone.mapPolygon.center
                    }
                }
            }
        }
    }
}
