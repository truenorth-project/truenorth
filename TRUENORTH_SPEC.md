# TrueNorth (NORTH) v1 Coin Specification

> **Status:** v1, shipped. Mainnet live since **2026-10-12 00:00:00 UTC**.
> Public testnet4 live since 2026-08. Describes TrueNorth Core v30.5.1.
> **Name / Ticker:** TrueNorth / NORTH. Canadian-themed.
> **Base codebase:** fork of Bitcoin Core.
>
> Where this document and the source code disagree, the code is correct.

## 1. Identity
- **Name / Ticker:** TrueNorth / NORTH
- **Theme:** Canadian
- **Smallest unit:** 1 NORTH = 100,000,000 **miettes** (8 decimals), abbreviated `mie`. The base unit is the miette.
- **Base codebase:** fork of Bitcoin Core

## 2. Consensus & Proof of Work
- **Consensus:** Proof of Work
- **Algorithm:** RandomX. CPU-optimized and ASIC-resistant by design,
  battle-tested on Monero since 2019. Integrated as a bundled library into the
  Bitcoin Core fork. Replaces SHA-256d as the block-header PoW hash.
- **Customized variant ("RandomNorth"):** RandomX with its Argon2 salt changed to
  `TrueNorth/RandomNorth/v1` (`src/randomx/src/configuration.h`). Every other
  RandomX constant is upstream's. The salt feeds dataset construction, so stock
  RandomX / xmrig hashpower computes a different proof of work and cannot be aimed
  at this chain without a purpose-built miner.
- **Seed-key rotation:** the RandomX key changes every 2048 blocks (~2.84 days),
  with a 64-block activation lag so miners can precompute the next dataset
  (Monero's parameters; `src/truenorth/seed_key.h`).
- **Modes:** mining uses fast mode (~2 GiB dataset); node block-verification uses
  light mode (~256 MiB cache), so verification stays lightweight.
- **Block time target:** 120 seconds (262,800 blocks/year)
- **Coinbase maturity:** 100 blocks — deliberately above the 72-block reorg cap
  (§9), so a coinbase only becomes spendable once no reorg the network will
  accept can remove the block that created it.

## 3. Emission & Monetary Policy
- **No premine, no founder allocation.** Genesis coinbase is unspendable;
  emission begins at block 1, open to all. No dev fee: the coinbase carries a
  single output and consensus caps its amount, never its recipient.
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

- **Main emission (~24 yrs):** 1,059,609,600 NORTH
- **Tail emission (yr 24+):** 8 NORTH/block, about 2.1M per year, perpetual (~0.2% inflation, declining)
- `MAX_MONEY` sanity constant set well above any reachable supply (no hard cap)

## 4. Difficulty Adjustment
- **Algorithm:** LWMA-1 (Linear Weighted Moving Average), retargets every block
- **Window:** N = 90 blocks (~3 hours)
- **Launch `powLimit`:** permissive, so initial blocks mine quickly on CPUs

## 5. Network Parameters
All values live in `src/kernel/chainparams.cpp`.
- **P2P port:** 9555
- **RPC port:** 9554
- **Network magic bytes:** `fa c4 b8 d2`
- **Address prefixes:**
  - P2PKH version byte: **53** — legacy addresses always begin with **N**
  - P2SH version byte: **65** — P2SH addresses begin with **T**
  - WIF secret-key byte: **180** — compressed WIF keys begin with **T**
  - Bech32 HRP: `north`. Native-segwit addresses look like `north1q...`;
    P2QRH addresses (§7) look like `north1z...`
- **BIP32 extended-key prefixes:** `0x7E5D5A83` / `0x7E5C6544` — extended keys encode as `Tpub...` / `Tprv...`
- **BIP32 coin type (SLIP-44):** **1867** on mainnet — the year of Confederation —
  and 1 on test chains. Wallet descriptors derive at
  `m/<purpose>h/1867h/0h/<0|1>/<index>` (purpose 44/49/84/86 as usual, 88 for
  P2QRH). Deliberately not Bitcoin's 0: a shared coin type would derive identical
  keys from the same seed on both chains.

## 6. Genesis Block
- **Coinbase message:** `2026-10-05 Canada honours its top educators with Prime Minister's Awards - Canada.ca`
  — a Government of Canada news release dated 2026-10-05, as timestamp proof
  (Bitcoin-genesis style). Defined in `src/truenorth/genesis_spec.h`.
- **Genesis reward:** unspendable `OP_RETURN` (enforces no premine)
- **Hash:** `470c8b8ae824c06575cfae0851282c495b861829211194842590dc6a2c829981`
- **Merkle root:** `2e4de71a24b24ac64cea9bbc52cb6a588c9a0bb53b6745a603240fd9efc6b289`
- **nTime:** `1791763200` (2026-10-12 00:00:00 UTC) · **nBits:** `0x1e1179ec` · **nNonce:** `741213`

## 7. Inherited-feature Decisions and Additions
- **SegWit & Taproot:** keep. Both are standard, active Bitcoin Core features
  (bech32 / bech32m addresses, smaller transactions).
- **Relay/mempool policy:** inherit Bitcoin Core defaults.
- **P2QRH (added):** a witness-v2 output type committing to
  `SHA256("TrueNorth/QRH/v1" || scheme_id || pubkey || script_root)` rather than
  to a public key, so unspent coins do not expose their keys. A one-byte
  `scheme_id` selects the signature verifier; only `0x01` (BIP340 Schnorr) is
  valid today, and post-quantum schemes are reserved for activation by soft fork
  without a new address format. Active from genesis on every chain, and the
  wallet default address type. Full specification:
  [`doc/p2qrh.md`](doc/p2qrh.md).

## 8. Supply at a Glance
- Year 1: ~135M NORTH (12.5% of pre-tail; matches Bitcoin's year-1 share)
- Year 24: ~1.06B NORTH (main emission complete)
- After: ~2.1M NORTH/year, perpetual (~0.2% inflation, declining)

## 9. Security & 51%-Attack Resistance

**Threat.** A party controlling more than 50% of hashrate can rewrite recent history
to double-spend. New coins are vulnerable. Total hashrate is low, and hashpower for
common algorithms can be rented cheaply.

**In place today:**
- **RandomX (CPU-optimized, ASIC-resistant by design).** Battle-tested on Monero
  since 2019. ASIC resistance is a design goal rather than a proof: RandomX ASICs
  have been built (the iPollo X1), and the aim is to hold any specialized-hardware
  advantage small enough that commodity CPUs stay competitive, not to make such
  hardware impossible.
- **Customized "RandomNorth" variant.** The changed salt isolates this chain's PoW
  from stock RandomX / xmrig hashpower. Only purpose-built miners can mine or
  attack it.
- **LWMA difficulty (per-block).** Defeats "hash-and-run" difficulty-stranding attacks.
- **Reorg-depth cap.** `max_reorg_depth = 72` on mainnet, testnet3, testnet4 and
  signet (0 on regtest). A node refuses to disconnect more than 72 blocks from its
  own tip, checked in `ActivateBestChainStep` (`src/validation.cpp`). It protects
  a node that already holds a tip; a node syncing from scratch has none to defend.
  A refused reorg partitions the network and is resolved by operator action.
- **Launch-time rule.** Mainnet consensus rejects any block whose timestamp is
  earlier than `nLaunchTime` (`1791763200`, 2026-10-12 00:00:00 UTC), checked in
  `ContextualCheckBlockHeader` (`src/validation.cpp`). The rule constrains block
  timestamps; it does not establish when the work for a block was done. Not
  enforced on testnet, signet or regtest (`nLaunchTime = 0`).
- **Checkpoint mechanism.** Inherited from Bitcoin Core. As of v30.5.1 mainnet
  carries only the genesis entry; post-launch checkpoints are added at release
  time per [`doc/checkpoint-process.md`](doc/checkpoint-process.md).
- **Exchange guidance.** A fixed deposit depth of 100 confirmations, above the
  reorg cap and independent of hashrate. See
  [`doc/exchange-listing-guidance.md`](doc/exchange-listing-guidance.md) and
  [`doc/reorg-economics.md`](doc/reorg-economics.md).

**Planned:**
- **Notarization to a larger chain (delayed-PoW style).** Periodically commit
  TrueNorth block hashes into a high-hashrate chain so any reorg past a notarized
  point is rejected, including for a node syncing from scratch. Design only — no
  implementation and no committed date. See
  [`doc/notarization-design.md`](doc/notarization-design.md).
- **Documented emergency-response plan.** Coordinated checkpoint or node update to
  reject a malicious deep reorg (the real post-attack remedy).

**Residual risk & tradeoffs:**
- While hashrate is low, the measures above carry more of the security than
  proof of work does.
- Cloud-CPU rental remains a long-term vector. RandomX has some specialized hardware
  (e.g. iPollo X1) but its efficiency advantage over CPUs is small.
- **Merge-mining**, the strongest small-coin defence, was deliberately rejected. It
  ties mining to ASICs or a parent ecosystem and breaks the "anyone can mine" goal.
- There's **no after-the-fact "counter-mine to reverse" cure.** Counter-mining is
  itself a 51% attack and would prove the chain mutable. Defence is prevention plus
  instant tools (checkpoints), not hashpower retaliation.

## Open Items
- Post-launch checkpoints (§9): none shipped yet.
- Notarization (§9): design only.
- Post-quantum signature schemes for P2QRH (§7): reserved `scheme_id`s, not activated.

## Build Outline (complete)
Fork Bitcoin Core. Integrate RandomX with the customized "RandomNorth" variant and
seed-key rotation. Patch the reward function for tail emission. Replace the
difficulty algorithm with LWMA. Add P2QRH. Set network parameters. Generate genesis
block. Launch testnet. Public mainnet launch.
