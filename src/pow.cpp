// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"

#include "auxpow.h"
#include "arith_uint256.h"
#include "chain.h"
#include "floofy.h"
#include "primitives/block.h"
#include "uint256.h"
#include "util.h"

// Determine if the for the given block, a min difficulty setting applies
bool AllowMinDifficultyForBlock(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    // check if the chain allows minimum difficulty blocks
    if (!params.fPowAllowMinDifficultyBlocks)
        return false;

    // Floofy: Magic number at which reset protocol switches
    // check if we allow minimum difficulty at this block-height
    if (pindexLast->nHeight < 157500)
        return false;

    // Allow for a minimum block time if the elapsed time > 2*nTargetSpacing
    return (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2);
}


/**
 * Experimental fixed-anchor ASERT-style DAA using ACCEPTED parent time.
 *
 * The candidate block's nTime has NO influence on its own difficulty.
 * The first block after a total stall must still satisfy the unchanged
 * target. Only after acceptance can the algorithm respond to its delay.
 * The anchor is the last pre-activation block on this branch, so reorgs
 * use their own fixed reference.
 *
 * This still permits bounded influence via timestamps in accepted history;
 * it does not eliminate all miner-controlled time or solve no-block stalls.
 *
 * Fixed point exponent: 16 fractional bits. No floating point or
 * implementation-defined signed right shifts.
 */
static unsigned int FloofyAnchoredNextWork(
    const CBlockIndex* anchor,
    const CBlockIndex* lastInStream,
    const Consensus::Params& params,
    int64_t expectedSeconds)
{
    assert(anchor != nullptr && lastInStream != nullptr);
    assert(params.nFloofyAdaptiveHalfLife > 0);

    // Dual mode uses 600 seconds per accepted block of THIS lane,
    // not a fixed two-height distance. No candidate time is considered.
    const int64_t timeError =
        lastInStream->GetBlockTime() - anchor->GetBlockTime()
        - expectedSeconds;
    const int64_t exponent =
        (timeError * 65536) / params.nFloofyAdaptiveHalfLife;

    // floor(exponent / 65536) also when exponent is negative
    int64_t shifts = exponent / 65536;
    if (exponent < 0 && exponent % 65536)
        --shifts;
    const uint64_t frac =
        static_cast<uint64_t>(exponent - shifts * 65536);

    // BCH ASERT cubic approximation of 2^(frac / 65536).
    // The 64-bit sum does not overflow for frac in [0, 65535].
    const uint64_t factor = 65536 +
        ((195766423245049ULL * frac
         + 971821376ULL * frac * frac
         + 5127ULL * frac * frac * frac
         + (1ULL << 47)) >> 48);

    arith_uint256 target;
    target.SetCompact(anchor->nBits);
    const arith_uint256 powLimit = UintToArith256(params.powLimit);

    // Saturate before multiplying to avoid arith_uint256 wraparound.
    const arith_uint256 maxValue = ~arith_uint256(0);
    if (target > maxValue / static_cast<uint32_t>(factor))
        return powLimit.GetCompact();
    target *= static_cast<uint32_t>(factor);
    target >>= 16;

    if (shifts < 0) {
        if (shifts <= -256)
            return arith_uint256(1).GetCompact();
        target >>= static_cast<unsigned int>(-shifts);
    } else if (shifts > 0) {
        // Compare against a shifted limit BEFORE the left shift; a
        // 256-bit arithmetic overflow must never wrap the target to zero.
        if (shifts >= 256 ||
            target > (powLimit >> static_cast<unsigned int>(shifts)))
            return powLimit.GetCompact();
        target <<= static_cast<unsigned int>(shifts);
    }

    if (target == 0)
        target = arith_uint256(1);
    if (target > powLimit)
        target = powLimit;
    return target.GetCompact();
}

// Preserve the established single-stream behaviour before dual activation.
static unsigned int FloofyAdaptiveNextWork(
    const CBlockIndex* pindexLast,
    const Consensus::Params& params)
{
    assert(pindexLast != nullptr && params.nHeightEffective > 0);
    const CBlockIndex* anchor =
        pindexLast->GetAncestor(params.nHeightEffective - 1);
    assert(anchor != nullptr);
    const int64_t expectedSeconds =
        (static_cast<int64_t>(pindexLast->nHeight) - anchor->nHeight)
        * params.nPowTargetSpacing;
    return FloofyAnchoredNextWork(anchor, pindexLast, params, expectedSeconds);
}

/**
 * Consensus rolling 1000-block mining-type counts.
 *
 * Count only accepted blocks since the fork, with the first 1000 post-fork
 * blocks filling the window. Once full, discard exactly one old block when
 * adding a new one. This is O(log height), per-branch, and has no timestamp.
 */
void FloofyGetRollingWindowCounts(const CBlockIndex* parent,
                                  const Consensus::Params& params,
                                  uint32_t& native,
                                  uint32_t& auxpow)
{
    native = auxpow = 0;
    if (parent == nullptr ||
        parent->nHeight < static_cast<int>(params.nHeightEffective))
        return;

    const int FORK_PARENT = static_cast<int>(params.nHeightEffective) - 1;
    const int WINDOW_START = parent->nHeight - 1000;
    const int boundary = WINDOW_START > FORK_PARENT
        ? WINDOW_START : FORK_PARENT;
    const CBlockIndex* start = parent->GetAncestor(boundary);
    assert(start != nullptr);
    native = parent->nFloofyNativeCount - start->nFloofyNativeCount;
    auxpow = parent->nFloofyAuxpowCount - start->nFloofyAuxpowCount;
}

/**
 * Q10 linear balancing multiplier: [1, 2] on the disadvantaged/advantaged
 * lane, computed from the DIFFERENCE in the last 1000 accepted block counts.
 * An underrepresented lane receives a larger *target* (easier PoW), while
 * the majority lane's target shrinks (harder PoW).
 *
 * The 1000-block denominator ramps gradually: 1 extra block adds at most
 * 0.1% bias, not a full 2x change just after fork activation.
 * No candidate timestamps or wall-clock decisions. All integer arithmetic.
 */
unsigned int FloofyApplyRollingBalance(unsigned int baseBits,
                                       const CBlockIndex* parent,
                                       bool auxpowCandidate,
                                       const Consensus::Params& params)
{
    uint32_t native = 0, auxpow = 0;
    FloofyGetRollingWindowCounts(parent, params, native, auxpow);
    if (native == auxpow)
        return baseBits;

    const uint32_t gap = native > auxpow ? native - auxpow : auxpow - native;
    assert(gap <= 1000);
    const uint32_t factor = 1024U +
        static_cast<uint32_t>((static_cast<uint64_t>(gap) * 1024U) / 1000U);
    const bool favour = auxpowCandidate ? auxpow < native : native < auxpow;

    arith_uint256 baseTarget;
    baseTarget.SetCompact(baseBits);
    const arith_uint256 powLimit = UintToArith256(params.powLimit);
    if (favour) {
        // (baseTarget * factor) / 1024 without ever overflowing uint256.
        const arith_uint256 whole = baseTarget >> 10;
        if (whole > powLimit / arith_uint256(factor))
            return powLimit.GetCompact();
        arith_uint256 scaled = whole * factor;
        const arith_uint256 fraction(
            ((baseTarget.GetLow64() & 1023ULL) * factor) / 1024U);
        if (fraction > powLimit || scaled > powLimit - fraction)
            return powLimit.GetCompact();
        scaled += fraction;
        if (scaled == 0)
            scaled = arith_uint256(1);
        return scaled.GetCompact();
    }

    // (baseTarget * 1024) / factor; at most 2x harder.
    const arith_uint256 quotient = baseTarget / arith_uint256(factor);
    arith_uint256 scaled = quotient << 10;
    const arith_uint256 remainder = baseTarget - quotient * factor;
    scaled += (remainder.GetLow64() * 1024U) / factor;
    if (scaled == 0)
        scaled = arith_uint256(1);
    return scaled.GetCompact();
}

/** Both mining types are ALWAYS eligible. Find the most recent block
 * of the candidate's own type and its accepted post-fork block count.
 * The fork's parent is the shared anchor, regardless of block order.
 */
static unsigned int FloofyDualNextWork(
    const CBlockIndex* pindexLast,
    const CBlockHeader* pblock,
    const Consensus::Params& params)
{
    assert(pindexLast != nullptr && pblock != nullptr);
    assert(params.nHeightEffective > 0);
    const CBlockIndex* anchor =
        pindexLast->GetAncestor(params.nHeightEffective - 1);
    assert(anchor != nullptr);

    const bool auxpow = pblock->IsAuxpow();
    const CBlockIndex* lastSame = auxpow
        ? pindexLast->pFloofyLastAuxpow : pindexLast->pFloofyLastNative;
    const uint32_t count = auxpow
        ? pindexLast->nFloofyAuxpowCount : pindexLast->nFloofyNativeCount;
    // Calculate independent ASERT first; then bias its target towards
    // the underrepresented mining type. Both are always eligible.
    const unsigned int baseBits = (lastSame == nullptr || count == 0)
        ? anchor->nBits
        : FloofyAnchoredNextWork(
            anchor, lastSame, params,
            static_cast<int64_t>(count) * 2 * params.nPowTargetSpacing);
    return FloofyApplyRollingBalance(baseBits, pindexLast, auxpow, params);
}

bool FloofyAdaptiveTimestampValid(const CBlockIndex* pindexLast,
                                   const CBlockHeader* pblock,
                                   int64_t localTime)
{
    // Retain MTP and a bounded future drift. Do not require timestamps
    // strictly greater than the immediate parent: it can freeze mining
    // after a future-dated block. Full contextual time checks still apply.
    static const int64_t MAX_FUTURE_SECONDS = 5 * 60;
    return pindexLast != nullptr && pblock != nullptr
        && pblock->GetBlockTime() > pindexLast->GetMedianTimePast()
        && pblock->GetBlockTime() <= localTime + MAX_FUTURE_SECONDS;
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // Genesis block
    if (pindexLast == NULL)
        return nProofOfWorkLimit;

    // Preserve all historical DigiShield/minimum-difficulty behavior.
    if (params.fFloofyAdaptiveDifficulty) {
        if (params.fPowNoRetargeting)
            return pindexLast->nBits;
        if (params.fFloofyDualPowDifficulty)
            return FloofyDualNextWork(pindexLast, pblock, params);
        return FloofyAdaptiveNextWork(pindexLast, params);
    }

    // Floofy: Special rules for minimum difficulty blocks with Digishield
    if (AllowDigishieldMinDifficultyForBlock(pindexLast, pblock, params))
    {
        // Special difficulty rule for testnet:
        // If the new block's timestamp is more than 2* nTargetSpacing minutes
        // then allow mining of a min-difficulty block.
        return nProofOfWorkLimit;
    }

    // Only change once per difficulty adjustment interval
    bool fNewDifficultyProtocol = (pindexLast->nHeight >= 145000);
    const int64_t difficultyAdjustmentInterval = fNewDifficultyProtocol
                                                 ? 1
                                                 : params.DifficultyAdjustmentInterval();
    if ((pindexLast->nHeight+1) % difficultyAdjustmentInterval != 0)
    {
        if (params.fPowAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 10 minutes
            // then allow mining of a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Litecoin: This fixes an issue where a 51% attack can change difficulty at will.
    // Go back the full period unless it's the first retarget after genesis. Code courtesy of Art Forz
    int blockstogoback = difficultyAdjustmentInterval-1;
    if ((pindexLast->nHeight+1) != difficultyAdjustmentInterval)
        blockstogoback = difficultyAdjustmentInterval;

    // Go back by what we want to be 14 days worth of blocks
    int nHeightFirst = pindexLast->nHeight - blockstogoback;
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    return CalculateFloofyNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}
