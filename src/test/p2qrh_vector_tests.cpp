// Copyright (c) 2026 The TrueNorth developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

// Checks the published P2QRH wallet test vectors against TrueNorth Core. The
// vectors are generated independently, from the functional test framework's
// Python, by contrib/testgen/gen_p2qrh_vectors.py. See doc/p2qrh.md.

#include <test/data/p2qrh_wallet_vectors.json.h>

#include <addresstype.h>
#include <chainparams.h>
#include <core_io.h>
#include <hash.h>
#include <key.h>
#include <key_io.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <script/descriptor.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/script_error.h>
#include <script/sign.h>
#include <script/signingprovider.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <truenorth/qrh.h>
#include <uint256.h>
#include <util/bip32.h>
#include <util/chaintype.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>
#include <univalue.h>

#include <algorithm>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace {

UniValue LoadVectors()
{
    UniValue vectors;
    vectors.read(json_tests::p2qrh_wallet_vectors);
    return vectors;
}

CScript ToScript(const std::string& hex)
{
    const auto bytes{ParseHex(hex)};
    return CScript(bytes.begin(), bytes.end());
}

CMutableTransaction ToTx(const std::string& hex)
{
    const auto bytes{ParseHex(hex)};
    CMutableTransaction tx;
    SpanReader{bytes} >> TX_WITH_WITNESS(tx);
    return tx;
}

std::vector<CTxOut> SpentOutputs(const UniValue& vec)
{
    std::vector<CTxOut> utxos;
    for (const auto& utxo : vec["given"]["utxosSpent"].getValues()) {
        utxos.emplace_back(utxo["amount"].getInt<int64_t>(), ToScript(utxo["scriptPubKey"].get_str()));
    }
    return utxos;
}

std::vector<std::string> WitnessHex(const CScriptWitness& witness)
{
    std::vector<std::string> out;
    for (const auto& item : witness.stack)
        out.push_back(HexStr(item));
    return out;
}

std::vector<std::string> StringArray(const UniValue& array)
{
    std::vector<std::string> out;
    for (const auto& item : array.getValues())
        out.push_back(item.get_str());
    return out;
}

//! The vectors write hardened steps as "88h", as descriptors do;
//! ParseHDKeypath only takes "88'".
std::vector<uint32_t> ParsePath(std::string path_str)
{
    std::replace(path_str.begin(), path_str.end(), 'h', '\'');
    std::vector<uint32_t> path;
    BOOST_REQUIRE(ParseHDKeypath(path_str, path));
    return path;
}

CKey DeriveKey(const CExtKey& master, const std::string& path_str)
{
    const std::vector<uint32_t> path{ParsePath(path_str)};
    CExtKey key{master};
    for (const uint32_t index : path)
        BOOST_REQUIRE(key.Derive(key, index));
    return key.key;
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(p2qrh_vector_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(p2qrh_scriptpubkey_vectors)
{
    const UniValue vectors{LoadVectors()};
    for (const auto& vec : vectors["scriptPubKey"].getValues()) {
        const auto pubkey{ParseHex(vec["given"]["pubkey"].get_str())};
        const auto scheme_id{static_cast<uint8_t>(vec["given"]["schemeId"].getInt<int>())};
        const uint256 script_root{ParseHex(vec["given"]["scriptRoot"].get_str())};

        const uint256 commitment{truenorth::ComputeQRHCommitment(scheme_id, pubkey, script_root)};
        BOOST_CHECK_EQUAL(HexStr(commitment), vec["intermediary"]["commitment"].get_str());
        const auto preimage{ParseHex(vec["intermediary"]["commitmentPreimage"].get_str())};
        BOOST_CHECK_EQUAL(HexStr((HashWriter{} << std::span<const uint8_t>{preimage}).GetSHA256()), HexStr(commitment));

        const CScript script{GetScriptForDestination(WitnessV2QRH{commitment})};
        BOOST_CHECK_EQUAL(HexStr(script), vec["expected"]["scriptPubKey"].get_str());

        const UniValue& addresses{vec["expected"]["addresses"]};
        for (const auto& chain : addresses.getKeys()) {
            SelectParams(ChainTypeFromString(chain).value());
            const std::string& address{addresses[chain].get_str()};
            BOOST_CHECK_EQUAL(EncodeDestination(WitnessV2QRH{commitment}), address);
            const CTxDestination dest{DecodeDestination(address)};
            BOOST_CHECK(std::holds_alternative<WitnessV2QRH>(dest));
            BOOST_CHECK(GetScriptForDestination(dest) == script);
        }
    }
    SelectParams(ChainType::MAIN);
}

BOOST_AUTO_TEST_CASE(p2qrh_invalid_address_vectors)
{
    const UniValue vectors{LoadVectors()};
    for (const auto& vec : vectors["invalidAddresses"].getValues()) {
        SelectParams(ChainTypeFromString(vec["chain"].get_str()).value());
        const std::string& comment{vec["comment"].get_str()};
        const CTxDestination dest{DecodeDestination(vec["address"].get_str())};
        if (vec.exists("valid_but_not_p2qrh")) {
            BOOST_CHECK_MESSAGE(std::holds_alternative<WitnessUnknown>(dest), comment);
        } else {
            BOOST_CHECK_MESSAGE(!IsValidDestination(dest), comment);
        }
    }
    SelectParams(ChainType::MAIN);
}

BOOST_AUTO_TEST_CASE(p2qrh_derivation_vectors)
{
    const UniValue vectors{LoadVectors()};
    const UniValue& derivation{vectors["derivation"]};
    const auto seed{ParseHex(derivation["seed"].get_str())};
    CExtKey master;
    master.SetSeed(std::as_bytes(std::span{seed}));

    for (const auto& entry : derivation["chains"].getValues()) {
        SelectParams(ChainTypeFromString(entry["chain"].get_str()).value());
        BOOST_CHECK_EQUAL(EncodeExtKey(master), entry["rootXprv"].get_str());

        const std::vector<uint32_t> account_path{ParsePath(entry["accountPath"].get_str())};
        CExtKey account{master};
        for (const uint32_t index : account_path)
            BOOST_REQUIRE(account.Derive(account, index));
        BOOST_CHECK_EQUAL(EncodeExtKey(account), entry["accountXprv"].get_str());
        BOOST_CHECK_EQUAL(EncodeExtPubKey(account.Neuter()), entry["accountXpub"].get_str());

        // Descriptors parse with their checksums and round-trip unchanged.
        FlatSigningProvider desc_keys;
        std::map<uint32_t, std::unique_ptr<Descriptor>> descs;
        for (const auto& [branch, field] : {std::pair{0U, "receiveDescriptor"}, std::pair{1U, "changeDescriptor"}}) {
            std::string error;
            auto parsed{Parse(entry[field].get_str(), desc_keys, error, /*require_checksum=*/true)};
            BOOST_REQUIRE_MESSAGE(parsed.size() == 1, error);
            BOOST_CHECK_EQUAL(parsed[0]->ToString(), entry[field].get_str());
            descs[branch] = std::move(parsed[0]);
        }

        for (const auto& k : entry["keys"].getValues()) {
            const std::string& path_str{k["path"].get_str()};
            const CKey key{DeriveKey(master, path_str)};
            BOOST_CHECK_EQUAL(HexStr(std::span{key.begin(), key.end()}), k["privkey"].get_str());
            const XOnlyPubKey xonly{key.GetPubKey()};
            BOOST_CHECK_EQUAL(HexStr(xonly), k["xonlyPubkey"].get_str());
            BOOST_CHECK_EQUAL(key.GetPubKey()[0] == 0x03, k["fullPubkeyHasOddY"].get_bool());

            const uint256 commitment{truenorth::ComputeQRHCommitment(truenorth::QRH_SCHEME_SCHNORR, std::span<const unsigned char>{xonly.begin(), xonly.end()}, truenorth::QRH_EMPTY_SCRIPT_ROOT)};
            BOOST_CHECK_EQUAL(HexStr(commitment), k["commitment"].get_str());
            const CScript script{GetScriptForDestination(WitnessV2QRH{commitment})};
            BOOST_CHECK_EQUAL(HexStr(script), k["scriptPubKey"].get_str());
            BOOST_CHECK_EQUAL(EncodeDestination(WitnessV2QRH{commitment}), k["address"].get_str());

            // The account descriptor produces the same script at this index.
            const std::vector<uint32_t> path{ParsePath(path_str)};
            const uint32_t branch{path.at(path.size() - 2)};
            const int index{static_cast<int>(path.back())};
            std::vector<CScript> scripts;
            FlatSigningProvider out;
            BOOST_REQUIRE(descs.at(branch)->Expand(index, desc_keys, scripts, out));
            BOOST_REQUIRE_EQUAL(scripts.size(), 1U);
            BOOST_CHECK(scripts[0] == script);
        }
    }
    SelectParams(ChainType::MAIN);
}

BOOST_AUTO_TEST_CASE(p2qrh_keypath_spending_vectors)
{
    const UniValue vectors{LoadVectors()};
    for (const auto& vec : vectors["keyPathSpending"].getValues()) {
        BOOST_TEST_CONTEXT(vec["comment"].get_str())
        {
            CMutableTransaction tx{ToTx(vec["given"]["rawUnsignedTx"].get_str())};
            const std::vector<CTxOut> utxos{SpentOutputs(vec)};
            PrecomputedTransactionData txdata;
            txdata.Init(tx, std::vector<CTxOut>{utxos}, /*force=*/true);

            BOOST_CHECK(txdata.m_bip341_taproot_ready);
            BOOST_CHECK_EQUAL(HexStr(txdata.m_spent_amounts_single_hash), vec["intermediary"]["hashAmounts"].get_str());
            BOOST_CHECK_EQUAL(HexStr(txdata.m_outputs_single_hash), vec["intermediary"]["hashOutputs"].get_str());
            BOOST_CHECK_EQUAL(HexStr(txdata.m_prevouts_single_hash), vec["intermediary"]["hashPrevouts"].get_str());
            BOOST_CHECK_EQUAL(HexStr(txdata.m_spent_scripts_single_hash), vec["intermediary"]["hashScriptPubkeys"].get_str());
            BOOST_CHECK_EQUAL(HexStr(txdata.m_sequences_single_hash), vec["intermediary"]["hashSequences"].get_str());

            for (const auto& input : vec["inputSpending"].getValues()) {
                const int txinpos{input["given"]["txinIndex"].getInt<int>()};
                const int hashtype{input["given"]["hashType"].getInt<int>()};
                const auto privkey{ParseHex(input["given"]["privkey"].get_str())};
                CKey key;
                key.Set(privkey.begin(), privkey.end(), /*fCompressedIn=*/true);
                BOOST_CHECK_EQUAL(HexStr(XOnlyPubKey{key.GetPubKey()}), input["intermediary"]["xonlyPubkey"].get_str());

                // Sign through the wallet path: a qrh() descriptor holding the key,
                // then ProduceSignature, which builds the P2QRH witness.
                FlatSigningProvider keys;
                std::string error;
                auto descs{Parse("qrh(" + EncodeSecret(key) + ")", keys, error)};
                BOOST_REQUIRE_MESSAGE(descs.size() == 1, error);
                std::vector<CScript> scripts;
                FlatSigningProvider provider;
                BOOST_REQUIRE(descs[0]->Expand(0, keys, scripts, provider));
                descs[0]->ExpandPrivate(0, keys, provider);
                BOOST_CHECK(scripts.at(0) == utxos.at(txinpos).scriptPubKey);

                MutableTransactionSignatureCreator creator(tx, txinpos, utxos[txinpos].nValue, &txdata, hashtype);
                SignatureData sigdata;
                BOOST_CHECK(ProduceSignature(provider, creator, utxos[txinpos].scriptPubKey, sigdata));
                BOOST_CHECK(WitnessHex(sigdata.scriptWitness) == StringArray(input["expected"]["witness"]));
                UpdateInput(tx.vin[txinpos], sigdata);

                ScriptExecutionData sed;
                sed.m_annex_init = true;
                sed.m_annex_present = false;
                uint256 sighash;
                BOOST_CHECK(SignatureHashSchnorr(sighash, sed, tx, txinpos, hashtype, SigVersion::TAPROOT, txdata, MissingDataBehavior::FAIL));
                BOOST_CHECK_EQUAL(HexStr(sighash), input["intermediary"]["sigHash"].get_str());
                const auto sigmsg{ParseHex(input["intermediary"]["sigMsg"].get_str())};
                BOOST_CHECK_EQUAL(HexStr((HashWriter{HASHER_TAPSIGHASH} << std::span<const uint8_t>{sigmsg}).GetSHA256()), HexStr(sighash));
            }

            const CTransaction signed_tx{tx};
            BOOST_CHECK_EQUAL(EncodeHexTx(signed_tx), vec["expected"]["signedTx"].get_str());
            BOOST_CHECK_EQUAL(signed_tx.GetHash().GetHex(), vec["expected"]["txid"].get_str());
            BOOST_CHECK_EQUAL(signed_tx.GetWitnessHash().GetHex(), vec["expected"]["wtxid"].get_str());

            for (unsigned int i = 0; i < tx.vin.size(); ++i) {
                ScriptError err;
                BOOST_CHECK_MESSAGE(VerifyScript(tx.vin[i].scriptSig, utxos[i].scriptPubKey, &tx.vin[i].scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS,
                                                 MutableTransactionSignatureChecker(&tx, i, utxos[i].nValue, txdata, MissingDataBehavior::ASSERT_FAIL), &err),
                                    "input " << i << ": " << ScriptErrorString(err));
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(p2qrh_invalid_keypath_spending_vectors)
{
    const UniValue vectors{LoadVectors()};
    const UniValue& base{vectors["keyPathSpending"][0]};
    CMutableTransaction tx{ToTx(base["expected"]["signedTx"].get_str())};
    const std::vector<CTxOut> utxos{SpentOutputs(base)};
    PrecomputedTransactionData txdata;
    txdata.Init(tx, std::vector<CTxOut>{utxos}, /*force=*/true);

    const std::map<std::string, ScriptError> errors{
        {"WITNESS_PROGRAM_MISMATCH", SCRIPT_ERR_WITNESS_PROGRAM_MISMATCH},
        {"SCHNORR_SIG", SCRIPT_ERR_SCHNORR_SIG},
    };

    const auto verify{[&](ScriptError& err) {
        return VerifyScript(tx.vin[0].scriptSig, utxos[0].scriptPubKey, &tx.vin[0].scriptWitness, MANDATORY_SCRIPT_VERIFY_FLAGS,
                            MutableTransactionSignatureChecker(&tx, 0, utxos[0].nValue, txdata, MissingDataBehavior::ASSERT_FAIL), &err);
    }};

    // The base transaction's own witness is valid.
    ScriptError err;
    BOOST_REQUIRE(verify(err));

    for (const auto& vec : vectors["invalidKeyPathSpending"].getValues()) {
        const std::string& comment{vec["comment"].get_str()};
        tx.vin[0].scriptWitness.stack.clear();
        for (const auto& item : vec["witness"].getValues())
            tx.vin[0].scriptWitness.stack.push_back(ParseHex(item.get_str()));
        BOOST_CHECK_MESSAGE(!verify(err), comment);
        BOOST_CHECK_MESSAGE(err == errors.at(vec["scriptError"].get_str()), comment << ": got " << ScriptErrorString(err));
    }
}

BOOST_AUTO_TEST_SUITE_END()
