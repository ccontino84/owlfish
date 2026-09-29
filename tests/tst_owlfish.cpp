// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "alscalibration.h"
#include "ambientcutoff.h"
#include "colorfilteritem.h"
#include "colortemperature.h"
#include "controller.h"
#include "crashguard.h"
#include "schedule.h"
#include "settings.h"
#include "statusservice.h"
#include "sun.h"
#include "timezonelocation.h"

#include <QDBusInterface>
#include <QDBusReply>
#include <QGenericPluginFactory>
#include <QProcess>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include <cmath>

namespace {

// Four 50x100 columns: white, black, mid grey, orange. "above" is a
// half-transparent red square that tests may raise over the filter.
const char *const SceneQml =
        "import QtQuick 2.0\n"
        "Item {\n"
        "    width: 200; height: 100\n"
        "    Rectangle { x: 0;   width: 50; height: 100; color: '#ffffff' }\n"
        "    Rectangle { x: 50;  width: 50; height: 100; color: '#000000' }\n"
        "    Rectangle { x: 100; width: 50; height: 100; color: '#808080' }\n"
        "    Rectangle { x: 150; width: 50; height: 100; color: '#ff8040' }\n"
        "    Rectangle { objectName: 'above'; visible: false; z: 20\n"
        "                x: 0; y: 0; width: 50; height: 50; color: '#80ff0000' }\n"
        "}\n";

const QPoint White(25, 75);
const QPoint Black(75, 75);
const QPoint Grey(125, 75);
const QPoint Orange(175, 75);
const QPoint Above(25, 25);

}

class tst_Owlfish : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void identityHasNoEffect();
    void uniformDim();
    void nightGainKeepsBlack();
    void blendStateRestored();
    void gainIsClamped();
    void filterGain();
    void temperatureGain_data();
    void temperatureGain();
    void temperatureGainRange();
    void partialTemperature();
    void nightScheduleStrength_data();
    void nightScheduleStrength();
    void nightScheduleShortWindow();
    void nightScheduleLastEdge_data();
    void nightScheduleLastEdge();
    void colourStrength();
    void sunTimes_data();
    void sunTimes();
    void timeZoneCoordinates_data();
    void timeZoneCoordinates();
    void timeZoneLookup();
    void timeZoneSystemData();
    void timeZoneLinks();
    void crashGuard();
    void status();
    void statusCrashGuard();
    void statusOnDBus();
    void updateEnv_data();
    void updateEnv();
    void cutoffFirstReadingAppliesImmediately();
    void cutoffHysteresis();
    void cutoffThresholdChangeAppliesAtOnce();
    void cutoffDebounce();
    void cutoffReset();
    void alsMultiplier();
    void cutoffLuxRange();
    void pluginAttachesFilter();
    void pluginCombinesTintAndDim();
    void pluginScheduleOnlyAffectsColour();
    void pluginSunSchedule();
    void pluginPolarSun();
    void pluginIgnoresOtherProcesses();

private:
    QQuickView *createView();
    QImage grab(QQuickView *view);
    void compare(const QImage &image, const QPoint &pos, const QColor &expected);

    QTemporaryDir m_cacheDir;
};

void tst_Owlfish::initTestCase()
{
    QVERIFY(m_cacheDir.isValid());
    qputenv("XDG_CACHE_HOME", m_cacheDir.path().toLocal8Bit());
}

QQuickView *tst_Owlfish::createView()
{
    QQuickView *view = new QQuickView;
    view->setColor(Qt::white);
    view->setResizeMode(QQuickView::SizeViewToRootObject);
    view->setSource(QUrl(QStringLiteral("data:,") + QString::fromLatin1(QUrl::toPercentEncoding(SceneQml))));
    if (!view->errors().isEmpty())
        qWarning() << view->errors();
    view->show();
    if (!QTest::qWaitForWindowExposed(view)) {
        delete view;
        return nullptr;
    }
    return view;
}

QImage tst_Owlfish::grab(QQuickView *view)
{
    return view->grabWindow().convertToFormat(QImage::Format_RGB32);
}

void tst_Owlfish::compare(const QImage &image, const QPoint &pos, const QColor &expected)
{
    const QColor actual = image.pixelColor(pos);
    const int tolerance = 2;
    if (qAbs(actual.red() - expected.red()) > tolerance
            || qAbs(actual.green() - expected.green()) > tolerance
            || qAbs(actual.blue() - expected.blue()) > tolerance) {
        QFAIL(qPrintable(QStringLiteral("pixel (%1,%2) is %3, expected %4")
                         .arg(pos.x()).arg(pos.y()).arg(actual.name(), expected.name())));
    }
}

void tst_Owlfish::identityHasNoEffect()
{
    QScopedPointer<QQuickView> view(createView());
    QVERIFY(view);

    ColorFilterItem *filter = new ColorFilterItem(view->rootObject());
    filter->setSize(QSizeF(200, 100));
    filter->setZ(10);
    QVERIFY(filter->isIdentity());

    const QImage image = grab(view.data());
    compare(image, White, QColor(255, 255, 255));
    compare(image, Black, QColor(0, 0, 0));
    compare(image, Grey, QColor(128, 128, 128));
    compare(image, Orange, QColor(255, 128, 64));
}

void tst_Owlfish::uniformDim()
{
    QScopedPointer<QQuickView> view(createView());
    QVERIFY(view);

    ColorFilterItem *filter = new ColorFilterItem(view->rootObject());
    filter->setSize(QSizeF(200, 100));
    filter->setZ(10);
    filter->setGain(QVector3D(0.5f, 0.5f, 0.5f));

    const QImage image = grab(view.data());
    compare(image, White, QColor(128, 128, 128));
    compare(image, Black, QColor(0, 0, 0));
    compare(image, Grey, QColor(64, 64, 64));
    compare(image, Orange, QColor(128, 64, 32));
}

void tst_Owlfish::nightGainKeepsBlack()
{
    QScopedPointer<QQuickView> view(createView());
    QVERIFY(view);

    ColorFilterItem *filter = new ColorFilterItem(view->rootObject());
    filter->setSize(QSizeF(200, 100));
    filter->setZ(10);
    filter->setGain(QVector3D(1.0f, 0.8f, 0.6f));

    const QImage image = grab(view.data());
    compare(image, White, QColor(255, 204, 153));
    compare(image, Black, QColor(0, 0, 0));
    compare(image, Grey, QColor(128, 102, 77));
}

void tst_Owlfish::blendStateRestored()
{
    // Content drawn after the filter must be blended normally again
    QScopedPointer<QQuickView> view(createView());
    QVERIFY(view);

    ColorFilterItem *filter = new ColorFilterItem(view->rootObject());
    filter->setSize(QSizeF(200, 100));
    filter->setZ(10);
    filter->setGain(QVector3D(0.5f, 0.5f, 0.5f));

    QQuickItem *above = view->rootObject()->findChild<QQuickItem *>(QStringLiteral("above"));
    QVERIFY(above);
    above->setVisible(true);

    // 50% red over the filtered white (128): source-over gives (191, 64, 64);
    // a leaked multiply blend would give (64, 0, 0)
    const QImage image = grab(view.data());
    compare(image, Above, QColor(191, 64, 64));
    compare(image, White, QColor(128, 128, 128));
}

void tst_Owlfish::gainIsClamped()
{
    ColorFilterItem filter;
    filter.setGain(QVector3D(-1.0f, 2.0f, 0.5f));
    QCOMPARE(filter.gain(), QVector3D(0.0f, 1.0f, 0.5f));
}

void tst_Owlfish::filterGain()
{
    const QVector3D warm = ColorTemperature::gain(3400);

    // Neutral colour, no dimming: nothing to do
    QCOMPARE(OwlfishController::filterGain(1, 6500, 0), QVector3D(1, 1, 1));
    QCOMPARE(OwlfishController::filterGain(0, 3400, 0), QVector3D(1, 1, 1));
    // Only dimming
    QCOMPARE(OwlfishController::filterGain(1, 6500, 50), QVector3D(0.5f, 0.5f, 0.5f));
    QCOMPARE(OwlfishController::filterGain(0, 3400, 50), QVector3D(0.5f, 0.5f, 0.5f));
    // Never below 25% brightness
    QVERIFY(qFuzzyCompare(OwlfishController::filterGain(1, 6500, 100), QVector3D(0.25f, 0.25f, 0.25f)));
    // Tint times dimming
    QVERIFY(qFuzzyCompare(OwlfishController::filterGain(1, 3400, 50), warm * 0.5f));
    // Halfway through a colour transition: half the tint (in mireds), the
    // dimming unaffected
    QVERIFY(qFuzzyCompare(OwlfishController::filterGain(0.5, 3400, 50),
                          ColorTemperature::gain(ColorTemperature::partial(3400, 0.5)) * 0.5f));
}

void tst_Owlfish::temperatureGain_data()
{
    QTest::addColumn<int>("kelvin");
    QTest::addColumn<QVector3D>("expected");

    // redshift's colorramp.c table (sRGB-encoded, D65 at 6500 K), which the
    // same construction reproduces
    QTest::newRow("1900") << 1900 << QVector3D(1, 0.51943421f, 0.00000000f);
    QTest::newRow("2300") << 2300 << QVector3D(1, 0.60724493f, 0.22137978f);
    QTest::newRow("2700") << 2700 << QVector3D(1, 0.67645822f, 0.34786758f);
    QTest::newRow("3400") << 3400 << QVector3D(1, 0.76888303f, 0.52427322f);
    QTest::newRow("4500") << 4500 << QVector3D(1, 0.86860704f, 0.73688797f);
    QTest::newRow("5500") << 5500 << QVector3D(1, 0.93853986f, 0.88130458f);
    QTest::newRow("6000") << 6000 << QVector3D(1, 0.97107439f, 0.94305985f);
}

void tst_Owlfish::temperatureGain()
{
    QFETCH(int, kelvin);
    QFETCH(QVector3D, expected);

    const QVector3D gain = ColorTemperature::gain(kelvin);
    for (int i = 0; i < 3; ++i)
        QVERIFY2(qAbs(gain[i] - expected[i]) < 0.003f,
                 qPrintable(QStringLiteral("channel %1: %2, expected %3").arg(i).arg(gain[i]).arg(expected[i])));
}

void tst_Owlfish::temperatureGainRange()
{
    QCOMPARE(ColorTemperature::gain(6500), QVector3D(1, 1, 1));
    QCOMPARE(ColorTemperature::gain(9000), QVector3D(1, 1, 1));
    QCOMPARE(ColorTemperature::gain(1000), ColorTemperature::gain(1900));

    // Warmer is always less green and less blue, never less red
    QVector3D previous = ColorTemperature::gain(6500);
    for (int kelvin = 6400; kelvin >= 1900; kelvin -= 100) {
        const QVector3D gain = ColorTemperature::gain(kelvin);
        QCOMPARE(gain.x(), 1.0f);
        QVERIFY(gain.y() < previous.y());
        QVERIFY(gain.z() < previous.z() || gain.z() == 0);
        QVERIFY(gain.z() >= 0);
        previous = gain;
    }
    // Close to neutral is (almost) no filter, so the node goes away
    ColorFilterItem filter;
    filter.setGain(ColorTemperature::gain(6499));
    QVERIFY(filter.isIdentity());
}

void tst_Owlfish::partialTemperature()
{
    QCOMPARE(ColorTemperature::partial(2700, 0), qreal(6500));
    QCOMPARE(ColorTemperature::partial(2700, -1), qreal(6500));
    QCOMPARE(ColorTemperature::partial(2700, 1), qreal(2700));
    QCOMPARE(ColorTemperature::partial(1000, 1), qreal(1900));
    // Halfway in mireds: 1e6 / ((153.85 + 370.37) / 2)
    QVERIFY(qAbs(ColorTemperature::partial(2700, 0.5) - 3815.3) < 0.5);
}

void tst_Owlfish::nightScheduleStrength_data()
{
    QTest::addColumn<QTime>("time");
    QTest::addColumn<bool>("contains");
    QTest::addColumn<qreal>("strength");

    // 21:00 to 07:00, 30 min transitions
    QTest::newRow("before") << QTime(20, 59) << false << qreal(0);
    QTest::newRow("start") << QTime(21, 0) << true << qreal(0);
    QTest::newRow("fading in") << QTime(21, 15) << true << qreal(0.5);
    QTest::newRow("full") << QTime(21, 30) << true << qreal(1);
    QTest::newRow("midnight") << QTime(0, 0) << true << qreal(1);
    QTest::newRow("night") << QTime(3, 0) << true << qreal(1);
    QTest::newRow("fading out") << QTime(6, 45) << true << qreal(0.5);
    QTest::newRow("almost over") << QTime(6, 59, 30) << true << qreal(1.0 / 60);
    QTest::newRow("end") << QTime(7, 0) << false << qreal(0);
    QTest::newRow("day") << QTime(12, 0) << false << qreal(0);
}

void tst_Owlfish::nightScheduleStrength()
{
    QFETCH(QTime, time);
    QFETCH(bool, contains);
    QFETCH(qreal, strength);

    const OwlfishSchedule schedule(21 * 60, 7 * 60, 30);
    const QDateTime now(QDate(2026, 1, 15), time);
    QCOMPARE(schedule.contains(now), contains);
    QVERIFY(qAbs(schedule.strength(now) - strength) < 1e-6);
}

void tst_Owlfish::nightScheduleShortWindow()
{
    const QDate date(2026, 1, 15);

    // Transitions longer than half the window are shortened to fit
    const OwlfishSchedule shortWindow(13 * 60, 14 * 60, 60);
    QCOMPARE(shortWindow.strength(QDateTime(date, QTime(13, 15))), qreal(0.5));
    QCOMPARE(shortWindow.strength(QDateTime(date, QTime(13, 30))), qreal(1));
    QCOMPARE(shortWindow.strength(QDateTime(date, QTime(13, 45))), qreal(0.5));

    // No transition: fully on for the whole window
    const OwlfishSchedule instant(22 * 60, 6 * 60, 0);
    QCOMPARE(instant.strength(QDateTime(date, QTime(22, 0))), qreal(1));
    QCOMPARE(instant.strength(QDateTime(date, QTime(5, 59))), qreal(1));
    QCOMPARE(instant.strength(QDateTime(date, QTime(6, 0))), qreal(0));

    // Same start and end: no window at all
    const OwlfishSchedule empty(22 * 60, 22 * 60, 30);
    QVERIFY(empty.isEmpty());
    QVERIFY(!empty.contains(QDateTime(date, QTime(22, 0))));
    QVERIFY(!empty.lastEdge(QDateTime(date, QTime(23, 0))).time.isValid());
}

void tst_Owlfish::nightScheduleLastEdge_data()
{
    QTest::addColumn<int>("from");
    QTest::addColumn<int>("to");
    QTest::addColumn<QDateTime>("now");
    QTest::addColumn<QDateTime>("edgeTime");
    QTest::addColumn<bool>("edgeOn");

    const QDate today(2026, 1, 15);
    const QDate yesterday = today.addDays(-1);
    const int night = 21 * 60, morning = 7 * 60;

    QTest::newRow("evening") << night << morning << QDateTime(today, QTime(22, 0))
                             << QDateTime(today, QTime(21, 0)) << true;
    QTest::newRow("exactly at start") << night << morning << QDateTime(today, QTime(21, 0))
                                      << QDateTime(today, QTime(21, 0)) << true;
    QTest::newRow("after midnight") << night << morning << QDateTime(today, QTime(6, 0))
                                    << QDateTime(yesterday, QTime(21, 0)) << true;
    QTest::newRow("morning") << night << morning << QDateTime(today, QTime(8, 0))
                             << QDateTime(today, QTime(7, 0)) << false;
    QTest::newRow("afternoon") << night << morning << QDateTime(today, QTime(20, 0))
                               << QDateTime(today, QTime(7, 0)) << false;
    QTest::newRow("day window, before") << 13 * 60 << 14 * 60 << QDateTime(today, QTime(9, 0))
                                        << QDateTime(yesterday, QTime(14, 0)) << false;
    QTest::newRow("day window, inside") << 13 * 60 << 14 * 60 << QDateTime(today, QTime(13, 30))
                                        << QDateTime(today, QTime(13, 0)) << true;
}

void tst_Owlfish::nightScheduleLastEdge()
{
    QFETCH(int, from);
    QFETCH(int, to);
    QFETCH(QDateTime, now);
    QFETCH(QDateTime, edgeTime);
    QFETCH(bool, edgeOn);

    const OwlfishSchedule::Edge edge = OwlfishSchedule(from, to, 30).lastEdge(now);
    QCOMPARE(edge.time, edgeTime);
    QCOMPARE(edge.on, edgeOn);
}

void tst_Owlfish::colourStrength()
{
    const OwlfishSchedule schedule(21 * 60, 7 * 60, 30);
    const QDate date(2026, 1, 15);
    const QDateTime fadingIn(date, QTime(21, 15));
    const QDateTime night(date, QTime(2, 0));
    const QDateTime day(date, QTime(12, 0));

    // Without a schedule the colour is always on
    QCOMPARE(OwlfishController::colourStrength(false, schedule, day), qreal(1));
    // With one it follows the window and its transitions
    QCOMPARE(OwlfishController::colourStrength(true, schedule, day), qreal(0));
    QCOMPARE(OwlfishController::colourStrength(true, schedule, fadingIn), qreal(0.5));
    QCOMPARE(OwlfishController::colourStrength(true, schedule, night), qreal(1));
}

void tst_Owlfish::sunTimes_data()
{
    QTest::addColumn<QDate>("date");
    QTest::addColumn<double>("latitude");
    QTest::addColumn<double>("longitude");
    QTest::addColumn<QByteArray>("zone");
    QTest::addColumn<int>("state");
    // Local times, minutes after midnight
    QTest::addColumn<int>("sunrise");
    QTest::addColumn<int>("sunset");
    QTest::addColumn<int>("tolerance");

    // Reference values from the US Naval Observatory's API
    // (aa.usno.navy.mil/api/rstt/oneday, fetched 2026-09-28), which uses the
    // same definition of sunrise and sunset. USNO lists events by calendar
    // date; SunTimes gives this evening's sunset, which in the far north can
    // be after midnight: there the sunset is the next date's listed one.
    const int normal = SunTimes::Normal;
    auto at = [](int hour, int minute) { return hour * 60 + minute; };
    const double berlin[] = { 52.52, 13.405 };
    const double sydney[] = { -33.8688, 151.2093 };
    const double tromso[] = { 69.6496, 18.956 };

    QTest::newRow("Berlin, summer solstice") << QDate(2026, 6, 21) << berlin[0] << berlin[1]
            << QByteArray("Europe/Berlin") << normal << at(4, 43) << at(21, 33) << 2;
    QTest::newRow("Berlin, winter solstice") << QDate(2026, 12, 21) << berlin[0] << berlin[1]
            << QByteArray("Europe/Berlin") << normal << at(8, 15) << at(15, 54) << 2;
    QTest::newRow("Berlin, DST starts") << QDate(2026, 3, 29) << berlin[0] << berlin[1]
            << QByteArray("Europe/Berlin") << normal << at(6, 48) << at(19, 35) << 2;
    QTest::newRow("Berlin, DST ends") << QDate(2026, 10, 25) << berlin[0] << berlin[1]
            << QByteArray("Europe/Berlin") << normal << at(6, 50) << at(16, 50) << 2;
    QTest::newRow("Sydney, June") << QDate(2026, 6, 21) << sydney[0] << sydney[1]
            << QByteArray("Australia/Sydney") << normal << at(7, 0) << at(16, 54) << 2;
    QTest::newRow("Sydney, December") << QDate(2026, 12, 21) << sydney[0] << sydney[1]
            << QByteArray("Australia/Sydney") << normal << at(5, 41) << at(20, 5) << 2;
    QTest::newRow("Sydney, DST starts") << QDate(2026, 10, 4) << sydney[0] << sydney[1]
            << QByteArray("Australia/Sydney") << normal << at(6, 29) << at(19, 0) << 2;
    QTest::newRow("New York, DST starts") << QDate(2026, 3, 8) << 40.7128 << -74.006
            << QByteArray("America/New_York") << normal << at(7, 19) << at(18, 55) << 2;
    QTest::newRow("Quito, equator") << QDate(2026, 9, 28) << -0.1807 << -78.4678
            << QByteArray("America/Guayaquil") << normal << at(6, 1) << at(18, 8) << 2;
    QTest::newRow("Ushuaia, June") << QDate(2026, 6, 21) << -54.8019 << -68.303
            << QByteArray("America/Argentina/Ushuaia") << normal << at(9, 59) << at(17, 11) << 2;
    QTest::newRow("Oulu, sunset after midnight") << QDate(2026, 6, 21) << 65.0121 << 25.4651
            << QByteArray("Europe/Helsinki") << normal << at(2, 19) << at(0, 21) << 3;
    QTest::newRow("Reykjavik, sunset after midnight") << QDate(2026, 6, 21) << 64.1466 << -21.9426
            << QByteArray("Atlantic/Reykjavik") << normal << at(2, 55) << at(0, 4) << 3;
    // The last night before the midnight sun: 50 minutes long
    QTest::newRow("Tromso, last night") << QDate(2026, 5, 17) << tromso[0] << tromso[1]
            << QByteArray("Europe/Oslo") << normal << at(1, 18) << at(0, 28) << 3;
    QTest::newRow("Tromso, midnight sun begins") << QDate(2026, 5, 19) << tromso[0] << tromso[1]
            << QByteArray("Europe/Oslo") << int(SunTimes::PolarDay) << 0 << 0 << 0;
    QTest::newRow("Tromso, midnight sun") << QDate(2026, 6, 21) << tromso[0] << tromso[1]
            << QByteArray("Europe/Oslo") << int(SunTimes::PolarDay) << 0 << 0 << 0;
    QTest::newRow("Tromso, short day") << QDate(2026, 11, 25) << tromso[0] << tromso[1]
            << QByteArray("Europe/Oslo") << normal << at(10, 43) << at(12, 18) << 3;
    QTest::newRow("Tromso, polar night begins") << QDate(2026, 11, 28) << tromso[0] << tromso[1]
            << QByteArray("Europe/Oslo") << int(SunTimes::PolarNight) << 0 << 0 << 0;
    QTest::newRow("Tromso, polar night") << QDate(2026, 12, 21) << tromso[0] << tromso[1]
            << QByteArray("Europe/Oslo") << int(SunTimes::PolarNight) << 0 << 0 << 0;
}

void tst_Owlfish::sunTimes()
{
    QFETCH(QDate, date);
    QFETCH(double, latitude);
    QFETCH(double, longitude);
    QFETCH(QByteArray, zone);
    QFETCH(int, state);
    QFETCH(int, sunrise);
    QFETCH(int, sunset);
    QFETCH(int, tolerance);

    const QTimeZone timeZone(zone);
    QVERIFY(timeZone.isValid());

    const SunTimes sun = SunTimes::compute(date, latitude, longitude);
    QCOMPARE(int(sun.state()), state);
    if (state != SunTimes::Normal) {
        QVERIFY(!sun.sunrise().isValid());
        QVERIFY(!sun.sunset().isValid());
        return;
    }

    auto check = [&](const char *name, const QDateTime &time, int expected) {
        const int minutes = SunTimes::minuteOfDay(time, timeZone);
        const int day = 24 * 60;
        const int difference = qAbs(((minutes - expected) % day + day + day / 2) % day - day / 2);
        QVERIFY2(difference <= tolerance,
                 qPrintable(QStringLiteral("%1 at %2:%3, expected %4:%5")
                            .arg(QLatin1String(name))
                            .arg(minutes / 60).arg(minutes % 60, 2, 10, QLatin1Char('0'))
                            .arg(expected / 60).arg(expected % 60, 2, 10, QLatin1Char('0'))));
    };
    check("sunrise", sun.sunrise(), sunrise);
    check("sunset", sun.sunset(), sunset);
    // The sunset follows the sunrise, less than a day later
    QVERIFY(sun.sunrise() < sun.sunset());
    QVERIFY(sun.sunrise().secsTo(sun.sunset()) < 24 * 3600);
}

void tst_Owlfish::timeZoneCoordinates_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("valid");
    QTest::addColumn<double>("latitude");
    QTest::addColumn<double>("longitude");

    QTest::newRow("minutes") << "+5230+01322" << true << 52.5 << 13 + 22 / 60.0;
    QTest::newRow("south east") << "-3352+15113" << true << -(33 + 52 / 60.0) << 151 + 13 / 60.0;
    QTest::newRow("seconds") << "+404251-0740023" << true
                             << 40 + 42 / 60.0 + 51 / 3600.0 << -(74 + 0 / 60.0 + 23 / 3600.0);
    QTest::newRow("mixed") << "-5448-06818" << true << -(54 + 48 / 60.0) << -(68 + 18 / 60.0);
    QTest::newRow("short longitude") << "+5230+1322" << false << 0.0 << 0.0;
    QTest::newRow("no sign") << "5230+01322" << false << 0.0 << 0.0;
    QTest::newRow("beyond the pole") << "+9100+00000" << false << 0.0 << 0.0;
    QTest::newRow("empty") << "" << false << 0.0 << 0.0;
}

void tst_Owlfish::timeZoneCoordinates()
{
    QFETCH(QString, text);
    QFETCH(bool, valid);
    QFETCH(double, latitude);
    QFETCH(double, longitude);

    double lat = 0, lon = 0;
    QCOMPARE(TimeZoneLocation::parseCoordinates(text, &lat, &lon), valid);
    if (valid) {
        QVERIFY(qAbs(lat - latitude) < 1e-9);
        QVERIFY(qAbs(lon - longitude) < 1e-9);
    }
}

void tst_Owlfish::timeZoneLookup()
{
    QTemporaryDir root;
    QVERIFY(QDir(root.path()).mkpath(QStringLiteral("usr/share/zoneinfo")));
    auto write = [&root](const char *name, const QByteArray &content) {
        QFile file(root.filePath(QStringLiteral("usr/share/zoneinfo/") + QLatin1String(name)));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(content);
    };
    // Excerpts in tzdata's format; zone1970.tab has no Oslo, and its Berlin
    // line covers several countries
    write("zone.tab",
          "# comment\tline\n"
          "DE\t+5230+01322\tEurope/Berlin\tmost of Germany\n"
          "NO\t+5955+01045\tEurope/Oslo\n"
          "AR\t-3436-05827\tAmerica/Argentina/Buenos_Aires\tBuenos Aires (BA, CF)\n");
    write("zone1970.tab",
          "DE,DK,NO,SE,SJ\t+5230+01322\tEurope/Berlin\tmost of Germany\n"
          "CH,DE,LI\t+4723+00832\tEurope/Zurich\tBüsingen\n");

    const TimeZoneLocation oslo(QStringLiteral("Europe/Oslo"), root.path());
    QVERIFY(oslo.isValid());
    QCOMPARE(oslo.place(), QStringLiteral("Oslo"));
    QVERIFY(qAbs(oslo.latitude() - (59 + 55 / 60.0)) < 1e-9);
    QVERIFY(qAbs(oslo.longitude() - (10 + 45 / 60.0)) < 1e-9);

    // Only in zone1970.tab
    const TimeZoneLocation zurich(QStringLiteral("Europe/Zurich"), root.path());
    QVERIFY(zurich.isValid());
    QVERIFY(qAbs(zurich.latitude() - (47 + 23 / 60.0)) < 1e-9);

    const TimeZoneLocation buenosAires(QStringLiteral("America/Argentina/Buenos_Aires"), root.path());
    QVERIFY(buenosAires.isValid());
    QCOMPARE(buenosAires.place(), QStringLiteral("Buenos Aires"));
    QVERIFY(buenosAires.latitude() < 0 && buenosAires.longitude() < 0);

    // No location
    QVERIFY(!TimeZoneLocation(QStringLiteral("UTC"), root.path()).isValid());
    QVERIFY(!TimeZoneLocation(QString(), root.path()).isValid());
    QVERIFY(!TimeZoneLocation(QStringLiteral("Europe/Berlin"),
                              root.filePath(QStringLiteral("missing"))).isValid());
}

void tst_Owlfish::timeZoneSystemData()
{
    // The host's own tzdata, if it has the same files as Sailfish OS
    if (!QFile::exists(QStringLiteral("/usr/share/zoneinfo/zone.tab")))
        QSKIP("No zone.tab on this system");
    const TimeZoneLocation berlin(QStringLiteral("Europe/Berlin"));
    QVERIFY(berlin.isValid());
    QVERIFY(qAbs(berlin.latitude() - 52.5) < 0.1);
    QVERIFY(qAbs(berlin.longitude() - 13.37) < 0.1);
    // A zone.tab line of its own, not Berlin's from zone1970.tab
    const TimeZoneLocation oslo(QStringLiteral("Europe/Oslo"));
    QVERIFY(oslo.isValid());
    QVERIFY(qAbs(oslo.latitude() - 59.9) < 0.1);
}

void tst_Owlfish::timeZoneLinks()
{
    QTemporaryDir root;
    QDir dir(root.path());
    for (const char *path : { "etc", "var/lib/timed", "usr/share/zoneinfo/Europe",
                              "usr/share/zoneinfo/posix/Europe" })
        QVERIFY(dir.mkpath(QLatin1String(path)));
    QFile berlin(dir.filePath(QStringLiteral("usr/share/zoneinfo/Europe/Berlin")));
    QVERIFY(berlin.open(QIODevice::WriteOnly));
    berlin.close();
    const QString localtime = dir.filePath(QStringLiteral("etc/localtime"));
    const QString timed = dir.filePath(QStringLiteral("var/lib/timed/localtime"));

    // No /etc/localtime, or a plain file
    QCOMPARE(TimeZoneLocation::systemZone(root.path()), QString());

    // Sailfish OS: through timed's link. Oslo is an alias installed as a
    // link to Berlin's file; resolving the whole chain would give Berlin.
    QVERIFY(QFile::link(dir.filePath(QStringLiteral("usr/share/zoneinfo/Europe/Berlin")),
                        dir.filePath(QStringLiteral("usr/share/zoneinfo/Europe/Oslo"))));
    QVERIFY(QFile::link(dir.filePath(QStringLiteral("usr/share/zoneinfo/Europe/Oslo")), timed));
    QVERIFY(QFile::link(timed, localtime));
    QCOMPARE(TimeZoneLocation::systemZone(root.path()), QStringLiteral("Europe/Oslo"));

    // A relative link, into the posix variant
    QVERIFY(QFile::remove(localtime));
    QVERIFY(QFile::link(QStringLiteral("../usr/share/zoneinfo/posix/Europe/Rome"), localtime));
    QCOMPARE(TimeZoneLocation::systemZone(root.path()), QStringLiteral("Europe/Rome"));

    // A loop ends
    QVERIFY(QFile::remove(localtime));
    QVERIFY(QFile::remove(timed));
    QVERIFY(QFile::link(timed, localtime));
    QVERIFY(QFile::link(localtime, timed));
    QCOMPARE(TimeZoneLocation::systemZone(root.path()), QString());

    // A plain file
    QVERIFY(QFile::remove(localtime));
    QFile plain(localtime);
    QVERIFY(plain.open(QIODevice::WriteOnly));
    plain.close();
    QCOMPARE(TimeZoneLocation::systemZone(root.path()), QString());
}

void tst_Owlfish::crashGuard()
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/sub/starts");

    CrashGuard guard(path, 3);
    QVERIFY(guard.begin());
    QVERIFY(guard.begin());
    QVERIFY(guard.begin());
    QCOMPARE(guard.unhealthyStarts(), 3);
    QVERIFY(!guard.begin());

    guard.markHealthy();
    QCOMPARE(guard.unhealthyStarts(), 0);
    QVERIFY(guard.begin());
}

void tst_Owlfish::status()
{
    QTemporaryDir dir;
    QScopedPointer<OwlfishController> controller(new OwlfishController(
            "QQuickView", dir.filePath(QStringLiteral("unhealthy-starts"))));
    // Owned by the controller, as in the plugin
    OwlfishStatusService *service = new OwlfishStatusService(controller.data());
    QCOMPARE(service->status(), QStringLiteral("starting"));

    QScopedPointer<QQuickView> view(createView());
    QVERIFY(view);
    QTRY_COMPARE(service->status(), QStringLiteral("active"));
}

void tst_Owlfish::statusCrashGuard()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("unhealthy-starts"));
    CrashGuard guard(path, 3);
    for (int i = 0; i < 3; ++i)
        QVERIFY(guard.begin());

    QScopedPointer<OwlfishController> controller(new OwlfishController("QQuickView", path));
    // Owned by the controller, as in the plugin
    OwlfishStatusService *service = new OwlfishStatusService(controller.data());
    QCOMPARE(service->status(), QStringLiteral("crash-guard"));

    // Takes effect at the next start
    service->resetCrashGuard();
    QCOMPARE(guard.unhealthyStarts(), 0);
    QCOMPARE(service->status(), QStringLiteral("crash-guard"));
}

void tst_Owlfish::statusOnDBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        QSKIP("No session bus");

    QTemporaryDir dir;
    QScopedPointer<OwlfishController> controller(new OwlfishController(
            "QQuickView", dir.filePath(QStringLiteral("unhealthy-starts"))));
    OwlfishStatusService *service = new OwlfishStatusService(controller.data());
    QVERIFY(service->registerOn(bus));

    // What the settings page calls
    QDBusInterface remote(QLatin1String(OwlfishStatusService::ServiceName),
                          QLatin1String(OwlfishStatusService::ObjectPath),
                          QStringLiteral("io.github.ccontino84.owlfish"), bus);
    QDBusReply<QString> reply = remote.call(QStringLiteral("status"));
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    QCOMPARE(reply.value(), QStringLiteral("starting"));
    reply = remote.call(QStringLiteral("version"));
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    QCOMPARE(reply.value(), QStringLiteral(OWLFISH_VERSION));
    QVERIFY(QRegularExpression(QStringLiteral("^\\d+\\.\\d+\\.\\d+")).match(reply.value()).hasMatch());
    QVERIFY(remote.call(QStringLiteral("resetCrashGuard")).type() == QDBusMessage::ReplyMessage);

    bus.unregisterService(QLatin1String(OwlfishStatusService::ServiceName));
}

void tst_Owlfish::updateEnv_data()
{
    // Files in the compositor environment directory before the install, as
    // name=content pairs separated by "|"; whether Owlfish gets its file
    QTest::addColumn<QString>("files");
    QTest::addColumn<bool>("active");

    QTest::newRow("empty") << QString() << true;
    QTest::newRow("other variables")
            << "10-qt.conf=# Qt\nQT_QPA_PLATFORM=wayland\nQT_SCALE_FACTOR=2\n" << true;
    QTest::newRow("port plugins")
            << "50-pinetab.conf=QT_QPA_GENERIC_PLUGINS=evdevtouch,evdevkeyboard\n" << false;
    QTest::newRow("quoted, spaces")
            << "50-port.conf=  QT_QPA_GENERIC_PLUGINS = \"evdevtouch\"  \n" << false;
    QTest::newRow("commented out")
            << "50-port.conf=#QT_QPA_GENERIC_PLUGINS=evdevtouch\n" << true;
    QTest::newRow("cleared by a later file")
            << "50-port.conf=QT_QPA_GENERIC_PLUGINS=evdevtouch\n|60-fix.conf=QT_QPA_GENERIC_PLUGINS=\n" << true;
    QTest::newRow("last assignment in a file")
            << "50-port.conf=QT_QPA_GENERIC_PLUGINS=evdevtouch\nQT_QPA_GENERIC_PLUGINS=\n" << true;
    // A hand-made file sorting last that adds Owlfish to the device's list
    QTest::newRow("list with owlfish")
            << "50-port.conf=QT_QPA_GENERIC_PLUGINS=evdevtouch\n"
               "|zz-owlfish.conf=QT_QPA_GENERIC_PLUGINS='evdevtouch,Owlfish:class=X'\n" << true;
    QTest::newRow("similar name")
            << "50-port.conf=QT_QPA_GENERIC_PLUGINS=evdevtouch,owlfishy\n" << false;
    // During an upgrade from 1.1.0 its file is still there; a device file
    // before it still counts
    QTest::newRow("upgrade")
            << "50-port.conf=QT_QPA_GENERIC_PLUGINS=evdevtouch\n"
               "|90-owlfish.conf=QT_QPA_GENERIC_PLUGINS=owlfish\n" << false;
    QTest::newRow("upgrade, no conflict")
            << "90-owlfish.conf=QT_QPA_GENERIC_PLUGINS=owlfish\n" << true;
    // Reinstalling after the device added its own list
    QTest::newRow("own file removed")
            << "00-owlfish.conf=QT_QPA_GENERIC_PLUGINS=owlfish\n"
               "|50-port.conf=QT_QPA_GENERIC_PLUGINS=evdevtouch\n" << false;
}

void tst_Owlfish::updateEnv()
{
    QFETCH(QString, files);
    QFETCH(bool, active);

    QTemporaryDir dir;
    if (!files.isEmpty()) {
        for (const QString &entry : files.split(QLatin1Char('|'))) {
            const int separator = entry.indexOf(QLatin1Char('='));
            QFile file(dir.filePath(entry.left(separator)));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(entry.mid(separator + 1).toUtf8());
        }
    }

    QProcess process;
    process.start(QStringLiteral("sh"), { QStringLiteral(OWLFISH_UPDATE_ENV), dir.path() });
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitCode(), active ? 0 : 1);

    QFile own(dir.filePath(QStringLiteral("00-owlfish.conf")));
    QCOMPARE(own.exists(), active);
    if (active) {
        QVERIFY(own.open(QIODevice::ReadOnly));
        QCOMPARE(own.readAll(), QByteArray("QT_QPA_GENERIC_PLUGINS=owlfish\n"));
        QVERIFY(process.readAllStandardOutput().isEmpty());
    } else {
        const QString message = QString::fromUtf8(process.readAllStandardOutput());
        QVERIFY2(message.startsWith(QStringLiteral("owlfish: installed but inactive: ")), qPrintable(message));
        QVERIFY2(message.contains(QStringLiteral("50-port")) || message.contains(QStringLiteral("50-pinetab")),
                 qPrintable(message));
    }
}

void tst_Owlfish::cutoffFirstReadingAppliesImmediately()
{
    AmbientCutoff cutoff;
    cutoff.setThreshold(1000);
    QSignalSpy spy(&cutoff, &AmbientCutoff::brightChanged);

    cutoff.addReading(20000);
    QVERIFY(cutoff.isBright());
    QCOMPARE(spy.count(), 1);
}

void tst_Owlfish::cutoffHysteresis()
{
    AmbientCutoff cutoff;
    cutoff.setThreshold(1000);
    cutoff.setDelays(0, 0);

    cutoff.addReading(100);
    QVERIFY(!cutoff.isBright());

    cutoff.addReading(1000);  // at the threshold is not above it
    QTest::qWait(20);
    QVERIFY(!cutoff.isBright());

    cutoff.addReading(1200);
    QTRY_VERIFY(cutoff.isBright());

    cutoff.addReading(800);   // below the threshold, above 75 % of it
    QTest::qWait(20);
    QVERIFY(cutoff.isBright());

    cutoff.addReading(750);   // at 75 % is not below it
    QTest::qWait(20);
    QVERIFY(cutoff.isBright());

    cutoff.addReading(700);
    QTRY_VERIFY(!cutoff.isBright());
}

void tst_Owlfish::cutoffThresholdChangeAppliesAtOnce()
{
    // Long delays: nothing here may depend on them
    AmbientCutoff cutoff;
    cutoff.setThreshold(1000);
    cutoff.setDelays(60000, 60000);
    QSignalSpy spy(&cutoff, &AmbientCutoff::brightChanged);

    cutoff.addReading(1200);
    QVERIFY(cutoff.isBright());

    // Raising the threshold above the current light resumes at once, even
    // though 1200 would still be inside the hysteresis band of 1500
    cutoff.setThreshold(1500);
    QVERIFY(!cutoff.isBright());

    // Lowering it below the current light suspends at once
    cutoff.setThreshold(1000);
    QVERIFY(cutoff.isBright());
    QCOMPARE(spy.count(), 3);

    // Unchanged threshold: no re-evaluation
    cutoff.setThreshold(1000);
    QCOMPARE(spy.count(), 3);

    // Before any reading there is nothing to compare against
    AmbientCutoff fresh;
    fresh.setThreshold(10);
    QVERIFY(!fresh.isBright());
}

void tst_Owlfish::cutoffDebounce()
{
    AmbientCutoff cutoff;
    cutoff.setThreshold(1000);
    cutoff.setDelays(100, 300);
    QSignalSpy spy(&cutoff, &AmbientCutoff::brightChanged);

    cutoff.addReading(50);
    QCOMPARE(spy.count(), 0);  // already dark

    // A brief bright reading that goes away again is ignored
    cutoff.addReading(5000);
    QTest::qWait(50);
    cutoff.addReading(50);
    QTest::qWait(150);
    QVERIFY(!cutoff.isBright());
    QCOMPARE(spy.count(), 0);

    // A lasting one counts after the delay, even without further readings
    cutoff.addReading(5000);
    QTest::qWait(50);
    QVERIFY(!cutoff.isBright());
    QTRY_VERIFY(cutoff.isBright());
    QCOMPARE(spy.count(), 1);

    // Getting dark again takes the longer delay
    cutoff.addReading(10);
    QTest::qWait(150);
    QVERIFY(cutoff.isBright());
    QTRY_VERIFY(!cutoff.isBright());
}

void tst_Owlfish::cutoffReset()
{
    AmbientCutoff cutoff;
    cutoff.setThreshold(1000);
    cutoff.addReading(5000);
    QVERIFY(cutoff.isBright());

    cutoff.reset();
    QVERIFY(!cutoff.isBright());
    // Primed again: the next reading applies at once
    cutoff.addReading(5000);
    QVERIFY(cutoff.isBright());
}

void tst_Owlfish::alsMultiplier()
{
    QTemporaryDir dir;
    auto write = [&dir](const QString &name, const QByteArray &content) {
        QFile file(dir.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(content);
    };

    // Not configured
    QCOMPARE(alsValueMultiplier(dir.path()), 1.0);
    QCOMPARE(alsValueMultiplier(dir.path() + QStringLiteral("/missing")), 1.0);

    // Only [Sensors] in NN*.ini files counts, like in mce
    write(QStringLiteral("sensors.ini"), "[Sensors]\nAlsValueMultiplier=0.5\n");
    write(QStringLiteral("10other.ini"), "[Display]\nAlsValueMultiplier=0.5\n");
    QCOMPARE(alsValueMultiplier(dir.path()), 1.0);

    write(QStringLiteral("20hw-sensors.ini"), "# comment\n[Sensors]\n  AlsValueMultiplier = 0.04 \n");
    QCOMPARE(alsValueMultiplier(dir.path()), 0.04);

    // Later files override earlier ones; invalid values are ignored
    write(QStringLiteral("30override.ini"), "[Sensors]\nAlsValueMultiplier=0.1\n");
    write(QStringLiteral("40broken.ini"), "[Sensors]\nAlsValueMultiplier=-3\n");
    QCOMPARE(alsValueMultiplier(dir.path()), 0.1);
}

void tst_Owlfish::cutoffLuxRange()
{
    // Same range as the settings page's scale
    auto cutoffLux = [](const QByteArray &value) {
        qputenv("OWLFISH_DIM_CUTOFF_LUX", value);
        const int lux = OwlfishSettings().cutoffLux();
        qunsetenv("OWLFISH_DIM_CUTOFF_LUX");
        return lux;
    };
    QCOMPARE(cutoffLux("700"), 700);
    QCOMPARE(cutoffLux("20"), 100);
    QCOMPARE(cutoffLux("1000000"), 50000);
    QCOMPARE(cutoffLux("nan"), 1000);
}

void tst_Owlfish::pluginAttachesFilter()
{
    qputenv("OWLFISH_ENABLED", "1");
    qputenv("OWLFISH_TEMPERATURE", "6500");
    qputenv("OWLFISH_DIM", "50");
    QCoreApplication::addLibraryPath(QStringLiteral(OWLFISH_PLUGINS_DIR));
    QScopedPointer<QObject> plugin(QGenericPluginFactory::create(
            QStringLiteral("owlfish"), QStringLiteral("class=QQuickView:process=*")));
    QVERIFY(plugin);

    QScopedPointer<QQuickView> view(createView());
    QVERIFY(view);

    // The plugin's own copy of ColorFilterItem, found by name
    QQuickItem *filter = nullptr;
    QTRY_VERIFY((filter = view->contentItem()->findChild<QQuickItem *>(QStringLiteral("owlfish-filter"))));
    QTRY_COMPARE(filter->property("gain").value<QVector3D>(), QVector3D(0.5f, 0.5f, 0.5f));
    QCOMPARE(filter->size(), QSizeF(200, 100));

    const QImage image = grab(view.data());
    compare(image, White, QColor(128, 128, 128));
    compare(image, Orange, QColor(128, 64, 32));

    // This start is recorded until the run has been healthy for 30 s
    QCOMPARE(CrashGuard(CrashGuard::defaultFilePath()).unhealthyStarts(), 1);
    QVERIFY(CrashGuard::defaultFilePath().startsWith(m_cacheDir.path()));
}

void tst_Owlfish::pluginCombinesTintAndDim()
{
    qputenv("OWLFISH_ENABLED", "1");
    qputenv("OWLFISH_TEMPERATURE", "2700");
    qputenv("OWLFISH_DIM", "50");
    QCoreApplication::addLibraryPath(QStringLiteral(OWLFISH_PLUGINS_DIR));
    QScopedPointer<QObject> plugin(QGenericPluginFactory::create(
            QStringLiteral("owlfish"), QStringLiteral("class=QQuickView:process=*")));
    QVERIFY(plugin);

    QScopedPointer<QQuickView> view(createView());
    QVERIFY(view);

    const QVector3D expected = ColorTemperature::gain(2700) * 0.5f;
    QQuickItem *filter = nullptr;
    QTRY_VERIFY((filter = view->contentItem()->findChild<QQuickItem *>(QStringLiteral("owlfish-filter"))));
    QTRY_VERIFY(qFuzzyCompare(filter->property("gain").value<QVector3D>(), expected));

    const QImage image = grab(view.data());
    compare(image, White, QColor(qRound(255 * expected.x()), qRound(255 * expected.y()),
                                 qRound(255 * expected.z())));
    compare(image, Black, QColor(0, 0, 0));
}

void tst_Owlfish::pluginScheduleOnlyAffectsColour()
{
    // Outside a window that starts in an hour: neutral colour, but the
    // dimming applies
    const int now = QTime::currentTime().msecsSinceStartOfDay() / 60000;
    qputenv("OWLFISH_ENABLED", "1");
    qputenv("OWLFISH_TEMPERATURE", "3400");
    qputenv("OWLFISH_DIM", "50");
    qputenv("OWLFISH_SCHEDULE", "1");
    qputenv("OWLFISH_SCHEDULE_FROM", QByteArray::number((now + 60) % (24 * 60)));
    qputenv("OWLFISH_SCHEDULE_TO", QByteArray::number((now + 120) % (24 * 60)));
    qputenv("OWLFISH_SCHEDULE_TRANSITION", "0");
    QCoreApplication::addLibraryPath(QStringLiteral(OWLFISH_PLUGINS_DIR));
    QScopedPointer<QObject> plugin(QGenericPluginFactory::create(
            QStringLiteral("owlfish"), QStringLiteral("class=QQuickView:process=*")));
    for (const char *name : { "OWLFISH_SCHEDULE", "OWLFISH_SCHEDULE_FROM", "OWLFISH_SCHEDULE_TO",
                              "OWLFISH_SCHEDULE_TRANSITION" })
        qunsetenv(name);
    QVERIFY(plugin);

    QScopedPointer<QQuickView> view(createView());
    QVERIFY(view);

    QQuickItem *filter = nullptr;
    QTRY_VERIFY((filter = view->contentItem()->findChild<QQuickItem *>(QStringLiteral("owlfish-filter"))));
    QTRY_COMPARE(filter->property("gain").value<QVector3D>(), QVector3D(0.5f, 0.5f, 0.5f));
}

void tst_Owlfish::pluginSunSchedule()
{
    // Manual coordinates; the plugin's own controller is not reachable from
    // here, so run one directly to see what it publishes
    const double latitude = 52.52, longitude = 13.405;
    qputenv("OWLFISH_ENABLED", "1");
    qputenv("OWLFISH_TEMPERATURE", "3400");
    qputenv("OWLFISH_DIM", "0");
    qputenv("OWLFISH_SCHEDULE", "1");
    qputenv("OWLFISH_SCHEDULE_SUN", "1");
    qputenv("OWLFISH_SCHEDULE_TRANSITION", "0");
    qputenv("OWLFISH_LOCATION_MANUAL", "1");
    qputenv("OWLFISH_LATITUDE", QByteArray::number(latitude));
    qputenv("OWLFISH_LONGITUDE", QByteArray::number(longitude));
    QTemporaryDir dir;
    QScopedPointer<OwlfishController> controller(new OwlfishController(
            "QQuickView", dir.filePath(QStringLiteral("unhealthy-starts"))));
    QScopedPointer<QQuickView> view(createView());
    QVERIFY(view);
    QTRY_VERIFY(controller->filterItem());

    // The sun times of today at those coordinates, and the matching colour
    const SunTimes sun = SunTimes::compute(QDate::currentDate(), latitude, longitude);
    QCOMPARE(int(sun.state()), int(SunTimes::Normal));
    OwlfishSettings *settings = controller->settings();
    QCOMPARE(settings->published(QStringLiteral("sun_state")).toString(), QStringLiteral("normal"));
    QCOMPARE(settings->published(QStringLiteral("sun_set")).toInt(), SunTimes::minuteOfDay(sun.sunset()));
    QCOMPARE(settings->published(QStringLiteral("sun_rise")).toInt(), SunTimes::minuteOfDay(sun.sunrise()));
    QCOMPARE(settings->published(QStringLiteral("sun_latitude")).toDouble(), latitude);

    const OwlfishSchedule night(SunTimes::minuteOfDay(sun.sunset()),
                                  SunTimes::minuteOfDay(sun.sunrise()), 0);
    const bool warm = night.contains(QDateTime::currentDateTime());
    const QVector3D expected = warm ? ColorTemperature::gain(3400) : QVector3D(1, 1, 1);
    QTRY_VERIFY(qFuzzyCompare(controller->filterItem()->gain(), expected));
    controller.reset();

    // Manual without coordinates: the time zone's location, if it has one
    qunsetenv("OWLFISH_LATITUDE");
    qunsetenv("OWLFISH_LONGITUDE");
    controller.reset(new OwlfishController("QQuickView", dir.filePath(QStringLiteral("unhealthy-starts"))));
    QTRY_VERIFY(controller->filterItem());
    settings = controller->settings();
    const QVariant autoLatitude = settings->published(QStringLiteral("auto_latitude"));
    QCOMPARE(settings->published(QStringLiteral("sun_latitude")), autoLatitude);
    QCOMPARE(settings->published(QStringLiteral("sun_place")).toString().isEmpty(), !autoLatitude.isValid());
    if (!autoLatitude.isValid())
        QCOMPARE(settings->published(QStringLiteral("sun_state")).toString(), QStringLiteral("no_location"));

    for (const char *name : { "OWLFISH_SCHEDULE", "OWLFISH_SCHEDULE_SUN", "OWLFISH_SCHEDULE_TRANSITION",
                              "OWLFISH_LOCATION_MANUAL" })
        qunsetenv(name);
}

void tst_Owlfish::pluginPolarSun()
{
    // Near the poles one of the two has midnight sun and the other polar
    // night, except for a few days around the equinoxes
    const QDate today = QDate::currentDate();
    double polarDay = 0, polarNight = 0;
    for (double latitude : { 89.9, -89.9 }) {
        const SunTimes::State state = SunTimes::compute(today, latitude, 0).state();
        if (state == SunTimes::PolarDay)
            polarDay = latitude;
        else if (state == SunTimes::PolarNight)
            polarNight = latitude;
    }
    if (polarDay == 0 || polarNight == 0)
        QSKIP("Too close to an equinox");

    auto gainAt = [this](double latitude) {
        qputenv("OWLFISH_ENABLED", "1");
        qputenv("OWLFISH_TEMPERATURE", "3400");
        qputenv("OWLFISH_DIM", "50");
        qputenv("OWLFISH_SCHEDULE", "1");
        qputenv("OWLFISH_SCHEDULE_SUN", "1");
        qputenv("OWLFISH_LOCATION_MANUAL", "1");
        qputenv("OWLFISH_LATITUDE", QByteArray::number(latitude));
        qputenv("OWLFISH_LONGITUDE", "0");
        // Every load in this run counts as an unhealthy start; the earlier
        // tests have used up the crash guard's allowance
        QFile::remove(CrashGuard::defaultFilePath());
        QCoreApplication::addLibraryPath(QStringLiteral(OWLFISH_PLUGINS_DIR));
        QScopedPointer<QObject> plugin(QGenericPluginFactory::create(
                QStringLiteral("owlfish"), QStringLiteral("class=QQuickView:process=*")));
        for (const char *name : { "OWLFISH_SCHEDULE", "OWLFISH_SCHEDULE_SUN", "OWLFISH_LOCATION_MANUAL",
                                  "OWLFISH_LATITUDE", "OWLFISH_LONGITUDE" })
            qunsetenv(name);
        QScopedPointer<QQuickView> view(createView());
        QQuickItem *filter = nullptr;
        if (!view || !QTest::qWaitFor([&]() {
            return (filter = view->contentItem()->findChild<QQuickItem *>(QStringLiteral("owlfish-filter")));
        }))
            return QVector3D(-1, -1, -1);
        // Give the fade time to finish
        QTest::qWait(600);
        return filter->property("gain").value<QVector3D>();
    };

    // Midnight sun: neutral, only the dimming
    QVERIFY(qFuzzyCompare(gainAt(polarDay), QVector3D(0.5f, 0.5f, 0.5f)));
    // Polar night: warm all day
    QVERIFY(qFuzzyCompare(gainAt(polarNight), ColorTemperature::gain(3400) * 0.5f));
}

void tst_Owlfish::pluginIgnoresOtherProcesses()
{
    QCoreApplication::addLibraryPath(QStringLiteral(OWLFISH_PLUGINS_DIR));
    // Default options only activate inside the "lipstick" executable
    QScopedPointer<QObject> plugin(QGenericPluginFactory::create(QStringLiteral("owlfish"), QString()));
    QVERIFY(plugin);
    QVERIFY(!plugin->inherits("OwlfishController"));
}

QTEST_MAIN(tst_Owlfish)

#include "tst_owlfish.moc"
