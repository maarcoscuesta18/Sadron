import QtQuick
import QtLocation
import QtPositioning

import QGroundControl
import QGroundControl.Controls

// Renders per-vehicle search trails on the map:
// - Wide semi-transparent swath line (approximate camera coverage)
// - Thin solid trail line (exact flight path)
// - Colored vehicle label badge at current position
Item {
    id: _root

    property var  mapControl
    property bool showTrails: true

    // 8-color palette for vehicle differentiation
    readonly property var _trailColors: [
        "#e74c3c",  // red
        "#3498db",  // blue
        "#2ecc71",  // green
        "#f39c12",  // orange
        "#9b59b6",  // purple
        "#1abc9c",  // teal
        "#e67e22",  // dark orange
        "#34495e"   // dark blue-grey
    ]

    function _colorForVehicle(vehicle) {
        return _trailColors[vehicle.id % _trailColors.length]
    }

    // ── Swath lines (wide, semi-transparent) ──
    Instantiator {
        model:  (_root.mapControl) ? QGroundControl.multiVehicleManager.vehicles : null
        active: _root.mapControl !== null && _root.mapControl !== undefined

        delegate: Item {
            property var    _vehicle:   object
            property color  _color:     _colorForVehicle(_vehicle)
            property var    _path:      []

            MapPolyline {
                id:         swathLine
                parent:     _root.mapControl
                visible:    _root.showTrails && _path.length > 1
                line.width: 14
                line.color: Qt.rgba(_color.r, _color.g, _color.b, 0.25)
                path:       _path
                z:          QGroundControl.zOrderTrajectoryLines - 1
            }

            Connections {
                target: _vehicle.trajectoryPoints
                function onPointAdded(coordinate) {
                    _path.push(coordinate)
                    _path = _path  // trigger binding update
                }
                function onUpdateLastPoint(coordinate) {
                    if (_path.length > 0) {
                        _path[_path.length - 1] = coordinate
                        _path = _path
                    }
                }
                function onPointsCleared() {
                    _path = []
                }
            }

            Component.onCompleted: {
                _path = _vehicle.trajectoryPoints.list()
                parent = _root
            }
            Component.onDestruction: {
                swathLine.parent = null
            }
        }
    }

    // ── Trail lines (thin, solid) + vehicle label badges ──
    Instantiator {
        model:  (_root.mapControl) ? QGroundControl.multiVehicleManager.vehicles : null
        active: _root.mapControl !== null && _root.mapControl !== undefined

        delegate: Item {
            property var    _vehicle:   object
            property color  _color:     _colorForVehicle(_vehicle)
            property var    _path:      []

            MapPolyline {
                id:         trailLine
                parent:     _root.mapControl
                visible:    _root.showTrails && _path.length > 1
                line.width: 3
                line.color: _color
                path:       _path
                z:          QGroundControl.zOrderTrajectoryLines
            }

            // Vehicle label badge at current position
            MapQuickItem {
                id:         vehicleLabel
                parent:     _root.mapControl
                visible:    _root.showTrails && _vehicle.coordinate.isValid
                coordinate: _vehicle.coordinate
                anchorPoint.x: _badgeSize / 2
                anchorPoint.y: _badgeSize / 2
                z:          QGroundControl.zOrderTrajectoryLines + 1

                property real _badgeSize: ScreenTools.defaultFontPixelHeight * 1.4

                sourceItem: Rectangle {
                    width:  vehicleLabel._badgeSize
                    height: vehicleLabel._badgeSize
                    radius: vehicleLabel._badgeSize / 2
                    color:  _color
                    border.color: "white"
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text:           "V" + _vehicle.id
                        color:          "white"
                        font.bold:      true
                        font.pixelSize: ScreenTools.smallFontPointSize
                        font.family:    ScreenTools.normalFontFamily
                    }
                }
            }

            Connections {
                target: _vehicle.trajectoryPoints
                function onPointAdded(coordinate) {
                    _path.push(coordinate)
                    _path = _path
                }
                function onUpdateLastPoint(coordinate) {
                    if (_path.length > 0) {
                        _path[_path.length - 1] = coordinate
                        _path = _path
                    }
                }
                function onPointsCleared() {
                    _path = []
                }
            }

            Component.onCompleted: {
                _path = _vehicle.trajectoryPoints.list()
                parent = _root
            }
            Component.onDestruction: {
                trailLine.parent = null
                vehicleLabel.parent = null
            }
        }
    }
}
