// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "controller.h"
#include "alscalibration.h"
#include "ambientcutoff.h"
#include "colormatrix.h"
#include "correction.h"
#include "settings.h"
#include "colortemperature.h"
#include "logging.h"
#include <owlfish_version.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QFile>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStringList>
#include <QTime>
#include <QTimeZone>

#include <cmath>

#ifdef HAVE_QTSENSORS
#include <QLightSensor>
#endif

namespace {

const int FindIntervalMs = 500;
// The compositor window exists within seconds of startup; give up after 2 min
const int MaxFindAttempts = 240;
// A start counts as healthy once the filter has been in place this long
const int HealthyAfterMs = 30000;
const int SettingsFadeMs = 400;
const int CutoffFadeMs = 1000;
// While the display is on, the schedule is checked this often; each check
// moves the colour transitions one step
const int ScheduleIntervalMs = 30000;
const int ScheduleStepFadeMs = 1000;
// Above everything the compositor QML puts in the window
const qreal FilterZ = 1e9;

// os-release's PRETTY_NAME, e.g. "Sailfish OS 5.2.0.17 (Tampella)", for the
// diagnostics
QString systemName()
{
    QFile file(QStringLiteral("/etc/os-release"));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (!line.startsWith(QLatin1String("PRETTY_NAME=")))
                continue;
            QString name = line.mid(12);
            if (name.size() >= 2 && name.startsWith(QLatin1Char('"')) && name.endsWith(QLatin1Char('"')))
                name = name.mid(1, name.size() - 2);
            if (!name.isEmpty())
                return name;
        }
    }
    return QStringLiteral("unknown");
}

}

OwlfishController::OwlfishController(const QByteArray &windowClass,
                                       const QString &crashGuardPath,
                                       QObject *parent)
    : QObject(parent)
    , m_windowClass(windowClass)
    , m_guard(crashGuardPath)
    , m_active(false)
    , m_settings(nullptr)
    , m_cutoff(nullptr)
    , m_lightSensor(nullptr)
    , m_alsMultiplier(1.0)
    , m_luxQuery(0)
    , m_findAttempts(0)
    , m_displayOn(true)
    , m_pq(new PqDisplay)
    , m_hwReleasePath(QLatin1String(PqDisplay::HwReleasePath))
    , m_deviceTreePath(QLatin1String(PqDisplay::DeviceTreePath))
    , m_ccorrRead(false)
    , m_pqActive(false)
    , m_pqFailed(false)
    , m_reasonNamesDevice(false)
    , m_capabilitiesLogged(false)
    , m_sunLocated(false)
    , m_sunLatitude(0)
    , m_sunLongitude(0)
    , m_sunState(SunTimes::Normal)
    , m_sunset(0)
    , m_sunrise(0)
{
    if (!m_guard.begin()) {
        qCWarning(lcOwlfish) << "Disabled after" << m_guard.unhealthyStarts()
                              << "unhealthy compositor starts; remove"
                              << m_guard.filePath() << "to enable again";
        return;
    }
    m_active = true;

    // The display hardware keeps its matrix after the compositor is gone
    connect(qApp, &QCoreApplication::aboutToQuit, this, &OwlfishController::stopPq);

    m_settings = new OwlfishSettings(this);
    connect(m_settings, &OwlfishSettings::changed, this, &OwlfishController::applySettings);

    m_cutoff = new AmbientCutoff(this);
    connect(m_cutoff, &AmbientCutoff::brightChanged, this, &OwlfishController::cutoffChanged);

    m_animation.setStartValue(0.0);
    m_animation.setEndValue(1.0);
    m_animation.setEasingCurve(QEasingCurve::InOutQuad);
    connect(&m_animation, &QVariantAnimation::valueChanged, this, &OwlfishController::animate);

    m_healthyTimer.setSingleShot(true);
    m_healthyTimer.setInterval(HealthyAfterMs);
    connect(&m_healthyTimer, &QTimer::timeout, this, [this]() { m_guard.markHealthy(); });

    m_scheduleTimer.setInterval(ScheduleIntervalMs);
    connect(&m_scheduleTimer, &QTimer::timeout, this, &OwlfishController::scheduleTick);

    // Constructed while QGuiApplication itself is being set up; look for the
    // window once the event loop runs
    m_findTimer.setInterval(FindIntervalMs);
    connect(&m_findTimer, &QTimer::timeout, this, &OwlfishController::findWindow);
    m_findTimer.start();
    QTimer::singleShot(0, this, &OwlfishController::findWindow);
}

OwlfishController::~OwlfishController()
{
    stopPq();
}

void OwlfishController::setDisplayHardware(PqDisplay::Backend *backend, const QString &hwReleasePath,
                                           const QString &deviceTreePath)
{
    m_pq.reset(new PqDisplay(backend));
    m_hwReleasePath = hwReleasePath;
    m_deviceTreePath = deviceTreePath;
    m_ccorrRead = false;
}

QVector3D OwlfishController::filterGain(qreal colourStrength, int temperature, qreal dimPercent)
{
    const QVector3D tint = ColorTemperature::gain(ColorTemperature::partial(temperature, colourStrength));
    // double: qreal is float on 32-bit ARM
    const float dim = float(qBound(0.0, double(dimPercent), double(OwlfishSettings::MaximumDim)) / 100);
    return tint * (1 - dim);
}

qreal OwlfishController::saturationFactor(qreal colourStrength, int saturationPercent)
{
    const double strength = qBound(0.0, double(colourStrength), 1.0);
    return qreal(1 - strength * (1 - qBound(0, saturationPercent, 100) / 100.0));
}

qreal OwlfishController::colourStrength(bool scheduled, const OwlfishSchedule &schedule,
                                          const QDateTime &now)
{
    return scheduled ? schedule.strength(now) : 1;
}

QString OwlfishController::status() const
{
    if (!m_active)
        return QStringLiteral("crash-guard");
    if (m_window)
        return QStringLiteral("active");
    if (m_findAttempts >= MaxFindAttempts)
        return QStringLiteral("no-window");
    return QStringLiteral("starting");
}

void OwlfishController::resetCrashGuard()
{
    m_guard.markHealthy();
}

QString OwlfishController::renderer() const
{
    if (!m_item)
        return QStringLiteral("none");
    if (m_pqActive)
        return QStringLiteral("pq");
    return ColorFilter::rendererName(m_item->renderer());
}

QString OwlfishController::diagnostics() const
{
    // Pasted into reports as a whole, so it names the version too
    QStringList lines;
    lines << QStringLiteral("version %1").arg(QStringLiteral(OWLFISH_VERSION))
          << QStringLiteral("system %1").arg(systemName())
          << QStringLiteral("status %1").arg(status());
    lines << QStringLiteral("renderer %1 (%2)")
             .arg(renderer(), m_item ? m_rendererReason : status());
    if (m_settings)
        lines << QStringLiteral("renderer key %1").arg(m_settings->renderer());
    // Unless the reason already names it
    if (!m_reasonNamesDevice) {
        const QString device = PqDisplay::deviceId(m_hwReleasePath);
        lines << QStringLiteral("device %1, %2")
                 .arg(device.isEmpty() ? QStringLiteral("unknown") : device,
                      PqDisplay::isVerified(device) ? QStringLiteral("verified") : QStringLiteral("not verified"));
    }
    if (m_ccorrRead)
        lines << QStringLiteral("display colour correction %1").arg(PqDisplay::describe(m_ccorr));
    if (m_pq->isOpen()) {
        lines << QStringLiteral("display hardware version %1, %2 calls, mean %3 ms, max %4 ms")
                 .arg(m_pq->interfaceVersion()).arg(m_pq->calls())
                 .arg(m_pq->meanCallMs(), 0, 'f', 2).arg(m_pq->maxCallMs(), 0, 'f', 2);
        if (!m_pq->error().isEmpty())
            lines << QStringLiteral("display hardware error: %1").arg(m_pq->error());
    } else {
        lines << QStringLiteral("display hardware %1")
                 .arg(m_pq->error().isEmpty() ? QStringLiteral("not checked") : m_pq->error());
    }
    if (m_settings) {
        const QString correction = m_settings->correction();
        QString line = QStringLiteral("correction %1").arg(correction);
        if (correction != QLatin1String("none")) {
            if (Correction::hasStrength(correction))
                line += QStringLiteral(" %1 %").arg(m_settings->correctionStrength());
            if (!matrixSupported())
                line += QStringLiteral(", not supported by this renderer");
        }
        lines << line;
        QString saturation = QStringLiteral("saturation %1 %").arg(m_settings->saturation());
        // Only the display hardware and Fetch mix the channels
        if (m_settings->saturation() < 100 && !matrixSupported())
            saturation += QStringLiteral(", not supported by this renderer");
        lines << saturation;
        const QString when = m_settings->dimWhen();
        lines << (when == QLatin1String("fixed")
                  ? QStringLiteral("dimming when fixed %1-%2")
                    .arg(QTime(0, 0).addSecs(m_settings->dimFrom() * 60).toString(QStringLiteral("HH:mm")),
                         QTime(0, 0).addSecs(m_settings->dimTo() * 60).toString(QStringLiteral("HH:mm")))
                  : QStringLiteral("dimming when %1").arg(when));
    }
    // Only checked once the GPU draws
    if (m_item && m_item->capabilities().detected) {
        const ColorFilter::Capabilities capabilities = m_item->capabilities();
        lines << QStringLiteral("GL %1, %2, %3")
                 .arg(QString::fromLatin1(capabilities.vendor), QString::fromLatin1(capabilities.renderer),
                      QString::fromLatin1(capabilities.version))
              << QStringLiteral("fetch %1, %2")
                 .arg(ColorFilter::fetchExtensionName(capabilities.fetch),
                      capabilities.fetchWorks ? QStringLiteral("works") : QStringLiteral("not usable"));
    }
    return lines.join(QLatin1Char('\n'));
}

bool OwlfishController::matrixSupported() const
{
    if (m_pqActive)
        return true;
    return m_item && m_item->renderer() == ColorFilter::Fetch && m_item->capabilities().fetchWorks;
}

QMatrix3x3 OwlfishController::colourMatrix(qreal colourStrength) const
{
    if (!matrixSupported())
        return QMatrix3x3();
    // Desaturated after the correction, so a grey bedtime keeps the
    // correction's lightness cues; tinted last (filterGain())
    return ColorMatrix::saturation(saturationFactor(colourStrength, m_settings->saturation()))
            * Correction::matrix(m_settings->correction(), m_settings->correctionStrength() / 100.0);
}

void OwlfishController::findWindow()
{
    if (m_window)
        return;

    for (QWindow *window : QGuiApplication::topLevelWindows()) {
        QQuickWindow *quickWindow = qobject_cast<QQuickWindow *>(window);
        if (quickWindow && quickWindow->inherits(m_windowClass.constData())) {
            m_findTimer.stop();
            attach(quickWindow);
            return;
        }
    }

    if (++m_findAttempts >= MaxFindAttempts) {
        m_findTimer.stop();
        qCWarning(lcOwlfish) << "No" << m_windowClass << "window found; staying inactive";
        // Nothing was changed in this process, so this start was not risky
        m_guard.markHealthy();
    }
}

void OwlfishController::attach(QQuickWindow *window)
{
    m_window = window;
    m_item = new ColorFilterItem(window->contentItem());
    m_item->setObjectName(QStringLiteral("owlfish-filter"));
    m_item->setZ(FilterZ);
    connect(m_item.data(), &ColorFilterItem::rendererChanged, this, &OwlfishController::rendererChanged);

    connect(window, &QWindow::widthChanged, this, &OwlfishController::updateItemGeometry);
    connect(window, &QWindow::heightChanged, this, &OwlfishController::updateItemGeometry);
    updateItemGeometry();

    qCInfo(lcOwlfish) << "Filter attached to" << window->metaObject()->className();

    m_alsMultiplier = alsValueMultiplier();
    qCInfo(lcOwlfish) << "Ambient light sensor multiplier" << m_alsMultiplier;
    m_settings->publishAlsMultiplier(m_alsMultiplier);
    querySensorMaximum();

    // Catch up with the schedule as soon as the display turns on. The timer
    // does not run during suspend.
    QDBusConnection::systemBus().connect(QStringLiteral("com.nokia.mce"),
                                         QStringLiteral("/com/nokia/mce/signal"),
                                         QStringLiteral("com.nokia.mce.signal"),
                                         QStringLiteral("display_status_ind"),
                                         this, SLOT(displayStatusChanged(QString)));

    applySettings();
    m_healthyTimer.start();
}

void OwlfishController::updateItemGeometry()
{
    if (m_item && m_window)
        m_item->setSize(QSizeF(m_window->width(), m_window->height()));
}

void OwlfishController::applySettings()
{
    if (!m_item)
        return;

    updateRenderer();

    qCInfo(lcOwlfish) << "enabled" << m_settings->enabled()
                        << "temperature" << m_settings->temperature() << "K"
                        << "dim" << m_settings->dim() << "%"
                        << "cutoff" << m_settings->cutoffEnabled() << m_settings->cutoffLux() << "lux"
                        << "schedule" << m_settings->scheduled() << m_settings->scheduleFrom()
                        << m_settings->scheduleTo() << m_settings->scheduleTransition()
                        << "sun" << m_settings->scheduleSun() << "manual location" << m_settings->locationManual()
                        << "saturation" << m_settings->saturation() << "%"
                        << "dim when" << qPrintable(m_settings->dimWhen()) << m_settings->dimFrom() << m_settings->dimTo()
                        << "renderer" << qPrintable(renderer())
                        << "correction" << qPrintable(m_settings->correction()) << m_settings->correctionStrength() << "%";

    m_cutoff->setThreshold(m_settings->cutoffLux());
    updateLightSensor();

    updateSun();

    updateScheduleTimer();
    updateGain(SettingsFadeMs);
}

void OwlfishController::cutoffChanged(bool bright)
{
    qCInfo(lcOwlfish) << (bright ? "Bright ambient light, suspending dimming" : "Ambient light low, resuming dimming");
    updateGain(CutoffFadeMs);
}

OwlfishSchedule OwlfishController::schedule() const
{
    return schedule(m_settings->scheduleTransition());
}

OwlfishSchedule OwlfishController::schedule(int transitionMinutes) const
{
    if (m_settings->scheduleSun()) {
        // No night under the midnight sun, and none without a location. In
        // polar night the colour stays on (currentColourStrength).
        if (!m_sunLocated || m_sunState != SunTimes::Normal)
            return OwlfishSchedule(0, 0, 0);
        // Today's sunrise stands in for tomorrow's: it only moves by minutes
        // a day
        return OwlfishSchedule(m_sunset, m_sunrise, transitionMinutes);
    }
    return OwlfishSchedule(m_settings->scheduleFrom(), m_settings->scheduleTo(), transitionMinutes);
}

qreal OwlfishController::currentColourStrength(const QDateTime &now) const
{
    if (m_settings->scheduled() && m_settings->scheduleSun() && m_sunLocated
            && m_sunState == SunTimes::PolarNight)
        return 1;
    return colourStrength(m_settings->scheduled(), schedule(), now);
}

qreal OwlfishController::currentDimStrength(const QDateTime &now) const
{
    const QString when = m_settings->dimWhen();
    if (when == QLatin1String("night_light")) {
        // Night light's times, without its gradual change: on from the start
        // (or sunset) to the end (or sunrise)
        if (!m_settings->scheduled())
            return 1;
        if (m_settings->scheduleSun() && m_sunLocated && m_sunState == SunTimes::PolarNight)
            return 1;
        return schedule(0).strength(now);
    }
    if (when == QLatin1String("fixed"))
        return OwlfishSchedule(m_settings->dimFrom(), m_settings->dimTo(), 0).strength(now);
    return 1;
}

void OwlfishController::updateSun()
{
    // Checked on every schedule tick, so a new time zone or date is picked
    // up within one. Reading two links is cheap; zone.tab is only read again
    // when the zone changes.
    QString zone = TimeZoneLocation::systemZone();
    // Qt 5.6 only follows one link of /etc/localtime, which is not enough on
    // Sailfish OS, so this is only a fallback
    if (zone.isEmpty())
        zone = QString::fromUtf8(QTimeZone::systemTimeZoneId());
    if (zone != m_zoneLocation.zone())
        m_zoneLocation = TimeZoneLocation(zone);

    bool located = m_zoneLocation.isValid();
    double latitude = m_zoneLocation.latitude();
    double longitude = m_zoneLocation.longitude();
    if (m_settings->locationManual() && std::isfinite(m_settings->latitude())
            && std::isfinite(m_settings->longitude())) {
        located = true;
        latitude = m_settings->latitude();
        longitude = m_settings->longitude();
    }

    const QDate today = QDate::currentDate();
    if (today == m_sunDate && zone == m_sunZone && located == m_sunLocated
            && (!located || (latitude == m_sunLatitude && longitude == m_sunLongitude)))
        return;

    m_sunDate = today;
    m_sunZone = zone;
    m_sunLocated = located;
    m_sunLatitude = latitude;
    m_sunLongitude = longitude;
    if (located) {
        const SunTimes sun = SunTimes::compute(today, latitude, longitude);
        m_sunState = sun.state();
        if (m_sunState == SunTimes::Normal) {
            // In the same local time as the schedule's "now"
            m_sunset = SunTimes::minuteOfDay(sun.sunset());
            m_sunrise = SunTimes::minuteOfDay(sun.sunrise());
        }
    }

    qCInfo(lcOwlfish) << "Sun for" << today.toString(Qt::ISODate) << "in" << zone
                        << (!located ? "no location"
                            : m_sunState == SunTimes::PolarDay ? "midnight sun"
                            : m_sunState == SunTimes::PolarNight ? "polar night"
                            : qPrintable(QStringLiteral("sunset %1:%2 sunrise %3:%4")
                                         .arg(m_sunset / 60, 2, 10, QLatin1Char('0'))
                                         .arg(m_sunset % 60, 2, 10, QLatin1Char('0'))
                                         .arg(m_sunrise / 60, 2, 10, QLatin1Char('0'))
                                         .arg(m_sunrise % 60, 2, 10, QLatin1Char('0'))));
    publishSun();
}

void OwlfishController::publishSun()
{
    // The page reads no files; it shows what the plugin found
    const bool zoneLocated = m_zoneLocation.isValid();
    // Always set, so the page can tell whether the auto_ keys apply
    m_settings->publish(QStringLiteral("sun_place"), zoneLocated ? m_zoneLocation.place() : QString());
    m_settings->publish(QStringLiteral("auto_latitude"),
                        zoneLocated ? QVariant(m_zoneLocation.latitude()) : QVariant());
    m_settings->publish(QStringLiteral("auto_longitude"),
                        zoneLocated ? QVariant(m_zoneLocation.longitude()) : QVariant());

    const bool normal = m_sunLocated && m_sunState == SunTimes::Normal;
    const char *state = !m_sunLocated ? "no_location"
                      : m_sunState == SunTimes::PolarDay ? "polar_day"
                      : m_sunState == SunTimes::PolarNight ? "polar_night"
                      : "normal";
    m_settings->publish(QStringLiteral("sun_state"), QString::fromLatin1(state));
    m_settings->publish(QStringLiteral("sun_set"), normal ? QVariant(m_sunset) : QVariant());
    m_settings->publish(QStringLiteral("sun_rise"), normal ? QVariant(m_sunrise) : QVariant());
    m_settings->publish(QStringLiteral("sun_latitude"),
                        m_sunLocated ? QVariant(m_sunLatitude) : QVariant());
}

void OwlfishController::updateScheduleTimer()
{
    // Only the schedules change the gain over time. Dimming with Night light
    // only follows the colour's schedule.
    const bool timed = m_settings->scheduled() || m_settings->dimWhen() == QLatin1String("fixed");
    if (m_settings->enabled() && timed && m_displayOn) {
        if (!m_scheduleTimer.isActive())
            m_scheduleTimer.start();
    } else {
        m_scheduleTimer.stop();
    }
}

void OwlfishController::scheduleTick()
{
    updateSun();
    // The light sensor runs only while the dimming can apply
    updateLightSensor();
    updateGain(ScheduleStepFadeMs);
}

void OwlfishController::displayStatusChanged(const QString &status)
{
    // "on", "dimmed" or "off"
    const bool on = status != QLatin1String("off");
    if (on == m_displayOn)
        return;
    m_displayOn = on;
    updateScheduleTimer();
    if (on && m_item)
        scheduleTick();
}

void OwlfishController::updateGain(int fadeMs)
{
    if (!m_item)
        return;

    if (!m_settings->enabled()) {
        fadeTo(QVector3D(1, 1, 1), QMatrix3x3(), fadeMs);
        return;
    }

    const QDateTime now = QDateTime::currentDateTime();
    const qreal colour = currentColourStrength(now);
    const bool dimSuspended = m_settings->cutoffEnabled() && m_cutoff->isBright();
    const qreal dim = dimSuspended ? 0 : m_settings->dim() * currentDimStrength(now);
    // Corrected and desaturated first, then tinted: a warm grey at bedtime
    fadeTo(filterGain(colour, m_settings->temperature(), dim), colourMatrix(colour), fadeMs);
}

void OwlfishController::updateRenderer()
{
    const QString key = m_settings->renderer();
    // The GPU is fetch wherever it works, blend otherwise. blend and fetch
    // skip the display hardware.
    const bool forcedFetch = key == QLatin1String("fetch");
    const bool forcedBlend = key == QLatin1String("blend");
    const bool forcedGpu = forcedBlend || forcedFetch;
    const bool forcedPq = key == QLatin1String("pq");
    if (!forcedGpu && !forcedPq && key != QLatin1String("auto"))
        qCWarning(lcOwlfish) << "Unknown renderer" << key << "- using auto";

    bool pq = false;
    QString reason;
    bool namesDevice = false;
    if (m_pqFailed) {
        reason = m_rendererReason;
    } else if (forcedGpu) {
        reason = QStringLiteral("forced by the renderer key");
    } else {
        // The device tree says whether the display controller's colour
        // correction is the kind whose scale is known. Nothing is loaded or
        // called on other devices.
        if (!m_ccorrRead) {
            m_ccorr = PqDisplay::ccorr(m_deviceTreePath);
            m_ccorrRead = true;
        }
        const QString device = PqDisplay::deviceId(m_hwReleasePath);
        const bool verified = PqDisplay::isVerified(device);
        const bool detected = PqDisplay::isKnown(m_ccorr);
        if (!forcedPq && !verified && !detected) {
            reason = device.isEmpty() ? QStringLiteral("device unknown")
                                      : QStringLiteral("device %1 not verified").arg(device);
            namesDevice = true;
        } else if (!m_pq->open()) {
            reason = QStringLiteral("display hardware not available: %1").arg(m_pq->error());
        } else {
            pq = true;
            namesDevice = !forcedPq && verified;
            if (forcedPq)
                reason = QStringLiteral("forced by the renderer key");
            else if (verified)
                reason = QStringLiteral("device %1 verified").arg(device);
            else
                reason = QStringLiteral("display hardware detected");
        }
    }

    // Until the item has checked it (at its first frame), Fetch counts as
    // working
    const ColorFilter::Capabilities capabilities = m_item->capabilities();
    const bool fetchFails = capabilities.detected && !capabilities.fetchWorks;
    m_item->setRenderer(!forcedBlend && !fetchFails ? ColorFilter::Fetch : ColorFilter::Blend);
    if (!pq && !forcedBlend && fetchFails) {
        const QString noFetch = QStringLiteral("fetch not supported (extension %1)")
                .arg(ColorFilter::fetchExtensionName(capabilities.fetch));
        reason = forcedFetch ? noFetch : reason + QStringLiteral(", ") + noFetch;
    }

    if (pq && !m_pqActive) {
        // Also clears whatever an earlier run left behind
        if (m_pq->reset()) {
            m_pqActive = true;
        } else {
            m_pqFailed = true;
            reason = QStringLiteral("display hardware failed: %1").arg(m_pq->error());
            namesDevice = false;
        }
    } else if (!pq && m_pqActive) {
        stopPq();
    }
    m_item->setVisible(!m_pqActive);

    m_reasonNamesDevice = namesDevice;
    if (reason != m_rendererReason) {
        m_rendererReason = reason;
        qCInfo(lcOwlfish) << "Renderer" << qPrintable(renderer()) << "-" << qPrintable(reason);
    }
    pushPq();
}

void OwlfishController::pushPq()
{
    if (!m_pqActive || !m_item)
        return;
    if (!m_pq->setMatrix(ColorMatrix::withGain(m_matrix, m_item->gain())))
        pqFailed();
}

void OwlfishController::stopPq()
{
    if (!m_pqActive)
        return;
    m_pqActive = false;
    m_pq->reset();
}

void OwlfishController::pqFailed()
{
    m_pqFailed = true;
    m_rendererReason = QStringLiteral("display hardware failed: %1").arg(m_pq->error());
    m_reasonNamesDevice = false;
    // Whatever can still be reset
    stopPq();
    if (m_item)
        m_item->setVisible(true);
    qCWarning(lcOwlfish) << "Renderer" << qPrintable(renderer()) << "-" << qPrintable(m_rendererReason);
}

void OwlfishController::rendererChanged()
{
    if (!m_item || m_capabilitiesLogged || !m_item->capabilities().detected)
        return;
    m_capabilitiesLogged = true;
    const ColorFilter::Capabilities capabilities = m_item->capabilities();
    qCInfo(lcOwlfish) << "OpenGL" << capabilities.vendor << capabilities.renderer << capabilities.version
                        << "fetch" << ColorFilter::fetchExtensionName(capabilities.fetch)
                        << (capabilities.fetchWorks ? "works" : "not usable");
    // Now it is known whether the correction and the saturation can be drawn
    updateRenderer();
    updateGain(SettingsFadeMs);
}

void OwlfishController::updateLightSensor()
{
#ifdef HAVE_QTSENSORS
    // Only read the sensor while it can make a difference. It also stops
    // delivering readings while the display is off.
    const bool wanted = m_settings->enabled() && m_settings->dim() > 0 && m_settings->cutoffEnabled()
            && currentDimStrength(QDateTime::currentDateTime()) > 0;

    if (wanted && !m_lightSensor) {
        m_lightSensor = new QLightSensor(this);
        // Event driven: sensorfw only delivers readings when the value
        // changes, and none when a session starts (queryCurrentLux())
        connect(m_lightSensor, &QSensor::readingChanged, this, [this]() {
            if (QLightReading *reading = m_lightSensor->reading())
                m_cutoff->addReading(reading->lux() * m_alsMultiplier);
        });
    }

    if (!m_lightSensor)
        return;

    if (wanted && !m_lightSensor->isActive()) {
        if (m_lightSensor->start())
            queryCurrentLux();
        else
            qCWarning(lcOwlfish) << "Cannot start the ambient light sensor; cut-off inactive";
    } else if (!wanted && m_lightSensor->isActive()) {
        m_lightSensor->stop();
        ++m_luxQuery;
        m_cutoff->reset();
    }
#endif
}

void OwlfishController::querySensorMaximum()
{
    const QDBusMessage call = QDBusMessage::createMethodCall(
            QStringLiteral("com.nokia.SensorService"), QStringLiteral("/SensorManager/alssensor"),
            QStringLiteral("local.ALSSensor"), QStringLiteral("getAvailableDataRanges"));
    QDBusPendingCallWatcher *watcher =
            new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        double maximum = 0;
        if (!alsMaximumFromReply(watcher->reply(), &maximum)) {
            qCWarning(lcOwlfish) << "Cannot read the ambient light sensor's range:" << watcher->error().message();
            m_settings->publish(QStringLiteral("als_max_lux"), QVariant());
            return;
        }
        const double lux = maximum * m_alsMultiplier;
        qCInfo(lcOwlfish) << "Ambient light sensor reports up to" << lux << "lux";
        m_cutoff->setMaximum(lux);
        m_settings->publish(QStringLiteral("als_max_lux"), lux);
    });
}

void OwlfishController::queryCurrentLux()
{
    // As mce does; without it, steady bright light counts as dark until the
    // level changes
    const int query = ++m_luxQuery;
    const QDBusMessage call = QDBusMessage::createMethodCall(
            QStringLiteral("com.nokia.SensorService"), QStringLiteral("/SensorManager/alssensor"),
            QStringLiteral("local.ALSSensor"), QStringLiteral("lux"));
    QDBusPendingCallWatcher *watcher =
            new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, query](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        // The sensor was stopped or started again since
        if (query != m_luxQuery)
            return;
        quint32 lux = 0;
        if (!alsLuxFromReply(watcher->reply(), &lux)) {
            qCWarning(lcOwlfish) << "Cannot read the current ambient light:" << watcher->error().message();
            return;
        }
        qCInfo(lcOwlfish) << "Ambient light now" << lux * m_alsMultiplier << "lux";
        m_cutoff->addInitialReading(lux * m_alsMultiplier);
    });
}

void OwlfishController::fadeTo(const QVector3D &gain, const QMatrix3x3 &matrix, int durationMs)
{
    m_animation.stop();
    m_fromGain = m_item->gain();
    m_toGain = gain;
    m_fromMatrix = m_matrix;
    m_toMatrix = matrix;
    if (m_fromGain == m_toGain && m_fromMatrix == m_toMatrix)
        return;
    m_animation.setDuration(durationMs);
    m_animation.start();
}

void OwlfishController::animate(const QVariant &progress)
{
    if (!m_item)
        return;
    const float t = progress.toFloat();
    m_item->setGain(m_fromGain + (m_toGain - m_fromGain) * t);
    m_matrix = m_fromMatrix + (m_toMatrix - m_fromMatrix) * t;
    // Drawn by Fetch or the display hardware
    m_item->setMatrix(m_matrix);
    pushPq();
}
