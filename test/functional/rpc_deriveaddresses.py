#!/usr/bin/env python3
# Copyright (c) 2018-2022 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the deriveaddresses rpc call."""
from test_framework.test_framework import BitcoinTestFramework
from test_framework.descriptors import descsum_create
from test_framework.util import assert_equal, assert_raises_rpc_error

class DeriveaddressesTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1

    def run_test(self):
        assert_raises_rpc_error(-5, "Missing checksum", self.nodes[0].deriveaddresses, "a")

        descriptor = "wpkh(tpruZLtDPZhDAKMtwBfMwezyrT6Bhka2nzQRyoaBJBmU82Xm9M6anvmNG2ersDwoUytjv2RNDrkAo6NR6s6pXpH1ttjpDbnm8o1EZKzXFedzDsu/1/1/0)#jx4rn2du"
        address = "rnorth1qjqmxmkpmxt80xz4y3746zgt0q3u3ferrcd46zl"
        assert_equal(self.nodes[0].deriveaddresses(descriptor), [address])

        descriptor = descriptor[:-9]
        assert_raises_rpc_error(-5, "Missing checksum", self.nodes[0].deriveaddresses, descriptor)

        descriptor_pubkey = "wpkh(tpuadsaFXyjTJh3Zpeh9qJfaFrkJGn5xxKbLZ7AxahomYJL9yqMMRKaxSXGj3LfYkm27j4aGgJr8ftuyd91EuHv7oaAmFckJLcTieVRvCtsgyGw/1/1/0)#e53n0ta2"
        address = "rnorth1qjqmxmkpmxt80xz4y3746zgt0q3u3ferrcd46zl"
        assert_equal(self.nodes[0].deriveaddresses(descriptor_pubkey), [address])

        ranged_descriptor = "wpkh(tpruZLtDPZhDAKMtwBfMwezyrT6Bhka2nzQRyoaBJBmU82Xm9M6anvmNG2ersDwoUytjv2RNDrkAo6NR6s6pXpH1ttjpDbnm8o1EZKzXFedzDsu/1/1/*)#04sswftd"
        assert_equal(self.nodes[0].deriveaddresses(ranged_descriptor, [1, 2]), ["rnorth1qhku5rq7jz8ulufe2y6fkcpnlvpsta7rqudagz0", "rnorth1qpgptk2gvshyl0s9lqshsmx932l9ccsv2nvrwjt"])
        assert_equal(self.nodes[0].deriveaddresses(ranged_descriptor, 2), [address, "rnorth1qhku5rq7jz8ulufe2y6fkcpnlvpsta7rqudagz0", "rnorth1qpgptk2gvshyl0s9lqshsmx932l9ccsv2nvrwjt"])

        ranged_descriptor = descsum_create("wpkh(tpruZLtDPZhDAKMtwBfMwezyrT6Bhka2nzQRyoaBJBmU82Xm9M6anvmNG2ersDwoUytjv2RNDrkAo6NR6s6pXpH1ttjpDbnm8o1EZKzXFedzDsu/1/<0;1>/*)")
        assert_equal(self.nodes[0].deriveaddresses(ranged_descriptor, [1, 2]), [["rnorth1q7c8mdmdktrzs8xgpjmqw90tjn65j5a3ymhae7c", "rnorth1qs6n37uzu0v0qfzf0r0csm0dwa7prc0v549y2ty"], ["rnorth1qhku5rq7jz8ulufe2y6fkcpnlvpsta7rqudagz0", "rnorth1qpgptk2gvshyl0s9lqshsmx932l9ccsv2nvrwjt"]])

        assert_raises_rpc_error(-8, "Range should not be specified for an un-ranged descriptor", self.nodes[0].deriveaddresses, descsum_create("wpkh(tpruZLtDPZhDAKMtwBfMwezyrT6Bhka2nzQRyoaBJBmU82Xm9M6anvmNG2ersDwoUytjv2RNDrkAo6NR6s6pXpH1ttjpDbnm8o1EZKzXFedzDsu/1/1/0)"), [0, 2])

        assert_raises_rpc_error(-8, "Range must be specified for a ranged descriptor", self.nodes[0].deriveaddresses, descsum_create("wpkh(tpruZLtDPZhDAKMtwBfMwezyrT6Bhka2nzQRyoaBJBmU82Xm9M6anvmNG2ersDwoUytjv2RNDrkAo6NR6s6pXpH1ttjpDbnm8o1EZKzXFedzDsu/1/1/*)"))

        assert_raises_rpc_error(-8, "End of range is too high", self.nodes[0].deriveaddresses, descsum_create("wpkh(tpruZLtDPZhDAKMtwBfMwezyrT6Bhka2nzQRyoaBJBmU82Xm9M6anvmNG2ersDwoUytjv2RNDrkAo6NR6s6pXpH1ttjpDbnm8o1EZKzXFedzDsu/1/1/*)"), 10000000000)

        assert_raises_rpc_error(-8, "Range is too large", self.nodes[0].deriveaddresses, descsum_create("wpkh(tpruZLtDPZhDAKMtwBfMwezyrT6Bhka2nzQRyoaBJBmU82Xm9M6anvmNG2ersDwoUytjv2RNDrkAo6NR6s6pXpH1ttjpDbnm8o1EZKzXFedzDsu/1/1/*)"), [1000000000, 2000000000])

        assert_raises_rpc_error(-8, "Range specified as [begin,end] must not have begin after end", self.nodes[0].deriveaddresses, descsum_create("wpkh(tpruZLtDPZhDAKMtwBfMwezyrT6Bhka2nzQRyoaBJBmU82Xm9M6anvmNG2ersDwoUytjv2RNDrkAo6NR6s6pXpH1ttjpDbnm8o1EZKzXFedzDsu/1/1/*)"), [2, 0])

        assert_raises_rpc_error(-8, "Range should be greater or equal than 0", self.nodes[0].deriveaddresses, descsum_create("wpkh(tpruZLtDPZhDAKMtwBfMwezyrT6Bhka2nzQRyoaBJBmU82Xm9M6anvmNG2ersDwoUytjv2RNDrkAo6NR6s6pXpH1ttjpDbnm8o1EZKzXFedzDsu/1/1/*)"), [-1, 0])

        combo_descriptor = descsum_create("combo(tpruZLtDPZhDAKMtwBfMwezyrT6Bhka2nzQRyoaBJBmU82Xm9M6anvmNG2ersDwoUytjv2RNDrkAo6NR6s6pXpH1ttjpDbnm8o1EZKzXFedzDsu/1/1/0)")
        assert_equal(self.nodes[0].deriveaddresses(combo_descriptor), ["rKP7dfs7xKAXzHhP6YgJnexAuMe4cVRNVr", address, "rrFD4LunQfUYyLRg2h51qser6v8wVWx13Z"])

        # P2PK does not have a valid address
        assert_raises_rpc_error(-5, "Descriptor does not have a corresponding address", self.nodes[0].deriveaddresses, descsum_create("pk(tpruZLtDPZhDAKMtwBfMwezyrT6Bhka2nzQRyoaBJBmU82Xm9M6anvmNG2ersDwoUytjv2RNDrkAo6NR6s6pXpH1ttjpDbnm8o1EZKzXFedzDsu)"))

        # Before #26275, bitcoind would crash when deriveaddresses was
        # called with derivation index 2147483647, which is the maximum
        # positive value of a signed int32, and - currently - the
        # maximum value that the deriveaddresses bitcoin RPC call
        # accepts as derivation index.
        assert_equal(self.nodes[0].deriveaddresses(descsum_create("wpkh(tpruZLtDPZhDAKMtwBfMwezyrT6Bhka2nzQRyoaBJBmU82Xm9M6anvmNG2ersDwoUytjv2RNDrkAo6NR6s6pXpH1ttjpDbnm8o1EZKzXFedzDsu/1/1/*)"), [2147483647, 2147483647]), ["rnorth1qtzs23vgzpreks5gtygwxf8tv5rldxvvsd3f0er"])

        hardened_without_privkey_descriptor = descsum_create("wpkh(tpuadsaFXyjTJh3Zpeh9qJfaFrkJGn5xxKbLZ7AxahomYJL9yqMMRKaxSXGj3LfYkm27j4aGgJr8ftuyd91EuHv7oaAmFckJLcTieVRvCtsgyGw/1'/1/0)")
        assert_raises_rpc_error(-5, "Cannot derive script without private keys", self.nodes[0].deriveaddresses, hardened_without_privkey_descriptor)

        bare_multisig_descriptor = descsum_create("multi(1,tpuadsaFXyjTJh3Zpeh9qJfaFrkJGn5xxKbLZ7AxahomYJL9yqMMRKaxSXGj3LfYkm27j4aGgJr8ftuyd91EuHv7oaAmFckJLcTieVRvCtsgyGw/1/1/0,tpuadsaFXyjTJh3Zpeh9qJfaFrkJGn5xxKbLZ7AxahomYJL9yqMMRKaxSXGj3LfYkm27j4aGgJr8ftuyd91EuHv7oaAmFckJLcTieVRvCtsgyGw/1/1/1)")
        assert_raises_rpc_error(-5, "Descriptor does not have a corresponding address", self.nodes[0].deriveaddresses, bare_multisig_descriptor)

if __name__ == '__main__':
    DeriveaddressesTest(__file__).main()
