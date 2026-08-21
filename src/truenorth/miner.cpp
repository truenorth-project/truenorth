// Copyright (c) 2026 The TrueNorth developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

// truenorth-miner: solo CPU miner for RandomX-based TrueNorth.
//
// Talks to a running truenorthd via HTTP JSON-RPC (same libevent-based
// transport truenorth-cli uses -- no more shell-out). Auth defaults to
// the cookie file at <datadir>/<chain-subdir>/.cookie; fall back to
// -rpcuser/-rpcpassword for remote-node setups. Hashing goes through
// src/truenorth/randomx_wrapper.
//
// Workers stride the nonce space (-threads=N). Builds full block
// templates including the BIP141 witness commitment. Hash rate and
// solve time go to stderr.
//
// Also has -benchmark=1 for raw hash-rate testing without a node running.
//
// RandomX mode selection: `-mode=auto|light|fast` (default auto).
//   auto  = pick FAST when >= 2560 MiB memory is available, else LIGHT.
//   light = 256 MiB cache. Slow but small footprint (Raspberry Pi etc.).
//   fast  = ~2 GiB dataset. 5-10x throughput per thread; startup pays
//           ~5-10 s to build the dataset, plus another build each seed-
//           rotation (~every 2.84 days).
//
// Build: cmake --build build --target truenorth-miner
// Usage:
//   truenorth-miner -chain=regtest -datadir=/path/to/dd
//                   -address=bcrt1q... [-threads=N] [-maxblocks=N]
//                   [-budgetseconds=N]
//                   [-rpchost=127.0.0.1] [-rpcport=<port>]
//                   [-rpcuser=<u>] [-rpcpassword=<p>]
//                   [-mode=auto|light|fast]
//                   [-largepages=auto|on|off] [-numa=auto|on|off]
//   truenorth-miner -benchmark=1 -threads=N [-budgetseconds=N]
//                   [-mode=auto|light|fast]
//
// -datadir is required (miner reads the cookie file there).
// -cli=<path> is deprecated (was used for the shell-out design); the
//   flag is still accepted but ignored, with a warning at startup.

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
#include <rpc/request.h>
#include <script/script.h>
#include <serialize.h>
#include <streams.h>
#include <support/events.h>
#include <truenorth/numa.h>
#include <truenorth/randomx_wrapper.h>
#include <truenorth/seed_key.h>
#include <uint256.h>
#include <util/chaintype.h>
#include <util/strencodings.h>
#include <util/translation.h>

#include <event2/buffer.h>
#include <event2/http.h>
#include <event2/keyvalq_struct.h>
#include <univalue.h>

#include <filesystem>
#include <fstream>

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

// ---- HTTP JSON-RPC client ------------------------------------------------
//
// Talks directly to truenorthd over HTTP-RPC via libevent (same transport
// truenorth-cli uses; pattern lifted from truenorth-cli.cpp). No more
// popen() + parse-cli-output. Kills three classes of bug:
//
//   - CLI unquoted-string output breaking JSON parsing (the class the
//     submitblock "inconclusive" crash was in). HTTP responses are always
//     valid JSON with strings properly quoted.
//   - fork+exec cost per RPC call (~5-10 ms). Direct HTTP is a few hundred
//     microseconds.
//   - Shell-injection attack surface. There's no shell command any more.
//
// Auth: cookie file by default (single-box deployment), rpcuser/password
// fallback for remote nodes.

struct RpcConfig {
    std::string host{"127.0.0.1"};
    int port{0};             //!< 0 -> BaseParams().RPCPort()
    std::string user;        //!< empty -> use cookie
    std::string password;    //!< empty -> use cookie
    std::string cookie_path; //!< resolved from datadir + chain
};

struct HTTPReply {
    int status{0};
    int error{-1};
    std::string body;
};

void RpcHttpDone(struct evhttp_request* req, void* ctx)
{
    HTTPReply* reply = static_cast<HTTPReply*>(ctx);
    if (req == nullptr) {
        reply->status = 0;
        return;
    }
    reply->status = evhttp_request_get_response_code(req);
    struct evbuffer* buf = evhttp_request_get_input_buffer(req);
    if (buf) {
        size_t size = evbuffer_get_length(buf);
        const char* data = reinterpret_cast<const char*>(evbuffer_pullup(buf, size));
        if (data) reply->body = std::string(data, size);
        evbuffer_drain(buf, size);
    }
}

void RpcHttpError(enum evhttp_request_error err, void* ctx)
{
    static_cast<HTTPReply*>(ctx)->error = static_cast<int>(err);
}

// Read the cookie file at `path`. Cookie format is a single line
// "__cookie__:<random_password>" written by bitcoind at startup. Return
// true on success and fill `out` with the raw user:pass string ready for
// base64 encoding.
bool ReadCookieFile(const std::string& path, std::string& out)
{
    std::ifstream f(path);
    if (!f) return false;
    std::getline(f, out);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) {
        out.pop_back();
    }
    return !out.empty();
}

uint256 Uint256FromHexOrDie(const std::string& hex, const char* what)
{
    auto v = uint256::FromHex(hex);
    if (!v) Die(std::string("expected hex ") + what + ", got: " + hex);
    return *v;
}

// Single RPC call over HTTP. `method` + `params` are encoded into a
// JSON-RPC 1.0 request body; the response `result` is returned. On any
// transport / HTTP / JSON-RPC error, throws std::runtime_error with a
// clear message. Callers that want to distinguish (e.g. submitblock's
// "inconclusive" string result on rejection) inspect the returned value.
UniValue RpcCall(const RpcConfig& cfg, const std::string& method, const UniValue& params)
{
    raii_event_base base = obtain_event_base();
    raii_evhttp_connection evcon = obtain_evhttp_connection_base(base.get(), cfg.host, cfg.port);
    evhttp_connection_set_timeout(evcon.get(), 30); // seconds

    HTTPReply response;
    raii_evhttp_request req = obtain_evhttp_request(RpcHttpDone, &response);
    if (!req) throw std::runtime_error("obtain_evhttp_request failed");
    evhttp_request_set_error_cb(req.get(), RpcHttpError);

    // Auth: explicit user/password if given, else cookie file. Fail with
    // a clear pointer at the exact cookie path we tried.
    std::string user_colon_pass;
    if (!cfg.password.empty()) {
        user_colon_pass = cfg.user + ":" + cfg.password;
    } else if (!ReadCookieFile(cfg.cookie_path, user_colon_pass)) {
        throw std::runtime_error(
            "no -rpcpassword given and cookie file not readable at " + cfg.cookie_path +
            " (start truenorthd first, or pass -rpcuser/-rpcpassword for a remote node)");
    }

    struct evkeyvalq* headers = evhttp_request_get_output_headers(req.get());
    evhttp_add_header(headers, "Host", cfg.host.c_str());
    evhttp_add_header(headers, "Connection", "close");
    evhttp_add_header(headers, "Content-Type", "application/json");
    evhttp_add_header(headers, "Authorization", ("Basic " + EncodeBase64(user_colon_pass)).c_str());

    // JSON-RPC 1.0 request body.
    UniValue request_obj(UniValue::VOBJ);
    request_obj.pushKV("jsonrpc", "1.0");
    request_obj.pushKV("id", "truenorth-miner");
    request_obj.pushKV("method", method);
    request_obj.pushKV("params", params);
    const std::string body = request_obj.write() + "\n";

    struct evbuffer* output = evhttp_request_get_output_buffer(req.get());
    evbuffer_add(output, body.data(), body.size());

    const int r = evhttp_make_request(evcon.get(), req.get(), EVHTTP_REQ_POST, "/");
    req.release(); // ownership moved to evcon
    if (r != 0) throw std::runtime_error("evhttp_make_request failed");

    event_base_dispatch(base.get());

    if (response.status == 0) {
        throw std::runtime_error("could not connect to truenorthd at " + cfg.host + ":" +
                                 std::to_string(cfg.port) + " (is the daemon running?)");
    }
    if (response.status == 401) {
        throw std::runtime_error("RPC 401 Unauthorized -- bad -rpcuser/-rpcpassword or stale cookie");
    }
    if (response.status >= 400 && response.status != 500 && response.status != 404) {
        throw std::runtime_error("RPC HTTP " + std::to_string(response.status) + ": " + response.body);
    }

    UniValue reply;
    if (!reply.read(response.body)) {
        throw std::runtime_error("RPC returned unparseable JSON: " + response.body);
    }
    const UniValue& err = reply["error"];
    if (!err.isNull()) {
        const std::string msg = err["message"].isStr() ? err["message"].get_str() : err.write();
        throw std::runtime_error("RPC method " + method + " returned error: " + msg);
    }
    return reply["result"];
}

// Convenience: getblockhash returns a hex string; parse it or die.
uint256 GetBlockHashAt(const RpcConfig& cfg, int height)
{
    UniValue params(UniValue::VARR);
    params.push_back(height);
    UniValue res = RpcCall(cfg, "getblockhash", params);
    if (!res.isStr()) Die("getblockhash returned non-string: " + res.write());
    return Uint256FromHexOrDie(res.get_str(), "block hash");
}

// ---- Per-template seed key -----------------------------------------------

// Compute the RandomX seed key for the block at next_height. For the genesis
// epoch + lag window, returns kGenesisSeed. Otherwise looks up the seed
// block's hash via getblockhash RPC.
uint256 SeedKeyForNextHeight(const RpcConfig& cfg, int next_height)
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
        const int node = truenorth::numa::NodeForThread(t, num_threads);
        workers.emplace_back([&, t, node]() {
            truenorth::MinerThread mt(seed_key, node);
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

                // Cheap budget check every 256 iterations per thread.
                // MUST key off local_hashes, not nonce -- each worker
                // strides the nonce space by num_threads from its own
                // starting offset t, so `nonce & 0xff == 0` only fires
                // for threads whose offset t satisfies
                // gcd(num_threads, 256) | t. At -threads=12 that's 3
                // of 12 threads; at -threads=8 (or any power of two up
                // to 128) it's exactly 1. The other threads never
                // reach a nonce that's a multiple of 256, ignore the
                // budget entirely, and MineOnce's w.join() hangs on
                // them for the full block-solve time -- hours on hard
                // targets. local_hashes increments by 1 per iteration
                // regardless of stride, so every thread checks every
                // 256 of its own hashes.
                if ((local_hashes & 0xff) == 0) {
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
                 "benchmark: mode=%s  threads=%d  duration=%ds  largepages=%s  numa=%s(active=%s,nodes=%d)  seed=kGenesisSeed\n",
                 truenorth::ModeName(truenorth::CurrentMinerMode()),
                 num_threads, budget_seconds,
                 truenorth::LargePagesPrefName(truenorth::CurrentLargePagesPreference()),
                 truenorth::numa::PrefName(truenorth::numa::CurrentPreference()),
                 truenorth::numa::ShouldEnable() ? "yes" : "no",
                 truenorth::numa::NumNodes());

    const uint256 seed = uint256::ZERO;

    // Warm up the shared wrapper state (cache + dataset if FAST) BEFORE
    // starting the timer. Otherwise fast-mode benchmarks look artificially
    // slow because the ~5-10 s dataset build gets counted against the
    // hashing budget. In production this cost is real and one-time per
    // seed rotation; the benchmark is meant to report steady-state H/s.
    {
        const auto warmup_start = std::chrono::steady_clock::now();
        unsigned char dummy[80] = {0};
        (void)truenorth::RandomXLightHash(seed, dummy, sizeof(dummy));
        const auto warmup_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - warmup_start)
                                   .count();
        std::fprintf(stderr, "benchmark: warmup (cache%s init) took %lldms\n",
                     truenorth::CurrentMinerMode() == truenorth::RandomXMode::FAST ? " + dataset" : "",
                     static_cast<long long>(warmup_ms));
    }

    const auto t_start = std::chrono::steady_clock::now();
    const auto t_deadline = t_start + std::chrono::seconds(budget_seconds);
    std::atomic<uint64_t> total{0};

    std::vector<std::thread> workers;
    workers.reserve(num_threads);
    for (int t = 0; t < num_threads; ++t) {
        const int node = truenorth::numa::NodeForThread(t, num_threads);
        workers.emplace_back([&, t, node]() {
            truenorth::MinerThread mt(seed, node);
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

// Data-directory subdirectory used per chain for the .cookie file (and
// everything else). Matches Bitcoin Core's per-chain layout. Mainnet has
// no subdirectory.
std::string ChainDataSubdir(ChainType c)
{
    switch (c) {
    case ChainType::MAIN: return "";
    case ChainType::TESTNET: return "testnet3";
    case ChainType::TESTNET4: return "testnet4";
    case ChainType::SIGNET: return "signet";
    case ChainType::REGTEST: return "regtest";
    }
    return "";
}

// Resolve the cookie file path: <datadir>/<chain-subdir>/.cookie.
// -datadir is required (miner won't guess a platform default -- the
// cookie is read at startup and we want a loud failure over silent
// fallback if the operator forgot to point us at their node's data
// directory).
std::string ResolveCookiePath(const std::string& datadir, ChainType chain)
{
    if (datadir.empty()) {
        Die("-datadir=<path> required (miner needs it to locate the RPC cookie file)");
    }
    std::filesystem::path base(datadir);
    const std::string sub = ChainDataSubdir(chain);
    if (!sub.empty()) base /= sub;
    base /= ".cookie";
    return base.string();
}

} // namespace

int main(int argc, char* argv[])
{
    std::string chain_str = "main";
    std::string address;
    std::string datadir;
    std::string rpcport;
    std::string rpchost = "127.0.0.1";
    std::string rpcuser;     //!< optional; empty -> cookie-file auth
    std::string rpcpassword; //!< optional; empty -> cookie-file auth
    int max_blocks = 0;
    int budget_seconds = 30;
    int num_threads = 1;
    bool benchmark_mode = false;
    std::string mode_str = "auto";       //!< auto | light | fast
    std::string largepages_str = "auto"; //!< auto | on | off
    std::string numa_str = "auto";       //!< auto | on | off

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
        else if (key == "-cli") {
            // Deprecated in the HTTP-RPC refactor. Miner no longer shells
            // out to truenorth-cli; talks HTTP-RPC directly. Kept as a
            // no-op with a warning for one release cycle.
            std::fprintf(stderr,
                         "warning: -cli=%s is deprecated and ignored -- "
                         "miner now talks HTTP JSON-RPC directly to truenorthd. "
                         "Remove -cli= from your invocation.\n",
                         val.c_str());
        } else if (key == "-rpcport")
            rpcport = val;
        else if (key == "-rpchost")
            rpchost = val;
        else if (key == "-rpcuser")
            rpcuser = val;
        else if (key == "-rpcpassword")
            rpcpassword = val;
        else if (key == "-maxblocks")
            max_blocks = std::stoi(val);
        else if (key == "-budgetseconds")
            budget_seconds = std::stoi(val);
        else if (key == "-threads")
            num_threads = std::stoi(val);
        else if (key == "-benchmark")
            benchmark_mode = (val == "1");
        else if (key == "-mode")
            mode_str = val;
        else if (key == "-largepages")
            largepages_str = val;
        else if (key == "-numa")
            numa_str = val;
        else
            Die("unknown option: " + key);
    }
    if (num_threads < 1) Die("-threads must be >= 1");

    // Resolve huge-pages preference and install it before any Cache
    // is constructed. Preference must be set BEFORE SetMinerMode
    // side-effects (though in practice both are pre-startup and
    // don't collide).
    truenorth::LargePagesPref lp_pref;
    if (largepages_str == "auto") {
        lp_pref = truenorth::LargePagesPref::AUTO;
    } else if (largepages_str == "on") {
        lp_pref = truenorth::LargePagesPref::ON;
    } else if (largepages_str == "off") {
        lp_pref = truenorth::LargePagesPref::OFF;
    } else {
        Die("unknown -largepages=" + largepages_str + " (expected auto | on | off)");
    }
    truenorth::SetLargePagesPreference(lp_pref);

    // Resolve NUMA preference before any MinerThread constructs, so the
    // first alloc sees the right topology.
    truenorth::numa::NumaPref numa_pref;
    if (numa_str == "auto") {
        numa_pref = truenorth::numa::NumaPref::AUTO;
    } else if (numa_str == "on") {
        numa_pref = truenorth::numa::NumaPref::ON;
    } else if (numa_str == "off") {
        numa_pref = truenorth::numa::NumaPref::OFF;
    } else {
        Die("unknown -numa=" + numa_str + " (expected auto | on | off)");
    }
    truenorth::numa::SetPreference(numa_pref);

    // Resolve RandomX mode and install it before any MinerThread is
    // constructed (see randomx_wrapper.h SetMinerMode SAFETY note).
    truenorth::RandomXMode rx_mode;
    if (mode_str == "auto") {
        rx_mode = truenorth::AutoDetectMinerMode();
    } else if (mode_str == "light") {
        rx_mode = truenorth::RandomXMode::LIGHT;
    } else if (mode_str == "fast") {
        rx_mode = truenorth::RandomXMode::FAST;
    } else {
        Die("unknown -mode=" + mode_str + " (expected auto | light | fast)");
    }
    truenorth::SetMinerMode(rx_mode);

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

    // Resolve RPC endpoint. Port defaults to chain's RPC port (via
    // BaseParams, which was just selected). Cookie file resolved from
    // -datadir. rpcuser/rpcpassword override the cookie if given.
    RpcConfig cfg;
    cfg.host = rpchost;
    cfg.port = rpcport.empty() ? BaseParams().RPCPort() : std::stoi(rpcport);
    cfg.user = rpcuser;
    cfg.password = rpcpassword;
    cfg.cookie_path = ResolveCookiePath(datadir, chain);

    std::fprintf(stderr,
                 "truenorth-miner -- chain=%s address=%s datadir=%s rpc=%s:%d threads=%d maxblocks=%d budget=%ds mode=%s largepages=%s numa=%s(active=%s,nodes=%d)\n",
                 chain_str.c_str(), address.c_str(),
                 datadir.empty() ? "<default>" : datadir.c_str(),
                 cfg.host.c_str(), cfg.port,
                 num_threads, max_blocks, budget_seconds,
                 truenorth::ModeName(truenorth::CurrentMinerMode()),
                 truenorth::LargePagesPrefName(truenorth::CurrentLargePagesPreference()),
                 truenorth::numa::PrefName(truenorth::numa::CurrentPreference()),
                 truenorth::numa::ShouldEnable() ? "yes" : "no",
                 truenorth::numa::NumNodes());

    int blocks_found = 0;
    uint64_t extranonce = 0;
    while (!g_stop.load()) {
        // getblocktemplate takes one params-object arg specifying which
        // BIPs the miner supports. We claim segwit; the node fills in
        // the witness commitment for us.
        UniValue gbt_params(UniValue::VARR);
        UniValue rules(UniValue::VARR);
        rules.push_back("segwit");
        UniValue gbt_arg(UniValue::VOBJ);
        gbt_arg.pushKV("rules", rules);
        gbt_params.push_back(gbt_arg);
        UniValue tmpl = RpcCall(cfg, "getblocktemplate", gbt_params);
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
        // submitblock returns null on success and a bare string rejection
        // reason on failure ("inconclusive" for a valid-but-stale block,
        // "duplicate" for a resubmission, "invalid" for a validation
        // failure, "high-hash", "rejected", ...). Over HTTP-RPC the
        // string comes back as a proper JSON string value -- no
        // CLI-unquoting quirk to work around.
        UniValue submit_params(UniValue::VARR);
        submit_params.push_back(hex);
        UniValue res = RpcCall(cfg, "submitblock", submit_params);
        if (!res.isNull()) {
            const std::string reason = res.isStr() ? res.get_str() : res.write();
            std::fprintf(stderr, "  submitblock rejected: %s\n", reason.c_str());
            continue;
        }
        std::fprintf(stderr, "  submitblock accepted; new tip at h=%d\n", height);
        ++blocks_found;
        if (max_blocks > 0 && blocks_found >= max_blocks) break;
    }

    std::fprintf(stderr, "truenorth-miner done -- found %d block(s)\n", blocks_found);
    return 0;
}
