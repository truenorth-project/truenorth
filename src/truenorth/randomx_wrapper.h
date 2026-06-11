// Copyright (c) 2026 The TrueNorth developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef TRUENORTH_RANDOMX_WRAPPER_H
#define TRUENORTH_RANDOMX_WRAPPER_H

#include <uint256.h>

#include <cstddef>

namespace truenorth {

// RandomX hash of `data` using `seed_key` as the cache key.
//
// The first call with a given seed allocates the RandomX cache, which
// takes 1-2 seconds. Calls with the same seed reuse that cache and only
// pay the per-hash cost (a few milliseconds in light-mode).
//
// Light-mode means ~256 MiB cache, no 2 GiB dataset. Fine for block and
// header validation. Don't use this for mining at thread-parallel rates;
// it serializes on a global mutex. See MinerThread below for that.
//
// Thread-safe.
uint256 RandomXLightHash(const uint256& seed_key,
                         const unsigned char* data,
                         std::size_t size);

// Per-thread RandomX VM for mining. The constructor takes the global
// mutex briefly to make sure the shared cache exists for `seed_key`, then
// allocates a private VM bound to that cache. Hash() can be called from
// the constructing thread with no locking after that.
//
// Don't re-initialise the shared cache against a different seed while any
// MinerThread is still alive. The VMs hold pointers into the cache that
// would dangle. truenorth-miner spawns workers per block template, joins
// them all, then switches seeds, which avoids this.
class MinerThread
{
public:
    explicit MinerThread(const uint256& seed_key);
    ~MinerThread();
    MinerThread(const MinerThread&) = delete;
    MinerThread& operator=(const MinerThread&) = delete;

    // Compute the RandomX hash of `data` using this thread's VM. Output is
    // written to `out`. No locking; safe to call from one thread only (the
    // one that constructed this MinerThread).
    void Hash(const unsigned char* data, std::size_t size, uint256& out);

private:
    void* m_vm; // randomx_vm* (opaque so callers don't need <randomx.h>)
};

} // namespace truenorth

#endif // TRUENORTH_RANDOMX_WRAPPER_H
