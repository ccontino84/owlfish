// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

import QtQuick 2.0
import Sailfish.Silica 1.0

// A slider with two handles, drawn like Silica's Slider (the same public
// GlassItem pieces and sizes; Silica's own SliderBase is private). The
// handles can't cross: the lower one stops at the upper one and the other
// way round. The values are shown fixed in the row above the track, the
// lower one on the left after its icon and the upper one on the right
// before its icon; the label is under the track, as in Silica.
//
// Like Silica's Slider, the values follow the finger while dragging; set
// them from outside with a Binding element so that they keep following
// the setting.
MouseArea {
    id: slider

    property real minimumValue: 0
    property real maximumValue: 1
    property real stepSize: 1
    property real lowerValue: 0
    property real upperValue: 1
    property alias label: labelText.text
    property string lowerText
    property string upperText
    property alias lowerIcon: lowerIconImage.source
    property alias upperIcon: upperIconImage.source

    // "lower" or "upper" while a finger holds that handle, "" otherwise.
    // When both handles are in the same place, the first drag direction
    // decides, so it stays "" until then.
    readonly property string activeHandle: pressed && !_canceled ? _grabbed : ""

    property string _grabbed: ""
    property bool _canceled
    property real _pressX
    readonly property real _margin: Math.round(Screen.width / 8)
    readonly property real _grooveWidth: Math.max(0, width - 2 * _margin)
    // As Silica's: the glow on light ambiences needs more room
    readonly property real _glassPadding: Theme.paddingMedium
    readonly property real _handlePadding: (Theme.colorScheme === Theme.LightOnDark ? 1 : 2) * _glassPadding
    readonly property real _valueHeight: Math.max(lowerLabel.height, Theme.iconSizeSmallPlus)
    readonly property real _centerY: _valueHeight + Theme.paddingMedium + Theme.paddingSmall
                                     + background.height / 2
    readonly property real _lowerX: _xOf(Math.min(lowerValue, upperValue))
    readonly property real _upperX: _xOf(Math.max(lowerValue, upperValue))

    implicitHeight: labelText.y + labelText.height + Theme.paddingMedium
    width: parent ? parent.width : Screen.width

    function _xOf(value) {
        var range = maximumValue - minimumValue
        var fraction = range > 0 ? (Math.max(minimumValue, Math.min(maximumValue, value)) - minimumValue) / range : 0
        return _margin + _handlePadding + fraction * (_grooveWidth - 2 * _handlePadding)
    }

    function _valueAt(x) {
        var span = _grooveWidth - 2 * _handlePadding
        if (span <= 0)
            return minimumValue
        var value = minimumValue + (x - _margin - _handlePadding) / span * (maximumValue - minimumValue)
        if (stepSize > 0)
            value = minimumValue + Math.round((value - minimumValue) / stepSize) * stepSize
        return Math.max(minimumValue, Math.min(maximumValue, value))
    }

    // The grabbed handle to the finger, stopping at the other one
    function _moveTo(x) {
        var value = _valueAt(x)
        if (_grabbed === "lower") {
            value = Math.min(value, upperValue)
            if (value !== lowerValue)
                lowerValue = value
        } else if (_grabbed === "upper") {
            value = Math.max(value, lowerValue)
            if (value !== upperValue)
                upperValue = value
        }
    }

    onPressed: {
        _canceled = false
        _pressX = mouse.x
        // The nearer handle; none yet if they are in the same place
        if (Math.abs(_upperX - _lowerX) < 1)
            _grabbed = ""
        else
            _grabbed = Math.abs(mouse.x - _lowerX) <= Math.abs(mouse.x - _upperX) ? "lower" : "upper"
        draggable.x = Math.max(drag.minimumX, Math.min(drag.maximumX, mouse.x - draggable.width / 2))
    }
    // The finger, not the drag target: Qt moves that from where it was
    onPositionChanged: {
        if (!drag.active || _canceled)
            return
        if (_grabbed === "")
            _grabbed = mouse.x < _pressX ? "lower" : "upper"
        _moveTo(mouse.x)
    }
    onReleased: {
        if (_canceled)
            return
        // A tap moves the nearer handle there
        if (!drag.active) {
            if (_grabbed === "")
                _grabbed = mouse.x < _lowerX ? "lower" : "upper"
            _moveTo(mouse.x)
        }
        _grabbed = ""
    }
    // A flick of the page took over before the drag started
    onCanceled: {
        _canceled = true
        _grabbed = ""
    }

    // As Silica's Slider: once the finger moves sideways past the drag
    // threshold, the slider keeps it, and the page can't flick or go back
    drag {
        target: draggable
        axis: Drag.XAxis
        minimumX: _margin - draggable.width / 2
        maximumX: width - _margin - draggable.width / 2
    }

    Item {
        id: draggable

        width: Theme.itemSizeMedium
        height: Theme.itemSizeMedium
    }

    Row {
        x: Theme.horizontalPageMargin
        height: slider._valueHeight
        spacing: Theme.paddingSmall

        // Tinted like the text, as Silica tints its monochrome icons
        HighlightImage {
            id: lowerIconImage
            anchors.verticalCenter: parent.verticalCenter
            sourceSize.width: Theme.iconSizeSmallPlus
            sourceSize.height: Theme.iconSizeSmallPlus
            visible: source != ""
            color: Theme.primaryColor
            highlighted: slider.activeHandle === "lower"
        }
        Label {
            id: lowerLabel
            anchors.verticalCenter: parent.verticalCenter
            font.pixelSize: Theme.fontSizeLarge
            color: slider.activeHandle === "lower" ? Theme.highlightColor : Theme.primaryColor
            text: slider.lowerText
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: Theme.horizontalPageMargin
        height: slider._valueHeight
        spacing: Theme.paddingSmall

        Label {
            anchors.verticalCenter: parent.verticalCenter
            font.pixelSize: Theme.fontSizeLarge
            color: slider.activeHandle === "upper" ? Theme.highlightColor : Theme.primaryColor
            text: slider.upperText
        }
        // After the value: the icons at the outer edges, mirrored
        HighlightImage {
            id: upperIconImage
            anchors.verticalCenter: parent.verticalCenter
            sourceSize.width: Theme.iconSizeSmallPlus
            sourceSize.height: Theme.iconSizeSmallPlus
            visible: source != ""
            color: Theme.primaryColor
            highlighted: slider.activeHandle === "upper"
        }
    }

    GlassItem {
        id: background

        // Glass doesn't paint up to its edges, hence the extra width
        x: slider._margin - slider._glassPadding
        width: slider._grooveWidth + 2 * slider._glassPadding
        y: slider._centerY - height / 2
        height: (Theme.colorScheme === Theme.DarkOnLight ? 1.0 : 0.5) * Theme.itemSizeExtraSmall
        dimmed: true
        radius: Theme.colorScheme === Theme.DarkOnLight ? 0.06 : 0.05
        falloffRadius: Theme.colorScheme === Theme.DarkOnLight ? 0.09 : 0.05
        ratio: 0.0
        color: slider.pressed ? Theme.secondaryHighlightColor : Theme.secondaryColor
    }

    // From one handle to the other
    GlassItem {
        x: slider._lowerX - height / 2
        width: slider._upperX - slider._lowerX + height
        anchors.verticalCenter: background.verticalCenter
        height: background.height
        dimmed: false
        radius: Theme.colorScheme === Theme.DarkOnLight ? 0.05 : 0.04
        falloffRadius: Theme.colorScheme === Theme.DarkOnLight ? 0.14 : 0.10
        ratio: 0.0
        color: slider.pressed ? Theme.highlightColor : Theme.lightPrimaryColor
        backgroundColor: Theme.colorScheme === Theme.DarkOnLight ? Theme.highlightDimmerColor : "transparent"
    }

    Repeater {
        model: ["lower", "upper"]

        GlassItem {
            x: (modelData === "lower" ? slider._lowerX : slider._upperX) - width / 2
            width: Theme.itemSizeMedium
            height: Theme.itemSizeMedium
            anchors.verticalCenter: background.verticalCenter
            radius: 0.17
            falloffRadius: 0.17
            color: slider.activeHandle === modelData ? Theme.highlightColor : Theme.lightPrimaryColor
            backgroundColor: Theme.colorScheme === Theme.DarkOnLight ? Theme.highlightDimmerColor : "transparent"
        }
    }

    Label {
        id: labelText

        anchors.horizontalCenter: parent.horizontalCenter
        y: slider._centerY + Theme.paddingMedium
        width: Math.min(implicitWidth, parent.width - 2 * Theme.paddingMedium)
        truncationMode: TruncationMode.Fade
        font.pixelSize: Theme.fontSizeSmall
        color: slider.pressed ? Theme.secondaryHighlightColor : Theme.secondaryColor
    }
}
