// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef OWLFISH_QUALCOMMOUTPUT_H
#define OWLFISH_QUALCOMMOUTPUT_H

#include "filteroutput.h"
#include "qualcommclient.h"

#include <QScopedPointer>

// The display hardware on Qualcomm devices such as the Xperias: the global
// PCC of Qualcomm's display colour manager, through Owlfish's helper
// (QualcommClient). The check and the calls are answered later; applied()
// asks for a frame, which the display needs to show the change. The PCC
// works on gamma-encoded values (checked on the Xperia 10 III), so the gain
// goes in as the GPU applies it, not in linear light as pq's.
class QualcommOutput : public FilterOutput
{
    Q_OBJECT

public:
    // Takes ownership of backend; nullptr is the D-Bus one
    explicit QualcommOutput(QualcommClient::Backend *backend = nullptr, QObject *parent = nullptr);
    ~QualcommOutput() override;

    QString name() const override { return QStringLiteral("qualcomm"); }
    // The helper is installed (the owlfish-qualcomm package)
    bool applies() override;
    // Asks the helper to check the hardware, once
    void check() override;
    bool supportsMatrix() const override { return true; }

    bool activate() override;
    void deactivate() override;
    void apply(const QVector3D &gain, const QMatrix3x3 &matrix) override;

    QStringList diagnostics() const override;

    // The whole transform for the hardware: the matrix, then each output row
    // scaled by the encoded gain
    static QMatrix3x3 transform(const QMatrix3x3 &matrix, const QVector3D &gain);

private:
    void displayChecked();
    void displayFailed();

    QScopedPointer<QualcommClient> m_display;
};

#endif
