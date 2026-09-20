#!/usr/bin/env python3
# Copyright (c) 2019-2022 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the generation of UTXO snapshots using `dumptxoutset`.
"""

from test_framework.blocktools import COINBASE_MATURITY
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    sha256sum_file,
)


class DumptxoutsetTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def check_expected_network(self, node, active):
        rev_file = node.blocks_path / "rev00000.dat"
        bogus_file = node.blocks_path / "bogus.dat"
        rev_file.rename(bogus_file)
        assert_raises_rpc_error(
            -1, 'Could not roll back to requested height.', node.dumptxoutset, 'utxos.dat', rollback=99)
        assert_equal(node.getnetworkinfo()['networkactive'], active)

        # Cleanup
        bogus_file.rename(rev_file)

    def run_test(self):
        """Test a trivial usage of the dumptxoutset RPC command."""
        node = self.nodes[0]
        mocktime = node.getblockheader(node.getblockhash(0))['time'] + 1
        node.setmocktime(mocktime)
        self.generate(node, COINBASE_MATURITY)

        FILENAME = 'txoutset.dat'
        out = node.dumptxoutset(FILENAME, "latest")
        expected_path = node.chain_path / FILENAME

        assert expected_path.is_file()

        assert_equal(out['coins_written'], 100)
        assert_equal(out['base_height'], 100)
        assert_equal(out['path'], str(expected_path))
        # Blockhash should be deterministic based on mocked time.
        assert_equal(
            out['base_hash'],
            '7ee7c623e8a1e0fdd0b385765f3cd596390823ef02f852b89ebc3501df7a0a4f')

        # UTXO snapshot hash should be deterministic based on mocked time.
        assert_equal(
            sha256sum_file(str(expected_path)).hex(),
            '376b5260894bfd2c698c4aced864c911c0fe234c5ee7f826bcadc4cb6e4324dd')

        assert_equal(
            out['txoutset_hash'], '25de38aa02621bf26972320ddb1ade2b5650e55fbea2bf69983c3d2c144904d6')
        assert_equal(out['nchaintx'], 101)

        # Specifying a path to an existing or invalid file will fail.
        assert_raises_rpc_error(
            -8, '{} already exists'.format(FILENAME),  node.dumptxoutset, FILENAME, "latest")
        invalid_path = node.datadir_path / "invalid" / "path"
        assert_raises_rpc_error(
            -8, "Couldn't open file {}.incomplete for writing".format(invalid_path), node.dumptxoutset, invalid_path, "latest")

        self.log.info("Test that dumptxoutset with unknown dump type fails")
        assert_raises_rpc_error(
            -8, 'Invalid snapshot type "bogus" specified. Please specify "rollback" or "latest"', node.dumptxoutset, 'utxos.dat', "bogus")

        self.log.info("Test that dumptxoutset failure does not leave the network activity suspended when it was on previously")
        self.check_expected_network(node, True)

        self.log.info("Test that dumptxoutset failure leaves the network activity suspended when it was off")
        node.setnetworkactive(False)
        self.check_expected_network(node, False)
        node.setnetworkactive(True)


if __name__ == '__main__':
    DumptxoutsetTest(__file__).main()
