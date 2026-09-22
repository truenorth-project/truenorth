// Copyright (c) 2026 The TrueNorth developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef TRUENORTH_GENESIS_SPEC_H
#define TRUENORTH_GENESIS_SPEC_H

// pszTimestamp strings for the genesis coinbase. chainparams.cpp reads these
// at startup; mine_genesis.cpp uses them to find a matching nonce.
//
// Split per chain deliberately. The string goes into the coinbase, which feeds
// the merkle root, which feeds the genesis hash -- and every chain asserts its
// genesis hash in chainparams. A single shared string therefore means that
// editing mainnet's message at the genesis ceremony silently changes the
// genesis of testnet3, testnet4, signet and regtest, and every node built from
// that commit aborts on startup. Keep them separate so the ceremony touches
// mainnet only.
//
// Mainnet wants a recent newspaper headline, set at the ceremony (task #31).
// The test chains just need something distinctive, and should not be edited
// without resetting the chain in question.
namespace truenorth {

inline constexpr const char* GENESIS_TIMESTAMP_MSG_MAIN =
    "TrueNorth - Canadian RandomX genesis - pre-launch placeholder";

inline constexpr const char* GENESIS_TIMESTAMP_MSG_TEST =
    "TrueNorth - Canadian RandomX genesis - pre-launch placeholder";

} // namespace truenorth

#endif // TRUENORTH_GENESIS_SPEC_H
