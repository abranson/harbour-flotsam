// Copyright (C) 2026 Jolla Mobile Ltd
// SPDX-License-Identifier: BSD-3-Clause

import QtQuick 2.6
import Sailfish.Silica 1.0

ApplicationWindow {
    id: applicationWindow

    initialPage: Qt.resolvedUrl("pages/MainPage.qml")
    cover: Qt.resolvedUrl("pages/CoverPage.qml")
    allowedOrientations: Orientation.All

    Connections {
        target: applicationActivation
        onActivateRequested: applicationWindow.activate()
        onOpenNetworkRequested: {
            controller.loadDetails(networkId, false)
            pageStack.push(Qt.resolvedUrl("pages/NetworkPage.qml"), {
                networkId: networkId
            })
        }
    }
}
