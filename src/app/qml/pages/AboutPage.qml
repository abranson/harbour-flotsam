// Copyright (C) 2026 Jolla Mobile Ltd
// SPDX-License-Identifier: BSD-3-Clause

import QtQuick 2.6
import Sailfish.Silica 1.0

Page {
    allowedOrientations: Orientation.All

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height

        Column {
            id: content

            width: parent.width

            PageHeader { title: "About Flotsam" }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                text: "Flotsam synchronizes supported saved Wi-Fi settings through one existing Nextcloud account. " +
                      "Only SSID, security family, password, hidden status, and autoconnect are synchronized. " +
                      "IP, DNS, proxy, adapter, BSSID, lease, and enterprise settings stay local.\n\n" +
                      "Passwords are intentionally stored as plaintext JSON under Sailfish OS/NetworkSync.\n\n" +
                      "Licensed under BSD-3-Clause."
                color: Theme.primaryColor
                wrapMode: Text.Wrap
            }
        }
    }
}
