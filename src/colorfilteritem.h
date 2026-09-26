// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef COLORFILTERITEM_H
#define COLORFILTERITEM_H

#include <QQuickItem>
#include <QVector3D>

// Full-item colour filter: everything rendered below the item is multiplied
// per channel by gain. A gain of (1, 1, 1) removes the scene graph node, so
// the filter costs nothing while it is off.
class ColorFilterItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QVector3D gain READ gain WRITE setGain NOTIFY gainChanged)

public:
    explicit ColorFilterItem(QQuickItem *parent = nullptr);

    QVector3D gain() const { return m_gain; }
    void setGain(const QVector3D &gain);

    bool isIdentity() const;

signals:
    void gainChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;
    void geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    QVector3D m_gain;
};

#endif
