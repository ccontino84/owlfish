// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "settings.h"
#include "colortemperature.h"
#include "correction.h"

#ifdef HAVE_MLITE
#include <MDConfItem>
#else
#include <QByteArray>
#endif

#include <cmath>
#include <limits>

namespace {

int clampDim(double value)
{
    if (!std::isfinite(value))
        return 0;
    return qBound(0, int(std::lround(value)), int(OwlfishSettings::MaximumDim));
}

int clampTemperature(double value)
{
    if (!std::isfinite(value))
        return OwlfishSettings::DefaultTemperature;
    return qBound(int(ColorTemperature::Minimum), int(std::lround(value)),
                  int(ColorTemperature::Neutral));
}

int clampLux(double value)
{
    if (!std::isfinite(value))
        return OwlfishSettings::DefaultCutoffLux;
    return qBound(int(OwlfishSettings::MinimumCutoffLux), int(std::lround(value)),
                  int(OwlfishSettings::MaximumCutoffLux));
}

int clampMinuteOfDay(double value, int fallback)
{
    if (!std::isfinite(value) || value < 0 || value >= 24 * 60)
        return fallback;
    return int(value);
}

int clampTransition(double value)
{
    if (!std::isfinite(value))
        return OwlfishSettings::DefaultTransition;
    return qBound(0, int(std::lround(value)), int(OwlfishSettings::MaximumTransition));
}

double checkDegrees(double value, double limit)
{
    if (!std::isfinite(value) || qAbs(value) > limit)
        return std::numeric_limits<double>::quiet_NaN();
    return value;
}

QString normalizeRenderer(const QString &value)
{
    const QString renderer = value.trimmed().toLower();
    return renderer.isEmpty() ? QStringLiteral("auto") : renderer;
}

int clampSaturation(double value)
{
    if (!std::isfinite(value))
        return OwlfishSettings::DefaultSaturation;
    return qBound(0, int(std::lround(value)), 100);
}

int clampCorrectionStrength(double value)
{
    if (!std::isfinite(value))
        return OwlfishSettings::DefaultCorrectionStrength;
    return qBound(0, int(std::lround(value)), 100);
}

QString normalizeDimWhen(const QString &value)
{
    const QString when = value.trimmed().toLower();
    if (when == QLatin1String("fixed") || when == QLatin1String("night_light"))
        return when;
    return QStringLiteral("always");
}

}

#ifdef HAVE_MLITE

namespace {

MDConfItem *item(const char *key, QObject *parent)
{
    return new MDConfItem(QStringLiteral("/apps/owlfish/") + QLatin1String(key), parent);
}

double checkDegrees(const QVariant &value, double limit)
{
    bool ok = false;
    const double degrees = value.toDouble(&ok);
    return checkDegrees(ok ? degrees : std::numeric_limits<double>::quiet_NaN(), limit);
}

}

OwlfishSettings::OwlfishSettings(QObject *parent)
    : QObject(parent)
    , m_enabled(item("enabled", this))
    , m_temperature(item("temperature", this))
    , m_dim(item("dim", this))
    , m_cutoffEnabled(item("dim_cutoff", this))
    , m_cutoffLux(item("dim_cutoff_lux", this))
    , m_scheduled(item("schedule", this))
    , m_scheduleSun(item("schedule_sun", this))
    , m_scheduleFrom(item("schedule_from", this))
    , m_scheduleTo(item("schedule_to", this))
    , m_scheduleTransition(item("schedule_transition", this))
    , m_locationManual(item("location_manual", this))
    , m_latitude(item("latitude", this))
    , m_longitude(item("longitude", this))
    , m_renderer(item("renderer", this))
    , m_saturation(item("saturation", this))
    , m_correction(item("correction", this))
    , m_correctionStrength(item("correction_strength", this))
    , m_dimWhen(item("dim_when", this))
    , m_dimFrom(item("dim_from", this))
    , m_dimTo(item("dim_to", this))
{
    // Not the keys only the plugin writes
    for (MDConfItem *item : { m_enabled, m_temperature, m_dim, m_cutoffEnabled, m_cutoffLux,
                              m_scheduled, m_scheduleSun, m_scheduleFrom, m_scheduleTo,
                              m_scheduleTransition, m_locationManual, m_latitude, m_longitude,
                              m_renderer, m_saturation, m_dimWhen, m_dimFrom, m_dimTo,
                              m_correction, m_correctionStrength })
        connect(item, &MDConfItem::valueChanged, this, &OwlfishSettings::changed);
}

bool OwlfishSettings::enabled() const
{
    return m_enabled->value(false).toBool();
}

int OwlfishSettings::temperature() const
{
    return clampTemperature(m_temperature->value(DefaultTemperature).toDouble());
}

int OwlfishSettings::dim() const
{
    return clampDim(m_dim->value(0).toDouble());
}

bool OwlfishSettings::cutoffEnabled() const
{
    return m_cutoffEnabled->value(true).toBool();
}

int OwlfishSettings::cutoffLux() const
{
    return clampLux(m_cutoffLux->value(DefaultCutoffLux).toDouble());
}

bool OwlfishSettings::scheduled() const
{
    return m_scheduled->value(false).toBool();
}

bool OwlfishSettings::scheduleSun() const
{
    return m_scheduleSun->value(false).toBool();
}

int OwlfishSettings::scheduleFrom() const
{
    return clampMinuteOfDay(m_scheduleFrom->value(DefaultFrom).toDouble(), DefaultFrom);
}

int OwlfishSettings::scheduleTo() const
{
    return clampMinuteOfDay(m_scheduleTo->value(DefaultTo).toDouble(), DefaultTo);
}

int OwlfishSettings::scheduleTransition() const
{
    return clampTransition(m_scheduleTransition->value(DefaultTransition).toDouble());
}

bool OwlfishSettings::locationManual() const
{
    return m_locationManual->value(false).toBool();
}

double OwlfishSettings::latitude() const
{
    return checkDegrees(m_latitude->value(), 90);
}

double OwlfishSettings::longitude() const
{
    return checkDegrees(m_longitude->value(), 180);
}

QString OwlfishSettings::renderer() const
{
    return normalizeRenderer(m_renderer->value().toString());
}

int OwlfishSettings::saturation() const
{
    return clampSaturation(m_saturation->value(DefaultSaturation).toDouble());
}

QString OwlfishSettings::correction() const
{
    return Correction::normalize(m_correction->value().toString());
}

int OwlfishSettings::correctionStrength() const
{
    return clampCorrectionStrength(m_correctionStrength->value(DefaultCorrectionStrength).toDouble());
}

QString OwlfishSettings::dimWhen() const
{
    return normalizeDimWhen(m_dimWhen->value().toString());
}

int OwlfishSettings::dimFrom() const
{
    return clampMinuteOfDay(m_dimFrom->value(DefaultFrom).toDouble(), DefaultFrom);
}

int OwlfishSettings::dimTo() const
{
    return clampMinuteOfDay(m_dimTo->value(DefaultTo).toDouble(), DefaultTo);
}

void OwlfishSettings::publishAlsMultiplier(double multiplier)
{
    publish(QStringLiteral("als_multiplier"), multiplier);
}

void OwlfishSettings::publish(const QString &key, const QVariant &value)
{
    MDConfItem *&item = m_publishedItems[key];
    if (!item)
        item = new MDConfItem(QStringLiteral("/apps/owlfish/") + key, this);
    if (item->value() == value)
        return;
    if (value.isValid())
        item->set(value);
    else
        item->unset();
}

#else

// Host builds without mlite5: read once from OWLFISH_<KEY> environment
// variables
namespace {

double env(const char *key, double fallback)
{
    const QByteArray name = QByteArray("OWLFISH_") + QByteArray(key).toUpper();
    return qEnvironmentVariableIsSet(name.constData()) ? qgetenv(name.constData()).toDouble() : fallback;
}

}

OwlfishSettings::OwlfishSettings(QObject *parent)
    : QObject(parent)
    , m_enabled(env("enabled", 0) != 0)
    , m_temperature(clampTemperature(env("temperature", DefaultTemperature)))
    , m_dim(clampDim(env("dim", 0)))
    , m_cutoffEnabled(env("dim_cutoff", 1) != 0)
    , m_cutoffLux(clampLux(env("dim_cutoff_lux", DefaultCutoffLux)))
    , m_scheduled(env("schedule", 0) != 0)
    , m_scheduleSun(env("schedule_sun", 0) != 0)
    , m_scheduleFrom(clampMinuteOfDay(env("schedule_from", DefaultFrom), DefaultFrom))
    , m_scheduleTo(clampMinuteOfDay(env("schedule_to", DefaultTo), DefaultTo))
    , m_scheduleTransition(clampTransition(env("schedule_transition", DefaultTransition)))
    , m_locationManual(env("location_manual", 0) != 0)
    , m_latitude(checkDegrees(env("latitude", std::numeric_limits<double>::quiet_NaN()), 90))
    , m_longitude(checkDegrees(env("longitude", std::numeric_limits<double>::quiet_NaN()), 180))
    , m_renderer(normalizeRenderer(QString::fromLocal8Bit(qgetenv("OWLFISH_RENDERER"))))
    , m_saturation(clampSaturation(env("saturation", DefaultSaturation)))
    , m_correction(Correction::normalize(QString::fromLocal8Bit(qgetenv("OWLFISH_CORRECTION"))))
    , m_correctionStrength(clampCorrectionStrength(env("correction_strength", DefaultCorrectionStrength)))
    , m_dimWhen(normalizeDimWhen(QString::fromLocal8Bit(qgetenv("OWLFISH_DIM_WHEN"))))
    , m_dimFrom(clampMinuteOfDay(env("dim_from", DefaultFrom), DefaultFrom))
    , m_dimTo(clampMinuteOfDay(env("dim_to", DefaultTo), DefaultTo))
{
}

bool OwlfishSettings::enabled() const
{
    return m_enabled;
}

int OwlfishSettings::temperature() const
{
    return m_temperature;
}

int OwlfishSettings::dim() const
{
    return m_dim;
}

bool OwlfishSettings::cutoffEnabled() const
{
    return m_cutoffEnabled;
}

int OwlfishSettings::cutoffLux() const
{
    return m_cutoffLux;
}

bool OwlfishSettings::scheduled() const
{
    return m_scheduled;
}

bool OwlfishSettings::scheduleSun() const
{
    return m_scheduleSun;
}

int OwlfishSettings::scheduleFrom() const
{
    return m_scheduleFrom;
}

int OwlfishSettings::scheduleTo() const
{
    return m_scheduleTo;
}

int OwlfishSettings::scheduleTransition() const
{
    return m_scheduleTransition;
}

bool OwlfishSettings::locationManual() const
{
    return m_locationManual;
}

double OwlfishSettings::latitude() const
{
    return m_latitude;
}

double OwlfishSettings::longitude() const
{
    return m_longitude;
}

QString OwlfishSettings::renderer() const
{
    return m_renderer;
}

int OwlfishSettings::saturation() const
{
    return m_saturation;
}

QString OwlfishSettings::correction() const
{
    return m_correction;
}

int OwlfishSettings::correctionStrength() const
{
    return m_correctionStrength;
}

QString OwlfishSettings::dimWhen() const
{
    return m_dimWhen;
}

int OwlfishSettings::dimFrom() const
{
    return m_dimFrom;
}

int OwlfishSettings::dimTo() const
{
    return m_dimTo;
}

void OwlfishSettings::publishAlsMultiplier(double multiplier)
{
    publish(QStringLiteral("als_multiplier"), multiplier);
}

void OwlfishSettings::publish(const QString &key, const QVariant &value)
{
    if (value.isValid())
        m_published.insert(key, value);
    else
        m_published.remove(key);
}

void OwlfishSettings::setEnabled(bool enabled)
{
    m_enabled = enabled;
    emit changed();
}

void OwlfishSettings::setRenderer(const QString &renderer)
{
    m_renderer = normalizeRenderer(renderer);
    emit changed();
}

void OwlfishSettings::setSaturation(int saturation)
{
    m_saturation = clampSaturation(saturation);
    emit changed();
}

void OwlfishSettings::setCorrection(const QString &correction, int strength)
{
    m_correction = Correction::normalize(correction);
    m_correctionStrength = clampCorrectionStrength(strength);
    emit changed();
}

void OwlfishSettings::setDimWhen(const QString &when)
{
    m_dimWhen = normalizeDimWhen(when);
    emit changed();
}

#endif
