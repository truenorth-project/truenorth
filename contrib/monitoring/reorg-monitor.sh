#!/usr/bin/env bash
# reorg-monitor.sh -- long-running tail on debug.log for reorg + attack signals.
#
# Watches the node's debug.log for:
#   - "block-connect" reorgs (normal short reorgs, log INFO-level)
#   - "refusing reorg of depth" from the max_reorg_depth cap (WARN)
#   - "bad-fork-prior-to-checkpoint" from checkpoint enforcement (CRIT)
#   - "InvalidChainFound" for chains that failed consensus (WARN)
#
# Prints one line per event, prefixed with severity. Suitable for
# piping into a local alerting mechanism (email, notify-send,
# ntfy.sh, systemd journal). Runs forever until interrupted.
#
# Env: CLI, CHAIN, DATADIR (see README.md)

set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
source "$DIR/_common.sh"

LOG=$(resolve_debug_log)
if [[ ! -r "$LOG" ]]; then
    echo "ERROR: cannot read debug.log at $LOG" >&2
    echo "  Set DATADIR to the node's data directory." >&2
    exit 1
fi

echo "=== reorg-monitor watching $LOG ==="
echo "(prefix: [CRIT] hardcoded-checkpoint conflict; [WARN] deep-reorg refused or"
echo " invalid chain found; [INFO] normal short reorg)"
echo ""

# tail -F handles log rotation (bitcoind rotates debug.log at ~10MB).
# grep -E matches any of our patterns; the awk pass rewrites each line
# with a severity prefix and a timestamp.
tail -n 0 -F "$LOG" 2>/dev/null | \
    grep -E --line-buffered 'refusing reorg of depth|bad-fork-prior-to-checkpoint|InvalidChainFound|Reorganize:|SetBestChain: new best' | \
    awk '{
        ts = strftime("%Y-%m-%dT%H:%M:%S%z");
        sev = "INFO";
        if ($0 ~ /bad-fork-prior-to-checkpoint/)  sev = "CRIT";
        else if ($0 ~ /refusing reorg of depth/)  sev = "WARN";
        else if ($0 ~ /InvalidChainFound/)        sev = "WARN";
        printf "[%s] [%s] %s\n", sev, ts, $0
        fflush()
    }'
