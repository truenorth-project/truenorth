# TrueNorth (NORTH) v1 Coin Specification

> **Status:** v1 design, confirmed.
> **Name / Ticker:** TrueNorth / NORTH. Canadian-themed.
> **Base codebase:** fork of Bitcoin Core (current stable release).

## 1. Identity
- **Name / Ticker:** TrueNorth / NORTH
- **Theme:** Canadian
- **Smallest unit:** 1 NORTH = 100,000,000 **loonies** (8 decimals). The base unit is the loonie.
- **Base codebase:** fork of Bitcoin Core (current stable release)

## 2. Consensus & Proof of Work
- **Consensus:** Proof of Work
- **Algorithm:** RandomX. CPU-optimized, strongest mainstream ASIC-resistance,
  battle-tested on Monero since 2019. Integrated as a bundled library into the
  Bitcoin Core fork. Replaces SHA-256d as the block-header PoW hash.
- **Customized variant ("RandomNorth"):** a unique Argon2 salt (and, optionally,
  tweaked RandomX constants) so stock RandomX / xmrig hashpower can't be aimed at
  this chain without a purpose-built miner. This is the security role the Yespower
  personalization string used to serve.
- **Seed-key rotation:** the RandomX key changes every 2048 blocks (~2.84 days),
  with a 64-block activation lag so miners can precompute the next dataset
  (Monero's parameters).
- **Modes:** mining uses fast mode (~2 GiB dataset); node block-verification uses
  light mode (~256 MiB cache), so verification stays lightweight.
- **Block time target:** 120 seconds (262,800 blocks/year)
- **Coinbase maturity:** 100 blocks

## 3. Emission & Monetary Policy
- **Fair launch.** No premine, no founder allocation. Genesis coinbase is
  unspendable; emission begins at block 1, open to all.
- **Reward formula:** `reward(height) = max(512 >> floor(height / 1051200), 8)` NORTH
- **Halving cadence:** every 1,051,200 blocks (~4 years at 120s block time).
  Matches Bitcoin's halving period; produces a distribution curve where
  year-1 issuance is ~12.5% of pre-tail supply.

| Phase | Years | Block reward |
|-------|-------|--------------|
| 1     | 0–4   | 512          |
| 2     | 4–8   | 256          |
| 3     | 8–12  | 128          |
| 4     | 12–16 | 64           |
| 5     | 16–20 | 32           |
| 6     | 20–24 | 16           |
| Tail  | 24+   | 8 (perpetual)|

- **Main emission (~24 yrs):** ~1.06 billion NORTH
- **Tail emission (yr 24+):** 8 NORTH/block, about 2.1M per year, perpetual (~0.2% inflation, declining)
- `MAX_MONEY` sanity constant set well above any reachable supply (no hard cap)

## 4. Difficulty Adjustment
- **Algorithm:** LWMA (Linear Weighted Moving Average), retargets every block
- **Window:** N = 90 blocks (~3 hours)
- **Launch `powLimit`:** permissive, so initial blocks mine quickly on CPUs

## 5. Network Parameters
*Values below are proposed; finalize as unique before launch.*
- **P2P port:** 9555
- **RPC port:** 9554
- **Network magic bytes:** TBD (4 bytes, unique)
- **Address prefixes:**
  - P2PKH version byte: chosen so legacy addresses begin with **N** (exact byte set at network-params time)
  - P2SH version byte: TBD
  - Bech32 HRP: `north`. Native-segwit addresses look like `north1q...`
- **BIP32 extended-key prefixes:** TBD

## 6. Genesis Block
- **Coinbase message:** TBD. A real Canadian newspaper headline dated to launch day
  (timestamp proof, Bitcoin-genesis style)
- **Genesis reward:** unspendable (enforces no premine)
- **Timestamp / nonce / hash:** computed in a one-time genesis-mining run

## 7. Inherited-feature Decisions
- **SegWit & Taproot:** keep. Both are standard, active Bitcoin Core features
  (bech32 / bech32m addresses, smaller transactions).
- **Relay/mempool policy:** inherit Bitcoin Core defaults.

## 8. Supply at a Glance
- Year 1: ~135M NORTH (12.5% of pre-tail; matches Bitcoin's year-1 share)
- Year 24: ~1.06B NORTH (main emission complete)
- After: ~2.1M NORTH/year, perpetual (~0.2% inflation, declining)

## 9. Security & 51%-Attack Resistance

**Threat.** A party controlling more than 50% of hashrate can rewrite recent history
to double-spend. New coins are vulnerable. Total hashrate is low, and hashpower for
common algorithms can be rented cheaply.

**Design choices that mitigate it:**
- **RandomX (CPU-optimized, strong ASIC-resistance).** Battle-tested on Monero since
  2019. No efficient ASIC market that meaningfully out-competes CPUs.
- **Customized "RandomNorth" variant.** A unique RandomX configuration isolates this
  chain's PoW from stock RandomX / xmrig hashpower. Only purpose-built miners can
  mine or attack it.
- **LWMA difficulty (per-block).** Defeats "hash-and-run" difficulty-stranding attacks.
- **CPU mining, broad miner base.** Raises genuine distributed hashrate.

**Planned additional measures:**
- **Developer checkpoints.** Active from genesis (mechanism ships in the Bitcoin
  Core base). Primary defense in the early launch window; a fresh checkpoint added
  each release. Retained afterward as a near-zero-cost static fallback.
- **Notarization to a larger chain (delayed-PoW style).** Committed; built and
  deployed once the chain is live. Periodically commits TrueNorth block hashes into a
  high-hashrate chain so any reorg past a notarized point gets rejected. Becomes the
  primary ongoing 51% defense as checkpoint reliance is wound down.
- **Documented emergency-response plan.** Coordinated checkpoint or node update to
  reject a malicious deep reorg (the real post-attack remedy).
- **Exchange guidance.** Recommend 60–100+ confirmations for deposits.

**Residual risk & tradeoffs:**
- The first weeks, before a miner base forms and while hashrate is low, are the
  danger window.
- Cloud-CPU rental remains a long-term vector. RandomX has some specialized hardware
  (e.g. iPollo X1) but its efficiency advantage over CPUs is small.
- **Merge-mining**, the strongest small-coin defense, was deliberately rejected. It
  ties mining to ASICs or a parent ecosystem and breaks the "anyone can mine" goal.
- There's **no after-the-fact "counter-mine to reverse" cure.** Counter-mining is
  itself a 51% attack and would prove the chain mutable. Defense is prevention plus
  instant tools (checkpoints), not hashpower retaliation.

## Open Items
- Finalize §5 network parameters and §6 genesis details (does not block development)

## Build Outline
Fork Bitcoin Core. Integrate RandomX with the customized "RandomNorth" variant and
seed-key rotation. Patch the reward function for tail emission. Replace the
difficulty algorithm with LWMA. Set network parameters. Generate genesis block.
Launch testnet. Public mainnet launch.
