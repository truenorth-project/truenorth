# Mining topology: project stance on pools

Written pre-launch. Not a consensus rule — the chain accepts blocks
from any miner, however they organize their hash. This document
records the project's preference for how mining should be
distributed, and why.

## Position

Solo mining and pool mining are both welcome on TrueNorth. Among
pools, **P2Pool-style peer-to-peer pools are strongly preferred over
centralized pools**.

TrueNorth ships no stratum pool software and hosts no reference
pool. Pools are external infrastructure.

## Why P2Pool over centralized pools

A centralized pool operator has structural power the chain cannot
constrain:

- **Transaction censorship**. The operator picks which transactions
  go into templates. If the operator refuses to include certain
  transactions, every miner pointed at that pool follows the
  operator's policy without individual consent.
- **51% concentration risk**. Bitcoin has repeatedly seen individual
  pools cross 40-50% of network hash. A single pool with >50% hash
  can produce chain reorgs at will; the consensus rules cannot
  distinguish "large legitimate pool" from "attacker with rented
  hash".
- **Payout dependency**. Operator holds the pool's coinbase outputs
  until scheduled distribution; miners take counterparty risk on
  every share.

P2Pool inverts each of these:

- Each participant runs a full node and picks their own transactions.
  There is no operator to censor.
- The share chain is peer-to-peer. There is no central point to
  compromise, subpoena, or capture.
- Coinbase outputs pay participants directly in the block that finds
  a share. No custody.

P2Pool has real costs — higher payout variance for small miners,
more complex setup, higher node overhead — and those tradeoffs are
legitimate reasons an individual miner might choose a centralized
pool. This document records the project's preference, not a mandate.

## Why centralized pools are not restricted

The consensus rules cannot cleanly enforce "no centralized pools",
and attempting to do so via out-of-band pressure (block-hash
blacklists, coinbase-signature checks, etc.) would violate the
neutrality principle the chain is built on. A miner who chooses a
centralized pool is participating validly.

The project's role is to state a preference clearly, explain the
reasoning, and let operators make informed choices.

## What miners can do

- **Prefer P2Pool** when the tradeoffs work for your hardware and
  patience for setup. Configuration guides live outside this repo.
- **If running a centralized pool**, publish your coinbase-inclusion
  policy so miners can make an informed choice. Rotate coinbase
  addresses across blocks to reduce apparent concentration in
  block-explorer views.
- **Any pool**, centralized or peer-to-peer, should keep the
  operator's aggregate share below 33% of network hash whenever
  possible. Above 33% enables selfish-mining strategies; above 50%
  enables reorg attacks.

## Protocol-level mitigations

The chain ships one selfish-mining backstop today:

- **72-block max reorg depth cap** (in the validation module). Any
  reorg attempting to disconnect more than 72 blocks from the
  current tip is refused, regardless of accumulated work. This
  bounds the worst-case damage from selfish mining (or any
  block-withholding attack) to roughly 2.4 hours of rewritten chain
  history, and does so with a small, well-understood rule.

Additional protocol-level mitigations from the academic literature
— Freshness Preferred (Zhang & Preneel 2019 and variants),
Ethereum-style uncle/ommer rewards, DECOR-style orphan penalties,
weak-blocks / pre-consensus signalling — are **considered and
deferred**. Reasons:

- Threshold-raising mitigations (like Freshness Preferred) are
  designed for chains where a single pool creeps toward ~33% of
  network hash. On a small chain with few active miners, every
  participant is already above that threshold; the mitigation
  does not bind.
- The 72-block reorg cap already caps worst-case damage.
- Consensus rules added pre-launch have zero real-world validation;
  timestamp-based mechanisms in particular have adversarial
  subtleties (timestamp manipulation, clock-skew abuse, MTP
  interactions) that need careful modelling and testnet burn-in.
- If selfish mining becomes an observed problem post-launch, a
  scheduled release can add the appropriate mitigation with
  community coordination — an asymmetric-reversibility argument in
  favour of shipping less rather than more.

Revisit approximately 6-12 months post-launch when the miner
distribution is measurable and any live pool-concentration behaviour
is observable. At that point Freshness Preferred is the leading
candidate: fork-choice tiebreak change, no tokenomic impact, targets
the specific propagation-advantage weakness that makes selfish
mining profitable.

## What this document does not say

- It does not restrict who may mine, on what pool, or via what
  strategy.
- It does not commit the project to shipping pool software.
- It does not obligate the maintainers to endorse or vet any specific
  pool.
