import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

// ============================================================================
// SARReTaskingPopup — Slide-in operator override popup for dynamic re-tasking
// Shows when a re-task is proposed with a countdown auto-confirm timer.
// Operator can: Confirm Now, Cancel, or Change Drone.
// ============================================================================
Item {
    id: _root

    anchors.fill: parent

    property bool _hasPending: sarReTaskingManager ? sarReTaskingManager.pendingReTask !== null : false
    property var  _pending:    sarReTaskingManager ? sarReTaskingManager.pendingReTask : null
    property int  _countdown:  _pending ? _pending.countdownSec : 0
    property int  _timeout:    sarReTaskingManager ? sarReTaskingManager.autoConfirmTimeoutSec : 10

    // Priority labels & colors
    function priorityLabel(p) {
        switch (p) {
        case 0: return "LOW"
        case 1: return "MEDIUM"
        case 2: return "HIGH"
        case 3: return "CRITICAL"
        }
        return "UNKNOWN"
    }
    function priorityColor(p) {
        switch (p) {
        case 0: return "#3498db"
        case 1: return "#f39c12"
        case 2: return "#e67e22"
        case 3: return "#e74c3c"
        }
        return "#aaaaaa"
    }

    // ── Active re-task count badge (top-right corner) ──
    Rectangle {
        id: activeReTaskBadge
        visible: sarReTaskingManager && sarReTaskingManager.activeReTaskCount > 0
        anchors.top:        parent.top
        anchors.right:      parent.right
        anchors.topMargin:  ScreenTools.defaultFontPixelHeight * 0.5
        anchors.rightMargin: ScreenTools.defaultFontPixelWidth * 12
        width:              badgeRow.width + ScreenTools.defaultFontPixelWidth * 2
        height:             badgeRow.height + ScreenTools.defaultFontPixelHeight * 0.4
        radius:             ScreenTools.defaultFontPixelHeight * 0.3
        color:              Qt.rgba(0.9, 0.3, 0.2, 0.85)

        RowLayout {
            id: badgeRow
            anchors.centerIn: parent
            spacing: ScreenTools.defaultFontPixelWidth * 0.3

            QGCLabel {
                text:       sarReTaskingManager ? sarReTaskingManager.activeReTaskCount + " Active Re-Task(s)" : ""
                color:      "white"
                font.bold:  true
                font.pointSize: ScreenTools.smallFontPointSize
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: {
                if (sarReTaskingManager) sarReTaskingManager.completeAllReTasks()
            }

            ToolTip.visible: containsMouse
            ToolTip.text: "Click to complete all active re-tasks"
            hoverEnabled: true
        }
    }

    // ── Proposed re-task popup (slides in from top-right) ──
    Rectangle {
        id:         popupCard
        visible:    false
        opacity:    0
        width:      popupContent.width + ScreenTools.defaultFontPixelWidth * 3
        height:     popupContent.height + ScreenTools.defaultFontPixelHeight * 1.5
        radius:     ScreenTools.defaultFontPixelHeight * 0.5
        color:      Qt.rgba(0.06, 0.06, 0.15, 0.95)
        border.color: _pending ? priorityColor(_pending.targetPriority) : "#f39c12"
        border.width: 2

        anchors.top:        parent.top
        anchors.right:      parent.right
        anchors.topMargin:  ScreenTools.defaultFontPixelHeight * 0.5
        anchors.rightMargin: ScreenTools.defaultFontPixelWidth * 2

        // ── Countdown progress bar at top ──
        Rectangle {
            id:             countdownBar
            anchors.top:    parent.top
            anchors.left:   parent.left
            anchors.topMargin: 2
            anchors.leftMargin: parent.radius
            height:         4
            radius:         2
            color:          _pending ? priorityColor(_pending.targetPriority) : "#f39c12"
            width:          _timeout > 0 ? (parent.width - parent.radius * 2) * (_countdown / _timeout) : 0

            Behavior on width {
                NumberAnimation { duration: 900; easing.type: Easing.Linear }
            }
        }

        ColumnLayout {
            id:             popupContent
            anchors.centerIn: parent
            spacing:        ScreenTools.defaultFontPixelHeight * 0.4

            // Header
            RowLayout {
                spacing: ScreenTools.defaultFontPixelWidth * 0.5

                Rectangle {
                    width:  ScreenTools.defaultFontPixelHeight * 0.8
                    height: width
                    radius: width / 2
                    color:  _pending ? priorityColor(_pending.targetPriority) : "#f39c12"
                }

                QGCLabel {
                    text:       "DRONE RE-TASK PROPOSED"
                    color:      "white"
                    font.bold:  true
                    font.pointSize: ScreenTools.defaultFontPointSize
                }

                QGCLabel {
                    text:       _countdown + "s"
                    color:      _countdown <= 3 ? "#e74c3c" : "#aaaaaa"
                    font.bold:  _countdown <= 3
                    font.pointSize: ScreenTools.smallFontPointSize
                }
            }

            // Target info
            Rectangle {
                Layout.fillWidth:   true
                height:             targetInfoCol.height + ScreenTools.defaultFontPixelHeight * 0.4
                radius:             ScreenTools.defaultFontPixelHeight * 0.2
                color:              Qt.rgba(1, 1, 1, 0.05)

                ColumnLayout {
                    id:                 targetInfoCol
                    anchors.left:       parent.left
                    anchors.right:      parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins:    ScreenTools.defaultFontPixelWidth
                    spacing:            2

                    RowLayout {
                        spacing: ScreenTools.defaultFontPixelWidth

                        QGCLabel {
                            text:   "Target:"
                            color:  "#888888"
                            font.pointSize: ScreenTools.smallFontPointSize
                        }
                        QGCLabel {
                            text:   _pending ? _pending.targetDescription : ""
                            color:  "white"
                            font.pointSize: ScreenTools.smallFontPointSize
                            font.bold: true
                        }
                        Rectangle {
                            width:  priLabel.width + ScreenTools.defaultFontPixelWidth
                            height: priLabel.height + 2
                            radius: 3
                            color:  _pending ? priorityColor(_pending.targetPriority) : "#888"

                            QGCLabel {
                                id:             priLabel
                                anchors.centerIn: parent
                                text:           _pending ? priorityLabel(_pending.targetPriority) : ""
                                color:          "white"
                                font.pointSize: ScreenTools.smallFontPointSize * 0.85
                                font.bold:      true
                            }
                        }
                    }

                    QGCLabel {
                        text:   _pending ? _pending.targetCoord.latitude.toFixed(6) + ", " + _pending.targetCoord.longitude.toFixed(6) : ""
                        color:  "#aaaaaa"
                        font.pointSize: ScreenTools.smallFontPointSize * 0.9
                    }
                }
            }

            // Drone info
            Rectangle {
                Layout.fillWidth:   true
                height:             droneInfoCol.height + ScreenTools.defaultFontPixelHeight * 0.4
                radius:             ScreenTools.defaultFontPixelHeight * 0.2
                color:              Qt.rgba(1, 1, 1, 0.05)

                ColumnLayout {
                    id:                 droneInfoCol
                    anchors.left:       parent.left
                    anchors.right:      parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins:    ScreenTools.defaultFontPixelWidth
                    spacing:            2

                    RowLayout {
                        spacing: ScreenTools.defaultFontPixelWidth

                        QGCLabel {
                            text:   "Drone:"
                            color:  "#888888"
                            font.pointSize: ScreenTools.smallFontPointSize
                        }
                        QGCLabel {
                            text:   _pending ? "Vehicle " + _pending.vehicleId : ""
                            color:  "#2ecc71"
                            font.pointSize: ScreenTools.smallFontPointSize
                            font.bold: true
                        }
                    }

                    QGCLabel {
                        text:   _pending ? "Pausing Zone " + _pending.originalZoneId : ""
                        color:  "#aaaaaa"
                        font.pointSize: ScreenTools.smallFontPointSize * 0.9
                    }
                }
            }

            // Action buttons
            RowLayout {
                Layout.fillWidth: true
                spacing: ScreenTools.defaultFontPixelWidth

                // Confirm Now
                Rectangle {
                    Layout.fillWidth: true
                    height:         confirmLabel.height + ScreenTools.defaultFontPixelHeight * 0.5
                    radius:         ScreenTools.defaultFontPixelHeight * 0.2
                    color:          confirmMa.containsMouse ? "#27ae60" : "#2ecc71"

                    QGCLabel {
                        id:             confirmLabel
                        anchors.centerIn: parent
                        text:           "Confirm Now"
                        color:          "white"
                        font.bold:      true
                        font.pointSize: ScreenTools.smallFontPointSize
                    }
                    MouseArea {
                        id:             confirmMa
                        anchors.fill:   parent
                        hoverEnabled:   true
                        onClicked:      { if (sarReTaskingManager) sarReTaskingManager.confirmReTask() }
                    }
                }

                // Cancel
                Rectangle {
                    Layout.fillWidth: true
                    height:         cancelLabel.height + ScreenTools.defaultFontPixelHeight * 0.5
                    radius:         ScreenTools.defaultFontPixelHeight * 0.2
                    color:          cancelMa.containsMouse ? "#c0392b" : "#e74c3c"

                    QGCLabel {
                        id:             cancelLabel
                        anchors.centerIn: parent
                        text:           "Cancel"
                        color:          "white"
                        font.bold:      true
                        font.pointSize: ScreenTools.smallFontPointSize
                    }
                    MouseArea {
                        id:             cancelMa
                        anchors.fill:   parent
                        hoverEnabled:   true
                        onClicked:      { if (sarReTaskingManager) sarReTaskingManager.cancelReTask() }
                    }
                }
            }
        }

        // ── Show/hide states ──
        states: [
            State {
                name: "visible"
                when: _hasPending
                PropertyChanges { target: popupCard; visible: true; opacity: 1 }
            },
            State {
                name: "hidden"
                when: !_hasPending
                PropertyChanges { target: popupCard; opacity: 0 }
            }
        ]

        transitions: [
            Transition {
                from: "hidden"; to: "visible"
                SequentialAnimation {
                    PropertyAction { target: popupCard; property: "visible"; value: true }
                    NumberAnimation { property: "opacity"; duration: 250; easing.type: Easing.OutQuad }
                }
            },
            Transition {
                from: "visible"; to: "hidden"
                SequentialAnimation {
                    NumberAnimation { property: "opacity"; duration: 200; easing.type: Easing.InQuad }
                    PropertyAction { target: popupCard; property: "visible"; value: false }
                }
            }
        ]
    }
}
