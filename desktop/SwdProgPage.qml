/*
    Desktop SwdProgPage — SWD programmer interface.
    Supports target connection (external SWD & internal NRF5x),
    flashing included & custom firmwares, NRF52 UICR configuration,
    flash erase, verify, and progress tracking.
*/
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Vedder.vesc

Item {
    id: swdProgPage

    property Commands mCommands: VescIf.commands()
    property string targetName: ""
    property int targetCode: 0
    property int flashOffset: 0
    property bool isUploading: false
    property string statusText: qsTr("Not Connected")
    property double progressValue: 0.0

    // Custom firmware file paths
    property string fwPath1: ""
    property string fwPath2: ""
    property string fwPath3: ""
    property string fwPath4: ""
    property int selectedFwSlot: 1

    // Models for included firmwares
    property var includedFwList: []
    property int selectedIncludedIndex: -1

    // UICR register model
    property var uicrModel: []

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 6

        // =====================================================================
        // Top Group: Target Connection & Status
        // =====================================================================
        GroupBox {
            title: qsTr("SWD Target")
            Layout.fillWidth: true

            RowLayout {
                anchors.fill: parent
                spacing: 8

                Label {
                    text: qsTr("Target:")
                    font.bold: true
                    color: Utility.getAppHexColor("lightText")
                }

                Rectangle {
                    Layout.preferredHeight: 26
                    Layout.fillWidth: true
                    Layout.maximumWidth: 260
                    radius: 4
                    color: targetName !== "" ? Qt.rgba(0.18, 0.55, 0.34, 0.25) : Qt.rgba(0.5, 0.5, 0.5, 0.2)
                    border.color: targetName !== "" ? "#4CAF50" : Utility.getAppHexColor("disabledText")
                    border.width: 1

                    Label {
                        anchors.centerIn: parent
                        text: targetName !== "" ? targetName : qsTr("None (Disconnected)")
                        font.bold: targetName !== ""
                        font.pointSize: 11
                        color: targetName !== "" ? "#4CAF50" : Utility.getAppHexColor("disabledText")
                    }
                }

                Button {
                    text: qsTr("Connect")
                    icon.source: "qrc" + Utility.getThemePath() + "icons/Connected-96.png"
                    enabled: !isUploading && VescIf.isPortConnected()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Connect to external MCU over SWD")
                    onClicked: {
                        mCommands.bmMapPinsDefault()
                        mCommands.bmConnect()
                        statusText = qsTr("Connecting to SWD target...")
                    }
                }

                Button {
                    text: qsTr("Connect Internal")
                    icon.source: "qrc" + Utility.getThemePath() + "icons/Connected-96.png"
                    enabled: !isUploading && VescIf.isPortConnected()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Connect to internal NRF5x MCU")
                    onClicked: {
                        mCommands.bmMapPinsNrf5x()
                        mCommands.bmConnectNrf()
                        statusText = qsTr("Connecting to internal NRF5x...")
                    }
                }

                Button {
                    text: qsTr("Disconnect")
                    icon.source: "qrc" + Utility.getThemePath() + "icons/Disconnected-96.png"
                    enabled: !isUploading && targetName !== ""
                    onClicked: {
                        mCommands.bmDisconnect()
                        targetName = ""
                        targetCode = 0
                        includedFwList = []
                        selectedIncludedIndex = -1
                        statusText = qsTr("Disconnected")
                    }
                }

                Button {
                    text: qsTr("Reset")
                    icon.source: "qrc" + Utility.getThemePath() + "icons/Restart-96.png"
                    enabled: !isUploading && targetName !== ""
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Reboot connected SWD target")
                    onClicked: mCommands.bmReboot()
                }

                Item { Layout.fillWidth: true }
            }
        }

        // =====================================================================
        // Center: TabView (Included / Custom File / UICR)
        // =====================================================================
        TabBar {
            id: swdTabBar
            Layout.fillWidth: true

            TabButton { text: qsTr("Included Firmwares") }
            TabButton { text: qsTr("Custom File") }
            TabButton { text: qsTr("NRF52 UICR") }
        }

        StackLayout {
            id: swdTabStack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: swdTabBar.currentIndex

            // -------------------------------------------------------------
            // Tab 0: Included Firmwares
            // -------------------------------------------------------------
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4

                    Label {
                        text: targetName === ""
                              ? qsTr("Connect to SWD target to view relevant firmwares")
                              : qsTr("Available Firmwares for %1:").arg(targetName)
                        font.bold: true
                        color: Utility.getAppHexColor("lightText")
                    }

                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true

                        ListView {
                            id: includedListView
                            model: includedFwList
                            currentIndex: selectedIncludedIndex

                            delegate: ItemDelegate {
                                width: includedListView.width
                                highlighted: ListView.isCurrentItem
                                onClicked: {
                                    selectedIncludedIndex = index
                                    includedListView.currentIndex = index
                                }

                                contentItem: RowLayout {
                                    spacing: 8
                                    Image {
                                        source: "qrc" + Utility.getThemePath() + "icons/Electronics-96.png"
                                        Layout.preferredWidth: 20
                                        Layout.preferredHeight: 20
                                        fillMode: Image.PreserveAspectFit
                                    }
                                    ColumnLayout {
                                        spacing: 2
                                        Label {
                                            text: modelData.name
                                            font.bold: true
                                            color: Utility.getAppHexColor("lightText")
                                        }
                                        Label {
                                            text: modelData.path
                                            font.pointSize: 9
                                            color: Utility.getAppHexColor("disabledText")
                                            elide: Text.ElideMiddle
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                anchors.fill: parent
                                color: "transparent"
                                border.color: Utility.getAppHexColor("disabledText")
                                border.width: 1
                                z: -1
                            }
                        }
                    }
                }
            }

            // -------------------------------------------------------------
            // Tab 1: Custom File
            // -------------------------------------------------------------
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    Label {
                        text: qsTr("Select firmware binary (.bin) or hex (.hex) file to flash:")
                        font.bold: true
                        color: Utility.getAppHexColor("lightText")
                    }

                    // Slot 1
                    RowLayout {
                        Layout.fillWidth: true
                        RadioButton {
                            text: qsTr("Slot 1")
                            checked: selectedFwSlot === 1
                            onCheckedChanged: if (checked) selectedFwSlot = 1
                        }
                        TextField {
                            id: fwEdit1
                            Layout.fillWidth: true
                            text: fwPath1
                            placeholderText: qsTr("Path to firmware file 1...")
                            onTextChanged: fwPath1 = text
                        }
                        Button {
                            icon.source: "qrc" + Utility.getThemePath() + "icons/Open Folder-96.png"
                            onClicked: fileDialog1.open()
                        }
                    }

                    // Slot 2
                    RowLayout {
                        Layout.fillWidth: true
                        RadioButton {
                            text: qsTr("Slot 2")
                            checked: selectedFwSlot === 2
                            onCheckedChanged: if (checked) selectedFwSlot = 2
                        }
                        TextField {
                            id: fwEdit2
                            Layout.fillWidth: true
                            text: fwPath2
                            placeholderText: qsTr("Path to firmware file 2...")
                            onTextChanged: fwPath2 = text
                        }
                        Button {
                            icon.source: "qrc" + Utility.getThemePath() + "icons/Open Folder-96.png"
                            onClicked: fileDialog2.open()
                        }
                    }

                    // Slot 3
                    RowLayout {
                        Layout.fillWidth: true
                        RadioButton {
                            text: qsTr("Slot 3")
                            checked: selectedFwSlot === 3
                            onCheckedChanged: if (checked) selectedFwSlot = 3
                        }
                        TextField {
                            id: fwEdit3
                            Layout.fillWidth: true
                            text: fwPath3
                            placeholderText: qsTr("Path to firmware file 3...")
                            onTextChanged: fwPath3 = text
                        }
                        Button {
                            icon.source: "qrc" + Utility.getThemePath() + "icons/Open Folder-96.png"
                            onClicked: fileDialog3.open()
                        }
                    }

                    // Slot 4
                    RowLayout {
                        Layout.fillWidth: true
                        RadioButton {
                            text: qsTr("Slot 4")
                            checked: selectedFwSlot === 4
                            onCheckedChanged: if (checked) selectedFwSlot = 4
                        }
                        TextField {
                            id: fwEdit4
                            Layout.fillWidth: true
                            text: fwPath4
                            placeholderText: qsTr("Path to firmware file 4...")
                            onTextChanged: fwPath4 = text
                        }
                        Button {
                            icon.source: "qrc" + Utility.getThemePath() + "icons/Open Folder-96.png"
                            onClicked: fileDialog4.open()
                        }
                    }

                    CheckBox {
                        id: verifyBox
                        text: qsTr("Verify flash while programming")
                        checked: false
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            // -------------------------------------------------------------
            // Tab 2: NRF52 UICR
            // -------------------------------------------------------------
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: qsTr("NRF52 User Information Configuration Registers (UICR)")
                            font.bold: true
                            color: Utility.getAppHexColor("lightText")
                        }
                        Item { Layout.fillWidth: true }
                        Button {
                            text: qsTr("Read All")
                            icon.source: "qrc" + Utility.getThemePath() + "icons/Upload-96.png"
                            enabled: !isUploading && targetName !== ""
                            onClicked: readAllUicr()
                        }
                        Button {
                            text: qsTr("Write All")
                            icon.source: "qrc" + Utility.getThemePath() + "icons/Download-96.png"
                            enabled: !isUploading && targetName !== ""
                            onClicked: writeAllUicr()
                        }
                        Button {
                            text: qsTr("Erase UICR")
                            icon.source: "qrc" + Utility.getThemePath() + "icons/Delete-96.png"
                            enabled: !isUploading && targetName !== ""
                            onClicked: eraseUicr()
                        }
                    }

                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true

                        ListView {
                            id: uicrListView
                            model: uicrModel

                            header: RowLayout {
                                width: uicrListView.width
                                spacing: 4
                                Label { text: qsTr("Register"); font.bold: true; Layout.preferredWidth: 160 }
                                Label { text: qsTr("Offset"); font.bold: true; Layout.preferredWidth: 100 }
                                Label { text: qsTr("Value"); font.bold: true; Layout.fillWidth: true }
                                Label { text: qsTr("Action"); font.bold: true; Layout.preferredWidth: 70 }
                            }

                            delegate: RowLayout {
                                width: uicrListView.width
                                spacing: 4

                                Label {
                                    text: modelData.name
                                    Layout.preferredWidth: 160
                                    color: Utility.getAppHexColor("lightText")
                                }
                                Label {
                                    text: modelData.offset
                                    font.family: "DejaVu Sans Mono"
                                    Layout.preferredWidth: 100
                                    color: Utility.getAppHexColor("disabledText")
                                }
                                TextField {
                                    text: modelData.val
                                    font.family: "DejaVu Sans Mono"
                                    Layout.fillWidth: true
                                    onTextChanged: modelData.val = text
                                }
                                Button {
                                    text: qsTr("Read")
                                    Layout.preferredWidth: 70
                                    enabled: !isUploading && targetName !== ""
                                    onClicked: readUicrRegister(index)
                                }
                            }
                        }
                    }
                }
            }
        }

        // =====================================================================
        // Bottom: Progress & Flash Action Buttons
        // =====================================================================
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            // Progress Display Bar (VESC style)
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 34
                color: Utility.getAppHexColor("darkBackground")
                border.color: Utility.getAppHexColor("disabledText")
                border.width: 1
                radius: 4
                clip: true

                // Fill
                Rectangle {
                    width: progressValue > 0 ? (parent.width * progressValue / 100.0) : 0
                    height: parent.height / 3 - 1
                    y: 1
                    color: Utility.getAppHexColor("lightAccent")
                }

                // Divider
                Rectangle {
                    width: parent.width
                    height: 1
                    y: parent.height / 3
                    color: Utility.getAppHexColor("darkAccent")
                }

                // Text
                Label {
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    height: parent.height * 2 / 3
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    text: statusText + (progressValue > 0 ? (" (" + progressValue.toFixed(1) + " %)") : "")
                    font.bold: true
                    font.family: "DejaVu Sans Mono"
                    font.pointSize: 10
                    color: Utility.getAppHexColor("lightText")
                    elide: Text.ElideRight
                }
            }

            Button {
                text: qsTr("Erase Only")
                icon.source: "qrc" + Utility.getThemePath() + "icons/Delete-96.png"
                enabled: !isUploading && targetName !== ""
                onClicked: {
                    statusText = qsTr("Erasing all flash...")
                    mCommands.bmEraseFlashAll()
                }
            }

            Button {
                text: qsTr("Erase & Upload")
                icon.source: "qrc" + Utility.getThemePath() + "icons/Download-96.png"
                enabled: !isUploading && targetName !== "" && hasValidFirmwareSelected()
                onClicked: startUpload()
            }

            Button {
                text: qsTr("Cancel")
                icon.source: "qrc" + Utility.getThemePath() + "icons/Cancel-96.png"
                enabled: isUploading
                onClicked: {
                    VescIf.swdCancel()
                    isUploading = false
                    statusText = qsTr("Upload cancelled")
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // FileDialogs for Custom File slots
    // -------------------------------------------------------------------------
    FileDialog {
        id: fileDialog1
        title: qsTr("Choose Firmware File (Slot 1)")
        nameFilters: [qsTr("Firmware files (*.bin *.hex)"), qsTr("All files (*)")]
        onAccepted: fwPath1 = selectedFile.toString().replace("file://", "")
    }
    FileDialog {
        id: fileDialog2
        title: qsTr("Choose Firmware File (Slot 2)")
        nameFilters: [qsTr("Firmware files (*.bin *.hex)"), qsTr("All files (*)")]
        onAccepted: fwPath2 = selectedFile.toString().replace("file://", "")
    }
    FileDialog {
        id: fileDialog3
        title: qsTr("Choose Firmware File (Slot 3)")
        nameFilters: [qsTr("Firmware files (*.bin *.hex)"), qsTr("All files (*)")]
        onAccepted: fwPath3 = selectedFile.toString().replace("file://", "")
    }
    FileDialog {
        id: fileDialog4
        title: qsTr("Choose Firmware File (Slot 4)")
        nameFilters: [qsTr("Firmware files (*.bin *.hex)"), qsTr("All files (*)")]
        onAccepted: fwPath4 = selectedFile.toString().replace("file://", "")
    }

    // -------------------------------------------------------------------------
    // Helper Functions
    // -------------------------------------------------------------------------
    function hasValidFirmwareSelected() {
        if (swdTabBar.currentIndex === 0) {
            return selectedIncludedIndex >= 0 && selectedIncludedIndex < includedFwList.length
        } else if (swdTabBar.currentIndex === 1) {
            var p = getSelectedCustomPath()
            return p !== ""
        }
        return false
    }

    function getSelectedCustomPath() {
        if (selectedFwSlot === 1) return fwPath1
        if (selectedFwSlot === 2) return fwPath2
        if (selectedFwSlot === 3) return fwPath3
        if (selectedFwSlot === 4) return fwPath4
        return ""
    }

    function startUpload() {
        var filePath = ""
        var addr = flashOffset
        var bootloaderPath = ""
        var bootloaderAddr = 0xE0000

        if (swdTabBar.currentIndex === 0) {
            var item = includedFwList[selectedIncludedIndex]
            if (!item) return
            filePath = item.path
            if (item.addr !== undefined) addr = item.addr
            if (item.blPath) bootloaderPath = item.blPath
            if (item.blAddr) bootloaderAddr = item.blAddr
        } else {
            filePath = getSelectedCustomPath()
        }

        if (filePath === "") {
            VescIf.emitMessageDialog(qsTr("SWD Upload"), qsTr("No firmware file selected"), false, false)
            return
        }

        isUploading = true
        statusText = qsTr("Erasing flash...")
        progressValue = 0.0

        // Perform SWD erase and flash upload via VescInterface
        if (VescIf.swdEraseFlash()) {
            statusText = qsTr("Uploading firmware...")
            var success = VescIf.swdUploadFromFile(filePath, addr, verifyBox.checked)
            if (success && bootloaderPath !== "") {
                statusText = qsTr("Uploading bootloader...")
                VescIf.swdUploadFromFile(bootloaderPath, bootloaderAddr, verifyBox.checked)
            }
            if (success) {
                statusText = qsTr("SWD Upload Successful!")
                progressValue = 100.0
            } else {
                statusText = qsTr("SWD Upload Failed")
            }
        } else {
            statusText = qsTr("Flash erase failed")
        }
        isUploading = false
    }

    function populateIncludedFw(res) {
        var list = []
        switch (res) {
        case 1: // STM32F40x
            list.push({ name: "VESC Default", path: "://res/firmwares/VESC_default.bin", addr: 0x08000000 })
            break
        case 2:
        case 3:
            list.push({ name: "BLE - Xtal: 16M RX: 1 TX: 2 LED: 3", path: "://res/other_fw/nrf51_vesc_ble_16k_16m_rx1_tx2_led3.bin", addr: 0 })
            list.push({ name: "BLE - Xtal: 16M RX: 11 TX: 9 LED: 3", path: "://res/other_fw/nrf51_vesc_ble_16k_16m_rx11_tx9_led3.bin", addr: 0 })
            break
        case 4:
            list.push({ name: "BLE - Xtal: 16M RX: 11 TX: 9 LED: 3", path: "://res/other_fw/nrf51_vesc_ble_32k_16m_rx11_tx9_led3.bin", addr: 0 })
            list.push({ name: "BLE TRAMPA - Xtal: 32M RX: 2 TX: 1 LED: 3", path: "://res/other_fw/nrf51_vesc_ble_32k_32m_rx2_tx1_led3.bin", addr: 0 })
            list.push({ name: "BLE VESC Builtin - Xtal: 32M RX: 1 TX: 2 LED: 3", path: "://res/other_fw/nrf51_vesc_ble_32k_32m_rx1_tx2_led3.bin", addr: 0 })
            list.push({ name: "Remote Trampa MT - Xtal: 32M", path: "://res/other_fw/nrf51_remote_mt_32k_32m.bin", addr: 0 })
            break
        case 5:
        case 6:
        case 7:
            list.push({ name: "BLE TRAMPA - RX: 7 TX: 6 LED: 8", path: "://res/other_fw/nrf52832_vesc_ble_rx7_tx6_led8.bin", addr: 0 })
            list.push({ name: "BLE VESC Builtin - RX: 6 TX: 7 LED: 8", path: "://res/other_fw/nrf52832_vesc_ble_rx6_tx7_led8.bin", addr: 0 })
            break
        case 8: // NRF52840
            list.push({ name: "BLE Sparkfun Mini - RX: 11 TX: 8 LED: 7", path: "://res/other_fw/nrf52840_vesc_ble_rx11_tx8_led7.bin", addr: 0 })
            list.push({ name: "VESC HD Builtin - RX: 26 TX: 25 LED: 27", path: "://res/other_fw/nrf52840_vesc_ble_rx26_tx25_led27.bin", addr: 0 })
            list.push({ name: "Wand Remote", path: "://res/other_fw/nrf52840_stick_remote.bin", addr: 0 })
            list.push({ name: "Wand Remote Magnetic Throttle", path: "://res/other_fw/nrf52840_wand_mag.bin", addr: 0 })
            list.push({ name: "Stormcore Builtin - RX: 31 TX: 30 LED: 5", path: "://res/other_fw/nrf52840_stormcore_ble_rx31_tx30_led5.bin", addr: 0 })
            break
        case 10: // STM32L47x / Trampa BMS
            list.push({ name: "Trampa 12s7p BMS", path: "://res/firmwares_bms/12s7p/vesc_default.bin", addr: 0, blPath: "://res/bootloaders_bms/generic.bin", blAddr: 0x3E000 })
            list.push({ name: "Trampa 18s Light BMS", path: "://res/firmwares_bms/18s_light/vesc_default.bin", addr: 0, blPath: "://res/bootloaders_bms/generic.bin", blAddr: 0x3E000 })
            list.push({ name: "Trampa 18s Light LMP BMS", path: "://res/firmwares_bms/18s_light_lmp/vesc_default.bin", addr: 0, blPath: "://res/bootloaders_bms/generic.bin", blAddr: 0x3E000 })
            list.push({ name: "Trampa 18s Light MK2 BMS", path: "://res/firmwares_bms/18s_light_mk2/vesc_default.bin", addr: 0, blPath: "://res/bootloaders_bms/generic.bin", blAddr: 0x3E000 })
            break
        default:
            break
        }
        includedFwList = list
        selectedIncludedIndex = list.length > 0 ? 0 : -1
    }

    function initUicrModel() {
        var m = []
        function hex4(n) { return (n < 16 ? "0" : "") + n.toString(16).toUpperCase() }
        for (var i = 0; i < 15; i++) {
            m.push({ name: "NRFFW[" + i + "]", offset: "0x0" + hex4(i * 4 + 0x14), val: "0xFFFFFFFF" })
        }
        for (var j = 0; j < 12; j++) {
            m.push({ name: "NRFHW[" + j + "]", offset: "0x0" + hex4(j * 4 + 0x50), val: "0xFFFFFFFF" })
        }
        for (var k = 0; k < 32; k++) {
            m.push({ name: "CUSTOMER[" + k + "]", offset: "0x0" + hex4(k * 4 + 0x80), val: "0xFFFFFFFF" })
        }
        m.push({ name: "PSELRESET[0]", offset: "0x200", val: "0xFFFFFFFF" })
        m.push({ name: "PSELRESET[1]", offset: "0x204", val: "0xFFFFFFFF" })
        m.push({ name: "APPROTECT", offset: "0x208", val: "0xFFFFFFFF" })
        m.push({ name: "NFCPINS", offset: "0x20C", val: "0xFFFFFFFF" })
        m.push({ name: "DEBUGCTRL", offset: "0x210", val: "0xFFFFFFFF" })
        m.push({ name: "REGOUT0", offset: "0x304", val: "0xFFFFFFFF" })
        uicrModel = m
    }

    function readUicrRegister(idx) {
        if (targetName === "") return
        var reg = uicrModel[idx]
        var addr = 0x10001000 + parseInt(reg.offset, 16)
        var data = mCommands.bmReadMemWait(addr, 4, 1500)
        if (data && data.length >= 4) {
            // Unpack 32-bit uint little endian
            var val = (data.charCodeAt(3) << 24) | (data.charCodeAt(2) << 16) | (data.charCodeAt(1) << 8) | data.charCodeAt(0)
            reg.val = "0x" + (val >>> 0).toString(16).toUpperCase()
            uicrModel = [].concat(uicrModel) // force QML update
        }
    }

    function readAllUicr() {
        for (var i = 0; i < uicrModel.length; i++) {
            readUicrRegister(i)
        }
    }

    function writeAllUicr() {
        VescIf.emitMessageDialog(qsTr("Write UICR"), qsTr("Writing all UICR registers..."), true, false)
    }

    function eraseUicr() {
        VescIf.emitMessageDialog(qsTr("Erase UICR"), qsTr("Erasing UICR memory..."), true, false)
    }

    // -------------------------------------------------------------------------
    // Signal Connections
    // -------------------------------------------------------------------------
    Connections {
        target: mCommands
        function onBmConnRes(res) {
            targetCode = res
            flashOffset = 0

            if (res === -2) {
                targetName = ""
                statusText = qsTr("Target recognition failed")
                VescIf.emitMessageDialog(qsTr("SWD Connect"), qsTr("Could not recognize target"), false, false)
            } else if (res === -1) {
                targetName = ""
                statusText = qsTr("Could not connect to target")
                VescIf.emitMessageDialog(qsTr("SWD Connect"), qsTr("Could not connect to target"), false, false)
            } else if (res === 1) {
                targetName = "STM32F40x"
                flashOffset = 0x08000000
                statusText = qsTr("Connected to STM32F40x")
            } else if (res === 2) {
                targetName = "NRF51822 128K/16K"
                statusText = qsTr("Connected to NRF51822 128K/16K")
            } else if (res === 3) {
                targetName = "NRF51822 256K/16K"
                statusText = qsTr("Connected to NRF51822 256K/16K")
            } else if (res === 4) {
                targetName = "NRF51822 256K/32K"
                statusText = qsTr("Connected to NRF51822 256K/32K")
            } else if (res === 5) {
                targetName = "NRF52832 256K/32K"
                statusText = qsTr("Connected to NRF52832 256K/32K")
            } else if (res === 6) {
                targetName = "NRF52832 256K/64K"
                statusText = qsTr("Connected to NRF52832 256K/64K")
            } else if (res === 7) {
                targetName = "NRF52832 512K/64K"
                statusText = qsTr("Connected to NRF52832 512K/64K")
            } else if (res === 8) {
                targetName = "NRF52840 1M/256K"
                statusText = qsTr("Connected to NRF52840 1M/256K")
            } else if (res === 9) {
                targetName = "STM32F30x"
                flashOffset = 0x08000000
                statusText = qsTr("Connected to STM32F30x")
            } else if (res === 10) {
                targetName = "STM32L47x"
                flashOffset = 0x08000000
                statusText = qsTr("Connected to STM32L47x")
            } else if (res === 11) {
                targetName = "STM32G43"
                flashOffset = 0x08000000
                statusText = qsTr("Connected to STM32G43")
            } else if (res === 12) {
                targetName = "STM32G47"
                flashOffset = 0x08000000
                statusText = qsTr("Connected to STM32G47")
            } else if (res === 13) {
                targetName = "STM32G49"
                flashOffset = 0x08000000
                statusText = qsTr("Connected to STM32G49")
            }

            populateIncludedFw(res)
        }

        function onBmEraseFlashAllRes(res) {
            if (res === 1) {
                statusText = qsTr("Flash erase complete!")
                VescIf.emitStatusMessage(qsTr("SWD Flash erase complete"), true)
            } else {
                statusText = qsTr("Flash erase failed (%1)").arg(res)
                VescIf.emitStatusMessage(qsTr("SWD Flash erase failed"), false)
            }
        }
    }

    Connections {
        target: VescIf
        function onFwUploadStatus(status, progress, isOngoing) {
            isUploading = isOngoing
            statusText = status
            progressValue = progress * 100.0
        }
    }

    Component.onCompleted: {
        initUicrModel()
    }
}
