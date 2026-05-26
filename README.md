# keeper-basecamp

Permanent preservation of [Internet Archive](https://archive.org) collections on [Logos](https://logos.co).

Paste an IA item URL or identifier — Keeper downloads the files, stores them in Logos Storage (IPFS), and inscribes the CIDs on-chain via Beacon. Your archive is then discoverable by anyone running Cord who subscribes to your Keeper channel.

## How it works

```
Browser extension  ──► Keeper ──► Stash (Logos Storage / IPFS)
  [ Keep ]                   └──► Beacon (on-chain inscription)
                                        │
                                   Zone Sequencer ──► LEZ Testnet
                                        │
                                   Cord (discovery by others)
```

1. **Input** — paste an `archive.org/details/<id>` URL, or use the browser extension
2. **Download** — Keeper fetches files from the IA CDN sequentially
3. **Store** — each file is uploaded to Logos Storage via Stash; a collection CID is pinned
4. **Inscribe** — Beacon publishes the collection CID + rich label to your dedicated Keeper channel, signed by your Keycard key
5. **Discover** — anyone subscribed to your Keeper channel via Cord sees new inscriptions in real time

## Modules

| Module | Repo | Role |
|--------|------|------|
| `keeper` (this) | keeper-basecamp | orchestration, IA fetch, queue |
| `keeper-ui` | keeper-basecamp | QML interface |
| `stash` | stash-basecamp | Logos Storage upload → CID |
| `beacon` | beacon-basecamp | CID → on-chain inscription |
| `cord` | cord-basecamp | channel subscription + discovery |
| `keycard` | keycard-basecamp | Ed25519 signing key source |

## Inscription format

Each on-chain inscription is a JSON label attached to the collection CID:

```json
{
  "module": "keeper",
  "source": "internet_archive",
  "id": "popeye_taxi-turvey",
  "title": "Popeye: Taxi-Turvy",
  "totalSize": 307027329,
  "files": [
    { "name": "__ia_thumb.jpg", "cid": "zDvZRwzm..." },
    { "name": "taxi-turvy.mpeg", "cid": "zDvZRwzm..." }
  ]
}
```

## Browser extension

The companion extension (`keeper-extension`) adds a **[ Keep ]** button to every `archive.org/details/*` page. Clicking it sends the identifier to Keeper via `localhost:17432`.

## Build

```bash
cd keeper-basecamp
nix build
```

Install the `.so` into your Logos Basecamp modules directory and the QML plugin into plugins. See [basecamp-skills](https://github.com/xAlisher/basecamp-skills) for the full install recipe.

## Status

`v0.1.0` — working on LEZ testnet. Known rough edges tracked as GitHub issues.
