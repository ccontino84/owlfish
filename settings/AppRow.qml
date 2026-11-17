// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

import QtQuick 2.0
import Sailfish.Silica 1.0

// One app in the Exceptions lists: its icon, its name and, for an Android
// app, a line saying so. app: { id, name, icon, android }.
Item {
    id: row

    property var app
    property bool highlighted
    // A tick at the end, for a selected app
    property bool selected

    width: parent ? parent.width : 0
    height: Math.max(icon.height, texts.height)

    Image {
        id: icon

        x: Theme.horizontalPageMargin
        anchors.verticalCenter: parent.verticalCenter
        width: Theme.iconSizeMedium
        height: Theme.iconSizeMedium
        sourceSize.width: width
        sourceSize.height: height
        fillMode: Image.PreserveAspectFit
        source: row.app ? row.app.icon : ""
    }

    Column {
        id: texts

        anchors {
            left: icon.right
            leftMargin: Theme.paddingLarge
            right: tick.visible ? tick.left : parent.right
            rightMargin: Theme.horizontalPageMargin
            verticalCenter: parent.verticalCenter
        }

        Label {
            width: parent.width
            truncationMode: TruncationMode.Fade
            color: row.highlighted || row.selected ? Theme.highlightColor : Theme.primaryColor
            text: row.app ? row.app.name : ""
        }

        Label {
            visible: row.app && row.app.android
            width: parent.width
            font.pixelSize: Theme.fontSizeExtraSmall
            color: row.highlighted || row.selected ? Theme.secondaryHighlightColor : Theme.secondaryColor
            //: Under the name of an app from Android App Support
            //% "Android"
            text: qsTrId("owlfish-la-android")
        }
    }

    Image {
        id: tick

        visible: row.selected
        anchors {
            right: parent.right
            rightMargin: Theme.horizontalPageMargin
            verticalCenter: parent.verticalCenter
        }
        source: "image://theme/icon-m-acknowledge?" + Theme.highlightColor
    }
}
