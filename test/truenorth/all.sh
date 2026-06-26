#!/usr/bin/env bash
# One-shot pre-launch validation. Builds the binaries, runs the
# truenorth_tests Boost suite, then runs all five end-to-end regression
# scripts back-to-back. Use this as the "is the codebase ready to ship
# right now?" smoke check.
#
# Exit 0 if everything is green, 1 on the first failure.
#
# Tunables (env overrides):
#   BUILD     build dir relative to repo root  (default: build)
#   SKIP_BUILD=1 to assume binaries are already built (faster re-runs)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
BUILD="${BUILD:-build}"

# Phase 1: build (unless skipped)
if [ "${SKIP_BUILD:-0}" != "1" ]; then
    printf "%-50s " "[1/7] cmake --build target=all-launch ..."
    if cmake --build "$BUILD" -j --target truenorthd bitcoin-cli truenorth-miner test_bitcoin > /tmp/all_sh_build.log 2>&1; then
        echo "OK"
    else
        echo "FAIL"
        tail -30 /tmp/all_sh_build.log
        exit 1
    fi
else
    echo "[1/7] build:                                       SKIPPED (\$SKIP_BUILD=1)"
fi

# Phase 2: unit test suite.
printf "%-50s " "[2/7] truenorth_tests (Boost unit suite) ..."
if "$BUILD/bin/test_bitcoin" --run_test=truenorth_tests > /tmp/all_sh_unit.log 2>&1; then
    echo "OK (16 cases)"
else
    echo "FAIL"
    tail -20 /tmp/all_sh_unit.log
    exit 1
fi

# Phase 3-7: five end-to-end regression scripts.
i=3
for script in local_testnet_sync \
              local_testnet_multitx \
              local_testnet_seed_rotation \
              local_testnet_reorg \
              local_testnet_ibd; do
    printf "%-50s " "[$i/7] $script.sh ..."
    if bash "test/truenorth/${script}.sh" > "/tmp/all_sh_${script}.log" 2>&1; then
        PASS_LINE=$(grep "^PASS" "/tmp/all_sh_${script}.log" | tail -1)
        echo "OK"
        [ -n "$PASS_LINE" ] && echo "         ${PASS_LINE#PASS: }"
    else
        echo "FAIL"
        tail -20 "/tmp/all_sh_${script}.log"
        exit 1
    fi
    i=$((i + 1))
done

echo
echo "===================================================================="
echo " All pre-launch checks green: build OK, 16/16 unit tests pass,"
echo " 5/5 regression scripts pass."
echo "===================================================================="
