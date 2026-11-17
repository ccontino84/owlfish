// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "appmatcher.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace {

// Linux cuts a process's comm to this many characters
const int CommLength = 15;

bool isWrapper(const QString &name)
{
    static const QSet<QString> wrappers = {
        QStringLiteral("invoker"), QStringLiteral("sailjail"), QStringLiteral("firejail"),
        QStringLiteral("env"), QStringLiteral("nice"),
    };
    return wrappers.contains(name);
}

// Options of the wrappers above that take the next word as their value
bool takesValue(const QString &option)
{
    static const QSet<QString> options = {
        QStringLiteral("-t"), QStringLiteral("--type"), QStringLiteral("-d"), QStringLiteral("--delay"),
        QStringLiteral("-p"), QStringLiteral("--profile"), QStringLiteral("-r"), QStringLiteral("--respawn"),
        QStringLiteral("-a"), QStringLiteral("--app"), QStringLiteral("-n"),
    };
    return options.contains(option);
}

// Programs that run a script; the script names the app
bool isInterpreter(const QString &name)
{
    static const QSet<QString> interpreters = {
        QStringLiteral("sailfish-qml"), QStringLiteral("qmlscene"), QStringLiteral("python"),
        QStringLiteral("python3"), QStringLiteral("sh"), QStringLiteral("bash"), QStringLiteral("perl"),
    };
    return interpreters.contains(name) || name.startsWith(QLatin1String("python3."));
}

QString baseName(const QString &path)
{
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? path : path.mid(slash + 1);
}

QString withoutExtension(const QString &name)
{
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    return dot > 0 ? name.left(dot) : name;
}

// An Exec line's words; double quotes group, a backslash escapes
QStringList splitExec(const QString &exec)
{
    QStringList words;
    QString word;
    bool quoted = false;
    bool inWord = false;
    for (int i = 0; i < exec.size(); ++i) {
        const QChar c = exec.at(i);
        if (c == QLatin1Char('\\') && i + 1 < exec.size()) {
            word += exec.at(++i);
            inWord = true;
        } else if (c == QLatin1Char('"')) {
            quoted = !quoted;
            inWord = true;
        } else if (c.isSpace() && !quoted) {
            if (inWord)
                words << word;
            word.clear();
            inWord = false;
        } else {
            word += c;
            inWord = true;
        }
    }
    if (inWord)
        words << word;
    return words;
}

// The first word that is neither an option nor a value (from index from on)
QString firstArgument(const QStringList &words, int from)
{
    for (int i = from; i < words.size(); ++i) {
        const QString &word = words.at(i);
        if (word.startsWith(QLatin1Char('-')) || word.startsWith(QLatin1Char('%')))
            continue;
        return word;
    }
    return QString();
}

void addKey(QStringList *keys, const QString &key)
{
    if (!key.isEmpty() && !keys->contains(key))
        keys->append(key);
}

}

namespace AppMatcher {

QStringList defaultDesktopDirs()
{
    return QStringList()
            << QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
               + QStringLiteral("/applications")
            << QStringLiteral("/usr/share/applications");
}

QString findDesktopFile(const QString &id, const QStringList &dirs)
{
    // A file name, never a path
    if (id.isEmpty() || id.contains(QLatin1Char('/')) || id.startsWith(QLatin1Char('.')))
        return QString();
    for (const QString &dir : dirs) {
        const QString path = dir + QLatin1Char('/') + id;
        if (QFileInfo(path).isFile())
            return path;
    }
    return QString();
}

QString execProgram(const QString &exec)
{
    const QStringList words = splitExec(exec);
    for (int i = 0; i < words.size(); ++i) {
        const QString &word = words.at(i);
        if (word.startsWith(QLatin1Char('-'))) {
            if (takesValue(word))
                ++i;
            continue;
        }
        // Field codes, environment assignments, numbers (a delay) and the
        // launcher entry sailjail is given
        if (word.startsWith(QLatin1Char('%'))
                || (word.contains(QLatin1Char('=')) && !word.startsWith(QLatin1Char('/')))
                || word.endsWith(QLatin1String(".desktop")))
            continue;
        bool number = false;
        word.toInt(&number);
        if (number)
            continue;
        const QString name = baseName(word);
        if (isWrapper(name))
            continue;
        if (isInterpreter(name)) {
            const QString script = firstArgument(words, i + 1);
            return script.isEmpty() ? name : withoutExtension(baseName(script));
        }
        return name;
    }
    return QString();
}

QStringList desktopKeys(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringList();

    QString exec;
    QString package;
    bool inEntry = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inEntry = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        if (!inEntry)
            continue;
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0)
            continue;
        const QString key = line.left(equals).trimmed();
        const QString value = line.mid(equals + 1).trimmed();
        if (key == QLatin1String("Exec"))
            exec = value;
        else if (key == QLatin1String("X-apkd-packageName"))
            package = value;
    }

    QStringList keys;
    if (!package.isEmpty()) {
        addKey(&keys, package);
        return keys;
    }
    addKey(&keys, execProgram(exec));
    addKey(&keys, withoutExtension(baseName(path)));
    return keys;
}

QStringList windowKeys(qint64 pid, const QString &className, const QString &procRoot)
{
    QStringList keys;
    addKey(&keys, className);
    if (pid <= 0)
        return keys;

    const QString dir = procRoot + QLatin1Char('/') + QString::number(pid);
    QString interpreter;

    const QString exe = baseName(QFileInfo(dir + QStringLiteral("/exe")).symLinkTarget());
    // Boosted apps run in a copy of the booster (mapplauncherd)
    if (!exe.isEmpty() && !exe.startsWith(QLatin1String("booster"))) {
        addKey(&keys, exe);
        if (isInterpreter(exe))
            interpreter = exe;
    }

    QFile cmdline(dir + QStringLiteral("/cmdline"));
    if (cmdline.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> parts = cmdline.readAll().split('\0');
        QStringList args;
        for (const QByteArray &part : parts)
            args << QString::fromLocal8Bit(part);
        if (!args.isEmpty()) {
            const QString first = baseName(args.first());
            addKey(&keys, first);
            if (isInterpreter(first) || !interpreter.isEmpty()) {
                const QString script = firstArgument(args, 1);
                addKey(&keys, withoutExtension(baseName(script)));
            }
        }
    }

    QFile comm(dir + QStringLiteral("/comm"));
    if (comm.open(QIODevice::ReadOnly))
        addKey(&keys, QString::fromLocal8Bit(comm.readAll()).trimmed());

    return keys;
}

bool matches(const QStringList &windowKeys, const QSet<QString> &keys)
{
    for (const QString &name : windowKeys) {
        if (keys.contains(name))
            return true;
        if (name.size() != CommLength)
            continue;
        for (const QString &key : keys) {
            if (key.startsWith(name))
                return true;
        }
    }
    return false;
}

}
