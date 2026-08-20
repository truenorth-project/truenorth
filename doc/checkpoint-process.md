# Checkpoint process

This is the maintainer's runbook for adding block-hash checkpoints to
the TrueNorth mainnet chain parameters at release time. Background on
why checkpoints exist and what they defend against is in
[`src/checkpoints.h`](../src/checkpoints.h).

TL;DR: at each release, pick a block height, get its canonical hash
from a trusted synced node, add the `(height, hash)` entry to
`CMainParams::m_checkpoint_data` in `src/kernel/chainparams.cpp`, and
ship the release. Nodes running the new release refuse to accept any
chain whose block at that height has a different hash, regardless of
accumulated work.

## When to add a new checkpoint

Recommended cadence:

| Post-launch phase | Cadence | Rationale |
|---|---|---|
| First 3 months | Every 5,000 blocks (~7 days at 2-min blocks) | Network smallest, cheapest to attack |
| Months 3-12 | Every 20,000 blocks (~28 days) | Network hash growing, attack cost rising |
| Year 1+ | Every 50,000 blocks (~70 days) | Chain matured; attacks costly |
| Year 3+ | Reassess | May stop adding new checkpoints entirely (Bitcoin's path) |

Cadence is not a strict rule -- it's fine to skip a scheduled
checkpoint if no release is being cut, or to add extras during periods
of elevated attack risk (e.g. right after an exchange listing).

## Who can add checkpoints

Only the release-signing maintainer. Miners cannot influence
checkpoints -- there is no on-chain checkpoint mechanism, only source
code that ships in signed releases.

## How to add one

Prerequisites:

- A fully-synced trusted mainnet node running the current release
- Local clone of the source tree
- Ability to build, test, and cut a signed release

Steps:

### 1. Pick the checkpoint height

Follow the cadence above. Typically the height is the current chain
tip height rounded down to a round number (e.g. if the tip is at
57,342 and cadence is every 20,000, checkpoint at 40,000 or 60,000).

The safe rule: **the checkpoint must be at least ~100 blocks (or
current PoW confirmation depth) below the tip on your trusted node.**
This ensures the block is unlikely to be reorganized between when you
record its hash and when the release ships. Deeper is safer.

### 2. Get the block hash and chain work

On the trusted synced node:

```sh
./truenorth-cli getblockhash <height>
# -> 000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f
```

For the accumulated chain work at that height (needed to update
`nMinimumChainWork`):

```sh
./truenorth-cli getblockchaininfo | grep chainwork
./truenorth-cli getblockheader <hash> | grep chainwork
```

Both give a 64-hex-character `chainwork` value. Use the value from
the specific block you're checkpointing.

Cross-check on a second independent trusted node if available. The
checkpoint is authoritative for every future node; getting it wrong
by even one block hash forks the network.

### 3. Update the source

Edit `src/kernel/chainparams.cpp`, in the `CMainParams` constructor.

Add the new entry to `m_checkpoint_data.mapCheckpoints`:

```cpp
m_checkpoint_data = {
    /* .mapCheckpoints = */ {
        {0, consensus.hashGenesisBlock},
        {5000, uint256{"<hash of block 5000>"}},
        {10000, uint256{"<hash of block 10000>"}},
        // ... previous checkpoints ...
        {<new height>, uint256{"<new block hash>"}},
    },
};
```

Update `nMinimumChainWork` to the chain work value from step 2:

```cpp
consensus.nMinimumChainWork = uint256{"<chainwork hex value>"};
```

Update `defaultAssumeValid` to the new checkpoint block's hash:

```cpp
consensus.defaultAssumeValid = uint256{"<new block hash>"};
```

### 4. Commit

Single commit, clear subject:

```
chainparams: add checkpoint at height <N>

Block <N> hash: <hash>
Chain work: <chainwork>
```

No other changes in the same commit.

### 5. Build, test, cut release

- Full build passes locally on all target platforms
- `test_bitcoin --run_test="checkpoints_tests"` still passes
- `test_bitcoin --run_test="truenorth_tests"` still passes
- Any relevant regression scripts under `test/truenorth/` still pass
- Cut the tag, push to origin, let CI produce signed release binaries

### 6. Coordinate with seed nodes

Notify seed node operators so they upgrade to the new release
promptly. Post-release, encourage all node operators to upgrade;
older releases still enforce whatever checkpoints they shipped with,
but don't get the newer ones.

## What can go wrong

### Choosing a hash that gets reorganized

**Symptom**: after the release ships, nodes reject the canonical
chain because the checkpointed block is no longer in the main chain.
Network splits between updated nodes (following the old, "wrong"
chain) and non-updated nodes (following the new, real chain).

**Cause**: the maintainer picked a hash too close to the tip and it
got reorged before the release shipped, or before enough nodes
upgraded.

**Prevention**: always checkpoint blocks at least ~100 blocks below
the current tip. Cross-check on a second independent node.

**Recovery**: emergency release that removes or corrects the bad
checkpoint. This is a coordinated hard-fork event; treat it as an
incident.

### Adding to testnet or regtest by accident

**Symptom**: testnet or regression tests unable to exercise reorg
scenarios.

**Prevention**: only edit `CMainParams`. Never touch
`CTestNet3Params`, `CTestNet4Params`, or `CRegTestParams` checkpoint
data. Those chains must remain checkpoint-free by design.

### Forgetting to update supporting values

**Symptom**: `defaultAssumeValid` still points to an old block, so
IBD signature verification runs over more blocks than necessary
(performance drag, not a security issue). Or `nMinimumChainWork` too
low, allowing sync of trivially-low-work attacker chains.

**Prevention**: update all three in the same commit.

## Testing before release

The `checkpoints_tests` unit tests exercise the pure `Checkpoints::`
functions -- they don't need to know about actual chain data and stay
valid as new checkpoints are added.

For end-to-end validation of a specific new checkpoint, the
regression scripts under `test/truenorth/` can be extended to
simulate a reorg attempt that would replace the checkpointed block
and confirm rejection. (Not required per release; useful for
verifying the wire-up works after major consensus refactors.)

## Removing checkpoints

Don't. Once a checkpoint has shipped in a release, users are relying
on it. Removing it is a soft consensus rule loosening -- new nodes
would accept chains old nodes reject. Only remove a checkpoint if:

1. It was demonstrably wrong (bad hash from a reorg), AND
2. All previously-affected users have upgraded past the broken
   release, AND
3. There's a coordinated cutover plan.

Adding a checkpoint is a one-way door in practice.

## Relationship to other defenses

Checkpoints are one of several layered defenses:

- **Checkpoints** (this doc): release-time hard immutability for
  specific historical blocks
- **`nMinimumChainWork`**: rejects clearly-lower-work chains during
  IBD; updated alongside checkpoints
- **`defaultAssumeValid`**: IBD performance optimization; updated
  alongside checkpoints
- **Maximum reorg depth cap** (see separate issue): per-node
  continuous policy rejecting reorgs deeper than N blocks;
  complements checkpoints in the window between them
- **LWMA-1 difficulty adjustment**: responds to hash-rate changes
  within ~90 blocks; already in place, no maintenance needed
- **Community monitoring**: public visibility of block rate, hash
  distribution, reorg events; social layer
- **Delayed exchange listings**: reduces attack payoff; social layer

No single defense is sufficient in isolation. The maintainer's job
regarding checkpoints is one small but critical piece of the whole.
