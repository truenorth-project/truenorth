// Copyright (c) 2026 The TrueNorth developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <truenorth/numa.h>

#include <cstdio>
#include <mutex>

#ifdef HAVE_LIBNUMA
#include <numa.h>
#include <pthread.h>
#include <sched.h>
#endif

namespace truenorth::numa {

namespace {

std::mutex g_mutex;
NumaPref g_pref = NumaPref::AUTO;

// Cached runtime availability. -1 = not yet probed, 0 = unavailable,
// 1 = available. Probed once on first query and cached; NUMA topology
// doesn't change during process lifetime.
int g_available_cache = -1;
int g_num_nodes_cache = -1;

#ifdef HAVE_LIBNUMA
int ProbeAvailable()
{
    return numa_available() >= 0 ? 1 : 0;
}
int ProbeNumNodes()
{
    // numa_num_configured_nodes returns count including empty nodes.
    // numa_max_node + 1 is the same for typical systems. Use the
    // former.
    const int n = numa_num_configured_nodes();
    return n > 0 ? n : 1;
}
#else
int ProbeAvailable() { return 0; }
int ProbeNumNodes() { return 1; }
#endif

} // namespace

void SetPreference(NumaPref pref)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_pref = pref;
}

NumaPref CurrentPreference()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_pref;
}

const char* PrefName(NumaPref pref)
{
    switch (pref) {
    case NumaPref::AUTO: return "auto";
    case NumaPref::ON: return "on";
    case NumaPref::OFF: return "off";
    }
    return "?";
}

bool IsAvailable()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_available_cache == -1) {
        g_available_cache = ProbeAvailable();
    }
    return g_available_cache == 1;
}

int NumNodes()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_num_nodes_cache == -1) {
        if (g_available_cache == -1) g_available_cache = ProbeAvailable();
        g_num_nodes_cache = (g_available_cache == 1) ? ProbeNumNodes() : 1;
    }
    return g_num_nodes_cache;
}

bool ShouldEnable()
{
    // Read pref outside the g_mutex lock cascade (CurrentPreference
    // takes its own lock). Compare NumNodes independently.
    const NumaPref pref = CurrentPreference();
    if (pref == NumaPref::OFF) return false;
    if (!IsAvailable()) {
        if (pref == NumaPref::ON) {
            std::fprintf(stderr,
                         "TrueNorth: [ERROR] -numa=on requested but libnuma "
                         "unavailable (either not linked at build time or "
                         "numa_available() returned <0); NUMA is disabled.\n");
        }
        return false;
    }
    if (NumNodes() < 2) {
        if (pref == NumaPref::ON) {
            std::fprintf(stderr,
                         "TrueNorth: [INFO] -numa=on requested but the system "
                         "has only 1 NUMA node; NUMA pinning is a no-op here.\n");
        }
        return false;
    }
    return true;
}

bool BindThreadToNode(int node)
{
    if (!ShouldEnable()) return false;
    if (node < 0 || node >= NumNodes()) return false;

#ifdef HAVE_LIBNUMA
    // Bind memory allocations from this thread onward to the node.
    // Uses libnuma's internal per-thread membind state.
    numa_set_preferred(node);

    // Pin the thread's CPU affinity to the node's CPU set. libnuma's
    // numa_run_on_node handles the affinity syscall; we prefer to be
    // explicit with pthread_setaffinity_np so failures are visible.
    struct bitmask* cpus = numa_allocate_cpumask();
    if (cpus == nullptr) return false;
    const int rc = numa_node_to_cpus(node, cpus);
    if (rc != 0) {
        numa_free_cpumask(cpus);
        return false;
    }
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    const int max_cpus = static_cast<int>(cpus->size);
    for (int c = 0; c < max_cpus; ++c) {
        if (numa_bitmask_isbitset(cpus, c)) {
            CPU_SET(c, &cpuset);
        }
    }
    numa_free_cpumask(cpus);
    const int aff_rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    return aff_rc == 0;
#else
    (void)node;
    return false;
#endif
}

int NodeForThread(int thread_idx, int total_threads)
{
    if (!ShouldEnable()) return 0;
    if (total_threads < 1) return 0;
    const int n = NumNodes();
    if (n < 2) return 0;
    // Round-robin: thread 0 -> node 0, thread 1 -> node 1, ..., thread n
    // -> node 0. Even distribution across nodes independent of total
    // thread count.
    if (thread_idx < 0) return 0;
    return thread_idx % n;
}

} // namespace truenorth::numa
