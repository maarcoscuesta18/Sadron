import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import Custom.Widgets
import "qrc:/custom/Sadron" as Sadron

// Sadron SAR custom FlyView layer
// Integrates SAR panel, zone map visuals, target map visuals, and toast feedback
Item {
    id: _root

    property var parentToolInsets
    property var totalToolInsets:    _totalToolInsets
    property var mapControl

    // ── SAR Mode state ──
    property bool sarModeActive:    false
    property bool searchAreaEditing: sarZoneManager ? sarZoneManager.searchAreaPolygon.interactive : false

    readonly property string noGPS:             qsTr("NO GPS")
    readonly property real   indicatorValueWidth: ScreenTools.defaultFontPixelWidth * 7

    property var    _activeVehicle:     QGroundControl.multiVehicleManager.activeVehicle
    property real   _indicatorDiameter: ScreenTools.defaultFontPixelWidth * 18
    property real   _indicatorsHeight:  ScreenTools.defaultFontPixelHeight
    property var    _sepColor:          qgcPal.globalTheme === QGCPalette.Light ? Qt.rgba(0,0,0,0.5) : Qt.rgba(1,1,1,0.5)
    property color  _indicatorsColor:   qgcPal.text
    property bool   _isVehicleGps:      _activeVehicle ? _activeVehicle.gps.count.rawValue > 1 && _activeVehicle.gps.hdop.rawValue < 1.4 : false
    property string _altitude:          _activeVehicle ? (isNaN(_activeVehicle.altitudeRelative.value) ? "0.0" : _activeVehicle.altitudeRelative.value.toFixed(1)) + ' ' + _activeVehicle.altitudeRelative.units : "0.0"
    property string _distanceStr:       isNaN(_distance) ? "0" : _distance.toFixed(0) + ' ' + QGroundControl.unitsConversion.appSettingsHorizontalDistanceUnitsString
    property real   _heading:           _activeVehicle   ? _activeVehicle.heading.rawValue : 0
    property real   _distance:          _activeVehicle ? _activeVehicle.distanceToHome.rawValue : 0
    property real   _groundSpeed:       _activeVehicle ? _activeVehicle.groundSpeed.rawValue : 0
    property real   _climbRate:         _activeVehicle ? _activeVehicle.climbRate.rawValue : 0
    property real   _toolsMargin:       ScreenTools.defaultFontPixelWidth * 0.75

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    QGCToolInsets {
        id:                     _totalToolInsets
        leftEdgeTopInset:       parentToolInsets.leftEdgeTopInset
        leftEdgeCenterInset:    sarPanel.visible && sarPanel.item ? sarPanel.x + sarPanel.item.width + _toolsMargin : parentToolInsets.leftEdgeCenterInset
        leftEdgeBottomInset:    parentToolInsets.leftEdgeBottomInset
        rightEdgeTopInset:      parentToolInsets.rightEdgeTopInset
        rightEdgeCenterInset:   zoneEditorLoader.visible && zoneEditorLoader.item ? parent.width - zoneEditorLoader.x + _toolsMargin : parentToolInsets.rightEdgeCenterInset
        rightEdgeBottomInset:   parent.width - compassBackground.x
        topEdgeLeftInset:       parentToolInsets.topEdgeLeftInset
        topEdgeCenterInset:     compassArrowIndicator.y + compassArrowIndicator.height
        topEdgeRightInset:      parentToolInsets.topEdgeRightInset
        bottomEdgeLeftInset:    parentToolInsets.bottomEdgeLeftInset
        bottomEdgeCenterInset:  parentToolInsets.bottomEdgeCenterInset
        bottomEdgeRightInset:   parent.height - attitudeIndicator.y
    }

    // ── SAR Map Interaction Layer (right-click menu + click-to-place target) ──
    Loader {
        id:             mapInteractionLoader
        anchors.fill:   parent
        z:              1   // above visual overlays so MouseArea receives clicks
        source:         "qrc:/custom/Sadron/SARMapInteractionLayer.qml"
        active:         sarModeActive
        onLoaded: {
            if (item.hasOwnProperty("mapControl"))
                item.mapControl = Qt.binding(function() { return _root.mapControl })
            if (item.hasOwnProperty("editingPolygon"))
                item.editingPolygon = Qt.binding(function() { return _root.searchAreaEditing })
            if (item.hasOwnProperty("markTargetMode"))
                item.markTargetMode = Qt.binding(function() { return sarPanel.item ? sarPanel.item.markTargetMode : false })
        }
    }

    // ── SAR Search Trail Visuals (per-vehicle colored trails on map) ──
    Loader {
        id:             searchTrailLoader
        anchors.fill:   parent
        source:         "qrc:/custom/Sadron/SARSearchTrailVisuals.qml"
        active:         true
        onLoaded: {
            item.mapControl = Qt.binding(function() { return _root.mapControl })
            item.showTrails = Qt.binding(function() { return sarPanel.item ? sarPanel.item.showSearchTrails : true })
        }
    }

    // ── SAR Zone Map Visuals (always visible when zones/search area exist) ──
    Loader {
        id:             zoneVisualsLoader
        anchors.fill:   parent
        source:         "qrc:/custom/Sadron/SARZoneMapVisuals.qml"
        active:         true
        onLoaded: {
            if (item.hasOwnProperty("mapControl"))
                item.mapControl = Qt.binding(function() { return _root.mapControl })
        }
    }

    // ── SAR Transect Map Visuals (flight path polylines per zone) ──
    Loader {
        id:             transectVisualsLoader
        anchors.fill:   parent
        source:         "qrc:/custom/Sadron/SARTransectMapVisuals.qml"
        active:         true
        onLoaded: {
            if (item.hasOwnProperty("mapControl"))
                item.mapControl = Qt.binding(function() { return _root.mapControl })
        }
    }

    // ── SAR Target Map Visuals (always visible when targets exist) ──
    Loader {
        id:             targetVisualsLoader
        anchors.fill:   parent
        source:         "qrc:/custom/Sadron/SARTargetMapVisuals.qml"
        active:         true
        onLoaded: {
            if (item.hasOwnProperty("mapControl"))
                item.mapControl = Qt.binding(function() { return _root.mapControl })
        }
    }

    // ── SAR Coverage Overlay ──
    Loader {
        id:             coverageLoader
        anchors.fill:   parent
        source:         "qrc:/custom/Sadron/SARCoverageOverlay.qml"
        active:         true
        onLoaded: {
            if (item.hasOwnProperty("mapControl")) {
                item.mapControl = Qt.binding(function() { return _root.mapControl })
            }
        }
    }

    // ── Environmental Overlays ──

    // Weather overlay (wind vectors + precipitation)
    Loader {
        id:             weatherOverlayLoader
        anchors.fill:   parent
        source:         "qrc:/custom/Sadron/SARWeatherOverlay.qml"
        active:         sarPanel.item ? sarPanel.item.showWeatherOverlay : false
        onLoaded: {
            if (item.hasOwnProperty("mapControl")) {
                item.mapControl = Qt.binding(function() { return _root.mapControl })
            }
        }
    }

    // Terrain slope overlay
    Loader {
        id:             slopeOverlayLoader
        anchors.fill:   parent
        source:         "qrc:/custom/Sadron/SARTerrainSlopeOverlay.qml"
        active:         sarPanel.item ? sarPanel.item.showSlopeOverlay : false
        onLoaded: {
            if (item.hasOwnProperty("mapControl")) {
                item.mapControl = Qt.binding(function() { return _root.mapControl })
            }
        }
    }

    // Hydrological overlay (OSM water features)
    Loader {
        id:             hydroOverlayLoader
        anchors.fill:   parent
        source:         "qrc:/custom/Sadron/SARHydroOverlay.qml"
        active:         sarPanel.item ? sarPanel.item.showHydroOverlay : false
        onLoaded: {
            if (item.hasOwnProperty("mapControl")) {
                item.mapControl = Qt.binding(function() { return _root.mapControl })
            }
        }
    }

    // ── Proximity Alert Overlay (safety bubbles + conflict lines on map) ──
    Loader {
        id:             proximityOverlayLoader
        anchors.fill:   parent
        source:         "qrc:/custom/Sadron/ProximityAlertOverlay.qml"
        active:         true
        onLoaded: {
            if (item.hasOwnProperty("mapControl")) {
                item.mapControl = Qt.binding(function() { return _root.mapControl })
            }
        }
    }

    // ── SAR Panel (left side, visible when SAR mode is active) ──
    Loader {
        id:                     sarPanel
        anchors.left:           parent.left
        anchors.top:            parent.top
        anchors.bottom:         parent.bottom
        anchors.margins:        _toolsMargin
        anchors.leftMargin:     parentToolInsets.leftEdgeCenterInset + _toolsMargin
        visible:                sarModeActive
        active:                 true
        source:                 "qrc:/custom/Sadron/SARPanel.qml"
        onLoaded: {
            if (item.hasOwnProperty("mapControl"))
                item.mapControl = Qt.binding(function() { return _root.mapControl })
            if (item.hasOwnProperty("closePanel"))
                item.closePanel.connect(function() { sarModeActive = false })
        }
    }

    // ── SAR Zone Editor (right side, visible when a zone is selected) ──
    Loader {
        id:                     zoneEditorLoader
        anchors.right:          parent.right
        anchors.top:            parent.top
        anchors.bottom:         parent.bottom
        anchors.margins:        _toolsMargin
        anchors.rightMargin:    parentToolInsets.rightEdgeCenterInset + _toolsMargin
        visible:                sarModeActive && sarZoneManager && sarZoneManager.selectedZone !== null
        active:                 true
        source:                 "qrc:/custom/Sadron/SARZoneEditor.qml"
        onLoaded: {
            if (item.hasOwnProperty("zone"))
                item.zone = Qt.binding(function() { return sarZoneManager ? sarZoneManager.selectedZone : null })
            if (item.hasOwnProperty("mapControl"))
                item.mapControl = Qt.binding(function() { return _root.mapControl })
            if (item.hasOwnProperty("closeEditor"))
                item.closeEditor.connect(function() { if (sarZoneManager) sarZoneManager.clearSelection() })
        }
    }

    // ── Coordination Status Panel (bottom-left, visible when SAR mode is active) ──
    Loader {
        id:                     coordPanelLoader
        anchors.left:           sarPanel.visible ? sarPanel.right : parent.left
        anchors.bottom:         parent.bottom
        anchors.leftMargin:     _toolsMargin
        anchors.bottomMargin:   _toolsMargin + parentToolInsets.bottomEdgeLeftInset
        width:                  ScreenTools.defaultFontPixelWidth * 28
        height:                 ScreenTools.defaultFontPixelHeight * 24
        visible:                sarModeActive && vehicleCoordinator && vehicleCoordinator.enabled
        active:                 true
        source:                 "qrc:/custom/Sadron/CoordinationStatusPanel.qml"
    }

    // ── SAR Toast (bottom-center feedback) ──
    Loader {
        id:             sarToastLoader
        anchors.fill:   parent
        source:         "qrc:/custom/Sadron/SARToast.qml"
        active:         true
    }

    // ── SAR Re-Tasking Popup (top-right, operator override for dynamic re-assignment) ──
    Loader {
        id:             reTaskingPopupLoader
        anchors.fill:   parent
        source:         "qrc:/custom/Sadron/SARReTaskingPopup.qml"
        active:         true
    }

    // ── Wire toast to SAR signals ──
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

    // ── Wire toast to Re-Tasking signals ──
    Connections {
        target: sarReTaskingManager || null
        function onReTaskConfirmed(vehicleId, targetId) {
            if (sarToastLoader.item) {
                sarToastLoader.item.show(
                    "Re-Task Confirmed",
                    "V" + vehicleId + " \u2192 T" + targetId,
                    "#e74c3c"
                )
            }
        }
        function onReTaskCancelled(targetId) {
            if (sarToastLoader.item) {
                sarToastLoader.item.show(
                    "Re-Task Cancelled",
                    "Target T" + targetId + " — operator override",
                    "#f39c12"
                )
            }
        }
        function onReTaskCompleted(vehicleId, originalZoneId) {
            if (sarToastLoader.item) {
                sarToastLoader.item.show(
                    "Investigation Complete",
                    "V" + vehicleId + " resuming Zone " + originalZoneId,
                    "#2ecc71"
                )
            }
        }
        function onConfirmationPassProposed(vehicleId, targetId) {
            if (sarToastLoader.item) {
                sarToastLoader.item.show(
                    "Confirmation Pass Proposed",
                    "V" + vehicleId + " \u2192 T" + targetId + " (verify)",
                    "#9b59b6"
                )
            }
        }
    }

    // ── Wire toast to VehicleCoordinator signals ──
    Connections {
        target: vehicleCoordinator || null
        function onProximityAlert(vehicleIdA, vehicleIdB, distanceM) {
            if (sarToastLoader.item) {
                sarToastLoader.item.show(
                    "Proximity Alert",
                    "V" + vehicleIdA + " / V" + vehicleIdB + " — " + distanceM.toFixed(0) + "m",
                    "#e74c3c"
                )
            }
        }
        function onAltitudeAdjusted(vehicleId, newAltitudeM) {
            if (sarToastLoader.item) {
                sarToastLoader.item.show(
                    "Altitude Adjusted",
                    "V" + vehicleId + " \u2192 " + newAltitudeM.toFixed(1) + "m",
                    "#3498db"
                )
            }
        }
        function onCommsLossDetected(vehicleId) {
            if (sarToastLoader.item) {
                sarToastLoader.item.show(
                    "COMMS LOST",
                    "Vehicle V" + vehicleId + " — monitoring...",
                    "#e74c3c"
                )
            }
        }
        function onCommsRestored(vehicleId) {
            if (sarToastLoader.item) {
                sarToastLoader.item.show(
                    "Comms Restored",
                    "Vehicle V" + vehicleId + " back online",
                    "#2ecc71"
                )
            }
        }
        function onCommsLossRTL(vehicleId) {
            if (sarToastLoader.item) {
                sarToastLoader.item.show(
                    "RTL TRIGGERED",
                    "V" + vehicleId + " — failsafe timeout",
                    "#c0392b"
                )
            }
        }
        function onZoneReassigned(zoneId, oldVehicleId, newVehicleId) {
            if (sarToastLoader.item) {
                sarToastLoader.item.show(
                    "Zone Reassigned",
                    "Zone " + zoneId + ": V" + oldVehicleId + " \u2192 V" + newVehicleId,
                    "#9b59b6"
                )
            }
        }
        function onZoneOverlapDetected(zoneIdA, zoneIdB) {
            if (sarToastLoader.item) {
                sarToastLoader.item.show(
                    "Zone Overlap Detected",
                    "Zone " + zoneIdA + " / Zone " + zoneIdB,
                    "#f39c12"
                )
            }
        }
        function onBoundaryViolation(vehicleId, violatedZoneId) {
            if (sarToastLoader.item) {
                sarToastLoader.item.show(
                    "Boundary Violation",
                    "V" + vehicleId + " outside Zone " + violatedZoneId,
                    "#f39c12"
                )
            }
        }
        function onFailsafeVerified(vehicleId, correct) {
            if (sarToastLoader.item) {
                sarToastLoader.item.show(
                    correct ? "Failsafe OK" : "Failsafe Warning",
                    "V" + vehicleId + (correct ? " — RTL configured" : " — CHECK CONFIG"),
                    correct ? "#2ecc71" : "#e74c3c"
                )
            }
        }
        function onHandoffBlocked(vehicleId, reason) {
            if (sarToastLoader.item) {
                sarToastLoader.item.show(
                    "Handoff Blocked",
                    "V" + vehicleId + " — " + reason,
                    "#e74c3c"
                )
            }
        }
    }

    // ── Wire toast to abort completion ──
    Connections {
        target: sarMissionManager || null
        function onAbortCompleted() {
            if (sarToastLoader.item) {
                sarToastLoader.item.show(
                    "Operation Aborted",
                    "All vehicles landed. SAR state reset.",
                    "#e74c3c"
                )
            }
        }
    }

    // ── Show consolidated "remove missions?" dialog after recovery ──
    Connections {
        target: sarMissionManager || null
        function onRecoveryMissionClearPrompt() {
            QGroundControl.showMessageDialog(
                _root,
                qsTr("SAR Operation Complete"),
                qsTr("All drones have returned and landed.\n\nRemove missions from all vehicles?"),
                Dialog.Yes | Dialog.No,
                function() { sarMissionManager.clearAllVehicleMissions() }
            )
        }
    }

    // ── SAR Mode toggle button (prominent, visible when panel is hidden) ──
    Sadron.SadronGlassPanel {
        id:                     sarToggleBtn
        anchors.top:            parent.top
        anchors.left:           parent.left
        anchors.leftMargin:     parentToolInsets.leftEdgeCenterInset + _toolsMargin
        anchors.topMargin:      _toolsMargin
        width:                  sarToggleRow.width + ScreenTools.defaultFontPixelWidth * 2
        height:                 sarToggleRow.height + ScreenTools.defaultFontPixelHeight * 0.6
        padding:                ScreenTools.defaultFontPixelWidth
        radius:                 height / 2
        panelColor:             sarModeActive ? qgcPal.colorOrange : qgcPal.brandingPurple
        panelOpacity:           sarModeActive ? 0.86 : 0.18
        borderColor:            sarModeActive ? Qt.rgba(0.90, 0.49, 0.13, 0.9) : Qt.rgba(0.23, 0.51, 0.96, 0.8)
        borderWidth:            1
        scale:                  sarModeActive ? 1.02 : 1.0

        RowLayout {
            id:                 sarToggleRow
            anchors.centerIn:   parent
            spacing:            ScreenTools.defaultFontPixelWidth * 0.5

            QGCColoredImage {
                width:                  ScreenTools.defaultFontPixelHeight * 1.2
                height:                 width
                source:                 "/res/search.svg"
                color:                  "white"
                Layout.alignment:       Qt.AlignVCenter
            }

            QGCLabel {
                text:               sarModeActive ? qsTr("SAR Active") : qsTr("SAR Panel")
                color:              sarModeActive ? "white" : qgcPal.text
                font.bold:          true
                font.pointSize:     ScreenTools.defaultFontPointSize
            }
        }

        SequentialAnimation on scale {
            running: sarModeActive
            loops: Animation.Infinite
            NumberAnimation { from: 1.0; to: 1.03; duration: 900; easing.type: Easing.InOutQuad }
            NumberAnimation { from: 1.03; to: 1.0; duration: 900; easing.type: Easing.InOutQuad }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: sarModeActive = !sarModeActive
        }
    }

    // ── Telemetry Info Bar (speed, altitude, climb rate, distance) ──
    Sadron.SadronGlassPanel {
        id:                         telemetryBar
        height:                     telemetryBarRow.height + _toolsMargin
        width:                      telemetryBarRow.width + _toolsMargin * 4
        anchors.bottom:             compassBar.top
        anchors.bottomMargin:       _toolsMargin * 0.5
        anchors.horizontalCenter:   parent.horizontalCenter
        padding:                    ScreenTools.defaultFontPixelWidth
        radius:                     ScreenTools.defaultFontPixelHeight * 0.5
        panelColor:                 qgcPal.windowShadeDark
        panelOpacity:               qgcPal.globalTheme === QGCPalette.Light ? 0.9 : 0.84
        borderColor:                Qt.rgba(1, 1, 1, 0.08)

        Row {
            id:                 telemetryBarRow
            anchors.centerIn:   parent
            spacing:            _toolsMargin * 3

            // Speed
            Row {
                spacing: _toolsMargin * 0.5
                anchors.verticalCenter: parent.verticalCenter

                QGCLabel {
                    text:       "\u2191"
                    color:      "#3498db"
                    font.pointSize: ScreenTools.smallFontPointSize
                    anchors.verticalCenter: parent.verticalCenter
                }
                QGCLabel {
                    text:       _groundSpeed.toFixed(1) + " m/s"
                    color:      "white"
                    font.pointSize: ScreenTools.smallFontPointSize
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // Separator
            Rectangle {
                width: 1; height: parent.height * 0.6
                color: Qt.rgba(1, 1, 1, 0.2)
                anchors.verticalCenter: parent.verticalCenter
            }

            // Climb rate
            Row {
                spacing: _toolsMargin * 0.5
                anchors.verticalCenter: parent.verticalCenter

                QGCLabel {
                    text:       "\u2195"
                    color:      _climbRate > 0.5 ? "#2ecc71" : (_climbRate < -0.5 ? "#e74c3c" : "#aaa")
                    font.pointSize: ScreenTools.smallFontPointSize
                    anchors.verticalCenter: parent.verticalCenter
                }
                QGCLabel {
                    text:       (_climbRate >= 0 ? "+" : "") + _climbRate.toFixed(1) + " m/s"
                    color:      _climbRate > 0.5 ? "#2ecc71" : (_climbRate < -0.5 ? "#e74c3c" : "white")
                    font.pointSize: ScreenTools.smallFontPointSize
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // Separator
            Rectangle {
                width: 1; height: parent.height * 0.6
                color: Qt.rgba(1, 1, 1, 0.2)
                anchors.verticalCenter: parent.verticalCenter
            }

            // Altitude
            Row {
                spacing: _toolsMargin * 0.5
                anchors.verticalCenter: parent.verticalCenter

                QGCLabel {
                    text:       "\u2302"
                    color:      "#e67e22"
                    font.pointSize: ScreenTools.smallFontPointSize
                    anchors.verticalCenter: parent.verticalCenter
                }
                QGCLabel {
                    text:       _altitude
                    color:      "white"
                    font.pointSize: ScreenTools.smallFontPointSize
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // Separator
            Rectangle {
                width: 1; height: parent.height * 0.6
                color: Qt.rgba(1, 1, 1, 0.2)
                anchors.verticalCenter: parent.verticalCenter
            }

            // Distance to home
            Row {
                spacing: _toolsMargin * 0.5
                anchors.verticalCenter: parent.verticalCenter

                QGCLabel {
                    text:       "\u27A4"
                    color:      "#aaa"
                    font.pointSize: ScreenTools.smallFontPointSize
                    anchors.verticalCenter: parent.verticalCenter
                }
                QGCLabel {
                    text:       _distanceStr
                    color:      "white"
                    font.pointSize: ScreenTools.smallFontPointSize
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }

    // ── Heading Indicator (from original custom layer) ──
    Rectangle {
        id:                         compassBar
        height:                     ScreenTools.defaultFontPixelHeight * 1.5
        width:                      ScreenTools.defaultFontPixelWidth  * 50
        anchors.bottom:             parent.bottom
        anchors.bottomMargin:       _toolsMargin
        color:                      "#DEDEDE"
        radius:                     2
        clip:                       true
        anchors.horizontalCenter:   parent.horizontalCenter
        Repeater {
            model: 720
            QGCLabel {
                function _normalize(degrees) {
                    var a = degrees % 360
                    if (a < 0) a += 360
                    return a
                }
                property int _startAngle: modelData + 180 + _heading
                property int _angle: _normalize(_startAngle)
                anchors.verticalCenter: parent.verticalCenter
                x:              visible ? ((modelData * (compassBar.width / 360)) - (width * 0.5)) : 0
                visible:        _angle % 45 == 0
                color:          "#75505565"
                font.pointSize: ScreenTools.smallFontPointSize
                text: {
                    switch(_angle) {
                    case 0:     return "N"
                    case 45:    return "NE"
                    case 90:    return "E"
                    case 135:   return "SE"
                    case 180:   return "S"
                    case 225:   return "SW"
                    case 270:   return "W"
                    case 315:   return "NW"
                    }
                    return ""
                }
            }
        }
    }
    Rectangle {
        id:                         headingIndicator
        height:                     ScreenTools.defaultFontPixelHeight
        width:                      ScreenTools.defaultFontPixelWidth * 4
        color:                      qgcPal.windowShadeDark
        anchors.top:                compassBar.top
        anchors.topMargin:          -headingIndicator.height / 2
        anchors.horizontalCenter:   parent.horizontalCenter
        QGCLabel {
            text:                   _heading
            color:                  qgcPal.text
            font.pointSize:         ScreenTools.smallFontPointSize
            anchors.centerIn:       parent
        }
    }
    Image {
        id:                         compassArrowIndicator
        height:                     _indicatorsHeight
        width:                      height
        source:                     "/custom/img/compass_pointer.svg"
        fillMode:                   Image.PreserveAspectFit
        sourceSize.height:          height
        anchors.top:                compassBar.bottom
        anchors.topMargin:          -height / 2
        anchors.horizontalCenter:   parent.horizontalCenter
    }

    Sadron.SadronGlassPanel {
        id:                     compassBackground
        anchors.bottom:         attitudeIndicator.bottom
        anchors.right:          attitudeIndicator.left
        anchors.rightMargin:    -attitudeIndicator.width / 2
        width:                  -anchors.rightMargin + compassBezel.width + (_toolsMargin * 2)
        height:                 attitudeIndicator.height * 0.75
        padding:                0
        radius:                 ScreenTools.defaultFontPixelHeight * 0.55
        panelColor:             qgcPal.windowShadeDark
        panelOpacity:           qgcPal.globalTheme === QGCPalette.Light ? 0.9 : 0.84
        borderColor:            Qt.rgba(1, 1, 1, 0.08)

        Rectangle {
            id:                     compassBezel
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin:     _toolsMargin
            anchors.left:           parent.left
            width:                  height
            height:                 parent.height - (northLabelBackground.height / 2) - (headingLabelBackground.height / 2)
            radius:                 height / 2
            border.color:           qgcPal.text
            border.width:           1
            color:                  Qt.rgba(0,0,0,0)
        }

        Rectangle {
            id:                         northLabelBackground
            anchors.top:                compassBezel.top
            anchors.topMargin:          -height / 2
            anchors.horizontalCenter:   compassBezel.horizontalCenter
            width:                      northLabel.contentWidth * 1.5
            height:                     northLabel.contentHeight * 1.5
            radius:                     ScreenTools.defaultFontPixelWidth  * 0.25
            color:                      qgcPal.windowShade
            QGCLabel {
                id: northLabel; anchors.centerIn: parent; text: "N"; color: qgcPal.text; font.pointSize: ScreenTools.smallFontPointSize
            }
        }

        Image {
            id:                 headingNeedle
            anchors.centerIn:   compassBezel
            height:             compassBezel.height * 0.75
            width:              height
            source:             "/custom/img/compass_needle.svg"
            fillMode:           Image.PreserveAspectFit
            sourceSize.height:  height
            transform: [ Rotation { origin.x: headingNeedle.width / 2; origin.y: headingNeedle.height / 2; angle: _heading } ]
        }

        Rectangle {
            id:                         headingLabelBackground
            anchors.top:                compassBezel.bottom
            anchors.topMargin:          -height / 2
            anchors.horizontalCenter:   compassBezel.horizontalCenter
            width:                      headingLabel.contentWidth * 1.5
            height:                     headingLabel.contentHeight * 1.5
            radius:                     ScreenTools.defaultFontPixelWidth  * 0.25
            color:                      qgcPal.windowShade
            QGCLabel {
                id: headingLabel; anchors.centerIn: parent; text: _heading; color: qgcPal.text; font.pointSize: ScreenTools.smallFontPointSize
            }
        }
    }

    Sadron.SadronGlassPanel {
        id:                     attitudeIndicator
        anchors.bottomMargin:   _toolsMargin + parentToolInsets.bottomEdgeRightInset
        anchors.rightMargin:    _toolsMargin
        anchors.bottom:         parent.bottom
        anchors.right:          parent.right
        height:                 ScreenTools.defaultFontPixelHeight * 6
        width:                  height
        padding:                ScreenTools.defaultFontPixelHeight * 0.15
        radius:                 height * 0.5
        panelColor:             qgcPal.windowShadeDark
        panelOpacity:           qgcPal.globalTheme === QGCPalette.Light ? 0.9 : 0.84
        borderColor:            Qt.rgba(1, 1, 1, 0.08)

        CustomAttitudeWidget {
            size:               parent.height * 0.95
            vehicle:            _activeVehicle
            showHeading:        false
            anchors.centerIn:   parent
        }
    }
}
