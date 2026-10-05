# Reorg economics

Supporting arithmetic for
[doc/exchange-listing-guidance.md](exchange-listing-guidance.md). The
recommendation there — a fixed deposit depth above 72 confirmations — follows
from the consensus reorg cap, not from anything in this file. This document
exists so the cost claims can be checked independently, and so an operator who
chooses to run inside the cap can size a deposit limit for it.

## Rented hashrate

RandomX is CPU-only, which cuts both ways. There is no ASIC advantage for
defenders, and equally no capital-investment barrier separating an attacker
from one: capacity is rentable on demand from any cloud provider.

Cost to sustain a reorg for one day at commodity on-demand pricing:

| Cloud capacity | Approx hashrate | Approx cost/day |
|---|---|---|
| 5 × c5.24xlarge | ~250 kH/s | ~$500 |
| 10 × c5.24xlarge | ~500 kH/s | ~$1,000 |
| 50 × c5.24xlarge | ~2.5 MH/s | ~$5,000 |
| 100 × c5.24xlarge | ~5 MH/s | ~$10,000 |

At these prices rentable capacity is effectively unbounded, so an attacker's
upper bound is set by willingness to spend rather than by availability.

## Cost to reorg

Reorg cost is linear in both honest hashrate and depth, so a figure only means
something against a stated network size. At 120-second spacing, **against a
36 MH/s network**, with the attacker sustaining 2× that (72 MH/s rented,
extrapolating the table above linearly at ~$0.002 per H/s per day):

| Confirmations | Chain time | Attacker cost to reorg |
|---|---|---|
| 6 | 12 min | ~$1,200 |
| 30 | 60 min | ~$6,000 |
| 100 | ~3.3 hr | ~$20,000 |
| 200 | ~6.7 hr | ~$40,000 |
| 500 | ~16.7 hr | ~$100,000 |

**Scale proportionally for any other network size:** a tenth of the hashrate
is a tenth of the cost at the same depth.

Collapsed, with `r` at $0.002 per H/s per day, the table is just:

```
attacker cost  ~=  (network H/s  ×  confirmations) / 180,000   dollars
```

Read the other way, that is the deposit value a given depth protects:

```
max safe deposit  ~=  (network H/s  ×  confirmations) / 180,000   dollars
```

Measure the hashrate rather than assuming one — `truenorth-cli
getnetworkhashps 1000` on any synced node. The soft input is `r`: $0.002 is
derived from the capacity table above, while a per-thread estimate from the
repo's own benchmarks gives roughly $0.007. The lower figure is the
attacker-favourable end, which is the conservative choice for anyone deciding
whether to accept a deposit.

## Depth is not safe or unsafe on its own

What matters is whether rewriting a chosen depth costs more than the deposit
it protects. Six confirmations covers about $3 at 100 kH/s and about $1,200 at
36 MH/s, so an exchange that caps individual deposits low enough can run a
shallow policy rationally — it just has to track hashrate and hold the cap.

Three regimes:

- **Below about 6 confirmations.** Ordinary orphan races can orphan a deposit
  with no attacker involved. The deepest honest fork observed on public
  testnet4 across six weeks of operation was 3 blocks. Treat 6 as a floor
  whatever the value arithmetic says.
- **6 to 72 confirmations.** Defensible only with a per-deposit value cap
  computed from current hashrate and revisited as it moves.
- **Above 72 confirmations.** Reorg refused by consensus — no cap, no hashrate
  tracking, no arithmetic. This is why the recommended depth is 100: it is the
  point at which an exchange stops having to do any of this.

## What this does not model

- **Block subsidy recapture.** An attacker who reorgs a span also collects its
  coinbase. At 512 NORTH per block that offsets the rental cost, and above
  roughly `hashrate / 92,160,000` dollars per NORTH it exceeds it outright,
  making the hardware self-funding before any double-spend. Depth does not
  help: both the cost and the recaptured subsidy scale with it, so the ratio
  is fixed by hashrate alone. The 100-block coinbase maturity rule delays
  liquidity but does not remove the effect.
- **The honest-mining alternative.** An attacker holding rented capacity could
  mine honestly instead and still earn. The figures above are gross rental
  cost, not the margin over that alternative.
- **Difficulty response.** LWMA retargets during a sustained attack; this is
  a static snapshot.
