# SPDX-FileCopyrightText: 2026 ccontino84
# SPDX-License-Identifier: LGPL-2.1-only

# The lipstickaddons loader (lipstick-addons.md). Every add-on package ships
# a copy under its own file name with the same key; Qt uses the first.
TEMPLATE = lib
TARGET = lipstickaddons-owlfish
CONFIG += plugin c++11 hide_symbols
QT += gui

SOURCES += loader.cpp
OTHER_FILES += lipstickaddons.json

DESTDIR = $$OUT_PWD/../plugins/generic

target.path = $$[QT_INSTALL_PLUGINS]/generic

INSTALLS += target
