// Copyright (C) 2026 Jolla Mobile Ltd
// SPDX-License-Identifier: BSD-3-Clause

import QtQuick 2.6
import Sailfish.Silica 1.0
import Amber.Barcode 1.0

Page {
    id: page

    property string networkId
    property string networkName

    allowedOrientations: Orientation.All

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height

        Column {
            id: content

            width: parent.width
            spacing: Theme.paddingLarge

            PageHeader { title: networkName }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                text: "Anyone who scans this code can see and use the Wi-Fi password."
                color: Theme.highlightColor
                wrapMode: Text.Wrap
            }

            BarcodeImage {
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(parent.width - 2 * Theme.horizontalPageMargin, Theme.itemSizeHuge * 3)
                height: width
                format: BarcodeImage.QRCode
                sourceText: controller.qrPayload
            }
        }
    }

    Component.onCompleted: controller.loadQr(networkId)
}
