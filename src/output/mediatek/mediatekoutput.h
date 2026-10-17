// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef OWLFISH_MEDIATEKOUTPUT_H
#define OWLFISH_MEDIATEKOUTPUT_H

#include "filteroutput.h"
#include "pqdisplay.h"

#include <QScopedPointer>

// The display hardware on MediaTek devices (the Jolla Phone): the colour
// matrix of the picture quality service (PqDisplay), in linear light. Its
// calls answer at once. The service keeps the matrix after the compositor
// is gone, so deactivate() must run on quit; activate() also clears what an
// earlier run left behind.
class MediaTekOutput : public FilterOutput
{
    Q_OBJECT

public:
    // Takes ownership of backend; nullptr is the libgbinder one
    MediaTekOutput(PqDisplay::Backend *backend, const QString &hwReleasePath,
                   const QString &deviceTreePath, QObject *parent = nullptr);
    ~MediaTekOutput() override;

    QString name() const override { return QStringLiteral("pq"); }
    // The device is on the verified list, or the device tree's colour
    // correction block is the known kind. Reads the device tree once;
    // nothing is loaded or called.
    bool applies() override;
    // Loads libgbinder and checks the service; only once
    void check() override;
    bool supportsMatrix() const override { return true; }

    bool activate() override;
    void deactivate() override;
    void apply(const QVector3D &gain, const QMatrix3x3 &matrix) override;

    QStringList diagnostics() const override;

private:
    // On the verified list (the reason then names the device)
    bool isVerified() const;
    void failedCall();

    QScopedPointer<PqDisplay> m_pq;
    QString m_hwReleasePath;
    QString m_deviceTreePath;
    bool m_ccorrRead;
    PqDisplay::Ccorr m_ccorr;
};

#endif
