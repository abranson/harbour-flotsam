// Copyright (C) 2026 Jolla Mobile Ltd
// SPDX-License-Identifier: BSD-3-Clause

import QtQuick 2.6
import Sailfish.Silica 1.0

Page {
    id: page

    allowedOrientations: Orientation.All

    SilicaListView {
        anchors.fill: parent
        model: controller.networks
        section.property: "category"
        section.criteria: ViewSection.FullString
        section.delegate: SectionHeader { text: section }
        header: Column {
            width: page.width

            PageHeader { title: "Flotsam" }

            SectionHeader { text: "Status" }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                text: controller.status.setupComplete
                      ? (controller.status.syncing ? "Synchronizing…" : "Up to date")
                      : "Nextcloud sync is not set up"
                color: Theme.primaryColor
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                text: controller.error || controller.status.lastError
                      || controller.status.lastResult
                      || "Saved Wi-Fi networks synchronize through Nextcloud."
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.Wrap
            }

            SectionHeader { text: "Networks" }
        }

        PullDownMenu {
            MenuItem {
                text: "About"
                onClicked: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
            }
            MenuItem {
                text: controller.status.setupComplete ? "Change Nextcloud account" : "Set up Nextcloud sync"
                onClicked: pageStack.push(Qt.resolvedUrl("SetupPage.qml"), {
                    switchingAccount: controller.status.setupComplete
                })
            }
            MenuItem {
                text: "Sync now"
                enabled: controller.status.setupComplete && !controller.status.syncing
                onClicked: controller.manualSync()
            }
            MenuItem {
                text: "Scan Wi-Fi QR code"
                onClicked: pageStack.push(Qt.resolvedUrl("ScanPage.qml"))
            }
        }

        ViewPlaceholder {
            enabled: controller.networks.length === 0
            text: controller.status.setupComplete ? "No supported saved Wi-Fi networks" : "Setup required"
            hintText: controller.status.setupComplete
                      ? "Open, WEP, and personal WPA networks appear here."
                      : "Pull down to choose an existing Nextcloud account."
        }

        delegate: BackgroundItem {
            id: delegate

            property color networkColor: modelData.category === "Synced"
                                         ? "#43a047"
                                         : modelData.category === "Pending"
                                           ? "#e0a800" : "#d64b4b"

            width: ListView.view.width
            height: Theme.itemSizeMedium
            onClicked: {
                controller.loadDetails(modelData.networkId, false)
                pageStack.push(Qt.resolvedUrl("NetworkPage.qml"), {
                    networkId: modelData.networkId,
                    initialData: modelData
                })
            }

            Icon {
                id: wlanIcon

                anchors {
                    left: parent.left
                    leftMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                source: "image://theme/icon-m-wlan?" + delegate.networkColor
            }

            Column {
                anchors {
                    left: wlanIcon.right
                    right: parent.right
                    leftMargin: Theme.paddingMedium
                    rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                Label {
                    width: parent.width
                    text: modelData.displayName
                    color: delegate.highlighted ? Theme.highlightColor : Theme.primaryColor
                    truncationMode: TruncationMode.Fade
                }
                Label {
                    width: parent.width
                    text: modelData.reason || (modelData.securityFamily === "open" ? "Open" :
                          modelData.securityFamily === "wep" ? "WEP" : "WPA/WPA2/WPA3 personal")
                    color: delegate.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeExtraSmall
                    truncationMode: TruncationMode.Fade
                }
            }
        }

        VerticalScrollDecorator {}
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: controller.busy && controller.networks.length === 0
        size: BusyIndicatorSize.Large
    }

    Component.onCompleted: controller.refresh()
}
