# Senty Review — Round 1

**Date:** 2026-06-01
**Reviewer:** Senty (via Codex plugin)
**Document reviewed:** `docs/keeper-protocol-lez.md`
**Verdict:** BLOCKED

---

## HIGH Findings (block implementation)

### H1 — `finalize_challenge` can pay the bounty to an arbitrary account

**Evidence:** The `challenger` parameter is `challenger: AccountWithMetadata` with no constraint tying it to `challenge.challenger` and no `#[account(signer)]`.
**Impact:** Any caller can finalize another user's successful challenge and redirect the bounty payout to an arbitrary account.
**Fix:** Constrain payout account to `ChallengeRecord.challenger`; reject mismatches.

---

### H2 — `verify_holding` accepts attacker-supplied block numbers

**Evidence:** `verify_holding` is permissionless ("Any node can submit this") and its ABI takes `block_number: u64 // submitted by Keeper client; trust-based`. That value sets `record.last_verified_block` and drives `Pending / Active / Delinquent` transitions.
**Impact:** An unauthorized verifier can manipulate holding status and all timing-dependent reward/challenge outcomes by submitting arbitrary block numbers.
**Fix:** Do not let untrusted callers supply block height for state transitions; wait for LEZ runtime block exposure or restrict the instruction to the preserver or an authorized verifier only.

---

### H3 — Challenge-pending deposit guard does not cover the full `ChallengeRecord` key space

**Evidence:** `ChallengeRecord` is keyed by `(challenger, preserver, ia_id)` — one PDA per challenger. The `deregister_item` guard checks only `if ChallengeRecord exists for (user, ia_id)` — a single key, not all challengers.
**Impact:** A preserver can escape with the holding deposit while a different challenger's unresolved challenge still exists.
**Fix:** Redesign to a single per-`(preserver, ia_id)` challenge state, or maintain an aggregate outstanding-challenge counter that `deregister_item` can verify atomically.

---

### H4 — `migrate_score` duplicates `active_bytes` without moving item records

**Evidence:** `migrate_score` sets `new_stats.active_bytes = old_stats.active_bytes` but does not zero `old_stats.active_bytes` and does not migrate any `PreservationRecord` PDAs (keyed by `[literal("pres"), account("preserver"), arg("ia_id")]`). `claim_monthly_reward` pays from `stats.active_bytes`.
**Impact:** The new account can claim rewards for storage it does not control; the old account may also remain claimable. Double-spending of the same bytes.
**Fix:** Zero `old_stats.active_bytes` unconditionally; block reward claims until `PreservationRecord` PDAs are explicitly reassigned, or forbid migrating `active_bytes` entirely and require preservers to re-verify under the new identity.

---

## MEDIUM Findings (block implementation)

### M1 — Monthly reward distribution is order-dependent

**Evidence:** `claim_monthly_reward` computes `pool_budget = pool.fee_balance` at call time, then decrements `pool.fee_balance -= payout`. Early claimants compute against a larger budget than later claimants in the same month.
**Fix:** Snapshot the month's distributable budget and denominator once; have all claimants use that snapshot.

---

### M2 — Item bounty is first-come-first-served (same race)

**Evidence:** `claim_item_bounty` uses live `bounty.balance` and decrements it per claim. Doc acknowledges: "Payout is therefore first-come-first-served within the month."
**Fix:** Snapshot the month's bounty balance and denominator at the start of claim month; track claimed amounts against that fixed budget.

---

### M3 — Dispute voting has no per-voter deduplication

**Evidence:** `vote_on_dispute` takes `dispute`, `voter`, `voter_stats` — no vote receipt PDA. Logic only says "Weight proportional to `first_preserved_count`."
**Impact:** The same voter can call repeatedly and accumulate unlimited voting weight on a dispute.
**Fix:** Add a per-`(voter, ia_id)` vote receipt PDA (`#[account(init)]`) and reject duplicate votes.

---

### M4 — `withdraw` instruction is referenced but not defined

**Evidence:** `UserStats.claimable_balance` carries comment "Cleared by `withdraw()`" and the workaround section says "`withdraw` is the only instruction that needs ChainedCall." No `withdraw` instruction is in the v1 instruction set.
**Impact:** Rewards accrue in `claimable_balance` with no defined exit path.
**Fix:** Define `withdraw(amount)` in v1 or remove the internal-ledger fallback from the design entirely.

---

### M5 — `deregister_item` signature is missing the `stats` account

**Evidence:** The logic comment says `stats.active_bytes -= record.total_bytes` with note "must decrement or pool total drifts." The instruction signature does not include a `stats` account parameter.
**Impact:** The documented state transition cannot be implemented; if omitted, reward accounting is broken.
**Fix:** Add `#[account(mut, pda = [literal("stats"), account("user")])] stats: AccountWithMetadata` to `deregister_item`.

---

## Next steps

All 4 HIGH and all 5 MEDIUM findings must be resolved before implementation. Apply fixes to `keeper-protocol-lez.md` and re-run Senty for Round 2.
