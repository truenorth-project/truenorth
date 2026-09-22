// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <common/system.h>
#include <interfaces/mining.h>
#include <node/miner.h>
#include <util/time.h>
#include <validation.h>

#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

using interfaces::BlockTemplate;
using interfaces::Mining;
using node::BlockAssembler;
using node::BlockWaitOptions;

namespace testnet4_miner_tests {

struct Testnet4MinerTestingSetup : public Testnet4Setup {
    std::unique_ptr<Mining> MakeMining()
    {
        return interfaces::MakeMining(m_node);
    }
};
} // namespace testnet4_miner_tests

BOOST_FIXTURE_TEST_SUITE(testnet4_miner_tests, Testnet4MinerTestingSetup)

BOOST_AUTO_TEST_CASE(MiningInterface)
{
    auto mining{MakeMining()};
    BOOST_REQUIRE(mining);

    BlockAssembler::Options options;
    std::unique_ptr<BlockTemplate> block_template;

    // Set node time a few minutes past the testnet4 genesis block
    const int64_t genesis_time{WITH_LOCK(cs_main, return m_node.chainman->ActiveChain().Tip()->GetBlockTime())};
    SetMockTime(genesis_time + 3 * 60);

    block_template = mining->createNewBlock(options);
    BOOST_REQUIRE(block_template);

    // The template should use the mocked system time
    BOOST_REQUIRE_EQUAL(block_template->getBlockHeader().nTime, genesis_time + 3 * 60);

    const BlockWaitOptions wait_options{.timeout = MillisecondsDouble{0}, .fee_threshold = 1};

    // waitNext() should return nullptr because there is no better template
    auto should_be_nullptr = block_template->waitNext(wait_options);
    BOOST_REQUIRE(should_be_nullptr == nullptr);

    // Upstream checked here that once the block ran far enough behind, the
    // min-difficulty rule dropped the target and waitNext() produced a better
    // template. testnet4 no longer carries that rule (see
    // doc/testnet4-reset.md), so a late block changes nothing about the
    // template and waitNext() keeps returning nullptr however long it waits.
    for (const int64_t delay : {20 * 60, 20 * 60 + 1, 6 * 60 * 60}) {
        {
            LOCK(cs_main);
            SetMockTime(m_node.chainman->ActiveChain().Tip()->GetBlockTime() + delay);
        }
        should_be_nullptr = block_template->waitNext(wait_options);
        BOOST_REQUIRE(should_be_nullptr == nullptr);
    }
}

BOOST_AUTO_TEST_SUITE_END()
