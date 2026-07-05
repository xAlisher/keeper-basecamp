# Retro Log


## fail 2026-07-05 — invented UI sizing instead of copying a shipped sibling's exact convention

[process] During keeper-ui polish (inscription-format cogwheel), I **mirrored the STRUCTURE** of proven
modules (receiver cogwheel, delivery-demo LogosComboBox) but **re-guessed the DIMENSIONS**: Keep button
`Layout.preferredHeight: 36`, a fixed `28x28` cogwheel, an input `placeholderTextColor` override, an
inline dropdown. The user corrected it 3× (dropdown→cogwheel, badge→receiver-pattern, then sizes).
Root cause: I copy the component *choice* from a reference but not its *sizing rules*. The archiver
(**ia/archive_ui**) had the exact, shipped convention right there: **`LogosButton { implicitWidth: X;
implicitHeight: 40 }`** (never `Layout.preferredHeight`/`height`), **`LogosTextField` self-sizes** (no
height or placeholderColor override), **gear box `implicitWidth/Height: <badge>.implicitHeight`** (matches
the status badge), and a file-top rule: *"Inside layouts use implicitHeight, never height."*
Fix: when re-skinning, open the nearest **already-shipped DS sibling** and copy its exact sizing
(implicitHeight values, self-sizing inputs, gear=badge-height), not just which component to use.

## fail 2026-07-05 — anchors.centerIn doesn't visually center a glyph in a box

[process] The cogwheel ⚙ used `LogosText { anchors.centerIn: parent }` (copied from ia/receiver) — but
`centerIn` centers the Text *item's bounding box*, not the glyph within it, so a single glyph reads
off-center. Fix: `anchors.fill: parent` + `horizontalAlignment: AlignHCenter` + `verticalAlignment:
AlignVCenter`. Rule: to center a single glyph in a framed box, fill + align both axes.

## Week of 2026-07-05 — DWeb proof-links + copy-feedback + multi-agent iso

### Wins
- [project] getSourceChannel primitive unblocked keeper's proof link cleanly. keeper's beaconLogMap has txHash but no channelId, and explorer.logos.live routes on /#<channel>. Added ONE beacon core method getSourceChannel(source)=derive_channel_id(SHA256(masterKey+source)); keeper fetches it once (universal, sync-safe) → links every confirmed row to explorer.logos.live/#<channel>. No key re-derivation in keeper. (keeper#41, beacon merged)
- [project] The beacon-vs-keeper copy-feedback divergence pinpointed a general QML pitfall: identical delegate-local "copied ✓" flash worked in beacon (updates rows in place) but NOT keeper (logModel.clear()+append() every 2s recreates delegates → flash wiped instantly). Root-level flag keyed by cid + a binding fixed it. → extracted delegate-state-wiped-by-model-rebuild.
- [process] Independent node-side verification during live E2E: watched the Paradox channel tip advance (423216→428365) while the user drove the UI — confirmed the fresh op landed on-chain independent of UI state.

### Fails
- [project] explorer.logos.live/txs/<hash> → 404. Repointed the explorer BASE to explorer.logos.live but the keeper link still built the old /txs/<txHash> path (explorer routes /#<channel>). keeper reads the base from beacon config, so it silently got the new domain + old path. Root cause: changed the base URL without changing the path FORMAT in every consumer. Fix: build /#<channel> from the per-module channel.
- [project] Copy feedback "worked in beacon, not keeper" — applied the same delegate-local text-flash to both without checking each UI's model-update strategy. keeper rebuilds the ListModel every refresh → delegates recreated → flash gone in <2s. Root cause: assumed two UIs share the same model strategy; delegate-local imperative state can't survive clear()+append(). Fix: root-level flag + binding.
- [process] A linter reformatted keeper Main.qml (literal ⧉/✓ → ⧉/✓ escapes) BETWEEN my Edit-tool edit and a later python string-replace, so the python (matching literals) silently missed one button (0 replacements, no error). Root cause: assumed file bytes were unchanged between edits. Fix: grep the actual bytes first when a formatter may have run between edits.
