// Copyright (c) 2026 The TrueNorth developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef TRUENORTH_SYSTEM_MEM_H
#define TRUENORTH_SYSTEM_MEM_H

#include <cstdint>

namespace truenorth {

// Returns the amount of memory currently available to userland processes,
// in MiB. "Available" here means "physical memory the kernel would give
// us right now without incurring swap or eviction" -- roughly `MemAvailable`
// on Linux, physfootprint headroom on macOS, and the physical-avail figure
// from GlobalMemoryStatusEx on Windows.
//
// Returns 0 if the platform is not supported or the probe failed. Callers
// treating "unknown" as "fail-safe to light mode" is the intended pattern;
// see randomx_wrapper.h AutoDetectMinerMode.
std::uint64_t AvailableMemoryMiB();

} // namespace truenorth

#endif // TRUENORTH_SYSTEM_MEM_H
