import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlyView
import QGroundControl.FlightMap

/// Individual drone video + telemetry tile used inside SARVideoPanel.
/// Shows live video for the active vehicle, telemetry overlay for all vehicles.
Rectangle {
    id: _root

    /// The Vehicle object this tile represents
    property var vehicle:       null
    /// Whether this tile's vehicle is the active vehicle (gets the live video)
    property bool isActive:     vehicle && QGroundControl.multiVehicleManager.activeVehicle === vehicle
    /// Compact mode reduces font sizes for small tiles
    property bool compact:      false

    property real _margin:      ScreenTools.defaultFontPixelWidth * 0.5
    property real _smallFont:   compact ? ScreenTools.smallFontPointSize * 0.9 : ScreenTools.smallFontPointSize
    property real _tinyFont:    compact ? ScreenTools.smallFontPointSize * 0.8 : ScreenTools.smallFontPointSize * 0.9

    // Telemetry values
    property real _heading:     vehicle ? vehicle.heading.rawValue       : 0
    property real _groundSpeed: vehicle ? vehicle.groundSpeed.rawValue   : 0
    property real _climbRate:   vehicle ? vehicle.climbRate.rawValue     : 0
    property real _altitude:    vehicle ? vehicle.altitudeRelative.rawValue : 0
    property string _flightMode: vehicle ? vehicle.flightMode           : ""
    property bool _armed:       vehicle ? vehicle.armed                 : false
    property bool _flying:      vehicle ? vehicle.flying                : false

    color:          isActive ? Qt.rgba(0.9, 0.5, 0.13, 0.25) : Qt.rgba(0.12, 0.12, 0.15, 0.95)
    border.color:   isActive ? "#e67e22" : Qt.rgba(1, 1, 1, 0.15)
    border.width:   isActive ? 2 : 1
    radius:         ScreenTools.defaultFontPixelWidth * 0.5

    signal switchRequested(var vehicle)
    signal recordToggled(var vehicle)

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    // ── Helper: compass direction string ──
    function headingToCardinal(deg) {
        var dirs = ["N","NNE","NE","ENE","E","ESE","SE","SSE",
                    "S","SSW","SW","WSW","W","WNW","NW","NNW"]
        var idx = Math.round(((deg % 360) + 360) % 360 / 22.5) % 16
        return dirs[idx]
    }

    ColumnLayout {
        anchors.fill:       parent
        anchors.margins:    _margin
        spacing:            0

        // ── Header row: Vehicle ID, flight mode, record button, heading badge ──
        Rectangle {
            Layout.fillWidth:   true
            height:             headerRow.height + _margin
            radius:             _margin * 0.5
            color:              Qt.rgba(0, 0, 0, 0.5)

            RowLayout {
                id:                 headerRow
                anchors.left:       parent.left
                anchors.right:      parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: _margin
                anchors.rightMargin: _margin
                spacing:            _margin

                // Vehicle ID badge
                Rectangle {
                    width:      vidLabel.width + ScreenTools.defaultFontPixelWidth * 0.8
                    height:     vidLabel.height + 4
                    radius:     3
                    color:      _armed ? (_flying ? "#2ecc71" : "#3498db") : "#888"

                    QGCLabel {
                        id:                 vidLabel
                        anchors.centerIn:   parent
                        text:               vehicle ? "V" + vehicle.id : "—"
                        color:              "white"
                        font.bold:          true
                        font.pointSize:     _smallFont
                    }
                }

                // Flight mode / armed state
                QGCLabel {
                    text:               _armed ? _flightMode : "Disarmed"
                    color:              _armed ? "#2ecc71" : "#aaa"
                    font.pointSize:     _smallFont
                    Layout.fillWidth:   true
                    elide:              Text.ElideRight
                }

                // Record button (only functional for active vehicle)
                Rectangle {
                    id:         recButton
                    width:      ScreenTools.defaultFontPixelHeight * 1.3
                    height:     width
                    radius:     width / 2
                    color:      recMa.containsMouse ? Qt.rgba(1, 0, 0, 0.3) : "transparent"
                    border.color: isActive ? "#ff4444" : "#666"
                    border.width: 2
                    opacity:    isActive ? 1.0 : 0.4

                    Rectangle {
                        anchors.centerIn:   parent
                        width:              parent.width * 0.5
                        height:             width
                        radius:             QGroundControl.videoManager.recording ? 2 : width / 2
                        color:              isActive && QGroundControl.videoManager.recording ? "#ff4444" : (isActive ? "#ff4444" : "#666")
                    }

                    MouseArea {
                        id:             recMa
                        anchors.fill:   parent
                        hoverEnabled:   true
                        enabled:        isActive
                        onClicked:      _root.recordToggled(_root.vehicle)
                    }

                    ToolTip {
                        visible:    recMa.containsMouse && !isActive
                        text:       qsTr("Switch to this drone to record")
                        delay:      500
                    }
                }

                // Heading badge
                Rectangle {
                    width:      hdgText.width + ScreenTools.defaultFontPixelWidth * 0.6
                    height:     hdgText.height + 2
                    radius:     2
                    color:      Qt.rgba(1, 1, 1, 0.1)

                    QGCLabel {
                        id:                 hdgText
                        anchors.centerIn:   parent
                        text:               _heading.toFixed(0) + "\u00B0"
                        color:              "#3498db"
                        font.pointSize:     _smallFont
                        font.bold:          true
                    }
                }
            }
        }

        // ── Video / Placeholder area ──
        Item {
            Layout.fillWidth:   true
            Layout.fillHeight:  true

            // Live video feed (only for active vehicle)
            Loader {
                id:             videoLoader
                anchors.fill:   parent
                active:         isActive && QGroundControl.videoManager.hasVideo
                sourceComponent: videoComponent
            }

            Component {
                id: videoComponent

                Item {
                    anchors.fill: parent

                    FlightDisplayViewVideo {
                        id:             liveFeed
                        anchors.fill:   parent
                        useSmallFont:   true
                        visible:        QGroundControl.videoManager.isStreamSource
                    }

                    // UVC fallback
                    Loader {
                        anchors.fill:   parent
                        visible:        QGroundControl.videoManager.isUvc
                        source:         QGroundControl.videoManager.uvcEnabled
                                        ? "qrc:/qml/QGroundControl/FlyView/FlightDisplayViewUVC.qml"
                                        : "qrc:/qml/QGroundControl/FlyView/FlightDisplayViewDummy.qml"
                    }
                }
            }

            // Placeholder when video not available or not active vehicle
            Rectangle {
                anchors.fill:   parent
                color:          Qt.rgba(0.08, 0.08, 0.1, 1.0)
                visible:        !videoLoader.active || !QGroundControl.videoManager.decoding
                radius:         _margin * 0.5

                ColumnLayout {
                    anchors.centerIn:   parent
                    spacing:            _margin

                    // Direction arrow
                    Item {
                        Layout.alignment:   Qt.AlignHCenter
                        width:              ScreenTools.defaultFontPixelHeight * 3
                        height:             width

                        // Compass ring
                        Rectangle {
                            anchors.fill:   parent
                            radius:         width / 2
                            color:          "transparent"
                            border.color:   Qt.rgba(1, 1, 1, 0.15)
                            border.width:   1
                        }

                        // N / E / S / W labels
                        Repeater {
                            model: [
                                { label: "N", angle: 0 },
                                { label: "E", angle: 90 },
                                { label: "S", angle: 180 },
                                { label: "W", angle: 270 }
                            ]

                            QGCLabel {
                                property real rad: modelData.angle * Math.PI / 180
                                x: parent.width / 2 + (parent.width / 2 - 8) * Math.sin(rad) - width / 2
                                y: parent.height / 2 - (parent.height / 2 - 8) * Math.cos(rad) - height / 2
                                text:       modelData.label
                                color:      modelData.label === "N" ? "#e74c3c" : Qt.rgba(1, 1, 1, 0.4)
                                font.pointSize: _tinyFont
                                font.bold:  modelData.label === "N"
                            }
                        }

                        // Heading arrow
                        Image {
                            anchors.centerIn:   parent
                            width:              parent.width * 0.5
                            height:             width
                            source:             "/custom/img/compass_needle.svg"
                            fillMode:           Image.PreserveAspectFit
                            sourceSize.height:  height
                            rotation:           _heading
                        }
                    }

                    QGCLabel {
                        Layout.alignment:   Qt.AlignHCenter
                        text:               isActive ? qsTr("WAITING FOR VIDEO") : qsTr("Tap to view feed")
                        color:              isActive ? "#aaa" : "#e67e22"
                        font.pointSize:     _smallFont
                        font.bold:          !isActive
                    }

                    QGCLabel {
                        Layout.alignment:   Qt.AlignHCenter
                        text:               _groundSpeed.toFixed(1) + " m/s " + headingToCardinal(_heading)
                        color:              Qt.rgba(1, 1, 1, 0.6)
                        font.pointSize:     _tinyFont
                        visible:            !isActive
                    }
                }

                MouseArea {
                    anchors.fill:   parent
                    visible:        !isActive
                    cursorShape:    Qt.PointingHandCursor
                    onClicked:      _root.switchRequested(_root.vehicle)
                }
            }

            // Telemetry overlay on video (shown when video is active and decoding)
            Rectangle {
                anchors.left:       parent.left
                anchors.right:      parent.right
                anchors.bottom:     parent.bottom
                height:             telemetryOverlay.height + _margin * 2
                color:              Qt.rgba(0, 0, 0, 0.6)
                visible:            isActive && QGroundControl.videoManager.decoding

                RowLayout {
                    id:                 telemetryOverlay
                    anchors.left:       parent.left
                    anchors.right:      parent.right
                    anchors.bottom:     parent.bottom
                    anchors.margins:    _margin
                    spacing:            _margin * 2

                    // Direction mini-arrow
                    Item {
                        width:  ScreenTools.defaultFontPixelHeight * 1.2
                        height: width
                        Layout.alignment: Qt.AlignVCenter

                        Image {
                            anchors.centerIn:   parent
                            width:              parent.width * 0.8
                            height:             width
                            source:             "/custom/img/compass_needle.svg"
                            fillMode:           Image.PreserveAspectFit
                            sourceSize.height:  height
                            rotation:           _heading
                        }
                    }

                    QGCLabel {
                        text:   _heading.toFixed(0) + "\u00B0 " + headingToCardinal(_heading)
                        color:  "#3498db"
                        font.pointSize: _tinyFont
                        font.bold: true
                    }

                    QGCLabel {
                        text:   "SPD " + _groundSpeed.toFixed(1) + " m/s"
                        color:  "white"
                        font.pointSize: _tinyFont
                    }

                    QGCLabel {
                        text:   "ALT " + _altitude.toFixed(1) + " m"
                        color:  "white"
                        font.pointSize: _tinyFont
                    }

                    QGCLabel {
                        text:   "VS " + (_climbRate >= 0 ? "+" : "") + _climbRate.toFixed(1) + " m/s"
                        color:  _climbRate > 0.5 ? "#2ecc71" : (_climbRate < -0.5 ? "#e74c3c" : "white")
                        font.pointSize: _tinyFont
                    }

                    Item { Layout.fillWidth: true }
                }
            }
        }

        // ── Footer telemetry bar (shown for non-active drones, or when video not decoding) ──
        Rectangle {
            Layout.fillWidth:   true
            height:             footerGrid.height + _margin * 2
            radius:             _margin * 0.5
            color:              Qt.rgba(0, 0, 0, 0.5)
            visible:            !isActive || !QGroundControl.videoManager.decoding

            GridLayout {
                id:             footerGrid
                anchors.left:   parent.left
                anchors.right:  parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: _margin
                columns:        2
                columnSpacing:  _margin * 2
                rowSpacing:     1

                // Row 1
                QGCLabel {
                    text:   "\u2191 " + _groundSpeed.toFixed(1) + " m/s"
                    color:  "white"
                    font.pointSize: _tinyFont
                }
                QGCLabel {
                    text:   "\u2195 " + (_climbRate >= 0 ? "+" : "") + _climbRate.toFixed(1) + " m/s"
                    color:  _climbRate > 0.5 ? "#2ecc71" : (_climbRate < -0.5 ? "#e74c3c" : "white")
                    font.pointSize: _tinyFont
                }

                // Row 2
                QGCLabel {
                    text:   "\u2302 " + _altitude.toFixed(1) + " m"
                    color:  "white"
                    font.pointSize: _tinyFont
                }
                QGCLabel {
                    text:   "\u27A4 " + _heading.toFixed(0) + "\u00B0 " + headingToCardinal(_heading)
                    color:  "#3498db"
                    font.pointSize: _tinyFont
                    font.bold: true
                }

                // Row 3: Battery
                RowLayout {
                    spacing: _margin * 0.5

                    QGCLabel {
                        text:   "BAT"
                        color:  Qt.rgba(1, 1, 1, 0.5)
                        font.pointSize: _tinyFont
                    }

                    Rectangle {
                        width:      ScreenTools.defaultFontPixelWidth * 5
                        height:     3
                        radius:     1.5
                        color:      Qt.rgba(1, 1, 1, 0.15)

                        Rectangle {
                            property real battPct: _root.vehicle && _root.vehicle.batteries && _root.vehicle.batteries.count > 0
                                                   ? _root.vehicle.batteries.get(0).percentRemaining.rawValue / 100 : 0
                            width:      parent.width * Math.max(0, Math.min(1, battPct))
                            height:     parent.height
                            radius:     parent.radius
                            color:      battPct > 0.5 ? "#2ecc71" : battPct > 0.2 ? "#f39c12" : "#e74c3c"
                        }
                    }

                    QGCLabel {
                        text: {
                            if (_root.vehicle && _root.vehicle.batteries && _root.vehicle.batteries.count > 0) {
                                var pct = _root.vehicle.batteries.get(0).percentRemaining.rawValue
                                return isNaN(pct) ? "--" : pct.toFixed(0) + "%"
                            }
                            return "--"
                        }
                        color:  "white"
                        font.pointSize: _tinyFont
                    }
                }

                // Distance to home
                QGCLabel {
                    property real dist: _root.vehicle ? _root.vehicle.distanceToHome.rawValue : 0
                    text:   "HOME " + (isNaN(dist) ? "0" : dist.toFixed(0)) + " m"
                    color:  Qt.rgba(1, 1, 1, 0.6)
                    font.pointSize: _tinyFont
                }
            }
        }
    }
}
