/*
    Copyright 2022 - 2025 Benjamin Vedder	benjamin@vedder.se

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

Item {
    id: appPageItem
    property var dialogParent: ApplicationWindow.overlay
    property ConfigParams mAppConf: VescIf.appConfig()
    property Commands mCommands: VescIf.commands()

    function updateArchive() {
        disableDialog()
        workaroundTimerDl.start()
    }

    Timer {
        id: workaroundTimerDl
        interval: 0
        repeat: false
        running: false
        onTriggered: {
            reloadArchive()
        }
    }

    CodeLoader {
        id: mLoader
        Component.onCompleted: {
            mLoader.setVesc(VescIf)
        }
        onDownloadProgress: function(bytesReceived, bytesTotal) {
            if (bytesTotal > 0) {
                dlDialog.title = "Fetching... " + Number(100.0 * bytesReceived / bytesTotal).toFixed(0) + "%"
            } else {
                dlDialog.title = "Fetching... " + Number(bytesReceived / 1000).toFixed(1) + " kB"
            }
        }
        onPackageArchiveDownloaded: function(success) {
            reloadArchive()
            enableDialog()
        }
        onPackageInstallProgress: function(stepName, bytes, bytesTotal, percentage) {
            dlProgress.value = percentage
            dlDialog.title = stepName + " (" + Number(100.0 * percentage).toFixed(0) + "%)"
        }
        onPackageInstallFinished: function(success, message) {
            enableDialog()
            VescIf.emitMessageDialog("Install Package",
                                     message ? message : (success ? "Installation Done!" : "Installation failed"),
                                     success, false)
        }
        onPackageUninstallFinished: function(success, message) {
            enableDialog()
            VescIf.emitMessageDialog("Uninstall Package",
                                     message ? message : (success ? "Uninstallation Done!" : "Uninstallation failed"),
                                     success, false)
        }
    }

    Component.onCompleted: {
        reloadArchive()
    }

    property var allPkgs: []

    function reloadArchive() {
        pkgModel.clear()
        var pkgs = mLoader.reloadPackageArchive()
        var validPkgs = []

        for (var i = 0; i < pkgs.length; i++) {
            if (!pkgs[i].isLibrary && mLoader.shouldShowPackage(pkgs[i])) {
                validPkgs.push(pkgs[i])
                pkgModel.append({"pkgIndex": validPkgs.length - 1,
                                 "pkgName": pkgs[i].name,
                                 "pkgDescription": pkgs[i].description})
            }
        }
        allPkgs = validPkgs
        enableDialog()
    }

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.leftMargin: notchLeft
        anchors.rightMargin: notchRight

        ListModel {
            id: pkgModel
        }

        ListView {
            id: pkgList
            Layout.fillWidth: true
            Layout.fillHeight: true
            focus: true
            clip: true
            spacing: 5

            Component {
                id: pkgDelegate

                ImageButton {
                    id: connectButton
                    width: pkgList.width
                    height: 80

                    buttonText: pkgName
                    imageSrc: "qrc" + Utility.getThemePath() + "icons/Package-96.png"
                    onClicked: {
                        if (pkgIndex !== undefined && pkgIndex >= 0 && pkgIndex < allPkgs.length) {
                            openPkgDialog(allPkgs[pkgIndex])
                        }
                    }
                }
            }

            model: pkgModel
            delegate: pkgDelegate
        }

        RowLayout {
            Layout.fillWidth: true

            Button {
                text: "Update Archive"
                Layout.fillWidth: true
                onClicked: {
                    disableDialog()
                    workaroundTimerArchive.start()
                }

                Timer {
                    id: workaroundTimerArchive
                    interval: 0
                    repeat: false
                    running: false
                    onTriggered: {
                        if (!mLoader.downloadPackageArchive()) {
                            enableDialog()
                        }
                    }
                }
            }

            Button {
                text: "Uninstall Current"
                Layout.fillWidth: true
                onClicked: {
                    if (!VescIf.isPortConnected()) {
                        VescIf.emitMessageDialog("Uninstall Package", "Not Connected", false, false)
                        return
                    }
                    disableDialog()
                    dlDialog.title = "Uninstalling Package..."
                    mLoader.uninstallPackage()
                }
            }

            Button {
                Layout.preferredWidth: 50
                Layout.fillWidth: true
                text: "..."
                onClicked: menu.open()

                Menu {
                    id: menu
                    leftPadding: notchLeft
                    rightPadding: notchRight
                    parent: appPageItem
                    y: parent.height - implicitHeight
                    width: parent.width

                    MenuItem {
                        text: "Install from file..."
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
                                var pkg = mLoader.unpackVescPackageFromPath(selectedFile.toString())

                                if (!pkg.loadOk) {
                                    return
                                }

                                openPkgDialog(pkg)
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

    function openPkgDialog(pkg) {
        if (!pkg) {
            console.warn("openPkgDialog: pkg is null or undefined")
            return
        }
        var desc = pkg.description || ""
        var newlineIdx = desc.indexOf("\n")
        var line1 = newlineIdx >= 0 ? desc.slice(0, newlineIdx) : desc
        if (line1.toUpperCase().includes("<!DOCTYPE HTML PUBLIC")) {
            installFromPathText.text = desc
        } else {
            installFromPathText.text = Utility.md2html(desc)
        }

        installPkgCompatibleText.visible = !mLoader.shouldShowPackage(pkg)
        installFromPathDialog.currentPkg = pkg
        installFromPathDialog.open()
    }

    function disableDialog() {
        dlProgress.value = 0
        dlDialog.title = "Processing..."
        dlDialog.open()
        column.enabled = false
    }

    function enableDialog() {
        dlDialog.close()
        column.enabled = true
    }

    Dialog {
        id: dlDialog
        title: "Processing..."
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
            id: dlProgress
            anchors.fill: parent
            indeterminate: value <= 0 || value >= 1
            value: 0
        }
    }

    Dialog {
        title: "Install Package"

        id: installFromPathDialog
        property var currentPkg: null
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        Overlay.modal: Rectangle {
            color: "#AA000000"
        }

        width: parent.width - 20 - notchLeft - notchRight
        height: Math.min(implicitHeight, parent.height - 20)
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        parent: dialogParent

        ColumnLayout {
            anchors.fill: parent

            ScrollView {
                id: vescDialogScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth

                Text {
                    id: installFromPathText
                    color: Utility.getAppHexColor("lightText")
                    linkColor: {linkColor = Utility.getAppHexColor("lightAccent")}
                    verticalAlignment: Text.AlignVCenter
                    anchors.fill: parent
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    textFormat: Text.RichText
                    onLinkActivated: {
                        Qt.openUrlExternally(link)
                    }
                }
            }

            Text {
                id: installPkgCompatibleText
                visible: false
                color: Utility.getAppHexColor("red")
                verticalAlignment: Text.AlignVCenter
                Layout.fillWidth: true
                font.bold: true
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                text: "Warning: This package is NOT compatble with the connected device. " +
                      "Install it only if you know what you are doing!"
            }

            RowLayout {
                Layout.fillWidth: true

                Button {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 500
                    text: "Install Package"

                    onClicked: {
                        var p = installFromPathDialog.currentPkg
                        installFromPathDialog.close()
                        if (!p || !p.compressedData) {
                            VescIf.emitMessageDialog("Install Package", "Package data is missing.", false, false)
                            return
                        }
                        disableDialog()
                        dlDialog.title = "Preparing installation..."
                        var started = mLoader.installVescPackage(p.compressedData)
                        if (!started) {
                            enableDialog()
                        }
                    }
                }

                Button {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 500
                    text: "Cancel"

                    onClicked: {
                        installFromPathDialog.close()
                    }
                }
            }
        }
    }

    Connections {
        target: VescIf

        function onPortConnectedChanged() {
            if (!VescIf.isPortConnected()) {
                reloadArchive()
            }
        }

        function onCustomConfigLoadDone() {
            reloadArchive()
        }
    }
}
