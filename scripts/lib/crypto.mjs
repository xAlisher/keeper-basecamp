import { webcrypto } from "node:crypto";
import { readFileSync, writeFileSync, existsSync } from "node:fs";

const { subtle } = webcrypto;
const KEYPAIR_PATH = new URL("../keypair.json", import.meta.url).pathname;

export async function loadOrGenKeypair() {
  if (existsSync(KEYPAIR_PATH)) {
    return JSON.parse(readFileSync(KEYPAIR_PATH, "utf8"));
  }
  const kp = await subtle.generateKey("Ed25519", true, ["sign", "verify"]);
  const priv = await subtle.exportKey("jwk", kp.privateKey);
  const pub  = await subtle.exportKey("jwk", kp.publicKey);
  // x is the 32-byte public key in base64url
  const pubkeyHex = Buffer.from(pub.x, "base64").toString("hex");
  const stored = { priv, pub, pubkeyHex };
  writeFileSync(KEYPAIR_PATH, JSON.stringify(stored, null, 2));
  return stored;
}

export async function sign(privJwk, canonicalObj) {
  const key = await subtle.importKey("jwk", privJwk, "Ed25519", false, ["sign"]);
  const data = Buffer.from(JSON.stringify(canonicalObj));
  const sig  = await subtle.sign("Ed25519", key, data);
  return Buffer.from(sig).toString("base64");   // standard base64 — matches btoa() in extension
}
