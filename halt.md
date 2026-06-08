# Halt — 2026-06-08

## Where we stopped

Completed a full post-merge retro session covering keeper, beacon, stash, and cord.
Extracted 3 new basecamp-skills recipes (zone-seq-data-bytes-not-path, zone-seq-no-checkpoint-one-shot,
zone-seq-sleep-before-destroy), updated ipc-client-eager-init with the permanent QRO failure mode,
created PROJECT_KNOWLEDGE.md for keeper and beacon, rewrote READMEs for all 4 modules, triaged and
closed all fixed GitHub issues across all repos, and fixed both open cord bugs (node URL persistence +
dispatch log live-update). Session ended cleanly — no in-progress work.

## Current state

- Branch: main
- Last commit: 191cb40 docs: rewrite README — full install recipe, dependencies, build, tests, IPC architecture
- Build status: passing (Tier 1 17/17, integration confirmed working Jun 7)
- Open review: none

## Next steps (in order)

1. **keeper #25** — UX: inscription lifecycle states — progress bar, slots countdown, copy URL
   (keeper QML needs slotFrom/libAtSubmit from beaconLogMap; beacon already has this data)
2. **keeper #20** — Add copy button to activity log entries (CID, identifier)
3. **keeper #18** — Re-inscribe duplicate: bypass beacon dedup when user explicitly retries
4. **keeper #10/#11** — Fix popup text + add Settings panel (small UI work, low-risk)
5. **keeper #9** — pairing reentrancy: add pollBusy guard to doAdd() callModule
6. **keeper #15** — Timestamps don't match local time (timezone fix)
7. **keeper #12/#13** — HTTP bridge security: CORS wildcard + no auth on /queue /status
8. **beacon #16** — Add per-entry copy button to activity log
9. **beacon #9** — Adopt logos-design-system tokens (stash #17 same)
10. **keeper #6/#7** — Logos Messaging migration (large, deferred)

## Blockers

- none

## Context that's hard to re-derive

- cord dispatch log bug root cause: `dispatchLogModel.insert` was gated on `res && res.ok` from
  `recordDispatch`; if that call returned anything other than `{"ok":true}` (duplicate, encoding issue),
  the live insert was silently skipped. Fix: decouple insert from recordDispatch, check by messageId.

- getClient() permanently broken in keeper: not a timing issue, not fixable with retry. QML-mediated
  IPC (pendingUpload_ pattern) is the confirmed working path. Never attempt getClient() in keeper again.

- zone_sequencer_publish() return value vs explorer hash: the Python FFI returns a Poseidon2 hash that
  differs from mantle_tx.hash. beacon's findExplorerTxHash 2-step is the correct approach for explorer
  URLs. The Logos QML module publish() returns the correct hash; the raw C FFI does not.

- install-portable vs install: ALWAYS use install-portable for keeper. The plain install target embeds
  Nix store RPATH → dual Qt heap corruption → std::bad_alloc at runtime. Verify with patchelf.

- stash-basecamp is on branch fix/caller-source-dropped (not main) with uncommitted changes to
  src/plugin/StashPlugin.cpp, StashPlugin.h, .gitignore. The README was cherry-picked to main.
  The in-progress changes on the branch are related to caller/source field work — do not discard.
