// Copyright (c) 2026 The TrueNorth developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <truenorth/randomx_wrapper.h>

#include <randomx.h>
#include <threadsafety.h>
#include <truenorth/system_mem.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace truenorth {

namespace {

// Recommended host flags + RANDOMX_FLAG_V2 (program format v2).
// randomx_get_flags() does not include V2 by default; we opt in
// because TrueNorth uses the v2 program format from genesis.
randomx_flags FlagsForMode(RandomXMode mode)
{
    randomx_flags flags = randomx_get_flags() | RANDOMX_FLAG_V2;
    if (mode == RandomXMode::FAST) {
        // VMs must be told to use the dataset. Without this flag, they
        // ignore any dataset argument and run in light mode.
        flags |= RANDOMX_FLAG_FULL_MEM;
    }
    return flags;
}

// A single populated cache slot. Owns its randomx_cache, optional
// randomx_dataset (only in FAST mode), and a light-mode VM used by
// RandomXLightHash calls that land on this slot. Reference-counted via
// std::shared_ptr from the slot map + any in-flight MinerThread /
// hash call; destructor releases resources when the last reference
// drops.
class Cache
{
public:
    Cache(const uint256& seed_key, RandomXMode mode)
        : m_seed(seed_key), m_mode(mode)
    {
        const randomx_flags cache_flags = FlagsForMode(mode);
        m_cache = randomx_alloc_cache(cache_flags);
        if (m_cache == nullptr) {
            std::fprintf(stderr,
                         "TrueNorth: randomx_alloc_cache failed (out of memory or unsupported flags)\n");
            std::abort();
        }
        randomx_init_cache(m_cache, seed_key.begin(), 32);

        if (mode == RandomXMode::FAST) {
            m_dataset = randomx_alloc_dataset(cache_flags);
            if (m_dataset == nullptr) {
                // Dataset alloc failed -- likely OOM on a machine that
                // passed AutoDetectMinerMode at process start but is now
                // under memory pressure. Fall back to LIGHT for this
                // seed rather than abort; miner keeps making progress.
                std::fprintf(stderr,
                             "TrueNorth: randomx_alloc_dataset failed; falling back to LIGHT mode for this seed\n");
                randomx_release_cache(m_cache);
                m_mode = RandomXMode::LIGHT;
                m_cache = randomx_alloc_cache(FlagsForMode(RandomXMode::LIGHT));
                if (m_cache == nullptr) {
                    std::fprintf(stderr,
                                 "TrueNorth: cache re-alloc for LIGHT fallback also failed\n");
                    std::abort();
                }
                randomx_init_cache(m_cache, seed_key.begin(), 32);
            } else {
                BuildDatasetParallel();
            }
        }

        m_light_vm = randomx_create_vm(FlagsForMode(m_mode), m_cache, m_dataset);
        if (m_light_vm == nullptr) {
            std::fprintf(stderr,
                         "TrueNorth: randomx_create_vm failed in Cache constructor\n");
            std::abort();
        }
    }

    ~Cache()
    {
        if (m_light_vm != nullptr) randomx_destroy_vm(m_light_vm);
        if (m_dataset != nullptr) randomx_release_dataset(m_dataset);
        if (m_cache != nullptr) randomx_release_cache(m_cache);
    }

    Cache(const Cache&) = delete;
    Cache& operator=(const Cache&) = delete;
    Cache(Cache&&) = delete;
    Cache& operator=(Cache&&) = delete;

    const uint256& seed() const { return m_seed; }
    RandomXMode mode() const { return m_mode; }
    randomx_cache* raw_cache() const { return m_cache; }
    randomx_dataset* raw_dataset() const { return m_dataset; }

    // Hash `data` against this cache's shared VM. Serialised on
    // m_vm_mutex; concurrent validation calls against the same seed
    // block on this mutex, calls against different seeds do not.
    uint256 Hash(const unsigned char* data, std::size_t size)
    {
        std::lock_guard<std::mutex> lock(m_vm_mutex);
        uint256 result;
        randomx_calculate_hash(m_light_vm, data, size, result.begin());
        return result;
    }

    // Approximate bytes held by this Cache's underlying RandomX
    // resources. Uses the well-known ~256 MiB cache and ~2080 MiB
    // dataset figures from the RandomX spec; per-thread VMs held by
    // MinerThreads add ~256 MiB each but are not counted here.
    std::uint64_t allocated_bytes() const
    {
        std::uint64_t bytes = 256ULL * 1024ULL * 1024ULL;
        if (m_dataset != nullptr) bytes += 2080ULL * 1024ULL * 1024ULL;
        return bytes;
    }

private:
    // Parallelise dataset initialisation across all available cores.
    // Single-threaded init takes ~15-30 s on modern hardware; splitting
    // cuts this to ~5-10 s. Each worker handles a disjoint slice of the
    // dataset item range so no locking is needed inside randomx.
    void BuildDatasetParallel()
    {
        const unsigned long total_items = randomx_dataset_item_count();
        unsigned int nthreads = std::thread::hardware_concurrency();
        if (nthreads == 0) nthreads = 1;
        // Guard against pathological cases (embedded systems reporting
        // 64+ cores where per-thread work becomes tiny). Cap for sanity.
        nthreads = std::min(nthreads, 32u);
        const unsigned long per_thread = total_items / nthreads;

        std::vector<std::thread> workers;
        workers.reserve(nthreads);
        for (unsigned int t = 0; t < nthreads; ++t) {
            const unsigned long start = t * per_thread;
            const unsigned long count = (t == nthreads - 1) ? (total_items - start) : per_thread;
            workers.emplace_back([this, start, count] {
                randomx_init_dataset(m_dataset, m_cache, start, count);
            });
        }
        for (auto& w : workers) {
            w.join();
        }
    }

    uint256 m_seed;
    RandomXMode m_mode;
    randomx_cache* m_cache{nullptr};
    randomx_dataset* m_dataset{nullptr}; //!< nullptr in LIGHT mode
    randomx_vm* m_light_vm{nullptr};
    std::mutex m_vm_mutex;
};

// Two-slot LRU. g_main is the most-recently-used cache; g_secondary is
// the previous main after promotion. shared_ptr semantics keep an
// evicted Cache alive as long as any MinerThread or hash call holds it.
std::mutex g_slot_mutex;
std::shared_ptr<Cache> g_main GUARDED_BY(g_slot_mutex);
std::shared_ptr<Cache> g_secondary GUARDED_BY(g_slot_mutex);

RandomXMode g_active_mode GUARDED_BY(g_slot_mutex) = RandomXMode::LIGHT;

// Releases both slots at process exit. Cosmetic in production -- the OS
// reclaims the memory at exit either way -- but eliminates leak reports
// under AddressSanitizer / LeakSanitizer and gives us a tidy teardown
// story. Constructed after g_slot_mutex so it is destroyed before, and
// dropping shared_ptrs is thread-safe against concurrent slot access
// which shouldn't be happening at process exit anyway.
struct GlobalCleanup {
    ~GlobalCleanup()
    {
        std::lock_guard<std::mutex> lock(g_slot_mutex);
        g_secondary.reset();
        g_main.reset();
    }
};
const GlobalCleanup g_cleanup;

// Look up an existing Cache by seed. Returns the shared_ptr if either
// slot matches, else nullptr. On secondary hit, swaps main <-> secondary
// so the just-used cache becomes main (LRU semantics).
std::shared_ptr<Cache> LookupAndPromote(const uint256& seed_key)
    EXCLUSIVE_LOCKS_REQUIRED(g_slot_mutex)
{
    if (g_main && g_main->seed() == seed_key) {
        return g_main;
    }
    if (g_secondary && g_secondary->seed() == seed_key) {
        std::swap(g_main, g_secondary);
        return g_main;
    }
    return nullptr;
}

// Miss handler: allocate a fresh Cache and install it as main; the
// previous main becomes secondary; whatever was in secondary is
// released from the slot map (shared_ptr may keep it alive if callers
// still hold it).
std::shared_ptr<Cache> InstallNewMain(const uint256& seed_key, RandomXMode mode)
    EXCLUSIVE_LOCKS_REQUIRED(g_slot_mutex)
{
    auto fresh = std::make_shared<Cache>(seed_key, mode);
    g_secondary = std::move(g_main);
    g_main = fresh;
    return g_main;
}

// Public-ish acquire: look up in slots, or install a new main. Callers
// pass their preferred mode; if the existing cache has a stronger mode
// (already FAST) that's returned as-is because the FAST cache also
// serves LIGHT hashing correctly. If the existing cache is LIGHT and
// the caller wants FAST, we don't upgrade in place -- we install a
// fresh FAST main, the existing LIGHT demotes to secondary. That
// double-holds the cache briefly (until the LIGHT one falls out of
// secondary later) but avoids invalidating any in-flight users of the
// LIGHT cache.
std::shared_ptr<Cache> AcquireForSeed(const uint256& seed_key, RandomXMode preferred_mode)
{
    std::lock_guard<std::mutex> lock(g_slot_mutex);
    if (auto existing = LookupAndPromote(seed_key)) {
        const bool need_upgrade =
            (preferred_mode == RandomXMode::FAST) && (existing->mode() == RandomXMode::LIGHT);
        if (!need_upgrade) {
            return existing;
        }
        // Fall through to install a fresh FAST main.
    }
    return InstallNewMain(seed_key, preferred_mode);
}

} // namespace

RandomXMode AutoDetectMinerMode(std::uint64_t min_free_mib)
{
    return AvailableMemoryMiB() >= min_free_mib ? RandomXMode::FAST : RandomXMode::LIGHT;
}

void SetMinerMode(RandomXMode mode)
{
    std::lock_guard<std::mutex> lock(g_slot_mutex);
    g_active_mode = mode;
}

RandomXMode CurrentMinerMode()
{
    std::lock_guard<std::mutex> lock(g_slot_mutex);
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

std::size_t RandomXCacheAllocations()
{
    std::lock_guard<std::mutex> lock(g_slot_mutex);
    std::size_t n = 0;
    if (g_main) ++n;
    if (g_secondary) ++n;
    return n;
}

std::uint64_t RandomXCacheAllocatedBytes()
{
    std::lock_guard<std::mutex> lock(g_slot_mutex);
    std::uint64_t bytes = 0;
    if (g_main) bytes += g_main->allocated_bytes();
    if (g_secondary) bytes += g_secondary->allocated_bytes();
    return bytes;
}

uint256 RandomXLightHash(const uint256& seed_key,
                         const unsigned char* data,
                         std::size_t size)
{
    // Validation always uses LIGHT preferred mode. If the miner already
    // installed a FAST cache for this seed, AcquireForSeed reuses it
    // (correct -- fast-mode VMs also serve light hashing calls).
    auto cache = AcquireForSeed(seed_key, RandomXMode::LIGHT);
    return cache->Hash(data, size);
}

// -------------------------------------------------------------------
// MinerThread
// -------------------------------------------------------------------

struct MinerThread::Impl {
    std::shared_ptr<Cache> cache;
    randomx_vm* vm{nullptr};
};

MinerThread::MinerThread(const uint256& seed_key)
    : m_impl(std::make_unique<Impl>())
{
    const RandomXMode mode = CurrentMinerMode();
    m_impl->cache = AcquireForSeed(seed_key, mode);
    m_impl->vm = randomx_create_vm(FlagsForMode(m_impl->cache->mode()),
                                   m_impl->cache->raw_cache(),
                                   m_impl->cache->raw_dataset());
    if (m_impl->vm == nullptr) {
        std::fprintf(stderr,
                     "TrueNorth: randomx_create_vm failed in MinerThread (out of memory or unsupported flags)\n");
        std::abort();
    }
}

MinerThread::~MinerThread()
{
    if (m_impl && m_impl->vm != nullptr) {
        randomx_destroy_vm(m_impl->vm);
    }
}

void MinerThread::Hash(const unsigned char* data, std::size_t size, uint256& out)
{
    randomx_calculate_hash(m_impl->vm, data, size, out.begin());
}

} // namespace truenorth
