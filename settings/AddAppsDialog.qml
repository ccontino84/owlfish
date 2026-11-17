// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

import QtQuick 2.0
import Sailfish.Silica 1.0
import Nemo.Configuration 1.0

// Picks apps to add to the exceptions: the launcher's apps that aren't
// exceptions yet, any number of them.
Dialog {
    id: dialog

    // { id, name, icon, android }, sorted by name
    property var apps: []
    // Entry file names already in the exceptions
    property var excluded: []
    // Entry file names picked here
    property var picked: []
    property string filter

    canAccept: picked.length > 0

    ConfigurationValue {
        id: exceptions

        key: "/apps/owlfish/exceptions"
        defaultValue: []
    }

    readonly property var shown: {
        var needle = filter.trim().toLowerCase()
        var result = []
        for (var i = 0; i < apps.length; i++) {
            if (excluded.indexOf(apps[i].id) >= 0)
                continue
            if (needle !== "" && apps[i].name.toLowerCase().indexOf(needle) < 0)
                continue
            result.push(apps[i])
        }
        return result
    }

    function toggle(id) {
        var list = picked.slice()
        var index = list.indexOf(id)
        if (index >= 0)
            list.splice(index, 1)
        else
            list.push(id)
        picked = list
    }

    onAccepted: {
        var list = []
        var value = exceptions.value
        for (var i = 0; value && i < value.length; i++)
            list.push(String(value[i]))
        for (var j = 0; j < picked.length; j++) {
            if (list.indexOf(picked[j]) < 0)
                list.push(picked[j])
        }
        exceptions.value = list
    }

    SilicaListView {
        id: listView

        anchors.fill: parent
        model: dialog.shown

        header: Column {
            width: listView.width

            DialogHeader {
                //: Adds the picked apps to the exceptions
                //% "Add"
                acceptText: qsTrId("owlfish-bt-add")
            }

            SearchField {
                width: parent.width
                //% "Search"
                placeholderText: qsTrId("owlfish-ph-search")
                onTextChanged: dialog.filter = text
            }
        }

        delegate: ListItem {
            id: item

            readonly property bool isPicked: dialog.picked.indexOf(modelData.id) >= 0

            contentHeight: Theme.itemSizeMedium
            highlighted: down || isPicked
            onClicked: dialog.toggle(modelData.id)

            AppRow {
                anchors.verticalCenter: parent.verticalCenter
                app: modelData
                highlighted: item.down
                selected: item.isPicked
            }
        }

        ViewPlaceholder {
            enabled: listView.count === 0 && dialog.filter === ""
            //: All apps are exceptions already, or the list could not be read
            //% "No apps to add"
            text: qsTrId("owlfish-la-no_apps_to_add")
        }

        VerticalScrollDecorator {}
    }
}
