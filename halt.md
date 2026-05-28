# Halt — 2026-05-26

## Where we stopped

Full session implementing beacon#11 fix (module sub-channel inscriptions). Confirmed keeper
inscriptions now route to the correct keeper channel (`d6ad1ad7...`) instead of the primary
beacon channel (`47945cf8...`) — verified via node logs showing two distinct channels.

Discovered a second issue: `zone_publish` (used by `publish_to`) returns a **zone message
hash**, not a `mantle_tx.hash`. The explorer URL requires the `mantle_tx.hash`. Implemented
Option A: beacon QML now defers `confirmInscription` for module channels and polls
`resolveAnchorTxs()` every 10s — when zone message appears in finalized chain, it queries
`/cryptarchia/blocks` via new `BeaconPlugin::findAnchorTx()` to find the real `mantle_tx.hash`,
then confirms with the correct ID.

Basecamp restarted with new code installed. **Not yet live-tested** — need to trigger a fresh
keeper inscription and watch the resolution flow end-to-end.

## Current state

- **keeper-basecamp** branch: `master` — last commit: `a75310a`
- **beacon-basecamp** branch: `fix/11-module-channel-publish` — last commit: `d481fc4`
  (1 commit ahead of origin — needs push)
- Build status: passing (both built, installed)
- Open review: beacon PR#13 merged; anchor-tx resolution commit (`d481fc4`) not yet pushed

## Next steps (in order)

1. **Push** beacon branch: `cd beacon-basecamp && git push origin fix/11-module-channel-publish`
2. **Live-test anchor resolution**: paste fresh IA URL in keeper → watch beacon activity log for
   "zone msg published — awaiting on-chain anchor…" then "anchor resolved: `<hash>`…"
3. **Verify green URL**: once resolved, confirm keeper shows clickable URL and
   `https://testnet.blockchain.logos.co/web/explorer/transactions/<hash>` resolves in explorer
4. **Open PR** for beacon `d481fc4` (or merge to main if tested)
5. **Implement keeper plan** (`~/.claude/plans/pure-gliding-zebra.md`):
   - Resume `pollForTxHash` after restart in `loadQueue()`
   - "Clear" button in keeper QML Log section
   - Remove debug `qDebug` from `pollForTxHash`

## Blockers

- `resolveAnchorTxs` not yet live-tested — anchor tx timing depends on zone validators;
  may need slot range tuning if the 800-slot window is too narrow or `query_channel` returns
  messages before the ChannelInscribe tx appears in blocks

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
