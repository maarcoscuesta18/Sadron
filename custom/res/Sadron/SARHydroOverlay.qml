import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtLocation
import QtPositioning

import QGroundControl
import QGroundControl.Controls

// Map overlay: hydrological data from OpenStreetMap Overpass API
// Renders water body polygons (lakes, ponds) and waterways (rivers, streams) on the map
Item {
    id: hydroOverlay

    property var mapControl

    property real   _margin:        ScreenTools.defaultFontPixelWidth * 0.75
    property color  _waterColor:    Qt.rgba(0.13, 0.59, 0.95, 0.35)    // Blue translucent
    property color  _wetlandColor:  Qt.rgba(0.13, 0.59, 0.75, 0.25)    // Teal translucent
    property color  _riverColor:    "#2196f3"
    property color  _streamColor:   "#64b5f6"

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    // Debounce timer for canvas repaints — avoids repaint on every map pan frame
    Timer {
        id:         repaintDebounce
        interval:   50
        repeat:     false
        onTriggered: {
            for (var i = 0; i < waterPolygonRepeater.count; i++) {
                var item = waterPolygonRepeater.itemAt(i)
                if (item) {
                    var canvas = item.children[0]
                    if (canvas && canvas.requestPaint) canvas.requestPaint()
                }
            }
            for (var j = 0; j < waterwayRepeater.count; j++) {
                var wItem = waterwayRepeater.itemAt(j)
                if (wItem) {
                    var wCanvas = wItem.children[0]
                    if (wCanvas && wCanvas.requestPaint) wCanvas.requestPaint()
                }
            }
        }
    }

    // ── Water Body Polygons (lakes, ponds, wetlands) ──
    Repeater {
        id: waterPolygonRepeater
        model: environmentalDataProvider ? environmentalDataProvider.waterPolygons : []

        Item {
            id: polyDelegate

            property var featureData:   modelData
            property var coords:        featureData ? featureData.coordinates : []
            property string featureName: featureData ? (featureData.name || "") : ""
            property string featureType: featureData ? (featureData.type || "water") : "water"
            property bool isWetland:    featureType === "wetland"

            anchors.fill: parent

            // Draw polygon as a series of connected line segments
            // Using Canvas for filled polygon rendering
            Canvas {
                id: polyCanvas
                anchors.fill: parent
                visible: coords.length >= 3 && mapControl !== null

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)

                    if (!coords || coords.length < 3 || !mapControl) return

                    ctx.beginPath()

                    for (var i = 0; i < coords.length; i++) {
                        var pos = mapControl.fromCoordinate(
                            QtPositioning.coordinate(coords[i].latitude, coords[i].longitude), false)
                        if (i === 0) {
                            ctx.moveTo(pos.x, pos.y)
                        } else {
                            ctx.lineTo(pos.x, pos.y)
                        }
                    }
                    ctx.closePath()

                    // Fill
                    ctx.fillStyle = isWetland ? _wetlandColor : _waterColor
                    ctx.fill()

                    // Stroke
                    ctx.strokeStyle = isWetland ? Qt.rgba(0.13, 0.59, 0.75, 0.6) : Qt.rgba(0.13, 0.59, 0.95, 0.6)
                    ctx.lineWidth = 1.5
                    ctx.stroke()

                    // Wetland hatching pattern
                    if (isWetland) {
                        ctx.save()
                        ctx.clip()
                        ctx.strokeStyle = Qt.rgba(0.13, 0.59, 0.75, 0.3)
                        ctx.lineWidth = 0.5
                        for (var y = 0; y < height; y += 8) {
                            ctx.beginPath()
                            ctx.moveTo(0, y)
                            ctx.lineTo(width, y)
                            ctx.stroke()
                        }
                        ctx.restore()
                    }
                }

                // Repaint when map moves/zooms (debounced)
                Connections {
                    target: mapControl || null
                    function onCenterChanged()   { repaintDebounce.restart() }
                    function onZoomLevelChanged() { repaintDebounce.restart() }
                }

                // Initial paint
                Component.onCompleted: requestPaint()
            }

            // Label for named water bodies
            Item {
                visible: featureName.length > 0 && coords.length > 0 && mapControl !== null

                property var centerCoord: {
                    if (!coords || coords.length === 0) return null
                    var sumLat = 0, sumLon = 0
                    for (var i = 0; i < coords.length; i++) {
                        sumLat += coords[i].latitude
                        sumLon += coords[i].longitude
                    }
                    return QtPositioning.coordinate(sumLat / coords.length, sumLon / coords.length)
                }

                // Reference center + zoomLevel so QML re-evaluates on pan/zoom
                property var labelPos: {
                    if (!centerCoord || !mapControl) return Qt.point(0, 0)
                    var _c = mapControl.center
                    var _z = mapControl.zoomLevel
                    return mapControl.fromCoordinate(centerCoord, false)
                }

                x: labelPos.x - labelBg.width / 2
                y: labelPos.y - labelBg.height / 2

                Rectangle {
                    id:         labelBg
                    width:      waterLabel.width + _margin * 2
                    height:     waterLabel.height + _margin
                    radius:     3
                    color:      Qt.rgba(0, 0, 0, 0.6)

                    QGCLabel {
                        id:                 waterLabel
                        anchors.centerIn:   parent
                        text:               featureName
                        color:              isWetland ? _wetlandColor : "#64b5f6"
                        font.pointSize:     ScreenTools.smallFontPointSize * 0.9
                        font.italic:        true
                    }
                }
            }
        }
    }

    // ── Waterways (rivers, streams, canals) ──
    Repeater {
        id: waterwayRepeater
        model: environmentalDataProvider ? environmentalDataProvider.waterways : []

        Item {
            id: wayDelegate

            property var featureData:    modelData
            property var coords:         featureData ? featureData.coordinates : []
            property string featureName: featureData ? (featureData.name || "") : ""
            property string featureType: featureData ? (featureData.type || "stream") : "stream"
            property int lineWidth:      featureData ? (featureData.width || 2) : 2

            anchors.fill: parent

            Canvas {
                id: wayCanvas
                anchors.fill: parent
                visible: coords.length >= 2 && mapControl !== null

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)

                    if (!coords || coords.length < 2 || !mapControl) return

                    ctx.beginPath()

                    for (var i = 0; i < coords.length; i++) {
                        var pos = mapControl.fromCoordinate(
                            QtPositioning.coordinate(coords[i].latitude, coords[i].longitude), false)
                        if (i === 0) {
                            ctx.moveTo(pos.x, pos.y)
                        } else {
                            ctx.lineTo(pos.x, pos.y)
                        }
                    }

                    // River vs stream styling
                    ctx.strokeStyle = featureType === "river" ? _riverColor : _streamColor
                    ctx.lineWidth = lineWidth
                    ctx.lineCap = "round"
                    ctx.lineJoin = "round"
                    ctx.stroke()

                    // Outer glow for rivers
                    if (featureType === "river") {
                        ctx.strokeStyle = Qt.rgba(0.13, 0.59, 0.95, 0.2)
                        ctx.lineWidth = lineWidth + 4
                        ctx.stroke()
                    }
                }

                Connections {
                    target: mapControl || null
                    function onCenterChanged()   { repaintDebounce.restart() }
                    function onZoomLevelChanged() { repaintDebounce.restart() }
                }

                Component.onCompleted: requestPaint()
            }

            // Name label at midpoint for named waterways
            Item {
                visible: featureName.length > 0 && coords.length >= 2 && mapControl !== null

                property var midCoord: {
                    if (!coords || coords.length < 2) return null
                    var midIdx = Math.floor(coords.length / 2)
                    return QtPositioning.coordinate(coords[midIdx].latitude, coords[midIdx].longitude)
                }

                // Reference center + zoomLevel so QML re-evaluates on pan/zoom
                property var labelPos: {
                    if (!midCoord || !mapControl) return Qt.point(0, 0)
                    var _c = mapControl.center
                    var _z = mapControl.zoomLevel
                    return mapControl.fromCoordinate(midCoord, false)
                }

                x: labelPos.x - wayLabelBg.width / 2
                y: labelPos.y - wayLabelBg.height - 4

                Rectangle {
                    id:         wayLabelBg
                    width:      wayLabel.width + _margin * 2
                    height:     wayLabel.height + _margin
                    radius:     3
                    color:      Qt.rgba(0, 0, 0, 0.6)

                    QGCLabel {
                        id:                 wayLabel
                        anchors.centerIn:   parent
                        text:               featureName
                        color:              "#64b5f6"
                        font.pointSize:     ScreenTools.smallFontPointSize * 0.85
                        font.italic:        true
                    }
                }
            }
        }
    }

    // ── Hydro Legend ──
    Rectangle {
        anchors.bottom:     parent.bottom
        anchors.left:       parent.left
        anchors.margins:    _margin * 2
        anchors.leftMargin: ScreenTools.defaultFontPixelWidth * 34
        width:              hydroLegendCol.width + _margin * 3
        height:             hydroLegendCol.height + _margin * 2
        radius:             ScreenTools.defaultFontPixelHeight * 0.3
        color:              Qt.rgba(0, 0, 0, 0.75)
        border.color:       Qt.rgba(1, 1, 1, 0.15)
        border.width:       1
        visible:            environmentalDataProvider && environmentalDataProvider.hydroDataAvailable

        ColumnLayout {
            id:                 hydroLegendCol
            anchors.centerIn:   parent
            spacing:            3

            QGCLabel {
                text:       "WATER FEATURES"
                color:      "#2196f3"
                font.bold:  true
                font.pointSize: ScreenTools.smallFontPointSize
            }

            QGCLabel {
                text:   environmentalDataProvider
                        ? environmentalDataProvider.totalWaterFeatures + " features"
                        : "0 features"
                color:  "#aaa"
                font.pointSize: ScreenTools.smallFontPointSize * 0.9
            }

            // Legend items
            Repeater {
                model: [
                    { label: "Water Body",  clr: "#2196f3", shape: "rect" },
                    { label: "Wetland",     clr: "#00acc1", shape: "rect" },
                    { label: "River",       clr: "#2196f3", shape: "line" },
                    { label: "Stream",      clr: "#64b5f6", shape: "line" }
                ]

                RowLayout {
                    spacing: _margin

                    Rectangle {
                        width:  modelData.shape === "line" ? ScreenTools.defaultFontPixelWidth * 2 : ScreenTools.defaultFontPixelHeight * 0.6
                        height: modelData.shape === "line" ? 2 : ScreenTools.defaultFontPixelHeight * 0.6
                        radius: modelData.shape === "line" ? 1 : 2
                        color:  modelData.clr
                        opacity: modelData.shape === "rect" ? 0.6 : 1.0
                    }

                    QGCLabel {
                        text:   modelData.label
                        color:  "#ccc"
                        font.pointSize: ScreenTools.smallFontPointSize * 0.85
                    }
                }
            }
        }
    }

    // ── Loading indicator ──
    Rectangle {
        anchors.centerIn:   parent
        width:              hydroLoadingLabel.width + _margin * 4
        height:             hydroLoadingLabel.height + _margin * 2
        radius:             4
        color:              Qt.rgba(0, 0, 0, 0.8)
        visible:            environmentalDataProvider && environmentalDataProvider.hydroLoading

        QGCLabel {
            id:                 hydroLoadingLabel
            anchors.centerIn:   parent
            text:               "Fetching water features..."
            color:              "#2196f3"
            font.pointSize:     ScreenTools.defaultFontPointSize
        }
    }
}
