// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "colorfiltermaterial.h"

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>

namespace {

const char *const VertexShader =
        "attribute highp vec4 qt_Vertex;\n"
        "uniform highp mat4 qt_Matrix;\n"
        "void main() {\n"
        "    gl_Position = qt_Matrix * qt_Vertex;\n"
        "}\n";

const char *const BlendShader =
        "uniform lowp vec3 gain;\n"
        "void main() {\n"
        "    gl_FragColor = vec4(gain, 1.0);\n"
        "}\n";

// Fetch: the colour already in the framebuffer, then the matrix in
// approximately linear light (gamma 2.0: square, matrix, square root; no
// pow(), so it costs little more than the gain), then the gain in encoded
// space (the same gain as Blend). sqrt() only gets clamped, non-negative
// arguments.
QByteArray fetchShader(ColorFilter::FetchExtension fetch)
{
    QByteArray source;
    QByteArray read;
    if (fetch == ColorFilter::ArmFetch) {
        source += "#extension GL_ARM_shader_framebuffer_fetch : require\n";
        read = "gl_LastFragColorARM.rgb";
    } else {
        source += "#extension GL_EXT_shader_framebuffer_fetch : require\n";
        read = "gl_LastFragData[0].rgb";
    }
    source += "#ifdef GL_ES\n"
              "precision mediump float;\n"
              "#endif\n"
              "uniform vec3 gain;\n"
              "uniform mat3 colorMatrix;\n"
              "void main() {\n"
              "    vec3 c = " + read + ";\n"
              "    c = sqrt(clamp(colorMatrix * (c * c), 0.0, 1.0));\n"
              // Alpha is masked out (activate())
              "    gl_FragColor = vec4(c * gain, 1.0);\n"
              "}\n";
    return source;
}

class ColorFilterShader : public QSGMaterialShader
{
public:
    ColorFilterShader(ColorFilter::Renderer renderer, ColorFilter::FetchExtension fetch)
        : m_renderer(renderer)
        , m_fragmentShader(ColorFilterMaterial::fragmentShaderSource(renderer, fetch))
    {
    }

    const char *vertexShader() const override
    {
        return ColorFilterMaterial::vertexShaderSource();
    }

    const char *fragmentShader() const override
    {
        return m_fragmentShader.constData();
    }

    char const *const *attributeNames() const override
    {
        static const char *const names[] = { "qt_Vertex", nullptr };
        return names;
    }

    void activate() override
    {
        QOpenGLFunctions *gl = QOpenGLContext::currentContext()->functions();
        if (m_renderer == ColorFilter::Blend) {
            // dst * src.rgb, destination alpha untouched
            gl->glBlendFunc(GL_ZERO, GL_SRC_COLOR);
            return;
        }
        // Replace the colour, keep the destination alpha
        gl->glBlendFunc(GL_ONE, GL_ZERO);
        gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    }

    void deactivate() override
    {
        QOpenGLFunctions *gl = QOpenGLContext::currentContext()->functions();
        // The batch renderer draws every blended batch with premultiplied
        // alpha blending and does not reset it between batches
        gl->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        if (m_renderer != ColorFilter::Blend)
            gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }

    void updateState(const RenderState &state, QSGMaterial *newMaterial, QSGMaterial *oldMaterial) override
    {
        if (state.isMatrixDirty())
            program()->setUniformValue(m_matrixLocation, state.combinedMatrix());

        ColorFilterMaterial *material = static_cast<ColorFilterMaterial *>(newMaterial);
        ColorFilterMaterial *previous = static_cast<ColorFilterMaterial *>(oldMaterial);
        if (!previous || previous->gain() != material->gain())
            program()->setUniformValue(m_gainLocation, material->gain());
        if (m_renderer == ColorFilter::Fetch && (!previous || previous->matrix() != material->matrix()))
            program()->setUniformValue(m_colorMatrixLocation, material->matrix());
    }

protected:
    void initialize() override
    {
        m_matrixLocation = program()->uniformLocation("qt_Matrix");
        m_gainLocation = program()->uniformLocation("gain");
        if (m_renderer == ColorFilter::Fetch)
            m_colorMatrixLocation = program()->uniformLocation("colorMatrix");
    }

private:
    ColorFilter::Renderer m_renderer;
    QByteArray m_fragmentShader;
    int m_matrixLocation = -1;
    int m_gainLocation = -1;
    int m_colorMatrixLocation = -1;
};

// Links a program from the sources the material would use
bool links(ColorFilter::FetchExtension fetch)
{
    QOpenGLShaderProgram program;
    program.addShaderFromSourceCode(QOpenGLShader::Vertex, ColorFilterMaterial::vertexShaderSource());
    program.addShaderFromSourceCode(QOpenGLShader::Fragment,
                                    ColorFilterMaterial::fragmentShaderSource(ColorFilter::Fetch, fetch));
    program.bindAttributeLocation("qt_Vertex", 0);
    return program.link();
}

}

namespace ColorFilter {

QString rendererName(Renderer renderer)
{
    switch (renderer) {
    case None:
        return QStringLiteral("none");
    case Blend:
        return QStringLiteral("blend");
    case Fetch:
        return QStringLiteral("fetch");
    }
    return QString();
}

QString fetchExtensionName(FetchExtension fetch)
{
    switch (fetch) {
    case NoFetch:
        return QStringLiteral("none");
    case ArmFetch:
        return QStringLiteral("GL_ARM_shader_framebuffer_fetch");
    case ExtFetch:
        return QStringLiteral("GL_EXT_shader_framebuffer_fetch");
    }
    return QString();
}

}

ColorFilterMaterial::ColorFilterMaterial(ColorFilter::Renderer renderer, ColorFilter::FetchExtension fetch)
    : m_renderer(renderer == ColorFilter::Fetch ? ColorFilter::Fetch : ColorFilter::Blend)
    , m_fetch(fetch)
    , m_gain(1, 1, 1)
{
    // Keeps the node in the renderer's blended (back-to-front) pass, so it is
    // drawn after the content below it
    setFlag(Blending, true);
}

QSGMaterialType *ColorFilterMaterial::type() const
{
    // One per shader
    static QSGMaterialType blend, fetch;
    return m_renderer == ColorFilter::Fetch ? &fetch : &blend;
}

QSGMaterialShader *ColorFilterMaterial::createShader() const
{
    return new ColorFilterShader(m_renderer, m_fetch);
}

int ColorFilterMaterial::compare(const QSGMaterial *other) const
{
    const ColorFilterMaterial *material = static_cast<const ColorFilterMaterial *>(other);
    for (int i = 0; i < 3; ++i) {
        if (m_gain[i] != material->m_gain[i])
            return m_gain[i] < material->m_gain[i] ? -1 : 1;
    }
    const float *values = m_matrix.constData();
    const float *otherValues = material->m_matrix.constData();
    for (int i = 0; i < 9; ++i) {
        if (values[i] != otherValues[i])
            return values[i] < otherValues[i] ? -1 : 1;
    }
    return 0;
}

ColorFilter::Capabilities ColorFilterMaterial::detect()
{
    ColorFilter::Capabilities capabilities;
    capabilities.detected = true;
    QOpenGLContext *context = QOpenGLContext::currentContext();
    if (!context)
        return capabilities;

    QOpenGLFunctions *gl = context->functions();
    const auto string = [gl](GLenum name) {
        return QByteArray(reinterpret_cast<const char *>(gl->glGetString(name)));
    };
    capabilities.vendor = string(GL_VENDOR);
    capabilities.renderer = string(GL_RENDERER);
    capabilities.version = string(GL_VERSION);

    // ARM's is Mali's own and always coherent
    if (context->hasExtension("GL_ARM_shader_framebuffer_fetch"))
        capabilities.fetch = ColorFilter::ArmFetch;
    else if (context->hasExtension("GL_EXT_shader_framebuffer_fetch"))
        capabilities.fetch = ColorFilter::ExtFetch;

    capabilities.fetchWorks = capabilities.fetch != ColorFilter::NoFetch
            && links(capabilities.fetch);
    return capabilities;
}

const char *ColorFilterMaterial::vertexShaderSource()
{
    return VertexShader;
}

QByteArray ColorFilterMaterial::fragmentShaderSource(ColorFilter::Renderer renderer,
                                                     ColorFilter::FetchExtension fetch)
{
    if (renderer != ColorFilter::Fetch)
        return BlendShader;
    return fetchShader(fetch);
}
