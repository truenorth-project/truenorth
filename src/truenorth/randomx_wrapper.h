// Copyright (c) 2026 The TrueNorth developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef TRUENORTH_RANDOMX_WRAPPER_H
#define TRUENORTH_RANDOMX_WRAPPER_H

#include <uint256.h>

#include <cstddef>
#include <cstdint>

namespace truenorth {

// RandomX operates in two modes. Both produce IDENTICAL hashes for the
// same (seed, data) pair; the difference is purely resource + throughput.
enum class RandomXMode {
    LIGHT, //!< ~256 MiB cache only. Small footprint, ~5-30 H/s per thread. Node default.
    FAST,  //!< ~256 MiB cache + ~2 GiB dataset. Miner default when free RAM allows.
};

// Recommended headroom (in MiB) over the ~2080 MiB dataset before
// AutoDetectMinerMode picks FAST. 25% margin so a competing process
// pressuring RAM doesn't OOM-kill the miner right after startup.
inline constexpr std::uint64_t kFastModeMinAvailMiB = 2560;

// Choose LIGHT or FAST based on currently available memory. Returns
// FAST if AvailableMemoryMiB() >= min_free_mib, else LIGHT. On
// unsupported platforms (where AvailableMemoryMiB returns 0) this
// fails safe to LIGHT.
RandomXMode AutoDetectMinerMode(std::uint64_t min_free_mib = kFastModeMinAvailMiB);

// Set the mining mode for subsequent MinerThread construction. Called
// once at process startup by truenorth-miner after CLI parsing.
//
// SAFETY: not concurrent-safe. Do not change the mode while any
// MinerThread is alive: existing MinerThreads hold VMs that assume the
// current (cache, dataset) shape. truenorth-miner sets this once
// before spawning workers and never changes it.
//
// Called by: truenorth-miner. NOT called by the node (validation
// stays LIGHT unconditionally).
void SetMinerMode(RandomXMode mode);

// The mode most recently passed to SetMinerMode(), or LIGHT if
// SetMinerMode has never been called (default for the node).
RandomXMode CurrentMinerMode();

// Short label for logging: "light" / "fast".
const char* ModeName(RandomXMode mode);

// RandomX hash of `data` using `seed_key` as the cache key.
//
// The first call with a given seed allocates the RandomX cache, which
// takes 1-2 seconds. Calls with the same seed reuse that cache and only
// pay the per-hash cost (a few milliseconds in light-mode).
//
// Callers on the node (validation) side get light-mode behaviour
// regardless of SetMinerMode, because the node never calls SetMinerMode
// and CurrentMinerMode() defaults to LIGHT. Don't use this for mining
// at thread-parallel rates; it serializes on a global mutex. See
// MinerThread below for that.
//
// Thread-safe.
uint256 RandomXLightHash(const uint256& seed_key,
                         const unsigned char* data,
                         std::size_t size);

// Per-thread RandomX VM for mining. The constructor takes the global
// mutex briefly to make sure the shared cache (and, in FAST mode, the
// shared ~2 GiB dataset) exists for `seed_key`, then allocates a
// private VM bound to it. Hash() can be called from the constructing
// thread with no locking after that.
//
// The active mode (LIGHT vs FAST) is whatever SetMinerMode was last
// called with. FAST mode's first-for-a-given-seed construction pays
// the dataset-build cost (~10 s wall clock, parallelised across cores);
// subsequent MinerThreads for the same seed reuse it.
//
// Don't re-initialise the shared cache against a different seed while
// any MinerThread is still alive. The VMs hold pointers into the cache
// (and dataset) that would dangle. truenorth-miner spawns workers per
// block template, joins them all, then switches seeds, which avoids
// this.
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
