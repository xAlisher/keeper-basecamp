# Halt — 2026-05-28

## Where we stopped

Implemented `feature/logos-messaging` — replaces localhost HTTP bridge with Logos Messaging
(delivery_module IPC + Ed25519 signature verification). PR #8 open, handoff posted, awaiting
Senty review. Extension repo created at `xAlisher/keeper-messaging-extension`.

**Merge gate: test before merge** — do NOT merge PR #8 to main after Senty review; live-test
first (build → install → pair extension → click Keep → verify queued in keeper UI).

## Current state

- **keeper-basecamp** branch: `feature/logos-messaging` — commit `9ea1348`, PR #8 open
- **keeper-messaging-extension**: `xAlisher/keeper-messaging-extension` main, commit `76e9d2b`
- Build status: NOT YET BUILT on new branch
- Open issues: #3 (pollForTxHash restart), #4 (inscription label), #5 (clear log button) — deferred

## Next steps (in order)

1. **Senty review**: `/codex:review --base main` on PR #8
2. **Fix any HIGH/MEDIUM findings** from Senty
3. **Build**: `nix develop` in keeper-basecamp on `feature/logos-messaging`
4. **Install**: lgx install keeper
5. **Live-test**:
   - Logos Messaging pill shows connecting → online
   - Paste 64-hex pubkey from extension popup → Pair → appears in list
   - Click Keep on archive.org → item appears in keeper queue
   - Status updates relay to button label
   - Confirm v1 extension cannot reach keeper (no HTTP listener)
   - Craft unknown-pubkey message → confirm keeper ignores it
6. **Merge PR #8** (only after live-test passes)
7. **Install extension npm**: `npm install && npm run build` in keeper-messaging-extension

## Blockers

- `delivery_module` must be installed separately (absent from AppImage v173 per skill
  `delivery-module-messaging`) — required before any end-to-end test
- libsodium link via PkgConfig not yet sandbox-tested

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
