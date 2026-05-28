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
    property bool   pollBusy:        false
    property string lmStatus:        "offline"
    property bool   lmReady:         false
    property int    lmPairedCount:   0

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

    function fmtTime(ms) {
        return Qt.formatTime(new Date(ms), "hh:mm:ss")
    }

    function fmtSize(bytes) {
        if (!bytes || bytes <= 0)    return ""
        if (bytes >= 1073741824)     return (bytes / 1073741824).toFixed(1) + "G"
        if (bytes >= 1048576)        return (bytes / 1048576).toFixed(1) + "M"
        if (bytes >= 1024)           return (bytes / 1024).toFixed(1) + "K"
        return bytes + "B"
    }

    function fmtCid(cid) {
        if (!cid || cid.length <= 16) return cid || ""
        return cid.slice(0, 8) + "…" + cid.slice(-4)
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

        // Logos Messaging status
        var wRaw = callModuleParse(logos.callModule("keeper", "getLogosMsgStatus", []))
        if (wRaw) {
            root.lmStatus      = wRaw.status      || "offline"
            root.lmReady       = wRaw.ready       === true
            root.lmPairedCount = wRaw.pairedCount || 0
        }

        // Paired extensions
        var pRaw = callModuleParse(logos.callModule("keeper", "getPairedExtensions", []))
        if (Array.isArray(pRaw)) {
            pairedModel.clear()
            for (var pi = 0; pi < pRaw.length; pi++)
                pairedModel.append({ pubkey: pRaw[pi] })
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

        // Log — full rebuild each poll so txHash updates are picked up
        var lRaw = callModuleParse(logos.callModule("keeper", "getLog", []))
        if (Array.isArray(lRaw)) {
            logModel.clear()
            for (var k = 0; k < lRaw.length; k++) {
                var entry = lRaw[k]
                var cidList = []
                if (Array.isArray(entry.files)) {
                    for (var fi = 0; fi < entry.files.length; fi++) {
                        var c = entry.files[fi].cid
                        if (c) cidList.push(fmtCid(c))
                    }
                }
                var txHash = entry.txHash || ""
                logModel.append({
                    entryTs:    entry.ts    || 0,
                    entryTitle: entry.title || entry.id || "",
                    entryCids:  cidList.join(", "),
                    entrySize:  fmtSize(entry.totalSize || 0),
                    entryCollectionCid: entry.collectionCid || "",
                    entryTxHash: txHash,
                    entryExplorerUrl: txHash
                        ? "https://testnet.blockchain.logos.co/web/explorer/transactions/" + txHash
                        : ""
                })
            }
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

    // Clipboard helper — zero-opacity TextEdit used to copy text to clipboard
    // Must NOT be visible:false — invisible items can't receive focus needed for copy()
    TextEdit {
        id: clipboard
        opacity: 0
        width: 1; height: 1
    }

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

            // Logos Messaging status pill
            Rectangle {
                height: 28
                implicitWidth: lmPillRow.implicitWidth + 20
                radius: 14
                color: Qt.rgba(0.149, 0.149, 0.149, 0.85)
                border.color: root.borderColor
                border.width: 1
                Layout.alignment: Qt.AlignVCenter

                RowLayout {
                    id: lmPillRow
                    anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
                    spacing: 6

                    Rectangle {
                        width: 7; height: 7; radius: 4
                        Layout.alignment: Qt.AlignVCenter
                        color: root.lmReady ? root.successGreen
                             : (root.lmStatus === "CONNECTING" ? "#F59E0B" : root.errorRed)
                    }

                    Text {
                        text: root.lmReady ? "Logos Messaging"
                            : ("Logos Messaging " + root.lmStatus.toLowerCase())
                        font.pixelSize: 11
                        color: root.textPrimary
                    }
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

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: "Queue"
                    font.pixelSize: 13
                    font.bold: true
                    color: root.textPrimary
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: "Clear"
                    font.pixelSize: 11
                    color: clearQueueArea.containsMouse ? root.textSecondary : root.textMuted
                    visible: queueModel.count > 0

                    MouseArea {
                        id: clearQueueArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (typeof logos === "undefined" || !logos.callModule) return
                            logos.callModule("keeper", "clearQueue", [])
                            root.refresh()
                        }
                    }
                }
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

        // ── Paired Extensions ─────────────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            Text {
                text: "Paired Extensions"
                font.pixelSize: 13
                font.bold: true
                color: root.textPrimary
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Rectangle {
                    Layout.fillWidth: true
                    height: 32
                    radius: 6
                    color: root.bgSecondary
                    border.color: pairField.activeFocus ? root.accentOrange : root.borderColor
                    border.width: 1

                    TextField {
                        id: pairField
                        anchors.fill: parent
                        anchors.margins: 4
                        background: null
                        color: root.textPrimary
                        font.pixelSize: 11
                        placeholderText: "64-char hex pubkey from extension popup"
                        placeholderTextColor: root.textMuted
                        onAccepted: addPairBtn.doPair()
                    }
                }

                Rectangle {
                    id: addPairBtn
                    width: 52; height: 32
                    radius: 6
                    color: addPairArea.containsMouse ? "#CC4000" : root.accentOrange

                    function doPair() {
                        var val = pairField.text.trim()
                        if (!val) return
                        if (typeof logos === "undefined" || !logos.callModule) return
                        logos.callModule("keeper", "addPairedExtension", [val])
                        pairField.text = ""
                        root.refresh()
                    }

                    Text {
                        anchors.centerIn: parent
                        text: "Pair"
                        font.pixelSize: 12
                        font.bold: true
                        color: root.textPrimary
                    }

                    MouseArea {
                        id: addPairArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: addPairBtn.doPair()
                    }
                }
            }

            // Paired list — only visible when non-empty
            Rectangle {
                visible: pairedModel.count > 0
                Layout.fillWidth: true
                implicitHeight: pairedList.contentHeight + 16
                radius: 6
                color: root.bgSecondary
                border.color: root.borderColor
                border.width: 1
                clip: true

                ListView {
                    id: pairedList
                    anchors { fill: parent; margins: 8 }
                    clip: true
                    spacing: 4
                    model: ListModel { id: pairedModel }

                    delegate: RowLayout {
                        required property string pubkey
                        width: pairedList.width
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            text: pubkey.slice(0, 8) + "\u2026" + pubkey.slice(-8)
                            font.pixelSize: 11
                            font.family: "monospace"
                            color: root.textSecondary
                        }

                        Text {
                            text: "remove"
                            font.pixelSize: 10
                            color: removeArea.containsMouse ? root.errorRed : root.textMuted

                            MouseArea {
                                id: removeArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (typeof logos === "undefined" || !logos.callModule) return
                                    logos.callModule("keeper", "removePairedExtension", [pubkey])
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

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: "Log"
                    font.pixelSize: 13
                    font.bold: true
                    color: root.textPrimary
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: "Clear"
                    font.pixelSize: 11
                    color: clearArea.containsMouse ? root.textSecondary : root.textMuted
                    visible: logModel.count > 0

                    MouseArea {
                        id: clearArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (typeof logos === "undefined" || !logos.callModule) return
                            logos.callModule("keeper", "clearLog", [])
                            root.refresh()
                        }
                    }
                }
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
                    text: "No entries yet"
                    color: root.textMuted
                    font.pixelSize: 11
                }

                ListView {
                    id: logView
                    anchors { fill: parent; margins: 10 }
                    clip: true
                    spacing: 6
                    onCountChanged: Qt.callLater(() => logView.positionViewAtEnd())
                    model: ListModel { id: logModel }

                    delegate: Column {
                        required property int    entryTs
                        required property string entryTitle
                        required property string entryCids
                        required property string entrySize
                        required property string entryCollectionCid
                        required property string entryTxHash
                        required property string entryExplorerUrl

                        width: logView.width
                        spacing: 1

                        // Stash line
                        TextEdit {
                            width: parent.width
                            text: {
                                var t = "[" + fmtTime(entryTs) + "] Stash → Logos Storage: " + entryTitle
                                if (entrySize)  t += " (" + entrySize + ")"
                                if (entryCids)  t += ", CIDs: " + entryCids
                                return t
                            }
                            font.pixelSize: 11
                            font.family: "monospace"
                            color: root.textSecondary
                            wrapMode: TextEdit.Wrap
                            readOnly: true
                            selectByMouse: true
                        }

                        // Beacon line
                        RowLayout {
                            width: parent.width
                            spacing: 6
                            visible: entryExplorerUrl !== "" || entryCollectionCid !== ""

                            TextEdit {
                                Layout.fillWidth: true
                                wrapMode: TextEdit.WrapAnywhere
                                readOnly: true
                                selectByMouse: true
                                font.pixelSize: 11
                                font.family: "monospace"
                                textFormat: TextEdit.RichText
                                text: {
                                    var prefix = "[" + fmtTime(entryTs) + "] Beacon → Logos Blockchain: "
                                    var url = entryExplorerUrl
                                    var muted = root.textMuted
                                    var secondary = root.textSecondary
                                    var green = root.successGreen
                                    if (url)
                                        return "<span style='color:" + secondary + "'>" + prefix + "</span>"
                                             + "<span style='color:" + green + "'>" + url + "</span>"
                                    if (entryCollectionCid)
                                        return "<span style='color:" + secondary + "'>" + prefix + "</span>"
                                             + "<span style='color:" + muted + "'>confirming\u2026</span>"
                                    return ""
                                }
                            }

                            Text {
                                id: copyBtn
                                visible: entryExplorerUrl !== ""
                                text: "copy"
                                font.pixelSize: 10
                                color: copyBtnArea.containsMouse ? root.textPrimary : root.textMuted
                                Layout.alignment: Qt.AlignVCenter

                                MouseArea {
                                    id: copyBtnArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        clipboard.text = entryExplorerUrl
                                        clipboard.forceActiveFocus()
                                        clipboard.selectAll()
                                        clipboard.copy()
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
