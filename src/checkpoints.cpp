// Copyright (c) 2026 The TrueNorth developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <checkpoints.h>

#include <chain.h>

namespace Checkpoints {

bool CheckBlock(const CCheckpointData& data, int nHeight, const uint256& hash)
{
    const auto it = data.mapCheckpoints.find(nHeight);
    if (it == data.mapCheckpoints.end()) return true;
    return it->second == hash;
}

const CBlockIndex* GetLastCheckpoint(
    const CCheckpointData& data,
    const std::function<const CBlockIndex*(const uint256&)>& block_index_lookup)
{
    // Iterate checkpoints from highest to lowest height. First one
    // present in the block index is our answer. std::map is ordered
    // by key so rbegin/rend gives us descending iteration.
    for (auto it = data.mapCheckpoints.rbegin(); it != data.mapCheckpoints.rend(); ++it) {
        const CBlockIndex* pindex = block_index_lookup(it->second);
        if (pindex != nullptr) return pindex;
    }
    return nullptr;
}

} // namespace Checkpoints
