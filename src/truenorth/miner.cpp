// Copyright (c) 2026 The TrueNorth developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

// truenorth-miner: solo CPU miner for RandomX-based TrueNorth.
//
// Talks to a running bitcoind via popen() to bitcoin-cli (on PATH or
// passed with -cli=...). The popen overhead is 10-30ms per call but we
// only make one call per block template, so it's noise next to the
// hashing cost. Hashing goes through src/truenorth/randomx_wrapper.
//
// Workers stride the nonce space (-threads=N). Builds full block
// templates including the BIP141 witness commitment. Hash rate and
// solve time go to stderr.
//
// Also has -benchmark=1 for raw hash-rate testing without a node running.
//
// Build: cmake --build build --target truenorth-miner
// Usage:
//   truenorth-miner -chain=regtest -datadir=/path/to/dd
//                   -address=bcrt1q... [-threads=N] [-maxblocks=N]
//                   [-budgetseconds=N] [-cli=/path/to/bitcoin-cli]
//   truenorth-miner -benchmark=1 -threads=N [-budgetseconds=N]

#include <addresstype.h>
#include <arith_uint256.h>
#include <chainparams.h>
#include <chainparamsbase.h>
#include <consensus/amount.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <hash.h>
#include <key_io.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <serialize.h>
#include <streams.h>
#include <truenorth/randomx_wrapper.h>
#include <truenorth/seed_key.h>
#include <uint256.h>
#include <util/chaintype.h>
#include <util/strencodings.h>
#include <util/translation.h>

#include <univalue.h>

// Every standalone executable that links bitcoin_common (which pulls in
// clientversion.cpp / the bilingual-string machinery) must define the
// translation-function pointer. bitcoind.cpp and bitcoin-cli.cpp do the
// same; without it the link fails with "undefined reference to
// G_TRANSLATION_FUN". nullptr = identity translation (no localisation).
const TranslateFn G_TRANSLATION_FUN{nullptr};

#include <atomic>
#include <cassert>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_stop{false};
void OnSignal(int /*sig*/) { g_stop.store(true); }

[[noreturn]] void Die(const std::string& msg)
{
    std::fprintf(stderr, "truenorth-miner: %s\n", msg.c_str());
    std::exit(1);
}

// ---- bitcoin-cli RPC shell-out -------------------------------------------

struct CliConfig {
    std::string cli_path;  // "bitcoin-cli" or absolute path
    std::string chain_arg; // "-regtest" / "-testnet=3" / "-signet" / ""
    std::string datadir;   // empty -> use default
    std::string rpcport;   // empty -> bitcoin-cli's default for the chain
    std::string rpchost;   // empty -> 127.0.0.1
};

// POSIX shell single-quote escape. Wraps `s` in single quotes; any embedded
// single quote becomes the four-character sequence '\'' (close-quote,
// escaped-quote, open-quote). After escaping, the resulting string is
// guaranteed safe to embed in a /bin/sh command line as a single argument
// regardless of the input contents -- no shell metacharacter can escape
// the wrapping quotes.
static std::string ShellQuote(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 2);
    out += '\'';
    for (char c : s) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    out += '\'';
    return out;
}

std::string CallCLI(const CliConfig& cfg, const std::string& argv_tail)
{
    // cli_path is an operator-supplied path (typically "bitcoin-cli" or an
    // absolute path); we leave it un-quoted so the shell can do PATH
    // resolution + globbing if the operator wants. chain_arg is one of a
    // small set of code-controlled string literals. The operator-supplied
    // datadir / rpcport / rpchost fields can contain anything, so we
    // shell-escape each value -- this is the boundary that prevents
    // `truenorth-miner -datadir='; rm -rf ~'` from being a command-injection.
    // argv_tail is also code-controlled (built from getblocktemplate /
    // submitblock arguments) and may intentionally contain single quotes
    // for JSON literals; do not shell-escape it.
    std::string cmd = cfg.cli_path;
    if (!cfg.chain_arg.empty()) cmd += " " + cfg.chain_arg;
    if (!cfg.datadir.empty()) cmd += " -datadir=" + ShellQuote(cfg.datadir);
    if (!cfg.rpcport.empty()) cmd += " -rpcport=" + ShellQuote(cfg.rpcport);
    if (!cfg.rpchost.empty()) cmd += " -rpcconnect=" + ShellQuote(cfg.rpchost);
    cmd += " " + argv_tail;

    FILE* p = popen(cmd.c_str(), "r");
    if (!p) Die("popen failed for: " + cmd);
    std::string out;
    char buf[4096];
    while (std::fgets(buf, sizeof(buf), p))
        out.append(buf);
    const int status = pclose(p);
    if (status != 0) {
        Die("RPC failed (exit=" + std::to_string(status) + "): " + cmd +
            "\n--> " + out);
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
        out.pop_back();
    return out;
}

UniValue CallRPC(const CliConfig& cfg, const std::string& argv_tail)
{
    const std::string raw = CallCLI(cfg, argv_tail);
    UniValue v;
    if (!v.read(raw)) {
        // Some RPCs return bare null (submitblock on success). UniValue::read
        // should handle "null"; if it doesn't, treat empty-after-trim as null.
        if (raw.empty() || raw == "null") {
            v.setNull();
            return v;
        }
        Die("could not parse RPC response as JSON: <" + raw + ">");
    }
    return v;
}

// uint256{string_literal} is consteval -- only good for compile-time
// literals. For runtime hex (RPC responses), use uint256::FromHex and
// die loudly on parse failure.
uint256 Uint256FromHexOrDie(const std::string& hex, const char* what)
{
    auto v = uint256::FromHex(hex);
    if (!v) Die(std::string("expected hex ") + what + ", got: " + hex);
    return *v;
}

uint256 GetBlockHashAt(const CliConfig& cfg, int height)
{
    // bitcoin-cli prints string-valued RPC results unquoted (just the raw
    // hex), which is not valid JSON, so don't go through CallRPC -- take
    // the raw line directly.
    const std::string raw = CallCLI(cfg, "getblockhash " + std::to_string(height));
    return Uint256FromHexOrDie(raw, "block hash");
}

// ---- Per-template seed key -----------------------------------------------

// Compute the RandomX seed key for the block at next_height. For the genesis
// epoch + lag window, returns kGenesisSeed. Otherwise looks up the seed
// block's hash via getblockhash RPC.
uint256 SeedKeyForNextHeight(const CliConfig& cfg, int next_height)
{
    const int seed_height = truenorth::SeedHeightForNextHeight(next_height);
    if (seed_height == 0) return truenorth::kGenesisSeed;
    return GetBlockHashAt(cfg, seed_height);
}

// ---- Block assembly from template ----------------------------------------

// BIP141 witness commitment scriptPubKey: OP_RETURN OP_PUSH36 0xaa21a9ed
// <32-byte commitment hash>. Total 38 bytes (MINIMUM_WITNESS_COMMITMENT).
CScript WitnessCommitmentScript(const uint256& commitment_hash)
{
    CScript s;
    s.resize(MINIMUM_WITNESS_COMMITMENT);
    s[0] = OP_RETURN;
    s[1] = 0x24;
    s[2] = 0xaa;
    s[3] = 0x21;
    s[4] = 0xa9;
    s[5] = 0xed;
    std::memcpy(&s[6], commitment_hash.begin(), 32);
    return s;
}

// BIP34: scriptSig must begin with a push of the block height. We follow
// with an 8-byte extranonce so the coinbase txid varies across attempts
// once the 32-bit nonce space is exhausted.
//
// If `add_witness_commitment_placeholder` is true (SegWit-active chains),
// vout[1] gets a zero-hash placeholder commitment and the coinbase witness
// stack gets the 32-byte BIP141 reserved value. Callers MUST patch vout[1]
// with the real commitment hash after computing BlockWitnessMerkleRoot
// over the final tx set.
CMutableTransaction BuildCoinbase(int height,
                                  CAmount value,
                                  const CScript& pay,
                                  bool add_witness_commitment_placeholder,
                                  uint64_t extranonce)
{
    CMutableTransaction tx;
    tx.version = 1;
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();

    // BIP34: the coinbase scriptSig MUST begin with the block height,
    // serialized exactly the way ContextualCheckBlock builds its expected
    // prefix: `CScript() << nHeight`. That goes through push_int64(), which
    // uses OP_1..OP_16 for small heights -- *not* the same bytes as
    // `CScript() << CScriptNum(height)`. Pass the int directly.
    CScript sig = CScript() << static_cast<int64_t>(height);
    std::vector<unsigned char> en(8);
    for (int i = 0; i < 8; ++i)
        en[i] = static_cast<unsigned char>((extranonce >> (8 * i)) & 0xff);
    sig << en;
    tx.vin[0].scriptSig = sig;
    tx.vin[0].nSequence = 0xffffffffu;

    tx.vout.emplace_back(value, pay);

    if (add_witness_commitment_placeholder) {
        tx.vout.emplace_back(CAmount{0}, WitnessCommitmentScript(uint256::ZERO));
        tx.vin[0].scriptWitness.stack.emplace_back(32, 0);
    }
    return tx;
}

CBlock AssembleBlock(const UniValue& tmpl, const CScript& pay, uint64_t extranonce)
{
    CBlock block;
    block.nVersion = tmpl["version"].getInt<int32_t>();
    block.hashPrevBlock = Uint256FromHexOrDie(tmpl["previousblockhash"].get_str(),
                                              "previousblockhash");
    block.nTime = tmpl["curtime"].getInt<uint32_t>();
    block.nBits = static_cast<uint32_t>(std::stoul(tmpl["bits"].get_str(), nullptr, 16));
    block.nNonce = 0;

    const int height = tmpl["height"].getInt<int>();
    const CAmount cbvalue = static_cast<CAmount>(tmpl["coinbasevalue"].getInt<int64_t>());
    // Presence of default_witness_commitment in the template is our signal
    // that SegWit is active on this chain; on a pre-SegWit chain the field
    // is absent and no commitment vout is needed.
    const bool segwit_active = tmpl.exists("default_witness_commitment");

    // Build coinbase with a zero-hash commitment placeholder (if SegWit
    // active); we patch it once we know the witness merkle root over the
    // final tx set.
    CMutableTransaction cb = BuildCoinbase(height, cbvalue, pay,
                                           /*add_witness_commitment_placeholder=*/segwit_active,
                                           extranonce);
    block.vtx.push_back(MakeTransactionRef(std::move(cb)));

    // Deserialise + append every template transaction. coinbasevalue from
    // the template already includes subsidy + sum-of-fees, so vout[0] needs
    // no adjustment as long as we accept the full template tx set.
    if (tmpl.exists("transactions")) {
        const UniValue& txs = tmpl["transactions"];
        for (size_t i = 0; i < txs.size(); ++i) {
            const std::string& hex = txs[i]["data"].get_str();
            CMutableTransaction mtx;
            if (!DecodeHexTx(mtx, hex, /*try_no_witness=*/true, /*try_witness=*/true)) {
                Die("could not deserialise template tx[" + std::to_string(i) + "]");
            }
            block.vtx.push_back(MakeTransactionRef(std::move(mtx)));
        }
    }

    if (segwit_active) {
        // BIP141: commitment = SHA256d(witness_merkle_root || reserved_value).
        // BlockWitnessMerkleRoot treats the coinbase's wtxid as zero, so the
        // placeholder we put in vout[1] does not affect the witness merkle
        // root -- safe to compute it now and patch vout[1] after.
        uint256 witnessroot = BlockWitnessMerkleRoot(block, /*mutated=*/nullptr);
        const std::vector<unsigned char> reserved_value(32, 0x00);
        uint256 commitment;
        CHash256().Write(witnessroot).Write(reserved_value).Finalize(commitment);

        CMutableTransaction patched(*block.vtx[0]);
        patched.vout[1].scriptPubKey = WitnessCommitmentScript(commitment);
        block.vtx[0] = MakeTransactionRef(std::move(patched));
    }

    block.hashMerkleRoot = BlockMerkleRoot(block);
    return block;
}

// ---- Hashing loop --------------------------------------------------------

// Multi-threaded nonce-grinding loop. Spawns `num_threads` workers each
// striding the 32-bit nonce space at step `num_threads` from a per-worker
// offset; the first worker to find a hash <= target wins via CAS on
// `found`, and the others bail on their next loop iteration. Returns true
// with block.nNonce set on solution, false on budget/stop.
//
// Inner-loop optimisation: the 80-byte block header is serialised once and
// each worker mutates only the 4 nonce bytes per attempt -- byte-identical
// to what validation would hash but with no per-iteration serialise cost.
bool MineOnce(CBlock& block,
              const arith_uint256& target,
              const uint256& seed_key,
              std::chrono::seconds budget,
              int num_threads,
              uint64_t& out_hashes)
{
    if (num_threads < 1) num_threads = 1;

    const auto t_start = std::chrono::steady_clock::now();
    const auto t_deadline = t_start + budget;

    std::atomic<bool> found{false};
    std::atomic<uint64_t> total_hashes{0};
    std::atomic<uint32_t> winning_nonce{0};

    // Pre-serialise the block header into 80 bytes. CBlockHeader layout:
    // version(4) + prevhash(32) + merkleroot(32) + time(4) + bits(4) +
    // nonce(4); nonce starts at offset 76.
    DataStream hdr_stream;
    hdr_stream << static_cast<const CBlockHeader&>(block);
    std::vector<unsigned char> hdr_template(hdr_stream.size());
    std::memcpy(hdr_template.data(), hdr_stream.data(), hdr_stream.size());
    constexpr std::size_t NONCE_OFFSET = 76;
    assert(hdr_template.size() == 80);

    std::vector<std::thread> workers;
    workers.reserve(num_threads);
    for (int t = 0; t < num_threads; ++t) {
        workers.emplace_back([&, t]() {
            truenorth::MinerThread mt(seed_key);
            // Per-thread mutable copy of the header so we can patch the
            // nonce without coordinating with other workers.
            std::vector<unsigned char> hdr = hdr_template;
            uint64_t local_hashes = 0;

            for (uint64_t nonce = static_cast<uint64_t>(t);
                 nonce <= std::numeric_limits<uint32_t>::max();
                 nonce += static_cast<uint64_t>(num_threads)) {
                if (found.load(std::memory_order_relaxed) || g_stop.load()) break;

                const uint32_t n = static_cast<uint32_t>(nonce);
                // Little-endian write of n into the 4 nonce bytes.
                hdr[NONCE_OFFSET] = static_cast<unsigned char>(n);
                hdr[NONCE_OFFSET + 1] = static_cast<unsigned char>(n >> 8);
                hdr[NONCE_OFFSET + 2] = static_cast<unsigned char>(n >> 16);
                hdr[NONCE_OFFSET + 3] = static_cast<unsigned char>(n >> 24);

                uint256 h;
                mt.Hash(hdr.data(), hdr.size(), h);
                ++local_hashes;

                if (UintToArith256(h) <= target) {
                    bool expected = false;
                    if (found.compare_exchange_strong(expected, true,
                                                      std::memory_order_acq_rel)) {
                        winning_nonce.store(n, std::memory_order_release);
                    }
                    break;
                }

                // Cheap budget check every 256 iters per thread.
                if ((nonce & 0xff) == 0) {
                    if (std::chrono::steady_clock::now() > t_deadline) break;
                }
            }

            total_hashes.fetch_add(local_hashes, std::memory_order_relaxed);
        });
    }
    for (auto& w : workers)
        w.join();

    out_hashes = total_hashes.load(std::memory_order_relaxed);
    if (found.load(std::memory_order_acquire)) {
        block.nNonce = winning_nonce.load(std::memory_order_acquire);
        return true;
    }
    return false;
}

std::string HexBlock(const CBlock& block)
{
    DataStream s;
    s << TX_WITH_WITNESS(block);
    return HexStr(s);
}

// Raw-hashrate benchmark independent of any node or block template. Each
// worker thread holds its own RandomX VM and hashes a per-thread 80-byte
// buffer in a tight loop, varying the trailing 4 bytes per iteration so the
// hashing is genuinely fresh work (no CPU-cache freebies). Reports total
// hashes and aggregate / per-thread rate at the end.
void RunBenchmark(int num_threads, int budget_seconds)
{
    std::fprintf(stderr,
                 "benchmark: threads=%d  duration=%ds  seed=kGenesisSeed\n",
                 num_threads, budget_seconds);

    const uint256 seed = uint256::ZERO;
    const auto t_start = std::chrono::steady_clock::now();
    const auto t_deadline = t_start + std::chrono::seconds(budget_seconds);
    std::atomic<uint64_t> total{0};

    std::vector<std::thread> workers;
    workers.reserve(num_threads);
    for (int t = 0; t < num_threads; ++t) {
        workers.emplace_back([&, t]() {
            truenorth::MinerThread mt(seed);
            // Distinct per-thread prefix so workers aren't all hashing the
            // exact same input -- prevents accidentally pessimistic or
            // optimistic numbers from CPU-side micro-architectural sharing.
            std::vector<unsigned char> data(80, 0);
            data[0] = static_cast<unsigned char>(t);
            data[1] = static_cast<unsigned char>(t >> 8);
            uint64_t local = 0;
            uint32_t counter = 0;
            while (!g_stop.load(std::memory_order_relaxed) &&
                   std::chrono::steady_clock::now() < t_deadline) {
                ++counter;
                data[76] = static_cast<unsigned char>(counter);
                data[77] = static_cast<unsigned char>(counter >> 8);
                data[78] = static_cast<unsigned char>(counter >> 16);
                data[79] = static_cast<unsigned char>(counter >> 24);
                uint256 h;
                mt.Hash(data.data(), data.size(), h);
                ++local;
            }
            total.fetch_add(local, std::memory_order_relaxed);
        });
    }
    for (auto& w : workers)
        w.join();

    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - t_start)
                                .count();
    const uint64_t hashes = total.load(std::memory_order_relaxed);
    const double seconds = static_cast<double>(elapsed_ms) / 1000.0;
    const double hps = static_cast<double>(hashes) / (seconds + 1e-3);
    std::fprintf(stderr,
                 "benchmark: %llu hashes in %.2fs -> %.1f H/s aggregate (%.1f H/s/thread)\n",
                 static_cast<unsigned long long>(hashes),
                 seconds,
                 hps,
                 hps / static_cast<double>(num_threads));
}

// ---- chain helpers -------------------------------------------------------

std::string CLIChainArg(ChainType c)
{
    switch (c) {
    case ChainType::MAIN: return "";
    case ChainType::TESTNET: return "-testnet=3";
    case ChainType::TESTNET4: return "-testnet=4";
    case ChainType::SIGNET: return "-signet";
    case ChainType::REGTEST: return "-regtest";
    }
    return "";
}

} // namespace

int main(int argc, char* argv[])
{
    std::string chain_str = "main";
    std::string address;
    std::string datadir;
    std::string cli_path = "bitcoin-cli";
    std::string rpcport;
    std::string rpchost;
    int max_blocks = 0;
    int budget_seconds = 30;
    int num_threads = 1;
    bool benchmark_mode = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto eq = arg.find('=');
        if (eq == std::string::npos) Die("expected -key=value, got: " + arg);
        const std::string key = arg.substr(0, eq);
        const std::string val = arg.substr(eq + 1);
        if (key == "-chain")
            chain_str = val;
        else if (key == "-address")
            address = val;
        else if (key == "-datadir")
            datadir = val;
        else if (key == "-cli")
            cli_path = val;
        else if (key == "-rpcport")
            rpcport = val;
        else if (key == "-rpchost")
            rpchost = val;
        else if (key == "-maxblocks")
            max_blocks = std::stoi(val);
        else if (key == "-budgetseconds")
            budget_seconds = std::stoi(val);
        else if (key == "-threads")
            num_threads = std::stoi(val);
        else if (key == "-benchmark")
            benchmark_mode = (val == "1");
        else
            Die("unknown option: " + key);
    }
    if (num_threads < 1) Die("-threads must be >= 1");

    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    if (benchmark_mode) {
        // Benchmark mode skips the node + address path entirely.
        RunBenchmark(num_threads, budget_seconds);
        return 0;
    }

    if (address.empty()) Die("-address=<bech32 addr> is required");

    const std::optional<ChainType> chain_opt = ChainTypeFromString(chain_str);
    if (!chain_opt) Die("unknown -chain=" + chain_str);
    const ChainType chain = *chain_opt;

    SelectBaseParams(chain);
    SelectParams(chain);

    const CTxDestination dest = DecodeDestination(address);
    if (!IsValidDestination(dest)) {
        Die("invalid address for chain=" + chain_str + ": " + address);
    }
    const CScript pay = GetScriptForDestination(dest);

    const CliConfig cfg{cli_path, CLIChainArg(chain), datadir, rpcport, rpchost};

    std::fprintf(stderr,
                 "truenorth-miner -- chain=%s address=%s datadir=%s threads=%d maxblocks=%d budget=%ds\n",
                 chain_str.c_str(), address.c_str(),
                 datadir.empty() ? "<default>" : datadir.c_str(),
                 num_threads, max_blocks, budget_seconds);

    int blocks_found = 0;
    uint64_t extranonce = 0;
    while (!g_stop.load()) {
        UniValue tmpl = CallRPC(cfg, "getblocktemplate '{\"rules\":[\"segwit\"]}'");
        const int height = tmpl["height"].getInt<int>();
        const uint256 seed = SeedKeyForNextHeight(cfg, height);

        std::fprintf(stderr,
                     "[h=%d] prev=%s bits=%s seed=%s coinbasevalue=%lld\n",
                     height,
                     tmpl["previousblockhash"].get_str().substr(0, 12).c_str(),
                     tmpl["bits"].get_str().c_str(),
                     seed.GetHex().substr(0, 12).c_str(),
                     static_cast<long long>(tmpl["coinbasevalue"].getInt<int64_t>()));

        CBlock block = AssembleBlock(tmpl, pay, extranonce++);
        arith_uint256 target;
        target.SetCompact(block.nBits);

        uint64_t hashes = 0;
        const auto t0 = std::chrono::steady_clock::now();
        const bool found = MineOnce(block, target, seed,
                                    std::chrono::seconds(budget_seconds),
                                    num_threads, hashes);
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - t0)
                                    .count();
        const double hps = static_cast<double>(hashes) /
                           (static_cast<double>(elapsed_ms) / 1000.0 + 1e-3);

        if (!found) {
            std::fprintf(stderr,
                         "  no solution in %llds (%.1f H/s); refetching template\n",
                         static_cast<long long>(budget_seconds), hps);
            continue;
        }

        std::fprintf(stderr,
                     "  SOLUTION nonce=%u in %lldms (%.1f H/s)\n",
                     block.nNonce, static_cast<long long>(elapsed_ms), hps);

        const std::string hex = HexBlock(block);
        UniValue res = CallRPC(cfg, "submitblock " + hex);
        if (!res.isNull()) {
            std::fprintf(stderr, "  submitblock rejected: %s\n", res.get_str().c_str());
            continue;
        }
        std::fprintf(stderr, "  submitblock accepted; new tip at h=%d\n", height);
        ++blocks_found;
        if (max_blocks > 0 && blocks_found >= max_blocks) break;
    }

    std::fprintf(stderr, "truenorth-miner done -- found %d block(s)\n", blocks_found);
    return 0;
}
