# P2QRH — Pay-to-Quantum-Resistant-Hash

**Status:** Draft
**Type:** Consensus + Wallet
**Chain:** TrueNorth

## Summary

A new SegWit witness version 2 output type where the on-chain commitment is a
hash covering `(scheme_id, pubkey, script_root)` — not the pubkey itself. A
`scheme_id` byte in the spend witness selects the signature verifier.

Witness version 2 accepts exactly one kind of spend: a secp256k1 Schnorr
key-path spend with `scheme_id=0x01`. Every other witness is consensus-invalid,
so v2 cannot gain script paths or new signature schemes by soft fork. Those are
planned for a new output type, witness version 3, activated by soft fork; see
[Upgrade path](#upgrade-path).

## Design goals

1. Coins-at-rest are quantum-safe — a CRQC cannot recover the pubkey from an
   unspent P2QRH output.
2. Signature scheme is not pre-committed in the commitment format — the
   commitment carries a `scheme_id`, so a post-quantum scheme fits the same
   32-byte commitment as Schnorr. In v2 only Schnorr is valid; new schemes
   arrive in the v3 output type.
3. Fixed address size — the commitment is 32 bytes whatever the scheme, so
   kilobyte-sized post-quantum keys never reach the address.
4. Reuse Taproot script-path infrastructure via a Merkle tree of alternate
   spend conditions — planned for v3; v2 has no script path.

## Output format

```
scriptPubKey: OP_2 <32-byte-commitment>
```

Where `OP_2` marks witness version 2 (previously unused; v0 = P2WPKH/P2WSH,
v1 = Taproot).

The commitment is 32 bytes regardless of signature scheme, so the address
length does not depend on the scheme, even for PQ schemes with kilobyte-sized
pubkeys.

## Address encoding

- Bech32m (BIP-350) with witness version 2
- Mainnet HRP: `north`; address form `north1z…`
- Testnet4 HRP: `tnorth4`; address form `tnorth41z…`
- Total length: 65 chars on mainnet, 67 on testnet4. Three more than Taproot's
  62, and the difference is the HRP (`north` against `bc`), not the witness
  version character -- Taproot and P2QRH each have exactly one of those,
  `p` for v1 and `z` for v2. Both encode a 32-byte program, so both carry 52
  data characters and a 6-character checksum.
- Wallets should label as "P2QRH" in UI

## Commitment format

```
commitment = SHA256("TrueNorth/QRH/v1" || scheme_id || pubkey_bytes || script_root)
```

- **Domain separation string** `"TrueNorth/QRH/v1"` prevents cross-protocol
  collisions and enables safe versioning: a changed commitment format, or a
  new output type, uses a new tag.
- `scheme_id` is 1 byte.
- `pubkey_bytes` is scheme-defined (32 B for Schnorr x-only; ~1.3 KB for
  ML-DSA; etc.).
- `script_root` is 32 bytes. Witness v2 has no script path, so for v2 it is
  always `0x00…00` (`QRH_EMPTY_SCRIPT_ROOT`).

## Witness format

**Key-path spend** (single signature, no script tree used):

```
witness stack (bottom to top):
  [signature]
  [pubkey]
  [scheme_id]  (1 byte)
```

This is the only valid witness for a v2 output, apart from an optional annex
(below). There is no script-path spend in v2; any other witness is
consensus-invalid.

The `scheme_id` at the top of the stack lets the interpreter know which
verifier to dispatch before parsing the rest of the witness.

**Annex.** As in BIP-341, if the witness has at least two elements and the top
one starts with `0x50`, that element is an annex. It is removed before
`scheme_id` is read, and its hash is committed in the signature hash. Consensus
accepts an annex on a P2QRH spend so that a future soft fork can give it a
meaning; policy refuses one, so such a transaction is not relayed or mined by
default nodes. Wallets must not add an annex. Because the annex check comes
first, a `scheme_id` of `0x50` would always be read as an annex, so `0x50` is
excluded from the scheme registry.

## Consensus validation

1. Remove an annex, if present.
2. Read top of stack: `scheme_id`. It must be exactly one byte, and it must be
   `0x01`; any other value fails.
3. Exactly two elements must remain: `[signature, pubkey]`.
4. The pubkey must be a 32-byte x-only key.
5. Recompute `commitment = SHA256("TrueNorth/QRH/v1" || 0x01 || pubkey || 0x00…00)`
   and verify it equals the 32 bytes in the scriptPubKey.
6. Verify the BIP-340 Schnorr signature over the BIP-341 sighash.

Every step can only fail. No v2 spend succeeds without a valid Schnorr
signature from the committed key, which is why a new kind of v2 spend could
only be added by a hard fork (see [Upgrade path](#upgrade-path)).

Each rule a key-path witness can break has its own script error, so a reject
reason says which one failed:

| Script error                    | Reject reason                                            |
|---------------------------------|----------------------------------------------------------|
| `WITNESS_PROGRAM_WITNESS_EMPTY` | Witness program was passed an empty witness              |
| `QRH_WITNESS_SIZE`              | P2QRH key-path witness must have exactly three elements  |
| `QRH_SCHEME_SIZE`               | P2QRH scheme id must be exactly one byte                 |
| `QRH_SCHEME_UNKNOWN`            | P2QRH scheme id is not active                            |
| `QRH_PUBKEY_SIZE`               | P2QRH public key has the wrong size for its scheme       |
| `WITNESS_PROGRAM_MISMATCH`      | Witness program hash mismatch                            |
| `SCHNORR_SIG_SIZE`              | Invalid Schnorr signature size                           |
| `SCHNORR_SIG_HASHTYPE`          | Invalid Schnorr signature hash type                      |
| `SCHNORR_SIG`                   | Invalid Schnorr signature                                |

Releases up to v30.5.4 return `WITNESS_PROGRAM_MISMATCH` for all of these except
an empty witness and a bad signature.

## Scheme registry

| scheme_id | Signature scheme            | Pubkey  | Sig     | Status                        |
|-----------|-----------------------------|---------|---------|-------------------------------|
| `0x00`    | RESERVED                    | —       | —       | Never valid                   |
| `0x01`    | secp256k1 Schnorr (BIP-340) | 32 B    | 64 B    | Active at launch              |
| `0x02`    | ML-DSA-44                   | ~1.3 KB | ~2.4 KB | Reserved (invalid in v2)      |
| `0x03`    | Falcon-512                  | ~900 B  | ~650 B  | Reserved (invalid in v2)      |
| `0x04`    | SLH-DSA-128s                | 32 B    | ~8 KB   | Reserved (invalid in v2)      |
| `0x05-0x4F` | Reserved for future schemes | —     | —       | —                             |
| `0x50`    | Unusable (annex tag)        | —       | —       | Never assigned                |
| `0x51-0xFE` | Reserved for future schemes | —     | —       | —                             |
| `0xFF`    | INVALID (sentinel)          | —       | —       | Reject at consensus           |

In witness v2, `scheme_id=0x01` is the only accepted value at consensus, and
any other value fails validation permanently: making one valid would be a hard
fork. The registry keeps its numbering for the v3 output type, where new
schemes are to be added by soft fork.

## Signature aggregation

For `scheme_id=0x01`, MuSig2 (BIP-327) key aggregation is supported:
participants aggregate their pubkeys off-chain; the resulting aggregate pubkey
goes into the commitment; key-path spending uses a single Schnorr signature.
Same code path as Taproot's BIP-341/342/327 stack.

PQ schemes may or may not support aggregation depending on their properties;
this is defined per-scheme in their activation specification.

## Sighash

`scheme_id=0x01` uses BIP-341 sighash (same as Taproot key-path). PQ schemes
define their sighash algorithm in their activation specification — likely a
scheme-specific variant that binds the scheme_id byte into the signed message
to prevent scheme-substitution attacks.

## Standardness / mempool policy

- Only `scheme_id=0x01` key-path spends are valid; policy additionally refuses
  an annex.
- Witness v2 programs of any length other than 32 bytes are not P2QRH.
  Consensus accepts any spend of them and policy refuses to relay one
  (upgradable-witness convention). The same holds for witness v3 until the v3
  output type activates.

## Weight and fee implications

- **scriptPubKey**: 34 bytes (1 `OP_2` + 1 length + 32 commitment). Same as
  Taproot. **No UTXO-set growth.**
- **Witness (scheme_id=0x01, key-path)**: ~99 bytes (32 pubkey + 64 sig +
  1 scheme_id + length prefixes). +3 bytes vs Taproot; negligible.
- **Witness (PQ schemes, v3)**: significantly larger per-spend, but spend-time
  cost only. Standard witness weight discount (1 wu/byte) applies.

## Activation

**Active from block 0 on every chain** (mainnet, testnet, testnet4, signet,
regtest). No BIP9 deployment, no legacy grandfathering, no
`script_flag_exceptions`.

Rationale: TrueNorth is a fresh chain with no historical P2QRH outputs on
any network. Retroactively "activating" the rule from genesis has no effect
on any past block (they contain zero witness-v2 outputs to validate). And
because unknown scheme_ids are consensus-invalid rather than merely
non-standard, there is nothing on-chain that a from-genesis activation could
turn from valid to invalid.

Concretely, `SCRIPT_VERIFY_QRH` is included in the base flag set constructed
by `GetBlockScriptFlags()` in `src/validation.cpp` — the same code path that
enables P2SH, WITNESS, and TAPROOT unconditionally.

## Upgrade path

A soft fork can only make the rules stricter. Every v2 spend other than the
Schnorr key path is already invalid, so a new kind of v2 spend — a script
path, or a new `scheme_id` — would be a hard fork, which every node and miner
must adopt before anyone uses it.

Script paths and new signature schemes are therefore planned for a new output
type, **witness version 3** (`north1r…` on mainnet), activated by soft fork at
a height. Today's nodes already treat any witness v3 spend as valid at
consensus and refuse to relay one, the upgradable-witness convention of
BIP-141, which is what makes adding it a soft fork. Its specification is a
separate document. It must meet these requirements:

- **Key path and script path**, reusing BIP-341/342 (tapscript) rather than
  new interpreter logic.
- **Upgrade hooks.** An unknown `scheme_id`, or an unknown leaf version,
  succeeds once the commitment (and, for a script path, the Merkle proof)
  checks out, as Taproot treats unknown leaf versions. Later schemes are then
  soft forks. The commitment binds `scheme_id`, pubkey and `script_root`, so
  an existing output cannot be spent through any other scheme or tree.
- **Its own domain separation tag**, so the same key never yields related
  v2 and v3 commitments.
- **No mainnet use before activation.** Until v3 activates, consensus does not
  protect v3 outputs: any miner can spend them. Wallets must not create them on
  mainnet before then, and must never create an address for a scheme that is
  not active.

v2 outputs stay valid and spendable with `scheme_id=0x01` permanently. An
address fixes its scheme and script tree when it is created, so using a new
scheme or a script path means moving coins to a new address with an ordinary
transaction, under any upgrade design.

Until v3 activates, n-of-n multisig is available on v2 through MuSig2 key
aggregation (below); k-of-n multisig, timelocks and escrow need the v3 script
path.

The options considered, the precedent, and the risks this plan has to manage
are in [`doc/design/p2qrh-upgrade-path.md`](design/p2qrh-upgrade-path.md).

## Wallet implications

1. New `OutputType::BECH32M_QRH` in `src/outputtype.h`.
2. New descriptor `qrh(...)` mirroring `tr(...)` in `src/script/descriptor.cpp`.
3. **`DEFAULT_ADDRESS_TYPE = BECH32M_QRH`** in `src/wallet/wallet.h` — QRH is
   the default for `getnewaddress`, new wallet setup, and all address-generation
   flows. Users can opt back to P2WPKH via `-addresstype=bech32` if needed for
   external interop as the ecosystem develops.
4. **Derivation path.** Wallet-generated `qrh()` descriptors use BIP32
   purpose `88h`: `m/88h/<coin_type>/0h/<0|1>/<index>`, with change on
   branch `1`, following the BIP44/84/86 layout. `<coin_type>` is `1867h`
   on mainnet (SLIP-44 index, deliberately not Bitcoin's `0h`) and `1h` on test
   chains (`src/wallet/walletutil.cpp`). Purpose
   `88h` is TrueNorth-specific and not registered as a BIP. Recovery tools and other
   wallets must scan this path to find P2QRH funds from a TrueNorth seed.
5. **Fallback when a wallet has no P2QRH descriptor.** External signers
   (hardware wallets via HWI) and wallets built from imported descriptors
   usually have no `qrh()` descriptor, because current devices don't support
   P2QRH. When `getnewaddress` or `getrawchangeaddress` is called without an
   `address_type` and the wallet has no active descriptor for the default,
   it falls back to the first type the wallet can produce, in the order
   `bech32m`, `bech32`, `p2sh-segwit`, `legacy`. An explicitly requested
   type is never substituted; if the wallet can't produce it, the call
   fails. Fallback addresses are **not** quantum-resistant at rest;
   hardware-wallet users get P2QRH once their device and HWI support
   `qrh()`.
6. **Change outputs.** When the wallet has a P2QRH descriptor and P2QRH is
   the default (or any recipient is P2QRH), change always goes to a P2QRH
   address, even when paying a Taproot or SegWit address. Bitcoin Core
   matches change to the recipient type for privacy; TrueNorth prefers
   keeping change quantum-resistant at rest.
7. **PSBTs with external inputs.** The `scheme_id=0x01` sighash follows
   BIP-341 and commits to the amounts and scripts of every input. A wallet
   cannot sign its own P2QRH input in a PSBT that also spends inputs it
   doesn't know until the PSBT carries UTXO data (`witness_utxo`) for all
   inputs. This matches Taproot's behavior in Bitcoin Core.

## Test vectors

`src/test/data/p2qrh_wallet_vectors.json` covers what a wallet implementing
P2QRH needs to match, for `scheme_id=0x01` key-path spends:

- commitment preimage, commitment, scriptPubKey and address on every chain,
  for raw x-only keys;
- addresses a wallet must reject, and a witness v2 address with a 20-byte
  program that is valid but is not P2QRH;
- BIP32 derivation from the standard BIP39 test mnemonic on the `88h` path,
  for mainnet and testnet4: extended keys with TrueNorth's version bytes,
  `qrh()` descriptors, and the first receive and change keys and addresses;
- signed spends, one with `SIGHASH_DEFAULT` and one with every sighash type,
  with the BIP-341 intermediate hashes, the signature message, the sighash,
  the witness and the final transaction;
- witnesses consensus must reject, with the script error for each.

Signatures use 32 zero bytes as BIP-340 `aux_rand`, as TrueNorth Core does, so
a wallet that does the same reproduces them byte for byte. Any `aux_rand`
gives a valid signature.

`contrib/testgen/gen_p2qrh_vectors.py` generates the file from the functional
test framework's Python implementation; `src/test/p2qrh_vector_tests.cpp`
checks every value against TrueNorth Core's descriptor, signing and script
code.

An independent set is maintained at
<https://github.com/severka-monitor/truenorth-cold-address/tree/main/vectors>.
It was computed with code that shares nothing with TrueNorth Core and checked
against a running node: its spends were accepted and mined, and it includes
descriptors with private keys in the form `importdescriptors` takes. The two
sets agree everywhere they overlap.

## Rationale — key design decisions

**Why witness version 2 rather than an OP_RETURN commitment or a taproot-annex
extension?** Version 2 gives us a clean namespace: no conflict with Taproot
semantics, no dependency on tapscript extension machinery, no confusion for
existing tooling. It's the cleanest place to put a new output type.

**Why not ship a PQ signature scheme at launch?** The signature-scheme
ecosystem hasn't converged (ML-DSA vs Falcon vs SLH-DSA vs future schemes).
PQ implementations are still hardening. PQ sig sizes are 10-100× Schnorr's,
imposing immediate throughput cost for a threat 10-25 years away. The
commitment format lets us wait for ecosystem convergence and add the winner
later, in the v3 output type, without disrupting existing UTXOs.

**Why domain-separate the hash with a version-tagged string?** Two reasons.
First: prevents accidental cross-protocol collisions if anyone reuses SHA256
elsewhere with similar structure. Second: gives us a clean escape hatch — if
the commitment format itself ever needs to change (e.g., we discover we should
have included additional metadata), a new tag defines a new envelope without
disturbing outputs that use `"TrueNorth/QRH/v1"`.

**Why is `scheme_id` at the TOP of the witness stack rather than the bottom?**
The interpreter reads it first without needing to know per-scheme witness
parsing. Simpler dispatch.

**Why 32 bytes of zeros for empty script_root rather than omitting the field?**
Fixed commitment structure regardless of whether a script tree exists. Wallet
code, verifier code, and address-generation code all handle exactly one
commitment shape. Zero is a sentinel that "no tree" chooses.

## References

- BIP-141: SegWit output types
- BIP-340: Schnorr signatures for secp256k1
- BIP-341: Taproot output and spend format
- BIP-342: Validation of Taproot scripts (tapscript)
- BIP-350: Bech32m address encoding
- BIP-360 (draft): Pay-to-Quantum-Resistant-Hash — the community proposal this
  design draws from
- NIST PQC Standardization (FIPS 203/204/205): post-quantum cryptography schemes
