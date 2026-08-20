// Copyright (c) 2026 The TrueNorth developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <checkpoints.h>

#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <map>
#include <memory>

// Unit tests for the reintroduced checkpoint infrastructure. Wire-up
// (that a block with a wrong hash at a checkpointed height is rejected
// by validation) is covered by the regression script under
// test/truenorth/local_testnet_checkpoint.sh; here we only test the
// pure Checkpoints:: functions.

BOOST_AUTO_TEST_SUITE(checkpoints_tests)

namespace {

// Helper: build a fixed uint256 from a small integer for deterministic
// test data. Uses ArithToUint256 so the test hashes are well-defined
// but obviously not real block hashes.
uint256 FakeHash(uint64_t n)
{
    return ArithToUint256(arith_uint256{n});
}

// Helper: build a CBlockIndex with only nHeight and phashBlock set --
// enough for GetLastCheckpoint to identify it. Owned by the test.
struct TestBlockIndex {
    uint256 hash;
    CBlockIndex index;
    TestBlockIndex(int height, const uint256& h) : hash(h)
    {
        index.nHeight = height;
        index.phashBlock = &hash;
    }
};

// Helper: mimic BlockManager's block-hash-to-index lookup with a
// caller-provided map, for GetLastCheckpoint testing.
struct LookupMap {
    std::map<uint256, const CBlockIndex*> m;

    std::function<const CBlockIndex*(const uint256&)> as_lookup() const
    {
        return [this](const uint256& h) -> const CBlockIndex* {
            auto it = m.find(h);
            return it == m.end() ? nullptr : it->second;
        };
    }
};

} // namespace

BOOST_AUTO_TEST_CASE(check_block_empty_data_accepts_everything)
{
    CCheckpointData data;
    // Any (height, hash) pair should pass when there are no checkpoints.
    BOOST_CHECK(Checkpoints::CheckBlock(data, 0, FakeHash(0)));
    BOOST_CHECK(Checkpoints::CheckBlock(data, 1000, FakeHash(999)));
    BOOST_CHECK(Checkpoints::CheckBlock(data, 100000, FakeHash(12345)));
}

BOOST_AUTO_TEST_CASE(check_block_non_checkpointed_height_accepts)
{
    CCheckpointData data;
    data.mapCheckpoints[100] = FakeHash(100);
    data.mapCheckpoints[200] = FakeHash(200);
    // Heights that aren't checkpointed pass with any hash.
    BOOST_CHECK(Checkpoints::CheckBlock(data, 50, FakeHash(0)));
    BOOST_CHECK(Checkpoints::CheckBlock(data, 150, FakeHash(999)));
    BOOST_CHECK(Checkpoints::CheckBlock(data, 250, FakeHash(1)));
}

BOOST_AUTO_TEST_CASE(check_block_matching_hash_accepts)
{
    CCheckpointData data;
    data.mapCheckpoints[100] = FakeHash(100);
    BOOST_CHECK(Checkpoints::CheckBlock(data, 100, FakeHash(100)));
}

BOOST_AUTO_TEST_CASE(check_block_wrong_hash_at_checkpoint_rejects)
{
    CCheckpointData data;
    data.mapCheckpoints[100] = FakeHash(100);
    BOOST_CHECK(!Checkpoints::CheckBlock(data, 100, FakeHash(101)));
    BOOST_CHECK(!Checkpoints::CheckBlock(data, 100, FakeHash(0)));
    BOOST_CHECK(!Checkpoints::CheckBlock(data, 100, uint256::ZERO));
}

BOOST_AUTO_TEST_CASE(check_block_multiple_checkpoints)
{
    CCheckpointData data;
    data.mapCheckpoints[10] = FakeHash(10);
    data.mapCheckpoints[20] = FakeHash(20);
    data.mapCheckpoints[30] = FakeHash(30);

    // Correct hashes at each checkpointed height: all pass.
    BOOST_CHECK(Checkpoints::CheckBlock(data, 10, FakeHash(10)));
    BOOST_CHECK(Checkpoints::CheckBlock(data, 20, FakeHash(20)));
    BOOST_CHECK(Checkpoints::CheckBlock(data, 30, FakeHash(30)));

    // Wrong hashes at each checkpointed height: all fail.
    BOOST_CHECK(!Checkpoints::CheckBlock(data, 10, FakeHash(20)));
    BOOST_CHECK(!Checkpoints::CheckBlock(data, 20, FakeHash(30)));
    BOOST_CHECK(!Checkpoints::CheckBlock(data, 30, FakeHash(10)));
}

BOOST_AUTO_TEST_CASE(get_last_checkpoint_empty_data_returns_null)
{
    CCheckpointData data;
    LookupMap lookup;
    BOOST_CHECK(Checkpoints::GetLastCheckpoint(data, lookup.as_lookup()) == nullptr);
}

BOOST_AUTO_TEST_CASE(get_last_checkpoint_no_matching_index_returns_null)
{
    CCheckpointData data;
    data.mapCheckpoints[100] = FakeHash(100);
    data.mapCheckpoints[200] = FakeHash(200);
    LookupMap lookup; // nothing in the map
    BOOST_CHECK(Checkpoints::GetLastCheckpoint(data, lookup.as_lookup()) == nullptr);
}

BOOST_AUTO_TEST_CASE(get_last_checkpoint_returns_highest_present)
{
    CCheckpointData data;
    data.mapCheckpoints[100] = FakeHash(100);
    data.mapCheckpoints[200] = FakeHash(200);
    data.mapCheckpoints[300] = FakeHash(300);

    // Only the middle checkpoint is present in the index.
    TestBlockIndex tbi_200(200, FakeHash(200));
    LookupMap lookup;
    lookup.m[FakeHash(200)] = &tbi_200.index;

    const CBlockIndex* got = Checkpoints::GetLastCheckpoint(data, lookup.as_lookup());
    BOOST_REQUIRE(got != nullptr);
    BOOST_CHECK_EQUAL(got->nHeight, 200);
}

BOOST_AUTO_TEST_CASE(get_last_checkpoint_prefers_higher_when_multiple_present)
{
    CCheckpointData data;
    data.mapCheckpoints[100] = FakeHash(100);
    data.mapCheckpoints[200] = FakeHash(200);
    data.mapCheckpoints[300] = FakeHash(300);

    TestBlockIndex tbi_100(100, FakeHash(100));
    TestBlockIndex tbi_200(200, FakeHash(200));
    LookupMap lookup;
    lookup.m[FakeHash(100)] = &tbi_100.index;
    lookup.m[FakeHash(200)] = &tbi_200.index;
    // 300 is NOT in the lookup map.

    // Should return 200 (the highest present), not 100.
    const CBlockIndex* got = Checkpoints::GetLastCheckpoint(data, lookup.as_lookup());
    BOOST_REQUIRE(got != nullptr);
    BOOST_CHECK_EQUAL(got->nHeight, 200);
}

BOOST_AUTO_TEST_SUITE_END()
