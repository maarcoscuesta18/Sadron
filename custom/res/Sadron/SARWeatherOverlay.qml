import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtLocation
import QtPositioning

import QGroundControl
import QGroundControl.Controls

// Map overlay: wind vectors (arrows) + precipitation info + weather conditions
// Wind vectors are rendered as rotated arrow icons at grid positions
// Precipitation is shown as a color-coded info badge
Item {
    id: weatherOverlay

    property var mapControl

    property real _margin: ScreenTools.defaultFontPixelWidth * 0.75

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    // ── Wind Vector Arrows on Map ──
    // Each vector is an arrow icon positioned at a geo coordinate, rotated by wind direction
    Repeater {
        id: windVectorRepeater
        model: environmentalDataProvider ? environmentalDataProvider.windVectors : []

        Item {
            id: windArrowDelegate

            property var  vectorData:   modelData
            property real lat:          vectorData ? vectorData.latitude  : 0
            property real lon:          vectorData ? vectorData.longitude : 0
            property real speed:        vectorData ? vectorData.speed     : 0
            property real direction:    vectorData ? vectorData.direction : 0

            // Convert geo coordinate to screen position via map
            property var screenPos: mapControl ? mapControl.fromCoordinate(
                QtPositioning.coordinate(lat, lon), false) : Qt.point(0, 0)

            x:       screenPos.x - width / 2
            y:       screenPos.y - height / 2
            width:   arrowSize
            height:  arrowSize
            visible: mapControl !== null && screenPos.x > 0 && screenPos.y > 0

            // Arrow size scales with wind speed (min 20, max 48)
            property real arrowSize: Math.max(20, Math.min(48, 16 + speed * 3))

            // Arrow color: light wind = cyan, moderate = yellow, strong = red
            property color arrowColor: {
                if (speed < 3)  return "#00bcd4"    // Light — cyan
                if (speed < 8)  return "#4caf50"    // Moderate — green
                if (speed < 15) return "#ff9800"    // Strong — orange
                return "#f44336"                     // Severe — red
            }

            // Wind arrow image — rotated to wind direction (meteorological: FROM direction)
            QGCColoredImage {
                id:                 arrowImage
                anchors.centerIn:   parent
                width:              parent.arrowSize * 0.8
                height:             width
                source:             "/custom/img/wind_arrow.svg"
                color:              parent.arrowColor
                sourceSize.width:   width
                fillMode:           Image.PreserveAspectFit

                transform: Rotation {
                    origin.x: arrowImage.width / 2
                    origin.y: arrowImage.height / 2
                    angle:    direction   // Wind direction is "from" — arrow points in wind flow direction
                }
            }

            // Speed label beneath arrow
            QGCLabel {
                anchors.top:                arrowImage.bottom
                anchors.topMargin:          1
                anchors.horizontalCenter:   parent.horizontalCenter
                text:                       speed.toFixed(1)
                color:                      parent.arrowColor
                font.pointSize:             ScreenTools.smallFontPointSize * 0.8
                font.bold:                  true
            }
        }
    }

    // ── Weather Info Badge (top-left of overlay area) ──
    Rectangle {
        id:                     weatherBadge
        anchors.top:            parent.top
        anchors.right:          parent.right
        anchors.margins:        _margin * 2
        anchors.rightMargin:    ScreenTools.defaultFontPixelWidth * 10
        width:                  weatherBadgeCol.width + _margin * 3
        height:                 weatherBadgeCol.height + _margin * 2
        radius:                 ScreenTools.defaultFontPixelHeight * 0.3
        color:                  Qt.rgba(0, 0, 0, 0.75)
        border.color:           Qt.rgba(1, 1, 1, 0.15)
        border.width:           1
        visible:                environmentalDataProvider && environmentalDataProvider.weatherAvailable

        ColumnLayout {
            id:                 weatherBadgeCol
            anchors.centerIn:   parent
            spacing:            2

            QGCLabel {
                text:       "WEATHER"
                color:      "#00bcd4"
                font.bold:  true
                font.pointSize: ScreenTools.smallFontPointSize
            }

            // Wind row
            RowLayout {
                spacing: _margin

                QGCLabel {
                    text:   "\u2B06"  // Up arrow (wind direction indicator)
                    color:  "#4caf50"
                    font.pointSize: ScreenTools.smallFontPointSize
                    rotation: environmentalDataProvider ? environmentalDataProvider.windDirection : 0
                }

                QGCLabel {
                    text: {
                        if (!environmentalDataProvider) return ""
                        var s = environmentalDataProvider.windSpeed.toFixed(1) + " m/s"
                        if (environmentalDataProvider.windGust > 0) {
                            s += " (G" + environmentalDataProvider.windGust.toFixed(1) + ")"
                        }
                        return s
                    }
                    color:  "white"
                    font.pointSize: ScreenTools.smallFontPointSize
                }
            }

            // Precipitation row
            RowLayout {
                spacing: _margin
                visible: environmentalDataProvider && environmentalDataProvider.precipitation > 0

                Rectangle {
                    width:  ScreenTools.defaultFontPixelHeight * 0.5
                    height: width
                    radius: width / 2
                    color:  "#2196f3"
                }

                QGCLabel {
                    text:   environmentalDataProvider ? environmentalDataProvider.precipitation.toFixed(1) + " mm/h" : ""
                    color:  "#90caf9"
                    font.pointSize: ScreenTools.smallFontPointSize
                }
            }

            // Temperature + visibility
            RowLayout {
                spacing: _margin * 2

                QGCLabel {
                    text: {
                        if (!environmentalDataProvider) return ""
                        var temp = environmentalDataProvider.temperature
                        return (typeof temp === 'number' ? temp.toFixed(0) : "--") + "\u00B0C"
                    }
                    color:  "white"
                    font.pointSize: ScreenTools.smallFontPointSize
                }

                QGCLabel {
                    text: {
                        if (!environmentalDataProvider) return ""
                        var vis = environmentalDataProvider.visibility
                        if (typeof vis !== 'number' || isNaN(vis)) return "-- vis"
                        if (vis >= 1000) return (vis / 1000).toFixed(1) + " km vis"
                        return vis.toFixed(0) + " m vis"
                    }
                    color:  environmentalDataProvider && typeof environmentalDataProvider.visibility === 'number' && environmentalDataProvider.visibility < 1000 ? "#ff9800" : "#aaa"
                    font.pointSize: ScreenTools.smallFontPointSize
                }
            }

            // Description
            QGCLabel {
                text:   environmentalDataProvider ? environmentalDataProvider.weatherDescription : ""
                color:  "#aaa"
                font.pointSize: ScreenTools.smallFontPointSize * 0.9
                font.italic: true
            }
        }
    }

    // ── Wind Speed Legend ──
    Rectangle {
        anchors.bottom:     parent.bottom
        anchors.left:       parent.left
        anchors.margins:    _margin * 2
        anchors.leftMargin: ScreenTools.defaultFontPixelWidth * 34
        width:              windLegendCol.width + _margin * 2
        height:             windLegendCol.height + _margin * 2
        radius:             4
        color:              Qt.rgba(0, 0, 0, 0.7)
        border.color:       Qt.rgba(1, 1, 1, 0.1)
        border.width:       1

        ColumnLayout {
            id:                 windLegendCol
            anchors.centerIn:   parent
            spacing:            2

            QGCLabel {
                text:       "Wind (m/s)"
                color:      "#aaa"
                font.pointSize: ScreenTools.smallFontPointSize * 0.8
                font.bold:  true
            }

            Repeater {
                model: [
                    { label: "< 3", clr: "#00bcd4" },
                    { label: "3-8", clr: "#4caf50" },
                    { label: "8-15", clr: "#ff9800" },
                    { label: "> 15", clr: "#f44336" }
                ]

                RowLayout {
                    spacing: _margin * 0.5

                    Rectangle {
                        width:  ScreenTools.defaultFontPixelHeight * 0.5
                        height: width
                        radius: 2
                        color:  modelData.clr
                    }

                    QGCLabel {
                        text:   modelData.label
                        color:  modelData.clr
                        font.pointSize: ScreenTools.smallFontPointSize * 0.8
                    }
                }
            }
        }
    }
}
