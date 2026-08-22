# TrueNorth Core

A Canadian-themed cryptocurrency forked from Bitcoin Core, using **RandomX** (CPU-friendly, ASIC-resistant) proof-of-work and **LWMA-1** per-block difficulty adjustment.

**Status**: Public testnet4 (open to anyone; see [`doc/testnet.md`](doc/testnet.md) to join). Mainnet TBD.

---

## What's different from Bitcoin Core?

| | Bitcoin Core | TrueNorth |
|---|---|---|
| **Proof of work** | SHA256d | RandomX ("RandomNorth"), light-mode, with per-epoch seed-key rotation (every 2048 blocks) |
| **Difficulty adjustment** | 2016-block retarget | LWMA-1, retargets every block over a 90-block window |
| **Block time** | 10 minutes | 2 minutes |
| **Subsidy** | Halves every 210,000 blocks, dies at zero | Halves every 1,051,200 blocks, floors at 8 NORTH (tail emission, perpetual security budget) |
| **Initial reward** | 50 BTC | 512 NORTH |
| **Tickers** | BTC | NORTH (mainnet) / tNORTH (testnet) |
| **Default address type** | P2WPKH (bech32) | **P2QRH** (bech32m, witness v2, quantum-resistant hash commitment) |
| **Mainnet bech32 HRP** | `bc1...` | `north1z...` (P2QRH default) |
| **Testnet bech32 HRP** | `tb1...` (testnet4) | `tnorth41z...` (testnet4, P2QRH default) |
| **Mainnet P2P / RPC port** | 8333 / 8332 | 9555 / 9554 |
| **Testnet4 P2P / RPC port** | 48333 / 48332 | 49555 / 49554 |
| **Magic bytes (mainnet)** | `f9 be b4 d9` | `fa c4 b8 d2` |
| **Magic bytes (testnet4)** | `1c 16 3f 28` | `fa c4 b8 d4` |

A TrueNorth node and a Bitcoin node will refuse to peer with each other. The magic bytes, ports, address prefixes, and genesis block are all distinct.

---

## Pre-built binaries

Tagged releases produce binaries on GitHub Releases. See https://github.com/truenorth-project/truenorth/releases. Each release currently ships three archives:

- `truenorth-<tag>-linux-x86_64.tar.gz` — Linux x86_64 (glibc)
- `truenorth-<tag>-macos-arm64.tar.gz` — macOS Apple Silicon
- `truenorth-<tag>-windows-x86_64.zip` — Windows x86_64, cross-compiled from Linux with mingw-w64

Every archive ships with a matching `.sha256` — verify before running:

```bash
sha256sum -c truenorth-<tag>-linux-x86_64.tar.gz.sha256
```

Each package unpacks to a directory containing `bin/truenorthd`, `bin/truenorth-cli`, `bin/truenorth-miner` (or their `.exe` counterparts on Windows), a `doc/` directory with tester-relevant documentation, this `README.md`, and `COPYING`.

**Other platforms** (Intel Macs, Linux ARM64, FreeBSD, etc.) are not currently pre-built — build from source (below).

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
cmake --build build -j --target truenorthd truenorth-cli truenorth-miner
```

This produces three binaries:

- `build/bin/truenorthd`: the full TrueNorth node.
- `build/bin/truenorth-cli`: RPC client for talking to a running node.
- `build/bin/truenorth-miner`: standalone solo CPU miner. Talks to `truenorthd` via RPC, hashes with the same RandomX library the node uses.

Building on macOS works too. Replace the apt line with `brew install cmake boost libevent sqlite ccache llvm` and configure with the homebrew LLVM toolchain. See [`doc/build-osx.md`](doc/build-osx.md).

Windows binaries are **cross-compiled from Linux** via mingw-w64 (see the `depends/` system and [`doc/build-windows.md`](doc/build-windows.md)). Native Windows builds are not currently supported — for a Windows build environment, use WSL2 with the Linux instructions above.

---

## Running a node

### Testnet4

The TrueNorth testnet currently runs onion-only — this is a testnet4-specific bootstrapping choice; mainnet will support clearnet peers alongside Tor. See [`doc/testnet.md`](doc/testnet.md) for the seed `.onion` address, a sample tester config, and bootstrap steps.

A bare-minimum startup once the conf is in place:

```bash
./build/bin/truenorthd -testnet4 -daemon
./build/bin/truenorth-cli -testnet4 -rpcwait getblockchaininfo
```

On Windows (from an unpacked release archive, using Command Prompt or PowerShell):

```
bin\truenorthd.exe -testnet4 -daemon
bin\truenorth-cli.exe -testnet4 -rpcwait getblockchaininfo
```

Default ports: P2P **49555**, RPC **49554**. Both bound to `127.0.0.1` by default; pass `-rpcallowip=` or set `rpcauth` if you need remote RPC.

Address example:

```bash
./build/bin/truenorth-cli -testnet4 getnewaddress
# -> tnorth41z...   (P2QRH by default; pass -addresstype=bech32 for P2WPKH)
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
    -chain=test -datadir=<path-to-truenorthd-datadir> \
    -address=<your-tnorth1-address> \
    -threads=<N>
```

`-datadir` is required — the miner reads the RPC cookie file at
`<datadir>/<chain-subdir>/.cookie` for auth. For a remote node, use
`-rpcuser=<user> -rpcpassword=<pass>` explicitly instead (and
`-rpchost=<host> -rpcport=<port>` to point at it).

The miner talks HTTP JSON-RPC to `truenorthd` directly via libevent —
no `truenorth-cli` subprocess. If you have systemd units still passing
`-cli=<path>`, that flag now emits a deprecation warning and is
ignored; remove it at your convenience.

`-threads=N` distributes nonce-grinding across N worker threads, each holding its own RandomX VM. Hashing is lock-free on the hot path; per-thread rate holds within about 5% across 1-4 threads.

**Mode selection** (`-mode=auto|light|fast`, default `auto`):

- **light** — ~256 MiB RandomX cache only. Small footprint, ~5-30 H/s per core.
  Right choice for a Raspberry Pi or a low-RAM VM.
- **fast** — ~2 GiB dataset built at startup (~5-10 s). 5-10× throughput per
  thread. One dataset rebuild per seed-key rotation (~every 2.84 days).
- **auto** (default) — probes available memory and picks `fast` when at least
  2560 MiB is free, else `light`. Fails safe to `light` on unsupported
  platforms.

The node binary (`truenorthd`) always runs in light mode; only `truenorth-miner`
selects a mode. Full nodes keep the small memory footprint.

**Hash-rate sanity check** (no node required):

```bash
./build/bin/truenorth-miner -benchmark=1 -threads=$(nproc) -budgetseconds=10
```

Sample output on an Apple Silicon laptop:

| Threads | Light (aggr H/s) | Light per-thread | Fast (aggr H/s) | Fast per-thread | Speedup |
|---|---|---|---|---|---|
| 1 | 56 | 56 | 583 | 583 | 10.4× |
| 4 | 208 | 52 | 2237 | 559 | 10.8× |
| 8 | 321 | 40 (E-cores past 4 P-cores) | 2541 | 318 | 7.9× |

### Performance tuning: huge pages and NUMA

RandomX benefits substantially from two OS-level configurations. Both
are opt-in but recommended for serious mining rigs.

**Huge pages** (`-largepages=auto|on|off`, default `auto`) — RandomX's
~2 GiB fast-mode dataset fits in ~1024 huge pages (2 MiB each), which
sit comfortably in the CPU's TLB and avoid a page-walk on every dataset
lookup. Measured improvement: **~10-20% hashrate** in fast mode.

Linux setup (once per boot; persist across boot via `/etc/sysctl.d/`):

```bash
# Reserve 1250 huge pages of 2 MiB each (~2.5 GiB, covers dataset + cache
# + headroom for one active seed epoch).
sudo sysctl -w vm.nr_hugepages=1250
```

Verify:

```bash
grep HugePages /proc/meminfo
# HugePages_Total: 1250
# HugePages_Free:  1250   (before the miner runs)
```

Windows setup: enable "Lock pages in memory" (SeLockMemoryPrivilege) via
`gpedit.msc` → Local Computer Policy → Windows Settings → Security
Settings → Local Policies → User Rights Assignment. Add the user that
runs `truenorth-miner.exe`. Requires a re-login to take effect.

The miner's startup log shows `largepages=auto|on|off` and each Cache
constructor logs a fallback message if huge pages were requested but
unavailable — check the log to confirm the mode you expected is in
effect.

**NUMA thread pinning** (`-numa=auto|on|off`, default `auto`) — on
multi-socket systems (dual-Xeon, dual-EPYC), memory on the "wrong"
socket is 2-3× slower to access. NUMA-aware operation allocates one
cache + dataset per socket and pins worker threads to their local
socket. Measured improvement on dual-socket Xeon: **~20-40% hashrate**.

Linux setup: install libnuma (`libnuma-dev` on Debian/Ubuntu,
`numactl-devel` on RHEL/Fedora) BEFORE building. If libnuma isn't
present at build time, NUMA support compiles as no-op stubs and
`-numa=on` will log a warning.

No manual configuration otherwise — the miner detects topology at
startup, distributes worker threads across nodes round-robin, and pins
each thread to its assigned node before allocating memory. Single-
socket systems automatically skip NUMA logic.

Memory cost on dual-socket fast mode: ~2× the single-node footprint
(one dataset per node), i.e. ~5 GiB total. Check `free -h` before
enabling on a memory-constrained box.

The miner's startup log shows `numa=auto(active=yes,nodes=2)` or
similar — `active=yes` means per-node caches are in use;
`active=no` means the topology or configuration disabled it.

### Other miners

The protocol is RandomX-based, so any miner that supports RandomX with a custom seed-key derivation could be adapted. There's no stratum pool yet; that's external infrastructure not part of this repo. See [`doc/mining-policy.md`](doc/mining-policy.md) for the project's stance on pool topology (short version: solo and P2Pool preferred, centralized pools welcome but with eyes open on the tradeoffs).

---

## Quantum resistance

TrueNorth ships mainnet with **P2QRH (Pay-to-Quantum-Resistant-Hash) as the default wallet address type**. The on-chain scriptPubKey commits to a 32-byte SHA256 hash of `(scheme_id, pubkey, script_root)` rather than the pubkey itself.

- **Coins-at-rest are quantum-safe.** A cryptographically-relevant quantum computer cannot recover the pubkey from an unspent P2QRH output — the hash commitment stands between the attacker and the pubkey.
- **Signature scheme is not pre-committed.** Launch uses secp256k1 Schnorr under the commitment (same signature crypto as Taproot, hidden behind the hash). Post-quantum schemes — ML-DSA, Falcon, SLH-DSA — get reserved `scheme_id` bytes and can be added via soft fork **without disturbing existing UTXOs or changing the address format**.
- **Address format is fixed forever.** `north1z…` on mainnet, `tnorth41z…` on testnet, `bcrt1z…` on regtest. Adding PQ signatures later does not require users to migrate addresses.

Bitcoin will eventually need a PQ transition and will spend years debating one because of its legacy pubkey-exposing outputs. TrueNorth launches with none of that debt: no historical P2QRH outputs to preserve, no legacy commitment format to grandfather. See [`doc/p2qrh.md`](doc/p2qrh.md) for the full protocol spec — output format, scheme registry, sighash, activation, and rationale for every design decision.

Opt out for a specific address if you need P2WPKH for interop with older tooling: `getnewaddress "" bech32`.

---

## Documentation

| File | What |
|---|---|
| [`doc/p2qrh.md`](doc/p2qrh.md) | P2QRH (witness v2) quantum-resistant output type: spec, scheme registry, activation |
| [`doc/testnet.md`](doc/testnet.md) | Testnet4 bootstrap: seed `.onion`, sample config |
| [`test/truenorth/`](test/truenorth/) | End-to-end regression scripts (sync, multitx, seed rotation, reorg, IBD, QRH) |
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
- **Quantum-resistant by default**: P2QRH addresses commit to a hash of the pubkey, not the pubkey itself; post-quantum signature schemes plug in via soft fork without disturbing existing UTXOs.
- **Canadian-themed**: a piece of flavor.

Not a meme launch. No presale. No premined treasury. Just a CPU-mineable coin with Bitcoin-style monetary policy plus tail emission.
