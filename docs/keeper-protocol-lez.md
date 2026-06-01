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

User sets storage pledge (client settings):
  → storage_quota: GB willing to donate
  → channel subscriptions: IA priority list, community lists, per-institution feeds
  → per-channel: auto_preserve (fills quota automatically) or require_approval (manual toggle)
  → client monitors channels, queues collections up to quota, preserves without user action

User preserves IA item (auto or manual):
  → Keeper downloads files from channel queue
  → uploads to Logos Storage → collection CID
  → keeper_protocol::register_preservation(ia_id, cid, hashes, ...)
      → ItemRecord PDA (first-write atomic — first preserver wins)
      → PreservationRecord PDA (per-user per-item; status = Pending)
      → UserStats.keeper_score incremented (soulbound, leaderboard only)

Every 7 days per item:
  → keeper_protocol::verify_holding()
      → record.verification_status updated (Pending / Active / Delinquent)
      → UserStats.active_bytes and RewardPool.total_active_bytes maintained

At any time (any node — challenge-response audit):
  → keeper_protocol::challenge_holding(ia_id, preserver)
      → challenger asserts CID is unreachable; sets deadline
  → preserver must respond with verify_holding before deadline
  → keeper_protocol::finalize_challenge(ia_id, preserver)
      → if preserver responded: cleared; challenger gets keeper_score
      → if preserver silent: immediately Delinquent; record flagged

// v2 — reward layer (inactive in v1):
//   fund_pool, claim_monthly_reward, take_monthly_snapshot, snapshot_user,
//   create_item_bounty, fund_item_bounty, claim_item_bounty
```

The on-chain `ItemRecord` is the source of truth — not Beacon, not Cord.

> **Instruction scope note:** The flowchart above shows the core use case (10 of 23
> total instructions: 16 active in v1, 7 reserved for v2). Omitted for brevity:
> v1: `initialize_program`, `open_dispute`, `vote_on_dispute`, `withdraw`, `deregister_user`,
> `deregister_item`, `migrate_score`, `create_channel`, `add_channel_entry`,
> `verify_channel`, `subscribe_channel`; v2: `fund_pool`, `snapshot_user`,
> `take_monthly_snapshot`, `claim_monthly_reward`, `create_item_bounty`,
> `fund_item_bounty`, `claim_item_bounty`. See the Instructions section for the complete set.

## Channel Subscriptions

Channels are curated, on-chain lists of IA collections worth preserving. Every
`(ia_id, CID)` pair is inscribed directly on-chain as a `ChannelEntry` PDA —
no off-chain files, no URL dependencies. Channels are as censorship-resistant as
the preservation records themselves.

### Account types

**`Channel`** — one per channel. Holds curator identity and verified status.

```rust
#[account_type]
pub struct Channel {
    pub channel_id:  String,
    pub curator:     AccountId,    // who manages this channel
    pub name:        String,
    pub description: String,
    pub verified:    bool,         // set by verify_channel (authority-only)
    pub entry_count: u64,
}
```

**`ChannelEntry`** — one per `(channel_id, ia_id)`. The inscription: curator attests
this collection at this CID is worth preserving.

```rust
#[account_type]
pub struct ChannelEntry {
    pub channel_id:     String,
    pub ia_id:          String,
    pub canonical_cid:  [u8; 32],  // CID curator attests for this collection
    pub added_at_block: u64,
}
```

**`ChannelRegistry`** — singleton enumerating all channels.

```rust
#[account_type]
pub struct ChannelRegistry {
    pub channels: Vec<AccountId>,  // channel PDAs in creation order
}
```

**`ChannelSubscription`** — one per `(user, channel_id)`. Records the subscription
and the user's mode preference.

```rust
#[account_type]
pub struct ChannelSubscription {
    pub user:        AccountId,
    pub channel_id:  String,
    pub mode:        SubscriptionMode,  // Auto | Approve
}

#[account_type]
pub enum SubscriptionMode { Auto, Approve }
```

### Instructions

**`create_channel(channel_id, name, description)`** — initialises a `Channel` PDA.
Anyone can create a channel; `curator = signer`.

**`add_channel_entry(channel_id, ia_id, canonical_cid)`** — curator-signed. Inscribes
one `(ia_id, CID)` pair on-chain. Increments `channel.entry_count`.

**`verify_channel(channel_id)`** — authority-signed (IA or Logos-designated key).
Sets `channel.verified = true`. Verified channels surface first in the client UI.

**`subscribe_channel(channel_id, mode)`** — creates a `ChannelSubscription` PDA for
the calling user. Client reads this PDA to know which channels to monitor.

### Client behaviour

```
// User sets storage pledge once:
settings.storage_quota = 200  // GB

// Client reads ChannelSubscription PDAs for this user,
// fetches ChannelEntry PDAs for each subscribed channel,
// queues items ordered by fewest active preservers first,
// auto-preserves (mode=Auto) or surfaces for approval (mode=Approve)
// up to storage_quota.
```

### Pagination

`ChannelEntry` PDAs for a large channel (e.g. IA priority list with thousands of items)
use the same enumeration pattern as `PreserverRegistry`: the client iterates all
`chanentry::{channel_id}::{ia_id}` PDAs. Past ~320k entries per channel the same
pagination concern applies — v2 plan: `ChannelEntryPage` shards.

---

## Dual Token Model

| Layer | Token | Nature | Source | Use |
|-------|-------|--------|--------|-----|
| **Reputation** | `keeper_score` | Soulbound, non-transferable | Earned by preserving items | Leaderboard, dispute voting weight, determines monthly reward **share** |
| **Economic** | Stable (configurable at deploy) | Transferable, real value | External sources (see below) | Funds the reward pool; paid out monthly in the same stable |

**`keeper_score` is not a financial asset.** It has no price, no market, no transfer. It is a permanent on-chain record of preservation work — like a credit score. It cannot be bought.

**Stable is the only money in the system.** In v1 the reward layer is inactive — preservers earn `keeper_score` only. In v2, stable rewards are enabled and the pool is funded from three external sources:

> **Source 1 — Stake-yield.** Holding deposits are escrowed on-chain, not burned. In v2 they are parked in a low-risk yield-bearing instrument; the yield flows into `pool.fee_balance` while the principal remains fully returnable to the preserver on clean exit. Value is genuinely external — no dilution, no donation dependency.
>
> **Source 2 — External funding.** Preservation of public-good archives is exactly what institutions fund. `fund_pool` is a permissionless top-up instruction open to the Internet Archive, university endowments, grants, and individual donors. `fund_item_bounty` lets sponsors target specific collections directly — preservers of that item earn the bounty on top of their monthly pool share.
>
> **Source 3 — Challenge forfeitures.** A failed audit slashes the preserver's holding deposit into the pool, with a cure period first (re-prove possession before the forfeit finalises) so the goal stays clean content, not punishment. The successful challenger earns a modest capped bounty — enough to reward the catch; not enough to make hunting failures the business model.

The stable token address is a program constant set at deployment — USDC, a Logos native stable, or any compatible token.

> **Design choice — stable-only, no flywheel token:** DePIN protocols that reward in their own volatile token create boom/bust participation cycles tied to price, not storage quality. ² Keeper deliberately avoids this: all value in the reward pool is externally sourced stable — no speculation layer, no reflexive tokenomics. Preservation as a public good, not an investment vehicle. This is a stated design decision, not an omission.

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
    pub last_verified_block:  u64,
    pub verification_status:  VerificationStatus,
    pub open_challenge_count: u32,  // incremented by challenge_holding; decremented by finalize_challenge
                                    // deregister_item must reject if > 0 (H3 fix)
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
    // Internal ledger workaround (v1): accrued stable pending withdrawal.
    // Populated by claim_monthly_reward and claim_item_bounty.
    // Cleared by withdraw(). Remove when ChainedCall stable transfer is confirmed.
    pub claimable_balance:     u128,
    // Per-user active_bytes snapshot (Finding 11 fix): locked at snapshot_user() time.
    // claim_monthly_reward uses this value, not live active_bytes.
    pub snapshot_active_bytes: u64,
    pub snapshot_claim_month:  u32,    // YYYYMM of the active snapshot; 0 = never snapshotted
}
```

### `RewardPool`
Singleton. Accumulates registration fees and tracks the network-wide active bytes
counter. Updated by `register_preservation` (fees in) and `verify_holding` (bytes counter).

```rust
#[account_type]
pub struct RewardPool {
    pub total_active_bytes:        u128,   // sum of active_bytes across all preservers
    pub min_hold_blocks:           u64,    // blocks an item must be held before Active
    pub challenge_response_window: u64,    // blocks preserver has to respond to a challenge
    pub delinquency_threshold:     u64,    // blocks since last_verified_block before status → Delinquent
    // v2 reward fields (inactive in v1 — set to 0 at initialize_program):
    pub fee_balance:               u128,   // stable from institutional top-ups (fund_pool)
    pub max_user_share_bp:         u32,    // basis points cap per user per month
    pub last_month:                u32,    // last YYYYMM distributed
    pub snapshot_balance:          u128,   // fee_balance snapshot for claim_monthly_reward
    pub snapshot_total_active_bytes: u128, // total_active_bytes snapshot
    pub snapshot_month:            u32,    // YYYYMM of active snapshot; 0 = none
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
    pub active_bytes_snapshot: u64,   // user.active_bytes at snapshot_user() time, not claim time
    pub amount:              u128,    // stable transferred this claim
    pub claimed:             bool,
}
```

### `BountyClaim`
One per `(user, ia_id, month)`. Prevents double-claiming the same item bounty in the same month.
Created by `claim_item_bounty`. (Finding 12 fix)

```rust
#[account_type]
pub struct BountyClaim {
    pub user:   AccountId,
    pub ia_id:  String,
    pub month:  u32,
    pub amount: u128,   // stable paid out this claim
}
```

### `VoteRecord`
One per `(voter, ia_id)`. Prevents the same voter casting multiple votes on the same dispute.
Created by `vote_on_dispute` as `#[account(init)]`. (M3 dedup guard)

```rust
#[account_type]
pub struct VoteRecord {
    pub voter:  AccountId,
    pub ia_id:  String,
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
| `vote::{voter}::{ia_id}` | `[literal("vote"), account("voter"), arg("ia_id")]` | (voter, item) — M3 dedup guard |
| `channel::{channel_id}` | `[literal("channel"), arg("channel_id")]` | channel |
| `chanentry::{channel_id}::{ia_id}` | `[literal("chanentry"), arg("channel_id"), arg("ia_id")]` | (channel, item) inscription |
| `chansub::{user}::{channel_id}` | `[literal("chansub"), account("user"), arg("channel_id")]` | (user, channel) subscription |
| `chanreg` | `[literal("chanreg")]` | singleton channel registry |
| `pool` | `[literal("pool")]` | singleton |
| `registry` | `[literal("registry")]` | singleton |

> **Multi-seed PDA confirmed working** (vpavlin, 2026-06-01). All `pda = [...]` array syntax
> in this table is implementable as written. See Upstream Research for details.

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

    min_hold_blocks:           u64,    // e.g. ~30 days of blocks before Pending → Active
    delinquency_threshold:     u64,    // e.g. ~30 days of blocks; gap before Active → Delinquent
    challenge_response_window: u64,    // e.g. ~3 days of blocks for preserver to respond
) -> SpelResult
// Logic:
//   pool.total_active_bytes        = 0
//   pool.min_hold_blocks           = min_hold_blocks
//   pool.delinquency_threshold     = delinquency_threshold
//   pool.challenge_response_window = challenge_response_window
//   // v2 fields zeroed:
//   pool.fee_balance               = 0
//   pool.max_user_share_bp         = 0
//   pool.last_month                = 0
//   registry.preservers            = []
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

    ia_id:          String,
    collection_cid: [u8; 32],
    metadata_hash:  [u8; 32],
    merkle_root:    [u8; 32],
    file_count:     u32,
    total_bytes:    u64,
    block_number:   u64,        // trust-based v1; replaced by ClockContext.block_id when spel#226 lands
) -> SpelResult
```

**Logic:**

```
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

    // M3 fix: init prevents the same voter voting twice on the same dispute
    #[account(init, pda = [literal("vote"), account("voter"), arg("ia_id")])]
    vote_receipt: AccountWithMetadata,

    #[account(signer)]
    voter: AccountWithMetadata,

    #[account(pda = [literal("stats"), account("voter")])]
    voter_stats: AccountWithMetadata,

    ia_id:          String,
    vote_canonical: bool,   // true = canonical correct, false = challenger correct
) -> SpelResult
```

---

### `claim_monthly_reward` *(v2 — inactive in v1)*

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
//   // M1 fix: use snapshot values so all claimants in the same month share a fixed budget
//   if pool.snapshot_month != month → return Err(SnapshotNotTaken)  // must call take_monthly_snapshot first
//   if pool.snapshot_total_active_bytes == 0 → return Err(NoActiveStorage)
//   // Finding 11 fix: use per-user snapshot, not live active_bytes
//   if stats.snapshot_claim_month != month → return Err(UserSnapshotNotTaken)  // must call snapshot_user first
//   pool_budget     = pool.snapshot_balance
//   reward_per_byte = pool_budget / pool.snapshot_total_active_bytes
//   raw_reward      = stats.snapshot_active_bytes as u128 × reward_per_byte
//   cap             = pool_budget × pool.max_user_share_bp / 10_000
//   payout          = min(raw_reward, cap)
//   user_stats.claimable_balance    += payout   // internal ledger; withdrawn via withdraw()
//   pool.fee_balance                -= payout
//   pool.snapshot_balance           -= payout   // track remaining snapshot budget
//   claim.active_bytes_snapshot      = stats.snapshot_active_bytes
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
`verify_holding` — proving the CID is reachable — or be set Delinquent and lose
`keeper_score` as the penalty. In v1 the mechanism is reputation-only; token staking
and spam fees are v2 features. ³

```rust
#[instruction]
pub fn challenge_holding(
    #[account(init, pda = [literal("challenge"), account("challenger"), account("preserver"), arg("ia_id")])]
    challenge: AccountWithMetadata,

    #[account(signer)]
    challenger: AccountWithMetadata,

    // the node whose holding is being challenged
    preserver: AccountWithMetadata,

    #[account(mut, pda = [literal("pres"), account("preserver"), arg("ia_id")])]
    record: AccountWithMetadata,   // must exist; mut required to persist open_challenge_count increment (H3 fix)

    #[account(mut, pda = [literal("pool")])]
    pool: AccountWithMetadata,

    ia_id:        String,
    block_number: u64,   // trust-based v1; replaced by ClockContext.block_id when spel#226 lands
) -> SpelResult
// Logic:
//   // v1: no financial stake; spam deterred by reputation loss (keeper_score) only
//   // spel#226 upgrade: if block_number - cooldown[challenger][preserver].last_block < pool.challenge_cooldown_blocks
//   //   → return Err(ChallengeCooldown)  // rate limit across all items for this pair
//   challenge.ia_id             = ia_id
//   challenge.preserver         = preserver.account_id
//   challenge.challenger        = challenger.account_id
//   challenge.challenge_block   = block_number
//   challenge.deadline_block    = block_number + pool.challenge_response_window
//   challenge.resolved          = false
//   challenge.preserver_cleared = false
//   record.open_challenge_count += 1    // H3 fix: deregister_item checks > 0
```

> **v1 spam deterrent:** challenger must have their own registered items (requires `create_user`
> and at least one `register_preservation`). Challenging with no skin in the game risks
> reputation loss if the challenge is frivolous.
>
> **Rate limiting (when `spel#226` lands):** add `challenge_cooldown_blocks: u64` to `RewardPool`
> and a `cooldown::{challenger}::{preserver}` PDA tracking the last challenge block per pair.

---

### `finalize_challenge`

Called by anyone after `challenge.deadline_block` has passed. Resolves the challenge
in favour of whichever party won: if the preserver successfully called `verify_holding`
after the challenge was opened, they are cleared; otherwise they are set Delinquent
and the challenger earns `keeper_score` as the reward. (v1: reputation-only)

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

    // challenger identity check (H1 fix) — must match challenge.challenger
    challenger: AccountWithMetadata,

    // challenger_stats: awards keeper_score on successful challenge (R3-M1 fix)
    #[account(mut, pda = [literal("stats"), account("challenger")])]
    challenger_stats: AccountWithMetadata,

    preserver:    AccountWithMetadata,
    ia_id:        String,
    block_number: u64,   // trust-based v1; replaced by ClockContext.block_id when spel#226 lands
) -> SpelResult
// Logic:
//   if challenge.resolved → return Err(AlreadyResolved)
//   if block_number < challenge.deadline_block → return Err(DeadlineNotReached)
//   // H1 fix: verify the payout account matches the recorded challenger
//   if challenger.account_id != challenge.challenger → return Err(Unauthorized)
//
//   if record.last_verified_block > challenge.challenge_block:
//     // preserver responded after challenge — cleared
//     challenge.preserver_cleared = true
//     // v1: no spam fee refund; challenger bears the gas cost of a wrong challenge
//
//   else:
//     // preserver did not respond — delinquent
//     record.verification_status = Delinquent
//     preserver_stats.active_bytes         -= record.total_bytes  (if was Active)
//     pool.total_active_bytes              -= record.total_bytes  (if was Active)
//     // v1: reputation bounty — challenger earns keeper_score for a successful catch (R3-M1 fix)
//     challenger_stats.keeper_score        += CHALLENGE_SUCCESS_SCORE
//     challenge.preserver_cleared = false
//
//   record.open_challenge_count -= 1    // H3 fix: allow deregister_item when all challenges resolved
//   challenge.resolved = true
```

---

### `fund_pool` *(v2 — inactive in v1)*

Open institutional entry point. Any party — Internet Archive, grant
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

### `snapshot_user` *(v2 — inactive in v1)*

Called once per month per user, after `take_monthly_snapshot`. Locks the user's
`active_bytes` into `UserStats.snapshot_active_bytes` for the current snapshot month.
`claim_monthly_reward` reads this value instead of live `active_bytes`, preventing
manipulation between pool snapshot and claim time. (Finding 11 fix)

```rust
#[instruction]
pub fn snapshot_user(
    #[account(mut, pda = [literal("stats"), account("user")])]
    stats: AccountWithMetadata,

    #[account(signer)]
    user: AccountWithMetadata,

    #[account(pda = [literal("pool")])]
    pool: AccountWithMetadata,

    month: u32,   // YYYYMM
) -> SpelResult
// Logic:
//   if pool.snapshot_month != month → return Err(SnapshotNotTaken)  // pool snapshot must precede user snapshot
//   if stats.snapshot_claim_month == month → return Err(AlreadySnapshotted)
//   stats.snapshot_active_bytes = stats.active_bytes
//   stats.snapshot_claim_month  = month
```

---

### `take_monthly_snapshot` *(v2 — inactive in v1)*

Called once per month before any `claim_monthly_reward` call. Freezes the pool budget
and total active bytes denominator for that month, so all claimants share a fixed
distribution regardless of transaction ordering. (M1 fix)

```rust
#[instruction]
pub fn take_monthly_snapshot(
    #[account(mut, pda = [literal("pool")])]
    pool: AccountWithMetadata,

    #[account(signer)]
    caller: AccountWithMetadata,    // permissionless — anyone can trigger

    month: u32,   // YYYYMM; when spel#226 lands: derive from ClockContext.timestamp, remove this arg
) -> SpelResult
// Logic:
//   if month <= pool.last_month → return Err(InvalidMonth)  // Finding 13: prevent re-snapshot of past month
//   if pool.snapshot_month == month → return Err(SnapshotAlreadyTaken)
//   pool.snapshot_balance            = pool.fee_balance
//   pool.snapshot_total_active_bytes = pool.total_active_bytes
//   pool.snapshot_month              = month
//   pool.last_month                  = month  // advance so next month must be strictly greater
```

---

### `withdraw`

Moves stable from `UserStats.claimable_balance` to the user's real wallet.
This is the single seam for the v1 internal ledger workaround; the only instruction
that needs `ChainedCall` to an external stable token program. (M4 fix)

```rust
#[instruction]
pub fn withdraw(
    #[account(mut, pda = [literal("stats"), account("user")])]
    stats: AccountWithMetadata,

    #[account(signer)]
    user: AccountWithMetadata,

    // destination: user's external stable token wallet; target of ChainedCall
    #[account(mut)]
    user_token_account: AccountWithMetadata,

    // stable token program — ChainedCall target (program ID TBD; see Stable Token Transfer section)
    token_program: AccountWithMetadata,

    amount: u128,
) -> SpelResult
// Logic:
//   if stats.claimable_balance < amount → return Err(InsufficientBalance)
//   ChainedCall to token_program: transfer amount stable from keeper program to user_token_account
//   stats.claimable_balance -= amount
```

---

### `create_item_bounty` *(v2 — inactive in v1)*

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

### `fund_item_bounty` *(v2 — inactive in v1)*

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

### `claim_item_bounty` *(v2 — inactive in v1)*

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

> **Race condition note:** Multiple preservers claiming the same bounty in the same month
> execute sequentially on-chain. Each claim decrements `bounty.balance` before the next
> runs, so no claimant can receive more than their pro-rata share of the balance remaining
> at the time their tx lands. Payout is therefore first-come-first-served within the
> month rather than a guaranteed fixed share — acceptable for v1.

---

### `deregister_user`

Allows a preserver to exit cleanly. Removes them from active reward calculations,
marks their stats as deregistered so
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
//   (caller issues one deregister_item per record before calling deregister_user)
```

> Note: `deregister_user` does not iterate all `PreservationRecord` PDAs (SPEL does not
> support iteration). Each item must be deregistered separately via `deregister_item`
> before calling `deregister_user`. `deregister_user` zeroes `active_bytes` in bulk at
> the stats level.

---

### `deregister_item`

Marks one registered item inactive and removes its bytes from the global pool total.
Called once per item when a preserver exits. `deregister_user` should be called first
to immediately zero `active_bytes` at the stats level.

```rust
#[instruction]
pub fn deregister_item(
    #[account(mut, pda = [literal("pres"), account("user"), arg("ia_id")])]
    record: AccountWithMetadata,

    #[account(mut, pda = [literal("item"), arg("ia_id")])]
    item: AccountWithMetadata,

    // M5 fix: stats is required to decrement active_bytes
    #[account(mut, pda = [literal("stats"), account("user")])]
    stats: AccountWithMetadata,

    // pool is required to decrement total_active_bytes (Finding 2 fix)
    #[account(mut, pda = [literal("pool")])]
    pool: AccountWithMetadata,

    #[account(signer)]
    user: AccountWithMetadata,

    ia_id: String,
) -> SpelResult
// Logic:
//   if record.open_challenge_count > 0:
//     return Err(ChallengePending)        // H3 fix: covers ALL challengers, not just one PDA key
//   if record.verification_status == Active:
//     stats.active_bytes          -= record.total_bytes   // *** must decrement or pool total drifts
//     pool.total_active_bytes     -= record.total_bytes
//     item.total_active_bytes     -= record.total_bytes
//   record.verification_status  = Delinquent          // item no longer maintained
```

> **Integrity note:** `deregister_item` must decrement `stats.active_bytes` and
> `pool.total_active_bytes` when the item is Active — otherwise the pool counter
> inflates and rewards are misdistributed. The `ChallengePending` guard prevents the
> challenge escape attack (deregister before `finalize_challenge` records the outcome).

---

### `migrate_score`

Transfers `keeper_score` and history metrics to a new account (key rotation or
wallet recovery). Both the old and new accounts must sign — the old to authorise the
migration, the new to accept. The new user must call `create_user` first to initialise
`new_stats` before calling `migrate_score`. ⁶

> **Scope:** `migrate_score` migrates reputation history only. `active_bytes` is NOT
> migrated — the new identity starts at zero and must re-register and re-verify each
> collection separately. SPEL cannot iterate PDAs, so atomically re-keying all
> `PreservationRecord` PDAs is impossible; migrating `active_bytes` without migrating
> the records would allow the new account to claim rewards for storage it does not hold
> under its own key. (R3-H1 fix)

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
//   // active_bytes is NOT migrated — new identity must re-register per item (R3-H1 fix)
//   old_stats.active_bytes          = 0         // H4 fix: prevent double reward claim under old key
//   old_stats.keeper_score          = 0         // old identity retired
//   old_stats.is_deregistered       = true
//   registry.preservers.push(new_user.account_id)
//   // old_user remains in registry for history; is_deregistered filters it from active leaderboard
```

> **Atomicity:** Both `old_user` and `new_user` must sign the same transaction — LEZ
> enforces this via `#[account(signer)]` on both. Either both sign and the migration
> executes atomically, or one refuses and the entire tx fails; there is no intermediate
> state.

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
- **Institutional top-ups** — `fund_pool` (v2): any party tops up the reward pool directly
  stable to the pool. More preservation activity = larger pool.
- **Institutional grants** — `fund_pool` accepts any stable transfer from any party.
  Internet Archive, universities, or grant programs can top up the pool directly
  at any time. No membership required — just a signed transaction.
- **Item bounties** — `fund_item_bounty` deposits accumulate in per-item `SponsorBounty`
  PDAs and are distributed as an additional top-up to preservers of that item.

**Cold start / bootstrap strategy:** ¹ Most DePIN protocols that failed died here.
A fresh pool has `reward_per_byte ≈ 0` — no preserver joins for zero reward.
The three v2 funding sources (stake-yield, external funding, challenge forfeitures) only flow
once there are preservers; the pool must be seeded externally before the first preserver joins. Mitigation:

| Phase | Mechanism |
|-------|-----------|
| Genesis | Project team or early sponsors call `fund_pool` with a committed minimum seed (recommend ≥ 6 months of estimated rewards at target participation) |
| Month 1–6 | Internet Archive (or Logos) calls `fund_pool` monthly on a public schedule — predictable reward for early preservers |
| Month 7+ | Stake-yield + challenge forfeitures begin to self-fund; external `fund_pool` role shrinks to emergency top-up |

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

> **v1 restriction (H2 fix):** `verify_holding` requires the preserver to be the signer.
> Permissionless community verification is a v2 feature — it requires runtime-injected block
> numbers to be safe. An unsigned verifier can submit any `block_number`, enabling a colluder
> to keep a preserver artificially Active (earning rewards without actually holding).
> v2 will add a separate `community_verify_holding` instruction once `logos-co/spel#226`
> (`ClockContext`) lands — the trust-based `block_number: u64` parameter is then replaced
> by the runtime-injected `ClockContext.block_id`.

```rust
#[instruction]
pub fn verify_holding(
    #[account(mut, pda = [literal("item"), arg("ia_id")])]
    item: AccountWithMetadata,

    // v1: preserver must be the signer — prevents attacker-supplied block_number manipulation
    #[account(signer)]
    preserver: AccountWithMetadata,

    #[account(mut, pda = [literal("pres"), account("preserver"), arg("ia_id")])]
    record: AccountWithMetadata,

    #[account(mut, pda = [literal("stats"), account("preserver")])]
    stats: AccountWithMetadata,

    #[account(mut, pda = [literal("pool")])]
    pool: AccountWithMetadata,

    ia_id:        String,
    block_number: u64,   // trust-based v1; replaced by ClockContext.block_id when spel#226 lands
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

> **v1 note:** In v1, `verify_holding` is preserver-signed only. Third-party peers can
> attempt a network retrieval to check reachability, but they cannot submit
> `verify_holding` on behalf of another preserver (doing so would reintroduce the H2
> trust gap). The community audit path in v1 is `challenge_holding`, not
> `verify_holding`. A separate `community_verify_holding` instruction will be added in
> v2 once `logos-co/spel#226` (`ClockContext`) lands. (R3-M2 fix)

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
| Reward pool (v2) | `fund_pool` — permissionless top-up; split-deposit model planned for v2 |
| Institutional pool funding | `fund_pool` — permissionless top-up; open to Internet Archive, grants, donors, universities |
| Monthly stable reward | `claim_monthly_reward` transfers stable pro-rata by active_bytes; hard cap per user |
| Holding accountability (v2) | Per-item `holding_deposit` escrowed; forfeited via `finalize_challenge` if preserver fails challenge — inactive in v1 |
| Third-party audit incentive | `challenge_holding` + `finalize_challenge` — challenger earns bounty for catching delinquent node |
| Demand-side sponsorship | `create_item_bounty` + `fund_item_bounty` + `claim_item_bounty` — sponsors target specific items; two-sided market |
| Clean exit | `deregister_user` + `deregister_item` — zeroes active_bytes, history preserved; v2: returns per-item deposits |
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

**Resolution applied (v1):** `confirm_reachable` collapsed into `verify_holding`. In v1
`verify_holding` is **preserver-signed only** (H2 fix — see verify_holding section).
Positive confirmation is submitted by the preserver itself. There was originally no path
for negative evidence.

**Extended resolution (market research gap D):** `challenge_holding` added as a distinct
instruction that handles the negative case. Any node can assert a CID is unreachable —
this creates a `ChallengeRecord` PDA with a deadline; if the preserver cannot respond with a
valid `verify_holding` before the deadline, `finalize_challenge` sets them Delinquent
and awards the challenger `keeper_score` as a reputation bounty (v1); token bounty from
holding deposit is a v2 feature. This gives third-party verifiers an incentive to run audits.

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

**Known limitation:** The registry only grows — `deregister_user` sets
`is_deregistered = true` but does not remove the entry. A `Vec<AccountId>` of 32-byte
entries can hold ~320,000 entries before hitting the LEZ account size limit (est. ~10 MB).
At that point `create_user` will fail. v2 migration path: split into paginated accounts
(`registry_0`, `registry_1`, …) or cap registration with `MaxPreserversReached` and
document the upgrade path. For v1 launch volumes this is not a concern.

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
| Proof CID is reachable on network | Verified via `downloadChunks(local=false)` — off-chain, any peer can check; preserver submits `verify_holding` (v1: preserver-signed); any peer can trigger `challenge_holding` |
| Proof node is *continuously* holding | Preserver can unpin and re-pin just before the deadline — `challenge_holding` creates economic pressure against this |
| Proof files are authentic IA content | Trust IA's sha1/md5 in metadata; `metadata_hash` anchors the claim at time of preservation |
| Sybil resistance | `challenge_holding` (active catching) + `holding_deposit` in v2 (economic downside) |
| `block_number` in instructions | Submitted by client, trust-based in v1. `logos-co/spel#226` (`ClockContext`) will eliminate this gap — see Upstream Research. |
| Full zk proof of holding | v1 is challenge-response (economic deterrent); v2 target is ZK proof of data possession (zk-SNARK PoDP, analogous to Filecoin PoDP launched May 2025) ¹ |

---

## Market Research Gaps — Status

Gaps identified by benchmarking against Filecoin, Arweave, Storj, and DePIN tokenomics research.
See Research Footnotes for sources.

| Gap | Severity | v1 Status | Resolution |
|-----|----------|-----------|------------|
| **A** — No cryptographic proof; verification self-reported | Critical | Partial | `challenge_holding` + `finalize_challenge` add economic deterrent; full ZK proof deferred to v2 |
| **B** — No collateral; zero downside for fake preservation | High | v1: partial (keeper_score at risk) | v2: split deposit — half to pool non-refundable, half escrowed and forfeited on challenge failure |
| **C** — Cold start / empty pool kills participation | High | Resolved | Bootstrap strategy documented; foundation seed + public funding schedule required pre-launch |
| **D** — Third-party verifier has no economic incentive | Medium | v1: partial | v1: challenger earns `keeper_score` on success; v2: token bounty from holding deposit |
| **E** — `PreserverRegistry` grows forever; ghost accounts | Medium | Resolved | `deregister_user` zeroes active_bytes, sets `is_deregistered`; leaderboard filters flag |
| **F** — Soulbound score lost on key compromise | Medium | Resolved | `migrate_score` — dual-signed migration; transfers score/history, not `active_bytes`; new identity re-registers per item |
| **G** — No demand side; sponsors cannot express preference | Medium | Resolved | `create_item_bounty` + `fund_item_bounty` + `claim_item_bounty`; per-item bounty claimable monthly |
| **H** — No deflationary flywheel | Low | By design | Closed-loop redistribution is an explicit non-goal; v2 pool funded by three external sources (stake-yield, external funding, challenge forfeitures) — documented in Dual Token Model |

---

## Privacy Analysis

### Why this matters

Keeper is explicitly designed to preserve collections that may be legally contested.
The $621M lawsuit against the Internet Archive is the founding motivation for the
protocol. A preserver who stores a potentially sueable collection must be able to
participate without their identity being publicly linked to that collection. If it is,
the censorship-resistance guarantee of the protocol is undermined: a rights-holder who
cannot take down the data can instead target the individual preservers by name.

In v1 every `PreservationRecord` PDA binds a public `AccountId` to a plaintext `ia_id`.
The `PreserverRegistry` lists all participants. The combination means any third party
can reconstruct a full map of "who is preserving what" for any collection of interest
without any special access.

### Options

| Option | What it hides | Tradeoff | Status |
|--------|--------------|----------|--------|
| **A — Do nothing** | Nothing | Full public exposure; legal targeting possible | v1 default |
| **B — Commit-reveal** | Collection-to-identity link (`ia_id` replaced by `sha256(ia_id \|\| nonce)`) | Passive enumeration blocked; active challenger who already knows ia_id via network monitoring can still challenge. `first_preserver` in `ItemRecord` still public. Cheapest to implement. | Proposed v1 option |
| **C — Private PDAs** | PDA existence not derivable by third parties even knowing inputs | Challenger must have independent network evidence before challenging — stronger model. Requires SPEL `private_pda` support (available). `active_bytes` on `UserStats` still visible. | v1.5 candidate |
| **D — Ephemeral keys** | Per-collection identity; main account never on-chain with ia_id | Key management complexity. Aggregating rewards across ephemeral keys requires ZK or trusted aggregator. | v2 candidate |
| **E — ZK proofs** | Everything: proves "I hold some item in the IA catalogue" without revealing which one or who | Requires: ZK circuit (set membership + nullifier), on-chain verifier primitive in LEZ runtime (not confirmed), client-side proving (~seconds per item), trusted setup if Groth16/PLONK. 3–6 months of ZK engineering. Full anonymity but high complexity. | v2 target |
| **F — Opt-in public mode** | Nothing (opt-in institutions stay fully public) | Dual code path in every collection instruction. Institutions wanting public credit keep current behaviour. | Compatible with B/C/D/E |

**Fundamental tension:** reward accounting requires the protocol to know `active_bytes`
per account to compute proportional payouts. Options B–D hide *which* collections
contribute to that count while still revealing the total byte volume. Option E (ZK) is
the only path to hiding both.

**Minimum recommended for v1:** Option B (commit-reveal). Two-week implementation,
no new runtime dependencies, breaks the primary attack vector (passive enumeration).
Option F (opt-in public mode) is additive and should ship alongside B.

---

### Exposure map

Every on-chain account in v1 links an `AccountId` to specific collections. This table
enumerates each exposure and its severity.

| Account / PDA | What is exposed | Severity |
|---------------|-----------------|----------|
| `PreservationRecord` | `preserver: AccountId` + `ia_id: String` stored together. PDA seed is `[literal("pres"), account("preserver"), arg("ia_id")]` — derivable by anyone who knows the account and tries ia_ids | **Critical** |
| `ItemRecord.first_preserver` | First preserver of a collection is permanently on-chain | High |
| `ChallengeRecord` | Names `challenger`, `preserver`, and `ia_id` in a single public PDA | High |
| `BountyClaim` PDA seed `bclaim::{user}::{ia_id}::{month}` | Reveals that a specific account claimed a bounty for a specific item | Medium |
| `vote::{voter}::{ia_id}` | Reveals which collections a voter participated in disputing | Medium |
| `fund_item_bounty` signer | Sponsor’s AccountId is a transaction signer — publicly links funder to ia_id | Medium |
| `PreserverRegistry` | Full public enumerable list of all participants | Low (public by design, but enables mapping) |

**Core threat:** an adversary enumerates `PreserverRegistry` to get all `AccountId`s, then
for any ia_id of interest derives `pda("pres", account_id, ia_id)` and checks if it
exists on-chain. No secret is needed. Full "who is preserving what" map is reconstructable
for any collection.

---

### Privacy path

#### Tier 1 — Short-term: commit-reveal for collection identity (v1 compatible)

The highest-impact change with the least structural disruption: store a **commitment**
in `PreservationRecord` instead of the ia_id plaintext.

```
collection_commitment: [u8; 32]  = sha256(ia_id || user_nonce)
```

The preserver generates a random `user_nonce` at registration time and keeps it private.
The ia_id itself is not stored on-chain. The preserver reveals `(ia_id, nonce)` when
needed — during a challenge response or reward claim — and the handler verifies the
preimage matches the stored commitment.

**What this protects:** passive enumeration of "which accounts preserve which collections"
is broken. An adversary cannot scan `(AccountId × ia_id)` pairs without knowing the nonce.

**What this does not protect:** a challenger who already knows the ia_id (from network
monitoring of the CID) can still construct a valid challenge.

**Impact on reward accounting:** none. `PreservationRecord.total_bytes` is still stored
plaintext — the protocol knows how much data a user holds without knowing which collection
it is.

**Impact on `ItemRecord.first_preserver`:** this field still names the first preserver
publicly. Options: (a) remove the field and replace the first-inscriber score bonus with
a block-timestamp bonus that does not name the account, or (b) accept it as a
permanent public record for the first participant only.

#### Tier 2 — Medium-term: private PDAs for collection records (SPEL supported)

SPEL supports `#[account(init, private_pda, pda = ..., npk = arg("user_npk"))]`. With
private PDAs, the PDA address is not derivable by third parties even if they know all
inputs — the NPK (nullifier public key) is user-controlled.

`PreservationRecord`, `BountyClaim`, and `ChallengeRecord` could all become private PDAs.
The preserver knows their own records; the general public cannot enumerate them.

**Implication for challenges:** the challenger must discover the `(preserver, collection)`
pair through off-chain network observation (watching CID reachability on Logos Storage),
not through chain enumeration. This is a better security model — challengers need
actual evidence, not just the ability to scan the chain.

#### Tier 3 — Long-term: ZK proofs (v2)

Prove "I hold some item in the IA catalogue" without revealing which one. This requires:
- ZK set-membership proof over IA catalogue identifiers
- Reward claims against a ZK identity, not a public `AccountId`
- Challenges proven via storage proof (ties directly into Q5 — Logos Storage team’s
  native proof API, if it exists)

---

### Opt-in public mode

Not every preserver wants privacy. Institutions — universities, libraries — may want
their preservation work to be publicly credited. Proposed: `create_user` takes a
`public_mode: bool` flag. Public-mode accounts behave exactly as v1 (ia_id plaintext,
listed in `PreserverRegistry`). Private-mode accounts use commit-reveal and are omitted
from the public registry, but still receive full reward participation.

---

### Fundamental tension

The reward mechanism requires the protocol to know `active_bytes` per account.
This is unavoidable: proportional rewards require a denominator. What can be hidden
is **which collections** contribute to that byte count. Commit-reveal achieves this.
Full anonymity (hiding the existence of a preservation relationship entirely) requires
private PDAs or ZK proofs.

---

### Open questions for LEZ / storage devs

6. **Private PDA performance** — What is the cost of `private_pda` vs standard PDA
   for `init` and `mut` operations? If high, commit-reveal is preferable for v1.
7. **Storage-level proof** — Does Logos Storage expose a reachability or possession
   proof that does not require naming the preserver? This would enable privacy-preserving
   challenges in Tier 3.

## Competitive Positioning

### Where Keeper shines

| Dimension | Keeper | Filecoin | Arweave | Storj |
|-----------|--------|----------|---------|-------|
| **Purpose** | Cultural preservation (Internet Archive) | General-purpose storage market | Permanent storage of any data | Enterprise/S3-compatible storage |
| **Reward currency** | Stable (no volatility risk) | FIL (volatile) | AR (volatile) | STORJ (volatile) |
| **Joining cost** | Free (v1: no deposit; v2: refundable deposit per item) | High collateral + hardware | None (read-only) | Node setup cost |
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

4. **Low barrier to entry** — v1 is fully free to join: no collateral, no upfront cost. v2 introduces a refundable per-item deposit for accountability. Filecoin requires significant upfront collateral. Keeper's v1 removes that barrier entirely.

5. **Stable reward eliminates participation boom/bust** — DePIN protocols with volatile token rewards see node counts swing with price. Keeper's stable reward makes ROI calculation for preservers simple and predictable — lowering the barrier for non-crypto-native institutions like libraries and universities.

---

## SPEL Framework Upstream Research

Findings from reading `logos-co/spel` v0.4.0 source at `~/basecamp/refs/spel`.

### Confirmed facts

| Topic | Finding |
|-------|---------|
| **Token transfer** | No native primitive. `SpelOutput.chained_calls: Vec<ChainedCall>` is the cross-program call mechanism. Fixture `transfer` instruction is a pure account data mutation. |
| **Multi-seed PDA** | `pda = [...]` **confirmed working** per vpavlin (2026-06-01). Workaround A (manual `compute_pda()`) is no longer needed. All multi-seed PDA macro syntax in this doc is correct and implementable as written. |
| **Seed hashing** | Multi-seeds will combine via `sha256(s1 \|\| s2 \|\| ... \|\| sN)`. Seed types: `literal` → UTF-8 zero-padded to 32 bytes; `account` → 32-byte AccountId; `arg` → serialised bytes zero-padded to 32. |
| **`#[account_type]` on enums** | Enum helper types (`VerificationStatus`, `ItemStatus`, `DisputeStatus`) must carry `#[account_type]` to appear in `SpelIdl::types` and be resolvable by the IDL BFS scanner. Fixed in this doc. |
| **SpelError variants** | `AccountAlreadyInitialized` (code 1002), `AccountNotInitialized` (1003), `InsufficientBalance` (1004), `Overflow` (1007), `Unauthorized` (1008), `PdaMismatch` (1009). Domain errors use `SpelError::custom(code, message)` (offset 6000). |
| **Block number** | NOT injected by runtime in v1 (confirmed vpavlin 2026-06-01). `block_validity_window` constrains tx validity range only. **In progress:** `logos-co/spel#226` proposes `ClockContext` (opt-in per-instruction parameter exposing `block_id` + `timestamp`). When it lands, all trust-based `block_number: u64` args are replaced by `ctx: ClockContext` with no change to instruction logic. |
| **Variable accounts** | `Vec<AccountWithMetadata>` works for rest-style variable-length account lists (confirmed via `batch_update` fixture). `PreserverRegistry` as `Vec<AccountId>` is valid. |
| **Private PDAs** | Supported via `#[account(init, private_pda, pda = ..., npk = arg("user_npk"))]`. Not used in keeper v1 but available. |
| **`ProgramContext`** | `ctx: ProgramContext` provides `self_program_id` and `caller_program_id`. Never part of instruction ABI or IDL. Useful for `#[account(owner = self_program_id)]` constraints. |

### Upstream blockers and workarounds

#### Blocker 1 — Multi-seed PDA — **RESOLVED** (2026-06-01)

`pda = [...]` array syntax is **confirmed working** per vpavlin. The SPEL macro supports
multi-seed PDAs as documented in `spel/docs/reference/macros.md`. All PDA macro syntax
in this doc is correct and can be implemented as written. **Workaround A is no longer needed.**

#### Blocker 2 — ChainedCall stable transfer — **PARTIALLY RESOLVED** (2026-06-01)

**Mechanism confirmed:** `ChainedCall` is the correct path for stable token transfers (confirmed by vpavlin).
A general token program is deployed on the network. **Program ID is TBD** — awaiting answer
from r4bbit on the stable token program ID on testnet.

**Workaround (still active until program ID confirmed):** Internal ledger model.

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

| Phase | Instructions | Notes |
|-------|--------------|-------|
| **1 — Singletons (v1)** | `initialize_program` | No blockers |
| **2 — Core flow (v1)** | `create_user`, `register_preservation`, `verify_holding`, `deregister_item`, `deregister_user`, `withdraw` | Internal ledger workaround (Workaround B); multi-seed PDA works natively |
| **3 — Channels (v1)** | `create_channel`, `add_channel_entry`, `verify_channel`, `subscribe_channel` | Multi-seed PDA works natively |
| **4 — Audit (v1)** | `challenge_holding`, `finalize_challenge` | Internal ledger workaround |
| **5 — Governance (v1)** | `open_dispute`, `vote_on_dispute`, `migrate_score` | No blockers |
| **6 — Rewards (v2)** | `fund_pool`, `snapshot_user`, `take_monthly_snapshot`, `claim_monthly_reward`, `create_item_bounty`, `fund_item_bounty`, `claim_item_bounty` | Activate when v2 reward model launches |
| **Upgrade B** | Replace `withdraw` internal ledger with direct `ChainedCall` transfers; remove `withdraw` and `claimable_balance` | When stable token program ID confirmed (r4bbit) |


Phase 1 (`initialize_program`) can start immediately. Phases 2–5 require the internal ledger workaround (Workaround B) until the stable token API is confirmed. Phase 6 (v2 rewards) activates when the v2 reward model launches.

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
