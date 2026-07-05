import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Logos.Theme // logos-design-system (native on RC3+ Basecamp) — skill: logos-design-system-adoption
import Logos.Controls

Item {
    id: root

    // ── Palette ───────────────────────────────────────────────────────────
    readonly property color bgPrimary:     Theme.palette.background
    readonly property color bgSecondary:   Theme.palette.backgroundSecondary
    readonly property color bgActive:      Theme.palette.surface
    readonly property color textPrimary:   Theme.palette.text
    readonly property color textSecondary: Theme.palette.textSecondary
    readonly property color textMuted:     Theme.palette.textMuted
    readonly property color accentOrange:  Theme.palette.primary
    readonly property color successGreen:  Theme.palette.success
    readonly property color errorRed:      Theme.palette.error
    readonly property color borderColor:   Theme.palette.border

    // ── State ─────────────────────────────────────────────────────────────
    property bool   bridgeRunning:  false
    property int    bridgePort:     7355
    property int    currentLibSlot: 0

    // Upload handoff: the C++ QtRO backend drives the legacy stash upload async
    // (a sync modules().stash.* call would deadlock the ui-host loop); QML only
    // kicks it via keeperUi.startUpload() and reacts to the lastUpload PROP.
    property var    pendingUpload:  null   // {id, file, path} while uploading
    property var    beaconLogMap:   ({})   // cid → {txHash, slotFrom, libAtSubmit, status}
    property string explorerUrl:    "https://logosblocks.noders.services"
    property string keeperChannel:  ""   // keeper's per-module channel (beacon.getSourceChannel)
    property string inscriptionFormat: "collection"   // "collection" (IA-Archiver) | "cid" (Legacy)
    property bool   configLoaded:      false
    property bool   settingsOpen:      false
    property bool   explorerUrlLoaded: false

    // ── keeper_ui QtRO backend (v0.2 universal) ─────────────────────────────
    // keeper + logos_beacon are now Qt-free universal modules, so legacy
    // logos.callModule("keeper"/"logos_beacon", …) returns "null". We reach them
    // (and drive the legacy stash upload async) through keeper_ui's own C++
    // backend via logos.module("keeper_ui") + logos.watch(...).
    readonly property var keeperUi: (typeof logos !== "undefined" && logos.module)
                                    ? logos.module("keeper_ui") : null
    property bool keeperReady: false

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

    // Rebuild the keeper log model using the current beaconLogMap. Called after
    // getLog resolves (async); kept a separate fn so a fresh beaconLogMap re-renders.
    function rebuildLog(lRaw) {
        if (!Array.isArray(lRaw)) return
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
                entryExplorerUrl: (txHash && root.keeperChannel.length > 0)
                    ? "https://explorer.logos.live/#" + root.keeperChannel
                    : "",
                entryInscriptionStatus: bEntry.status || (txHash ? "confirmed" : (collCid ? "submitted" : "")),
                entrySlotFrom:    bEntry.slotFrom    || 0,
                entryLibAtSubmit: bEntry.libAtSubmit || 0
            })
        }
    }

    function refresh() {
        if (!root.keeperUi) return

        // Current lib_slot for inscription progress bars (logos_beacon, sync forward)
        logos.watch(root.keeperUi.getNodeInfo(), function (raw) {
            var niRaw = callModuleParse(raw)
            if (niRaw && niRaw.lib_slot) root.currentLibSlot = niRaw.lib_slot
        }, function () {})

        // Explorer base URL from beacon config (once)
        if (!root.explorerUrlLoaded) {
            logos.watch(root.keeperUi.getBeaconConfig(), function (raw) {
                var bcRaw = callModuleParse(raw)
                if (bcRaw && bcRaw.explorerUrl) {
                    root.explorerUrl = bcRaw.explorerUrl
                    root.explorerUrlLoaded = true
                }
            }, function () {})
        }

        // Bridge status (keeper, sync forward)
        logos.watch(root.keeperUi.refreshStashStatus(), function () {}, function () {})

        if (root.keeperChannel.length === 0 && root.keeperUi) {
            logos.watch(root.keeperUi.getKeeperChannel(), function (raw) {
                var ch = callModuleParse(raw)
                if (typeof ch === "string" && /^[0-9a-f]{64}$/.test(ch)) root.keeperChannel = ch
            }, function () {})
        }

        logos.watch(root.keeperUi.getBridgeStatus(), function (raw) {
            var bRaw = callModuleParse(raw)
            if (bRaw) {
                root.bridgeRunning = bRaw.running === true
                if (bRaw.port) root.bridgePort = bRaw.port
            }
        }, function () {})

        // Queue (keeper, sync forward)
        if (!root.configLoaded) {
            logos.watch(root.keeperUi.getConfig(), function (raw) {
                var c = callModuleParse(raw)
                if (c && c.inscriptionFormat) { root.inscriptionFormat = c.inscriptionFormat; root.configLoaded = true }
            }, function () {})
        }

        logos.watch(root.keeperUi.getQueue(), function (raw) {
            var qRaw = callModuleParse(raw)
            if (!Array.isArray(qRaw)) return
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
        }, function () {})

        // Beacon log — build cid → inscription info map for keeper log display
        // (logos_beacon, sync forward). Rebuild the log after this so txHash lands.
        logos.watch(root.keeperUi.getInscriptionLog(), function (raw) {
            var bLogRaw = callModuleParse(raw)
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

            // Log — full rebuild each poll so txHash updates are picked up (keeper)
            logos.watch(root.keeperUi.getLog(), function (lraw) {
                root.rebuildLog(callModuleParse(lraw))
            }, function () {})
        }, function () {})

        // Stash upload handoff: hand a pending upload to the backend, which drives
        // the legacy stash upload async and surfaces the CID via the lastUpload PROP.
        if (!root.pendingUpload) {
            logos.watch(root.keeperUi.getPendingUpload(), function (raw) {
                var puRaw = callModuleParse(raw)
                if (puRaw && puRaw.path && !root.pendingUpload) {
                    root.pendingUpload = puRaw
                    logos.watch(root.keeperUi.startUpload(puRaw.path, puRaw.id, puRaw.file),
                                function () {}, function () {})
                }
            }, function () {})
        }
    }

    // ── Timer ─────────────────────────────────────────────────────────────

    Timer {
        interval: 2000
        running: true
        repeat: true
        onTriggered: root.refresh()
    }

    // The backend owns the stash upload poll now (it drives stash async and
    // fires keeper.onUploadResult on a CID match). QML only reacts to the
    // lastUpload PROP: {id,file,cid} on success or {id,file,error} on timeout —
    // either way, clear pendingUpload so the next getPendingUpload can hand off.
    Connections {
        target: root.keeperUi
        ignoreUnknownSignals: true
        function onLastUploadChanged() {
            if (!root.keeperUi || !root.keeperUi.lastUpload) return
            var r = root.callModuleParse(root.keeperUi.lastUpload)
            if (!r || !r.id) return
            root.pendingUpload = null   // keeper already recorded the CID backend-side
            root.refresh()
        }
    }

    // Universal-module readiness gate: the backend's QtRO context must be wired
    // before modules().keeper/.logos_beacon calls succeed.
    Connections {
        target: logos
        ignoreUnknownSignals: true
        function onViewModuleReadyChanged(moduleName, isReady) {
            if (moduleName === "keeper_ui") {
                root.keeperReady = isReady && root.keeperUi !== null
                if (root.keeperReady) root.refresh()
            }
        }
    }

    Component.onCompleted: {
        if (typeof logos === "undefined" || !logos.module) return
        if (root.keeperUi !== null && logos.isViewModuleReady("keeper_ui")) {
            root.keeperReady = true
            root.refresh()
        }
    }

    function copyLog() {
        var out = ""
        for (var i = 0; i < logModel.count; i++) {
            var u = logModel.get(i).entryExplorerUrl
            if (u) out += u + "\n"
        }
        clipboard.text = out
        clipboard.forceActiveFocus(); clipboard.selectAll(); clipboard.copy()
    }

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
                LogosText {
                    text: "Keeper"
                    font.pixelSize: Theme.typography.panelTitleText
                    font.weight: Theme.typography.weightBold
                    color: root.textPrimary
                }
                LogosText {
                    text: "Preserve Internet Archive items to Logos Storage with on-chain CID inscription."
                    font.pixelSize: Theme.typography.secondaryText
                    color: root.textSecondary
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }

            // Bridge status pill — DS badge (green=running / red=offline)
            LogosBadge {
                id: bridgeBadge
                Layout.alignment: Qt.AlignVCenter
                text: root.bridgeRunning ? ("bridge :" + root.bridgePort) : "bridge offline"
                color: root.bridgeRunning ? Theme.palette.success : Theme.palette.error
            }

            // Stash: Storage status — bound to the backend's stashStatus PROP (async-polled)
            LogosBadge {
                Layout.alignment: Qt.AlignVCenter
                readonly property string ss: root.keeperUi ? root.keeperUi.stashStatus : "offline"
                text: ss === "ready"    ? "stash: storage ready"
                    : ss === "starting" ? "stash: storage starting"
                    :                     "stash not launched"
                color: ss === "ready"    ? Theme.palette.success
                     : ss === "starting" ? Theme.palette.warning
                     :                     root.textMuted
            }

            // cogwheel — settings toggle (receiver pattern; no gear asset ships)
            Rectangle {
                implicitWidth: bridgeBadge.implicitHeight; implicitHeight: bridgeBadge.implicitHeight
                radius: Theme.spacing.radiusSmall
                Layout.alignment: Qt.AlignVCenter
                color: gearArea.containsMouse ? root.bgSecondary : "transparent"
                border.color: root.settingsOpen ? Theme.palette.primary : root.borderColor; border.width: 1
                LogosText {
                    anchors.fill: parent
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    text: "\u2699"
                    font.pixelSize: Theme.typography.primaryText
                    color: root.settingsOpen ? Theme.palette.primary : root.textSecondary
                }
                MouseArea {
                    id: gearArea; anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.settingsOpen = !root.settingsOpen
                }
            }
        }


        // ── Settings pane (cogwheel) ──────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            visible: root.settingsOpen
            implicitHeight: setCol.implicitHeight + Theme.spacing.large
            color: root.bgSecondary; radius: Theme.spacing.radiusMedium
            border.color: root.borderColor; border.width: 1

            ColumnLayout {
                id: setCol
                anchors { top: parent.top; left: parent.left; right: parent.right; margins: Theme.spacing.small }
                spacing: Theme.spacing.small

                LogosText {
                    text: "Inscription format"
                    color: root.textSecondary
                    font.pixelSize: Theme.typography.secondaryText
                    font.weight: Theme.typography.weightBold
                }
                LogosComboBox {
                    id: formatBox
                    Layout.fillWidth: true
                    model: ["CID-based — CIDs on Blockchain (Legacy)",
                            "Collection ID — CIDs stay in Storage (IA-Archiver)"]
                    currentIndex: root.inscriptionFormat === "cid" ? 0 : 1
                    onActivated: function (index) {
                        var fmt = index === 0 ? "cid" : "collection"
                        root.inscriptionFormat = fmt
                        if (root.keeperUi)
                            logos.watch(root.keeperUi.setConfig(JSON.stringify({ inscriptionFormat: fmt })),
                                        function () {}, function () {})
                    }
                }
                LogosText {
                    text: "CID-based inscribes every file CID on-chain (legacy). Collection ID inscribes one key — CIDs stay in Storage."
                    color: root.textMuted
                    font.pixelSize: Theme.typography.badgeText
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
        }

        // ── Input row ─────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            LogosTextField {
                id: urlField
                Layout.fillWidth: true
                placeholderText: "archive.org/details/… or bare identifier"
                // LogosTextField exposes no `accepted` signal (unlike a plain TextField) —
                // wiring onAccepted directly is a hard QML load crash. Bind Enter via Keys.
                Keys.onReturnPressed: keepBtn.doKeep()
                Keys.onEnterPressed: keepBtn.doKeep()
            }

            LogosButton {
                id: keepBtn
                text: "Keep"
                implicitWidth: 64
                implicitHeight: 40

                function doKeep() {
                    var val = urlField.text.trim()
                    if (!val) return
                    if (!root.keeperUi) return
                    logos.watch(root.keeperUi.preserveItem(val),
                                function () { root.refresh() }, function () {})
                    urlField.text = ""
                }

                onClicked: keepBtn.doKeep()
            }
        }

        // ── Queue ─────────────────────────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                LogosText {
                    text: "Queue"
                    font.pixelSize: Theme.typography.primaryText
                    font.weight: Theme.typography.weightBold
                    color: root.textPrimary
                }

                Item { Layout.fillWidth: true }

                LogosButton {
                    text: "Clear"
                    visible: queueModel.count > 0
                    onClicked: {
                        if (!root.keeperUi) return
                        logos.watch(root.keeperUi.clearQueue(),
                                    function () { root.refresh() }, function () {})
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

                LogosText {
                    anchors.centerIn: parent
                    visible: queueModel.count === 0
                    text: "No items queued"
                    color: root.textMuted
                    font.pixelSize: Theme.typography.secondaryText
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

                        LogosText {
                            text: statusIcon(status)
                            font.pixelSize: Theme.typography.secondaryText
                            color: statusColor(status)
                            Layout.alignment: Qt.AlignVCenter
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            LogosText {
                                text: title || identifier
                                font.pixelSize: Theme.typography.secondaryText
                                color: root.textPrimary
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            LogosText {
                                visible: status === "failed" && error.length > 0
                                text: error
                                font.pixelSize: Theme.typography.badgeText
                                color: root.errorRed
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }

                        // File progress: "3 / 5"
                        LogosText {
                            visible: totalFiles > 0 && status !== "done" && status !== "failed"
                            text: doneFiles + " / " + totalFiles
                            font.pixelSize: Theme.typography.badgeText
                            color: root.textMuted
                        }

                        // Cancel button (queued or active only)
                        LogosButton {
                            visible: status === "queued" || status === "active"
                            text: "✕"
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            implicitHeight: 24
                            onClicked: {
                                if (!root.keeperUi) return
                                logos.watch(root.keeperUi.cancelItem(identifier),
                                            function () { root.refresh() }, function () {})
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

                LogosText {
                    text: "Log"
                    font.pixelSize: Theme.typography.primaryText
                    font.weight: Theme.typography.weightBold
                    color: root.textPrimary
                }

                Item { Layout.fillWidth: true }

                LogosButton {
                    text: "\u29C9"                      // copy-all glyph
                    visible: logModel.count > 0
                    implicitWidth: 28; implicitHeight: 28
                    Layout.preferredWidth: 28; Layout.preferredHeight: 28
                    radius: 14                           // circular
                    onClicked: root.copyLog()
                }
                LogosButton {
                    text: "\uD83D\uDDD1"               // trash (clear)
                    visible: logModel.count > 0
                    implicitWidth: 28; implicitHeight: 28
                    Layout.preferredWidth: 28; Layout.preferredHeight: 28
                    radius: 14                           // circular
                    onClicked: {
                        if (!root.keeperUi) return
                        logos.watch(root.keeperUi.clearLog(),
                                    function () { root.refresh() }, function () {})
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 6
                color: root.bgPrimary
                border.color: root.borderColor
                border.width: 1
                clip: true

                LogosText {
                    anchors.centerIn: parent
                    visible: logModel.count === 0
                    text: "No entries yet"
                    color: root.textMuted
                    font.pixelSize: Theme.typography.secondaryText
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

                                LogosText {
                                    text: "[" + fmtTime(entryTs) + "] Beacon → Logos Blockchain:"
                                    font.pixelSize: Theme.typography.secondaryText
                                    font.family: "monospace"
                                    color: root.textSecondary
                                }

                                // Confirmed: truncated hash
                                LogosText {
                                    visible: logDel.inscDone
                                    text: entryTxHash.substring(0, 16) + "…"
                                    font.pixelSize: Theme.typography.badgeText
                                    font.family: "monospace"
                                    color: root.successGreen
                                }

                                // Copy URL button (confirmed)
                                LogosButton {
                                    visible: logDel.inscDone
                                    text: "copy URL"
                                    Layout.preferredHeight: 20
                                    implicitHeight: 20
                                    onClicked: {
                                        clipboard.text = entryExplorerUrl
                                        clipboard.forceActiveFocus()
                                        clipboard.selectAll()
                                        clipboard.copy()
                                    }
                                }

                                // In-flight: progress bar
                                Rectangle {
                                    visible: logDel.inscInFlight
                                    Layout.fillWidth: true
                                    height: 4; radius: 2
                                    color: root.borderColor

                                    Rectangle {
                                        width: parent.width * logDel.inscProgress
                                        height: parent.height; radius: parent.radius
                                        color: root.accentOrange
                                        Behavior on width { NumberAnimation { duration: 600 } }
                                    }
                                }

                                // Time estimate
                                LogosText {
                                    visible: logDel.inscInFlight && logDel.inscTimeEst.length > 0
                                    text: logDel.inscTimeEst
                                    font.pixelSize: Theme.typography.badgeText
                                    color: root.textMuted
                                }

                                // Status label when no slot info yet
                                LogosText {
                                    visible: logDel.inscInFlight && entrySlotFrom <= 0
                                    text: {
                                        if (entryInscriptionStatus === "finalizing") return "finalizing…"
                                        if (entryInscriptionStatus === "submitted")  return "submitted…"
                                        return "queued…"
                                    }
                                    font.pixelSize: Theme.typography.badgeText
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
