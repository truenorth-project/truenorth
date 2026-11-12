# TrueNorth Core v30.5.4

Fixes the initial headers sync, which on a slow machine never finished on a
fresh mainnet node, and a miner defect that cost hashrate after every RandomX
seed rotation. It also stops the node taking huge pages the miner needs and
lets a fresh testnet4 node find peers on its own. Upgrading is recommended for
everyone, and especially for anyone syncing a new node or running a node and a
miner on the same machine. It adds a mainnet checkpoint at height 20,000 and
the first mainnet DNS seed.

v30.5.3 was tagged but not released, because its higher minimum chain work
would have made the sync problem worse. Everything it contained is in this
release.

Binaries for linux-x86_64, macos-arm64 and windows-x86_64 are attached, with
`SHA256SUMS` and `SHA256SUMS.asc`.

## Headers sync

A fresh mainnet node now finishes its headers sync on slow hardware (`adac644`,
#36). Since the first checkpoints, a new node checks the work of the whole
header chain before storing any of it, and each header costs a RandomX hash
where Bitcoin's costs a SHA256 one. Three things inherited from Bitcoin then
combined: the node ran a separate check for every peer, all on the same CPU;
the sync peer was dropped after 15 minutes plus 1 ms per header, and its
replacement started again from height 0; and the second pass over the headers
logged nothing, so the node looked stalled. On a 2 vCPU shared VM the sync
never finished.

The timeout now allows 150 ms per header, only one of these checks runs at a
time, and the second pass logs its progress. On one slow core, headers reach
the tip in about 30 minutes.

On v30.5.2, starting the first sync with `-minimumchainwork=0` works around
the problem. Remove it once the node has synced.

Reported in #36 by RuojiRuoli, with logs and an analysis that pointed at the
cause.

## Mining

- **The miner releases the old dataset at a seed rotation** (`2d22620`, #31).
  Until now the new dataset was allocated while the old one was still held,
  so on a machine with huge pages reserved for one dataset the new one fell
  back to normal pages. The miner then ran with about 2 GiB of extra memory
  and 30-55% less hashrate until it was restarted. That happened every 2,048
  blocks, at heights 64 above a multiple of 2,048. The old dataset is now
  freed first, and the switch costs a few seconds of hashing. If huge pages
  are still unavailable at a rotation, the miner now logs a warning instead
  of an informational line.
- **The node no longer uses huge pages** (`0f66852`). `truenorthd` tried them
  for its validation caches by default and could take up to 512 MiB of a
  reservation sized for the miner's dataset, which then fell back to normal
  pages. The node gains little from them, so it now leaves them alone.
  `truenorth-miner -largepages` is unchanged and still defaults to `auto`.

Reported in #31, which includes a test of the fix across a mainnet rotation
with two unpatched miners as controls.

## Checkpoint

Mainnet adds a checkpoint at height 20,000 (`d82e32d`), alongside 5,000,
10,000 and 15,000. Minimum chain work and assumevalid move to the same block.
The values were read from independently synced nodes, which agreed. The
current chain matches every checkpoint, so upgrading does not change which
chain any node follows.

## DNS seed

Mainnet has its first DNS seed, `seed.tor.tncoin.xyz`, run by robert-pathy
(`b9b95e1`). A fresh node now finds peers from it as well as from the
compiled-in list. Nodes with `onlynet=onion` skip DNS seeds, as before, and
keep bootstrapping from the onion fixed seeds.

## Testnet4

A fresh testnet4 node now finds peers from the compiled-in seed list
(`a266736`). On v30.5.2 and earlier that list was never used, and testnet4 has
no DNS seeds, so a new node without `addnode=` lines stayed at zero
connections. The documented onion seed is added to the list.

## Regtest (developers)

Regtest has its own address and key prefixes instead of Bitcoin's
(`5b6c04a`): bech32 HRP `rnorth`, P2PKH and P2SH addresses starting with `r`
(versions 122 and 123), WIF version 252, and extended-key versions
`043587CB`/`04358390`. A Bitcoin testnet or regtest address no longer decodes
as a TrueNorth regtest one.

Existing regtest datadirs still load, but addresses, keys and descriptors
saved from an earlier version no longer decode. Regenerate them, or start a
fresh regtest chain. Mainnet, testnet4 and signet are unaffected.

## Documentation

- `doc/payment-uri.md` drafts the `truenorth:` payment URI scheme, following
  BIP 321 (#32, from Ironfist-2000).
- `doc/testnet.md` gives the correct testnet4 address prefix, adds
  `fallbackfee` to the example configuration so sends work without fee
  estimation, and gives `addnode=` lines for versions before this one (#35,
  from Ironfist-2000).
- The README lists the community site and subreddit with the other
  community-run resources (#33, from robert-pathy).

## Breaking changes

None on mainnet, testnet4 or signet. Wallets, the P2P protocol, the on-disk
format and the consensus rules are unchanged from v30.5.2, apart from the new
checkpoint. The regtest prefixes above change for developers only.

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
