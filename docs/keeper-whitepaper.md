# Keeper Protocol — Whitepaper

*Permanent preservation of the world's knowledge, verifiable on-chain.*

---

## TLDR

The Internet Archive is under threat. Centralized archives can be sued, seized, or shut down. Keeper Protocol makes permanent preservation unstoppable by putting it on a blockchain: anyone who downloads and holds an Internet Archive collection registers that fact on-chain, earns a permanent reputation score for it, and makes the archive harder to destroy. The more nodes hold a collection, the more resilient it becomes. No single entity controls it. No single lawsuit can take it all down.

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

The Logos tech stack — Logos Messaging, Logos Storage, Logos Blockchain — exists precisely because censorship-resistant infrastructure cannot be built on top of systems that can be censored. Keeper Protocol is the coordination layer that makes decentralized preservation self-sustaining rather than dependent on a single organization's continued existence.

---

## How Keeper Works

### The core idea

Every Internet Archive collection has an identifier — a stable, permanent ID like `popeye_taxi-turvey` or `great-78-project`. Keeper treats these identifiers as first-class on-chain objects. When a preserver downloads a collection and uploads it to Logos Storage, they register that fact on-chain. The chain records who preserved what, when, and how much data they are actively holding.

That registration is the core act. It is public, permanent, and verifiable — independent of any institution's continued existence.

### The three actors

**Preservers** are the nodes of the network. They download Internet Archive collections, store them on Logos Storage, and register their holdings on-chain. Every act of preservation earns `keeper_score` — a permanent, soulbound on-chain record of contribution that cannot be transferred, bought, or revoked. Joining is free.

**Sponsors** are institutions, individuals, or foundations who believe specific collections matter. The Internet Archive, a university library, a museum, a community — anyone can fund a bounty for a specific collection, creating a direct economic signal: *this collection is worth preserving*.

**The ledger** belongs to no one. Every preservation registration, every challenge, every score is on-chain and auditable. No intermediary controls access. No platform ban can erase the record.

### Trust without permission

A preserver's claim to be holding a collection is not taken on faith. Any third party can challenge it: submit an on-chain assertion that a collection's CID is unreachable on the Logos Storage network. The preserver then has a fixed window to prove otherwise. If they respond — the challenge fails. If they cannot — the preserver is marked delinquent and loses standing in the network.

This is not a financial system. It is an accountability system. The protocol does not require everyone to be watched all the time — it requires that the cost of being caught faking is high enough that faking is not worth it.

### A record that cannot be taken from you

Every preservation act accumulates permanently as `keeper_score` — a metric that grows with time and cannot be reset by a platform ban, a company acquisition, or a change in terms of service. A preserver who contributes for ten years carries that record in the chain's history regardless of what happens to any institution or service. When a library or university participates, their contribution to the global archive is written permanently into the chain.

---

## Why This Works Where Others Have Failed

Decentralized storage protocols have existed for years. Filecoin, Arweave, Storj — all functional, all growing. None of them are focused on cultural preservation. They are storage markets or investment vehicles. Keeper is neither.

**The mission is specific.** Keeper is not a general-purpose storage market. Every collection in the system is an Internet Archive collection with a stable identifier, verified metadata, and a content hash. This specificity is a feature: preservers know exactly what they are preserving, sponsors know exactly what they are funding, and auditors know exactly what to check.

**The identity layer is permanent.** `keeper_score` is the first on-chain reputation primitive built specifically for preservation work. It compounds over time. It cannot be bought. A library that preserves ten thousand collections has a record that speaks for itself — permanently, on-chain, without any central authority to appeal to.

**The economic layer is stable when it activates.** Protocols that pay in their own token create feedback loops: price drops, preservers leave, service degrades, price drops further. Keeper's reward layer — when activated — pays in stable, sourced from outside the system: deposit yield, institutional contributions, and challenge forfeitures. No speculative token. No closed loop where rewards can never exceed total stake.

**Institutions can participate without asking permission.** There is no application process. A university, a library, or the Internet Archive itself can start preserving, sponsoring specific collections, or contributing to the reward pool with a single signed transaction. No intermediary takes a cut.

---

## The Bigger Picture

The Logos network is building infrastructure for communities that need to exist independent of any state or corporation. Information preservation is not separate from that mission — it is the foundation of it.

A movement that cannot preserve its own history is fragile. A civilization whose records exist only on centralized servers is one court order away from losing its past. Keeper Protocol is one piece of the answer: a coordination system that makes preservation persistent — not dependent on any single organization's survival, but distributed across thousands of independent nodes, each with a permanent stake in the archive's survival.

The Internet Archive's motto is *universal access to all knowledge*. Keeper Protocol's contribution is to make that access structurally more resilient — written into the chain's history by every node that joins.

---

## For Institutions

Universities, libraries, museums, and cultural organizations can participate in three ways:

1. **Preserve** — run a Logos Storage node, download and register collections, build an on-chain preservation record that accumulates permanently as `keeper_score`.
2. **Sponsor** — fund a bounty for collections your institution cares about, creating a persistent on-chain signal that attracts and retains preservers for those specific items.
3. **Contribute to the pool** — make a one-time or recurring contribution to the global reward pool, supporting all preservation work across all collections.

All three paths are on-chain, transparent, and auditable. There is no application process, no approval committee, and no single organization that can revoke access.

---

## Status

Keeper Protocol is in design phase. The LEZ on-chain program is being specified against the SPEL smart contract framework on the Logos Execution Zone. The whitepaper reflects the current design; the full technical specification is in the design document:

→ [Keeper Protocol — LEZ Program Design](keeper-protocol-lez.md)

The Logos testnet is expected in 2026. Keeper Protocol targets deployment on testnet as an early demonstration of verifiable, on-chain preservation at the LEZ application layer.

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
