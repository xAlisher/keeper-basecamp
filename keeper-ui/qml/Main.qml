import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Item {
    id: root

    // ── Palette ───────────────────────────────────────────────────────────
    readonly property color bgPrimary:     "#171717"
    readonly property color bgSecondary:   "#262626"
    readonly property color bgActive:      "#332A27"
    readonly property color textPrimary:   "#FFFFFF"
    readonly property color textSecondary: "#A4A4A4"
    readonly property color textMuted:     "#5D5D5D"
    readonly property color accentOrange:  "#FF5000"
    readonly property color successGreen:  "#22C55E"
    readonly property color errorRed:      "#FB3748"
    readonly property color borderColor:   "#383838"

    // ── State ─────────────────────────────────────────────────────────────
    property bool   pollBusy: false
    property int    logSeenCount: 0

    // ── Helpers ───────────────────────────────────────────────────────────

    function callModuleParse(raw) {
        try {
            var tmp = JSON.parse(raw)
            if (typeof tmp === "string") {
                try { return JSON.parse(tmp) } catch(e) { return tmp }
            }
            return tmp
        } catch(e) { return null }
    }

    function statusIcon(status) {
        if (status === "done")      return "✓"
        if (status === "failed")    return "✗"
        if (status === "cancelled") return "–"
        if (status === "active")    return "●"
        return "○"
    }

    function statusColor(status) {
        if (status === "done")      return root.successGreen
        if (status === "failed")    return root.errorRed
        if (status === "cancelled") return root.textMuted
        if (status === "active")    return root.accentOrange
        return root.textSecondary
    }

    function refresh() {
        if (root.pollBusy) return
        root.pollBusy = true

        if (typeof logos === "undefined" || !logos.callModule) {
            root.pollBusy = false
            return
        }

        // Queue
        var qRaw = callModuleParse(logos.callModule("keeper", "getQueue", []))
        if (Array.isArray(qRaw)) {
            queueModel.clear()
            for (var i = 0; i < qRaw.length; i++) {
                var item = qRaw[i]
                var total = Array.isArray(item.files) ? item.files.length : 0
                var done  = 0
                if (Array.isArray(item.files)) {
                    for (var j = 0; j < item.files.length; j++) {
                        if (item.files[j].status === "done") done++
                    }
                }
                queueModel.append({
                    identifier: item.id       || item.identifier || "",
                    title:      item.title    || item.id         || "",
                    status:     item.status   || "queued",
                    error:      item.error    || "",
                    doneFiles:  done,
                    totalFiles: total
                })
            }
        }

        // Log (append-only)
        var lRaw = callModuleParse(logos.callModule("keeper", "getLog", []))
        if (Array.isArray(lRaw) && lRaw.length > root.logSeenCount) {
            for (var k = root.logSeenCount; k < lRaw.length; k++) {
                var entry = lRaw[k]
                if (logModel.count >= 200) logModel.remove(0)
                logModel.append({
                    cid:   entry.cid   || "",
                    label: entry.label || entry.file || ""
                })
            }
            root.logSeenCount = lRaw.length
        }

        root.pollBusy = false
    }

    // ── Timer ─────────────────────────────────────────────────────────────

    Timer {
        interval: 2000
        running: true
        repeat: true
        onTriggered: root.refresh()
    }

    Component.onCompleted: root.refresh()

    // ── Background ────────────────────────────────────────────────────────

    Rectangle {
        anchors.fill: parent
        color: root.bgPrimary
    }

    // ── Layout ────────────────────────────────────────────────────────────

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // ── Header ────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    text: "Keeper"
                    font.pixelSize: 20
                    font.bold: true
                    color: root.textPrimary
                }
                Text {
                    text: "Preserve Internet Archive items to Logos Storage with on-chain CID inscription."
                    font.pixelSize: 11
                    color: root.textSecondary
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
        }

        // ── Input row ─────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Rectangle {
                Layout.fillWidth: true
                height: 36
                radius: 6
                color: root.bgSecondary
                border.color: urlField.activeFocus ? root.accentOrange : root.borderColor
                border.width: 1

                TextField {
                    id: urlField
                    anchors.fill: parent
                    anchors.margins: 4
                    background: null
                    color: root.textPrimary
                    font.pixelSize: 12
                    placeholderText: "archive.org/details/… or bare identifier"
                    placeholderTextColor: root.textMuted
                    onAccepted: keepBtn.doKeep()
                }
            }

            Rectangle {
                id: keepBtn
                width: 64; height: 36
                radius: 6
                color: keepBtnArea.containsMouse ? "#CC4000" : root.accentOrange

                function doKeep() {
                    var val = urlField.text.trim()
                    if (!val) return
                    if (typeof logos === "undefined" || !logos.callModule) return
                    logos.callModule("keeper", "preserveItem", [val])
                    urlField.text = ""
                    root.refresh()
                }

                Text {
                    anchors.centerIn: parent
                    text: "Keep"
                    font.pixelSize: 13
                    font.bold: true
                    color: root.textPrimary
                }

                MouseArea {
                    id: keepBtnArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: keepBtn.doKeep()
                }
            }
        }

        // ── Queue ─────────────────────────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            Text {
                text: "Queue"
                font.pixelSize: 13
                font.bold: true
                color: root.textPrimary
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: Math.max(48, queueList.contentHeight + 16)
                height: implicitHeight
                radius: 6
                color: root.bgSecondary
                border.color: root.borderColor
                border.width: 1
                clip: true

                Text {
                    anchors.centerIn: parent
                    visible: queueModel.count === 0
                    text: "No items queued"
                    color: root.textMuted
                    font.pixelSize: 11
                }

                ListView {
                    id: queueList
                    anchors { fill: parent; margins: 8 }
                    clip: true
                    spacing: 4
                    model: ListModel { id: queueModel }

                    delegate: RowLayout {
                        required property string identifier
                        required property string title
                        required property string status
                        required property string error
                        required property int    doneFiles
                        required property int    totalFiles

                        width: queueList.width
                        spacing: 8

                        Text {
                            text: statusIcon(status)
                            font.pixelSize: 12
                            color: statusColor(status)
                            Layout.alignment: Qt.AlignVCenter
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                text: title || identifier
                                font.pixelSize: 12
                                color: root.textPrimary
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Text {
                                visible: status === "failed" && error.length > 0
                                text: error
                                font.pixelSize: 10
                                color: root.errorRed
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }

                        // File progress: "3 / 5"
                        Text {
                            visible: totalFiles > 0 && status !== "done" && status !== "failed"
                            text: doneFiles + " / " + totalFiles
                            font.pixelSize: 10
                            color: root.textMuted
                        }

                        // Cancel button (queued or active only)
                        Rectangle {
                            visible: status === "queued" || status === "active"
                            width: 20; height: 20
                            radius: 4
                            color: cancelArea.containsMouse ? "#3A2020" : "transparent"

                            Text {
                                anchors.centerIn: parent
                                text: "✕"
                                font.pixelSize: 10
                                color: root.textMuted
                            }

                            MouseArea {
                                id: cancelArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (typeof logos === "undefined" || !logos.callModule) return
                                    logos.callModule("keeper", "cancelItem", [identifier])
                                    root.refresh()
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── Log ───────────────────────────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6

            Text {
                text: "Inscription log"
                font.pixelSize: 13
                font.bold: true
                color: root.textPrimary
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 6
                color: "#0D0D0D"
                border.color: root.borderColor
                border.width: 1
                clip: true

                Text {
                    anchors.centerIn: parent
                    visible: logModel.count === 0
                    text: "No inscriptions yet"
                    color: root.textMuted
                    font.pixelSize: 11
                    font.family: "Courier New, monospace"
                }

                ListView {
                    id: logView
                    anchors { fill: parent; margins: 10 }
                    clip: true
                    spacing: 2
                    onCountChanged: Qt.callLater(() => logView.positionViewAtEnd())
                    model: ListModel { id: logModel }

                    delegate: RowLayout {
                        required property string cid
                        required property string label

                        width: logView.width
                        spacing: 8

                        Text {
                            text: cid.length > 12 ? cid.slice(0, 8) + "…" + cid.slice(-4) : cid
                            font.pixelSize: 11
                            font.family: "Courier New, monospace"
                            color: root.successGreen
                        }

                        Text {
                            text: label
                            font.pixelSize: 11
                            font.family: "Courier New, monospace"
                            color: root.textSecondary
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }
    }
}
