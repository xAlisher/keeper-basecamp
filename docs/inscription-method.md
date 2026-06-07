# Keeper Inscription Method

How keeper inscribes CIDs to the Logos Blockchain zone channel — confirmed working June 7 2026.

## Working example

```
CID: ia:kuMUquaeE6g
Explorer: https://testnet.blockchain.logos.co/web/explorer/transactions/67039dd5ab15838f49f09ca63b7df7a14d36762bf201044ead100ccdd6059688
Block: df976320... slot=4709374 height=241916 fork=3214
```

Playwright-verified: renders "Transaction 67039dd5ab…d6059688 / ChannelInscribe" correctly.

## Inscription payload

```json
{
  "v": 1,
  "type": "cid_pin",
  "cid": "ia:kuMUquaeE6g",
  "source": "keeper",
  "ts": 1780781543
}
```

## Python one-shot pattern (correct)

```python
import ctypes, os, json, time, urllib.request

LIB_DIR = os.path.expanduser("~/.local/share/Logos/LogosBasecamp/modules/liblogos_zone_sequencer_module")
os.environ["LD_LIBRARY_PATH"] = LIB_DIR + ":" + os.environ.get("LD_LIBRARY_PATH", "")

lib = ctypes.CDLL(os.path.join(LIB_DIR, "libzone_sequencer_rs.so"))
lib.zone_sequencer_create.restype = ctypes.c_void_p
lib.zone_sequencer_create.argtypes = [ctypes.c_char_p] * 4
lib.zone_sequencer_publish.restype = ctypes.c_char_p
lib.zone_sequencer_publish.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
lib.zone_sequencer_destroy.restype = None
lib.zone_sequencer_destroy.argtypes = [ctypes.c_void_p]

NODE_URL   = b"http://100.108.127.3:8080"
CHANNEL_ID = b"<64-char hex channel id>"
SIGNING_KEY = b"<64-char hex signing key>"

inscription_data = json.dumps({
    "v": 1, "type": "cid_pin", "cid": "ia:XXXXX",
    "source": "keeper", "ts": int(time.time())
}, separators=(',', ':')).encode()

# Step 1: create with NO checkpoint — bootstraps fresh to current LIB
handle = lib.zone_sequencer_create(NODE_URL, CHANNEL_ID, SIGNING_KEY, b"")

# Step 2: publish raw JSON bytes (NOT a file path)
result = lib.zone_sequencer_publish(handle, inscription_data)
txhash = result.decode()  # this is Poseidon2 TxHash — NOT the explorer hash

# Step 3: wait for background HTTP submission to node
time.sleep(35)
lib.zone_sequencer_destroy(handle)

# Step 4: scan blocks to get explorer hash (mantle_tx.hash ≠ txhash)
info = json.loads(urllib.request.urlopen(f"{NODE_URL.decode()}/cryptarchia/info").read())
# scan /cryptarchia/blocks?slot_from=X&slot_to=Y, find block with your channel_id
# extract mantle_tx.hash from that block — THAT is the explorer URL hash
```

## Critical rules

| Rule | Why |
|------|-----|
| Pass `b""` as checkpoint_path | Checkpoint with old lib_slot triggers full backfill (60s+) before submit; fresh bootstrap goes straight to current LIB |
| Pass raw bytes as data arg | Library publishes the data arg bytes as inscription content — if you pass a file path string, the path string becomes the inscription |
| Sleep 35s before destroy | `zone_sequencer_publish` queues the tx and returns immediately; actual HTTP POST to node happens in a background tokio task; `destroy` aborts it |
| Scan block for `mantle_tx.hash` | `zone_sequencer_publish` return value is a local Poseidon2 TxHash, not the explorer hash; explorer indexes by `mantle_tx.hash` (different computation) |
| Use post-PR#2481 .so | Pre-fix `libzone_sequencer_rs.so` (built against logos-blockchain rev `6b42706`) computes wrong Fr field hash → inscription_id permanently 404 on explorer |

## Correct .so location

```
/nix/store/0q0vj0v0bhjfw4zcp0m7shacdfiq7rhw-logos-zone-sequencer-module-0.1.0/lib/libzone_sequencer_rs.so
```
Built from logos-blockchain `05f84a5` (v0.1.2, post-PR#2481 "Fix fr from bytes condition").

Installed to:
```
~/.local/share/Logos/LogosBasecamp/modules/liblogos_zone_sequencer_module/libzone_sequencer_rs.so
```

## Chain reorgs

Cryptarchia PoS testnet reorgs above LIB are **normal**. The explorer's `fork` integer increments on every re-index. Blocks below `lib_slot` are permanent; the top ~485 slots (`tip - lib_slot`) are not yet finalized and can be reorganized.

TX data remains indexed at all fork IDs — the frontend renders at the current `fork-choice` only. A TX that shows "Not found" or "Error: HTTP 500" during a fork transition will render correctly once the explorer settles at the new fork.

To verify a TX is on-chain independent of the frontend:
```bash
curl "https://testnet.blockchain.logos.co/web/explorer/api/v1/transactions/{hash}?fork=3214"
# Returns full tx data if indexed; 404 if not found
```
