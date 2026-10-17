# SPDX-FileCopyrightText: 2026 ccontino84
# SPDX-License-Identifier: LGPL-2.1-only

# The display helper for Qualcomm devices such as the Xperias: a system
# service running as graphics, see helper.h. Packaged on its own
# (owlfish-qualcomm): installing it enables it.
TEMPLATE = app
TARGET = qualcomm-helper
CONFIG += c++11 console
CONFIG -= app_bundle
QT = core dbus

HEADERS += helper.h qdcm.h
SOURCES += main.cpp helper.cpp qdcm.cpp

target.path = /usr/libexec/owlfish

unit.files = system/owlfish-qualcomm.service
unit.path = /usr/lib/systemd/system
activation.files = system/io.github.ccontino84.owlfish.Qualcomm.service
activation.path = /usr/share/dbus-1/system-services
policy.files = system/io.github.ccontino84.owlfish.Qualcomm.conf
policy.path = /etc/dbus-1/system.d

INSTALLS += target unit activation policy
