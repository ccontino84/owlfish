// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef OWLFISH_GPUOUTPUT_H
#define OWLFISH_GPUOUTPUT_H

#include "colorfilteritem.h"
#include "filteroutput.h"

#include <QPointer>

class QQuickWindow;

// The GPU: a ColorFilterItem on top of the compositor's scene, drawing with
// Fetch (the matrix, then the gain) where it works and Blend (the gain only)
// otherwise. Always there, the fallback for the display hardware. While
// another output is active, the item is hidden but still gets every step,
// so it can take over at once.
class GpuOutput : public FilterOutput
{
    Q_OBJECT

public:
    explicit GpuOutput(QQuickWindow *window, QObject *parent = nullptr);
    ~GpuOutput() override;

    ColorFilterItem *item() const { return m_item; }

    // "fetch" or "blend", as ColorFilter::rendererName()
    QString name() const override;
    // Both force the GPU; blend also keeps it from using Fetch
    bool isNamed(const QString &key) const override;
    // The renderer key: Fetch where it works, unless it is "blend"
    void configure(const OwlfishSettings *settings) override;
    bool applies() override { return true; }
    // Nothing to check: Blend always works. Whether Fetch does is found at
    // the item's first frame; checked() follows once. Until then Fetch
    // counts as working. reason() is empty, or says that Fetch doesn't work.
    void check() override {}
    // Fetch, once it is known to work
    bool supportsMatrix() const override;

    bool activate() override;
    void deactivate() override;
    void apply(const QVector3D &gain, const QMatrix3x3 &matrix) override;

    QStringList diagnostics() const override;

private:
    void updateGeometry();
    void itemRendererChanged();
    // Fetch or Blend, and the note on it
    void updateRenderer();

    QPointer<QQuickWindow> m_window;
    QPointer<ColorFilterItem> m_item;
    bool m_blendOnly;
    bool m_capabilitiesLogged;
};

#endif
