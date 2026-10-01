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
#include <streams.h>
#include <truenorth/genesis_spec.h>
#include <truenorth/numa.h>
#include <truenorth/randomx_wrapper.h>
#include <uint256.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace {

// Pull the single source-of-truth pszTimestamp from genesis_spec.h.
// chainparams.cpp uses the same constant; editing it there is the one
// place users change before re-mining for a new launch.
using truenorth::GENESIS_TIMESTAMP_MSG_MAIN;
using truenorth::GENESIS_TIMESTAMP_MSG_TEST;

// Genesis coinbase output is OP_RETURN -- explicitly unspendable, per spec
// (genesis reward is unspendable to enforce the no-premine property).
CScript GenesisOutputScript()
{
    return CScript() << OP_RETURN;
}

// Build the genesis CBlock for the given header parameters. Mirrors
// chainparams.cpp's CreateGenesisBlock helpers byte-for-byte so the
// resulting block hash matches what bitcoind will compute at runtime.
CBlock BuildGenesis(const char* pszTimestamp, uint32_t nTime, uint32_t nNonce,
                    uint32_t nBits, int32_t nVersion, CAmount genesisReward)
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
                         reinterpret_cast<const unsigned char*>(pszTimestamp),
                         reinterpret_cast<const unsigned char*>(pszTimestamp) + std::strlen(pszTimestamp)};

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
    const char* msg; // per-chain pszTimestamp; see genesis_spec.h
};

bool MineFor(const ChainSpec& spec, int num_threads)
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

    CBlock genesis = BuildGenesis(spec.msg, spec.nTime, /*nNonce=*/0, spec.nBits,
                                  nVersion, reward);
    const auto t0 = std::chrono::steady_clock::now();

    // Parallel nonce search.
    //
    // The original loop called CBlockHeader::GetPoWHash(), which goes through
    // RandomXLightHash -- LIGHT mode, 5-30 H/s per thread -- on one core. At
    // mainnet's launch nBits that is ~960k expected hashes, i.e. many hours.
    // MinerThread gives each worker its own FAST-mode VM hashing lock-free,
    // which is what truenorth-miner uses, so this scales with cores and runs
    // ~10x faster per core besides.
    //
    // Workers stride the nonce space from their own offset. The 80-byte header
    // is serialised once and each worker patches only the 4 nonce bytes, so no
    // worker re-serialises and none of them share mutable state.
    //
    // Deterministic: returns the LOWEST valid nonce, same as the old
    // single-threaded loop, so re-runs are idempotent regardless of -threads.
    // Workers do not stop at the first hit by anyone; each continues until its
    // own candidate passes the best known winner, so no lower nonce can be
    // skipped. Costs a few wasted hashes per worker, not minutes.
    truenorth::SetMinerMode(truenorth::RandomXMode::FAST);
    truenorth::SetRandomXFallbackNotices(true);

    DataStream hdr_ss;
    hdr_ss << static_cast<const CBlockHeader&>(genesis);
    const unsigned char* hdr_begin = reinterpret_cast<const unsigned char*>(hdr_ss.data());
    const std::vector<unsigned char> hdr_template(hdr_begin, hdr_begin + hdr_ss.size());
    constexpr std::size_t NONCE_OFFSET = 76; // version 4 + prev 32 + merkle 32 + time 4 + bits 4

    // `best` is the lowest valid nonce seen so far, or kNoWinner if none.
    // Workers monotonically lower it and stop once their own candidate passes
    // it, which is what makes the result deterministic -- see below.
    constexpr uint64_t kNoWinner = std::numeric_limits<uint64_t>::max();
    std::atomic<uint64_t> best{kNoWinner};
    std::atomic<uint64_t> total_attempts{0};

    {
        std::vector<std::thread> workers;
        workers.reserve(static_cast<std::size_t>(num_threads));
        for (int t = 0; t < num_threads; ++t) {
            workers.emplace_back([&, t]() {
                // Genesis epoch seed. truenorth::kGenesisSeed is uint256::ZERO
                // but lives in bitcoin_common, which this target does not link;
                // GetPoWHash()'s own default is the same value, and it is what
                // the committed genesis was mined under.
                truenorth::MinerThread mt(uint256::ZERO,
                                          truenorth::numa::NodeForThread(t, num_threads));
                std::vector<unsigned char> hdr = hdr_template;
                uint256 h;
                uint64_t local = 0;
                for (uint64_t n = static_cast<uint64_t>(t);
                     n <= std::numeric_limits<uint32_t>::max();
                     n += static_cast<uint64_t>(num_threads)) {
                    // Stop only once this worker's own candidate has passed the
                    // best known winner -- NOT on the first hit by anyone. That
                    // is what keeps the result the lowest valid nonce rather
                    // than whichever one got there first: workers scan upward,
                    // `best` only decreases, so when every worker has stopped,
                    // every nonce below `best` has been examined by exactly the
                    // worker whose stride covers it. A load per hash is free
                    // next to a RandomX hash.
                    if (n > best.load(std::memory_order_relaxed)) break;
                    const uint32_t nonce = static_cast<uint32_t>(n);
                    hdr[NONCE_OFFSET] = static_cast<unsigned char>(nonce);
                    hdr[NONCE_OFFSET + 1] = static_cast<unsigned char>(nonce >> 8);
                    hdr[NONCE_OFFSET + 2] = static_cast<unsigned char>(nonce >> 16);
                    hdr[NONCE_OFFSET + 3] = static_cast<unsigned char>(nonce >> 24);
                    mt.Hash(hdr.data(), hdr.size(), h);
                    ++local;
                    if (UintToArith256(h) <= target) {
                        // Lower `best` to n if n is smaller. Reload on failure:
                        // another worker may have set something lower still.
                        uint64_t cur = best.load(std::memory_order_relaxed);
                        while (n < cur &&
                               !best.compare_exchange_weak(cur, n,
                                                           std::memory_order_release,
                                                           std::memory_order_relaxed)) {
                        }
                        break;
                    }
                }
                total_attempts.fetch_add(local, std::memory_order_relaxed);
            });
        }
        for (auto& w : workers)
            w.join();
    }

    if (best.load(std::memory_order_acquire) != kNoWinner) {
        genesis.nNonce = static_cast<uint32_t>(best.load(std::memory_order_acquire));
        const uint256 pow_hash = genesis.GetPoWHash();
        const uint64_t i = total_attempts.load(std::memory_order_relaxed) - 1;
        {
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
    }
    std::fprintf(stderr, "[%s] nonce space exhausted without solution\n", spec.name);
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
    // Default to every core. The search is embarrassingly parallel and this
    // tool is run interactively during the genesis ceremony, so there is no
    // reason to leave cores idle.
    int num_threads = static_cast<int>(std::thread::hardware_concurrency());
    if (num_threads < 1) num_threads = 1;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto eq = arg.find('=');
        if (eq == std::string::npos) {
            std::fprintf(stderr, "usage: %s [-chain=NAME] [-time=UNIX] [-nbits=0xHEX] [-threads=N]\n", argv[0]);
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
        } else if (key == "-threads") {
            num_threads = std::stoi(val);
            if (num_threads < 1) num_threads = 1;
        } else {
            std::fprintf(stderr, "unknown option: %s\n", key.c_str());
            return 1;
        }
    }

    std::fprintf(stderr, "== TrueNorth genesis-mining utility ==\n");
    std::fprintf(stderr, "Timestamp message (main): \"%s\"\n", GENESIS_TIMESTAMP_MSG_MAIN);
    std::fprintf(stderr, "Timestamp message (test chains): \"%s\"\n", GENESIS_TIMESTAMP_MSG_TEST);
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

    // Default per-chain nTimes.
    //
    // Mainnet nTime is set to 2026-10-12 00:00:00 UTC (1791763200), the
    // coordinated launch instant. This matches Consensus::Params::nLaunchTime
    // in kernel/chainparams.cpp, so the genesis block's timestamp equals
    // the point after which post-genesis blocks are permitted. See
    // src/consensus/params.h::nLaunchTime and
    // src/validation.cpp::ContextualCheckBlockHeader.
    //
    // Testnet/signet/regtest defaults are placeholders. For any real
    // re-mine, pass -chain=NAME -time=NTIME so the single edit to
    // chainparams.cpp's CreateGenesisBlock(NTIME, ...) call and the mine
    // here agree by construction.
    std::array<ChainSpec, 5> chains{{
        // Mainnet mines at the launch difficulty, not at powLimit. 0x1e1179ec
        // is ~960k hashes/block, targeting 120s at an assumed 8 kH/s launch
        // network. Starting at the floor (as the test chains do) would produce
        // a burst of near-free blocks before LWMA has a window -- testnet4's
        // 2026-09-23 reset mined 316 blocks in 22 minutes doing exactly that.
        {"main", 1791763200, 0x1e1179ecu, GENESIS_TIMESTAMP_MSG_MAIN},
        {"testnet3", 1748000010, 0x207fffffu, GENESIS_TIMESTAMP_MSG_TEST},
        {"testnet4", 1790186400, 0x207fffffu, GENESIS_TIMESTAMP_MSG_TEST},
        {"signet", 1748000030, 0x207fffffu, GENESIS_TIMESTAMP_MSG_TEST},
        {"regtest", 1296688602, 0x207fffffu, GENESIS_TIMESTAMP_MSG_TEST},
    }};

    bool all_ok = true;
    bool any_matched = false;
    for (auto& chain : chains) {
        if (!chain_filter.empty() && chain_filter != chain.name) continue;
        any_matched = true;
        if (override_time != 0) chain.nTime = override_time;
        if (override_nbits != 0) chain.nBits = override_nbits;
        // No attempt cap. The old 1'000'000 limit sat below mainnet's ~960k
        // expected attempts, so roughly a third of runs would have given up
        // without finding genesis. Search the whole nonce space instead.
        if (!MineFor(chain, num_threads)) {
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
