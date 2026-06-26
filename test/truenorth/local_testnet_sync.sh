#!/usr/bin/env bash
# TrueNorth local-testnet validation: two regtest bitcoind nodes, peered.
#
# Validates the multi-node path that the single-node smoke test cannot
# reach:
#   - Both nodes start cleanly on regtest and share the same genesis.
#   - Node B can connect to node A over the P2P port.
#   - truenorth-miner mines blocks on node A using getblocktemplate.
#   - Node B receives and validates each of those blocks via P2P relay,
#     converging on the same tip.
#
# Exit 0 on success, 1 on any failure.
#
# Tunables (env overrides):
#   BUILD           build dir relative to repo root  (default: build)
#   BLOCKS_TO_MINE  how many blocks to mine on A     (default: 5)
#   SYNC_TIMEOUT_S  how long to wait for B to catch  (default: 30)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${BUILD:-build}"
BITCOIND="$ROOT/$BUILD/bin/truenorthd"
BITCOINCLI="$ROOT/$BUILD/bin/truenorth-cli"
MINER="$ROOT/$BUILD/bin/truenorth-miner"

for bin in "$BITCOIND" "$BITCOINCLI" "$MINER"; do
    [ -x "$bin" ] || { echo "FAIL: missing or not executable: $bin"; exit 1; }
done

BLOCKS_TO_MINE="${BLOCKS_TO_MINE:-5}"
SYNC_TIMEOUT_S="${SYNC_TIMEOUT_S:-30}"

# Pick a random-ish high port to avoid clashing with a developer's regtest.
BASE_PORT=$(( 20000 + RANDOM % 2000 ))
PORTA=$BASE_PORT
RPCPORTA=$(( BASE_PORT + 1 ))
PORTB=$(( BASE_PORT + 2 ))
RPCPORTB=$(( BASE_PORT + 3 ))

DDA="$(mktemp -d -t tn-testnet-A.XXXXXX)"
DDB="$(mktemp -d -t tn-testnet-B.XXXXXX)"

cleanup() {
    # Try a clean stop first; fall back to SIGTERM via pidfile if RPC is gone.
    "$BITCOINCLI" -regtest -datadir="$DDA" -rpcport="$RPCPORTA" stop >/dev/null 2>&1 || true
    "$BITCOINCLI" -regtest -datadir="$DDB" -rpcport="$RPCPORTB" stop >/dev/null 2>&1 || true
    sleep 1
    for pidfile in "$DDA/regtest/bitcoind.pid" "$DDB/regtest/bitcoind.pid"; do
        if [ -f "$pidfile" ]; then
            kill -TERM "$(cat "$pidfile")" 2>/dev/null || true
        fi
    done
    rm -rf "$DDA" "$DDB"
}
trap cleanup EXIT

cliA() { "$BITCOINCLI" -regtest -datadir="$DDA" -rpcport="$RPCPORTA" "$@"; }
cliB() { "$BITCOINCLI" -regtest -datadir="$DDB" -rpcport="$RPCPORTB" "$@"; }

start_node() {
    local dd="$1" port="$2" rpc="$3"
    "$BITCOIND" \
        -regtest -datadir="$dd" -daemon \
        -port="$port" -rpcport="$rpc" \
        -bind="127.0.0.1:$port" -rpcbind="127.0.0.1:$rpc" \
        -listen=1 -discover=0 -dnsseed=0 \
        -fallbackfee=0.0002
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

echo "== TrueNorth local-testnet sync test =="
echo "  build dir:       $BUILD"
echo "  node A:          port $PORTA, rpc $RPCPORTA, datadir $DDA"
echo "  node B:          port $PORTB, rpc $RPCPORTB, datadir $DDB"
echo "  blocks to mine:  $BLOCKS_TO_MINE"
echo "  sync timeout:    ${SYNC_TIMEOUT_S}s"
echo

echo "[1/5] start both nodes"
start_node "$DDA" "$PORTA" "$RPCPORTA"
start_node "$DDB" "$PORTB" "$RPCPORTB"
cliA -rpcwait getblockchaininfo >/dev/null
cliB -rpcwait getblockchaininfo >/dev/null

GENA="$(cliA getbestblockhash)"
GENB="$(cliB getbestblockhash)"
[ "$GENA" = "$GENB" ] || { echo "  FAIL: genesis differs (A=$GENA B=$GENB)"; exit 1; }
echo "  shared genesis: $GENA"

echo "[2/5] peer B -> A"
cliB addnode "127.0.0.1:$PORTA" onetry
# addnode onetry returns immediately; give libevent a moment to establish.
for _ in $(seq 1 10); do
    PA="$(cliA getconnectioncount)"
    PB="$(cliB getconnectioncount)"
    [ "$PA" -ge 1 ] && [ "$PB" -ge 1 ] && break
    sleep 0.5
done
echo "  A peers: $PA  B peers: $PB"
[ "$PA" -ge 1 ] && [ "$PB" -ge 1 ] || { echo "  FAIL: nodes did not peer"; exit 1; }

echo "[3/5] create wallet + address on A"
cliA createwallet ci >/dev/null
ADDR="$(cliA getnewaddress)"
echo "  miner address: $ADDR"

echo "[4/5] mine $BLOCKS_TO_MINE blocks on A via truenorth-miner"
# Pass through -rpcport= so bitcoin-cli inside the miner targets node A's
# non-default RPC port (bitcoin-cli's default for regtest is 18443).
"$MINER" \
    -chain=regtest -datadir="$DDA" -cli="$BITCOINCLI" -rpcport="$RPCPORTA" \
    -address="$ADDR" -maxblocks="$BLOCKS_TO_MINE" -budgetseconds=30 \
    2>&1 | sed 's/^/  miner: /'

COUNTA="$(cliA getblockcount)"
TIPA="$(cliA getbestblockhash)"
echo "  A tip after mining: h=$COUNTA $TIPA"
[ "$COUNTA" -eq "$BLOCKS_TO_MINE" ] || { echo "  FAIL: A blockcount=$COUNTA, expected $BLOCKS_TO_MINE"; exit 1; }

echo "[5/5] wait for B to sync (timeout ${SYNC_TIMEOUT_S}s)"
SYNCED_AT=""
for i in $(seq 1 "$SYNC_TIMEOUT_S"); do
    TIPB="$(cliB getbestblockhash)"
    if [ "$TIPB" = "$TIPA" ]; then
        SYNCED_AT="${i}s"
        break
    fi
    sleep 1
done
COUNTB="$(cliB getblockcount)"
TIPB="$(cliB getbestblockhash)"
echo "  B tip after wait:   h=$COUNTB $TIPB"
if [ -z "$SYNCED_AT" ]; then
    echo "  FAIL: B did not converge to A's tip within ${SYNC_TIMEOUT_S}s"
    exit 1
fi
echo "  B synced in ~$SYNCED_AT"

echo
echo "PASS: 2-node regtest sync OK ($BLOCKS_TO_MINE blocks relayed A -> B)"
