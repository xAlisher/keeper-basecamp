# keeper-basecamp

Permanent preservation of [Internet Archive](https://archive.org) collections on [Logos](https://logos.co).

Paste an IA item URL or identifier — Keeper downloads the files, stores them in Logos Storage (IPFS), and inscribes the collection CID on-chain via Beacon. The inscription is then discoverable by anyone running Cord who subscribes to your Keeper channel.

---

## How it works

```
archive.org/details/<id>
        │
  [ paste URL ]  or  [ browser extension ]
        │
   Keeper (this)
        │
        ├── Downloads files from IA CDN sequentially
        │
        ├── Uploads each file → Stash (Logos Storage / IPFS)
        │         └── returns CID per file
        │
        ├── Pins collection → Stash
        │         └── returns collection CID
        │
        └── Inscribes (collectionCid + label) → Beacon
                  └── Zone Sequencer → LEZ Testnet
                            └── Cord subscribers see it
```

1. **Input** — paste an `archive.org/details/<id>` URL, or press **Keep** in the browser extension
2. **Download** — Keeper fetches files from the IA CDN, up to `maxFilesPerItem` (default 20), optionally skipping derivatives
3. **Store** — each file is uploaded to Logos Storage via Stash; a final collection CID is pinned
4. **Inscribe** — Beacon publishes `{module:"keeper", id, title, files:[...]}` to your dedicated Keeper channel, signed by your Ed25519 key
5. **Discover** — anyone subscribed to your channel in Cord sees the inscription with a working explorer link

---

## Dependencies

All dependencies must be installed and loaded in Logos Basecamp before Keeper operates end-to-end.

| Module | Installed name | Repo | Release |
|--------|---------------|------|---------|
| **keeper** (this) | `keeper` | [keeper-basecamp](https://github.com/xAlisher/keeper-basecamp) | [v0.1.0 LGX](https://github.com/xAlisher/keeper-basecamp/releases/tag/v0.1.0) |
| **keeper-ui** (this) | `keeper_ui` (plugin) | [keeper-basecamp](https://github.com/xAlisher/keeper-basecamp) | [v0.1.0 LGX](https://github.com/xAlisher/keeper-basecamp/releases/tag/v0.1.0) |
| **stash** | `stash` | [stash-basecamp](https://github.com/xAlisher/stash-basecamp) | [v0.1.0 LGX](https://github.com/xAlisher/stash-basecamp/releases/tag/v0.1.0) |
| **beacon** | `logos_beacon` | [beacon-basecamp](https://github.com/xAlisher/beacon-basecamp) | [v1.0.0 LGX](https://github.com/xAlisher/beacon-basecamp/releases/tag/v1.0.0) |
| **zone sequencer** | `liblogos_zone_sequencer_module` | shipped with Basecamp AppImage | — |
| **cord** | `logos_cord` | [cord-basecamp](https://github.com/xAlisher/cord-basecamp) | [v1.0.0 LGX](https://github.com/xAlisher/cord-basecamp/releases/tag/v1.0.0) |
| **keycard** | `logos_keycard` | [keycard-basecamp](https://github.com/xAlisher/keycard-basecamp) | [v1.0.0 LGX](https://github.com/xAlisher/keycard-basecamp/releases/tag/v1.0.0) |

### Runtime environment

- [Logos Basecamp](https://github.com/logos-co/logos-app) AppImage — tested on `v0.2.0+`
- Linux x86-64 (AppImage runs on Ubuntu 22.04+, NixOS, Arch)

---

## Install

### Option A — via LGX (recommended for testing)

Download the LGX files from each module's release page and install with `lgpm`. Install in this order (dependencies first):

```bash
# Find lgpm in the AppImage Nix store
lgpm=$(find /nix/store -name lgpm -path "*/logos-package-manager-cli-*/bin/lgpm" 2>/dev/null | head -1)
MDIR=~/.local/share/Logos/LogosBasecamp/modules
PDIR=~/.local/share/Logos/LogosBasecamp/plugins

# 1. Keycard — https://github.com/xAlisher/keycard-basecamp/releases/tag/v1.0.0
rm -rf $MDIR/logos_keycard $PDIR/keycard_ui
$lgpm --modules-dir $MDIR --ui-plugins-dir $PDIR --allow-unsigned install --file logos-keycard-module-lib-lgx-1.0.0.lgx
$lgpm --modules-dir $MDIR --ui-plugins-dir $PDIR --allow-unsigned install --file logos-keycard-ui-module-lgx-1.0.0.lgx

# 2. Stash — https://github.com/xAlisher/stash-basecamp/releases/tag/v0.1.0
rm -rf $MDIR/stash $PDIR/stash_ui
$lgpm --modules-dir $MDIR --ui-plugins-dir $PDIR --allow-unsigned install --file logos-stash-module-lib-lgx-0.1.0.lgx
$lgpm --modules-dir $MDIR --ui-plugins-dir $PDIR --allow-unsigned install --file logos-stash_ui-module-lgx-0.1.0.lgx

# 3. Beacon — https://github.com/xAlisher/beacon-basecamp/releases/tag/v1.0.0
rm -rf $MDIR/logos_beacon $PDIR/beacon_ui
$lgpm --modules-dir $MDIR --ui-plugins-dir $PDIR --allow-unsigned install --file logos-logos_beacon-module-lib-lgx-1.0.0.lgx
$lgpm --modules-dir $MDIR --ui-plugins-dir $PDIR --allow-unsigned install --file logos-beacon_ui-module-lgx-1.0.0.lgx

# 4. Cord — https://github.com/xAlisher/cord-basecamp/releases/tag/v1.0.0
rm -rf $MDIR/logos_cord $PDIR/cord_ui
$lgpm --modules-dir $MDIR --ui-plugins-dir $PDIR --allow-unsigned install --file logos-logos_cord-module-lib-lgx-1.0.0.lgx
$lgpm --modules-dir $MDIR --ui-plugins-dir $PDIR --allow-unsigned install --file logos-cord_ui-module-lgx-1.0.0.lgx

# 5. Keeper — https://github.com/xAlisher/keeper-basecamp/releases/tag/v0.1.0
rm -rf $MDIR/keeper $PDIR/keeper_ui
$lgpm --modules-dir $MDIR --ui-plugins-dir $PDIR --allow-unsigned install --file logos-keeper-module-lib-lgx-0.1.0.lgx
$lgpm --modules-dir $MDIR --ui-plugins-dir $PDIR --allow-unsigned install --file logos-keeper_ui-module-lgx-0.1.0.lgx

# Clear QML cache
rm -rf ~/.cache/Logos/LogosBasecamp/qmlcache
```

Restart Logos Basecamp — all five modules should appear in the sidebar.

---

### Option B — build from source

Requires [Nix with flakes](https://nixos.org/download) (`nix --version` ≥ 2.18).

```bash
git clone https://github.com/xAlisher/keeper-basecamp
cd keeper-basecamp

# IMPORTANT: use install-portable, NOT the default install target.
# The plain 'install' target keeps Nix store RPATHs → loads a second Qt
# alongside logos_host Qt → heap corruption + std::bad_alloc at runtime.
nix build .#packages.x86_64-linux.install-portable
```

Output is at `result/modules/keeper/`. Verify RPATH before installing:

```bash
patchelf --print-rpath result/modules/keeper/keeper_plugin.so
# Must show: $ORIGIN/.
# Must NOT contain any /nix/store/*/qtbase* paths
```

Then install manually:

```bash
INSTALL_DIR=~/.local/share/Logos/LogosBasecamp/modules/keeper
chmod -R u+w "$INSTALL_DIR" 2>/dev/null; rm -rf "$INSTALL_DIR"
cp -r result/modules/keeper/. "$INSTALL_DIR/"
cp metadata.json "$INSTALL_DIR/"

chmod u+w "$INSTALL_DIR/manifest.json"
python3 -c "
import json
with open('$INSTALL_DIR/manifest.json') as f: m = json.load(f)
m.pop('hashes', None)
m['main']['linux-amd64'] = 'keeper_plugin.so'
with open('$INSTALL_DIR/manifest.json', 'w') as f: json.dump(m, f, indent=2)"

echo "linux-amd64" > "$INSTALL_DIR/variant"

UI_DIR=~/.local/share/Logos/LogosBasecamp/plugins/keeper_ui
mkdir -p "$UI_DIR/qml"
cp keeper-ui/qml/Main.qml "$UI_DIR/qml/Main.qml"
cp keeper-ui/qml/Main.qml "$UI_DIR/Main.qml"
rm -rf ~/.cache/Logos/LogosBasecamp/qmlcache
```

Do the same for each dependency (stash, beacon, cord, keycard) using `install-portable` from their respective repos.

### Stale module cleanup

```bash
# Remove stale keeper-ui from modules/ (old install path)
rm -rf ~/.local/share/Logos/LogosBasecamp/modules/keeper-ui
```

---

## Usage

### UI

1. Launch Logos Basecamp — the **Keeper** tab appears in the sidebar
2. Paste an IA URL (`https://archive.org/details/<id>`) or bare identifier (`anarchy_Cypherpunk_Manifesto`)
3. Click **Preserve** — the item enters the queue and begins processing
4. Watch status in the queue panel: `downloading → uploading → inscribing → done`
5. When done, the log panel shows the Logos Storage CID and a live explorer link

### HTTP bridge (headless / scripted use)

The keeper HTTP bridge runs on **port 7355** and accepts requests from the browser extension or scripts:

```bash
# Queue an item
curl -s -X POST http://127.0.0.1:7355/preserve \
  -H "Content-Type: application/json" \
  -d '{"identifier": "anarchy_Cypherpunk_Manifesto"}'

# Get current queue
curl -s http://127.0.0.1:7355/queue | python3 -m json.tool

# Get completed log
curl -s http://127.0.0.1:7355/log | python3 -m json.tool

# Get bridge status
curl -s http://127.0.0.1:7355/status
```

### Browser extension

The companion extension adds a **[ Keep ]** button to every `archive.org/details/*` page.

```
extensions/keeper-extension/   ← load unpacked in chrome://extensions
```

1. Open `chrome://extensions`, enable **Developer mode**
2. **Load unpacked** → select `extensions/keeper-extension/`
3. Visit any `archive.org/details/*` page — press **Keep**

---

## Configuration

Via the Settings panel in the UI, or `setConfig`:

| Key | Default | Description |
|-----|---------|-------------|
| `maxFilesPerItem` | `20` | Max files downloaded per IA item |
| `skipDerivatives` | `true` | Skip IA derivative files (animated GIFs, compressed ZIPs, etc.) |

---

## Inscription format

Each on-chain inscription is a JSON payload attached to the collection CID:

```json
{
  "module": "keeper",
  "source": "internet_archive",
  "id": "popeye_taxi-turvey",
  "title": "Popeye: Taxi-Turvy",
  "totalSize": 307027329,
  "files": [
    { "name": "__ia_thumb.jpg",  "cid": "zDvZRwzm1TJ..." },
    { "name": "taxi-turvy.mpeg", "cid": "zDvZRwzm5jk..." }
  ]
}
```

The inscription is published to your Beacon channel on the LEZ testnet. The explorer link appears in the Keeper log panel once the transaction is confirmed:

```
https://testnet.blockchain.logos.co/web/explorer/transactions/<txHash>
```

---

## Tests

### Tier 1 — Headless unit tests (no network, ~2s)

Tests queue management, config, log persistence, bridge status. Uses `logoscore` single-invocation mode.

```bash
./tests/run-headless-tests.sh

# Custom logoscore path:
LOGOSCORE=/path/to/logoscore ./tests/run-headless-tests.sh
```

Requires: `logoscore` binary (from Nix store or Basecamp AppImage extraction), keeper module installed at `~/.local/share/Logos/LogosBasecamp/modules/keeper/`.

### Tier 2 — Integration pipeline (requires Basecamp running)

Queues a real IA item via the HTTP bridge, polls until done, asserts collection CID and txHash.

```bash
# Default item (anarchy_Cypherpunk_Manifesto)
./tests/run-integration-tests.sh

# Custom item
./tests/run-integration-tests.sh --item popeye_taxi-turvey

# With blockchain verification (checks explorer URL returns HTTP 200)
# Requires Tailscale access to the testnet node
VERIFY_BLOCKCHAIN=1 ./tests/run-integration-tests.sh
```

Requires: Basecamp running with keeper loaded, stash and beacon modules active, testnet node reachable.

---

## Architecture notes

### IPC pattern

Keeper C++ **never** calls `logosAPI->getClient()` for cross-module calls. The Qt QRO allocator
state is corrupted at platform startup by concurrent module IPC activity; `getClient()` reliably
throws `std::bad_alloc` regardless of timing. The workaround is QML-mediated IPC:

- Keeper C++ sets a `pendingUpload_` struct, exposes it via `getPendingUpload()` Q_INVOKABLE
- Keeper QML polls every 2s, calls `logos.callModule("stash", "upload", [...])` directly
- Stash result is polled via `logos.callModule("stash", "getLatestLogosResult", [])`
- QML calls `logos.callModule("keeper", "onUploadResult", [...])` back into C++

Beacon inscription uses a file-based queue (`keeper-inscription-queue.json`) that Beacon QML polls independently — no direct keeper→beacon IPC needed.

### Persistence

All state files are written to `instancePersistencePath` (injected by the platform):

```
module_data/keeper/<instanceId>/
├── keeper-queue.json              ← active queue (survives restart)
├── keeper-log.json                ← completed items
└── keeper-inscription-queue.json  ← pending beacon inscriptions
```

---

## Status

`v0.1.0` — working on LEZ testnet. Confirmed end-to-end: download → Logos Storage CIDs → on-chain inscription with working explorer link.

Known limitations:
- Sequential file downloads (no parallelism)
- No retry on IA CDN failures (item goes to `failed`, re-queue manually)
- Inscription confirmation can take 1-5 min depending on testnet slot timing
