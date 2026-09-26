// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef COLORFILTERMATERIAL_H
#define COLORFILTERMATERIAL_H

#include <QSGMaterial>
#include <QVector3D>

// Multiplies whatever has already been rendered below it by a per-channel
// gain: out.rgb = framebuffer.rgb * gain. Black stays black on every channel.
//
// Only public scene graph API is used. The blend function is switched in
// QSGMaterialShader::activate() and restored in deactivate(); the batch
// renderer calls these around every batch that uses this material.
class ColorFilterMaterial : public QSGMaterial
{
public:
    ColorFilterMaterial();

    QSGMaterialType *type() const override;
    QSGMaterialShader *createShader() const override;
    int compare(const QSGMaterial *other) const override;

    QVector3D gain() const { return m_gain; }
    void setGain(const QVector3D &gain) { m_gain = gain; }

private:
    QVector3D m_gain;
};

#endif
