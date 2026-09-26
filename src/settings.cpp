// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "settings.h"
#include "colortemperature.h"

#ifdef HAVE_MLITE
#include <MDConfItem>
#else
#include <QByteArray>
#endif

#include <cmath>

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

}

#ifdef HAVE_MLITE

namespace {

MDConfItem *item(const char *key, QObject *parent)
{
    return new MDConfItem(QStringLiteral("/apps/owlfish/") + QLatin1String(key), parent);
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
    , m_scheduleFrom(item("schedule_from", this))
    , m_scheduleTo(item("schedule_to", this))
    , m_scheduleTransition(item("schedule_transition", this))
    , m_alsMultiplier(item("als_multiplier", this))
{
    // Not the keys only the plugin writes
    for (MDConfItem *item : { m_enabled, m_temperature, m_dim, m_cutoffEnabled, m_cutoffLux,
                              m_scheduled, m_scheduleFrom, m_scheduleTo, m_scheduleTransition })
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

void OwlfishSettings::publishAlsMultiplier(double multiplier)
{
    if (m_alsMultiplier->value().toDouble() != multiplier)
        m_alsMultiplier->set(multiplier);
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
    , m_scheduleFrom(clampMinuteOfDay(env("schedule_from", DefaultFrom), DefaultFrom))
    , m_scheduleTo(clampMinuteOfDay(env("schedule_to", DefaultTo), DefaultTo))
    , m_scheduleTransition(clampTransition(env("schedule_transition", DefaultTransition)))
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

void OwlfishSettings::publishAlsMultiplier(double)
{
}

#endif
