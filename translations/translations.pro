# SPDX-FileCopyrightText: 2026 ccontino84
# SPDX-License-Identifier: LGPL-2.1-only

# The texts are qsTrId() ids with their English in //% comments. Each .ts
# here is compiled to the .qm of the same name in /usr/share/translations,
# named as Jolla's: owlfish_eng_en is the English, owlfish-<lang>
# its translations. The SDK has lrelease; host builds without it skip this.
TEMPLATE = aux

TS_FILES = owlfish_eng_en.ts owlfish-de.ts owlfish-es.ts owlfish-fr.ts owlfish-it.ts
LRELEASE = $$[QT_INSTALL_BINS]/lrelease
!exists($$LRELEASE): LRELEASE = $$system(command -v lrelease)

isEmpty(LRELEASE) {
    message("No lrelease: translations not built")
} else {
    qm.input = TS_FILES
    qm.output = $$OUT_PWD/${QMAKE_FILE_BASE}.qm
    qm.commands = $$LRELEASE -idbased -silent ${QMAKE_FILE_IN} -qm ${QMAKE_FILE_OUT}
    qm.CONFIG += no_link target_predeps
    QMAKE_EXTRA_COMPILERS += qm

    for(ts, TS_FILES): translations.files += $$OUT_PWD/$$replace(ts, \\.ts$, .qm)
    translations.path = /usr/share/translations
    translations.CONFIG += no_check_exist
    INSTALLS += translations
}

OTHER_FILES += $$TS_FILES
