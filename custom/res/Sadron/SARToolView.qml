import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtLocation

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlightMap
import "qrc:/custom/Sadron" as Sadron

Rectangle {
    id: _root

    color: qgcPal.windowShadeDark

    property real _margin:          ScreenTools.defaultFontPixelWidth * 0.75
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

    // Guard: true while applyMode() is syncing — prevents markCustomized() calls
    property bool _applyingMode: false

    // ── Connections: sync action bar + sidebar when disaster mode is applied ──
    Connections {
        target: sarModeManager || null
        function onModeApplied(mode) {
            _root._applyingMode = true
            if (sarPanel.item) sarPanel.item._applyingMode = true
            Qt.callLater(function() {
                _root._applyingMode = false
                if (sarPanel.item) sarPanel.item._applyingMode = false
            })
        }
    }

    // ── Top Action Bar ──
    Sadron.SadronGlassPanel {
        id: actionBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: ScreenTools.defaultFontPixelHeight * 2.6
        padding: 0
        radius: 0
        elevated: false
        panelColor: qgcPal.windowShadeDark
        panelOpacity: qgcPal.globalTheme === QGCPalette.Light ? 0.88 : 0.82
        borderWidth: 0
        z: 10

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: _margin
            anchors.rightMargin: _margin
            spacing: _margin * 1.5

            // ── Left: Title ──
            QGCLabel {
                text: qsTr("SAR Workspace")
                color: qgcPal.brandingPurple
                font.bold: true
                font.pointSize: ScreenTools.mediumFontPointSize
            }

            // Vertical separator
            Rectangle { width: 1; height: parent.height * 0.5; color: Qt.rgba(1, 1, 1, 0.15); Layout.alignment: Qt.AlignVCenter }

            // ── Disaster Mode badge (clickable) ──
            Rectangle {
                width:      actionBarModeBadgeRow.width + ScreenTools.defaultFontPixelWidth
                height:     actionBarModeBadgeRow.height + 6
                radius:     height / 2
                color:      sarModeManager ? sarModeManager.modeColor : qgcPal.brandingPurple
                Layout.alignment: Qt.AlignVCenter

                RowLayout {
                    id:                 actionBarModeBadgeRow
                    anchors.centerIn:   parent
                    spacing:            4

                    QGCLabel {
                        text:       sarModeManager ? sarModeManager.modeIcon + " " + sarModeManager.currentModeName : "Standard SAR"
                        color:      "white"
                        font.bold:  true
                        font.pointSize: ScreenTools.smallFontPointSize
                    }

                    QGCLabel {
                        text:       "\u25BC"
                        color:      "white"
                        font.pointSize: ScreenTools.smallFontPointSize * 0.8
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked:    actionBarModePopup.open()
                }
            }

            // Mode popup
            Popup {
                id:         actionBarModePopup
                y:          actionBar.height + _margin
                width:      ScreenTools.defaultFontPixelWidth * 22
                padding:    _margin

                background: Rectangle {
                    color:          Qt.rgba(qgcPal.windowShadeDark.r, qgcPal.windowShadeDark.g, qgcPal.windowShadeDark.b, 0.95)
                    radius:         _margin
                    border.color:   Qt.rgba(1, 1, 1, 0.12)
                    border.width:   1
                }

                ColumnLayout {
                    width:      parent.width
                    spacing:    2

                    Repeater {
                        model: sarModeManager ? sarModeManager.availableModeNames : []

                        Rectangle {
                            Layout.fillWidth:   true
                            height:             actionBarModeOptionLabel.height + ScreenTools.defaultFontPixelHeight * 0.5
                            radius:             _margin * 0.5
                            color:              actionBarModeOptionMa.containsMouse ? Qt.rgba(1, 1, 1, 0.12) :
                                                (index === (sarModeManager ? sarModeManager.currentMode : 0) ? Qt.rgba(1, 1, 1, 0.06) : "transparent")

                            QGCLabel {
                                id:                 actionBarModeOptionLabel
                                anchors.centerIn:   parent
                                text:               modelData
                                color:              index === (sarModeManager ? sarModeManager.currentMode : 0) ? qgcPal.colorOrange : qgcPal.text
                                font.bold:          index === (sarModeManager ? sarModeManager.currentMode : 0)
                                font.pointSize:     ScreenTools.smallFontPointSize
                            }

                            MouseArea {
                                id:             actionBarModeOptionMa
                                anchors.fill:   parent
                                hoverEnabled:   true
                                onClicked: {
                                    if (sarModeManager) sarModeManager.applyMode(index)
                                    actionBarModePopup.close()
                                }
                            }
                        }
                    }
                }
            }

            // Customized indicator + reset
            QGCLabel {
                visible:    sarModeManager && sarModeManager.isCustomized
                text:       "(customized)"
                color:      "#f39c12"
                font.italic: true
                font.pointSize: ScreenTools.smallFontPointSize
                Layout.alignment: Qt.AlignVCenter
            }

            QGCColoredImage {
                visible:    sarModeManager && sarModeManager.isCustomized
                width:      ScreenTools.defaultFontPixelHeight * 0.9
                height:     width
                source:     "/res/TrashDelete.svg"
                color:      "#f39c12"
                Layout.alignment: Qt.AlignVCenter

                MouseArea {
                    anchors.fill: parent
                    onClicked:    { if (sarModeManager) sarModeManager.resetToMode() }
                }
            }

            // Recommended overlay chips
            Repeater {
                model: sarModeManager ? sarModeManager.recommendedOverlays : []

                Rectangle {
                    width:      actionBarOverlayChipLabel.width + ScreenTools.defaultFontPixelWidth
                    height:     actionBarOverlayChipLabel.height + 4
                    radius:     height / 2
                    Layout.alignment: Qt.AlignVCenter
                    color: {
                        var active = false
                        if (modelData === "weather" && sarPanel.item) active = sarPanel.item.showWeatherOverlay
                        else if (modelData === "slope" && sarPanel.item) active = sarPanel.item.showSlopeOverlay
                        else if (modelData === "hydro" && sarPanel.item) active = sarPanel.item.showHydroOverlay
                        return active ? Qt.rgba(1, 1, 1, 0.25) : Qt.rgba(1, 1, 1, 0.08)
                    }
                    border.color: {
                        if (modelData === "weather") return "#00bcd4"
                        if (modelData === "slope") return "#ff9800"
                        if (modelData === "hydro") return "#2196f3"
                        return "#888"
                    }
                    border.width: 1

                    QGCLabel {
                        id: actionBarOverlayChipLabel
                        anchors.centerIn: parent
                        text: {
                            if (modelData === "weather") return "Weather"
                            if (modelData === "slope") return "Slope"
                            if (modelData === "hydro") return "Hydro"
                            return modelData
                        }
                        color:  qgcPal.text
                        font.pointSize: ScreenTools.smallFontPointSize * 0.9
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            if (!sarPanel.item) return
                            if (modelData === "weather") sarPanel.item.showWeatherOverlay = !sarPanel.item.showWeatherOverlay
                            else if (modelData === "slope") sarPanel.item.showSlopeOverlay = !sarPanel.item.showSlopeOverlay
                            else if (modelData === "hydro") sarPanel.item.showHydroOverlay = !sarPanel.item.showHydroOverlay
                        }
                    }
                }
            }

            // Vertical separator
            Rectangle { width: 1; height: parent.height * 0.5; color: Qt.rgba(1, 1, 1, 0.15); Layout.alignment: Qt.AlignVCenter }

            // ── Center: Phase + Stats ──
            Rectangle {
                width:      actionBarPhaseLabel.width + ScreenTools.defaultFontPixelWidth
                height:     actionBarPhaseLabel.height + 4
                radius:     height / 2
                Layout.alignment: Qt.AlignVCenter
                color: {
                    if (!sarMissionManager) return Qt.rgba(1, 1, 1, 0.08)
                    switch (sarMissionManager.phase) {
                    case 3: return Qt.rgba(0.18, 0.80, 0.44, 0.25)
                    case 4: return Qt.rgba(0.95, 0.61, 0.07, 0.25)
                    case 5: return Qt.rgba(0.91, 0.30, 0.24, 0.25)
                    default: return Qt.rgba(1, 1, 1, 0.08)
                    }
                }

                QGCLabel {
                    id: actionBarPhaseLabel
                    anchors.centerIn: parent
                    text: sarMissionManager ? {
                        0: "PLANNING",
                        1: "BRIEFING",
                        2: "DEPLOYING",
                        3: "SEARCHING",
                        4: "INVESTIGATING",
                        5: "RECOVERY",
                        6: "DEBRIEFING"
                    }[sarMissionManager.phase] || "UNKNOWN" : "OFFLINE"
                    color: sarMissionManager && sarMissionManager.phase === 3 ? "#2ecc71" :
                           sarMissionManager && sarMissionManager.phase === 4 ? "#f39c12" :
                           sarMissionManager && sarMissionManager.phase === 5 ? "#e74c3c" : qgcPal.text
                    font.bold: true
                    font.pointSize: ScreenTools.smallFontPointSize
                }
            }

            QGCLabel {
                text: (sarCoverageTracker ? sarCoverageTracker.coveragePercent.toFixed(1) : "0.0") + "%"
                color: "#2ecc71"
                font.pointSize: ScreenTools.smallFontPointSize
            }

            Rectangle { width: 1; height: ScreenTools.defaultFontPixelHeight * 0.8; color: Qt.rgba(1, 1, 1, 0.15); Layout.alignment: Qt.AlignVCenter }

            QGCLabel {
                text: "T:" + (sarTargetManager ? sarTargetManager.totalTargets : "0")
                color: sarTargetManager && sarTargetManager.totalTargets > 0 ? "#f39c12" : qgcPal.text
                font.pointSize: ScreenTools.smallFontPointSize
            }

            QGCLabel {
                text: "Z:" + (sarZoneManager ? sarZoneManager.totalZones : "0")
                color: qgcPal.text
                font.pointSize: ScreenTools.smallFontPointSize
            }

            QGCLabel {
                property int _armed:     sarMissionManager ? sarMissionManager.armedVehicleCount : 0
                property int _connected: sarMissionManager ? sarMissionManager.connectedVehicleCount : 0
                text: _armed + "/" + _connected + " armed"
                color: _connected === 0 ? "#e74c3c" : _armed === 0 ? "#f39c12" : "#2ecc71"
                font.pointSize: ScreenTools.smallFontPointSize
            }

            Item { Layout.fillWidth: true }

            // ── Right: Operation buttons ──

            // Abort progress (replaces buttons when aborting)
            RowLayout {
                visible:    sarMissionManager && sarMissionManager.abortInProgress
                spacing:    _margin * 0.5
                Layout.alignment: Qt.AlignVCenter

                BusyIndicator {
                    running:    true
                    Layout.preferredWidth:  ScreenTools.defaultFontPixelHeight * 1.2
                    Layout.preferredHeight: Layout.preferredWidth
                }

                QGCLabel {
                    text:       sarMissionManager ? sarMissionManager.abortStatusText : ""
                    color:      "#e74c3c"
                    font.bold:  true
                    font.pointSize: ScreenTools.smallFontPointSize
                }
            }

            // Readiness status (when mission can't start)
            QGCLabel {
                visible:    sarMissionManager && !sarMissionManager.missionActive
                            && !sarMissionManager.abortInProgress
                            && sarMissionManager.startBlockedReason !== ""
                text:       sarMissionManager ? sarMissionManager.startBlockedReason : ""
                color:      "#e74c3c"
                font.italic: true
                font.pointSize: ScreenTools.smallFontPointSize * 0.9
                Layout.maximumWidth: ScreenTools.defaultFontPixelWidth * 18
                elide:      Text.ElideRight
                Layout.alignment: Qt.AlignVCenter
            }

            // Start / Resume
            QGCButton {
                text: {
                    if (!sarMissionManager) return qsTr("Start")
                    if (sarMissionManager.missionActive && sarMissionManager.paused) return qsTr("Resume")
                    return qsTr("Start")
                }
                primary:    true
                visible:    !(sarMissionManager && sarMissionManager.abortInProgress)
                font.pointSize: ScreenTools.smallFontPointSize
                enabled: {
                    if (!sarMissionManager) return false
                    if (sarMissionManager.missionActive && sarMissionManager.paused) return true
                    return !sarMissionManager.missionActive && (sarMissionManager ? sarMissionManager.canStartMission : false)
                }
                onClicked: {
                    if (sarMissionManager.missionActive && sarMissionManager.paused) {
                        sarMissionManager.resumeOperation()
                    } else {
                        sarMissionManager.startOperation()
                    }
                }
            }

            // Pause
            QGCButton {
                text:               qsTr("Pause")
                backgroundColor:    "#f39c12"
                textColor:          "white"
                visible:            !(sarMissionManager && sarMissionManager.abortInProgress)
                font.pointSize:     ScreenTools.smallFontPointSize
                enabled:            sarMissionManager && sarMissionManager.missionActive && !sarMissionManager.paused
                onClicked:          sarMissionManager.pauseOperation()
            }

            // Abort
            QGCButton {
                text:               qsTr("Abort")
                backgroundColor:    "#e74c3c"
                textColor:          "white"
                visible:            !(sarMissionManager && sarMissionManager.abortInProgress)
                font.pointSize:     ScreenTools.smallFontPointSize
                enabled:            sarMissionManager
                                    && sarMissionManager.missionActive
                                    && !sarMissionManager.abortInProgress
                onClicked: {
                    QGroundControl.showMessageDialog(
                        _root,
                        qsTr("Abort Operation"),
                        qsTr("This will RTL all drones, disarm them, clear missions, and reset the entire SAR state back to Planning.\n\nAre you sure?"),
                        Dialog.Yes | Dialog.No,
                        function() { sarMissionManager.abortOperation() }
                    )
                }
            }

            // Vertical separator
            Rectangle { width: 1; height: parent.height * 0.5; color: Qt.rgba(1, 1, 1, 0.15); Layout.alignment: Qt.AlignVCenter }

            // Toggle video panel visibility
            Sadron.SadronGlassPanel {
                width:      vidToggleRow.width + ScreenTools.defaultFontPixelWidth * 1.5
                height:     vidToggleRow.height + _margin
                radius:     height / 2
                padding:    0
                panelColor: _videoPanelVisible ? qgcPal.brandingPurple : qgcPal.windowShade
                panelOpacity: _videoPanelVisible ? 0.16 : 0.75
                borderColor: _videoPanelVisible ? qgcPal.brandingPurple : Qt.rgba(1, 1, 1, 0.16)
                borderWidth: 1

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

        // ── SAR Panel (fixed left sidebar) ──
        Loader {
            id: sarPanel
            anchors.top:    parent.top
            anchors.left:   parent.left
            anchors.bottom: parent.bottom
            source:         "qrc:/custom/Sadron/SARPanel.qml"
            active:         true
            onLoaded: {
                item.mapControl = Qt.binding(function() { return sarMap })
            }
        }

        // ── Map Section (center, between sidebar and video) ──
        Item {
            id: mapSection
            anchors.top:    parent.top
            anchors.left:   sarPanel.right
            anchors.leftMargin: _margin * 0.5
            anchors.bottom: parent.bottom

            // Available space = total width minus the fixed sidebar and its margin
            property real _availableWidth: contentArea.width - sarPanel.width - _margin * 0.5

            width:          _videoPanelVisible
                            ? _availableWidth * _splitRatio - _dividerWidth / 2
                            : _availableWidth

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
                property rect centerViewport: Qt.rect(0, 0, width, height)

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

            // ── Environmental Overlays ──

            // Weather overlay (wind vectors + precipitation)
            Loader {
                id:             weatherOverlayLoader
                anchors.fill:   sarMap
                source:         "qrc:/custom/Sadron/SARWeatherOverlay.qml"
                active:         sarPanel.item ? sarPanel.item.showWeatherOverlay : false
                onLoaded: {
                    if (item.hasOwnProperty("mapControl")) {
                        item.mapControl = Qt.binding(function() { return sarMap })
                    }
                }
            }

            // Terrain slope overlay
            Loader {
                id:             slopeOverlayLoader
                anchors.fill:   sarMap
                source:         "qrc:/custom/Sadron/SARTerrainSlopeOverlay.qml"
                active:         sarPanel.item ? sarPanel.item.showSlopeOverlay : false
                onLoaded: {
                    if (item.hasOwnProperty("mapControl")) {
                        item.mapControl = Qt.binding(function() { return sarMap })
                    }
                }
            }

            // Hydrological overlay (OSM water features)
            Loader {
                id:             hydroOverlayLoader
                anchors.fill:   sarMap
                source:         "qrc:/custom/Sadron/SARHydroOverlay.qml"
                active:         sarPanel.item ? sarPanel.item.showHydroOverlay : false
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

            // SAR Toast (bottom-center of map)
            Loader {
                id:             sarToastLoader
                anchors.fill:   sarMap
                source:         "qrc:/custom/Sadron/SARToast.qml"
                active:         true
            }
        }

        // ── Draggable Divider ──
        Rectangle {
            id:             divider
            anchors.top:    parent.top
            anchors.bottom: parent.bottom
            anchors.left:   mapSection.right
            width:          _dividerWidth
            color:          dividerMa.containsMouse || dividerMa.pressed
                            ? qgcPal.brandingPurple : Qt.rgba(1, 1, 1, 0.2)
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
                        // Available space excludes the fixed sidebar
                        var availableWidth = contentArea.width - sarPanel.width - _margin * 0.5
                        var newRatio = _startRatio + delta / availableWidth
                        _splitRatio = Math.max(_minSplitRatio, Math.min(_maxSplitRatio, newRatio))
                    }
                }
            }
        }

        // ── Video Panel (right side) ──
        Loader {
            id:                 videoPanelLoader
            anchors.top:        parent.top
            anchors.left:       divider.right
            anchors.right:      parent.right
            anchors.bottom:     parent.bottom
            visible:            _videoPanelVisible
            active:             _videoPanelVisible
            source:             "qrc:/custom/Sadron/SARVideoPanel.qml"
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
