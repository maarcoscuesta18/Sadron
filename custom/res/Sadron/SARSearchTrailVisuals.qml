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

    // 3E: Maximum trail points before pruning oldest entries
    readonly property int _maxTrailPoints: 500

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

    // Single Instantiator: swath line + trail line + vehicle badge per vehicle
    Instantiator {
        model:  (_root.mapControl) ? QGroundControl.multiVehicleManager.vehicles : null
        active: _root.mapControl !== null && _root.mapControl !== undefined

        delegate: Item {
            property var    _vehicle:    object
            property color  _color:      _colorForVehicle(_vehicle)
            property int    _pointCount: 0

            // Wide semi-transparent swath (camera coverage approximation)
            MapPolyline {
                id:         swathLine
                parent:     _root.mapControl
                visible:    _root.showTrails && _pointCount > 1
                line.width: 14
                line.color: Qt.rgba(_color.r, _color.g, _color.b, 0.25)
                z:          QGroundControl.zOrderTrajectoryLines - 1
            }

            // Thin solid trail (exact flight path)
            MapPolyline {
                id:         trailLine
                parent:     _root.mapControl
                visible:    _root.showTrails && _pointCount > 1
                line.width: 3
                line.color: _color
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

            // Use native MapPolyline methods (matching FlyViewMap.qml pattern)
            Connections {
                target: _vehicle.trajectoryPoints
                function onPointAdded(coordinate) {
                    swathLine.addCoordinate(coordinate)
                    trailLine.addCoordinate(coordinate)
                    _pointCount++

                    // 3E: Prune oldest points to cap trail length
                    if (_pointCount > _maxTrailPoints) {
                        swathLine.removeCoordinate(0)
                        trailLine.removeCoordinate(0)
                        _pointCount--
                    }
                }
                function onUpdateLastPoint(coordinate) {
                    if (_pointCount > 0) {
                        swathLine.replaceCoordinate(swathLine.pathLength() - 1, coordinate)
                        trailLine.replaceCoordinate(trailLine.pathLength() - 1, coordinate)
                    }
                }
                function onPointsCleared() {
                    swathLine.path = []
                    trailLine.path = []
                    _pointCount = 0
                }
            }

            // Load existing trajectory points after a short delay so the map
            // has time to fully initialize its plugin/renderer.  Without this,
            // polylines added during map creation may not render until a
            // view switch forces re-creation.
            function _loadExistingPoints() {
                swathLine.path = []
                trailLine.path = []
                var pts = _vehicle.trajectoryPoints.list()
                // 3E: Only load last _maxTrailPoints to avoid large initial polylines
                var startIdx = Math.max(0, pts.length - _maxTrailPoints)
                for (var i = startIdx; i < pts.length; i++) {
                    swathLine.addCoordinate(pts[i])
                    trailLine.addCoordinate(pts[i])
                }
                _pointCount = pts.length - startIdx
            }

            Timer {
                id:       initTimer
                interval: 100
                repeat:   false
                onTriggered: _loadExistingPoints()
            }

            Component.onCompleted: {
                parent = _root
                initTimer.start()
            }
            Component.onDestruction: {
                swathLine.parent = null
                trailLine.parent = null
                vehicleLabel.parent = null
            }
        }
    }
}
