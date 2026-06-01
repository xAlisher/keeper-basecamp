import { sign } from "./crypto.mjs";

/**
 * Build a preserve message.
 * opts:
 *   staleTimestamp  — timestamp 5 min in the past  (triggers freshness rejection)
 *   futureTimestamp — timestamp 5 min in the future
 *   badSig          — flip first byte of sig       (triggers sig rejection)
 *   unknownPubkey   — replace pubkey with zeros     (triggers whitelist rejection)
 *   customPubkey    — hex string to use as pubkey
 */
export async function buildMessage(kp, identifier, url, opts = {}) {
  const ts = opts.staleTimestamp  ? Math.floor(Date.now() / 1000) - 300
           : opts.futureTimestamp ? Math.floor(Date.now() / 1000) + 300
           : Math.floor(Date.now() / 1000);

  const pubkey = opts.unknownPubkey ? "0".repeat(64)
               : opts.customPubkey  ? opts.customPubkey
               : kp.pubkeyHex;

  // Canonical order must match JS JSON.stringify insertion order AND keeper's raw string build
  const canonical = { action: "preserve", identifier, url, timestamp: ts, pubkey };
  let sig = await sign(kp.priv, canonical);

  if (opts.badSig) {
    // Flip first char of base64 sig to corrupt it
    sig = (sig[0] === "A" ? "B" : "A") + sig.slice(1);
  }

  return { ...canonical, sig };
}
