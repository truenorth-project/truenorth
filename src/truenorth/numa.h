// Copyright (c) 2026 The TrueNorth developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef TRUENORTH_NUMA_H
#define TRUENORTH_NUMA_H

// NUMA topology + thread-pinning helpers.
//
// On multi-socket systems (dual-Xeon, dual-EPYC), each socket has its
// own memory controllers and local RAM (a NUMA node). Memory local to
// a socket is accessed at ~80-90 ns; memory on the other socket costs
// a QPI/UPI hop and takes ~180-250 ns -- 2-3x slower.
//
// RandomX fast-mode does ~2 GB of random-access dataset lookups per
// hash. Without NUMA pinning, half the threads end up hitting remote
// memory on every hash. Measured penalty on dual-socket Xeon:
// 20-40% hashrate loss.
//
// This wrapper provides a thin abstraction over libnuma (Linux) that
// gracefully degrades on:
//   - macOS, Windows: no libnuma, we build stub implementations
//   - single-socket Linux: libnuma reports 1 node, no pinning applied
//   - libnuma unavailable at build time: stubs used, no-op behavior
//
// See src/truenorth/randomx_wrapper.cpp for how the per-node cache
// slots are managed; see src/truenorth/miner.cpp for thread pinning at
// worker spawn.

namespace truenorth::numa {

enum class NumaPref {
    AUTO, //!< enable per-node caches + pinning if libnuma present + multi-node
    ON,   //!< force enable; log ERROR if unavailable
    OFF,  //!< force disable
};

// Set the NUMA preference. Called once at miner startup after CLI
// parsing. Not concurrent-safe with existing MinerThreads.
void SetPreference(NumaPref pref);

// The preference most recently set (or AUTO if never set).
NumaPref CurrentPreference();

// Short label for logging: "auto" / "on" / "off".
const char* PrefName(NumaPref pref);

// True if libnuma was linked at build time AND numa_available()
// succeeds at runtime. False on macOS, Windows, or Linux without
// libnuma.
bool IsAvailable();

// Number of NUMA nodes on the system. Returns 1 if NUMA is unavailable
// or single-node.
int NumNodes();

// True if the miner should apply NUMA-aware behavior right now:
// preference != OFF AND libnuma available AND >1 node.
bool ShouldEnable();

// Pin the calling thread to any CPU on the given NUMA node. Also
// binds memory allocations to that node's local memory. Returns true
// on success; false if NUMA unavailable, the node index is invalid,
// or the affinity syscall fails. When ShouldEnable() is false, this
// is a no-op returning false.
bool BindThreadToNode(int node);

// Return the NUMA node index (0..NumNodes-1) that worker
// `thread_idx` of `total_threads` should be pinned to. Even round-
// robin distribution: thread i -> node (i % NumNodes()). Returns 0
// when NUMA is disabled or single-node.
int NodeForThread(int thread_idx, int total_threads);

} // namespace truenorth::numa

#endif // TRUENORTH_NUMA_H
