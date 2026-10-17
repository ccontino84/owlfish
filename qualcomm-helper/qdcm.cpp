// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "qdcm.h"

#include <QRegularExpression>

#include <cmath>
#include <cstring>

namespace Qdcm {

namespace {

const char *const Service = "display.qservice";
const char *const Command = "24";

// Requests
const int Init = 0;
const int Deinit = 1;
const int GetPcc = 102;
const int SetPcc = 103;

// The set payload: header 24 + flags 4 + padding 4 + 33 doubles
const int SetPccSize = 296;
const int HeaderSize = 24;
// Header (16) + display
const int SessionRequestSize = 16;
// A stored PCC in a get reply: 8 bytes of reply header + 0x118
const quint32 StoredPccSize = 0x118;
const int StoredPccWords = 72;
// Where the first row's c coefficient is, in bytes from the reply's start;
// a row is 11 doubles
const int FirstRowOffset = 0x18;
const int RowBytes = 11 * 8;

const double Luminance[3] = { 0.2126, 0.7152, 0.0722 };

QStringList start(int size)
{
    return QStringList() << QStringLiteral("call") << QLatin1String(Service) << QLatin1String(Command)
                         << QStringLiteral("i32") << QStringLiteral("0")
                         << QStringLiteral("i32") << QString::number(size);
}

QStringList &i32(QStringList &arguments, qint64 value)
{
    arguments << QStringLiteral("i32") << QString::number(value);
    return arguments;
}

QStringList &i64(QStringList &arguments, quint64 value)
{
    arguments << QStringLiteral("i64") << QString::number(value);
    return arguments;
}

QStringList &d(QStringList &arguments, double value)
{
    // QString::number() ignores the locale: always a decimal point
    arguments << QStringLiteral("d") << QString::number(value, 'g', 17);
    return arguments;
}

double doubleAt(const QVector<quint32> &words, int byteOffset)
{
    const quint64 bits = quint64(words.at(byteOffset / 4)) | (quint64(words.at(byteOffset / 4 + 1)) << 32);
    double value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

const double Identity[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };

}

QStringList initArguments()
{
    QStringList arguments = start(8);
    i32(arguments, Init);
    return i32(arguments, 0);
}

QStringList deinitArguments(quint64 session)
{
    QStringList arguments = start(SessionRequestSize);
    i32(arguments, Deinit);
    i64(arguments, session);
    return i32(arguments, 0);
}

QStringList getPccArguments(quint64 session)
{
    QStringList arguments = start(SessionRequestSize);
    i32(arguments, GetPcc);
    i64(arguments, session);
    return i32(arguments, 0);
}

QStringList disableArguments(quint64 session)
{
    // valid 0: no matrix follows; enable 0
    QStringList arguments = start(HeaderSize);
    i32(arguments, SetPcc);
    i32(arguments, 0);
    i64(arguments, session);
    i32(arguments, 0);
    return i32(arguments, 0);
}

QStringList setPccArguments(quint64 session, const double matrix[9])
{
    QStringList arguments = start(SetPccSize);
    i32(arguments, SetPcc);
    // valid 1: a matrix follows
    i32(arguments, 1);
    i64(arguments, session);
    // display 0, enable 1, flags 0, padding
    i32(arguments, 0);
    i32(arguments, 1);
    i32(arguments, 0);
    i32(arguments, 0);
    for (int row = 0; row < 3; ++row) {
        // c, then r g b, then the second-order terms
        d(arguments, 0);
        for (int column = 0; column < 3; ++column)
            d(arguments, matrix[row * 3 + column]);
        for (int i = 0; i < 7; ++i)
            d(arguments, 0);
    }
    return arguments;
}

QVector<quint32> parcelWords(const QByteArray &output)
{
    QVector<quint32> words;
    const int start = output.indexOf("Parcel(");
    if (start < 0)
        return words;
    const QString text = QString::fromLatin1(output.mid(start + 7));
    // e.g. "Parcel(Error: ...)"
    if (!text.isEmpty() && !text.at(0).isSpace() && !text.at(0).isDigit()
            && !(text.at(0) >= QLatin1Char('a') && text.at(0) <= QLatin1Char('f')))
        return words;
    static const QRegularExpression offset(QStringLiteral("0x[0-9a-fA-F]+:"));
    static const QRegularExpression word(QStringLiteral("\\b[0-9a-fA-F]{8}\\b"));
    for (QString line : text.split(QLatin1Char('\n'))) {
        // The ASCII column, from the first quote on
        const int quote = line.indexOf(QLatin1Char('\''));
        if (quote >= 0)
            line.truncate(quote);
        line.remove(offset);
        QRegularExpressionMatchIterator matches = word.globalMatch(line);
        while (matches.hasNext())
            words << matches.next().captured(0).toUInt(nullptr, 16);
    }
    return words;
}

qint64 status(const QVector<quint32> &words)
{
    return words.isEmpty() ? -1 : qint64(words.first());
}

bool parseInit(const QVector<quint32> &words, quint64 *session)
{
    if (words.size() < 4 || words.at(0) != 0 || words.at(1) != 8)
        return false;
    *session = quint64(words.at(2)) | (quint64(words.at(3)) << 32);
    return true;
}

bool parsePcc(const QVector<quint32> &words, double matrix[9], bool *enabled)
{
    if (words.size() < StoredPccWords || words.at(0) != 0 || words.at(1) != StoredPccSize)
        return false;
    *enabled = words.at(3) != 0;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column)
            matrix[row * 3 + column] = doubleAt(words, FirstRowOffset + row * RowBytes + (column + 1) * 8);
    }
    return true;
}

bool isIdentity(const double matrix[9])
{
    for (int i = 0; i < 9; ++i) {
        if (std::fabs(matrix[i] - Identity[i]) > 1e-6)
            return false;
    }
    return true;
}

bool isAcceptable(const double matrix[9], QString *why)
{
    double white = 0;
    for (int row = 0; row < 3; ++row) {
        double sum = 0;
        for (int column = 0; column < 3; ++column) {
            const double value = matrix[row * 3 + column];
            if (!std::isfinite(value) || std::fabs(value) > 2) {
                *why = QStringLiteral("value out of range");
                return false;
            }
            sum += value;
        }
        white += Luminance[row] * sum;
    }
    if (white < MinimumWhite) {
        *why = QStringLiteral("white would be black");
        return false;
    }
    return true;
}

Session::Session(const Run &run)
    : m_run(run)
    , m_open(false)
    , m_session(0)
{
}

bool Session::call(const QStringList &arguments, QVector<quint32> *words, QString *error)
{
    QByteArray output;
    if (!m_run(arguments, &output, error))
        return false;
    *words = parcelWords(output);
    if (words->isEmpty()) {
        *error = QStringLiteral("no reply: %1").arg(QString::fromLatin1(output.trimmed().left(200)));
        return false;
    }
    return true;
}

bool Session::open(QString *error)
{
    if (m_open)
        return true;
    QVector<quint32> words;
    if (!call(initArguments(), &words, error))
        return false;
    if (!parseInit(words, &m_session)) {
        *error = QStringLiteral("no colour manager (init status %1)").arg(status(words));
        return false;
    }
    m_open = true;
    return true;
}

void Session::close()
{
    if (!m_open)
        return;
    m_open = false;
    QVector<quint32> words;
    QString error;
    call(deinitArguments(m_session), &words, &error);
}

bool Session::applyOnce(const double matrix[9], QString *error)
{
    QVector<quint32> words;
    const QStringList arguments = isIdentity(matrix) ? disableArguments(m_session)
                                                     : setPccArguments(m_session, matrix);
    if (!call(arguments, &words, error))
        return false;
    if (status(words) != 0) {
        *error = QStringLiteral("set status %1").arg(status(words));
        return false;
    }
    return true;
}

bool Session::apply(const double matrix[9], QString *error)
{
    const bool wasOpen = m_open;
    if (!open(error))
        return false;
    if (applyOnce(matrix, error))
        return true;
    if (!wasOpen)
        return false;
    // The composer restarted since: its sessions are gone
    m_open = false;
    return open(error) && applyOnce(matrix, error);
}

bool Session::probe(QString *error)
{
    if (!open(error))
        return false;
    QVector<quint32> words;
    if (!call(setPccArguments(m_session, Identity), &words, error))
        return false;
    if (status(words) != 0) {
        // Perhaps a session from before a composer restart: once more
        m_open = false;
        if (!open(error) || !call(setPccArguments(m_session, Identity), &words, error))
            return false;
        if (status(words) != 0) {
            *error = QStringLiteral("set status %1").arg(status(words));
            return false;
        }
    }

    bool ok = call(getPccArguments(m_session), &words, error);
    double stored[9];
    bool enabled = false;
    if (ok && !(parsePcc(words, stored, &enabled) && enabled && isIdentity(stored))) {
        *error = QStringLiteral("read back differs (status %1, %2 words)").arg(status(words)).arg(words.size());
        ok = false;
    }

    QString disableError;
    if (!call(disableArguments(m_session), &words, &disableError) || status(words) != 0) {
        if (ok)
            *error = disableError.isEmpty() ? QStringLiteral("disable status %1").arg(status(words)) : disableError;
        return false;
    }
    return ok;
}

}
