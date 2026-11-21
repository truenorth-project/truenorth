# TrueNorth Core v30.5.5

Fixes two faults that made a fresh node's headers sync start over, makes the
macOS binaries run on a Mac without Homebrew, lets the miner use fast mode on
macOS when the memory is there, and stops a headers sync holding the node's
main lock for about a minute per batch. It also gives each P2QRH
witness rule its own reject reason, publishes P2QRH test vectors for wallet
authors, and adds a mainnet checkpoint at height 25,000. Upgrading is
recommended for everyone, and especially for anyone syncing a new node, macOS
users, and anyone building a wallet that spends P2QRH outputs.

Binaries for linux-x86_64, macos-arm64 and windows-x86_64 are attached, with
`SHA256SUMS` and `SHA256SUMS.asc`.

## macOS

- **The binaries no longer need Homebrew** (`ed31481`). The v30.5.4 macOS
  binaries linked libevent from `/opt/homebrew/opt/libevent`, so they failed
  to start unless `brew install libevent` had been run. libevent is now linked
  statically, built from the same version, hash and patches as `depends/`.
  The binaries link only libraries that ship with macOS.
- **macOS 13 or later is required, and now stated** (`ed31481`). The v30.5.4
  binaries were built for the macOS version of the build machine, 26. They
  ran on older versions, but nothing guaranteed it. They are now built for
  macOS 13.0, and the release job fails if any binary targets a different
  version or links a library outside the system.
- **`-mode=auto` sees the memory macOS can free** (`0407b5b`). The miner
  picks fast mode when at least 2,560 MiB is available, and on macOS it
  counted only free and inactive memory, which macOS keeps small by design.
  On a 16 GB Mac with plenty to spare it chose light mode, which hashed
  206 H/s against 742 H/s in fast mode on 8 threads. Available memory is now
  physical memory less app memory, wired memory and compressed memory, as
  Activity Monitor counts it.

  Memory a running virtual machine has reserved is wired and really is
  unavailable, so a Mac running one may still get light mode. Pass
  `-mode=fast` to override auto mode on any platform.
- The huge-pages notice the miner prints on every macOS run named only the
  Linux and Windows settings. It now says there is nothing to configure on
  macOS (`0407b5b`).

## Headers sync

A fresh node first checks the work of the whole header chain, then downloads
the headers a second time to store them, and stores nothing until it is about
15,000 headers into that second pass. Two faults made that sync start over,
and the higher minimum chain work in this release would have made both worse.
Before the tag, a fresh v30.5.5 node on one core synced mainnet from genesis
with three peers in 53 minutes, in a single headers sync.

- **A block announcement no longer aborts the sync** (`546c92e`). Once the
  second pass started storing headers, a block announcement could make the
  node send a second request to its sync peer, and the reply to that request
  aborted the sync. Each restart kept only the headers stored so far, and on
  one slow core a fresh mainnet node needed three attempts.
- **The sync peer is no longer dropped as behind** (`db6864a`). An outbound
  peer that has shown no header with as much work as the node's tip within
  20 minutes (plus 2 for a final request) is disconnected. A peer serving the
  initial headers sync shows none until headers are stored, which on one slow
  core took longer than that, so the node dropped the peer and started again
  with another. The initial sync peer is now kept while its sync is making
  progress. It is still dropped if the sync stalls for 10 minutes, and by the
  headers sync timeout.
- **Headers are no longer RandomX-hashed twice** (`343f091`). Each header in
  a batch was hashed once on arrival and again under the node's main lock. At
  20-30 ms a hash, a 2,000-header batch held that lock for about a minute, and
  every RPC call waited for it. The second hash is now skipped for a header
  that already passed against the same seed. Any other header is hashed as
  before.
- **The initial headers sync peer is never held back by another peer**
  (`cbae6f4`, #36). While one peer's low-work headers sync made progress,
  every other peer's was deferred, including the initial sync peer's. A peer
  serving a cheap low-work chain could keep doing that until the sync
  timeout disconnected the initial sync peer, then the next one. The initial
  sync peer is now exempt, and it is still bounded by the sync timeout.

## P2QRH

- **Each witness rule has its own reject reason** (`ed6d245`). Every P2QRH
  key-path failure except an empty witness and a bad signature was reported
  as `Witness program hash mismatch`, whatever rule the spend broke. There
  are four new script errors, for a witness with the wrong number of
  elements, a scheme id that is not one byte, a scheme id that is not
  active, and a public key of the wrong size. A public key that does not
  match the commitment keeps `Witness program hash mismatch`.
  `doc/p2qrh.md` lists the error for each rule.
- **Test vectors for wallet authors** (`7cb2456`, `db69e4e`).
  `src/test/data/p2qrh_wallet_vectors.json` covers what a wallet has to match for a
  key-path spend: commitments, scriptPubKeys and addresses on every chain,
  addresses to reject, BIP32 derivation on the `88h` path, signed spends for
  every sighash type with the BIP-341 intermediate values, and witnesses
  consensus must reject. The unit tests check every value against TrueNorth
  Core's own code. `doc/p2qrh.md` links an independent set, maintained by
  severka-monitor, that was checked against a running node; the two sets agree
  everywhere they overlap.
- `doc/p2qrh.md` describes the annex. An optional annex is accepted by
  consensus and refused by policy, as in BIP-341, and wallets must not add one.
  Scheme id `0x50` can never be used, because a top witness element starting
  with `0x50` is read as an annex (`db69e4e`).

## Checkpoint

Mainnet adds a checkpoint at height 25,000 (`66d1c8d`), alongside 5,000,
10,000, 15,000 and 20,000. Minimum chain work and assumevalid move to the same
block. The values were read from independently synced nodes, which agreed. The
current chain matches every checkpoint, so upgrading does not change which
chain any node follows.

## Breaking changes

- **P2QRH reject reasons.** A P2QRH spend rejected for one of the four rules
  above now gets the new reason instead of `Witness program hash mismatch`.
  Software that matches on reject text, from `sendrawtransaction` or
  `testmempoolaccept`, needs updating. Which spends are valid has not
  changed.
- **macOS 13** is the oldest macOS the release binaries support.

Wallets, the P2P protocol, the on-disk format and the consensus rules are
otherwise unchanged from v30.5.4, apart from the new checkpoint.

## Verification

Verify both the checksums and the signature before running anything:

```bash
sha256sum --ignore-missing -c SHA256SUMS
gpg --verify SHA256SUMS.asc SHA256SUMS
```

On macOS, `shasum -a 256 --ignore-missing -c SHA256SUMS` replaces the first
line.

The release key's primary fingerprint is

    3F0BAE04 5861 5427 B8CA 935E 3E24 2DF5 E447 8AE5

and the public key is in the repository at `contrib/keys/septentrion-dev.asc`.
Verification tools report the **signing subkey**, not the primary fingerprint
above; check the primary with `gpg --fingerprint` after importing.
