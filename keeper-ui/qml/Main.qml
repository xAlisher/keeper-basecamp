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
    property bool   pollBusy:       false
    property bool   bridgeRunning:  false
    property int    bridgePort:     7355
    property int    currentLibSlot: 0

    // Upload handoff: QML drives stash IPC (getClient is broken in C++)
    property var    pendingUpload:  null   // {id, file, path} while uploading
    property int    uploadAttempts: 0
    property var    beaconLogMap:   ({})   // cid → {txHash, slotFrom, libAtSubmit, status}

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

        // Current lib_slot for inscription progress bars
        var niRaw = callModuleParse(logos.callModule("logos_beacon", "getNodeInfo", []))
        if (niRaw && niRaw.lib_slot) root.currentLibSlot = niRaw.lib_slot

        // Bridge status
        var bRaw = callModuleParse(logos.callModule("keeper", "getBridgeStatus", []))
        if (bRaw) {
            root.bridgeRunning = bRaw.running === true
            if (bRaw.port) root.bridgePort = bRaw.port
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

        // Beacon log — build cid → inscription info map for keeper log display
        var bLogRaw = callModuleParse(logos.callModule("logos_beacon", "getInscriptionLog", []))
        var bMap = {}
        if (Array.isArray(bLogRaw)) {
            for (var bi = 0; bi < bLogRaw.length; bi++) {
                var be = bLogRaw[bi]
                if (be.cid) bMap[be.cid] = {
                    txHash:     be.inscriptionId || "",
                    slotFrom:   be.slotFrom      || 0,
                    libAtSubmit: be.libAtSubmit  || 0,
                    status:     be.status        || ""
                }
            }
        }
        root.beaconLogMap = bMap

        // Stash upload handoff: check for a pending upload from C++
        if (!root.pendingUpload) {
            var puRaw = callModuleParse(logos.callModule("keeper", "getPendingUpload", []))
            if (puRaw && puRaw.path) {
                root.pendingUpload  = puRaw
                root.uploadAttempts = 0
                logos.callModule("stash", "upload", [puRaw.path, "keeper"])
                stashPollTimer.start()
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
                var collCid = entry.collectionCid || ""
                var bEntry  = collCid ? (root.beaconLogMap[collCid] || {}) : {}
                var txHash  = bEntry.txHash || ""
                logModel.append({
                    entryTs:    entry.ts    || 0,
                    entryTitle: entry.title || entry.id || "",
                    entryCids:  cidList.join(", "),
                    entrySize:  fmtSize(entry.totalSize || 0),
                    entryCollectionCid: collCid,
                    entryTxHash: txHash,
                    entryExplorerUrl: txHash
                        ? "https://testnet.blockchain.logos.co/web/explorer/transactions/" + txHash
                        : "",
                    entryInscriptionStatus: bEntry.status || (txHash ? "confirmed" : (collCid ? "submitted" : "")),
                    entrySlotFrom:    bEntry.slotFrom    || 0,
                    entryLibAtSubmit: bEntry.libAtSubmit || 0
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

    // Polls stash getLatestLogosResult after triggering an upload
    Timer {
        id: stashPollTimer
        interval: 2000
        running: false
        repeat: true
        onTriggered: {
            if (!root.pendingUpload) { stop(); return }
            root.uploadAttempts++
            if (root.uploadAttempts > 60) {
                // Timeout after 120s — advance without CID
                var pu = root.pendingUpload
                root.pendingUpload = null
                stop()
                logos.callModule("keeper", "onUploadResult", [pu.id, pu.file, ""])
                return
            }
            var latestRaw = logos.callModule("stash", "getLatestLogosResult", [])
            var latest = root.callModuleParse(latestRaw)
            if (latest && latest.cid && latest.file) {
                var latestBase = latest.file.split("/").pop().split("\\").pop()
                var pendingBase = root.pendingUpload.path.split("/").pop().split("\\").pop()
                if (latestBase === pendingBase) {
                    var pu2 = root.pendingUpload
                    root.pendingUpload = null
                    stop()
                    logos.callModule("keeper", "onUploadResult", [pu2.id, pu2.file, latest.cid])
                }
            }
        }
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

            // Bridge status pill
            Rectangle {
                height: 28
                implicitWidth: bridgePillRow.implicitWidth + 20
                radius: 14
                color: Qt.rgba(0.149, 0.149, 0.149, 0.85)
                border.color: root.borderColor
                border.width: 1
                Layout.alignment: Qt.AlignVCenter

                RowLayout {
                    id: bridgePillRow
                    anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
                    spacing: 6

                    Rectangle {
                        width: 7; height: 7; radius: 4
                        Layout.alignment: Qt.AlignVCenter
                        color: root.bridgeRunning ? root.successGreen : root.errorRed
                    }

                    Text {
                        text: root.bridgeRunning ? ("Bridge :" + root.bridgePort) : "Bridge offline"
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

                    delegate: Item {
                        id: logDel
                        required property int    entryTs
                        required property string entryTitle
                        required property string entryCids
                        required property string entrySize
                        required property string entryCollectionCid
                        required property string entryTxHash
                        required property string entryExplorerUrl
                        required property string entryInscriptionStatus
                        required property int    entrySlotFrom
                        required property int    entryLibAtSubmit

                        width: logView.width
                        implicitHeight: logDelCol.implicitHeight + 4

                        property bool inscInFlight: entryCollectionCid !== "" && entryTxHash === ""
                        property bool inscDone:     entryTxHash !== ""

                        // Progress bar value: beacon lib advancing toward slotFrom
                        property real inscProgress: {
                            if (inscDone) return 1.0
                            if (entrySlotFrom <= 0 || entryLibAtSubmit <= 0) return 0.0
                            var total = entrySlotFrom - entryLibAtSubmit
                            if (total <= 0) return 0.0
                            var elapsed = root.currentLibSlot - entryLibAtSubmit
                            return Math.min(1.0, Math.max(0.0, elapsed / total))
                        }

                        property string inscTimeEst: {
                            if (inscDone || entrySlotFrom <= 0 || root.currentLibSlot <= 0) return ""
                            var rem = entrySlotFrom - root.currentLibSlot
                            if (rem <= 0) return ""
                            var mins = Math.floor(rem / 60)
                            var secs = rem % 60
                            return "~" + mins + ":" + (secs < 10 ? "0" : "") + secs
                        }

                        ColumnLayout {
                            id: logDelCol
                            anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: 2 }
                            spacing: 3

                            // Stash line
                            TextEdit {
                                Layout.fillWidth: true
                                text: {
                                    var t = "[" + fmtTime(entryTs) + "] Stash → Logos Storage: " + entryTitle
                                    if (entrySize) t += " (" + entrySize + ")"
                                    if (entryCids) t += ",  CIDs: " + entryCids
                                    return t
                                }
                                font.pixelSize: 11
                                font.family: "monospace"
                                color: root.textSecondary
                                wrapMode: TextEdit.Wrap
                                readOnly: true
                                selectByMouse: true
                            }

                            // Beacon line — only if an inscription exists
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                visible: entryCollectionCid !== ""

                                Text {
                                    text: "[" + fmtTime(entryTs) + "] Beacon → Logos Blockchain:"
                                    font.pixelSize: 11
                                    font.family: "monospace"
                                    color: root.textSecondary
                                }

                                // Confirmed: truncated hash
                                Text {
                                    visible: logDel.inscDone
                                    text: entryTxHash.substring(0, 16) + "…"
                                    font.pixelSize: 10
                                    font.family: "monospace"
                                    color: root.successGreen
                                }

                                // Copy URL button (confirmed)
                                Rectangle {
                                    visible: logDel.inscDone
                                    height: 18
                                    implicitWidth: kCopyLbl.implicitWidth + 14
                                    radius: 3
                                    color: kCopyArea.pressed      ? "#CC4000"
                                         : kCopyArea.containsMouse ? "#FF6B1A" : root.bgSecondary
                                    border.color: root.borderColor; border.width: 1

                                    Text {
                                        id: kCopyLbl
                                        anchors.centerIn: parent
                                        text: "copy URL"
                                        font.pixelSize: 9
                                        color: root.textPrimary
                                    }

                                    MouseArea {
                                        id: kCopyArea
                                        anchors.fill: parent
                                        hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            clipboard.text = entryExplorerUrl
                                            clipboard.forceActiveFocus()
                                            clipboard.selectAll()
                                            clipboard.copy()
                                        }
                                    }
                                }

                                // In-flight: progress bar
                                Rectangle {
                                    visible: logDel.inscInFlight
                                    Layout.fillWidth: true
                                    height: 4; radius: 2
                                    color: "#262626"

                                    Rectangle {
                                        width: parent.width * logDel.inscProgress
                                        height: parent.height; radius: parent.radius
                                        color: root.accentOrange
                                        Behavior on width { NumberAnimation { duration: 600 } }
                                    }
                                }

                                // Time estimate
                                Text {
                                    visible: logDel.inscInFlight && logDel.inscTimeEst.length > 0
                                    text: logDel.inscTimeEst
                                    font.pixelSize: 10
                                    color: root.textMuted
                                }

                                // Status label when no slot info yet
                                Text {
                                    visible: logDel.inscInFlight && entrySlotFrom <= 0
                                    text: {
                                        if (entryInscriptionStatus === "finalizing") return "finalizing…"
                                        if (entryInscriptionStatus === "submitted")  return "submitted…"
                                        return "queued…"
                                    }
                                    font.pixelSize: 10
                                    color: root.textMuted
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
