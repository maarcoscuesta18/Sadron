import QtQuick
import QtQuick.Controls
import QtLocation
import QtPositioning

import QGroundControl
import QGroundControl.Controls

// ============================================================================
// ProximityAlertOverlay — Map overlay showing safety bubbles around drones
// and conflict lines between drone pairs that are too close.
//
// Visualizes:
//   - Safety bubble circles (horizontal radius) around each vehicle
//   - Red/orange lines between conflicting drone pairs
//   - Comms-loss markers at last known positions
// ============================================================================
Item {
    id: _root

    property var mapControl

    // ── Safety bubble circles for each active vehicle ──
    Repeater {
        model: {
            if (!vehicleCoordinator || !vehicleCoordinator.enabled || !vehicleCoordinator.altitudeSeparationEnabled)
                return null
            return QGroundControl.multiVehicleManager.vehicles
        }

        MapCircle {
            center:     object.coordinate
            radius:     vehicleCoordinator ? vehicleCoordinator.safetyBubbleHorizontalM : 50
            color:      _circleColor()
            border.color: _borderColor()
            border.width: 2
            opacity:    0.15

            function _circleColor() {
                if (!vehicleCoordinator) return "transparent"
                // Check if this vehicle is involved in a conflict
                var conflicts = vehicleCoordinator.conflicts
                if (conflicts) {
                    for (var i = 0; i < conflicts.count; i++) {
                        var c = conflicts.get(i)
                        if (c.vehicleIdA === object.id || c.vehicleIdB === object.id) {
                            return c.severity === 1 ? "#e74c3c" : "#f39c12"
                        }
                    }
                }
                return "#2ecc71"
            }

            function _borderColor() {
                if (!vehicleCoordinator) return "#2ecc71"
                var conflicts = vehicleCoordinator.conflicts
                if (conflicts) {
                    for (var i = 0; i < conflicts.count; i++) {
                        var c = conflicts.get(i)
                        if (c.vehicleIdA === object.id || c.vehicleIdB === object.id) {
                            return c.severity === 1 ? "#e74c3c" : "#f39c12"
                        }
                    }
                }
                return "#2ecc71"
            }
        }
    }

    // ── Conflict lines between drone pairs ──
    Repeater {
        model: vehicleCoordinator && vehicleCoordinator.enabled ? vehicleCoordinator.conflicts : null

        MapPolyline {
            line.width: object.severity === 1 ? 3 : 2
            line.color: object.severity === 1 ? "#e74c3c" : "#f39c12"
            path: _conflictPath()

            function _conflictPath() {
                var vehicles = QGroundControl.multiVehicleManager.vehicles
                var posA = null
                var posB = null
                for (var i = 0; i < vehicles.count; i++) {
                    var v = vehicles.get(i)
                    if (v.id === object.vehicleIdA) posA = v.coordinate
                    if (v.id === object.vehicleIdB) posB = v.coordinate
                }
                if (posA && posB) return [posA, posB]
                return []
            }
        }
    }

    // ── Conflict distance labels (midpoint between conflicting drones) ──
    Repeater {
        model: vehicleCoordinator && vehicleCoordinator.enabled ? vehicleCoordinator.conflicts : null

        MapQuickItem {
            coordinate: _midpoint()
            anchorPoint.x: distLabel.width / 2
            anchorPoint.y: distLabel.height / 2
            sourceItem: Rectangle {
                id: distLabel
                width:  distText.width + ScreenTools.defaultFontPixelWidth
                height: distText.height + 4
                radius: 3
                color:  object.severity === 1 ? Qt.rgba(0.9, 0.3, 0.24, 0.9) : Qt.rgba(0.95, 0.6, 0.07, 0.9)

                QGCLabel {
                    id: distText
                    anchors.centerIn: parent
                    text: object.horizontalDistM.toFixed(0) + "m"
                    color: "white"
                    font.pointSize: ScreenTools.smallFontPointSize
                    font.bold: true
                }
            }

            function _midpoint() {
                var vehicles = QGroundControl.multiVehicleManager.vehicles
                var posA = null
                var posB = null
                for (var i = 0; i < vehicles.count; i++) {
                    var v = vehicles.get(i)
                    if (v.id === object.vehicleIdA) posA = v.coordinate
                    if (v.id === object.vehicleIdB) posB = v.coordinate
                }
                if (posA && posB) {
                    return QtPositioning.coordinate(
                        (posA.latitude + posB.latitude) / 2,
                        (posA.longitude + posB.longitude) / 2,
                        (posA.altitude + posB.altitude) / 2
                    )
                }
                return QtPositioning.coordinate(0, 0)
            }
        }
    }

    // ── Comms loss markers (last known position of lost drones) ──
    Repeater {
        model: vehicleCoordinator && vehicleCoordinator.enabled ? vehicleCoordinator.commsLossEvents : null

        MapQuickItem {
            coordinate: object.lastKnownPosition
            anchorPoint.x: commsLossMarker.width / 2
            anchorPoint.y: commsLossMarker.height / 2
            visible: object.state === 0 || object.state === 1  // Detected or RTLTriggered

            sourceItem: Rectangle {
                id: commsLossMarker
                width:  commsLossContent.width + ScreenTools.defaultFontPixelWidth * 1.5
                height: commsLossContent.height + ScreenTools.defaultFontPixelHeight * 0.5
                radius: ScreenTools.defaultFontPixelHeight * 0.3
                color:  Qt.rgba(0.9, 0.2, 0.15, 0.9)
                border.color: "white"
                border.width: 1

                Column {
                    id: commsLossContent
                    anchors.centerIn: parent
                    spacing: 1

                    QGCLabel {
                        text:               "V" + object.vehicleId + " LOST"
                        color:              "white"
                        font.bold:          true
                        font.pointSize:     ScreenTools.smallFontPointSize
                        anchors.horizontalCenter: parent.horizontalCenter
                    }

                    QGCLabel {
                        text:               object.elapsedSec + "s ago"
                        color:              "#ffcccc"
                        font.pointSize:     ScreenTools.smallFontPointSize * 0.85
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                }

                // Pulsing animation for active comms loss
                SequentialAnimation on opacity {
                    running: object.state === 0  // Only pulse during Detected state
                    loops: Animation.Infinite
                    NumberAnimation { from: 1.0; to: 0.4; duration: 800 }
                    NumberAnimation { from: 0.4; to: 1.0; duration: 800 }
                }
            }
        }
    }

    // ── Comms loss radius ring (visual RTL indicator) ──
    Repeater {
        model: vehicleCoordinator && vehicleCoordinator.enabled ? vehicleCoordinator.commsLossEvents : null

        MapCircle {
            center:         object.lastKnownPosition
            radius:         100   // 100m indicator ring
            color:          "transparent"
            border.color:   object.state === 1 ? "#e74c3c" : "#f39c12"
            border.width:   2
            opacity:        0.6
            visible:        object.state === 0 || object.state === 1
        }
    }
}
