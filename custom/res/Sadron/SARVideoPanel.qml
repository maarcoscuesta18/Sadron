import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Panel displaying all connected drone video feeds and telemetry in a scrollable grid.
/// The active vehicle gets a live video stream; others show telemetry + compass with a
/// "Tap to switch" action. Includes global and per-tile recording controls.
Rectangle {
    id: _root

    color:  Qt.rgba(0.08, 0.08, 0.1, 0.98)
    radius: ScreenTools.defaultFontPixelWidth * 0.5

    property real _margin:      ScreenTools.defaultFontPixelWidth * 0.75
    property var  _vehicles:    QGroundControl.multiVehicleManager.vehicles
    property var  _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle
    property bool _recording:   QGroundControl.videoManager.recording
    property int  _vehicleCount: _vehicles ? _vehicles.count : 0

    // Determine grid columns based on available width and vehicle count
    property int _columns: {
        var w = _root.width
        if (w < ScreenTools.defaultFontPixelWidth * 30 || _vehicleCount <= 1) return 1
        if (w < ScreenTools.defaultFontPixelWidth * 55 || _vehicleCount <= 2) return 2
        return 3
    }

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    ColumnLayout {
        anchors.fill:       parent
        anchors.margins:    _margin
        spacing:            _margin

        // ── Header Bar ──
        Rectangle {
            Layout.fillWidth:   true
            height:             headerLayout.height + _margin * 2
            radius:             _margin * 0.5
            color:              Qt.rgba(0.9, 0.5, 0.13, 0.15)

            RowLayout {
                id:                     headerLayout
                anchors.left:           parent.left
                anchors.right:          parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins:        _margin
                spacing:                _margin

                QGCLabel {
                    text:               qsTr("Drone Feeds")
                    color:              "#e67e22"
                    font.bold:          true
                    font.pointSize:     ScreenTools.mediumFontPointSize
                }

                QGCLabel {
                    text:       "(" + _vehicleCount + ")"
                    color:      qgcPal.text
                    font.pointSize: ScreenTools.smallFontPointSize
                }

                Item { Layout.fillWidth: true }

                // Recording elapsed timer
                QGCLabel {
                    id:         recTimerLabel
                    visible:    _recording
                    color:      "#ff4444"
                    font.pointSize: ScreenTools.smallFontPointSize
                    font.bold:  true
                    text:       "REC " + _formatDuration(_recSeconds)

                    property int _recSeconds: 0

                    function _formatDuration(s) {
                        var h = Math.floor(s / 3600)
                        var m = Math.floor((s % 3600) / 60)
                        var sec = s % 60
                        return (h > 0 ? h + ":" : "") +
                               (m < 10 ? "0" : "") + m + ":" +
                               (sec < 10 ? "0" : "") + sec
                    }

                    Timer {
                        running:    _recording
                        interval:   1000
                        repeat:     true
                        onTriggered: recTimerLabel._recSeconds++
                        onRunningChanged: {
                            if (!running) recTimerLabel._recSeconds = 0
                        }
                    }

                    // Blinking dot
                    Rectangle {
                        anchors.right:      parent.left
                        anchors.rightMargin: _margin * 0.5
                        anchors.verticalCenter: parent.verticalCenter
                        width:              ScreenTools.defaultFontPixelHeight * 0.5
                        height:             width
                        radius:             width / 2
                        color:              "#ff4444"

                        SequentialAnimation on opacity {
                            running:    _recording
                            loops:      Animation.Infinite
                            NumberAnimation { to: 0.2; duration: 600 }
                            NumberAnimation { to: 1.0; duration: 600 }
                        }
                    }
                }

                // Global Record All / Stop All button
                Rectangle {
                    width:      recAllRow.width + ScreenTools.defaultFontPixelWidth * 1.5
                    height:     recAllRow.height + _margin
                    radius:     height / 2
                    color:      recAllMa.containsMouse
                                ? (_recording ? Qt.rgba(1, 0, 0, 0.3) : Qt.rgba(1, 0, 0, 0.2))
                                : Qt.rgba(1, 1, 1, 0.08)
                    border.color: _recording ? "#ff4444" : Qt.rgba(1, 1, 1, 0.3)
                    border.width: 1

                    RowLayout {
                        id:                 recAllRow
                        anchors.centerIn:   parent
                        spacing:            _margin * 0.5

                        // Record dot/square
                        Rectangle {
                            width:  ScreenTools.defaultFontPixelHeight * 0.6
                            height: width
                            radius: _recording ? 2 : width / 2
                            color:  "#ff4444"
                            Layout.alignment: Qt.AlignVCenter
                        }

                        QGCLabel {
                            text:       _recording ? qsTr("Stop") : qsTr("Rec All")
                            color:      _recording ? "#ff4444" : "white"
                            font.pointSize: ScreenTools.smallFontPointSize
                            font.bold:  true
                        }
                    }

                    MouseArea {
                        id:             recAllMa
                        anchors.fill:   parent
                        hoverEnabled:   true
                        cursorShape:    Qt.PointingHandCursor
                        onClicked: {
                            if (_recording) {
                                QGroundControl.videoManager.stopRecording()
                            } else {
                                QGroundControl.videoManager.startRecording()
                            }
                        }
                    }

                    ToolTip {
                        visible:    recAllMa.containsMouse
                        text:       _recording
                                    ? qsTr("Stop recording active drone's feed")
                                    : qsTr("Start recording active drone's feed")
                        delay:      700
                    }
                }
            }
        }

        // ── Info bar: which drone is streaming ──
        Rectangle {
            Layout.fillWidth:   true
            height:             infoRow.height + _margin
            radius:             _margin * 0.5
            color:              Qt.rgba(0.2, 0.6, 0.9, 0.1)
            visible:            _activeVehicle !== null

            RowLayout {
                id:                 infoRow
                anchors.left:       parent.left
                anchors.right:      parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins:    _margin
                spacing:            _margin

                Rectangle {
                    width:  ScreenTools.defaultFontPixelHeight * 0.5
                    height: width
                    radius: width / 2
                    color:  QGroundControl.videoManager.decoding ? "#2ecc71" : "#f39c12"
                }

                QGCLabel {
                    text:   QGroundControl.videoManager.decoding
                            ? qsTr("Live feed: V%1").arg(_activeVehicle ? _activeVehicle.id : "?")
                            : qsTr("No video signal from V%1").arg(_activeVehicle ? _activeVehicle.id : "?")
                    color:  qgcPal.text
                    font.pointSize: ScreenTools.smallFontPointSize
                }

                Item { Layout.fillWidth: true }

                QGCLabel {
                    text:   _activeVehicle ? _activeVehicle.flightMode : ""
                    color:  "#2ecc71"
                    font.pointSize: ScreenTools.smallFontPointSize
                }
            }
        }

        // ── Scrollable Tile Grid ──
        Flickable {
            Layout.fillWidth:   true
            Layout.fillHeight:  true
            contentHeight:      tileGrid.height
            clip:               true
            flickableDirection:  Flickable.VerticalFlick
            boundsBehavior:     Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {
                policy:     ScrollBar.AsNeeded
            }

            GridLayout {
                id:             tileGrid
                width:          parent.width
                columns:        _columns
                columnSpacing:  _margin
                rowSpacing:     _margin

                Repeater {
                    model: _vehicles

                    Loader {
                        Layout.fillWidth:   true
                        Layout.preferredHeight: {
                            // Aspect ratio roughly 4:3 based on available width
                            var tileW = (tileGrid.width - (_columns - 1) * _margin) / _columns
                            return tileW * 0.85
                        }

                        source: "qrc:/custom/Sadron/SARDroneVideoTile.qml"

                        property var _vehicle: object

                        onLoaded: {
                            item.vehicle = Qt.binding(function() { return _vehicle })
                            item.switchRequested.connect(function(v) {
                                if (v) {
                                    QGroundControl.multiVehicleManager.activeVehicle = v
                                }
                            })
                            item.recordToggled.connect(function(v) {
                                if (v && QGroundControl.multiVehicleManager.activeVehicle === v) {
                                    if (QGroundControl.videoManager.recording) {
                                        QGroundControl.videoManager.stopRecording()
                                    } else {
                                        QGroundControl.videoManager.startRecording()
                                    }
                                }
                            })
                        }
                    }
                }
            }
        }

        // ── Empty state ──
        Item {
            Layout.fillWidth:   true
            Layout.fillHeight:  true
            visible:            _vehicleCount === 0

            ColumnLayout {
                anchors.centerIn:   parent
                spacing:            _margin * 2

                QGCLabel {
                    Layout.alignment:   Qt.AlignHCenter
                    text:               qsTr("No Drones Connected")
                    color:              Qt.rgba(1, 1, 1, 0.4)
                    font.pointSize:     ScreenTools.mediumFontPointSize
                }

                QGCLabel {
                    Layout.alignment:   Qt.AlignHCenter
                    text:               qsTr("Connect drones to view their video feeds\nand telemetry data here.")
                    color:              Qt.rgba(1, 1, 1, 0.25)
                    font.pointSize:     ScreenTools.smallFontPointSize
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }
}
