#!/usr/bin/env python3
# Copyright (c) 2026 The TrueNorth developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the launch-time consensus rule.

Mainnet rejects blocks timestamped before consensus.nLaunchTime. That rule is
what stops anyone mining a head start from a release candidate or leaked
chainparams before the chain opens, so the fair-launch claim depends on it.

Every test chain disables the rule (nLaunchTime = 0), which left it with no
coverage at all. Regtest therefore exposes -testlaunchtime so the branch can
actually be exercised.
"""

from test_framework.blocktools import create_block, create_coinbase
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

# Seconds after the genesis timestamp at which the chain "opens".
LAUNCH_OFFSET = 1000


class LaunchTimeTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def build_block(self, node, ntime):
        """A solved block 1 stamped at ntime."""
        tip = int(node.getbestblockhash(), 16)
        block = create_block(tip, create_coinbase(1), ntime)
        block.solve()
        return block

    def run_test(self):
        node = self.nodes[0]

        genesis_time = node.getblock(node.getblockhash(0))["time"]
        launch_time = genesis_time + LAUNCH_OFFSET

        # The rule is off by default, as on every other test chain.
        self.log.info("Rule disabled by default: an early block is accepted")
        early = self.build_block(node, genesis_time + 1)
        assert_equal(node.submitblock(early.serialize().hex()), None)
        assert_equal(node.getblockcount(), 1)

        # Start over with the rule enforced.
        self.restart_node(0, extra_args=[f"-testlaunchtime={launch_time}"])
        node = self.nodes[0]
        node.invalidateblock(node.getbestblockhash())
        assert_equal(node.getblockcount(), 0)

        self.log.info("Blocks stamped before the launch time are rejected")
        too_early = self.build_block(node, launch_time - 1)
        assert_equal(node.submitblock(too_early.serialize().hex()), "block-before-launch")
        assert_equal(node.getblockcount(), 0)

        self.log.info("A block stamped exactly at the launch time is accepted")
        at_launch = self.build_block(node, launch_time)
        assert_equal(node.submitblock(at_launch.serialize().hex()), None)
        assert_equal(node.getblockcount(), 1)
        assert_equal(node.getblock(node.getbestblockhash())["time"], launch_time)


if __name__ == "__main__":
    LaunchTimeTest(__file__).main()
