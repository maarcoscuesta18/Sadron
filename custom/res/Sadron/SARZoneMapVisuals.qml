import QtQuick
import QtLocation
import QtPositioning

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlightMap

// Renders search area polygon + sub-zone polygons on the flight map
// Defers QGCMapPolygonVisuals creation until mapControl is valid
Item {
    id: _root

    property var mapControl

    // Master search area polygon — only created once mapControl is ready
    Loader {
        active: _root.mapControl !== null && _root.mapControl !== undefined && sarZoneManager !== null
        sourceComponent: QGCMapPolygonVisuals {
            parent:             _root.mapControl
            mapControl:         _root.mapControl
            mapPolygon:         sarZoneManager ? sarZoneManager.searchAreaPolygon : null
            borderWidth:        _editing ? 5 : 3
            borderColor:        "#e67e22"
            interiorColor:      "#e67e22"
            interiorOpacity:    _editing ? 0.25 : 0.15
            interactive:        sarZoneManager && sarZoneManager.searchAreaPolygon ? sarZoneManager.searchAreaPolygon.interactive : false

            property bool _editing: sarZoneManager && sarZoneManager.searchAreaPolygon ? sarZoneManager.searchAreaPolygon.interactive : false
        }
    }

    // Sub-zone polygons (non-interactive, colored by status)
    // Uses Repeater + Loader to ensure QGCMapPolygonVisuals is created only
    // when mapControl is valid (same pattern as the master search area Loader above).
    Repeater {
        model: sarZoneManager ? sarZoneManager.zones : null

        delegate: Item {
            id: zoneDelegate
            property var _zone: object
            visible: false

            // Polygon visual — deferred until mapControl is ready
            Loader {
                active: _root.mapControl !== null && _root.mapControl !== undefined && zoneDelegate._zone !== null
                sourceComponent: QGCMapPolygonVisuals {
                    parent:             _root.mapControl
                    mapControl:         _root.mapControl
                    mapPolygon:         zoneDelegate._zone ? zoneDelegate._zone.mapPolygon : null
                    borderWidth:        zoneDelegate._zone && zoneDelegate._zone.selected ? 4 : 3
                    borderColor:        zoneDelegate._zone ? zoneDelegate._zone.displayColor : "transparent"
                    interiorColor:      zoneDelegate._zone ? zoneDelegate._zone.displayColor : "transparent"
                    interiorOpacity:    zoneDelegate._zone && zoneDelegate._zone.selected ? 0.40 : 0.28
                    interactive:        false
                }
            }

            // Zone label at polygon center (clickable for selection)
            Loader {
                active: _root.mapControl !== null && _root.mapControl !== undefined && zoneDelegate._zone !== null
                sourceComponent: MapQuickItem {
                    parent:         _root.mapControl
                    coordinate:     zoneDelegate._zone && zoneDelegate._zone.mapPolygon ? zoneDelegate._zone.mapPolygon.center : QtPositioning.coordinate()
                    anchorPoint.x:  labelRect.width / 2
                    anchorPoint.y:  labelRect.height / 2
                    z:              QGroundControl.zOrderMapItems

                    sourceItem: Rectangle {
                        id:     labelRect
                        width:  labelCol.width + ScreenTools.defaultFontPixelWidth * 1.5
                        height: labelCol.height + ScreenTools.defaultFontPixelHeight * 0.4
                        radius: ScreenTools.defaultFontPixelHeight * 0.25
                        color:  Qt.rgba(0, 0, 0, 0.7)
                        border.color: zoneDelegate._zone && zoneDelegate._zone.selected ? "white" : (zoneDelegate._zone ? zoneDelegate._zone.displayColor : "transparent")
                        border.width: zoneDelegate._zone && zoneDelegate._zone.selected ? 2 : 1

                        Column {
                            id: labelCol
                            anchors.centerIn: parent
                            spacing: 1

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text:               zoneDelegate._zone ? zoneDelegate._zone.name : ""
                                color:              "white"
                                font.bold:          true
                                font.pixelSize:     ScreenTools.smallFontPointSize * 1.1
                                font.family:        ScreenTools.normalFontFamily
                            }

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text:               zoneDelegate._zone && zoneDelegate._zone.assignedVehicle >= 0 ? "V" + zoneDelegate._zone.assignedVehicle : "Unassigned"
                                color:              zoneDelegate._zone && zoneDelegate._zone.assignedVehicle >= 0 ? "#2ecc71" : "#e74c3c"
                                font.pixelSize:     ScreenTools.smallFontPointSize
                                font.family:        ScreenTools.normalFontFamily
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (sarZoneManager && zoneDelegate._zone) {
                                    sarZoneManager.selectZone(zoneDelegate._zone.zoneId)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
