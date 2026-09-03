// Copyright (C) 2026 Jolla Mobile Ltd
// SPDX-License-Identifier: BSD-3-Clause

import QtQuick 2.6
import Sailfish.Silica 1.0

Dialog {
    id: dialog

    property string networkId
    property var networkData: ({})
    property bool resolvingConflict: false
    property var effectiveData: {
        if (controller.details.networkId !== networkId) return networkData
        if (resolvingConflict) return controller.details.localCandidate || networkData
        return controller.details
    }
    property bool networkActive: effectiveData.active || false

    allowedOrientations: Orientation.All
    canAccept: passwordField.errorHighlight === false
               && (!networkActive || confirmActive.checked)

    onAccepted: {
        if (resolvingConflict) {
            controller.resolveConflict(networkId, "edit", {
                passphrase: passwordField.text,
                hidden: hiddenSwitch.checked,
                autoconnect: autoConnectSwitch.checked,
                activeConfirmed: confirmActive.checked
            })
        } else {
            controller.edit(networkId, passwordField.text, hiddenSwitch.checked,
                            autoConnectSwitch.checked, confirmActive.checked)
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height

        Column {
            id: content

            width: parent.width

            DialogHeader { acceptText: resolvingConflict ? "Use result" : "Save" }
            DetailItem { label: "SSID"; value: effectiveData.displayName || "" }
            DetailItem { label: "Security"; value: effectiveData.securityFamily || "" }

            PasswordField {
                id: passwordField

                width: parent.width
                visible: effectiveData.securityFamily !== "open"
                label: "Password"
                text: effectiveData.passphrase || ""
                placeholderText: label
                errorHighlight: controller.passphraseError(effectiveData.securityFamily, text).length > 0
                description: controller.passphraseError(effectiveData.securityFamily, text)
            }

            TextSwitch {
                id: hiddenSwitch

                text: "Hidden network"
                checked: effectiveData.hidden || false
            }
            TextSwitch {
                id: autoConnectSwitch

                text: "Connect automatically"
                checked: effectiveData.autoconnect || false
            }
            TextSwitch {
                id: confirmActive

                visible: networkActive
                text: "Disconnect, apply, and reconnect"
                description: "This is the active network. Saving briefly interrupts connectivity."
            }
        }
    }

    Component.onCompleted: controller.loadDetails(networkId, true)
}
