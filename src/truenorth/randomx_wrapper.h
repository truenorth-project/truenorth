// Copyright (c) 2026 The TrueNorth developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef TRUENORTH_RANDOMX_WRAPPER_H
#define TRUENORTH_RANDOMX_WRAPPER_H

#include <uint256.h>

#include <cstddef>
#include <cstdint>
#include <memory>

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
// SAFETY: not concurrent-safe with existing MinerThreads on a different
// mode. Set once before spawning workers.
//
// Called by: truenorth-miner. NOT called by the node (validation
// stays LIGHT unconditionally).
void SetMinerMode(RandomXMode mode);

// The mode most recently passed to SetMinerMode(), or LIGHT if
// SetMinerMode has never been called (default for the node).
RandomXMode CurrentMinerMode();

// Short label for logging: "light" / "fast".
const char* ModeName(RandomXMode mode);

// -------------------------------------------------------------------
// Two-slot cache accessors (test-only)
//
// The wrapper keeps a main + secondary cache slot per tevador/RandomX
// guidance. This lets validation replay blocks from a recent prior
// seed epoch (chain reorg across a seed-rotation boundary) without
// paying the ~1s cache-init cost each time, and lets an unexpected
// old-seed template survive at least one lookup. Slots are populated
// lazily as unique seeds are requested; miss on both slots evicts the
// secondary, demotes the main, and installs a fresh main.
//
// In-flight MinerThreads and hash calls hold a shared_ptr to their
// Cache, so eviction from the slot map does NOT free the underlying
// resources until the last user drops the reference.
// -------------------------------------------------------------------

// Number of cache slots currently populated (0, 1, or 2).
std::size_t RandomXCacheAllocations();

// Total bytes allocated across populated slots. Approximate: uses the
// nominal ~256 MiB cache size and ~2080 MiB dataset size (fast mode
// only). Doesn't include per-thread VM overhead.
std::uint64_t RandomXCacheAllocatedBytes();

// -------------------------------------------------------------------

// RandomX hash of `data` using `seed_key` as the cache key.
//
// Consults the two-slot cache: hit main -> reuse; hit secondary ->
// swap-to-main; miss -> allocate new LIGHT-mode cache as main and
// demote previous main. If the miner has already installed a FAST
// cache for `seed_key`, this reuses that cache's light VM (still
// correct — fast-mode VMs also hash correctly, just with dataset
// access).
//
// Thread-safe. Called by block validation.
uint256 RandomXLightHash(const uint256& seed_key,
                         const unsigned char* data,
                         std::size_t size);

// Per-thread RandomX VM for mining. Constructor grabs the slot mutex
// briefly to acquire or install the Cache for `seed_key` at the
// currently-active miner mode (LIGHT or FAST per SetMinerMode).
// The MinerThread holds a shared_ptr<Cache> internally, keeping the
// cache alive even if it's later evicted from the slot map (e.g.
// because a new seed came in). Hash() then runs lock-free on the
// thread-private VM.
//
// The active mode (LIGHT vs FAST) is whatever SetMinerMode was last
// called with. FAST mode's first-for-a-given-seed construction pays
// the dataset-build cost (~10 s wall clock, parallelised across cores);
// subsequent MinerThreads for the same seed reuse the same cache.
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
    // Pimpl hides std::shared_ptr<Cache> and randomx_vm* from callers so
    // they don't need <randomx.h> or the internal Cache type.
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace truenorth

#endif // TRUENORTH_RANDOMX_WRAPPER_H
