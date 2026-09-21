// Copyright (c) 2015-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <pow.h>
#include <primitives/block.h>
#include <test/util/random.h>
#include <test/util/setup_common.h>
#include <tinyformat.h>
#include <util/chaintype.h>

#include <vector>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(pow_tests, BasicTestingSetup)

namespace {
//! LWMA window length used by CalculateNextWorkRequired().
constexpr int64_t LWMA_N{90};
//! A target 256x harder than mainnet's powLimit, so tests have room to move in
//! either direction without clamping.
constexpr uint32_t TEST_NBITS{0x1c00ffff};

//! Fill `blocks` with a linked chain spaced `spacing` seconds apart, every
//! block carrying `nbits`. Filled in place: the indices point at each other.
void BuildChain(std::vector<CBlockIndex>& blocks, int64_t spacing, uint32_t nbits)
{
    for (size_t i = 0; i < blocks.size(); ++i) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = static_cast<int>(i);
        blocks[i].nTime = static_cast<unsigned int>(1700000000 + static_cast<int64_t>(i) * spacing);
        blocks[i].nBits = nbits;
    }
}

//! Compact targets lose mantissa bits and the LWMA averages with integer
//! division, so compare within 1% rather than exactly.
void CheckTargetNear(uint32_t actual_nbits, const arith_uint256& expected)
{
    arith_uint256 actual;
    actual.SetCompact(actual_nbits);
    BOOST_CHECK_MESSAGE(actual <= expected + expected / 100 && actual >= expected - expected / 100,
                        strprintf("target %s is not within 1%% of %s", actual.ToString(), expected.ToString()));
}

arith_uint256 TargetOf(uint32_t nbits)
{
    arith_uint256 target;
    target.SetCompact(nbits);
    return target;
}
} // namespace

/* Blocks arriving exactly on target spacing leave difficulty where it is */
BOOST_AUTO_TEST_CASE(lwma_steady_state)
{
    const auto consensus{CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus()};
    std::vector<CBlockIndex> blocks(200);
    BuildChain(blocks, consensus.nPowTargetSpacing, TEST_NBITS);
    CheckTargetNear(CalculateNextWorkRequired(&blocks.back(), 0, consensus), TargetOf(TEST_NBITS));
}

/* Blocks arriving twice as fast halve the target (double the difficulty) */
BOOST_AUTO_TEST_CASE(lwma_fast_blocks_raise_difficulty)
{
    const auto consensus{CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus()};
    std::vector<CBlockIndex> blocks(200);
    BuildChain(blocks, consensus.nPowTargetSpacing / 2, TEST_NBITS);
    CheckTargetNear(CalculateNextWorkRequired(&blocks.back(), 0, consensus), TargetOf(TEST_NBITS) / 2);
}

/* Blocks arriving three times as slow triple the target */
BOOST_AUTO_TEST_CASE(lwma_slow_blocks_lower_difficulty)
{
    const auto consensus{CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus()};
    std::vector<CBlockIndex> blocks(200);
    BuildChain(blocks, consensus.nPowTargetSpacing * 3, TEST_NBITS);
    CheckTargetNear(CalculateNextWorkRequired(&blocks.back(), 0, consensus), TargetOf(TEST_NBITS) * 3);
}

/* Solvetimes are clamped at 6x spacing, so an enormous gap is no worse than 6x */
BOOST_AUTO_TEST_CASE(lwma_clamps_long_solvetimes)
{
    const auto consensus{CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus()};
    std::vector<CBlockIndex> at_clamp(200);
    std::vector<CBlockIndex> past_clamp(200);
    BuildChain(at_clamp, consensus.nPowTargetSpacing * 6, TEST_NBITS);
    BuildChain(past_clamp, consensus.nPowTargetSpacing * 50, TEST_NBITS);
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&at_clamp.back(), 0, consensus),
                      CalculateNextWorkRequired(&past_clamp.back(), 0, consensus));
}

/* The weighted sum is floored at a tenth of the denominator, so even a chain of
 * identical timestamps cannot raise difficulty by more than 10x in one step */
BOOST_AUTO_TEST_CASE(lwma_floors_difficulty_spike)
{
    const auto consensus{CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus()};
    std::vector<CBlockIndex> blocks(200);
    BuildChain(blocks, /*spacing=*/0, TEST_NBITS);
    CheckTargetNear(CalculateNextWorkRequired(&blocks.back(), 0, consensus), TargetOf(TEST_NBITS) / 10);
}

/* The result is never easier than powLimit */
BOOST_AUTO_TEST_CASE(lwma_clamps_to_pow_limit)
{
    const auto consensus{CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus()};
    const uint32_t pow_limit_nbits{UintToArith256(consensus.powLimit).GetCompact()};
    std::vector<CBlockIndex> blocks(200);
    BuildChain(blocks, consensus.nPowTargetSpacing * 6, pow_limit_nbits);
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&blocks.back(), 0, consensus), pow_limit_nbits);
}

/* Before the window is full there is nothing to average, so difficulty is minimal */
BOOST_AUTO_TEST_CASE(lwma_insufficient_history)
{
    const auto consensus{CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus()};
    const uint32_t pow_limit_nbits{UintToArith256(consensus.powLimit).GetCompact()};
    std::vector<CBlockIndex> blocks(LWMA_N);
    BuildChain(blocks, consensus.nPowTargetSpacing, TEST_NBITS);
    // Tip is at height N-1, one short of the window.
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&blocks.back(), 0, consensus), pow_limit_nbits);
}

/* Regtest disables retargeting entirely */
BOOST_AUTO_TEST_CASE(lwma_no_retargeting)
{
    const auto consensus{CreateChainParams(*m_node.args, ChainType::REGTEST)->GetConsensus()};
    BOOST_REQUIRE(consensus.fPowNoRetargeting);
    std::vector<CBlockIndex> blocks(200);
    BuildChain(blocks, consensus.nPowTargetSpacing * 10, TEST_NBITS);
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&blocks.back(), 0, consensus), TEST_NBITS);
}

/* On chains that allow it, a block more than 2x spacing late may use min difficulty */
BOOST_AUTO_TEST_CASE(get_next_work_min_difficulty)
{
    const auto consensus{CreateChainParams(*m_node.args, ChainType::TESTNET4)->GetConsensus()};
    BOOST_REQUIRE(consensus.fPowAllowMinDifficultyBlocks);
    const uint32_t pow_limit_nbits{UintToArith256(consensus.powLimit).GetCompact()};
    std::vector<CBlockIndex> blocks(200);
    BuildChain(blocks, consensus.nPowTargetSpacing, TEST_NBITS);
    const CBlockIndex& tip{blocks.back()};

    CBlockHeader late;
    late.nTime = static_cast<unsigned int>(tip.GetBlockTime() + consensus.nPowTargetSpacing * 2 + 1);
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&tip, &late, consensus), pow_limit_nbits);

    CBlockHeader on_time;
    on_time.nTime = static_cast<unsigned int>(tip.GetBlockTime() + consensus.nPowTargetSpacing);
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&tip, &on_time, consensus),
                      CalculateNextWorkRequired(&tip, 0, consensus));
}

/* A chain sitting at a permissive powLimit must stay there rather than wrapping
 * around: sum_target is then near 2^255 and the retarget multiply would overflow */
BOOST_AUTO_TEST_CASE(lwma_permissive_pow_limit_no_overflow)
{
    const auto consensus{CreateChainParams(*m_node.args, ChainType::TESTNET4)->GetConsensus()};
    const arith_uint256 pow_limit{UintToArith256(consensus.powLimit)};
    BOOST_REQUIRE(pow_limit > TargetOf(0x1f00ffff)); // permissive, unlike mainnet
    std::vector<CBlockIndex> blocks(200);
    BuildChain(blocks, consensus.nPowTargetSpacing, pow_limit.GetCompact());
    // Truncating division can shave the low mantissa bit; wrapping, which is what
    // this guards against, moved the target by a factor of 2^17 instead.
    CheckTargetNear(CalculateNextWorkRequired(&blocks.back(), 0, consensus), TargetOf(pow_limit.GetCompact()));
}

/* A single min-difficulty block inside an otherwise hard window may only make the
 * next target easier, never harder. Multiplying before dividing used to wrap here
 * and produce an arbitrary, much harder target for the rest of the window. */
BOOST_AUTO_TEST_CASE(lwma_min_difficulty_block_in_window)
{
    const auto consensus{CreateChainParams(*m_node.args, ChainType::TESTNET4)->GetConsensus()};
    const uint32_t pow_limit_nbits{UintToArith256(consensus.powLimit).GetCompact()};

    std::vector<CBlockIndex> all_hard(200);
    BuildChain(all_hard, consensus.nPowTargetSpacing, TEST_NBITS);
    const arith_uint256 baseline{TargetOf(CalculateNextWorkRequired(&all_hard.back(), 0, consensus))};

    std::vector<CBlockIndex> with_min_diff(200);
    BuildChain(with_min_diff, consensus.nPowTargetSpacing, TEST_NBITS);
    with_min_diff[150].nBits = pow_limit_nbits; // one min-difficulty block in the window
    const arith_uint256 poisoned{TargetOf(CalculateNextWorkRequired(&with_min_diff.back(), 0, consensus))};

    BOOST_CHECK_MESSAGE(poisoned >= baseline,
                        strprintf("one min-difficulty block made the target harder: %s < %s",
                                  poisoned.ToString(), baseline.ToString()));
}

/* Mainnet does not allow min-difficulty blocks no matter how late they are */
BOOST_AUTO_TEST_CASE(get_next_work_no_min_difficulty_on_main)
{
    const auto consensus{CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus()};
    BOOST_REQUIRE(!consensus.fPowAllowMinDifficultyBlocks);
    std::vector<CBlockIndex> blocks(200);
    BuildChain(blocks, consensus.nPowTargetSpacing, TEST_NBITS);
    const CBlockIndex& tip{blocks.back()};

    CBlockHeader late;
    late.nTime = static_cast<unsigned int>(tip.GetBlockTime() + consensus.nPowTargetSpacing * 100);
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&tip, &late, consensus),
                      CalculateNextWorkRequired(&tip, 0, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_negative_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    nBits = UintToArith256(consensus.powLimit).GetCompact(true);
    hash = uint256{1};
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_overflow_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits{~0x00800000U};
    hash = uint256{1};
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_too_easy_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 nBits_arith = UintToArith256(consensus.powLimit);
    nBits_arith *= 2;
    nBits = nBits_arith.GetCompact();
    hash = uint256{1};
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_biger_hash_than_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 hash_arith = UintToArith256(consensus.powLimit);
    nBits = hash_arith.GetCompact();
    hash_arith *= 2; // hash > nBits
    hash = ArithToUint256(hash_arith);
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_zero_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 hash_arith{0};
    nBits = hash_arith.GetCompact();
    hash = ArithToUint256(hash_arith);
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(GetBlockProofEquivalentTime_test)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    std::vector<CBlockIndex> blocks(10000);
    for (int i = 0; i < 10000; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nTime = 1269211443 + i * chainParams->GetConsensus().nPowTargetSpacing;
        blocks[i].nBits = 0x207fffff; /* target 0x7fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    for (int j = 0; j < 1000; j++) {
        CBlockIndex *p1 = &blocks[m_rng.randrange(10000)];
        CBlockIndex *p2 = &blocks[m_rng.randrange(10000)];
        CBlockIndex *p3 = &blocks[m_rng.randrange(10000)];

        int64_t tdiff = GetBlockProofEquivalentTime(*p1, *p2, *p3, chainParams->GetConsensus());
        BOOST_CHECK_EQUAL(tdiff, p1->GetBlockTime() - p2->GetBlockTime());
    }
}

void sanity_check_chainparams(const ArgsManager& args, ChainType chain_type)
{
    const auto chainParams = CreateChainParams(args, chain_type);
    const auto consensus = chainParams->GetConsensus();

    // hash genesis is correct
    BOOST_CHECK_EQUAL(consensus.hashGenesisBlock, chainParams->GenesisBlock().GetHash());

    // target timespan is an even multiple of spacing
    BOOST_CHECK_EQUAL(consensus.nPowTargetTimespan % consensus.nPowTargetSpacing, 0);

    // genesis nBits is positive, doesn't overflow and is lower than powLimit
    arith_uint256 pow_compact;
    bool neg, over;
    pow_compact.SetCompact(chainParams->GenesisBlock().nBits, &neg, &over);
    BOOST_CHECK(!neg && pow_compact != 0);
    BOOST_CHECK(!over);
    BOOST_CHECK(UintToArith256(consensus.powLimit) >= pow_compact);

    // Upstream checked here that powLimit was small enough for the retarget
    // multiplication not to overflow. The test chains deliberately use a
    // permissive powLimit that is not, so CalculateNextWorkRequired() handles
    // the wide case itself; lwma_permissive_pow_limit_no_overflow covers it.
}

BOOST_AUTO_TEST_CASE(ChainParams_MAIN_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::MAIN);
}

BOOST_AUTO_TEST_CASE(ChainParams_REGTEST_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::REGTEST);
}

BOOST_AUTO_TEST_CASE(ChainParams_TESTNET_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::TESTNET);
}

BOOST_AUTO_TEST_CASE(ChainParams_TESTNET4_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::TESTNET4);
}

BOOST_AUTO_TEST_CASE(ChainParams_SIGNET_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::SIGNET);
}

BOOST_AUTO_TEST_SUITE_END()
