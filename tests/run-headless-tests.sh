#!/usr/bin/env bash
# Headless unit tests for keeper-basecamp.
#
# Tier 1: no network, no Basecamp required — keeper module only (isolated temp dir).
# Tests all synchronous Q_INVOKABLEs: config, queue state, validation, bridge
# status, inscription queue, paired extensions.
#
# Uses logoscore single-invocation mode (multi -c flags) — current logos-liblogos-0.1.0
# has no -D daemon mode. Calls are batched per test group.
#
# Usage:
#   ./tests/run-headless-tests.sh
#   LOGOSCORE=/path/to/logoscore ./tests/run-headless-tests.sh
#
# Ref: basecamp-skills/skills/logoscore-headless-testing.md

set -euo pipefail

# ── Config ────────────────────────────────────────────────────────────────────

LOGOSCORE="${LOGOSCORE:-}"
MODULES_DIR="${MODULES_DIR:-}"

if [[ "${1:-}" == "--logoscore" ]]; then
    LOGOSCORE="$2"; shift 2
fi

if [[ -z "$LOGOSCORE" ]]; then
    LOGOSCORE=$(find /nix/store -maxdepth 3 -name "logoscore" \
        -path "*/bin/*" 2>/dev/null | head -1)
fi

if [[ -z "$LOGOSCORE" || ! -x "$LOGOSCORE" ]]; then
    echo "ERROR: logoscore not found. Set LOGOSCORE env var." >&2; exit 1
fi

KEEPER_MODULE_DIR="$HOME/.local/share/Logos/LogosBasecamp/modules/keeper"
KEEPER_SO="$KEEPER_MODULE_DIR/keeper_plugin.so"

if [[ ! -f "$KEEPER_SO" ]]; then
    echo "ERROR: keeper_plugin.so not found at $KEEPER_SO" >&2
    echo "       Run: cmake --install build --prefix \$HOME/.local/share/Logos/LogosBasecamp" >&2
    exit 1
fi

# Verify manifest has required variants before wasting time.
MANIFEST="$KEEPER_MODULE_DIR/manifest.json"
if [[ ! -f "$MANIFEST" ]]; then
    echo "ERROR: manifest.json not found at $MANIFEST" >&2; exit 1
fi
for VARIANT in "linux-amd64" "linux-amd64-dev"; do
    if ! python3 -c "
import json, sys
m = json.load(open('$MANIFEST'))
if '$VARIANT' not in m.get('main', {}):
    sys.exit(1)
" 2>/dev/null; then
        echo "ERROR: manifest.json missing variant '$VARIANT' — logoscore will silently skip the module." >&2
        exit 1
    fi
done

if [[ -z "$MODULES_DIR" ]]; then
    MODULES_DIR=$(mktemp -d)
    mkdir -p "$MODULES_DIR/keeper"
    cp -r "$KEEPER_MODULE_DIR/." "$MODULES_DIR/keeper/"
    CLEANUP_MODULES=1
fi

# ── Helpers ───────────────────────────────────────────────────────────────────

PASS=0; FAIL=0

log_pass() { echo "  + $1"; ((PASS++)) || true; }
log_fail() { echo "  FAIL: $1"; ((FAIL++)) || true; }

# Run one or more -c calls against keeper in a single logoscore invocation.
# Returns: one "Method call successful. Result: <json>" line per call.
run_calls() {
    local args=()
    for c in "$@"; do
        args+=("-c" "$c")
    done
    "$LOGOSCORE" -m "$MODULES_DIR" -l keeper "${args[@]}" --quit-on-finish 2>/dev/null || true
}

# Extract the Nth (0-based) result JSON from run_calls output.
# Usage: result=$(get_result N <output>)
get_result() {
    local n="$1"; shift
    local output="$*"
    echo "$output" \
        | grep "^Method call successful. Result:" \
        | sed 's/^Method call successful. Result: //' \
        | sed -n "$((n+1))p"
}

# Extract one result JSON from a single-call run_calls output.
result1() {
    get_result 0 "$@"
}

extract_field() {
    local json="$1" field="$2"
    echo "$json" | python3 -c \
        "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('$field',''))" 2>/dev/null
}

assert_field() {
    local desc="$1" json="$2" field="$3" expected="$4"
    local actual
    actual=$(extract_field "$json" "$field")
    if [[ "$actual" == "$expected" ]]; then log_pass "$desc"
    else log_fail "$desc (expected $field='$expected', got $field='$actual')"; fi
}

assert_has_field() {
    local desc="$1" json="$2" field="$3"
    local ok
    ok=$(echo "$json" | python3 -c \
        "import sys,json; d=json.loads(sys.stdin.read()); print('yes' if '$field' in d else 'no')" 2>/dev/null)
    if [[ "$ok" == "yes" ]]; then log_pass "$desc"
    else log_fail "$desc (field '$field' missing in: $json)"; fi
}

assert_is_array() {
    local desc="$1" json="$2"
    local ok
    ok=$(echo "$json" | python3 -c \
        "import sys,json; d=json.loads(sys.stdin.read()); print('yes' if isinstance(d,list) else 'no')" 2>/dev/null)
    if [[ "$ok" == "yes" ]]; then log_pass "$desc"
    else log_fail "$desc (expected JSON array, got: $json)"; fi
}

assert_empty_array() {
    local desc="$1" json="$2"
    local ok
    ok=$(echo "$json" | python3 -c \
        "import sys,json; d=json.loads(sys.stdin.read()); print('yes' if isinstance(d,list) and len(d)==0 else 'no')" 2>/dev/null)
    if [[ "$ok" == "yes" ]]; then log_pass "$desc"
    else log_fail "$desc (expected [], got: $json)"; fi
}

assert_error_or_false() {
    local desc="$1" json="$2"
    local ok
    ok=$(echo "$json" | python3 -c "
import sys,json
d=json.loads(sys.stdin.read())
ok = 'error' in d or d.get('success') == False
print('yes' if ok else 'no')
" 2>/dev/null)
    if [[ "$ok" == "yes" ]]; then log_pass "$desc"
    else log_fail "$desc (expected error/success:false, got: $json)"; fi
}

assert_success() {
    local desc="$1" json="$2"
    local ok
    ok=$(echo "$json" | python3 -c "
import sys,json
d=json.loads(sys.stdin.read())
ok = d.get('success') == True or ('error' not in d and d != {})
print('yes' if ok else 'no')
" 2>/dev/null)
    if [[ "$ok" == "yes" ]]; then log_pass "$desc"
    else log_fail "$desc (expected success, got: $json)"; fi
}

assert_valid_json() {
    local desc="$1" json="$2"
    local ok
    ok=$(echo "$json" | python3 -c "import sys,json; json.loads(sys.stdin.read()); print('yes')" 2>/dev/null || echo "no")
    if [[ "$ok" == "yes" ]]; then log_pass "$desc"
    else log_fail "$desc (invalid JSON or empty: '$json')"; fi
}

# Write a temp param file, return its path. Cleaned up on EXIT.
TMPDIR_PARAMS=$(mktemp -d)
trap 'rm -rf "$TMPDIR_PARAMS"; [[ -n "${CLEANUP_MODULES:-}" ]] && rm -rf "$MODULES_DIR"' EXIT

param_file() {
    local name="$1" content="$2"
    local path="$TMPDIR_PARAMS/$name"
    printf '%s' "$content" > "$path"
    echo "$path"
}

# A valid 64-char lowercase hex pubkey (synthetic Ed25519 pubkey — not a real key).
VALID_PUBKEY="0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20"

# ── Start ─────────────────────────────────────────────────────────────────────

echo ""
echo "keeper-basecamp headless unit tests"
echo "======================================"
echo "logoscore: $LOGOSCORE"
echo "module:    $KEEPER_SO"
echo ""

# ── 0. Setup: clear any stale persisted state from previous runs ──────────────
# Note: clearQueue() has a known bug — it removes keeper-queue.json but saveQueue()
# writes to queue.json. So we remove the actual file directly from shell.
echo "Setup (clear stale state)"

# Remove stale state from all logoscore keeper persistence dirs.
# Search by directory name "keeper" — not by file presence — so log-only
# dirs (no queue.json) are also cleaned. Exclude Basecamp's module_data dir.
while IFS= read -r KEEPER_DATA_DIR; do
    rm -f "$KEEPER_DATA_DIR/queue.json"
    rm -f "$KEEPER_DATA_DIR/keeper-log.json"
    echo "  cleared $KEEPER_DATA_DIR"
done < <(find "$HOME/.local/share" -maxdepth 4 -type d \
    -name "keeper" ! -path "*/LogosBasecamp/*" 2>/dev/null)

echo "  (if no dirs listed above, state was already clean)"
echo ""

# ── 1. Config read ────────────────────────────────────────────────────────────
echo "Config"

OUT=$(run_calls "keeper.getConfig()")
R=$(result1 "$OUT")
assert_has_field "getConfig has maxFilesPerItem" "$R" "maxFilesPerItem"
assert_has_field "getConfig has skipDerivatives"  "$R" "skipDerivatives"

echo ""

# ── 2. Config round-trip ──────────────────────────────────────────────────────
echo "Config round-trip"

P=$(param_file "setconfig.json" '{"maxFilesPerItem":5}')
OUT=$(run_calls "keeper.setConfig(@$P)" "keeper.getConfig()")
mapfile -t RES < <(echo "$OUT" | grep "^Method call successful. Result:" | sed 's/^Method call successful. Result: //')
R="${RES[1]:-}"
assert_field "setConfig round-trip: maxFilesPerItem=5" "$R" "maxFilesPerItem" "5"

# Restore default
P2=$(param_file "restore.json" '{"maxFilesPerItem":20}')
run_calls "keeper.setConfig(@$P2)" --quit-on-finish >/dev/null 2>&1 || true

echo ""

# ── 3. Queue and log — empty on fresh start ────────────────────────────────────
echo "Queue / log (fresh state)"

OUT=$(run_calls "keeper.getQueue()" "keeper.getLog()")
mapfile -t RES < <(echo "$OUT" | grep "^Method call successful. Result:" | sed 's/^Method call successful. Result: //')
R0="${RES[0]:-}"
R1="${RES[1]:-}"
assert_is_array    "getQueue returns array"          "$R0"
assert_empty_array "getQueue is empty on fresh start" "$R0"
assert_is_array    "getLog returns array"             "$R1"
assert_empty_array "getLog is empty on fresh start"   "$R1"

echo ""

# ── 4. Bridge status ──────────────────────────────────────────────────────────
echo "Bridge status"

OUT=$(run_calls "keeper.getBridgeStatus()")
R=$(result1 "$OUT")
assert_has_field "getBridgeStatus has 'running' field" "$R" "running"

echo ""

# ── 5. Inscription queue ──────────────────────────────────────────────────────
echo "Inscription queue"

OUT=$(run_calls "keeper.getInscriptionQueue()")
R=$(result1 "$OUT")
assert_is_array "getInscriptionQueue returns array" "$R"

echo ""

# ── 6. preserveItem — invalid identifier ─────────────────────────────────────
# Use a URL that contains '/' but doesn't match the archive.org pattern.
# parseIdentifier returns empty → immediate error, no network call.
echo "preserveItem — input validation"

P=$(param_file "invalid_url.json" '"https://example.com/not-ia"')
OUT=$(run_calls "keeper.preserveItem(@$P)")
R=$(result1 "$OUT")
assert_error_or_false "preserveItem non-IA URL → error/success:false" "$R"

echo ""

# ── 7. cancelItem — nonexistent identifier ────────────────────────────────────
echo "cancelItem — nonexistent id"

P=$(param_file "bogus_id.json" '"bogus-nonexistent-id"')
OUT=$(run_calls "keeper.cancelItem(@$P)")
R=$(result1 "$OUT")
assert_valid_json "cancelItem nonexistent id → valid JSON (no crash)" "$R"

echo ""

# ── 8–10. Paired extensions (feature-gated) ───────────────────────────────────
# addPairedExtension/removePairedExtension/getPairedExtensions were added in
# the feature/signed-auth branch and are not yet in main. Check at runtime
# whether the installed .so exposes them before running these tests.
echo "Paired extensions — capability check"

PAIRED_OUT=$(run_calls "keeper.getPairedExtensions()")
HAS_PAIRED=$(echo "$PAIRED_OUT" | python3 -c \
    "import sys; print('yes' if 'Method call successful' in sys.stdin.read() else 'no')" 2>/dev/null)

if [[ "$HAS_PAIRED" == "yes" ]]; then
    echo "  installed .so has paired extension APIs — running tests"
    echo ""

    echo "Paired extensions — validation"
    P=$(param_file "short_key.json" '"tooshort"')
    OUT=$(run_calls "keeper.addPairedExtension(@$P)")
    R=$(result1 "$OUT")
    assert_error_or_false "addPairedExtension short key → error/success:false" "$R"
    echo ""

    echo "Paired extensions — list"
    R=$(result1 "$PAIRED_OUT")
    assert_is_array "getPairedExtensions returns array" "$R"
    echo ""

    echo "Paired extensions — add + remove lifecycle"
    # Pass pubkey as raw string (no JSON quotes) — @file passes contents verbatim
    P=$(param_file "valid_key.txt" "$VALID_PUBKEY")
    OUT=$(run_calls "keeper.addPairedExtension(@$P)" "keeper.removePairedExtension(@$P)")
    mapfile -t RES < <(echo "$OUT" | grep "^Method call successful. Result:" | sed 's/^Method call successful. Result: //')
    R0="${RES[0]:-}"
    R1="${RES[1]:-}"
    assert_success "addPairedExtension valid key → success"   "$R0"
    assert_success "removePairedExtension valid key → success" "$R1"
    echo ""
else
    echo "  (skipped — paired extension APIs not in installed .so)"
    echo ""
fi

# ── 11. clearQueue / clearLog — no crash ─────────────────────────────────────
echo "clearQueue / clearLog (empty)"

OUT=$(run_calls "keeper.clearQueue()" "keeper.clearLog()")
mapfile -t RES < <(echo "$OUT" | grep "^Method call successful. Result:" | sed 's/^Method call successful. Result: //')
R0="${RES[0]:-}"
R1="${RES[1]:-}"
assert_valid_json "clearQueue on empty queue → valid JSON (no crash)" "$R0"
assert_valid_json "clearLog on empty log → valid JSON (no crash)"     "$R1"

echo ""

# ── Summary ───────────────────────────────────────────────────────────────────
echo "======================================"
echo "Results: $PASS passed, $FAIL failed"
echo ""

if [[ $FAIL -gt 0 ]]; then echo "FAIL"; exit 1
else echo "PASS"; exit 0; fi
