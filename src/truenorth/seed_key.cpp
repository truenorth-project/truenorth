// Copyright (c) 2026 The TrueNorth developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <truenorth/seed_key.h>

#include <chain.h>
#include <util/check.h>

namespace truenorth {

// Seed value used during the genesis epoch. The bytes themselves are
// arbitrary. All that matters is that every node uses the same value.
const uint256 kGenesisSeed = uint256::ZERO;

int SeedHeightForNextHeight(int next_height)
{
    if (next_height < RANDOMX_EPOCH_LENGTH + RANDOMX_SEED_LAG) {
        return 0;
    }
    return ((next_height - RANDOMX_SEED_LAG) / RANDOMX_EPOCH_LENGTH) * RANDOMX_EPOCH_LENGTH;
}

uint256 SeedKeyForChild(const CBlockIndex* pprev)
{
    const int next_height = pprev ? pprev->nHeight + 1 : 0;
    const int seed_height = SeedHeightForNextHeight(next_height);
    if (seed_height == 0) {
        return kGenesisSeed;
    }
    // Walk back to seed_height through the skip-list. seed_height is at
    // most pprev->nHeight by construction, so the ancestor exists unless
    // the chain index is broken. A null result here would mean a PoW
    // check against the wrong seed, so assert instead.
    const CBlockIndex* seed_block = Assert(pprev->GetAncestor(seed_height));
    return seed_block->GetBlockHash();
}

} // namespace truenorth
