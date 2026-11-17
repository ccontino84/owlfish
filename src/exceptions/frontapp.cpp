// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "frontapp.h"
#include "appmatcher.h"
#include "logging.h"

#include <QMetaMethod>
#include <QVariant>

FrontApp::FrontApp(QObject *compositor, QObject *parent)
    : QObject(parent)
    , m_compositor(compositor)
    , m_desktopDirs(AppMatcher::defaultDesktopDirs())
    , m_exceptionCount(0)
    , m_exception(false)
{
    // LipstickCompositor's, through its meta object: lipstick has no
    // headers for plugins
    if (compositor && compositor->metaObject()->indexOfSignal("topmostWindowIdChanged()") >= 0)
        connect(compositor, SIGNAL(topmostWindowIdChanged()), this, SLOT(update()));
}

void FrontApp::setExceptions(const QStringList &desktopIds)
{
    // Again after every settings change only while some aren't installed
    if (desktopIds == m_exceptions && m_exceptionCount == desktopIds.size())
        return;
    const bool changedList = desktopIds != m_exceptions;
    m_exceptions = desktopIds;
    m_keys.clear();
    m_exceptionCount = 0;
    for (const QString &id : desktopIds) {
        const QString path = AppMatcher::findDesktopFile(id, m_desktopDirs);
        if (path.isEmpty()) {
            if (changedList)
                qCInfo(lcOwlfish) << "Exception" << id << "is not installed";
            continue;
        }
        const QStringList keys = AppMatcher::desktopKeys(path);
        if (changedList)
            qCInfo(lcOwlfish) << "Exception" << id << "known as" << keys;
        if (!keys.isEmpty())
            ++m_exceptionCount;
        for (const QString &key : keys)
            m_keys.insert(key);
    }
    // The window in front may only now be one of them
    if (m_frontKeys.isEmpty())
        update();
    else
        evaluate();
}

void FrontApp::setFrontKeys(const QStringList &keys)
{
    m_frontKeys = keys;
    evaluate();
}

void FrontApp::update()
{
    QStringList keys;
    const int id = m_compositor ? m_compositor->property("topmostWindowId").toInt() : 0;
    if (id > 0) {
        QObject *window = nullptr;
        QMetaObject::invokeMethod(m_compositor, "windowForId", Qt::DirectConnection,
                                  Q_RETURN_ARG(QObject *, window), Q_ARG(int, id));
        // lipstick's own windows (dialogs) belong to no app
        if (window && !window->property("isInProcess").toBool()) {
            QObject *surface = window->property("surface").value<QObject *>();
            const QString className = surface ? surface->property("className").toString() : QString();
            keys = AppMatcher::windowKeys(window->property("processId").toLongLong(), className);
        }
    }
    if (keys != m_frontKeys)
        qCInfo(lcOwlfish) << "Front app" << id << keys;
    setFrontKeys(keys);
}

void FrontApp::evaluate()
{
    const bool exception = !m_keys.isEmpty() && AppMatcher::matches(m_frontKeys, m_keys);
    if (exception == m_exception)
        return;
    m_exception = exception;
    qCInfo(lcOwlfish) << (exception ? "Exception in front, pausing" : "No exception in front, resuming");
    emit exceptionChanged(exception);
}
