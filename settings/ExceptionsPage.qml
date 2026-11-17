// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

import QtQuick 2.0
import Sailfish.Silica 1.0
import Nemo.Configuration 1.0

// The exceptions: apps in front of which Owlfish fades out. Opened from the
// Owlfish page, which has the launcher's apps (owlfishPage.launcherApps).
Page {
    id: page

    property Item owlfishPage

    ConfigurationValue {
        id: exceptions

        key: "/apps/owlfish/exceptions"
        defaultValue: []
    }

    function ids() {
        var list = []
        var value = exceptions.value
        for (var i = 0; value && i < value.length; i++)
            list.push(String(value[i]))
        return list
    }

    // The chosen apps that are installed, sorted as the launcher
    readonly property var chosen: {
        var list = ids()
        var apps = owlfishPage ? owlfishPage.launcherApps : []
        var result = []
        for (var i = 0; i < apps.length; i++) {
            if (list.indexOf(apps[i].id) >= 0)
                result.push(apps[i])
        }
        return result
    }

    function remove(id) {
        exceptions.value = ids().filter(function(other) { return other !== id })
    }

    SilicaListView {
        id: listView

        anchors.fill: parent
        model: page.chosen

        PullDownMenu {
            MenuItem {
                //% "Add apps"
                text: qsTrId("owlfish-me-add_apps")
                onClicked: pageStack.push(Qt.resolvedUrl("AddAppsDialog.qml"),
                                          { apps: owlfishPage ? owlfishPage.launcherApps : [],
                                            excluded: page.ids() })
            }
        }

        header: Column {
            width: listView.width

            PageHeader {
                //% "Exceptions"
                title: qsTrId("owlfish-he-exceptions")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
                //% "Owlfish fades out while one of these apps is in front, and back in when you leave it."
                text: qsTrId("owlfish-la-exceptions_description")
            }

            Item {
                width: 1
                height: Theme.paddingLarge
            }
        }

        delegate: ListItem {
            id: item

            contentHeight: Theme.itemSizeMedium
            menu: ContextMenu {
                MenuItem {
                    //% "Remove"
                    text: qsTrId("owlfish-me-remove")
                    onClicked: {
                        var id = modelData.id
                        //: Countdown before an app leaves the exceptions
                        //% "Removing"
                        item.remorseAction(qsTrId("owlfish-la-removing"), function() { page.remove(id) })
                    }
                }
            }

            AppRow {
                anchors.verticalCenter: parent.verticalCenter
                app: modelData
                highlighted: item.highlighted
            }
        }

        ViewPlaceholder {
            enabled: listView.count === 0
            //% "No apps"
            text: qsTrId("owlfish-la-no_exceptions")
            //% "Pull down to add apps"
            hintText: qsTrId("owlfish-la-pull_to_add")
        }

        VerticalScrollDecorator {}
    }
}
