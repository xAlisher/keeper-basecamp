# Keeper Protocol — LEZ Program Design

A LEZ (Logos Execution Zone) program that rewards and verifies permanent preservation
of Internet Archive collections. Deployed on-chain; independent of Beacon, Cord, or any
Logos service for its core guarantees.

---

## Overview

```
User joins (free):
  → keeper_protocol::create_user()
      → UserStats PDA initialised, added to PreserverRegistry

User preserves IA item:
  → Keeper downloads files
  → Stash uploads to IPFS → collection CID
  → keeper_protocol::register_preservation(ia_id, cid, hashes, ...)
      → pays registration_fee stable → RewardPool.fee_balance
      → escrows holding_deposit_amount stable (returned on deregister_item; forfeited on failed challenge)
      → ItemRecord PDA (first-write atomic — first preserver wins)
      → PreservationRecord PDA (per-user per-item; status = Pending)
      → UserStats.keeper_score incremented (soulbound, leaderboard only)

Every 7 days per item:
  → keeper_protocol::verify_holding()
      → record.verification_status updated (Pending / Active / Delinquent)
      → UserStats.active_bytes and RewardPool.total_active_bytes maintained

End of each month:
  → keeper_protocol::claim_monthly_reward(month)
      → pool_budget     = RewardPool.fee_balance (all fees since last distribution)
      → reward_per_byte = pool_budget / RewardPool.total_active_bytes
      → user_payout     = min(user.active_bytes × reward_per_byte, pool_budget × max_user_share)
      → stable transferred from RewardPool to user
      → RewardClaim PDA created (claimed = true for this month)
      → RewardPool.fee_balance reduced by total distributed

At any time (institutions / foundation):
  → keeper_protocol::fund_pool(amount)
      → caller transfers `amount` stable directly to RewardPool.fee_balance
      → no keeper_score, no registration — purely a pool top-up
      → open to anyone: Logos Foundation, Internet Archive, donors, grants

At any time (sponsors / institutions):
  → keeper_protocol::create_item_bounty(ia_id)      ← first time only
  → keeper_protocol::fund_item_bounty(ia_id, amount)
      → stable added to SponsorBounty PDA for that specific item
  → preserver calls keeper_protocol::claim_item_bounty(ia_id, month)
      → pro-rata payout by record.total_bytes / item.total_active_bytes

At any time (any node — challenge-response audit):
  → keeper_protocol::challenge_holding(ia_id, preserver)
      → challenger asserts CID is unreachable; sets deadline
  → preserver must respond with verify_holding before deadline
  → keeper_protocol::finalize_challenge(ia_id, preserver)
      → if preserver responded: cleared; challenger pays anti-spam fee
      → if preserver silent: immediately Delinquent; challenger earns bounty
```

The on-chain `ItemRecord` is the source of truth — not Beacon, not Cord.

## Dual Token Model

| Layer | Token | Nature | Source | Use |
|-------|-------|--------|--------|-----|
| **Reputation** | `keeper_score` | Soulbound, non-transferable | Earned by preserving items | Leaderboard, dispute voting weight, determines monthly reward **share** |
| **Economic** | Stable (configurable at deploy) | Transferable, real value | Registration fees + institutional grants | Funds the reward pool; paid out monthly in the same stable |

**`keeper_score` is not a financial asset.** It has no price, no market, no transfer. It is a permanent on-chain record of preservation work — like a credit score. It cannot be bought.

**Stable is the only money in the system.** The reward pool is funded by registration fees paid on every `register_preservation` call. Preservers earn stable monthly, proportional to how much data they are actively holding. No entrance deposit required — joining is free.

The stable token address is a program constant set at deployment — USDC, a Logos native stable, or any compatible token.

> **Design choice — stable-only, no flywheel token:** DePIN protocols that reward in their own volatile token create boom/bust participation cycles tied to price, not storage quality. ² Keeper deliberately avoids this: rewarding preservers in the same stable they paid is redistribution with no speculation layer — preservation as a public good, not an investment vehicle. This is a stated design decision, not an omission.

> **v2 — Dispute stake:** A voluntary stable stake will be introduced for dispute participation. Preservers who want to challenge or defend canonical records will lock stable as collateral; the loser forfeits a portion to the winner. `keeper_score` is never at risk — only the voluntary dispute stake.

---

## Account Types

### `ItemRecord`
One per IA identifier. First preserver sets the canonical hashes.

```rust
#[account_type]
pub struct ItemRecord {
    pub ia_id:                    String,
    pub first_preserver:          AccountId,
    pub canonical_metadata_hash:  [u8; 32],  // sha256 of IA metadata JSON
    pub canonical_merkle_root:    [u8; 32],  // sha256 tree over sorted file CIDs
    pub canonical_collection_cid: [u8; 32],  // IPFS root CID from Stash
    pub preserver_count:          u32,       // confirmed matching preservations
    pub mismatch_count:           u32,       // diverging hash submissions
    pub status:                   ItemStatus,
    pub block:                    u64,
    pub file_count:               u32,
    pub total_bytes:              u64,
    pub total_active_bytes:       u64,  // sum of total_bytes across Active preservers of this item
                                        // maintained by verify_holding; used in per-item bounty distribution
}

#[account_type]
pub enum ItemStatus {
    Clean,      // single preserver, no disputes
    Confirmed,  // ≥2 preservers, all hashes match
    Suspicious, // hash mismatch detected
    Resolved,   // dispute closed by community vote
}
```

### `PreservationRecord`
One per `(preserver, ia_id)`. Records exactly what each user submitted.

```rust
#[account_type]
pub struct PreservationRecord {
    pub ia_id:             String,
    pub preserver:         AccountId,
    pub collection_cid:    [u8; 32],
    pub metadata_hash:     [u8; 32],
    pub merkle_root:       [u8; 32],
    pub matches_canonical:    bool,
    pub block:                u64,
    pub file_count:           u32,
    pub total_bytes:          u64,
    pub holding_deposit:      u128,  // stable escrowed at registration; returned by deregister_item
                                     // forfeited to pool if chronically Delinquent
    pub last_verified_block:  u64,
    pub verification_status:  VerificationStatus,
}

#[account_type]
pub enum VerificationStatus {
    Pending,    // min_hold_blocks not yet elapsed — no reward contribution
    Active,     // holding confirmed within pool.delinquency_threshold — contributes to active_bytes
    Delinquent, // gap exceeded — removed from active_bytes until re-verified
}
```

### `DisputeRecord`
Created explicitly by the challenger via `open_dispute` after an item is flagged Suspicious.

```rust
#[account_type]
pub struct DisputeRecord {
    pub ia_id:                String,
    pub canonical_preserver:  AccountId,
    pub canonical_hash:       [u8; 32],
    pub challenger_preserver: AccountId,
    pub challenger_hash:      [u8; 32],
    pub opened_at_block:      u64,
    pub votes_canonical:      u32,
    pub votes_challenger:     u32,
    pub status:               DisputeStatus,
}

#[account_type]
pub enum DisputeStatus {
    Open,
    CanonicalWins,
    ChallengerWins,
}
```

### `UserStats`
One per user. Drives both leaderboards.

```rust
#[account_type]
pub struct UserStats {
    pub first_preserved_count: u64,    // leaderboard 1: pioneer board
    pub total_bytes_preserved: u64,    // leaderboard 2: archive board
    pub confirmed_count:       u64,    // confirmed other users' items
    pub mismatch_count:        u32,    // reputation penalty source
    pub keeper_score:          u128,   // soulbound — accumulates forever, leaderboard only
    pub active_bytes:          u64,    // bytes currently held Active — drives reward share
    pub last_block:            u64,
    pub is_deregistered:       bool,   // set by deregister_user; leaderboard enumeration skips this flag
}
```

### `RewardPool`
Singleton. Accumulates registration fees and tracks the network-wide active bytes
counter. Updated by `register_preservation` (fees in) and `verify_holding` (bytes counter).

```rust
#[account_type]
pub struct RewardPool {
    pub fee_balance:            u128,   // stable accumulated from registration fees + institutional grants
    pub total_active_bytes:     u128,   // sum of active_bytes across all preservers
    pub registration_fee:       u128,   // stable charged per register_preservation call (into fee_balance)
    pub holding_deposit_amount: u128,   // stable escrowed per item (returned on deregister, slashed on chronic delinquency)
    pub max_user_share_bp:           u32,    // basis points cap per user, e.g. 2000 = 20%
    pub min_hold_blocks:             u64,    // blocks an item must be held before Active
    pub challenge_bounty_bp:         u32,    // % of holding_deposit paid to successful challenger
    pub challenge_response_window:   u64,    // blocks preserver has to respond to a challenge
    pub challenge_spam_fee:          u128,   // stable challenger must lock (lost if wrong)
    pub delinquency_threshold:       u64,    // blocks since last_verified_block before status → Delinquent
    pub last_month:                  u32,    // last YYYYMM distributed
}
```

### `RewardClaim`
One per `(user, month)`. Tracks whether a user has claimed their monthly stable reward.
`month` is a `u32` in `YYYYMM` format (e.g. `202607`).

```rust
#[account_type]
pub struct RewardClaim {
    pub user:                AccountId,
    pub month:               u32,
    pub active_bytes_snapshot: u64,   // user.active_bytes at time of claim
    pub amount:              u128,    // stable transferred this claim
    pub claimed:             bool,
}
```

### `PreserverRegistry`
Singleton. Holds the ordered list of all preserver `AccountId`s.
`create_user` appends to it; enables leaderboard enumeration without an off-chain indexer.

```rust
#[account_type]
pub struct PreserverRegistry {
    pub preservers: Vec<AccountId>,
}
```

### `ChallengeRecord`
One per `(challenger, preserver, ia_id)`. Created by `challenge_holding`; resolved by
`finalize_challenge` after the deadline. Provides economic incentive for third-party
verification — challenger earns a bounty if the preserver cannot prove holding. ³

```rust
#[account_type]
pub struct ChallengeRecord {
    pub ia_id:             String,
    pub preserver:         AccountId,
    pub challenger:        AccountId,
    pub challenge_block:   u64,
    pub deadline_block:    u64,    // challenge_block + pool.challenge_response_window
    pub resolved:          bool,
    pub preserver_cleared: bool,   // true = preserver responded; false = delinquent
}
```

### `SponsorBounty`
One per `ia_id`. Any party can deposit stable to sponsor preservation of a specific item.
Preservers of that item claim a pro-rata share of the bounty on top of the normal monthly
pool reward, funded by `fund_item_bounty`. ⁴

```rust
#[account_type]
pub struct SponsorBounty {
    pub ia_id:   String,
    pub balance: u128,   // stable contributed by sponsors for this specific item
    pub block:   u64,    // block of last sponsorship call
}
```

---

## PDA Layout

| Account | Seeds | One per |
|---------|-------|---------|
| `item::{ia_id}` | `[literal("item"), arg("ia_id")]` | IA identifier |
| `pres::{preserver}::{ia_id}` | `[literal("pres"), account("preserver"), arg("ia_id")]` | (user, item) pair |
| `dispute::{ia_id}` | `[literal("dispute"), arg("ia_id")]` | disputed item |
| `stats::{user}` | `[literal("stats"), account("user")]` | user |
| `claim::{user}::{month}` | `[literal("claim"), account("user"), arg("month")]` | (user, month) pair |
| `bounty::{ia_id}` | `[literal("bounty"), arg("ia_id")]` | sponsored item |
| `bclaim::{user}::{ia_id}::{month}` | `[literal("bclaim"), account("user"), arg("ia_id"), arg("month")]` | (user, item, month) bounty claim |
| `challenge::{challenger}::{preserver}::{ia_id}` | `[literal("challenge"), account("challenger"), account("preserver"), arg("ia_id")]` | (challenger, preserver, item) |
| `pool` | `[literal("pool")]` | singleton |
| `registry` | `[literal("registry")]` | singleton |

> **Upstream dependency — multi-seed PDA:** The `pda = [...]` array syntax and `arg("name")`
> seed type are documented in `spel-framework/issues/1` as a **proposed feature, not yet
> implemented**. The macro currently supports only single-seed PDAs: `pda = literal("x")` or
> `pda = account("name")`. Every keeper PDA that uses multi-part seeds (all except `pool` and
> `registry`) requires this upstream feature to land before the program can be compiled.
> Multi-seed derivation will combine seeds via `sha256(s1 || s2 || ... || sN)` into one
> 32-byte `PdaSeed` — this matches `compute_pda()` in `spel-framework-core/src/pda.rs`.

---

## Token Transfer Convention

> **Open question for LEZ devs — mechanism confirmed absent from framework; API call TBD.**

All instructions that move stable tokens use the notation:
```
transfer(from_account, to_account, amount)
```

**Source research finding:** The SPEL framework has **no native token transfer primitive**.
The fixture program's `transfer` instruction in `tests/e2e/fixture_program/src/lib.rs` is a
pure account data mutation — it declares `from` and `to` as `#[account(mut)]` and writes
balance fields directly. `SpelOutput` exposes `chained_calls: Vec<ChainedCall>` (from
`nssa_core::program::ChainedCall`) as the cross-program invocation mechanism. This is
likely how a real stable token contract would be called.

The exact call pattern depends on what the LEZ stable token program exposes, but the
structure will be one of:

| Mechanism | What it means in practice |
|-----------|--------------------------|
| **`ChainedCall` to token program** (most likely) | `SpelOutput::execute(accounts, vec![ChainedCall::new(token_program_id, transfer_ix_data)])` — requires `token_program` as an extra `#[account]` |
| **Account balance mutation** | Direct write to a `balance: u128` field on the pool/user accounts — only if the runtime enforces conservation; matches the fixture program pattern |
| **Native primitive** | `SpelTransfer::stable(from, to, amount)` or equivalent — not seen in source; probably does not exist |

**Until confirmed with LEZ devs, the doc uses `transfer(from, to, amount)` as a placeholder.**
Instructions affected: `register_preservation`, `claim_monthly_reward`, `fund_pool`,
`challenge_holding`, `finalize_challenge`, `fund_item_bounty`, `claim_item_bounty`,
`deregister_item`.

If the mechanism requires a `token_program` or similar extra account, every affected
instruction signature gains one more `#[account]` parameter. This is a mechanical
change — it does not affect any logic.

---

## Instructions

### `initialize_program`

One-time deploy instruction. Creates the `RewardPool` singleton and the empty
`PreserverRegistry`. Must be called once by the deployer before any other instruction.
All config values can be updated later by the authority via a separate `update_config`
instruction (v2); for v1 the values are set once at deploy time.

```rust
#[instruction]
pub fn initialize_program(
    #[account(init, pda = [literal("pool")])]
    pool: AccountWithMetadata,

    #[account(init, pda = [literal("registry")])]
    registry: AccountWithMetadata,

    #[account(signer)]
    authority: AccountWithMetadata,

    registration_fee:          u128,   // stable per register_preservation call
    holding_deposit_amount:    u128,   // stable escrowed per item
    max_user_share_bp:         u32,    // e.g. 2000 = 20% cap per user per month
    min_hold_blocks:           u64,    // e.g. ~30 days of blocks before Pending → Active
    delinquency_threshold:     u64,    // e.g. ~30 days of blocks; gap before Active → Delinquent
    challenge_bounty_bp:       u32,    // e.g. 5000 = 50% of holding_deposit to challenger
    challenge_response_window: u64,    // e.g. ~3 days of blocks for preserver to respond
    challenge_spam_fee:        u128,   // stable challenger locks; lost if wrong
) -> SpelResult
// Logic:
//   pool.fee_balance            = 0
//   pool.total_active_bytes     = 0
//   pool.registration_fee       = registration_fee
//   pool.holding_deposit_amount = holding_deposit_amount
//   pool.max_user_share_bp      = max_user_share_bp
//   pool.min_hold_blocks        = min_hold_blocks
//   pool.delinquency_threshold  = delinquency_threshold
//   pool.challenge_bounty_bp    = challenge_bounty_bp
//   pool.challenge_response_window = challenge_response_window
//   pool.challenge_spam_fee     = challenge_spam_fee
//   pool.last_month             = 0
//   registry.preservers         = []
```

---

### `create_user`

Entry point for new preservers. Joining is free — no deposit required.
`#[account(init)]` on stats rejects a duplicate call — safe to call again and ignore
`AccountAlreadyInitialized`.

```rust
#[instruction]
pub fn create_user(
    #[account(init, pda = [literal("stats"), account("user")])]
    stats: AccountWithMetadata,

    #[account(mut, pda = [literal("registry")])]
    registry: AccountWithMetadata,

    #[account(signer)]
    user: AccountWithMetadata,
) -> SpelResult
// Logic: registry.preservers.push(user.account_id)
```

---

### `register_preservation`

Core instruction. Handles both first and subsequent preservers.
Requires `create_user` to have been called first for this preserver.

```rust
#[instruction]
pub fn register_preservation(
    #[account(mut, pda = [literal("item"), arg("ia_id")])]
    item: AccountWithMetadata,

    // init prevents the same user registering the same item twice
    #[account(init, pda = [literal("pres"), account("preserver"), arg("ia_id")])]
    preservation: AccountWithMetadata,

    #[account(signer)]
    preserver: AccountWithMetadata,

    // mut — account created separately via create_user
    #[account(mut, pda = [literal("stats"), account("preserver")])]
    stats: AccountWithMetadata,

    // fee payment: registration_fee stable transferred from preserver to pool
    #[account(mut, pda = [literal("pool")])]
    pool: AccountWithMetadata,

    ia_id:          String,
    collection_cid: [u8; 32],
    metadata_hash:  [u8; 32],
    merkle_root:    [u8; 32],
    file_count:     u32,
    total_bytes:    u64,
    block_number:   u64,        // submitted by Keeper client; trust-based until LEZ exposes it natively
) -> SpelResult
```

**Logic:**

```
transfer pool.registration_fee stable from preserver to pool.fee_balance
transfer pool.holding_deposit_amount stable from preserver to pool (escrowed — not fee_balance)
preservation.holding_deposit = pool.holding_deposit_amount

if item does not exist yet (first_preserver == default):
    → set canonical hashes
    → item.block = block_number
    → stats.first_preserved_count += 1
    → stats.total_bytes_preserved += total_bytes
    → stats.keeper_score += compute_score(file_count, total_bytes)   // soulbound
    → item.status = Clean

else:
    compare (metadata_hash, merkle_root) to canonical

    if both match:
        → stats.confirmed_count += 1
        → stats.total_bytes_preserved += total_bytes
        → stats.keeper_score += compute_score(file_count, total_bytes) * 20 / 100
        → item.preserver_count += 1
        → item.status = Confirmed (if ≥2 matching)

    else:
        → stats.mismatch_count += 1
        → item.mismatch_count += 1
        → item.status = Suspicious
        → no score awarded
        → (caller must follow with open_dispute for a formal DisputeRecord)
```

---

### `open_dispute`

Any preserver who already has a `PreservationRecord` for the item can formally open a dispute.

```rust
#[instruction]
pub fn open_dispute(
    #[account(mut, pda = [literal("item"), arg("ia_id")])]
    item: AccountWithMetadata,

    #[account(init, pda = [literal("dispute"), arg("ia_id")])]
    dispute: AccountWithMetadata,

    #[account(signer)]
    challenger: AccountWithMetadata,

    // must have already preserved this item
    #[account(pda = [literal("pres"), account("challenger"), arg("ia_id")])]
    challenger_record: AccountWithMetadata,

    ia_id:        String,
    block_number: u64,   // submitted by Keeper client
) -> SpelResult
```

---

### `vote_on_dispute`

Community members who have preserved items can vote. Weight proportional to
`first_preserved_count` — more preservation history = more voting power.

```rust
#[instruction]
pub fn vote_on_dispute(
    #[account(mut, pda = [literal("dispute"), arg("ia_id")])]
    dispute: AccountWithMetadata,

    #[account(signer)]
    voter: AccountWithMetadata,

    #[account(pda = [literal("stats"), account("voter")])]
    voter_stats: AccountWithMetadata,

    ia_id:          String,
    vote_canonical: bool,   // true = canonical correct, false = challenger correct
) -> SpelResult
```

---

### `claim_monthly_reward`

Called once per month per user. Transfers stable from `RewardPool` proportional to
the user's `active_bytes` share of the network total. `#[account(init)]` on the claim
PDA prevents double claiming the same month.

```rust
#[instruction]
pub fn claim_monthly_reward(
    #[account(init, pda = [literal("claim"), account("user"), arg("month")])]
    claim: AccountWithMetadata,

    #[account(signer)]
    user: AccountWithMetadata,

    #[account(pda = [literal("stats"), account("user")])]
    stats: AccountWithMetadata,

    #[account(mut, pda = [literal("pool")])]
    pool: AccountWithMetadata,

    month: u32,   // YYYYMM — e.g. 202607
) -> SpelResult
// Logic:
//   if pool.total_active_bytes == 0 → return Err(NoActiveStorage)
//   pool_budget     = pool.fee_balance
//   reward_per_byte = pool_budget / pool.total_active_bytes
//   raw_reward      = stats.active_bytes as u128 × reward_per_byte
//   cap             = pool_budget × pool.max_user_share_bp / 10_000
//   payout          = min(raw_reward, cap)
//   transfer payout stable from pool to user
//   pool.fee_balance                -= payout
//   claim.active_bytes_snapshot      = stats.active_bytes
//   claim.amount                     = payout
//   claim.claimed                    = true
```

Unclaimed months are not carried forward — undistributed fees remain in `pool.fee_balance`
and increase next month's budget. The pool never distributes more than its current balance.

---

### `challenge_holding`

Any node can challenge a preserver's claimed holding. The challenger asserts (off-chain
verified) that the preserver's CID is unreachable on the Logos Storage network.
The preserver then has `pool.challenge_response_window` blocks to submit a successful
`verify_holding` — proving the CID is reachable — or be immediately set Delinquent
and lose their holding deposit. Challengers who are wrong lose their anti-spam fee
to the pool. ³

```rust
#[instruction]
pub fn challenge_holding(
    #[account(init, pda = [literal("challenge"), account("challenger"), account("preserver"), arg("ia_id")])]
    challenge: AccountWithMetadata,

    #[account(signer)]
    challenger: AccountWithMetadata,

    // the node whose holding is being challenged
    preserver: AccountWithMetadata,

    #[account(pda = [literal("pres"), account("preserver"), arg("ia_id")])]
    record: AccountWithMetadata,   // must exist — can only challenge registered items

    #[account(mut, pda = [literal("pool")])]
    pool: AccountWithMetadata,

    ia_id:        String,
    block_number: u64,
) -> SpelResult
// Logic:
//   transfer pool.challenge_spam_fee stable from challenger to pool (anti-spam; lost if challenger is wrong)
//   challenge.ia_id             = ia_id
//   challenge.preserver         = preserver.account_id
//   challenge.challenger        = challenger.account_id
//   challenge.challenge_block   = block_number
//   challenge.deadline_block    = block_number + pool.challenge_response_window
//   challenge.resolved          = false
//   challenge.preserver_cleared = false
```

---

### `finalize_challenge`

Called by anyone after `challenge.deadline_block` has passed. Resolves the challenge
in favour of whichever party won: if the preserver successfully called `verify_holding`
after the challenge was opened, they are cleared; otherwise they are set Delinquent
and their holding deposit is forfeited to the challenger as bounty.

```rust
#[instruction]
pub fn finalize_challenge(
    #[account(mut, pda = [literal("challenge"), account("challenger"), account("preserver"), arg("ia_id")])]
    challenge: AccountWithMetadata,

    #[account(mut, pda = [literal("pres"), account("preserver"), arg("ia_id")])]
    record: AccountWithMetadata,

    #[account(mut, pda = [literal("stats"), account("preserver")])]
    preserver_stats: AccountWithMetadata,

    #[account(mut, pda = [literal("pool")])]
    pool: AccountWithMetadata,

    // challenger account — bounty is paid here if preserver failed
    challenger: AccountWithMetadata,

    preserver:    AccountWithMetadata,
    ia_id:        String,
    block_number: u64,
) -> SpelResult
// Logic:
//   if challenge.resolved → return Err(AlreadyResolved)
//   if block_number < challenge.deadline_block → return Err(DeadlineNotReached)
//
//   if record.last_verified_block > challenge.challenge_block:
//     // preserver responded after challenge — cleared
//     challenge.preserver_cleared = true
//     // challenger's spam fee already in pool — no refund (cost of being wrong)
//
//   else:
//     // preserver did not respond — delinquent
//     record.verification_status = Delinquent
//     preserver_stats.active_bytes     -= record.total_bytes  (if was Active)
//     pool.total_active_bytes          -= record.total_bytes  (if was Active)
//     bounty = record.holding_deposit × pool.challenge_bounty_bp / 10_000
//     transfer bounty stable from pool to challenger
//     record.holding_deposit           -= bounty
//     challenge.preserver_cleared = false
//
//   challenge.resolved = true
```

---

### `fund_pool`

Open institutional entry point. Any party — Logos Foundation, Internet Archive, grant
programs, or individual donors — can top up the reward pool directly without joining as
a preserver. The caller receives no `keeper_score` and no `PreservationRecord`; this is
a pure economic contribution.

```rust
#[instruction]
pub fn fund_pool(
    #[account(mut, pda = [literal("pool")])]
    pool: AccountWithMetadata,

    #[account(signer)]
    funder: AccountWithMetadata,

    amount: u128,   // stable to transfer into pool.fee_balance
) -> SpelResult
// Logic:
//   transfer amount stable from funder to pool
//   pool.fee_balance += amount
```

This instruction has no access control — any account can call it. The pool grows,
`reward_per_byte` rises, and all active preservers benefit automatically from the
next claim cycle.

---

### `create_item_bounty`

Creates the `SponsorBounty` PDA for a specific IA item. Must be called once before
`fund_item_bounty`. Analogous to how `create_user` precedes `register_preservation`. ⁴

```rust
#[instruction]
pub fn create_item_bounty(
    #[account(init, pda = [literal("bounty"), arg("ia_id")])]
    bounty: AccountWithMetadata,

    #[account(signer)]
    sponsor: AccountWithMetadata,

    ia_id:        String,
    block_number: u64,
) -> SpelResult
// Logic: bounty.ia_id = ia_id; bounty.balance = 0; bounty.block = block_number
```

---

### `fund_item_bounty`

Demand-side entry point. Any party — Internet Archive, a library, a private collector —
can deposit stable to specifically incentivise preservation of one IA item. The bounty
is paid out pro-rata to active preservers of that item via `claim_item_bounty`, on top
of their normal monthly pool reward. This creates a two-sided marketplace: preservers
self-select supply; sponsors express demand economically. ⁴

```rust
#[instruction]
pub fn fund_item_bounty(
    #[account(mut, pda = [literal("bounty"), arg("ia_id")])]
    bounty: AccountWithMetadata,

    #[account(signer)]
    sponsor: AccountWithMetadata,

    ia_id:        String,
    amount:       u128,   // stable to add to this item's bounty balance
    block_number: u64,
) -> SpelResult
// Logic:
//   transfer amount stable from sponsor to bounty account
//   bounty.balance += amount
//   bounty.block    = block_number
```

---

### `claim_item_bounty`

Called once per month per user per sponsored item. Distributes the item's bounty
pro-rata by each preserver's contribution to that item's active storage. ⁴

```rust
#[instruction]
pub fn claim_item_bounty(
    // init prevents double-claiming the same item's bounty for the same month
    #[account(init, pda = [literal("bclaim"), account("user"), arg("ia_id"), arg("month")])]
    bounty_claim: AccountWithMetadata,

    #[account(mut, pda = [literal("bounty"), arg("ia_id")])]
    bounty: AccountWithMetadata,

    #[account(signer)]
    user: AccountWithMetadata,

    // user's preservation record for this item — must be Active
    #[account(pda = [literal("pres"), account("user"), arg("ia_id")])]
    record: AccountWithMetadata,

    // item — provides total_active_bytes denominator
    #[account(pda = [literal("item"), arg("ia_id")])]
    item: AccountWithMetadata,

    ia_id: String,
    month: u32,    // YYYYMM
) -> SpelResult
// Logic:
//   if record.verification_status != Active → return Err(NotActive)
//   if bounty.balance == 0 → return Err(NoBalance)
//   if item.total_active_bytes == 0 → return Err(NoActiveStorage)
//   payout = bounty.balance × record.total_bytes / item.total_active_bytes
//   transfer payout stable from bounty to user
//   bounty.balance              -= payout
//   bounty_claim.user            = user.account_id
//   bounty_claim.ia_id           = ia_id
//   bounty_claim.month           = month
//   bounty_claim.amount          = payout
```

If no active preservers hold the item, the bounty accumulates indefinitely — a growing
incentive signal that attracts new preservers to that specific collection.

---

### `deregister_user`

Allows a preserver to exit cleanly. Removes them from active reward calculations,
returns all outstanding holding deposits, and marks their stats as deregistered so
the leaderboard can filter them out. `keeper_score` and preservation history remain
on-chain permanently — only the economic participation ends. ⁵

```rust
#[instruction]
pub fn deregister_user(
    #[account(mut, pda = [literal("stats"), account("user")])]
    stats: AccountWithMetadata,

    #[account(mut, pda = [literal("pool")])]
    pool: AccountWithMetadata,

    #[account(signer)]
    user: AccountWithMetadata,
) -> SpelResult
// Logic:
//   pool.total_active_bytes -= stats.active_bytes
//   stats.active_bytes       = 0
//   stats.is_deregistered    = true   // leaderboard enumeration skips this flag
//   return all outstanding holding_deposit sums from all user's PreservationRecords
//   (holding_deposit returns handled per-item — caller issues one deregister_item per record)
```

> Note: `deregister_user` does not iterate all `PreservationRecord` PDAs (SPEL does not
> support iteration). Each item must be deregistered separately via `deregister_item`
> (passes a single `PreservationRecord`), which returns that item's `holding_deposit`
> and zeroes it. `deregister_user` zeroes `active_bytes` in bulk at the stats level.

---

### `deregister_item`

Returns the holding deposit for one registered item and marks it inactive.
Called once per item when a preserver exits. Calling `deregister_user` first is
recommended to immediately zero `active_bytes` at the stats level; `deregister_item`
then handles the per-item deposit refunds one at a time.

```rust
#[instruction]
pub fn deregister_item(
    #[account(mut, pda = [literal("pres"), account("user"), arg("ia_id")])]
    record: AccountWithMetadata,

    #[account(mut, pda = [literal("item"), arg("ia_id")])]
    item: AccountWithMetadata,

    #[account(mut, pda = [literal("pool")])]
    pool: AccountWithMetadata,

    #[account(signer)]
    user: AccountWithMetadata,

    ia_id: String,
) -> SpelResult
// Logic:
//   if record.verification_status == Active:
//     item.total_active_bytes -= record.total_bytes   // update per-item counter
//   deposit = record.holding_deposit
//   transfer deposit stable from pool to user
//   record.holding_deposit      = 0
//   record.verification_status  = Delinquent          // item no longer maintained
```

---

### `migrate_score`

Transfers `keeper_score` and preservation stats to a new account (key rotation or
wallet recovery). Both the old and new accounts must sign — the old to authorise the
migration, the new to accept. The new user must call `create_user` first to initialise
`new_stats` before calling `migrate_score`. ⁶

```rust
#[instruction]
pub fn migrate_score(
    #[account(mut, pda = [literal("stats"), account("old_user")])]
    old_stats: AccountWithMetadata,

    // new_user must have called create_user first — new_stats must already exist
    #[account(mut, pda = [literal("stats"), account("new_user")])]
    new_stats: AccountWithMetadata,

    #[account(mut, pda = [literal("registry")])]
    registry: AccountWithMetadata,

    #[account(signer)]
    old_user: AccountWithMetadata,

    #[account(signer)]
    new_user: AccountWithMetadata,
) -> SpelResult
// Logic:
//   new_stats.keeper_score          = old_stats.keeper_score
//   new_stats.first_preserved_count = old_stats.first_preserved_count
//   new_stats.total_bytes_preserved = old_stats.total_bytes_preserved
//   new_stats.confirmed_count       = old_stats.confirmed_count
//   new_stats.mismatch_count        = old_stats.mismatch_count
//   new_stats.active_bytes          = old_stats.active_bytes
//   old_stats.keeper_score          = 0         // old identity retired
//   old_stats.is_deregistered       = true
//   registry.preservers.push(new_user.account_id)
//   // old_user remains in registry for history; is_deregistered filters it from active leaderboard
```

---

## Keeper Score Formula

`keeper_score` is the soulbound leaderboard metric. It accumulates with every preservation,
never decreases, and never transfers. It drives leaderboard ranking and dispute voting
weight. **It does not directly determine economic reward** — that is driven by `active_bytes`.

Square-root scaling on the size component prevents any single large preserver from
dominating the leaderboard:

```rust
fn compute_score(file_count: u32, total_bytes: u64) -> u128 {
    let base:        u128 = 100;
    let mb                = (total_bytes as u128) / 1_000_000;
    let size_score:  u128 = isqrt(mb) * 100;          // sqrt — diminishing returns for large sets
    let file_bonus:  u128 = file_count as u128 * 5;   // 5 points per file
    base + size_score + file_bonus
}
```

**Sqrt effect — leaderboard fairness:**

| Data | Linear score | Sqrt score | Ratio vs 10 MB |
|------|-------------|------------|----------------|
| 10 MB | 100 | 316 | 1× |
| 1 GB | 10,000 | 3,162 | 10× |
| 1 TB | 10,000,000 | 100,000 | 316× |

A 1 TB preserver scores 316× a 10 MB preserver — not 100,000×. Small preservers remain competitive.

**Score by scenario:**

| Scenario | Score awarded |
|----------|--------------|
| First preserver | Full score |
| Confirmed — hashes match | 20% of full score |
| CID diverges only | 10% — logged, not penalized |
| Merkle diverges | 0 — flagged suspicious |
| Metadata diverges | 0 — dispute opened |
| Dispute resolved in your favor | Social signal only (v1) |

## Monthly Economic Reward

Monthly stable payout is based on **active storage right now**, not cumulative score.
This means reward tracks ongoing storage cost and stops immediately when holding stops.

### Reward formula

```
pool_budget     = RewardPool.fee_balance   ← all registration fees since last distribution
reward_per_byte = pool_budget / RewardPool.total_active_bytes   ← auto-balancing
raw_reward      = user.active_bytes × reward_per_byte
user_payout     = min(raw_reward, pool_budget × max_user_share_bp / 10_000)
```

`reward_per_byte` is not set manually — it floats with the market. If total preserved
data doubles, reward per byte halves. If few people preserve, rate rises — incentivising
others to fill the gap. The pool self-balances.

**Pool sustainability:** two inflow sources:
- **Registration fees** — every `register_preservation` call transfers `registration_fee`
  stable to the pool. More preservation activity = larger pool.
- **Institutional grants** — `fund_pool` accepts any stable transfer from any party.
  Logos Foundation, Internet Archive, or grant programs can top up the pool directly
  at any time. No membership required — just a signed transaction.
- **Item bounties** — `fund_item_bounty` deposits accumulate in per-item `SponsorBounty`
  PDAs and are distributed as an additional top-up to preservers of that item.

**Cold start / bootstrap strategy:** ¹ Most DePIN protocols that failed died here.
Before enough registration fees accumulate, `reward_per_byte ≈ 0` — no preserver
joins for zero reward, killing the fee flywheel. Mitigation:

| Phase | Mechanism |
|-------|-----------|
| Genesis | Logos Foundation calls `fund_pool` with a committed minimum seed (recommend ≥ 6 months of estimated rewards at target participation) |
| Month 1–6 | Internet Archive (or Logos) calls `fund_pool` monthly on a public schedule — predictable reward for early preservers |
| Month 7+ | Registration fees self-sustain if ≥ N active preservers; foundation role shrinks to emergency top-up |

The foundation commitment should be documented off-chain (grant agreement, public pledge)
before the protocol launches — `fund_pool` is the on-chain execution of that commitment.

### Balancing properties

| Risk | Mechanism |
|------|-----------|
| Whale dumps TBs and drains pool | 30-day `min_hold_blocks` before item enters `active_bytes`; sqrt score on leaderboard |
| User stops holding and still earns | `active_bytes` drops to 0 when Delinquent → payout = 0 |
| One user takes all rewards | Hard cap `max_user_share_bp` (e.g. 20%) |
| Pool runs dry | Fees accumulate continuously; undistributed balance rolls to next month |
| Reward irrelevant to storage cost | `reward_per_byte` floats with supply — market equilibrium |

### Example — $10,000 stable in fee pool this month, 20% cap

Pool has accumulated $10,000 in registration fees. Monthly budget = $10,000.

| User | Active bytes | Share | Raw reward | After cap |
|------|-------------|-------|-----------|-----------|
| alice | 500 GB | 50% | $5,000 | $2,000 ← capped at 20% |
| bob | 300 GB | 30% | $3,000 | $3,000 |
| carol | 200 GB | 20% | $2,000 | $2,000 |

Alice's excess ($3,000) stays in `fee_balance`, accrues to next month's budget.

### Minimum hold period

A new item contributes to `active_bytes` only after `min_hold_blocks` have elapsed
since registration. The `verification_status` starts as `Pending`, transitions to
`Active` on the first `verify_holding` call after the minimum period.

This closes the dump-and-drain attack: upload massive data → claim reward → delete.
With a 30-day minimum hold, data must be verifiably stored for a full month before
it contributes to the reward calculation.

---

## Content Verification

### Commitment hashes (stored at registration)

Three hashes stored in every `ItemRecord`. Each commits to a different layer.

#### `collection_cid` — Logos Storage root CID

Returned by Stash after uploading all files. Content-addressed: fetching this CID from
any node and checking reachability proves the data exists on the network. No trust required.

#### `metadata_hash` — `sha256(IA metadata JSON)`

Raw JSON response from `https://archive.org/metadata/{ia_id}`. Commits to the file list,
IA-provided checksums (sha1/md5/crc32 per file), and item title at time of preservation.
Two honest preservers fetching the same item should produce the same hash.

#### `file_merkle_root` — `sha256(sorted file CIDs)`

`sha256` over concatenated file CIDs sorted by filename. Commits to exactly which files
were uploaded and their storage addresses. Verifiable even if archive.org is unavailable.

### Computing the hashes in Keeper (C++)

```cpp
// 1. collection_cid — returned by Stash after upload
QByteArray collectionCidBytes = QByteArray::fromHex(item.collectionCid.toUtf8());

// 2. metadata_hash — file saved during fetchMetadata
QFile mf(QDir::tempPath() + "/keeper-" + identifier + "-metadata.json");
QByteArray metaBytes;
if (mf.open(QIODevice::ReadOnly)) metaBytes = mf.readAll();
QByteArray metadataHash = QCryptographicHash::hash(metaBytes, QCryptographicHash::Sha256);

// 3. file_merkle_root — sha256 over sorted file CIDs
QList<QByteArray> cidBytes;
for (const auto& f : item.files)
    if (!f.cid.isEmpty())
        cidBytes.append(QByteArray::fromHex(f.cid.toUtf8()));
std::sort(cidBytes.begin(), cidBytes.end());
QByteArray merkleInput;
for (const auto& c : cidBytes) merkleInput.append(c);
QByteArray fileMerkleRoot = QCryptographicHash::hash(merkleInput, QCryptographicHash::Sha256);
```

### Reading item state (account inspection)

LEZ instructions are state-transition functions — they cannot return query results.
To read an `ItemRecord`, inspect the account directly:

```
# CLI
spel inspect <item-pda> --type ItemRecord

# Generated FFI client (from spel-client-gen --target logos-module)
keeper_protocol_fetch_item_record(ia_id, callback)
```

This replaces a `verify_preservation` instruction — submitting a no-op transaction
to read state is unnecessary and wasteful.

### Content integrity check (anyone can run)

```
1. spel inspect <item-pda> --type ItemRecord
   → { canonical_metadata_hash, canonical_merkle_root, canonical_collection_cid, status }

2. Fetch from Logos Storage network (downloadChunks with local=false)
   → CID is reachable  ✓  (content-addressed — no trust needed)

3. curl archive.org/metadata/{ia_id} | sha256sum
   → compare to canonical_metadata_hash  ✓ or ✗

4. collect file CIDs from retrieved content, sort by filename
   → sha256(sorted CIDs concatenated) == canonical_merkle_root  ✓ or ✗

5. count files, sum sizes
   → match file_count and total_bytes  ✓ or ✗
```

---

## Storage Holding Verification

The Logos Storage node used by Stash exposes native primitives for proving a node
actively holds a CID. These are already present in `storage_module_api.h` (confirmed
via `StorageModule` typed SDK at `vpavlin/logos-storage-module` `v0.3.2`).

### Storage node SDK capabilities

| SDK call | What it does |
|----------|-------------|
| `manifests()` | Returns all manifest CIDs stored locally — full inventory of what this node holds |
| `exists(cid)` | Returns bool: does this node hold this specific CID locally right now |
| `downloadChunks(cid, local=true, chunkSize)` | Local-only retrieval — fails if not stored locally |
| `downloadChunks(cid, local=false, chunkSize)` | Network retrieval — proves CID is reachable from any peer |

At the network level, Logos Storage automatically announces manifest CIDs on its Discv5
DHT. Any peer can discover what a node holds without asking it directly.

### `verify_holding` instruction (LEZ)

Any node can submit this — both self-verification and community verification use the
same instruction. The `preserver` account identifies whose `PreservationRecord` to
update; the `verifier` is whoever signs the transaction (may or may not be the preserver).

```rust
#[instruction]
pub fn verify_holding(
    #[account(mut, pda = [literal("item"), arg("ia_id")])]
    item: AccountWithMetadata,

    // the node whose holding is being verified — not required to be the signer
    preserver: AccountWithMetadata,

    #[account(mut, pda = [literal("pres"), account("preserver"), arg("ia_id")])]
    record: AccountWithMetadata,

    #[account(mut, pda = [literal("stats"), account("preserver")])]
    stats: AccountWithMetadata,

    #[account(mut, pda = [literal("pool")])]
    pool: AccountWithMetadata,

    // signer: the preserver (self-verification) or any peer (community verification)
    #[account(signer)]
    verifier: AccountWithMetadata,

    ia_id:        String,
    block_number: u64,   // submitted by Keeper client
) -> SpelResult {
    // prev_status = record.verification_status
    // record.last_verified_block = block_number
    //
    // new_status:
    //   if gap > pool.delinquency_threshold → Delinquent
    //   else if block_number - item.block < pool.min_hold_blocks → Pending  // new — under minimum hold
    //   else → Active
    //
    // if prev_status != Active AND new_status == Active:
    //   stats.active_bytes          += record.total_bytes
    //   pool.total_active_bytes     += record.total_bytes
    //   item.total_active_bytes     += record.total_bytes   // per-item counter for bounty distribution
    //
    // if prev_status == Active AND new_status != Active:
    //   stats.active_bytes          -= record.total_bytes
    //   pool.total_active_bytes     -= record.total_bytes
    //   item.total_active_bytes     -= record.total_bytes
    //
    // record.verification_status = new_status
}
```

### Self-verification (Keeper, runs on a timer)

Keeper checks its own storage and submits a holding proof every 7 days per registered item:

```cpp
void KeeperPlugin::runVerificationCycle() {
    for (const auto& entry : log_) {
        QString cid = entry["collectionCid"].toString();
        QString id  = entry["id"].toString();
        if (cid.isEmpty()) continue;

        m_storage->existsAsync(cid, [this, id, cid](LogosResult r) {
            if (r.success && r.value.toBool()) {
                // CID is held locally → submit verify_holding tx to keeper_protocol
                submitVerifyHoldingTx(id);
            } else {
                // No longer held — warn user, do not submit
                qWarning() << "KeeperPlugin: CID no longer local:" << cid;
                emitEvent("holdingLost", {QVariantMap{{"id", id}, {"cid", cid}}});
            }
        });
    }
}
```

### Community verification (any peer)

Any Keeper node can verify another node's holdings by attempting a network retrieval
and submitting the same `verify_holding` instruction (verifier ≠ preserver):

```cpp
// Verifier: tries to fetch the target preserver's CID from the network
m_storage->downloadChunksAsync(collectionCid, /*local=*/false, chunkSize,
    [this, ia_id, targetPreserverId](LogosResult r) {
        if (r.success)
            // Any peer can submit verify_holding on behalf of the preserver
            submitVerifyHoldingTx(ia_id, targetPreserverId);
        // If fetch fails → simply do not submit; absence of verification
        // accumulates toward the delinquency threshold naturally
    });
```

### Full inventory reconciliation

`manifests()` returns everything the local node holds. Cross-reference against
on-chain registered items to surface any that are no longer stored:

```cpp
m_storage->manifestsAsync([this](LogosResult r) {
    QStringList heldCids = parseManifestList(r.value.toString());
    for (const auto& entry : log_) {
        QString cid = entry["collectionCid"].toString();
        if (!heldCids.contains(cid))
            emitEvent("holdingLost", {QVariantMap{{"cid", cid}}});
    }
});
```

### Verification cadence and delinquency

| Event | Action |
|-------|--------|
| Registration | `record.last_verified_block` set to `block_number` |
| Every 7 days | Keeper calls `exists()`, submits `verify_holding` tx if true |
| Gap > 30 days | `record.verification_status = Delinquent` for **this item only** |
| Delinquent | Confirmation bonuses suspended for this item; other items unaffected |
| Re-verified | Status restored to `Active`; bonuses resume for this item |

Delinquency is per `PreservationRecord` — one item going delinquent does not affect
the user's `UserStats` for other items or their ability to register new ones.
The 30-day window tolerates node restarts and short network disruptions.

---

## Community Filtration

### Suspicion severity

Not all hash divergences are equal:

| Divergence | Likely cause | Severity | Action |
|------------|-------------|----------|--------|
| Same metadata + merkle, different CID | Different IPFS chunking | Low | Log only, 10% reward |
| Same metadata, different merkle | Different files uploaded, or file modified | Medium | 0 reward, Suspicious |
| Different metadata hash | IA metadata changed, or tampering | High | 0 reward, item Suspicious — caller must call `open_dispute` |

### Auto-flagging logic in `register_preservation`

```rust
let metadata_diverges = canonical.metadata_hash  != metadata_hash;
let merkle_diverges   = canonical.merkle_root    != merkle_root;
let cid_diverges      = canonical.collection_cid != collection_cid;

let severity = match (metadata_diverges, merkle_diverges, cid_diverges) {
    (true,  _,     _    ) => Severity::High,
    (false, true,  _    ) => Severity::Medium,
    (false, false, true ) => Severity::Low,
    (false, false, false) => Severity::None,
};
```

`High` and `Medium` → `item.status = Suspicious`; preserver must follow with `open_dispute` to create a formal `DisputeRecord`.
`Low` → logged in `PreservationRecord.matches_canonical = false`, no escalation.

---

## Leaderboards

Both leaderboards read from the same `stats` PDA per user.
Enumeration: fetch the `registry` singleton PDA → get `Vec<AccountId>` → fetch each
`stats` PDA → sort client-side. No off-chain indexer needed.

### Pioneer Board — `first_preserved_count` desc

Who discovers and preserves new items first. Confirmations do not count.

| Rank | User | First preserved |
|------|------|----------------|
| 1 | alice | 340 |
| 2 | bob | 218 |
| 3 | carol | 104 |

### Archive Board — `total_bytes_preserved` desc

Who keeps the most data on the network.
Both first preservations and confirmations count — data is actually held in both cases.

| Rank | User | Data preserved |
|------|------|---------------|
| 1 | bob | 4.2 TB |
| 2 | dave | 1.8 TB |
| 3 | alice | 900 GB |

### Reputation score

Surfaced as a third column on either board:

```
reputation = confirmed_count / (confirmed_count + mismatch_count)
```

A user with 500 preservations and 30 mismatches ranks below one with 200 and 0 mismatches.
Directly incentivizes honest, complete preservation.

---

## Integration with Keeper Module

Add one step after `inscribeToBeacon` in `keeper_plugin.cpp`:

```
download → Stash → get collection CID
         ↓
    Beacon inscription           (existing — unchanged)
         ↓
    compute metadata_hash
         + file_merkle_root      (new — uses already-saved temp files)
         ↓
    keeper_protocol::register_preservation(...)   (new)
         ↓
    keeper_score updated on-chain (soulbound, leaderboard)
         ↓
    every 7 days: verify_holding() → active_bytes maintained
         ↓
    monthly: claim_monthly_reward(month) → stable transferred from pool
```

The generated FFI client from `spel-client-gen --target logos-module` provides typed
C++ bindings for all instructions directly from the IDL — no hand-written IPC code needed.

---

## What LEZ Handles

| Need | LEZ mechanism |
|------|--------------|
| First-inscriber atomicity | Handler checks `first_preserver == AccountId::default()` on `#[account(mut)]` item — first writer wins by blockchain ordering |
| On-chain CID record | `ItemRecord` account — queryable by anyone, independent of Beacon |
| Per-user stats + soulbound score | `UserStats` PDA — `keeper_score` (leaderboard) and `active_bytes` (reward share) |
| Fee-funded reward pool | `register_preservation` charges `registration_fee` + `holding_deposit` per call |
| Institutional pool funding | `fund_pool` — permissionless top-up; open to Logos Foundation, Internet Archive, grants, donors |
| Monthly stable reward | `claim_monthly_reward` transfers stable pro-rata by active_bytes; hard cap per user |
| Holding accountability | Per-item `holding_deposit` escrowed; forfeited via `finalize_challenge` if preserver fails challenge |
| Third-party audit incentive | `challenge_holding` + `finalize_challenge` — challenger earns bounty for catching delinquent node |
| Demand-side sponsorship | `create_item_bounty` + `fund_item_bounty` + `claim_item_bounty` — sponsors target specific items; two-sided market |
| Clean exit | `deregister_user` + `deregister_item` — returns deposits, zeroes active_bytes, history preserved |
| Score recovery | `migrate_score` — dual-signed key migration; soulbound score survives wallet compromise |
| Suspicion detection | Logic inside `register_preservation` — pure zkVM arithmetic |
| Community voting | `vote_on_dispute` weighted by `first_preserved_count` |
| Cross-program composition | `ctx.caller_program_id` — other programs can verify holdings |

## Design Gaps

Gaps found by comparing the design above against the SPEL framework source
(`logos-co/spel` v0.4.0, `spel-framework-macros/src/lib.rs` and
`spel-framework-core/src/`).

---

### GAP 1 — `current_block` is not available inside handlers

**Severity: High — Status: Resolved**

The doc uses `current_block` in `verify_holding` (`last_verified_block`) and in
`register_preservation` (`ItemRecord.block`, `opened_at_block`). `ProgramContext`
only exposes `self_program_id` and `caller_program_id` (confirmed:
`spel-framework-core/src/context.rs`). `ProgramInput` destructuring in the macro
shows `{ self_program_id, caller_program_id, pre_states, instruction }` — no block
field.

**Resolution applied:** `block_number: u64` added as an explicit argument to
`register_preservation`, `open_dispute`, and `verify_holding`. Submitted by the
Keeper client; trust-based until LEZ exposes it natively.

---

### GAP 2 — `#[account(init)]` on `stats` and `balance` fails on second call

**Severity: High — Status: Resolved**

`register_preservation` declared `stats` and `balance` with `#[account(init)]`. The
macro generates: `if accounts[idx].account != Account::default() { return Err(AccountAlreadyInitialized) }`. On a preserver's second registration (different item),
both PDAs already exist with data — the instruction fails before the handler runs.

**Resolution applied:** split into two instructions:

- `create_user` — `#[account(init)]` on `stats`; called once per user.
- `register_preservation` — `#[account(mut)]` on `stats`; assumes it exists.

Keeper calls `create_user` on first launch, then `register_preservation` for each item.
(`TokenBalance` removed entirely under the dual token model — soulbound score lives in
`UserStats.keeper_score`; economic tokens are minted by `claim_monthly_reward`.)

---

### GAP 3 — `DisputeRecord` cannot be created inside `register_preservation`

**Severity: High — Status: Resolved**

The logic block said "create DisputeRecord if not already open" inside the handler.
In LEZ, all accounts that a handler writes must be declared as instruction parameters
upfront — the macro only generates post-states for declared accounts. Creating a new
PDA inside a handler body with no corresponding parameter is not possible.

**Resolution applied:** removed auto-creation from `register_preservation`. The handler
only sets `item.status = Suspicious` and `item.mismatch_count += 1`. Opening a formal
`DisputeRecord` is always an explicit separate call to `open_dispute` by the challenger.
Auto-flagging is purely a status field on `ItemRecord`.

---

### GAP 4 — `verify_preservation` instruction is redundant

**Severity: Medium — Status: Resolved**

The instruction was described as "read-only" and "returns the full `ItemRecord`". But
LEZ instructions are state-transition functions — they must return post-states, not
query results. A no-op instruction that returns identical pre-states is valid but
pointless: you cannot read the return value from a submitted transaction.

**Resolution applied:** `verify_preservation` removed from the instruction set.
Account inspection documented in the Content Verification section:
`spel inspect <item-pda> --type ItemRecord` (CLI) or
`keeper_protocol_fetch_item_record()` (generated FFI client).

---

### GAP 5 — `confirm_reachable` and `challenge_holding` undefined

**Severity: Medium — Status: Resolved**

Both were referenced by name in the Storage Holding Verification section but had no
instruction definition.

**Resolution applied (v1):** `confirm_reachable` collapsed into `verify_holding` — any
signer (`verifier`) can submit it for any `preserver`. Positive confirmation (CID reachable)
uses `verify_holding`; there was originally no path for negative evidence.

**Extended resolution (market research gap D):** `challenge_holding` added as a distinct
instruction that handles the negative case. A challenger who asserts a CID is unreachable
creates a `ChallengeRecord` PDA with a deadline; if the preserver cannot respond with a
valid `verify_holding` before the deadline, `finalize_challenge` sets them Delinquent
and pays the challenger a bounty from the holding deposit. This gives third-party verifiers
an economic reason to run audits.

---

### GAP 6 — Delinquency is per-item, doc implies per-user

**Severity: Medium — Status: Resolved**

`verification_status` sits on `PreservationRecord` (one per preserver+item), but the
cadence table said "Rewards suspended; confirmed count frozen" as if the whole account
is affected.

**Resolution applied:** cadence table updated — delinquency text now explicitly scoped
to "this item only". Added note that `UserStats` totals are not frozen globally.

---

### GAP 7 — Leaderboard enumeration has no on-chain solution

**Severity: Medium — Status: Resolved**

The doc said "off-chain indexer reads all `stats` PDAs". But to compute a `stats` PDA
address you need the preserver's `AccountId`. There is no `getAllAccounts` query on
LEZ — there is no way to enumerate all `stats` PDAs without already knowing all
preserver IDs.

**Resolution applied:** `PreserverRegistry` singleton PDA `[literal("registry")]`
holds `Vec<AccountId>` of all preservers. `create_user` appends to it and passes it
as a `#[account(mut)]` parameter (LEZ-legal — declared upfront). Leaderboard reads
the registry to get all IDs, then fetches each `stats` PDA. `Vec` size limits apply;
pagination strategy deferred until adoption warrants it.

---

### GAP 8 — Slash mechanism is undefined

**Severity: Low — Status: Resolved**

The reward table referenced "50% of slashed tokens from losing party" on dispute
resolution. No slash instruction or escrow mechanism was defined, and SPEL has no
native stake/escrow primitive.

**Resolution applied:** dispute resolution is social signal only for v1. Slashing
deferred to v2 as a **voluntary dispute stake**:

- Preservers who want to challenge or defend a canonical record lock stable as collateral
  via a new `lock_dispute_stake` instruction.
- The loser forfeits a configurable portion of their locked stable to the winner.
- `keeper_score` is never at risk — only the voluntarily locked stable is.
- Joining the protocol remains free; the dispute stake is opt-in for those who wish to
  participate in governance.

This requires a new `dispute_stake` field on a `UserDisputeStake` PDA and a
`resolve_dispute` instruction (both v2).

---

### GAP 9 — `What LEZ Handles` table is inconsistent with the instruction

**Severity: Low — Status: Resolved**

The table said "First-inscriber atomicity: `#[account(init)]` on item PDA" but
`register_preservation` declares item as `#[account(mut)]`. Atomicity comes from
blockchain ordering — the first tx to write `first_preserver` wins; subsequent txs
see a non-default value and take the other branch.

**Resolution applied:** table entry corrected to "handler checks
`first_preserver == AccountId::default()` on `#[account(mut)]` item — first writer
wins by blockchain ordering."

---

## Honest Gaps (runtime / trust)

| Gap | Note |
|-----|------|
| Proof CID is reachable on network | Verified via `downloadChunks(local=false)` — off-chain, any peer can check and submit `verify_holding` or `challenge_holding` |
| Proof node is *continuously* holding | Preserver can unpin and re-pin just before the deadline — `challenge_holding` creates economic pressure against this |
| Proof files are authentic IA content | Trust IA's sha1/md5 in metadata; `metadata_hash` anchors the claim at time of preservation |
| Sybil resistance | Mitigated by `holding_deposit` (economic downside) + `challenge_holding` (active catching) |
| `block_number` in instructions | Submitted by client, not injected by runtime — trust-based until LEZ exposes it natively |
| Full zk proof of holding | v1 is challenge-response (economic deterrent); v2 target is ZK proof of data possession (zk-SNARK PoDP, analogous to Filecoin PoDP launched May 2025) ¹ |

---

## Market Research Gaps — Status

Gaps identified by benchmarking against Filecoin, Arweave, Storj, and DePIN tokenomics research.
See Research Footnotes for sources.

| Gap | Severity | v1 Status | Resolution |
|-----|----------|-----------|------------|
| **A** — No cryptographic proof; verification self-reported | Critical | Partial | `challenge_holding` + `finalize_challenge` add economic deterrent; full ZK proof deferred to v2 |
| **B** — No collateral; zero downside for fake preservation | High | Resolved | `holding_deposit_amount` in `RewardPool`; per-item deposit escrowed at registration; forfeited on challenge failure |
| **C** — Cold start / empty pool kills participation | High | Resolved | Bootstrap strategy documented; foundation seed + public funding schedule required pre-launch |
| **D** — Third-party verifier has no economic incentive | Medium | Resolved | `challenge_holding` pays `challenge_bounty_bp` of holding deposit to successful challenger |
| **E** — `PreserverRegistry` grows forever; ghost accounts | Medium | Resolved | `deregister_user` zeroes active_bytes, sets `is_deregistered`; leaderboard filters flag |
| **F** — Soulbound score lost on key compromise | Medium | Resolved | `migrate_score` — dual-signed migration; old identity retired, new identity carries full history |
| **G** — No demand side; sponsors cannot express preference | Medium | Resolved | `create_item_bounty` + `fund_item_bounty` + `claim_item_bounty`; per-item bounty claimable monthly |
| **H** — No deflationary flywheel | Low | By design | Stable-only redistribution is an explicit choice; documented in Dual Token Model |

---

## Competitive Positioning

### Where Keeper shines

| Dimension | Keeper | Filecoin | Arweave | Storj |
|-----------|--------|----------|---------|-------|
| **Purpose** | Cultural preservation (Internet Archive) | General-purpose storage market | Permanent storage of any data | Enterprise/S3-compatible storage |
| **Reward currency** | Stable (no volatility risk) | FIL (volatile) | AR (volatile) | STORJ (volatile) |
| **Joining cost** | Free (holding deposit only) | High collateral + hardware | None (read-only) | Node setup cost |
| **Soulbound reputation** | Yes — `keeper_score` permanent, unkillable | No | No | No |
| **Demand-side sponsorship** | Yes — `fund_item_bounty` per-item bounty | Via storage deals | Via endowment | Not applicable |
| **Institutional top-up** | Yes — `fund_pool` permissionless | No direct equivalent | Foundation grants (off-chain) | VC-funded company |
| **Score recovery** | Yes — `migrate_score` dual-signed | N/A | N/A | N/A |
| **Leaderboard on-chain** | Yes — `PreserverRegistry` + `UserStats` | No | No | No |
| **Content identity layer** | Yes — IA identifier + 3-hash commitment | No (sector-level only) | TX hash only | No |
| **Community audit incentive** | Yes — `challenge_holding` bounty | Automatic zk-SNARK (stronger) | Random sampling (weaker) | Centralized satellite |
| **Proof strength** | Economic deterrent (v1); ZK target (v2) | zk-SNARK PoRep + PoSt (strongest) | Content-addressing only | Erasure-coded audits |

### Where Keeper lags (known, accepted)

| Dimension | Gap vs leader | Leader | Keeper v2 target |
|-----------|--------------|--------|-----------------|
| Proof cryptographic strength | Filecoin uses zk-SNARK; Keeper uses economic deterrent | Filecoin | ZK proof of data possession |
| Storage market size | Filecoin has 20+ EiB; Keeper targets IA-specific niche | Filecoin | Not a general market — intentional |
| Hardware agnosticism | Storj runs on commodity hardware with S3 API | Storj | Logos Storage node required |
| Endowment permanence | Arweave's 200-year endowment model is mathematically proven | Arweave | Monthly fee model requires ongoing activity |

### Unique advantages no competitor has

1. **Cultural mission + protocol alignment** — Keeper is the only storage protocol designed specifically for Internet Archive collections. The IA identifier is a first-class on-chain primitive, not an afterthought.

2. **Soulbound reputation that cannot be bought** — `keeper_score` is earned only through preservation work and transfers with the person (via `migrate_score`), not the wallet. No competitor has a non-financial on-chain reputation layer tied to storage behaviour.

3. **Two-sided market at the item level** — `fund_item_bounty` lets Internet Archive, libraries, or individuals pay to prioritise specific collections. No other storage protocol expresses demand at the content identity level — they work at the byte or sector level only.

4. **Free entry with skin in the game** — The holding deposit model provides economic accountability without an entrance barrier. Filecoin requires significant upfront collateral. Keeper requires only the deposit for items you actually register, refundable on clean exit.

5. **Stable reward eliminates participation boom/bust** — DePIN protocols with volatile token rewards see node counts swing with price. Keeper's stable reward makes ROI calculation for preservers simple and predictable — lowering the barrier for non-crypto-native institutions like libraries and universities.

---

## SPEL Framework Upstream Research

Findings from reading `logos-co/spel` v0.4.0 source at `~/basecamp/refs/spel`.

### Confirmed facts

| Topic | Finding |
|-------|---------|
| **Token transfer** | No native primitive. `SpelOutput.chained_calls: Vec<ChainedCall>` is the cross-program call mechanism. Fixture `transfer` instruction is a pure account data mutation. |
| **Multi-seed PDA** | `pda = [...]` and `pda = arg(...)` are an **open upstream issue** (`spel-framework/issues/1`), not yet in the macro. Only `pda = literal("x")` and `pda = account("name")` (single seed) work today. |
| **Seed hashing** | Multi-seeds will combine via `sha256(s1 \|\| s2 \|\| ... \|\| sN)`. Seed types: `literal` → UTF-8 zero-padded to 32 bytes; `account` → 32-byte AccountId; `arg` → serialised bytes zero-padded to 32. |
| **`#[account_type]` on enums** | Enum helper types (`VerificationStatus`, `ItemStatus`, `DisputeStatus`) must carry `#[account_type]` to appear in `SpelIdl::types` and be resolvable by the IDL BFS scanner. Fixed in this doc. |
| **SpelError variants** | `AccountAlreadyInitialized` (code 1002), `AccountNotInitialized` (1003), `InsufficientBalance` (1004), `Overflow` (1007), `Unauthorized` (1008), `PdaMismatch` (1009). Domain errors use `SpelError::custom(code, message)` (offset 6000). |
| **Block number** | NOT injected by runtime. `block_validity_window` constrains tx validity range but does not expose block number inside a handler. Confirmed: keeper doc's trust-based `block_number: u64` parameter is the only option for v1. |
| **Variable accounts** | `Vec<AccountWithMetadata>` works for rest-style variable-length account lists (confirmed via `batch_update` fixture). `PreserverRegistry` as `Vec<AccountId>` is valid. |
| **Private PDAs** | Supported via `#[account(init, private_pda, pda = ..., npk = arg("user_npk"))]`. Not used in keeper v1 but available. |
| **`ProgramContext`** | `ctx: ProgramContext` provides `self_program_id` and `caller_program_id`. Never part of instruction ABI or IDL. Useful for `#[account(owner = self_program_id)]` constraints. |

### Upstream blockers and workarounds

#### Blocker 1 — Multi-seed PDA (`spel-framework/issues/1`)

All PDAs except `pool` and `registry` require multi-seed or `arg()` seed syntax not yet in the macro.

**Workaround A — Manual PDA verification in handler body**

Remove `pda = ...` from `#[account]`. Declare the account as plain `#[account(init)]` or
`#[account(mut)]`, then verify the address inside the handler using the already-implemented
`compute_pda_multi()`:

```rust
use spel_framework_core::pda::{compute_pda_multi, seed_from_str};

#[instruction]
pub fn register_preservation(
    #[account(init)]          // ← no pda= constraint; verified manually below
    pres: AccountWithMetadata,
    preserver: AccountWithMetadata,
    ia_id: String,
    ...
) -> SpelResult {
    let expected = compute_pda_multi(
        &ctx.self_program_id,
        &[&seed_from_str("pres"), &preserver.id().to_seed(), &ia_id.to_seed()],
    );
    if pres.id() != expected {
        return Err(SpelError::PdaMismatch { account_name: "pres".into(),
            expected: format!("{:?}", expected), actual: format!("{:?}", pres.id()) });
    }
    // ... rest of handler
}
```

`compute_pda_multi()` and the `sha256(s1 || s2 || ... || sN)` hash scheme are already in
`spel-framework-core/src/pda.rs`. The macro is the only missing piece. On-chain safety
is identical to the constrained form — the IDL just won't encode seed metadata, so the
generated C++ client must compute addresses itself using the same scheme.

**Workaround B — Single-account seed (for `stats` only)**

`stats::{user}` can use `pda = account("user")` (single AccountId seed — works today).
No literal prefix needed; the program ID already namespaces it. No collision risk because
`stats` is the only single-account PDA in keeper.

#### Blocker 2 — ChainedCall stable transfer

The `ChainedCall` struct comes from `nssa_core::program` which is not in the local refs.
Its call format is unknown until LEZ devs document the stable token program.

**Workaround — Internal ledger model**

Track all stable balances as `u128` fields in keeper-owned accounts. "Transfer" becomes
a pure account data mutation — no cross-program call needed:

```rust
// Instead of: transfer(pool, user_wallet, amount)
pool.fee_balance           -= amount;
user_stats.claimable_balance += amount;

// User calls withdraw(amount) to move from claimable_balance to their real wallet.
// withdraw() is the only instruction that needs ChainedCall — isolated to one place.
```

`withdraw` is the single seam between the internal ledger and the external token world.
When `ChainedCall` format is confirmed, only `withdraw` changes — all other instruction
logic is unaffected.

### Build phases

| Phase | Instructions | Blocker workaround |
|-------|--------------|--------------------|
| **1 — Singletons** | `initialize_program`, `create_user`, `fund_pool` | `pool`/`registry` use single-literal seeds (work today); `stats` uses single-account seed (Workaround B) |
| **2 — Core flow** | `register_preservation`, `verify_holding`, `claim_monthly_reward`, `deregister_item`, `deregister_user` | Manual PDA verification (Workaround A) + internal ledger (Workaround 2) |
| **3 — Audit + bounty** | `challenge_holding`, `finalize_challenge`, `create_item_bounty`, `fund_item_bounty`, `claim_item_bounty` | Manual PDA verification (Workaround A) + internal ledger |
| **4 — Governance** | `open_dispute`, `vote_on_dispute`, `migrate_score` | Manual PDA verification (Workaround A) |
| **Upgrade A** | Replace manual PDA checks with `pda = [...]` macro constraints | When `spel-framework/issues/1` lands |
| **Upgrade B** | Replace `withdraw` internal ledger with `ChainedCall` to real stable token | When LEZ stable token API is confirmed |

Phase 1 can start immediately with no upstream dependencies.

---

## Research Footnotes

¹ **Cold start / DePIN tokenomics** — Messari, *DePIN Tokenomics Part 2: Finding the Right Balance*, 2025.
  Frontiers in Blockchain, *Decentralized physical infrastructure networks (DePIN) tokenomics*, 2025.
  https://messari.io/report/depin-tokenomics-part-2-finding-the-right-balance-for-depin-token-rewards
  https://www.frontiersin.org/journals/blockchain/articles/10.3389/fbloc.2025.1644115/full

² **Volatile token reward boom/bust** — Storj DePIN analysis, *How AI is pushing the evolution of DePIN services*, 2025.
  https://www.storj.io/blog/how-ai-is-pushing-the-evolution-of-depin-services

³ **Challenge-response audit incentive** — Storj design docs, *Reputation and node selection*, 2019 (still operative).
  Filecoin Docs, *Storage proving* and *Slashing*.
  https://github.com/storj/design-docs/blob/main/20190909-reputation-and-node-selection.md
  https://docs.filecoin.io/storage-providers/filecoin-economics/storage-proving
  https://docs.filecoin.io/storage-providers/filecoin-economics/slashing

⁴ **Demand-side sponsorship / two-sided market** — Filecoin storage deals model.
  Arweave endowment — *Endowment with Arweave*, arweave.com.
  https://www.arweave.com/blog/endowment-with-arweave
  https://permaweb-journal.arweave.net/article/storage-endowment-explained.html

⁵ **Exit / deregistration** — Filecoin sector expiry and termination fees (FIP-0098).
  https://docs.filecoin.io/storage-providers/filecoin-economics/storage-proving

⁶ **Soulbound token key recovery** — ERC-5192 Minimal Soulbound NFTs; Gitcoin Passport re-attestation pattern.
  https://eips.ethereum.org/EIPS/eip-5192
  https://www.coingecko.com/learn/soulbound-tokens-sbt
