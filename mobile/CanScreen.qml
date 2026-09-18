/*
    Copyright 2021 Benjamin Vedder	benjamin@vedder.se

    This file is part of VESC Tool.

    VESC Tool is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    VESC Tool is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Vedder.vesc

Item {
    id: rootItem

    property var dialogParent: ApplicationWindow.overlay
    property BleUart mBle: VescIf.bleDevice()
    property Commands mCommands: VescIf.commands()
    property int animationSpeed: 500
    property bool hasScannedOnConnect: false
    property var pendingCanDevs: []
    property int currentCanDev: -1

    Timer {
        id: canQueryTimeoutTimer
        interval: 1200
        repeat: false
        onTriggered: {
            if (currentCanDev !== -1) {
                console.warn("[CAN_SCREEN] Timeout waiting for FW version from CAN ID:", currentCanDev)
                VescIf.canTmpOverrideEnd()
                currentCanDev = -1
                mCommands.resetFwTimeout()
                canQueryNextTimer.interval = 50
                canQueryNextTimer.start()
            }
        }
    }

    Timer {
        id: canQueryNextTimer
        interval: 50
        repeat: false
        onTriggered: {
            queryNextCanDevice()
        }
    }

    function cancelPendingQueries() {
        canQueryTimeoutTimer.stop()
        canQueryNextTimer.stop()
        if (currentCanDev !== -1) {
            VescIf.canTmpOverrideEnd()
            currentCanDev = -1
            mCommands.resetFwTimeout()
        }
        pendingCanDevs = []
    }

    function queryNextCanDevice() {
        if (!VescIf.isPortConnected()) {
            cancelPendingQueries()
            return
        }

        if (pendingCanDevs.length === 0) {
            currentCanDev = -1
            console.log("[CAN_SCREEN] All CAN device names resolved successfully.")
            return
        }

        currentCanDev = pendingCanDevs.shift()
        console.log("[CAN_SCREEN] Querying FW version for CAN ID:", currentCanDev)
        VescIf.canTmpOverride(true, currentCanDev)
        mCommands.resetFwTimeout()
        mCommands.getFwVersion()
        canQueryTimeoutTimer.restart()
    }

    Component.onCompleted: {
        scanButton.enabled = VescIf.isPortConnected()
    }

    function scanIfEmpty() {
        if (!hasScannedOnConnect &&
                VescIf.isPortConnected() &&
                scanButton.enabled) {
            hasScannedOnConnect = true
            scanButton.clicked()
        }
    }

    function selectDeviceInList() {
        if (mCommands.getSendCan()) {
            for (var i = 0; i < canModel.count;i++) {
                var id = parseInt(canModel.get(i).ID)
                if (id === mCommands.getCanSendId() && canList.currentIndex != i) {
                    canList.currentIndex = i
                    break
                }
            }
        } else {
            if (canList.currentIndex != 0) {
                canList.currentIndex = 0
            }
        }
    }

    function ensureLocalDevice() {
        if (canModel.count === 0 && VescIf.isPortConnected() && VescIf.fwRx()) {
            var params = VescIf.getLastFwRxParams()
            var name = params.hw
            var theme = "qrc" + Utility.getThemePath()
            var devicePath = theme + "icons/motor_side.png"
            var logoPath = " "
            if (params.major === -1) {
                devicePath = theme + "icons/Help-96.png"
                name = "Unknown"
            } else if (params.hwTypeStr() === "VESC") {
                devicePath = theme + "icons/motor_side.png"
                name = (params.fwName.length !== 0) ? (params.hw + "-" + params.fwName) : params.hw
            } else if (params.hwTypeStr() === "VESC BMS") {
                devicePath = theme + "icons/icons8-battery-100.png"
                name = (params.fwName.length !== 0) ? (params.hw + "-" + params.fwName) : params.hw
            } else {
                devicePath = theme + "icons/Electronics-96.png"
                name = (params.fwName.length !== 0) ? (params.hw + "-" + params.fwName) : params.hw
            }
            name = name.replace("_", " ")
            canModel.append({"name": name,
                             "ID": "LOCAL",
                             "deviceIconPath": devicePath,
                             "logoIconPath": logoPath})
        }
    }

    Timer {
        repeat: true
        interval: 1000
        running: true

        onTriggered: {
            if (!VescIf.isPortConnected()) {
                hasScannedOnConnect = false
                cancelPendingQueries()
                mCommands.setSendCan(false, -1)
                canList.currentIndex = 0;
                canModel.clear()
                scanButton.enabled = false
            } else {
                ensureLocalDevice()
                selectDeviceInList()
            }

            if (VescIf.scanCanOnConnect() &&
                    !hasScannedOnConnect &&
                    scanButton.enabled &&
                    VescIf.isPortConnected() &&
                    VescIf.fwRx() && VescIf.customConfigRxDone()) {
                hasScannedOnConnect = true
                scanButton.clicked()
            }
        }
    }

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: 10

        Text {
            id: text
            Layout.fillWidth: true
            color: Utility.getAppHexColor("lightText")
            text: qsTr("CAN Devices")
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        ListView {
            ListModel {
                id: canModel
            }

            // transitions for insertion/deletation of elements
            add: Transition {
                NumberAnimation { property: "opacity"; from: 0; to: 1.0; duration: 200 }
                NumberAnimation { property: "scale"; easing.type: Easing.OutBack; from: 0; to: 1.0; duration: 800 }
            }

            addDisplaced: Transition {
                NumberAnimation { properties: "y"; duration: 600; easing.type: Easing.InBack }
            }

            remove: Transition {
                NumberAnimation { property: "scale"; from: 1.0; to: 0; duration: 200 }
                NumberAnimation { property: "opacity"; from: 1.0; to: 0; duration: 200 }
            }

            removeDisplaced: Transition {
                NumberAnimation { properties: "x,y"; duration: 500; easing.type: Easing.OutExpo }
            }

            id: canList
            Layout.fillWidth: true
            Layout.fillHeight: true
            focus: true
            clip: true
            spacing: 5

            Component {
                id: canDelegate
                Rectangle {
                    Component.onCompleted: showAnim.start();
                    transform: Rotation { id:rt; origin.x: 0; origin.y: height; axis { x: 0.3; y: 1; z: 0 } angle: 0}//     <--- I like this one more!
                    SequentialAnimation {
                        id: showAnim
                        running: false
                        RotationAnimation { target: rt; from: 180; to: 0; duration: 800; easing.type: Easing.OutExpo; property: "angle" }
                    }
                    width: canList.width
                    height: 40
                    color: canList.currentIndex === index ? Utility.getAppHexColor("darkAccent") : Utility.getAppHexColor("normalBackground")
                    radius: 10
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            cancelPendingQueries()
                            canList.currentIndex = index
                            if (index === 0) {
                                mCommands.setSendCan(false, 0)
                            } else {
                                mCommands.setSendCan(true, parseInt(ID))
                            }
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        spacing: 10
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10

                        Image {
                            id: image
                            fillMode: Image.PreserveAspectFit
                            Layout.preferredWidth: 20
                            Layout.preferredHeight: 20
                            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                            source: deviceIconPath
                        }
                        Text {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                            text: name
                            horizontalAlignment: Text.AlignHCenter
                            color: Utility.getAppHexColor("lightText")
                            wrapMode: Text.WordWrap
                        }
                        Rectangle{
                            color: "#00000000"
                            Layout.preferredWidth: t_metrics.boundingRect.width + 10
                            Layout.preferredHeight: idText.implicitHeight * 1.5
                            border.color: Utility.getAppHexColor("lightText")
                            border.width: 2
                            radius: 2

                            TextMetrics {
                                id:     t_metrics
                                font:   idText.font
                                text:   "LOCAL"
                            }

                            Text {
                                id:idText
                                textFormat: Text.RichText
                                anchors.centerIn: parent
                                Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                                text: ID
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                                color: Utility.getAppHexColor("lightText")
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }
            model: canModel
            delegate: canDelegate
        }

        Button {
            id: scanButton
            text: qsTr("CAN Scan")
            enabled: false
            Layout.fillWidth: true
            onClicked: {
                cancelPendingQueries()
                scanButton.enabled = false
                canModel.clear()

                VescIf.canTmpOverride(false, -1)
                mCommands.pingCan()
                VescIf.canTmpOverrideEnd()
            }
        }
    }

    Connections {
        target: mBle

        function onBleError(info) {
            VescIf.emitMessageDialog("BLE Error", info, false, false)
            enableDialog()
        }
    }

    function disableDialog() {
        commDialog.open()
        column.enabled = false
    }

    function enableDialog() {
        commDialog.close()
        column.enabled = true
    }

    Dialog {
        id: commDialog
        title: "Connecting..."
        closePolicy: Popup.NoAutoClose
        modal: true
        focus: true

        Overlay.modal: Rectangle {
            color: "#AA000000"
        }

        width: parent.width - 20
        x: 10
        y: parent.height / 2 - height / 2
        parent: dialogParent

        ProgressBar {
            anchors.fill: parent
            indeterminate: visible
        }
    }

    Connections {
        target: VescIf

        function onPortConnectedChanged() {
            scanButton.enabled = VescIf.isPortConnected()
            if (!VescIf.isPortConnected()) {
                hasScannedOnConnect = false
                cancelPendingQueries()
            }
        }
    }

    Connections {
        target: mCommands

        function onPingCanRx(devs, isTimeout) {
            if (scanButton.enabled) {
                return
            }

            scanButton.enabled = true
            scanButton.text = qsTr("Scan")
            cancelPendingQueries()

            if (VescIf.isPortConnected()) {
                canModel.clear()
                var params = VescIf.getLastFwRxParams()
                var name = params.hw
                var theme = "qrc" + Utility.getThemePath()
                var devicePath = theme + "icons/motor_side.png"
                var logoPath = " "
                if (params.major === -1) {
                    devicePath = theme + "icons/Help-96.png"
                    name = "Unknown"
                } else if (params.hwTypeStr() === "VESC") { //is a motor
                    devicePath = theme + "icons/motor_side.png"
                    if (params.fwName.length !== 0) {
                        name = params.hw + "-" + params.fwName
                    } else {
                        name = params.hw
                    }
                } else if (params.hwTypeStr() === "VESC BMS") { //is a bms
                    devicePath = theme + "icons/icons8-battery-100.png"
                    if (params.fwName.length !== 0) {
                        name = params.hw + "-" + params.fwName
                    } else {
                        name = params.hw
                    }
                } else {
                    devicePath = theme + "icons/Electronics-96.png"
                    if (params.fwName.length !== 0) {
                        name = params.hw + "-" + params.fwName
                    } else {
                        name = params.hw
                    }
                }
                name = name.replace("_", " ")

                canModel.append({"name": name,
                                 "ID": "LOCAL",
                                 "deviceIconPath": devicePath,
                                 "logoIconPath": logoPath})

                var devIds = []
                for (var i = 0; i < devs.length; i++) {
                    var devId = devs[i]
                    devIds.push(devId)
                    canModel.append({"name": "CAN Device " + devId,
                                     "ID": devId.toString(),
                                     "deviceIconPath": theme + "icons/Electronics-96.png",
                                     "logoIconPath": logoPath})
                }

                canList.currentIndex = 0

                if (!isTimeout) {
                    VescIf.emitStatusMessage("CAN Scan Finished", true)
                } else {
                    VescIf.emitStatusMessage("CAN Scan Timed Out", false)
                }

                selectDeviceInList()

                if (devIds.length > 0) {
                    pendingCanDevs = devIds
                    canQueryNextTimer.interval = 50
                    canQueryNextTimer.start()
                }
            } else {
                VescIf.emitStatusMessage("Device not connected", false)
            }
        }

        function onFwVersionReceived(params) {
            if (currentCanDev !== -1) {
                canQueryTimeoutTimer.stop()
                var devId = currentCanDev
                currentCanDev = -1
                VescIf.canTmpOverrideEnd()

                console.log("[CAN_SCREEN] Received FW version for CAN ID:", devId,
                            "hw:", params.hw, "fwName:", params.fwName, "hwType:", params.hwTypeStr())

                var theme = "qrc" + Utility.getThemePath()
                var devicePath = theme + "icons/Electronics-96.png"
                var name = (params && params.hw && params.hw.length > 0) ? params.hw : ("CAN Device " + devId)

                if (params && params.hwTypeStr() === "VESC") {
                    devicePath = theme + "icons/motor_side.png"
                    if (params.fwName && params.fwName.length !== 0) {
                        name = params.hw + "-" + params.fwName
                    }
                } else if (params && params.hwTypeStr() === "VESC BMS") {
                    devicePath = theme + "icons/icons8-battery-100.png"
                    if (params.fwName && params.fwName.length !== 0) {
                        name = params.hw + "-" + params.fwName
                    }
                } else {
                    devicePath = theme + "icons/Electronics-96.png"
                    if (params.fwName && params.fwName.length !== 0) {
                        name = params.hw + "-" + params.fwName
                    }
                }

                name = name.replace("_", " ")

                for (var i = 0; i < canModel.count; i++) {
                    if (canModel.get(i).ID === devId.toString()) {
                        canModel.setProperty(i, "name", name)
                        canModel.setProperty(i, "deviceIconPath", devicePath)
                        break
                    }
                }

                canQueryNextTimer.interval = 50
                canQueryNextTimer.start()
            }
        }
    }
}
