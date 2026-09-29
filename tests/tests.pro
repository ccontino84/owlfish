# SPDX-FileCopyrightText: 2026 ccontino84
# SPDX-License-Identifier: LGPL-2.1-only

TEMPLATE = app
TARGET = tst_owlfish
CONFIG += testcase c++11
QT += testlib gui quick

include(../src/owlfish.pri)

SOURCES += tst_owlfish.cpp

DEFINES += OWLFISH_PLUGINS_DIR=\\\"$$OUT_PWD/../plugins\\\"
DEFINES += OWLFISH_UPDATE_ENV=\\\"$$PWD/../config/update-env\\\"
