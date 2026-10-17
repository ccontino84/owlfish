// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef COLORFILTERITEM_H
#define COLORFILTERITEM_H

#include "colorfiltermaterial.h"

#include <QGenericMatrix>
#include <QQuickItem>
#include <QVector3D>

// Full-item colour filter: everything rendered below the item is multiplied
// per channel by gain. With the Fetch renderer, each pixel can first go
// through a matrix in approximately linear light. With gain (1, 1, 1) and
// no matrix the scene graph node is removed, so the filter costs nothing
// while it is off.
class ColorFilterItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QVector3D gain READ gain WRITE setGain NOTIFY gainChanged)

public:
    explicit ColorFilterItem(QQuickItem *parent = nullptr);

    QVector3D gain() const { return m_gain; }
    void setGain(const QVector3D &gain);

    // Applied only by Fetch; the identity means none
    QMatrix3x3 matrix() const { return m_matrix; }
    void setMatrix(const QMatrix3x3 &matrix);

    // Blend (the default) or Fetch. Whether Fetch works is checked at the
    // first frame after it is asked for. Fetch draws only while the matrix
    // isn't the identity; otherwise, until the check, and if Fetch does not
    // work, Blend draws the gain without the matrix.
    ColorFilter::Renderer renderer() const { return m_renderer; }
    void setRenderer(ColorFilter::Renderer renderer);

    // Nothing to draw: gain (1, 1, 1) and no matrix
    bool isIdentity() const;

    // From the last frame's synchronisation; None before the first
    ColorFilter::Renderer activeRenderer() const { return m_active; }
    // Found at the first synchronisation with Fetch asked for; detected is
    // false until then
    ColorFilter::Capabilities capabilities() const { return m_capabilities; }

signals:
    void gainChanged();
    // Queued from the render thread when capabilities or activeRenderer
    // changed
    void rendererChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;
    void geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    QVector3D m_gain;
    QMatrix3x3 m_matrix;
    ColorFilter::Renderer m_renderer;
    // Written during synchronisation, while the GUI thread is blocked
    ColorFilter::Renderer m_active;
    ColorFilter::Capabilities m_capabilities;
};

#endif
