# SPDX-FileCopyrightText: 2026 ccontino84
# SPDX-License-Identifier: LGPL-2.1-only

# One folder per part; the include paths keep #include "name.h" working
# from anywhere
#   core      the controller, settings, D-Bus status, crash guard, logging
#   colour    colour maths: matrices, temperature, corrections
#   schedule  the night's times: fixed, sunset to sunrise, time zone city
#   sensor    the ambient light cut-off
#   output    where the filter is applied: the GPU, or the display hardware
#             (MediaTek's PQ service)
OWLFISH_DIRS = core colour schedule sensor output/gpu output/mediatek
INCLUDEPATH += $$PWD
for(dir, OWLFISH_DIRS): INCLUDEPATH += $$PWD/$$dir

# mce display state, for the schedule; the status for the settings page
QT += dbus
# libgbinder is loaded at run time (pqdisplay.cpp)
LIBS += -ldl

HEADERS += \
    $$PWD/core/controller.h \
    $$PWD/core/crashguard.h \
    $$PWD/core/logging.h \
    $$PWD/core/settings.h \
    $$PWD/core/statusservice.h \
    $$PWD/colour/colormatrix.h \
    $$PWD/colour/colortemperature.h \
    $$PWD/colour/correction.h \
    $$PWD/schedule/schedule.h \
    $$PWD/schedule/sun.h \
    $$PWD/schedule/timezonelocation.h \
    $$PWD/sensor/alscalibration.h \
    $$PWD/sensor/ambientcutoff.h \
    $$PWD/output/gpu/colorfilteritem.h \
    $$PWD/output/gpu/colorfiltermaterial.h \
    $$PWD/output/mediatek/pqdisplay.h

SOURCES += \
    $$PWD/core/controller.cpp \
    $$PWD/core/crashguard.cpp \
    $$PWD/core/logging.cpp \
    $$PWD/core/settings.cpp \
    $$PWD/core/statusservice.cpp \
    $$PWD/colour/colormatrix.cpp \
    $$PWD/colour/colortemperature.cpp \
    $$PWD/colour/correction.cpp \
    $$PWD/schedule/schedule.cpp \
    $$PWD/schedule/sun.cpp \
    $$PWD/schedule/timezonelocation.cpp \
    $$PWD/sensor/alscalibration.cpp \
    $$PWD/sensor/ambientcutoff.cpp \
    $$PWD/output/gpu/colorfilteritem.cpp \
    $$PWD/output/gpu/colorfiltermaterial.cpp \
    $$PWD/output/mediatek/pqdisplay.cpp

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
