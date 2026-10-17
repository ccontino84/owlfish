// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "colorfilteritem.h"
#include "colorfiltermaterial.h"
#include "colormatrix.h"

#include <QMetaObject>
#include <QSGGeometryNode>

namespace {

// Gains this close to 1 are indistinguishable at 8 bits per channel
const float IdentityThreshold = 0.999f;

float clampGain(float value)
{
    return qBound(0.0f, value, 1.0f);
}

}

ColorFilterItem::ColorFilterItem(QQuickItem *parent)
    : QQuickItem(parent)
    , m_gain(1, 1, 1)
    , m_renderer(ColorFilter::Blend)
    , m_active(ColorFilter::None)
{
    // Purely visual: no mouse, touch or key handling, so input goes to the
    // items below
    setFlag(ItemHasContents, true);
}

void ColorFilterItem::setGain(const QVector3D &gain)
{
    const QVector3D clamped(clampGain(gain.x()), clampGain(gain.y()), clampGain(gain.z()));
    if (m_gain == clamped)
        return;

    m_gain = clamped;
    update();
    emit gainChanged();
}

void ColorFilterItem::setMatrix(const QMatrix3x3 &matrix)
{
    if (m_matrix == matrix)
        return;
    m_matrix = matrix;
    update();
}

void ColorFilterItem::setRenderer(ColorFilter::Renderer renderer)
{
    renderer = renderer == ColorFilter::Fetch ? ColorFilter::Fetch : ColorFilter::Blend;
    if (m_renderer == renderer)
        return;
    m_renderer = renderer;
    update();
}

bool ColorFilterItem::isIdentity() const
{
    return m_gain.x() >= IdentityThreshold
            && m_gain.y() >= IdentityThreshold
            && m_gain.z() >= IdentityThreshold
            && ColorMatrix::isIdentity(m_matrix);
}

QSGNode *ColorFilterItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    QSGGeometryNode *node = static_cast<QSGGeometryNode *>(oldNode);

    // Render thread, with the context current and the GUI thread blocked.
    // Nothing is checked unless Fetch is asked for.
    const ColorFilter::Renderer previous = m_active;
    bool detected = false;
    if (m_renderer == ColorFilter::Fetch && !m_capabilities.detected) {
        m_capabilities = ColorFilterMaterial::detect();
        detected = true;
    }

    // Fetch only while there is a matrix to apply; the gain alone is cheaper
    // with Blend
    const bool fetch = m_renderer == ColorFilter::Fetch && m_capabilities.fetchWorks
            && !ColorMatrix::isIdentity(m_matrix);
    const bool gainOnly = m_gain.x() >= IdentityThreshold && m_gain.y() >= IdentityThreshold
            && m_gain.z() >= IdentityThreshold;
    ColorFilter::Renderer renderer = fetch ? ColorFilter::Fetch : ColorFilter::Blend;
    if ((gainOnly && !fetch) || width() <= 0 || height() <= 0)
        renderer = ColorFilter::None;
    m_active = renderer;
    if (detected || m_active != previous)
        QMetaObject::invokeMethod(this, "rendererChanged", Qt::QueuedConnection);

    if (renderer == ColorFilter::None) {
        delete node;
        return nullptr;
    }

    ColorFilterMaterial *material = node ? static_cast<ColorFilterMaterial *>(node->material()) : nullptr;
    if (material && material->renderer() != renderer) {
        delete node;
        node = nullptr;
    }

    if (!node) {
        node = new QSGGeometryNode;
        QSGGeometry *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4);
        geometry->setDrawingMode(GL_TRIANGLE_STRIP);
        node->setGeometry(geometry);
        node->setFlag(QSGNode::OwnsGeometry);
        material = new ColorFilterMaterial(renderer, m_capabilities.fetch);
        node->setMaterial(material);
        node->setFlag(QSGNode::OwnsMaterial);
    }

    QSGGeometry::updateRectGeometry(node->geometry(), boundingRect());
    material->setGain(m_gain);
    material->setMatrix(m_matrix);
    node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);

    return node;
}

void ColorFilterItem::geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChanged(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        update();
}
