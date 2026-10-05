# Exchange listing guidance

This document is for two audiences:

- **Exchange operators** considering listing NORTH — what deposit depth is
  safe, and why.
- **Users** deciding whether a claimed NORTH listing is legitimate.

The project does not solicit, negotiate, or pay for exchange listings.
Anything below is guidance grounded in the security model, not a business
development document. The cost arithmetic behind it is in
[doc/reorg-economics.md](reorg-economics.md); this file gives the conclusions.

## What to do

1. **Require more than 72 confirmations for deposits. Use 100.** Consensus
   refuses any reorg deeper than 72 blocks, so past that depth a double-spend
   is not expensive — it is rejected. Inside 72 it is cheap, and a shallower
   policy is viable only alongside a per-deposit value cap that you compute
   from current hashrate and maintain yourself. See
   [reorg-economics](reorg-economics.md) if you want to do that.
2. **Keep your node online continuously.** The cap defends a node relative to
   a tip it already holds. A node that was down during an attack, or that you
   resync afterwards, has no tip to defend and will accept the attacker's
   chain. Deposit crediting must depend on a node that stayed up.
3. **Monitor for deep reorgs.** Run `getchaintips` on a schedule and
   auto-freeze deposits if the height differential exceeds 1.5× your deposit
   depth. `reorg-monitor.sh` in
   [`contrib/monitoring/`](../contrib/monitoring/) is a starting point.

That is the whole operational recommendation. It does not move with network
hashrate or with the chain's age.

## Why 72

TrueNorth caps reorg depth in consensus: `max_reorg_depth = 72` on mainnet,
testnet3, testnet4 and signet, and 0 on regtest so reorg-heavy test scripts
still work. A node refuses to disconnect more than 72 blocks from its own tip
— 2.4 hours at 120-second spacing — and drops the offending chain from its
candidate set. The check is in `ActivateBestChainStep`
(`src/validation.cpp`).

An attacker cannot force a deeper reorg at any price, which is why the
recommendation is a fixed depth rather than a function of hashrate. Two limits
matter:

**It protects a tip, not a chain.** A node syncing from scratch has no tip to
defend and will follow whatever most-work chain it is offered. The cap cannot
help there. Notarization is what would.

**A refused reorg is a partition, not a clean win.** Nodes that watched the
honest chain reject the attacker's history while freshly-synced nodes accept
it. Recovery is operator action — `invalidateblock` on the current tip until
the fork depth falls inside the cap — announced on `#truenorth-dev`. Treat the
cap as defence in depth that converts a theft into an incident, not as a
reason the chain cannot be attacked.

Separately, below about 6 confirmations there is a problem with no attacker in
it at all: ordinary orphan races. The deepest honest fork observed on public
testnet4 across six weeks of operation was 3 blocks, so a 1- or
2-confirmation policy loses deposits on its own.

## What the project will and won't do

**Will:**
- Respond to technical inquiries from exchange operators (API, node setup,
  chain parameters, reorg-monitoring recommendations).
- Confirm on `#truenorth` IRC (OFTC) that a specific listing appears
  legitimate, once due diligence indicates it is.
- Publish a security advisory if a listing is unsafe — too-low confirmation
  depth, obvious operational red flags, or believed-fraudulent.

**Will not:**
- Accept listing fees, marketing spend, or paid coordination.
- Coordinate airdrops, giveaways, or promotional campaigns with any exchange.
- Sign legal agreements or "letters of endorsement" for exchanges.
- Respond to KYC-related inquiries about the project or contributors.
- Provide market-making, liquidity provision, or any pre-listing token
  transfers. There are no such tokens: NORTH exists only as mined coinbase
  emissions.

The project will not vouch for a listing inside the first 30 days after
launch, because there is not yet enough chain history to evaluate reorg
behaviour under load. That is a statement about what the project will confirm,
not an accusation against anyone who lists earlier.

## Advice for users

- Do not assume a listing is legitimate because the chain has been running a
  while, and do not assume it is fraudulent because the chain is new. Check
  the exchange's stated deposit depth, and whether the listing has been
  discussed on `#truenorth`. An unverified listing is not safe to deposit into
  even if the exchange is real.
- A shallow deposit depth is only sound if the exchange also caps deposit size
  against current hashrate. If it states 6 blocks — or worse, "1 block for
  testnet-style speed" — and accepts deposits of any size, that is the tell:
  it has either not done the arithmetic or is not applying it.
- If an exchange DMs you claiming to represent the project, or asks you to
  send NORTH to prove ownership, verify signatures, or complete a "listing
  pool contribution": stop. The project does not communicate that way. See
  [README's community section](../README.md#community) for official channels.

## For exchange operators, beyond the three items above

1. **Sync a full node yourself** from
   `github.com/truenorth-project/truenorth` releases. Verify the GPG signature
   before running; the fingerprint is published in
   [README.md](../README.md) and in the `#truenorth` IRC topic. Cross-check
   the two — a fingerprint appearing in only one place is not corroborated.
2. **Run it for at least two weeks** before crediting deposits, to validate
   reorg behaviour, block-time variance under LWMA, and seed-key rotation on
   your own infrastructure.
3. **Follow `#truenorth-dev` on OFTC IRC.** Emergency-response actions are
   announced there first.

   One limitation worth understanding: hardcoded checkpoints
   (`src/checkpoints.h`, procedure in
   [doc/checkpoint-process.md](checkpoint-process.md)) are a real mechanism —
   TrueNorth reintroduces the `CCheckpointData` map that upstream Core
   removed — but they take effect only in a release that operators then
   install. At launch `m_checkpoint_data` holds the genesis entry alone, which
   consensus already enforces, so the effective protection is zero until a
   post-launch release adds higher-height entries. Treat a checkpoint release
   as recovery with a build-and-upgrade delay in front of it, not a live
   defence.

Technical inquiries are welcome via `#truenorth-dev` on OFTC (open, logged),
or GitHub issues for protocol and consensus questions. Do not expect email
correspondence — the project operates through public channels.

## When this changes

**Not with hashrate.** More than 72 confirmations is a consensus bound, not an
economic one, so growth neither strengthens nor weakens it. What hashrate
changes is continuous rather than a milestone — the deposit value a given
depth protects, for anyone operating inside the cap — and that is covered in
[reorg-economics](reorg-economics.md).

**With notarization.** TRUENORTH_SPEC.md §9 commits to notarizing TrueNorth
block hashes into a high-hashrate chain (Bitcoin, most likely) at intervals.
Once live, the effective reorg bound becomes the notarization interval rather
than 72 blocks, and it covers the case the cap cannot: a node syncing from
scratch. That is the milestone that would let conventional confirmation depths
apply. It has no fixed date; it will be discussed on `#truenorth-dev` when it
lands, and this document updated.

## References

- [doc/reorg-economics.md — Cost arithmetic behind the depth recommendation](reorg-economics.md)
- [TRUENORTH_SPEC.md §9 — Security & 51%-Attack Resistance](../TRUENORTH_SPEC.md#9-security--51-attack-resistance)
- [doc/checkpoint-process.md — Maintainer runbook for release-time checkpoints](checkpoint-process.md)
- [doc/mining-policy.md — Project stance on mining topology and 33% norm](mining-policy.md)
- [doc/testnet.md — Testnet4 bootstrap; useful for exchange integration testing](testnet.md)
