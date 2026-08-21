#!/usr/bin/env bash
# block-rate.sh -- recent block-time distribution.
#
# Reads the last N blocks (default 60), computes the inter-block time
# for each, and prints summary stats (min / p25 / median / p75 / max
# in seconds) plus a histogram. Highlights LWMA-1 oscillation vs
# healthy operation.
#
# Healthy small-hash network: median ~120s (target), p75-p25 spread
# a few minutes.
# Unhealthy: multi-minute median, huge spread. Sustained deviation
# for hours warrants investigation.
#
# Usage:
#   ./block-rate.sh [N]
#
# Env: CLI, CHAIN, DATADIR (see README.md)

set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
source "$DIR/_common.sh"

require_node_reachable

N="${1:-60}"
if ! [[ "$N" =~ ^[0-9]+$ ]] || (( N < 2 )); then
    echo "usage: $0 [block_count]  (default 60; min 2)" >&2
    exit 2
fi

TIP=$(cli getblockcount)
START=$(( TIP - N + 1 ))
if (( START < 1 )); then START=1; fi   # skip genesis
ACTUAL=$(( TIP - START + 1 ))

echo "=== block-rate: blocks h${START}..h${TIP} (${ACTUAL} blocks) ==="

# Collect timestamps for [START-1 .. TIP] so we can diff.
{
    for (( h = START - 1; h <= TIP; h++ )); do
        bh=$(cli getblockhash "$h")
        t=$(cli getblockheader "$bh" | jq '.time')
        echo "$h $t"
    done
} > /tmp/br.$$.$$

# Compute inter-block deltas.
awk '
NR > 1 { print $2 - prev }
{ prev = $2 }
' /tmp/br.$$.$$ > /tmp/brd.$$.$$

# Summary stats.
sort -n /tmp/brd.$$.$$ > /tmp/brs.$$.$$
COUNT=$(wc -l < /tmp/brs.$$.$$)
MIN=$(head -1 /tmp/brs.$$.$$)
MAX=$(tail -1 /tmp/brs.$$.$$)
P25_LINE=$(( (COUNT + 3) / 4 ))
P50_LINE=$(( (COUNT + 1) / 2 ))
P75_LINE=$(( (COUNT * 3 + 3) / 4 ))
P25=$(sed -n "${P25_LINE}p" /tmp/brs.$$.$$)
P50=$(sed -n "${P50_LINE}p" /tmp/brs.$$.$$)
P75=$(sed -n "${P75_LINE}p" /tmp/brs.$$.$$)
MEAN=$(awk '{s+=$1} END { printf "%.1f", s/NR }' /tmp/brs.$$.$$)

printf "  count=%d  target=120s\n" "$COUNT"
printf "  min=%ds  p25=%ds  median=%ds  p75=%ds  max=%ds  mean=%ss\n" \
    "$MIN" "$P25" "$P50" "$P75" "$MAX" "$MEAN"

# Small histogram: buckets 0-60s, 60-120s, 120-180s, 180-300s, 300s+.
echo ""
echo "  distribution:"
awk '
{
    if ($1 < 60)       b0++
    else if ($1 < 120) b1++
    else if ($1 < 180) b2++
    else if ($1 < 300) b3++
    else               b4++
}
END {
    printf "    <60s   (fast)         %5d\n", b0+0
    printf "    60-120s              %5d\n", b1+0
    printf "    120-180s (target)    %5d\n", b2+0
    printf "    180-300s             %5d\n", b3+0
    printf "    300s+  (slow)        %5d\n", b4+0
}' /tmp/brd.$$.$$

rm -f /tmp/br.$$.$$ /tmp/brd.$$.$$ /tmp/brs.$$.$$
