// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef OWLFISH_FILTEROUTPUT_H
#define OWLFISH_FILTEROUTPUT_H

#include <QGenericMatrix>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector3D>

class OwlfishSettings;

// Where the filter is applied: the GPU (GpuOutput) or the display hardware
// (MediaTekOutput, QualcommOutput). The controller fades the filter's gain
// and matrix and hands every step to the active output; each output turns
// them into what it applies (fixed point in linear light, encoded doubles,
// a shader's uniforms).
class FilterOutput : public QObject
{
    Q_OBJECT

public:
    enum State {
        NotChecked,
        // Answered later: checked() follows
        Checking,
        Ready,
        // reason() says why
        Unavailable
    };

    explicit FilterOutput(QObject *parent = nullptr);

    // The renderer's name, for the diagnostics and the settings page
    virtual QString name() const = 0;
    // The renderer key's values that force this output
    virtual bool isNamed(const QString &key) const { return key == name(); }
    // Reads the settings that concern only this output; before every choice
    virtual void configure(const OwlfishSettings *settings) { Q_UNUSED(settings) }
    // The device has this kind of output, as far as can be told without
    // calling anything
    virtual bool applies() = 0;
    // Checks that it works, once. The state is Ready or Unavailable on
    // return, or Checking until checked().
    virtual void check() = 0;
    State state() const { return m_state; }
    // Why it can or can't be used; for the GPU, a note on how it draws
    QString reason() const { return m_reason; }
    // Can apply the matrix (the correction and the saturation)
    virtual bool supportsMatrix() const = 0;

    // Takes the filter over; false, with reason(), if it can't
    virtual bool activate() = 0;
    // Leaves the display as it is without Owlfish. Only does something
    // while active.
    virtual void deactivate() = 0;
    bool isActive() const { return m_active; }
    // The filter: the matrix, then the gain, gamma-encoded as the colour
    // temperature and the dimming give it. At every fade step.
    virtual void apply(const QVector3D &gain, const QMatrix3x3 &matrix) = 0;

    // Lines for the diagnostics
    virtual QStringList diagnostics() const = 0;

signals:
    void checked();
    // The output has the new filter; the display shows it with the next
    // frame
    void applied();
    // A call failed: unusable for the rest of the run, see reason()
    void failed();

protected:
    void setState(State state, const QString &reason);
    void setActive(bool active) { m_active = active; }

private:
    State m_state;
    QString m_reason;
    bool m_active;
};

#endif
