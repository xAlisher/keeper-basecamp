# Retro Log

## win 2026-05-26
keeper crash fixed — pre-init IPC clients before network activity avoids bad_alloc from QRemoteObjectNode in post-download Qt RO socket state

## win 2026-05-26
keeper full chain confirmed — download→stash→beacon→txHash→UI with Clear+copy buttons, clipboard fix (opacity:0 not visible:false), QML deploy path confirmed (plugins/ not modules/)

## win 2026-05-26
Qt6HttpServer nix build unblocked — qt6.qthttpserver in nix.packages.runtime + QTcpServer::listen+bind + QHttpHeaders API fixes

## win 2026-05-26
Log panel redesigned — Stash → Logos Storage + Beacon → Logos Blockchain two-line format with selectable CID; file status bug (uploaded→done) and startup load bug fixed

## fail 2026-05-26
stash attribution overstated as blocker for keeper — keeper calls beacon directly with source="keeper"; stash fix only relevant for modules relying on stash auto-inscription path

## win 2026-06-07
automated tests shipped — Tier 1 headless unit tests (run-headless-tests.sh, logoscore single-invocation mode, 13 assertions) + Tier 2 integration pipeline (run-integration-tests.sh, HTTP bridge polling, VERIFY_BLOCKCHAIN=1 mode); PR #23 open

## win 2026-06-07
ghost-tx root cause identified — zone_sequencer_rs.so built against commit 6b42706 (2026-03-30), which is 9 days before PR #2481 "Fix fr from bytes condition" merged 2026-04-08; Poseidon2 Fr::from_bytes() bug produces wrong inscription_id → permanent 404 on explorer even when tx is on-chain; fix is rebuild against v0.1.2 tag (released 2026-04-13, includes the fix)

## win 2026-06-07
canonical chain verification method — compare local node LIB hash via `curl http://localhost:8080/cryptarchia/info` against `https://testnet.blockchain.logos.co/web/explorer/api/v1/blocks/{LIB_HASH}`; if explorer returns the block with fork=3173, node is on canonical chain; sneg node confirmed on canonical chain after DB wipes

## fail 2026-06-07
chased minority fork for multiple sessions — sneg node was actually on the canonical chain (fork 3173) all along after DB wipes; wasted investigation time on bootstrap IP research and devnet vs testnet distinction; correct diagnosis required comparing LIB hashes directly

## fail 2026-06-07
agent concluded wrong bootstrap IP — explore agent found .env.devnet uses 65.108.203.235 (v0.1.3-rc) and concluded canonical testnet moved there; was wrong — .env.testnet still uses 65.109.51.37 (v0.1.2) and that IS the canonical testnet; user confirmed no team comms about testnet upgrade to rc; verify IP conclusions by comparing actual LIB hashes not just config files

## win 2026-06-07
explorer HTTP 500 decoded — explorer frontend shows "Error: HTTP 500" for any not-found tx, but actual API returns 404; this is a frontend display bug, not a server error; confused initial diagnosis

## win 2026-06-07
headless tests 17/17 pass — keeper-basecamp Tier 1 tests all green after fr_from_bytes rebuild install

## win 2026-06-07
8 working explorer links recovered — scanned canonical blocks (fork=3199) for all keeper channel inscriptions, cross-referenced with explorer API to get correct explorer hashes (different from node mantle_tx.hash); beacon inscription-log.json updated with correct inscriptionIds

## fail 2026-06-07
zone_publish arg order inverted — Python ctypes call passed checkpoint_path as data and data_path as checkpoint; resulting inscription had checkpoint path as content instead of ia:kuMUquaeE6g JSON; tx confirmed on-chain but with wrong content

## fail 2026-06-07
fr_from_bytes fix does NOT fix explorer link mismatch — inscription_id from zone_publish (TxHash using lb-poseidon2) is always different from explorer hash (uses different computation); node API returns whatever hash was submitted, explorer independently recomputes; all 3 txs in block 4701195 show consistent mismatch between node mantle_tx.hash and explorer hash; the fix was correct for boundary condition but doesn't fix the hash algorithm difference

## win 2026-06-07
universal hash mismatch identified — mantle_tx.hash (node API) ≠ explorer hash for ALL transactions, not just zone inscriptions; this is a fundamental protocol difference: zone_sequencer_rs computes one hash, explorer independently computes another; both old and new zone_sequencer_rs match each other and the node API, but neither matches the explorer

## win 2026-06-07
hash mismatch was false alarm — zone-scanner source confirms tx_id = mantle_tx.hash = inscription_id = explorer hash; previous mismatch was comparing forked-chain tx vs canonical-chain tx at same slot (completely different txs)

## fail 2026-06-07
bootstrap peers stale — node.yaml had old peer IDs (12D3KooWFrou...) that answered with wrong protocol (0.1.3-rc.6 chainsync unsupported); logos-live API peer IDs also stale; canonical peers found in logos-crawler peers.json (152.53.103.89, 160.30.20.30, 189.28.190.192, 158.220.92.29)

## win 2026-06-07
canonical peers deployed — node.yaml updated with peers from logos-crawler; node now discovering 30+ canonical peers via Kademlia; prolonged_bootstrap_period=300s in progress

## fail 2026-06-07
explorer frontend shows HTTP 500 for valid inscription — API returns HTTP 200 with correct data but frontend JS crashes because block_id=null in /transactions/{hash} endpoint response; explorer tries to render block link, gets null, throws; upstream explorer bug; inscription is verifiable via raw API; not a problem with our data

## win 2026-06-07
keeper inscription on canonical chain confirmed — ia:kuMUquaeE6g inscribed to testnet with correct content; working explorer URL: https://testnet.blockchain.logos.co/web/explorer/transactions/67039dd5ab15838f49f09ca63b7df7a14d36762bf201044ead100ccdd6059688 (block df976320 slot 4709374 fork=3210); root fixes: (1) replaced libzone_sequencer_rs.so with nix store 0.1.2 build (logos-blockchain 05f84a5, post-PR#2481); (2) use zone_sequencer_create+publish API not zone_publish; (3) pass actual JSON bytes not file path as data arg; (4) use no-checkpoint mode for one-shot scripts to avoid stale-checkpoint backfill deadlock; (5) sleep 30s after publish before destroy to allow async tx submission

## fail 2026-06-07
zone_sequencer_rs inscription_id ≠ explorer hash — zone_sequencer_publish returns TxHash f33515bfdd... (Poseidon2 computation) but explorer records same tx as 67039dd5... (node's mantle_tx.hash); hash mismatch persists even with 0.1.2 library; to get working explorer URL must scan block after submission and extract mantle_tx.hash; cannot use zone_sequencer_publish return value directly as explorer URL

## fail 2026-06-07
zone_publish data arg is content not file path — keeper_inscribe_v2.py passed data_path.encode() (a file path string) as the data arg; library published the path string as inscription content; correct call: pass inscription JSON bytes directly as data arg, not a file path

## fail 2026-06-07
zone_sequencer stale checkpoint backfill deadlock — if checkpoint has old lib_slot, sequencer backfills all finalized blocks before submitting; with checkpoint from 5 minutes ago, backfill takes 60s+ and process exits before submission; fix: use no-checkpoint mode (pass empty string) for one-shot scripts so create bootstraps to current LIB

## fail 2026-06-07
node crash-loop after jun7 db wipe — recovery/consensus had lib_height=241580 with empty block DB, causing panic on startup. Fixed by clearing recovery/consensus, node restarted and IBD resumed from genesis with canonical peers. Node syncing fast (~238K slots/min) from 0 → 4.7M. zone_publish times out during Bootstrapping mode, local monitor waiting for Online.

## win 2026-06-07
working explorer URL confirmed via playwright — https://testnet.blockchain.logos.co/web/explorer/transactions/67039dd5ab15838f49f09ca63b7df7a14d36762bf201044ead100ccdd6059688 — ChannelInscribe ia:kuMUquaeE6g renders correctly at fork 3214; earlier "Error: HTTP 500" was transient frontend glitch during fork 3210→3214 transition, not permanent; TX was on-chain and correctly indexed

## win 2026-06-07
chain reorg explanation confirmed — Cryptarchia PoS testnet reorgs above LIB are normal; explorer fork ID increments on every re-index; blocks below lib_slot are permanent; we had 5 fixable code bugs (wrong .so, file path as data, stale checkpoint, no sleep before destroy, wrong hash from publish return value) — all now fixed

## win 2026-06-07
CONFIRMED WORKING inscription method — zone_sequencer_create(node, channel, key, b"") + zone_sequencer_publish(handle, raw_json_bytes) + sleep(35) + zone_sequencer_destroy(handle) → scan /cryptarchia/blocks for mantle_tx.hash (≠ publish return value) → explorer URL: https://testnet.blockchain.logos.co/web/explorer/transactions/{mantle_tx.hash}; playwright-verified rendering at fork 3214; inscription ia:kuMUquaeE6g live at https://testnet.blockchain.logos.co/web/explorer/transactions/67039dd5ab15838f49f09ca63b7df7a14d36762bf201044ead100ccdd6059688

## fail 2026-06-07
keeper module not loading after lgpm install — logos-module-builder generates linux-amd64-dev variant but platform expects linux-amd64; fix: add linux-amd64 alias to installed manifest.json and update variant file to linux-amd64
