// Copyright (c) 2026 The TrueNorth developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_CHECKPOINTS_H
#define BITCOIN_CHECKPOINTS_H

#include <uint256.h>

#include <functional>
#include <map>

class CBlockIndex;

// Block-height -> block-hash map for hardcoded checkpoints.
//
// Upstream Bitcoin Core removed the CCheckpointData mechanism in
// favour of nMinimumChainWork + defaultAssumeValid once the network
// reached a hash rate that made deep reorgs economically absurd. A
// small-hash chain in its early years does not have that luxury:
// a well-funded attacker can rent enough cloud CPU to sustain more
// than the network's honest hash rate for hours or days, and rewrite
// history down to the last checkpoint or (without one) to genesis.
//
// TrueNorth reintroduces the mechanism for that reason. Checkpoints
// are release-authority actions: the maintainer computes the block
// hash at a chosen height on a trusted synced node, adds it to
// CMainParams::m_checkpoint_data, and ships a release. Anyone running
// that release refuses to accept any chain whose block at that height
// has a different hash, regardless of accumulated work.
//
// See doc/checkpoint-process.md for the maintainer procedure.
using MapCheckpoints = std::map<int, uint256>;

// Hardcoded checkpoint set for a chain. Empty for testnet4 and
// regtest by design -- those chains need to accept deep reorgs for
// testing.
struct CCheckpointData {
    MapCheckpoints mapCheckpoints;
};

namespace Checkpoints {

// Verify that a block at `nHeight` with hash `hash` is consistent
// with the checkpoint set. Returns:
//   true  -- the height is not checkpointed, or the hash matches
//   false -- the height IS checkpointed and the hash differs
//
// Callers should reject the block on false.
bool CheckBlock(const CCheckpointData& data, int nHeight, const uint256& hash);

// Return the block index of the highest-height checkpoint that is
// present in the caller-provided block index. Returns nullptr if the
// checkpoint set is empty or if none of the checkpointed hashes are
// found in the block index.
//
// `block_index_lookup` is a caller-supplied function that resolves a
// block hash to its CBlockIndex, or returns nullptr if the block is
// unknown. Passing the lookup as a callable rather than depending on
// BlockManager keeps this header dependency-free.
const CBlockIndex* GetLastCheckpoint(
    const CCheckpointData& data,
    const std::function<const CBlockIndex*(const uint256&)>& block_index_lookup);

} // namespace Checkpoints

#endif // BITCOIN_CHECKPOINTS_H
