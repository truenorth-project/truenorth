#!/usr/bin/env python3
# Copyright (c) 2019-2022 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test basic signet functionality

Upstream drives most of this test from a list of pregenerated signet blocks.
None of them are usable here: they are Bitcoin signet blocks, so they carry
SHA256d proof of work rather than RandomX, build on Bitcoin's signet genesis,
and their solutions sign Bitcoin's default challenge. Submitting one to a
TrueNorth node fails at "high-hash" long before the signet rules are reached,
which tests nothing about signet.

The blocks are also submitted upstream to a node running the *default*
challenge, and TrueNorth's default is the placeholder OP_RETURN (`6a`) set in
SigNetParams -- a script nothing can satisfy, deliberately, because there is
no public TrueNorth signet to point at. So that node could never accept a
block on any chain, from any miner.

Blocks are generated here instead, on the OP_TRUE signet, where the empty
solution satisfies the challenge and ordinary mining therefore works. That
covers the same ground the pregenerated blocks did -- a signet accepts a block
whose solution satisfies its own challenge and rejects one that does not --
against the chain this binary actually runs.
"""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

# TrueNorth's default signet challenge is a placeholder OP_RETURN (see
# SigNetParams in src/kernel/chainparams.cpp); a real signet passes
# -signetchallenge explicitly.
SIGNET_DEFAULT_CHALLENGE = '6a'


class SignetParams:
    def __init__(self, challenge=None):
        # Prune to prevent disk space warning on CI systems with limited space,
        # when using networks other than regtest.
        if challenge is None:
            self.challenge = SIGNET_DEFAULT_CHALLENGE
            self.shared_args = ["-prune=1500"]
        else:
            self.challenge = challenge
            self.shared_args = ["-prune=1500", f"-signetchallenge={challenge}"]

class SignetBasicTest(BitcoinTestFramework):
    def set_test_params(self):
        self.chain = "signet"
        self.num_nodes = 6
        self.setup_clean_chain = True
        self.signets = [
            SignetParams(challenge='51'), # OP_TRUE
            SignetParams(), # default challenge
            # default challenge as a 2-of-2, which means it should fail
            SignetParams(challenge='522103ad5e0edad18cb1f0fc0d28a3d4f1f3e445640337489abb10404f2d1e086be430210359ef5021964fe22d6f8e05b2463c9540ce96883fe3b278760f048f5189f2e6c452ae')
        ]

        self.extra_args = [
            self.signets[0].shared_args, self.signets[0].shared_args,
            self.signets[1].shared_args, self.signets[1].shared_args,
            self.signets[2].shared_args, self.signets[2].shared_args,
        ]

    def setup_network(self):
        self.setup_nodes()

        # Setup the three signets, which are incompatible with each other
        self.connect_nodes(0, 1)
        self.connect_nodes(2, 3)
        self.connect_nodes(4, 5)

    def run_test(self):
        self.log.info("basic tests using OP_TRUE challenge")

        self.log.info('getblockchaininfo')
        def check_getblockchaininfo(node_idx, signet_idx):
            blockchain_info = self.nodes[node_idx].getblockchaininfo()
            assert_equal(blockchain_info['chain'], 'signet')
            assert_equal(blockchain_info['signet_challenge'], self.signets[signet_idx].challenge)
        check_getblockchaininfo(node_idx=1, signet_idx=0)
        check_getblockchaininfo(node_idx=2, signet_idx=1)
        check_getblockchaininfo(node_idx=5, signet_idx=2)

        self.log.info('getmininginfo')
        def check_getmininginfo(node_idx, signet_idx):
            mining_info = self.nodes[node_idx].getmininginfo()
            assert_equal(mining_info['blocks'], 0)
            assert_equal(mining_info['chain'], 'signet')
            assert 'currentblocktx' not in mining_info
            assert 'currentblockweight' not in mining_info
            assert_equal(mining_info['networkhashps'], Decimal('0'))
            assert_equal(mining_info['pooledtx'], 0)
            assert_equal(mining_info['signet_challenge'], self.signets[signet_idx].challenge)
        check_getmininginfo(node_idx=0, signet_idx=0)
        check_getmininginfo(node_idx=3, signet_idx=1)
        check_getmininginfo(node_idx=4, signet_idx=2)

        self.log.info("mining on the OP_TRUE signet, where the empty solution suffices")

        self.generate(self.nodes[0], 10, sync_fun=self.no_op)
        assert_equal(self.nodes[0].getblockcount(), 10)

        # The three signets share a genesis block -- only the challenge, and so
        # the derived magic, differ. Any of them will therefore accept this as
        # a well-formed child of its own tip, and the signet solution is the
        # only thing that can reject it. Submitting the one block to all three
        # leaves the challenge as the single variable.
        block = self.nodes[0].getblock(self.nodes[0].getblockhash(1), 0)

        self.log.info("accepted by a node whose challenge the solution satisfies")

        assert_equal(self.nodes[1].submitblock(block), None)
        assert_equal(self.nodes[1].getblockcount(), 1)

        self.log.info("rejected where the solution does not satisfy the challenge")

        assert_equal(self.nodes[4].submitblock(block), 'bad-signet-blksig')
        assert_equal(self.nodes[4].getblockcount(), 0)

        self.log.info("rejected by the placeholder default challenge, as every block is")

        # OP_RETURN cannot be satisfied by any solution, so the default signet
        # is inert by construction. Asserting it keeps that deliberate, rather
        # than something for whoever first tries to use it to discover.
        assert_equal(self.nodes[2].submitblock(block), 'bad-signet-blksig')
        assert_equal(self.nodes[2].getblockcount(), 0)

        self.log.info("test that signet logs the network magic on node start")
        with self.nodes[0].assert_debug_log(["Signet derived magic (message start)"]):
            self.restart_node(0)
        self.stop_node(0)
        self.nodes[0].assert_start_raises_init_error(extra_args=["-signetchallenge=abc"], expected_msg="Error: -signetchallenge must be hex, not 'abc'.")
        self.nodes[0].assert_start_raises_init_error(extra_args=["-signetchallenge=abc"] * 2, expected_msg="Error: -signetchallenge cannot be multiple values.")


if __name__ == '__main__':
    SignetBasicTest(__file__).main()
