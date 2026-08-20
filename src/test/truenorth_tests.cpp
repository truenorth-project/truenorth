// Copyright (c) 2026 The TrueNorth developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

// Unit tests for TrueNorth-specific additions:
//   A. LWMA difficulty           (src/pow.cpp CalculateNextWorkRequired)
//   B. Seed-key rotation         (src/truenorth/seed_key.{h,cpp})
//   C. Tail emission             (src/validation.cpp GetBlockSubsidy)
//   D. CBlockHeader::GetPoWHash  (src/primitives/block.cpp)
//   E. RandomX wrapper           (src/truenorth/randomx_wrapper.{h,cpp})
//
// The RandomX tests trigger seed-cache (re)initialisation; each new seed
// adds ~1-2 s to wall-clock test time. The cases are intentionally minimal
// to keep the suite fast.

#include <arith_uint256.h>
#include <chain.h>
#include <chainparams.h>
#include <consensus/amount.h>
#include <consensus/params.h>
#include <key.h>
#include <policy/policy.h>
#include <pow.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <pubkey.h>
#include <script/descriptor.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/script_error.h>
#include <script/sign.h>
#include <script/signingprovider.h>
#include <test/util/setup_common.h>
#include <truenorth/numa.h>
#include <truenorth/qrh.h>
#include <truenorth/randomx_wrapper.h>
#include <truenorth/seed_key.h>
#include <truenorth/system_mem.h>
#include <uint256.h>
#include <util/chaintype.h>
#include <util/strencodings.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

#include <deque>
#include <memory>
#include <span>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(truenorth_tests, BasicTestingSetup)

// -------------------------------------------------------------------------
// Shared helper: build a fake chain of `length` CBlockIndex objects, each
// spaced exactly `target_spacing` seconds apart, with constant `nBits`. The
// returned struct owns the storage; the tip is back of the vector.
// -------------------------------------------------------------------------

namespace {

// std::deque so element addresses are stable across emplace_back -- the
// chain has CBlockIndex pointers into itself (pprev, pskip) and we use
// std::deque because CBlockIndex disables copy + move (std::vector::resize
// would require Cpp17MoveInsertable).
struct FakeChain {
    std::deque<CBlockIndex> blocks;
    std::vector<uint256> hashes;
    CBlockIndex* tip() { return &blocks.back(); }
};

// Build a fake chain with valid pprev linkage and skip-list pointers
// populated. Each block gets a distinct phashBlock value so GetBlockHash()
// returns a height-derived value -- useful for asserting which ancestor a
// lookup landed on.
std::unique_ptr<FakeChain> MakeFakeChain(int length, int64_t target_spacing, uint32_t nBits)
{
    auto c = std::make_unique<FakeChain>();
    c->hashes.resize(length);
    for (int i = 0; i < length; ++i) {
        c->hashes[i] = ArithToUint256(arith_uint256(static_cast<uint64_t>(i + 1)));
        c->blocks.emplace_back();
        CBlockIndex& b = c->blocks.back();
        b.phashBlock = &c->hashes[i];
        b.nHeight = i;
        b.nTime = 1700000000u + static_cast<uint32_t>(i * target_spacing);
        b.nBits = nBits;
        b.pprev = (i == 0) ? nullptr : &c->blocks[i - 1];
        b.BuildSkip();
    }
    return c;
}

} // namespace

// =========================================================================
// A. LWMA difficulty
// =========================================================================

// Short chains (nHeight < N=90) skip the LWMA math and return powLimit
// directly. We pass MAIN here because regtest has fPowNoRetargeting=true
// which short-circuits before the height check.
BOOST_AUTO_TEST_CASE(lwma_short_chain_returns_powlimit)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& consensus = chainParams->GetConsensus();
    BOOST_REQUIRE(!consensus.fPowNoRetargeting);

    auto c = MakeFakeChain(/*length=*/10, /*target_spacing=*/120, /*nBits=*/0x1d00ffff);
    const uint32_t result = CalculateNextWorkRequired(c->tip(), 0, consensus);
    const uint32_t expected = UintToArith256(consensus.powLimit).GetCompact();
    BOOST_CHECK_EQUAL(result, expected);
}

// A chain of N+1 blocks at exactly T solvetime is the LWMA's fixed point:
// next_target should be (almost) identical to the running target. Per-block
// integer division loses up to N units per accumulated sum, so we accept a
// 1% tolerance rather than asserting bit-for-bit equality.
BOOST_AUTO_TEST_CASE(lwma_constant_solvetime_is_a_fixed_point)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& consensus = chainParams->GetConsensus();
    BOOST_REQUIRE(!consensus.fPowNoRetargeting);

    const uint32_t initial = 0x1d00ffff;
    auto c = MakeFakeChain(/*length=*/95, consensus.nPowTargetSpacing, initial);
    const uint32_t result = CalculateNextWorkRequired(c->tip(), 0, consensus);

    arith_uint256 t_initial, t_result;
    t_initial.SetCompact(initial);
    t_result.SetCompact(result);
    const arith_uint256 diff = (t_initial > t_result) ? t_initial - t_result : t_result - t_initial;
    BOOST_CHECK(diff < t_initial / 100);
}

// Regtest sets fPowNoRetargeting=true; the function must short-circuit and
// return pindexLast->nBits unchanged, regardless of chain length or timing.
BOOST_AUTO_TEST_CASE(lwma_respects_pow_no_retargeting)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::REGTEST);
    const auto& consensus = chainParams->GetConsensus();
    BOOST_REQUIRE(consensus.fPowNoRetargeting);

    auto c = MakeFakeChain(/*length=*/100, 120, /*nBits=*/0x207fffff);
    const uint32_t result = CalculateNextWorkRequired(c->tip(), 0, consensus);
    BOOST_CHECK_EQUAL(result, c->tip()->nBits);
}

// =========================================================================
// B. Seed-key rotation
// =========================================================================

// SeedHeightForNextHeight returns 0 throughout the genesis epoch + lag
// window, then EPOCH at the first boundary, 2*EPOCH at the second, etc.
BOOST_AUTO_TEST_CASE(seed_height_boundary_values)
{
    using truenorth::SeedHeightForNextHeight;
    constexpr int EPOCH = truenorth::RANDOMX_EPOCH_LENGTH;
    constexpr int LAG = truenorth::RANDOMX_SEED_LAG;

    BOOST_CHECK_EQUAL(SeedHeightForNextHeight(0), 0);
    BOOST_CHECK_EQUAL(SeedHeightForNextHeight(EPOCH - 1), 0);
    BOOST_CHECK_EQUAL(SeedHeightForNextHeight(EPOCH), 0);
    BOOST_CHECK_EQUAL(SeedHeightForNextHeight(EPOCH + LAG - 1), 0);

    BOOST_CHECK_EQUAL(SeedHeightForNextHeight(EPOCH + LAG), EPOCH);
    BOOST_CHECK_EQUAL(SeedHeightForNextHeight(2 * EPOCH + LAG - 1), EPOCH);
    BOOST_CHECK_EQUAL(SeedHeightForNextHeight(2 * EPOCH + LAG), 2 * EPOCH);
    BOOST_CHECK_EQUAL(SeedHeightForNextHeight(3 * EPOCH + LAG), 3 * EPOCH);
}

// pprev == nullptr means we are computing the seed for genesis itself; the
// genesis seed is the all-zero constant.
BOOST_AUTO_TEST_CASE(seed_key_for_child_null_returns_genesis)
{
    BOOST_CHECK(truenorth::SeedKeyForChild(nullptr) == truenorth::kGenesisSeed);
}

// Any pprev whose child still falls inside the genesis epoch + lag window
// also gets kGenesisSeed.
BOOST_AUTO_TEST_CASE(seed_key_for_child_in_genesis_epoch_returns_genesis)
{
    auto c = MakeFakeChain(/*length=*/100, 120, /*nBits=*/0x207fffff);
    BOOST_CHECK(truenorth::SeedKeyForChild(c->tip()) == truenorth::kGenesisSeed);
}

// Once past the first epoch boundary, SeedKeyForChild must walk pprev's
// skip-list to the seed_height and return that block's hash.
BOOST_AUTO_TEST_CASE(seed_key_for_child_post_genesis_uses_ancestor_hash)
{
    constexpr int EPOCH = truenorth::RANDOMX_EPOCH_LENGTH;
    constexpr int LAG = truenorth::RANDOMX_SEED_LAG;

    // tip is at height EPOCH+LAG; child of tip has next_height = EPOCH+LAG+1
    // and so seed_height = EPOCH. We expect SeedKeyForChild(tip) to return
    // the hash of the block at height EPOCH.
    const int length = EPOCH + LAG + 1;
    auto c = MakeFakeChain(length, 120, /*nBits=*/0x207fffff);
    BOOST_CHECK(truenorth::SeedKeyForChild(c->tip()) == c->blocks[EPOCH].GetBlockHash());
}

// =========================================================================
// C. Tail emission (GetBlockSubsidy)
// =========================================================================

// At and just before the first halving, subsidy is 1024 NORTH; after the
// first halving it is 512; after the second 256; etc.
BOOST_AUTO_TEST_CASE(subsidy_initial_and_first_halvings)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& c = chainParams->GetConsensus();

    BOOST_CHECK_EQUAL(GetBlockSubsidy(0, c), 512 * COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(c.nSubsidyHalvingInterval - 1, c), 512 * COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(c.nSubsidyHalvingInterval, c), 256 * COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(2 * c.nSubsidyHalvingInterval, c), 128 * COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(3 * c.nSubsidyHalvingInterval, c), 64 * COIN);
}

// 512 >> 6 = 8 NORTH exactly, so halving 6 hits the floor on the nose;
// halving 7 (512 >> 7 = 4) is pinned to the 8-NORTH tail; anything beyond
// stays at the floor in perpetuity.
BOOST_AUTO_TEST_CASE(subsidy_tail_floor_kicks_in)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& c = chainParams->GetConsensus();

    BOOST_CHECK_EQUAL(GetBlockSubsidy(6 * c.nSubsidyHalvingInterval, c), 8 * COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(7 * c.nSubsidyHalvingInterval, c), 8 * COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(50 * c.nSubsidyHalvingInterval, c), 8 * COIN);
}

// At halvings >= 64 the right-shift would be UB on 64-bit CAmount, so the
// function short-circuits to the tail floor. We probe far beyond that.
BOOST_AUTO_TEST_CASE(subsidy_halvings_above_64_short_circuit)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto& c = chainParams->GetConsensus();

    const int huge_height = 100 * c.nSubsidyHalvingInterval;
    BOOST_CHECK_EQUAL(GetBlockSubsidy(huge_height, c), 8 * COIN);
}

// =========================================================================
// D. CBlockHeader::GetPoWHash
// =========================================================================

static CBlockHeader MakeProbeHeader()
{
    CBlockHeader h;
    h.nVersion = 1;
    h.hashPrevBlock.SetNull();
    h.hashMerkleRoot.SetNull();
    h.nTime = 1700000000;
    h.nBits = 0x207fffff;
    h.nNonce = 42;
    return h;
}

// Same header + same seed must hash identically across calls.
BOOST_AUTO_TEST_CASE(get_pow_hash_is_deterministic)
{
    const CBlockHeader h = MakeProbeHeader();
    const uint256 seed = uint256::ZERO;
    BOOST_CHECK(h.GetPoWHash(seed) == h.GetPoWHash(seed));
}

// Changing only the seed must change the output. (RandomX's whole point is
// that the seed key fully programs the VM.)
BOOST_AUTO_TEST_CASE(get_pow_hash_depends_on_seed)
{
    const CBlockHeader h = MakeProbeHeader();
    const uint256 seed_zero = uint256::ZERO;
    const uint256 seed_one = ArithToUint256(arith_uint256(uint64_t{1}));
    BOOST_CHECK(h.GetPoWHash(seed_zero) != h.GetPoWHash(seed_one));
}

// Changing only the nonce must change the output -- otherwise no mining
// would be possible at all.
BOOST_AUTO_TEST_CASE(get_pow_hash_depends_on_nonce)
{
    CBlockHeader h = MakeProbeHeader();
    const uint256 seed = uint256::ZERO;
    const uint256 a = h.GetPoWHash(seed);
    h.nNonce = 99;
    const uint256 b = h.GetPoWHash(seed);
    BOOST_CHECK(a != b);
}

// =========================================================================
// E. RandomX wrapper
// =========================================================================

BOOST_AUTO_TEST_CASE(randomx_light_hash_deterministic_per_seed_and_data)
{
    using truenorth::RandomXLightHash;
    const uint256 seed = uint256::ZERO;
    const unsigned char data[] = {'a', 'b', 'c', 'd'};
    const uint256 a = RandomXLightHash(seed, data, sizeof(data));
    const uint256 b = RandomXLightHash(seed, data, sizeof(data));
    BOOST_CHECK(a == b);
}

BOOST_AUTO_TEST_CASE(randomx_light_hash_differs_for_different_seed)
{
    using truenorth::RandomXLightHash;
    const uint256 seed_zero = uint256::ZERO;
    const uint256 seed_one = ArithToUint256(arith_uint256(uint64_t{1}));
    const unsigned char data[] = {'a', 'b', 'c', 'd'};
    const uint256 a = RandomXLightHash(seed_zero, data, sizeof(data));
    const uint256 b = RandomXLightHash(seed_one, data, sizeof(data));
    BOOST_CHECK(a != b);
}

BOOST_AUTO_TEST_CASE(randomx_light_hash_differs_for_different_data)
{
    using truenorth::RandomXLightHash;
    const uint256 seed = uint256::ZERO;
    const unsigned char d1[] = {'a', 'b', 'c'};
    const unsigned char d2[] = {'a', 'b', 'd'};
    const uint256 a = RandomXLightHash(seed, d1, sizeof(d1));
    const uint256 b = RandomXLightHash(seed, d2, sizeof(d2));
    BOOST_CHECK(a != b);
}

// -------- RandomX mode selection + fast-mode plumbing (Issue #1) --------

BOOST_AUTO_TEST_CASE(available_memory_returns_positive_on_supported_platforms)
{
    // On Linux, macOS, and Windows -- the platforms we build release
    // binaries for -- AvailableMemoryMiB should never return 0. A 0
    // return specifically means the platform is unsupported by the probe.
    const std::uint64_t mib = truenorth::AvailableMemoryMiB();
    BOOST_CHECK(mib > 0);
    // Sanity: no modern machine has <64 MiB actually free.
    BOOST_CHECK(mib >= 64);
}

BOOST_AUTO_TEST_CASE(auto_detect_picks_light_with_impossible_threshold)
{
    // Threshold larger than any physical machine's RAM guarantees LIGHT.
    const auto mode = truenorth::AutoDetectMinerMode(/*min_free_mib=*/std::uint64_t{1} << 40);
    BOOST_CHECK(mode == truenorth::RandomXMode::LIGHT);
}

BOOST_AUTO_TEST_CASE(auto_detect_picks_fast_with_zero_threshold_on_supported_platform)
{
    // Threshold 0 means "any positive amount of memory is enough". This
    // implicitly asserts the platform is supported (probe returns > 0);
    // on an unsupported platform AvailableMemoryMiB returns 0 and the
    // predicate 0 >= 0 still holds, so FAST is picked. That's a design
    // trade-off documented in the header.
    const auto mode = truenorth::AutoDetectMinerMode(/*min_free_mib=*/0);
    BOOST_CHECK(mode == truenorth::RandomXMode::FAST);
}

BOOST_AUTO_TEST_CASE(set_and_current_miner_mode_round_trip)
{
    using truenorth::CurrentMinerMode;
    using truenorth::RandomXMode;
    using truenorth::SetMinerMode;
    const auto original = CurrentMinerMode();
    SetMinerMode(RandomXMode::FAST);
    BOOST_CHECK(CurrentMinerMode() == RandomXMode::FAST);
    SetMinerMode(RandomXMode::LIGHT);
    BOOST_CHECK(CurrentMinerMode() == RandomXMode::LIGHT);
    // Restore whatever mode the suite started in so later tests are not
    // sensitive to ordering.
    SetMinerMode(original);
}

BOOST_AUTO_TEST_CASE(mode_name_labels)
{
    BOOST_CHECK_EQUAL(std::string(truenorth::ModeName(truenorth::RandomXMode::LIGHT)), "light");
    BOOST_CHECK_EQUAL(std::string(truenorth::ModeName(truenorth::RandomXMode::FAST)), "fast");
}

// -------- Huge pages preference (Issue #7 part 1) --------

BOOST_AUTO_TEST_CASE(large_pages_pref_round_trip)
{
    using truenorth::CurrentLargePagesPreference;
    using truenorth::LargePagesPref;
    using truenorth::SetLargePagesPreference;
    const auto original = CurrentLargePagesPreference();
    SetLargePagesPreference(LargePagesPref::OFF);
    BOOST_CHECK(CurrentLargePagesPreference() == LargePagesPref::OFF);
    SetLargePagesPreference(LargePagesPref::ON);
    BOOST_CHECK(CurrentLargePagesPreference() == LargePagesPref::ON);
    SetLargePagesPreference(LargePagesPref::AUTO);
    BOOST_CHECK(CurrentLargePagesPreference() == LargePagesPref::AUTO);
    // Restore so later tests are not sensitive to ordering.
    SetLargePagesPreference(original);
}

BOOST_AUTO_TEST_CASE(large_pages_pref_names)
{
    using truenorth::LargePagesPref;
    using truenorth::LargePagesPrefName;
    BOOST_CHECK_EQUAL(std::string(LargePagesPrefName(LargePagesPref::AUTO)), "auto");
    BOOST_CHECK_EQUAL(std::string(LargePagesPrefName(LargePagesPref::ON)), "on");
    BOOST_CHECK_EQUAL(std::string(LargePagesPrefName(LargePagesPref::OFF)), "off");
}

// -------- NUMA topology + pinning (Issue #7 part 2) --------
//
// These tests run on any host regardless of NUMA availability. On
// macOS / Windows / Linux-without-libnuma the wrapper compiles stubs
// that always report "not available", and the tests verify the
// graceful-fallback behavior. On Linux with libnuma installed and
// multi-node hardware, the assertions still hold (NUMA is opt-in).

BOOST_AUTO_TEST_CASE(numa_pref_round_trip)
{
    using truenorth::numa::CurrentPreference;
    using truenorth::numa::NumaPref;
    using truenorth::numa::SetPreference;
    const auto original = CurrentPreference();
    SetPreference(NumaPref::OFF);
    BOOST_CHECK(CurrentPreference() == NumaPref::OFF);
    SetPreference(NumaPref::ON);
    BOOST_CHECK(CurrentPreference() == NumaPref::ON);
    SetPreference(NumaPref::AUTO);
    BOOST_CHECK(CurrentPreference() == NumaPref::AUTO);
    SetPreference(original);
}

BOOST_AUTO_TEST_CASE(numa_pref_names)
{
    using truenorth::numa::NumaPref;
    using truenorth::numa::PrefName;
    BOOST_CHECK_EQUAL(std::string(PrefName(NumaPref::AUTO)), "auto");
    BOOST_CHECK_EQUAL(std::string(PrefName(NumaPref::ON)), "on");
    BOOST_CHECK_EQUAL(std::string(PrefName(NumaPref::OFF)), "off");
}

BOOST_AUTO_TEST_CASE(numa_num_nodes_at_least_one)
{
    // Any host must report at least 1 node (the "everything" node).
    BOOST_CHECK(truenorth::numa::NumNodes() >= 1);
}

BOOST_AUTO_TEST_CASE(numa_should_enable_false_when_pref_off)
{
    using truenorth::numa::CurrentPreference;
    using truenorth::numa::NumaPref;
    using truenorth::numa::SetPreference;
    using truenorth::numa::ShouldEnable;
    const auto original = CurrentPreference();
    SetPreference(NumaPref::OFF);
    BOOST_CHECK(!ShouldEnable());
    SetPreference(original);
}

BOOST_AUTO_TEST_CASE(numa_node_for_thread_returns_zero_when_disabled)
{
    using truenorth::numa::CurrentPreference;
    using truenorth::numa::NodeForThread;
    using truenorth::numa::NumaPref;
    using truenorth::numa::SetPreference;
    const auto original = CurrentPreference();
    SetPreference(NumaPref::OFF);
    // Regardless of which thread index we ask for, we should get
    // node 0 when NUMA is disabled.
    BOOST_CHECK_EQUAL(NodeForThread(0, 8), 0);
    BOOST_CHECK_EQUAL(NodeForThread(3, 8), 0);
    BOOST_CHECK_EQUAL(NodeForThread(7, 8), 0);
    SetPreference(original);
}

BOOST_AUTO_TEST_CASE(numa_bind_thread_returns_false_when_disabled)
{
    using truenorth::numa::BindThreadToNode;
    using truenorth::numa::CurrentPreference;
    using truenorth::numa::NumaPref;
    using truenorth::numa::SetPreference;
    const auto original = CurrentPreference();
    SetPreference(NumaPref::OFF);
    BOOST_CHECK(!BindThreadToNode(0));
    SetPreference(original);
}

BOOST_AUTO_TEST_CASE(large_pages_off_still_hashes_correctly)
{
    // Force huge pages off and verify RandomXLightHash still works
    // (fallback path exercises no-large-pages allocation). The seed is
    // in a range unlikely to be touched by other tests so slot state
    // doesn't matter.
    using truenorth::CurrentLargePagesPreference;
    using truenorth::LargePagesPref;
    using truenorth::RandomXLightHash;
    using truenorth::SetLargePagesPreference;
    const auto original = CurrentLargePagesPreference();
    SetLargePagesPreference(LargePagesPref::OFF);

    const uint256 seed = ArithToUint256(arith_uint256(uint64_t{7001}));
    const unsigned char data[] = {'p'};
    // Just verifying no crash + hash is deterministic.
    const uint256 h1 = RandomXLightHash(seed, data, sizeof(data));
    const uint256 h2 = RandomXLightHash(seed, data, sizeof(data));
    BOOST_CHECK(h1 == h2);

    SetLargePagesPreference(original);
}

// -------- Two-slot cache LRU behavior (Issue #2) --------
//
// These tests share global slot state with each other (and with the
// LightHash tests above), which is unavoidable -- the wrapper's slot
// map is a process singleton by design. Tests use disjoint seed
// ranges (100+, 200+, 300+, 400+, 500+) to reduce cross-test
// interference, and assert only invariants that hold regardless of
// prior slot contents.

BOOST_AUTO_TEST_CASE(randomx_cache_allocations_bounded_by_two)
{
    using truenorth::RandomXCacheAllocations;
    using truenorth::RandomXLightHash;
    const unsigned char data[] = {'q'};
    // Request many unique seeds; slot count should never exceed 2.
    // The two-slot LRU must evict the oldest, not accumulate.
    for (uint64_t i = 300; i < 320; ++i) {
        const uint256 s = ArithToUint256(arith_uint256(i));
        RandomXLightHash(s, data, sizeof(data));
        BOOST_CHECK(RandomXCacheAllocations() <= std::size_t{2});
    }
    // After the loop, exactly the last two seeds should occupy the
    // slots; total slot count is 2.
    BOOST_CHECK_EQUAL(RandomXCacheAllocations(), std::size_t{2});
}

BOOST_AUTO_TEST_CASE(randomx_hash_stable_across_slot_eviction)
{
    using truenorth::RandomXLightHash;
    const uint256 seed_a = ArithToUint256(arith_uint256(uint64_t{200}));
    const uint256 seed_b = ArithToUint256(arith_uint256(uint64_t{201}));
    const uint256 seed_c = ArithToUint256(arith_uint256(uint64_t{202}));
    const unsigned char data[] = {'x', 'y', 'z'};

    // Establish canonical hash for seed_a.
    const uint256 hash_first = RandomXLightHash(seed_a, data, sizeof(data));

    // Force eviction of seed_a: two subsequent unique seeds push it
    // out of both slots (main -> secondary -> evicted).
    RandomXLightHash(seed_b, data, sizeof(data));
    RandomXLightHash(seed_c, data, sizeof(data));

    // Request seed_a again; must allocate fresh cache and produce the
    // same hash. This is the core correctness invariant of the LRU:
    // eviction never changes hash results for a given (seed, data)
    // pair.
    const uint256 hash_second = RandomXLightHash(seed_a, data, sizeof(data));
    BOOST_CHECK(hash_first == hash_second);
}

BOOST_AUTO_TEST_CASE(randomx_cache_promotion_preserves_hash)
{
    using truenorth::RandomXLightHash;
    const uint256 seed_a = ArithToUint256(arith_uint256(uint64_t{400}));
    const uint256 seed_b = ArithToUint256(arith_uint256(uint64_t{401}));
    const unsigned char data[] = {'w'};

    const uint256 hash_a_first = RandomXLightHash(seed_a, data, sizeof(data));
    // Push seed_a to secondary by adding seed_b.
    RandomXLightHash(seed_b, data, sizeof(data));
    // Requesting seed_a again promotes secondary -> main without
    // reallocation. Hash must match.
    const uint256 hash_a_promoted = RandomXLightHash(seed_a, data, sizeof(data));
    BOOST_CHECK(hash_a_first == hash_a_promoted);
}

BOOST_AUTO_TEST_CASE(randomx_cache_allocated_bytes_matches_slot_count)
{
    using truenorth::RandomXCacheAllocatedBytes;
    using truenorth::RandomXCacheAllocations;
    using truenorth::RandomXLightHash;
    const unsigned char data[] = {'m'};
    // Ensure at least one slot is populated.
    const uint256 s = ArithToUint256(arith_uint256(uint64_t{500}));
    RandomXLightHash(s, data, sizeof(data));

    const std::size_t slots = RandomXCacheAllocations();
    const std::uint64_t bytes = RandomXCacheAllocatedBytes();
    BOOST_CHECK(slots >= 1);
    // In LIGHT mode (validation-only, no miner active), each cache is
    // nominally 256 MiB. The reported byte total should be exactly
    // slots * 256 MiB when no FAST cache is present.
    const std::uint64_t light_per_slot = std::uint64_t{256} * 1024 * 1024;
    BOOST_CHECK_EQUAL(bytes, light_per_slot * slots);
}

// -------- Maximum reorg-depth cap (Issue #8) --------
//
// End-to-end enforcement (that Chainstate::ActivateBestChainStep
// actually refuses a > cap reorg) is not covered by these unit tests --
// it requires setting up a full Chainstate with two forks and would
// depend on a test-flag override for regtest (which sets
// max_reorg_depth=0 by design so reorg regression scripts still work).
// Here we verify that the chainparams-level configuration is correct;
// the wire-up is covered by inspection + the compile-time integration
// with ActivateBestChainStep.

BOOST_AUTO_TEST_CASE(reorg_cap_mainnet_is_72_blocks)
{
    const auto params = CChainParams::Main();
    BOOST_CHECK_EQUAL(params->GetConsensus().max_reorg_depth, 72);
}

BOOST_AUTO_TEST_CASE(reorg_cap_testnet4_is_72_blocks)
{
    const auto params = CChainParams::TestNet4();
    BOOST_CHECK_EQUAL(params->GetConsensus().max_reorg_depth, 72);
}

BOOST_AUTO_TEST_CASE(reorg_cap_regtest_is_disabled)
{
    const auto params = CChainParams::RegTest(CChainParams::RegTestOptions{});
    // Regtest deliberately has no cap so reorg-heavy regression tests
    // (e.g. local_testnet_reorg.sh) continue to work.
    BOOST_CHECK_EQUAL(params->GetConsensus().max_reorg_depth, 0);
}

// =========================================================================
// F. P2QRH commitment helper (pure function)
// =========================================================================

BOOST_AUTO_TEST_CASE(qrh_commitment_helper_deterministic)
{
    const std::vector<unsigned char> pubkey(32, 0xAB);
    const uint256 a = truenorth::ComputeQRHCommitment(
        truenorth::QRH_SCHEME_SCHNORR, pubkey, truenorth::QRH_EMPTY_SCRIPT_ROOT);
    const uint256 b = truenorth::ComputeQRHCommitment(
        truenorth::QRH_SCHEME_SCHNORR, pubkey, truenorth::QRH_EMPTY_SCRIPT_ROOT);
    BOOST_CHECK(a == b);
}

BOOST_AUTO_TEST_CASE(qrh_commitment_helper_differs_by_scheme_id)
{
    const std::vector<unsigned char> pubkey(32, 0xAB);
    const uint256 a = truenorth::ComputeQRHCommitment(
        truenorth::QRH_SCHEME_SCHNORR, pubkey, truenorth::QRH_EMPTY_SCRIPT_ROOT);
    const uint256 b = truenorth::ComputeQRHCommitment(
        0x02 /* future PQ scheme */, pubkey, truenorth::QRH_EMPTY_SCRIPT_ROOT);
    BOOST_CHECK(a != b);
}

BOOST_AUTO_TEST_CASE(qrh_commitment_helper_differs_by_pubkey)
{
    const std::vector<unsigned char> pk_a(32, 0xAB);
    const std::vector<unsigned char> pk_b(32, 0xCD);
    const uint256 a = truenorth::ComputeQRHCommitment(
        truenorth::QRH_SCHEME_SCHNORR, pk_a, truenorth::QRH_EMPTY_SCRIPT_ROOT);
    const uint256 b = truenorth::ComputeQRHCommitment(
        truenorth::QRH_SCHEME_SCHNORR, pk_b, truenorth::QRH_EMPTY_SCRIPT_ROOT);
    BOOST_CHECK(a != b);
}

BOOST_AUTO_TEST_CASE(qrh_commitment_helper_differs_by_script_root)
{
    const std::vector<unsigned char> pubkey(32, 0xAB);
    const uint256 nonzero_root = uint256::ONE;
    const uint256 a = truenorth::ComputeQRHCommitment(
        truenorth::QRH_SCHEME_SCHNORR, pubkey, truenorth::QRH_EMPTY_SCRIPT_ROOT);
    const uint256 b = truenorth::ComputeQRHCommitment(
        truenorth::QRH_SCHEME_SCHNORR, pubkey, nonzero_root);
    BOOST_CHECK(a != b);
}

// =========================================================================
// G. P2QRH consensus validation (VerifyScript over witness v2 spends)
// =========================================================================

namespace {

// A fully-signed valid QRH key-path spend, ready to be mutated for negative
// tests. Callers modify the witness stack or scriptPubKey and re-verify.
struct QRHSpendFixture {
    CKey key;
    XOnlyPubKey xpk;
    CAmount amount{100 * COIN};
    CScript scriptPubKey;
    CMutableTransaction txCredit;
    CMutableTransaction txSpend;
    PrecomputedTransactionData txdata;
    std::vector<unsigned char> sig; // 64-byte Schnorr signature over BIP-341 sighash
};

std::unique_ptr<QRHSpendFixture> MakeValidQRHSpend()
{
    auto f = std::make_unique<QRHSpendFixture>();

    f->key.MakeNewKey(true);
    f->xpk = XOnlyPubKey{f->key.GetPubKey()};

    // Build the QRH scriptPubKey.
    const uint256 commitment = truenorth::ComputeQRHCommitment(
        truenorth::QRH_SCHEME_SCHNORR,
        std::span<const unsigned char>(f->xpk.begin(), f->xpk.end()),
        truenorth::QRH_EMPTY_SCRIPT_ROOT);
    f->scriptPubKey = CScript() << OP_2 << ToByteVector(commitment);

    // Credit tx: single output pinned to the QRH scriptPubKey.
    f->txCredit.vin.emplace_back();
    f->txCredit.vin[0].scriptSig = CScript() << OP_0 << OP_0;
    f->txCredit.vout.emplace_back(f->amount, f->scriptPubKey);

    // Spend tx: consumes the QRH output, pays to a trivial anyone-can-spend
    // sink. The fee is folded into the sink amount for test simplicity.
    f->txSpend.vin.emplace_back(COutPoint{f->txCredit.GetHash(), 0});
    f->txSpend.vout.emplace_back(f->amount, CScript() << OP_TRUE);

    // Precompute BIP-341 sighash inputs.
    std::vector<CTxOut> spent_outputs{f->txCredit.vout[0]};
    f->txdata.Init(CTransaction(f->txSpend), std::move(spent_outputs), /*force=*/true);

    // Compute sighash and sign with the raw key (no taproot tweak — QRH
    // signs against the raw x-only pubkey).
    ScriptExecutionData sed;
    sed.m_annex_init = true;
    sed.m_annex_present = false;
    uint256 sighash;
    BOOST_REQUIRE(SignatureHashSchnorr(sighash, sed, CTransaction(f->txSpend), 0,
                                       SIGHASH_DEFAULT, SigVersion::TAPROOT, f->txdata,
                                       MissingDataBehavior::FAIL));
    f->sig.assign(64, 0);
    uint256 aux{}; // deterministic aux for reproducible tests
    BOOST_REQUIRE(f->key.SignSchnorr(sighash, f->sig, /*merkle_root=*/nullptr, aux));
    return f;
}

// Verify a QRH spend with the given witness stack against the given
// scriptPubKey. Returns (accepted, error).
std::pair<bool, ScriptError> RunVerify(const QRHSpendFixture& f,
                                       const std::vector<std::vector<unsigned char>>& witness_stack,
                                       const CScript& scriptPubKey_override,
                                       unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_QRH)
{
    CScriptWitness witness;
    witness.stack = witness_stack;
    ScriptError err = SCRIPT_ERR_OK;
    const bool ok = VerifyScript(
        CScript{}, // scriptSig empty (native segwit)
        scriptPubKey_override,
        &witness,
        flags,
        MutableTransactionSignatureChecker{&f.txSpend, 0, f.amount, f.txdata,
                                           MissingDataBehavior::ASSERT_FAIL},
        &err);
    return {ok, err};
}

std::vector<std::vector<unsigned char>> ValidWitnessStack(const QRHSpendFixture& f)
{
    return {f.sig,
            std::vector<unsigned char>{f.xpk.begin(), f.xpk.end()},
            {truenorth::QRH_SCHEME_SCHNORR}};
}

} // namespace

BOOST_AUTO_TEST_CASE(qrh_valid_key_path_spend_accepts)
{
    auto f = MakeValidQRHSpend();
    const auto [ok, err] = RunVerify(*f, ValidWitnessStack(*f), f->scriptPubKey);
    BOOST_CHECK(ok);
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);
}

BOOST_AUTO_TEST_CASE(qrh_unknown_scheme_id_rejects)
{
    auto f = MakeValidQRHSpend();
    auto stack = ValidWitnessStack(*f);
    stack[2] = {0x02}; // reserved for future PQ scheme; unknown at launch
    const auto [ok, err] = RunVerify(*f, stack, f->scriptPubKey);
    BOOST_CHECK(!ok);
}

BOOST_AUTO_TEST_CASE(qrh_scheme_id_zero_rejects)
{
    auto f = MakeValidQRHSpend();
    auto stack = ValidWitnessStack(*f);
    stack[2] = {truenorth::QRH_SCHEME_RESERVED_ZERO};
    const auto [ok, err] = RunVerify(*f, stack, f->scriptPubKey);
    BOOST_CHECK(!ok);
}

BOOST_AUTO_TEST_CASE(qrh_scheme_id_wrong_byte_length_rejects)
{
    auto f = MakeValidQRHSpend();
    auto stack = ValidWitnessStack(*f);
    stack[2] = {0x01, 0x00}; // 2 bytes instead of 1
    const auto [ok, err] = RunVerify(*f, stack, f->scriptPubKey);
    BOOST_CHECK(!ok);
}

BOOST_AUTO_TEST_CASE(qrh_wrong_pubkey_size_rejects)
{
    auto f = MakeValidQRHSpend();
    auto stack = ValidWitnessStack(*f);
    stack[1].pop_back(); // 31 bytes instead of 32
    const auto [ok, err] = RunVerify(*f, stack, f->scriptPubKey);
    BOOST_CHECK(!ok);
}

BOOST_AUTO_TEST_CASE(qrh_commitment_mismatch_rejects)
{
    auto f = MakeValidQRHSpend();
    auto stack = ValidWitnessStack(*f);
    // Flip one bit of the revealed pubkey. Signature verification is
    // never reached because the commitment recomputation fails first.
    stack[1][0] ^= 0x01;
    const auto [ok, err] = RunVerify(*f, stack, f->scriptPubKey);
    BOOST_CHECK(!ok);
}

BOOST_AUTO_TEST_CASE(qrh_invalid_signature_rejects)
{
    auto f = MakeValidQRHSpend();
    auto stack = ValidWitnessStack(*f);
    stack[0][0] ^= 0xFF; // corrupt the signature
    const auto [ok, err] = RunVerify(*f, stack, f->scriptPubKey);
    BOOST_CHECK(!ok);
}

BOOST_AUTO_TEST_CASE(qrh_empty_witness_rejects)
{
    auto f = MakeValidQRHSpend();
    const auto [ok, err] = RunVerify(*f, {}, f->scriptPubKey);
    BOOST_CHECK(!ok);
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_WITNESS_PROGRAM_WITNESS_EMPTY);
}

BOOST_AUTO_TEST_CASE(qrh_too_few_stack_items_rejects)
{
    auto f = MakeValidQRHSpend();
    auto stack = ValidWitnessStack(*f);
    stack.pop_back(); // drop scheme_id -> only 2 items left
    const auto [ok, err] = RunVerify(*f, stack, f->scriptPubKey);
    BOOST_CHECK(!ok);
}

BOOST_AUTO_TEST_CASE(qrh_extra_stack_items_rejects)
{
    auto f = MakeValidQRHSpend();
    auto stack = ValidWitnessStack(*f);
    // Insert an unexpected extra element at the front; script-path
    // spending is not yet supported at consensus.
    stack.insert(stack.begin(), {0xDE, 0xAD});
    const auto [ok, err] = RunVerify(*f, stack, f->scriptPubKey);
    BOOST_CHECK(!ok);
}

BOOST_AUTO_TEST_CASE(qrh_flag_off_passes_through)
{
    // Without SCRIPT_VERIFY_QRH, the branch is dormant and any witness
    // shape returns success (matches taproot's soft-fork-compat behavior
    // when SCRIPT_VERIFY_TAPROOT is off).
    auto f = MakeValidQRHSpend();
    auto stack = ValidWitnessStack(*f);
    stack[2] = {0x99}; // unknown scheme_id — would reject with flag on
    const auto [ok, err] = RunVerify(*f, stack, f->scriptPubKey, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS);
    BOOST_CHECK(ok);
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);
}

// =========================================================================
// H. P2QRH wallet-side signing (descriptor -> spend data -> SignStep)
// =========================================================================

// Descriptor path: parsing `qrh(HEX_XONLY_PUBKEY)` and expanding it should
// produce the correct scriptPubKey and populate qrh_spend_data so that
// later signing can look up the pubkey.
BOOST_AUTO_TEST_CASE(qrh_descriptor_populates_spend_data)
{
    CKey key;
    key.MakeNewKey(true);
    const XOnlyPubKey xpk{key.GetPubKey()};
    const std::string desc_str = "qrh(" + HexStr(xpk) + ")";

    FlatSigningProvider in_provider;
    std::string error;
    auto descs = Parse(desc_str, in_provider, error, /*require_checksum=*/false);
    BOOST_REQUIRE_MESSAGE(descs.size() == 1, "Parse: " + error);

    FlatSigningProvider out_provider;
    std::vector<CScript> scripts;
    BOOST_REQUIRE(descs[0]->Expand(0, in_provider, scripts, out_provider));
    BOOST_REQUIRE_EQUAL(scripts.size(), 1u);

    // Sanity-check the scriptPubKey shape: OP_2 <push32> <32-byte commitment>.
    BOOST_REQUIRE_EQUAL(scripts[0].size(), 34u);
    BOOST_CHECK_EQUAL(scripts[0][0], OP_2);

    // The descriptor should have populated spend data indexed by the same
    // commitment that ComputeQRHCommitment produces for these inputs.
    const uint256 expected_commitment = truenorth::ComputeQRHCommitment(
        truenorth::QRH_SCHEME_SCHNORR,
        std::span<const unsigned char>(xpk.begin(), xpk.end()),
        truenorth::QRH_EMPTY_SCRIPT_ROOT);
    QRHSpendData spend;
    BOOST_REQUIRE(out_provider.GetQRHSpendData(expected_commitment, spend));
    BOOST_CHECK_EQUAL(spend.scheme_id, truenorth::QRH_SCHEME_SCHNORR);
    BOOST_CHECK(spend.internal_key == xpk);
    BOOST_CHECK(spend.script_root == truenorth::QRH_EMPTY_SCRIPT_ROOT);
}

// Round-trip: given spend data (as populated by a descriptor) and the private
// key, ProduceSignature should produce a witness that VerifyScript accepts.
BOOST_AUTO_TEST_CASE(qrh_sign_step_produces_valid_witness)
{
    auto f = MakeValidQRHSpend();

    // Assemble the signing provider as the wallet would (private key +
    // qrh_spend_data populated at descriptor-expand time).
    FlatSigningProvider provider;
    provider.keys[f->key.GetPubKey().GetID()] = f->key;
    provider.pubkeys[f->key.GetPubKey().GetID()] = f->key.GetPubKey();
    const uint256 commitment = truenorth::ComputeQRHCommitment(
        truenorth::QRH_SCHEME_SCHNORR,
        std::span<const unsigned char>(f->xpk.begin(), f->xpk.end()),
        truenorth::QRH_EMPTY_SCRIPT_ROOT);
    provider.qrh_spend_data[commitment] = QRHSpendData{
        truenorth::QRH_SCHEME_SCHNORR,
        f->xpk,
        truenorth::QRH_EMPTY_SCRIPT_ROOT,
    };

    // Sign via the same code path the wallet uses at spend time.
    MutableTransactionSignatureCreator creator{f->txSpend, 0, f->amount, &f->txdata, SIGHASH_DEFAULT};
    SignatureData sigdata;
    BOOST_REQUIRE(ProduceSignature(provider, creator, f->scriptPubKey, sigdata));
    BOOST_CHECK(sigdata.complete);

    // Witness shape: [signature, pubkey, scheme_id].
    BOOST_REQUIRE_EQUAL(sigdata.scriptWitness.stack.size(), 3u);
    BOOST_CHECK_EQUAL(sigdata.scriptWitness.stack[0].size(), 64u); // Schnorr sig (SIGHASH_DEFAULT: no hash byte appended)
    BOOST_CHECK_EQUAL(sigdata.scriptWitness.stack[1].size(), 32u); // x-only pubkey
    BOOST_REQUIRE_EQUAL(sigdata.scriptWitness.stack[2].size(), 1u);
    BOOST_CHECK_EQUAL(sigdata.scriptWitness.stack[2][0], truenorth::QRH_SCHEME_SCHNORR);

    // And the resulting witness passes consensus.
    ScriptError err = SCRIPT_ERR_OK;
    const bool ok = VerifyScript(
        CScript{}, f->scriptPubKey, &sigdata.scriptWitness,
        SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_QRH,
        MutableTransactionSignatureChecker{&f->txSpend, 0, f->amount, f->txdata, MissingDataBehavior::ASSERT_FAIL},
        &err);
    BOOST_CHECK(ok);
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);
}

// SignStep should refuse to sign when the provider has no spend data for
// the commitment (e.g. the wallet does not own this output).
BOOST_AUTO_TEST_CASE(qrh_sign_step_without_spend_data_fails)
{
    auto f = MakeValidQRHSpend();

    // Provider has the key but no qrh_spend_data entry -- SignStep cannot
    // recover the pubkey behind the commitment.
    FlatSigningProvider provider;
    provider.keys[f->key.GetPubKey().GetID()] = f->key;
    provider.pubkeys[f->key.GetPubKey().GetID()] = f->key.GetPubKey();

    MutableTransactionSignatureCreator creator{f->txSpend, 0, f->amount, &f->txdata, SIGHASH_DEFAULT};
    SignatureData sigdata;
    ProduceSignature(provider, creator, f->scriptPubKey, sigdata);
    BOOST_CHECK(!sigdata.complete);
    BOOST_CHECK(sigdata.scriptWitness.stack.empty());
}

BOOST_AUTO_TEST_SUITE_END()
