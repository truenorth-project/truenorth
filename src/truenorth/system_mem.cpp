// Copyright (c) 2026 The TrueNorth developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <truenorth/system_mem.h>

#include <cstdint>

#if defined(__linux__)
#include <cstdio>
#include <cstring>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace truenorth {

std::uint64_t AvailableMemoryMiB()
{
#if defined(__linux__)
    // /proc/meminfo exposes "MemAvailable" (added in Linux 3.14, 2014) which
    // is the kernel's own estimate of "how much memory is available for
    // starting new applications without swapping." That's the number we want
    // for the fast/light decision, not raw MemFree.
    std::FILE* f = std::fopen("/proc/meminfo", "r");
    if (!f) return 0;
    char line[256];
    std::uint64_t kb = 0;
    while (std::fgets(line, sizeof(line), f) != nullptr) {
        if (std::strncmp(line, "MemAvailable:", 13) == 0) {
            std::sscanf(line + 13, "%llu", reinterpret_cast<unsigned long long*>(&kb));
            break;
        }
    }
    std::fclose(f);
    return kb / 1024; // KiB -> MiB
#elif defined(__APPLE__)
    // macOS keeps free + inactive small on purpose: file cache sits in the
    // active queue and idle anonymous memory goes to the compressor, so
    // that sum reads ~2-3 GiB on a 16 GiB machine with plenty to spare and
    // pushed auto mode to light. Use Activity Monitor's accounting instead:
    // available = physical - app memory (anonymous minus purgeable)
    //                      - wired - compressor-occupied pages.
    // File-backed cache is reclaimable, so it counts as available.
    std::uint64_t total_bytes = 0;
    size_t len = sizeof(total_bytes);
    if (sysctlbyname("hw.memsize", &total_bytes, &len, nullptr, 0) != 0) return 0;
    vm_size_t page_size = 0;
    if (host_page_size(mach_host_self(), &page_size) != KERN_SUCCESS) return 0;
    vm_statistics64_data_t stats;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          reinterpret_cast<host_info64_t>(&stats), &count) != KERN_SUCCESS) {
        return 0;
    }
    const std::uint64_t app_pages = stats.internal_page_count > stats.purgeable_count ? stats.internal_page_count - stats.purgeable_count : 0;
    const std::uint64_t used_bytes =
        (app_pages + stats.wire_count + stats.compressor_page_count) * static_cast<std::uint64_t>(page_size);
    return used_bytes < total_bytes ? (total_bytes - used_bytes) / (1024ULL * 1024ULL) : 0;
#elif defined(_WIN32)
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status)) return 0;
    return static_cast<std::uint64_t>(status.ullAvailPhys / (1024ULL * 1024ULL));
#else
    // Unsupported platform -- caller falls back to light mode.
    return 0;
#endif
}

} // namespace truenorth
