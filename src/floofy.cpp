// Copyright (c) 2015-2022 The Floofy Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/random/uniform_int.hpp>
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/discrete_distribution.hpp>
#include <boost/random/uniform_int_distribution.hpp>

#include "utilmoneystr.h"
#include "rewards.h"
#include "hash.h"       // For Hash(...)
#include "consensus/params.h"
#include "uint256.h"
#include "amount.h"
#include "random.h"
#include <vector>
#include <set>
#include "policy/policy.h"
#include "arith_uint256.h"
#include "floofy.h"
#include "txmempool.h"
#include "util.h"
#include "validation.h"
#include "floofy-fees.h"

// Floofy: Normally minimum difficulty blocks can only occur in between
// retarget blocks. However, once we introduce Digishield every block is
// a retarget, so we need to handle minimum difficulty on all blocks.
bool AllowDigishieldMinDifficultyForBlock(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    // check if the chain allows minimum difficulty blocks
    if (!params.fPowAllowMinDifficultyBlocks)
        return false;

    // check if the chain allows minimum difficulty blocks on recalc blocks
    if (pindexLast->nHeight < 157500)
    // if (!params.fPowAllowDigishieldMinDifficultyBlocks)
        return false;

    // Allow for a minimum block time if the elapsed time > 2*nTargetSpacing
    return (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2);
}

unsigned int CalculateFloofyNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    int nHeight = pindexLast->nHeight + 1;
    const int64_t retargetTimespan = params.nPowTargetTimespan;
    const int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    int64_t nModulatedTimespan = nActualTimespan;
    int64_t nMaxTimespan;
    int64_t nMinTimespan;

    if (params.fDigishieldDifficultyCalculation) //DigiShield implementation - thanks to RealSolid & WDC for this code
    {
        // amplitude filter - thanks to daft27 for this code
        nModulatedTimespan = retargetTimespan + (nModulatedTimespan - retargetTimespan) / 8;

        nMinTimespan = retargetTimespan - (retargetTimespan / 4);
        nMaxTimespan = retargetTimespan + (retargetTimespan / 2);
    } else if (nHeight > 10000) {
        nMinTimespan = retargetTimespan / 4;
        nMaxTimespan = retargetTimespan * 4;
    } else if (nHeight > 5000) {
        nMinTimespan = retargetTimespan / 8;
        nMaxTimespan = retargetTimespan * 4;
    } else {
        nMinTimespan = retargetTimespan / 16;
        nMaxTimespan = retargetTimespan * 4;
    }

    // Limit adjustment step
    if (nModulatedTimespan < nMinTimespan)
        nModulatedTimespan = nMinTimespan;
    else if (nModulatedTimespan > nMaxTimespan)
        nModulatedTimespan = nMaxTimespan;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    arith_uint256 bnOld;
    bnNew.SetCompact(pindexLast->nBits);
    bnOld = bnNew;
    bnNew *= nModulatedTimespan;
    bnNew /= retargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

bool CheckAuxPowProofOfWork(const CBlockHeader& block, const Consensus::Params& params)
{
    // Genesis is special. This permits AuxPoW genesis only for the exact
    // configured genesis block hash.
    if (block.GetHash() == params.hashGenesisBlock) {
        LogPrintf("Allowing genesis block\n");
        return true;
    }

    // Strict chain ID check for non-genesis blocks.
    if (!block.IsLegacy() && params.fStrictChainId &&
        block.GetChainId() != params.nAuxpowChainId) {
        return error("%s : block does not have our chain ID"
                     " (got %d, expected %d, full nVersion %d)",
                     __func__,
                     block.GetChainId(),
                     params.nAuxpowChainId,
                     block.nVersion);
    }

    // Non-AuxPoW block.
    if (!block.auxpow) {
        if (block.IsAuxpow())
            return error("%s : no auxpow on block with auxpow version", __func__);

        if (!CheckProofOfWork(block.GetPoWHash(), block.nBits, params))
            return error("%s : non-AUX proof of work failed", __func__);

        return true;
    }

    // AuxPoW block.
    if (!block.IsAuxpow())
        return error("%s : auxpow on block with non-auxpow version", __func__);

    if (!CheckProofOfWork(block.auxpow->getParentBlockPoWHash(), block.nBits, params))
        return error("%s : AUX proof of work failed", __func__);

    if (!block.auxpow->check(block.GetHash(), block.GetChainId(), params))
        return error("%s : AUX POW is not valid", __func__);

    return true;
}
static const int N_BLOCK_HISTORY = 10;

// Multiplier Intervals
const std::vector<std::tuple<int, int, int>> rewardIntervals = {
    {370, 400, 2},
    {450, 500, 5},
    {10000, 15000, 2},
    {20000, 24999, 2},
    {40000, 44999, 4},
    {60000, 64999, 6},
    {80000, 84999, 2},
    {100000, 104999, 4},
    {120000, 124999, 6},
    {140000, 144999, 2},
    {160000, 164999, 4},
    {180000, 184999, 6},
    {200000, 204999, 2},
    {220000, 224999, 4},
    {240000, 244999, 6},
    {260000, 264999, 2},
    {280000, 284999, 4},
    {300000, 304999, 6},
    {320000, 324999, 2},
    {340000, 344999, 4},
    {360000, 364999, 6},
    {380000, 384999, 2},
    {400000, 404999, 4},
    {420000, 424999, 6},
    {440000, 444999, 2},
    {460000, 464999, 4},
    {480000, 484999, 6},
    {500000, 504999, 2}
};


CAmount GetFloofyBlockSubsidy(int nHeight, const Consensus::Params& consensusParams, const uint256& prevHash) {
    int multiplier = 1;

    for (const auto& interval : rewardIntervals) {
        int start = std::get<0>(interval);
        int end = std::get<1>(interval);
        int m = std::get<2>(interval);
        if (nHeight >= start && nHeight <= end) {
            multiplier = m;
            break;
        }
    }

    const auto& rewardList = GetRewardTable(nHeight);

    uint256 seedHash = Hash(prevHash.begin(), prevHash.end(),
                            reinterpret_cast<const unsigned char*>(&nHeight),
                            reinterpret_cast<const unsigned char*>(&nHeight) + sizeof(nHeight));

    uint64_t seed = seedHash.GetUint64(0);
    boost::random::mt19937 rng(seed);
    boost::random::uniform_int_distribution<> dist(0, rewardList.size() - 1);
    int selectedIndex = dist(rng);

    std::set<int> recentRewardIndexes;
    for (int offset = 1; offset <= N_BLOCK_HISTORY && nHeight >= offset; ++offset) {
        int pastHeight = nHeight - offset;
        uint256 pastSeedHash = Hash(prevHash.begin(), prevHash.end(),
                                    reinterpret_cast<const unsigned char*>(&pastHeight),
                                    reinterpret_cast<const unsigned char*>(&pastHeight) + sizeof(pastHeight));
        uint64_t pastSeed = pastSeedHash.GetUint64(0);
        boost::random::mt19937 pastRng(pastSeed);
        boost::random::uniform_int_distribution<> pastDist(0, rewardList.size() - 1);
        int pastIndex = pastDist(pastRng);

        while (recentRewardIndexes.count(pastIndex)) {
            pastIndex = (pastIndex + 1) % rewardList.size();
        }

        recentRewardIndexes.insert(pastIndex);
    }

    while (recentRewardIndexes.count(selectedIndex)) {
        selectedIndex = (selectedIndex + 1) % rewardList.size();
    }

    CAmount blockReward = rewardList[selectedIndex] * multiplier;

    //LogPrintf("Block reward: %s (Index: %d, Multiplier: %d, Height: %d)\n",
              //FormatMoney(blockReward), selectedIndex, multiplier, nHeight);

    return blockReward;
}




