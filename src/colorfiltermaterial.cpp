// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "colorfiltermaterial.h"

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>

namespace {

class ColorFilterShader : public QSGMaterialShader
{
public:
    const char *vertexShader() const override
    {
        return "attribute highp vec4 qt_Vertex;\n"
               "uniform highp mat4 qt_Matrix;\n"
               "void main() {\n"
               "    gl_Position = qt_Matrix * qt_Vertex;\n"
               "}\n";
    }

    const char *fragmentShader() const override
    {
        return "uniform lowp vec3 gain;\n"
               "void main() {\n"
               "    gl_FragColor = vec4(gain, 1.0);\n"
               "}\n";
    }

    char const *const *attributeNames() const override
    {
        static const char *const names[] = { "qt_Vertex", nullptr };
        return names;
    }

    void activate() override
    {
        // dst * src.rgb, destination alpha untouched
        QOpenGLContext::currentContext()->functions()->glBlendFunc(GL_ZERO, GL_SRC_COLOR);
    }

    void deactivate() override
    {
        // The batch renderer draws every blended batch with premultiplied
        // alpha blending and does not reset it between batches
        QOpenGLContext::currentContext()->functions()->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }

    void updateState(const RenderState &state, QSGMaterial *newMaterial, QSGMaterial *oldMaterial) override
    {
        if (state.isMatrixDirty())
            program()->setUniformValue(m_matrixLocation, state.combinedMatrix());

        ColorFilterMaterial *material = static_cast<ColorFilterMaterial *>(newMaterial);
        ColorFilterMaterial *previous = static_cast<ColorFilterMaterial *>(oldMaterial);
        if (!previous || previous->gain() != material->gain())
            program()->setUniformValue(m_gainLocation, material->gain());
    }

protected:
    void initialize() override
    {
        m_matrixLocation = program()->uniformLocation("qt_Matrix");
        m_gainLocation = program()->uniformLocation("gain");
    }

private:
    int m_matrixLocation = -1;
    int m_gainLocation = -1;
};

}

ColorFilterMaterial::ColorFilterMaterial()
    : m_gain(1, 1, 1)
{
    // Keeps the node in the renderer's blended (back-to-front) pass, so it is
    // drawn after the content below it
    setFlag(Blending, true);
}

QSGMaterialType *ColorFilterMaterial::type() const
{
    static QSGMaterialType type;
    return &type;
}

QSGMaterialShader *ColorFilterMaterial::createShader() const
{
    return new ColorFilterShader;
}

int ColorFilterMaterial::compare(const QSGMaterial *other) const
{
    const QVector3D otherGain = static_cast<const ColorFilterMaterial *>(other)->m_gain;
    for (int i = 0; i < 3; ++i) {
        if (m_gain[i] != otherGain[i])
            return m_gain[i] < otherGain[i] ? -1 : 1;
    }
    return 0;
}
