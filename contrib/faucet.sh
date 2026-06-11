#!/usr/bin/env bash
# Operator-side TrueNorth testnet faucet.
#
# Reads a list of testnet addresses from stdin (or a file), sends each one
# a fixed amount of tNORTH from the running bitcoind's wallet, and logs
# the txid + recipient to a CSV.
#
# This is a TEMPORARY MVP. It is designed to be run BY THE OPERATOR
# against THEIR OWN wallet -- it is NOT a public-facing service. Stand
# this in front of a Google Form / email inbox / Discord channel and run
# it manually as requests come in for the first weeks. Move to a proper
# web faucet once you have an idea of demand.
#
# Usage:
#   echo "tnorth1qabc..." | contrib/faucet.sh
#   contrib/faucet.sh < addresses.txt
#   cat <(grep -oE "tnorth1[a-z0-9]+" /path/to/email_dump) | contrib/faucet.sh
#
# Tunables (env overrides):
#   AMOUNT       payout per address     (default: 100, i.e. 100 tNORTH)
#   WALLET       wallet name to send from (default: faucet)
#   CHAIN        bitcoin-cli chain arg  (default: -testnet=3)
#   BITCOINCLI   bitcoin-cli path       (default: ./build/bin/bitcoin-cli)
#   LOG          CSV log file           (default: ./faucet.log.csv)
#   DRY_RUN=1    print what would be sent without actually sending
#   MIN_INTERVAL_S  seconds between sends (default: 1, anti-flood)
#
# Safety rails:
#   - DUPLICATE CHECK: skips any address that already appears in the
#     CSV (so re-running on the same address list is idempotent). Pass
#     ALLOW_REPEATS=1 to override.
#   - BALANCE CHECK: refuses to start if wallet balance < (AMOUNT * count
#     of pending recipients). Helps catch "I forgot to fund the wallet".
#   - VALIDATION: each address is dry-checked via bitcoin-cli
#     validateaddress before sending. Invalid addresses are skipped.

set -euo pipefail

AMOUNT="${AMOUNT:-100}"
WALLET="${WALLET:-faucet}"
CHAIN="${CHAIN:--testnet=3}"
BITCOINCLI="${BITCOINCLI:-./build/bin/bitcoin-cli}"
LOG="${LOG:-./faucet.log.csv}"
MIN_INTERVAL_S="${MIN_INTERVAL_S:-1}"
DRY_RUN="${DRY_RUN:-0}"
ALLOW_REPEATS="${ALLOW_REPEATS:-0}"

cli() { "$BITCOINCLI" $CHAIN -rpcwallet="$WALLET" "$@"; }

# Sanity: binary, RPC, wallet.
if [ ! -x "$BITCOINCLI" ]; then
    echo "FAIL: $BITCOINCLI not executable (build it first)"
    exit 1
fi
if ! cli getwalletinfo > /dev/null 2>&1; then
    echo "FAIL: cannot reach wallet '$WALLET' on $CHAIN via $BITCOINCLI."
    echo "  - Is bitcoind running?"
    echo "  - Is the wallet loaded? (try: $BITCOINCLI $CHAIN loadwallet $WALLET)"
    exit 1
fi

# Read addresses from stdin.
ADDRS=()
while IFS= read -r line; do
    line="$(printf '%s' "$line" | tr -d '[:space:]')"
    [ -z "$line" ] && continue
    case "$line" in
        \#*) continue ;;
    esac
    ADDRS+=("$line")
done

if [ "${#ADDRS[@]}" -eq 0 ]; then
    echo "no addresses on stdin -- nothing to do"
    exit 0
fi
echo "received ${#ADDRS[@]} candidate address(es)"

# Filter out duplicates against the log unless ALLOW_REPEATS.
PENDING=()
SKIPPED_DUP=0
if [ "$ALLOW_REPEATS" != "1" ] && [ -f "$LOG" ]; then
    for a in "${ADDRS[@]}"; do
        if grep -q -F ",$a," "$LOG" 2>/dev/null; then
            SKIPPED_DUP=$((SKIPPED_DUP + 1))
        else
            PENDING+=("$a")
        fi
    done
else
    PENDING=("${ADDRS[@]}")
fi
echo "  $SKIPPED_DUP already paid (skipping)"
echo "  ${#PENDING[@]} to send"

if [ "${#PENDING[@]}" -eq 0 ]; then
    echo "nothing to do"
    exit 0
fi

# Balance check.
BAL=$(cli getbalance)
NEEDED=$(awk -v a="$AMOUNT" -v n="${#PENDING[@]}" 'BEGIN{printf "%.8f", a*n}')
HAVE_ENOUGH=$(awk -v b="$BAL" -v n="$NEEDED" 'BEGIN{print (b+0 >= n+0) ? "1" : "0"}')
if [ "$HAVE_ENOUGH" != "1" ]; then
    echo "FAIL: wallet balance $BAL < required $NEEDED ($AMOUNT * ${#PENDING[@]})"
    echo "  Top up the faucet wallet and re-run."
    exit 1
fi
echo "  wallet balance: $BAL  (>=  required $NEEDED) -- OK"

# Initialise the CSV header if creating fresh.
if [ ! -f "$LOG" ]; then
    echo "timestamp,address,amount,txid,status" > "$LOG"
fi

SENT=0
FAILED=0
for addr in "${PENDING[@]}"; do
    TS=$(date -u +%Y-%m-%dT%H:%M:%SZ)
    # Validate the address.
    VALID=$(cli validateaddress "$addr" 2>/dev/null \
            | python3 -c 'import json,sys; d=json.load(sys.stdin); print("1" if d.get("isvalid") else "0")' \
            2>/dev/null || echo "0")
    if [ "$VALID" != "1" ]; then
        echo "  $TS  $addr  $AMOUNT  -  INVALID_ADDRESS"
        echo "$TS,$addr,$AMOUNT,,INVALID_ADDRESS" >> "$LOG"
        FAILED=$((FAILED + 1))
        continue
    fi

    if [ "$DRY_RUN" = "1" ]; then
        echo "  $TS  $addr  $AMOUNT  -  DRY_RUN"
        continue
    fi

    if TXID=$(cli sendtoaddress "$addr" "$AMOUNT" 2>/dev/null); then
        echo "  $TS  $addr  $AMOUNT  $TXID  SENT"
        echo "$TS,$addr,$AMOUNT,$TXID,SENT" >> "$LOG"
        SENT=$((SENT + 1))
    else
        ERR=$(cli sendtoaddress "$addr" "$AMOUNT" 2>&1 | head -1 || true)
        echo "  $TS  $addr  $AMOUNT  -  ERROR: $ERR"
        echo "$TS,$addr,$AMOUNT,,\"ERROR: $ERR\"" >> "$LOG"
        FAILED=$((FAILED + 1))
    fi

    sleep "$MIN_INTERVAL_S"
done

echo
echo "summary: $SENT sent, $FAILED failed, $SKIPPED_DUP skipped (dup)."
echo "log: $LOG"

[ "$FAILED" = "0" ] && exit 0 || exit 1
