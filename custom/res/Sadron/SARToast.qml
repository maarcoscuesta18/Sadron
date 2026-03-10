import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

// Toast notification bar for SAR feedback — slides in at bottom-center
Item {
    id: _root

    anchors.fill: parent

    function show(title, detail, accentColor) {
        _title = title
        _detail = detail
        _accent = accentColor || "#f39c12"
        _visible = true
        hideTimer.restart()
    }

    property string _title:     ""
    property string _detail:    ""
    property color  _accent:    "#f39c12"
    property bool   _visible:   false

    Timer {
        id:         hideTimer
        interval:   4000
        onTriggered: _visible = false
    }

    Rectangle {
        id:                         toastRect
        anchors.horizontalCenter:   parent.horizontalCenter
        anchors.bottom:             parent.bottom
        anchors.bottomMargin:       ScreenTools.defaultFontPixelHeight * 4
        width:                      toastLayout.width + ScreenTools.defaultFontPixelWidth * 3
        height:                     toastLayout.height + ScreenTools.defaultFontPixelHeight * 0.8
        radius:                     ScreenTools.defaultFontPixelHeight * 0.4
        color:                      Qt.rgba(0, 0, 0, 0.85)
        visible:                    false
        opacity:                    0

        // Colored left accent bar
        Rectangle {
            anchors.left:   parent.left
            anchors.top:    parent.top
            anchors.bottom: parent.bottom
            width:          4
            radius:         parent.radius
            color:          _accent

            // Clip the right side of the accent so only left corners are rounded
            Rectangle {
                anchors.right:  parent.right
                anchors.top:    parent.top
                anchors.bottom: parent.bottom
                width:          parent.width / 2
                color:          _accent
            }
        }

        ColumnLayout {
            id: toastLayout
            anchors.centerIn: parent
            anchors.leftMargin: ScreenTools.defaultFontPixelWidth * 1.5
            spacing: 2

            Text {
                text:               _title
                color:              "white"
                font.bold:          true
                font.pointSize:     ScreenTools.defaultFontPointSize
                font.family:        ScreenTools.normalFontFamily
            }

            Text {
                text:               _detail
                color:              "#aaaaaa"
                font.pointSize:     ScreenTools.smallFontPointSize
                font.family:        ScreenTools.normalFontFamily
                visible:            _detail.length > 0
            }
        }

        // Show/hide states
        states: [
            State {
                name: "visible"
                when: _visible
                PropertyChanges { target: toastRect; visible: true; opacity: 1 }
            },
            State {
                name: "hidden"
                when: !_visible
                PropertyChanges { target: toastRect; opacity: 0; visible: false }
            }
        ]

        transitions: [
            Transition {
                from: "hidden"; to: "visible"
                SequentialAnimation {
                    PropertyAction { target: toastRect; property: "visible"; value: true }
                    NumberAnimation { property: "opacity"; duration: 200; easing.type: Easing.OutQuad }
                }
            },
            Transition {
                from: "visible"; to: "hidden"
                SequentialAnimation {
                    NumberAnimation { property: "opacity"; duration: 300; easing.type: Easing.InQuad }
                    PropertyAction { target: toastRect; property: "visible"; value: false }
                }
            }
        ]
    }
}
