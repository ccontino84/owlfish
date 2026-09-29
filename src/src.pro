# SPDX-FileCopyrightText: 2026 ccontino84
# SPDX-License-Identifier: LGPL-2.1-only

TEMPLATE = lib
TARGET = owlfish
# hide_symbols: the plugin runs inside lipstick; none of its symbols may
# interpose with the compositor's own
CONFIG += plugin c++11 hide_symbols
QT += gui quick

include(owlfish.pri)

SOURCES += plugin.cpp
OTHER_FILES += owlfish.json

# Build output mirrors the installed layout so tests can load the plugin
DESTDIR = $$OUT_PWD/../plugins/generic

target.path = $$[QT_INSTALL_PLUGINS]/generic

INSTALLS += target
