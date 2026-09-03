// Copyright (C) 2026 Jolla Mobile Ltd
// SPDX-License-Identifier: BSD-3-Clause

import QtQuick 2.6
import Sailfish.Silica 1.0

Page {
    id: page

    property string networkId
    property var initialData: ({})
    property var network: controller.details.networkId === networkId ? controller.details : initialData
    property var localCandidate: network.localCandidate || ({})
    property var remoteCandidate: network.remoteCandidate || ({})
    property bool revealPassword: false

    allowedOrientations: Orientation.All

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height

        PullDownMenu {
            MenuItem {
                text: network.blocked ? "Synchronize this network" : "Keep only on this device"
                onClicked: network.blocked ? controller.unblock(networkId) : controller.block(networkId)
            }
            MenuItem {
                text: "Show Wi-Fi QR code"
                enabled: network.qrAvailable && !network.conflict
                onClicked: pageStack.push(Qt.resolvedUrl("QrPage.qml"), {
                    networkId: networkId,
                    networkName: network.displayName
                })
            }
            MenuItem {
                text: "Edit"
                enabled: network.presentLocally !== false && !network.conflict
                onClicked: pageStack.push(Qt.resolvedUrl("EditPage.qml"), {
                    networkId: networkId,
                    networkData: network
                })
            }
        }

        Column {
            id: content

            width: parent.width

            PageHeader { title: network.displayName || "Wi-Fi network" }

            DetailItem { label: "Status"; value: network.category || "" }
            DetailItem { label: "Security"; value: network.securityFamily || "" }
            DetailItem { label: "Hidden"; value: network.hidden ? "Yes" : "No" }
            DetailItem { label: "Connect automatically"; value: network.autoconnect ? "Yes" : "No" }

            TextSwitch {
                visible: network.hasPassphrase || localCandidate.hasPassphrase
                         || remoteCandidate.hasPassphrase
                text: "Reveal password"
                description: network.conflict
                             ? "Reveal passwords in the comparison below"
                             : (revealPassword && network.passphrase
                                ? network.passphrase : "Password concealed")
                checked: revealPassword
                onCheckedChanged: {
                    revealPassword = checked
                    controller.loadDetails(networkId, checked)
                }
            }

            SectionHeader { text: "Conflict"; visible: network.conflict }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: network.conflict
                text: "This device and Nextcloud both changed. Passwords stay concealed until you reveal them."
                color: Theme.highlightColor
                wrapMode: Text.Wrap
            }
            DetailItem {
                visible: network.conflict
                label: "This device · password"
                value: revealPassword
                       ? (localCandidate.hasPassphrase ? localCandidate.passphrase : "None")
                       : (localCandidate.hasPassphrase ? "Concealed" : "None")
            }
            DetailItem {
                visible: network.conflict
                label: "Nextcloud · password"
                value: revealPassword
                       ? (remoteCandidate.hasPassphrase ? remoteCandidate.passphrase : "None")
                       : (remoteCandidate.hasPassphrase ? "Concealed" : "None")
            }
            DetailItem {
                visible: network.conflict
                label: "This device · hidden / autoconnect"
                value: (localCandidate.hidden ? "Yes" : "No") + " / "
                       + (localCandidate.autoconnect ? "Yes" : "No")
            }
            DetailItem {
                visible: network.conflict
                label: "Nextcloud · hidden / autoconnect"
                value: (remoteCandidate.hidden ? "Yes" : "No") + " / "
                       + (remoteCandidate.autoconnect ? "Yes" : "No")
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: network.conflict
                text: "Use this device"
                onClicked: controller.resolveConflict(networkId, "device", {})
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: network.conflict
                text: "Use Nextcloud"
                onClicked: controller.resolveConflict(networkId, "nextcloud", {})
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: network.conflict
                text: "Edit result"
                onClicked: pageStack.push(Qt.resolvedUrl("EditPage.qml"), {
                    networkId: networkId,
                    networkData: network.localCandidate,
                    resolvingConflict: true,
                    networkActive: network.active
                })
            }

            SectionHeader { text: "Forgotten network"; visible: network.forgotten }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: network.forgotten
                text: "Restore on this device"
                onClicked: controller.resolveForgotten(networkId, "restore")
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: network.forgotten
                text: "Keep off this device"
                onClicked: controller.resolveForgotten(networkId, "keep-off")
            }

            Button {
                id: forgetButton

                anchors.horizontalCenter: parent.horizontalCenter
                text: "Forget everywhere"
                color: Theme.errorColor
                onClicked: remorse.execute(forgetButton, "Forgetting everywhere", function() {
                    controller.forgetEverywhere(networkId)
                    pageStack.pop()
                })
            }
        }

        VerticalScrollDecorator {}
    }

    RemorsePopup { id: remorse }

    Component.onCompleted: controller.loadDetails(networkId, false)
}
