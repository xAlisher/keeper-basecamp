# Keeper Protocol — Roadmap

---

## Where We Are

The protocol design is complete. The whitepaper is written. Senty's first security audit identified 9 findings (4 HIGH, 5 MEDIUM) — all 8 actionable ones have been fixed. The design is ready for a second audit pass and for feedback from LEZ and Logos Storage developers before implementation begins.

---

## Phase 0 — Design (complete)

| Deliverable | Status |
|-------------|--------|
| Protocol design doc (`keeper-protocol-lez.md`) | ✅ Done |
| Whitepaper (`keeper-whitepaper.md`) | ✅ Done |
| Market research + competitive positioning | ✅ Done |
| SPEL framework upstream research | ✅ Done |
| Upstream blocker identification + workarounds | ✅ Done |
| Senty security audit — round 1 (9 findings) | ✅ Fixed |
| Senty security audit — round 2 | ⏳ To run |
| LEZ / Logos Storage dev design review | ⏳ To request |

---

## Phase 1 — Unblock (before any code lands)

Two hard blockers must be resolved before the on-chain program can be compiled.

### Blocker A — Multi-seed PDA (`spel-framework/issues/1`)

The macro only supports single-seed PDAs today. Every keeper PDA except `pool` and `registry` requires array syntax (`pda = [literal("x"), account("y"), arg("z")]`) which is an open upstream issue.

**Action:** Comment on `spel-framework/issues/1` signalling demand. While waiting, all multi-seed PDAs are implemented via manual verification in handler bodies using `compute_pda()` (workaround documented in design doc).

**Unblocks:** All of Phase 2+.

### Blocker B — Stable token transfer (`ChainedCall` format)

The mechanism for transferring stable tokens between accounts is not documented. SPEL has no native transfer primitive; cross-program calls use `ChainedCall` from `nssa_core`.

**Action:** Request LEZ dev confirmation of: stable token program ID, `ChainedCall` construction format, and whether account balance mutation is permitted instead. While waiting, all stable flows use the internal ledger model (`claimable_balance` + `withdraw` instruction).

**Unblocks:** `register_preservation`, `claim_monthly_reward`, `fund_pool`, all bounty and challenge instructions.

---

## Phase 2 — Core implementation (workaround phase)

All instructions implemented using:
- Manual PDA verification via `compute_pda()` (Workaround A)
- Internal ledger model for stable transfers (Workaround B)

### Step 1 — Singletons (no blockers, start now)

| Instruction | Notes |
|-------------|-------|
| `initialize_program` | Single-literal PDAs only; sets all config |
| `fund_pool` | Single-literal pool PDA; internal ledger |

### Step 2 — Core preservation flow

| Instruction | Notes |
|-------------|-------|
| `create_user` | Manual PDA verify for stats |
| `register_preservation` | Manual PDA verify; internal ledger for fees |
| `verify_holding` | v1: preserver-signed only (H2 fix) |
| `take_monthly_snapshot` | Permissionless; must precede claim |
| `claim_monthly_reward` | Uses snapshot fields (M1 fix) |
| `withdraw` | Single ChainedCall seam (M4 fix) |
| `deregister_user` | Zeroes active_bytes at stats level |
| `deregister_item` | Returns deposit; ChallengePending guard (H3 fix) |

### Step 3 — Audit and bounty layer

| Instruction | Notes |
|-------------|-------|
| `challenge_holding` | Increments `open_challenge_count` (H3 fix) |
| `finalize_challenge` | Identity check on payout account (H1 fix) |
| `create_item_bounty` | Init bounty PDA |
| `fund_item_bounty` | Add stable to bounty |
| `claim_item_bounty` | Pro-rata by active bytes |

### Step 4 — Governance

| Instruction | Notes |
|-------------|-------|
| `open_dispute` | Formal dispute record |
| `vote_on_dispute` | Vote receipt PDA dedup (M3 fix) |
| `migrate_score` | Dual-signed; zeroes old active_bytes (H4 fix) |

---

## Phase 3 — Integration and testing

- Wire Keeper Basecamp client to on-chain program (replace off-chain log with on-chain calls)
- Self-verification cycle: client calls `verify_holding` every 7 days per item
- Integration with Logos Storage `downloadChunks(local=false)` for reachability check
- Headless test suite via `logoscore` against Logos testnet

---

## Phase 4 — Testnet deployment

Target: Logos testnet launch (2026).

- Deploy `initialize_program` on testnet; set conservative config values
- Seed pool via `fund_pool` for bootstrap period
- Open to early preservers; monitor `active_bytes`, reward pool, challenge activity
- Gather data on: registration rate, pool depletion speed, challenge frequency

---

## Upgrade A — Multi-seed PDA (when `spel-framework/issues/1` lands)

Replace all manual `compute_pda()` handler verification with macro-level `pda = [...]` constraints. The instruction signatures do not change — this is a pure implementation upgrade. The IDL will gain full seed metadata enabling auto-derivation in the C++ client.

---

## Upgrade B — Native stable transfer (when LEZ token API confirmed)

Replace the internal ledger model (`claimable_balance` + `withdraw`) with direct `ChainedCall` transfers in each instruction. The `withdraw` instruction is removed. `UserStats.claimable_balance` field is removed. All affected instruction signatures gain one `token_program` account parameter.

---

## v2 — Future work

| Feature | Trigger |
|---------|---------|
| **Community `verify_holding`** | When LEZ exposes runtime block height natively; removes trust-based block number parameter |
| **ZK proof of data possession** | Analogous to Filecoin PoDP (launched May 2025); replaces economic deterrent with cryptographic proof |
| **Dispute stake** | Voluntary stable collateral for dispute participants; loser forfeits portion to winner |
| **Registry pagination** | When `PreserverRegistry` approaches ~320k entries; split into `registry_0`, `registry_1`, … |
| **`update_config` instruction** | Governance-controlled updates to pool parameters without redeployment |
| **Per-item bounty snapshot** | Fix M2 (bounty race) — snapshot bounty balance per month so all claimants share fixed budget |

---

## Dependencies map

```
Design doc ──────────────────────────────────────────► Phase 2 implementation
      │
      ├── Senty round 2 ──────────────────────────────► clear to build
      │
      ├── LEZ dev review ─────────────────────────────► design locked
      │
      ├── spel-framework/issues/1 ────────────────────► Upgrade A
      │
      └── LEZ stable token API ───────────────────────► Upgrade B
```

---

## Open questions for LEZ / Logos Storage devs

1. **Multi-seed PDA timeline** — When is `pda = [...]` array syntax landing? Can we contribute the implementation?
2. **ChainedCall format** — What is the stable token program ID on testnet? How is a `ChainedCall` constructed for a transfer instruction?
3. **Account balance mutation** — Is direct mutation of a `u128` balance field in a keeper-owned account a valid substitute for a token program CPI?
4. **Block number** — Is there a roadmap for runtime-injected block height in handler parameters? This would unlock permissionless `verify_holding` and remove the trust assumption.
5. **Logos Storage API stability** — Are `exists()` and `downloadChunks(local=false)` stable for v1 integration?
