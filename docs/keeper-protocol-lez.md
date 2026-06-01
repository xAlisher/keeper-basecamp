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
      → ItemRecord PDA (first-write atomic — first preserver wins)
      → PreservationRecord PDA (per-user per-item)
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
}

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
    pub matches_canonical: bool,
    pub block:             u64,
    pub file_count:        u32,
    pub total_bytes:       u64,
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
}
```

### `RewardPool`
Singleton. Accumulates registration fees and tracks the network-wide active bytes
counter. Updated by `register_preservation` (fees in) and `verify_holding` (bytes counter).

```rust
#[account_type]
pub struct RewardPool {
    pub fee_balance:        u128,   // stable accumulated from registration fees
    pub total_active_bytes: u128,   // sum of active_bytes across all preservers
    pub registration_fee:   u128,   // stable charged per register_preservation call
    pub max_user_share_bp:  u32,    // basis points cap per user, e.g. 2000 = 20%
    pub min_hold_blocks:    u64,    // blocks an item must be held before Active
    pub last_month:         u32,    // last YYYYMM distributed
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

---

## PDA Layout

| Account | Seeds | One per |
|---------|-------|---------|
| `item::{ia_id}` | `[literal("item"), arg("ia_id")]` | IA identifier |
| `pres::{preserver}::{ia_id}` | `[literal("pres"), account("preserver"), arg("ia_id")]` | (user, item) pair |
| `dispute::{ia_id}` | `[literal("dispute"), arg("ia_id")]` | disputed item |
| `stats::{user}` | `[literal("stats"), account("user")]` | user |
| `claim::{user}::{month}` | `[literal("claim"), account("user"), arg("month")]` | (user, month) pair |
| `pool` | `[literal("pool")]` | singleton |
| `registry` | `[literal("registry")]` | singleton |

---

## Instructions

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
transfer pool.registration_fee stable from preserver to pool
pool.fee_balance += pool.registration_fee

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
    //   if gap > DELINQUENCY_THRESHOLD → Delinquent
    //   else if block_number - item.block < pool.min_hold_blocks → Pending  // new — under minimum hold
    //   else → Active
    //
    // if prev_status != Active AND new_status == Active:
    //   stats.active_bytes          += record.total_bytes
    //   pool.total_active_bytes     += record.total_bytes
    //
    // if prev_status == Active AND new_status != Active:
    //   stats.active_bytes          -= record.total_bytes
    //   pool.total_active_bytes     -= record.total_bytes
    //
    // record.verification_status = new_status
}
```

Add to `PreservationRecord`:

```rust
pub last_verified_block: u64,
pub verification_status: VerificationStatus,

pub enum VerificationStatus { Pending, Active, Delinquent }
// Pending: item registered but min_hold_blocks not yet elapsed — no reward contribution
// Active:  holding confirmed within DELINQUENCY_THRESHOLD — contributes to active_bytes
// Delinquent: gap exceeded — removed from active_bytes until re-verified
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
| Fee-funded reward pool | `register_preservation` charges `registration_fee` stable per call → `RewardPool.fee_balance` |
| Institutional pool funding | `fund_pool` — permissionless top-up; open to Logos Foundation, Internet Archive, grants, donors |
| Monthly stable reward | `claim_monthly_reward` transfers stable pro-rata by active_bytes; hard cap per user |
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

**Resolution applied:** collapsed into `verify_holding`. The instruction now accepts
any signer (`verifier`) separate from the `preserver` account being verified. Self-
and community-verification are the same instruction — no separate `confirm_reachable`
or `challenge_holding` needed. If a community peer's fetch fails they simply do not
submit; non-submission naturally accumulates toward the delinquency threshold.

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
| Proof CID is reachable on network | Verified via `downloadChunks(local=false)` — off-chain, any peer can check and submit `verify_holding` |
| Proof node is *continuously* holding | Preserver can unpin and re-pin just before the deadline and pass the check |
| Proof files are authentic IA content | Trust IA's sha1/md5 in metadata; `metadata_hash` anchors the claim at time of preservation |
| Sybil resistance | Nothing prevents fake registrations with made-up hashes — mitigated by dispute; slash deferred to v2 |
| `block_number` in instructions | Submitted by client, not injected by runtime — trust-based until LEZ exposes it natively |
