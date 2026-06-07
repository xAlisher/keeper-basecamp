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

| Module | Installed name | Repo | Role |
|--------|---------------|------|------|
| **keeper** (this) | `keeper` | [keeper-basecamp](https://github.com/xAlisher/keeper-basecamp) | orchestration, IA fetch, queue, HTTP bridge |
| **keeper-ui** (this) | `keeper_ui` (plugin) | [keeper-basecamp](https://github.com/xAlisher/keeper-basecamp) | QML interface |
| **stash** | `stash` | [stash-basecamp](https://github.com/xAlisher/stash-basecamp) | file upload → IPFS CID |
| **beacon** | `logos_beacon` | [beacon-basecamp](https://github.com/xAlisher/beacon-basecamp) | CID → on-chain inscription via Zone Sequencer |
| **zone sequencer** | `liblogos_zone_sequencer_module` | shipped with Basecamp AppImage | publishes to LEZ chain |
| **cord** | `logos_cord` | [cord-basecamp](https://github.com/xAlisher/cord-basecamp) | channel subscription + discovery (optional for basic use) |
| **keycard** | `logos_keycard` | [keycard-basecamp](https://github.com/xAlisher/keycard-basecamp) | Ed25519 signing key (optional — beacon generates its own key if absent) |

### Runtime environment

- [Logos Basecamp](https://github.com/logos-co/logos-app) AppImage — tested on `v0.2.0+`
- Linux x86-64 (AppImage runs on Ubuntu 22.04+, NixOS, Arch)

---

## Build

Keeper uses [logos-module-builder](https://github.com/logos-co/logos-module-builder) via Nix flakes.

### Prerequisites

```bash
# Nix with flakes enabled
nix --version          # >= 2.18 recommended
# OR: NixOS with experimental-features = nix-command flakes
```

### Build the portable installable

```bash
git clone https://github.com/xAlisher/keeper-basecamp
cd keeper-basecamp

# IMPORTANT: use install-portable, NOT the default install target.
# The plain 'install' target keeps Nix store RPATHs → loads a second Qt
# alongside logos_host Qt → heap corruption + std::bad_alloc at runtime.
nix build .#packages.x86_64-linux.install-portable
```

Output is at `result/modules/keeper/`.

Verify the RPATH is correct before installing:

```bash
patchelf --print-rpath result/modules/keeper/keeper_plugin.so
# Must show: $ORIGIN/.
# Must NOT contain any /nix/store/*/qtbase* paths
```

---

## Install

### One-shot install script

```bash
INSTALL_DIR=~/.local/share/Logos/LogosBasecamp/modules/keeper

# Remove stale install (safe — Basecamp will reload from fresh)
chmod -R u+w "$INSTALL_DIR" 2>/dev/null; rm -rf "$INSTALL_DIR"

# Copy build output
cp -r result/modules/keeper/. "$INSTALL_DIR/"

# Copy source metadata (required for module discovery)
cp metadata.json "$INSTALL_DIR/"

# Strip hashes from manifest — platform rejects dev modules with hashes field
# (triggers public registry check; always fails for private/dev modules)
chmod u+w "$INSTALL_DIR/manifest.json"
python3 -c "
import json
with open('$INSTALL_DIR/manifest.json') as f: m = json.load(f)
m.pop('hashes', None)
m['main']['linux-amd64'] = 'keeper_plugin.so'
with open('$INSTALL_DIR/manifest.json', 'w') as f: json.dump(m, f, indent=2)"

# Set variant to linux-amd64 (build produces linux-amd64-dev by default)
echo "linux-amd64" > "$INSTALL_DIR/variant"

# Install QML UI plugin
UI_DIR=~/.local/share/Logos/LogosBasecamp/plugins/keeper_ui
mkdir -p "$UI_DIR/qml"
cp keeper-ui/qml/Main.qml "$UI_DIR/qml/Main.qml"
cp keeper-ui/qml/Main.qml "$UI_DIR/Main.qml"

# Clear QML cache so Basecamp picks up new UI
rm -rf ~/.cache/Logos/LogosBasecamp/qmlcache
```

### Stale module cleanup

If a previous version is already installed:

```bash
# Also remove any stale keeper-ui from modules/ (old install path)
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
