/*
    Desktop DebugConsolePage — real-time debug and status message console.
    Displays timestamped status messages, firmware notifications, terminal output,
    and connection events with color-coding, filtering, auto-scroll, and copy support.
*/
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vedder.vesc

Item {
    id: debugConsolePage

    property Commands mCommands: VescIf.commands()
    property int maxLines: 5000
    property int trimLines: 1000
    property bool autoScroll: true
    property var allLogLines: []

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 6

        // Top Toolbar
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Image {
                source: "qrc" + Utility.getThemePath() + "icons/Bug-96.png"
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                fillMode: Image.PreserveAspectFit
                smooth: true
            }

            Label {
                text: qsTr("Debug Console")
                font.bold: true
                font.pointSize: 13
                color: Utility.getAppHexColor("lightText")
            }

            // Connection status pill
            Rectangle {
                Layout.preferredHeight: 22
                Layout.preferredWidth: connLabel.implicitWidth + 16
                radius: 11
                color: VescIf.isPortConnected() ? Qt.rgba(0.18, 0.55, 0.34, 0.3) : Qt.rgba(0.8, 0.2, 0.2, 0.3)
                border.color: VescIf.isPortConnected() ? "#4CAF50" : "#F44336"
                border.width: 1

                Label {
                    id: connLabel
                    anchors.centerIn: parent
                    text: VescIf.isPortConnected() ? qsTr("Connected") : qsTr("Disconnected")
                    font.pointSize: 9
                    font.bold: true
                    color: VescIf.isPortConnected() ? "#4CAF50" : "#F44336"
                }
            }

            Item { Layout.fillWidth: true }

            // Filter TextField
            TextField {
                id: filterInput
                Layout.preferredWidth: 220
                placeholderText: qsTr("Filter logs...")
                font.pointSize: 11
                selectByMouse: true
                onTextChanged: rebuildDisplayText()

                ToolButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    icon.source: "qrc" + Utility.getThemePath() + "icons/Cancel-96.png"
                    icon.width: 14; icon.height: 14
                    visible: filterInput.text !== ""
                    onClicked: filterInput.text = ""
                }
            }

            CheckBox {
                id: autoScrollBox
                text: qsTr("Auto-scroll")
                checked: true
                onCheckedChanged: debugConsolePage.autoScroll = checked
            }

            CheckBox {
                id: showTimestampsBox
                text: qsTr("Timestamps")
                checked: true
                onCheckedChanged: rebuildDisplayText()
            }

            Button {
                text: qsTr("Copy All")
                icon.source: "qrc" + Utility.getThemePath() + "icons/Copy-96.png"
                onClicked: {
                    consoleArea.selectAll()
                    consoleArea.copy()
                    consoleArea.deselect()
                    VescIf.emitStatusMessage(qsTr("Copied console output to clipboard"), true)
                }
            }

            Button {
                text: qsTr("Clear")
                icon.source: "qrc" + Utility.getThemePath() + "icons/Delete-96.png"
                onClicked: {
                    allLogLines = []
                    consoleArea.text = ""
                }
            }
        }

        // Console log viewer
        ScrollView {
            id: consoleScrollView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            TextArea {
                id: consoleArea
                readOnly: true
                font.family: "DejaVu Sans Mono"
                font.pointSize: 11
                wrapMode: TextEdit.NoWrap
                color: Utility.getAppHexColor("lightText")
                selectByMouse: true
                textFormat: TextEdit.RichText

                background: Rectangle {
                    color: Utility.getAppHexColor("darkBackground")
                    border.color: Utility.getAppHexColor("disabledText")
                    border.width: 1
                    radius: 3
                }
            }
        }
    }

    function formatTime() {
        var d = new Date()
        function pad(n) { return (n < 10 ? "0" : "") + n }
        return d.getFullYear() + "-" +
               pad(d.getMonth() + 1) + "-" +
               pad(d.getDate()) + " " +
               pad(d.getHours()) + ":" +
               pad(d.getMinutes()) + ":" +
               pad(d.getSeconds())
    }

    function appendLog(rawMsg, type) {
        if (!rawMsg || rawMsg.length === 0) return

        var timestamp = formatTime()
        var color = Utility.getAppHexColor("lightText")
        if (type === "good") {
            color = "#4CAF50"
        } else if (type === "bad") {
            color = "#FF5252"
        } else if (type === "info") {
            color = "#00A1E4"
        } else if (type === "warn") {
            color = "#FFA726"
        }

        // Escape basic HTML entities if present
        var escaped = rawMsg.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/\n/g, "<br>")

        var entry = {
            raw: rawMsg,
            time: timestamp,
            color: color,
            html: escaped
        }

        allLogLines.push(entry)
        if (allLogLines.length > maxLines) {
            allLogLines.splice(0, trimLines)
            rebuildDisplayText()
            return
        }

        // If filter is active, check match before appending
        var filter = filterInput.text.toLowerCase().trim()
        if (filter !== "" && rawMsg.toLowerCase().indexOf(filter) === -1) {
            return
        }

        var lineHtml = ""
        if (showTimestampsBox.checked) {
            lineHtml += "<span style='color:" + Utility.getAppHexColor("disabledText") + ";'>[" + timestamp + "] </span>"
        }
        lineHtml += "<span style='color:" + color + ";'>" + escaped + "</span><br>"

        consoleArea.append(lineHtml)

        if (debugConsolePage.autoScroll) {
            scrollToBottom()
        }
    }

    function rebuildDisplayText() {
        var filter = filterInput.text.toLowerCase().trim()
        var html = ""
        var showTs = showTimestampsBox.checked
        var tsColor = Utility.getAppHexColor("disabledText")

        for (var i = 0; i < allLogLines.length; i++) {
            var item = allLogLines[i]
            if (filter !== "" && item.raw.toLowerCase().indexOf(filter) === -1) {
                continue
            }
            if (showTs) {
                html += "<span style='color:" + tsColor + ";'>[" + item.time + "] </span>"
            }
            html += "<span style='color:" + item.color + ";'>" + item.html + "</span><br>"
        }

        consoleArea.text = html

        if (debugConsolePage.autoScroll) {
            scrollToBottom()
        }
    }

    function scrollToBottom() {
        var flickable = consoleArea.parent
        if (flickable && flickable.contentHeight !== undefined) {
            flickable.contentY = Math.max(0, flickable.contentHeight - flickable.height)
        }
    }

    Connections {
        target: VescIf
        function onStatusMessage(msg, isGood) {
            appendLog(msg, isGood ? "good" : "bad")
        }
        function onMessageDialog(title, msg, isGood, richText) {
            appendLog("[" + title + "] " + msg, isGood ? "good" : "bad")
        }
        function onPortConnectedChanged() {
            if (VescIf.isPortConnected()) {
                appendLog("Serial connected: " + VescIf.getConnectedPortName(), "good")
            } else {
                appendLog("Serial disconnected", "warn")
            }
        }
        function onFwUploadStatus(status, progress, isOngoing) {
            if (isOngoing) {
                appendLog("FW Progress: " + status + " (" + (progress * 100).toFixed(1) + "%)", "info")
            } else {
                appendLog("FW Status: " + status, "good")
            }
        }
    }

    Connections {
        target: mCommands
        function onPrintReceived(str) {
            appendLog(str.trim(), "info")
        }
    }

    Component.onCompleted: {
        appendLog("=== VESC® Tool Debug Console Started ===", "info")
        appendLog("Version: " + Utility.versionText(), "info")
        if (VescIf.isPortConnected()) {
            appendLog("Connected to: " + VescIf.getConnectedPortName(), "good")
        } else {
            appendLog("Port is currently disconnected", "warn")
        }
    }
}
