#!/usr/bin/env bash
# reorg-monitor.sh -- long-running tail on debug.log for reorg + attack signals.
#
# Watches the node's debug.log for:
#   - [CRIT] "bad-fork-prior-to-checkpoint" -- hardcoded checkpoint
#     enforcement rejecting a block. Should never fire in normal ops;
#     if it does, an attacker is publishing an alternate history and
#     our checkpoint pinned it out.
#   - [WARN] "refusing reorg of depth" -- the 72-block reorg-depth cap
#     rejected a chain that would have rewritten too much history.
#     Attempted deep-reorg attack, refused by policy.
#   - [WARN] "InvalidChainFound" -- consensus rejected a chain. Could
#     be attacker's bad chain, or a consensus bug on peers.
#   - [INFO] UpdateTip line where the new best height is STRICTLY LESS
#     than the previous tip's height. This is a real reorg (Bitcoin
#     chose a shorter-but-more-work chain), which happens naturally
#     under variable difficulty. Bitcoin Core's modern log format
#     doesn't emit a dedicated "Reorganize:" line, so we detect
#     reorgs by watching UpdateTip's height field.
#
# Reorgs where the new tip is at a HIGHER height than the old tip
# (e.g. attacker builds a longer chain, or we accept a competing
# extension) are not detected by this height-only heuristic. Those
# would need parent-hash tracking, which requires querying the block
# index. For the small-chain / mostly-testnet threat model this
# script is aimed at, the "height went backward" signal catches the
# concerning cases; horizontal 1-block reorgs are low-signal.
#
# Prints one line per event, prefixed with severity + local
# timestamp. Suitable for piping into local alerting (email,
# notify-send, ntfy.sh, systemd journal). Runs forever until
# interrupted.
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
echo "  [CRIT] hardcoded-checkpoint conflict"
echo "  [WARN] deep-reorg refused OR invalid chain found"
echo "  [INFO] real reorg (tip height went backward)"
echo ""

# tail -F handles log rotation (bitcoind rotates debug.log around 10MB).
# grep pre-filters lines so the awk pass only sees interesting ones.
# awk classifies + tracks last_height for reorg detection.
tail -n 0 -F "$LOG" 2>/dev/null | \
    grep -E --line-buffered \
        'refusing reorg of depth|bad-fork-prior-to-checkpoint|InvalidChainFound|UpdateTip: new best=' | \
    awk '
    BEGIN { last_h = -1 }
    /refusing reorg of depth/      { emit("WARN", $0); next }
    /bad-fork-prior-to-checkpoint/ { emit("CRIT", $0); next }
    /InvalidChainFound/            { emit("WARN", $0); next }
    /UpdateTip/ {
        if (match($0, /height=[0-9]+/)) {
            h = substr($0, RSTART+7, RLENGTH-7) + 0
            if (last_h >= 0 && h < last_h) {
                emit("INFO", $0 "  [REORG: " last_h " -> " h ", " (last_h - h) " blocks disconnected]")
            }
            last_h = h
        }
        next
    }
    function emit(sev, line) {
        ts = strftime("%Y-%m-%dT%H:%M:%S%z")
        printf "[%s] [%s] %s\n", sev, ts, line
        fflush()
    }'
