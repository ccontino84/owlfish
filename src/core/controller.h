// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef OWLFISH_CONTROLLER_H
#define OWLFISH_CONTROLLER_H

#include "crashguard.h"
#include "gpuoutput.h"
#include "schedule.h"
#include "sun.h"
#include "timezonelocation.h"

#include <QGenericMatrix>
#include <QList>
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

// Runs inside the compositor process: waits for the compositor's QQuickWindow
// and drives the filter from the settings. The schedule only affects the
// colour, the ambient light cut-off only the dimming. With a sunset to
// sunrise schedule it also publishes today's sun times for the settings page.
//
// The filter (a gain and a matrix) is faded here and applied by one output
// (FilterOutput): the first display hardware output that applies to the
// device and works, the GPU otherwise (GpuOutput). The GPU always gets the
// filter too, hidden while the hardware applies it, so it can take over at
// once. If a hardware call fails, the GPU takes over until the next start.
// The hardware is left neutral on quit, and the whole state is sent again
// at every start. Only the constructor knows which kinds of hardware exist.
class OwlfishController : public QObject
{
    Q_OBJECT

public:
    explicit OwlfishController(const QByteArray &windowClass,
                                const QString &crashGuardPath = CrashGuard::defaultFilePath(),
                                QObject *parent = nullptr);
    // Leaves the display hardware at identity
    ~OwlfishController() override;

    // For tests: the display hardware outputs, in the order they are tried,
    // and the file the device is identified by. Before the window is found;
    // takes ownership.
    void setHardwareOutputs(const QList<FilterOutput *> &outputs, const QString &hwReleasePath);

    bool isActive() const { return m_active; }
    // For the settings page: "active", "starting" (still looking for the
    // window), "no-window" or "crash-guard"
    QString status() const;
    // Lets the plugin try again at the next compositor start
    void resetCrashGuard();
    // The active output's name: "pq" or "qualcomm" (display hardware),
    // "fetch" or "blend" (the GPU; fetch draws only while the matrix isn't
    // the identity); "none" before the window is found
    QString renderer() const;
    // For support, several lines: version, status, the renderer and why it
    // was chosen
    QString diagnostics() const;
    // The GPU's item; it always has the filter, also while hidden
    ColorFilterItem *filterItem() const { return m_gpu ? m_gpu->item() : nullptr; }
    AmbientCutoff *cutoff() const { return m_cutoff; }
    OwlfishSettings *settings() const { return m_settings; }

    // Warm tint, colourStrength of the way from neutral to the temperature,
    // times the dimming
    static QVector3D filterGain(qreal colourStrength, int temperature, qreal dimPercent);
    // The saturation factor, colourStrength of the way from 1 to the setting
    static qreal saturationFactor(qreal colourStrength, int saturationPercent);
    // 1 without a schedule; with one, following it and its transitions
    static qreal colourStrength(bool scheduled, const OwlfishSchedule &schedule,
                                const QDateTime &now);

private slots:
    void findWindow();
    void applySettings();
    void cutoffChanged(bool bright);
    void animate(const QVariant &progress);
    void scheduleTick();
    void displayStatusChanged(const QString &status);
    // The display hardware back to neutral
    void stopHardware();
    // An output's check was answered: a better one, or the matrix, may
    // apply now
    void outputChecked();
    // The display shows the change with the next frame
    void outputApplied();
    void outputFailed();

private:
    // The active output can apply the correction and the saturation
    bool matrixSupported() const;
    // The correction, then Night light's saturation, where they can be
    // applied; the identity otherwise
    QMatrix3x3 colourMatrix(qreal colourStrength) const;
    void attach(QQuickWindow *window);
    void updateLightSensor();
    // The light level sensorfw already has, for the cut-off
    void queryCurrentLux();
    // The most the light sensor can report, for the cut-off and the page
    void querySensorMaximum();
    void updateRenderer();
    void connectOutput(FilterOutput *output);
    // The filter to the GPU and to the active output
    void pushFilter();
    // The colour's window, with its transitions or another length of them
    OwlfishSchedule schedule() const;
    OwlfishSchedule schedule(int transitionMinutes) const;
    qreal currentColourStrength(const QDateTime &now) const;
    // 0 or 1: whether the dimming applies now, by the dim_when setting
    qreal currentDimStrength(const QDateTime &now) const;
    void updateSun();
    void publishSun();
    void updateScheduleTimer();
    void updateGain(int fadeMs);
    void fadeTo(const QVector3D &gain, const QMatrix3x3 &matrix, int durationMs);

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
    QVariantAnimation m_animation;
    // The filter: the gain, encoded as the colour temperature and the
    // dimming give it
    QVector3D m_gain;
    QVector3D m_fromGain;
    QVector3D m_toGain;
    // The correction and the saturation, faded like the gain; applied by the
    // display hardware or Fetch, before the gain
    QMatrix3x3 m_matrix;
    QMatrix3x3 m_fromMatrix;
    QMatrix3x3 m_toMatrix;
    // For the device line of the diagnostics
    QString m_hwReleasePath;
    // The display hardware, in the order it is tried; children
    QList<FilterOutput *> m_hardware;
    // The fallback, from the window on
    QScopedPointer<GpuOutput> m_gpu;
    // The one applying the filter; null before the window is found
    FilterOutput *m_output;
    // Why a display hardware call failed in this run; the GPU until the
    // next start
    QString m_failure;
    QString m_rendererReason;
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
