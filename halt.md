# Halt — 2026-07-10

## ▶ Resume this session
```bash
cd ~/basecamp/modules/keeper-basecamp && claude --resume 1e8dfb1b-c6d0-4a34-94ee-d7d6726332e9
```
Fallback: `claude --continue`.

## Where we stopped
Landed the **keeper#43 standard-metadata strawman** (OpenSea-style superset in IA-Archiver/collection mode)
and deployed it. keeper 0.2.0 (core + ui) is the **latest** and is what's installed in BC v0.2.1. Paradox
has since proposed the **LORE** metadata standard — we agreed to migrate to it (issue **#44**), which also
resolves the "wrong JSON syntax" (our double-encoded stringified `label`).

## Current state
- Branch: **main** · Last commit: `ebf24c6` (Merge: standard OpenSea-style inscription metadata [#43])
- The strawman lives in `src/keeper_impl.cpp` `inscribeToBeacon` (~line 520): adds `name`/`description`/`external_url`/`image`/`attributes`/`content` alongside the legacy fields.
- Build: **passing** · installed 0.2.0 = main
- Open review: none

## Next steps (in order)
1. **#44** — emit **LORE** (`standard:"LORE"`, `resources[]` with `locator{protocol,id}` + attributes, object `attributes[]`); drop the stringified `label`. Co-defining with Paradox.
2. **#39 (also flagged live this session)** — the queue **"Clear" button is giant**; make it a compact icon button. Exact spot: `keeper-ui/qml/Main.qml:480-488` (`LogosButton text:"Clear"`, after a `Layout.fillWidth` spacer) → use the trash-glyph pattern at `Main.qml:610` or a `LogosIconButton`, width-constrained. (Diagnosis posted on #39.)
3. Proof-link cleanups (#40/#41/#38 → explorer.logos.live), subscribed-channels feed (#27).

## Blockers
- LORE emit (#44) — converging on the spec with Paradox first (LORE v0.1 draft in the Discord dWeb thread); reply drafted at `~/infra/dweb/paradox-lore-reply.txt`.

## Context that's hard to re-derive
- keeper inscribes a **per-module channel** `derive_channel_id(SHA256(masterKey+"keeper"))` (`dcab09a0…`); the master key is **keycard-only / in-memory**, not extractable headlessly.
- The **collection (IA-Archiver) mode** = one inscription per item, `cid:"ia:<id>"` + the metadata label. The archiver (ia-basecamp) reads it; ia#46 will read LORE.
- LORE example (from Paradox) uses our exact `Free_as_in_Freedom` item — good starting template for #44.
