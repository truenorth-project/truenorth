#!/usr/bin/env bash
# Copyright (c) 2026 The TrueNorth developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
#
# Build a static libevent for the macOS release binaries.
#
# Homebrew's libevent is a dylib under /opt/homebrew, so binaries linked
# against it fail to start on any Mac without Homebrew, and its objects
# target the build machine's macOS version. This builds the version,
# tarball hash and patches pinned in depends/packages/libevent.mk as
# static archives for MACOSX_DEPLOYMENT_TARGET (default 13.0, matching
# OSX_MIN_VERSION in depends/hosts/darwin.mk).
#
# Usage: contrib/macdeploy/build-static-libevent.sh <install-prefix>
# Then configure with -DCMAKE_PREFIX_PATH=<install-prefix>.

set -euo pipefail

prefix=${1:?usage: $0 <install-prefix>}
min_version=${MACOSX_DEPLOYMENT_TARGET:-13.0}

top=$(cd "$(dirname "$0")/../.." && pwd)
mk="$top/depends/packages/libevent.mk"
mkvar() { sed -n "s/^\$(package)_$1=//p" "$mk"; }

version=$(mkvar version)
file="libevent-${version}.tar.gz"
url="https://github.com/libevent/libevent/releases/download/release-${version}/${file}"
sha256=$(mkvar sha256_hash)

sources="$top/depends/sources"
mkdir -p "$sources"
if [ ! -f "$sources/$file" ]; then
    curl -fsSL --retry 3 -o "$sources/$file.tmp" "$url"
    mv "$sources/$file.tmp" "$sources/$file"
fi
echo "$sha256  $sources/$file" | shasum -a 256 -c -

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
tar xzf "$sources/$file" -C "$work"
cd "$work/libevent-${version}"
for p in cmake_fixups netbsd_fixup winver_fixup; do
    patch -p1 < "$top/depends/patches/libevent/$p.patch"
done

cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$min_version" \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DEVENT__LIBRARY_TYPE=STATIC \
    -DEVENT__DISABLE_OPENSSL=ON \
    -DEVENT__DISABLE_BENCHMARK=ON \
    -DEVENT__DISABLE_SAMPLES=ON \
    -DEVENT__DISABLE_REGRESS=ON \
    -DEVENT__DISABLE_TESTS=ON
cmake --build build --parallel "$(sysctl -n hw.ncpu)"
cmake --install build
