import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import "qrc:/custom/Sadron" as Sadron

// Left-side SAR panel: zone drawing, fleet list, target list, operation actions
Sadron.SadronGlassPanel {
    id: _root

    property var  mapControl
    property bool markTargetMode: false

    // Overlay toggle states (read by FlyViewCustomLayer)
    property bool showSearchTrails:   true
    property bool showWeatherOverlay: false
    property bool showSlopeOverlay:   false
    property bool showHydroOverlay:   false

    // Guard: true while applyMode() is syncing values — prevents markCustomized() calls
    property bool _applyingMode: false

    width:  ScreenTools.defaultFontPixelWidth * 32
    height: parent ? parent.height : 400
    padding: 0
    radius: ScreenTools.defaultFontPixelHeight * 0.5
    panelColor: qgcPal.windowShadeDark
    panelOpacity: qgcPal.globalTheme === QGCPalette.Light ? 0.94 : 0.86
    borderColor: Qt.rgba(1, 1, 1, 0.08)

    property real _margin:          ScreenTools.defaultFontPixelWidth * 0.75
    property real _sectionSpacing:  ScreenTools.defaultFontPixelHeight * 0.65
    property var  _activeVehicle:   QGroundControl.multiVehicleManager.activeVehicle
    property var  _vehicles:        QGroundControl.multiVehicleManager.vehicles
    property color _uiAccentBlue:   qgcPal.brandingPurple
    property color _cardBorderColor: Qt.rgba(1, 1, 1, 0.06)
    property color _cardFillColor:  Qt.rgba(qgcPal.windowShade.r, qgcPal.windowShade.g, qgcPal.windowShade.b, qgcPal.globalTheme === QGCPalette.Light ? 0.96 : 0.90)

    // Readiness convenience properties
    property bool _hasConnectedVehicles: sarMissionManager ? sarMissionManager.hasConnectedVehicles : false

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    Flickable {
        anchors.fill:       parent
        anchors.margins:    _margin
        contentHeight:      mainColumn.height
        clip:               true
        flickableDirection:  Flickable.VerticalFlick

        ColumnLayout {
            id:     mainColumn
            width:  parent.width
            spacing: _sectionSpacing

            // ── Connections: sync sidebar SpinBoxes when disaster mode is applied ──
            Connections {
                target: sarModeManager
                function onModeApplied(mode) {
                    _root._applyingMode = true
                    patternCombo.currentIndex = sarMissionManager ? sarMissionManager.currentPattern : 0
                    altitudeSpin.value        = sarMissionManager ? sarMissionManager.searchAltitude : 30
                    speedSpin.value           = sarMissionManager ? sarMissionManager.searchSpeed : 5
                    spacingSpin.value         = sarMissionManager ? sarMissionManager.trackSpacing : 20
                    strategyCombo.currentIndex = sarModeManager.recommendedPartitionStrategy === 2 ? 1 : 0
                    _root._applyingMode = false
                }
            }

            // ── Zone Drawing Section ──
            Rectangle {
                Layout.fillWidth:   true
                implicitHeight:     zoneDrawCol.height + _margin * 2
                radius:             _margin
                color:              _cardFillColor
                border.width:       1
                border.color:       _cardBorderColor

                ColumnLayout {
                    id: zoneDrawCol
                    anchors.left:       parent.left
                    anchors.right:      parent.right
                    anchors.top:        parent.top
                    anchors.margins:    _margin
                    spacing:            _margin

                    QGCLabel {
                        text:       "Search Area"
                        font.bold:  true
                        color:      _uiAccentBlue
                    }

                    // Search area edit / clear buttons
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: _margin

                        QGCButton {
                            text: {
                                if (!sarZoneManager) return qsTr("New Search Area")
                                var poly = sarZoneManager.searchAreaPolygon
                                if (poly.interactive) return qsTr("Finish Editing")
                                if (sarZoneManager.hasSearchArea) return qsTr("Edit Search Area")
                                return qsTr("New Search Area")
                            }
                            Layout.fillWidth: true
                            onClicked: {
                                if (sarZoneManager) {
                                    var poly = sarZoneManager.searchAreaPolygon
                                    if (poly.interactive) {
                                        // Finish editing — exit interactive mode
                                        if (poly.traceMode) poly.traceMode = false
                                        poly.interactive = false
                                    } else if (sarZoneManager.hasSearchArea) {
                                        // Edit existing search area
                                        poly.interactive = true
                                    } else {
                                        // New search area — clear and enter interactive mode
                                        poly.clear()
                                        poly.interactive = true
                                    }
                                }
                            }
                        }

                        QGCButton {
                            text:       qsTr("Clear")
                            enabled:    sarZoneManager && sarZoneManager.hasSearchArea
                            onClicked: {
                                if (sarZoneManager) {
                                    sarZoneManager.searchAreaPolygon.clear()
                                    sarZoneManager.clearAllZones()
                                }
                            }
                        }
                    }

                    // Area display
                    QGCLabel {
                        visible:    sarZoneManager && sarZoneManager.hasSearchArea
                        text: {
                            if (!sarZoneManager || !sarZoneManager.hasSearchArea) return ""
                            var count = sarZoneManager.searchAreaPolygon.path.length
                            return count + " vertices drawn"
                        }
                        color:      qgcPal.text
                        font.pointSize: ScreenTools.smallFontPointSize
                    }

                    // Partition parameters (visible when search area exists)
                    GridLayout {
                        columns:            2
                        Layout.fillWidth:   true
                        columnSpacing:      _margin * 2
                        rowSpacing:         _margin
                        visible:            sarZoneManager && sarZoneManager.hasSearchArea

                        QGCLabel { text: qsTr("Drones:") }
                        SpinBox {
                            id:                 droneCountSpin
                            from:               1
                            to:                 20
                            value:              sarMissionManager ? sarMissionManager.connectedVehicleCount : 1
                            Layout.fillWidth:   true
                        }

                        QGCLabel { text: qsTr("Strategy:") }
                        ComboBox {
                            id:                 strategyCombo
                            Layout.fillWidth:   true
                            model:              [qsTr("Grid"), qsTr("Strip")]
                        }

                        QGCLabel { text: qsTr("Pattern:") }
                        ComboBox {
                            id:                 patternCombo
                            Layout.fillWidth:   true
                            model:              [qsTr("Parallel Track"), qsTr("Creeping Line"), qsTr("Expanding Square"), qsTr("Sector")]
                            onCurrentIndexChanged: {
                                if (sarMissionManager) {
                                    sarMissionManager.currentPattern = currentIndex
                                    if (!_root._applyingMode && sarModeManager) sarModeManager.markCustomized()
                                    // Auto-regenerate transects when pattern changes so drones follow the new pattern
                                    if (sarZoneManager && sarZoneManager.totalZones > 0) {
                                        sarMissionManager.generateAllTransects()
                                    }
                                }
                            }
                        }

                        QGCLabel { text: qsTr("Altitude (m):") }
                        SpinBox {
                            id:     altitudeSpin
                            from:   10
                            to:     120
                            value:  sarMissionManager ? sarMissionManager.searchAltitude : 30
                            Layout.fillWidth: true
                            onValueModified: {
                                if (sarMissionManager) {
                                    sarMissionManager.searchAltitude = value
                                    if (!_root._applyingMode && sarModeManager) sarModeManager.markCustomized()
                                    if (sarZoneManager && sarZoneManager.totalZones > 0) {
                                        sarMissionManager.generateAllTransects()
                                    }
                                }
                            }
                        }

                        QGCLabel { text: qsTr("Speed (m/s):") }
                        SpinBox {
                            id:     speedSpin
                            from:   1
                            to:     15
                            value:  sarMissionManager ? sarMissionManager.searchSpeed : 5
                            enabled: !(sarMissionManager && sarMissionManager.missionActive)
                            Layout.fillWidth: true
                            onValueModified: {
                                if (sarMissionManager) {
                                    sarMissionManager.searchSpeed = value
                                    if (!_root._applyingMode && sarModeManager) sarModeManager.markCustomized()
                                    if (!sarMissionManager.missionActive && sarZoneManager && sarZoneManager.totalZones > 0) {
                                        sarMissionManager.generateAllTransects()
                                    }
                                }
                            }
                        }

                        QGCLabel {
                            visible: sarMissionManager && sarMissionManager.missionActive
                            text:    qsTr("Pause or finish the mission before changing speed.")
                            color:   "#f39c12"
                            font.italic: true
                            font.pointSize: ScreenTools.smallFontPointSize
                        }

                        QGCLabel { text: qsTr("Spacing (m):") }
                        SpinBox {
                            id:     spacingSpin
                            from:   5
                            to:     100
                            value:  sarMissionManager ? sarMissionManager.trackSpacing : 20
                            Layout.fillWidth: true
                            onValueModified: {
                                if (sarMissionManager) {
                                    sarMissionManager.trackSpacing = value
                                    if (!_root._applyingMode && sarModeManager) sarModeManager.markCustomized()
                                    if (sarZoneManager && sarZoneManager.totalZones > 0) {
                                        sarMissionManager.generateAllTransects()
                                    }
                                }
                            }
                        }
                    }

                    // Partition + Assign button
                    QGCButton {
                        text:               qsTr("Partition & Assign")
                        Layout.fillWidth:   true
                        enabled:            sarZoneManager && sarZoneManager.hasSearchArea && _hasConnectedVehicles
                        onClicked: {
                            if (sarZoneManager) {
                                sarZoneManager.partitionSearchArea(droneCountSpin.value, strategyCombo.currentIndex === 1 ? 2 : 0)
                                if (sarMissionManager) {
                                    // Sync all current UI parameters before generating transects
                                    sarMissionManager.currentPattern  = patternCombo.currentIndex
                                    sarMissionManager.searchAltitude  = altitudeSpin.value
                                    sarMissionManager.searchSpeed     = speedSpin.value
                                    sarMissionManager.trackSpacing    = spacingSpin.value
                                    sarMissionManager.autoAssignZones()
                                    sarMissionManager.generateAllTransects()
                                }
                            }
                        }
                    }

                    // Hint when partition disabled due to no vehicles
                    QGCLabel {
                        visible:    sarZoneManager && sarZoneManager.hasSearchArea && !_hasConnectedVehicles
                        text:       qsTr("Connect drones to partition & assign")
                        color:      "#f39c12"
                        font.italic: true
                        font.pointSize: ScreenTools.smallFontPointSize
                    }

                    // Flight path visibility toggle
                    RowLayout {
                        Layout.fillWidth: true
                        visible:          sarZoneManager && sarZoneManager.totalZones > 0

                        QGCButton {
                            text:               sarMissionManager && sarMissionManager.showFlightPaths ? qsTr("Hide Flight Paths") : qsTr("Show Flight Paths")
                            Layout.fillWidth:   true
                            onClicked: {
                                if (sarMissionManager) {
                                    sarMissionManager.showFlightPaths = !sarMissionManager.showFlightPaths
                                }
                            }
                        }

                        QGCButton {
                            text:               qsTr("Regenerate")
                            enabled:            sarZoneManager && sarZoneManager.totalZones > 0
                            onClicked: {
                                if (sarMissionManager) {
                                    sarMissionManager.generateAllTransects()
                                }
                            }
                        }
                    }

                    QGCButton {
                        Layout.fillWidth: true
                        visible:          QGroundControl.multiVehicleManager.vehicles.count > 0
                        text:             _root.showSearchTrails ? qsTr("Hide Search Trails") : qsTr("Show Search Trails")
                        onClicked:        _root.showSearchTrails = !_root.showSearchTrails
                    }
                }
            }

            // ── Zone List (after partitioning) ──
            Rectangle {
                Layout.fillWidth:   true
                implicitHeight:     zoneListCol.height + _margin * 2
                radius:             _margin
                color:              _cardFillColor
                border.width:       1
                border.color:       _cardBorderColor
                visible:            sarZoneManager && sarZoneManager.totalZones > 0

                ColumnLayout {
                    id: zoneListCol
                    anchors.left:       parent.left
                    anchors.right:      parent.right
                    anchors.top:        parent.top
                    anchors.margins:    _margin
                    spacing:            _margin

                    QGCLabel {
                        text:       qsTr("Zones (%1)").arg(sarZoneManager ? sarZoneManager.totalZones : 0)
                        font.bold:  true
                        color:      _uiAccentBlue
                    }

                    Repeater {
                        model: sarZoneManager ? sarZoneManager.zones : null

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: zoneCardCol.height + _margin * 2
                            radius:         _margin * 0.5
                            color:          object.selected ? Qt.rgba(_uiAccentBlue.r, _uiAccentBlue.g, _uiAccentBlue.b, 0.16) : Qt.rgba(1, 1, 1, 0.04)
                            border.color:   object.selected ? _uiAccentBlue : _cardBorderColor
                            border.width:   1

                            Rectangle {
                                anchors.left:   parent.left
                                anchors.top:    parent.top
                                anchors.bottom: parent.bottom
                                width:          ScreenTools.defaultFontPixelWidth * 0.4
                                radius:         parent.radius
                                color:          object.selected ? _uiAccentBlue : Qt.rgba(object.displayColor.r, object.displayColor.g, object.displayColor.b, 0.55)
                            }

                            ColumnLayout {
                                id: zoneCardCol
                                anchors.left:       parent.left
                                anchors.right:      parent.right
                                anchors.top:        parent.top
                                anchors.margins:    _margin
                                spacing:            2

                                RowLayout {
                                    Layout.fillWidth: true

                                    // Status dot
                                    Rectangle {
                                        width:      ScreenTools.defaultFontPixelHeight * 0.6
                                        height:     width
                                        radius:     width / 2
                                        color:      object.displayColor
                                    }

                                    QGCLabel {
                                        text:               object.name
                                        font.bold:          true
                                        Layout.fillWidth:   true
                                    }

                                    QGCLabel {
                                        text:   object.assignedVehicle >= 0 ? "V" + object.assignedVehicle : "—"
                                        color:  object.assignedVehicle >= 0 ? "#2ecc71" : "#e74c3c"
                                        font.pointSize: ScreenTools.smallFontPointSize
                                    }
                                }

                                // Progress bar
                                Rectangle {
                                    Layout.fillWidth:   true
                                    height:             3
                                    radius:             1.5
                                    color:              Qt.rgba(1, 1, 1, 0.15)

                                    Rectangle {
                                        width:  parent.width * object.progress
                                        height: parent.height
                                        radius: parent.radius
                                        color:  object.displayColor
                                    }
                                }

                                QGCLabel {
                                    text:   (object.progress * 100).toFixed(0) + "% searched"
                                    color:  qgcPal.text
                                    font.pointSize: ScreenTools.smallFontPointSize
                                }
                            }

                            MouseArea {
                                id: zoneCardMouseArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    // Select zone and center map on it
                                    if (sarZoneManager) {
                                        sarZoneManager.selectZone(object.zoneId)
                                    }
                                    if (mapControl && object.mapPolygon) {
                                        mapControl.center = object.mapPolygon.center
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ── Fleet Section ──
            Rectangle {
                Layout.fillWidth:   true
                implicitHeight:     fleetCol.height + _margin * 2
                radius:             _margin
                color:              _cardFillColor
                border.width:       1
                border.color:       _cardBorderColor

                ColumnLayout {
                    id: fleetCol
                    anchors.left:       parent.left
                    anchors.right:      parent.right
                    anchors.top:        parent.top
                    anchors.margins:    _margin
                    spacing:            _margin

                    QGCLabel {
                        text:       qsTr("Fleet (%1)").arg(_vehicles ? _vehicles.count : 0)
                        font.bold:  true
                        color:      _uiAccentBlue
                    }

                    Repeater {
                        model: _vehicles

                        Rectangle {
                            Layout.fillWidth:   true
                            implicitHeight:     vehicleCardCol.height + _margin * 2
                            radius:             _margin * 0.5
                            color:              object === _activeVehicle ? Qt.rgba(0.9, 0.5, 0.13, 0.2) : Qt.rgba(1, 1, 1, 0.05)
                            border.color:       object === _activeVehicle ? "#e67e22" : "transparent"
                            border.width:       object === _activeVehicle ? 1 : 0

                            property var _meshNode: meshNetworkManager ? meshNetworkManager.getNode(object.id) : null
                            property var _assignedZone: {
                                // Depend on zoneAssignmentGeneration so this re-evaluates
                                // whenever zones are assigned or reassigned
                                void(sarZoneManager ? sarZoneManager.zoneAssignmentGeneration : 0)
                                return sarZoneManager ? sarZoneManager.zoneForVehicle(object.id) : null
                            }

                            ColumnLayout {
                                id: vehicleCardCol
                                anchors.left:       parent.left
                                anchors.right:      parent.right
                                anchors.top:        parent.top
                                anchors.margins:    _margin
                                spacing:            2

                                // Header row — click to select vehicle
                                Item {
                                    Layout.fillWidth: true
                                    implicitHeight: headerRow.height

                                    RowLayout {
                                        id: headerRow
                                        anchors.left:   parent.left
                                        anchors.right:  parent.right

                                        // Vehicle ID badge
                                        Rectangle {
                                            width:      idLabel.width + ScreenTools.defaultFontPixelWidth
                                            height:     idLabel.height + 4
                                            radius:     3
                                            color: {
                                                if (_meshNode) {
                                                    switch (_meshNode.status) {
                                                    case 0: return "#2ecc71"    // Good
                                                    case 1: return "#f39c12"    // Warning
                                                    case 2: return "#e74c3c"    // Critical
                                                    default: return "#888"
                                                    }
                                                }
                                                return object.armed ? "#2ecc71" : "#888"
                                            }

                                            QGCLabel {
                                                id: idLabel
                                                anchors.centerIn: parent
                                                text:       "V" + object.id
                                                color:      "white"
                                                font.bold:  true
                                                font.pointSize: ScreenTools.smallFontPointSize
                                            }
                                        }

                                        // Armed state + flight mode
                                        QGCLabel {
                                            text:               object.armed ? object.flightMode : "Disarmed"
                                            color:              object.armed ? "#2ecc71" : qgcPal.text
                                            font.pointSize:     ScreenTools.smallFontPointSize
                                            Layout.fillWidth:   true
                                        }

                                        // Assigned zone
                                        QGCLabel {
                                            text:       _assignedZone ? _assignedZone.name : "—"
                                            color:      _assignedZone ? "#3498db" : "#888"
                                            font.pointSize: ScreenTools.smallFontPointSize
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: QGroundControl.multiVehicleManager.activeVehicle = object
                                    }
                                }

                                // Battery + Signal row
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: _margin * 2

                                    // Battery bar
                                    RowLayout {
                                        spacing: _margin * 0.5

                                        QGCLabel {
                                            text: "Bat"
                                            color: qgcPal.text
                                            font.pointSize: ScreenTools.smallFontPointSize
                                        }

                                        Rectangle {
                                            width:  ScreenTools.defaultFontPixelWidth * 6
                                            height: 4
                                            radius: 2
                                            color:  Qt.rgba(1, 1, 1, 0.15)

                                            Rectangle {
                                                property real battPct: object.batteries && object.batteries.count > 0 ?
                                                                       object.batteries.get(0).percentRemaining.rawValue / 100 : 0
                                                width:  parent.width * Math.max(0, Math.min(1, battPct))
                                                height: parent.height
                                                radius: parent.radius
                                                color:  battPct > 0.5 ? "#2ecc71" : battPct > 0.2 ? "#f39c12" : "#e74c3c"
                                            }
                                        }

                                        QGCLabel {
                                            text: {
                                                if (object.batteries && object.batteries.count > 0) {
                                                    var pct = object.batteries.get(0).percentRemaining.rawValue
                                                    return isNaN(pct) ? "—" : pct.toFixed(0) + "%"
                                                }
                                                return "—"
                                            }
                                            color: qgcPal.text
                                            font.pointSize: ScreenTools.smallFontPointSize
                                        }
                                    }

                                    // Signal strength (from mesh)
                                    RowLayout {
                                        spacing: _margin * 0.5
                                        visible: _meshNode !== null

                                        QGCLabel {
                                            text: "Sig"
                                            color: qgcPal.text
                                            font.pointSize: ScreenTools.smallFontPointSize
                                        }

                                        Rectangle {
                                            width:  ScreenTools.defaultFontPixelWidth * 4
                                            height: 4
                                            radius: 2
                                            color:  Qt.rgba(1, 1, 1, 0.15)

                                            Rectangle {
                                                property real sigPct: _meshNode ? _meshNode.signalStrength / 100 : 0
                                                width:  parent.width * sigPct
                                                height: parent.height
                                                radius: parent.radius
                                                color:  sigPct > 0.6 ? "#2ecc71" : sigPct > 0.3 ? "#f39c12" : "#e74c3c"
                                            }
                                        }
                                    }
                                }

                                // Telemetry row: heading, speed, climb rate, altitude
                                Rectangle {
                                    Layout.fillWidth:   true
                                    implicitHeight:     telemetryRow.height + _margin
                                    radius:             _margin * 0.4
                                    color:              Qt.rgba(0.2, 0.6, 0.9, 0.08)
                                    visible:            object.armed

                                    property real _vHeading:     object.heading ? object.heading.rawValue : 0
                                    property real _vSpeed:       object.groundSpeed ? object.groundSpeed.rawValue : 0
                                    property real _vClimb:       object.climbRate ? object.climbRate.rawValue : 0
                                    property real _vAlt:         object.altitudeRelative ? object.altitudeRelative.rawValue : 0

                                    function _headingDir(deg) {
                                        var dirs = ["N","NE","E","SE","S","SW","W","NW"]
                                        return dirs[Math.round(((deg % 360) + 360) % 360 / 45) % 8]
                                    }

                                    GridLayout {
                                        id: telemetryRow
                                        anchors.left:           parent.left
                                        anchors.right:          parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.margins:        _margin * 0.5
                                        columns:                4
                                        columnSpacing:          _margin
                                        rowSpacing:             1

                                        // Heading
                                        QGCLabel {
                                            text:   "\u27A4 " + parent.parent._vHeading.toFixed(0) + "\u00B0 " + parent.parent._headingDir(parent.parent._vHeading)
                                            color:  "#3498db"
                                            font.pointSize: ScreenTools.smallFontPointSize * 0.9
                                            font.bold: true
                                        }

                                        // Speed
                                        QGCLabel {
                                            text:   "\u2191 " + parent.parent._vSpeed.toFixed(1) + " m/s"
                                            color:  qgcPal.text
                                            font.pointSize: ScreenTools.smallFontPointSize * 0.9
                                        }

                                        // Climb rate
                                        QGCLabel {
                                            text:   "\u2195 " + (parent.parent._vClimb >= 0 ? "+" : "") + parent.parent._vClimb.toFixed(1)
                                            color:  parent.parent._vClimb > 0.5 ? "#2ecc71" : (parent.parent._vClimb < -0.5 ? "#e74c3c" : qgcPal.text)
                                            font.pointSize: ScreenTools.smallFontPointSize * 0.9
                                        }

                                        // Altitude
                                        QGCLabel {
                                            text:   "\u2302 " + parent.parent._vAlt.toFixed(1) + "m"
                                            color:  qgcPal.text
                                            font.pointSize: ScreenTools.smallFontPointSize * 0.9
                                        }
                                    }
                                }

                                // Vehicle control buttons
                                // Button visibility follows FlyView GuidedActionsController logic.
                                // Buttons shown per state:
                                //   Disarmed:                  [Arm] or [Force Arm]
                                //   Armed, on ground:          [Disarm] [Takeoff]
                                //   Armed, flying:             [E-Stop] [Land] [RTL]
                                //   Armed, landing/landed:     [Disarm] [E-Stop] [Land] [RTL]
                                // Disarm appears whenever the vehicle is in land flight mode
                                // or the landing flag is set, even if 'flying' is still true.
                                // This covers the window after touchdown where the FC hasn't
                                // cleared the flying flag yet.
                                // Actions are executed directly on the vehicle object (not via
                                // GuidedActionsController) so they work reliably for any vehicle
                                // in the fleet list regardless of which vehicle is active.
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: _margin * 0.5

                                    property bool _healthCheckSupported: object.healthAndArmingCheckReport ? object.healthAndArmingCheckReport.supported : false
                                    property bool _canArm:    !_healthCheckSupported || (object.healthAndArmingCheckReport ? object.healthAndArmingCheckReport.canArm : true)
                                    property bool _canTakeoff: !_healthCheckSupported || (object.healthAndArmingCheckReport ? object.healthAndArmingCheckReport.canTakeoff : true)
                                    // Detect land-mode or landing flag even while 'flying' is still true
                                    property bool _inLandMode: object.flightMode === object.landFlightMode
                                    property bool _isLanding:  object.landing || _inLandMode

                                    // ── Arm (visible when disarmed)
                                    QGCButton {
                                        text:               qsTr("Arm")
                                        Layout.fillWidth:   true
                                        font.pointSize:     ScreenTools.smallFontPointSize
                                        visible:            !object.armed && parent._canArm
                                        enabled:            !object.armed
                                        onClicked:          object.armed = true
                                    }

                                    // ── Force Arm (visible when disarmed AND health checks fail)
                                    QGCButton {
                                        text:               qsTr("Force Arm")
                                        Layout.fillWidth:   true
                                        font.pointSize:     ScreenTools.smallFontPointSize
                                        visible:            !object.armed && !parent._canArm
                                        enabled:            !object.armed
                                        onClicked:          object.forceArm()
                                    }

                                    // ── Disarm (visible when armed AND: on ground, OR in land mode / landing)
                                    QGCButton {
                                        text:               qsTr("Disarm")
                                        Layout.fillWidth:   true
                                        font.pointSize:     ScreenTools.smallFontPointSize
                                        visible:            object.armed && (!object.flying || parent._isLanding)
                                        enabled:            object.armed
                                        onClicked:          object.armed = false
                                    }

                                    // ── Emergency Stop (visible when armed and flying)
                                    QGCButton {
                                        text:               qsTr("E-Stop")
                                        Layout.fillWidth:   true
                                        font.pointSize:     ScreenTools.smallFontPointSize
                                        visible:            object.armed && object.flying
                                        enabled:            object.armed && object.flying
                                        onClicked:          object.emergencyStop()
                                    }

                                    // ── Takeoff (visible when armed and on the ground, not landing)
                                    QGCButton {
                                        text:               qsTr("Takeoff")
                                        Layout.fillWidth:   true
                                        font.pointSize:     ScreenTools.smallFontPointSize
                                        visible:            object.armed && !object.flying && !parent._isLanding
                                        enabled:            object.armed && !object.flying && object.guidedModeSupported && parent._canTakeoff
                                        onClicked:          object.guidedModeTakeoff(altitudeSpin.value)
                                    }

                                    // ── Land (visible when armed and flying, not already in land mode)
                                    QGCButton {
                                        text:               qsTr("Land")
                                        Layout.fillWidth:   true
                                        font.pointSize:     ScreenTools.smallFontPointSize
                                        visible:            object.armed && object.flying && !parent._inLandMode
                                        enabled:            object.armed && object.flying && object.guidedModeSupported
                                        onClicked:          object.guidedModeLand()
                                    }

                                    // ── RTL (visible when armed and flying)
                                    QGCButton {
                                        text:               qsTr("RTL")
                                        Layout.fillWidth:   true
                                        font.pointSize:     ScreenTools.smallFontPointSize
                                        visible:            object.armed && object.flying
                                        enabled:            object.armed && object.flying && object.guidedModeSupported
                                        onClicked:          object.guidedModeRTL(false)
                                    }
                                }

                                // Per-drone mission control buttons
                                // Allows the operator to start, pause, or resume the mission
                                // on an individual drone without affecting the rest of the fleet.
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: _margin * 0.5
                                    visible: object.armed && object.flying

                                    property bool _inMissionMode: object.flightMode === object.missionFlightMode
                                    property bool _inPauseMode:   object.flightMode === object.pauseFlightMode || object.guidedMode

                                    // ── Mission (visible when NOT already in mission mode)
                                    QGCButton {
                                        text:               qsTr("Mission")
                                        Layout.fillWidth:   true
                                        font.pointSize:     ScreenTools.smallFontPointSize
                                        visible:            !parent._inMissionMode
                                        enabled:            object.armed && object.flying
                                        onClicked:          object.startMission()
                                    }

                                    // ── Resume (visible when in pause / hold / guided mode)
                                    QGCButton {
                                        text:               qsTr("Resume")
                                        Layout.fillWidth:   true
                                        font.pointSize:     ScreenTools.smallFontPointSize
                                        visible:            parent._inPauseMode
                                        enabled:            object.armed && object.flying
                                        onClicked:          object.startMission()
                                    }

                                    // ── Pause (visible when in mission mode)
                                    QGCButton {
                                        text:               qsTr("Pause")
                                        Layout.fillWidth:   true
                                        font.pointSize:     ScreenTools.smallFontPointSize
                                        visible:            parent._inMissionMode
                                        enabled:            object.armed && object.flying && object.pauseVehicleSupported
                                        onClicked:          object.pauseVehicle()
                                    }
                                }
                            }

                            // Card background — no full-card MouseArea so buttons work
                        }
                    }
                }
            }

            // ── Target Section ──
            Rectangle {
                Layout.fillWidth:   true
                implicitHeight:     targetCol.height + _margin * 2
                radius:             _margin
                color:              _cardFillColor
                border.width:       1
                border.color:       _cardBorderColor

                ColumnLayout {
                    id: targetCol
                    anchors.left:       parent.left
                    anchors.right:      parent.right
                    anchors.top:        parent.top
                    anchors.margins:    _margin
                    spacing:            _margin

                    RowLayout {
                        Layout.fillWidth: true

                        QGCLabel {
                            text:       qsTr("Targets (%1)").arg(sarTargetManager ? sarTargetManager.totalTargets : 0)
                            font.bold:  true
                            color:      qgcPal.colorOrange
                            Layout.fillWidth: true
                        }

                        QGCButton {
                            text:       _root.markTargetMode ? qsTr("Stop Marking") : qsTr("Mark on Map")
                            font.pointSize: ScreenTools.smallFontPointSize
                            backgroundColor: _root.markTargetMode
                                             ? Qt.rgba(qgcPal.colorOrange.r, qgcPal.colorOrange.g, qgcPal.colorOrange.b, 0.94)
                                             : Qt.rgba(_uiAccentBlue.r, _uiAccentBlue.g, _uiAccentBlue.b, 0.20)
                            textColor:  _root.markTargetMode ? "white" : qgcPal.text
                            backRadius: height / 2
                            onClicked:  _root.markTargetMode = !_root.markTargetMode
                        }
                    }

                    Repeater {
                        model: sarTargetManager ? sarTargetManager.targets : null

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: targetCardRow.height + _margin
                            radius:         _margin * 0.5
                            color: {
                                switch (object.priority) {
                                case 3: return Qt.rgba(0.91, 0.3, 0.24, 0.2)
                                case 2: return Qt.rgba(0.95, 0.61, 0.07, 0.2)
                                case 1: return Qt.rgba(0.95, 0.77, 0.06, 0.15)
                                default: return Qt.rgba(1, 1, 1, 0.05)
                                }
                            }

                            RowLayout {
                                id: targetCardRow
                                anchors.left:       parent.left
                                anchors.right:      parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.margins:    _margin * 0.5
                                spacing:            _margin

                                QGCLabel {
                                    text:       "T" + object.targetId
                                    font.bold:  true
                                    color: {
                                        switch (object.priority) {
                                        case 3: return "#e74c3c"
                                        case 2: return "#f39c12"
                                        case 1: return "#f1c40f"
                                        default: return "#3498db"
                                        }
                                    }
                                }

                                QGCLabel {
                                    text: {
                                        switch (object.status) {
                                        case 0: return "UNCONFIRMED"
                                        case 1: return "INVESTIGATING"
                                        case 2: return "CONFIRMED"
                                        case 3: return "FALSE ALARM"
                                        case 4: return "RESCUED"
                                        default: return ""
                                        }
                                    }
                                    color: object.status === 2 ? "#2ecc71" : object.status === 3 ? "#888" : "#f39c12"
                                    font.pointSize: ScreenTools.smallFontPointSize
                                    Layout.fillWidth: true
                                }

                                QGCLabel {
                                    text:   object.coordinate.latitude.toFixed(4) + ", " + object.coordinate.longitude.toFixed(4)
                                    color:  qgcPal.text
                                    font.pointSize: ScreenTools.smallFontPointSize
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    if (mapControl) {
                                        mapControl.center = object.coordinate
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ══════════════════════════════════════════════════════════════
            // ── Coordination (Multi-Vehicle) ──
            // ══════════════════════════════════════════════════════════════
            Rectangle {
                Layout.fillWidth:   true
                implicitHeight:     coordCol.height + _margin * 2
                radius:             _margin
                color:              _cardFillColor
                border.width:       1
                border.color:       _cardBorderColor

                ColumnLayout {
                    id: coordCol
                    anchors.left:       parent.left
                    anchors.right:      parent.right
                    anchors.top:        parent.top
                    anchors.margins:    _margin
                    spacing:            _margin

                    // Section header with status badge and collapse toggle
                    RowLayout {
                        Layout.fillWidth: true

                        QGCLabel {
                            text:               qsTr("Coordination")
                            font.bold:          true
                            color:              _uiAccentBlue
                            Layout.fillWidth:   true
                        }

                        // Status badge
                        Rectangle {
                            width:      coordStatusLabel.width + 12
                            height:     coordStatusLabel.height + 6
                            radius:     height / 2
                            color: {
                                if (!vehicleCoordinator || !vehicleCoordinator.enabled) return "#888"
                                var conflicts = vehicleCoordinator.activeConflictCount
                                var commsLoss = vehicleCoordinator.dronesInCommsLoss
                                var overlaps  = vehicleCoordinator.overlapCount
                                if (conflicts > 0 || commsLoss > 0) return "#e74c3c"
                                if (overlaps > 0) return "#f39c12"
                                return "#2ecc71"
                            }

                            QGCLabel {
                                id: coordStatusLabel
                                anchors.centerIn: parent
                                text: {
                                    if (!vehicleCoordinator || !vehicleCoordinator.enabled) return "OFF"
                                    var conflicts = vehicleCoordinator.activeConflictCount
                                    var commsLoss = vehicleCoordinator.dronesInCommsLoss
                                    if (conflicts > 0 || commsLoss > 0) return "ALERT"
                                    if (vehicleCoordinator.overlapCount > 0) return "WARN"
                                    return "OK"
                                }
                                color:          "white"
                                font.pointSize: ScreenTools.smallFontPointSize
                                font.bold:      true
                            }
                        }

                        // Collapse toggle
                        Rectangle {
                            width:  ScreenTools.defaultFontPixelHeight * 1.2
                            height: width
                            radius: width / 2
                            color:  coordCollapseMa.containsMouse ? Qt.rgba(1, 1, 1, 0.2) : "transparent"

                            QGCLabel {
                                anchors.centerIn: parent
                                text:       coordContentCol.visible ? "\u25B2" : "\u25BC"
                                color:      qgcPal.text
                                font.pointSize: ScreenTools.smallFontPointSize
                            }

                            MouseArea {
                                id:             coordCollapseMa
                                anchors.fill:   parent
                                hoverEnabled:   true
                                onClicked:      coordContentCol.visible = !coordContentCol.visible
                            }
                        }
                    }

                    // Collapsible content
                    ColumnLayout {
                        id:                 coordContentCol
                        Layout.fillWidth:   true
                        spacing:            _margin
                        visible:            false

                        // ── Enable toggle ──
                        RowLayout {
                            Layout.fillWidth: true

                            QGCLabel {
                                text:   "Enable Coordination"
                                color:  qgcPal.text
                                font.pointSize: ScreenTools.smallFontPointSize
                                Layout.fillWidth: true
                            }

                            Switch {
                                checked: vehicleCoordinator ? vehicleCoordinator.enabled : false
                                onToggled: { if (vehicleCoordinator) vehicleCoordinator.enabled = checked }
                            }
                        }

                        // ── Separator ──
                        Rectangle { Layout.fillWidth: true; height: 1; color: Qt.rgba(1, 1, 1, 0.15) }

                        // ── Sector Deconfliction ──
                        QGCLabel {
                            text:   "Sector Deconfliction"
                            color:  "#3498db"
                            font.bold: true
                            font.pointSize: ScreenTools.smallFontPointSize
                            visible: vehicleCoordinator && vehicleCoordinator.enabled
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: _margin * 3
                            visible: vehicleCoordinator && vehicleCoordinator.enabled

                            QGCLabel {
                                text:   "Overlaps: " + (vehicleCoordinator ? vehicleCoordinator.overlapCount : 0)
                                color:  vehicleCoordinator && vehicleCoordinator.overlapCount > 0 ? "#f39c12" : "#2ecc71"
                                font.pointSize: ScreenTools.smallFontPointSize
                            }

                            QGCLabel {
                                text:   "Violations: " + (vehicleCoordinator ? vehicleCoordinator.boundaryViolationCount : 0)
                                color:  vehicleCoordinator && vehicleCoordinator.boundaryViolationCount > 0 ? "#e74c3c" : "#2ecc71"
                                font.pointSize: ScreenTools.smallFontPointSize
                            }
                        }

                        // Overlap list
                        ListView {
                            Layout.fillWidth: true
                            height:           Math.min(contentHeight, ScreenTools.defaultFontPixelHeight * 4)
                            clip:             true
                            spacing:          2
                            visible:          vehicleCoordinator && vehicleCoordinator.overlapCount > 0
                            model:            vehicleCoordinator ? vehicleCoordinator.overlaps : null

                            delegate: Rectangle {
                                width:  ListView.view.width
                                height: coordOverlapRow.height + 4
                                radius: 3
                                color:  Qt.rgba(0.95, 0.6, 0.07, 0.15)

                                RowLayout {
                                    id: coordOverlapRow
                                    anchors.left:   parent.left
                                    anchors.right:  parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.margins: 4
                                    spacing: _margin

                                    QGCLabel {
                                        text:   "Z" + object.zoneIdA + " / Z" + object.zoneIdB
                                        color:  "#f39c12"
                                        font.pointSize: ScreenTools.smallFontPointSize
                                        font.bold: true
                                    }

                                    QGCLabel {
                                        text:   object.overlapPercent.toFixed(1) + "% overlap"
                                        color:  "#aaa"
                                        font.pointSize: ScreenTools.smallFontPointSize
                                        Layout.fillWidth: true
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }
                            }
                        }

                        // ── Separator ──
                        Rectangle {
                            Layout.fillWidth: true; height: 1; color: Qt.rgba(1, 1, 1, 0.15)
                            visible: vehicleCoordinator && vehicleCoordinator.enabled
                        }

                        // ── Altitude Separation ──
                        QGCLabel {
                            text:   "Altitude Separation"
                            color:  "#3498db"
                            font.bold: true
                            font.pointSize: ScreenTools.smallFontPointSize
                            visible: vehicleCoordinator && vehicleCoordinator.enabled
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: _margin * 3
                            visible: vehicleCoordinator && vehicleCoordinator.enabled

                            QGCLabel {
                                text:   "Conflicts: " + (vehicleCoordinator ? vehicleCoordinator.activeConflictCount : 0)
                                color:  vehicleCoordinator && vehicleCoordinator.activeConflictCount > 0 ? "#e74c3c" : "#2ecc71"
                                font.pointSize: ScreenTools.smallFontPointSize
                            }

                            QGCLabel {
                                text:   "Bubble: " + (vehicleCoordinator ? vehicleCoordinator.safetyBubbleHorizontalM.toFixed(0) + "m H / " + vehicleCoordinator.safetyBubbleVerticalM.toFixed(0) + "m V" : "N/A")
                                color:  "#aaa"
                                font.pointSize: ScreenTools.smallFontPointSize
                            }
                        }

                        // Conflict list
                        ListView {
                            Layout.fillWidth: true
                            height:           Math.min(contentHeight, ScreenTools.defaultFontPixelHeight * 6)
                            clip:             true
                            spacing:          _margin
                            visible:          vehicleCoordinator && vehicleCoordinator.activeConflictCount > 0
                            model:            vehicleCoordinator ? vehicleCoordinator.conflicts : null

                            delegate: Rectangle {
                                width:  ListView.view.width
                                height: coordConflictCol.height + _margin * 2
                                radius: _margin
                                color:  object.severity === 1 ? Qt.rgba(0.9, 0.3, 0.24, 0.2) : Qt.rgba(0.95, 0.6, 0.07, 0.15)
                                border.color: object.severity === 1 ? "#e74c3c" : "#f39c12"
                                border.width: 1

                                ColumnLayout {
                                    id: coordConflictCol
                                    anchors.left:   parent.left
                                    anchors.right:  parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.margins: _margin
                                    spacing: 2

                                    RowLayout {
                                        spacing: _margin

                                        Rectangle {
                                            width:  coordSevLabel.width + 8
                                            height: coordSevLabel.height + 4
                                            radius: 3
                                            color:  object.severity === 1 ? "#e74c3c" : "#f39c12"

                                            QGCLabel {
                                                id: coordSevLabel
                                                anchors.centerIn: parent
                                                text:   object.severity === 1 ? "CRITICAL" : "WARNING"
                                                color:  "white"
                                                font.pointSize: ScreenTools.smallFontPointSize * 0.85
                                                font.bold: true
                                            }
                                        }

                                        QGCLabel {
                                            text:   "V" + object.vehicleIdA + " / V" + object.vehicleIdB
                                            color:  qgcPal.text
                                            font.pointSize: ScreenTools.smallFontPointSize
                                            font.bold: true
                                        }
                                    }

                                    RowLayout {
                                        spacing: _margin * 2

                                        QGCLabel {
                                            text:   "H: " + object.horizontalDistM.toFixed(1) + "m"
                                            color:  "#aaa"
                                            font.pointSize: ScreenTools.smallFontPointSize
                                        }
                                        QGCLabel {
                                            text:   "V: " + object.verticalDistM.toFixed(1) + "m"
                                            color:  "#aaa"
                                            font.pointSize: ScreenTools.smallFontPointSize
                                        }
                                    }

                                    QGCLabel {
                                        text:   object.resolution
                                        color:  "#2ecc71"
                                        font.pointSize: ScreenTools.smallFontPointSize * 0.9
                                        visible: object.resolution !== ""
                                    }
                                }
                            }
                        }

                        // ── Separator ──
                        Rectangle {
                            Layout.fillWidth: true; height: 1; color: Qt.rgba(1, 1, 1, 0.15)
                            visible: vehicleCoordinator && vehicleCoordinator.enabled
                        }

                        // ── Communications ──
                        QGCLabel {
                            text:   "Communications"
                            color:  "#3498db"
                            font.bold: true
                            font.pointSize: ScreenTools.smallFontPointSize
                            visible: vehicleCoordinator && vehicleCoordinator.enabled
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: _margin * 3
                            visible: vehicleCoordinator && vehicleCoordinator.enabled

                            QGCLabel {
                                text:   "Lost: " + (vehicleCoordinator ? vehicleCoordinator.dronesInCommsLoss : 0)
                                color:  vehicleCoordinator && vehicleCoordinator.dronesInCommsLoss > 0 ? "#e74c3c" : "#2ecc71"
                                font.pointSize: ScreenTools.smallFontPointSize
                            }

                            QGCLabel {
                                text:   "Timeout: " + (vehicleCoordinator ? vehicleCoordinator.commsLossTimeoutSec + "s" : "N/A")
                                color:  "#aaa"
                                font.pointSize: ScreenTools.smallFontPointSize
                            }

                            QGCLabel {
                                text:   "Failsafes: " + (vehicleCoordinator && vehicleCoordinator.allFailsafesVerified ? "OK" : "?")
                                color:  vehicleCoordinator && vehicleCoordinator.allFailsafesVerified ? "#2ecc71" : "#f39c12"
                                font.pointSize: ScreenTools.smallFontPointSize
                            }
                        }

                        // Comms loss events list
                        ListView {
                            Layout.fillWidth: true
                            height:           Math.min(contentHeight, ScreenTools.defaultFontPixelHeight * 6)
                            clip:             true
                            spacing:          _margin
                            visible:          vehicleCoordinator && vehicleCoordinator.dronesInCommsLoss > 0
                            model:            vehicleCoordinator ? vehicleCoordinator.commsLossEvents : null

                            delegate: Rectangle {
                                width:  ListView.view.width
                                height: coordCommsCol.height + _margin * 2
                                radius: _margin
                                color:  Qt.rgba(0.9, 0.3, 0.24, 0.15)
                                border.color: "#e74c3c"
                                border.width: 1

                                ColumnLayout {
                                    id: coordCommsCol
                                    anchors.left:   parent.left
                                    anchors.right:  parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.margins: _margin
                                    spacing: 2

                                    RowLayout {
                                        spacing: _margin

                                        Rectangle {
                                            width:  coordCommsStateLabel.width + 8
                                            height: coordCommsStateLabel.height + 4
                                            radius: 3
                                            color: {
                                                switch (object.state) {
                                                case 0: return "#e74c3c"
                                                case 1: return "#c0392b"
                                                case 2: return "#2ecc71"
                                                case 3: return "#3498db"
                                                default: return "#888"
                                                }
                                            }

                                            QGCLabel {
                                                id: coordCommsStateLabel
                                                anchors.centerIn: parent
                                                text: {
                                                    switch (object.state) {
                                                    case 0: return "LOST"
                                                    case 1: return "RTL"
                                                    case 2: return "RESTORED"
                                                    case 3: return "REASSIGNED"
                                                    default: return "?"
                                                    }
                                                }
                                                color:  "white"
                                                font.pointSize: ScreenTools.smallFontPointSize * 0.85
                                                font.bold: true
                                            }
                                        }

                                        QGCLabel {
                                            text:   "V" + object.vehicleId + " (Zone " + object.zoneId + ")"
                                            color:  qgcPal.text
                                            font.pointSize: ScreenTools.smallFontPointSize
                                            font.bold: true
                                        }

                                        QGCLabel {
                                            text:   object.elapsedSec + "s"
                                            color:  "#aaa"
                                            font.pointSize: ScreenTools.smallFontPointSize
                                            Layout.fillWidth: true
                                            horizontalAlignment: Text.AlignRight
                                        }
                                    }
                                }
                            }
                        }

                        // ── Action buttons ──
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: _margin
                            visible: vehicleCoordinator && vehicleCoordinator.enabled

                            // Verify all failsafes
                            Rectangle {
                                Layout.fillWidth: true
                                height:         coordVerifyLabel.height + ScreenTools.defaultFontPixelHeight * 0.5
                                radius:         ScreenTools.defaultFontPixelHeight * 0.2
                                color:          coordVerifyMa.containsMouse ? "#2980b9" : "#3498db"

                                QGCLabel {
                                    id:             coordVerifyLabel
                                    anchors.centerIn: parent
                                    text:           "Verify Failsafes"
                                    color:          "white"
                                    font.bold:      true
                                    font.pointSize: ScreenTools.smallFontPointSize
                                }
                                MouseArea {
                                    id:             coordVerifyMa
                                    anchors.fill:   parent
                                    hoverEnabled:   true
                                    onClicked:      { if (vehicleCoordinator) vehicleCoordinator.verifyAllFailsafes() }
                                }
                            }

                            // Check overlaps
                            Rectangle {
                                Layout.fillWidth: true
                                height:         coordCheckLabel.height + ScreenTools.defaultFontPixelHeight * 0.5
                                radius:         ScreenTools.defaultFontPixelHeight * 0.2
                                color:          coordCheckMa.containsMouse ? "#7d3c98" : "#9b59b6"

                                QGCLabel {
                                    id:             coordCheckLabel
                                    anchors.centerIn: parent
                                    text:           "Check Overlaps"
                                    color:          "white"
                                    font.bold:      true
                                    font.pointSize: ScreenTools.smallFontPointSize
                                }
                                MouseArea {
                                    id:             coordCheckMa
                                    anchors.fill:   parent
                                    hoverEnabled:   true
                                    onClicked:      { if (vehicleCoordinator) vehicleCoordinator.validateZoneOverlaps() }
                                }
                            }
                        }
                    }
                }
            }

            // ══════════════════════════════════════════════════════════════
            // ── Dynamic Re-Tasking Settings ──
            // ══════════════════════════════════════════════════════════════
            Rectangle {
                Layout.fillWidth:   true
                implicitHeight:     reTaskCol.height + _margin * 2
                radius:             _margin
                color:              _cardFillColor
                border.width:       1
                border.color:       _cardBorderColor

                ColumnLayout {
                    id: reTaskCol
                    anchors.left:       parent.left
                    anchors.right:      parent.right
                    anchors.top:        parent.top
                    anchors.margins:    _margin
                    spacing:            _margin

                    // Section header with collapse toggle
                    RowLayout {
                        Layout.fillWidth: true

                        QGCLabel {
                            text:               qsTr("Dynamic Re-Tasking")
                            font.bold:          true
                            color:              qgcPal.colorOrange
                            Layout.fillWidth:   true
                        }

                        // Active count badge
                        Rectangle {
                            width:      activeCountLabel.width + ScreenTools.defaultFontPixelWidth
                            height:     activeCountLabel.height + 4
                            radius:     height / 2
                            color:      "#e74c3c"
                            visible:    sarReTaskingManager && sarReTaskingManager.activeReTaskCount > 0

                            QGCLabel {
                                id:             activeCountLabel
                                anchors.centerIn: parent
                                text:           sarReTaskingManager ? sarReTaskingManager.activeReTaskCount.toString() : "0"
                                color:          "white"
                                font.bold:      true
                                font.pointSize: ScreenTools.smallFontPointSize
                            }
                        }

                        // Collapse toggle
                        Rectangle {
                            width:  ScreenTools.defaultFontPixelHeight * 1.2
                            height: width
                            radius: width / 2
                            color:  reTaskCollapseMa.containsMouse ? Qt.rgba(1, 1, 1, 0.2) : "transparent"

                            QGCLabel {
                                anchors.centerIn: parent
                                text:       reTaskContentCol.visible ? "\u25B2" : "\u25BC"
                                color:      qgcPal.text
                                font.pointSize: ScreenTools.smallFontPointSize
                            }

                            MouseArea {
                                id:             reTaskCollapseMa
                                anchors.fill:   parent
                                hoverEnabled:   true
                                onClicked:      reTaskContentCol.visible = !reTaskContentCol.visible
                            }
                        }
                    }

                    // Collapsible content
                    ColumnLayout {
                        id:                 reTaskContentCol
                        Layout.fillWidth:   true
                        spacing:            _margin
                        visible:            true

                        // ── Enable / Disable toggle ──
                        RowLayout {
                            Layout.fillWidth: true

                            Rectangle {
                                width:  ScreenTools.defaultFontPixelHeight * 1.4
                                height: ScreenTools.defaultFontPixelHeight * 0.8
                                radius: height / 2
                                color:  sarReTaskingManager && sarReTaskingManager.enabled ? "#e74c3c" : "#555"

                                Rectangle {
                                    width:  parent.height - 4
                                    height: width
                                    radius: width / 2
                                    color:  "white"
                                    x:      sarReTaskingManager && sarReTaskingManager.enabled ? parent.width - width - 2 : 2
                                    anchors.verticalCenter: parent.verticalCenter

                                    Behavior on x { NumberAnimation { duration: 150 } }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        if (sarReTaskingManager) {
                                            sarReTaskingManager.enabled = !sarReTaskingManager.enabled
                                        }
                                    }
                                }
                            }

                            QGCLabel {
                                text:               qsTr("Auto Re-Tasking")
                                color:              sarReTaskingManager && sarReTaskingManager.enabled ? "#e74c3c" : qgcPal.text
                                font.bold:          sarReTaskingManager && sarReTaskingManager.enabled
                                Layout.fillWidth:   true
                            }
                        }

                        // ── Settings grid (visible when enabled) ──
                        GridLayout {
                            columns:            2
                            Layout.fillWidth:   true
                            columnSpacing:      _margin * 2
                            rowSpacing:         _margin
                            visible:            sarReTaskingManager && sarReTaskingManager.enabled

                            QGCLabel { text: qsTr("Strategy:") }
                            ComboBox {
                                id:                 reTaskStrategyCombo
                                Layout.fillWidth:   true
                                model:              [qsTr("Nearest Available"), qsTr("Priority Weighted")]
                                currentIndex:       sarReTaskingManager ? sarReTaskingManager.selectionStrategy : 0
                                onActivated: function(index) {
                                    if (sarReTaskingManager) {
                                        sarReTaskingManager.selectionStrategy = index
                                    }
                                }
                            }

                            QGCLabel { text: qsTr("Min Priority:") }
                            ComboBox {
                                id:                 reTaskPriorityCombo
                                Layout.fillWidth:   true
                                model:              [qsTr("Low"), qsTr("Medium"), qsTr("High"), qsTr("Critical")]
                                currentIndex:       sarReTaskingManager ? sarReTaskingManager.minimumPriority : 1
                                onActivated: function(index) {
                                    if (sarReTaskingManager) {
                                        sarReTaskingManager.minimumPriority = index
                                    }
                                }
                            }

                            QGCLabel { text: qsTr("Timeout (s):") }
                            SpinBox {
                                id:                 reTaskTimeoutSpin
                                from:               3
                                to:                 60
                                value:              sarReTaskingManager ? sarReTaskingManager.autoConfirmTimeoutSec : 10
                                Layout.fillWidth:   true
                                onValueModified: {
                                    if (sarReTaskingManager) {
                                        sarReTaskingManager.autoConfirmTimeoutSec = value
                                    }
                                }
                            }
                        }

                        // ── Active re-tasks list ──
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing:          _margin * 0.5
                            visible:          sarReTaskingManager && sarReTaskingManager.activeReTaskCount > 0

                            QGCLabel {
                                text:       qsTr("Active Investigations")
                                color:      "#e74c3c"
                                font.pointSize: ScreenTools.smallFontPointSize
                                font.bold:  true
                            }

                            Repeater {
                                model: sarReTaskingManager ? sarReTaskingManager.activeReTasks : null

                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: reTaskCardRow.height + _margin
                                    radius:         _margin * 0.5
                                    color:          Qt.rgba(0.91, 0.3, 0.24, 0.15)
                                    border.color:   "#e74c3c"
                                    border.width:   1

                                    RowLayout {
                                        id: reTaskCardRow
                                        anchors.left:           parent.left
                                        anchors.right:          parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.margins:        _margin * 0.5
                                        spacing:                _margin

                                        QGCLabel {
                                            text:       "V" + object.vehicleId
                                            font.bold:  true
                                            color:      "#2ecc71"
                                        }

                                        QGCLabel {
                                            text:       "\u2192"
                                            color:      qgcPal.text
                                        }

                                        QGCLabel {
                                            text:       "T" + object.targetId
                                            font.bold:  true
                                            color:      "#f39c12"
                                            Layout.fillWidth: true
                                        }

                                        QGCButton {
                                            text:       qsTr("Done")
                                            font.pointSize: ScreenTools.smallFontPointSize
                                            onClicked: {
                                                if (sarReTaskingManager) {
                                                    sarReTaskingManager.completeReTask(object.vehicleId)
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            // Complete all button
                            QGCButton {
                                text:               qsTr("Complete All Investigations")
                                Layout.fillWidth:   true
                                visible:            sarReTaskingManager && sarReTaskingManager.activeReTaskCount > 1
                                onClicked: {
                                    if (sarReTaskingManager) {
                                        sarReTaskingManager.completeAllReTasks()
                                    }
                                }
                            }
                        }

                        // ── Status when disabled ──
                        QGCLabel {
                            visible:    sarReTaskingManager && !sarReTaskingManager.enabled
                            text:       qsTr("Enable to auto-reassign drones to new targets")
                            color:      "#888"
                            font.italic: true
                            font.pointSize: ScreenTools.smallFontPointSize
                        }
                    }
                }
            }

            // ══════════════════════════════════════════════════════════════
            // ── Environmental Data Section ──
            // ══════════════════════════════════════════════════════════════
            Rectangle {
                Layout.fillWidth:   true
                implicitHeight:     envCol.height + _margin * 2
                radius:             _margin
                color:              _cardFillColor
                border.width:       1
                border.color:       _cardBorderColor

                ColumnLayout {
                    id: envCol
                    anchors.left:       parent.left
                    anchors.right:      parent.right
                    anchors.top:        parent.top
                    anchors.margins:    _margin
                    spacing:            _margin

                    // Section header
                    RowLayout {
                        Layout.fillWidth: true

                        QGCLabel {
                            text:               qsTr("Environmental Data")
                            font.bold:          true
                            color:              _uiAccentBlue
                            Layout.fillWidth:   true
                        }

                        // Collapse toggle
                        Rectangle {
                            width:  ScreenTools.defaultFontPixelHeight * 1.2
                            height: width
                            radius: width / 2
                            color:  envCollapseMa.containsMouse ? Qt.rgba(1, 1, 1, 0.2) : "transparent"

                            QGCLabel {
                                anchors.centerIn: parent
                                text:       envContentCol.visible ? "\u25B2" : "\u25BC"
                                color:      qgcPal.text
                                font.pointSize: ScreenTools.smallFontPointSize
                            }

                            MouseArea {
                                id:             envCollapseMa
                                anchors.fill:   parent
                                hoverEnabled:   true
                                onClicked:      envContentCol.visible = !envContentCol.visible
                            }
                        }
                    }

                    // Collapsible content
                    ColumnLayout {
                        id:                 envContentCol
                        Layout.fillWidth:   true
                        spacing:            _margin
                        visible:            true

                        // ── Real-time Weather ──
                        Rectangle {
                            Layout.fillWidth:   true
                            implicitHeight:     weatherToggleCol.height + _margin * 2
                            radius:             _margin * 0.5
                            color:              _root.showWeatherOverlay ? Qt.rgba(0, 0.74, 0.83, 0.12) : Qt.rgba(1, 1, 1, 0.03)
                            border.color:       _root.showWeatherOverlay ? "#00bcd4" : "transparent"
                            border.width:       _root.showWeatherOverlay ? 1 : 0

                            ColumnLayout {
                                id: weatherToggleCol
                                anchors.left:       parent.left
                                anchors.right:      parent.right
                                anchors.top:        parent.top
                                anchors.margins:    _margin
                                spacing:            _margin * 0.5

                                RowLayout {
                                    Layout.fillWidth: true

                                    // Toggle circle (styled like the reference image)
                                    Rectangle {
                                        width:  ScreenTools.defaultFontPixelHeight * 1.4
                                        height: ScreenTools.defaultFontPixelHeight * 0.8
                                        radius: height / 2
                                        color:  _root.showWeatherOverlay ? "#00bcd4" : "#555"

                                        Rectangle {
                                            width:  parent.height - 4
                                            height: width
                                            radius: width / 2
                                            color:  "white"
                                            x:      _root.showWeatherOverlay ? parent.width - width - 2 : 2
                                            anchors.verticalCenter: parent.verticalCenter

                                            Behavior on x { NumberAnimation { duration: 150 } }
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: {
                                                _root.showWeatherOverlay = !_root.showWeatherOverlay
                                                if (_root.showWeatherOverlay && environmentalDataProvider) {
                                                    // Auto-fetch weather for map center
                                                    if (mapControl) {
                                                        environmentalDataProvider.fetchWeatherForArea(
                                                            mapControl.center.latitude - 0.05,
                                                            mapControl.center.longitude - 0.05,
                                                            mapControl.center.latitude + 0.05,
                                                            mapControl.center.longitude + 0.05
                                                        )
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    QGCLabel {
                                        text:               qsTr("Real-time Weather")
                                        color:              _root.showWeatherOverlay ? "#00bcd4" : qgcPal.text
                                        font.bold:          _root.showWeatherOverlay
                                        Layout.fillWidth:   true
                                    }
                                }

                                // Weather mini-preview (visible when active and data available)
                                ColumnLayout {
                                    Layout.fillWidth:   true
                                    spacing:            2
                                    visible:            _root.showWeatherOverlay && environmentalDataProvider
                                                        && environmentalDataProvider.weatherAvailable

                                    RowLayout {
                                        spacing: _margin * 2

                                        QGCLabel {
                                            text: environmentalDataProvider
                                                  ? "\u2B06 " + environmentalDataProvider.windSpeed.toFixed(1) + " m/s"
                                                  : ""
                                            color:  "#4caf50"
                                            font.pointSize: ScreenTools.smallFontPointSize
                                            rotation: environmentalDataProvider ? environmentalDataProvider.windDirection : 0
                                        }

                                        QGCLabel {
                                            text: environmentalDataProvider && environmentalDataProvider.precipitation > 0
                                                  ? "\u2614 " + environmentalDataProvider.precipitation.toFixed(1) + " mm/h"
                                                  : ""
                                            color: "#64b5f6"
                                            font.pointSize: ScreenTools.smallFontPointSize
                                        }

                                        QGCLabel {
                                            text: environmentalDataProvider
                                                  ? environmentalDataProvider.temperature.toFixed(0) + "\u00B0C"
                                                  : ""
                                            color: "white"
                                            font.pointSize: ScreenTools.smallFontPointSize
                                        }
                                    }

                                    QGCLabel {
                                        text: environmentalDataProvider ? environmentalDataProvider.weatherDescription : ""
                                        color: "#aaa"
                                        font.pointSize: ScreenTools.smallFontPointSize * 0.9
                                        font.italic: true
                                    }
                                }

                                // Loading indicator
                                QGCLabel {
                                    visible: _root.showWeatherOverlay && environmentalDataProvider
                                             && environmentalDataProvider.weatherLoading
                                    text:    qsTr("Fetching weather data...")
                                    color:   "#00bcd4"
                                    font.pointSize: ScreenTools.smallFontPointSize
                                    font.italic: true
                                }
                            }
                        }

                        // ── Terrain Slope Analysis ──
                        Rectangle {
                            Layout.fillWidth:   true
                            implicitHeight:     slopeToggleCol.height + _margin * 2
                            radius:             _margin * 0.5
                            color:              _root.showSlopeOverlay ? Qt.rgba(1, 0.6, 0, 0.12) : Qt.rgba(1, 1, 1, 0.03)
                            border.color:       _root.showSlopeOverlay ? "#ff9800" : "transparent"
                            border.width:       _root.showSlopeOverlay ? 1 : 0

                            ColumnLayout {
                                id: slopeToggleCol
                                anchors.left:       parent.left
                                anchors.right:      parent.right
                                anchors.top:        parent.top
                                anchors.margins:    _margin
                                spacing:            _margin * 0.5

                                RowLayout {
                                    Layout.fillWidth: true

                                    Rectangle {
                                        width:  ScreenTools.defaultFontPixelHeight * 1.4
                                        height: ScreenTools.defaultFontPixelHeight * 0.8
                                        radius: height / 2
                                        color:  _root.showSlopeOverlay ? "#ff9800" : "#555"

                                        Rectangle {
                                            width:  parent.height - 4
                                            height: width
                                            radius: width / 2
                                            color:  "white"
                                            x:      _root.showSlopeOverlay ? parent.width - width - 2 : 2
                                            anchors.verticalCenter: parent.verticalCenter

                                            Behavior on x { NumberAnimation { duration: 150 } }
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: {
                                                _root.showSlopeOverlay = !_root.showSlopeOverlay
                                                if (_root.showSlopeOverlay && environmentalDataProvider) {
                                                    // Compute slope for search area or map viewport
                                                    if (sarZoneManager && sarZoneManager.hasSearchArea) {
                                                        var bounds = sarZoneManager.searchAreaPolygon.boundingBox()
                                                        if (bounds) {
                                                            environmentalDataProvider.computeSlopeForArea(
                                                                bounds.bottomLeft.latitude,
                                                                bounds.bottomLeft.longitude,
                                                                bounds.topRight.latitude,
                                                                bounds.topRight.longitude
                                                            )
                                                        }
                                                    } else if (mapControl) {
                                                        environmentalDataProvider.computeSlopeForArea(
                                                            mapControl.center.latitude - 0.02,
                                                            mapControl.center.longitude - 0.02,
                                                            mapControl.center.latitude + 0.02,
                                                            mapControl.center.longitude + 0.02
                                                        )
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    QGCLabel {
                                        text:               qsTr("Terrain Slope Analysis")
                                        color:              _root.showSlopeOverlay ? "#ff9800" : qgcPal.text
                                        font.bold:          _root.showSlopeOverlay
                                        Layout.fillWidth:   true
                                    }
                                }

                                // Slope summary (visible when active and data available)
                                RowLayout {
                                    Layout.fillWidth:   true
                                    spacing:            _margin * 2
                                    visible:            _root.showSlopeOverlay && environmentalDataProvider
                                                        && environmentalDataProvider.slopeDataAvailable

                                    QGCLabel {
                                        text: "Avg: " + (environmentalDataProvider
                                              ? environmentalDataProvider.avgSlope.toFixed(1) : "0") + "\u00B0"
                                        color: "#ffeb3b"
                                        font.pointSize: ScreenTools.smallFontPointSize
                                    }

                                    QGCLabel {
                                        text: "Max: " + (environmentalDataProvider
                                              ? environmentalDataProvider.maxSlope.toFixed(1) : "0") + "\u00B0"
                                        color: "#f44336"
                                        font.pointSize: ScreenTools.smallFontPointSize
                                    }
                                }

                                // Loading indicator
                                QGCLabel {
                                    visible: _root.showSlopeOverlay && environmentalDataProvider
                                             && environmentalDataProvider.slopeLoading
                                    text:    qsTr("Computing terrain slopes...")
                                    color:   "#ff9800"
                                    font.pointSize: ScreenTools.smallFontPointSize
                                    font.italic: true
                                }
                            }
                        }

                        // ── Water Features (Hydrological) ──
                        Rectangle {
                            Layout.fillWidth:   true
                            implicitHeight:     hydroToggleCol.height + _margin * 2
                            radius:             _margin * 0.5
                            color:              _root.showHydroOverlay ? Qt.rgba(0.13, 0.59, 0.95, 0.12) : Qt.rgba(1, 1, 1, 0.03)
                            border.color:       _root.showHydroOverlay ? "#2196f3" : "transparent"
                            border.width:       _root.showHydroOverlay ? 1 : 0

                            ColumnLayout {
                                id: hydroToggleCol
                                anchors.left:       parent.left
                                anchors.right:      parent.right
                                anchors.top:        parent.top
                                anchors.margins:    _margin
                                spacing:            _margin * 0.5

                                RowLayout {
                                    Layout.fillWidth: true

                                    // Checkbox style (matching reference image "Tide/Water Levels" checkbox)
                                    Rectangle {
                                        width:  ScreenTools.defaultFontPixelHeight * 0.9
                                        height: width
                                        radius: 3
                                        color:  "transparent"
                                        border.color: _root.showHydroOverlay ? "#2196f3" : "#888"
                                        border.width: 1.5

                                        QGCLabel {
                                            anchors.centerIn: parent
                                            text:       _root.showHydroOverlay ? "\u2713" : ""
                                            color:      "#2196f3"
                                            font.bold:  true
                                            font.pointSize: ScreenTools.smallFontPointSize * 0.8
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: {
                                                _root.showHydroOverlay = !_root.showHydroOverlay
                                                if (_root.showHydroOverlay && environmentalDataProvider) {
                                                    // Fetch water features for search area or viewport
                                                    if (sarZoneManager && sarZoneManager.hasSearchArea) {
                                                        var bounds = sarZoneManager.searchAreaPolygon.boundingBox()
                                                        if (bounds) {
                                                            environmentalDataProvider.fetchWaterFeatures(
                                                                bounds.bottomLeft.latitude,
                                                                bounds.bottomLeft.longitude,
                                                                bounds.topRight.latitude,
                                                                bounds.topRight.longitude
                                                            )
                                                        }
                                                    } else if (mapControl) {
                                                        environmentalDataProvider.fetchWaterFeatures(
                                                            mapControl.center.latitude - 0.05,
                                                            mapControl.center.longitude - 0.05,
                                                            mapControl.center.latitude + 0.05,
                                                            mapControl.center.longitude + 0.05
                                                        )
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    QGCLabel {
                                        text:               qsTr("Water Features")
                                        color:              _root.showHydroOverlay ? "#2196f3" : qgcPal.text
                                        font.bold:          _root.showHydroOverlay
                                        Layout.fillWidth:   true
                                    }
                                }

                                // Hydro summary
                                QGCLabel {
                                    visible: _root.showHydroOverlay && environmentalDataProvider
                                             && environmentalDataProvider.hydroDataAvailable
                                    text: environmentalDataProvider
                                          ? environmentalDataProvider.totalWaterFeatures + " features found"
                                          : ""
                                    color: "#64b5f6"
                                    font.pointSize: ScreenTools.smallFontPointSize
                                }

                                // Loading indicator
                                QGCLabel {
                                    visible: _root.showHydroOverlay && environmentalDataProvider
                                             && environmentalDataProvider.hydroLoading
                                    text:    qsTr("Fetching water features...")
                                    color:   "#2196f3"
                                    font.pointSize: ScreenTools.smallFontPointSize
                                    font.italic: true
                                }
                            }
                        }

                        // ── Refresh All Button ──
                        QGCButton {
                            text:               qsTr("Refresh Environmental Data")
                            Layout.fillWidth:   true
                            enabled:            _root.showWeatherOverlay || _root.showSlopeOverlay || _root.showHydroOverlay
                            onClicked: {
                                if (environmentalDataProvider && mapControl) {
                                    var swLat, swLon, neLat, neLon
                                    if (sarZoneManager && sarZoneManager.hasSearchArea) {
                                        var bounds = sarZoneManager.searchAreaPolygon.boundingBox()
                                        if (bounds) {
                                            swLat = bounds.bottomLeft.latitude
                                            swLon = bounds.bottomLeft.longitude
                                            neLat = bounds.topRight.latitude
                                            neLon = bounds.topRight.longitude
                                        }
                                    }
                                    if (!swLat) {
                                        swLat = mapControl.center.latitude - 0.05
                                        swLon = mapControl.center.longitude - 0.05
                                        neLat = mapControl.center.latitude + 0.05
                                        neLon = mapControl.center.longitude + 0.05
                                    }

                                    if (_root.showWeatherOverlay) {
                                        environmentalDataProvider.fetchWeatherForArea(swLat, swLon, neLat, neLon)
                                    }
                                    if (_root.showSlopeOverlay) {
                                        environmentalDataProvider.computeSlopeForArea(swLat, swLon, neLat, neLon)
                                    }
                                    if (_root.showHydroOverlay) {
                                        environmentalDataProvider.fetchWaterFeatures(swLat, swLon, neLat, neLon)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
