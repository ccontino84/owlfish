// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "colorfilteritem.h"
#include "colorfiltermaterial.h"

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

bool ColorFilterItem::isIdentity() const
{
    return m_gain.x() >= IdentityThreshold
            && m_gain.y() >= IdentityThreshold
            && m_gain.z() >= IdentityThreshold;
}

QSGNode *ColorFilterItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    QSGGeometryNode *node = static_cast<QSGGeometryNode *>(oldNode);

    if (isIdentity() || width() <= 0 || height() <= 0) {
        delete node;
        return nullptr;
    }

    if (!node) {
        node = new QSGGeometryNode;
        QSGGeometry *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4);
        geometry->setDrawingMode(GL_TRIANGLE_STRIP);
        node->setGeometry(geometry);
        node->setFlag(QSGNode::OwnsGeometry);
        node->setMaterial(new ColorFilterMaterial);
        node->setFlag(QSGNode::OwnsMaterial);
    }

    QSGGeometry::updateRectGeometry(node->geometry(), boundingRect());
    static_cast<ColorFilterMaterial *>(node->material())->setGain(m_gain);
    node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);

    return node;
}

void ColorFilterItem::geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChanged(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        update();
}
