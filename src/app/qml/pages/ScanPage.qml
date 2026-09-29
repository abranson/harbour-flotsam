// Copyright (C) 2026 Jolla Mobile Ltd
// SPDX-License-Identifier: BSD-3-Clause

import QtQuick 2.6
import QtMultimedia 5.6
import Sailfish.Silica 1.0
import Amber.QrFilter 1.0

Page {
    id: page

    property string candidatePayload
    property var candidate
    property string scanError
    property bool importing
    readonly property bool scanning: status === PageStatus.Active
                                     && Qt.application.active
                                     && candidatePayload.length === 0
                                     && !importing

    allowedOrientations: Orientation.All

    function updateCamera() {
        if (scanning) {
            camera.start()
            camera.unlock()
        } else {
            camera.stop()
        }
    }

    function scanAgain() {
        candidatePayload = ""
        candidate = undefined
        scanError = ""
        qrFilter.clearResult()
    }

    onScanningChanged: updateCamera()
    Component.onCompleted: updateCamera()
    Component.onDestruction: camera.stop()

    Connections {
        target: controller
        onImportFinished: {
            page.importing = false
            if (success) {
                pageStack.pop()
            } else {
                page.scanError = message
            }
        }
    }

    Timer {
        id: retryTimer

        interval: 1500
        onTriggered: {
            qrFilter.clearResult()
            page.scanError = ""
        }
    }

    Camera {
        id: camera

        position: Camera.BackFace
        captureMode: Camera.CaptureStillImage
        flash.mode: Camera.FlashOff
    }

    QrFilter {
        id: qrFilter

        active: page.scanning
        onResultChanged: {
            if (!page.scanning || result.length === 0) {
                return
            }
            var parsed = controller.parseWifiQr(result)
            if (parsed.valid) {
                page.candidate = parsed
                page.candidatePayload = result
            } else {
                page.scanError = parsed.error
                retryTimer.restart()
            }
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height

        PullDownMenu {
            MenuItem {
                text: "Scan again"
                visible: page.candidatePayload.length > 0
                enabled: !page.importing
                onClicked: page.scanAgain()
            }
        }

        Column {
            id: content

            width: parent.width
            spacing: Theme.paddingLarge

            PageHeader { title: "Scan Wi-Fi network" }

            Rectangle {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                height: width
                visible: page.candidatePayload.length === 0
                color: "black"

                VideoOutput {
                    anchors.fill: parent
                    source: camera
                    fillMode: VideoOutput.PreserveAspectCrop
                    filters: [ qrFilter ]
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: page.candidatePayload.length === 0
                text: page.scanError || camera.errorString
                      || "Point the camera at a Wi-Fi QR code. Open, WEP, and personal WPA networks are supported."
                color: page.scanError || camera.errorString
                       ? Theme.errorColor : Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.Wrap
            }

            Column {
                width: parent.width
                visible: page.candidatePayload.length > 0

                SectionHeader { text: "Network found" }

                DetailItem {
                    label: "Network"
                    value: page.candidate ? page.candidate.displayName : ""
                }

                DetailItem {
                    label: "Security"
                    value: !page.candidate ? ""
                           : page.candidate.securityFamily === "open" ? "Open"
                           : page.candidate.securityFamily === "wep" ? "WEP"
                           : page.candidate.securityHint === "sae" ? "WPA3 personal"
                           : "WPA/WPA2 personal"
                }

                DetailItem {
                    label: "Hidden"
                    value: page.candidate && page.candidate.hidden ? "Yes" : "No"
                }

                DetailItem {
                    label: "Password"
                    value: page.candidate && page.candidate.hasPassphrase
                           ? "Included (concealed)" : "None"
                }

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * x
                    text: "Adding the network saves it on this device and includes it in Flotsam synchronization. Previously forgotten networks ask before syncing again."
                    color: Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.Wrap
                }

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * x
                    visible: page.scanError.length > 0
                    text: page.scanError
                    color: Theme.errorColor
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.Wrap
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Add network"
                    enabled: !page.importing
                    onClicked: {
                        page.scanError = ""
                        page.importing = true
                        controller.importWifiQr(page.candidatePayload)
                    }
                }

            }

            Item { width: 1; height: Theme.paddingLarge }
        }

        VerticalScrollDecorator {}
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: page.importing
        size: BusyIndicatorSize.Large
    }
}
