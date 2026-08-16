// Copyright (c) 2026 The TrueNorth developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <truenorth/randomx_wrapper.h>

#include <randomx.h>
#include <threadsafety.h>
#include <truenorth/system_mem.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

namespace truenorth {

namespace {

// Lazily-initialised RandomX cache, dataset (FAST only), and light-mode VM,
// keyed by `g_current_seed`. Switching seeds rebuilds cache (~1-2 s) plus
// dataset (~10 s wall in FAST mode).
std::mutex g_mutex;
randomx_cache* g_cache GUARDED_BY(g_mutex) = nullptr;
randomx_dataset* g_dataset GUARDED_BY(g_mutex) = nullptr; //!< only allocated in FAST mode
randomx_vm* g_vm GUARDED_BY(g_mutex) = nullptr;
uint256 g_current_seed GUARDED_BY(g_mutex);
bool g_have_seed GUARDED_BY(g_mutex) = false;
RandomXMode g_active_mode GUARDED_BY(g_mutex) = RandomXMode::LIGHT;

// The mode used to build (g_cache, g_dataset, g_vm). Distinct from
// g_active_mode: g_active_mode is what SetMinerMode requested; g_built_mode
// is what the current allocations reflect. A mismatch means we must tear
// down and rebuild (e.g. mode changed between epochs). Under normal
// operation the two are equal.
RandomXMode g_built_mode GUARDED_BY(g_mutex) = RandomXMode::LIGHT;

// Releases g_vm / g_dataset / g_cache at process exit. Cosmetic in
// production -- the OS reclaims the memory at exit either way -- but
// eliminates the leak reports under AddressSanitizer / LeakSanitizer and
// gives us a tidier teardown story for whoever wires up sanitizer CI later.
//
// Destruction order: this object is constructed AFTER g_mutex / g_cache /
// g_dataset / g_vm (declaration order within the TU) and therefore
// destructed BEFORE them, so g_mutex is still live when ~GlobalCleanup runs.
//
// MinerThread instances are NOT cleaned up here -- each MinerThread owns
// its own VM and releases it in its own destructor. Whoever uses
// MinerThread must join the worker thread before process exit; if they
// don't, the OS reclaims the leftover VM anyway.
struct GlobalCleanup {
    ~GlobalCleanup()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_vm != nullptr) {
            randomx_destroy_vm(g_vm);
            g_vm = nullptr;
        }
        if (g_dataset != nullptr) {
            randomx_release_dataset(g_dataset);
            g_dataset = nullptr;
        }
        if (g_cache != nullptr) {
            randomx_release_cache(g_cache);
            g_cache = nullptr;
        }
        g_have_seed = false;
    }
};
const GlobalCleanup g_cleanup;

randomx_flags FlagsForMode(RandomXMode mode)
{
    // Recommended host flags + RANDOMX_FLAG_V2 (program format v2).
    // randomx_get_flags() does not include V2 by default; we opt in
    // because TrueNorth uses the v2 program format from genesis.
    randomx_flags flags = randomx_get_flags() | RANDOMX_FLAG_V2;
    if (mode == RandomXMode::FAST) {
        // VMs must be told to use the dataset. Without this, they'd
        // ignore g_dataset and run in light mode.
        flags |= RANDOMX_FLAG_FULL_MEM;
    }
    return flags;
}

// Parallelise dataset initialisation across all available cores.
// Single-threaded init takes ~15-30 s on modern hardware; splitting cuts
// this to ~5-10 s. Each worker handles a disjoint slice of the dataset
// item range so no locking is needed.
void BuildDatasetParallel(randomx_dataset* dataset, randomx_cache* cache)
{
    const unsigned long total_items = randomx_dataset_item_count();
    unsigned int nthreads = std::thread::hardware_concurrency();
    if (nthreads == 0) nthreads = 1;
    // Guard against pathological cases (embedded systems reporting 64+
    // cores where per-thread work becomes tiny). Cap for sanity.
    nthreads = std::min(nthreads, 32u);
    const unsigned long per_thread = total_items / nthreads;

    std::vector<std::thread> workers;
    workers.reserve(nthreads);
    for (unsigned int t = 0; t < nthreads; ++t) {
        const unsigned long start = t * per_thread;
        const unsigned long count = (t == nthreads - 1) ? (total_items - start) : per_thread;
        workers.emplace_back([dataset, cache, start, count] {
            randomx_init_dataset(dataset, cache, start, count);
        });
    }
    for (auto& w : workers)
        w.join();
}

void EnsureForSeed(const uint256& seed_key) EXCLUSIVE_LOCKS_REQUIRED(g_mutex)
{
    const bool same_seed = g_have_seed && g_current_seed == seed_key;
    const bool same_mode = g_built_mode == g_active_mode;
    if (same_seed && same_mode) return;

    // Tear down in reverse allocation order.
    if (g_vm) {
        randomx_destroy_vm(g_vm);
        g_vm = nullptr;
    }
    if (g_dataset) {
        randomx_release_dataset(g_dataset);
        g_dataset = nullptr;
    }
    if (g_cache) {
        randomx_release_cache(g_cache);
        g_cache = nullptr;
    }

    const RandomXMode mode = g_active_mode;
    const randomx_flags flags = FlagsForMode(mode);

    g_cache = randomx_alloc_cache(flags);
    if (g_cache == nullptr) {
        std::fprintf(stderr,
                     "TrueNorth: randomx_alloc_cache failed (out of memory or unsupported flags)\n");
        std::abort();
    }
    randomx_init_cache(g_cache, seed_key.begin(), 32);

    if (mode == RandomXMode::FAST) {
        g_dataset = randomx_alloc_dataset(flags);
        if (g_dataset == nullptr) {
            // Dataset alloc failed -- likely OOM on a machine that passed
            // the AutoDetectMinerMode threshold at process start but is now
            // under memory pressure. Rather than abort, silently fall back
            // to LIGHT for this seed. Miner keeps making progress; user
            // sees the mode-name in subsequent logs.
            std::fprintf(stderr,
                         "TrueNorth: randomx_alloc_dataset failed; falling back to LIGHT mode for this seed\n");
            // Rebuild cache with LIGHT flags (RANDOMX_FLAG_FULL_MEM removed);
            // otherwise the VM below will still expect a dataset.
            randomx_release_cache(g_cache);
            g_cache = randomx_alloc_cache(FlagsForMode(RandomXMode::LIGHT));
            if (g_cache == nullptr) {
                std::fprintf(stderr, "TrueNorth: cache re-alloc for LIGHT fallback also failed\n");
                std::abort();
            }
            randomx_init_cache(g_cache, seed_key.begin(), 32);
            g_active_mode = RandomXMode::LIGHT;
            g_built_mode = RandomXMode::LIGHT;
        } else {
            BuildDatasetParallel(g_dataset, g_cache);
            g_built_mode = RandomXMode::FAST;
        }
    } else {
        g_built_mode = RandomXMode::LIGHT;
    }

    g_vm = randomx_create_vm(FlagsForMode(g_built_mode), g_cache, g_dataset);
    if (g_vm == nullptr) {
        std::fprintf(stderr,
                     "TrueNorth: randomx_create_vm failed (out of memory or unsupported flags)\n");
        std::abort();
    }

    g_current_seed = seed_key;
    g_have_seed = true;
}

} // namespace

RandomXMode AutoDetectMinerMode(std::uint64_t min_free_mib)
{
    return AvailableMemoryMiB() >= min_free_mib ? RandomXMode::FAST : RandomXMode::LIGHT;
}

void SetMinerMode(RandomXMode mode)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_active_mode = mode;
    // The next EnsureForSeed call will notice the mismatch with g_built_mode
    // and rebuild. If no MinerThread is ever constructed, no allocation
    // happens.
}

RandomXMode CurrentMinerMode()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_active_mode;
}

const char* ModeName(RandomXMode mode)
{
    switch (mode) {
    case RandomXMode::LIGHT: return "light";
    case RandomXMode::FAST: return "fast";
    }
    return "?";
}

uint256 RandomXLightHash(const uint256& seed_key,
                         const unsigned char* data,
                         std::size_t size)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    EnsureForSeed(seed_key);

    uint256 result;
    randomx_calculate_hash(g_vm, data, size, result.begin());
    return result;
}

MinerThread::MinerThread(const uint256& seed_key) : m_vm(nullptr)
{
    // Take the global mutex just long enough to set up the shared cache
    // (and dataset in FAST mode) for this seed. After that, the private VM
    // hashes without locking.
    std::lock_guard<std::mutex> lock(g_mutex);
    EnsureForSeed(seed_key);
    randomx_vm* vm = randomx_create_vm(FlagsForMode(g_built_mode), g_cache, g_dataset);
    if (vm == nullptr) {
        std::fprintf(stderr,
                     "TrueNorth: randomx_create_vm failed in MinerThread (out of memory or unsupported flags)\n");
        std::abort();
    }
    m_vm = vm;
}

MinerThread::~MinerThread()
{
    if (m_vm != nullptr) {
        randomx_destroy_vm(static_cast<randomx_vm*>(m_vm));
    }
}

void MinerThread::Hash(const unsigned char* data, std::size_t size, uint256& out)
{
    randomx_calculate_hash(static_cast<randomx_vm*>(m_vm), data, size, out.begin());
}

} // namespace truenorth
