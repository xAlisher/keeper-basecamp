**1. Who pays `registration_fee`?**
The preserver pays two things at `register_preservation`: `registration_fee` into the shared reward pool ([L472](https://github.com/xAlisher/keeper-basecamp/blob/keeper-protocol/docs/keeper-protocol-lez.md#L472)) and a `holding_deposit` in escrow per item, returned on clean exit, forfeited on failed challenge ([L473](https://github.com/xAlisher/keeper-basecamp/blob/keeper-protocol/docs/keeper-protocol-lez.md#L473)).

**2. Who runs `verify_holding` every 7 days?**
The preserver's own Keeper client — self-reporting ([L1341](https://github.com/xAlisher/keeper-basecamp/blob/keeper-protocol/docs/keeper-protocol-lez.md#L1341)). Known trust gap, economic deterrent only in v1.

**3. How do we prevent challenge spam?**
`challenge_spam_fee` — challenger locks stable, loses it if wrong ([L216](https://github.com/xAlisher/keeper-basecamp/blob/keeper-protocol/docs/keeper-protocol-lez.md#L216)). When `spel#226` lands: per-pair cooldown rate limit on top of that ([L655](https://github.com/xAlisher/keeper-basecamp/blob/keeper-protocol/docs/keeper-protocol-lez.md#L655)).

**4. Privacy**
Full analysis with options A–F and tradeoffs ([L1733](https://github.com/xAlisher/keeper-basecamp/blob/keeper-protocol/docs/keeper-protocol-lez.md#L1733)). v1 minimum: commit-reveal — store `sha256(ia_id || nonce)` instead of plaintext, breaks passive enumeration. Full anonymity requires ZK, ties into Q5.

**5. Storage team proofs**
Agreed — challenge-response is a workaround for missing cryptographic proof ([L1722](https://github.com/xAlisher/keeper-basecamp/blob/keeper-protocol/docs/keeper-protocol-lez.md#L1722)). If Logos Storage has native possession proofs, `verify_holding` + `challenge_holding` could simplify or disappear. Worth talking before v1 implementation starts.

**6. Which fees?**
Registration fees preservers pay when joining — they cycle back as monthly rewards ([whitepaper L53](https://github.com/xAlisher/keeper-basecamp/blob/keeper-protocol/docs/keeper-whitepaper.md#L53)). Tightened the sentence to say exactly that ([L71](https://github.com/xAlisher/keeper-basecamp/blob/keeper-protocol/docs/keeper-whitepaper.md#L71)).

**On "too optimistic"**
Fair. Whitepaper softened: removed "same effect as a cryptographic proof", added "economic deterrent not a cryptographic proof", "structurally more resilient" instead of "permanent". Open problems named explicitly in Honest Gaps ([L1702](https://github.com/xAlisher/keeper-basecamp/blob/keeper-protocol/docs/keeper-protocol-lez.md#L1702)).
