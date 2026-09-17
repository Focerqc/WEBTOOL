/*
    Copyright 2018 - 2019 Benjamin Vedder	benjamin@vedder.se

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
import QtQuick.Dialogs as Dl

import Vedder.vesc
import "qrc:/mobile" as MobileUi

Item {
    id: confPageMotorItem
    property Commands mCommands: VescIf.commands()
    property bool isHorizontal: width > height
    property var dialogParent: ApplicationWindow.overlay

    MobileUi.ParamEditors {
        id: editors
    }

    DetectBldc {
        id: detectBldc
        dialogParent: confPageMotorItem.dialogParent
    }

    DetectFocParam {
        id: detectFocParam
        dialogParent: confPageMotorItem.dialogParent
    }

    DetectFocHall {
        id: detectFocHall
        dialogParent: confPageMotorItem.dialogParent
    }

    DetectFocEncoder {
        id: detectFocEncoder
        dialogParent: confPageMotorItem.dialogParent
    }

    Dialog {
        id: directionSetupDialog
        title: "Direction Setup"
        standardButtons: Dialog.Close
        modal: true
        focus: true
        padding: 10

        Overlay.modal: Rectangle {
            color: "#AA000000"
        }

        width: parent.width - 10 - notchLeft - notchRight
        closePolicy: Popup.CloseOnEscape
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        parent: confPageMotorItem.dialogParent

        DirectionSetup {
            id: directionSetup
            anchors.fill: parent
            dialogParent: confPageMotorItem.dialogParent
        }
    }

    Dialog {
        id: measureOffsetDialog
        standardButtons: Dialog.Ok | Dialog.Cancel
        modal: true
        focus: true
        width: parent.width - 20
        closePolicy: Popup.CloseOnEscape
        title: "Measure Offsets"

        x: 10
        y: Math.max((parent.height - height) / 2, 10)
        parent: confPageMotorItem.dialogParent

        Overlay.modal: Rectangle {
            color: "#AA000000"
        }

        Text {
            width: parent.width
            id: detectLambdaLabel
            color: Utility.getAppHexColor("lightText")
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap
            text:
                "This is going to measure and store all offsets. Make sure " +
                "that the motor is not moving or disconnected. Motor rotation " +
                "during the measurement will cause an invalid result. Do you " +
                "want to continue?"
        }

        Timer {
            id: offsetReadConfTimer
            interval: 5000
            repeat: false
            running: false
            onTriggered: {
                mCommands.getMcconf()
            }
        }

        onAccepted: {
            mCommands.sendTerminalCmd("foc_dc_cal")
            offsetReadConfTimer.restart()
        }
    }

    onIsHorizontalChanged: {
        updateEditors()
    }

    function addSeparator(text) {
        var e = editors.createSeparator(scrollCol, text)
        e.Layout.columnSpan = isHorizontal ? 2 : 1
    }

    function destroyEditors() {
        for(var i = scrollCol.children.length;i > 0;i--) {
            scrollCol.children[i - 1].destroy(1) // Only works with delay on android, seems to be a bug
        }
    }

    function createEditorMc(param) {
        var e = editors.createEditorMc(scrollCol, param)
        e.Layout.preferredWidth = 500
        e.Layout.fillsWidth = true
    }

    function updateEditors() {
        destroyEditors()

        var params = VescIf.mcConfig().getParamsFromSubgroup(pageBox.currentText, tabBox.currentText)

        for (var i = 0;i < params.length;i++) {
            if (params[i].startsWith("::sep::")) {
                addSeparator(params[i].substr(7))
            } else {
                createEditorMc(params[i])
            }
        }
    }

    ColumnLayout {
        id: column
        anchors.fill: parent
        spacing: 0

        GridLayout {
            Layout.fillWidth: true
            columns: isHorizontal ? 2 : 1
            rowSpacing: -5
            ComboBox {
                id: pageBox
                Layout.fillWidth: true

                model: VescIf.mcConfig().getParamGroups()

                onCurrentTextChanged: {
                    var tabTextOld = tabBox.currentText
                    var subgroups = VescIf.mcConfig().getParamSubgroups(currentText)

                    tabBox.model = subgroups
                    tabBox.visible = subgroups.length > 1

                    if (tabTextOld === tabBox.currentText && tabTextOld !== "") {
                        updateEditors()
                    }
                }
            }
            
            ComboBox {
                id: tabBox
                Layout.fillWidth: true
                
                onCurrentTextChanged: {
                    updateEditors()
                }
            }
        }
        
        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: column.width
            contentHeight: scrollCol.preferredHeight
            clip: true

            GridLayout {
                id: scrollCol
                width: column.width
                columns: isHorizontal ? 2 : 1
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Button {
                Layout.preferredWidth: 100
                Layout.fillWidth: true
                text: "Write"

                onClicked: {
                    console.log("[MOTOR CFG] Write clicked, calling setMcconf(true)")
                    VescIf.commands().setMcconf(true)
                    VescIf.emitStatusMessage("Writing motor configuration...", true)
                }
            }

            Button {
                Layout.preferredWidth: 100
                Layout.fillWidth: true
                text: "Read"

                onClicked: {
                    console.log("[MOTOR CFG] Read clicked, calling getMcconfForce()")
                    VescIf.commands().getMcconfForce()
                    VescIf.emitStatusMessage("Reading motor configuration...", true)
                }
            }

            Button {
                id: menuButton
                Layout.preferredWidth: 50
                Layout.fillWidth: true
                text: "..."
                onClicked: menu.open()

                Menu {
                    id: menu
                    bottomPadding: notchBot
                    leftPadding: notchLeft
                    rightPadding: notchRight
                    parent: confPageMotorItem
                    y: parent.height - implicitHeight
                    width: parent.width

                    MenuItem {
                        text: "Read Default Settings"
                        onTriggered: {
                            console.log("[MOTOR CFG] Read Default Settings clicked")
                            VescIf.commands().getMcconfDefaultForce()
                            VescIf.emitStatusMessage("Reading default motor configuration...", true)
                        }
                    }
                    MenuItem {
                        text: "Detect BLDC Parameters..."
                        onTriggered: {
                            console.log("[MOTOR CFG] Opening Detect BLDC dialog")
                            detectBldc.openDialog()
                        }
                    }
                    MenuItem {
                        text: "Detect FOC Parameters..."
                        onTriggered: {
                            console.log("[MOTOR CFG] Opening Detect FOC dialog")
                            detectFocParam.openDialog()
                        }
                    }
                    MenuItem {
                        text: "Detect FOC Hall Sensors..."
                        onTriggered: {
                            console.log("[MOTOR CFG] Opening Detect FOC Hall dialog")
                            detectFocHall.openDialog()
                        }
                    }
                    MenuItem {
                        text: "Detect FOC Encoder..."
                        onTriggered: {
                            console.log("[MOTOR CFG] Opening Detect FOC Encoder dialog")
                            detectFocEncoder.openDialog()
                        }
                    }
                    MenuItem {
                        text: "Measure FOC Offsets"
                        onTriggered: {
                            if (VescIf.isPortConnected()) {
                                measureOffsetDialog.open()
                            } else {
                                VescIf.emitMessageDialog("Measure FOC Offsets", "Not Connected", false, false)
                            }
                        }
                    }
                    MenuItem {
                        text: "Setup Motor Directions..."
                        onTriggered: {
                            directionSetupDialog.open()
                            directionSetup.scanCan()
                        }
                    }
                    MenuItem {
                        text: "Save XML"
                        onTriggered: {
                            if (Utility.requestFilePermission()) {
                                fileDialogSave.close()
                                fileDialogSave.open()
                            } else {
                                VescIf.emitMessageDialog(
                                            "File Permissions",
                                            "Unable to request file system permission.",
                                            false, false)
                            }
                        }

                        Dl.FileDialog {
                            id: fileDialogSave
                            title: "Please choose a file"
                            nameFilters: ["*"]
                            fileMode: Dl.FileDialog.SaveFile
                            onAccepted: {
                                var path = selectedFile.toString()
                                if (VescIf.mcConfig().saveXml(path, "MCConfiguration")) {
                                    VescIf.emitStatusMessage("Mcconf Saved", true)
                                } else {
                                    VescIf.emitStatusMessage("Mcconf Save Failed", false)
                                }

                                close()
                                parent.forceActiveFocus()
                            }
                            onRejected: {
                                close()
                                parent.forceActiveFocus()
                            }
                        }
                    }
                    MenuItem {
                        text: "Load XML"
                        onTriggered: {
                            if (Utility.requestFilePermission()) {
                                fileDialogLoad.close()
                                fileDialogLoad.open()
                            } else {
                                VescIf.emitMessageDialog(
                                            "File Permissions",
                                            "Unable to request file system permission.",
                                            false, false)
                            }
                        }

                        Dl.FileDialog {
                            id: fileDialogLoad
                            title: "Please choose a file"
                            nameFilters: ["*"]
                            fileMode: Dl.FileDialog.OpenFile
                            onAccepted: {
                                var path = selectedFile.toString()
                                if (VescIf.mcConfig().loadXml(path, "MCConfiguration")) {
                                    VescIf.emitStatusMessage("Mcconf Loaded", true)
                                } else {
                                    VescIf.emitStatusMessage("Mcconf Load Failed", false)
                                }

                                close()
                                parent.forceActiveFocus()
                            }
                            onRejected: {
                                close()
                                parent.forceActiveFocus()
                            }
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: mCommands

        // TODO: For some reason this does not work
        function onMcConfigCheckResult(paramsNotSet) {
            if (paramsNotSet.length > 0) {
                var notUpdated = "The following parameters were truncated because " +
                        "they were beyond the hardware limits:\n"

                for (var i = 0;i < paramsNotSet.length;i++) {
                    notUpdated += mMcConf.getLongName(paramsNotSet[i]) + "\n"
                }

                VescIf.emitMessageDialog("Parameters truncated", notUpdated, false, false)
            }
        }
    }

    Connections {
        target: VescIf
        function onConfigurationChanged() {
            pageBox.model = VescIf.mcConfig().getParamGroups()

            var tabTextOld = tabBox.currentText
            var subgroups = VescIf.mcConfig().getParamSubgroups(pageBox.currentText)

            tabBox.model = subgroups
            tabBox.visible = subgroups.length > 1

            updateEditors()
        }
    }

    Connections {
        target: VescIf.mcConfig()
        function onUpdated() {
            console.log("[MOTOR CFG] mcConfig updated from VESC successfully, refreshing UI editors")
            VescIf.emitStatusMessage("Motor configuration updated", true)
            updateEditors()
        }
    }

    Connections {
        target: VescIf.commands()
        function onDeserializeConfigFailed(isMc, isApp) {
            if (isMc) {
                console.error("[MOTOR CFG] Deserializing motor configuration failed!")
                VescIf.emitStatusMessage("Motor config deserialization failed", false)
            }
        }
        function onMcConfigCheckResult(diff) {
            console.log("[MOTOR CFG] mcConfig check result diff size:", diff.length)
            if (diff.length === 0) {
                VescIf.emitStatusMessage("Motor configuration verified", true)
            } else {
                console.warn("[MOTOR CFG] mcConfig params not set:", diff.join(", "))
                VescIf.emitStatusMessage("Motor config: " + diff.length + " params rejected", false)
            }
        }
    }
}
