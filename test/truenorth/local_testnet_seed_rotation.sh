#!/usr/bin/env bash
# TrueNorth seed-key rotation validation.
#
# RandomX uses a per-epoch seed key derived from the block at
# `((next_height - LAG) / EPOCH) * EPOCH` once next_height >= EPOCH + LAG.
# For the entire genesis epoch + lag window (heights < 2112) the seed is
# the all-zero kGenesisSeed. The first rotation happens at the block
# whose next_height is 2112 -- i.e. block 2112 is the first one mined
# under the new seed (the hash of block 2048).
#
# This test mines past the boundary and verifies:
#   1. The chain advances cleanly through the boundary (no stalls, no
#      validation rejections).
#   2. truenorth-miner mining a new block after the boundary uses the
#      new (non-zero) seed -- visible in its stderr "seed=" report.
#   3. SeedKeyForChild against the post-rotation tip computes the
#      hash of block 2048, matching what the daemon used during
#      validation.
#
# Mining ~2122 regtest blocks takes a couple of seconds for the
# trivial-target hashing plus ~1-2 s for the seed-cache reinit at the
# boundary. Total wall-clock: under a minute on a developer laptop.
#
# Exit 0 on success, 1 on any failure.
#
# Tunables (env overrides):
#   BUILD          build dir relative to repo root      (default: build)
#   BLOCKS         total blocks to mine past genesis    (default: 2122)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${BUILD:-build}"
BITCOIND="$ROOT/$BUILD/bin/truenorthd"
BITCOINCLI="$ROOT/$BUILD/bin/truenorth-cli"
MINER="$ROOT/$BUILD/bin/truenorth-miner"
EPOCH=2048
LAG=64
BLOCKS="${BLOCKS:-2122}"

for bin in "$BITCOIND" "$BITCOINCLI" "$MINER"; do
    [ -x "$bin" ] || { echo "FAIL: missing or not executable: $bin"; exit 1; }
done

DD="$(mktemp -d -t tn-seedrot.XXXXXX)"

cleanup() {
    "$BITCOINCLI" -regtest -datadir="$DD" stop >/dev/null 2>&1 || true
    for _ in $(seq 1 30); do
        [ -f "$DD/regtest/bitcoind.pid" ] || break
        local pid
        pid="$(cat "$DD/regtest/bitcoind.pid" 2>/dev/null)" || break
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.5
    done
    rm -rf "$DD"
}
trap cleanup EXIT

cli() { "$BITCOINCLI" -regtest -datadir="$DD" "$@"; }

echo "== TrueNorth seed-key rotation validation =="
echo "  blocks to mine: $BLOCKS  (boundary at h=$((EPOCH + LAG)))"
echo

echo "[1/4] start bitcoind"
"$BITCOIND" -regtest -datadir="$DD" -daemon

# bitcoind -daemon hides startup errors and bitcoin-cli -rpcwait
# sometimes gives up before the cookie file exists. Poll for it.
COOKIE="$DD/regtest/.cookie"
for _ in $(seq 1 60); do
    [ -f "$COOKIE" ] && break
    sleep 1
done
if [ ! -f "$COOKIE" ]; then
    echo "FAIL: bitcoind did not create cookie within 60s"
    tail -50 "$DD/regtest/debug.log" 2>/dev/null || echo "(no debug.log)"
    exit 1
fi

cli -rpcwait createwallet ci >/dev/null
ADDR="$(cli getnewaddress)"

echo "[2/4] mine $BLOCKS blocks past genesis via generatetoaddress"
# Mine in batches so we can show progress; regtest mining is fast but
# the cache reinit at h=2112 adds a ~1-2 s pause.
START_NS="$(python3 -c 'import time; print(time.time_ns())')"
cli generatetoaddress "$BLOCKS" "$ADDR" >/dev/null
END_NS="$(python3 -c 'import time; print(time.time_ns())')"
ELAPSED_S="$(python3 -c "print((${END_NS}-${START_NS})/1e9)")"
echo "  mined in ${ELAPSED_S}s"
HEIGHT="$(cli getblockcount)"
echo "  blockcount: $HEIGHT  (expect $BLOCKS)"
[ "$HEIGHT" = "$BLOCKS" ] || { echo "FAIL: height mismatch"; exit 1; }

echo "[3/4] confirm boundary blocks exist + grab seed block hash"
H_SEED=$EPOCH                  # block 2048 is the seed source
H_FIRST_ROT=$((EPOCH + LAG))   # block 2112 is the first one under the new seed
SEED_HASH="$(cli getblockhash $H_SEED)"
ROT_HASH="$(cli getblockhash $H_FIRST_ROT)"
echo "  block $H_SEED hash:        $SEED_HASH"
echo "  block $H_FIRST_ROT hash:        $ROT_HASH"
echo "  tip hash:               $(cli getbestblockhash)"

echo "[4/4] mine 1 more block via truenorth-miner and confirm post-rotation seed"
ADDR2="$(cli getnewaddress)"
MINER_OUT="$("$MINER" -chain=regtest -datadir="$DD" -cli="$BITCOINCLI" \
                     -address="$ADDR2" -maxblocks=1 -budgetseconds=60 2>&1)"
echo "$MINER_OUT" | sed 's/^/  miner: /'

# The miner's stderr [h=NNNN] line shows the seed prefix; pull it out.
MINER_SEED="$(echo "$MINER_OUT" | grep -oE 'seed=[0-9a-f]+' | head -1 | sed 's/seed=//')"
EXPECTED_SEED_PREFIX="$(echo "$SEED_HASH" | head -c 12)"
echo
echo "  miner-reported seed prefix:  $MINER_SEED"
echo "  expected (h=$H_SEED prefix):    $EXPECTED_SEED_PREFIX"
echo "  zero-prefix would be:        000000000000"

NEW_HEIGHT="$(cli getblockcount)"
[ "$NEW_HEIGHT" = "$((BLOCKS + 1))" ] || { echo "FAIL: did not extend chain"; exit 1; }
[ "$MINER_SEED" = "$EXPECTED_SEED_PREFIX" ] || { echo "FAIL: miner used wrong seed"; exit 1; }
[ "$MINER_SEED" != "000000000000" ] || { echo "FAIL: miner still using genesis seed"; exit 1; }

echo
echo "PASS: chain crossed boundary cleanly; miner uses block-$H_SEED hash as seed key"
