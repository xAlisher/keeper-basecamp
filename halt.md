# Halt — 2026-05-28

## Where we stopped

Senty review completed on PR #8 (`feature/logos-messaging`). 4 findings: HIGH ×2, MEDIUM ×2.
Not yet fixed. Build not yet run (Codex sandbox network restriction prevented it).

**Merge gate: test before merge** — do NOT merge PR #8 to main until: findings fixed → build
passes → live-test passes.

## Senty findings (Round 1) — ALL FIXED (commit e8c92e3)

**[HIGH] Replay attack — no timestamp check or seen-message guard**
File: `src/keeper_plugin.cpp` (onWakuMessageReceived)
Fix: check `|now - msg.timestamp| <= 30s`; track seen signatures in a bounded set.

**[HIGH] getPairedExtensions() returns full pubkeys — key material in return value**
File: `src/keeper_plugin.cpp:962`
Fix: return redacted fingerprints (first 8 + last 8 hex chars) for QML display;
keep full keys internal. Add separate `getFullPubkey(index)` only if needed.

**[MEDIUM] Pubkey allowlist match not canonicalized — case mismatch → silent auth bypass**
File: `src/keeper_plugin.cpp:870`
Fix: `.toLower()` on input in `addPairedExtension()`, `onWakuMessageReceived()`, and at load time.

**[MEDIUM] QML addPairedExtension/removePairedExtension result not checked**
File: `keeper-ui/qml/Main.qml:487`
Fix: wrap in `callModuleParse()`; branch on `result.success`; show error toast on failure.

## Current state

- **keeper-basecamp** branch: `feature/logos-messaging` — last commit `e8c92e3`, PR #8 open
- **keeper-messaging-extension**: `xAlisher/keeper-messaging-extension` main, commit `76e9d2b`
- Build status: NOT YET BUILT
- Open issues: #3, #4, #5 — deferred

## Next steps (in order)

1. ~~Fix all 4 Senty findings~~ — done (e8c92e3)
2. **Re-run Senty** (Round 2) — should be clean
3. **Build**: `nix develop` in keeper-basecamp
4. **Install + live-test** (see test checklist below)
5. **Merge PR #8** only after live-test passes

## Live-test checklist

- [ ] Logos Messaging pill shows connecting → online
- [ ] Paste 64-hex pubkey from extension popup → Pair → fingerprint appears in list
- [ ] Click Keep on archive.org → item queued in keeper UI
- [ ] Status relay: button transitions Queued → Keeping… → Kept
- [ ] v1 extension: cannot reach keeper (no HTTP listener)
- [ ] Replay same signed message → keeper ignores (timestamp stale or sig seen)
- [ ] Unknown pubkey message → keeper ignores (logged)

## Blockers

- `delivery_module` must be installed separately (absent from AppImage v173)
- libsodium PkgConfig link not yet sandbox-tested

## Context that's hard to re-derive

- `zone_publish` (stateless) returns zone message envelope hash — NOT `mantle_tx.hash`
- `zone_sequencer_publish` (persistent handle) returns `mantle_tx.hash` — explorer-compatible
- `query_channel(channelId)` returns 0 messages until zone message is finalized on-chain
  (i.e., anchored by a ChannelInscribe tx and past the LIB slot)
- `findAnchorTx` searches `/cryptarchia/blocks?slot_from=X&slot_to=Y` for ops with matching
  `channel_id` in payload — returns first match's `mantle_tx.hash`
- `SLOT_WINDOW = 800` (~13 min) starting from `cursor_slot` returned by `query_channel_paged`
- beacon `fix/11-module-channel-publish` has two commits: `d6826a0` (publish_to routing) +
  `d481fc4` (anchor resolution) — both need to be on origin before merging
- zone_sequencer fork: `xAlisher/logos-zone-sequencer-module` branch `fix/publish-to` — merged
  to master; installed .so is at `~/.local/share/.../modules/liblogos_zone_sequencer_module/`
- Explorer "Transaction not found" for OLD inscriptions (72815b1c, b077196f): indexer lag —
  zone-scanner still catching up to slot 3692367. Will resolve on its own.
