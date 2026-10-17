// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "gpuoutput.h"
#include "logging.h"
#include "settings.h"

#include <QQuickWindow>

namespace {

// Above everything the compositor QML puts in the window
const qreal FilterZ = 1e9;

}

GpuOutput::GpuOutput(QQuickWindow *window, QObject *parent)
    : FilterOutput(parent)
    , m_window(window)
    , m_item(new ColorFilterItem(window->contentItem()))
    , m_blendOnly(false)
    , m_capabilitiesLogged(false)
{
    m_item->setObjectName(QStringLiteral("owlfish-filter"));
    m_item->setZ(FilterZ);
    connect(m_item.data(), &ColorFilterItem::rendererChanged, this, &GpuOutput::itemRendererChanged);
    connect(window, &QWindow::widthChanged, this, &GpuOutput::updateGeometry);
    connect(window, &QWindow::heightChanged, this, &GpuOutput::updateGeometry);
    updateGeometry();
    setState(Ready, QString());
    setActive(true);
}

GpuOutput::~GpuOutput()
{
}

QString GpuOutput::name() const
{
    return m_item ? ColorFilter::rendererName(m_item->renderer()) : QStringLiteral("none");
}

bool GpuOutput::supportsMatrix() const
{
    return m_item && m_item->renderer() == ColorFilter::Fetch && m_item->capabilities().fetchWorks;
}

bool GpuOutput::isNamed(const QString &key) const
{
    return key == QLatin1String("fetch") || key == QLatin1String("blend");
}

void GpuOutput::configure(const OwlfishSettings *settings)
{
    m_blendOnly = settings->renderer() == QLatin1String("blend");
    updateRenderer();
}

void GpuOutput::updateRenderer()
{
    if (!m_item)
        return;
    const ColorFilter::Capabilities capabilities = m_item->capabilities();
    const bool fetchFails = capabilities.detected && !capabilities.fetchWorks;
    m_item->setRenderer(!m_blendOnly && !fetchFails ? ColorFilter::Fetch : ColorFilter::Blend);
    setState(Ready, !m_blendOnly && fetchFails
             ? QStringLiteral("fetch not supported (extension %1)")
               .arg(ColorFilter::fetchExtensionName(capabilities.fetch))
             : QString());
}

bool GpuOutput::activate()
{
    setActive(true);
    if (m_item)
        m_item->setVisible(true);
    return true;
}

void GpuOutput::deactivate()
{
    setActive(false);
    if (m_item)
        m_item->setVisible(false);
}

void GpuOutput::apply(const QVector3D &gain, const QMatrix3x3 &matrix)
{
    if (!m_item)
        return;
    m_item->setGain(gain);
    // Drawn by Fetch only
    m_item->setMatrix(matrix);
}

void GpuOutput::updateGeometry()
{
    if (m_item && m_window)
        m_item->setSize(QSizeF(m_window->width(), m_window->height()));
}

void GpuOutput::itemRendererChanged()
{
    if (!m_item || m_capabilitiesLogged || !m_item->capabilities().detected)
        return;
    m_capabilitiesLogged = true;
    const ColorFilter::Capabilities capabilities = m_item->capabilities();
    qCInfo(lcOwlfish) << "OpenGL" << capabilities.vendor << capabilities.renderer << capabilities.version
                        << "fetch" << ColorFilter::fetchExtensionName(capabilities.fetch)
                        << (capabilities.fetchWorks ? "works" : "not usable");
    updateRenderer();
    // Now it is known whether the matrix can be drawn
    emit checked();
}

QStringList GpuOutput::diagnostics() const
{
    // Only known once the GPU draws
    if (!m_item || !m_item->capabilities().detected)
        return QStringList();
    const ColorFilter::Capabilities capabilities = m_item->capabilities();
    return QStringList()
            << QStringLiteral("GL %1, %2, %3")
               .arg(QString::fromLatin1(capabilities.vendor), QString::fromLatin1(capabilities.renderer),
                    QString::fromLatin1(capabilities.version))
            << QStringLiteral("fetch %1, %2")
               .arg(ColorFilter::fetchExtensionName(capabilities.fetch),
                    capabilities.fetchWorks ? QStringLiteral("works") : QStringLiteral("not usable"));
}
