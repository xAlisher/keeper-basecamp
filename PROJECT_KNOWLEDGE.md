# keeper-basecamp — Project Knowledge

Accumulated lessons specific to this codebase. See `docs/retro-log.md` for raw captures.
Platform-wide patterns live in `~/basecamp/basecamp-skills/skills/`.

---

## IPC Architecture

### getClient() is permanently broken for keeper

`logosAPI->getClient("stash")` and `logosAPI->getClient("logos_beacon")` **always**
throw `std::bad_alloc` inside `new QRemoteObjectNode()` in the keeper plugin process —
even with 20s/45s init timers, even when both modules are loaded. Root cause: Qt QRO
allocator state is corrupted globally at platform startup by other module activity.
Each failed call degrades state further. No timing or retry strategy fixes this.

**Rule: keeper C++ never calls `getClient()`. Use QML-mediated IPC only.**

See also: `ipc-client-eager-init` recipe in basecamp-skills (permanent failure mode section).

### QML-mediated stash upload (confirmed working 2026-06-07)

C++ sets `pendingUpload_` struct and returns. QML polls it every 2s:

```cpp
// C++: keeper_plugin.cpp
struct PendingUpload {
    QString identifier;
    QString fileName;
    QString localPath;
    bool    active = false;
} pendingUpload_;

void KeeperPlugin::uploadToStash(...) {
    pendingUpload_ = {identifier, fileName, localPath, true};
    saveQueue();
}

Q_INVOKABLE QString getPendingUpload() const;
Q_INVOKABLE QString onUploadResult(const QString& id, const QString& file, const QString& cid);
```

```qml
// QML: Main.qml — stash upload flow
Timer {
    id: stashPollTimer; interval: 2000; repeat: true
    onTriggered: {
        var res = callModuleParse(logos.callModule("stash", "getLatestLogosResult", []))
        if (res && res.cid && res.file === root.pendingUpload.file) {
            logos.callModule("keeper", "onUploadResult", [id, file, res.cid])
            stashPollTimer.stop()
        }
    }
}
// In refresh(): check getPendingUpload() → call stash.upload if active
```

### Beacon inscription tracking via QML (confirmed working 2026-06-07)

Keeper QML reads `logos_beacon.getInscriptionLog()` directly in `refresh()`, builds
`beaconLogMap` keyed by CID. The actual inscription happens via the file-based
`inscriptionQueue_` that keeper C++ writes and beacon QML polls.

No keeper-to-beacon direct IPC is needed.

---

## Build & Install

### Never use `lgpm install` for keeper

`lgpm install` produces a manifest with `hashes` field and `linux-amd64-dev` variant.
The platform rejects modules with `hashes` in manifest.json (registry check fails).

Always use the manual install recipe from `memory/keeper_debug_notes.md`:
```bash
nix build .#packages.x86_64-linux.install-portable
# Strip hashes, add linux-amd64 alias, set variant to linux-amd64
```

### Use `install-portable` not `install`

`install` keeps Nix store RPATH (`/nix/store/qtbase-6.9.2/...`) → loads nix Qt
alongside logos_host Qt → dual-Qt heap corruption → `std::bad_alloc`.

`install-portable` strips RPATH to `$ORIGIN/.`, bundles boost/ssl next to `.so`.
Verify: `patchelf --print-rpath keeper_plugin.so` must show `$ORIGIN/.`.

---

## Queue & Persistence

### File status flow

```
pending → downloading → uploading → inscribing → done
                                               → failed
```

`uploadToStash()` sets status to "uploading" before handing off to QML.
`onUploadResult()` sets CID and advances to "inscribing".
`inscribeToBeacon()` writes to `inscriptionQueue_` and sets status "inscribing".
`finishItem()` sets status "done" after beacon confirms.

### Persistence paths

All files written to `instancePersistencePath`:
- `keeper-queue.json` — active queue
- `keeper-log.json` — completed items log
- `keeper-inscription-queue.json` — pending beacon inscriptions

Retrieved in `initLogos` via `property("instancePersistencePath")`.

---

## HTTP Bridge

The `KeeperHttpBridge` runs on port 7355 and accepts:
- `POST /preserve` — preserve an IA item or collection
- `GET /queue` — current queue JSON
- `GET /log` — completed items log

Used by integration tests to drive the keeper without a UI.

---

## Testing

### Tier 1 — Headless unit tests (13 assertions, ~2s)

```bash
./tests/run-headless-tests.sh
```

Uses logoscore single-invocation mode. Tests queue management, config, log persistence,
CID completion, HTTP bridge endpoints.

### Tier 2 — Integration pipeline

```bash
./tests/run-integration-tests.sh --item anarchy_Cypherpunk_Manifesto
# With blockchain verification:
VERIFY_BLOCKCHAIN=1 ./tests/run-integration-tests.sh --item anarchy_Cypherpunk_Manifesto
```

`VERIFY_BLOCKCHAIN=1` polls the keeper log for a `collectionCid`, then verifies the
inscription appears on the canonical explorer at
`https://testnet.blockchain.logos.co/web/explorer/transactions/{txHash}`.

### Node canonical chain check

Before running integration tests, verify the node is on canonical chain:

```bash
LIB=$(curl -s http://localhost:8080/cryptarchia/info | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['lib_block'])")
curl -s "https://testnet.blockchain.logos.co/web/explorer/api/v1/blocks/$LIB?fork=0" | python3 -m json.tool
# Should return the block, not 404
```

---

## Known Pitfalls

- **logosAPI shadowing**: Never redeclare `LogosAPI* logosAPI` in the plugin class.
  Use only the inherited `PluginInterface::logosAPI`. (Fixes silent callModule failure.)
- **Lambda dangling refs**: Capture `file.name` by value in lambdas — capturing `&file`
  where `file` is a `const KeeperFile&` element becomes dangling after list mutation.
- **clipboard copy in QML**: Use `opacity: 0` with `width: 1; height: 1` on the helper
  TextEdit — `visible: false` elements can't receive focus, `copy()` silently fails.
