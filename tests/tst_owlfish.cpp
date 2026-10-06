// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "alscalibration.h"
#include "ambientcutoff.h"
#include "colorfilteritem.h"
#include "colormatrix.h"
#include "colortemperature.h"
#include "correction.h"
#include "controller.h"
#include "crashguard.h"
#include <owlfish_version.h>
#include "pqdisplay.h"
#include "schedule.h"
#include "settings.h"
#include "statusservice.h"
#include "sun.h"
#include "timezonelocation.h"

#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusPendingCall>
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

// A fake device tree with a MediaTek CCORR node; a negative value leaves the
// property out
void writeCcorr(const QString &root, int bits, int engines, int linear,
                const QByteArray &compatible = QByteArray("mediatek,disp_ccorr0\0mediatek,mt6858-disp-ccorr\0", 48))
{
    const QString node = root + QStringLiteral("/soc/disp-ccorr0@1400c000");
    QDir().mkpath(node);
    QDir().mkpath(root + QStringLiteral("/aliases"));
    const auto write = [](const QString &path, const QByteArray &data) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
            qWarning() << "Cannot write" << path;
    };
    const auto cell = [](int value) {
        const char bytes[4] = { char(value >> 24), char(value >> 16), char(value >> 8), char(value) };
        return QByteArray(bytes, 4);
    };
    write(root + QStringLiteral("/aliases/ccorr0"), QByteArray("/soc/disp-ccorr0@1400c000", 25) + '\0');
    write(node + QStringLiteral("/compatible"), compatible);
    if (bits >= 0)
        write(node + QStringLiteral("/ccorr-bit"), cell(bits));
    if (engines >= 0)
        write(node + QStringLiteral("/ccorr-num-per-pipe"), cell(engines));
    if (linear >= 0)
        write(node + QStringLiteral("/ccorr-linear"), cell(linear));
}

// The sRGB curves, for the exact result in linear light
double srgbDecode(double c)
{
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double srgbEncode(double l)
{
    return l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1 / 2.4) - 0.055;
}

// The GPU renderer auto picks, once the item has checked fetch: fetch where
// the host's OpenGL has it, blend otherwise
QString gpuRenderer(const ColorFilterItem *item)
{
    return item->capabilities().fetchWorks ? QStringLiteral("fetch") : QStringLiteral("blend");
}

// What fetch makes of an 8-bit colour with a matrix: the matrix on the
// squared values, then the square root, and a gain
QColor throughGamma2(const QColor &color, const QMatrix3x3 &matrix, const QVector3D &gain = QVector3D(1, 1, 1))
{
    const double in[3] = { color.redF() * color.redF(), color.greenF() * color.greenF(),
                           color.blueF() * color.blueF() };
    int out[3];
    for (int row = 0; row < 3; ++row) {
        double l = 0;
        for (int column = 0; column < 3; ++column)
            l += matrix(row, column) * in[column];
        out[row] = qRound(255 * std::sqrt(qBound(0.0, l, 1.0)) * gain[row]);
    }
    return QColor(out[0], out[1], out[2]);
}

float maxDifference(const QMatrix3x3 &a, const QMatrix3x3 &b)
{
    float difference = 0;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column)
            difference = qMax(difference, qAbs(a(row, column) - b(row, column)));
    }
    return difference;
}

float determinant(const QMatrix3x3 &m)
{
    return m(0, 0) * (m(1, 1) * m(2, 2) - m(1, 2) * m(2, 1))
         - m(0, 1) * (m(1, 0) * m(2, 2) - m(1, 2) * m(2, 0))
         + m(0, 2) * (m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0));
}

QVector3D apply(const QMatrix3x3 &m, const QVector3D &v)
{
    return QVector3D(m(0, 0) * v.x() + m(0, 1) * v.y() + m(0, 2) * v.z(),
                     m(1, 0) * v.x() + m(1, 1) * v.y() + m(1, 2) * v.z(),
                     m(2, 0) * v.x() + m(2, 1) * v.y() + m(2, 2) * v.z());
}

// The exact result in linear light, which fetch approximates (and the
// display hardware applies)
QColor throughMatrix(const QColor &color, const QMatrix3x3 &matrix, const QVector3D &gain = QVector3D(1, 1, 1))
{
    const double in[3] = { srgbDecode(color.redF()), srgbDecode(color.greenF()), srgbDecode(color.blueF()) };
    int out[3];
    for (int row = 0; row < 3; ++row) {
        double l = 0;
        for (int column = 0; column < 3; ++column)
            l += matrix(row, column) * in[column];
        out[row] = qRound(255 * srgbEncode(qBound(0.0, l, 1.0)) * gain[row]);
    }
    return QColor(out[0], out[1], out[2]);
}


// What a FakePq was asked to do; outlives the controller that owns the fake
struct PqLog
{
    int connects = 0;
    // Every matrix and gain call, including the ones that failed
    int attempts = 0;
    // Calls from this attempt on fail; -1 never
    int failFrom = -1;
    QVector<QVector<int>> matrices;
    QVector<QVector<int>> gains;
};

class FakePq : public PqDisplay::Backend
{
public:
    FakePq(PqLog *log, bool found = true, int version = 7)
        : m_log(log), m_found(found), m_version(version) {}

    bool connect(QString *error) override
    {
        ++m_log->connects;
        if (!m_found)
            *error = QStringLiteral("no service");
        return m_found;
    }
    int interfaceVersion() override { return m_version; }
    bool setColorMatrix3x3(const int values[9], QString *error) override
    {
        return record(&m_log->matrices, values, 9, error);
    }
    bool setRgbGain(const int values[3], QString *error) override
    {
        return record(&m_log->gains, values, 3, error);
    }

private:
    bool record(QVector<QVector<int>> *calls, const int *values, int count, QString *error)
    {
        if (m_log->failFrom >= 0 && m_log->attempts++ >= m_log->failFrom) {
            *error = QStringLiteral("fake failure");
            return false;
        }
        if (m_log->failFrom < 0)
            ++m_log->attempts;
        QVector<int> call;
        for (int i = 0; i < count; ++i)
            call << values[i];
        calls->append(call);
        return true;
    }

    PqLog *m_log;
    bool m_found;
    int m_version;
};

const QVector<int> PqIdentity = { 2048, 0, 0, 0, 2048, 0, 0, 0, 2048 };
const QVector<int> PqNeutralGain = { 2048, 2048, 2048 };

QVector<int> pqMatrixOf(const QMatrix3x3 &matrix)
{
    int values[9];
    PqDisplay::toFixed(matrix, values);
    QVector<int> result;
    for (int value : values)
        result << value;
    return result;
}

// Minutes after midnight, offset from now
int minutesFromNow(int offset)
{
    const QTime now = QTime::currentTime();
    return ((now.hour() * 60 + now.minute() + offset) % (24 * 60) + 24 * 60) % (24 * 60);
}

QVector<int> pqMatrixFor(const QVector3D &gain)
{
    int values[9];
    PqDisplay::toFixed(ColorMatrix::withGain(ColorMatrix::identity(), gain), values);
    QVector<int> matrix;
    for (int value : values)
        matrix << value;
    return matrix;
}

}

// sensorfw's TimedUnsigned, as its light sensor sends it on D-Bus: (tu)
struct TimedLux
{
    qulonglong timestamp;
    uint value;
};
Q_DECLARE_METATYPE(TimedLux)

QDBusArgument &operator<<(QDBusArgument &argument, const TimedLux &lux)
{
    argument.beginStructure();
    argument << lux.timestamp << lux.value;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, TimedLux &lux)
{
    argument.beginStructure();
    argument >> lux.timestamp >> lux.value;
    argument.endStructure();
    return argument;
}

// sensorfw's DataRange on D-Bus: (min, max, resolution)
struct SensorRange
{
    double min;
    double max;
    double resolution;
};
Q_DECLARE_METATYPE(SensorRange)
Q_DECLARE_METATYPE(QList<SensorRange>)

QDBusArgument &operator<<(QDBusArgument &argument, const SensorRange &range)
{
    argument.beginStructure();
    argument << range.min << range.max << range.resolution;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, SensorRange &range)
{
    argument.beginStructure();
    argument >> range.min >> range.max >> range.resolution;
    argument.endStructure();
    return argument;
}

// Stands in for com.nokia.SensorService /SensorManager/alssensor
class FakeAlsSensor : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "local.ALSSensor")

public slots:
    TimedLux lux() const { return TimedLux { 12345, 800 }; }
    // As on the Jolla Phone
    QList<SensorRange> getAvailableDataRanges() const { return { SensorRange { 0, 65535, 1 } }; }
    QList<SensorRange> noRanges() const { return {}; }
    // Not the shape sensorfw sends
    uint plainLux() const { return 800; }
};

class tst_Owlfish : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void identityHasNoEffect();
    void uniformDim();
    void nightGainKeepsBlack();
    void blendStateRestored();
    void fetchRenderer();
    void fetchRestoresBlending();
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
    void linearGain();
    void pqFixedPoint();
    void pqDeviceId();
    void pqCcorr();
    void pqOpen();
    void pqWithoutLibgbinder();
    void rendererSelection_data();
    void rendererSelection();
    void rendererPqDrawsNothing();
    void rendererHandover();
    void rendererMasterSwitch();
    void saturationMatrix();
    void saturationThroughPq();
    void saturationWithBlend();
    void saturationThroughFetch();
    void correctionMatrices();
    void correctionThroughPq();
    void correctionWithBlend();
    void correctionThroughFetch();
    void saturationAfterCorrection();
    void dimmingWhen_data();
    void dimmingWhen();
    void rendererFallback();
    void rendererResetOnQuit();
    void rendererCrashGuard();
    void updateEnv_data();
    void updateEnv();
    void cutoffFirstReadingAppliesImmediately();
    void cutoffHysteresis();
    void cutoffThresholdChangeAppliesAtOnce();
    void cutoffDebounce();
    void cutoffReset();
    void cutoffInitialReadingAppliesImmediately();
    void cutoffReadingReplacesInitialReading();
    void cutoffInitialReadingAfterReadingIgnored();
    void cutoffInitialReadingAfterReset();
    void cutoffSaturatedCountsAsBright();
    void cutoffMaximumAppliesToLastReading();
    void alsLuxFromReply();
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
    // A controller for a QQuickView with the display hardware faked; a null
    // hwRelease means no hw-release file
    // ccorr: "bits,engines,linear" for a fake device tree, empty for none
    OwlfishController *createPqController(const QTemporaryDir &dir, PqDisplay::Backend *backend,
                                          const QByteArray &hwRelease = "ID=jp2601\n",
                                          const QByteArray &ccorr = QByteArray());

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

OwlfishController *tst_Owlfish::createPqController(const QTemporaryDir &dir,
                                                   PqDisplay::Backend *backend,
                                                   const QByteArray &hwRelease,
                                                   const QByteArray &ccorr)
{
    // Never the host's own device tree
    const QString deviceTreePath = dir.filePath(QStringLiteral("devicetree"));
    if (!ccorr.isEmpty()) {
        const QList<QByteArray> values = ccorr.split(',');
        writeCcorr(deviceTreePath, values.value(0).toInt(), values.value(1).toInt(), values.value(2).toInt());
    }
    const QString hwReleasePath = dir.filePath(QStringLiteral("hw-release"));
    if (!hwRelease.isNull()) {
        QFile file(hwReleasePath);
        if (!file.open(QIODevice::WriteOnly) || file.write(hwRelease) != hwRelease.size())
            qWarning() << "Cannot write" << hwReleasePath;
    }
    OwlfishController *controller = new OwlfishController(
            "QQuickView", dir.filePath(QStringLiteral("unhealthy-starts")));
    controller->setDisplayHardware(backend, hwReleasePath, deviceTreePath);
    return controller;
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

void tst_Owlfish::fetchRenderer()
{
    QScopedPointer<QQuickView> view(createView());
    QVERIFY(view);
    ColorFilterItem *filter = new ColorFilterItem(view->rootObject());
    filter->setSize(QSizeF(200, 100));
    filter->setZ(10);

    // Not checked unless asked for
    grab(view.data());
    QVERIFY(!filter->capabilities().detected);

    filter->setRenderer(ColorFilter::Fetch);
    filter->setGain(QVector3D(0.5f, 0.5f, 0.5f));
    QImage image = grab(view.data());
    QVERIFY(filter->capabilities().detected);
    if (!filter->capabilities().fetchWorks)
        QSKIP("No framebuffer fetch in this OpenGL implementation");

    // Gain only: drawn by blend, which is cheaper
    QCOMPARE(int(filter->activeRenderer()), int(ColorFilter::Blend));
    compare(image, White, QColor(128, 128, 128));
    compare(image, Black, QColor(0, 0, 0));
    compare(image, Grey, QColor(64, 64, 64));
    compare(image, Orange, QColor(128, 64, 32));

    // Nothing to do: nothing drawn
    filter->setGain(QVector3D(1, 1, 1));
    image = grab(view.data());
    QCOMPARE(int(filter->activeRenderer()), int(ColorFilter::None));
    compare(image, Orange, QColor(255, 128, 64));

    // Grayscale in approximately linear light, then the gain; greys stay
    // grey, colours within a few levels of the exact result
    const QMatrix3x3 gray = ColorMatrix::saturation(0);
    const QVector3D gain(1.0f, 0.8f, 0.6f);
    filter->setMatrix(gray);
    image = grab(view.data());
    QCOMPARE(int(filter->activeRenderer()), int(ColorFilter::Fetch));
    compare(image, White, QColor(255, 255, 255));
    compare(image, Black, QColor(0, 0, 0));
    compare(image, Grey, QColor(128, 128, 128));
    compare(image, Orange, throughGamma2(QColor(255, 128, 64), gray));
    QVERIFY(qAbs(image.pixelColor(Orange).red() - throughMatrix(QColor(255, 128, 64), gray).red()) <= 6);
    filter->setGain(gain);
    image = grab(view.data());
    compare(image, Orange, throughGamma2(QColor(255, 128, 64), gray, gain));
    compare(image, White, QColor(255, 204, 153));

    // Vibrant clips at the edges of the gamut
    const QMatrix3x3 vibrant = ColorMatrix::saturation(1.2);
    filter->setGain(QVector3D(1, 1, 1));
    filter->setMatrix(vibrant);
    image = grab(view.data());
    compare(image, Grey, QColor(128, 128, 128));
    compare(image, Orange, throughGamma2(QColor(255, 128, 64), vibrant));

    // Back to no matrix: blend again
    filter->setMatrix(QMatrix3x3());
    filter->setGain(QVector3D(0.5f, 0.5f, 0.5f));
    image = grab(view.data());
    QCOMPARE(int(filter->activeRenderer()), int(ColorFilter::Blend));
    compare(image, Orange, QColor(128, 64, 32));

    // Blend ignores the matrix
    filter->setMatrix(vibrant);
    filter->setRenderer(ColorFilter::Blend);
    filter->setGain(QVector3D(0.5f, 0.5f, 0.5f));
    image = grab(view.data());
    QCOMPARE(int(filter->activeRenderer()), int(ColorFilter::Blend));
    compare(image, Orange, QColor(128, 64, 32));
}

void tst_Owlfish::fetchRestoresBlending()
{
    // Content drawn after the filter must be blended normally again
    QScopedPointer<QQuickView> view(createView());
    QVERIFY(view);
    ColorFilterItem *filter = new ColorFilterItem(view->rootObject());
    filter->setSize(QSizeF(200, 100));
    filter->setZ(10);
    filter->setRenderer(ColorFilter::Fetch);
    filter->setGain(QVector3D(0.5f, 0.5f, 0.5f));
    filter->setMatrix(ColorMatrix::saturation(1.2));
    grab(view.data());
    if (!filter->capabilities().fetchWorks)
        QSKIP("No framebuffer fetch in this OpenGL implementation");

    QQuickItem *above = view->rootObject()->findChild<QQuickItem *>(QStringLiteral("above"));
    QVERIFY(above);
    above->setVisible(true);

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
    reply = remote.call(QStringLiteral("renderer"));
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    QCOMPARE(reply.value(), QStringLiteral("none"));
    reply = remote.call(QStringLiteral("diagnostics"));
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    // The system from os-release, which the host has too
    QVERIFY2(QRegularExpression(QStringLiteral("^version " OWLFISH_VERSION "\nsystem [^\n]+\n"
                                               "status starting\nrenderer none \\(starting\\)\n"))
                     .match(reply.value()).hasMatch(),
             qPrintable(reply.value()));
    QVERIFY(!reply.value().contains(QStringLiteral("\nsystem unknown\n")));

    bus.unregisterService(QLatin1String(OwlfishStatusService::ServiceName));
}

void tst_Owlfish::linearGain()
{
    QCOMPARE(ColorMatrix::linearGain(1), 1.0);
    QCOMPARE(ColorMatrix::linearGain(0), 0.0);
    // Dim 50 %: what the Jolla Phone's display hardware needs to look the
    // same as the GPU filter (0.5^2.2 = 0.218 was checked by eye)
    QVERIFY(qAbs(ColorMatrix::linearGain(0.5) - 0.214) < 0.001);
    QCOMPARE(ColorMatrix::linearGain(2), 1.0);

    // One gain per output channel
    const QMatrix3x3 matrix = ColorMatrix::withGain(ColorMatrix::identity(), QVector3D(1.0f, 0.5f, 0.25f));
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            const float expected = row != column ? 0.0f
                    : float(ColorMatrix::linearGain(row == 0 ? 1.0 : row == 1 ? 0.5 : 0.25));
            QCOMPARE(matrix(row, column), expected);
        }
    }
}

void tst_Owlfish::pqFixedPoint()
{
    // 2048 is 1.0 (the neutral value checked on the Jolla Phone)
    int values[9];
    PqDisplay::toFixed(ColorMatrix::identity(), values);
    for (int i = 0; i < 9; ++i)
        QCOMPARE(values[i], PqIdentity[i]);

    // Dim 50 %: 0.214 × 2048
    QCOMPARE(pqMatrixFor(QVector3D(0.5f, 0.5f, 0.5f)),
             QVector<int>({ 438, 0, 0, 0, 438, 0, 0, 0, 438 }));

    // Rounded to the nearest step, also below zero
    const float coefficients[9] = { 1.0f, -0.1f, 0.00024f, 0, 0.5f, 0, 0, 0, 0.00025f };
    PqDisplay::toFixed(QMatrix3x3(coefficients), values);
    QCOMPARE(values[0], 2048);
    QCOMPARE(values[1], -205);
    QCOMPARE(values[2], 0);
    QCOMPARE(values[4], 1024);
    QCOMPARE(values[8], 1);
}

void tst_Owlfish::pqCcorr()
{
    QTemporaryDir dir;
    const QString root = dir.filePath(QStringLiteral("dt"));

    // None: no device tree, or no alias
    QVERIFY(!PqDisplay::ccorr(root).found);
    QCOMPARE(PqDisplay::describe(PqDisplay::ccorr(root)), QStringLiteral("none"));

    // The Jolla Phone's
    writeCcorr(root, 13, 1, 1);
    PqDisplay::Ccorr ccorr = PqDisplay::ccorr(root);
    QVERIFY(ccorr.found);
    QCOMPARE(ccorr.bits, 13);
    QCOMPARE(ccorr.engines, 1);
    QCOMPARE(ccorr.linear, 1);
    QVERIFY(PqDisplay::isKnown(ccorr));
    QCOMPARE(PqDisplay::describe(ccorr), QStringLiteral("13 bits, 1 engine, linear"));

    // Other layouts are found but not known
    QTemporaryDir other;
    writeCcorr(other.filePath(QStringLiteral("a")), 12, 1, 1);
    QVERIFY(!PqDisplay::isKnown(PqDisplay::ccorr(other.filePath(QStringLiteral("a")))));
    writeCcorr(other.filePath(QStringLiteral("b")), 13, 2, 1);
    ccorr = PqDisplay::ccorr(other.filePath(QStringLiteral("b")));
    QVERIFY(ccorr.found && !PqDisplay::isKnown(ccorr));
    QCOMPARE(PqDisplay::describe(ccorr), QStringLiteral("13 bits, 2 engines, linear"));
    writeCcorr(other.filePath(QStringLiteral("c")), 13, 1, 0);
    QVERIFY(!PqDisplay::isKnown(PqDisplay::ccorr(other.filePath(QStringLiteral("c")))));
    // Missing properties are unknown, not defaults
    writeCcorr(other.filePath(QStringLiteral("d")), -1, -1, -1);
    ccorr = PqDisplay::ccorr(other.filePath(QStringLiteral("d")));
    QVERIFY(ccorr.found && !PqDisplay::isKnown(ccorr));
    QCOMPARE(PqDisplay::describe(ccorr), QStringLiteral("unknown bits, unknown engines, linear unknown"));
    // Only MediaTek's
    writeCcorr(other.filePath(QStringLiteral("e")), 13, 1, 1, QByteArray("vendor,other-ccorr\0", 19));
    QVERIFY(!PqDisplay::ccorr(other.filePath(QStringLiteral("e"))).found);
}

void tst_Owlfish::pqDeviceId()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("hw-release"));
    QCOMPARE(PqDisplay::deviceId(path), QString());

    auto idOf = [&path](const QByteArray &content) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return QStringLiteral("cannot write");
        file.write(content);
        file.close();
        return PqDisplay::deviceId(path);
    };
    // As on the Jolla Phone
    QCOMPARE(idOf("# comment\nNAME=\"Jolla Jolla Phone\"\nID=jp2601\nMER_HA_DEVICE=jp2601\n"),
             QStringLiteral("jp2601"));
    QCOMPARE(idOf("ID=\"jp2601\"\n"), QStringLiteral("jp2601"));
    QCOMPARE(idOf("  ID='jp2601'  \n"), QStringLiteral("jp2601"));
    QCOMPARE(idOf("VERSION_ID=1.0.0.17\n"), QString());

    QVERIFY(PqDisplay::isVerified(QStringLiteral("jp2601")));
    QVERIFY(!PqDisplay::isVerified(QString()));
    QVERIFY(!PqDisplay::isVerified(QStringLiteral("jp2601 ")));
    QVERIFY(!PqDisplay::isVerified(QStringLiteral("xqbt52")));
}

void tst_Owlfish::pqOpen()
{
    {
        PqLog log;
        PqDisplay pq(new FakePq(&log));
        // Nothing is sent before it is open
        QVERIFY(!pq.setMatrix(ColorMatrix::identity()));
        QVERIFY(!pq.reset());
        QCOMPARE(log.attempts, 0);

        QVERIFY(pq.open());
        QVERIFY(pq.open());
        QCOMPARE(log.connects, 1);
        QCOMPARE(pq.interfaceVersion(), 7);

        const QMatrix3x3 dim = ColorMatrix::withGain(ColorMatrix::identity(), QVector3D(0.5f, 0.5f, 0.5f));
        QVERIFY(pq.setMatrix(dim));
        QVERIFY(pq.setMatrix(dim));
        QCOMPARE(log.matrices.size(), 1);
        QCOMPARE(pq.calls(), 1);

        // Sent even if the service should already have it
        QVERIFY(pq.reset());
        QCOMPARE(log.gains, QVector<QVector<int>>({ PqNeutralGain }));
        QCOMPARE(log.matrices.size(), 2);
        QCOMPARE(log.matrices.last(), PqIdentity);
        QVERIFY(pq.reset());
        QCOMPARE(log.matrices.size(), 3);

        log.failFrom = log.attempts;
        QVERIFY(!pq.setMatrix(dim));
        QCOMPARE(pq.error(), QStringLiteral("fake failure"));
    }
    {
        PqLog log;
        PqDisplay pq(new FakePq(&log, true, 6));
        QVERIFY(!pq.open());
        QVERIFY(pq.error().contains(QStringLiteral("version 6")));
        QVERIFY(!pq.open());
        QCOMPARE(log.connects, 1);
        QVERIFY(!pq.setMatrix(ColorMatrix::identity()));
        QCOMPARE(log.attempts, 0);
    }
    {
        PqLog log;
        PqDisplay pq(new FakePq(&log, false));
        QVERIFY(!pq.open());
        QCOMPARE(pq.error(), QStringLiteral("no service"));
    }
}

void tst_Owlfish::pqWithoutLibgbinder()
{
    if (PqDisplay().open())
        QSKIP("This host has libgbinder and a picture quality service");

    PqDisplay pq;
    QVERIFY(!pq.open());
    QVERIFY(!pq.error().isEmpty());
    QVERIFY(!pq.setMatrix(ColorMatrix::identity()));
    QCOMPARE(pq.calls(), 0);

    // Forced, with the real backend: the item draws
    qputenv("OWLFISH_ENABLED", "1");
    qputenv("OWLFISH_TEMPERATURE", "6500");
    qputenv("OWLFISH_DIM", "50");
    qputenv("OWLFISH_RENDERER", "pq");
    QTemporaryDir dir;
    QScopedPointer<OwlfishController> controller(new OwlfishController(
            "QQuickView", dir.filePath(QStringLiteral("unhealthy-starts"))));
    qunsetenv("OWLFISH_RENDERER");
    QScopedPointer<QQuickView> view(createView());
    QVERIFY(view);
    QTRY_VERIFY(controller->filterItem() && controller->filterItem()->capabilities().detected);
    QTRY_COMPARE(controller->renderer(), gpuRenderer(controller->filterItem()));
    QVERIFY(controller->diagnostics().contains(QStringLiteral("display hardware not available: ")));
    QTRY_COMPARE(controller->filterItem()->gain(), QVector3D(0.5f, 0.5f, 0.5f));
    compare(grab(view.data()), White, QColor(128, 128, 128));
}

void tst_Owlfish::rendererSelection_data()
{
    QTest::addColumn<QByteArray>("hwRelease");
    QTest::addColumn<QByteArray>("key");
    QTest::addColumn<bool>("found");
    QTest::addColumn<int>("version");
    QTest::addColumn<QString>("renderer");
    QTest::addColumn<QString>("reason");
    QTest::addColumn<int>("connects");
    // The separate device line, when the reason doesn't name the device
    QTest::addColumn<QString>("deviceLine");
    // A fake device tree's CCORR block, "bits,engines,linear"; empty for none
    QTest::addColumn<QByteArray>("ccorr");

    const QByteArray jollaPhone("NAME=\"Jolla Jolla Phone\"\nID=jp2601\n");
    const QByteArray other("ID=xqbt52\n");
    QTest::newRow("verified") << jollaPhone << QByteArray() << true << 7
                              << "pq" << "renderer pq (device jp2601 verified)" << 1 << QString()
                              << QByteArray();
    QTest::newRow("newer service") << jollaPhone << QByteArray("auto") << true << 8
                                   << "pq" << "renderer pq (device jp2601 verified)" << 1
                                   << QString()
                                   << QByteArray();
    QTest::newRow("old service") << jollaPhone << QByteArray() << true << 6
                                 << "gpu" << "service version 6, needs 7" << 1
                                 << QStringLiteral("device jp2601, verified")
                                 << QByteArray();
    QTest::newRow("no service") << jollaPhone << QByteArray() << false << 7
                                << "gpu" << "display hardware not available: no service" << 1
                                << QStringLiteral("device jp2601, verified")
                                << QByteArray();
    // Nothing is loaded or called
    QTest::newRow("other device") << other << QByteArray() << true << 7
                                  << "gpu" << "renderer %gpu (device xqbt52 not verified" << 0
                                  << QString()
                                  << QByteArray();
    QTest::newRow("no hw-release") << QByteArray() << QByteArray() << true << 7
                                   << "gpu" << "renderer %gpu (device unknown" << 0
                                   << QString()
                                   << QByteArray();
    QTest::newRow("forced blend") << jollaPhone << QByteArray("blend") << true << 7
                                  << "blend" << "renderer blend (forced by the renderer key)" << 0
                                  << QStringLiteral("device jp2601, verified")
                                  << QByteArray();
    QTest::newRow("forced pq") << other << QByteArray("PQ") << true << 7
                               << "pq" << "renderer pq (forced by the renderer key)" << 1
                               << QStringLiteral("device xqbt52, not verified")
                               << QByteArray();
    // Still only with a service that is new enough
    QTest::newRow("forced pq, old service") << other << QByteArray("pq") << true << 6
                                            << "gpu" << "service version 6" << 1
                                            << QStringLiteral("device xqbt52, not verified")
                                            << QByteArray();
    QTest::newRow("forced pq, no service") << other << QByteArray("pq") << false << 7
                                           << "gpu" << "not available: no service" << 1
                                           << QStringLiteral("device xqbt52, not verified")
                                           << QByteArray();
    QTest::newRow("unknown key") << jollaPhone << QByteArray("gpu") << true << 7
                                 << "pq" << "renderer pq (device jp2601 verified)" << 1
                                 << QString()
                                 << QByteArray();
    // Another MediaTek device: found through the device tree, whatever its ID
    const QByteArray future("ID=jp2701\n");
    QTest::newRow("detected") << future << QByteArray() << true << 7
                              << "pq" << "renderer pq (display hardware detected)" << 1
                              << QStringLiteral("device jp2701, not verified") << QByteArray("13,1,1");
    QTest::newRow("detected, no service") << future << QByteArray() << false << 7
                                          << "gpu" << "display hardware not available: no service" << 1
                                          << QStringLiteral("device jp2701, not verified")
                                          << QByteArray("13,1,1");
    // A layout whose scale isn't known: nothing is loaded or called
    QTest::newRow("12-bit") << future << QByteArray() << true << 7
                            << "gpu" << "renderer %gpu (device jp2701 not verified" << 0
                            << QString() << QByteArray("12,1,1");
    QTest::newRow("two engines") << future << QByteArray() << true << 7
                                 << "gpu" << "renderer %gpu (device jp2701 not verified" << 0
                                 << QString() << QByteArray("13,2,1");
}

void tst_Owlfish::rendererSelection()
{
    QFETCH(QByteArray, hwRelease);
    QFETCH(QByteArray, key);
    QFETCH(bool, found);
    QFETCH(int, version);
    QFETCH(QString, renderer);
    QFETCH(QString, reason);
    QFETCH(int, connects);
    QFETCH(QString, deviceLine);
    QFETCH(QByteArray, ccorr);

    qputenv("OWLFISH_ENABLED", "1");
    qputenv("OWLFISH_TEMPERATURE", "6500");
    qputenv("OWLFISH_DIM", "50");
    qputenv("OWLFISH_RENDERER", key);
    PqLog log;
    QTemporaryDir dir;
    QScopedPointer<OwlfishController> controller(createPqController(
            dir, new FakePq(&log, found, version), hwRelease.isEmpty() ? QByteArray() : hwRelease, ccorr));
    qunsetenv("OWLFISH_RENDERER");
    QCOMPARE(controller->renderer(), QStringLiteral("none"));

    QScopedPointer<QQuickView> view(createView());
    QVERIFY(view);
    // "gpu": fetch, or blend where fetch doesn't work, once checked
    if (renderer == QLatin1String("gpu")) {
        QTRY_VERIFY(controller->filterItem() && controller->filterItem()->capabilities().detected);
        renderer = gpuRenderer(controller->filterItem());
        reason.replace(QStringLiteral("%gpu"), renderer);
    }
    QTRY_COMPARE(controller->renderer(), renderer);
    QVERIFY2(controller->diagnostics().contains(reason), qPrintable(controller->diagnostics()));
    QCOMPARE(log.connects, connects);
    if (key.isEmpty() && !ccorr.isEmpty()) {
        const QList<QByteArray> values = ccorr.split(',');
        QVERIFY2(controller->diagnostics().contains(
                         QStringLiteral("\ndisplay colour correction %1 bits, ").arg(QString::fromLatin1(values.value(0)))),
                 qPrintable(controller->diagnostics()));
    }
    const QStringList lines = controller->diagnostics().split(QLatin1Char('\n'));
    QCOMPARE(lines.filter(QStringLiteral("device ")).size(), 1);
    if (!deviceLine.isEmpty())
        QVERIFY2(lines.contains(deviceLine), qPrintable(controller->diagnostics()));
    QTRY_COMPARE(controller->filterItem()->gain(), QVector3D(0.5f, 0.5f, 0.5f));

    if (renderer == QLatin1String("pq")) {
        // Clears what an earlier run left, then the state
        QCOMPARE(log.gains.value(0), PqNeutralGain);
        QCOMPARE(log.matrices.value(0), PqIdentity);
        QCOMPARE(log.matrices.last(), pqMatrixFor(QVector3D(0.5f, 0.5f, 0.5f)));
        QVERIFY(!controller->filterItem()->isVisible());
    } else {
        QCOMPARE(log.attempts, 0);
        QVERIFY(controller->filterItem()->isVisible());
    }
}

void tst_Owlfish::saturationMatrix()
{
    // 1 is no change, 0 every channel the Rec. 709 luminance
    QCOMPARE(ColorMatrix::saturation(1), ColorMatrix::identity());
    const float luminance[3] = { 0.2126f, 0.7152f, 0.0722f };
    const QMatrix3x3 grey = ColorMatrix::saturation(0);
    for (int row = 0; row < 3; ++row) {
        float sum = 0;
        for (int column = 0; column < 3; ++column) {
            QVERIFY(qFuzzyCompare(grey(row, column), luminance[column]));
            sum += ColorMatrix::saturation(0.4)(row, column);
        }
        // White stays white
        QVERIFY(qAbs(sum - 1) < 1e-6f);
    }

    // With the colour: the schedule's strength of the way to the setting
    QCOMPARE(OwlfishController::saturationFactor(1, 100), 1.0);
    QCOMPARE(OwlfishController::saturationFactor(1, 0), 0.0);
    QCOMPARE(OwlfishController::saturationFactor(0.5, 0), 0.5);
    QCOMPARE(OwlfishController::saturationFactor(0, 40), 1.0);
    QVERIFY(qFuzzyCompare(OwlfishController::saturationFactor(1, 40), 0.4));
}

void tst_Owlfish::saturationThroughPq()
{
    qputenv("OWLFISH_ENABLED", "1");
    qputenv("OWLFISH_TEMPERATURE", "6500");
    qputenv("OWLFISH_DIM", "50");
    qputenv("OWLFISH_SATURATION", "40");
    PqLog log;
    QTemporaryDir dir;
    QScopedPointer<OwlfishController> controller(createPqController(dir, new FakePq(&log)));
    qunsetenv("OWLFISH_SATURATION");
    QScopedPointer<QQuickView> view(createView());
    QVERIFY(view);
    QTRY_COMPARE(controller->renderer(), QStringLiteral("pq"));

    // Desaturated, then dimmed
    const QVector3D gain(0.5f, 0.5f, 0.5f);
    QTRY_COMPARE(log.matrices.last(), pqMatrixOf(ColorMatrix::withGain(ColorMatrix::saturation(0.4), gain)));
    QVERIFY(controller->diagnostics().split(QLatin1Char('\n')).contains(QStringLiteral("saturation 40 %")));

    // A change fades, like the gain
    const int matrices = log.matrices.size();
    controller->settings()->setSaturation(100);
    QTRY_COMPARE(log.matrices.last(), pqMatrixFor(gain));
    QVERIFY(log.matrices.size() > matrices + 2);

    // Off: identity
    controller->settings()->setSaturation(0);
    controller->settings()->setEnabled(false);
    QTRY_COMPARE(log.matrices.last(), PqIdentity);
}

void tst_Owlfish::saturationWithBlend()
{
    // Blending (forced, or where fetch doesn't work) can't mix the
    // channels: the gain alone, and the diagnostics say so
    qputenv("OWLFISH_ENABLED", "1");
    qputenv("OWLFISH_TEMPERATURE", "6500");
    qputenv("OWLFISH_DIM", "50");
    qputenv("OWLFISH_SATURATION", "40");
    qputenv("OWLFISH_RENDERER", "blend");
    PqLog log;
    QTemporaryDir dir;
    QScopedPointer<OwlfishController> controller(createPqController(
            dir, new FakePq(&log), QByteArray("ID=xqbt52\n")));
    qunsetenv("OWLFISH_SATURATION");
    qunsetenv("OWLFISH_RENDERER");
    QScopedPointer<QQuickView> view(createView());
    QVERIFY(view);
    QTRY_COMPARE(controller->filterItem()->gain(), QVector3D(0.5f, 0.5f, 0.5f));
    QCOMPARE(controller->renderer(), QStringLiteral("blend"));
    QVERIFY(log.matrices.isEmpty());
    compare(grab(view.data()), Orange, QColor(128, 64, 32));
    QVERIFY(controller->diagnostics().contains(QStringLiteral("\nsaturation 40 %, not supported by this renderer")));
}

void tst_Owlfish::saturationThroughFetch()
{
    // Another device, auto: the GPU with fetch where it works, with the
    // matrix on the encoded values
    qputenv("OWLFISH_ENABLED", "1");
    qputenv("OWLFISH_TEMPERATURE", "6500");
    qputenv("OWLFISH_DIM", "50");
    qputenv("OWLFISH_SATURATION", "40");
    PqLog log;
    QTemporaryDir dir;
    QScopedPointer<OwlfishController> controller(createPqController(
            dir, new FakePq(&log), QByteArray("ID=xqbt52\n")));
    qunsetenv("OWLFISH_SATURATION");
    QScopedPointer<QQuickView> view(createView());
    QVERIFY(view);
    QTRY_VERIFY(controller->filterItem() && controller->filterItem()->capabilities().detected);
    QCOMPARE(log.connects, 0);

    if (!controller->filterItem()->capabilities().fetchWorks) {
        // Blend, which can't desaturate, and the diagnostics say why
        QTRY_COMPARE(controller->renderer(), QStringLiteral("blend"));
        const QString diagnostics = controller->diagnostics();
        QVERIFY2(diagnostics.contains(QStringLiteral(
                         "\nrenderer blend (device xqbt52 not verified, fetch not supported")),
                 qPrintable(diagnostics));
        QVERIFY(diagnostics.contains(QStringLiteral("\nsaturation 40 %, not supported by this renderer")));
        QSKIP("No framebuffer fetch in this OpenGL implementation; the fallback was checked");
    }

    QCOMPARE(controller->renderer(), QStringLiteral("fetch"));
    const QMatrix3x3 saturation = ColorMatrix::saturation(0.4);
    QTRY_VERIFY(maxDifference(controller->filterItem()->matrix(), saturation) < 1e-6f);
    QTRY_COMPARE(controller->filterItem()->gain(), QVector3D(0.5f, 0.5f, 0.5f));
    compare(grab(view.data()), Orange,
            throughGamma2(QColor(255, 128, 64), saturation, QVector3D(0.5f, 0.5f, 0.5f)));
    const QString diagnostics = controller->diagnostics();
    QVERIFY2(diagnostics.contains(QStringLiteral("\nrenderer fetch (device xqbt52 not verified)\n")),
             qPrintable(diagnostics));
    QVERIFY(diagnostics.contains(QStringLiteral("\nsaturation 40 %\n")));
}

void tst_Owlfish::correctionMatrices()
{
    for (const QString &name : { QStringLiteral("protan"), QStringLiteral("deutan"),
                                 QStringLiteral("tritan"), QStringLiteral("greyscale") })
        QCOMPARE(Correction::normalize(name.toUpper() + QLatin1Char(' ')), name);
    QCOMPARE(Correction::normalize(QStringLiteral("cinema")), QStringLiteral("none"));
    QCOMPARE(Correction::normalize(QString()), QStringLiteral("none"));
    // Pre-release names, now the plain ones or dropped
    QCOMPARE(Correction::normalize(QStringLiteral("protan3")), QStringLiteral("none"));
    QCOMPARE(Correction::normalize(QStringLiteral("grayscale")), QStringLiteral("none"));
    QVERIFY(Correction::hasStrength(QStringLiteral("deutan")));
    QVERIFY(!Correction::hasStrength(QStringLiteral("greyscale")));
    QVERIFY(!Correction::hasStrength(QStringLiteral("none")));
    QVERIFY(ColorMatrix::isIdentity(Correction::simulation(QStringLiteral("none"))));
    QVERIFY(ColorMatrix::isIdentity(Correction::matrix(QStringLiteral("none"), 1)));
    QVERIFY(ColorMatrix::isIdentity(Correction::matrix(QStringLiteral("protan"), 0)));

    // Greyscale is full grey, whatever the strength
    QVERIFY(ColorMatrix::isIdentity(Correction::simulation(QStringLiteral("greyscale"))));
    QCOMPARE(Correction::matrix(QStringLiteral("greyscale"), 0), ColorMatrix::saturation(0));
    QCOMPARE(Correction::matrix(QStringLiteral("greyscale"), 0.7), ColorMatrix::saturation(0));

    // The simulation is Viénot, Brettel & Mollon 1999's (Color Res. Appl.
    // 24:243): their projection (Eq. 5) in their LMS (Eq. 4), so
    // lms · simulation = projection · lms
    const float rgbToLms[9] = { 17.8824f, 43.5161f, 4.11935f,
                                3.45565f, 27.1554f, 3.86714f,
                                0.0299566f, 0.184309f, 1.46709f };
    const float protanopia[9] = { 0, 2.02344f, -2.52581f, 0, 1, 0, 0, 0, 1 };
    const float deuteranopia[9] = { 1, 0, 0, 0.494207f, 0, 1.24827f, 0, 0, 1 };
    const QMatrix3x3 lms(rgbToLms);
    QVERIFY(maxDifference(lms * Correction::simulation(QStringLiteral("protan")),
                          QMatrix3x3(protanopia) * lms) < 0.01f);
    QVERIFY(maxDifference(lms * Correction::simulation(QStringLiteral("deutan")),
                          QMatrix3x3(deuteranopia) * lms) < 0.01f);

    // Tritan isn't in the paper: the same construction through red. Tritans
    // keep their L and M responses, so lms · simulation keeps those rows.
    const QMatrix3x3 tritanLms = lms * Correction::simulation(QStringLiteral("tritan"));
    for (int column = 0; column < 3; ++column) {
        QVERIFY(qAbs(tritanLms(0, column) - lms(0, column)) < 0.01f);
        QVERIFY(qAbs(tritanLms(1, column) - lms(1, column)) < 0.01f);
    }

    // As computed independently (numpy, research/colour-correction, where
    // these are the "3" set)
    const float protan30[9] = { 1.1194f, -0.1194f, 0, 0.1769f, 0.8231f, 0, -0.0247f, 0.0247f, 1 };
    const float protan70[9] = { 1.2174f, -0.2174f, 0, 0.3221f, 0.6779f, 0, -0.0449f, 0.0449f, 1 };
    const float deutan70[9] = { 0.7324f, 0.2676f, 0, -0.3966f, 1.3966f, 0, 0.0553f, -0.0553f, 1 };
    const float tritan70[9] = { 1, -0.2073f, 0.2073f, 0, 0.6928f, 0.3072f, 0, 0.0428f, 0.9572f };
    QVERIFY(maxDifference(Correction::matrix(QStringLiteral("protan"), 0.3), QMatrix3x3(protan30)) < 0.001f);
    QVERIFY(maxDifference(Correction::matrix(QStringLiteral("protan"), 0.7), QMatrix3x3(protan70)) < 0.001f);
    QVERIFY(maxDifference(Correction::matrix(QStringLiteral("deutan"), 0.7), QMatrix3x3(deutan70)) < 0.001f);
    QVERIFY(maxDifference(Correction::matrix(QStringLiteral("tritan"), 0.7), QMatrix3x3(tritan70)) < 0.001f);

    // Between the severity table's steps, in proportion: the correction is
    // linear in the spread
    const QMatrix3x3 between = (Correction::matrix(QStringLiteral("deutan"), 0.3)
                                + Correction::matrix(QStringLiteral("deutan"), 0.4)) * 0.5f;
    QVERIFY(maxDifference(Correction::matrix(QStringLiteral("deutan"), 0.35), between) < 1e-5f);
    // Above 100 % is 100 %
    QCOMPARE(Correction::matrix(QStringLiteral("deutan"), 2), Correction::matrix(QStringLiteral("deutan"), 1));

    struct Case {
        QString name;
        // Colours a dichromat sees as everyone does, besides white
        QVector3D kept[2];
        // One they don't
        QVector3D test;
        float minimumDeterminant;
    };
    const QVector3D red(1, 0, 0);
    const QVector3D blue(0, 0, 1);
    const Case cases[] = {
        { QStringLiteral("protan"), { blue, QVector3D(1, 1, 0) }, red, 0.8f },
        { QStringLiteral("deutan"), { blue, QVector3D(1, 1, 0) }, red, 0.8f },
        // Half the classic spread: stops at a determinant of 0.5
        { QStringLiteral("tritan"), { red, QVector3D(0, 1, 1) }, blue, 0.49f },
    };
    for (const Case &c : cases) {
        const QMatrix3x3 simulation = Correction::simulation(c.name);
        const QVector3D confused = apply(simulation, c.test);
        float previous = 0;
        for (qreal strength : { 0.1, 0.35, 0.7, 1.0 }) {
            const QMatrix3x3 correction = Correction::matrix(c.name, strength);
            for (const QVector3D &colour : { QVector3D(1, 1, 1), c.kept[0], c.kept[1] })
                QVERIFY2((apply(correction, colour) - colour).length() < 1e-4f, qPrintable(c.name));
            // The dichromat sees a growing difference
            const float seen = (apply(simulation, apply(correction, c.test))
                                - apply(simulation, apply(correction, confused))).length();
            QVERIFY2(seen > previous, qPrintable(c.name));
            previous = seen;
            // Never folds
            QVERIFY2(determinant(correction) > c.minimumDeterminant, qPrintable(c.name));
        }
    }
}

void tst_Owlfish::correctionThroughPq()
{
    qputenv("OWLFISH_ENABLED", "1");
    qputenv("OWLFISH_TEMPERATURE", "6500");
    qputenv("OWLFISH_DIM", "50");
    qputenv("OWLFISH_CORRECTION", "protan");
    PqLog log;
    QTemporaryDir dir;
    QScopedPointer<OwlfishController> controller(createPqController(dir, new FakePq(&log)));
    qunsetenv("OWLFISH_CORRECTION");
    QScopedPointer<QQuickView> view(createView());
    QVERIFY(view);
    QTRY_COMPARE(controller->renderer(), QStringLiteral("pq"));

    // Corrected, then dimmed; 50 % by default
    const QVector3D gain(0.5f, 0.5f, 0.5f);
    QTRY_COMPARE(log.matrices.last(), pqMatrixOf(ColorMatrix::withGain(
            Correction::matrix(QStringLiteral("protan"), 0.5), gain)));
    QVERIFY(controller->diagnostics().split(QLatin1Char('\n')).contains(QStringLiteral("correction protan 50 %")));

    // A change fades, like the gain
    const int matrices = log.matrices.size();
    controller->settings()->setCorrection(QStringLiteral("deutan"), 30);
    QTRY_COMPARE(log.matrices.last(), pqMatrixOf(ColorMatrix::withGain(
            Correction::matrix(QStringLiteral("deutan"), 0.3), gain)));
    QVERIFY(log.matrices.size() > matrices + 2);

    // 0 % changes nothing; above 100 % is 100 %
    controller->settings()->setCorrection(QStringLiteral("protan"), 0);
    QTRY_COMPARE(log.matrices.last(), pqMatrixFor(gain));
    QVERIFY(controller->diagnostics().split(QLatin1Char('\n')).contains(QStringLiteral("correction protan 0 %")));
    controller->settings()->setCorrection(QStringLiteral("protan"), 150);
    QCOMPARE(controller->settings()->correctionStrength(), 100);

    // Greyscale, without a strength
    controller->settings()->setCorrection(QStringLiteral("greyscale"), 30);
    QTRY_COMPARE(log.matrices.last(), pqMatrixOf(ColorMatrix::withGain(ColorMatrix::saturation(0), gain)));
    QVERIFY(controller->diagnostics().split(QLatin1Char('\n')).contains(QStringLiteral("correction greyscale")));

    // None: the gain alone
    controller->settings()->setCorrection(QStringLiteral("none"));
    QTRY_COMPARE(log.matrices.last(), pqMatrixFor(gain));
    QVERIFY(controller->diagnostics().split(QLatin1Char('\n')).contains(QStringLiteral("correction none")));

    // Off: identity
    controller->settings()->setCorrection(QStringLiteral("protan"));
    controller->settings()->setEnabled(false);
    QTRY_COMPARE(log.matrices.last(), PqIdentity);
}

void tst_Owlfish::correctionWithBlend()
{
    // Blending (forced, or where fetch doesn't work) can't mix the
    // channels: the gain alone, and the diagnostics say so
    qputenv("OWLFISH_ENABLED", "1");
    qputenv("OWLFISH_TEMPERATURE", "6500");
    qputenv("OWLFISH_DIM", "50");
    qputenv("OWLFISH_CORRECTION", "deutan");
    qputenv("OWLFISH_RENDERER", "blend");
    PqLog log;
    QTemporaryDir dir;
    QScopedPointer<OwlfishController> controller(createPqController(
            dir, new FakePq(&log), QByteArray("ID=xqbt52\n")));
    qunsetenv("OWLFISH_CORRECTION");
    qunsetenv("OWLFISH_RENDERER");
    QScopedPointer<QQuickView> view(createView());
    QVERIFY(view);
    QTRY_COMPARE(controller->filterItem()->gain(), QVector3D(0.5f, 0.5f, 0.5f));
    QCOMPARE(controller->renderer(), QStringLiteral("blend"));
    QVERIFY(ColorMatrix::isIdentity(controller->filterItem()->matrix()));
    QVERIFY(log.matrices.isEmpty());
    compare(grab(view.data()), Orange, QColor(128, 64, 32));
    QVERIFY(controller->diagnostics().contains(QStringLiteral("\ncorrection deutan 50 %, not supported by this renderer")));
    controller->settings()->setCorrection(QStringLiteral("greyscale"));
    QTRY_VERIFY(controller->diagnostics().contains(QStringLiteral("\ncorrection greyscale, not supported by this renderer")));
}

void tst_Owlfish::correctionThroughFetch()
{
    // Another device, auto: the correction alone is reason enough to draw
    // with fetch
    qputenv("OWLFISH_ENABLED", "1");
    qputenv("OWLFISH_TEMPERATURE", "6500");
    qputenv("OWLFISH_DIM", "50");
    qputenv("OWLFISH_CORRECTION", "greyscale");
    PqLog log;
    QTemporaryDir dir;
    QScopedPointer<OwlfishController> controller(createPqController(
            dir, new FakePq(&log), QByteArray("ID=xqbt52\n")));
    qunsetenv("OWLFISH_CORRECTION");
    QScopedPointer<QQuickView> view(createView());
    QVERIFY(view);
    QTRY_VERIFY(controller->filterItem() && controller->filterItem()->capabilities().detected);
    if (!controller->filterItem()->capabilities().fetchWorks)
        QSKIP("No framebuffer fetch in this OpenGL implementation");

    QCOMPARE(controller->renderer(), QStringLiteral("fetch"));
    const QMatrix3x3 grey = ColorMatrix::saturation(0);
    QTRY_VERIFY(maxDifference(controller->filterItem()->matrix(), grey) < 1e-6f);
    QTRY_COMPARE(controller->filterItem()->gain(), QVector3D(0.5f, 0.5f, 0.5f));
    compare(grab(view.data()), Orange,
            throughGamma2(QColor(255, 128, 64), grey, QVector3D(0.5f, 0.5f, 0.5f)));
    QVERIFY(controller->diagnostics().contains(QStringLiteral("\ncorrection greyscale\n")));
}

void tst_Owlfish::saturationAfterCorrection()
{
    qputenv("OWLFISH_ENABLED", "1");
    qputenv("OWLFISH_TEMPERATURE", "6500");
    qputenv("OWLFISH_DIM", "50");
    qputenv("OWLFISH_SATURATION", "40");
    qputenv("OWLFISH_CORRECTION", "protan");
    PqLog log;
    QTemporaryDir dir;
    QScopedPointer<OwlfishController> controller(createPqController(dir, new FakePq(&log)));
    qunsetenv("OWLFISH_SATURATION");
    qunsetenv("OWLFISH_CORRECTION");
    QScopedPointer<QQuickView> view(createView());
    QVERIFY(view);
    QTRY_COMPARE(controller->renderer(), QStringLiteral("pq"));

    // Corrected, then desaturated, then dimmed
    const QVector3D gain(0.5f, 0.5f, 0.5f);
    const QMatrix3x3 correction = Correction::matrix(QStringLiteral("protan"), 0.5);
    QTRY_COMPARE(log.matrices.last(), pqMatrixOf(ColorMatrix::withGain(
            ColorMatrix::saturation(0.4) * correction, gain)));

    // At 0 % grey, but with the correction's lightness: red and green get
    // other greys than without it
    controller->settings()->setSaturation(0);
    const QMatrix3x3 grey = ColorMatrix::saturation(0);
    QTRY_COMPARE(log.matrices.last(), pqMatrixOf(ColorMatrix::withGain(grey * correction, gain)));
    QVERIFY(maxDifference(grey * correction, grey) > 0.01f);

    // Greyscale and the saturation share the grey: together they stay grey
    controller->settings()->setSaturation(40);
    controller->settings()->setCorrection(QStringLiteral("greyscale"));
    QTRY_COMPARE(log.matrices.last(), pqMatrixOf(ColorMatrix::withGain(grey, gain)));
    QVERIFY(maxDifference(ColorMatrix::saturation(0.4) * grey, grey) < 1e-6f);
}

void tst_Owlfish::dimmingWhen_data()
{
    QTest::addColumn<QByteArray>("when");
    // Night light's schedule, or none
    QTest::addColumn<bool>("scheduled");
    // The window around now, or one that has not started
    QTest::addColumn<bool>("inside");
    // Night light's gradual change, in minutes
    QTest::addColumn<int>("transition");
    QTest::addColumn<bool>("dimmed");

    QTest::newRow("always") << QByteArray("always") << true << false << 0 << true;
    QTest::newRow("fixed, inside") << QByteArray("fixed") << false << true << 0 << true;
    QTest::newRow("fixed, outside") << QByteArray("fixed") << false << false << 0 << false;
    QTest::newRow("with Night light, inside") << QByteArray("night_light") << true << true << 0 << true;
    QTest::newRow("with Night light, outside") << QByteArray("night_light") << true << false << 0 << false;
    // Night light all the time: so is the dimming
    QTest::newRow("with Night light, no schedule") << QByteArray("night_light") << false << false << 0 << true;
    // Fully dimmed while the colour is still fading in: the times, not the
    // gradual change
    QTest::newRow("with Night light, gradual change") << QByteArray("night_light") << true << true << 120 << true;
}

void tst_Owlfish::dimmingWhen()
{
    QFETCH(QByteArray, when);
    QFETCH(bool, scheduled);
    QFETCH(bool, inside);
    QFETCH(int, transition);
    QFETCH(bool, dimmed);

    const QByteArray from = QByteArray::number(minutesFromNow(inside ? -60 : 60));
    const QByteArray to = QByteArray::number(minutesFromNow(inside ? 60 : 120));
    qputenv("OWLFISH_ENABLED", "1");
    qputenv("OWLFISH_TEMPERATURE", "6500");
    qputenv("OWLFISH_DIM", "50");
    qputenv("OWLFISH_DIM_CUTOFF", "0");
    qputenv("OWLFISH_DIM_WHEN", when);
    qputenv("OWLFISH_DIM_FROM", from);
    qputenv("OWLFISH_DIM_TO", to);
    qputenv("OWLFISH_SCHEDULE", scheduled ? "1" : "0");
    qputenv("OWLFISH_SCHEDULE_FROM", from);
    qputenv("OWLFISH_SCHEDULE_TO", to);
    qputenv("OWLFISH_SCHEDULE_TRANSITION", QByteArray::number(transition));
    PqLog log;
    QTemporaryDir dir;
    QScopedPointer<OwlfishController> controller(createPqController(
            dir, new FakePq(&log), QByteArray("ID=xqbt52\n")));
    for (const char *name : { "OWLFISH_DIM_CUTOFF", "OWLFISH_DIM_WHEN", "OWLFISH_DIM_FROM", "OWLFISH_DIM_TO",
                              "OWLFISH_SCHEDULE", "OWLFISH_SCHEDULE_FROM", "OWLFISH_SCHEDULE_TO",
                              "OWLFISH_SCHEDULE_TRANSITION" })
        qunsetenv(name);
    QScopedPointer<QQuickView> view(createView());
    QVERIFY(view);

    const float gain = dimmed ? 0.5f : 1.0f;
    QTRY_COMPARE(controller->filterItem()->gain(), QVector3D(gain, gain, gain));
    QVERIFY(controller->diagnostics().contains(QStringLiteral("\ndimming when ")));
}

void tst_Owlfish::rendererPqDrawsNothing()
{
    qputenv("OWLFISH_ENABLED", "1");
    qputenv("OWLFISH_TEMPERATURE", "2700");
    qputenv("OWLFISH_DIM", "50");
    PqLog log;
    QTemporaryDir dir;
    QScopedPointer<OwlfishController> controller(createPqController(dir, new FakePq(&log)));
    QScopedPointer<QQuickView> view(createView());
    QVERIFY(view);

    const QVector3D gain = ColorTemperature::gain(2700) * 0.5f;
    QTRY_COMPARE(log.matrices.value(log.matrices.size() - 1), pqMatrixFor(gain));
    QCOMPARE(controller->renderer(), QStringLiteral("pq"));
    // The fade is sent step by step, each step once
    QVERIFY(log.matrices.size() > 3);
    for (int i = 1; i < log.matrices.size(); ++i)
        QVERIFY(log.matrices.at(i) != log.matrices.at(i - 1));

    const QImage image = grab(view.data());
    compare(image, White, QColor(255, 255, 255));
    compare(image, Orange, QColor(255, 128, 64));

    QVERIFY(controller->diagnostics().contains(QStringLiteral("display hardware version 7, ")));
    QVERIFY(!controller->diagnostics().contains(QStringLiteral("device jp2601, verified")));

    // Leaves the hardware neutral
    const int gains = log.gains.size();
    controller.reset();
    QCOMPARE(log.gains.size(), gains + 1);
    QCOMPARE(log.gains.last(), PqNeutralGain);
    QCOMPARE(log.matrices.last(), PqIdentity);
}

void tst_Owlfish::rendererHandover()
{
    qputenv("OWLFISH_ENABLED", "1");
    qputenv("OWLFISH_TEMPERATURE", "2700");
    qputenv("OWLFISH_DIM", "50");
    PqLog log;
    QTemporaryDir dir;
    QScopedPointer<OwlfishController> controller(createPqController(dir, new FakePq(&log)));
    QScopedPointer<QQuickView> view(createView());
    QVERIFY(view);
    const QVector3D gain = ColorTemperature::gain(2700) * 0.5f;
    QTRY_COMPARE(log.matrices.value(log.matrices.size() - 1), pqMatrixFor(gain));

    // To the GPU: the hardware is reset, no double tint
    controller->settings()->setRenderer(QStringLiteral("blend"));
    QCOMPARE(controller->renderer(), QStringLiteral("blend"));
    QCOMPARE(log.matrices.last(), PqIdentity);
    QCOMPARE(log.gains.last(), PqNeutralGain);
    QVERIFY(controller->filterItem()->isVisible());
    const QColor tinted(qRound(255 * gain.x()), qRound(255 * gain.y()), qRound(255 * gain.z()));
    QTRY_VERIFY(grab(view.data()).pixelColor(White) != QColor(Qt::white));
    compare(grab(view.data()), White, tinted);

    // And back, with the state sent again
    const int matrices = log.matrices.size();
    controller->settings()->setRenderer(QString());
    QCOMPARE(controller->renderer(), QStringLiteral("pq"));
    QCOMPARE(log.connects, 1);
    QCOMPARE(log.matrices.size(), matrices + 2);
    QCOMPARE(log.matrices.at(matrices), PqIdentity);
    QCOMPARE(log.matrices.last(), pqMatrixFor(gain));
    QTRY_COMPARE(grab(view.data()).pixelColor(White), QColor(Qt::white));
}

void tst_Owlfish::rendererMasterSwitch()
{
    qputenv("OWLFISH_ENABLED", "1");
    qputenv("OWLFISH_TEMPERATURE", "3400");
    qputenv("OWLFISH_DIM", "40");
    PqLog log;
    QTemporaryDir dir;
    QScopedPointer<OwlfishController> controller(createPqController(dir, new FakePq(&log)));
    QScopedPointer<QQuickView> view(createView());
    QVERIFY(view);
    const QVector3D gain = ColorTemperature::gain(3400) * 0.6f;
    QTRY_COMPARE(log.matrices.value(log.matrices.size() - 1), pqMatrixFor(gain));

    // Fades to neutral, still on the hardware
    int matrices = log.matrices.size();
    controller->settings()->setEnabled(false);
    QTRY_COMPARE(log.matrices.last(), PqIdentity);
    QVERIFY(log.matrices.size() > matrices + 2);
    QCOMPARE(controller->renderer(), QStringLiteral("pq"));

    matrices = log.matrices.size();
    controller->settings()->setEnabled(true);
    QTRY_COMPARE(log.matrices.last(), pqMatrixFor(gain));
    QVERIFY(log.matrices.size() > matrices + 2);
}

void tst_Owlfish::rendererFallback()
{
    qputenv("OWLFISH_ENABLED", "1");
    qputenv("OWLFISH_TEMPERATURE", "2700");
    qputenv("OWLFISH_DIM", "50");
    PqLog log;
    // The reset at the start works (gain, identity), the fade's second step
    // fails
    log.failFrom = 3;
    QTemporaryDir dir;
    QScopedPointer<OwlfishController> controller(createPqController(dir, new FakePq(&log)));
    QScopedPointer<QQuickView> view(createView());
    QVERIFY(view);

    QTRY_VERIFY(controller->filterItem()->capabilities().detected);
    const QString gpu = gpuRenderer(controller->filterItem());
    QTRY_COMPARE(controller->renderer(), gpu);
    QVERIFY(controller->filterItem()->isVisible());
    QVERIFY2(controller->diagnostics().contains(
                     QStringLiteral("renderer %1 (display hardware failed: fake failure").arg(gpu)),
             qPrintable(controller->diagnostics()));
    QVERIFY(controller->diagnostics().contains(QStringLiteral("\ndevice jp2601, verified\n")));
    // The failed step, then the reset of what can still be reset
    QCOMPARE(log.attempts, 6);

    // The item carries on with the fade
    const QVector3D gain = ColorTemperature::gain(2700) * 0.5f;
    QTRY_VERIFY(qFuzzyCompare(controller->filterItem()->gain(), gain));
    compare(grab(view.data()), White, QColor(qRound(255 * gain.x()), qRound(255 * gain.y()),
                                             qRound(255 * gain.z())));

    // For the rest of the run
    controller->settings()->setRenderer(QStringLiteral("pq"));
    QCOMPARE(controller->renderer(), gpu);
    QCOMPARE(log.attempts, 6);
    controller.reset();
    QCOMPARE(log.attempts, 6);
}

void tst_Owlfish::rendererResetOnQuit()
{
    qputenv("OWLFISH_ENABLED", "1");
    qputenv("OWLFISH_TEMPERATURE", "2700");
    qputenv("OWLFISH_DIM", "0");
    PqLog log;
    QTemporaryDir dir;
    QScopedPointer<OwlfishController> controller(createPqController(dir, new FakePq(&log)));
    QScopedPointer<QQuickView> view(createView());
    QVERIFY(view);
    QTRY_COMPARE(log.matrices.value(log.matrices.size() - 1), pqMatrixFor(ColorTemperature::gain(2700)));

    // The compositor quits; the hardware must not keep the tint
    const int gains = log.gains.size();
    QVERIFY(QMetaObject::invokeMethod(QCoreApplication::instance(), "aboutToQuit"));
    QCOMPARE(log.gains.size(), gains + 1);
    QCOMPARE(log.matrices.last(), PqIdentity);
    // Only once
    controller.reset();
    QCOMPARE(log.gains.size(), gains + 1);
}

void tst_Owlfish::rendererCrashGuard()
{
    qputenv("OWLFISH_ENABLED", "1");
    qputenv("OWLFISH_TEMPERATURE", "2700");
    qputenv("OWLFISH_DIM", "50");
    QTemporaryDir dir;
    CrashGuard guard(dir.filePath(QStringLiteral("unhealthy-starts")), 3);
    for (int i = 0; i < 3; ++i)
        QVERIFY(guard.begin());

    // Left to a reboot: the calls could be what crashed the compositor
    PqLog log;
    QScopedPointer<OwlfishController> controller(createPqController(dir, new FakePq(&log)));
    QVERIFY(!controller->isActive());
    QScopedPointer<QQuickView> view(createView());
    QVERIFY(view);
    QTest::qWait(200);
    QCOMPARE(controller->renderer(), QStringLiteral("none"));
    QVERIFY(controller->diagnostics().contains(QStringLiteral("\nstatus crash-guard\nrenderer none (crash-guard)\n")));
    controller.reset();
    QCOMPARE(log.connects, 0);
    QCOMPARE(log.attempts, 0);
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

void tst_Owlfish::cutoffInitialReadingAppliesImmediately()
{
    AmbientCutoff cutoff;
    cutoff.setThreshold(1000);
    cutoff.setDelays(60000, 60000);
    QSignalSpy spy(&cutoff, &AmbientCutoff::brightChanged);

    cutoff.addInitialReading(20000);
    QVERIFY(cutoff.isBright());
    QCOMPARE(spy.count(), 1);

    // A moved threshold applies to it as to a reading
    cutoff.setThreshold(30000);
    QVERIFY(!cutoff.isBright());
}

void tst_Owlfish::cutoffReadingReplacesInitialReading()
{
    // The initial level may be stale: the first real reading applies at once
    AmbientCutoff cutoff;
    cutoff.setThreshold(1000);
    cutoff.setDelays(60000, 60000);
    cutoff.addInitialReading(20000);
    QVERIFY(cutoff.isBright());

    cutoff.addReading(10);
    QVERIFY(!cutoff.isBright());

    // Then the delays apply again
    cutoff.addReading(20000);
    QVERIFY(!cutoff.isBright());
}

void tst_Owlfish::cutoffInitialReadingAfterReadingIgnored()
{
    // A reply that arrives after the first reading is older than it
    AmbientCutoff cutoff;
    cutoff.setThreshold(1000);
    cutoff.addReading(20000);
    QSignalSpy spy(&cutoff, &AmbientCutoff::brightChanged);

    cutoff.addInitialReading(10);
    QVERIFY(cutoff.isBright());
    QCOMPARE(spy.count(), 0);

    // Only one initial reading, too
    AmbientCutoff other;
    other.setThreshold(1000);
    other.addInitialReading(20000);
    other.addInitialReading(10);
    QVERIFY(other.isBright());
}

void tst_Owlfish::cutoffInitialReadingAfterReset()
{
    AmbientCutoff cutoff;
    cutoff.setThreshold(1000);
    cutoff.addReading(20000);
    cutoff.reset();
    QVERIFY(!cutoff.isBright());

    // The sensor was started again
    cutoff.addInitialReading(20000);
    QVERIFY(cutoff.isBright());
}

void tst_Owlfish::cutoffSaturatedCountsAsBright()
{
    // The Jolla Phone's sensor stops at 65535 × 0.0333333 lux, below a
    // threshold set on another phone
    const qreal maximum = 65535 * 0.0333333;
    AmbientCutoff cutoff;
    cutoff.setThreshold(5000);
    cutoff.setDelays(0, 0);
    cutoff.setMaximum(maximum);

    cutoff.addReading(maximum);
    QVERIFY(cutoff.isBright());
    cutoff.addReading(1000);
    QTRY_VERIFY(!cutoff.isBright());
    // One step below the top still counts
    cutoff.addReading(65534 * 0.0333333);
    QTRY_VERIFY(cutoff.isBright());

    // Not known: only the threshold
    AmbientCutoff unknown;
    unknown.setThreshold(5000);
    unknown.addReading(maximum);
    QVERIFY(!unknown.isBright());
}

void tst_Owlfish::cutoffMaximumAppliesToLastReading()
{
    // The range can arrive after the first reading
    AmbientCutoff cutoff;
    cutoff.setThreshold(5000);
    cutoff.setDelays(60000, 60000);
    cutoff.addInitialReading(2184.5);
    QVERIFY(!cutoff.isBright());

    cutoff.setMaximum(2184.5);
    QVERIFY(cutoff.isBright());
    // And stays after a restart of the sensor
    cutoff.reset();
    cutoff.addReading(2184.5);
    QVERIFY(cutoff.isBright());
}

void tst_Owlfish::alsLuxFromReply()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        QSKIP("No session bus");
    qDBusRegisterMetaType<TimedLux>();
    qDBusRegisterMetaType<SensorRange>();
    qDBusRegisterMetaType<QList<SensorRange>>();

    FakeAlsSensor sensor;
    const QString path = QStringLiteral("/SensorManager/alssensor");
    QVERIFY(bus.registerObject(path, &sensor, QDBusConnection::ExportAllSlots));
    // Another connection, so the call goes through the bus and the reply is
    // demarshalled as on the phone
    QDBusConnection client = QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                                           QStringLiteral("owlfish-test-client"));
    QVERIFY(client.isConnected());

    auto call = [&](const QString &method) -> QDBusMessage {
        QDBusPendingCall pending = client.asyncCall(QDBusMessage::createMethodCall(
                bus.baseService(), path, QStringLiteral("local.ALSSensor"), method));
        if (!QTest::qWaitFor([&pending]() { return pending.isFinished(); }, 5000))
            qWarning() << method << "timed out";
        return pending.reply();
    };
    quint32 lux = 0;
    QVERIFY(::alsLuxFromReply(call(QStringLiteral("lux")), &lux));
    QCOMPARE(lux, 800u);

    lux = 0;
    QVERIFY(!::alsLuxFromReply(call(QStringLiteral("plainLux")), &lux));
    QVERIFY(!::alsLuxFromReply(call(QStringLiteral("noSuchMethod")), &lux));
    QCOMPARE(lux, 0u);

    double maximum = 0;
    QVERIFY(::alsMaximumFromReply(call(QStringLiteral("getAvailableDataRanges")), &maximum));
    QCOMPARE(maximum, 65535.0);
    maximum = 0;
    QVERIFY(!::alsMaximumFromReply(call(QStringLiteral("noRanges")), &maximum));
    QVERIFY(!::alsMaximumFromReply(call(QStringLiteral("lux")), &maximum));
    QVERIFY(!::alsMaximumFromReply(call(QStringLiteral("noSuchMethod")), &maximum));
    QCOMPARE(maximum, 0.0);

    bus.unregisterObject(path);
    QDBusConnection::disconnectFromBus(QStringLiteral("owlfish-test-client"));
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
