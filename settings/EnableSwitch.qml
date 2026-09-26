// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

import QtQuick 2.0
import Sailfish.Silica 1.0
import com.jolla.settings 1.0
import Nemo.Configuration 1.0

// Top menu shortcut
SettingsToggle {
    id: root

    name: "Owlfish"
    icon.source: "image://theme/icon-m-owlfish"
    checked: !!enabledConfig.value

    onToggled: enabledConfig.value = !checked

    menu: ContextMenu {
        SettingsMenuItem {
            onClicked: root.goToSettings()
        }
    }

    ConfigurationValue {
        id: enabledConfig

        key: "/apps/owlfish/enabled"
        defaultValue: false
    }
}
