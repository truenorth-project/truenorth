#!/usr/bin/env bash
# coinbase-distribution.sh -- who mined the last N blocks?
#
# Scans the last N blocks (default 100), extracts the coinbase output
# address of each, and prints a distribution table sorted by block
# count. Useful for spotting new miners joining the network and for
# estimating hash-rate distribution during incident triage.
#
# Usage:
#   ./coinbase-distribution.sh [N]
#
# Env: CLI, CHAIN, DATADIR (see README.md)

set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
source "$DIR/_common.sh"

require_node_reachable

N="${1:-100}"
if ! [[ "$N" =~ ^[0-9]+$ ]] || (( N < 1 )); then
    echo "usage: $0 [block_count]  (default 100)" >&2
    exit 2
fi

TIP=$(cli getblockcount)
START=$(( TIP - N + 1 ))
if (( START < 0 )); then
    START=0
fi
ACTUAL=$(( TIP - START + 1 ))

echo "=== coinbase distribution: blocks h${START}..h${TIP} (${ACTUAL} blocks) ==="

for (( h = START; h <= TIP; h++ )); do
    bh=$(cli getblockhash "$h")
    # verbose=2 returns full tx details; coinbase is tx[0], its first
    # output paying a value >= 100 is the miner's reward. There may be
    # a second output for the witness commitment (value 0).
    cli getblock "$bh" 2 | \
        jq -r --arg h "$h" '
          .tx[0].vout
          | map(select(.value >= 100))
          | if length > 0 then .[0].scriptPubKey.address // "unknown" else "unknown" end
          | "\($h) \(.)"
        '
done | \
awk '
{
    height=$1; addr=$2;
    count[addr]++
    if (first[addr] == "") first[addr] = height
    last[addr] = height
}
END {
    for (a in count) {
        printf "%5d  h%s-h%s  %s\n", count[a], first[a], last[a], a
    }
}' | \
sort -rn | \
awk 'BEGIN { printf "%5s  %-14s  %s\n", "COUNT", "HEIGHT RANGE", "ADDRESS" } { print }'
