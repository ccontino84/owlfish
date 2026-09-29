// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef OWLFISH_CONTROLLER_H
#define OWLFISH_CONTROLLER_H

#include "colorfilteritem.h"
#include "crashguard.h"
#include "schedule.h"
#include "sun.h"
#include "timezonelocation.h"

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantAnimation>
#include <QVector3D>

class QQuickWindow;
class QLightSensor;
class AmbientCutoff;
class OwlfishSettings;

// Runs inside the compositor process: waits for the compositor's QQuickWindow,
// puts a ColorFilterItem on top of its scene and drives it from the settings.
// The schedule only affects the colour, the ambient light cut-off only the
// dimming. With a sunset to sunrise schedule it also publishes today's sun
// times for the settings page.
class OwlfishController : public QObject
{
    Q_OBJECT

public:
    explicit OwlfishController(const QByteArray &windowClass,
                                const QString &crashGuardPath = CrashGuard::defaultFilePath(),
                                QObject *parent = nullptr);

    bool isActive() const { return m_active; }
    // For the settings page: "active", "starting" (still looking for the
    // window), "no-window" or "crash-guard"
    QString status() const;
    // Lets the plugin try again at the next compositor start
    void resetCrashGuard();
    ColorFilterItem *filterItem() const { return m_item; }
    AmbientCutoff *cutoff() const { return m_cutoff; }
    OwlfishSettings *settings() const { return m_settings; }

    // Warm tint, colourStrength of the way from neutral to the temperature,
    // times the dimming
    static QVector3D filterGain(qreal colourStrength, int temperature, int dimPercent);
    // 1 without a schedule; with one, following it and its transitions
    static qreal colourStrength(bool scheduled, const OwlfishSchedule &schedule,
                                const QDateTime &now);

private slots:
    void findWindow();
    void updateItemGeometry();
    void applySettings();
    void cutoffChanged(bool bright);
    void animate(const QVariant &progress);
    void scheduleTick();
    void displayStatusChanged(const QString &status);

private:
    void attach(QQuickWindow *window);
    void updateLightSensor();
    OwlfishSchedule schedule() const;
    qreal currentColourStrength(const QDateTime &now) const;
    void updateSun();
    void publishSun();
    void updateScheduleTimer();
    void updateGain(int fadeMs);
    void fadeTo(const QVector3D &gain, int durationMs);

    QByteArray m_windowClass;
    CrashGuard m_guard;
    bool m_active;
    OwlfishSettings *m_settings;
    AmbientCutoff *m_cutoff;
    QLightSensor *m_lightSensor;
    double m_alsMultiplier;
    QTimer m_findTimer;
    int m_findAttempts;
    QTimer m_healthyTimer;
    QTimer m_scheduleTimer;
    bool m_displayOn;
    QPointer<QQuickWindow> m_window;
    QPointer<ColorFilterItem> m_item;
    QVariantAnimation m_animation;
    QVector3D m_fromGain;
    QVector3D m_toGain;
    // The time zone's city, looked up again when the zone changes
    TimeZoneLocation m_zoneLocation;
    // Today's sun times, for the date, zone and location they were
    // calculated for
    QDate m_sunDate;
    QString m_sunZone;
    bool m_sunLocated;
    double m_sunLatitude;
    double m_sunLongitude;
    SunTimes::State m_sunState;
    // Minutes after local midnight
    int m_sunset;
    int m_sunrise;
};

#endif
