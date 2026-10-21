# Reorg economics

Supporting arithmetic for
[doc/exchange-listing-guidance.md](exchange-listing-guidance.md). The
recommendation there — a fixed deposit depth above 72 confirmations — follows
from the consensus reorg cap, not from anything in this file. This document
exists so the cost claims can be checked independently, and so an operator who
chooses to run inside the cap can size a deposit limit for it.

**Everything below is worked examples.** The figures are arithmetic over a
range of network sizes, chosen to show the shape of the cost curve. They are
not a report on the live chain and are not maintained against it. Current
hashrate is a one-command measurement and belongs in the reader's own
calculation, not in this file:

```bash
truenorth-cli getnetworkhashps 1000
```

## Where an attacker gets hashrate

RandomX is CPU-only, which cuts both ways: there is no ASIC advantage for
defenders, and no capital-investment barrier separating an attacker from one
either. Commodity CPUs are the hardware. But "CPU-only" is not the same as
"rentable on demand", and the difference matters for every figure below.

### Hashrate marketplaces cannot supply RandomNorth today

NiceHash and comparable marketplaces sell hashpower for specific algorithms.
They sell RandomX. They do not sell RandomNorth, because the custom Argon2 salt
means a stock RandomX miner computes a different proof of work: the hashes are
real, and they are for a chain nobody is paying for.

**Treat this as a listing decision with a short shelf life, not a property of
the design.** The salt is a constant in open-source mining software; supporting
RandomNorth means changing it and recompiling. Third parties have already
shipped RandomNorth-capable miners, so the engineering barrier is demonstrably
close to zero. What is absent is a marketplace listing decision and nothing
else.

If a marketplace does list RandomNorth, rental becomes a product again:
instant, contract-free, and priced by a market rather than by this document.
Every figure below would then need recomputing from marketplace pricing
instead of from cloud compute. Check whether that has happened before relying
on this section.

### The major clouds prohibit mining outright

An attacker who cannot buy hashes must rent or own general-purpose compute and
run the miner. The obvious source is excluded by contract rather than by price.
AWS Service Terms §1.25:

> You will not use, and will not facilitate or allow End Users to use, the
> Services to mine cryptocurrency.

Microsoft Azure and Google Cloud both prohibit mining without prior written
approval. Oracle Cloud prohibits it outright. So the hyperscalers are not an
open market for this workload at any price, and an on-demand price list does
not describe an available transaction.

Detection is weaker than the prohibition, and it is worth being accurate about
that. Cloud mining detection keys on queries to known mining domains and known
pool ports, plus optional runtime process inspection. A miner pointed at the
attacker's own node on port 9555 matches none of those signatures, and an
attacker does not enable runtime monitoring on their own account. The binding
risk is breach of terms and loss of the account and its instances, not reliable
discovery.

That leaves four real sources, none of them a price list:

- **Breach of terms on a hyperscaler.** Cheapest per hash, and the position is
  revocable at the provider's discretion at any moment.
- **Mining-tolerant hosts and bare metal.** Smaller, dearer, and they want
  identity and payment details.
- **Owned hardware.** No terms problem, but capital outlay and lead time.
- **Compromised compute.** How illicit mining is mostly actually done, and a
  cost model this document does not attempt.

### The rental rate, if you need one

For an attacker who does rent, the cost per unit of hashrate per day is `r`.
It is set by silicon generation far more than by provider, so quote it with the
hardware and the date attached:

| Basis | Measured hashrate | On-demand | `r` per H/s per day |
|---|---|---|---|
| 2 × Xeon Platinum 8275CL (48 cores) | 18.5–25.8 kH/s | ~$98/day | ~$0.004–0.005 |
| 2 × EPYC 9654-class (192 cores) | ~88–110 kH/s | ~$236/day | ~$0.002–0.003 |

Hashrates are published xmrig RandomX benchmarks for those CPUs; pricing is
us-east-1 on-demand as of 2026-10-13. Interruptible capacity discounts `r` by
roughly 70%, which a bounded attack window can tolerate. Cloud pricing and
silicon both move, so re-derive rather than quoting this table in a year.

**Use $0.005 when the question is whether an attack is affordable, and $0.002
when the question is whether a deposit is safe.** They differ by 2.5× and the
conservative direction is opposite for the two questions.

## Cost to reorg

Reorg cost is linear in both honest hashrate and depth, so a figure means
nothing without a stated network size. These rows are examples spanning two
orders of magnitude; read across to the column for the depth in question, and
interpolate or scale for a network size between them.

Attacker sustaining 2× the honest network. Each cell spans both ends of the
`r` range above, $0.002 to $0.005 per H/s per day.

| Example network | 6 conf (12 min) | 30 conf (60 min) | 72 conf (~2.4 hr) |
|---|---|---|---|
| 100 kH/s | $3 – $8 | $17 – $42 | $40 – $100 |
| 500 kH/s | $17 – $42 | $83 – $210 | $200 – $500 |
| 1 MH/s | $33 – $83 | $170 – $420 | $400 – $1,000 |
| 5 MH/s | $170 – $420 | $830 – $2,100 | $2,000 – $5,000 |

**Cost is exactly linear in hashrate**, so no row needs to match a real network:
ten times the hashrate is ten times the cost at the same depth, and a network
between two rows sits between their figures. Use the formula below with a
measured value rather than picking the nearest row.

The table stops at 72 because consensus refuses any deeper reorg. Past that
depth the cost is not merely large, it is irrelevant, which is the whole reason
the recommended deposit depth is a fixed 100 rather than a computed figure.

Collapsed, with `r` kept visible rather than folded into a constant:

```
attacker cost  ~=  2 × network H/s × r × (confirmations / 720)   dollars
```

720 is blocks per day at 120-second spacing. Read the other way, the same
expression gives the deposit value a given depth protects.

Folding `r` into a single divisor is convenient, and it is also how a stale
assumption becomes invisible — so both ends of the range above are given
explicitly rather than one being adopted as the constant:

```
r = $0.002  ->  cost  ~=  (network H/s × confirmations) / 180,000
r = $0.005  ->  cost  ~=  (network H/s × confirmations) /  72,000
```

Both forms take a measured hashrate, not one of the example rows. Read either
direction: given a network size and a depth, the expression gives the attacker's
cost; given a network size and a deposit value, it gives the depth at which the
deposit stops being worth attacking.

## The incumbent case: a pool that already holds the hashrate

Everything above prices an attacker who must acquire capacity. It does not
describe the cheapest attacker available on a chain where one pool already
mines most of the blocks.

That operator rents nothing. Their cost to rewrite a span is the subsidy and
fees given up on the branch they abandon, and because reorging a span also
recaptures its coinbase (see *What this does not model* below), the two largely
cancel. The rental table is not a loose upper bound on their cost. It does not
apply to them at all.

What follows for anyone sizing a deposit policy:

- Between **6 and 72 confirmations**, a value cap computed from the formula
  above is protection against an outsider and not against the largest
  incumbent. Treat that arithmetic as a floor on safety rather than a measure
  of it.
- **Above 72 confirmations** nothing changes. Consensus refuses the reorg
  whoever proposes it and whatever it cost them. That is the only
  recommendation in these documents which holds against an incumbent majority,
  and it is why the guidance is a fixed depth rather than a computed cap.
- Concentration is measurable from the chain. Count coinbase tags over a recent
  window before relying on any figure here, and recount periodically.

A large pool behaving normally does not change the arithmetic. The point is
that any depth below the cap is priced for the wrong adversary.

## Depth is not safe or unsafe on its own

What matters is whether rewriting a chosen depth costs more than the deposit
it protects. From the example rows, six confirmations covers roughly $3–8 at
100 kH/s and $170–420 at 5 MH/s, so an exchange that caps deposits low enough
can run a shallow policy rationally — it just has to track hashrate, hold the
cap, and accept that the cap does not bind an incumbent majority.

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
