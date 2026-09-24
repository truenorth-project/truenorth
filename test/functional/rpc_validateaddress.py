#!/usr/bin/env python3
# Copyright (c) 2023 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test validateaddress for main chain"""

from test_framework.test_framework import BitcoinTestFramework

from test_framework.util import assert_equal

INVALID_DATA = [
    # Bitcoin addresses: rejected outright, they are not TrueNorth's hrp.
    ('tc1qw508d6qejxtdg4y5r3zarvary0c5xw7kg3g4ty', 'Invalid or unsupported Segwit (Bech32) or Base58 encoding.', []),
    ('tb1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3q0sL5k7', 'Invalid or unsupported Segwit (Bech32) or Base58 encoding.', []),
    ('tb1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3pjxtptv', 'Invalid or unsupported Segwit (Bech32) or Base58 encoding.', []),
    ('tc1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vq5zuyut', 'Invalid or unsupported Segwit (Bech32) or Base58 encoding.', []),
    ('tb1z0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vqglt7rf', 'Invalid or unsupported Segwit (Bech32) or Base58 encoding.', []),
    ('tb1q0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vq24jc47', 'Invalid or unsupported Segwit (Bech32) or Base58 encoding.', []),
    ('tb1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vq47Zagq', 'Invalid or unsupported Segwit (Bech32) or Base58 encoding.', []),
    ('tb1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vpggkg4j', 'Invalid or unsupported Segwit (Bech32) or Base58 encoding.', []),
    # Constructed under TrueNorth's 'north' hrp so each reaches the error it
    # is meant to exercise instead of short-circuiting on unknown hrp.
    ('north1pqqqsyqcyq5rqwzqfpg9scrgwpugpzysnzs23v9ccrydpk8qarc0sjzw8v4', 'Version 1+ witness address must use Bech32m checksum', []),
    ('north1zqqqsyqcyq5rqwzqfpg9scrgwpugpzysnjv8t0j', 'Version 1+ witness address must use Bech32m checksum', []),
    ('north1sqqqsyqcyq5rqwzqfpg9scrgwpugpzysns43f6s', 'Version 1+ witness address must use Bech32m checksum', []),
    ('north1qqqqsyqcyq5rqwzqfpg9scrgwpugpzysn39s3sx', 'Version 0 witness address must use Bech32 checksum', []),
    ('north1qqqqsyqcyq5rqwzqfpg9scrgwpugn86v5', 'Invalid Bech32 v0 address program size (16 bytes), per BIP141', []),
    ('north1pqqmsq07h', 'Invalid Bech32 address program size (1 byte)', []),
    ('north1pqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqcupvf7', 'Invalid Bech32 address program size (41 bytes)', []),
    ('north13qqqsyqcyq5rqwzqfpg9scrgwpugpzysnwhkwje', 'Invalid Bech32 address witness version', []),
    ('north1zdpvs8', 'Empty Bech32 data section', []),
    ('north1qqpqsyqcyq5rqwzqfpg9scrgwpugpzysnyeqa4y', 'Invalid Bech32 checksum', [8]),
    ('NORTH1QQQQsYQCYQ5RQWZQFPG9SCRGWPUGPZYSNYEQA4Y', 'Invalid character or mixed case', [10]),
    ('north1qqbqsyqcyq5rqwzqfpg9scrgwpugpzysnyeqa4y', 'Invalid Base 32 character', [8]),
    ('north1pqpxx56r9', 'Invalid padding in Bech32 data section', []),
]

VALID_DATA = [
    ('north1qw508d6qejxtdg4y5r3zarvary0c5xw7kajxat9', '0014751e76e8199196d454941c45d1b3a323f1433bd6'),
    ('north1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3qhjutky', '00201863143c14c5166804bd19203356da136c985678cd4d27a1b8c6329604903262'),
    ('north1pw508d6qejxtdg4y5r3zarvary0c5xw7kw508d6qejxtdg4y5r3zarvary0c5xw7k45u8dn', '5128751e76e8199196d454941c45d1b3a323f1433bd6751e76e8199196d454941c45d1b3a323f1433bd6'),
    ('north1sw50q5k5tet', '6002751e'),
    ('north1zw508d6qejxtdg4y5r3zarvaryvrtja9n', '5210751e76e8199196d454941c45d1b3a323'),
    ('north1qqqqqp399et2xygdj5xreqhjjvcmzhxw4aywxecjdzew6hylgvsesmy59h7', '0020000000c4a5cad46221b2a187905e5266362b99d5e91c6ce24d165dab93e86433'),
    ('north1pqqqqp399et2xygdj5xreqhjjvcmzhxw4aywxecjdzew6hylgvses3n5v0z', '5120000000c4a5cad46221b2a187905e5266362b99d5e91c6ce24d165dab93e86433'),
    ('north1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vqdupzg6', '512079be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798'),
    ('north1pfeesvct09s', '51024e73'),
]


class ValidateAddressMainTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.chain = ""  # main
        self.num_nodes = 1
        self.extra_args = [["-prune=1500"]] * self.num_nodes

    def check_valid(self, addr, spk):
        info = self.nodes[0].validateaddress(addr)
        assert_equal(info["isvalid"], True)
        assert_equal(info["scriptPubKey"], spk)
        assert "error" not in info
        assert "error_locations" not in info

    def check_invalid(self, addr, error_str, error_locations):
        res = self.nodes[0].validateaddress(addr)
        assert_equal(res["isvalid"], False)
        assert_equal(res["error"], error_str)
        assert_equal(res["error_locations"], error_locations)

    def test_validateaddress(self):
        for (addr, error, locs) in INVALID_DATA:
            self.check_invalid(addr, error, locs)
        for (addr, spk) in VALID_DATA:
            self.check_valid(addr, spk)

    def run_test(self):
        self.test_validateaddress()


if __name__ == "__main__":
    ValidateAddressMainTest(__file__).main()
