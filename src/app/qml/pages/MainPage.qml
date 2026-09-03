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

            BackgroundItem {
                width: parent.width
                height: statusColumn.height + 2 * Theme.paddingMedium
                onClicked: if (!controller.status.setupComplete) {
                    pageStack.push(Qt.resolvedUrl("SetupPage.qml"))
                }

                Column {
                    id: statusColumn

                    anchors {
                        left: parent.left
                        right: parent.right
                        margins: Theme.horizontalPageMargin
                        verticalCenter: parent.verticalCenter
                    }
                    Label {
                        width: parent.width
                        text: controller.status.setupComplete
                              ? (controller.status.syncing ? "Synchronizing…" : "Wi-Fi sync")
                              : "Set up Nextcloud sync"
                        color: parent.parent.highlighted ? Theme.highlightColor : Theme.primaryColor
                    }
                    Label {
                        width: parent.width
                        text: controller.error || controller.status.lastError
                              || controller.status.lastResult ||
                              "Synchronize saved Wi-Fi networks through Nextcloud."
                        color: parent.parent.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                        font.pixelSize: Theme.fontSizeSmall
                        wrapMode: Text.Wrap
                    }
                }
            }
        }

        PullDownMenu {
            MenuItem {
                text: "About"
                onClicked: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
            }
            MenuItem {
                text: "Change Nextcloud account"
                onClicked: pageStack.push(Qt.resolvedUrl("SetupPage.qml"), { switchingAccount: true })
            }
            MenuItem {
                text: "Sync now"
                enabled: controller.status.setupComplete && !controller.status.syncing
                onClicked: controller.manualSync()
            }
        }

        ViewPlaceholder {
            enabled: controller.networks.length === 0
            text: controller.status.setupComplete ? "No supported saved Wi-Fi networks" : "Setup required"
            hintText: controller.status.setupComplete
                      ? "Open, WEP, and personal WPA networks appear here."
                      : "Choose an existing Nextcloud account to begin."
        }

        delegate: BackgroundItem {
            id: delegate

            width: ListView.view.width
            height: Theme.itemSizeMedium
            onClicked: {
                controller.loadDetails(modelData.networkId, false)
                pageStack.push(Qt.resolvedUrl("NetworkPage.qml"), {
                    networkId: modelData.networkId,
                    initialData: modelData
                })
            }

            Column {
                anchors {
                    left: parent.left
                    right: parent.right
                    margins: Theme.horizontalPageMargin
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
