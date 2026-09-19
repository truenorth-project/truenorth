#!/usr/bin/env python3
# Copyright (c) 2026 The TrueNorth developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""RandomX proof-of-work hashing for the functional test framework.

TrueNorth checks proof of work against a RandomX hash of the 80-byte header
(light mode, RandomNorth Argon2 salt), keyed by a per-epoch seed, not against
SHA256d. The block *identity* hash stays SHA256d; only PoW changes.

Hashing goes through build/lib/libtruenorth_pow_testlib, a thin wrapper over
the node's own truenorth::RandomXLightHash, so flags and salt always match
the node. The library path comes from TRUENORTH_POW_LIB, which
BitcoinTestFramework sets from config.ini's BUILDDIR.

Seed rules mirror src/truenorth/seed_key.h: blocks below
RANDOMX_EPOCH_LENGTH + RANDOMX_SEED_LAG use GENESIS_SEED (all zeros); above
that the seed is the hash of the block at seed_height_for_height(height),
which callers must supply.
"""

import ctypes
import os

RANDOMX_EPOCH_LENGTH = 2048
RANDOMX_SEED_LAG = 64
GENESIS_SEED = bytes(32)

_lib = None


def _load():
    global _lib
    if _lib is None:
        path = os.environ.get("TRUENORTH_POW_LIB")
        if not path or not os.path.exists(path):
            raise RuntimeError(
                "RandomX test library not found (TRUENORTH_POW_LIB=%r). Build the "
                "truenorth_pow_testlib target; the test framework sets this from "
                "config.ini BUILDDIR." % path)
        lib = ctypes.CDLL(path)
        fn = lib.truenorth_test_randomx_light_hash
        fn.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p]
        fn.restype = ctypes.c_int
        _lib = lib
    return _lib


def library_path_for_builddir(builddir):
    """Return the expected path of the RandomX test library in a build dir."""
    libdir = os.path.join(builddir, "lib")
    for name in ("libtruenorth_pow_testlib.so", "libtruenorth_pow_testlib.dylib", "truenorth_pow_testlib.dll"):
        candidate = os.path.join(libdir, name)
        if os.path.exists(candidate):
            return candidate
    return os.path.join(libdir, "libtruenorth_pow_testlib.so")


def pow_hash(seed, data):
    """RandomX light hash of `data` under 32-byte `seed`; 32 bytes, uint256 internal order."""
    assert len(seed) == 32
    out = ctypes.create_string_buffer(32)
    rc = _load().truenorth_test_randomx_light_hash(bytes(seed), bytes(data), len(data), out)
    if rc != 0:
        raise RuntimeError("truenorth_test_randomx_light_hash failed (%d)" % rc)
    return out.raw


def seed_height_for_height(height):
    """Height whose block hash seeds RandomX for a block at `height` (0 = genesis seed)."""
    if height < RANDOMX_EPOCH_LENGTH + RANDOMX_SEED_LAG:
        return 0
    return ((height - RANDOMX_SEED_LAG) // RANDOMX_EPOCH_LENGTH) * RANDOMX_EPOCH_LENGTH


def seed_for_height(node, height):
    """RandomX seed for a block at `height`, looking up the seed block on `node` if needed."""
    seed_height = seed_height_for_height(height)
    if seed_height == 0:
        return GENESIS_SEED
    return bytes.fromhex(node.getblockhash(seed_height))[::-1]
