#!/usr/bin/env bash
#
# Copyright (c) 2026 The TrueNorth developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Run every Boost unit suite in its own process and compare the set of failing
# suites against a committed baseline.
#
# Why per-suite rather than one test_bitcoin run: a suite that aborts takes the
# whole process with it, and an abort also leaves gArgs populated so every
# later suite dies on a duplicate-AddArg assertion. One run therefore reports
# a single failure and hides everything behind it. Running each suite
# separately gives an honest count.
#
# Why a baseline rather than a skip list: the baseline is compared in both
# directions. A new failure fails CI, and so does a suite that starts passing,
# which forces the baseline to shrink as things get fixed instead of quietly
# rotting. When the list reaches zero this script can be replaced with a plain
# test_bitcoin run.

set -uo pipefail

TEST_BIN="${1:-build/bin/test_bitcoin}"
BASELINE="${2:-test/ci-known-failing-suites.txt}"

if [ ! -x "$TEST_BIN" ]; then
    echo "error: test binary not found or not executable: $TEST_BIN" >&2
    exit 2
fi
if [ ! -f "$BASELINE" ]; then
    echo "error: baseline not found: $BASELINE" >&2
    exit 2
fi

mapfile -t suites < <("$TEST_BIN" --list_content 2>&1 | grep -E '^[a-z_0-9]+\*?$' | sed 's/\*$//')
if [ "${#suites[@]}" -eq 0 ]; then
    echo "error: no suites discovered -- has --list_content output changed?" >&2
    exit 2
fi

actual="$(mktemp)"
expected="$(mktemp)"
trap 'rm -f "$actual" "$expected"' EXIT

pass=0
fail=0
for suite in "${suites[@]}"; do
    if out="$("$TEST_BIN" --run_test="$suite" 2>&1)" && grep -q "No errors detected" <<<"$out"; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "$suite" >> "$actual"
        printf '  %-40s %s\n' "$suite" \
            "$(sed 's/\x1b\[[0-9;]*m//g' <<<"$out" | grep -m1 -E 'error: in|fatal error: in' | cut -c1-120)"
    fi
done
sort -o "$actual" "$actual" 2>/dev/null || : > "$actual"

grep -vE '^\s*(#|$)' "$BASELINE" | sort > "$expected"

echo
echo "suites: $pass passed, $fail failed"

new_failures="$(comm -23 "$actual" "$expected")"
now_passing="$(comm -13 "$actual" "$expected")"

status=0
if [ -n "$new_failures" ]; then
    echo
    echo "REGRESSION -- these suites are failing and are not in the baseline:"
    sed 's/^/  /' <<<"$new_failures"
    status=1
fi
if [ -n "$now_passing" ]; then
    echo
    echo "These baseline suites now PASS. Remove them from $BASELINE:"
    sed 's/^/  /' <<<"$now_passing"
    status=1
fi
if [ "$status" -eq 0 ]; then
    echo "Failing set matches the baseline exactly."
fi
exit "$status"
