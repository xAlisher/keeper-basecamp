# Keeper Protocol — Whitepaper

*Permanent preservation of the world's knowledge, backed by verifiable incentives.*

---

## TLDR

The Internet Archive is under threat. Centralized archives can be sued, seized, or shut down. Keeper Protocol makes permanent preservation unstoppable by putting it on a blockchain: anyone who downloads and holds an Internet Archive collection earns stable rewards for as long as they keep holding it. The more people preserve, the more resilient the archive becomes. No single entity controls it. No lawsuit can take it down.

---

## The Problem: Cultural Memory Is Fragile

In 2023, major record labels filed a $621 million lawsuit against the Internet Archive over its Great 78 Project — a community effort to digitize some of the earliest recorded music in history, recordings made between 1898 and the 1950s that exist nowhere else.

The Logos network stood with the Internet Archive:

> "While centralised projects like the Internet Archive remain vulnerable to external forces seeking to destroy or manipulate them, Logos is building [Logos Storage] as an open-source data storage protocol to allow any individual or community to durably preserve and decentralise the cultural artefacts that define their legacy." ¹

The lawsuit settled. But the vulnerability it exposed remains. A single institution, no matter how mission-driven, is a single point of failure. Courts, governments, and copyright holders can threaten one organization. They cannot threaten ten thousand independent nodes holding copies of the same files.

This is not a hypothetical risk. The average lifespan of a web page is 100 days. Libraries burn. Servers go offline. Companies close. The archive of human civilization is stored on infrastructure that was never designed to last.

Keeper Protocol is designed to last.

---

## The Logos Position

Logos was founded on the principle that sovereignty over information must be built into the infrastructure, not granted by institutions:

> "Logos wholeheartedly supports the Internet Archive and similar efforts to prevent the eradication and distortion of our collective memory." ¹

The Logos tech stack — Logos Messaging, Logos Storage, Logos Blockchain — exists precisely because censorship-resistant infrastructure cannot be built on top of systems that can be censored. Keeper Protocol is the economic layer that makes decentralized preservation self-sustaining rather than dependent on donations and goodwill.

---

## How Keeper Works

### The core idea

Every Internet Archive collection has an identifier — a stable, permanent ID like `popeye_taxi-turvey` or `great-78-project`. Keeper treats these identifiers as first-class on-chain objects. When a preserver downloads a collection and uploads it to Logos Storage, they register that fact on-chain. The chain records who preserved what, when, and how much data they are actively holding.

At the end of each month, the protocol distributes rewards from a shared pool to everyone who kept their data active. The more data you hold, the more you earn. Stop holding it, and you stop earning.

### The three actors

**Preservers** are the nodes of the network. They download Internet Archive collections, store them on Logos Storage, and register their holdings on-chain. In return, they earn a monthly share of the reward pool proportional to how much data they are actively holding. Joining is free — the only cost is a refundable deposit per collection, returned when they exit cleanly.

**Sponsors** are institutions, individuals, or foundations who believe specific collections matter. The Internet Archive, a university library, a museum, a community — anyone can fund a bounty for a specific collection. Preservers of that collection earn from the bounty in addition to the monthly pool. Sponsorship creates a direct economic signal: *this collection is worth preserving*.

**The pool** is funded by two sources: registration fees paid by preservers when they register a new collection, and institutional top-ups from organizations like the Internet Archive, universities, or individual donors. The pool belongs to no one. It is a public resource that flows to whoever is doing the work.

### Trust without permission

A preserver's claim to be holding a collection is not taken on faith. Any third party can challenge it: submit an on-chain assertion that a collection's CID is unreachable on the Logos Storage network. The preserver then has a fixed window to prove otherwise. If they can — the challenge fails and the challenger loses their anti-spam fee. If they cannot — the preserver is marked delinquent, removed from the reward pool, and their deposit goes to the challenger as a bounty.

This creates a market for auditing. It pays to catch cheaters. Over time, this economic pressure produces the same effect as a cryptographic proof — not because everyone is watching all the time, but because anyone who tries to collect rewards without actually holding data risks losing more than they gain.

### A record that cannot be taken from you

Every preservation act is recorded permanently on-chain as `keeper_score` — a soulbound metric that accumulates forever and cannot be transferred, bought, or sold. It is a permanent record of contribution, like a publication record or a service medal. When a library or university participates, their contribution to the global archive is written into the chain's history, independent of any institution's continued existence.

---

## Why This Works Where Others Have Failed

Decentralized storage protocols have existed for years. Filecoin, Arweave, Storj — all functional, all growing. None of them are focused on cultural preservation. They are storage markets or investment vehicles. Keeper is neither.

**The reward is stable.** Protocols that pay preservers in their own token create a feedback loop: when token price drops, preservers leave; when preservers leave, service degrades; when service degrades, the token drops further. Keeper pays in stable currency — the same money preservers paid in. This is redistribution of fees, not speculation. A library or university can calculate their expected return with the same certainty as a utility bill.

**The mission is specific.** Keeper is not a general-purpose storage market. Every collection in the system is an Internet Archive collection with a stable identifier, verified metadata, and a content hash. This specificity is a feature: preservers know exactly what they are preserving, sponsors know exactly what they are funding, and auditors know exactly what to check.

**The identity layer is permanent.** `keeper_score` cannot be stripped by a platform ban, a company acquisition, or a change in terms of service. A preserver who contributes for ten years carries that record in the chain's history regardless of what happens to any institution or service.

**Institutions can participate directly.** The `fund_pool` instruction is permissionless — the Internet Archive, a university endowment, or any individual can top up the reward pool at any time. The `fund_item_bounty` instruction lets them express preference: *this specific collection matters to us*. This is a direct economic relationship between those who value preservation and those who do the preserving, with no intermediary taking a cut.

---

## The Bigger Picture

The Logos network is building infrastructure for communities that need to exist independent of any state or corporation. Information preservation is not separate from that mission — it is the foundation of it.

A movement that cannot preserve its own history is fragile. A civilization whose records exist only on centralized servers is one court order away from losing its past. Keeper Protocol is one piece of the answer: an economic system that makes it profitable to hold the archive, making the archive itself harder to destroy with every node that joins.

The Internet Archive's motto is *universal access to all knowledge*. Keeper Protocol's contribution is to make that access structurally permanent — not dependent on any single organization's survival, but distributed across thousands of independent nodes, each earning for the work they do.

---

## For Institutions

Universities, libraries, museums, and cultural organizations can participate in three ways:

1. **Preserve** — run a Logos Storage node, download and register collections, earn monthly rewards proportional to holdings.
2. **Sponsor** — fund a bounty for collections your institution cares about, creating a persistent economic signal that attracts and retains preservers for those specific items.
3. **Top up the pool** — make a one-time or recurring contribution to the global reward pool, supporting all preservation work across all collections.

All three paths are on-chain, transparent, and auditable. There is no application process, no approval committee, and no single organization that can revoke access.

---

## Status

Keeper Protocol is in design phase. The LEZ on-chain program is being specified against the SPEL smart contract framework on the Logos Execution Zone. The whitepaper reflects the current design; the full technical specification is in the design document:

→ [Keeper Protocol — LEZ Program Design](keeper-protocol-lez.md)

The Logos testnet is expected in 2026. Keeper Protocol targets deployment on testnet as an early demonstration of real-world incentivized preservation at the LEZ application layer.

---

## References

¹ Logos Press Engine, *Save the Songs: Decentralise the Internet Archive's Music Collection*, 2025.
  https://press.logos.co/article/save-the-songs

² Logos Press Engine, *State of the Logos Network: May 2025*, 2025.
  https://press.logos.co/article/may-2025

³ Internet Archive, *The Great 78 Project — Community Preservation, Research, Discovery of 78rpm Records*.
  https://great78.archive.org/

⁴ Consequence, *Internet Archive Settles $621 Million Lawsuit with Major Labels Over Vinyl Preservation Project*, September 2025.
  https://consequence.net/2025/09/internet-archive-labels-settle-copyright-lawsuit/

⁵ Logos Press Engine, *State of the Logos Network: October 2025*, 2025.
  https://press.logos.co/article/october-2025
