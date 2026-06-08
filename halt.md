# Halt — 2026-06-08

## Where we stopped

Published LGX releases for all 5 IA loop modules (keeper, stash, beacon, cord, keycard),
updated keeper README with LGX install path, corrected zone sequencer / storage module
dependencies, and added keeper-extension repo link. Session was mostly docs + packaging —
no code changes to any module. Also had a strategic discussion about module consolidation
(merge keeper+stash+beacon) and Berlin dry run readiness.

## Current state

- Branch: main
- Last commit: 3b819f2 docs: fix explorer link framing, zone-seq/storage deps, add extension repo link
- Build status: passing (all LGX builds verified; Tier 1 + integration confirmed Jun 7)
- Open review: none

## Next steps (in order)

1. **cord: "keep from feed"** — add Keep button to Cord's dispatch log so users can
   preserve items from others' channels. New feature, ~1-2 weeks. Needed before Berlin.
2. **keeper #25** — UX: inscription lifecycle states — progress bar, slots countdown
   (keeper QML needs slotFrom/libAtSubmit from beaconLogMap; beacon already has this data)
3. **keeper #20** — Add copy button to activity log entries (CID, identifier)
4. **keeper #18** — Re-inscribe duplicate: bypass beacon dedup when user explicitly retries
5. **keeper #10/#11** — Fix popup text + add Settings panel (small UI, low-risk)
6. **keeper #9** — pairing reentrancy: add pollBusy guard to doAdd() callModule
7. **keeper #15** — Timestamps don't match local time (timezone fix)
8. **keeper #12/#13** — HTTP bridge security: CORS wildcard + no auth on /queue /status
9. **stash fix/caller-source-dropped** — uncommitted changes to src/plugin/StashPlugin.cpp
   and StashPlugin.h need review and commit before they're lost

## Blockers

- none

## Context that's hard to re-derive

- **Both root causes (zone_sequencer hash bug + sneg minority fork) were fixed on Jun 7.**
  There was a stale plan file (mossy-tinkering-russell.md) re-raising them as open — it was
  deleted this session. Do not re-raise these issues.

- **Module consolidation decision**: merge keeper+stash+beacon into one module makes sense
  post-Berlin (eliminates QML-mediated IPC workarounds entirely). logos-notes dependency on
  stash/beacon is irrelevant to the IA loop decision. Not before Berlin — too risky.

- **Berlin message sent to mart1n/jonny/vpavlin**: recommendation is keep modular, focus on
  cord "keep from feed" this week, Alisher attending LANBerlin first half of day.

- **LGX releases published**: all 5 modules now have GitHub Releases with core + UI LGX files.
  beacon_ui and cord_ui had no Nix flake before this session — added mkLogosQmlModule flakes.
  cord_ui had empty icon field that broke the build — fixed to point at Cord_sidebar.png.

- **zone sequencer**: NOT shipped with AppImage. From vpavlin/zone-sequencer-module, built
  with xAlisher/zone-sequencer-rs fork (stale-checkpoint fix). storage_module must be
  vpavlin/logos-storage-module at v0.3.2 (9552adf) — newer versions deadlock uploadUrl.

- **stash WIP**: src/plugin/StashPlugin.cpp and StashPlugin.h have uncommitted caller-source
  changes on main (git stash was popped after LGX build). These are in-progress work, do not
  discard.
