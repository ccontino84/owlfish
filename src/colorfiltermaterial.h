// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef COLORFILTERMATERIAL_H
#define COLORFILTERMATERIAL_H

#include <QByteArray>
#include <QGenericMatrix>
#include <QSGMaterial>
#include <QString>
#include <QVector3D>

namespace ColorFilter {

// How the GPU draws the filter over what has already been rendered below it:
//   Blend  out = dst × gain, by the blend unit (GL_ZERO, GL_SRC_COLOR);
//          gain only
//   Fetch  the shader reads dst through a framebuffer fetch extension and
//          writes the result with blending replaced, after a matrix in
//          approximately linear light (gamma 2.0 for the sRGB curves):
//          out = sqrt(matrix · dst²) × gain. Only while there is a matrix
//          to apply: without one, Blend draws the gain (cheaper on Mali).
enum Renderer {
    None,
    Blend,
    Fetch
};

enum FetchExtension {
    NoFetch,
    // GL_ARM_shader_framebuffer_fetch (Mali): gl_LastFragColorARM
    ArmFetch,
    // GL_EXT_shader_framebuffer_fetch: gl_LastFragData[0]
    ExtFetch
};

// What the GL context supports for Fetch, found once with the context
// current
struct Capabilities
{
    bool detected = false;
    QByteArray vendor;
    QByteArray renderer;
    QByteArray version;
    FetchExtension fetch = NoFetch;
    // Fetch's shader compiled and linked
    bool fetchWorks = false;
};

QString rendererName(Renderer renderer);
QString fetchExtensionName(FetchExtension fetch);

}

// One full-screen quad drawn after everything below it, in the renderer's
// blended pass. Only public scene graph API is used: the GL state the
// renderer does not expect is set in QSGMaterialShader::activate() and
// restored in deactivate(), which the batch renderer calls around every
// batch that uses this material.
class ColorFilterMaterial : public QSGMaterial
{
public:
    explicit ColorFilterMaterial(ColorFilter::Renderer renderer = ColorFilter::Blend,
                                 ColorFilter::FetchExtension fetch = ColorFilter::NoFetch);

    QSGMaterialType *type() const override;
    QSGMaterialShader *createShader() const override;
    int compare(const QSGMaterial *other) const override;

    ColorFilter::Renderer renderer() const { return m_renderer; }

    QVector3D gain() const { return m_gain; }
    void setGain(const QVector3D &gain) { m_gain = gain; }
    QMatrix3x3 matrix() const { return m_matrix; }
    void setMatrix(const QMatrix3x3 &matrix) { m_matrix = matrix; }

    // Needs a current OpenGL context
    static ColorFilter::Capabilities detect();

    static const char *vertexShaderSource();
    static QByteArray fragmentShaderSource(ColorFilter::Renderer renderer, ColorFilter::FetchExtension fetch);

private:
    ColorFilter::Renderer m_renderer;
    ColorFilter::FetchExtension m_fetch;
    QVector3D m_gain;
    QMatrix3x3 m_matrix;
};

#endif
