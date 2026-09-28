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
//
// GENESIS_TIMESTAMP_MSG_TEST is used by FOUR chains -- testnet3, testnet4,
// signet and regtest -- each asserting its own genesis hash in chainparams.
// Editing it breaks all four asserts at once: the binary then aborts on
// startup on every chain, the whole functional suite dies with it, and the
// live testnet4 chain (genesis ab30dfbf...) is invalidated. Do not edit it
// without deliberately resetting every chain that uses it.
//
// NOTE both constants below currently hold the SAME placeholder text, so a
// find-and-replace on that text silently hits both. At the ceremony, edit the
// _MAIN line by name and then confirm _TEST is byte-identical to what shipped.
namespace truenorth {

inline constexpr const char* GENESIS_TIMESTAMP_MSG_MAIN =
    "TrueNorth - Canadian RandomX genesis - pre-launch placeholder";

inline constexpr const char* GENESIS_TIMESTAMP_MSG_TEST =
    "TrueNorth - Canadian RandomX genesis - pre-launch placeholder";

} // namespace truenorth

#endif // TRUENORTH_GENESIS_SPEC_H
