# SPDX-FileCopyrightText: 2026 ccontino84
# SPDX-License-Identifier: LGPL-2.1-only

INCLUDEPATH += $$PWD

# mce display state, for the schedule; the status for the settings page
QT += dbus
# libgbinder is loaded at run time (pqdisplay.cpp)
LIBS += -ldl

HEADERS += \
    $$PWD/alscalibration.h \
    $$PWD/ambientcutoff.h \
    $$PWD/colorfilteritem.h \
    $$PWD/colorfiltermaterial.h \
    $$PWD/colormatrix.h \
    $$PWD/colortemperature.h \
    $$PWD/controller.h \
    $$PWD/crashguard.h \
    $$PWD/logging.h \
    $$PWD/pqdisplay.h \
    $$PWD/schedule.h \
    $$PWD/settings.h \
    $$PWD/statusservice.h \
    $$PWD/sun.h \
    $$PWD/timezonelocation.h

SOURCES += \
    $$PWD/alscalibration.cpp \
    $$PWD/ambientcutoff.cpp \
    $$PWD/colorfilteritem.cpp \
    $$PWD/colorfiltermaterial.cpp \
    $$PWD/colormatrix.cpp \
    $$PWD/colortemperature.cpp \
    $$PWD/controller.cpp \
    $$PWD/crashguard.cpp \
    $$PWD/logging.cpp \
    $$PWD/pqdisplay.cpp \
    $$PWD/schedule.cpp \
    $$PWD/settings.cpp \
    $$PWD/statusservice.cpp \
    $$PWD/sun.cpp \
    $$PWD/timezonelocation.cpp

# The package version, for the settings page; the spec is the one source.
# It goes into a generated header, rewritten only when it changes, so that
# the files using it are rebuilt; the spec is a dependency of the Makefile, so
# qmake runs again when Version is raised. Where the spec is not next to the
# sources (OBS's tar_git moves rpm/ away), the spec passes the version as
# OWLFISH_VERSION; those builds always start clean.
SPEC_FILE = $$PWD/../rpm/owlfish.spec
exists($$SPEC_FILE) {
    SPEC_LINES = $$cat($$SPEC_FILE, lines)
    for(line, SPEC_LINES) {
        contains(line, "^Version:.*"): OWLFISH_VERSION = $$section(line, " ", -1)
    }
    QMAKE_INTERNAL_INCLUDED_FILES += $$SPEC_FILE
}
isEmpty(OWLFISH_VERSION): error("No Version in rpm/owlfish.spec, and no OWLFISH_VERSION given")
# In a subdirectory of the build directory: qmake searches the sources
# first, where an in-source build's copy would shadow an out-of-source one
VERSION_HEADER = $$OUT_PWD/generated/owlfish_version.h
VERSION_LINE = "$${LITERAL_HASH}define OWLFISH_VERSION \"$$OWLFISH_VERSION\""
VERSION_EXISTING = $$cat($$VERSION_HEADER, lines)
!equals(VERSION_EXISTING, $$VERSION_LINE) {
    !write_file($$VERSION_HEADER, VERSION_LINE): error("Cannot write $$VERSION_HEADER")
}
INCLUDEPATH += $$OUT_PWD/generated
QMAKE_CLEAN += $$VERSION_HEADER

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
