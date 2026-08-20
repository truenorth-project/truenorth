// Copyright (c) 2026 The TrueNorth developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <truenorth/randomx_wrapper.h>

#include <randomx.h>
#include <threadsafety.h>
#include <truenorth/numa.h>
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
//
// `with_large_pages` opts into RANDOMX_FLAG_LARGE_PAGES for the alloc
// this flag set will drive. Callers should retry without it if the
// alloc fails (RandomX returns nullptr rather than aborting when the
// huge page allocation is denied).
randomx_flags FlagsForMode(RandomXMode mode, bool with_large_pages)
{
    randomx_flags flags = randomx_get_flags() | RANDOMX_FLAG_V2;
    if (mode == RandomXMode::FAST) {
        // VMs must be told to use the dataset. Without this flag, they
        // ignore any dataset argument and run in light mode.
        flags |= RANDOMX_FLAG_FULL_MEM;
    }
    if (with_large_pages) {
        flags |= RANDOMX_FLAG_LARGE_PAGES;
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
    Cache(const uint256& seed_key, RandomXMode mode, LargePagesPref lp_pref)
        : m_seed(seed_key), m_mode(mode)
    {
        // Try huge pages first if requested; on any allocation failure
        // fall back to normal pages. Track the state we ended up in
        // so miner startup can log it. If pref==ON and we fall back,
        // log at ERROR so the operator sees the failure to configure.
        bool try_large = (lp_pref != LargePagesPref::OFF);
        const bool must_have_large = (lp_pref == LargePagesPref::ON);

        randomx_flags flags = FlagsForMode(mode, try_large);
        m_cache = randomx_alloc_cache(flags);
        if (m_cache == nullptr && try_large) {
            std::fprintf(stderr,
                         "TrueNorth: [%s] randomx_alloc_cache failed with LARGE_PAGES; "
                         "retrying without huge pages (configure vm.nr_hugepages on Linux, "
                         "or SeLockMemoryPrivilege on Windows, to get the ~10-20%% hashrate boost)\n",
                         must_have_large ? "ERROR" : "INFO");
            try_large = false;
            flags = FlagsForMode(mode, try_large);
            m_cache = randomx_alloc_cache(flags);
        }
        if (m_cache == nullptr) {
            std::fprintf(stderr,
                         "TrueNorth: randomx_alloc_cache failed even without LARGE_PAGES "
                         "(out of memory or unsupported flags)\n");
            std::abort();
        }
        randomx_init_cache(m_cache, seed_key.begin(), 32);

        if (mode == RandomXMode::FAST) {
            m_dataset = randomx_alloc_dataset(flags);
            if (m_dataset == nullptr && try_large) {
                // Cache succeeded with LARGE_PAGES but dataset alloc
                // failed -- likely not enough huge pages reserved for
                // both. Retry the dataset alone without huge pages;
                // keep the cache as-is (mixed configuration is fine).
                std::fprintf(stderr,
                             "TrueNorth: [%s] randomx_alloc_dataset failed with LARGE_PAGES; "
                             "retrying without huge pages\n",
                             must_have_large ? "ERROR" : "INFO");
                randomx_flags ds_flags = FlagsForMode(mode, false);
                m_dataset = randomx_alloc_dataset(ds_flags);
                if (m_dataset != nullptr) {
                    // Cache retains large pages but dataset does not.
                    // Downgrade m_used_large_pages accounting to false
                    // since the biggest allocation is on normal pages.
                    try_large = false;
                }
            }
            if (m_dataset == nullptr) {
                // Fall back to LIGHT mode for this seed rather than
                // abort; miner keeps making progress at reduced speed.
                std::fprintf(stderr,
                             "TrueNorth: randomx_alloc_dataset failed; falling back to LIGHT mode for this seed\n");
                randomx_release_cache(m_cache);
                m_mode = RandomXMode::LIGHT;
                // Re-alloc the cache under LIGHT flags (may still try
                // huge pages if we haven't hit OOM there yet).
                randomx_flags light_flags = FlagsForMode(RandomXMode::LIGHT, try_large);
                m_cache = randomx_alloc_cache(light_flags);
                if (m_cache == nullptr && try_large) {
                    try_large = false;
                    light_flags = FlagsForMode(RandomXMode::LIGHT, false);
                    m_cache = randomx_alloc_cache(light_flags);
                }
                if (m_cache == nullptr) {
                    std::fprintf(stderr, "TrueNorth: cache re-alloc for LIGHT fallback also failed\n");
                    std::abort();
                }
                randomx_init_cache(m_cache, seed_key.begin(), 32);
            } else {
                BuildDatasetParallel();
            }
        }

        // VM alloc: try with the same large-pages preference as the
        // cache. VM scratchpad is 2 MiB (small), retry-without on
        // failure is straightforward.
        m_light_vm = randomx_create_vm(FlagsForMode(m_mode, try_large), m_cache, m_dataset);
        if (m_light_vm == nullptr && try_large) {
            m_light_vm = randomx_create_vm(FlagsForMode(m_mode, false), m_cache, m_dataset);
        }
        if (m_light_vm == nullptr) {
            std::fprintf(stderr,
                         "TrueNorth: randomx_create_vm failed in Cache constructor\n");
            std::abort();
        }

        m_used_large_pages = try_large;
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
    bool used_large_pages() const { return m_used_large_pages; }

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
    bool m_used_large_pages{false};
};

// Per-NUMA-node two-slot LRU. On single-node systems (or when NUMA is
// disabled), g_slots_by_node has one entry. On multi-socket systems
// with NUMA on, one entry per node -- MinerThreads pin to a specific
// node and use that node's slot; RandomX allocations happen on that
// node's local memory (because the calling thread is pinned there
// during the alloc, and the underlying mmap/VirtualAlloc respects
// current-thread affinity).
//
// shared_ptr semantics keep an evicted Cache alive as long as any
// MinerThread or hash call holds it, regardless of which node slot it
// came from.
struct NodeSlots {
    std::shared_ptr<Cache> main;
    std::shared_ptr<Cache> secondary;
};

std::mutex g_slot_mutex;
std::vector<NodeSlots> g_slots_by_node GUARDED_BY(g_slot_mutex);

RandomXMode g_active_mode GUARDED_BY(g_slot_mutex) = RandomXMode::LIGHT;
LargePagesPref g_lp_pref GUARDED_BY(g_slot_mutex) = LargePagesPref::AUTO;

// Releases all slots at process exit. Cosmetic in production -- the OS
// reclaims the memory at exit either way -- but eliminates leak reports
// under AddressSanitizer / LeakSanitizer and gives us a tidy teardown
// story.
struct GlobalCleanup {
    ~GlobalCleanup()
    {
        std::lock_guard<std::mutex> lock(g_slot_mutex);
        g_slots_by_node.clear();
    }
};
const GlobalCleanup g_cleanup;

// Ensure g_slots_by_node has the right number of entries. Called at
// the top of every slot-touching function. NUMA topology doesn't
// change during process lifetime so resizing happens once at first
// use.
void EnsureSlotsInitialized() EXCLUSIVE_LOCKS_REQUIRED(g_slot_mutex)
{
    if (!g_slots_by_node.empty()) return;
    const int n = numa::ShouldEnable() ? numa::NumNodes() : 1;
    g_slots_by_node.resize(n);
}

// Clamp a caller-supplied node index into a valid slot index. NUMA-
// off systems only have slot 0.
int ClampNode(int node) EXCLUSIVE_LOCKS_REQUIRED(g_slot_mutex)
{
    if (node < 0) return 0;
    if (static_cast<std::size_t>(node) >= g_slots_by_node.size()) return 0;
    return node;
}

// Look up an existing Cache by seed within a specific node's slots.
// Returns the shared_ptr on hit; nullptr on miss. Secondary hit
// promotes to main (LRU) within that node.
std::shared_ptr<Cache> LookupAndPromote(const uint256& seed_key, int node)
    EXCLUSIVE_LOCKS_REQUIRED(g_slot_mutex)
{
    auto& slots = g_slots_by_node[node];
    if (slots.main && slots.main->seed() == seed_key) {
        return slots.main;
    }
    if (slots.secondary && slots.secondary->seed() == seed_key) {
        std::swap(slots.main, slots.secondary);
        return slots.main;
    }
    return nullptr;
}

// Miss handler: allocate a fresh Cache for the given node. Because
// the calling thread is expected to have been pinned to `node` by
// its MinerThread constructor before this call, RandomX's underlying
// mmap/VirtualAlloc lands on that node's local memory.
std::shared_ptr<Cache> InstallNewMain(const uint256& seed_key, RandomXMode mode, int node)
    EXCLUSIVE_LOCKS_REQUIRED(g_slot_mutex)
{
    auto fresh = std::make_shared<Cache>(seed_key, mode, g_lp_pref);
    auto& slots = g_slots_by_node[node];
    slots.secondary = std::move(slots.main);
    slots.main = fresh;
    return slots.main;
}

// Acquire a cache for the given seed on the given node. Callers pass
// their preferred mode; if the existing cache has a stronger mode
// (already FAST) that's returned as-is. If existing is LIGHT and
// caller wants FAST, install a fresh FAST main and demote the LIGHT
// to secondary within the same node.
std::shared_ptr<Cache> AcquireForSeed(const uint256& seed_key, RandomXMode preferred_mode, int node)
{
    std::lock_guard<std::mutex> lock(g_slot_mutex);
    EnsureSlotsInitialized();
    node = ClampNode(node);
    if (auto existing = LookupAndPromote(seed_key, node)) {
        const bool need_upgrade =
            (preferred_mode == RandomXMode::FAST) && (existing->mode() == RandomXMode::LIGHT);
        if (!need_upgrade) {
            return existing;
        }
    }
    return InstallNewMain(seed_key, preferred_mode, node);
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

void SetLargePagesPreference(LargePagesPref pref)
{
    std::lock_guard<std::mutex> lock(g_slot_mutex);
    g_lp_pref = pref;
}

LargePagesPref CurrentLargePagesPreference()
{
    std::lock_guard<std::mutex> lock(g_slot_mutex);
    return g_lp_pref;
}

const char* LargePagesPrefName(LargePagesPref pref)
{
    switch (pref) {
    case LargePagesPref::AUTO: return "auto";
    case LargePagesPref::ON: return "on";
    case LargePagesPref::OFF: return "off";
    }
    return "?";
}

bool RandomXCacheUsedLargePages()
{
    std::lock_guard<std::mutex> lock(g_slot_mutex);
    // Report the state of the first populated main slot. If no slot
    // is allocated yet, we haven't attempted an allocation, so
    // effectively false. In practice all nodes have consistent
    // large-pages state (they use the same g_lp_pref at alloc time).
    for (const auto& slots : g_slots_by_node) {
        if (slots.main) return slots.main->used_large_pages();
    }
    return false;
}

std::size_t RandomXCacheAllocations()
{
    std::lock_guard<std::mutex> lock(g_slot_mutex);
    std::size_t n = 0;
    for (const auto& slots : g_slots_by_node) {
        if (slots.main) ++n;
        if (slots.secondary) ++n;
    }
    return n;
}

std::uint64_t RandomXCacheAllocatedBytes()
{
    std::lock_guard<std::mutex> lock(g_slot_mutex);
    std::uint64_t bytes = 0;
    for (const auto& slots : g_slots_by_node) {
        if (slots.main) bytes += slots.main->allocated_bytes();
        if (slots.secondary) bytes += slots.secondary->allocated_bytes();
    }
    return bytes;
}

uint256 RandomXLightHash(const uint256& seed_key,
                         const unsigned char* data,
                         std::size_t size)
{
    // Validation is single-threaded and doesn't benefit from NUMA
    // pinning; always use node 0. If the miner already installed a
    // FAST cache for this seed on node 0, AcquireForSeed reuses it
    // (correct -- fast-mode VMs also serve light hashing calls).
    // On NUMA-off / single-node systems this is the only slot anyway.
    auto cache = AcquireForSeed(seed_key, RandomXMode::LIGHT, /*node=*/0);
    return cache->Hash(data, size);
}

// -------------------------------------------------------------------
// MinerThread
// -------------------------------------------------------------------

struct MinerThread::Impl {
    std::shared_ptr<Cache> cache;
    randomx_vm* vm{nullptr};
};

MinerThread::MinerThread(const uint256& seed_key, int numa_node)
    : m_impl(std::make_unique<Impl>())
{
    // Pin the constructing thread (which is expected to be the worker
    // thread that will later call Hash()) to the requested NUMA node
    // BEFORE we allocate the Cache. This ensures RandomX's internal
    // mmap / VirtualAlloc land on that node's local memory. On single-
    // node / NUMA-off systems this is a no-op.
    if (numa::ShouldEnable()) {
        numa::BindThreadToNode(numa_node);
    }

    const RandomXMode mode = CurrentMinerMode();
    m_impl->cache = AcquireForSeed(seed_key, mode, numa_node);

    // Per-thread VM scratchpad is 2 MiB. Try huge pages if the
    // underlying Cache uses them (consistency); retry without on
    // failure (retry succeeding is expected because scratchpad is
    // small enough that a huge page is usually available even after
    // cache/dataset reservations).
    const bool try_large = m_impl->cache->used_large_pages();
    m_impl->vm = randomx_create_vm(FlagsForMode(m_impl->cache->mode(), try_large),
                                   m_impl->cache->raw_cache(),
                                   m_impl->cache->raw_dataset());
    if (m_impl->vm == nullptr && try_large) {
        m_impl->vm = randomx_create_vm(FlagsForMode(m_impl->cache->mode(), false),
                                       m_impl->cache->raw_cache(),
                                       m_impl->cache->raw_dataset());
    }
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
