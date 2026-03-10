import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtLocation

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlightMap

Rectangle {
    id: _root

    color: qgcPal.window

    property real _margin:          ScreenTools.defaultFontPixelWidth * 0.75
    property bool _panelVisible:    true
    property bool _videoPanelVisible: true

    // Split ratio: 0.0 = all video, 1.0 = all map. Default ~65% map.
    property real _splitRatio:      0.65
    property real _minSplitRatio:   0.3
    property real _maxSplitRatio:   0.85
    property real _dividerWidth:    ScreenTools.defaultFontPixelWidth * 0.6

    QGCPalette {
        id: qgcPal
        colorGroupEnabled: true
    }

    // ── Top Action Bar ──
    Rectangle {
        id: actionBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: ScreenTools.defaultFontPixelHeight * 2.6
        color: qgcPal.windowShade
        z: 10

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: _margin
            anchors.rightMargin: _margin
            spacing: _margin

            QGCLabel {
                text: qsTr("SAR Workspace")
                color: "#e67e22"
                font.bold: true
                font.pointSize: ScreenTools.mediumFontPointSize
            }

            QGCLabel {
                text: qsTr("Coverage: %1%").arg(sarCoverageTracker ? sarCoverageTracker.coveragePercent.toFixed(1) : "0.0")
                color: "#2ecc71"
                Layout.leftMargin: _margin
            }

            QGCLabel {
                text: qsTr("Targets: %1").arg(sarTargetManager ? sarTargetManager.totalTargets : 0)
                color: qgcPal.text
            }

            Item { Layout.fillWidth: true }

            // Toggle video panel visibility
            Rectangle {
                width:      vidToggleRow.width + ScreenTools.defaultFontPixelWidth * 1.5
                height:     vidToggleRow.height + _margin
                radius:     height / 2
                color:      vidToggleMa.containsMouse ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(1, 1, 1, 0.05)
                border.color: _videoPanelVisible ? "#3498db" : Qt.rgba(1, 1, 1, 0.3)
                border.width: 1

                RowLayout {
                    id:                 vidToggleRow
                    anchors.centerIn:   parent
                    spacing:            _margin * 0.5

                    Rectangle {
                        width:  ScreenTools.defaultFontPixelHeight * 0.5
                        height: width
                        radius: 2
                        color:  _videoPanelVisible ? "#3498db" : "#888"
                        Layout.alignment: Qt.AlignVCenter
                    }

                    QGCLabel {
                        text:       _videoPanelVisible ? qsTr("Feeds On") : qsTr("Feeds Off")
                        color:      _videoPanelVisible ? "#3498db" : qgcPal.text
                        font.pointSize: ScreenTools.smallFontPointSize
                        font.bold:  true
                    }
                }

                MouseArea {
                    id:             vidToggleMa
                    anchors.fill:   parent
                    hoverEnabled:   true
                    cursorShape:    Qt.PointingHandCursor
                    onClicked:      _videoPanelVisible = !_videoPanelVisible
                }

                ToolTip {
                    visible:    vidToggleMa.containsMouse
                    text:       _videoPanelVisible ? qsTr("Hide drone video feeds panel") : qsTr("Show drone video feeds panel")
                    delay:      700
                }
            }

            QGCButton {
                text: qsTr("Fly")
                onClicked: {
                    if (mainWindow.allowViewSwitch()) {
                        mainWindow.showFlyView()
                    }
                }
            }

            QGCButton {
                text: qsTr("Plan")
                onClicked: {
                    if (mainWindow.allowViewSwitch()) {
                        mainWindow.showPlanView()
                    }
                }
            }
        }
    }

    // ── Content area below action bar ──
    Item {
        id: contentArea
        anchors.top:        actionBar.bottom
        anchors.left:       parent.left
        anchors.right:      parent.right
        anchors.bottom:     parent.bottom
        anchors.margins:    _margin
        anchors.topMargin:  _margin * 0.5

        // ── Map Section (left side) ──
        Item {
            id: mapSection
            anchors.top:    parent.top
            anchors.left:   parent.left
            anchors.bottom: parent.bottom
            width:          _videoPanelVisible
                            ? parent.width * _splitRatio - _dividerWidth / 2
                            : parent.width

            Behavior on width {
                NumberAnimation { duration: 200; easing.type: Easing.InOutQuad }
            }

            FlightMap {
                id:                         sarMap
                anchors.fill:               parent
                mapName:                    "SARView"
                allowGCSLocationCenter:     true
                allowVehicleLocationCenter: true
                zoomLevel:                  QGroundControl.flightMapZoom
                center:                     QGroundControl.flightMapPosition

                // Required by QGCMapPolygonVisuals for toolbar positioning and default polygon vertices
                property rect centerViewport: Qt.rect(
                    sarPanel.visible && sarPanel.item ? sarPanel.width + _margin : 0,
                    0,
                    width - (sarPanel.visible && sarPanel.item ? sarPanel.width + _margin : 0),
                    height
                )

                onZoomLevelChanged: QGroundControl.flightMapZoom = sarMap.zoomLevel
                onCenterChanged: QGroundControl.flightMapPosition = sarMap.center

                // Add vehicle markers to the SAR map (matching FlyViewMap)
                MapItemView {
                    model: QGroundControl.multiVehicleManager.vehicles
                    delegate: VehicleMapItem {
                        vehicle:        object
                        coordinate:     object.coordinate
                        map:            sarMap
                        size:           ScreenTools.defaultFontPixelHeight * 3
                        z:              QGroundControl.zOrderVehicles
                    }
                }

                // Mission execution visuals (waypoints, survey patterns, connecting lines)
                Repeater {
                    model: QGroundControl.multiVehicleManager.vehicles

                    PlanMapItems {
                        map:                    sarMap
                        largeMapView:           true
                        planMasterController:   masterController
                        vehicle:                _vehicle

                        property var _vehicle: object

                        PlanMasterController {
                            id: masterController
                            Component.onCompleted: startStaticActiveVehicle(object)
                        }
                    }
                }
            }

            // SAR Zone Map Visuals
            Loader {
                id:             zoneVisualsLoader
                anchors.fill:   sarMap
                source:         "qrc:/custom/Sadron/SARZoneMapVisuals.qml"
                active:         true
                onLoaded: {
                    item.mapControl = Qt.binding(function() { return sarMap })
                }
            }

            // SAR Target Map Visuals
            Loader {
                id:             targetVisualsLoader
                anchors.fill:   sarMap
                source:         "qrc:/custom/Sadron/SARTargetMapVisuals.qml"
                active:         true
                onLoaded: {
                    item.mapControl = Qt.binding(function() { return sarMap })
                }
            }

            // SAR Transect Map Visuals (flight path polylines per zone)
            Loader {
                id:             transectVisualsLoader
                anchors.fill:   sarMap
                source:         "qrc:/custom/Sadron/SARTransectMapVisuals.qml"
                active:         true
                onLoaded: {
                    item.mapControl = Qt.binding(function() { return sarMap })
                }
            }

            // SAR Search Trail Visuals (per-vehicle colored trails)
            Loader {
                id:             searchTrailLoader
                anchors.fill:   sarMap
                source:         "qrc:/custom/Sadron/SARSearchTrailVisuals.qml"
                active:         true
                onLoaded: {
                    item.mapControl = Qt.binding(function() { return sarMap })
                    item.showTrails = Qt.binding(function() { return sarPanel.item ? sarPanel.item.showSearchTrails : true })
                }
            }

            // SAR Coverage Overlay
            Loader {
                id:             coverageLoader
                anchors.fill:   sarMap
                source:         "qrc:/custom/Sadron/SARCoverageOverlay.qml"
                active:         true
                onLoaded: {
                    if (item.hasOwnProperty("mapControl")) {
                        item.mapControl = Qt.binding(function() { return sarMap })
                    }
                }
            }

            // SAR Map Interaction Layer (click-to-place target + right-click menu)
            Loader {
                id:             mapInteractionLoader
                anchors.fill:   sarMap
                z:              1
                source:         "qrc:/custom/Sadron/SARMapInteractionLayer.qml"
                active:         true
                onLoaded: {
                    if (item.hasOwnProperty("mapControl"))
                        item.mapControl = Qt.binding(function() { return sarMap })
                    if (item.hasOwnProperty("markTargetMode"))
                        item.markTargetMode = Qt.binding(function() { return sarPanel.item ? sarPanel.item.markTargetMode : false })
                }
            }

            // SAR Panel (left side of map)
            Loader {
                id: sarPanel
                anchors.top:        sarMap.top
                anchors.left:       sarMap.left
                anchors.bottom:     sarMap.bottom
                anchors.margins:    _margin
                source:             "qrc:/custom/Sadron/SARPanel.qml"
                active:             true
                visible:            _panelVisible
                onLoaded: {
                    item.mapControl = Qt.binding(function() { return sarMap })
                    item.closePanel.connect(function() { _panelVisible = false })
                }
            }

            // SAR Toast (bottom-center of map)
            Loader {
                id:             sarToastLoader
                anchors.fill:   sarMap
                source:         "qrc:/custom/Sadron/SARToast.qml"
                active:         true
            }

            // Show SAR Panel toggle button (visible when panel hidden)
            Rectangle {
                id:                 panelToggle
                anchors.top:        sarMap.top
                anchors.left:       sarMap.left
                anchors.margins:    _margin
                width:              toggleRow.width + ScreenTools.defaultFontPixelWidth * 2
                height:             toggleRow.height + ScreenTools.defaultFontPixelHeight * 0.6
                radius:             ScreenTools.defaultFontPixelHeight * 0.3
                color:              "#e67e22"
                border.color:       "white"
                border.width:       1
                visible:            !_panelVisible

                RowLayout {
                    id:                 toggleRow
                    anchors.centerIn:   parent
                    spacing:            ScreenTools.defaultFontPixelWidth * 0.5

                    QGCColoredImage {
                        width:              ScreenTools.defaultFontPixelHeight * 1.2
                        height:             width
                        source:             "/res/search.svg"
                        color:              "white"
                        Layout.alignment:   Qt.AlignVCenter
                    }

                    QGCLabel {
                        text:           qsTr("Show SAR Panel")
                        color:          "white"
                        font.bold:      true
                        font.pointSize: ScreenTools.defaultFontPointSize
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: _panelVisible = true
                }
            }
        }

        // ── Draggable Divider ──
        Rectangle {
            id:             divider
            anchors.top:    parent.top
            anchors.bottom: parent.bottom
            x:              mapSection.width
            width:          _dividerWidth
            color:          dividerMa.containsMouse || dividerMa.pressed
                            ? "#e67e22" : Qt.rgba(1, 1, 1, 0.2)
            visible:        _videoPanelVisible
            z:              5

            Behavior on color {
                ColorAnimation { duration: 150 }
            }

            // Grip dots
            Column {
                anchors.centerIn:   parent
                spacing:            3

                Repeater {
                    model: 5
                    Rectangle {
                        width:  3
                        height: 3
                        radius: 1.5
                        color:  dividerMa.containsMouse || dividerMa.pressed
                                ? "white" : Qt.rgba(1, 1, 1, 0.4)
                    }
                }
            }

            MouseArea {
                id:             dividerMa
                anchors.fill:   parent
                anchors.margins: -ScreenTools.defaultFontPixelWidth  // easier to grab
                hoverEnabled:   true
                cursorShape:    Qt.SplitHCursor
                drag.target:    null  // we handle dragging manually

                property real _startX: 0
                property real _startRatio: 0

                onPressed: function(mouse) {
                    _startX = mouse.x + divider.x
                    _startRatio = _splitRatio
                }

                onPositionChanged: function(mouse) {
                    if (pressed) {
                        var currentX = mouse.x + divider.x
                        var delta = currentX - _startX
                        var newRatio = _startRatio + delta / contentArea.width
                        _splitRatio = Math.max(_minSplitRatio, Math.min(_maxSplitRatio, newRatio))
                    }
                }
            }
        }

        // ── Video Panel (right side) ──
        Loader {
            id:                 videoPanelLoader
            anchors.top:        parent.top
            anchors.right:      parent.right
            anchors.bottom:     parent.bottom
            width:              _videoPanelVisible
                                ? parent.width * (1 - _splitRatio) - _dividerWidth / 2
                                : 0
            visible:            _videoPanelVisible
            active:             _videoPanelVisible
            source:             "qrc:/custom/Sadron/SARVideoPanel.qml"

            Behavior on width {
                NumberAnimation { duration: 200; easing.type: Easing.InOutQuad }
            }
        }
    }

    // ── Wire toast to SAR signals (unchanged) ──
    Connections {
        target: sarTargetManager || null
        function onTargetAdded(targetId, coordinate) {
            if (sarToastLoader.item) {
                sarToastLoader.item.show(
                    "Target T" + targetId + " Marked",
                    coordinate.latitude.toFixed(6) + ", " + coordinate.longitude.toFixed(6),
                    "#f39c12"
                )
            }
        }
    }

    Connections {
        target: sarZoneManager || null
        function onZonesChanged() {
            if (sarToastLoader.item && sarZoneManager && sarZoneManager.totalZones > 0) {
                sarToastLoader.item.show(
                    sarZoneManager.totalZones + " Zones Created",
                    "Search area partitioned",
                    "#3498db"
                )
            }
        }
        function onZoneAssigned(zoneId, vehicleId) {
            if (sarToastLoader.item) {
                sarToastLoader.item.show(
                    "Zone " + zoneId + " Assigned",
                    "Vehicle V" + vehicleId,
                    "#2ecc71"
                )
            }
        }
    }
}
