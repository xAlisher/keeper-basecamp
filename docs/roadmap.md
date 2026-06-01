# Keeper Protocol — Roadmap

---

## Where We Are

Design is complete. Three Senty security audit rounds have been run — all HIGH and MEDIUM findings across all rounds are resolved. Upstream blockers are confirmed. The design is ready for implementation.

---

## Phase 0 — Design (complete)

| Deliverable | Status |
|-------------|--------|
| Protocol design doc (`keeper-protocol-lez.md`) | ✅ Done |
| Whitepaper (`keeper-whitepaper.md`) | ✅ Done |
| Market research + competitive positioning | ✅ Done |
| SPEL framework upstream research | ✅ Done |
| Upstream blocker identification + workarounds | ✅ Done |
| Dev feedback integration (vpavlin, Vaclav) | ✅ Done |
| Privacy analysis — options A–F, ZK tradeoffs | ✅ Done |
| Channel Subscriptions design (on-chain PDAs) | ✅ Done |
| v1 / v2 split (volunteer model vs reward model) | ✅ Done |
| Senty security audit — round 1 (9 findings, 9 fixed) | ✅ Done |
| Senty security audit — round 2 (6 findings, 6 fixed) | ✅ Done |
| Senty security audit — round 3 (6 findings, 6 fixed) | ✅ Done |
| LEZ / Logos Storage dev design review | ⏳ To request |

---

## Upstream status

### Blocker A — Multi-seed PDA
**Status: RESOLVED.**
vpavlin confirmed multi-seed PDAs (`pda = [literal("x"), account("y"), arg("z")]`) work today in SPEL. No workaround needed. All PDAs use native macro syntax.

### Blocker B — Stable token transfer
**Status: PARTIALLY CONFIRMED.**
ChainedCall format confirmed by vpavlin. Stable token program ID on testnet still pending (r4bbit to confirm). Workaround in place: internal ledger model (`claimable_balance` + `withdraw` instruction) for v1. Upgrade B replaces this when program ID is confirmed.

### `logos-co/spel#226` — ClockContext
**Status: PROPOSED, NOT LANDED.**
Exposes `block_id` + `timestamp` per-instruction. When it lands:
- `verify_holding` gains a v2 path: trust-based `block_number: u64` parameter removed
- `challenge_holding`, `finalize_challenge`, `take_monthly_snapshot` updated similarly
- Per-challenger cooldown rate limit enabled (requires block-height comparison)

---

## Protocol model

### v1 — Volunteer model (build now)
No token mechanics. No registration fees. No holding deposits. Preservers earn `keeper_score` — a permanent soulbound on-chain record of contribution. Reputation is the incentive. The reward layer is defined in the design doc but inactive.

**16 active v1 instructions:**
`initialize_program`, `create_user`, `register_preservation`, `verify_holding`,
`deregister_item`, `deregister_user`, `withdraw`, `challenge_holding`,
`finalize_challenge`, `open_dispute`, `vote_on_dispute`, `migrate_score`,
`create_channel`, `add_channel_entry`, `verify_channel`, `subscribe_channel`

### v2 — Reward model (activate later)
7 additional instructions activate when the stable reward model launches:
`fund_pool`, `snapshot_user`, `take_monthly_snapshot`, `claim_monthly_reward`,
`create_item_bounty`, `fund_item_bounty`, `claim_item_bounty`

Preservers earn stable pro-rata by active bytes. Pool funded by external contributions (`fund_pool`) and optionally by per-item sponsors (`fund_item_bounty`). Holding deposit per item escrowed at registration; forfeited on failed challenge.

---

## Phase 1 — v1 Implementation

### Step 1 — Singletons

| Instruction | Notes |
|-------------|-------|
| `initialize_program` | Sets all config; no blockers |

### Step 2 — Core preservation flow

| Instruction | Notes |
|-------------|-------|
| `create_user` | Appends to `PreserverRegistry`; stats PDA init |
| `register_preservation` | Multi-seed PDA works natively; no fees in v1 |
| `verify_holding` | Preserver-signed only (H2 fix); trust-based `block_number` in v1 |
| `deregister_item` | `pool` account required to decrement `total_active_bytes` (R3-H2 fix) |
| `deregister_user` | Zeroes `active_bytes` at stats level; caller deregisters items first |
| `withdraw` | Single `ChainedCall` seam; removed when Upgrade B lands |

### Step 3 — Channels

| Instruction | Notes |
|-------------|-------|
| `create_channel` | Init `Channel` + `ChannelRegistry` entry |
| `add_channel_entry` | Inscribes `(ia_id, CID)` as `ChannelEntry` PDA on-chain |
| `verify_channel` | Marks channel as verified by authorized signer |
| `subscribe_channel` | Creates `ChannelSubscription` PDA; client auto-preserves on creation |

### Step 4 — Audit

| Instruction | Notes |
|-------------|-------|
| `challenge_holding` | Increments `open_challenge_count` (H3 fix) |
| `finalize_challenge` | Identity check (H1 fix); awards `keeper_score` to successful challenger (R3-M1 fix) |

### Step 5 — Governance

| Instruction | Notes |
|-------------|-------|
| `open_dispute` | Formal dispute record |
| `vote_on_dispute` | Vote receipt PDA dedup (M3 fix) |
| `migrate_score` | Dual-signed; transfers score history only, not `active_bytes` (H4 + R3-H1 fix) |

---

## Phase 2 — Integration and testing

- Wire Keeper Basecamp client to on-chain program (replace off-chain log with on-chain calls)
- Self-verification cycle: client calls `verify_holding` every 7 days per item
- Channel auto-preservation: client watches `ChannelSubscription` PDAs, preserves new `ChannelEntry` additions
- Integration with Logos Storage `exists()` and `downloadChunks(local=false)` for reachability
- Headless test suite via `logoscore` against Logos testnet

---

## Phase 3 — Testnet deployment

Target: Logos testnet launch (2026).

- Deploy `initialize_program` on testnet; set conservative config values
- Open to early preservers; build leaderboard on `PreserverRegistry` + `keeper_score`
- Seed at least one public channel; test channel subscription and auto-preservation flow
- Monitor: `active_bytes` accumulation, challenge frequency, dispute activity

---

## Upgrade A — Native stable transfer (when stable token program ID confirmed)

Replace the internal ledger model (`claimable_balance` + `withdraw`) with direct `ChainedCall` transfers in each instruction. `withdraw` instruction removed. `UserStats.claimable_balance` field removed. All affected instruction signatures gain one `token_program` account parameter.

---

## Upgrade B — ClockContext (when `spel#226` lands)

Replace trust-based `block_number: u64` parameters with runtime-injected `ClockContext.block_id`. Affects: `verify_holding`, `challenge_holding`, `finalize_challenge`, `take_monthly_snapshot`. Also enables the per-challenger cooldown PDA rate limit in `challenge_holding`.

---

## v2 — Reward model activation

When the economic model is ready to launch:

1. Activate 7 reward instructions (already specified; currently v2-inactive in design doc)
2. Add `holding_deposit` to `PreservationRecord`; escrow on `register_preservation`
3. `finalize_challenge`: switch from reputation-only to token bounty from holding deposit
4. `claim_monthly_reward`: distributes stable pro-rata by `snapshot_active_bytes`
5. Pool seeded via `fund_pool`; item-level demand via `fund_item_bounty`

---

## v2+ — Future work

| Feature | Trigger |
|---------|---------|
| **`community_verify_holding`** | When `spel#226` (`ClockContext`) lands — safe permissionless third-party verification |
| **ZK proof of data possession** | Analogous to Filecoin PoDP; replaces economic deterrent with cryptographic proof |
| **Commit-reveal privacy** | Store `sha256(ia_id || nonce)` instead of plaintext `ia_id`; breaks passive enumeration |
| **Full ZK privacy** | Private PDAs (nullifier key model); requires LEZ ZK verifier primitive |
| **Dispute stake** | Voluntary stable collateral for dispute participants; loser forfeits portion to winner |
| **Registry pagination** | When `PreserverRegistry` approaches ~320k entries |
| **`update_config` instruction** | Governance-controlled parameter updates without redeployment |
| **Per-item bounty snapshot** | Fix M2 bounty race — snapshot bounty balance per month |

---

## Dependencies map

```
Design doc (complete) ───────────────────────────────► Phase 1 implementation
      │
      ├── Senty rounds 1-3 (done) ─────────────────► clear to build
      │
      ├── LEZ dev review ───────────────────────────► design locked
      │
      ├── Multi-seed PDA (CONFIRMED) ───────────────► no workaround needed
      │
      ├── ChainedCall format (CONFIRMED) ───────────► v1 ledger workaround
      │
      ├── Stable token program ID (pending r4bbit) ► Upgrade A
      │
      └── spel#226 ClockContext (proposed) ─────────► Upgrade B + community verify
```

---

## Open questions for LEZ / Logos Storage devs

1. **Stable token program ID** — What is the stable token program ID on testnet? (r4bbit)
2. **Account balance mutation** — Is direct mutation of a `u128` balance field in a keeper-owned account a valid substitute for a token program CPI?
3. **`spel#226` timeline** — When does `ClockContext` land? Can we contribute?
4. **Logos Storage API stability** — Are `exists()` and `downloadChunks(local=false)` stable for v1 integration?
5. **Logos Storage possession proofs** — Does Logos Storage have native proof-of-possession primitives? If so, `verify_holding` + `challenge_holding` could simplify or be replaced entirely in v2.
