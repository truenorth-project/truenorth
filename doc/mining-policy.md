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

## What this document does not say

- It does not restrict who may mine, on what pool, or via what
  strategy.
- It does not commit the project to shipping pool software.
- It does not obligate the maintainers to endorse or vet any specific
  pool.
