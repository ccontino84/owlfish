// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef OWLFISH_QDCM_H
#define OWLFISH_QDCM_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

// The colour matrix of the display hardware on Qualcomm devices such as the
// Xperia 10 III: the global PCC (polynomial colour correction) of Qualcomm's
// display colour manager (QDCM), set through the composer's QService
// (display.qservice, command 24) on /dev/vndbinder. QService only accepts a
// few system users (graphics among them), and libgbinder can't write the
// vendor binder's header, so the calls go through Android's own vndservice
// tool, run by a helper running as graphics.
//
// Layouts, as checked on the Xperia 10 III (research/xperia-display-color):
// every request starts with the display (0) and the payload size, then the
// payload: a header (u32 request, u32 valid, u64 session, u32 display,
// u32 enable) and, to set a matrix, u32 flags + 4 bytes of padding + 33
// doubles: per output row c r g b rr gg bb rg gb rb rgb, of which only r g b
// are used here. A reply starts with the status (0 = done).
namespace Qdcm {

const char *const Tool = "/vendor/bin/vndservice";

// Arguments for the tool
QStringList initArguments();
QStringList deinitArguments(quint64 session);
QStringList getPccArguments(quint64 session);
// Turns the PCC off: no change to the colours
QStringList disableArguments(quint64 session);
// matrix: rows are the output R, G, B; columns the input R, G, B
QStringList setPccArguments(quint64 session, const double matrix[9]);

// The 32-bit words of the tool's "Result: Parcel(...)" output; empty if
// there is none
QVector<quint32> parcelWords(const QByteArray &output);
// The reply's status: 0 = done; -1 without a reply
qint64 status(const QVector<quint32> &words);
// The session handle from an init reply
bool parseInit(const QVector<quint32> &words, quint64 *session);
// The matrix in a get reply; false if the reply isn't a stored PCC
bool parsePcc(const QVector<quint32> &words, double matrix[9], bool *enabled);

bool isIdentity(const double matrix[9]);
// What the helper accepts: finite values in [-2, 2], and never a black
// screen (the luminance of white at least MinimumWhite)
bool isAcceptable(const double matrix[9], QString *why);
const double MinimumWhite = 0.005;

// One QDCM session over the tool. Run: the tool's arguments to its output;
// false with *error if it couldn't run.
class Session
{
public:
    typedef std::function<bool(const QStringList &arguments, QByteArray *output, QString *error)> Run;

    explicit Session(const Run &run);

    // Sets the identity, reads it back to check the layout, then turns the
    // PCC off. False with *error if anything differs from what is expected.
    bool probe(QString *error);
    // The identity turns the PCC off. Opens a session if needed; once
    // more if the composer forgot it (it restarts with the compositor).
    bool apply(const double matrix[9], QString *error);
    // Ends the session, if there is one
    void close();
    bool isOpen() const { return m_open; }

private:
    bool open(QString *error);
    bool call(const QStringList &arguments, QVector<quint32> *words, QString *error);
    bool applyOnce(const double matrix[9], QString *error);

    Run m_run;
    bool m_open;
    quint64 m_session;
};

}

#endif
