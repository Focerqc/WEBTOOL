/*
    Desktop EspProgPage — ESP32 programmer interface.
    Supports serial connection to ESP modules, flashing included and custom
    firmwares (bootloader, partition table, application) via USB or VESC bootloader,
    erasing LispBM script storage, and erasing custom QML UI storage.
*/
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Vedder.vesc

Item {
    id: espProgPage

    property Commands mCommands: VescIf.commands()
    property bool isEspConnected: false
    property string targetChipName: ""
    property bool isFlashing: false
    property string statusText: qsTr("Ready")
    property double progressValue: 0.0

    // Serial ports list
    property var portList: []

    // Included firmwares model
    property var includedFwModel: []
    property int selectedIncludedIndex: -1

    // Custom firmware file paths & offsets
    property string customBlPath: ""
    property int customBlOffset: 0x1000
    property string customPartPath: ""
    property int customPartOffset: 0x8000
    property string customAppPath: ""
    property int customAppOffset: 0x10000

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 6

        // =====================================================================
        // Top Group: Serial Connection
        // =====================================================================
        GroupBox {
            title: qsTr("Connection")
            Layout.fillWidth: true

            RowLayout {
                anchors.fill: parent
                spacing: 8

                Label {
                    text: qsTr("Port:")
                    font.bold: true
                    color: Utility.getAppHexColor("lightText")
                }

                ComboBox {
                    id: portComboBox
                    Layout.fillWidth: true
                    Layout.maximumWidth: 260
                    model: portList
                    textRole: "name"
                }

                Button {
                    icon.source: "qrc" + Utility.getThemePath() + "icons/Refresh-96.png"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Refresh serial ports")
                    onClicked: refreshPorts()
                }

                Button {
                    text: qsTr("Connect")
                    icon.source: "qrc" + Utility.getThemePath() + "icons/Connected-96.png"
                    enabled: !isFlashing && !isEspConnected && portComboBox.currentIndex >= 0
                    onClicked: connectEsp()
                }

                Button {
                    text: qsTr("Disconnect")
                    icon.source: "qrc" + Utility.getThemePath() + "icons/Disconnected-96.png"
                    enabled: !isFlashing && isEspConnected
                    onClicked: disconnectEsp()
                }

                // Target Chip Badge
                Rectangle {
                    Layout.preferredHeight: 26
                    Layout.preferredWidth: Math.max(chipLabel.implicitWidth + 16, 100)
                    radius: 4
                    color: isEspConnected ? Qt.rgba(0.18, 0.55, 0.34, 0.25) : Qt.rgba(0.5, 0.5, 0.5, 0.2)
                    border.color: isEspConnected ? "#4CAF50" : Utility.getAppHexColor("disabledText")
                    border.width: 1

                    Label {
                        id: chipLabel
                        anchors.centerIn: parent
                        text: isEspConnected ? (targetChipName !== "" ? targetChipName : qsTr("Connected")) : qsTr("Not Connected")
                        font.bold: isEspConnected
                        font.pointSize: 10
                        color: isEspConnected ? "#4CAF50" : Utility.getAppHexColor("disabledText")
                    }
                }

                Item { Layout.fillWidth: true }
            }
        }

        // =====================================================================
        // Center: TabView (Included / Custom)
        // =====================================================================
        TabBar {
            id: espTabBar
            Layout.fillWidth: true

            TabButton { text: qsTr("Included Firmwares") }
            TabButton { text: qsTr("Custom Firmwares") }
        }

        StackLayout {
            id: espTabStack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: espTabBar.currentIndex

            // -------------------------------------------------------------
            // Tab 0: Included Firmwares
            // -------------------------------------------------------------
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4

                    Label {
                        text: qsTr("Select an included ESP firmware package:")
                        font.bold: true
                        color: Utility.getAppHexColor("lightText")
                    }

                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true

                        ListView {
                            id: includedFwListView
                            model: includedFwModel
                            currentIndex: selectedIncludedIndex

                            delegate: ItemDelegate {
                                width: includedFwListView.width
                                highlighted: ListView.isCurrentItem
                                onClicked: {
                                    selectedIncludedIndex = index
                                    includedFwListView.currentIndex = index
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
                                            text: modelData.dir
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
            // Tab 1: Custom
            // -------------------------------------------------------------
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    Label {
                        text: qsTr("Specify custom binaries and target flash offsets:")
                        font.bold: true
                        color: Utility.getAppHexColor("lightText")
                    }

                    // Bootloader row
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Label { text: qsTr("Bootloader:"); Layout.preferredWidth: 100 }
                        TextField {
                            id: blPathEdit
                            Layout.fillWidth: true
                            text: customBlPath
                            placeholderText: qsTr("Path to bootloader.bin...")
                            onTextChanged: customBlPath = text
                        }
                        Label { text: qsTr("Offset: 0x") }
                        TextField {
                            Layout.preferredWidth: 80
                            text: customBlOffset.toString(16).toUpperCase()
                            onTextChanged: {
                                var v = parseInt(text, 16)
                                if (!isNaN(v)) customBlOffset = v
                            }
                        }
                        Button {
                            icon.source: "qrc" + Utility.getThemePath() + "icons/Open Folder-96.png"
                            onClicked: blFileDialog.open()
                        }
                    }

                    // Partition Table row
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Label { text: qsTr("Partition Table:"); Layout.preferredWidth: 100 }
                        TextField {
                            id: partPathEdit
                            Layout.fillWidth: true
                            text: customPartPath
                            placeholderText: qsTr("Path to partition-table.bin...")
                            onTextChanged: customPartPath = text
                        }
                        Label { text: qsTr("Offset: 0x") }
                        TextField {
                            Layout.preferredWidth: 80
                            text: customPartOffset.toString(16).toUpperCase()
                            onTextChanged: {
                                var v = parseInt(text, 16)
                                if (!isNaN(v)) customPartOffset = v
                            }
                        }
                        Button {
                            icon.source: "qrc" + Utility.getThemePath() + "icons/Open Folder-96.png"
                            onClicked: partFileDialog.open()
                        }
                    }

                    // Application row
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Label { text: qsTr("Application:"); Layout.preferredWidth: 100 }
                        TextField {
                            id: appPathEdit
                            Layout.fillWidth: true
                            text: customAppPath
                            placeholderText: qsTr("Path to vesc_express.bin...")
                            onTextChanged: customAppPath = text
                        }
                        Label { text: qsTr("Offset: 0x") }
                        TextField {
                            Layout.preferredWidth: 80
                            text: customAppOffset.toString(16).toUpperCase()
                            onTextChanged: {
                                var v = parseInt(text, 16)
                                if (!isNaN(v)) customAppOffset = v
                            }
                        }
                        Button {
                            icon.source: "qrc" + Utility.getThemePath() + "icons/Open Folder-96.png"
                            onClicked: appFileDialog.open()
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }

        // =====================================================================
        // Bottom: Progress & Flash Actions
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
                text: qsTr("Flash USB")
                icon.source: "qrc" + Utility.getThemePath() + "icons/Download-96.png"
                enabled: !isFlashing && (isEspConnected || typeof Esp32Flash !== "undefined")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Flash ESP module directly over USB serial")
                onClicked: flashUsb()
            }

            Button {
                text: qsTr("Flash Bootloader")
                icon.source: "qrc" + Utility.getThemePath() + "icons/Download-96.png"
                enabled: !isFlashing && VescIf.isPortConnected()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Flash ESP module through connected VESC bootloader")
                onClicked: flashBootloader()
            }

            Button {
                text: qsTr("Erase LispBM")
                enabled: !isFlashing && VescIf.isPortConnected()
                onClicked: {
                    statusText = qsTr("Erasing LispBM script...")
                    mCommands.lispEraseCode(512 * 1024)
                    statusText = qsTr("LispBM script erased")
                }
            }

            Button {
                text: qsTr("Erase QML")
                enabled: !isFlashing && VescIf.isPortConnected()
                onClicked: {
                    statusText = qsTr("Erasing QML UI...")
                    mCommands.qmlUiErase(512 * 1024)
                    statusText = qsTr("QML UI erased")
                }
            }

            Button {
                text: qsTr("Cancel")
                icon.source: "qrc" + Utility.getThemePath() + "icons/Cancel-96.png"
                enabled: isFlashing
                onClicked: {
                    VescIf.fwUploadCancel()
                    isFlashing = false
                    statusText = qsTr("Flashing cancelled")
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // File Dialogs for Custom Binaries
    // -------------------------------------------------------------------------
    FileDialog {
        id: blFileDialog
        title: qsTr("Choose Bootloader Binary")
        nameFilters: [qsTr("Binary files (*.bin)"), qsTr("All files (*)")]
        onAccepted: customBlPath = selectedFile.toString().replace("file://", "")
    }
    FileDialog {
        id: partFileDialog
        title: qsTr("Choose Partition Table Binary")
        nameFilters: [qsTr("Binary files (*.bin)"), qsTr("All files (*)")]
        onAccepted: customPartPath = selectedFile.toString().replace("file://", "")
    }
    FileDialog {
        id: appFileDialog
        title: qsTr("Choose Application Binary")
        nameFilters: [qsTr("Binary files (*.bin)"), qsTr("All files (*)")]
        onAccepted: customAppPath = selectedFile.toString().replace("file://", "")
    }

    // -------------------------------------------------------------------------
    // Operations
    // -------------------------------------------------------------------------
    function refreshPorts() {
        var ports = []
        if (typeof VescIf !== "undefined") {
            VescIf.pairSerialPort()
            var list = VescIf.listSerialPorts()
            for (var i = 0; i < list.length; i++) {
                var p = list[i]
                ports.push({ name: p.name ? p.name : p.systemPath, path: p.systemPath })
            }
        }
        portList = ports
    }

    function connectEsp() {
        if (typeof Esp32Flash !== "undefined" && portComboBox.currentIndex >= 0) {
            var path = portList[portComboBox.currentIndex].path
            statusText = qsTr("Connecting to ESP at %1...").arg(path)
            if (Esp32Flash.connectEsp(path)) {
                isEspConnected = true
                targetChipName = Esp32Flash.getTargetName()
                statusText = qsTr("Connected to %1").arg(targetChipName)
                filterIncludedFwForChip(targetChipName)
            } else {
                statusText = qsTr("ESP connection failed")
            }
        } else {
            statusText = qsTr("ESP flashing interface not available")
        }
    }

    function disconnectEsp() {
        if (typeof Esp32Flash !== "undefined") {
            Esp32Flash.disconnectEsp()
        }
        isEspConnected = false
        targetChipName = ""
        statusText = qsTr("Disconnected")
    }

    function scanIncludedFw() {
        VescIf.reloadFirmwareResources()
        var list = []
        var root = "://res/firmwares_esp"
        var chipDirs = Utility.listDirEntries(root, true)
        for (var c = 0; c < chipDirs.length; c++) {
            var chipPath = root + "/" + chipDirs[c]
            var boardDirs = Utility.listDirEntries(chipPath, true)
            for (var b = 0; b < boardDirs.length; b++) {
                var path = chipPath + "/" + boardDirs[b]
                if (Utility.fileExists(path + "/vesc_express.bin")) {
                    list.push({
                        name: chipDirs[c].toUpperCase() + ": " + boardDirs[b],
                        dir: path,
                        app: path + "/vesc_express.bin",
                        bl: path + "/bootloader.bin",
                        part: path + "/partition-table.bin"
                    })
                }
            }
        }
        includedFwModel = list
        selectedIncludedIndex = list.length > 0 ? 0 : -1
    }

    function filterIncludedFwForChip(chipName) {
        scanIncludedFw()
        var target = chipName.toLowerCase().replace("-", "")
        for (var i = 0; i < includedFwModel.length; i++) {
            if (includedFwModel[i].name.toLowerCase().replace("-", "").indexOf(target) !== -1) {
                selectedIncludedIndex = i
                break
            }
        }
    }

    function flashUsb() {
        var bl = ""
        var part = ""
        var app = ""
        var blOfs = customBlOffset
        var partOfs = customPartOffset
        var appOfs = customAppOffset

        if (espTabBar.currentIndex === 0) {
            var item = includedFwModel[selectedIncludedIndex]
            if (!item) {
                VescIf.emitMessageDialog(qsTr("Flash USB"), qsTr("No firmware package selected"), false, false)
                return
            }
            bl = item.bl
            part = item.part
            app = item.app
        } else {
            bl = customBlPath
            part = customPartPath
            app = customAppPath
        }

        if (app === "" || !Utility.fileExists(app)) {
            VescIf.emitMessageDialog(qsTr("Flash USB"), qsTr("Application binary not found"), false, false)
            return
        }

        if (typeof Esp32Flash !== "undefined") {
            isFlashing = true
            statusText = qsTr("Flashing bootloader...")
            var blData = Utility.readAllFromFile(bl)
            Esp32Flash.flashFirmware(blData, blOfs)

            statusText = qsTr("Flashing partition table...")
            var partData = Utility.readAllFromFile(part)
            Esp32Flash.flashFirmware(partData, partOfs)

            statusText = qsTr("Flashing application...")
            var appData = Utility.readAllFromFile(app)
            Esp32Flash.flashFirmware(appData, appOfs)

            statusText = qsTr("Flash Complete!")
            isFlashing = false
        } else {
            VescIf.emitMessageDialog(qsTr("Flash USB"), qsTr("Direct USB flashing is only available on native builds."), false, false)
        }
    }

    function flashBootloader() {
        var app = ""
        if (espTabBar.currentIndex === 0) {
            var item = includedFwModel[selectedIncludedIndex]
            if (!item) return
            app = item.app
        } else {
            app = customAppPath
        }

        if (app === "" || !Utility.fileExists(app)) {
            VescIf.emitMessageDialog(qsTr("Flash Bootloader"), qsTr("Application binary not found"), false, false)
            return
        }

        isFlashing = true
        statusText = qsTr("Uploading firmware to ESP via VESC...")
        VescIf.fwUploadFromFile(app, false, false)
    }

    // -------------------------------------------------------------------------
    // Signal Connections
    // -------------------------------------------------------------------------
    Connections {
        target: VescIf
        function onFwUploadStatus(status, progress, isOngoing) {
            isFlashing = isOngoing
            statusText = status
            progressValue = progress * 100.0
        }
        function onFwUploadFinished(success, message) {
            isFlashing = false
            statusText = message
        }
    }

    Component.onCompleted: {
        refreshPorts()
        scanIncludedFw()
    }
}
