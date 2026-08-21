#!/usr/bin/env bash
# Shared helpers for the truenorth on-node monitoring scripts.
# Source this from other scripts, don't run directly.

set -euo pipefail

CLI="${CLI:-truenorth-cli}"
CHAIN="${CHAIN:-testnet4}"
DATADIR="${DATADIR:-}"
RPCPORT="${RPCPORT:-}"
RPCHOST="${RPCHOST:-}"

# Emit the -flag args truenorth-cli needs for the requested chain,
# datadir, and (optional) RPC endpoint.
cli_args() {
    local args=()
    case "$CHAIN" in
        main|mainnet)  ;;                              # no chain flag
        testnet|test3) args+=("-testnet=3") ;;
        testnet4)      args+=("-testnet4") ;;
        signet)        args+=("-signet") ;;
        regtest)       args+=("-regtest") ;;
        *) echo "unknown CHAIN=$CHAIN" >&2; exit 1 ;;
    esac
    [[ -n "$DATADIR" ]] && args+=("-datadir=$DATADIR")
    [[ -n "$RPCPORT" ]] && args+=("-rpcport=$RPCPORT")
    [[ -n "$RPCHOST" ]] && args+=("-rpcconnect=$RPCHOST")
    printf '%s\n' "${args[@]}"
}

# Wrapper that expands cli_args and runs truenorth-cli.
cli() {
    mapfile -t args < <(cli_args)
    "$CLI" "${args[@]}" "$@"
}

# Path to the node's debug.log. Guesses standard locations if DATADIR
# isn't set. Fails loudly if we can't find one.
resolve_debug_log() {
    local base
    if [[ -n "$DATADIR" ]]; then
        base="$DATADIR"
    else
        base="$HOME/.truenorth"
    fi
    case "$CHAIN" in
        main|mainnet)  echo "$base/debug.log" ;;
        testnet|test3) echo "$base/testnet3/debug.log" ;;
        testnet4)      echo "$base/testnet4/debug.log" ;;
        signet)        echo "$base/signet/debug.log" ;;
        regtest)       echo "$base/regtest/debug.log" ;;
        *) echo "unknown CHAIN=$CHAIN" >&2; return 1 ;;
    esac
}

# Sanity check that truenorth-cli is reachable and the node is up.
require_node_reachable() {
    if ! cli getblockcount >/dev/null 2>&1; then
        echo "ERROR: truenorth-cli cannot reach the node." >&2
        echo "  CLI=$CLI CHAIN=$CHAIN DATADIR=$DATADIR" >&2
        echo "  Ensure truenorthd is running and RPC is accessible." >&2
        exit 1
    fi
}
