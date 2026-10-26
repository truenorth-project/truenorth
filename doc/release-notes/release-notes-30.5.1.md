# TrueNorth Core v30.5.1

A maintenance release. Two miner fixes, one build-requirement change, and a
large documentation pass. **No consensus changes**, so there is nothing a node
or miner operator must do beyond upgrading at their convenience.

Binaries for linux-x86_64, macos-arm64 and windows-x86_64 are attached, with
`SHA256SUMS` and `SHA256SUMS.asc`.

## Fixes

- **The miner now starts on Windows.** `truenorth-miner` initialises Winsock
  before libevent, which it previously did not, so every invocation on Windows
  failed during startup (`51392d5`). The Windows artifact is cross-compiled
  with mingw-w64 on a Linux runner and is never executed in CI, which is why
  this shipped in v30.5.0 — the build attestation proves provenance, not that
  the binary runs. Confirmed fixed on Windows 10 against the `v30.5.1-rc1`
  build before this release was tagged.
- **An RPC timeout is no longer reported as an unreachable daemon.** The miner
  distinguishes the two, so a long-poll that times out while waiting for a new
  block template no longer prints a message claiming it could not connect
  (`5bf1952`). The daemon was reachable throughout; only the diagnostic was
  wrong.

## Build

- **libnuma is now a hard dependency on Linux** (`47bd10a`). It was previously
  discovered with `find_library`, which silently produced a binary with NUMA
  support stubbed out if the development package was absent — and because
  CMake caches that result, installing the package afterwards did not fix the
  build. Install `libnuma-dev` (Debian/Ubuntu) or `numactl-devel` (Fedora/RHEL)
  before configuring. The release workflow verifies the published artifacts
  report NUMA support rather than assuming it.

## Documentation

The shipped documentation is inherited from Bitcoin Core and large parts of it
still described that project rather than this one. This release corrects the
substance of that, not just the names:

- Documentation no longer refers to this software as Bitcoin Core (`1ca8ff2`).
  Two of those were broken instructions rather than cosmetic: the
  configuration-file reference told the reader to copy an example file that
  does not exist under that name, and `CONTRIBUTING.md` directed contributors
  to an IRC channel belonging to another project on a different network.
- `doc/reorg-economics.md` now gives worked examples across a range of network
  sizes instead of quoting one measured hashrate (`4d2e120`). The arithmetic is
  unchanged; it no longer goes stale as the network moves.
- `doc/dnsseed-policy.md` states this project's expectations and contact path
  rather than upstream's.
- `doc/tor.md` covers peer discovery over Tor (`134b332`), and the documented
  P2QRH address length is correct (`85b2a85`).
- `README.md` states which channels are official and which do not exist, points
  security reports at `SECURITY.md`, and names the project website
  (`f737068`, `61aa891`).
- `doc/release-process.md` describes the process this project actually follows,
  including the local regression scripts that are deliberately absent from CI.

- **The example configuration file is correct for this chain.**
  `share/examples/truenorth.conf` is generated from the daemon's own `--help`
  output, and the generator had been looking for an upstream binary name since
  the rename, so it exited without doing anything and nobody noticed. The file
  shipped in v30.4.1 and v30.5.0 therefore documented Bitcoin's values: P2P port
  8333 rather than 9555, RPC port 8332 rather than 9554, onion bind port 8334
  rather than 9556, Bitcoin's `assumevalid` block hashes, a default
  `addresstype` of `bech32` rather than `bech32m-qrh`, a pruning minimum of 550
  rather than 1500, and a block of ZeroMQ options that the released binaries do
  not contain. All corrected.

## Breaking changes

None for node or miner operators. Wallets, the P2P protocol, consensus rules
and the on-disk format are unchanged from v30.5.0, and v30.5.0 nodes interop
with v30.5.1 nodes.

Builders on Linux need `libnuma-dev` present before configuring, per **Build**
above.

## Known issues

- **Builds are not reproducible.** Nobody, including the maintainer, can
  recompile the tagged source and confirm it yields the published bytes. The
  attestation covers provenance only.
- **The Windows binary is never executed in CI.** It is cross-compiled and the
  workflow has no Wine step, so functional problems on Windows are found by
  users rather than by the build. The fix in this release is an instance of
  that.
- **RandomX targets ASIC resistance, not ASIC proofness**, and RandomX ASICs
  exist.
- **The network is small**, so confirmation depth matters considerably more
  than it does on Bitcoin. Consensus refuses any reorg deeper than 72 blocks;
  see `doc/exchange-listing-guidance.md` for deposit guidance and
  `doc/reorg-economics.md` for the arithmetic behind it.

## Verification

Verify both the checksums and the signature before running anything:

```bash
sha256sum --ignore-missing -c SHA256SUMS
gpg --verify SHA256SUMS.asc SHA256SUMS
```

The release key's primary fingerprint is

    3F0BAE04 5861 5427 B8CA 935E 3E24 2DF5 E447 8AE5

and the public key is in the repository at `contrib/keys/septentrion-dev.asc`.
Note that verification tools report the **signing subkey**, not the primary
fingerprint above; check the primary with `gpg --fingerprint` after importing.
