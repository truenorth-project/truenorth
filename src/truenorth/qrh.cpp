// Copyright (c) 2026 The TrueNorth developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <truenorth/qrh.h>

#include <crypto/sha256.h>
#include <uint256.h>

#include <cstdint>
#include <span>

namespace truenorth {

const uint256 QRH_EMPTY_SCRIPT_ROOT{};

uint256 ComputeQRHCommitment(uint8_t scheme_id,
                             std::span<const unsigned char> pubkey,
                             const uint256& script_root)
{
    CSHA256 hasher;
    hasher.Write(reinterpret_cast<const unsigned char*>(QRH_DOMAIN_TAG.data()),
                 QRH_DOMAIN_TAG.size());
    hasher.Write(&scheme_id, 1);
    hasher.Write(pubkey.data(), pubkey.size());
    hasher.Write(script_root.data(), script_root.size());
    uint256 result;
    hasher.Finalize(result.begin());
    return result;
}

} // namespace truenorth
