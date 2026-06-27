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
#include <pow.h>
#include <primitives/block.h>
#include <test/util/setup_common.h>
#include <truenorth/randomx_wrapper.h>
#include <truenorth/seed_key.h>
#include <uint256.h>
#include <util/chaintype.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

#include <deque>
#include <memory>
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
    const arith_uint256 diff = (t_initial > t_result) ? t_initial - t_result
                                                      : t_result - t_initial;
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

BOOST_AUTO_TEST_SUITE_END()
