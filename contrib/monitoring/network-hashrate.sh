#!/usr/bin/env bash
# network-hashrate.sh -- current hash rate + difficulty trend.
#
# Prints:
#   - current network hash rate (from getnetworkhashps)
#   - current difficulty + bits
#   - difficulty at the last N heights (default 10, at chosen spacing)
#     so a sudden spike / crash is visible in the table
#
# Small chains often sit at minimum difficulty (bits=207fffff) for long
# stretches, especially testnet with 1-2 active miners. That's normal,
# not a signal.
#
# Usage:
#   ./network-hashrate.sh [samples] [spacing_blocks]
#     samples: number of past checkpoints to sample (default 10)
#     spacing: blocks between samples (default 100)
#
# Env: CLI, CHAIN, DATADIR (see README.md)

set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
source "$DIR/_common.sh"

require_node_reachable

SAMPLES="${1:-10}"
SPACING="${2:-100}"

TIP=$(cli getblockcount)
INFO=$(cli getmininginfo)

echo "=== network hash rate ==="
echo "  tip:                    h$TIP"
printf "  networkhashps:          %s H/s\n" "$(echo "$INFO" | jq '.networkhashps')"
printf "  current difficulty:     %s\n" "$(echo "$INFO" | jq '.difficulty')"
printf "  current bits (compact): %s\n" "$(echo "$INFO" | jq -r '.bits')"

echo ""
echo "=== difficulty trend: last $SAMPLES samples spaced $SPACING blocks apart ==="
printf "  %-8s %-25s %-16s\n" "HEIGHT" "DIFFICULTY" "BITS"

for (( i = 0; i < SAMPLES; i++ )); do
    h=$(( TIP - i * SPACING ))
    if (( h < 0 )); then break; fi
    bh=$(cli getblockhash "$h")
    hdr=$(cli getblockheader "$bh")
    diff=$(echo "$hdr" | jq '.difficulty')
    bits=$(echo "$hdr" | jq -r '.bits')
    printf "  h%-7d %-25s %-16s\n" "$h" "$diff" "$bits"
done
