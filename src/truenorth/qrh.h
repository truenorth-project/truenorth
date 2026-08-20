// Copyright (c) 2026 The TrueNorth developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef TRUENORTH_QRH_H
#define TRUENORTH_QRH_H

#include <uint256.h>

#include <cstdint>
#include <span>
#include <string_view>

namespace truenorth {

// Domain separation tag for P2QRH commitments. Baking the tag into the
// hash prevents cross-protocol collisions with other SHA256 uses and
// lets us bump to a v2 envelope later without disturbing v1 UTXOs.
// See doc/p2qrh.md.
inline constexpr std::string_view QRH_DOMAIN_TAG{"TrueNorth/QRH/v1"};

// Registered signature schemes for the QRH envelope. Only QRH_SCHEME_SCHNORR
// is accepted at consensus at launch; PQ schemes are added via soft fork.
// See doc/p2qrh.md scheme registry.
inline constexpr uint8_t QRH_SCHEME_RESERVED_ZERO = 0x00; //!< Never valid
inline constexpr uint8_t QRH_SCHEME_SCHNORR = 0x01;       //!< secp256k1 Schnorr (BIP-340)
inline constexpr uint8_t QRH_SCHEME_INVALID = 0xFF;       //!< Sentinel; reject at consensus

// Sentinel for "no script tree" — 32 bytes of zeros, mirroring the
// Taproot empty-tree convention.
extern const uint256 QRH_EMPTY_SCRIPT_ROOT;

// Compute the 32-byte P2QRH commitment carried in the scriptPubKey.
//
//   commitment = SHA256(QRH_DOMAIN_TAG || scheme_id || pubkey || script_root)
//
// `pubkey` bytes are scheme-defined: 32 bytes (x-only) for QRH_SCHEME_SCHNORR,
// scheme-specific sizes for future PQ schemes.
uint256 ComputeQRHCommitment(uint8_t scheme_id,
                             std::span<const unsigned char> pubkey,
                             const uint256& script_root);

} // namespace truenorth

#endif // TRUENORTH_QRH_H
