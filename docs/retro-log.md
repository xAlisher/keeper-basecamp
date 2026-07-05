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
