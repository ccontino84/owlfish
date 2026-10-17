// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

// qualcomm-helper: see helper.h. Run as graphics:
//   (no arguments)  the D-Bus service, started by D-Bus activation
//   --probe         checks the hardware once and prints what it found
//   --reset         turns the PCC off (after the service stops)

#include "helper.h"
#include "qdcm.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QProcess>
#include <QtDebug>

#include <cstdio>

namespace {

const double Identity[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };

bool runTool(const QStringList &arguments, QByteArray *output, QString *error)
{
    QProcess process;
    process.start(QLatin1String(Qdcm::Tool), arguments);
    if (!process.waitForStarted(2000)) {
        *error = QStringLiteral("cannot start %1: %2").arg(QLatin1String(Qdcm::Tool), process.errorString());
        return false;
    }
    if (!process.waitForFinished(3000)) {
        process.kill();
        process.waitForFinished(500);
        *error = QStringLiteral("%1 timed out").arg(QLatin1String(Qdcm::Tool));
        return false;
    }
    *output = process.readAllStandardOutput();
    const QByteArray errors = process.readAllStandardError().trimmed();
    if (!errors.isEmpty())
        qWarning() << Qdcm::Tool << "says:" << errors.left(300).constData();
    return true;
}

}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments().mid(1);

    if (arguments == QStringList(QStringLiteral("--probe"))) {
        Qdcm::Session session(runTool);
        QString error;
        const bool ok = session.probe(&error);
        session.close();
        std::printf("%s\n", ok ? "ok: QDCM PCC, read back checked" : qPrintable(QStringLiteral("not usable: ") + error));
        return ok ? 0 : 1;
    }
    if (arguments == QStringList(QStringLiteral("--reset"))) {
        Qdcm::Session session(runTool);
        QString error;
        const bool ok = session.apply(Identity, &error);
        session.close();
        if (!ok)
            qWarning() << "Cannot turn the colour matrix off:" << error;
        return ok ? 0 : 1;
    }
    if (!arguments.isEmpty()) {
        std::fprintf(stderr, "usage: %s [--probe | --reset]\n", argv[0]);
        return 2;
    }

    qDBusRegisterMetaType<QList<double>>();
    QualcommHelper helper(runTool);
    helper.setQuitAfterFailedProbe(true);
    if (!helper.registerOn(QDBusConnection::systemBus()))
        return 1;
    return app.exec();
}
