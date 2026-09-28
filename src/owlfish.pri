# SPDX-FileCopyrightText: 2026 ccontino84
# SPDX-License-Identifier: LGPL-2.1-only

INCLUDEPATH += $$PWD

# mce display state, for the schedule
QT += dbus

HEADERS += \
    $$PWD/alscalibration.h \
    $$PWD/ambientcutoff.h \
    $$PWD/colorfilteritem.h \
    $$PWD/colorfiltermaterial.h \
    $$PWD/colortemperature.h \
    $$PWD/controller.h \
    $$PWD/crashguard.h \
    $$PWD/logging.h \
    $$PWD/schedule.h \
    $$PWD/settings.h \
    $$PWD/sun.h \
    $$PWD/timezonelocation.h

SOURCES += \
    $$PWD/alscalibration.cpp \
    $$PWD/ambientcutoff.cpp \
    $$PWD/colorfilteritem.cpp \
    $$PWD/colorfiltermaterial.cpp \
    $$PWD/colortemperature.cpp \
    $$PWD/controller.cpp \
    $$PWD/crashguard.cpp \
    $$PWD/logging.cpp \
    $$PWD/schedule.cpp \
    $$PWD/settings.cpp \
    $$PWD/sun.cpp \
    $$PWD/timezonelocation.cpp

# On Sailfish OS settings come from dconf via mlite5; host builds fall back
# to environment variables (see settings.cpp)
packagesExist(mlite5) {
    CONFIG += link_pkgconfig
    PKGCONFIG += mlite5
    DEFINES += HAVE_MLITE
}

# Ambient light cut-off; host builds without QtSensors only get the logic
qtHaveModule(sensors) {
    QT += sensors
    DEFINES += HAVE_QTSENSORS
}
