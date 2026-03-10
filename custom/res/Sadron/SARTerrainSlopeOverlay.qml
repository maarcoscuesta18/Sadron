import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtLocation
import QtPositioning

import QGroundControl
import QGroundControl.Controls

// Map overlay: terrain slope analysis — colored grid cells from Copernicus elevation data
// Green (flat) → Yellow (moderate) → Orange (steep) → Red (very steep)
Item {
    id: slopeOverlay

    property var mapControl

    property real _margin: ScreenTools.defaultFontPixelWidth * 0.75

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    // ── Slope color helper ──
    function slopeColor(slopeDeg) {
        if (slopeDeg < 5)   return Qt.rgba(0.30, 0.69, 0.31, 0.35)    // Green — flat
        if (slopeDeg < 15)  return Qt.rgba(0.55, 0.76, 0.29, 0.40)    // Light green
        if (slopeDeg < 25)  return Qt.rgba(1.00, 0.92, 0.23, 0.45)    // Yellow — moderate
        if (slopeDeg < 35)  return Qt.rgba(1.00, 0.60, 0.00, 0.50)    // Orange — steep
        if (slopeDeg < 45)  return Qt.rgba(0.96, 0.26, 0.21, 0.55)    // Red — very steep
        return Qt.rgba(0.62, 0.16, 0.16, 0.60)                         // Dark red — cliff
    }

    function slopeLabel(slopeDeg) {
        if (slopeDeg < 5)   return "Flat"
        if (slopeDeg < 15)  return "Gentle"
        if (slopeDeg < 25)  return "Moderate"
        if (slopeDeg < 35)  return "Steep"
        if (slopeDeg < 45)  return "Very Steep"
        return "Cliff"
    }

    // ── Slope Grid Cells on Map ──
    Repeater {
        id: slopeGridRepeater
        model: environmentalDataProvider ? environmentalDataProvider.slopeGrid : []

        Item {
            id: slopeCellDelegate

            property var  cellData:  modelData
            property real lat:       cellData ? cellData.latitude  : 0
            property real lon:       cellData ? cellData.longitude : 0
            property real slope:     cellData ? cellData.slope     : 0
            property real aspect:    cellData ? cellData.aspect    : 0
            property real elevation: cellData ? cellData.elevation : 0

            property var screenPos: mapControl ? mapControl.fromCoordinate(
                QtPositioning.coordinate(lat, lon), false) : Qt.point(0, 0)

            // Cell size depends on the grid density and zoom level
            // Use a neighboring cell to compute pixel spacing
            property real cellPixelSize: {
                if (!mapControl || !environmentalDataProvider) return 8
                var rows = environmentalDataProvider.slopeRows
                var cols = environmentalDataProvider.slopeCols
                if (rows < 2 || cols < 2) return 8
                // Estimate pixel size from grid resolution
                var grid = environmentalDataProvider.slopeGrid
                if (grid.length < 2) return 8
                var p0 = mapControl.fromCoordinate(
                    QtPositioning.coordinate(grid[0].latitude, grid[0].longitude), false)
                var p1 = mapControl.fromCoordinate(
                    QtPositioning.coordinate(grid[1].latitude, grid[1].longitude), false)
                var dx = Math.abs(p1.x - p0.x)
                return Math.max(4, dx)
            }

            x:       screenPos.x - cellPixelSize / 2
            y:       screenPos.y - cellPixelSize / 2
            width:   cellPixelSize
            height:  cellPixelSize
            visible: mapControl !== null && screenPos.x > -cellPixelSize && screenPos.y > -cellPixelSize
                     && screenPos.x < slopeOverlay.width + cellPixelSize
                     && screenPos.y < slopeOverlay.height + cellPixelSize

            Rectangle {
                anchors.fill:   parent
                color:          slopeColor(slope)
                radius:         1

                // Aspect indicator — tiny line showing direction of steepest descent
                Rectangle {
                    visible:                    slope > 5 && parent.width > 10
                    anchors.centerIn:           parent
                    width:                      1
                    height:                     parent.height * 0.4
                    color:                      Qt.rgba(1, 1, 1, 0.4)
                    rotation:                   aspect
                    transformOrigin:            Item.Center
                }
            }

            // Tooltip on hover (only for larger cells)
            ToolTip {
                visible:    cellMa.containsMouse && cellPixelSize > 12
                text:       slope.toFixed(1) + "\u00B0 " + slopeLabel(slope) + "\n"
                            + "Aspect: " + aspect.toFixed(0) + "\u00B0\n"
                            + "Elev: " + elevation.toFixed(0) + "m"
                delay:      300
            }

            MouseArea {
                id:             cellMa
                anchors.fill:   parent
                hoverEnabled:   true
                acceptedButtons: Qt.NoButton
            }
        }
    }

    // ── Slope Summary + Legend Panel ──
    Rectangle {
        anchors.bottom:     parent.bottom
        anchors.left:       parent.left
        anchors.margins:    _margin * 2
        anchors.leftMargin: ScreenTools.defaultFontPixelWidth * 34
        width:              slopeLegendCol.width + _margin * 3
        height:             slopeLegendCol.height + _margin * 2
        radius:             ScreenTools.defaultFontPixelHeight * 0.3
        color:              Qt.rgba(0, 0, 0, 0.75)
        border.color:       Qt.rgba(1, 1, 1, 0.15)
        border.width:       1
        visible:            environmentalDataProvider && environmentalDataProvider.slopeDataAvailable

        ColumnLayout {
            id:                 slopeLegendCol
            anchors.centerIn:   parent
            spacing:            3

            QGCLabel {
                text:       "TERRAIN SLOPE"
                color:      "#ff9800"
                font.bold:  true
                font.pointSize: ScreenTools.smallFontPointSize
            }

            // Summary stats
            RowLayout {
                spacing: _margin * 2

                QGCLabel {
                    text:   "Min: " + (environmentalDataProvider ? environmentalDataProvider.minSlope.toFixed(1) : "0") + "\u00B0"
                    color:  "#4caf50"
                    font.pointSize: ScreenTools.smallFontPointSize * 0.9
                }

                QGCLabel {
                    text:   "Avg: " + (environmentalDataProvider ? environmentalDataProvider.avgSlope.toFixed(1) : "0") + "\u00B0"
                    color:  "#ffeb3b"
                    font.pointSize: ScreenTools.smallFontPointSize * 0.9
                }

                QGCLabel {
                    text:   "Max: " + (environmentalDataProvider ? environmentalDataProvider.maxSlope.toFixed(1) : "0") + "\u00B0"
                    color:  "#f44336"
                    font.pointSize: ScreenTools.smallFontPointSize * 0.9
                }
            }

            // Color scale legend
            Repeater {
                model: [
                    { label: "0-5\u00B0   Flat",         clr: "#4caf50" },
                    { label: "5-15\u00B0  Gentle",       clr: "#8bc34a" },
                    { label: "15-25\u00B0 Moderate",     clr: "#ffeb3b" },
                    { label: "25-35\u00B0 Steep",        clr: "#ff9800" },
                    { label: "35-45\u00B0 Very Steep",   clr: "#f44336" },
                    { label: "> 45\u00B0  Cliff",        clr: "#9e2929" }
                ]

                RowLayout {
                    spacing: _margin * 0.5

                    Rectangle {
                        width:  ScreenTools.defaultFontPixelHeight * 0.6
                        height: width
                        radius: 2
                        color:  modelData.clr
                    }

                    QGCLabel {
                        text:   modelData.label
                        color:  "#ccc"
                        font.pointSize: ScreenTools.smallFontPointSize * 0.8
                    }
                }
            }
        }
    }

    // ── Loading indicator ──
    Rectangle {
        anchors.centerIn:   parent
        width:              loadingLabel.width + _margin * 4
        height:             loadingLabel.height + _margin * 2
        radius:             4
        color:              Qt.rgba(0, 0, 0, 0.8)
        visible:            environmentalDataProvider && environmentalDataProvider.slopeLoading

        QGCLabel {
            id:                 loadingLabel
            anchors.centerIn:   parent
            text:               "Computing terrain slope..."
            color:              "#ff9800"
            font.pointSize:     ScreenTools.defaultFontPointSize
        }
    }
}
