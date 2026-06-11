// Copyright (c) 2026 The TrueNorth developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef TRUENORTH_SEED_KEY_H
#define TRUENORTH_SEED_KEY_H

#include <uint256.h>

class CBlockIndex;

namespace truenorth {

// Seed key rotates every EPOCH_LENGTH blocks. The lag window after each
// rotation gives miners time to precompute the next dataset before it
// becomes active. Same values Monero uses.
constexpr int RANDOMX_EPOCH_LENGTH = 2048; // ~2.84 days at 120s blocks
constexpr int RANDOMX_SEED_LAG = 64;       // ~2 hours of overlap

// Seed for the first 2112 blocks (genesis epoch plus the lag window).
// There's no past block at genesis to derive a seed from, so a fixed
// constant is used.
extern const uint256 kGenesisSeed;

// Returns the height whose block hash should be used as the RandomX seed
// when validating or mining a block at `next_height`. Returns 0 during
// the genesis epoch; callers should use kGenesisSeed in that case.
//
//   seed_height = ((next_height - LAG) / EPOCH) * EPOCH   for next_height >= EPOCH + LAG
//   seed_height = 0                                       otherwise
int SeedHeightForNextHeight(int next_height);

// Returns the RandomX seed key for the block whose parent is `pprev`.
// During the genesis epoch (including pprev == nullptr) the result is
// kGenesisSeed. Past that, it walks the skip-list O(log N) back to the
// ancestor at the seed height and returns that block's hash.
uint256 SeedKeyForChild(const CBlockIndex* pprev);

} // namespace truenorth

#endif // TRUENORTH_SEED_KEY_H
