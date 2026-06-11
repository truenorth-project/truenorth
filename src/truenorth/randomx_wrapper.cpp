// Copyright (c) 2026 The TrueNorth developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <truenorth/randomx_wrapper.h>

#include <randomx.h>
#include <threadsafety.h>

#include <cstdlib>
#include <mutex>

namespace truenorth {

namespace {

// Lazily-initialized RandomX cache + light-mode VM, keyed by `g_current_seed`.
// Switching seeds incurs cache re-initialization (~1-2 seconds); a small LRU
// of caches across recent epochs would help once we have meaningful epoch
// turnover in production. Not needed for current correctness.
std::mutex g_mutex;
randomx_cache* g_cache GUARDED_BY(g_mutex) = nullptr;
randomx_vm* g_vm GUARDED_BY(g_mutex) = nullptr;
uint256 g_current_seed GUARDED_BY(g_mutex);
bool g_have_seed GUARDED_BY(g_mutex) = false;

// Releases g_cache and g_vm at process exit. Cosmetic in production -- the
// OS reclaims the memory at exit either way -- but eliminates the leak
// reports under AddressSanitizer / LeakSanitizer and gives us a tidier
// teardown story for whoever wires up sanitizer CI later.
//
// Destruction order: this object is constructed AFTER g_mutex / g_cache /
// g_vm (declaration order within the TU) and therefore destructed BEFORE
// them, so g_mutex is still live when ~GlobalCleanup runs.
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
        if (g_cache != nullptr) {
            randomx_release_cache(g_cache);
            g_cache = nullptr;
        }
        g_have_seed = false;
    }
};
const GlobalCleanup g_cleanup;

randomx_flags FlagsForLight()
{
    // Recommended host flags + RANDOMX_FLAG_V2 (program format v2).
    // randomx_get_flags() does not include V2 by default; we opt in because
    // TrueNorth uses the v2 program format from genesis.
    return randomx_get_flags() | RANDOMX_FLAG_V2;
}

void EnsureForSeed(const uint256& seed_key)
{
    if (g_have_seed && g_current_seed == seed_key) return;

    if (g_vm) {
        randomx_destroy_vm(g_vm);
        g_vm = nullptr;
    }
    if (g_cache) {
        randomx_release_cache(g_cache);
        g_cache = nullptr;
    }

    const randomx_flags flags = FlagsForLight();
    g_cache = randomx_alloc_cache(flags);
    if (g_cache == nullptr) {
        std::fprintf(stderr,
                     "TrueNorth: randomx_alloc_cache failed (out of memory or unsupported flags)\n");
        std::abort();
    }
    randomx_init_cache(g_cache, seed_key.begin(), 32);

    g_vm = randomx_create_vm(flags, g_cache, /*dataset=*/nullptr);
    if (g_vm == nullptr) {
        std::fprintf(stderr,
                     "TrueNorth: randomx_create_vm failed (out of memory or unsupported flags)\n");
        std::abort();
    }

    g_current_seed = seed_key;
    g_have_seed = true;
}

} // namespace

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
    // for this seed. After that, the private VM hashes without locking.
    std::lock_guard<std::mutex> lock(g_mutex);
    EnsureForSeed(seed_key);
    randomx_vm* vm = randomx_create_vm(FlagsForLight(), g_cache, /*dataset=*/nullptr);
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
