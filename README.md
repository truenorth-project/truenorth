# TrueNorth Core

A Canadian-themed cryptocurrency forked from Bitcoin Core, using **RandomX** (CPU-friendly, ASIC-resistant) proof-of-work and **LWMA-1** per-block difficulty adjustment.

**Status**: Private testnet. Mainnet TBD. If you found this repo and want to test, open a GitHub Issue.

---

## What's different from Bitcoin Core?

| | Bitcoin Core | TrueNorth |
|---|---|---|
| **Proof of work** | SHA256d | RandomX ("RandomNorth"), light-mode, with per-epoch seed-key rotation (every 2048 blocks) |
| **Difficulty adjustment** | 2016-block retarget | LWMA-1, retargets every block over a 90-block window |
| **Block time** | 10 minutes | 2 minutes |
| **Subsidy** | Halves every 210,000 blocks, dies at zero | Halves every 525,600 blocks, floors at 8 NORTH (tail emission, perpetual security budget) |
| **Initial reward** | 50 BTC | 1024 NORTH |
| **Tickers** | BTC | NORTH (mainnet) / tNORTH (testnet) |
| **Mainnet bech32 HRP** | `bc1...` | `north1...` |
| **Testnet bech32 HRP** | `tb1...` | `tnorth1...` |
| **Mainnet P2P / RPC port** | 8333 / 8332 | 9555 / 9554 |
| **Testnet3 P2P / RPC port** | 18333 / 18332 | 19555 / 19554 |
| **Magic bytes (mainnet)** | `f9 be b4 d9` | `fa c4 b8 d2` |
| **Magic bytes (testnet3)** | `0b 11 09 07` | `fa c4 b8 d3` |

A TrueNorth node and a Bitcoin node will refuse to peer with each other. The magic bytes, ports, address prefixes, and genesis block are all distinct.

---

## Pre-built binaries

Tagged releases produce binaries on GitHub Releases. See https://github.com/truenorth-project/truenorth/releases. Currently published:

- `truenorth-<tag>-linux-x86_64.tar.gz` (with matching `.sha256`)

Each tarball unpacks to a directory containing `bin/truenorthd`, `bin/truenorth-cli`, `bin/truenorth-miner`, this `README.md`, and `COPYING`.

**macOS, Windows, other platforms**: no pre-built binary yet. Build from source (below). macOS release binaries are deliberately disabled in the release workflow for now because the macOS CI runner is the 10× billing tier; they may be re-enabled later.

## Build from source

If you want to build yourself instead of using a pre-built binary:

Dependencies (Ubuntu / Debian):

```bash
sudo apt-get install build-essential cmake pkg-config \
                     libboost-dev libevent-dev libsqlite3-dev \
                     ccache
```

Build:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j --target truenorthd bitcoin-cli truenorth-miner
```

This produces three binaries:

- `build/bin/truenorthd`: the full TrueNorth node.
- `build/bin/truenorth-cli`: RPC client for talking to a running node.
- `build/bin/truenorth-miner`: standalone solo CPU miner. Talks to `truenorthd` via RPC, hashes with the same RandomX library the node uses.

Building on macOS works too. Replace the apt line with `brew install cmake boost libevent sqlite ccache llvm` and configure with the homebrew LLVM toolchain. See [`doc/build-osx.md`](doc/build-osx.md).

---

## Running a node

### Testnet3

```bash
./build/bin/truenorthd -testnet=3 -daemon -dnsseed=1 -listen=1
```

Default ports: P2P **19555**, RPC **19554**. Both bound to `127.0.0.1` by default; pass `-rpcallowip=` or set `rpcauth` if you need remote RPC.

Once running:

```bash
./build/bin/truenorth-cli -testnet=3 getblockchaininfo
./build/bin/truenorth-cli -testnet=3 createwallet mywallet
./build/bin/truenorth-cli -testnet=3 getnewaddress
# -> tnorth1q...
```

### Mainnet

Not yet launched. Don't run anyone's "mainnet" binary that you can't trace back to this repository's tagged releases.

### Regtest (local development)

```bash
./build/bin/truenorthd -regtest -daemon
./build/bin/truenorth-cli -regtest generatetoaddress 1 $(./build/bin/truenorth-cli -regtest getnewaddress)
```

Used by the regression scripts under [`test/truenorth/`](test/truenorth/).

---

## Mining

### Solo mining with truenorth-miner

```bash
./build/bin/truenorth-miner \
    -chain=test -datadir=<path-to-bitcoind-datadir> \
    -address=<your-tnorth1-address> \
    -threads=<N>
```

`-threads=N` distributes nonce-grinding across N worker threads, each holding its own RandomX VM. Hashing is lock-free on the hot path; per-thread rate holds within about 5% across 1-4 threads.

**Hash-rate sanity check** (no node required):

```bash
./build/bin/truenorth-miner -benchmark=1 -threads=$(nproc) -budgetseconds=10
```

Sample output on an Apple Silicon laptop:

| Threads | Aggregate H/s | Per-thread |
|---|---|---|
| 1 | 50.5 | 50.5 |
| 4 | 189.5 | 47.4 |
| 8 | 291.0 | 36.4 (E-cores past 4 P-cores) |

### Other miners

The protocol is RandomX-based, so any miner that supports RandomX with a custom seed-key derivation could be adapted. There's no stratum pool yet; that's external infrastructure not part of this repo.

---

## Documentation

| File | What |
|---|---|
| [`test/truenorth/`](test/truenorth/) | End-to-end regression scripts (sync, multitx, seed rotation, reorg, IBD) |
| [`doc/`](doc/) | Inherited Bitcoin Core docs. Build instructions per platform live here; consensus and RPC reference notes also apply largely unchanged |

---

## License

MIT. See [`COPYING`](COPYING). RandomX is vendored under [`src/randomx/`](src/randomx/) and licensed under the original tevador/RandomX terms (also MIT-compatible).

---

## Why does this exist?

Bitcoin's architecture isn't the only way. Block time, PoW algorithm, subsidy schedule: all of those were design choices. TrueNorth makes a few different ones and keeps the rest of the Bitcoin protocol intact. The aim:

- **CPU-mineable**: anyone with a laptop should be able to participate.
- **ASIC-resistant**: RandomX is intentionally hostile to specialized hardware.
- **Perpetual security**: tail emission keeps miners paid past the last halving.
- **Faster confirmations**: 2-minute blocks for usability without sacrificing security relative to other small PoW coins.
- **Canadian-themed**: a piece of flavor.

Not a meme launch. No presale. No premined treasury. Just a CPU-mineable coin with Bitcoin-style monetary policy plus tail emission.
