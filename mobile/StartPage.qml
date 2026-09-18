/*
    Copyright 2017 - 2019 Benjamin Vedder	benjamin@vedder.se

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
    id: topItem
    property Commands mCommands: VescIf.commands()
    property bool isHorizontal: width > height
    signal requestOpenControls()
    signal requestConnect()
    signal requestOpenMultiSettings()

    ScrollView {
        anchors.fill: parent
        contentWidth: parent.width
        contentHeight: grid.height + 30
        property int gridItemPreferredWidth: isHorizontal ? parent.width/2.0 - 15 : parent.width - 10
        clip: true

        GridLayout {
            id: grid
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            columns: isHorizontal ? 2 : 1
            anchors.topMargin: 15
            anchors.bottomMargin: 15
            columnSpacing: 10
            rowSpacing: 5
            Item {
                Layout.columnSpan: 1
                Layout.fillWidth: true
                Layout.fillHeight: true
                implicitHeight: image.height
                Image {
                    id: image
                    anchors.centerIn: parent
                    width: Math.min(parent.width * 0.8, 0.4 * sourceSize.width * wizardBox.height/sourceSize.height)
                    height: (sourceSize.height * width) / sourceSize.width
                    source: "qrc" + Utility.getThemePath() + "/logo.png"
                    antialiasing: true
                }
            }

            GroupBox {
                id: wizardBox
                title: qsTr("Configuration")
                Layout.fillWidth: true
                GridLayout {
                    anchors.topMargin: -5
                    anchors.bottomMargin: -5
                    anchors.fill: parent
                    columns: 2
                    columnSpacing: 5
                    rowSpacing: 5

                    ImageButton {
                        id: connectButton
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 500
                        Layout.preferredHeight: 80

                        buttonText: "Connect"
                        imageSrc: "qrc" + Utility.getThemePath() + (VescIf.isPortConnected() ? "icons/Disconnected-96.png" : "icons/Connected-96.png")
                        onClicked: {
                            if (VescIf.isPortConnected()) {
                                VescIf.disconnectPort()
                            } else {
                                topItem.requestConnect()
                            }
                        }
                    }

                    ImageButton {
                        id: nrfPairButton
                        visible: !nrfPair.visible
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 500
                        Layout.preferredHeight: 80

                        buttonText: "VESC\nRemote\nQuick Pair"
                        imageSrc: "qrc" + Utility.getThemePath() + "icons/icons8-fantasy-96.png"

                        onClicked: {
                            if (!VescIf.isPortConnected()) {
                                VescIf.emitMessageDialog("Quick Pair",
                                                         "You are not connected to the VESC. Please connect in order " +
                                                         "to quick pair an NRF-based remote.", false, false)
                            } else {
                                nrfPairStartDialog.open()
                            }
                        }
                    }

                    ImageButton {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 500
                        Layout.preferredHeight: 80

                        buttonText: "Setup\nMotors"
                        imageSrc: "qrc" + Utility.getThemePath() + "icons/motor.png"

                        onClicked: {
                            if (!VescIf.isPortConnected()) {
                                VescIf.emitMessageDialog("FOC Setup Wizard",
                                                         "You are not connected to the VESC. Please connect in order " +
                                                         "to run this wizard.", false, false)
                            } else {
                                wizardFoc.openDialog()
                            }
                        }
                    }

                    ImageButton {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 500
                        Layout.preferredHeight: 80

                        buttonText: "Setup\nInput"
                        imageSrc: "qrc" + Utility.getThemePath() + "icons/Wizard-96.png"

                        onClicked: {
                            if (!VescIf.isPortConnected()) {
                                VescIf.emitMessageDialog("Input Setup Wizard",
                                                         "You are not connected to the VESC. Please connect in order " +
                                                         "to run this wizard.", false, false)
                            } else {
                                // Something in the opendialog function causes a weird glitch, probably
                                // caused by the eventloop in the can scan function. Disabling the button
                                // seems to help. TODO: figure out what the actual problem is.
                                enabled = false
                                wizardInput.openDialog()
                                enabled = true
                            }
                        }
                    }

                    ImageButton {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 500
                        Layout.preferredHeight: 80

                        buttonText: "Setup\nIMU"
                        imageSrc: "qrc" + Utility.getThemePath() + "icons/imu_off.png"

                        onClicked: {
                            if (!VescIf.isPortConnected()) {
                                VescIf.emitMessageDialog("IMU Setup Wizard",
                                                         "You are not connected to the VESC. Please connect in order " +
                                                         "to run this wizard.", false, false)
                            } else {
                                wizardIMU.openDialog()
                            }
                        }
                    }

                    NrfPair {
                        id: nrfPair
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.columnSpan: 2
                        visible: false
                        hideAfterPair: true
                    }

                    Item {
                        visible: isHorizontal
                        Layout.fillHeight: true
                    }
                }
            }

            GroupBox {
                id: toolsBox
                title: qsTr("Tools and Operations")
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.rowSpan: isHorizontal ? 2 : 1

                GridLayout {
                    anchors.topMargin: -5
                    anchors.bottomMargin: -5
                    anchors.fill: parent
                    columns: 2
                    columnSpacing: 5
                    rowSpacing: 5

                    ImageButton {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 500
                        Layout.preferredHeight: 80

                        buttonText: "Controls"
                        imageSrc: "qrc" + Utility.getThemePath() + "icons/Controller-96.png"

                        onClicked: {
                            requestOpenControls()
                        }
                    }

                    ImageButton {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 500
                        Layout.preferredHeight: 80

                        buttonText: "Invert\nMotor\nDirections"
                        imageSrc: "qrc" + Utility.getThemePath() + "icons/Process-96.png"

                        onClicked: {
                            if (!VescIf.isPortConnected()) {
                                VescIf.emitMessageDialog("Directions",
                                                         "You are not connected to the VESC. Please connect in order " +
                                                         "to map directions.", false, false)
                            } else {
                                enabled = false
                                directionSetupDialog.open()
                                directionSetup.scanCan()
                                enabled = true
                            }
                        }
                    }

                    ImageButton {
                        id: backupConfButton
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 500
                        Layout.preferredHeight: 80

                        buttonText: "Backup\nConfigs"
                        imageSrc: "qrc" + Utility.getThemePath() + "icons/Save-96.png"

                        onClicked: {
                            backupConfigDialog.openDialog()
                        }
                    }

                    ImageButton {
                        id: restoreConfButton
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 500
                        Layout.preferredHeight: 80

                        buttonText: "Restore\nConfigs"
                        imageSrc: "qrc" + Utility.getThemePath() + "icons/Open Folder-96.png"

                        onClicked: {
                            restoreConfigDialog.openDialog()
                        }
                    }

                    ImageButton {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 500
                        Layout.preferredHeight: 80

                        buttonText: "Setup\nBluetooth\nModule"
                        imageSrc: "qrc" + Utility.getThemePath() + "icons/bluetooth.png"

                        onClicked: {
                            if (VescIf.getLastFwRxParams().nrfNameSupported &&
                                    VescIf.getLastFwRxParams().nrfPinSupported) {
                                bleSetupDialog.openDialog()
                            } else {
                                pairDialog.openDialog()
                            }
                        }
                    }

                    ImageButton {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 500
                        Layout.preferredHeight: 80

                        buttonText: "Multi\nSettings"
                        imageSrc: "qrc" + Utility.getThemePath() + "icons/Settings-96.png"

                        onClicked: {
                            requestOpenMultiSettings()
                        }
                    }

                    ImageButton {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 500
                        Layout.preferredHeight: 80

                        buttonText: "Update\nFirmware"
                        imageSrc: "qrc" + Utility.getThemePath() + "icons/Electronics-96.png"

                        onClicked: {
                            firmwareDialog.open()
                        }
                    }

                    ImageButton {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 500
                        Layout.preferredHeight: 80

                        buttonText: "Package\nStore"
                        imageSrc: "qrc" + Utility.getThemePath() + "icons/Package-96.png"

                        onClicked: {
                            packageDialog.open()
                        }
                    }
                }

                Timer {
                    id: toolsTimer
                    interval: 100
                    repeat: true
                    running: true
                    onTriggered: {
                        restoreConfButton.enabled = VescIf.customConfigRxDone()
                        backupConfButton.enabled = VescIf.customConfigRxDone()
                    }
                }
            }

            GroupBox {
                title: qsTr("Realtime Data Logging")
                Layout.fillWidth: true

                LogBox {
                    anchors.fill: parent
                    dialogParent: topItem
                }
            }

            GroupBox {
                title: qsTr("Wireless Bridge to Computer (TCP)")
                Layout.fillWidth: true

                TcpBox {
                    anchors.fill: parent
                }
            }

            GroupBox {
                title: qsTr("TCP Hub (Internet Bridge)")
                Layout.bottomMargin: isHorizontal ? 0 : 10
                Layout.fillWidth: true

                TcpHubBox {
                    anchors.fill: parent
                }
            }
        }
    }

    SetupWizardFoc {
        id: wizardFoc
        dialogParent: mainSwipeView
        isHorizontal: mainIsHorizontal
    }

    SetupWizardInput {
        id: wizardInput
        dialogParent: mainSwipeView
        isHorizontal: mainIsHorizontal
    }

    PairingDialog {
        id: pairDialog
        dialogParent: mainSwipeView
    }

    BleSetupDialog {
        id: bleSetupDialog
        dialogParent: mainSwipeView
    }

    SetupWizardIMU {
        id: wizardIMU
        dialogParent: mainSwipeView
    }

    Connections {
        target: VescIf
        function onPortConnectedChanged() {
            if (VescIf.isPortConnected()) {
                connectButton.buttonText = "Disconnect"
                connectButton.imageSrc = "qrc" + Utility.getThemePath() + "icons/Disconnected-96.png"
            } else {
                connectButton.buttonText = "Connect"
                connectButton.imageSrc = "qrc" + Utility.getThemePath() + "icons/Connected-96.png"
            }
        }

        function onBackupFolderSelected(folderName, ok) {
            if (ok) {
                backupConfigDialog.savedFolder = folderName
                restoreConfigDialog.currentFolder = folderName
                if (backupConfigDialog.autoStartAfterFolder && backupConfigDialog.visible) {
                    backupConfigDialog.autoStartAfterFolder = false
                    backupConfigDialog.close()
                    progDialog.title = "Backing up Configuration..."
                    progDialog.statusText = "Initializing backup..."
                    progDialog.progressValue = 0.05
                    progDialog.isIndeterminate = true
                    progDialog.open()
                    var canId = mCommands.getSendCan() ? mCommands.getCanSendId() : -1
                    VescIf.startWebBackup(canId, backupConfigDialog.customBackupName)
                }
                if (restoreConfigDialog.visible) {
                    restoreConfigDialog.scanning = true
                    VescIf.requestWebBackupList()
                }
            }
        }

        function onWebBackupProgress(message, progress) {
            progDialog.statusText = message
            progDialog.progressValue = progress
            progDialog.isIndeterminate = (progress <= 0.0 || progress >= 1.0)
        }

        function onWebBackupFinished(success, message, folderName) {
            progDialog.close()
            VescIf.emitMessageDialog("Backup Configs", message, success, false)
        }

        function onWebBackupListReady(backups) {
            restoreConfigDialog.backupModel = backups
            restoreConfigDialog.scanning = false
        }

        function onWebRestoreFinished(success, message) {
            progDialog.close()
            VescIf.emitMessageDialog("Restore Configs", message, success, false)
        }
    }

    Dialog {
        id: directionSetupDialog
        title: "Direction Setup"
        standardButtons: Dialog.Close
        modal: true
        focus: true
        padding: 10

        width: parent.width - 10 - notchLeft - notchRight
        closePolicy: Popup.CloseOnEscape
        x: parent.width/2 - width/2
        y: parent.height / 2 - height / 2
        parent: mainSwipeView

        Overlay.modal: Rectangle {
            color: "#AA000000"
        }

        DirectionSetup {
            id: directionSetup
            anchors.fill: parent
            dialogParent: topItem
        }
    }

    Dialog {
        id: nrfPairStartDialog
        standardButtons: Dialog.Ok | Dialog.Cancel
        modal: true
        focus: true
        width: parent.width - 20 - notchLeft - notchRight
        closePolicy: Popup.CloseOnEscape
        title: "NRF Pairing"

        parent: mainSwipeView
        x: parent.width/2 - width/2
        y: topItem.y + topItem.height / 2 - height / 2

        Overlay.modal: Rectangle {
            color: "#AA000000"
        }

        Text {
            color: Utility.getAppHexColor("lightText")
            verticalAlignment: Text.AlignVCenter
            width: parent.width
            wrapMode: Text.WordWrap
            text:
                "After clicking OK the VESC will be put in pairing mode for 10 seconds. Switch" +
                "on your remote during this time to complete the pairing process."
        }

        onAccepted: {
            nrfPair.visible = true
            nrfPair.startPairing()
        }
    }

    Dialog {
        id: backupConfigDialog
        modal: true
        focus: true
        width: Math.min(480, parent.width - 20 - notchLeft - notchRight)
        closePolicy: Popup.CloseOnEscape
        title: "Backup Configuration"

        parent: mainSwipeView
        x: parent.width/2 - width/2
        y: Math.max(20, topItem.y + topItem.height / 2 - height / 2)

        Overlay.modal: Rectangle {
            color: "#AA000000"
        }

        property string savedFolder: ""
        property string customBackupName: ""
        property bool autoStartAfterFolder: false

        function openDialog() {
            if (!VescIf.isPortConnected()) {
                VescIf.emitMessageDialog("Backup Configs", "Please connect to the VESC before backing up configurations.", false, false)
                return
            }
            if (VescIf.hasWebFsBackupApi()) {
                savedFolder = VescIf.getSavedBackupFolderName()
                autoStartAfterFolder = false
                customBackupNameInput.text = ""
                open()
            } else {
                legacyBackupConfigDialog.open()
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 12

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: targetCol.implicitHeight + 16
                color: Utility.getAppHexColor("darkerBackground")
                radius: 6
                border.width: 1
                border.color: Utility.getAppHexColor("midAccent")

                ColumnLayout {
                    id: targetCol
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "Target Device:"
                            color: Utility.getAppHexColor("lightText")
                            font.bold: true
                        }
                        Text {
                            text: mCommands.getSendCan() ? ("CAN ID " + mCommands.getCanSendId()) : "Direct USB / BLE"
                            color: Utility.getAppHexColor("lightAccent")
                            font.bold: true
                        }
                    }

                    Text {
                        text: "Backs up Motor (mcconf.xml), App (appconf.xml), and Custom/Refloat (customconf.xml) configurations directly to your local computer folder."
                        color: Utility.getAppHexColor("lightText")
                        opacity: 0.85
                        font.pointSize: 9
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: folderCol.implicitHeight + 16
                color: Utility.getAppHexColor("darkerBackground")
                radius: 6

                ColumnLayout {
                    id: folderCol
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6

                    Text {
                        text: "Destination Folder (Chrome File System):"
                        color: Utility.getAppHexColor("lightText")
                        font.bold: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            id: folderLabel
                            Layout.fillWidth: true
                            text: backupConfigDialog.savedFolder !== "" ? backupConfigDialog.savedFolder : "(No folder selected yet)"
                            color: backupConfigDialog.savedFolder !== "" ? Utility.getAppHexColor("lightText") : Utility.getAppHexColor("disabledText")
                            font.italic: backupConfigDialog.savedFolder === ""
                            elide: Text.ElideMiddle
                        }

                        Button {
                            text: backupConfigDialog.savedFolder !== "" ? "Change Folder" : "Choose Folder..."
                            onClicked: {
                                VescIf.selectBackupFolder()
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    text: "Backup Name / Note (Optional):"
                    color: Utility.getAppHexColor("lightText")
                    font.pointSize: 9
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: customBackupNameInput.implicitHeight + 12
                    color: "#202020"
                    radius: 4
                    border.color: customBackupNameInput.activeFocus ? Utility.getAppHexColor("lightAccent") : "#444444"
                    border.width: 1

                    TextInput {
                        id: customBackupNameInput
                        anchors.fill: parent
                        anchors.margins: 6
                        color: Utility.getAppHexColor("lightText")
                        font.pointSize: 10
                        clip: true
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Layout.topMargin: 4

                Button {
                    Layout.fillWidth: true
                    text: "Cancel"
                    onClicked: backupConfigDialog.close()
                }

                Button {
                    Layout.fillWidth: true
                    text: "Start Backup"
                    highlighted: true
                    onClicked: {
                        backupConfigDialog.customBackupName = customBackupNameInput.text
                        if (backupConfigDialog.savedFolder === "") {
                            backupConfigDialog.autoStartAfterFolder = true
                            VescIf.selectBackupFolder()
                            return
                        }
                        backupConfigDialog.close()
                        progDialog.title = "Backing up Configuration..."
                        progDialog.statusText = "Initializing backup..."
                        progDialog.progressValue = 0.05
                        progDialog.isIndeterminate = true
                        progDialog.open()
                        var canId = mCommands.getSendCan() ? mCommands.getCanSendId() : -1
                        VescIf.startWebBackup(canId, backupConfigDialog.customBackupName)
                    }
                }
            }
        }
    }

    Dialog {
        id: restoreConfigDialog
        modal: true
        focus: true
        width: Math.min(520, parent.width - 20 - notchLeft - notchRight)
        height: Math.min(580, parent.height - 40)
        closePolicy: Popup.CloseOnEscape
        title: "Restore Configuration"

        parent: mainSwipeView
        x: parent.width/2 - width/2
        y: Math.max(10, topItem.y + topItem.height / 2 - height / 2)

        Overlay.modal: Rectangle {
            color: "#AA000000"
        }

        property string currentFolder: ""
        property var backupModel: []
        property string selectedBackup: ""
        property bool scanning: false

        function openDialog() {
            if (!VescIf.isPortConnected()) {
                VescIf.emitMessageDialog("Restore Configs", "Please connect to the VESC before restoring configurations.", false, false)
                return
            }
            if (VescIf.hasWebFsBackupApi()) {
                currentFolder = VescIf.getSavedBackupFolderName()
                selectedBackup = ""
                backupModel = []
                open()
                if (currentFolder !== "") {
                    scanning = true
                    VescIf.requestWebBackupList()
                }
            } else {
                legacyRestoreConfigDialog.open()
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: rFolderCol.implicitHeight + 14
                color: Utility.getAppHexColor("darkerBackground")
                radius: 6

                ColumnLayout {
                    id: rFolderCol
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            text: restoreConfigDialog.currentFolder !== "" ?
                                  ("Folder: " + restoreConfigDialog.currentFolder) :
                                  "No backup folder selected yet."
                            color: Utility.getAppHexColor("lightText")
                            font.bold: true
                            elide: Text.ElideMiddle
                        }

                        Button {
                            text: restoreConfigDialog.currentFolder !== "" ? "Change Folder" : "Choose Folder..."
                            onClicked: VescIf.selectBackupFolder()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "Restore Target: "
                            color: Utility.getAppHexColor("lightText")
                            font.pointSize: 9
                        }
                        Text {
                            text: mCommands.getSendCan() ? ("CAN ID " + mCommands.getCanSendId()) : "Direct USB / BLE"
                            color: Utility.getAppHexColor("lightAccent")
                            font.bold: true
                            font.pointSize: 9
                        }
                    }
                }
            }

            Text {
                visible: restoreConfigDialog.scanning || restoreConfigDialog.backupModel.length === 0
                Layout.fillWidth: true
                Layout.fillHeight: restoreConfigDialog.backupModel.length === 0
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
                color: Utility.getAppHexColor("disabledText")
                font.italic: true
                wrapMode: Text.WordWrap
                text: restoreConfigDialog.scanning ? "Scanning folder for backups..." :
                      (restoreConfigDialog.currentFolder === "" ? "Click 'Choose Folder' above to select your VESC backups folder." :
                       "No backup folders found in " + restoreConfigDialog.currentFolder + ". Back up a device first!")
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                visible: !restoreConfigDialog.scanning && restoreConfigDialog.backupModel.length > 0

                ListView {
                    id: backupListView
                    width: parent.width
                    model: restoreConfigDialog.backupModel
                    spacing: 6

                    delegate: Rectangle {
                        width: backupListView.width
                        implicitHeight: delegateCol.implicitHeight + 14
                        radius: 6
                        color: restoreConfigDialog.selectedBackup === (modelData.folderName ? modelData.folderName : modelData.name) ?
                               "#2a4460" : Utility.getAppHexColor("darkerBackground")
                        border.width: restoreConfigDialog.selectedBackup === (modelData.folderName ? modelData.folderName : modelData.name) ? 2 : 1
                        border.color: restoreConfigDialog.selectedBackup === (modelData.folderName ? modelData.folderName : modelData.name) ?
                                      Utility.getAppHexColor("lightAccent") : "#333333"

                        ColumnLayout {
                            id: delegateCol
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 3

                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: modelData.name ? modelData.name : (modelData.folderName ? modelData.folderName : "Backup")
                                    color: Utility.getAppHexColor("lightText")
                                    font.bold: true
                                    font.pointSize: 10
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }
                                Text {
                                    text: modelData.date ? modelData.date : ""
                                    color: Utility.getAppHexColor("disabledText")
                                    font.pointSize: 9
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Text {
                                    text: (modelData.canId !== undefined && modelData.canId >= 0 ? ("CAN " + modelData.canId) : "Local") +
                                          (modelData.hw ? (" • " + modelData.hw) : "")
                                    color: Utility.getAppHexColor("lightText")
                                    opacity: 0.8
                                    font.pointSize: 9
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }

                                RowLayout {
                                    spacing: 4
                                    Rectangle {
                                        visible: modelData.hasMc !== false
                                        width: mcText.implicitWidth + 6; height: 16; radius: 3
                                        color: "#1e5330"
                                        Text { id: mcText; anchors.centerIn: parent; text: "MC"; color: "#a8f5c0"; font.pointSize: 8; font.bold: true }
                                    }
                                    Rectangle {
                                        visible: modelData.hasApp !== false
                                        width: appText.implicitWidth + 6; height: 16; radius: 3
                                        color: "#204666"
                                        Text { id: appText; anchors.centerIn: parent; text: "APP"; color: "#a0d2f8"; font.pointSize: 8; font.bold: true }
                                    }
                                    Rectangle {
                                        visible: !!modelData.hasCustom
                                        width: customText.implicitWidth + 6; height: 16; radius: 3
                                        color: "#5c3d1e"
                                        Text { id: customText; anchors.centerIn: parent; text: "REFLOAT"; color: "#f8caa0"; font.pointSize: 8; font.bold: true }
                                    }
                                }
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                restoreConfigDialog.selectedBackup = (modelData.folderName ? modelData.folderName : modelData.name)
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Layout.topMargin: 4

                Button {
                    Layout.fillWidth: true
                    text: "Cancel"
                    onClicked: restoreConfigDialog.close()
                }

                Button {
                    Layout.fillWidth: true
                    text: "Restore Selected"
                    enabled: restoreConfigDialog.selectedBackup !== ""
                    highlighted: enabled
                    onClicked: {
                        var bkpName = restoreConfigDialog.selectedBackup
                        restoreConfigDialog.close()
                        progDialog.title = "Restoring Configuration..."
                        progDialog.statusText = "Loading backup files..."
                        progDialog.progressValue = 0.1
                        progDialog.isIndeterminate = true
                        progDialog.open()
                        var canId = mCommands.getSendCan() ? mCommands.getCanSendId() : -1
                        VescIf.restoreFromWebBackup(bkpName, canId)
                    }
                }
            }
        }
    }

    Dialog {
        id: legacyBackupConfigDialog
        standardButtons: Dialog.Ok | Dialog.Cancel
        modal: true
        focus: true
        width: parent.width - 20 - notchLeft - notchRight
        closePolicy: Popup.CloseOnEscape
        title: "Backup configuration(s)"

        parent: mainSwipeView
        x: parent.width/2 - width/2
        y: topItem.y + topItem.height / 2 - height / 2

        Overlay.modal: Rectangle {
            color: "#AA000000"
        }

        Text {
            color: Utility.getAppHexColor("lightText")
            verticalAlignment: Text.AlignVCenter
            width: parent.width
            wrapMode: Text.WordWrap
            text:
                "This will backup the configuration of the connected VESC, as well as for the VESCs " +
                "connected over CAN-bus. The configurations are stored by VESC UUID. If a backup for a " +
                "VESC UUID already exists it will be overwritten. Continue?"
        }

        onAccepted: {
            progDialog.open()
            workaroundTimerBackup.start()
        }
        Timer {
            id: workaroundTimerBackup
            interval: 0
            repeat: false
            running: false
            onTriggered: {
                VescIf.confStoreBackup(true, "")
                progDialog.close()
            }
        }
    }

    Dialog {
        id: legacyRestoreConfigDialog
        standardButtons: Dialog.Ok | Dialog.Cancel
        modal: true
        focus: true
        width: parent.width - 20 - notchLeft - notchRight
        closePolicy: Popup.CloseOnEscape
        title: "Restore configuration backup(s)"

        parent: mainSwipeView
        x: parent.width/2 - width/2
        y: topItem.y + topItem.height / 2 - height / 2

        Overlay.modal: Rectangle {
            color: "#AA000000"
        }

        Text {
            color: Utility.getAppHexColor("lightText")
            verticalAlignment: Text.AlignVCenter
            width: parent.width
            wrapMode: Text.WordWrap
            text:
                "This will restore the configuration of the connected VESC, as well as the VESCs connected over CAN bus " +
                "if a backup exists for their UUID in this instance of VESC Tool. If no backup is found for the UUID of " +
                "the VESCs nothing will be changed. Continue?"
        }

        onAccepted: {
            progDialog.open()
            workaroundTimerRestore.start()
        }
        Timer {
            id: workaroundTimerRestore
            interval: 0
            repeat: false
            running: false
            onTriggered: {
                VescIf.confRestoreBackup(true)
                progDialog.close()
            }
        }
    }

    Dialog {
        id: progDialog
        title: "Processing..."
        closePolicy: Popup.NoAutoClose
        modal: true
        focus: true

        property alias statusText: progStatusText.text
        property alias progressValue: progProgressBar.value
        property alias isIndeterminate: progProgressBar.indeterminate

        width: Math.min(420, parent.width - 20 - notchLeft - notchRight)
        x: parent.width/2 - width/2
        y: parent.height / 2 - height / 2
        parent: mainSwipeView

        Overlay.modal: Rectangle {
            color: "#AA000000"
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 12

            Text {
                id: progStatusText
                Layout.fillWidth: true
                text: "Please wait..."
                color: Utility.getAppHexColor("lightText")
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                font.pointSize: 10
            }

            ProgressBar {
                id: progProgressBar
                Layout.fillWidth: true
                indeterminate: true
            }
        }
    }

    Dialog {
        id: firmwareDialog
        standardButtons: Dialog.Close
        closePolicy: Popup.CloseOnEscape
        modal: true
        focus: true

        width: parent.width - 20 - notchLeft - notchRight
        height: parent.height - 20
        x: parent.width/2 - width/2
        y: parent.height / 2 - height / 2
        parent: mainSwipeView

        Overlay.modal: Rectangle {
            color: "#AA000000"
        }

        contentItem: FwUpdate {
            id: fwUpdate
            anchors.fill: parent
            anchors.bottomMargin: 50
            anchors.topMargin: tabBar.implicitHeight
            pageIndicatorVisible: false
            swipeOrientation: Qt.Horizontal
            currentPage: tabBar.currentIndex
            isHorizontal: topItem.isHorizontal
        }

        header: Rectangle {
            color: Utility.getAppHexColor("lightText")
            height: tabBar.implicitHeight

            TabBar {
                id: tabBar
                currentIndex: fwUpdate.currentPage
                anchors.fill: parent
                clip: true

                background: Rectangle {
                    opacity: 1
                    color: Utility.getAppHexColor("lightestBackground")
                }

                property int buttonWidth: Math.max(120, tabBar.width / (rep.model.length))

                Repeater {
                    id: rep
                    model: ["Included", "Custom", "Bootloader", "Archive"]

                    TabButton {
                        text: modelData
                        width: tabBar.buttonWidth
                    }
                }

                Component.onCompleted: {
                    tabBar.setCurrentIndex(0)
                }
            }
        }
    }

    Dialog {
        id: packageDialog
        standardButtons: Dialog.Close
        closePolicy: Popup.CloseOnEscape
        modal: true
        focus: true

        width: parent.width - 20 - notchLeft - notchRight
        height: parent.height - 20
        x: parent.width / 2 - width / 2
        y: parent.height / 2 - height / 2
        parent: mainSwipeView

        Overlay.modal: Rectangle {
            color: "#AA000000"
        }

        contentItem: Packages {
            anchors.fill: parent
            anchors.margins: 5
            anchors.bottomMargin: 50
            dialogParent: mainSwipeView
        }
    }
}
