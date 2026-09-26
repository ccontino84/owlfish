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

#include <QGenericPluginFactory>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QtTest>

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
    void crashGuard();
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
