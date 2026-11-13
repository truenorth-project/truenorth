#!/usr/bin/env python3
# Copyright (c) 2026 The TrueNorth developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
'''
Generate P2QRH wallet test vectors (src/test/data/p2qrh_wallet_vectors.json).

Everything here is computed with the functional test framework's own
secp256k1, BIP340, bech32m and BIP341 sighash code, not with truenorthd.
src/test/p2qrh_vector_tests.cpp checks every value against TrueNorth Core,
so the two implementations have to agree. See doc/p2qrh.md.

Usage: gen_p2qrh_vectors.py > src/test/data/p2qrh_wallet_vectors.json
'''

import hashlib
import hmac
import json
import os
import sys

sys.path.append(os.path.join(os.path.dirname(__file__), '../../test/functional'))

from test_framework.crypto import secp256k1  # noqa: E402
from test_framework.descriptors import descsum_create  # noqa: E402
from test_framework.key import compute_xonly_pubkey, sign_schnorr, verify_schnorr  # noqa: E402
from test_framework.messages import COutPoint, CTransaction, CTxIn, CTxInWitness, CTxOut, sha256  # noqa: E402
from test_framework.script import (  # noqa: E402
    BIP341_sha_amounts,
    BIP341_sha_outputs,
    BIP341_sha_prevouts,
    BIP341_sha_scriptpubkeys,
    BIP341_sha_sequences,
    CScript,
    OP_0,
    OP_2,
    TaprootSignatureHash,
    TaprootSignatureMsg,
    hash160,
)
from test_framework.segwit_addr import Encoding, bech32_encode, convertbits, encode_segwit_address  # noqa: E402

QRH_DOMAIN_TAG = b"TrueNorth/QRH/v1"
QRH_SCHEME_SCHNORR = 0x01
QRH_EMPTY_SCRIPT_ROOT = bytes(32)
AUX_RAND = bytes(32)  # what TrueNorth Core's signer uses

HRPS = {"main": "north", "test": "tnorth", "testnet4": "tnorth4", "signet": "snorth", "regtest": "rnorth"}

# BIP32 version bytes and the qrh() derivation path, per chain.
CHAINS = {
    "main": {"xprv": bytes.fromhex("7E5C6544"), "xpub": bytes.fromhex("7E5D5A83"), "coin_type": 1867},
    "testnet4": {"xprv": bytes.fromhex("04358392"), "xpub": bytes.fromhex("043587CD"), "coin_type": 1},
}
QRH_PURPOSE = 88
HARDENED = 0x80000000

# The standard BIP39 test mnemonic, as used by the BIP84 and BIP86 vectors.
MNEMONIC = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about"
B58_ALPHABET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"


def qrh_commitment(scheme_id, pubkey, script_root):
    return sha256(QRH_DOMAIN_TAG + bytes([scheme_id]) + pubkey + script_root)


def qrh_script(commitment):
    return CScript([OP_2, commitment])


def b58check(payload):
    data = payload + hashlib.sha256(hashlib.sha256(payload).digest()).digest()[:4]
    n = int.from_bytes(data, "big")
    out = ""
    while n:
        n, r = divmod(n, 58)
        out = B58_ALPHABET[r] + out
    return "1" * (len(data) - len(data.lstrip(b"\0"))) + out


def compressed_pubkey(priv):
    return (int.from_bytes(priv, "big") * secp256k1.G).to_bytes_compressed()


class ExtKey:
    def __init__(self, priv, chaincode, depth=0, fingerprint=bytes(4), child=0):
        self.priv, self.chaincode, self.depth, self.fingerprint, self.child = priv, chaincode, depth, fingerprint, child

    @staticmethod
    def from_seed(seed):
        i = hmac.new(b"Bitcoin seed", seed, hashlib.sha512).digest()
        return ExtKey(i[:32], i[32:])

    def derive(self, index):
        if index & HARDENED:
            data = b"\0" + self.priv + index.to_bytes(4, "big")
        else:
            data = compressed_pubkey(self.priv) + index.to_bytes(4, "big")
        i = hmac.new(self.chaincode, data, hashlib.sha512).digest()
        tweak = int.from_bytes(i[:32], "big")
        assert tweak < secp256k1.GE.ORDER
        child = (tweak + int.from_bytes(self.priv, "big")) % secp256k1.GE.ORDER
        assert child != 0
        return ExtKey(child.to_bytes(32, "big"), i[32:], self.depth + 1,
                      hash160(compressed_pubkey(self.priv))[:4], index)

    def derive_path(self, path):
        key = self
        for index in path:
            key = key.derive(index)
        return key

    def _serialize(self, version, key_data):
        return b58check(version + bytes([self.depth]) + self.fingerprint + self.child.to_bytes(4, "big") +
                        self.chaincode + key_data)

    def xprv(self, chain):
        return self._serialize(CHAINS[chain]["xprv"], b"\0" + self.priv)

    def xpub(self, chain):
        return self._serialize(CHAINS[chain]["xpub"], compressed_pubkey(self.priv))


def path_str(path):
    return "m" + "".join(f"/{i & ~HARDENED}h" if i & HARDENED else f"/{i}" for i in path)


def qrh_output(priv):
    pubkey, negated = compute_xonly_pubkey(priv)
    commitment = qrh_commitment(QRH_SCHEME_SCHNORR, pubkey, QRH_EMPTY_SCRIPT_ROOT)
    return pubkey, negated, commitment, qrh_script(commitment)


def address_vectors():
    """Commitment, scriptPubKey and address for raw x-only keys."""
    vectors = []
    privs = [(1).to_bytes(32, "big"), (3).to_bytes(32, "big"),
             bytes.fromhex("b7e151628aed2a6abf7158809cf4f3c762e7160f38b4da56a784d9045190cfef")]
    for priv in privs:
        pubkey, negated, commitment, spk = qrh_output(priv)
        vectors.append({
            "given": {"schemeId": QRH_SCHEME_SCHNORR, "pubkey": pubkey.hex(), "scriptRoot": QRH_EMPTY_SCRIPT_ROOT.hex()},
            "intermediary": {
                "commitmentPreimage": (QRH_DOMAIN_TAG + bytes([QRH_SCHEME_SCHNORR]) + pubkey + QRH_EMPTY_SCRIPT_ROOT).hex(),
                "commitment": commitment.hex(),
            },
            "expected": {
                "scriptPubKey": spk.hex(),
                "addresses": {chain: encode_segwit_address(hrp, 2, commitment) for chain, hrp in HRPS.items()},
            },
            "comment": f"x-only key of private key {int.from_bytes(priv, 'big') if int.from_bytes(priv, 'big') < 16 else priv.hex()}"
                       + ("; its full public key has odd Y" if negated else ""),
        })
    return vectors


def invalid_address_vectors():
    commitment = qrh_output((1).to_bytes(32, "big"))[2]
    data = [2] + convertbits(commitment, 8, 5)
    short = [2] + convertbits(commitment[:20], 8, 5)
    return [
        {"address": bech32_encode(Encoding.BECH32, "north", data), "chain": "main",
         "comment": "witness v2 with a bech32 (BIP173) checksum; v1+ requires bech32m"},
        {"address": encode_segwit_address("tnorth4", 2, commitment), "chain": "main",
         "comment": "valid testnet4 P2QRH address, so invalid on mainnet"},
        {"address": encode_segwit_address("north", 2, commitment).upper()[:10] + encode_segwit_address("north", 2, commitment)[10:],
         "chain": "main", "comment": "mixed case"},
        {"address": encode_segwit_address("north", 2, commitment)[:-1] + ("q" if encode_segwit_address("north", 2, commitment)[-1] != "q" else "p"),
         "chain": "main", "comment": "bad checksum"},
        {"address": bech32_encode(Encoding.BECH32M, "north", short), "chain": "main", "valid_but_not_p2qrh": True,
         "comment": "witness v2 with a 20-byte program decodes as an unknown witness program, NOT P2QRH; "
                    "a wallet must not treat it as P2QRH or send to it as one"},
    ]


def derivation_vectors():
    seed = hashlib.pbkdf2_hmac("sha512", MNEMONIC.encode(), b"mnemonic", 2048)
    master = ExtKey.from_seed(seed)
    master_fp = hash160(compressed_pubkey(master.priv))[:4]
    out = {"mnemonic": MNEMONIC, "passphrase": "", "seed": seed.hex(), "chains": []}
    for chain in ("main", "testnet4"):
        account_path = [QRH_PURPOSE | HARDENED, CHAINS[chain]["coin_type"] | HARDENED, 0 | HARDENED]
        account = master.derive_path(account_path)
        origin = master_fp.hex() + path_str(account_path)[1:]
        entry = {
            "chain": chain,
            "rootXprv": master.xprv(chain),
            "accountPath": path_str(account_path),
            "accountXprv": account.xprv(chain),
            "accountXpub": account.xpub(chain),
            "receiveDescriptor": descsum_create(f"qrh([{origin}]{account.xpub(chain)}/0/*)"),
            "changeDescriptor": descsum_create(f"qrh([{origin}]{account.xpub(chain)}/1/*)"),
            "keys": [],
        }
        for branch, indexes in ((0, (0, 1, 2)), (1, (0,))):
            for index in indexes:
                path = account_path + [branch, index]
                key = master.derive_path(path)
                pubkey, negated, commitment, spk = qrh_output(key.priv)
                entry["keys"].append({
                    "path": path_str(path),
                    "privkey": key.priv.hex(),
                    "xonlyPubkey": pubkey.hex(),
                    "fullPubkeyHasOddY": negated,
                    "commitment": commitment.hex(),
                    "scriptPubKey": spk.hex(),
                    "address": encode_segwit_address(HRPS[chain], 2, commitment),
                })
        out["chains"].append(entry)
    return out


def make_tx(inputs, outputs, locktime=0):
    tx = CTransaction()
    tx.version = 2
    tx.nLockTime = locktime
    tx.vin = [CTxIn(COutPoint(int(txid, 16), n), nSequence=seq) for txid, n, seq in inputs]
    tx.vout = [CTxOut(amount, spk) for amount, spk in outputs]
    return tx


def sign_input(tx, utxos, index, priv, hash_type):
    msg = TaprootSignatureMsg(tx, utxos, hash_type, index)
    sighash = TaprootSignatureHash(tx, utxos, hash_type, index)
    sig = sign_schnorr(priv, sighash, AUX_RAND)
    pubkey = compute_xonly_pubkey(priv)[0]
    assert verify_schnorr(pubkey, sig, sighash)
    if hash_type != 0:
        sig += bytes([hash_type])
    return msg, sighash, [sig, pubkey, bytes([QRH_SCHEME_SCHNORR])]


def spending_vector(comment, keys, utxo_amounts, hash_types, outputs, inputs, locktime=0):
    utxos = [CTxOut(amount, qrh_output(priv)[3]) for priv, amount in zip(keys, utxo_amounts)]
    tx = make_tx(inputs, outputs, locktime)
    unsigned_hex = tx.serialize_without_witness().hex()
    vector = {
        "comment": comment,
        "given": {
            "rawUnsignedTx": unsigned_hex,
            "utxosSpent": [{"scriptPubKey": u.scriptPubKey.hex(), "amount": u.nValue} for u in utxos],
        },
        "intermediary": {
            "hashAmounts": BIP341_sha_amounts(utxos).hex(),
            "hashOutputs": BIP341_sha_outputs(tx).hex(),
            "hashPrevouts": BIP341_sha_prevouts(tx).hex(),
            "hashScriptPubkeys": BIP341_sha_scriptpubkeys(utxos).hex(),
            "hashSequences": BIP341_sha_sequences(tx).hex(),
        },
        "inputSpending": [],
    }
    witnesses = []
    for index, (priv, hash_type) in enumerate(zip(keys, hash_types)):
        msg, sighash, witness = sign_input(tx, utxos, index, priv, hash_type)
        witnesses.append(witness)
        vector["inputSpending"].append({
            "given": {"txinIndex": index, "privkey": priv.hex(), "hashType": hash_type},
            "intermediary": {"xonlyPubkey": witness[1].hex(), "sigMsg": msg.hex(), "sigHash": sighash.hex()},
            "expected": {"witness": [w.hex() for w in witness]},
        })
    tx.wit.vtxinwit = [CTxInWitness() for _ in tx.vin]
    for txinwit, witness in zip(tx.wit.vtxinwit, witnesses):
        txinwit.scriptWitness.stack = witness
    vector["expected"] = {"signedTx": tx.serialize_with_witness().hex(), "txid": tx.txid_hex, "wtxid": tx.wtxid_hex}
    return vector


def p2wpkh_script(priv):
    return CScript([OP_0, hash160(compressed_pubkey(priv))])


def spending_vectors(derivation):
    main_keys = [bytes.fromhex(k["privkey"]) for k in derivation["chains"][0]["keys"]]
    receive0, receive1, receive2, change0 = main_keys
    # Plain wallet spend: one P2QRH input, a payment and P2QRH change, SIGHASH_DEFAULT.
    simple = spending_vector(
        "one P2QRH input (m/88h/1867h/0h/0/0) paying a P2WPKH address with P2QRH change (m/88h/1867h/0h/1/0); SIGHASH_DEFAULT",
        [receive0], [5_000_000_000], [0x00],
        [(1_000_000_000, p2wpkh_script((7).to_bytes(32, "big"))), (3_999_990_000, qrh_output(change0)[3])],
        [("9a2f6d4e4b3c2a19087f6e5d4c3b2a1908f7e6d5c4b3a29180f7e6d5c4b3a291", 1, 0xfffffffd)],
    )
    # Every sighash type, one per input. SINGLE inputs sit at indexes that have
    # a matching output.
    hash_types = [0x03, 0x83, 0x00, 0x01, 0x02, 0x81, 0x82]
    keys = [receive0, receive1, receive2, change0, (11).to_bytes(32, "big"), (12).to_bytes(32, "big"), (13).to_bytes(32, "big")]
    inputs = [(f"{i + 1:02x}" * 32, i % 3, 0xffffffff - i) for i in range(len(keys))]
    every_hash_type = spending_vector(
        "seven P2QRH inputs, one per sighash type (SINGLE, SINGLE|ANYONECANPAY, DEFAULT, ALL, NONE, ALL|ANYONECANPAY, NONE|ANYONECANPAY)",
        keys, [100_000_000 * (i + 1) for i in range(len(keys))], hash_types,
        [(250_000_000, qrh_output((21).to_bytes(32, "big"))[3]), (300_000_000, p2wpkh_script((22).to_bytes(32, "big")))],
        inputs, locktime=500_000,
    )
    return [simple, every_hash_type]


def invalid_spend_vectors(simple):
    """Witnesses for input 0 of the simple spend that consensus must reject."""
    good = [bytes.fromhex(w) for w in simple["inputSpending"][0]["expected"]["witness"]]
    sig, pubkey, scheme = good
    other_pubkey = compute_xonly_pubkey((2).to_bytes(32, "big"))[0]
    bad_sig = bytes([sig[0] ^ 1]) + sig[1:]
    full_pubkey = compressed_pubkey(bytes.fromhex(simple["inputSpending"][0]["given"]["privkey"]))
    cases = [
        ([sig, pubkey, b"\x00"], "WITNESS_PROGRAM_MISMATCH", "scheme_id 0x00 is reserved and never valid"),
        ([sig, pubkey, b"\x02"], "WITNESS_PROGRAM_MISMATCH", "scheme_id 0x02 (ML-DSA-44) is not active"),
        ([sig, pubkey, b"\xff"], "WITNESS_PROGRAM_MISMATCH", "scheme_id 0xff is the invalid sentinel"),
        ([sig, pubkey, b"\x01\x00"], "WITNESS_PROGRAM_MISMATCH", "scheme_id must be exactly one byte"),
        ([sig, other_pubkey, scheme], "WITNESS_PROGRAM_MISMATCH", "pubkey does not match the commitment"),
        ([sig, full_pubkey, scheme], "WITNESS_PROGRAM_MISMATCH", "33-byte compressed pubkey; P2QRH scheme 0x01 takes the 32-byte x-only key"),
        ([pubkey, scheme], "WITNESS_PROGRAM_MISMATCH", "signature missing"),
        ([b"\x01", sig, pubkey, scheme], "WITNESS_PROGRAM_MISMATCH", "extra witness element; script-path spends are not active"),
        ([bad_sig, pubkey, scheme], "SCHNORR_SIG", "signature does not verify"),
        ([sig + b"\x01", pubkey, scheme], "SCHNORR_SIG", "SIGHASH_ALL appended to a signature made with SIGHASH_DEFAULT"),
        ([sig, pubkey, scheme, pubkey], "WITNESS_PROGRAM_MISMATCH", "scheme_id must be the top witness element"),
    ]
    return [{"witness": [w.hex() for w in witness], "scriptError": error, "comment": comment}
            for witness, error, comment in cases]


def main():
    derivation = derivation_vectors()
    spends = spending_vectors(derivation)
    vectors = {
        "version": 1,
        "comment": "P2QRH wallet test vectors, scheme_id 0x01 key-path only (script-path spends are not active). "
                   "Generated by contrib/testgen/gen_p2qrh_vectors.py; checked against TrueNorth Core by "
                   "src/test/p2qrh_vector_tests.cpp. Signatures use 32 zero bytes as BIP340 aux_rand, as "
                   "TrueNorth Core does; any aux_rand gives a valid signature. Amounts are in base units "
                   "(1 NORTH = 100000000). See doc/p2qrh.md.",
        "scriptPubKey": address_vectors(),
        "invalidAddresses": invalid_address_vectors(),
        "derivation": derivation,
        "keyPathSpending": spends,
        "invalidKeyPathSpending": invalid_spend_vectors(spends[0]),
    }
    json.dump(vectors, sys.stdout, indent=4)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
