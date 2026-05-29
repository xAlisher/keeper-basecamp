# Plan: Logos Messaging Extension for Keeper

## Status: APPROVED — implementing on `feature/logos-messaging`

## Goal

Replace the insecure `localhost:7355` HTTP bridge with Logos Messaging (delivery_module /
js-waku). No TCP listener. Messages signed with Ed25519; keeper rejects unknown pubkeys.

## Architecture

```
Chrome Extension (keeper-messaging-extension)      keeper-basecamp (feature/logos-messaging)
────────────────────────────────────────           ──────────────────────────────────────────
background.js (service worker)                     keeper_plugin.cpp
  js-waku LightNode (LightPush + Filter)  ←Waku→   delivery_module IPC
  Ed25519 keypair (chrome.storage.local)            subscribe /keeper/1/preserve/json
  publish signed preserve requests        ───────►  verify sig → preserveItem()
  subscribe status updates                ◄───────  publish /keeper/1/status/json
                                                    paired pubkeys stored in DB meta
content_script.js                                  keeper-ui/qml/Main.qml
  Keep button on archive.org/details/*              "Pair Extension" flow
  chrome.runtime ↔ SW (no HTTP)                     pubkey input + paired list
```

## Content Topics

| Direction | Topic |
|-----------|-------|
| Extension → Keeper | `/keeper/1/preserve/json` |
| Keeper → Extension | `/keeper/1/status/json` |

## Message Schemas

**Preserve request:**
```json
{ "action":"preserve","identifier":"...","url":"...","timestamp":1716000000,
  "pubkey":"<64-hex ed25519>","sig":"<base64 ed25519 sig of canonical JSON>" }
```
Canonical JSON (JS key order for signing): `{action,identifier,url,timestamp,pubkey}`

**Status update:**
```json
{ "identifier":"...","status":"queued|active|inscribing|done|failed",
  "cid":"...","progress":0.45,"error":"" }
```

## Files

### keeper-basecamp changes

| File | Action |
|------|--------|
| `src/keeper_http_bridge.h` | DELETE |
| `src/keeper_http_bridge.cpp` | DELETE |
| `CMakeLists.txt` | Remove HttpServer; add libsodium via PkgConfig |
| `metadata.json` | runtime: libsodium, build: pkg-config; remove qthttpserver |
| `src/keeper_plugin.h` | Remove bridge members; add delivery/pairing members |
| `src/keeper_plugin.cpp` | Remove bridge init; add Waku init, sig verify, pairing methods |
| `keeper-ui/qml/Main.qml` | Replace bridge pill → Logos Messaging status; add Pair section |

### New repo: keeper-messaging-extension

Location: `~/basecamp/modules/extensions/keeper-messaging-extension/`

| File | Purpose |
|------|---------|
| `manifest.json` | MV3, Chrome 116+, no localhost permissions |
| `package.json` | @waku/sdk ^0.0.27, vite |
| `vite.config.js` | Bundle background.js + content_script.js + popup.js → dist/ |
| `src/crypto.js` | Ed25519 keygen + sign via Web Crypto API |
| `src/background.js` | SW: Waku node, sign + send, subscribe status |
| `src/content_script.js` | Keep button (v1 logic minus HTTP polling) |
| `src/popup.js` | Show full hex pubkey for pairing |
| `popup.html` | Popup UI |

## Key Design Decisions

1. **Single base64 layer**: delivery_module adds one base64 layer on send (confirmed skill
   `delivery-module-messaging`). Use `QByteArray::fromBase64(data[2].toString().toLatin1())`
   to recover payload. No double-encode.

2. **Canonical JSON for signing**: Build as raw string in C++ to preserve JS `JSON.stringify`
   key insertion order `{action,identifier,url,timestamp,pubkey}`. QJsonObject serialises
   alphabetically — not compatible with JS signer.

3. **libsodium**: Add via PkgConfig in CMakeLists guard block. Add to `metadata.json`
   `nix.packages.build: ["pkg-config"]` and `nix.packages.runtime: ["libsodium"]`.

4. **No self-echo filter needed**: keeper only subscribes and does not publish on
   `/keeper/1/preserve/json`. Extension only publishes there. No collision.

5. **Logos Messaging status**: QML replaces bridge pill with Logos Messaging status pill.
   `getWakuStatus()` → renamed to `getLogosMsgStatus()` for clarity but internal impl
   still uses delivery_module.

## Assumptions Register

| Assumption | Verification | Break Condition |
|-----------|-------------|-----------------|
| delivery_module present in target AppImage | Check AppImage at test time | "no delivery_module client" in logs |
| libsodium available via nixpkgs | builder-cmake-extra-deps-guard-block confirms pattern | pkg-config fails in nix sandbox |
| `data[2]` = single base64 payload | Confirmed in delivery-module-messaging skill | Message parse fails |
| Chrome 116 Ed25519 Web Crypto | Plan requires minimum_chrome_version: 116 | crypto.subtle.generateKey throws |

## Success Criteria

1. `nix develop` build compiles with no HttpServer dep
2. Keeper UI shows "Logos Messaging" status (connecting/online)
3. Pair Extension flow: paste 64-hex pubkey → "Add" → appears in list
4. Click Keep on archive.org → item queued in keeper
5. Status updates: "Queued" → "Done" in button label
6. v1 extension: no longer reaches keeper (no HTTP listener)
7. Unknown pubkey message: ignored (logged but not processed)
