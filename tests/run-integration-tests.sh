#!/usr/bin/env bash
# Integration tests for keeper-basecamp — full preservation pipeline.
#
# Tier 2: requires Basecamp running (keeper loaded with HTTP bridge on port 7355).
# Queues a real IA item via the keeper HTTP bridge, polls until done, then
# asserts txHash in the keeper log and forms the explorer URL.
#
# Uses keeper HTTP bridge (localhost:7355) for live operations and direct log
# file reads for txHash (bridge doesn't expose it). No logoscore needed.
#
# Usage:
#   ./tests/run-integration-tests.sh
#   ./tests/run-integration-tests.sh --item <ia_identifier>
#
# Extended blockchain verification (optional, requires Tailscale):
#   VERIFY_BLOCKCHAIN=1 ./tests/run-integration-tests.sh

set -euo pipefail

# ── Config ────────────────────────────────────────────────────────────────────

# Small stable IA item. Can override with --item or TEST_ITEM env var.
TEST_ITEM="${TEST_ITEM:-anarchy_Cypherpunk_Manifesto}"
VERIFY_BLOCKCHAIN="${VERIFY_BLOCKCHAIN:-0}"
BRIDGE_URL="http://127.0.0.1:7355"
POLL_INTERVAL=10
TIMEOUT_SECS=$((15 * 60))  # 15 minutes
# Testnet blockchain node (Tailscale — local testnet only)
NODE_URL="http://100.108.127.3:8080"
EXPLORER_BASE="https://testnet.blockchain.logos.co/web/explorer/transactions/"

if [[ "${1:-}" == "--item" ]]; then
    TEST_ITEM="$2"; shift 2
fi

# ── Helpers ───────────────────────────────────────────────────────────────────

PASS=0; FAIL=0

log_pass() { echo "  + $1"; ((PASS++)) || true; }
log_fail() { echo "  FAIL: $1"; ((FAIL++)) || true; }
log_warn() { echo "  WARN: $1"; }

extract_field() {
    local json="$1" field="$2"
    echo "$json" | python3 -c \
        "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('$field',''))" 2>/dev/null
}

# Read the keeper log file from Basecamp's module_data dir.
keeper_log_file() {
    find "$HOME/.local/share/Logos/LogosBasecamp/module_data/keeper" \
        -name "keeper-log.json" 2>/dev/null | head -1
}

# Find item in keeper-log.json by id, return JSON object or empty string.
find_log_entry() {
    local item_id="$1"
    local log_file
    log_file=$(keeper_log_file)
    if [[ -z "$log_file" || ! -f "$log_file" ]]; then
        echo ""
        return
    fi
    python3 -c "
import json
items = json.load(open('$log_file'))
for item in items:
    if item.get('id') == '$item_id':
        import json as j
        print(j.dumps(item))
        break
" 2>/dev/null || echo ""
}

# ── Preflight ─────────────────────────────────────────────────────────────────

echo ""
echo "keeper-basecamp integration tests"
echo "======================================"
echo "test item: $TEST_ITEM"
echo "bridge:    $BRIDGE_URL"
echo ""
echo "Preflight: checking keeper HTTP bridge..."

QUEUE_R=$(curl -sf "$BRIDGE_URL/queue" 2>/dev/null || echo "")
if [[ -z "$QUEUE_R" ]]; then
    echo "ERROR: keeper HTTP bridge not reachable at $BRIDGE_URL" >&2
    echo "       Start Basecamp with keeper module loaded first." >&2
    exit 1
fi
echo "  keeper bridge: OK"

LOG_FILE=$(keeper_log_file)
if [[ -z "$LOG_FILE" ]]; then
    echo "  keeper log: not yet created (OK — will be created on first completed item)"
else
    echo "  keeper log: $LOG_FILE"
fi
echo ""

# ── Step 1: preserveItem ──────────────────────────────────────────────────────
echo "Step 1: POST /preserve  item=$TEST_ITEM"

PRESERVE_R=$(curl -sf -X POST "$BRIDGE_URL/preserve" \
    -H "Content-Type: application/json" \
    -d "{\"url\":\"$TEST_ITEM\"}" 2>/dev/null || echo "{}")

echo "  response: $PRESERVE_R"

QUEUED=$(echo "$PRESERVE_R" | python3 -c "
import sys,json
d=json.loads(sys.stdin.read())
ok = d.get('success') == True or \
     'id' in d or \
     'queued' in str(d.get('status','')) or \
     'queued' in str(d.get('message',''))
print('yes' if ok else 'no')
" 2>/dev/null)

if [[ "$QUEUED" == "yes" ]]; then
    log_pass "preserveItem queued successfully"
else
    log_fail "preserveItem did not queue: $PRESERVE_R"
    echo ""
    echo "======================================"
    echo "Results: $PASS passed, $FAIL failed"
    echo "FAIL (could not queue item)"
    exit 1
fi
echo ""

# ── Step 2: poll /status/<id> until done/failed ───────────────────────────────
echo "Step 2: polling /status/$TEST_ITEM  (timeout: ${TIMEOUT_SECS}s, interval: ${POLL_INTERVAL}s)"

ELAPSED=0
FINAL_STATUS=""
LAST_STATUS=""

while [[ $ELAPSED -lt $TIMEOUT_SECS ]]; do
    STATUS_R=$(curl -sf "$BRIDGE_URL/status/$TEST_ITEM" 2>/dev/null || echo "{}")
    ITEM_STATUS=$(extract_field "$STATUS_R" "status")

    if [[ "$ITEM_STATUS" != "$LAST_STATUS" && -n "$ITEM_STATUS" ]]; then
        printf "  [%4ds] status: %s\n" "$ELAPSED" "$ITEM_STATUS"
        LAST_STATUS="$ITEM_STATUS"
    fi

    if [[ "$ITEM_STATUS" == "done" || "$ITEM_STATUS" == "failed" || "$ITEM_STATUS" == "cancelled" ]]; then
        FINAL_STATUS="$ITEM_STATUS"
        break
    fi

    sleep "$POLL_INTERVAL"
    ELAPSED=$((ELAPSED + POLL_INTERVAL))
done

if [[ -z "$FINAL_STATUS" ]]; then
    log_fail "pipeline did not complete within ${TIMEOUT_SECS}s (last status: '${LAST_STATUS:-unknown}')"
    echo ""
    echo "======================================"
    echo "Results: $PASS passed, $FAIL failed"
    echo "FAIL (timeout)"
    exit 1
fi

echo ""

# ── Step 3: assert final state ────────────────────────────────────────────────
echo "Step 3: asserting final state  (status=$FINAL_STATUS)"

if [[ "$FINAL_STATUS" == "failed" || "$FINAL_STATUS" == "cancelled" ]]; then
    LOG_ENTRY=$(find_log_entry "$TEST_ITEM")
    ITEM_ERROR=""
    if [[ -n "$LOG_ENTRY" ]]; then
        ITEM_ERROR=$(extract_field "$LOG_ENTRY" "error")
    fi
    log_fail "item $FINAL_STATUS: ${ITEM_ERROR:-'(no error detail)'}"
    echo ""
    echo "======================================"
    echo "Results: $PASS passed, $FAIL failed"
    echo "FAIL (item $FINAL_STATUS)"
    exit 1
fi

log_pass "item reached status: done"

# Get collectionCid from /status (available in bridge response)
STATUS_R=$(curl -sf "$BRIDGE_URL/status/$TEST_ITEM" 2>/dev/null || echo "{}")
COLLECTION_CID=$(extract_field "$STATUS_R" "cid")

# Get txHash from keeper log file (not exposed via bridge)
LOG_ENTRY=$(find_log_entry "$TEST_ITEM")
TX_HASH=""
if [[ -n "$LOG_ENTRY" ]]; then
    TX_HASH=$(extract_field "$LOG_ENTRY" "txHash")
fi

# txHash must be non-empty 64-char lowercase hex
if [[ -n "$TX_HASH" ]] && echo "$TX_HASH" | grep -qE '^[0-9a-f]{64}$'; then
    log_pass "txHash is 64-char lowercase hex: $TX_HASH"
else
    log_fail "txHash invalid or missing (got: '$TX_HASH')"
fi

# collectionCid must start with "ia:"
if [[ "$COLLECTION_CID" == ia:* ]]; then
    log_pass "collectionCid starts with 'ia:': $COLLECTION_CID"
else
    log_fail "collectionCid does not start with 'ia:' (got: '$COLLECTION_CID')"
fi

# Explorer URL
if [[ -n "$TX_HASH" ]]; then
    EXPLORER_URL="${EXPLORER_BASE}${TX_HASH}"
    if [[ -n "$EXPLORER_URL" ]]; then
        log_pass "explorer URL formed: $EXPLORER_URL"
    else
        log_fail "explorer URL is empty"
    fi
fi

echo ""

# ── Step 4: blockchain verification (optional, VERIFY_BLOCKCHAIN=1) ───────────
if [[ "$VERIFY_BLOCKCHAIN" == "1" && -n "$TX_HASH" ]]; then
    echo "Step 4: blockchain verification  (node: $NODE_URL)"

    SLOT_INFO=$(curl -sf "${NODE_URL}/cryptarchia/info" 2>/dev/null || echo "{}")
    CURRENT_SLOT=$(extract_field "$SLOT_INFO" "slot")

    if [[ -n "$CURRENT_SLOT" && "$CURRENT_SLOT" -gt 0 ]]; then
        echo "  current slot: $CURRENT_SLOT"
        SCAN_FROM=$(( CURRENT_SLOT > 20000 ? CURRENT_SLOT - 20000 : 0 ))
        FOUND=0
        WINDOW=5000
        slot=$SCAN_FROM

        while [[ $slot -lt $CURRENT_SLOT ]]; do
            slot_end=$(( slot + WINDOW < CURRENT_SLOT ? slot + WINDOW : CURRENT_SLOT ))
            BLOCKS=$(curl -sf \
                "${NODE_URL}/cryptarchia/blocks?slot_from=${slot}&slot_to=${slot_end}" \
                2>/dev/null || echo "[]")
            if echo "$BLOCKS" | python3 -c "
import sys,json
blocks = json.loads(sys.stdin.read())
for block in blocks:
    for tx in block.get('transactions', []):
        if tx.get('hash','') == '$TX_HASH':
            sys.exit(0)
sys.exit(1)
" 2>/dev/null; then
                FOUND=1
                break
            fi
            slot=$slot_end
        done

        if [[ $FOUND -eq 1 ]]; then
            log_pass "txHash found on chain in slots $SCAN_FROM–$CURRENT_SLOT"
        else
            log_warn "txHash NOT found in scanned slots $SCAN_FROM–$CURRENT_SLOT (may be in older blocks)"
        fi
    else
        log_warn "could not reach blockchain node or slot=0 — skipping chain scan"
    fi
    echo ""
else
    echo "Step 4: blockchain verification skipped (set VERIFY_BLOCKCHAIN=1 to enable)"
    echo ""
fi

# ── Summary ───────────────────────────────────────────────────────────────────
echo "======================================"
echo "Results: $PASS passed, $FAIL failed"
echo ""

if [[ $FAIL -gt 0 ]]; then echo "FAIL"; exit 1
else echo "PASS"; exit 0; fi
