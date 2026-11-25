# P2QRH upgrade path: decision record

**Status:** Decided 2026-11-24
**Decision:** add script paths and new signature schemes in a new output type,
witness version 3, activated by soft fork. Witness version 2 stays as it is.

This records why, what else was considered, and the risks and requirements the
v3 work has to meet. Use it as the checklist while v3 is specified, built and
reviewed. The protocol itself is in [`doc/p2qrh.md`](../p2qrh.md).

## The problem

`doc/p2qrh.md` said both that unknown `scheme_id`s are consensus-invalid and
that future soft forks would add them. The code implements the first: a
32-byte witness v2 program accepts exactly `[signature, pubkey, 0x01]` plus an
optional annex, and every other witness fails
(`VerifyWitnessProgram` in `src/script/interpreter.cpp`).

A soft fork can only make the rules stricter. Since every other v2 spend is
already invalid, making any new kind of v2 spend valid, whether a script path
or a new `scheme_id`, is a hard fork. As built, v2 cannot be extended by soft
fork at all.

The one soft-fork hook v2 has is the annex: consensus accepts it and the
signature commits to it. A soft fork can use it to add requirements to a spend,
but never a new way to spend, so it does not help here.

## Facts that hold under every option

1. **Existing coins must move to use a new feature.** An address commits to
   its `scheme_id` and `script_root` when it is created. A v2 address today
   commits to Schnorr and an empty tree, so it can never gain a script path or
   a post-quantum scheme under any design. Coins move to a new address with an
   ordinary transaction.
2. **Senders need no upgrade.** Following BIP-350, today's nodes decode any
   unknown witness version as a valid destination (`src/key_io.cpp`), and
   creating such an output is standard. Only spending one is refused by policy
   (`src/policy/policy.cpp`, `SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_WITNESS_PROGRAM`).
3. **Existing v2 outputs are safe either way.** Their commitment binds
   `scheme_id`, pubkey and `script_root`; spending one through any other scheme
   or tree needs a SHA-256 second preimage.

## Options considered

### A: new output type, witness v3, by soft fork (chosen)

v3 carries key path, script path and Taproot-style upgrade hooks. v2 stays
valid permanently, Schnorr key path only.

| For | Against |
|---|---|
| No hard fork; nodes that do not upgrade stay on the chain | Two P2QRH output types (`north1z…`, `north1r…`) |
| No deadline: ships when the spec and tests are ready | v3 outputs are distinguishable from v2 while both are in use (privacy, until wallets default to v3) |
| Today's wallets can already pay a v3 address | Two code paths in consensus, wallet and descriptors |
| Follows Bitcoin's precedent (SegWit, then Taproot) | The hooks must be right, or v3 freezes like v2 |
| A flawed v3 can be superseded by v4 the same way | Safety after activation rests on miner enforcement, as for any soft fork |

### B: relax v2 once by hard fork, then soft forks

Change v2 so an unknown `scheme_id` succeeds once its commitment checks out,
and define the script-path structure with leaf execution left to later soft
forks.

| For | Against |
|---|---|
| One output type and one code path | Hard fork: every node, above all every miner, must upgrade before the activation height. Once active, a single miner can create and mine a spend that old nodes reject, splitting the chain |
| All outputs look alike | Deadline pressure: the cost grows with the network |
| | Sets a precedent of scheduled hard forks for a Bitcoin-conservative project |
| | Its main benefit is cosmetic: coins move either way (fact 1) |

B would be the better choice only if keeping one address type outweighed
avoiding a hard fork, or if a hard fork were happening anyway for another
reason (for example a proof-of-work change), in which case the relaxation
could ride along.

### C: correct the documentation only, decide later

Nothing breaks while waiting, but anyone building multisig, timelocks or escrow
has nothing to build on. This is the state until A ships, not a separate path.

## Precedent

- **SegWit, then Taproot.** BIP-141 left witness versions 1-16 and unknown
  program lengths spendable by anyone and discouraged by policy, which is
  what let Taproot (witness v1) arrive by soft fork. Option A is the same move.
- **Taproot's hooks (BIP-341/342).** Unknown leaf versions succeed after the
  tree commitment is checked. `OP_SUCCESSx` opcodes make a script succeed.
  Public keys of unknown length pass `CHECKSIG` without verification, to leave
  room for new signature schemes. The annex is reserved. v3 copies these; v2
  has none of them.
- **Bitcoin has not deliberately hard-forked.** Its one consensus split
  (BIP-50, March 2013) was accidental, and miners resolved it within hours by
  reverting to the older rules. Bitcoin Cash (2017) shows the other outcome:
  when enough miners stay behind, the split is permanent.
- **Scheduled hard forks** work in communities that expect them: Monero and
  Zcash network upgrades activate at a height, and zcashd halts itself some
  weeks after a release to force upgrades.
- **Multiple address types.** Bitcoin carries five without serious trouble.
  Zcash accumulated several and added Unified Addresses (ZIP-316) to hide
  them.

## Risks, and how the plan handles them

1. **v3 outputs are unprotected until activation.** Any miner can spend them,
   and creating them is already standard.
   *Plan:* v3 runs only on regtest and testnet4 until mainnet activation.
   Wallets, including third-party ones, must not create v3 outputs on mainnet
   before then.
2. **Soft-fork safety rests on miner enforcement.** After activation,
   non-upgraded nodes do not check v3 spends and would follow a block with an
   invalid one until enforcing miners overtake it. Hashrate is concentrated in
   a few miners, so "most miners enforce" depends on a few operators.
   A non-upgraded miner's own policy refuses v3 spends, so accidental
   inclusion is unlikely.
   *Plan:* choose the activation method deliberately (see open questions), give
   long notice, and confirm that the main miners run the new release before
   the height.
3. **Work for existing v2 implementers.** The in-tree wallet vectors, the
   independent vector set and third-party wallets all target v2, and each must
   add v3. The v2 vectors become the legacy set.
   *Plan:* publish v3 vectors in the same format alongside the spec.
4. **New consensus surface.** Script paths and hooks are a large addition.
   *Plan:* reuse the BIP-341/342 code paths nearly verbatim instead of new
   interpreter logic; adversarial tests (below); a long testnet4 period.
5. **The hooks could be wrong again.** The way out would be v4, still without
   a hard fork; 13 witness versions remain unused.
   *Plan:* review the spec for upgradability before any code is written.
6. **Communication.** "P2QRH" now covers two output types.
   *Plan:* README, `doc/p2qrh.md` and the release notes describe v2 as
   permanent and v3 as the upgrade path, in the same terms.

## Requirements for the v3 specification

- [ ] Key path and script path. Decide explicitly whether v3 has a key path;
      the commitment hides the key either way.
- [ ] Script path reuses BIP-341 (control block, Merkle proof) and BIP-342
      (tapscript), including `OP_SUCCESSx` and unknown public key types.
- [ ] An unknown `scheme_id` or leaf version succeeds **only after** the
      commitment and, for a script path, the Merkle proof check out.
- [ ] The witness layout from which the hook takes the pubkey and
      `script_root` is fixed now, since every later scheme inherits it,
      including kilobyte-sized post-quantum keys.
- [ ] Its own domain separation tag, distinct from `"TrueNorth/QRH/v1"`.
- [ ] `scheme_id` numbering shared with the existing registry; `0x50`
      (annex tag), `0x00` and `0xFF` never assigned.
- [ ] Sighash for `0x01` follows BIP-341; each later scheme defines its own,
      binding the `scheme_id`.
- [ ] Policy: spends with an unknown `scheme_id` or leaf version, and any
      annex, are non-standard.
- [ ] Wallet rules: never create v3 outputs on mainnet before activation, and
      never create an address for an inactive scheme. Whether and when
      `getnewaddress` moves from v2 to v3 by default.
- [ ] Activation: method, height, notice period, and the testnet4 period
      before it.
- [ ] An **Upgradability** section stating, for each consensus rule, what a
      later soft fork can add on top of it.

Tests that must exist before activation:

- [ ] Spending an existing output through every other `scheme_id` fails.
- [ ] Wrong pubkey, wrong `script_root`, wrong Merkle path and wrong tag each
      fail.
- [ ] An unknown `scheme_id` or leaf version with a matching commitment
      succeeds under consensus rules and is refused by policy.
- [ ] v2 behaviour is unchanged (the existing P2QRH vectors still pass).
- [ ] Before activation: v3 spends are accepted by consensus and refused by
      policy, on every chain.
- [ ] Wallet vectors for v3 in the format of
      `src/test/data/p2qrh_wallet_vectors.json`.

## Review rule

For every consensus rule, new or changed, a review states what a future soft
fork could add on top of it. A rule that rejects all unknown cases closes that
door; leaving unknown cases valid at consensus and refused by policy keeps it
open.

## Open questions

- Activation method: a fixed height with long notice, or miner signalling with
  a fallback height.
- Whether v3 has a key path.
- When wallets stop creating v2 addresses by default.
- How to word the change for exchanges and other integrators.
