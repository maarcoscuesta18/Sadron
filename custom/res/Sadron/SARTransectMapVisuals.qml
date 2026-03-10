import QtQuick
import QtLocation
import QtPositioning

import QGroundControl
import QGroundControl.Controls

// Renders per-zone transect flight path polylines + entry/exit markers on the map
Item {
    id: _root

    property var mapControl
    property bool showPaths: sarMissionManager ? sarMissionManager.showFlightPaths : true

    Instantiator {
        model:  (_root.mapControl && sarZoneManager) ? sarZoneManager.zones : null
        active: _root.mapControl !== null && _root.mapControl !== undefined

        delegate: Item {
            property var _zone: object

            // Transect polyline
            MapPolyline {
                parent:     _root.mapControl
                visible:    _root.showPaths && _zone.transectPath.length > 1
                line.width: _zone.selected ? 3 : 2
                line.color: _zone.displayColor
                path:       _zone.transectPath
            }

            // Entry marker ("S" = Start) at first waypoint
            MapQuickItem {
                parent:     _root.mapControl
                visible:    _root.showPaths && _zone.transectPath.length > 1
                coordinate: _zone.transectPath.length > 0 ? _zone.transectPath[0] : QtPositioning.coordinate()
                anchorPoint.x: _entrySize / 2
                anchorPoint.y: _entrySize / 2
                z: QGroundControl.zOrderMapItems - 1

                property real _entrySize: ScreenTools.defaultFontPixelHeight * 1.2

                sourceItem: Rectangle {
                    width:  _entrySize
                    height: _entrySize
                    radius: _entrySize / 2
                    color:  _zone.displayColor

                    property real _entrySize: ScreenTools.defaultFontPixelHeight * 1.2

                    Text {
                        anchors.centerIn: parent
                        text:           "S"
                        color:          "white"
                        font.bold:      true
                        font.pixelSize: ScreenTools.smallFontPointSize
                        font.family:    ScreenTools.normalFontFamily
                    }
                }
            }

            // Exit marker ("E" = End) at last waypoint
            MapQuickItem {
                parent:     _root.mapControl
                visible:    _root.showPaths && _zone.transectPath.length > 1
                coordinate: _zone.transectPath.length > 1 ? _zone.transectPath[_zone.transectPath.length - 1] : QtPositioning.coordinate()
                anchorPoint.x: _exitSize / 2
                anchorPoint.y: _exitSize / 2
                z: QGroundControl.zOrderMapItems - 1

                property real _exitSize: ScreenTools.defaultFontPixelHeight * 1.2

                sourceItem: Rectangle {
                    width:  _exitSize
                    height: _exitSize
                    radius: _exitSize / 2
                    color:  Qt.darker(_zone.displayColor, 1.3)

                    property real _exitSize: ScreenTools.defaultFontPixelHeight * 1.2

                    Text {
                        anchors.centerIn: parent
                        text:           "E"
                        color:          "white"
                        font.bold:      true
                        font.pixelSize: ScreenTools.smallFontPointSize
                        font.family:    ScreenTools.normalFontFamily
                    }
                }
            }

            Component.onCompleted: parent = _root
        }
    }
}
