# Joining the TrueNorth testnet

This is the operator runbook for testers who want to run a TrueNorth testnet4
node. The testnet is reachable only via Tor; the seed node lives behind a
hidden service, not a clearnet address.

## What's running

| Parameter | Value |
|---|---|
| Chain | testnet4 |
| Block time | 120 seconds (target) |
| Address HRP | `tnorth41q...` (segwit) |
| P2P port | 49555 |
| RPC port (local-only by default) | 49554 |
| Seed node | `3guf2wvltezb6w3tjjveumtt6lsnb7bjpts6ornk4hj4wlod4s7n53id.onion:49555` |

The testnet runs onion-only. There is no clearnet seed and no DNS seeder
yet. Bootstrapping happens entirely through the `.onion` addnode below.

testnet4 was picked over testnet3 because testnet4's chainparams are
designed for fresh chains: all BIPs activate from genesis, BIP94 timewarp
protection is on, and there's no historical Bitcoin testnet3 baggage to
work around. If you're familiar with Bitcoin Core's testnet3 conventions,
the differences here are: `-testnet4` instead of `-testnet=3`, addresses
prefixed `tnorth4` instead of `tnorth1q`, ports in the 49000s instead of
19000s.

## Requirements

- Linux x86_64 host, macOS arm64 host, or build-from-source on anything else
- Tor 0.4.x or later running locally with SOCKS on 127.0.0.1:9050
- Roughly 1 GiB RAM, 4 GiB disk, any always-on internet connection

The pre-built binaries dynamically link against modern glibc, so an Ubuntu
24.04 (or equivalent) host is the easy path. Older glibc will need source
builds.

## Getting the binary

Either download the tarball from the releases page:

    https://github.com/truenorth-project/truenorth/releases

(verify the included `.sha256` against the tarball before extracting)

Or build from source. The README at the repo root has dependencies and the
cmake invocation. A from-scratch build takes 10-30 minutes depending on CPU.

## Configuration

Create `~/.truenorth/truenorth.conf` (on macOS: `~/Library/Application
Support/TrueNorth/truenorth.conf`):

```
testnet4=1
proxy=127.0.0.1:9050

[testnet4]
addnode=3guf2wvltezb6w3tjjveumtt6lsnb7bjpts6ornk4hj4wlod4s7n53id.onion:49555
onlynet=onion
listen=1
listenonion=0

rpcbind=127.0.0.1
rpcallowip=127.0.0.1
rpcuser=tn_user
rpcpassword=GENERATE_YOUR_OWN
```

Generate a strong RPC password and put it in `rpcpassword=`. The simplest:

    openssl rand -hex 32

`listenonion=0` is correct if you only consume the seed and don't host your
own hidden service. If you want to also accept inbound peers, configure a
Tor `HiddenServiceDir` block in your torrc that maps a port to
`127.0.0.1:49555`, then set `externalip=<your-onion>` in the conf and drop
`listenonion=0`.

## First run

```
./truenorthd -testnet4 -daemon
./truenorth-cli -testnet4 -rpcwait getblockchaininfo
```

The `-rpcwait` flag waits for the daemon to come up before issuing the RPC.
First call after a fresh start may take 10-30 seconds while Tor circuits
establish and the seed peer is reached.

After connection:

```
./truenorth-cli -testnet4 getconnectioncount
./truenorth-cli -testnet4 getbestblockhash
./truenorth-cli -testnet4 getblockcount
```

You should see at least one connection and a best block hash that matches
what the seed reports. Sync time depends on chain length and your Tor
circuit; expect a few minutes for a testnet at meaningful height.

## What to look for and report

Open a GitHub issue at
[github.com/truenorth-project/truenorth/issues/new](https://github.com/truenorth-project/truenorth/issues/new)
for any of:

- A node that fails to connect to the seed (likely a Tor or local config
  issue, but the issue tracker is the right place)
- A node that connects but doesn't sync past a specific height
- A consensus failure: your node has a different best-block hash from the
  seed and neither side reorgs
- A crash or panic on startup, during sync, or while mining
- An RPC method that misbehaves on testnet4 versus its documented behaviour
- Anything around the LWMA difficulty adjustment that looks wrong (sudden
  difficulty cliffs, retarget that fails to track hashrate, etc.)
- Seed-key rotation issues at epoch boundaries (the chain rotates the
  RandomX seed every 2048 blocks with a 64-block activation lag)

When filing, include:

- The version output of `truenorthd --version`
- A short description of the platform (OS / arch)
- Steps to reproduce
- Relevant log output from `~/.truenorth/testnet4/debug.log` (be aware logs
  can contain IP addresses and similar)

## Mining (optional)

If you want to mine on testnet, use `truenorth-miner` against your local
node. Light-mode RandomX is the reference miner; a recent x86_64 laptop CPU
will turn over a few hundred hashes per second per core. The chain's PoW
difficulty floor is permissive, so even a single laptop can keep up at low
hashrate.

```
./truenorth-miner -chain=testnet4 -datadir=$HOME/.truenorth \
                  -cli=$(which truenorth-cli) \
                  -address=$(./truenorth-cli -testnet4 getnewaddress) \
                  -threads=$(nproc) -budgetseconds=300
```

There is no stratum pool. Mining is solo only at this stage.

## Known issues

- The release tarball ships Linux x86_64 only. macOS arm64 is in the release
  workflow but may not be present on every tag. Source builds work on all
  platforms (see `doc/build-*.md`).
- Block 0 through about block 2113 exercises the genesis-epoch seed (kept at
  zero); first rotation happens at the boundary. Rotation transitions are
  visible in `debug.log` if you watch for them.
- The chain has no DNS seeds. If the operator's seed node goes offline,
  inter-peer discovery falls back to whatever each tester has in their
  `addnode=` list. Operationally this means: if the seed drops for a while,
  you'll keep talking to whoever you already know about.
- `fPowAllowMinDifficultyBlocks=true` on testnet4: if no block has been
  found in 240 seconds (2x target spacing), the next block can be mined at
  minimum difficulty. This keeps the chain from getting stuck at LWMA-set
  difficulty when there's only one miner. It also causes visible
  oscillation in block times until hashrate stabilises.
