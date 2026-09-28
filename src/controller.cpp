// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "controller.h"
#include "alscalibration.h"
#include "ambientcutoff.h"
#include "settings.h"
#include "colortemperature.h"
#include "logging.h"

#include <QDBusConnection>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
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
    , m_findAttempts(0)
    , m_displayOn(true)
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

QVector3D OwlfishController::filterGain(qreal colourStrength, int temperature, int dimPercent)
{
    const QVector3D tint = ColorTemperature::gain(ColorTemperature::partial(temperature, colourStrength));
    const float dim = qBound(0, dimPercent, int(OwlfishSettings::MaximumDim)) / 100.0f;
    return tint * (1 - dim);
}

qreal OwlfishController::colourStrength(bool scheduled, const OwlfishSchedule &schedule,
                                          const QDateTime &now)
{
    return scheduled ? schedule.strength(now) : 1;
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

    connect(window, &QWindow::widthChanged, this, &OwlfishController::updateItemGeometry);
    connect(window, &QWindow::heightChanged, this, &OwlfishController::updateItemGeometry);
    updateItemGeometry();

    qCInfo(lcOwlfish) << "Filter attached to" << window->metaObject()->className();

    m_alsMultiplier = alsValueMultiplier();
    qCInfo(lcOwlfish) << "Ambient light sensor multiplier" << m_alsMultiplier;
    m_settings->publishAlsMultiplier(m_alsMultiplier);

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

    qCInfo(lcOwlfish) << "enabled" << m_settings->enabled()
                        << "temperature" << m_settings->temperature() << "K"
                        << "dim" << m_settings->dim() << "%"
                        << "cutoff" << m_settings->cutoffEnabled() << m_settings->cutoffLux() << "lux"
                        << "schedule" << m_settings->scheduled() << m_settings->scheduleFrom()
                        << m_settings->scheduleTo() << m_settings->scheduleTransition()
                        << "sun" << m_settings->scheduleSun() << "manual location" << m_settings->locationManual();

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
    if (m_settings->scheduleSun()) {
        // No night under the midnight sun, and none without a location. In
        // polar night the colour stays on (currentColourStrength).
        if (!m_sunLocated || m_sunState != SunTimes::Normal)
            return OwlfishSchedule(0, 0, 0);
        // Today's sunrise stands in for tomorrow's: it only moves by minutes
        // a day
        return OwlfishSchedule(m_sunset, m_sunrise, m_settings->scheduleTransition());
    }
    return OwlfishSchedule(m_settings->scheduleFrom(), m_settings->scheduleTo(),
                             m_settings->scheduleTransition());
}

qreal OwlfishController::currentColourStrength(const QDateTime &now) const
{
    if (m_settings->scheduled() && m_settings->scheduleSun() && m_sunLocated
            && m_sunState == SunTimes::PolarNight)
        return 1;
    return colourStrength(m_settings->scheduled(), schedule(), now);
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
    // Only the schedule changes the gain over time
    if (m_settings->enabled() && m_settings->scheduled() && m_displayOn) {
        if (!m_scheduleTimer.isActive())
            m_scheduleTimer.start();
    } else {
        m_scheduleTimer.stop();
    }
}

void OwlfishController::scheduleTick()
{
    updateSun();
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
        fadeTo(QVector3D(1, 1, 1), fadeMs);
        return;
    }

    const qreal colour = currentColourStrength(QDateTime::currentDateTime());
    const bool dimSuspended = m_settings->cutoffEnabled() && m_cutoff->isBright();
    const int dim = dimSuspended ? 0 : m_settings->dim();
    fadeTo(filterGain(colour, m_settings->temperature(), dim), fadeMs);
}

void OwlfishController::updateLightSensor()
{
#ifdef HAVE_QTSENSORS
    // Only read the sensor while it can make a difference. It also stops
    // delivering readings while the display is off.
    const bool wanted = m_settings->enabled() && m_settings->dim() > 0 && m_settings->cutoffEnabled();

    if (wanted && !m_lightSensor) {
        m_lightSensor = new QLightSensor(this);
        // Event driven: sensorfw only delivers readings when the value changes
        m_lightSensor->setSkipDuplicates(true);
        connect(m_lightSensor, &QSensor::readingChanged, this, [this]() {
            if (QLightReading *reading = m_lightSensor->reading())
                m_cutoff->addReading(reading->lux() * m_alsMultiplier);
        });
    }

    if (!m_lightSensor)
        return;

    if (wanted && !m_lightSensor->isActive()) {
        if (!m_lightSensor->start())
            qCWarning(lcOwlfish) << "Cannot start the ambient light sensor; cut-off inactive";
    } else if (!wanted && m_lightSensor->isActive()) {
        m_lightSensor->stop();
        m_cutoff->reset();
    }
#endif
}

void OwlfishController::fadeTo(const QVector3D &gain, int durationMs)
{
    m_animation.stop();
    m_fromGain = m_item->gain();
    m_toGain = gain;
    if (m_fromGain == m_toGain)
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
}
