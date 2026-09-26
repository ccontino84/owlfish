// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

import QtQuick 2.0
import Sailfish.Silica 1.0
import Nemo.Configuration 1.0
import QtSensors 5.0

// Every option is always shown in the same place; options that do not apply
// are greyed out, and their text says why
Page {
    id: page

    // Keys and defaults must match the lipstick plugin (settings.h)
    ConfigurationGroup {
        id: config

        path: "/apps/owlfish"

        property bool enabled: false
        property int temperature: 4500
        property bool schedule: false
        // Minutes after midnight
        property int schedule_from: 1260
        property int schedule_to: 420
        property int schedule_transition: 60
        property int dim: 0
        property bool dim_cutoff: true
        property int dim_cutoff_lux: 1000
        // Written by the plugin from mce's AlsValueMultiplier, so that the
        // light reading below is in the same units as the threshold
        property real als_multiplier: 1.0
    }

    // ColorTemperature::Neutral and Minimum: from no tint to no blue left
    readonly property int neutralTemperature: 6500
    readonly property int minimumTemperature: 1900
    readonly property int maximumDim: 75
    readonly property var transitionSteps: [0, 30, 60, 120]

    // Light levels span several orders of magnitude; step through them
    // logarithmically. Indoors is typically 100-500 lux, daylight indoors near
    // a window 1000-2000, outdoors in shade 10 000-25 000, in sun more.
    // Same range as OwlfishSettings::MinimumCutoffLux..MaximumCutoffLux.
    readonly property var luxSteps: [100, 150, 200, 300, 500, 700,
                                     1000, 1500, 2000, 3000, 5000, 7000,
                                     10000, 15000, 20000, 30000, 50000]

    readonly property bool dimming: config.dim > 0
    readonly property bool cutoffApplies: dimming && config.dim_cutoff

    // Minutes after midnight now, for the schedule status line
    property int nowMinutes: currentMinutes()

    // Ambient light for the meter, smoothed so that it does not jitter;
    // -1 before the first reading
    property real ambientLux: -1

    function currentMinutes() {
        var now = new Date()
        return now.getHours() * 60 + now.getMinutes()
    }

    function formatMinutes(minutes) {
        return Format.formatDate(new Date(2000, 0, 1, Math.floor(minutes / 60), minutes % 60),
                                 Formatter.TimeValue)
    }

    function pickTime(property) {
        var minutes = config[property]
        var dialog = pageStack.push("Sailfish.Silica.TimePickerDialog", {
                                        hour: Math.floor(minutes / 60),
                                        minute: minutes % 60
                                    })
        dialog.accepted.connect(function() {
            config[property] = dialog.hour * 60 + dialog.minute
        })
    }

    // Same window as OwlfishSchedule: may cross midnight, empty if the
    // start and end are equal
    function scheduleStatus() {
        if (!config.schedule)
            return "Warm whenever Owlfish is enabled."
        var day = 24 * 60
        var length = (config.schedule_to - config.schedule_from + day) % day
        if (length === 0)
            return "The start and end are the same, so the colour stays neutral."
        var into = (nowMinutes - config.schedule_from + day) % day
        if (into < length)
            return "Warm now; neutral from " + formatMinutes(config.schedule_to) + "."
        return "Neutral now; warms up from " + formatMinutes(config.schedule_from) + "."
    }

    function luxStepIndex(lux) {
        var best = 0
        for (var i = 1; i < luxSteps.length; ++i) {
            if (Math.abs(Math.log(luxSteps[i] / lux)) < Math.abs(Math.log(luxSteps[best] / lux)))
                best = i
        }
        return best
    }

    // Position of a light level on the threshold slider's scale, between its
    // logarithmic steps
    function luxPosition(lux) {
        if (lux <= luxSteps[0])
            return 0
        for (var i = 1; i < luxSteps.length; ++i) {
            if (lux <= luxSteps[i])
                return i - 1 + Math.log(lux / luxSteps[i - 1]) / Math.log(luxSteps[i] / luxSteps[i - 1])
        }
        return luxSteps.length - 1
    }

    function formatLux(lux) {
        return Math.round(lux).toLocaleString(Qt.locale(), "f", 0) + " lux"
    }

    // Two significant digits: a meter, not a measurement
    function roundLux(lux) {
        var magnitude = Math.pow(10, Math.max(0, Math.floor(Math.log(lux) / Math.LN10) - 1))
        return Math.round(lux / magnitude) * magnitude
    }

    Timer {
        interval: 30000
        repeat: true
        running: page.status === PageStatus.Active && Qt.application.active
        triggeredOnStart: true
        onTriggered: page.nowMinutes = page.currentMinutes()
    }

    // Live reading to help pick the threshold; only while the page is shown
    LightSensor {
        id: lightSensor
        active: page.status === PageStatus.Active && Qt.application.active
    }

    // The sensor can report many times per second; sample it once a second,
    // move halfway towards the new level in log terms, and ignore changes
    // under 10 %
    Timer {
        interval: 1000
        repeat: true
        running: lightSensor.active
        triggeredOnStart: true
        onTriggered: {
            if (!lightSensor.reading)
                return
            var lux = Math.max(1, lightSensor.reading.illuminance * (config.als_multiplier || 1.0))
            if (page.ambientLux < 0) {
                page.ambientLux = lux
                return
            }
            var next = Math.sqrt(page.ambientLux * lux)
            if (Math.abs(Math.log(next / page.ambientLux)) > 0.1)
                page.ambientLux = next
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        VerticalScrollDecorator {}

        Column {
            id: column

            width: parent.width

            PageHeader {
                title: "Owlfish"
            }

            TextSwitch {
                text: "Enabled"
                description: "Also available as a top menu shortcut; add it in Settings > Top menu."
                checked: config.enabled
                automaticCheck: false
                onClicked: config.enabled = !checked
            }

            SectionHeader {
                text: "Colour temperature"
            }

            Slider {
                width: parent.width
                label: "Warmth"
                // Left is no tint, right is no blue left, in 100 K steps
                minimumValue: 0
                maximumValue: (page.neutralTemperature - page.minimumTemperature) / 100
                stepSize: 1
                value: (page.neutralTemperature - config.temperature) / 100
                valueText: {
                    var kelvin = page.neutralTemperature - Math.round(sliderValue) * 100
                    if (kelvin === page.neutralTemperature)
                        return kelvin + " K · no tint"
                    if (kelvin === page.minimumTemperature)
                        return kelvin + " K · no blue"
                    return kelvin + " K"
                }
                // Applies while dragging, so the effect can be judged live
                onSliderValueChanged: {
                    var kelvin = page.neutralTemperature - Math.round(sliderValue) * 100
                    if (kelvin !== config.temperature)
                        config.temperature = kelvin
                }
            }

            TextSwitch {
                text: "Only at night"
                description: "Warm only between the start and end times below; neutral the rest of the day."
                checked: config.schedule
                automaticCheck: false
                onClicked: config.schedule = !checked
            }

            ValueButton {
                enabled: config.schedule
                opacity: enabled ? 1.0 : Theme.opacityLow
                label: "Start"
                value: page.formatMinutes(config.schedule_from)
                onClicked: page.pickTime("schedule_from")
            }

            ValueButton {
                enabled: config.schedule
                opacity: enabled ? 1.0 : Theme.opacityLow
                label: "End"
                value: page.formatMinutes(config.schedule_to)
                onClicked: page.pickTime("schedule_to")
            }

            ComboBox {
                enabled: config.schedule
                opacity: enabled ? 1.0 : Theme.opacityLow
                label: "Gradual change"
                description: "Warms up gradually from the start time, and is back to neutral at the end time."
                currentIndex: Math.max(0, page.transitionSteps.indexOf(config.schedule_transition))

                menu: ContextMenu {
                    // Same order as page.transitionSteps
                    MenuItem { text: "Off"; onClicked: config.schedule_transition = 0 }
                    MenuItem { text: "30 minutes"; onClicked: config.schedule_transition = 30 }
                    MenuItem { text: "1 hour"; onClicked: config.schedule_transition = 60 }
                    MenuItem { text: "2 hours"; onClicked: config.schedule_transition = 120 }
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: page.scheduleStatus()
            }

            SectionHeader {
                text: "Dimming"
            }

            Slider {
                width: parent.width
                label: "Darker than the current brightness"
                minimumValue: 0
                maximumValue: page.maximumDim
                stepSize: 5
                value: config.dim
                valueText: Math.round(sliderValue) === 0 ? "Off" : Math.round(sliderValue) + " %"
                onSliderValueChanged: {
                    var dim = Math.round(sliderValue)
                    if (dim !== config.dim)
                        config.dim = dim
                }
            }

            TextSwitch {
                enabled: page.dimming
                text: "Only in the dark"
                description: "Uses the light sensor to skip the dimming in bright light, for example outdoors or in a well-lit room, and dims again when it gets darker. Applies when dimming is set above."
                checked: config.dim_cutoff
                automaticCheck: false
                onClicked: config.dim_cutoff = !checked
            }

            Slider {
                id: thresholdSlider

                enabled: page.cutoffApplies
                opacity: enabled ? 1.0 : Theme.opacityLow
                width: parent.width
                label: "Bright light starts at"
                minimumValue: 0
                maximumValue: page.luxSteps.length - 1
                stepSize: 1
                value: page.luxStepIndex(config.dim_cutoff_lux)
                valueText: page.formatLux(page.luxSteps[Math.round(sliderValue)])
                onSliderValueChanged: {
                    var lux = page.luxSteps[Math.round(sliderValue)]
                    if (lux !== config.dim_cutoff_lux)
                        config.dim_cutoff_lux = lux
                }
            }

            // Read-only meter on the same scale as the threshold above, so
            // the two can be compared directly
            Slider {
                enabled: false
                opacity: page.cutoffApplies ? 1.0 : Theme.opacityLow
                width: parent.width
                label: "Light here now"
                minimumValue: 0
                maximumValue: page.luxSteps.length - 1
                value: page.ambientLux < 0 ? 0 : page.luxPosition(page.ambientLux)
                valueText: page.ambientLux < 0 ? "Not available"
                                               : page.formatLux(page.roundLux(page.ambientLux))
            }

            Label {
                opacity: page.cutoffApplies ? 1.0 : Theme.opacityLow
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                // The plugin dims again below 75 % of the threshold, so that
                // light hovering around it does not toggle the dimming
                // (AmbientCutoff::ResumeRatio)
                text: {
                    var threshold = page.luxSteps[Math.round(thresholdSlider.sliderValue)]
                    return "Stops dimming above " + page.formatLux(threshold)
                            + " and dims again below " + page.formatLux(threshold * 0.75) + "."
                }
            }
        }
    }
}
