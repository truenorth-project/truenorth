#!/usr/bin/env python3
# Copyright (c) 2023 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test wallet change address selection"""

import re

from test_framework.blocktools import COINBASE_MATURITY
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
)


class WalletChangeAddressTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        # discardfee is added before the change_pos test (see run_test) to
        # make change outputs less likely there. The change-index loop needs
        # every send to produce change; P2QRH change costs more to spend than
        # P2WPKH, so under -discardfee=1 some loop sends would drop change.
        self.extra_args = [
            [],
            [],
            ["-avoidpartialspends"]
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def assert_change_index(self, node, tx, index):
        change_index = None
        for vout in tx["vout"]:
            info = node.getaddressinfo(vout["scriptPubKey"]["address"])
            if (info["ismine"] and info["ischange"]):
                change_index = int(re.findall(r'\d+', info["hdkeypath"])[-1])
                break
        assert_equal(change_index, index)

    def assert_change_pos(self, wallet, tx, pos):
        change_pos = None
        for index, output in enumerate(tx["vout"]):
            info = wallet.getaddressinfo(output["scriptPubKey"]["address"])
            if (info["ismine"] and info["ischange"]):
                change_pos = index
                break
        assert_equal(change_pos, pos)

    def run_test(self):
        self.log.info("Setting up")
        # Mine some coins
        self.generate(self.nodes[0], COINBASE_MATURITY + 1)

        # Get some addresses from the two nodes
        addr1 = [self.nodes[1].getnewaddress() for _ in range(3)]
        addr2 = [self.nodes[2].getnewaddress() for _ in range(3)]
        addrs = addr1 + addr2

        # Send 1 + 0.5 coin to each address
        [self.nodes[0].sendtoaddress(addr, 1.0) for addr in addrs]
        [self.nodes[0].sendtoaddress(addr, 0.5) for addr in addrs]
        self.generate(self.nodes[0], 1)

        for i in range(20):
            for n in [1, 2]:
                self.log.debug(f"Send transaction from node {n}: expected change index {i}")
                txid = self.nodes[n].sendtoaddress(self.nodes[0].getnewaddress(), 0.2)
                tx = self.nodes[n].getrawtransaction(txid, True)
                # find the change output and ensure that expected change index was used
                self.assert_change_index(self.nodes[n], tx, i)

        # Start next test with fresh wallets and new coins. Confirm the loop's
        # sends first so restarted nodes don't reload them into their mempools.
        self.generate(self.nodes[0], 1)
        # -nowallet: this section only uses the fresh w1/w2 wallets. Loading the
        # default wallet at startup would print the high -discardfee warning
        # to stderr, which the framework treats as a failure.
        self.restart_node(1, extra_args=["-discardfee=1", "-nowallet"])
        self.restart_node(2, extra_args=["-avoidpartialspends", "-discardfee=1", "-nowallet"])
        self.connect_nodes(0, 1)
        self.connect_nodes(0, 2)
        self.nodes[1].createwallet("w1")
        self.nodes[2].createwallet("w2")
        w1 = self.nodes[1].get_wallet_rpc("w1")
        w2 = self.nodes[2].get_wallet_rpc("w2")
        addr1 = w1.getnewaddress()
        addr2 = w2.getnewaddress()
        self.nodes[0].sendtoaddress(addr1, 3.0)
        self.nodes[0].sendtoaddress(addr1, 0.1)
        self.nodes[0].sendtoaddress(addr2, 3.0)
        self.nodes[0].sendtoaddress(addr2, 0.1)
        self.generate(self.nodes[0], 1)

        sendTo1 = self.nodes[0].getnewaddress()
        sendTo2 = self.nodes[0].getnewaddress()
        sendTo3 = self.nodes[0].getnewaddress()

        # The avoid partial spends wallet will always create a change output
        node = self.nodes[2]
        # Change pinned to bech32: under -discardfee=1 the larger P2QRH change
        # would be dropped to fees here (exceeding -maxtxfee), and this test
        # is about change position, not change type.
        res = w2.send({sendTo1: "1.0", sendTo2: "1.0", sendTo3: "0.9999"}, options={"change_position": 0, "change_type": "bech32"})
        tx = node.getrawtransaction(res["txid"], True)
        self.assert_change_pos(w2, tx, 0)

        # The default wallet will internally create a tx without change first,
        # then create a second candidate using APS that requires a change output.
        # Ensure that the user-configured change position is kept
        node = self.nodes[1]
        res = w1.send({sendTo1: "1.0", sendTo2: "1.0", sendTo3: "0.9999"}, options={"change_position": 0, "change_type": "bech32"})
        tx = node.getrawtransaction(res["txid"], True)
        # If the wallet ignores the user's change_position there is still a 25%
        # that the random change position passes the test
        self.assert_change_pos(w1, tx, 0)

if __name__ == '__main__':
    WalletChangeAddressTest(__file__).main()
