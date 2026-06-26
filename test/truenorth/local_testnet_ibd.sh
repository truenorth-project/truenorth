#!/usr/bin/env bash
# TrueNorth IBD (initial block download) validation.
#
# Mine N blocks on node A, then start a fresh node B with an empty
# datadir, peer it to A, and wait for B to download + validate the
# entire chain. Verifies the cold-cache PoW validation path (issue #4
# made this cheap; this script exercises it end-to-end at meaningful
# chain length).
#
# Exit 0 on success, 1 on any failure.
#
# Tunables (env overrides):
#   BUILD          build dir relative to repo root  (default: build)
#   BLOCKS         total blocks to mine on A         (default: 200)
#   IBD_TIMEOUT_S  wait for B to catch up           (default: 120)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${BUILD:-build}"
BITCOIND="$ROOT/$BUILD/bin/truenorthd"
BITCOINCLI="$ROOT/$BUILD/bin/truenorth-cli"

for bin in "$BITCOIND" "$BITCOINCLI"; do
    [ -x "$bin" ] || { echo "FAIL: missing or not executable: $bin"; exit 1; }
done

BLOCKS="${BLOCKS:-200}"
IBD_TIMEOUT_S="${IBD_TIMEOUT_S:-120}"

BASE_PORT=$((26000 + RANDOM % 2000))
PORTA=$BASE_PORT
RPCPORTA=$((BASE_PORT + 1))
PORTB=$((BASE_PORT + 2))
RPCPORTB=$((BASE_PORT + 3))

DDA="$(mktemp -d -t tn-ibd-A.XXXXXX)"
DDB="$(mktemp -d -t tn-ibd-B.XXXXXX)"

cleanup() {
    "$BITCOINCLI" -regtest -datadir="$DDA" -rpcport="$RPCPORTA" stop >/dev/null 2>&1 || true
    "$BITCOINCLI" -regtest -datadir="$DDB" -rpcport="$RPCPORTB" stop >/dev/null 2>&1 || true
    sleep 1
    rm -rf "$DDA" "$DDB"
}
trap cleanup EXIT

cliA() { "$BITCOINCLI" -regtest -datadir="$DDA" -rpcport="$RPCPORTA" "$@"; }
cliB() { "$BITCOINCLI" -regtest -datadir="$DDB" -rpcport="$RPCPORTB" "$@"; }

start_node() {
    local dd="$1" port="$2" rpc="$3"
    "$BITCOIND" -regtest -datadir="$dd" -daemon \
        -port="$port" -rpcport="$rpc" \
        -bind="127.0.0.1:$port" -rpcbind="127.0.0.1:$rpc" \
        -listen=1 -discover=0 -dnsseed=0 -fallbackfee=0.0002
    # bitcoind -daemon hides startup errors and bitcoin-cli -rpcwait
    # sometimes gives up before the cookie file exists. Poll for it.
    local cookie="$dd/regtest/.cookie"
    for _ in $(seq 1 60); do
        [ -f "$cookie" ] && return 0
        sleep 1
    done
    echo "FAIL: bitcoind ($dd) did not create cookie within 60s"
    echo "---- debug.log (last 50 lines) ----"
    tail -50 "$dd/regtest/debug.log" 2>/dev/null || echo "(no debug.log)"
    echo "---- end debug.log ----"
    return 1
}

echo "== TrueNorth IBD validation =="
echo "  source (A) blocks: $BLOCKS   sync timeout: ${IBD_TIMEOUT_S}s"
echo

echo "[1/4] start node A and mine $BLOCKS blocks"
start_node "$DDA" "$PORTA" "$RPCPORTA"
cliA -rpcwait createwallet a >/dev/null
ADDR_A="$(cliA getnewaddress)"
START_NS="$(python3 -c 'import time; print(time.time_ns())')"
cliA generatetoaddress "$BLOCKS" "$ADDR_A" >/dev/null
END_NS="$(python3 -c 'import time; print(time.time_ns())')"
MINE_S="$(python3 -c "print(f'{(${END_NS}-${START_NS})/1e9:.1f}')")"
A_HEIGHT="$(cliA getblockcount)"
A_TIP="$(cliA getbestblockhash)"
echo "  A mined $A_HEIGHT blocks in ${MINE_S}s"
echo "  A tip: $A_TIP"
[ "$A_HEIGHT" = "$BLOCKS" ] || { echo "FAIL: A height $A_HEIGHT != expected $BLOCKS"; exit 1; }

echo "[2/4] start fresh node B (empty datadir) and peer to A"
start_node "$DDB" "$PORTB" "$RPCPORTB"
cliB -rpcwait getblockchaininfo >/dev/null
[ "$(cliB getblockcount)" = "0" ] || { echo "FAIL: B not fresh"; exit 1; }
cliB addnode "127.0.0.1:$PORTA" onetry
for _ in $(seq 1 10); do
    PA="$(cliA getconnectioncount)"
    PB="$(cliB getconnectioncount)"
    [ "$PA" -ge 1 ] && [ "$PB" -ge 1 ] && break
    sleep 0.5
done
echo "  A peers: $PA  B peers: $PB"
[ "$PA" -ge 1 ] && [ "$PB" -ge 1 ] || { echo "FAIL: not peered"; exit 1; }

echo "[3/4] wait for B to sync ${BLOCKS} blocks (timeout ${IBD_TIMEOUT_S}s)"
IBD_START_NS="$(python3 -c 'import time; print(time.time_ns())')"
SYNCED_AT=""
for i in $(seq 1 "$IBD_TIMEOUT_S"); do
    B_TIP="$(cliB getbestblockhash)"
    if [ "$B_TIP" = "$A_TIP" ]; then SYNCED_AT="$i"; break; fi
    if [ $((i % 10)) = 0 ]; then
        echo "  ${i}s elapsed -- B at h=$(cliB getblockcount)"
    fi
    sleep 1
done
IBD_END_NS="$(python3 -c 'import time; print(time.time_ns())')"
IBD_S="$(python3 -c "print(f'{(${IBD_END_NS}-${IBD_START_NS})/1e9:.1f}')")"
B_HEIGHT="$(cliB getblockcount)"
B_TIP="$(cliB getbestblockhash)"
echo "  B: h=$B_HEIGHT  tip=$B_TIP   (IBD ${IBD_S}s)"

[ -n "$SYNCED_AT" ] || { echo "FAIL: B did not sync in ${IBD_TIMEOUT_S}s"; exit 1; }
[ "$B_HEIGHT" = "$A_HEIGHT" ] || { echo "FAIL: B height $B_HEIGHT != A $A_HEIGHT"; exit 1; }
[ "$B_TIP" = "$A_TIP" ] || { echo "FAIL: tips differ"; exit 1; }

echo "[4/4] sanity: B's height-1 hash matches A's"
A_H1="$(cliA getblockhash 1)"
B_H1="$(cliB getblockhash 1)"
[ "$A_H1" = "$B_H1" ] || { echo "FAIL: block 1 hash differs"; exit 1; }
echo "  block 1 matches: $A_H1"

echo
echo "PASS: B synced $BLOCKS blocks from A in ~${SYNCED_AT}s; tips and intermediate hashes match"
