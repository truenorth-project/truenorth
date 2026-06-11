#!/usr/bin/env bash
# TrueNorth multi-tx mining validation: proves the miner's block-assembly
# path correctly includes template transactions and computes the BIP141
# witness commitment from scratch (it can no longer rely on the template's
# default_witness_commitment, which is only valid for empty blocks).
#
# Sequence:
#   1. Start a regtest bitcoind with -fallbackfee enabled (regtest has no
#      fee history, so sendtoaddress would otherwise fail).
#   2. Mine 101 blocks via internal generatetoaddress so the first
#      coinbase output matures and becomes spendable.
#   3. sendtoaddress to put a real tx in the mempool.
#   4. Run truenorth-miner once.
#   5. Verify the new block contains exactly the coinbase + the
#      sendtoaddress tx, that bitcoind accepted it (so the witness
#      commitment validates), and that the mempool drained to zero.
#
# Exit 0 on success, 1 on any failure.
#
# Tunables (env overrides):
#   BUILD     build dir relative to repo root  (default: build)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${BUILD:-build}"
BITCOIND="$ROOT/$BUILD/bin/bitcoind"
BITCOINCLI="$ROOT/$BUILD/bin/bitcoin-cli"
MINER="$ROOT/$BUILD/bin/truenorth-miner"

for bin in "$BITCOIND" "$BITCOINCLI" "$MINER"; do
    [ -x "$bin" ] || { echo "FAIL: missing or not executable: $bin"; exit 1; }
done

DD="$(mktemp -d -t tn-multitx.XXXXXX)"

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

echo "== TrueNorth multi-tx mining validation =="
echo "  datadir: $DD"
echo

echo "[1/5] start bitcoind"
"$BITCOIND" -regtest -datadir="$DD" -daemon -fallbackfee=0.0002
cli -rpcwait createwallet ci >/dev/null

echo "[2/5] mine 101 blocks (mature first coinbase)"
ADDR="$(cli getnewaddress)"
cli generatetoaddress 101 "$ADDR" >/dev/null
echo "  blockcount: $(cli getblockcount)   balance: $(cli getbalance)"

echo "[3/5] sendtoaddress -> mempool"
DEST="$(cli getnewaddress)"
TXID="$(cli sendtoaddress "$DEST" 1.5)"
echo "  txid: $TXID"
MEMPOOL_SIZE="$(cli getmempoolinfo | python3 -c 'import json,sys; print(json.load(sys.stdin)["size"])')"
[ "$MEMPOOL_SIZE" = "1" ] || { echo "  FAIL: expected mempool size 1, got $MEMPOOL_SIZE"; exit 1; }

echo "[4/5] mine 1 block via truenorth-miner"
ADDR2="$(cli getnewaddress)"
"$MINER" -chain=regtest -datadir="$DD" -cli="$BITCOINCLI" \
         -address="$ADDR2" -maxblocks=1 -budgetseconds=30 \
         2>&1 | sed 's/^/  miner: /'
NEW_HEIGHT="$(cli getblockcount)"
TIP="$(cli getbestblockhash)"

echo "[5/5] verify"
BLOCK_TX_COUNT="$(cli getblock "$TIP" 1 | python3 -c 'import json,sys; print(len(json.load(sys.stdin)["tx"]))')"
IN_BLOCK="$(cli getblock "$TIP" 1 | python3 -c "import json,sys; print('yes' if '$TXID' in json.load(sys.stdin)['tx'] else 'no')")"
MEMPOOL_AFTER="$(cli getmempoolinfo | python3 -c 'import json,sys; print(json.load(sys.stdin)["size"])')"
echo "  tip h=$NEW_HEIGHT"
echo "  tx count in block: $BLOCK_TX_COUNT  (expect 2)"
echo "  sendtoaddress tx in block? $IN_BLOCK  (expect yes)"
echo "  mempool after: $MEMPOOL_AFTER  (expect 0)"

if [ "$NEW_HEIGHT" = "102" ] && [ "$BLOCK_TX_COUNT" = "2" ] && \
   [ "$IN_BLOCK" = "yes" ] && [ "$MEMPOOL_AFTER" = "0" ]; then
    echo
    echo "PASS: miner included the wallet tx, witness commitment validated, mempool drained"
else
    echo
    echo "FAIL"
    exit 1
fi
