import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

// Map marker for SAR targets — with appear animation and ripple effect
Item {
    id: targetMarker

    property var target         // SARTarget object
    property real markerSize:   ScreenTools.defaultFontPixelHeight * 2.5

    width:  markerSize
    height: markerSize + infoRect.height

    // Priority-based color
    property color markerColor: {
        if (!target) return "#888"
        switch (target.priority) {
        case 3: return "#e74c3c"    // Critical — red
        case 2: return "#f39c12"    // High — orange
        case 1: return "#f1c40f"    // Medium — yellow
        default: return "#3498db"   // Low — blue
        }
    }

    // ── Ripple ring (expanding circle that fades out) ──
    Rectangle {
        id: ripple
        anchors.centerIn: marker
        width:      0
        height:     width
        radius:     width / 2
        color:      "transparent"
        border.color: markerColor
        border.width: 2
        opacity:    1

        ParallelAnimation {
            id: rippleAnim
            running: true
            alwaysRunToEnd: true

            NumberAnimation {
                target: ripple
                property: "width"
                from: markerSize * 0.5
                to: markerSize * 3
                duration: 600
                easing.type: Easing.OutQuad
            }

            NumberAnimation {
                target: ripple
                property: "opacity"
                from: 0.8
                to: 0
                duration: 600
                easing.type: Easing.OutQuad
            }
        }
    }

    // ── Marker icon ──
    Rectangle {
        id: marker
        anchors.horizontalCenter: parent.horizontalCenter
        width:      markerSize
        height:     markerSize
        radius:     markerSize / 2
        color:      markerColor
        border.color: "white"
        border.width: 2

        // Entry animation: scale bounce
        scale: 0
        opacity: 0

        SequentialAnimation on scale {
            id: bounceAnim
            running: true
            NumberAnimation { to: 1.2; duration: 200; easing.type: Easing.OutQuad }
            NumberAnimation { to: 1.0; duration: 150; easing.type: Easing.OutBounce }
        }

        NumberAnimation on opacity {
            id: fadeInAnim
            running: true
            from: 0; to: 1
            duration: 200
            easing.type: Easing.OutQuad
        }

        QGCLabel {
            anchors.centerIn:   parent
            text:               target ? "T" + target.targetId : "?"
            color:              "white"
            font.bold:          true
            font.pointSize:     ScreenTools.smallFontPointSize
        }

        // Pulse animation for unconfirmed targets
        SequentialAnimation on opacity {
            running:    target && target.status === 0 && !fadeInAnim.running
            loops:      Animation.Infinite
            NumberAnimation { to: 0.4; duration: 800 }
            NumberAnimation { to: 1.0; duration: 800 }
        }
    }

    // Info popup on hover/click
    Rectangle {
        id: infoRect
        anchors.top:                marker.bottom
        anchors.topMargin:          2
        anchors.horizontalCenter:   parent.horizontalCenter
        width:                      infoColumn.width + 8
        height:                     infoColumn.height + 8
        radius:                     4
        color:                      Qt.rgba(0, 0, 0, 0.8)
        visible:                    mouseArea.containsMouse

        ColumnLayout {
            id: infoColumn
            anchors.centerIn: parent
            spacing: 2

            QGCLabel {
                text:   target ? target.description || "Unknown target" : ""
                color:  "white"
                font.pointSize: ScreenTools.smallFontPointSize
            }

            QGCLabel {
                text: {
                    if (!target) return ""
                    switch (target.status) {
                    case 0: return "Status: Unconfirmed"
                    case 1: return "Status: Investigating"
                    case 2: return "Status: CONFIRMED"
                    case 3: return "Status: False Alarm"
                    case 4: return "Status: Rescued"
                    default: return ""
                    }
                }
                color:  target && target.status === 2 ? "#2ecc71" : "#aaa"
                font.pointSize: ScreenTools.smallFontPointSize
            }

            QGCLabel {
                text:   target ? target.coordinate.latitude.toFixed(6) + ", " + target.coordinate.longitude.toFixed(6) : ""
                color:  "#888"
                font.pointSize: ScreenTools.smallFontPointSize
            }

            QGCLabel {
                text:   target ? "Spotted by V" + target.spottedByVehicle : ""
                color:  "#888"
                font.pointSize: ScreenTools.smallFontPointSize
                visible: target && target.spottedByVehicle >= 0
            }
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
    }
}
