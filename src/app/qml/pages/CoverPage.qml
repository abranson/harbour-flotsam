// Copyright (C) 2026 Jolla Mobile Ltd
// SPDX-License-Identifier: BSD-3-Clause

import QtQuick 2.6
import Sailfish.Silica 1.0

CoverBackground {
    Image {
        anchors.centerIn: parent
        width: parent.width
        height: width
        source: "../images/cover-background.svg"
        sourceSize.width: width
        sourceSize.height: height
        fillMode: Image.PreserveAspectFit
        opacity: 0.1
    }

    Label {
        anchors {
            left: parent.left
            right: parent.right
            margins: Theme.paddingLarge
            verticalCenter: parent.verticalCenter
        }
        text: controller.status.version === undefined ? "Service unavailable"
              : controller.status.needsAttention
              ? controller.status.needsAttention + " need attention"
              : controller.status.syncing ? "Synchronizing Wi-Fi" : "Wi-Fi synchronized"
        color: Theme.highlightColor
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
    }

    CoverActionList {
        enabled: controller.status.setupComplete && !controller.status.syncing
        CoverAction {
            iconSource: "image://theme/icon-cover-sync"
            onTriggered: controller.manualSync()
        }
    }
}
