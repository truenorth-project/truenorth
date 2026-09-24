#!/usr/bin/env python3
# Copyright (c) 2025 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test mining on an alternate mainnet

Test mining related RPCs that involve difficulty adjustment, which
regtest doesn't have.

It uses an alternate mainnet chain. See data/README.md for how it was generated.

Mine one retarget period worth of blocks with a short interval in
order to maximally raise the difficulty. Verify this using the getmininginfo RPC.

"""

from test_framework.test_framework import BitcoinTestFramework, SkipTest
from test_framework.util import (
    assert_equal,
)
from test_framework.blocktools import (
    DIFF_1_N_BITS,
    DIFF_1_TARGET,
    DIFF_4_N_BITS,
    DIFF_4_TARGET,
    create_coinbase,
    nbits_str,
    target_str
)

from test_framework.messages import (
    CBlock,
    SEQUENCE_FINAL,
)

import json
import os

# See data/README.md
COINBASE_SCRIPT_PUBKEY="76a914eadbac7f36c37e39361168b7aaee3cb24a25312d88ac"

class MiningMainnetTest(BitcoinTestFramework):

    def skip_test_if_missing_module(self):
        # Not runnable on TrueNorth, for two independent reasons.
        #
        # 1. It asserts Bitcoin's retarget epoch: n_blocks == 2016, difficulty
        #    exactly 1 at height 2015 and exactly 4 at 2016, and
        #    mining_info['next']['height'] == 2016. TrueNorth uses LWMA, which
        #    retargets every block over a 90-block window;
        #    DifficultyAdjustmentInterval() is 90 and there is no epoch
        #    boundary for any of this to be true at.
        #
        # 2. data/mainnet_alt.json holds 2016 pre-generated blocks whose nonces
        #    satisfy SHA256d. Under RandomX they are worthless, and
        #    regenerating them is not possible: ~960k hashes per block at the
        #    launch difficulty is roughly 4.8 hours each.
        #
        # The RPC surface it covers -- getmininginfo's difficulty/bits/target
        # and the 'next' projection -- is still worth testing, but against LWMA
        # on a cheap retargeting chain such as testnet4. That is a new test,
        # not a repair of this one. See pending-tasks.md.
        raise SkipTest("assumes Bitcoin's 2016-block retarget epoch and SHA256d-mined fixture blocks; TrueNorth uses LWMA")

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.chain = "" # main

    def add_options(self, parser):
        parser.add_argument(
            '--datafile',
            default='data/mainnet_alt.json',
            help='Block data file (default: %(default)s)',
        )

    def mine(self, height, prev_hash, blocks, node):
        self.log.debug(f"height={height}")
        block = CBlock()
        block.nVersion = 0x20000000
        block.hashPrevBlock = int(prev_hash, 16)
        block.nTime = blocks['timestamps'][height - 1]
        block.nBits = DIFF_1_N_BITS if height < 2016 else DIFF_4_N_BITS
        block.nNonce = blocks['nonces'][height - 1]
        block.vtx = [create_coinbase(height=height, script_pubkey=bytes.fromhex(COINBASE_SCRIPT_PUBKEY), halving_period=210000)]
        # The alternate mainnet chain was mined with non-timelocked coinbase txs.
        block.vtx[0].nLockTime = 0
        block.vtx[0].vin[0].nSequence = SEQUENCE_FINAL
        block.hashMerkleRoot = block.calc_merkle_root()
        block_hex = block.serialize(with_witness=False).hex()
        self.log.debug(block_hex)
        assert_equal(node.submitblock(block_hex), None)
        prev_hash = node.getbestblockhash()
        assert_equal(prev_hash, block.hash_hex)
        return prev_hash


    def run_test(self):
        node = self.nodes[0]
        # Clear disk space warning
        node.stderr.seek(0)
        node.stderr.truncate()
        self.log.info("Load alternative mainnet blocks")
        path = os.path.join(os.path.dirname(os.path.realpath(__file__)), self.options.datafile)
        prev_hash = node.getbestblockhash()
        blocks = None
        with open(path, encoding='utf-8') as f:
            blocks = json.load(f)
            n_blocks = len(blocks['timestamps'])
            assert_equal(n_blocks, 2016)

        # Mine up to the last block of the first retarget period
        for i in range(2015):
            prev_hash = self.mine(i + 1, prev_hash, blocks, node)

        assert_equal(node.getblockcount(), 2015)

        self.log.info("Check difficulty adjustment with getmininginfo")
        mining_info = node.getmininginfo()
        assert_equal(mining_info['difficulty'], 1)
        assert_equal(mining_info['bits'], nbits_str(DIFF_1_N_BITS))
        assert_equal(mining_info['target'], target_str(DIFF_1_TARGET))

        assert_equal(mining_info['next']['height'], 2016)
        assert_equal(mining_info['next']['difficulty'], 4)
        assert_equal(mining_info['next']['bits'], nbits_str(DIFF_4_N_BITS))
        assert_equal(mining_info['next']['target'], target_str(DIFF_4_TARGET))

        # Mine first block of the second retarget period
        height = 2016
        prev_hash = self.mine(height, prev_hash, blocks, node)
        assert_equal(node.getblockcount(), height)

        mining_info = node.getmininginfo()
        assert_equal(mining_info['difficulty'], 4)

        self.log.info("getblock RPC should show historical target")
        block_info = node.getblock(node.getblockhash(1))

        assert_equal(block_info['difficulty'], 1)
        assert_equal(block_info['bits'], nbits_str(DIFF_1_N_BITS))
        assert_equal(block_info['target'], target_str(DIFF_1_TARGET))


if __name__ == '__main__':
    MiningMainnetTest(__file__).main()
