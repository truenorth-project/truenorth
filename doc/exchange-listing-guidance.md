# Exchange listing guidance

This document is for two audiences:

- **Exchange operators** considering listing NORTH — what depths are safe, and
  when.
- **Users** deciding whether a claimed NORTH listing is legitimate.

The project does not solicit, negotiate, or pay for exchange listings.
Anything below is guidance grounded in the security model, not a business
development document.

## The short version

- **Require more than 72 confirmations for deposits; 100 gives margin.** This
  is the whole recommendation. Consensus refuses any reorg deeper than 72
  blocks, so past that depth a double-spend is not expensive, it is rejected
  -- see the cap section below. One number, not a schedule: it moves with
  neither hashrate nor the calendar.
- **Inside the cap, safety is a value test rather than a depth.** Rewriting a
  given depth costs `hashrate x depth / 180,000` dollars, so a policy inside
  the cap is sound only for deposits below that figure -- about $3 at
  100 kH/s with six confirmations, about $1,200 at 36 MH/s. An exchange can
  run a shallow policy if it caps deposit size to match and re-checks as
  hashrate moves. Above 72 it needs neither the cap nor the tracking.
- **Safety here is a consensus rule plus arithmetic, not a date.** Nothing in
  this document is gated on elapsed time. For the sub-72 window the inputs are
  both observable -- network hashrate (`truenorth-cli getnetworkhashps 1000`)
  and the exchange's stated deposit depth -- so a chain that grows fast earns
  a better position immediately rather than waiting out a schedule.
- **The project will not vouch for any listing inside the first 30 days**,
  because there is not yet enough chain history to evaluate reorg behaviour
  under load. That is a statement about what the project will confirm, not an
  accusation against anyone who lists earlier.
- **After notarization to a larger chain is deployed** (see
  [TRUENORTH_SPEC.md](../TRUENORTH_SPEC.md) §9): confirmation depth
  requirements can relax to conventional levels. Attack cost is bounded by
  the notarization interval.

## Why confirmation depth matters

The threat model is an attacker renting cloud CPU capacity to outpace organic
mining, reorganize recent history, and double-spend against an exchange. What
makes that cheap or expensive is the chain's hashrate at the time, which is a
measurement rather than a forecast -- check it with
`truenorth-cli getnetworkhashps 1000` and read the figures below against it.

RandomX is CPU-only, which cuts both ways: it means no ASIC advantage for
defenders, and it also means an attacker can rent capacity on demand from
any cloud provider. There is no "capital investment" barrier separating
attackers from defenders.

Rough attack economics — cost to sustain a reorg for one day, assuming an
attacker willing to pay commodity cloud on-demand pricing:

| Cloud capacity | Approx hashrate | Approx cost/day |
|---|---|---|
| 5 × c5.24xlarge | ~250 kH/s | ~$500 |
| 10 × c5.24xlarge | ~500 kH/s | ~$1,000 |
| 50 × c5.24xlarge | ~2.5 MH/s | ~$5,000 |
| 100 × c5.24xlarge | ~5 MH/s | ~$10,000 |

Rentable capacity is effectively unbounded at these prices, so an attacker's
upper bound is set by willingness to spend rather than by availability. Two
things make the attack uneconomical: confirmation depth, and the reorg cap
documented below.

Reorg cost scales linearly with both honest hashrate and depth, so it only
means anything against a stated network size. At 120-second block time,
**against a 36 MH/s network**, with the attacker sustaining 2x that (72 MH/s
rented, extrapolating the capacity table above linearly at ~$0.002 per H/s
per day):

| Confirmations | Chain time | Attacker cost to reorg |
|---|---|---|
| 6 | 12 min | ~$1,200 |
| 30 | 60 min | ~$6,000 |
| 100 | ~3.3 hr | ~$20,000 |
| 200 | ~6.7 hr | ~$40,000 |
| 500 | ~16.7 hr | ~$100,000 |

**Scale proportionally for any other network size:** a tenth of the hashrate
is a tenth of the cost at the same depth. A chain an order of magnitude
smaller than the figure above offers an order of magnitude less protection at
every row.

Depth on its own is neither safe nor unsafe. What matters is whether
rewriting your chosen depth costs more than the deposit it protects, which
makes the test `deposit value < hashrate x depth / 180,000`. Six
confirmations covers about $3 at 100 kH/s and about $1,200 at 36 MH/s, so an
exchange that caps individual deposits low enough can run a shallow policy
rationally — it just has to track hashrate and hold the cap.

Three regimes:

- **Below about 6 confirmations:** ordinary orphan races can orphan a deposit
  with no attacker involved. The deepest honest fork observed on public
  testnet4 across six weeks of operation was 3 blocks. Treat 6 as a floor
  whatever the value arithmetic says.
- **6 to 72 confirmations:** defensible only with a per-deposit value cap
  computed from current hashrate, and re-checked as hashrate moves.
- **Above 72 confirmations:** reorg refused by consensus — no cap, no hashrate
  tracking, no arithmetic. This is why the recommended depth is 100: it is the
  point at which an exchange stops having to do any of this.

### The 72-block cap is what actually settles this

The costs above only describe reorgs that can happen. TrueNorth caps reorg
depth in consensus: `max_reorg_depth = 72` on mainnet, testnet3, testnet4 and
signet (regtest sets 0 so reorg-heavy test scripts still work). A node refuses
to disconnect more than 72 blocks from its own tip -- 2.4 hours at 120-second
spacing -- and drops the offending chain from its candidate set.

So an attacker cannot force a deeper reorg at any price. Hashrate and rental
cost both stop mattering past 72 blocks.

| Deposit confirmation depth | Exposure |
|---|---|
| <= 72 blocks | Reorgable. Cost is the table above; rewriting the full 72 blocks runs ~$40 at 100 kH/s, ~$400 at 1 MH/s, ~$14,400 at 36 MH/s. |
| > 72 blocks (**use 100** for margin) | Reorg refused by consensus on any continuously-online node. |
| Post-notarization | Reorg additionally capped by the notarization interval. |

**The depth recommendation is therefore a single number, not a ladder: more
than 72, and 100 gives margin.** Scaling depth against hashrate buys nothing
above the cap, because every depth past it is equally unreorgable. Inside the
cap the value test above still governs, so a shallower policy is viable only
with a per-deposit cap sized from current hashrate and revisited as it moves
-- which is precisely the work that choosing `>72` avoids.

**The cap protects a node relative to a tip it already holds.** A node that
was offline during an attack, or still doing initial sync, has no tip to
defend and will follow the most-work chain it is offered -- including the
attacker's. Continuous uptime is therefore a security requirement for anyone
accepting deposits, not merely good practice.

It also means a successful deep-reorg attempt produces a **partition** rather
than a takeover: nodes that watched the honest chain refuse the attacker's
history while freshly-synced nodes accept it. Recovery is operator action
(`invalidateblock` on the current tip until the fork depth falls inside the
cap), announced on `#truenorth-dev`. Treat the cap as defence in depth that
converts a theft into an incident, not as a reason the chain cannot be
attacked.

## What the project will and won't do

**Will:**
- Respond to technical inquiries from exchange operators (API, node setup,
  chain parameters, reorg-monitoring recommendations).
- Confirm on `#truenorth` IRC (OFTC) that a specific listing appears
  legitimate, once due diligence indicates it is.
- Publish a security advisory if a listing is unsafe (too-low confirmation
  depth, obvious operational red flags, or believed-fraudulent).
- Point to the on-node monitoring scripts in
  [`contrib/monitoring/`](../contrib/monitoring/) — in particular
  `reorg-monitor.sh` for detecting deep chain reorganizations from a
  running node.

**Will not:**
- Accept listing fees, marketing spend, or paid coordination.
- Coordinate airdrops, giveaways, or promotional campaigns with any
  exchange.
- Sign legal agreements or "letters of endorsement" for exchanges.
- Respond to KYC-related inquiries about the project or contributors.
- Provide market-making, liquidity provision, or any pre-listing token
  transfers. There are no such tokens: NORTH exists only as mined coinbase
  emissions.

## Advice for users

- Do not assume a listing is legitimate because the chain has been running a
  while, and do not assume it is fraudulent because the chain is new. Check
  the arithmetic: network hashrate, the exchange's stated deposit depth, and
  whether the listing has been discussed on `#truenorth`. An unverified
  listing is not safe to deposit into even if the exchange is real.
- Whatever the chain's age, check the exchange's stated deposit confirmation
  depth. A shallow depth is only sound if the exchange also caps deposit size
  against current hashrate. If it states 6 blocks — or worse, "1 block for
  testnet-style speed" — and accepts deposits of any size, that is the tell:
  it has either not done the arithmetic or is not applying it. A 1-block
  policy is wrong regardless of size, since honest orphan races alone run
  deeper than that.
- Real exchanges pursuing a legitimate listing will be identifiable through
  `#truenorth` IRC discussion — either the project confirming they exist,
  or the community discussing them openly.
- If an exchange DMs you claiming to represent the project, or asking you
  to send NORTH to prove ownership, verify signatures, or complete a
  "listing pool contribution": stop. The project does not communicate that
  way. See [README's community section](../README.md#community) for
  official channels.

## Advice for exchange operators

If you are considering listing NORTH:

1. **Sync a full node yourself** from `github.com/truenorth-project/truenorth`
   releases. Verify the GPG signature before running; the fingerprint is
   published in [README.md](../README.md) and in the `#truenorth` IRC topic.
   Cross-check the two — a fingerprint appearing in only one place is not
   corroborated.
2. **Run for at least two weeks** to validate reorg behaviour, block time
   variance under LWMA, and seed-key rotation on your infrastructure.
   **Then keep it online continuously.** The 72-block reorg cap only defends
   a node relative to a tip it already has; a node that was down during an
   attack, or that you resync afterwards, will accept the attacker's chain.
   Deposit crediting must depend on a node that has been online throughout.
3. **Set a deposit confirmation minimum** per the table above, and above 72
   unless you are also capping deposit value. The cap bounds a rewrite to
   2.4 hours of history, so the exposure is not open-ended — but inside that
   window it is cheap, and it is your deposits rather than the chain that
   pay for it.
4. **Monitor for deep reorgs.** Run `getchaintips` on a schedule.
   Auto-freeze deposits if the height differential exceeds
   1.5× your deposit confirmation depth. Reference scripts in
   [`contrib/monitoring/`](../contrib/monitoring/); `reorg-monitor.sh`
   is a starting point.
5. **Follow `#truenorth-dev` on OFTC IRC.** Emergency-response actions are
   announced there first. If you list NORTH and don't watch that channel,
   you're operating blind during the highest-risk period for the chain.

   One limitation worth understanding: hardcoded checkpoints
   (`src/checkpoints.h`, procedure in
   [doc/checkpoint-process.md](checkpoint-process.md)) are a real mechanism —
   TrueNorth reintroduces the `CCheckpointData` map that upstream Core
   removed — but they take effect only in a release that operators then
   install. At launch `m_checkpoint_data` contains the genesis entry alone,
   which consensus already enforces, so the effective protection is zero
   until a post-launch release adds higher-height entries. Treat a
   checkpoint release as recovery with a build-and-upgrade delay in front of
   it, not as a live defence.

The project welcomes technical inquiries from exchange operators. Reach
out via `#truenorth-dev` on OFTC (open, logged), or GitHub issues for
protocol/consensus questions. Do not expect email correspondence — the
project operates through public channels.

## When this policy relaxes

**The depth recommendation does not relax with hashrate.** More than 72
confirmations is a consensus bound, not an economic one, so growth in
hashrate neither strengthens nor weakens it. Nothing here is waiting on the
chain getting bigger before `>72` becomes sound — it is sound now.

What hashrate changes is continuous rather than a milestone. Inside the cap,
the deposit value a given depth protects is
`hashrate x depth / 180,000`, so every increase raises the cap an exchange
can safely set at its chosen depth. There is no height at which something
switches on. As a concrete marker, rewriting the full 72-block window costs
roughly `hashrate / 2,500` dollars — about $400 at 1 MH/s and about $10,000
at 25 MH/s, the latter being roughly where the window alone covers a retail
deposit book with no per-deposit cap needed.

**One genuine step change remains: notarization.** TRUENORTH_SPEC.md §9
commits to notarizing TrueNorth block hashes into a high-hashrate chain
(Bitcoin, most likely) at intervals. Once live, the effective reorg bound
becomes the notarization interval rather than 72 blocks — and it covers the
case the reorg cap cannot, namely a node syncing from scratch, which has no
tip to defend and will follow whatever most-work chain it is offered. That is
the milestone that would let conventional confirmation depths apply.

It has no fixed date. It will be discussed openly on `#truenorth-dev` when it
lands, and this document updated.

## References

- [TRUENORTH_SPEC.md §9 — Security & 51%-Attack Resistance](../TRUENORTH_SPEC.md#9-security--51-attack-resistance)
- [doc/checkpoint-process.md — Maintainer runbook for release-time checkpoints](checkpoint-process.md)
- [doc/mining-policy.md — Project stance on mining topology and 33% norm](mining-policy.md)
- [doc/testnet.md — Testnet4 bootstrap; useful for exchange integration testing](testnet.md)
