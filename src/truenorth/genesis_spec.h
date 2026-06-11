// Copyright (c) 2026 The TrueNorth developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef TRUENORTH_GENESIS_SPEC_H
#define TRUENORTH_GENESIS_SPEC_H

// pszTimestamp string for the genesis coinbase. chainparams.cpp reads it
// at startup; mine_genesis.cpp uses it to find a matching nonce.
//
// Edit before doing a real launch mine. Mainnet wants a recent newspaper
// headline. Testnet just needs something distinctive.
namespace truenorth {

inline constexpr const char* GENESIS_TIMESTAMP_MSG =
    "TrueNorth - Canadian RandomX genesis - pre-launch placeholder";

} // namespace truenorth

#endif // TRUENORTH_GENESIS_SPEC_H
