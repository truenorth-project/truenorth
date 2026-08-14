#!/usr/bin/env bash
# TrueNorth local-testnet P2QRH end-to-end: two regtest nodes.
#
# Validates the full P2QRH stack against real consensus + wallet:
#   - Wallet generates a bech32m-qrh address via getnewaddress.
#   - Wallet A funds a QRH output; tx accepted, mined, relayed to B.
#   - Wallet A spends the QRH UTXO to a bech32 address; SignStep produces
#     the [sig, pubkey, scheme_id] witness, VerifyWitnessProgram accepts.
#   - Node B sees and validates both transactions (consensus for QRH is
#     symmetric across nodes).
#
# Exit 0 on success, 1 on any failure.
#
# Tunables (env overrides):
#   BUILD           build dir relative to repo root  (default: build)
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

SYNC_TIMEOUT_S="${SYNC_TIMEOUT_S:-30}"

# Pick a random-ish high port to avoid clashing with a developer's regtest.
BASE_PORT=$(( 20000 + RANDOM % 2000 ))
PORTA=$BASE_PORT
RPCPORTA=$(( BASE_PORT + 1 ))
PORTB=$(( BASE_PORT + 2 ))
RPCPORTB=$(( BASE_PORT + 3 ))

DDA="$(mktemp -d -t tn-qrh-A.XXXXXX)"
DDB="$(mktemp -d -t tn-qrh-B.XXXXXX)"

cleanup() {
    "$BITCOINCLI" -regtest -datadir="$DDA" -rpcport="$RPCPORTA" stop >/dev/null 2>&1 || true
    "$BITCOINCLI" -regtest -datadir="$DDB" -rpcport="$RPCPORTB" stop >/dev/null 2>&1 || true
    sleep 1
    for pidfile in "$DDA/regtest/truenorthd.pid" "$DDB/regtest/truenorthd.pid"; do
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

wait_sync_to() {
    local target="$1"
    for _ in $(seq 1 "$SYNC_TIMEOUT_S"); do
        [ "$(cliB getbestblockhash)" = "$target" ] && return 0
        sleep 1
    done
    echo "  FAIL: B did not sync to $target within ${SYNC_TIMEOUT_S}s"
    echo "  A tip: $(cliA getbestblockhash) (h=$(cliA getblockcount))"
    echo "  B tip: $(cliB getbestblockhash) (h=$(cliB getblockcount))"
    return 1
}

mine_n() {
    local n="$1"
    "$MINER" \
        -chain=regtest -datadir="$DDA" -cli="$BITCOINCLI" -rpcport="$RPCPORTA" \
        -address="$MINER_ADDR" -maxblocks="$n" -budgetseconds=30 \
        2>&1 | tail -2 | sed 's/^/  miner: /'
}

echo "== TrueNorth P2QRH end-to-end test =="
echo "  build dir:  $BUILD"
echo "  node A:     port $PORTA, rpc $RPCPORTA, datadir $DDA"
echo "  node B:     port $PORTB, rpc $RPCPORTB, datadir $DDB"
echo

echo "[1/8] start both nodes"
start_node "$DDA" "$PORTA" "$RPCPORTA"
start_node "$DDB" "$PORTB" "$RPCPORTB"
cliA -rpcwait getblockchaininfo >/dev/null
cliB -rpcwait getblockchaininfo >/dev/null

echo "[2/8] peer B -> A"
cliB addnode "127.0.0.1:$PORTA" onetry
PA=0; PB=0
for _ in $(seq 1 10); do
    PA="$(cliA getconnectioncount)"
    PB="$(cliB getconnectioncount)"
    [ "$PA" -ge 1 ] && [ "$PB" -ge 1 ] && break
    sleep 0.5
done
[ "$PA" -ge 1 ] && [ "$PB" -ge 1 ] || { echo "  FAIL: nodes did not peer (A=$PA, B=$PB)"; exit 1; }
echo "  peered"

echo "[3/8] create wallet + get addresses"
cliA createwallet ci >/dev/null
# Miner reward goes to a bech32 (P2WPKH) address -- coinbase is spendable
# via the pre-QRH path. This test focuses on the QRH SEND-and-SPEND path,
# not QRH mining rewards specifically.
MINER_ADDR="$(cliA getnewaddress "" "bech32")"
QRH_ADDR="$(cliA getnewaddress "" "bech32m-qrh")"
DEST_ADDR="$(cliA getnewaddress "" "bech32")"
echo "  miner (bech32):   $MINER_ADDR"
echo "  qrh dest:         $QRH_ADDR"
echo "  final dest:       $DEST_ADDR"

# Sanity-check the QRH address prefix. Regtest HRP is 'bcrt' and witness
# version 2 encodes to the 'z' character in bech32m.
case "$QRH_ADDR" in
    bcrt1z*) echo "  qrh prefix ok (bcrt1z...)";;
    *) echo "  FAIL: qrh address prefix wrong: $QRH_ADDR"; exit 1;;
esac

echo "[4/8] mine 101 blocks to MINER_ADDR (coinbase maturity)"
mine_n 101
BALANCE="$(cliA getbalance)"
COUNTA="$(cliA getblockcount)"
echo "  A tip:  h=$COUNTA"
echo "  wallet balance: $BALANCE"
[ "$COUNTA" -ge 101 ] || { echo "  FAIL: expected >=101 blocks"; exit 1; }

echo "[5/8] send 10 to QRH_ADDR + mine + sync"
TXID_FUND="$(cliA sendtoaddress "$QRH_ADDR" 10.0)"
echo "  fund txid: $TXID_FUND"
mine_n 1
TIPA="$(cliA getbestblockhash)"
wait_sync_to "$TIPA"
echo "  B synced to $TIPA"

# Confirm the QRH output is now in the wallet as a spendable UTXO.
QRH_UTXO_COUNT="$(cliA listunspent 1 9999 "[\"$QRH_ADDR\"]" | grep -c '"vout"' || true)"
echo "  QRH UTXOs owned by wallet: $QRH_UTXO_COUNT"
[ "$QRH_UTXO_COUNT" -ge 1 ] || { echo "  FAIL: wallet does not see a UTXO at $QRH_ADDR"; exit 1; }

echo "[6/8] SPEND the QRH UTXO explicitly via sendtoaddress"
# Constrain coin selection to the QRH UTXO so the spend definitely
# exercises SignStep for WITNESS_V2_QRH.
QRH_UTXO="$(cliA listunspent 1 9999 "[\"$QRH_ADDR\"]" | python3 -c '
import json, sys
utxos = json.load(sys.stdin)
u = utxos[0]
print(json.dumps([{"txid": u["txid"], "vout": u["vout"]}]))
')"
# Build a raw tx spending just that UTXO. Send 5.0 to DEST, ~5.0 change
# back to wallet (subtract fee).
FEE=0.0001
CHANGE_ADDR="$(cliA getrawchangeaddress)"
RAW_TX="$(cliA createrawtransaction "$QRH_UTXO" "{\"$DEST_ADDR\": 5.0, \"$CHANGE_ADDR\": $(echo "10 - 5 - $FEE" | bc)}")"
SIGNED="$(cliA signrawtransactionwithwallet "$RAW_TX")"
SIGNED_HEX="$(echo "$SIGNED" | python3 -c 'import json,sys; d=json.load(sys.stdin); print(d["hex"])')"
COMPLETE="$(echo "$SIGNED" | python3 -c 'import json,sys; d=json.load(sys.stdin); print(d["complete"])')"
echo "  signrawtx complete: $COMPLETE"
[ "$COMPLETE" = "True" ] || { echo "  FAIL: signrawtransactionwithwallet did not fully sign"; echo "$SIGNED" | head -50; exit 1; }

TXID_SPEND="$(cliA sendrawtransaction "$SIGNED_HEX")"
echo "  spend txid: $TXID_SPEND"

echo "[7/8] mine the spend + sync"
mine_n 1
TIPA="$(cliA getbestblockhash)"
wait_sync_to "$TIPA"
echo "  B synced to $TIPA"

echo "[8/8] verify spend on both nodes + witness shape"
# gettransaction on wallet A -- A owns the tx (sent it).
CONFS_A="$(cliA gettransaction "$TXID_SPEND" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("confirmations", 0))')"
# B doesn't own the tx; look it up via the block hash we just mined.
CONFS_B="$(cliB getrawtransaction "$TXID_SPEND" true "$TIPA" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("confirmations", 0))')"
echo "  spend confirmations: A=$CONFS_A B=$CONFS_B"
[ "$CONFS_A" -ge 1 ] || { echo "  FAIL: spend not confirmed on A"; exit 1; }
[ "$CONFS_B" -ge 1 ] || { echo "  FAIL: spend not seen on B (relay + validation broke)"; exit 1; }

# Introspect the witness on the spending input: should be exactly 3
# stack items [sig(64), pubkey(32), scheme_id(1)]. Also verify the
# scheme_id byte is 0x01 (QRH_SCHEME_SCHNORR). Use B's view via block
# hash so this exercises both nodes' agreement on the witness.
WITNESS_JSON="$(cliB getrawtransaction "$TXID_SPEND" true "$TIPA" | python3 -c '
import json, sys
tx = json.load(sys.stdin)
w = tx["vin"][0].get("txinwitness", [])
print(json.dumps({"count": len(w), "sig_len": len(w[0])//2 if w else 0,
                  "pk_len":  len(w[1])//2 if len(w) > 1 else 0,
                  "scheme_id": w[2] if len(w) > 2 else None}))
')"
echo "  witness: $WITNESS_JSON"

# Assert the witness shape matches what SignStep for WITNESS_V2_QRH
# produces. Exact byte counts prove the whole signing pipeline worked.
COUNT="$(echo "$WITNESS_JSON" | python3 -c 'import json,sys; print(json.load(sys.stdin)["count"])')"
SIG_LEN="$(echo "$WITNESS_JSON" | python3 -c 'import json,sys; print(json.load(sys.stdin)["sig_len"])')"
PK_LEN="$(echo "$WITNESS_JSON" | python3 -c 'import json,sys; print(json.load(sys.stdin)["pk_len"])')"
SCHEME_HEX="$(echo "$WITNESS_JSON" | python3 -c 'import json,sys; print(json.load(sys.stdin)["scheme_id"])')"
[ "$COUNT" = "3" ]  || { echo "  FAIL: expected 3 witness items, got $COUNT"; exit 1; }
[ "$SIG_LEN" = "64" ] || { echo "  FAIL: expected 64-byte sig, got $SIG_LEN bytes"; exit 1; }
[ "$PK_LEN" = "32" ]  || { echo "  FAIL: expected 32-byte x-only pubkey, got $PK_LEN bytes"; exit 1; }
[ "$SCHEME_HEX" = "01" ] || { echo "  FAIL: expected scheme_id=01 (QRH_SCHEME_SCHNORR), got $SCHEME_HEX"; exit 1; }

echo
echo "PASS: P2QRH end-to-end -- fund, spend, relay, witness shape all OK"
