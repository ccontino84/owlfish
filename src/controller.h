// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef OWLFISH_CONTROLLER_H
#define OWLFISH_CONTROLLER_H

#include "colorfilteritem.h"
#include "crashguard.h"
#include "pqdisplay.h"
#include "schedule.h"
#include "sun.h"
#include "timezonelocation.h"

#include <QObject>
#include <QPointer>
#include <QScopedPointer>
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
//
// On verified devices the display hardware applies the filter instead
// (PqDisplay): the item then only holds the gain and is hidden. The hardware
// keeps its matrix after the compositor is gone, so it is reset on quit, and
// the whole state is sent again at every start. If a call fails, the item
// takes over until the next start.
class OwlfishController : public QObject
{
    Q_OBJECT

public:
    explicit OwlfishController(const QByteArray &windowClass,
                                const QString &crashGuardPath = CrashGuard::defaultFilePath(),
                                QObject *parent = nullptr);
    // Leaves the display hardware at identity
    ~OwlfishController() override;

    // For tests: the display hardware's service and the file the device is
    // identified by. Before the window is found; takes ownership.
    void setDisplayHardware(PqDisplay::Backend *backend, const QString &hwReleasePath);

    bool isActive() const { return m_active; }
    // For the settings page: "active", "starting" (still looking for the
    // window), "no-window" or "crash-guard"
    QString status() const;
    // Lets the plugin try again at the next compositor start
    void resetCrashGuard();
    // "pq" when the display hardware applies the filter, "blend" when the
    // item draws it, "none" before the window is found
    QString renderer() const;
    // For support, several lines: version, status, the renderer and why it
    // was chosen
    QString diagnostics() const;
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
    void stopPq();

private:
    void attach(QQuickWindow *window);
    void updateLightSensor();
    // The light level sensorfw already has, for the cut-off
    void queryCurrentLux();
    // The most the light sensor can report, for the cut-off and the page
    void querySensorMaximum();
    void updateRenderer();
    // The item's gain, to the display hardware
    void pushPq();
    // A call failed: the item takes over until the next start
    void pqFailed();
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
    // Counts the sensor's starts and stops, so that a reply to an older
    // queryCurrentLux() is ignored
    int m_luxQuery;
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
    QScopedPointer<PqDisplay> m_pq;
    QString m_hwReleasePath;
    // The display hardware applies the filter instead of the item
    bool m_pqActive;
    // A call failed in this run
    bool m_pqFailed;
    QString m_rendererReason;
    // The reason says which device this is, so diagnostics() leaves that
    // line out
    bool m_reasonNamesDevice;
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
