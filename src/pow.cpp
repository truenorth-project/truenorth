// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>
#include <util/check.h>

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    const unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // LWMA retargets every block, so there's no 2016-block interval check.
    // Testnet min-difficulty rule still applies: if the new block is more
    // than 2x target spacing later, allow min difficulty.
    if (params.fPowAllowMinDifficultyBlocks && pblock != nullptr) {
        if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing * 2) {
            return nProofOfWorkLimit;
        }
    }

    return CalculateNextWorkRequired(pindexLast, /*nFirstBlockTime=*/0, params);
}

// Zawy's LWMA-1. Per-block retarget using a linear-weighted moving average
// of the last N solvetimes, with more weight on recent blocks. Masari and
// Wownero run this. Reference:
//   https://github.com/zawy12/difficulty-algorithms/issues/3
//
// nFirstBlockTime is unused. Kept for ABI compatibility.
unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t /*nFirstBlockTime*/, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting) {
        return pindexLast->nBits;
    }

    const arith_uint256 powLimit = UintToArith256(params.powLimit);
    const int64_t T = params.nPowTargetSpacing; // target solvetime, seconds
    const int64_t N = 90;                       // LWMA window

    // Not enough history yet. Return powLimit.
    if (pindexLast == nullptr || pindexLast->nHeight < N) {
        return powLimit.GetCompact();
    }

    int64_t weighted_solvetime_sum = 0;
    arith_uint256 sum_target = 0;
    const CBlockIndex* prev = Assert(pindexLast->GetAncestor(pindexLast->nHeight - N));
    for (int64_t i = 1; i <= N; ++i) {
        const CBlockIndex* cur = Assert(pindexLast->GetAncestor(pindexLast->nHeight - N + i));
        int64_t solvetime = cur->GetBlockTime() - prev->GetBlockTime();
        // Clamp solvetimes to tame out-of-order timestamps and extreme spikes.
        if (solvetime > 6 * T) solvetime = 6 * T;
        if (solvetime < -5 * T) solvetime = -5 * T;
        weighted_solvetime_sum += solvetime * i;

        arith_uint256 target;
        target.SetCompact(cur->nBits);
        // Accumulate as a running average to prevent overflow in wide arithmetic.
        sum_target += target / N;
        prev = cur;
    }

    const int64_t denominator = T * N * (N + 1) / 2;
    // Floor the weighted sum to avoid runaway difficulty spikes from clustered
    // negative or near-zero solvetimes.
    if (weighted_solvetime_sum < denominator / 10) {
        weighted_solvetime_sum = denominator / 10;
    }

    arith_uint256 next_target = sum_target;
    next_target *= arith_uint256(static_cast<uint64_t>(weighted_solvetime_sum));
    next_target /= arith_uint256(static_cast<uint64_t>(denominator));

    if (next_target > powLimit) {
        next_target = powLimit;
    }
    return next_target.GetCompact();
}

// With LWMA per-block retargeting, the original interval-based "permitted
// transition" check is not applicable: every block legitimately changes nBits.
// Difficulty validation still happens via CheckProofOfWork against the
// LWMA-computed target, so this is a permissive shim so that header-chain
// validation does not reject correctly-LWMA-targeted headers during IBD.
bool PermittedDifficultyTransition(const Consensus::Params& /*params*/, int64_t /*height*/, uint32_t /*old_nbits*/, uint32_t /*new_nbits*/)
{
    return true;
}

// Bypasses the actual proof of work check during fuzz testing with a simplified validation checking whether
// the most significant bit of the last byte of the hash is set.
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    if (EnableFuzzDeterminism()) return (hash.data()[31] & 0x80) == 0;
    return CheckProofOfWorkImpl(hash, nBits, params);
}

std::optional<arith_uint256> DeriveTarget(unsigned int nBits, const uint256 pow_limit)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(pow_limit))
        return {};

    return bnTarget;
}

bool CheckProofOfWorkImpl(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    auto bnTarget{DeriveTarget(nBits, params.powLimit)};
    if (!bnTarget) return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}
