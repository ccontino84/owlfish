// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

import QtQuick 2.0
import Sailfish.Silica 1.0
import Nemo.Configuration 1.0
import Nemo.DBus 2.0
import QtSensors 5.0
import "defaults.js" as Defaults

// Options that only matter after a choice on this page are hidden until it
// is made (the times with "Fixed times", the location with "Sunset to
// sunrise"). Options that something else rules out, such as the device,
// stay in place, greyed out, and their text says why.
Page {
    id: page

    // Keys and defaults must match the lipstick plugin (settings.h); the
    // defaults are in defaults.js
    ConfigurationGroup {
        id: config

        path: "/apps/owlfish"

        property bool enabled: Defaults.values.enabled
        property int temperature: Defaults.values.temperature
        // Percent; 100 is no change, 0 grey
        property int saturation: Defaults.values.saturation
        property bool schedule: Defaults.values.schedule
        // Sunset to sunrise instead of the fixed times
        property bool schedule_sun: Defaults.values.schedule_sun
        // Minutes after midnight
        property int schedule_from: Defaults.values.schedule_from
        property int schedule_to: Defaults.values.schedule_to
        property int schedule_transition: Defaults.values.schedule_transition
        // Typed coordinates instead of the time zone's city; out of range
        // until set
        property bool location_manual: Defaults.values.location_manual
        property real latitude: Defaults.values.latitude
        property real longitude: Defaults.values.longitude
        property int dim: Defaults.values.dim
        // "always", "fixed" (dim_from to dim_to) or "night_light"
        property string dim_when: Defaults.values.dim_when
        property int dim_from: Defaults.values.dim_from
        property int dim_to: Defaults.values.dim_to
        property bool dim_cutoff: Defaults.values.dim_cutoff
        property int dim_cutoff_lux: Defaults.values.dim_cutoff_lux
        // "none", "protan", "deutan", "tritan" or "greyscale" (Correction in
        // the plugin)
        property string correction: Defaults.values.correction
        property int correction_strength: Defaults.values.correction_strength
        // Written by the plugin from mce's AlsValueMultiplier, so that the
        // light reading below is in the same units as the threshold
        property real als_multiplier: 1.0
        // Written by the plugin: the most the light sensor can report, in
        // the same units; 0 if not known
        property real als_max_lux: 0
        // Written by the plugin, which reads the time zone: its city (empty
        // if it has none) and that city's coordinates
        property string sun_place: ""
        property real auto_latitude: 1000
        property real auto_longitude: 1000
        // Today's sun for the location in use: "normal", "polar_day",
        // "polar_night" or "no_location"; empty until the plugin has run.
        // The times are minutes after midnight.
        property string sun_state: ""
        property int sun_set: -1
        property int sun_rise: -1
        property real sun_latitude: 0
    }

    // The sliders' and menus' values. Silica's Slider and ComboBox replace a
    // plain binding when the user moves or picks; these keep following the
    // settings (Reset to defaults, or a change from elsewhere).
    Binding { target: warmthSlider; property: "value"; value: (page.neutralTemperature - config.temperature) / 100 }
    Binding { target: saturationSlider; property: "value"; value: config.saturation }
    Binding { target: whenCombo; property: "currentIndex"; value: page.whenIndex }
    Binding { target: transitionCombo; property: "currentIndex"; value: Math.max(0, page.transitionSteps.indexOf(config.schedule_transition)) }
    Binding { target: dimSlider; property: "value"; value: config.dim }
    Binding { target: dimWhenCombo; property: "currentIndex"; value: Math.max(0, page.dimWhenNames.indexOf(config.dim_when)) }
    Binding { target: thresholdSlider; property: "value"; value: page.luxStepIndex(config.dim_cutoff_lux) }
    Binding { target: correctionCombo; property: "currentIndex"; value: page.correctionNames.indexOf(page.correction) }
    Binding { target: strengthSlider; property: "value"; value: config.correction_strength }

    // ColorTemperature::Neutral and Minimum: from no tint to no blue left
    readonly property int neutralTemperature: 6500
    readonly property int minimumTemperature: 1900
    readonly property int maximumDim: 75
    readonly property var transitionSteps: [0, 30, 60, 120]

    // Light levels span several orders of magnitude; step through them
    // logarithmically. A dark room is about 1 lux, a dim one 10-50, indoors
    // 100-500, daylight indoors near a window 1000-2000, outdoors in shade
    // 10 000-25 000, in sun more. Same range as
    // OwlfishSettings::MinimumCutoffLux..MaximumCutoffLux; coarser below 100.
    readonly property var allLuxSteps: [1, 10, 50,
                                        100, 150, 200, 300, 500, 700,
                                        1000, 1500, 2000, 3000, 5000, 7000,
                                        10000, 15000, 20000, 30000, 50000]
    // Only the levels the sensor can report: above its maximum a threshold
    // would never be reached (the Jolla Phone's stops near 2200 lux)
    readonly property var luxSteps: {
        var maximum = config.als_max_lux
        if (!(maximum > 0))
            return allLuxSteps
        var steps = allLuxSteps.filter(function(lux) { return lux <= maximum })
        return steps.length >= 2 ? steps : allLuxSteps
    }

    // Index of the "When" choices: all the time, fixed times, sunset to sunrise
    readonly property int whenIndex: !config.schedule ? 0 : (config.schedule_sun ? 2 : 1)
    readonly property bool sunSchedule: whenIndex === 2
    readonly property bool hasAutoLocation: config.sun_place !== ""
    readonly property bool hasManualLocation: validCoordinates(config.latitude, config.longitude)
    // The same choice the plugin makes
    readonly property bool usesManualLocation: config.location_manual && hasManualLocation

    readonly property bool dimming: config.dim > 0
    readonly property bool cutoffApplies: dimming && config.dim_cutoff
    // Same order as the dimming's "When" menu
    readonly property var dimWhenNames: ["always", "fixed", "night_light"]

    // OwlfishController::renderer() of the running plugin: "pq" or "qualcomm"
    // (display hardware), "fetch" or "blend" (GPU), "none" before it has a
    // window; empty without a reply
    property string pluginRenderer: ""
    // Only the display hardware and fetch (GPU) can change the saturation
    // and correct colours. "blend" means fetch doesn't work or is turned
    // off. Unknown counts as possible, so the settings stay editable while
    // Owlfish isn't running.
    readonly property bool matrixSupported: pluginRenderer !== "blend"
    // Same order as the correction menu
    readonly property var correctionNames: ["none", "protan", "deutan", "tritan", "greyscale"]
    // Anything else, such as a pre-release's name, is none, as in the plugin
    readonly property string correction: correctionNames.indexOf(config.correction) >= 0
                                         ? config.correction : "none"

    // Minutes after midnight now, for the schedule status line
    property int nowMinutes: currentMinutes()

    // Ambient light for the meter, smoothed so that it does not jitter;
    // -1 before the first reading
    property real ambientLux: -1
    // The latest level from the sensor, in lux; -1 until one arrives
    property real latestLux: -1
    // A reading arrived since the sensor started, so sensorfw's stored level
    // is no longer needed
    property bool sensorReported: false

    // Whether the plugin runs in the home screen, checked once when the page
    // opens: OwlfishController::status() ("active", "starting", "no-window",
    // "crash-guard"), or without a reply "not-running" or "unsupported"; empty
    // until known
    property string pluginStatus: ""
    // Of the running plugin; empty when it does not reply
    property string pluginVersion: ""
    readonly property string pluginProblem: {
        switch (pluginStatus) {
        case "unsupported":
            //% "Not supported on this device."
            return qsTrId("owlfish-la-unsupported")
        case "not-running":
            //% "Not running. Restart the phone to start it."
            return qsTrId("owlfish-la-not_running")
        case "crash-guard":
            //% "Turned off after the home screen failed to start."
            return qsTrId("owlfish-la-crash_guard")
        case "no-window":
            //% "Couldn't attach to the home screen."
            return qsTrId("owlfish-la-no_window")
        }
        return ""
    }

    // No reply: the package only writes the environment file that loads the
    // plugin when the device does not load plugins of its own the same way
    // (config/update-env). Since 1.6 the file names the loader
    // (lipstickaddons), before that owlfish.
    function checkEnvironmentFile() {
        var request = new XMLHttpRequest()
        request.onreadystatechange = function() {
            if (request.readyState === XMLHttpRequest.DONE) {
                page.pluginStatus = request.responseText.indexOf("QT_QPA_GENERIC_PLUGINS") >= 0
                        ? "not-running" : "unsupported"
            }
        }
        request.open("GET", "file:///var/lib/environment/compositor/00-owlfish.conf")
        request.send()
    }

    // The plugin's diagnostics, or fallbackDiagnostics()
    function collectDiagnostics(done) {
        plugin.typedCall("diagnostics", [],
                         function(text) { done(text) },
                         function() {
                             // The plugin's system line, from os-release
                             var request = new XMLHttpRequest()
                             request.onreadystatechange = function() {
                                 if (request.readyState === XMLHttpRequest.DONE)
                                     done(fallbackDiagnostics(request.responseText || ""))
                             }
                             request.open("GET", "file:///etc/os-release")
                             request.send()
                         })
    }

    // What the page knows when the plugin doesn't reply (not running, or
    // older than 1.2)
    function fallbackDiagnostics(osRelease) {
        var lines = []
        if (page.pluginVersion !== "")
            lines.push("version " + page.pluginVersion)
        var system = /^PRETTY_NAME="?([^"\n]*)/m.exec(osRelease)
        lines.push("system " + (system && system[1] !== "" ? system[1] : "unknown"))
        switch (page.pluginStatus) {
        case "unsupported":
            lines.push("status unsupported (00-owlfish.conf missing)")
            break
        case "not-running":
            lines.push("status not-running (00-owlfish.conf present)")
            break
        default:
            lines.push("status " + (page.pluginStatus || "unknown"))
            lines.push("no diagnostics from this version")
        }
        return lines.join("\n")
    }

    // Every key back to its default: the page's own values first (an unset
    // key doesn't change them), then everything under the path, hidden and
    // old keys too. The plugin then reads its settings again and works out
    // its own keys (light sensor, sun times) from scratch; if it isn't
    // running, it does that when it starts.
    function resetSettings() {
        for (var key in Defaults.values)
            config[key] = Defaults.values[key]
        config.clear()
        config.sync()
        plugin.typedCall("reload", [])
    }

    function openDiagnostics() {
        collectDiagnostics(function(text) {
            pageStack.push(Qt.resolvedUrl("DiagnosticsPage.qml"), { text: text })
        })
    }

    // A new GitHub issue in the browser, from the bug report form
    // (.github/ISSUE_TEMPLATE/bug_report.yml) with the diagnostics filled in;
    // the user writes the rest and submits it there
    function reportBug() {
        collectDiagnostics(function(diagnostics) {
            Qt.openUrlExternally("https://github.com/ccontino84/owlfish/issues/new"
                                 + "?template=bug_report.yml"
                                 + "&diagnostics=" + encodeURIComponent(diagnostics))
        })
    }

    Component.onCompleted: {
        plugin.typedCall("status", [],
                         function(status) {
                             page.pluginStatus = status
                             plugin.typedCall("version", [],
                                              function(version) { page.pluginVersion = version })
                             plugin.typedCall("renderer", [],
                                              function(renderer) { page.pluginRenderer = renderer })
                         },
                         function() { page.checkEnvironmentFile() })
    }

    DBusInterface {
        id: plugin

        // OwlfishStatusService, on the session bus
        service: "io.github.ccontino84.owlfish"
        path: "/owlfish"
        iface: "io.github.ccontino84.owlfish"
    }

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
            //% "Warm whenever Owlfish is enabled."
            return qsTrId("owlfish-la-warm_always")
        var from = config.schedule_from
        var to = config.schedule_to
        if (config.schedule_sun) {
            if (config.sun_state === "polar_day")
                //% "Neutral all day today."
                return qsTrId("owlfish-la-neutral_today")
            if (config.sun_state === "polar_night")
                //% "Warm all day today."
                return qsTrId("owlfish-la-warm_today")
            if (config.sun_state !== "normal")
                //% "Neutral until sunset and sunrise are known."
                return qsTrId("owlfish-la-neutral_until_sun")
            from = config.sun_set
            to = config.sun_rise
        }
        var day = 24 * 60
        var length = (to - from + day) % day
        if (length === 0)
            //% "The start and end are the same, so the colour stays neutral."
            return qsTrId("owlfish-la-same_times")
        var into = (nowMinutes - from + day) % day
        if (into < length)
            //: %1 is a time of day
            //% "Warm now; neutral from %1."
            return qsTrId("owlfish-la-warm_now").arg(formatMinutes(to))
        //: %1 is a time of day
        //% "Neutral now; warms up from %1."
        return qsTrId("owlfish-la-neutral_now").arg(formatMinutes(from))
    }

    function formatPercent(percent) {
        //: A percentage
        //% "%1 %"
        return qsTrId("owlfish-la-percent").arg(percent)
    }

    function formatDuration(minutes) {
        var hours = Math.floor(minutes / 60)
        if (hours === 0)
            //: A duration in minutes
            //% "%1 min"
            return qsTrId("owlfish-la-minutes").arg(minutes)
        if (minutes % 60 === 0)
            //: A duration in hours
            //% "%1 h"
            return qsTrId("owlfish-la-hours").arg(hours)
        //: A duration in hours and minutes
        //% "%1 h %2 min"
        return qsTrId("owlfish-la-hours_minutes").arg(hours).arg(minutes % 60)
    }

    // Today's sunset and sunrise, as the plugin calculated them
    function sunStatus() {
        switch (config.sun_state) {
        case "":
            //% "Sunset and sunrise appear once Owlfish is running."
            return qsTrId("owlfish-la-sun_unknown")
        case "no_location":
            //% "Your time zone has no location. Set it manually below."
            return qsTrId("owlfish-la-sun_no_location")
        case "polar_day":
            //% "The sun doesn't set today, so the colour stays neutral."
            return qsTrId("owlfish-la-polar_day")
        case "polar_night":
            //% "The sun doesn't rise today, so the colour stays warm all day."
            return qsTrId("owlfish-la-polar_night")
        }

        //: %1 is the time zone's city, in English
        //% "%1, from your time zone"
        var where = page.usesManualLocation
                ? formatCoordinates(config.latitude, config.longitude)
                : qsTrId("owlfish-la-zone_city").arg(config.sun_place)
        //: %1 and %2 are times of day; %3 is the place
        //% "Today: sunset %1, sunrise %2 (%3)."
        var lines = [qsTrId("owlfish-la-sun_times").arg(formatMinutes(config.sun_set))
                     .arg(formatMinutes(config.sun_rise)).arg(where)]

        // OwlfishSchedule shortens the transitions to fit
        var night = (config.sun_rise - config.sun_set + 24 * 60) % (24 * 60)
        if (config.schedule_transition > 0 && night < 2 * config.schedule_transition)
            //: %1 is a duration, such as "1 h 20 min"
            //% "Tonight is only %1 long; the change is quicker to fit."
            lines.push(qsTrId("owlfish-la-short_night").arg(formatDuration(night)))

        // Near the polar circles the sun crosses the horizon at a shallow
        // angle, so small errors move the times a lot
        if (Math.abs(config.sun_latitude) >= 60) {
            lines.push(config.sun_latitude > 0
                       //% "This far north the times are approximate: sunset and sunrise can shift by several minutes."
                       ? qsTrId("owlfish-la-far_north")
                       //% "This far south the times are approximate: sunset and sunrise can shift by several minutes."
                       : qsTrId("owlfish-la-far_south"))
            if (!page.usesManualLocation)
                //% "The time zone's city may also be far from you."
                lines.push(qsTrId("owlfish-la-city_far"))
            //% "Fixed times give a steadier routine."
            lines.push(qsTrId("owlfish-la-fixed_steadier"))
        }
        return lines.join(" ")
    }

    function validCoordinates(latitude, longitude) {
        return Math.abs(latitude) <= 90 && Math.abs(longitude) <= 180
    }

    function formatCoordinates(latitude, longitude) {
        return latitude.toFixed(2) + ", " + longitude.toFixed(2)
    }

    // As map apps and websites copy them ("60.1699, 24.9384"), also with
    // decimal commas, degree signs, or N/S/E/W instead of signs. Returns
    // [latitude, longitude] rounded to 2 decimals (about 1 km), or null.
    function parseCoordinates(text) {
        var number = "([+\\-\u2212]?)(\\d+(?:[.,]\\d+)?)\\s*°?\\s*([NSEWnsew]?)"
        var match = new RegExp("^\\s*" + number + "(?:\\s*[,;]\\s*|\\s+)" + number + "\\s*$").exec(text)
        if (!match)
            return null

        function angle(sign, digits, hemisphere, positive, negative) {
            hemisphere = hemisphere.toUpperCase()
            if (hemisphere !== "" && hemisphere !== positive && hemisphere !== negative)
                return NaN
            var value = parseFloat(digits.replace(",", "."))
            if (sign !== "" && sign !== "+")
                value = -value
            if (hemisphere === negative)
                value = -value
            return Math.round(value * 100) / 100
        }
        var latitude = angle(match[1], match[2], match[3], "N", "S")
        var longitude = angle(match[4], match[5], match[6], "E", "W")
        return validCoordinates(latitude, longitude) ? [latitude, longitude] : null
    }

    function setLocationManual(manual) {
        // The first time, start from the time zone's coordinates; afterwards
        // from the last ones typed, which are kept while switched off
        if (manual && !page.hasManualLocation && page.hasAutoLocation) {
            config.latitude = Math.round(config.auto_latitude * 100) / 100
            config.longitude = Math.round(config.auto_longitude * 100) / 100
        }
        config.location_manual = manual
    }

    function saveCoordinates() {
        var coordinates = parseCoordinates(coordinatesField.text)
        if (!coordinates)
            return
        config.latitude = coordinates[0]
        config.longitude = coordinates[1]
        coordinatesField.text = coordinatesField.savedText
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
        //: A light level
        //% "%1 lux"
        return qsTrId("owlfish-la-lux").arg(Math.round(lux).toLocaleString(Qt.locale(), "f", 0))
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

    // Live reading to help pick the threshold; only while the page is shown.
    // Its reading holds garbage until the first one arrives (Qt 5.6), so only
    // readings that arrived count.
    LightSensor {
        id: lightSensor
        active: page.status === PageStatus.Active && Qt.application.active

        onActiveChanged: {
            if (!active)
                return
            page.sensorReported = false
            // sensorfw sends nothing to a new session until the level
            // changes; ask for the one it has, as the plugin does
            sensorService.typedCall("lux", [], function(result) {
                // (timestamp, value)
                var raw = Array.isArray(result) ? result[1] : undefined
                if (!page.sensorReported && typeof raw === "number")
                    page.latestLux = raw * (config.als_multiplier || 1.0)
            })
        }
        onReadingChanged: {
            page.sensorReported = true
            page.latestLux = reading.illuminance * (config.als_multiplier || 1.0)
        }
    }

    DBusInterface {
        id: sensorService

        bus: DBus.SystemBus
        service: "com.nokia.SensorService"
        path: "/SensorManager/alssensor"
        iface: "local.ALSSensor"
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
            if (page.latestLux < 0)
                return
            var lux = Math.max(1, page.latestLux)
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

        PullDownMenu {
            MenuItem {
                //% "Reset to defaults"
                text: qsTrId("owlfish-me-reset_defaults")
                onClicked: Remorse.popupAction(page, qsTrId("owlfish-me-reset_defaults"), page.resetSettings)
            }
            MenuItem {
                //: Opens a bug report on GitHub in the browser
                //% "Report a bug"
                text: qsTrId("owlfish-me-report_bug")
                onClicked: page.reportBug()
            }
        }

        Column {
            id: column

            width: parent.width

            PageHeader {
                title: "Owlfish"
                //% "Version %1"
                description: page.pluginVersion !== "" ? qsTrId("owlfish-la-version").arg(page.pluginVersion) : ""

                // Hidden, for support
                MouseArea {
                    anchors.fill: parent
                    onDoubleClicked: page.openDiagnostics()
                }
            }

            // Only when there is a problem; the settings stay editable
            Column {
                visible: page.pluginProblem !== ""
                width: parent.width
                spacing: Theme.paddingMedium

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    color: Theme.highlightColor
                    text: page.pluginProblem
                }

                Button {
                    visible: page.pluginStatus === "crash-guard"
                    anchors.horizontalCenter: parent.horizontalCenter
                    //: Lets Owlfish start again after it turned itself off
                    //% "Reset"
                    text: qsTrId("owlfish-bt-reset")
                    onClicked: plugin.typedCall("resetCrashGuard", [],
                                                function() { page.pluginStatus = "not-running" })
                }

                Item {
                    width: 1
                    height: Theme.paddingMedium
                }
            }

            TextSwitch {
                //% "Enabled"
                text: qsTrId("owlfish-la-enabled")
                //: "Settings > Top menu" as the phone names them
                //% "Also available as a top menu shortcut; add it in Settings > Top menu."
                description: qsTrId("owlfish-la-enabled_description")
                checked: config.enabled
                automaticCheck: false
                onClicked: config.enabled = !checked
            }

            SectionHeader {
                //% "Night light"
                text: qsTrId("owlfish-he-night_light")
            }

            Slider {
                id: warmthSlider
                width: parent.width
                //% "Warmth"
                label: qsTrId("owlfish-la-warmth")
                // Left is no tint, right is no blue left, in 100 K steps
                minimumValue: 0
                maximumValue: (page.neutralTemperature - page.minimumTemperature) / 100
                stepSize: 1
                valueText: {
                    var kelvin = page.neutralTemperature - Math.round(sliderValue) * 100
                    if (kelvin === page.neutralTemperature)
                        //: %1 is a colour temperature in kelvin
                        //% "%1 K · no tint"
                        return qsTrId("owlfish-la-kelvin_no_tint").arg(kelvin)
                    if (kelvin === page.minimumTemperature)
                        //: %1 is a colour temperature in kelvin
                        //% "%1 K · no blue"
                        return qsTrId("owlfish-la-kelvin_no_blue").arg(kelvin)
                    return kelvin + " K"
                }
                // Applies while dragging, so the effect can be judged live
                onSliderValueChanged: {
                    var kelvin = page.neutralTemperature - Math.round(sliderValue) * 100
                    if (kelvin !== config.temperature)
                        config.temperature = kelvin
                }
            }

            Slider {
                id: saturationSlider
                enabled: page.matrixSupported
                opacity: enabled ? 1.0 : Theme.opacityLow
                width: parent.width
                //% "Saturation"
                label: qsTrId("owlfish-la-saturation")
                minimumValue: 0
                maximumValue: 100
                stepSize: 5
                valueText: {
                    var saturation = Math.round(sliderValue)
                    if (saturation === 100)
                        //% "100 % · no change"
                        return qsTrId("owlfish-la-saturation_full")
                    if (saturation === 0)
                        //% "0 % · grey"
                        return qsTrId("owlfish-la-saturation_grey")
                    return page.formatPercent(saturation)
                }
                onSliderValueChanged: {
                    var saturation = Math.round(sliderValue)
                    if (saturation !== config.saturation)
                        config.saturation = saturation
                }
            }

            Label {
                visible: !page.matrixSupported
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: qsTrId("owlfish-la-unsupported")
            }

            ComboBox {
                id: whenCombo
                //: When Night light warms the colours
                //% "When"
                label: qsTrId("owlfish-la-when")

                menu: ContextMenu {
                    // Same order as page.whenIndex
                    MenuItem {
                        //% "All the time"
                        text: qsTrId("owlfish-me-always")
                        onClicked: config.schedule = false
                    }
                    MenuItem {
                        //% "Fixed times"
                        text: qsTrId("owlfish-me-fixed_times")
                        onClicked: {
                            config.schedule_sun = false
                            config.schedule = true
                        }
                    }
                    MenuItem {
                        //% "Sunset to sunrise"
                        text: qsTrId("owlfish-me-sunset_sunrise")
                        onClicked: {
                            config.schedule_sun = true
                            config.schedule = true
                        }
                    }
                }
            }

            ValueButton {
                visible: page.whenIndex === 1
                //: Start time
                //% "Start"
                label: qsTrId("owlfish-la-start")
                value: page.formatMinutes(config.schedule_from)
                onClicked: page.pickTime("schedule_from")
            }

            ValueButton {
                visible: page.whenIndex === 1
                //: End time
                //% "End"
                label: qsTrId("owlfish-la-end")
                value: page.formatMinutes(config.schedule_to)
                onClicked: page.pickTime("schedule_to")
            }

            Label {
                visible: page.sunSchedule
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: page.sunStatus()
            }

            // Silica has no checkbox; the switch sits above the field
            // because both do not fit on one row
            TextSwitch {
                visible: page.sunSchedule
                //% "Set location manually"
                text: qsTrId("owlfish-la-location_manual")
                //% "For sunset to sunrise: your own coordinates instead of your time zone's city."
                description: qsTrId("owlfish-la-location_manual_description")
                checked: config.location_manual
                automaticCheck: false
                onClicked: page.setLocationManual(!checked)
            }

            TextField {
                id: coordinatesField

                // What is stored, as shown when not editing
                readonly property string savedText: page.usesManualLocation
                        ? page.formatCoordinates(config.latitude, config.longitude)
                        : (!config.location_manual && page.hasAutoLocation
                           ? page.formatCoordinates(config.auto_latitude, config.auto_longitude) : "")
                readonly property bool editable: page.sunSchedule && config.location_manual
                readonly property bool valid: page.parseCoordinates(text) !== null

                // The time zone's city is in the line above
                visible: editable
                width: parent.width
                //% "Coordinates"
                label: qsTrId("owlfish-la-coordinates")
                //% "Latitude, longitude"
                placeholderText: qsTrId("owlfish-ph-coordinates")
                inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                errorHighlight: editable && text !== "" && !valid
                description: {
                    if (errorHighlight)
                        //% "Not valid coordinates"
                        return qsTrId("owlfish-la-coordinates_invalid")
                    if (config.location_manual)
                        //% "Copy them from a map app or website. They stay on this device."
                        return qsTrId("owlfish-la-coordinates_hint")
                    return page.hasAutoLocation
                            //: %1 is the time zone's city, in English
                            //% "From your time zone (%1)."
                            ? qsTrId("owlfish-la-coordinates_zone").arg(config.sun_place)
                            //% "Your time zone has no location."
                            : qsTrId("owlfish-la-zone_no_location")
                }

                onSavedTextChanged: {
                    if (!activeFocus)
                        text = savedText
                }
                Component.onCompleted: text = savedText
                onActiveFocusChanged: {
                    if (!activeFocus)
                        page.saveCoordinates()
                }

                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false
            }

            ComboBox {
                id: transitionCombo
                visible: config.schedule
                //% "Gradual change"
                label: qsTrId("owlfish-la-transition")
                //% "Warms up gradually from the start time or sunset, and is back to neutral at the end time or sunrise."
                description: qsTrId("owlfish-la-transition_description")

                menu: ContextMenu {
                    // Same order as page.transitionSteps
                    MenuItem {
                        //% "Off"
                        text: qsTrId("owlfish-me-off")
                        onClicked: config.schedule_transition = 0
                    }
                    MenuItem {
                        //% "30 minutes"
                        text: qsTrId("owlfish-me-30_minutes")
                        onClicked: config.schedule_transition = 30
                    }
                    MenuItem {
                        //% "1 hour"
                        text: qsTrId("owlfish-me-1_hour")
                        onClicked: config.schedule_transition = 60
                    }
                    MenuItem {
                        //% "2 hours"
                        text: qsTrId("owlfish-me-2_hours")
                        onClicked: config.schedule_transition = 120
                    }
                }
            }

            Label {
                visible: config.schedule
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: page.scheduleStatus()
            }

            SectionHeader {
                //% "Dimming"
                text: qsTrId("owlfish-he-dimming")
            }

            Slider {
                id: dimSlider
                width: parent.width
                //% "Darker than the current brightness"
                label: qsTrId("owlfish-la-dim")
                minimumValue: 0
                maximumValue: page.maximumDim
                stepSize: 5
                valueText: Math.round(sliderValue) === 0 ? qsTrId("owlfish-me-off") : page.formatPercent(Math.round(sliderValue))
                onSliderValueChanged: {
                    var dim = Math.round(sliderValue)
                    if (dim !== config.dim)
                        config.dim = dim
                }
            }

            ComboBox {
                id: dimWhenCombo
                visible: page.dimming
                label: qsTrId("owlfish-la-when")

                menu: ContextMenu {
                    // Same order as page.dimWhenNames
                    MenuItem {
                        text: qsTrId("owlfish-me-always")
                        onClicked: config.dim_when = "always"
                    }
                    MenuItem {
                        text: qsTrId("owlfish-me-fixed_times")
                        onClicked: config.dim_when = "fixed"
                    }
                    MenuItem {
                        //% "With Night light"
                        text: qsTrId("owlfish-me-with_night_light")
                        onClicked: config.dim_when = "night_light"
                    }
                }
            }

            ValueButton {
                visible: page.dimming && config.dim_when === "fixed"
                label: qsTrId("owlfish-la-start")
                value: page.formatMinutes(config.dim_from)
                onClicked: page.pickTime("dim_from")
            }

            ValueButton {
                visible: page.dimming && config.dim_when === "fixed"
                label: qsTrId("owlfish-la-end")
                value: page.formatMinutes(config.dim_to)
                onClicked: page.pickTime("dim_to")
            }

            TextSwitch {
                visible: page.dimming
                //% "Only in the dark"
                text: qsTrId("owlfish-la-only_dark")
                //% "Uses the light sensor to skip the dimming in bright light, for example outdoors or in a well-lit room, and dims again when it gets darker."
                description: qsTrId("owlfish-la-only_dark_description")
                checked: config.dim_cutoff
                automaticCheck: false
                onClicked: config.dim_cutoff = !checked
            }

            Slider {
                id: thresholdSlider

                visible: page.cutoffApplies
                width: parent.width
                //% "Bright light starts at"
                label: qsTrId("owlfish-la-threshold")
                minimumValue: 0
                maximumValue: page.luxSteps.length - 1
                stepSize: 1
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
                visible: page.cutoffApplies
                // Read-only, but not greyed out
                enabled: false
                opacity: 1.0
                width: parent.width
                //% "Light here now"
                label: qsTrId("owlfish-la-light_now")
                minimumValue: 0
                maximumValue: page.luxSteps.length - 1
                value: page.ambientLux < 0 ? 0 : page.luxPosition(page.ambientLux)
                //: No reading from the light sensor
                //% "Not available"
                valueText: page.ambientLux < 0 ? qsTrId("owlfish-la-not_available")
                                               : page.formatLux(page.roundLux(page.ambientLux))
            }

            Label {
                visible: page.cutoffApplies
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
                    //: %1 and %2 are light levels, such as "200 lux"
                    //% "Stops dimming above %1 and dims again below %2."
                    return qsTrId("owlfish-la-threshold_description").arg(page.formatLux(threshold))
                            .arg(page.formatLux(threshold * 0.75))
                }
            }

            SectionHeader {
                //% "Accessibility"
                text: qsTrId("owlfish-he-accessibility")
            }

            ComboBox {
                id: correctionCombo
                enabled: page.matrixSupported
                opacity: enabled ? 1.0 : Theme.opacityLow
                //: Colour correction for colour blindness
                //% "Correction"
                label: qsTrId("owlfish-la-correction")
                description: {
                    if (!page.matrixSupported)
                        return qsTrId("owlfish-la-unsupported")
                    switch (page.correction) {
                    case "none":
                        return ""
                    case "greyscale":
                        //% "Shows everything in shades of grey."
                        return qsTrId("owlfish-la-greyscale_description")
                    }
                    // The Strength slider appears below, often under the
                    // screen's edge
                    //% "Makes colours you confuse easier to tell apart. Set the strength below."
                    return qsTrId("owlfish-la-correction_description")
                }

                menu: ContextMenu {
                    // Same order as page.correctionNames
                    MenuItem {
                        //: No colour correction
                        //% "None"
                        text: qsTrId("owlfish-me-none")
                        onClicked: config.correction = "none"
                    }
                    MenuItem {
                        //% "Red-weak (protan)"
                        text: qsTrId("owlfish-me-protan")
                        onClicked: config.correction = "protan"
                    }
                    MenuItem {
                        //% "Green-weak (deutan)"
                        text: qsTrId("owlfish-me-deutan")
                        onClicked: config.correction = "deutan"
                    }
                    MenuItem {
                        //% "Blue-weak (tritan)"
                        text: qsTrId("owlfish-me-tritan")
                        onClicked: config.correction = "tritan"
                    }
                    MenuItem {
                        //% "Greyscale"
                        text: qsTrId("owlfish-me-greyscale")
                        onClicked: config.correction = "greyscale"
                    }
                }
            }

            Slider {
                id: strengthSlider
                // Greyscale is always full
                visible: page.correction !== "none" && page.correction !== "greyscale"
                enabled: page.matrixSupported
                opacity: enabled ? 1.0 : Theme.opacityLow
                width: parent.width
                //: How strong the colour weakness is
                //% "Strength"
                label: qsTrId("owlfish-la-strength")
                // 0 changes nothing, but puts 50 % in the middle
                minimumValue: 0
                maximumValue: 100
                stepSize: 5
                valueText: page.formatPercent(Math.round(sliderValue))
                onSliderValueChanged: {
                    var strength = Math.round(sliderValue)
                    if (strength !== config.correction_strength)
                        config.correction_strength = strength
                }
            }
        }
    }
}
