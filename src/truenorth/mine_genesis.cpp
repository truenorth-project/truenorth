// Copyright (c) 2026 The TrueNorth developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

// Mines RandomX-based TrueNorth genesis blocks. Run once per chain to
// produce the nonces and hashes that chainparams.cpp asserts against.
//
// Build: cmake --build build --target truenorth-mine-genesis
// Run:   ./build/bin/truenorth-mine-genesis
// Out:   C++ snippets per chain, paste into chainparams.cpp.
//
// The block construction here has to match chainparams.cpp's
// CreateGenesisBlock helper byte for byte (timestamp, output script,
// scriptSig, version, reward). Any drift and the runtime hash won't
// match what this tool reported, and the chainparams assertion fires
// at startup.

#include <arith_uint256.h>
#include <consensus/amount.h>
#include <consensus/merkle.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <truenorth/genesis_spec.h>
#include <uint256.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Pull the single source-of-truth pszTimestamp from genesis_spec.h.
// chainparams.cpp uses the same constant; editing it there is the one
// place users change before re-mining for a new launch.
using truenorth::GENESIS_TIMESTAMP_MSG;

// Genesis coinbase output is OP_RETURN -- explicitly unspendable, per spec
// (genesis reward is unspendable to enforce the no-premine property).
CScript GenesisOutputScript()
{
    return CScript() << OP_RETURN;
}

// Build the genesis CBlock for the given header parameters. Mirrors
// chainparams.cpp's CreateGenesisBlock helpers byte-for-byte so the
// resulting block hash matches what bitcoind will compute at runtime.
CBlock BuildGenesis(uint32_t nTime, uint32_t nNonce, uint32_t nBits,
                    int32_t nVersion, CAmount genesisReward)
{
    CMutableTransaction txNew;
    txNew.version = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);

    // scriptSig shape mirrors Bitcoin's CreateGenesisBlock convention so that
    // identical pszTimestamp + script bytes produce an identical coinbase tx.
    txNew.vin[0].scriptSig =
        CScript() << 486604799 << CScriptNum(4)
                  << std::vector<unsigned char>{
                         reinterpret_cast<const unsigned char*>(GENESIS_TIMESTAMP_MSG),
                         reinterpret_cast<const unsigned char*>(GENESIS_TIMESTAMP_MSG) + std::strlen(GENESIS_TIMESTAMP_MSG)};

    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = GenesisOutputScript();

    CBlock genesis;
    genesis.nTime = nTime;
    genesis.nBits = nBits;
    genesis.nNonce = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

struct ChainSpec {
    const char* name;
    uint32_t nTime;
    uint32_t nBits;
};

bool MineFor(const ChainSpec& spec, uint64_t max_nonce_tries)
{
    arith_uint256 target;
    bool neg = false, over = false;
    target.SetCompact(spec.nBits, &neg, &over);
    if (neg || over || target == 0) {
        std::fprintf(stderr, "[%s] invalid target from nBits=0x%08x\n",
                     spec.name, spec.nBits);
        return false;
    }

    const int32_t nVersion = 1;
    const CAmount reward = 512 * COIN;

    CBlock genesis = BuildGenesis(spec.nTime, /*nNonce=*/0, spec.nBits,
                                  nVersion, reward);
    const auto t0 = std::chrono::steady_clock::now();

    for (uint64_t i = 0; i < max_nonce_tries; ++i) {
        const uint256 pow_hash = genesis.GetPoWHash();
        if (UintToArith256(pow_hash) <= target) {
            const auto elapsed_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - t0)
                    .count();
            std::printf("\n// %s -- mined in %llu attempt(s), %lldms\n",
                        spec.name,
                        static_cast<unsigned long long>(i + 1),
                        static_cast<long long>(elapsed_ms));
            std::printf("genesis = CreateGenesisBlock(%u, %u, 0x%08x, %d, 512 * COIN);\n",
                        genesis.nTime, genesis.nNonce, genesis.nBits, nVersion);
            std::printf("consensus.hashGenesisBlock = genesis.GetHash();\n");
            std::printf("assert(consensus.hashGenesisBlock == uint256{\"%s\"});\n",
                        genesis.GetHash().GetHex().c_str());
            std::printf("assert(genesis.hashMerkleRoot       == uint256{\"%s\"});\n",
                        genesis.hashMerkleRoot.GetHex().c_str());
            std::printf("// PoW hash (RandomNorth): %s\n",
                        pow_hash.GetHex().c_str());
            std::fflush(stdout);
            return true;
        }
        ++genesis.nNonce;
        if (genesis.nNonce == 0) {
            std::fprintf(stderr, "[%s] nonce wrapped without solution\n", spec.name);
            return false;
        }
    }
    std::fprintf(stderr, "[%s] exhausted max_nonce_tries=%llu without solution\n",
                 spec.name, static_cast<unsigned long long>(max_nonce_tries));
    return false;
}

} // namespace

int main(int argc, char* argv[])
{
    // Optional CLI overrides for single-chain re-mine workflow:
    //   ./truenorth-mine-genesis                    -- mine all five with defaults
    //   ./truenorth-mine-genesis -chain=testnet3    -- mine only that chain (default nTime)
    //   ./truenorth-mine-genesis -chain=testnet3 -time=1779765738
    //                                               -- mine that chain at the supplied nTime
    //                                                  (matches chainparams.cpp's value;
    //                                                   used during a launch-morning re-mine)
    //   ./truenorth-mine-genesis -chain=testnet3 -time=N -nbits=0x1f00ffff
    //                                               -- also override the target
    std::string chain_filter;
    uint32_t override_time = 0;
    uint32_t override_nbits = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto eq = arg.find('=');
        if (eq == std::string::npos) {
            std::fprintf(stderr, "usage: %s [-chain=NAME] [-time=UNIX] [-nbits=0xHEX]\n", argv[0]);
            return 1;
        }
        const std::string key = arg.substr(0, eq);
        const std::string val = arg.substr(eq + 1);
        if (key == "-chain") {
            chain_filter = val;
        } else if (key == "-time") {
            override_time = static_cast<uint32_t>(std::stoul(val));
        } else if (key == "-nbits") {
            override_nbits = static_cast<uint32_t>(std::stoul(val, nullptr, 0));
        } else {
            std::fprintf(stderr, "unknown option: %s\n", key.c_str());
            return 1;
        }
    }

    std::fprintf(stderr, "== TrueNorth genesis-mining utility ==\n");
    std::fprintf(stderr, "Timestamp message: \"%s\"\n", GENESIS_TIMESTAMP_MSG);
    std::fprintf(stderr, "Genesis output script: OP_RETURN (unspendable)\n");
    std::fprintf(stderr, "Genesis reward: 512 * COIN (unspendable)\n");
    if (!chain_filter.empty()) {
        std::fprintf(stderr, "Filtering to chain: %s\n", chain_filter.c_str());
    }
    if (override_time != 0) {
        std::fprintf(stderr, "Overriding nTime: %u\n", override_time);
    }
    if (override_nbits != 0) {
        std::fprintf(stderr, "Overriding nBits: 0x%08x\n", override_nbits);
    }
    std::fprintf(stderr, "\n");

    // Default per-chain nTimes are pre-launch placeholders. For an actual
    // launch re-mine, pass -chain=NAME -time=NTIME from the runbook so the
    // single edit to chainparams.cpp's CreateGenesisBlock(NTIME, ...) call
    // and the corresponding mine here agree by construction.
    std::array<ChainSpec, 5> chains{{
        {"main", 1748000000, 0x207fffffu},
        {"testnet3", 1748000010, 0x207fffffu},
        {"testnet4", 1748000020, 0x207fffffu},
        {"signet", 1748000030, 0x207fffffu},
        {"regtest", 1296688602, 0x207fffffu},
    }};

    bool all_ok = true;
    bool any_matched = false;
    for (auto& chain : chains) {
        if (!chain_filter.empty() && chain_filter != chain.name) continue;
        any_matched = true;
        if (override_time != 0) chain.nTime = override_time;
        if (override_nbits != 0) chain.nBits = override_nbits;
        if (!MineFor(chain, /*max_nonce_tries=*/1'000'000)) {
            all_ok = false;
            break;
        }
    }
    if (!chain_filter.empty() && !any_matched) {
        std::fprintf(stderr, "no chain matched -chain=%s\n", chain_filter.c_str());
        return 1;
    }
    return all_ok ? 0 : 1;
}
