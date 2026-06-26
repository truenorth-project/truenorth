# TrueNorth Core doc/ directory

Most files here are inherited from Bitcoin Core. They still reference Bitcoin Core paths, URLs, and project names in places. The mechanics they describe (build system, RPC and REST interfaces, consensus rules, PSBT, descriptor wallets, networking) all carry over to TrueNorth unchanged, but the branding has not been swept through every line. Read them as a developer reference for inherited behaviour rather than authoritative TrueNorth documentation.

For TrueNorth-specific docs, see [`/README.md`](../README.md) at the repo root.

## Build instructions per platform

- [`build-unix.md`](build-unix.md). Linux. The apt-get list in the root README is the curated TrueNorth dependency list.
- [`build-osx.md`](build-osx.md). macOS. The brew list in the root README is curated for TrueNorth.
- [`build-windows.md`](build-windows.md) and [`build-windows-msvc.md`](build-windows-msvc.md).
- [`build-freebsd.md`](build-freebsd.md), [`build-openbsd.md`](build-openbsd.md), [`build-netbsd.md`](build-netbsd.md).
- [`dependencies.md`](dependencies.md). Third-party library list. TrueNorth also vendors [RandomX](https://github.com/tevador/RandomX) at `src/randomx/`.
- [`guix.md`](guix.md). Reproducible-build setup. Not currently used by TrueNorth's release workflow.

## Developer references

- [`developer-notes.md`](developer-notes.md). Coding conventions. Inherited as-is.
- [`productivity.md`](productivity.md). IDE and tooling tips.
- [`JSON-RPC-interface.md`](JSON-RPC-interface.md). RPC surface. TrueNorth defaults to port 9554 on mainnet and 19554 on testnet3. The protocol itself is unchanged.
- [`REST-interface.md`](REST-interface.md). REST surface.
- [`bips.md`](bips.md). BIPs implemented. TrueNorth keeps all inherited BIPs and adds none of its own.

## Operations

- [`testnet.md`](testnet.md). How to join the TrueNorth testnet3 (seed `.onion`, sample tester config, what to report).
- [`bitcoin-conf.md`](bitcoin-conf.md). Config file reference. The TrueNorth daemon looks for `truenorth.conf` rather than `bitcoin.conf`. Everything else applies.
- [`files.md`](files.md). Datadir layout reference.
- [`init.md`](init.md). systemd, openrc, upstart unit examples.
- [`reduce-memory.md`](reduce-memory.md) and [`reduce-traffic.md`](reduce-traffic.md). Resource tuning.
- [`tor.md`](tor.md), [`i2p.md`](i2p.md), [`cjdns.md`](cjdns.md). Alt-network support.

## Wallet and transaction workflows

- [`managing-wallets.md`](managing-wallets.md).
- [`descriptors.md`](descriptors.md). Descriptor wallet format.
- [`psbt.md`](psbt.md). Partially Signed Transaction format. TrueNorth's RPC help text was rebranded but the on-wire BIP-174 encoding is unchanged.
- [`multisig-tutorial.md`](multisig-tutorial.md).
- [`offline-signing-tutorial.md`](offline-signing-tutorial.md).
- [`external-signer.md`](external-signer.md).

## Internals

- [`assumeutxo.md`](assumeutxo.md). UTXO snapshot design. TrueNorth ships no `m_assumeutxo_data` entries because the chain is too new. The mechanism is still in place for future use.
- [`fuzzing.md`](fuzzing.md), [`benchmarking.md`](benchmarking.md).
- [`tracing.md`](tracing.md). eBPF and DTrace tracepoints.
- [`multiprocess.md`](multiprocess.md). Cap'n Proto IPC mode. Built with `-DENABLE_IPC=OFF` by default.
- [`p2p-bad-ports.md`](p2p-bad-ports.md), [`dnsseed-policy.md`](dnsseed-policy.md).
- [`design/`](design/). Internal design notes.

## Release process

- [`release-process.md`](release-process.md). Inherited Bitcoin Core release flow doc. TrueNorth release tagging runs through the GitHub Actions workflow in `.github/workflows/release.yml`; historical Bitcoin Core release notes have been dropped from the repo.

---

If you find a doc that's been rendered actively wrong by a TrueNorth change rather than just inheriting Bitcoin branding, open a GitHub Issue.
