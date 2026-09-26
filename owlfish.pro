# SPDX-FileCopyrightText: 2026 ccontino84
# SPDX-License-Identifier: LGPL-2.1-only

TEMPLATE = subdirs
SUBDIRS = src

# Host-side tests; the RPM build passes CONFIG+=notests
!notests {
    SUBDIRS += tests
    tests.depends = src
}

# Settings app page and top menu shortcut (jolla-settings)
settings_entries.files = settings/owlfish.json
settings_entries.path = /usr/share/jolla-settings/entries
settings_pages.files = settings/OwlfishPage.qml settings/EnableSwitch.qml
settings_pages.path = /usr/share/jolla-settings/pages/owlfish
INSTALLS += settings_entries settings_pages

# Monochrome icon for every Silica theme scale, so image://theme/icon-m-owlfish
# resolves (and gets tinted) like the stock icons. Regenerate with icons/render.sh.
for(scale, $$list(z1.0 z1.25 z1.5 z1.5-large z1.75 z2.0 z2.5)) {
    target = icon_$$replace(scale, [.-], _)
    $${target}.files = icons/$${scale}/icon-m-owlfish.png
    $${target}.path = /usr/share/themes/sailfish-default/silica/$${scale}/icons-monochrome
    INSTALLS += $${target}
}

OTHER_FILES += \
    rpm/owlfish.spec \
    config/90-owlfish.conf \
    settings/*.json \
    settings/*.qml \
    icons/*.svg \
    icons/render.sh \
    README.md \
    DEVELOPMENT.md \
    LICENSE
