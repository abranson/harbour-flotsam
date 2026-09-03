// Copyright (C) 2026 Jolla Mobile Ltd
// SPDX-License-Identifier: BSD-3-Clause

import QtQuick 2.6
import Sailfish.Silica 1.0

Page {
    id: page

    property bool switchingAccount: false
    property int selectedAccountId: controller.status.accountId || 0
    property var selectedIds: ({})

    allowedOrientations: Orientation.All

    function selectedNetworkList() {
        var result = []
        for (var i = 0; i < controller.networks.length; ++i) {
            var key = controller.networks[i].networkId
            if (selectedIds[key] !== false) result.push(key)
        }
        return result
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height + Theme.paddingLarge

        Column {
            id: content

            width: parent.width

            PageHeader { title: switchingAccount ? "Change account" : "Set up Flotsam" }

            SectionHeader { text: "Nextcloud account" }

            ComboBox {
                id: accountBox

                width: parent.width
                label: "Account"
                menu: ContextMenu {
                    Repeater {
                        model: controller.accounts
                        MenuItem {
                            text: modelData.displayName
                            onClicked: page.selectedAccountId = modelData.id
                        }
                    }
                }
                currentIndex: {
                    for (var i = 0; i < controller.accounts.length; ++i) {
                        if (controller.accounts[i].id === page.selectedAccountId) return i
                    }
                    return -1
                }
                value: currentIndex >= 0 ? controller.accounts[currentIndex].displayName : "Select account"
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                text: "Flotsam stores Wi-Fi passwords as readable plaintext JSON in " +
                      "Sailfish OS/NetworkSync in this account. Anyone with access to those files can read them."
                color: Theme.highlightColor
                wrapMode: Text.Wrap
            }

            TextSwitch {
                id: warningSwitch

                text: "I understand the plaintext-storage warning"
                checked: controller.status.plaintextWarningAcknowledged
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: controller.status.setupStage === "selection" ? "Directory ready" : "Create sync directory"
                enabled: page.selectedAccountId > 0 && warningSwitch.checked
                         && controller.status.setupStage !== "selection" && !controller.busy
                onClicked: controller.beginSetup(page.selectedAccountId, warningSwitch.checked)
            }

            SectionHeader {
                text: "Networks to synchronize"
                visible: controller.status.setupStage === "selection"
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: controller.status.setupStage === "selection"
                text: "All existing supported networks are selected. Turn one off to keep it only on this device."
                color: Theme.secondaryHighlightColor
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
            }

            Repeater {
                model: controller.status.setupStage === "selection" ? controller.networks : []
                delegate: TextSwitch {
                    width: page.width
                    text: modelData.displayName
                    description: modelData.securityFamily === "personal" ? "Personal Wi-Fi" : modelData.securityFamily
                    checked: page.selectedIds[modelData.networkId] === undefined
                             ? true : page.selectedIds[modelData.networkId]
                    onCheckedChanged: page.selectedIds[modelData.networkId] = checked
                }
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: controller.status.setupStage === "selection"
                text: "Finish setup"
                onClicked: {
                    controller.completeSetup(page.selectedNetworkList())
                    pageStack.pop()
                }
            }
        }

        VerticalScrollDecorator {}
    }
}
