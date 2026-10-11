Release Process
====================

## Before tagging

There are no release branches. The maintainer commits directly to `master` and
tags from there, so everything below happens on `master`.

* Set the version in [`CMakeLists.txt`](/CMakeLists.txt). Release candidates
  additionally set `CLIENT_VERSION_RC`.
* Regenerate the man pages after rebuilding the binaries, see
  [gen-manpages.py](/contrib/devtools/README.md#gen-manpagespy). They carry the
  version in their `.TH` header and body; skipping this ships man pages
  advertising the previous release.
* Regenerate the example config, see
  [gen-bitcoin-conf.sh](/contrib/devtools/README.md#gen-bitcoin-confsh). The
  generated file records the daemon's own default values, so it goes stale
  whenever a default changes.
* Update the fixed seeds if the reachable set has changed, see
  [contrib/seeds](/contrib/seeds/README.md). A fixed seed pointing at a
  renumbered host cannot be corrected without a new release.
* Write the release notes under `doc/release-notes/`. List breaking changes
  even when there are none, and keep known issues honest.
* Run [`contrib/devtools/check-upstream-identifiers.py`](/contrib/devtools/check-upstream-identifiers.py)
  and confirm nothing new has appeared on the shipped surface.
* Check that `doc/` contains no instruction that would send a reader to
  upstream Bitcoin Core infrastructure. The documentation is inherited, so this
  regresses rather than staying fixed.

This project does not use Transifex, does not maintain translations, and has no
release-candidate testing meta-issue.

### Tagging

Tags are signed. The signing subkey lives on a machine separate from the
working checkout (see [`SECURITY.md`](/SECURITY.md)), so the tag is created
there and the ref moved across rather than signed in place. GPG needs a TTY for
pinentry, so this is an interactive step.

Keep commit and tag timestamps in the project's historical timezone so the
history stays consistent.


## Building

Releases are built by GitHub Actions from the tagged source, not on a
maintainer's machine. There is no Guix build, no multi-builder signature
plurality, and no detached codesigning step. Pushing a `v*` tag triggers
`.github/workflows/release.yml`, which can also be run manually against an
existing tag via `workflow_dispatch`.

The matrix produces three platforms:

| Platform | Runner | Built via |
|----------|--------|-----------|
| `linux-x86_64` | `ubuntu-latest` | native |
| `macos-arm64` | `macos-latest` | native |
| `windows-x86_64` | `ubuntu-latest` | cross-compiled, `x86_64-w64-mingw32` |

Each job archives `truenorth-<tag>-<platform>.tar.gz` (`.zip` on Windows)
with a `.sha256` beside it, attaches a build provenance attestation binding
the archive to the workflow run and commit, and uploads to the release.

**The builds are not reproducible.** Neither the provenance attestation nor
the GPG signature lets anyone recompile the tag and confirm it yields the
published bytes. Say so in the release notes rather than implying otherwise.

### Push the tag

Pushing the signed tag starts the build. Watch the workflow to green before
touching the release page.


### Sign and publish the checksums

The workflow publishes per-archive `.sha256` files, but `SHA256SUMS` and its
detached signature are produced offline and uploaded afterwards:

1. Download every release archive once the workflow is green.
2. Generate `SHA256SUMS` covering all of them.
3. Sign it with the release subkey on the signing machine, producing
   `SHA256SUMS.asc`. GPG needs a TTY for pinentry, so an interactive session
   is required rather than a piped command.
4. Upload both files to the release.

### Verify the release as a user would

Before announcing, verify the published artifacts the way a stranger will, in
a keyring containing no secret keys:

```bash
gpg --homedir "$(mktemp -d)" --import contrib/keys/septentrion-dev.asc
gpg --homedir ... --verify SHA256SUMS.asc SHA256SUMS
sha256sum -c SHA256SUMS
```

Checking that the signature validates is not the same as checking that the
published instructions lead a reader to the right conclusion. Signing is done
by a subkey, so the first fingerprint GPG prints is not the one published as
the release key; confirm the documented steps match the actual output. See
[`SECURITY.md`](/SECURITY.md).

Also run `contrib/devtools/check-upstream-identifiers.py` and confirm nothing
new has appeared on the shipped surface.

### Announce

Canonical announcement is a GitHub Discussion, with the IRC topic carrying the
release key fingerprint. Do not claim reproducible builds, and do not state a
hashrate the project cannot measure.


## Additional information

#### <a name="how-to-calculate-assumed-blockchain-and-chain-state-size"></a>How to calculate `m_assumed_blockchain_size` and `m_assumed_chain_state_size`

Both variables are used as a guideline for how much space the user needs on their drive in total, not just strictly for the blockchain.
Note that all values should be taken from a **fully synced** node and have an overhead of 5-10% added on top of its base value.

To calculate `m_assumed_blockchain_size`, take the size in GiB of these directories:
- For `mainnet` -> the data directory, excluding the `/testnet3`, `/testnet4`, `/signet`, and `/regtest` directories and any overly large files, e.g. a huge `debug.log`
- For `testnet` -> `/testnet3`
- For `testnet4` -> `/testnet4`
- For `signet` -> `/signet`

To calculate `m_assumed_chain_state_size`, take the size in GiB of these directories:
- For `mainnet` -> `/chainstate`
- For `testnet` -> `/testnet3/chainstate`
- For `testnet4` -> `/testnet4/chainstate`
- For `signet` -> `/signet/chainstate`

Notes:
- When taking the size for `m_assumed_blockchain_size`, there's no need to exclude the `/chainstate` directory since it's a guideline value and an overhead will be added anyway.
- The expected overhead for growth may change over time. Consider whether the percentage needs to be changed in response; if so, update it here in this section.
