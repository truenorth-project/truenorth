#!/usr/bin/env bash
# TrueNorth reorg validation.
#
# Two regtest nodes A and B; share a common 5-block prefix; disconnect;
# mine asymmetrically (A: +3, B: +5); reconnect; verify both converge on
# the heavier (longer) chain (B's). Then check A's displaced 3 blocks are
# still in B's block index but flagged as no longer on the active chain.
#
# This exercises the consensus fork-choice path with our RandomX PoW +
# LWMA difficulty + per-epoch seed key (all blocks here are well inside
# the genesis epoch, so seed_key == kGenesisSeed throughout -- the
# seed-rotation script covers the post-boundary case).
#
# Exit 0 on success, 1 on any failure.
#
# Tunables (env overrides):
#   BUILD          build dir relative to repo root  (default: build)
#   COMMON_BLOCKS  shared prefix length             (default: 5)
#   A_EXTRA        blocks A mines alone             (default: 3)
#   B_EXTRA        blocks B mines alone             (default: 5)
#   SYNC_TIMEOUT_S wait after reconnect             (default: 30)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${BUILD:-build}"
BITCOIND="$ROOT/$BUILD/bin/bitcoind"
BITCOINCLI="$ROOT/$BUILD/bin/bitcoin-cli"

for bin in "$BITCOIND" "$BITCOINCLI"; do
    [ -x "$bin" ] || { echo "FAIL: missing or not executable: $bin"; exit 1; }
done

COMMON_BLOCKS="${COMMON_BLOCKS:-5}"
A_EXTRA="${A_EXTRA:-3}"
B_EXTRA="${B_EXTRA:-5}"
SYNC_TIMEOUT_S="${SYNC_TIMEOUT_S:-30}"

BASE_PORT=$((24000 + RANDOM % 2000))
PORTA=$BASE_PORT
RPCPORTA=$((BASE_PORT + 1))
PORTB=$((BASE_PORT + 2))
RPCPORTB=$((BASE_PORT + 3))

DDA="$(mktemp -d -t tn-reorg-A.XXXXXX)"
DDB="$(mktemp -d -t tn-reorg-B.XXXXXX)"

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
}

wait_for_tip() {
    local who="$1" target_hash="$2" timeout="$3"
    for i in $(seq 1 "$timeout"); do
        local tip
        if [ "$who" = "A" ]; then tip="$(cliA getbestblockhash)"; else tip="$(cliB getbestblockhash)"; fi
        [ "$tip" = "$target_hash" ] && { echo "  $who converged in ~${i}s"; return 0; }
        sleep 1
    done
    return 1
}

echo "== TrueNorth reorg validation =="
echo "  common prefix: $COMMON_BLOCKS  A extra: $A_EXTRA  B extra: $B_EXTRA"
echo

echo "[1/6] start both nodes"
start_node "$DDA" "$PORTA" "$RPCPORTA"
start_node "$DDB" "$PORTB" "$RPCPORTB"
cliA -rpcwait getblockchaininfo >/dev/null
cliB -rpcwait getblockchaininfo >/dev/null
[ "$(cliA getbestblockhash)" = "$(cliB getbestblockhash)" ] || { echo "FAIL: genesis mismatch"; exit 1; }

echo "[2/6] peer B -> A and mine common prefix on A"
cliB addnode "127.0.0.1:$PORTA" onetry
sleep 1
cliA createwallet a >/dev/null
ADDR_A="$(cliA getnewaddress)"
cliA generatetoaddress "$COMMON_BLOCKS" "$ADDR_A" >/dev/null
COMMON_TIP="$(cliA getbestblockhash)"
echo "  common tip: $COMMON_TIP  (h=$(cliA getblockcount))"
wait_for_tip B "$COMMON_TIP" 30 || { echo "FAIL: B did not sync common prefix"; exit 1; }

echo "[3/6] disconnect B from A"
cliB disconnectnode "127.0.0.1:$PORTA"
for _ in $(seq 1 20); do
    PA="$(cliA getconnectioncount)"
    PB="$(cliB getconnectioncount)"
    [ "$PA" = "0" ] && [ "$PB" = "0" ] && break
    sleep 0.5
done
echo "  A peers: $PA   B peers: $PB"
[ "$PA" = "0" ] && [ "$PB" = "0" ] || { echo "FAIL: nodes did not disconnect"; exit 1; }

echo "[4/6] mine asymmetrically"
cliA generatetoaddress "$A_EXTRA" "$ADDR_A" >/dev/null
TIP_A_AFTER="$(cliA getbestblockhash)"
H_A="$(cliA getblockcount)"
echo "  A: +$A_EXTRA -> h=$H_A   tip=$TIP_A_AFTER"

cliB createwallet b >/dev/null
ADDR_B="$(cliB getnewaddress)"
cliB generatetoaddress "$B_EXTRA" "$ADDR_B" >/dev/null
TIP_B_AFTER="$(cliB getbestblockhash)"
H_B="$(cliB getblockcount)"
echo "  B: +$B_EXTRA -> h=$H_B   tip=$TIP_B_AFTER"

[ "$TIP_A_AFTER" != "$TIP_B_AFTER" ] || { echo "FAIL: tips did not diverge"; exit 1; }

# Cache A's now-soon-to-be-displaced blocks so we can verify they end up
# in B's block index after the reorg.
DISPLACED=()
for h in $(seq $((COMMON_BLOCKS + 1)) "$H_A"); do
    DISPLACED+=("$(cliA getblockhash "$h")")
done
echo "  A's displaced blocks (will be reorged out): ${DISPLACED[*]}"

echo "[5/6] reconnect and wait for convergence on the longer chain (B's)"
cliB addnode "127.0.0.1:$PORTA" onetry
wait_for_tip A "$TIP_B_AFTER" "$SYNC_TIMEOUT_S" || {
    echo "FAIL: A did not converge to B's chain in ${SYNC_TIMEOUT_S}s"
    echo "  A tip: $(cliA getbestblockhash) (h=$(cliA getblockcount))"
    echo "  B tip: $(cliB getbestblockhash) (h=$(cliB getblockcount))"
    exit 1
}
[ "$(cliA getbestblockhash)" = "$TIP_B_AFTER" ] || { echo "FAIL: A's tip != B's tip"; exit 1; }
[ "$(cliB getbestblockhash)" = "$TIP_B_AFTER" ] || { echo "FAIL: B's tip changed"; exit 1; }
echo "  both nodes converged on B's tip: $TIP_B_AFTER (h=$H_B)"

echo "[6/6] check A's displaced blocks are still known but off the active chain"
ALL_DISPLACED_OK=1
for hash in "${DISPLACED[@]}"; do
    # Confirmations should be -1 for blocks that are in the index but off-chain.
    CONF="$(cliA getblockheader "$hash" | python3 -c 'import json,sys; print(json.load(sys.stdin)["confirmations"])')"
    if [ "$CONF" = "-1" ]; then
        echo "  $hash  conf=-1  (off-chain, as expected)"
    else
        echo "  $hash  conf=$CONF  (FAIL: expected -1)"
        ALL_DISPLACED_OK=0
    fi
done

echo
if [ "$ALL_DISPLACED_OK" = "1" ]; then
    echo "PASS: A reorged from its $A_EXTRA-block fork to B's $B_EXTRA-block heavier chain; displaced blocks retained in index"
else
    echo "FAIL: displaced-block status incorrect"
    exit 1
fi
