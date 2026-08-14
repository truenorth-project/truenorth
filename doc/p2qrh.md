# P2QRH — Pay-to-Quantum-Resistant-Hash

**Status:** Draft
**Type:** Consensus + Wallet
**Chain:** TrueNorth

## Summary

A new SegWit witness version 2 output type where the on-chain commitment is a
hash covering `(scheme_id, pubkey, script_root)` — not the pubkey itself. A
`scheme_id` byte in the spend witness selects the signature verifier, allowing
new signature schemes (including post-quantum) to be added via soft fork
without changing the output type, address format, or wallet code.

## Design goals

1. Coins-at-rest are quantum-safe — a CRQC cannot recover the pubkey from an
   unspent P2QRH output.
2. Signature scheme is not pre-committed at launch — start with secp256k1
   Schnorr; add PQ schemes later via soft fork without disturbing existing
   UTXOs.
3. One address format forever — users don't migrate addresses when new
   signature schemes are added.
4. Reuse Taproot script-path infrastructure via a Merkle tree of alternate
   spend conditions.

## Output format

```
scriptPubKey: OP_2 <32-byte-commitment>
```

Where `OP_2` marks witness version 2 (previously unused; v0 = P2WPKH/P2WSH,
v1 = Taproot).

The commitment is 32 bytes regardless of signature scheme. Address size is
fixed forever, even when adding PQ schemes with kilobyte-sized pubkeys.

## Address encoding

- Bech32m (BIP-350) with witness version 2
- Mainnet HRP: `north`; address form `north1z…`
- Testnet4 HRP: `tnorth4`; address form `tnorth41z…`
- Total length: 62 chars (10 more than Taproot due to witness version character)
- Wallets should label as "P2QRH" in UI

## Commitment format

```
commitment = SHA256("TrueNorth/QRH/v1" || scheme_id || pubkey_bytes || script_root)
```

- **Domain separation string** `"TrueNorth/QRH/v1"` prevents cross-protocol
  collisions and enables safe versioning if the commitment format ever needs
  to change (v2 would prefix `"TrueNorth/QRH/v2"`).
- `scheme_id` is 1 byte.
- `pubkey_bytes` is scheme-defined (32 B for Schnorr x-only; ~1.3 KB for
  ML-DSA; etc.).
- `script_root` is 32 bytes: root of the taptree, or `0x00…00` when no script
  tree exists (mirroring Taproot convention).

## Witness format

**Key-path spend** (single signature, no script tree used):

```
witness stack (bottom to top):
  [signature]
  [pubkey]
  [scheme_id]  (1 byte)
```

**Script-path spend** (Taproot-analogous):

```
witness stack (bottom to top):
  [script args]
  [tapscript-analog script]
  [control block]  (33 + 32k bytes, k = tree depth)
  [scheme_id]  (1 byte)
```

The `scheme_id` at the top of the stack lets the interpreter know which
verifier to dispatch before parsing the rest of the witness.

## Consensus validation

1. Read top of stack: `scheme_id`. If not in the registered set, fail.
2. Recompute `commitment = SHA256("TrueNorth/QRH/v1" || scheme_id || pubkey || script_root)`.
3. Verify recomputed commitment equals the 32 bytes in the scriptPubKey.
4. **Key-path**: dispatch to `scheme_id`'s signature verifier with
   `(sighash, pubkey, signature)`.
5. **Script-path**: verify Merkle proof against `script_root` (universal —
   hash-based, safe for all scheme_ids), then dispatch to the tapscript
   interpreter (defined per-scheme; scheme_id=0x01 reuses BIP-342).

## Scheme registry

| scheme_id | Signature scheme            | Pubkey  | Sig     | Status                        |
|-----------|-----------------------------|---------|---------|-------------------------------|
| `0x00`    | RESERVED                    | —       | —       | Never valid                   |
| `0x01`    | secp256k1 Schnorr (BIP-340) | 32 B    | 64 B    | Active at launch              |
| `0x02`    | ML-DSA-44                   | ~1.3 KB | ~2.4 KB | Reserved (future soft fork)   |
| `0x03`    | Falcon-512                  | ~900 B  | ~650 B  | Reserved (future soft fork)   |
| `0x04`    | SLH-DSA-128s                | 32 B    | ~8 KB   | Reserved (future soft fork)   |
| `0x05-0xFE` | Reserved for future schemes | —     | —       | —                             |
| `0xFF`    | INVALID (sentinel)          | —       | —       | Reject at consensus           |

At launch, `scheme_id=0x01` is the only accepted value at consensus. Any other
value fails validation. Future soft forks add scheme_ids.

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

- Only `scheme_id=0x01` accepted at launch
- Reserved scheme_ids rejected as non-standard until their respective
  activation
- Non-`OP_2`-formatted outputs pretending to be v2 are non-standard but not
  consensus-invalid (upgradable-witness convention)

## Weight and fee implications

- **scriptPubKey**: 34 bytes (1 `OP_2` + 1 length + 32 commitment). Same as
  Taproot. **No UTXO-set growth.**
- **Witness (scheme_id=0x01, key-path)**: ~99 bytes (32 pubkey + 64 sig +
  1 scheme_id + length prefixes). +3 bytes vs Taproot; negligible.
- **Witness (PQ schemes)**: significantly larger per-spend, but spend-time
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

## Wallet implications

1. New `OutputType::BECH32M_QRH` in `src/outputtype.h`.
2. New descriptor `qrh(...)` mirroring `tr(...)` in `src/script/descriptor.cpp`.
3. **`DEFAULT_ADDRESS_TYPE = BECH32M_QRH`** in `src/wallet/wallet.h` — QRH is
   the default for `getnewaddress`, new wallet setup, and all address-generation
   flows. Users can opt back to P2WPKH via `-addresstype=bech32` if needed for
   external interop as the ecosystem develops.

## Rationale — key design decisions

**Why witness version 2 rather than an OP_RETURN commitment or a taproot-annex
extension?** Version 2 gives us a clean namespace: no conflict with Taproot
semantics, no dependency on tapscript extension machinery, no confusion for
existing tooling. It's the cleanest place to put a new output type.

**Why not ship a PQ signature scheme at launch?** The signature-scheme
ecosystem hasn't converged (ML-DSA vs Falcon vs SLH-DSA vs future schemes).
PQ implementations are still hardening. PQ sig sizes are 10-100× Schnorr's,
imposing immediate throughput cost for a threat 10-25 years away. The envelope
lets us wait for ecosystem convergence and soft-fork the winner in without
disrupting existing UTXOs.

**Why domain-separate the hash with a version-tagged string?** Two reasons.
First: prevents accidental cross-protocol collisions if anyone reuses SHA256
elsewhere with similar structure. Second: gives us a clean escape hatch — if
the commitment format itself ever needs to change (e.g., we discover we should
have included additional metadata), we bump to `"TrueNorth/QRH/v2"` as a new
envelope, without disturbing v1.

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
