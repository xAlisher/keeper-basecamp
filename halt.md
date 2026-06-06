# Halt — 2026-05-29

## Where we stopped

Senty security review completed — LGTM after Round 12 (no findings at any severity). All 12 rounds of HIGH/MEDIUM findings were fixed across `src/keeper_plugin.cpp`, `src/keeper_plugin.h`, `keeper-ui/qml/Main.qml`, and `CMakeLists.txt`. Branch is clean, all commits pushed. Build has NOT been run yet — `nix develop` is the next step.

## Current state

- Branch: `feature/logos-messaging`
- Last commit: `29592c4` — chore: update halt.md — Senty LGTM after Round 12, ready for build
- Build status: NOT YET BUILT
- Open review: none — Senty LGTM (Round 12 clean)
- PR: #8 open on xAlisher/keeper-basecamp — do NOT merge until live-test passes

## Next steps (in order)

1. **Build**: `cd ~/basecamp/modules/keeper-basecamp && nix develop` — confirm it compiles cleanly
2. **Install**: follow `lgx-package-format` / `dev-install-convention` skills to install the built module
3. **Live-test** (see checklist below)
4. **Merge PR #8** only after live-test passes

## Live-test checklist

- [ ] Logos Messaging pill shows connecting → online
- [ ] Paste 64-hex pubkey from extension popup → Pair → fingerprint appears in list
- [ ] Click Keep on archive.org/details/* → item queued in keeper UI
- [ ] Status relay: button transitions Queued → Keeping… → Kept
- [ ] v1 extension: cannot reach keeper (no HTTP listener)
- [ ] Replay same signed message → keeper ignores (timestamp stale or sig seen)
- [ ] Unknown pubkey message → keeper ignores (logged)

## Blockers

- `delivery_module` must be installed separately (absent from AppImage v173)
- libsodium PkgConfig link not yet sandbox-tested (build will confirm)
- `npm install && npm run build` still needed in `keeper-messaging-extension` before loading in Chrome

## Context that's hard to re-derive

- Senty ran 12 rounds total; findings grew from pre-existing code (not just the Logos Messaging diff) — all fixed
- `advanceQueue()` after failure paths is intentional (prevents queue stall) — documented in code with comments
- `finishItem()` uses a two-phase saveQueue: saves "done" state while item still in queue, then removes and saves again
- `clearLog()`/`clearQueue()` check file remove BEFORE clearing memory (order matters for rollback safety)
- `getPairedExtensions()` returns `[{fp:"abcd...ef12",idx:0}]` — QML uses `idx` for removal via `removePairedExtensionAt(int)`
- Ed25519 canonical JSON built as raw string (not QJsonObject) to match JS `JSON.stringify` key insertion order
- Single base64 decode on delivery_module receive (`data[2]` = `base64(payload)`)
- `keeper-messaging-extension` repo: `~/basecamp/modules/extensions/keeper-messaging-extension/`, remote `xAlisher/keeper-messaging-extension`, commit `76e9d2b`
- `zone_publish` vs `zone_sequencer_publish` distinction (see previous halt notes if needed)
