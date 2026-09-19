// Copyright (c) 2026 The TrueNorth developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

// Test-only shared library exposing TrueNorth's RandomX PoW hash to the
// Python functional test framework (test/functional/test_framework/randomx.py).
//
// The framework builds and solves blocks itself. TrueNorth checks proof of
// work against RandomX (with the RandomNorth Argon2 salt and V2 flags), not
// SHA256d, so the framework needs the node's exact hash. Calling the same
// wrapper the node uses keeps the two from drifting apart.

#include <truenorth/randomx_wrapper.h>
#include <uint256.h>

#include <cstddef>
#include <cstring>
#include <span>

extern "C" {

// Light-mode RandomX hash of `data` under `seed32` (32 bytes, uint256
// internal byte order, as in CBlockIndex::GetBlockHash()). Writes 32 bytes
// to `out32` in uint256 internal byte order. Returns 0 on success.
__attribute__((visibility("default")))
int truenorth_test_randomx_light_hash(const unsigned char* seed32,
                                      const unsigned char* data,
                                      std::size_t len,
                                      unsigned char* out32)
{
    if (seed32 == nullptr || out32 == nullptr || (data == nullptr && len != 0)) return 1;
    const uint256 seed{std::span<const unsigned char>{seed32, 32}};
    const uint256 hash{truenorth::RandomXLightHash(seed, data, len)};
    std::memcpy(out32, hash.begin(), 32);
    return 0;
}

} // extern "C"
