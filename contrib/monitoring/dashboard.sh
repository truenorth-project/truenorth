#!/usr/bin/env bash
# dashboard.sh -- one-screen summary combining the individual monitors.
#
# Prints (in order):
#   - chain tip + basic node state
#   - block-rate stats (last 60 blocks)
#   - coinbase distribution (last 60 blocks)
#   - network hash rate + brief difficulty trend
#   - peer count
#
# Suitable for `watch -n 60 ./dashboard.sh` in a tmux pane, or as a
# cron job that mails the output daily.
#
# Env: CLI, CHAIN, DATADIR (see README.md)

set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
source "$DIR/_common.sh"

require_node_reachable

TIP=$(cli getblockcount)
CHAIN_INFO=$(cli getblockchaininfo)
NAME=$(echo "$CHAIN_INFO" | jq -r '.chain')
PEERS=$(cli getconnectioncount)
BEST_HASH=$(echo "$CHAIN_INFO" | jq -r '.bestblockhash')

echo "===================================================================="
printf "  TrueNorth node summary -- %s\n" "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "===================================================================="
printf "  chain:  %s\n" "$NAME"
printf "  tip:    h%s  (%s)\n" "$TIP" "$BEST_HASH"
printf "  peers:  %s\n" "$PEERS"
echo ""

"$DIR/block-rate.sh" 60
echo ""
"$DIR/coinbase-distribution.sh" 60
echo ""
"$DIR/network-hashrate.sh" 5 200
