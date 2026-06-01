#!/usr/bin/env node
/**
 * debug-all.mjs — End-to-end scenario runner for the keeper preserve pipeline.
 *
 * Before running:
 *   1. npm install (in scripts/)
 *   2. node debug-all.mjs --gen-keypair   # first run: generate keypair + print pubkey
 *   3. Paste pubkey into keeper UI → Pair Extension
 *   4. node debug-all.mjs                 # run all scenarios
 */

import { connect, disconnect, PRESERVE_TOPIC, STATUS_TOPIC } from "./lib/waku.mjs";
import { loadOrGenKeypair } from "./lib/crypto.mjs";
import { buildMessage } from "./lib/message.mjs";

const IDENTIFIER = "test-keeper-debug-" + Date.now();
const URL        = `https://archive.org/details/${IDENTIFIER}`;
const TIMEOUT_MS = 8_000;

const SCENARIOS = [
  {
    name:     "happy-path",
    desc:     "valid signed message — expect keeper to queue and reply with status:queued",
    opts:     {},
    expectStatus: "queued",
  },
  {
    name:     "unknown-pubkey",
    desc:     "pubkey not in paired list — expect no reply (silent reject)",
    opts:     { unknownPubkey: true },
    expectStatus: null,
  },
  {
    name:     "stale-timestamp",
    desc:     "timestamp 5 min in past — expect no reply (freshness reject)",
    opts:     { staleTimestamp: true },
    expectStatus: null,
  },
  {
    name:     "bad-signature",
    desc:     "corrupted sig — expect no reply (sig verification reject)",
    opts:     { badSig: true },
    expectStatus: null,
  },
  {
    name:     "replay",
    desc:     "same message sent twice — second send expects no reply (replay guard)",
    opts:     {},
    expectStatus: null,   // second send — first was the happy-path above
    replayOf: "happy-path",
  },
];

async function waitForStatus(node, statusDecoder, identifier, timeoutMs) {
  return new Promise((resolve) => {
    let unsub;
    const timer = setTimeout(() => { unsub?.(); resolve(null); }, timeoutMs);
    node.filter.subscribe([statusDecoder], (msg) => {
      if (!msg.payload) return;
      try {
        const p = JSON.parse(new TextDecoder().decode(msg.payload));
        if (p.identifier === identifier) {
          clearTimeout(timer);
          unsub?.();
          resolve(p.status);
        }
      } catch {}
    }).then(u => { unsub = u; });
  });
}

async function run() {
  const kp = await loadOrGenKeypair();
  console.log("\nTest pubkey (pair this in keeper UI before running):");
  console.log(" ", kp.pubkeyHex, "\n");

  console.log("Connecting to Logos Messaging...");
  const { node, preserveEncoder, statusDecoder } = await connect();
  console.log("Connected to Logos Messaging.\n");

  const results = [];
  const replayMessages = {};

  for (const scenario of SCENARIOS) {
    process.stdout.write(`[${scenario.name}] ${scenario.desc}\n  → sending... `);

    // Build message (replay uses the stored message from the original scenario)
    let msg;
    if (scenario.replayOf && replayMessages[scenario.replayOf]) {
      msg = replayMessages[scenario.replayOf];
    } else {
      const id = IDENTIFIER + "-" + scenario.name;
      msg = await buildMessage(kp, id, URL, scenario.opts);
      if (!scenario.replayOf) replayMessages[scenario.name] = msg;
    }

    const payload = new TextEncoder().encode(JSON.stringify(msg));

    // Subscribe then send
    const statusPromise = waitForStatus(node, statusDecoder, msg.identifier, TIMEOUT_MS);
    const result = await node.lightPush.send(preserveEncoder, { payload });
    const sent = result.successes?.length > 0;

    if (!sent) {
      console.log("SEND FAILED");
      results.push({ name: scenario.name, pass: false, reason: "lightPush failed" });
      continue;
    }

    process.stdout.write(`sent. Waiting ${TIMEOUT_MS / 1000}s for reply... `);
    const status = await statusPromise;

    const pass = status === scenario.expectStatus;
    const indicator = pass ? "PASS" : "FAIL";
    const got = status ?? "(no reply)";
    const exp = scenario.expectStatus ?? "(no reply)";
    console.log(`${indicator}  got=${got}  expected=${exp}`);
    results.push({ name: scenario.name, pass, got, expected: exp });
  }

  await disconnect(node);

  console.log("\n── Summary ──────────────────────────────");
  let allPass = true;
  for (const r of results) {
    const mark = r.pass ? "✓" : "✗";
    console.log(`  ${mark}  ${r.name}`);
    if (!r.pass) { allPass = false; console.log(`       got=${r.got}  expected=${r.expected}`); }
  }
  console.log(allPass ? "\nAll scenarios passed." : "\nSome scenarios FAILED.");
  process.exit(allPass ? 0 : 1);
}

run().catch(err => { console.error(err); process.exit(1); });
