// Copyright (c) 2013-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <clientversion.h>
#include <key.h>
#include <key_io.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>

#include <string>
#include <vector>

namespace {

struct TestDerivation {
    std::string pub;
    std::string prv;
    unsigned int nChild;
};

struct TestVector {
    std::string strHexMaster;
    std::vector<TestDerivation> vDerive;

    explicit TestVector(std::string strHexMasterIn) : strHexMaster(strHexMasterIn) {}

    TestVector& operator()(std::string pub, std::string prv, unsigned int nChild) {
        vDerive.emplace_back();
        TestDerivation &der = vDerive.back();
        der.pub = pub;
        der.prv = prv;
        der.nChild = nChild;
        return *this;
    }
};

TestVector test1 =
  TestVector("000102030405060708090a0b0c0d0e0f")
    ("Tpub17LTNxYHMGvdAofPLbzdo1xKFcDh8qpekt8GNxEWJ9h4ZmWomPgm3sCtdtmAfAJ91zeqrhoL9pPQTgmpfVdV7AssWPnNwNtALw8XujxMMxpy",
     "Tprv127AUUZj4XJtsB5AVLe6aphKT7XqjxcCu32RhUGpBZtuY3bhRbKrMz8KPvJf3XuxGMqLU9CFHxxiNrKr296xJQnKChvAECFBu9nmxsdRnMBg",
     0x80000000)
    ("Tpub17NinxFapNBk9xGX3okWRtzRMw3FVDu8mZjhGvKtR7tFr8o6wovJ8zMF3g2jMuc3Yzs8Ra2saZ2UYfAnoSfDVEmLEbRBCJC4ZT4fU3ZxXv3q",
     "Tprv129RtUH2Xca1rKgJCYPyDhjRZSMQ6LgguidrbSNCJY66pQszc1ZPT7Gfoha1NwdLE4tEAWVaAMKZFa5sjnhEVCrPimnqNSCtnroQpJqLfwFp",
     1)
    ("Tpub17QtvA38hktdDniZVY1QJVjK1zREbDvs4W4N7WbfE4X44NLc5XiiSwv9H67BwKYktxakUMTKgfcy2FUXcuzPNvRAAtd23mq4q5oCFLW31fxS",
     "Tprv12Bc1g4aR1GtvA8LeGes6JUKDVjPCLiRCexXS2dy7Uiu2eRVjjMom4qa37dCSYZ7GWxyrKKAbS2Wfi2Pxv32Tz1tAHZo6VxfeXbjBqmQAQj7",
     0x80000002)
    ("Tpub17TWCCZxZTmUdetManbofrjMADa77VUT3qbbnkvxPEPRVnBoCLUYsrMhCe8NmmRCDK6rCxqJJ3ZXzhofud5vuysK1UWzBrn8WoV7PRne3g2J",
     "Tprv12EDHibQGi9kL2J8jXFGTfUMMitFicG1BzVm7GyGGebGU4GgrY7eByH7xffR4Wncm2S1Eun7WrSLZwE8iW3wf77KpeBMeetub8sUQUNMy6mJ",
     2)
    ("Tpub17Vjb315WeF9cjMqJUs1kkEWUJPPr84mFH5V61tUrYpz79HRZQFFF3wA8KTNABDLwnQyP6qnF6JS7GE1uiPN7hTnGPJYNgy2LpB2EaQjSNmm",
     "Tprv12GSgZ2XDtdRK6mcTDWUYYyWfohYTErKPRyeQXvnjy2q5RNKDbtLZAratLypWyT2C6E8wXhM5mFnjQR3AhUV1JVus3RSkCyjJHqwZyoibsPf",
     1000000000)
    ("Tpub17XTMWggkmNXpFhckbQaV3wWU6cy9wZaH5KseGArzkuNbrQpwvfVuK3KBxHH72URfG7FvcBi3dW8hreQvpErxYiwPFqxTVSbyd8e9pW7bioh",
     "Tprv12JAT2i8U1koWd7PuL43GrgWfbw7m4M8REE2xnDAtB7Da8Vic8JbDRxjwyqbitN9GhdYCK4L2cJKrQb3YEYVjRTZQNvCyKtV1aBVviwDvHDX",
     0);

TestVector test2 =
  TestVector("fffcf9f6f3f0edeae7e4e1dedbd8d5d2cfccc9c6c3c0bdbab7b4b1aeaba8a5a29f9c999693908d8a8784817e7b7875726f6c696663605d5a5754514e4b484542")
    ("Tpub17LTNxYHMGvdAoGtfi9i5UA31rQqZXSuaZi47FBF885BS4VpxZqXzJ74EEsFdsYVyuD5ecTzKDTuiQZYtcpQGJnsTFiaQDNf1FWD21uKR6KQ",
     "Tprv127AUUZj4XJtsAgfpSoAsGu3DMizAeETiicDRmDZ1YH2QLaicmUdJR2UzGNS6iRWMbt5ttUhkF4kZwra8BXM129dyALEULb6dj3QCEfgmF4F",
     0)
    ("Tpub17Pj8EFyJkwTFKYUxqw5xXxSYDNapERPs7soUQzGaCV3PkUGw96t4SwdUpLDXfdWR7XZpUgz4cYkgZiyaVy6gePXjXmK57ioPNzKgT8R6Vxy",
     "Tprv12ASDkHR21KiwgxG7aaYkLhSjigjRMCx1Gmxnw2aTcgtN2ZAbLjyNZs4EqshuMVMC1YKinV7UgEAhJNCwjor3xNoNAWeXj5GNrTJkbtPQ7th",
     0xFFFFFFFF)
    ("Tpub17QtBUrzq8qdMVQh9GP5c1J86c7oimuZZD2t1LPBLrDonCmWPkzJNf1R5RiTeRN7Wu3VN8XqfumBHEPqvGsaoNXzN4qw8UT4QFNbHHtPc5uF",
     "Tprv12BbGztSYPDu3rpUJ12YPp38J7RxKth7hMw3KrRVEGRekUrQ3xdPgmvqqTEBt5k5fumecZWK7jEfojbwiUBTBXgEvbEssVazdeEeHNLerfNA",
     1)
    ("Tpub17Th9tsymJUvYb2NHL3EAyF4KwegDt6UpXUNsBSqgA7ADsbNZ4Vq5YuoTGWenDEooVDwuyAvuYtV7KC6wBn1kdsTGf88ygzVcB9GEYKnLgaP",
     "Tprv12EQFQuRUYsCExS9S4ggxmz4XSxppzt2xgNYBhV9ZaK1C9gGDG8vPfqEDJ2PW7THNDj1KJoPW8nM24JisX4yZYA4Dj1QgLgy28351qxYsEE1",
     0xFFFFFFFE)
    ("Tpub17UsBoqv7ustFsybt4X3iLkkWrP9aw57KZB6kiAY5zsRUKwhVAAm65SAYuAnHQcLFSfbYmfzcbVoxxqGKsf8GME9iw2TUFHb23NLp5FeZJzT",
     "Tprv12FaHKsMqAG9xFPP2oAWW9VkiMhJC3rfTi5G5ECqyR5GSc2b9MorQCMbJvj7tjsfemJJ1cdAVX9vbsRpF31t4DzjPYwwzUtzeTd5XuiS5Emq",
     2)
    ("Tpub17WEDmH8dS4CVdEnmnbBQKW8VFSghBQ2QDLie7wtadfH4Q934KogrG17YyZrZYBgwxion3bW2VCpxTGKphkpAfV1JaCHZBeskifXzjvz1YA1",
     "Tprv12GwKHJaLgSUBzeZvXEeC8F8gkkqJJBaYNEsxdzCU3s82gDviXSnANvYK18ntttkioc9juNoJyFmHuyhkmLFHGyU7MoTVYK8N9zR5zKfKsQ1",
     0);

TestVector test3 =
  TestVector("4b381541583be4423346c643850da4b320e46a87ae3d2a4e6da11eba819cd4acba45d239319ac14f863b8d5ab5a0d0c64d2e8a1e7d1457df2e5a3c51c73235be")
    ("Tpub17LTNxYHMGvdAnLLqEYCsg9J3j4AfHK4rH3pfeUGgPmGE2k4EDymxFNHkirnch95d5HqWSTsKc8jZqsBoTXShj8XbNxUcww6r3DiiYKAn3Jo",
     "Tprv127AUUZj4XJts9k7yyBffUtJFENKGQ6czRwyzAWaZoy7CJpwtRcsGNHiWkNAGfSjggakeTQPr9VVHJWBSQxVRod9Dgco7Y2Sc1m4YtYhUeHd",
      0x80000000)
    ("Tpub17NpahW7UxHzWysSETgYNjQ3iRQn3SAUkgY4cThxXL5BbHmBgoUYpKq5U2ad8RVgNGfLMDRd4T5w4UL89PmNtJNoY44RWVnbeXQ5dCn728s5",
     "Tprv129XgDXZCCgGDMHDPCL1AY93uviveYx2tqSDvykGQkH2ZZr5M17e8SkWE48Wd1Q4c8jQ4UMBioE7jx3pazB5BZPLr7kiAHBiR8n2Cqu8q2Th",
      0);

TestVector test4 =
  TestVector("3ddd5602285899a946114506157c7997e5444528f3003f6134712147db19b678")
    ("Tpub17LTNxYHMGvdApPrQ5GZgUtkwMHkqCZUbXBX52CxGGJ6usdsZRtNiTaqrQz3Hiafgepy4b96WYuWJejxvsjTcozPW9NmhWF4ewpPwdAz7RMv",
     "Tprv127AUUZj4XJtsBodYov2UHdm8rbuSKM2jg5gPYFG9gVwt9imDdXU2aWGcSXSPgB8sG3iGZbGz5ph3wWoj53kLtSocBrf9auLGHtvhejoFksV",
     0x80000000)
    ("Tpub17PcVLvQ1egjcTGsXhbVTVHrhoDe6us19GV8jNB8EukAtNAvfzDoSbdXCaaK3kD2p4vjRYBV89qSZBfezaWv1uJSYBhCfP4A2zcwzRY8dEkG",
     "Tprv12AKarwqiu51JpgegSExFJ2ruJXni2eZHRPJ3tDS8Kx1reFpLBrtkiYwxc5MdizKEE2ji6ATDQGo8tAzpgRQyu5gY3ExpnpnGH9xWPazUrpg",
     0x80000001)
    ("Tpub17RkAzuo2AQnD8RjAqsQMbASYy9pBQnhwvRBZ8veSZwGL9GFBriH9pkV5UX5abPfVeiLnRxREcHnYA9yu37jAcbNuXV3iLndys3JtVBz45y6",
     "Tprv12CTGWwEjQo3uVqWKaWs9PuSkUTxnXaG65KLsexxKz97JRM8r4MNTwfuqW3KinPM4Jo8pGmHBAuu1b8pY5MN7BU9mHafqts2b8qWCV9sadZU",
     0);

const std::vector<std::string> TEST5 = {
    "Tpub17LTNxYHMGvdAnKHo72HbAHmee95ZcDT9UPr3WLZFfdVyeQYJ93RTUMkRS5DEuAUmj4fwJWJ11JjypW8xnbqfk3QGVTTF6Ytn4MZzBDb5QHK",
    "Tprv127AUUZj4XJts9j4wqfkNy2mr9TEAj11HdJ1N2Ns95qLwvVRxLgWmbHBBTpcX1kg8AdJXqncobjreh1bsMsW1Zha5W5eKzvguvb4SapciogU",
    "Tpub17LTNxYHMGvdAnKHo72HbAHmee95ZcDT9UPr3WLZFfdVyeQYJ93RTUMkRSCzDc6Gu8mNqqncpddDydnY4T4JYLrmme1jg59eN2ybY9ekD5FV",
    "Tprv127AUUZj4XJts9j4wqfkNy2mr9TEAj11HdJ1N2Ns95qLwvVRxLgWmbHBBTpydQRMd1kKPsgSBkk8jzRCmW77qSwdPDqvpCiNU5tZ6ZbwPo7z",
    "Tpub17LTNxYHMGvdAnKHo72HbAHmee95ZcDT9UPr3WLZFfdVyeQYJ93RTUMkRS79z5PvoaVMASKshutMympjjhiTPPkF9H6XMLhag41aNfpWjXHk",
    "Tprv127AUUZj4XJts9j4wqfkNy2mr9TEAj11HdJ1N2Ns95qLwvVRxLgWmbHBBTj9Psj1XTUHiUDh531Gk8TQSkmGgVq6krviVUGJn6vXw5mgBBhm",
    "Tprv127AuJJD9vQMADwAsVw1S9UC5ht6fUHjS7a4SikuSDJxv7pZNTzqS3jJEtFiToLtYEdb1e5PNSXDNA7GLdTBVvw9oXJPNGDW923DGMafcmWT",
    "Tpub17LTonGmSg25TrXPimHYeLjBtCZx4MWBHxfu8CibYo77wqjfiGMk7vosUrm8vKGvSvELW7a5SniWWKMPq6dDFYRcPNGCAuTr4nSnbwF7o3ns",
    "Tprv127AUUZj4b6v21P4CyvHtEpTTaJqjFrRr1zfsnoyxbBPr7eF4fui2AyDRBtCxUmDy7wo5QoiV94QgKp5CbKyhgjj6s9DuL2gZ95mi3pdCfqk",
    "Tpub17LTNxYHMLieKdyH4FGq6S5TG4zh894shs6WZGmg5AyYsqZMQUGci43nfAPdQzhFsoYYZtJQZVFhpV4Ch4W1TJEBgi72hyH2UuVM3da7LYUU",
    "DMwo58pR1QLEFihHiXPVykYB6fJmsTeHvyTp7hRThAtCX8CvYzgPcn8XnmdfHGMQzT7ayAmfo4z3gY5KfbrZWZ6St24UVf2Qgo6oujFktLHdHY4",
    "DMwo58pR1QLEFihHiXPVykYB6fJmsTeHvyTp7hRThAtCX8CvYzgPcn8XnmdfHPmHJiEDXkTiJTVV9rHEBUem2mwVbbNfvT2MTcAqj3nesx8uBf9",
    "Tprv127AUUZj4XJts9j4wqfkNy2mr9TEAj11HdJ1N2Ns95qLwvVRxLgWmbHBBThCehVZVc3cVLQ7N8RekB8ofqeexr8Ft5HePE7ct7GXYZomhQ2G",
    "Tprv127AUUZj4XJts9j4wqfkNy2mr9TEAj11HdJ1N2Ns95qLwvVRxLgWmbHBBTj9Psj1XTUHiUDh531Gk8TQNDtumxoqyQDTWDQbTQdWZJ22PAwv",
    "Tpub17LTNxYHMGvdAnKHo72HbAHmee95ZcDT9UPr3WLZFfdVyeQYJ93RTUMkRS96jFdNqRv2Pa9TQpTyyj9LWcq573T624jbTarGa3fam9pUEy6C",
    "xprv9s21ZrQH143K3QTDL4LXw2F7HEK3wJUD2nW2nRk4stbPy6cq3jPPqjiChkVvvNKmPGJxWUtg6LnF5kejMRNNU3TGtRBeJgk33yuGBxrMPHL"
};

void RunTest(const TestVector& test)
{
    std::vector<std::byte> seed{ParseHex<std::byte>(test.strHexMaster)};
    CExtKey key;
    CExtPubKey pubkey;
    key.SetSeed(seed);
    pubkey = key.Neuter();
    for (const TestDerivation &derive : test.vDerive) {
        unsigned char data[74];
        key.Encode(data);
        pubkey.Encode(data);

        // Test private key
        BOOST_CHECK(EncodeExtKey(key) == derive.prv);
        BOOST_CHECK(DecodeExtKey(derive.prv) == key); //ensure a base58 decoded key also matches

        // Test public key
        BOOST_CHECK(EncodeExtPubKey(pubkey) == derive.pub);
        BOOST_CHECK(DecodeExtPubKey(derive.pub) == pubkey); //ensure a base58 decoded pubkey also matches

        // Derive new keys
        CExtKey keyNew;
        BOOST_CHECK(key.Derive(keyNew, derive.nChild));
        CExtPubKey pubkeyNew = keyNew.Neuter();
        if (!(derive.nChild & 0x80000000)) {
            // Compare with public derivation
            CExtPubKey pubkeyNew2;
            BOOST_CHECK(pubkey.Derive(pubkeyNew2, derive.nChild));
            BOOST_CHECK(pubkeyNew == pubkeyNew2);
        }
        key = keyNew;
        pubkey = pubkeyNew;
    }
}

}  // namespace

BOOST_FIXTURE_TEST_SUITE(bip32_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(bip32_test1) {
    RunTest(test1);
}

BOOST_AUTO_TEST_CASE(bip32_test2) {
    RunTest(test2);
}

BOOST_AUTO_TEST_CASE(bip32_test3) {
    RunTest(test3);
}

BOOST_AUTO_TEST_CASE(bip32_test4) {
    RunTest(test4);
}

BOOST_AUTO_TEST_CASE(bip32_test5) {
    for (const auto& str : TEST5) {
        auto dec_extkey = DecodeExtKey(str);
        auto dec_extpubkey = DecodeExtPubKey(str);
        BOOST_CHECK_MESSAGE(!dec_extkey.key.IsValid(), "Decoding '" + str + "' as xprv should fail");
        BOOST_CHECK_MESSAGE(!dec_extpubkey.pubkey.IsValid(), "Decoding '" + str + "' as xpub should fail");
    }
}

BOOST_AUTO_TEST_CASE(bip32_max_depth) {
    CExtKey key_parent{DecodeExtKey(test1.vDerive[0].prv)}, key_child;
    CExtPubKey pubkey_parent{DecodeExtPubKey(test1.vDerive[0].pub)}, pubkey_child;

    // We can derive up to the 255th depth..
    for (auto i = 0; i++ < 255;) {
        BOOST_CHECK(key_parent.Derive(key_child, 0));
        std::swap(key_parent, key_child);
        BOOST_CHECK(pubkey_parent.Derive(pubkey_child, 0));
        std::swap(pubkey_parent, pubkey_child);
    }

    // But trying to derive a non-existent 256th depth will fail!
    BOOST_CHECK(key_parent.nDepth == 255 && pubkey_parent.nDepth == 255);
    BOOST_CHECK(!key_parent.Derive(key_child, 0));
    BOOST_CHECK(!pubkey_parent.Derive(pubkey_child, 0));
}

BOOST_AUTO_TEST_SUITE_END()
