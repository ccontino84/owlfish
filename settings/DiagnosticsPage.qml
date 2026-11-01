// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

import QtQuick 2.0
import Sailfish.Silica 1.0

// For support: what the plugin reports about itself, to copy into a report.
// Opened by a double tap on the Owlfish page's header.
Page {
    id: page

    property string text

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        VerticalScrollDecorator {}

        Column {
            id: column

            width: parent.width
            spacing: Theme.paddingLarge

            PageHeader {
                //: Header of the hidden page for bug reports
                //% "Diagnostics"
                title: qsTrId("owlfish-he-diagnostics")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                text: page.text
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                //% "Copy"
                text: qsTrId("owlfish-bt-copy")
                onClicked: {
                    Clipboard.text = page.text
                    //% "Copied"
                    Notices.show(qsTrId("owlfish-la-copied"), Notice.Short, Notice.Bottom)
                }
            }
        }
    }
}
