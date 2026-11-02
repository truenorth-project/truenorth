# TrueNorth Core v30.5.2

Adds the first mainnet checkpoints since launch. Upgrading is recommended for
every node, and especially for seed operators and anyone about to sync a new
node. Nothing else in the node, wallet or miner changes.

Binaries for linux-x86_64, macos-arm64 and windows-x86_64 are attached, with
`SHA256SUMS` and `SHA256SUMS.asc`.

## Checkpoints

Mainnet now carries checkpoints at heights 5,000, 10,000 and 15,000
(`12eab0b`). v30.5.1 had the genesis entry only.

- **Checkpoints.** A v30.5.2 node rejects any chain whose block at one of
  those heights has a different hash, however much work that chain has. Such
  a chain is not accepted at all, rather than winning a reorg.
- **Minimum chain work** is set to the work at block 15,000. A node syncing
  from scratch ignores any chain with less work than that. A low-work chain
  can no longer occupy a new node's initial sync, and can no longer be
  presented to it as the real one.
- **assumevalid** moves to block 15,000. A new node skips script checks for
  blocks below it, so initial sync is faster. Every other check still runs.

The values were read from independently synced nodes, which agreed. A fresh
node built from this release synced the live chain from genesis through all
three checkpoints before the tag.

The current chain matches all three checkpoints, so upgrading does not change
which chain any node follows. Nodes on v30.5.1 and v30.5.0 keep working and
stay in consensus with v30.5.2. They just don't get the added protection.

## Documentation

- `TRUENORTH_SPEC.md` now describes what shipped rather than the pre-launch
  design (`bc6e598`): the real genesis parameters and network magic,
  RandomNorth as a changed Argon2 salt only, P2QRH, the 72-block reorg cap, and
  which of the security measures are in place and which are only planned.
- The comments on `nLaunchTime` give the correct launch time (2026-10-12
  00:00:00 UTC) and describe the rule as constraining block timestamps
  (`26a05b9`).
- `README.md` says the project has no connection to unrelated trading schemes
  using the TrueNorth name (`f0a3bd9`), and lists community-run resources,
  starting with a block explorer, separately from the project's own channels
  (`e75f1b5`).

## Breaking changes

None. Wallets, the P2P protocol, the on-disk format and every consensus rule
other than the checkpoints themselves are unchanged from v30.5.1.

## Known issues

- **Builds are not reproducible.** Nobody, including the maintainer, can
  recompile the tagged source and confirm it yields the published bytes. The
  attestation covers provenance only.
- **The Windows binary is never executed in CI.** It is cross-compiled and the
  workflow has no Wine step, so functional problems on Windows are found by
  users rather than by the build.
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
Verification tools report the **signing subkey**, not the primary fingerprint
above; check the primary with `gpg --fingerprint` after importing.
