// Copyright (c) 2015 The Bitcoin Core developers
// Copyright (c) 2018-2022 The Floofy Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chain.h"
#include "chainparams.h"
#include "primitives/block.h"
#include "pow.h"
#include "random.h"
#include "util.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(pow_tests, BasicTestingSetup)

/* Test calculation of next difficulty target with no constraints applying */
BOOST_AUTO_TEST_CASE(get_next_work)
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus(0);

    int64_t nLastRetargetTime = 1388149872; // Block #30240
    CBlockIndex pindexLast;
    pindexLast.nHeight = 30479;
    pindexLast.nTime = 1388163922; // Block #30479
    pindexLast.nBits = 0x1c00974f;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, params), 0x1c0093a1);
}

/* Test the constraint on the upper bound for next work */
BOOST_AUTO_TEST_CASE(get_next_work_pow_limit)
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus(0);

    int64_t nLastRetargetTime = 1231006505; // Block #0
    CBlockIndex pindexLast;
    pindexLast.nHeight = 2015;
    pindexLast.nTime = 1233061996;  // Block #2015
    pindexLast.nBits = 0x1d00ffff;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, params), 0x1d03fffc);
}

/* Test the constraint on the lower bound for actual time taken */
BOOST_AUTO_TEST_CASE(get_next_work_lower_limit_actual)
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus(0);

    int64_t nLastRetargetTime = 1279008237; // Block #66528
    CBlockIndex pindexLast;
    pindexLast.nHeight = 66767;
    pindexLast.nTime = 1279008237 + (239 * 60 / 4 - 1); // Bitcoin Block #66528 + less than a quarter of the target timespan
    pindexLast.nBits = 0x1c05a3f4;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, params), 0x1c0168fd);
}

/* Test the constraint on the upper bound for actual time taken */
// Replaced by Floofy-specific PoW test
/* BOOST_AUTO_TEST_CASE(get_next_work_upper_limit_actual)
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus(0);

    int64_t nLastRetargetTime = 1263163443; // NOTE: Not an actual block time
    CBlockIndex pindexLast;
    pindexLast.nHeight = 46367;
    pindexLast.nTime = 1269211443;  // Block #46367
    pindexLast.nBits = 0x1c387f6f;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, params), 0x1d00e1fd);
} */

BOOST_AUTO_TEST_CASE(GetBlockProofEquivalentTime_test)
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus(0);

    std::vector<CBlockIndex> blocks(10000);
    for (int i = 0; i < 10000; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : NULL;
        blocks[i].nHeight = i;
        blocks[i].nTime = 1269211443 + i * params.nPowTargetSpacing;
        blocks[i].nBits = 0x207fffff; /* target 0x7fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    for (int j = 0; j < 1000; j++) {
        CBlockIndex *p1 = &blocks[InsecureRandRange(10000)];
        CBlockIndex *p2 = &blocks[InsecureRandRange(10000)];
        CBlockIndex *p3 = &blocks[InsecureRandRange(10000)];

        int64_t tdiff = GetBlockProofEquivalentTime(*p1, *p2, *p3, params);
        BOOST_CHECK_EQUAL(tdiff, p1->GetBlockTime() - p2->GetBlockTime());
    }
}


BOOST_AUTO_TEST_CASE(floofy_adaptive_height_boundary_and_stalls)
{
    SelectParams(CBaseChainParams::MAIN);
    const int activation = 245000;
    const Consensus::Params& before = Params().GetConsensus(activation - 1);
    const Consensus::Params& at = Params().GetConsensus(activation);
    const Consensus::Params& after = Params().GetConsensus(activation + 1);
    BOOST_CHECK(!before.fFloofyAdaptiveDifficulty);
    BOOST_REQUIRE(at.fFloofyAdaptiveDifficulty);
    BOOST_CHECK(after.fFloofyAdaptiveDifficulty);
    BOOST_CHECK_EQUAL(at.nFloofyAdaptiveHalfLife, 3600);
    BOOST_CHECK_EQUAL(before.nPowTargetSpacing, 60);
    BOOST_CHECK_EQUAL(at.nPowTargetSpacing, 300);
    BOOST_CHECK_EQUAL(after.nPowTargetSpacing, 300);
    BOOST_CHECK_EQUAL(at.nPowTargetTimespan, 300);
    BOOST_CHECK(!at.fAuxpowLegacyRule);
    BOOST_CHECK(!before.fFloofyDualPowDifficulty);
    BOOST_CHECK(at.fFloofyDualPowDifficulty);
    BOOST_CHECK(after.fFloofyDualPowDifficulty);
    BOOST_CHECK_EQUAL(at.nAuxpowStrictValidationHeight, 245000U);

    CBlockIndex anchor;
    anchor.nHeight = activation - 1;
    anchor.nTime = 1750000000;
    anchor.nBits = 0x1b0ffff0;
    anchor.nVersion = CPureBlockHeader::VERSION_AUXPOW;
    CBlockHeader candidate;
    candidate.SetBaseVersion(4, 666);
    candidate.nTime = anchor.nTime + at.nPowTargetSpacing;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&anchor, &candidate, at), anchor.nBits);
    candidate.SetAuxpowFlag(true);
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&anchor, &candidate, at), anchor.nBits);
    candidate.SetAuxpowFlag(false);
    const unsigned int fixedBits = GetNextWorkRequired(&anchor, &candidate, at);
    BOOST_CHECK_EQUAL(fixedBits, anchor.nBits);

    // Any candidate timestamp produces the SAME target on the same tip.
    candidate.nTime = anchor.nTime + 4 * 3600 + at.nPowTargetSpacing;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&anchor, &candidate, at), fixedBits);
    candidate.nTime += 300;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&anchor, &candidate, at), fixedBits);

    // Only AFTER the stalled block is accepted can difficulty reduce.
    CBlockIndex first;
    first.pprev = &anchor;
    first.nHeight = activation;
    first.nTime = anchor.nTime + 4 * 3600 + at.nPowTargetSpacing;
    first.nBits = fixedBits;
    first.nVersion = 4;
    first.BuildFloofyMiningHistory(true);
    CBlockHeader following;
    following.SetBaseVersion(4, 666);
    following.nTime = first.nTime + after.nPowTargetSpacing;
    const unsigned int recovered = GetNextWorkRequired(&first, &following, after);
    arith_uint256 t0, t1;
    t0.SetCompact(fixedBits);
    t1.SetCompact(recovered);
    BOOST_CHECK(t1 > t0);
    BOOST_CHECK(t1 <= UintToArith256(after.powLimit));
    following.nTime += 300;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&first, &following, after), recovered);
}

BOOST_AUTO_TEST_CASE(floofy_regtest_fork_height_boundaries)
{
    SelectParams(CBaseChainParams::REGTEST);
    const CChainParams& chain = Params();
    const Consensus::Params& h19 = chain.GetConsensus(19);
    const Consensus::Params& h20 = chain.GetConsensus(20);
    const Consensus::Params& h29 = chain.GetConsensus(29);
    const Consensus::Params& h30 = chain.GetConsensus(30);
    const Consensus::Params& h31 = chain.GetConsensus(31);
    const Consensus::Params& h39 = chain.GetConsensus(39);
    const Consensus::Params& h40 = chain.GetConsensus(40);
    const Consensus::Params& h41 = chain.GetConsensus(41);

    BOOST_CHECK(h19.fAllowLegacyBlocks);
    BOOST_CHECK(!h20.fAllowLegacyBlocks);
    BOOST_CHECK(!h29.fAllowLegacyBlocks);
    BOOST_CHECK(!h30.fAllowLegacyBlocks);
    BOOST_CHECK_EQUAL(h20.nAuxpowChainId, 666);
    BOOST_CHECK_EQUAL(h30.nAuxpowChainId, 666);
    BOOST_CHECK_EQUAL(h30.nAuxpowStrictValidationHeight, 30U);
    BOOST_CHECK(h20.fAuxpowLegacyRule);
    BOOST_CHECK(h29.fAuxpowLegacyRule);
    BOOST_CHECK(!h30.fAuxpowLegacyRule);
    BOOST_CHECK(!h31.fAuxpowLegacyRule);
    BOOST_CHECK(!h39.fFloofyAdaptiveDifficulty);
    BOOST_CHECK(h39.fPowNoRetargeting);
    BOOST_REQUIRE(h40.fFloofyAdaptiveDifficulty);
    BOOST_CHECK(h41.fFloofyAdaptiveDifficulty);
    BOOST_CHECK(!h40.fAuxpowLegacyRule);
    BOOST_CHECK(!h40.fPowNoRetargeting);
    BOOST_CHECK(!h40.fPowAllowMinDifficultyBlocks);
    BOOST_CHECK(!h40.fPowAllowDigishieldMinDifficultyBlocks);
    BOOST_CHECK_EQUAL(h39.nPowTargetSpacing, 1);
    BOOST_CHECK_EQUAL(h40.nPowTargetSpacing, 300);
    BOOST_CHECK_EQUAL(h41.nPowTargetSpacing, 300);
    BOOST_CHECK_EQUAL(h40.nPowTargetTimespan, 300);
    BOOST_CHECK_EQUAL(h40.nFloofyAdaptiveHalfLife, 3600);

    CBlockIndex preAdaptive;
    preAdaptive.nHeight = 39;
    preAdaptive.nTime = 1750000000;
    preAdaptive.nBits = 0x1b0ffff0;
    CBlockHeader next;
    next.nTime = preAdaptive.nTime + h40.nPowTargetSpacing;
    const unsigned int normal = GetNextWorkRequired(&preAdaptive, &next, h40);
    next.nTime += 2 * 3600;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&preAdaptive, &next, h40), normal);

    CBlockIndex late;
    late.pprev = &preAdaptive;
    late.nHeight = 40;
    late.nTime = next.nTime;
    late.nBits = normal;
    next.nTime += h41.nPowTargetSpacing;
    arith_uint256 initialTarget, recoveredTarget;
    initialTarget.SetCompact(normal);
    recoveredTarget.SetCompact(GetNextWorkRequired(&late, &next, h41));
    BOOST_CHECK(recoveredTarget > initialTarget);

    // Mainnet uses ONE height 245000, but regtest keeps 20/30/40/50.
    SelectParams(CBaseChainParams::MAIN);
    const CChainParams& mainnet = Params();
    const Consensus::Params& pre = mainnet.GetConsensus(244999);
    const Consensus::Params& atFork = mainnet.GetConsensus(245000);
    const Consensus::Params& post = mainnet.GetConsensus(245001);
    BOOST_CHECK(pre.fAuxpowLegacyRule);
    BOOST_CHECK(!pre.fFloofyAdaptiveDifficulty);
    BOOST_CHECK(!pre.fFloofyDualPowDifficulty);
    BOOST_CHECK_EQUAL(pre.nPowTargetSpacing, 60);
    BOOST_CHECK_EQUAL(pre.nAuxpowStrictValidationHeight, 245000U);
    BOOST_CHECK(!atFork.fAuxpowLegacyRule);
    BOOST_CHECK(atFork.fFloofyAdaptiveDifficulty);
    BOOST_CHECK(atFork.fFloofyDualPowDifficulty);
    BOOST_CHECK_EQUAL(atFork.nHeightEffective, 245000U);
    BOOST_CHECK_EQUAL(atFork.nPowTargetSpacing, 300);
    BOOST_CHECK_EQUAL(atFork.nFloofyAdaptiveHalfLife, 3600);
    BOOST_CHECK_EQUAL(atFork.nAuxpowStrictValidationHeight, 245000U);
    BOOST_CHECK(post.fFloofyDualPowDifficulty);
}


BOOST_AUTO_TEST_CASE(floofy_dual_pow_fork_and_independent_targets)
{
    SelectParams(CBaseChainParams::REGTEST);
    const CChainParams& chain = Params();
    const Consensus::Params& pre = chain.GetConsensus(49);
    const Consensus::Params& dual = chain.GetConsensus(50);
    BOOST_CHECK(!pre.fFloofyDualPowDifficulty);
    BOOST_REQUIRE(dual.fFloofyDualPowDifficulty);
    BOOST_CHECK_EQUAL(dual.nHeightEffective, 50U);
    BOOST_CHECK_EQUAL(dual.nPowTargetSpacing, 300);
    BOOST_CHECK_EQUAL(dual.nFloofyAdaptiveHalfLife, 3600);

    CBlockIndex anchor;
    anchor.nHeight = 49;
    anchor.nTime = 1750000000;
    anchor.nBits = 0x1b0ffff0;
    anchor.nVersion = CPureBlockHeader::VERSION_AUXPOW;
    CBlockHeader native, auxpow;
    native.SetBaseVersion(4, 666);
    auxpow.SetBaseVersion(4, 666);
    auxpow.SetAuxpowFlag(true);
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&anchor, &native, dual), anchor.nBits);
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&anchor, &auxpow, dual), anchor.nBits);

    CBlockIndex native50;
    native50.pprev = &anchor;
    native50.nHeight = 50;
    native50.nTime = anchor.nTime + 600;
    native50.nVersion = 4;
    native50.nBits = anchor.nBits;
    native50.BuildFloofyMiningHistory(true);
    BOOST_CHECK_EQUAL(native50.nFloofyNativeCount, 1U);
    BOOST_CHECK_EQUAL(native50.nFloofyAuxpowCount, 0U);
    arith_uint256 auxAfterFirst, baseTarget;
    auxAfterFirst.SetCompact(GetNextWorkRequired(&native50, &auxpow,
                                                 chain.GetConsensus(51)));
    baseTarget.SetCompact(anchor.nBits);
    BOOST_CHECK(auxAfterFirst > baseTarget); // Underrepresented AuxPoW easier.


    // The second native block can be accepted, with its own 600s cadence.
    CBlockIndex native51;
    native51.pprev = &native50;
    native51.nHeight = 51;
    native51.nTime = anchor.nTime + 1200;
    native51.nVersion = 4;
    native51.nBits = GetNextWorkRequired(&native50, &native,
                                         chain.GetConsensus(51));
    native51.BuildFloofyMiningHistory(true);
    BOOST_CHECK_EQUAL(native51.nFloofyNativeCount, 2U);
    BOOST_CHECK_EQUAL(native51.nFloofyAuxpowCount, 0U);
    arith_uint256 nativeAfterSecond, auxAfterSecond;
    nativeAfterSecond.SetCompact(GetNextWorkRequired(&native51, &native,
                                                     chain.GetConsensus(52)));
    auxAfterSecond.SetCompact(GetNextWorkRequired(&native51, &auxpow,
                                                  chain.GetConsensus(52)));
    BOOST_CHECK(nativeAfterSecond < baseTarget); // Two native: harder.
    BOOST_CHECK(auxAfterSecond > baseTarget);    // Missing AuxPoW: easier.


    // Native accepted timestamps do not drive the AuxPoW target.
    const unsigned int auxBefore = GetNextWorkRequired(
        &native51, &auxpow, chain.GetConsensus(52));
    native51.nTime += 1800;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&native51, &auxpow,
                                           chain.GetConsensus(52)), auxBefore);
    arith_uint256 initial, delayed;
    initial.SetCompact(anchor.nBits);
    delayed.SetCompact(GetNextWorkRequired(&native51, &native,
                                          chain.GetConsensus(52)));
    BOOST_CHECK(delayed > initial);
    native51.nTime -= 1800;

    // Two AuxPoW blocks consecutively: no alternation gate, own counts.
    CBlockIndex aux52;
    aux52.pprev = &native51;
    aux52.nHeight = 52;
    aux52.nTime = anchor.nTime + 1800;
    aux52.nVersion = CPureBlockHeader::VERSION_AUXPOW;
    aux52.nBits = GetNextWorkRequired(&native51, &auxpow,
                                      chain.GetConsensus(52));
    aux52.BuildFloofyMiningHistory(true);
    BOOST_CHECK_EQUAL(aux52.nFloofyNativeCount, 2U);
    BOOST_CHECK_EQUAL(aux52.nFloofyAuxpowCount, 1U);
    CBlockIndex aux53;
    aux53.pprev = &aux52;
    aux53.nHeight = 53;
    aux53.nTime = anchor.nTime + 2400;
    aux53.nVersion = CPureBlockHeader::VERSION_AUXPOW;
    aux53.nBits = GetNextWorkRequired(&aux52, &auxpow,
                                      chain.GetConsensus(53));
    aux53.BuildFloofyMiningHistory(true);
    BOOST_CHECK_EQUAL(aux53.nFloofyNativeCount, 2U);
    BOOST_CHECK_EQUAL(aux53.nFloofyAuxpowCount, 2U);

    const unsigned int nativeBits = GetNextWorkRequired(
        &aux53, &native, chain.GetConsensus(54));
    BOOST_CHECK_EQUAL(nativeBits, anchor.nBits);
    aux53.nTime += 600;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&aux53, &native,
                                           chain.GetConsensus(54)), nativeBits);
    aux53.nTime -= 600;

    const unsigned int auxBits = GetNextWorkRequired(
        &aux53, &auxpow, chain.GetConsensus(54));
    auxpow.nTime += 300;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&aux53, &auxpow,
                                           chain.GetConsensus(54)), auxBits);

    // Recalculate memory-only history from disk-like reconstructed indexes.
    CBlockIndex recoveredAnchor = anchor;
    CBlockIndex rec50 = native50, rec51 = native51, rec52 = aux52, rec53 = aux53;
    rec50.pprev = &recoveredAnchor;
    rec50.BuildFloofyMiningHistory(true);
    rec51.pprev = &rec50;
    rec51.BuildFloofyMiningHistory(true);
    rec52.pprev = &rec51;
    rec52.BuildFloofyMiningHistory(true);
    rec53.pprev = &rec52;
    rec53.BuildFloofyMiningHistory(true);
    BOOST_CHECK_EQUAL(rec53.nFloofyNativeCount, aux53.nFloofyNativeCount);
    BOOST_CHECK_EQUAL(rec53.nFloofyAuxpowCount, aux53.nFloofyAuxpowCount);
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&rec53, &native,
                                           chain.GetConsensus(54)), nativeBits);
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&rec53, &auxpow,
                                           chain.GetConsensus(54)), auxBits);

    // Mainnet strict AuxPoW, ASERT and both lanes switch at ONE height.
    SelectParams(CBaseChainParams::MAIN);
    const CChainParams& mainnet = Params();
    BOOST_CHECK(!mainnet.GetConsensus(244999).fFloofyDualPowDifficulty);
    BOOST_CHECK(mainnet.GetConsensus(245000).fFloofyDualPowDifficulty);
    BOOST_CHECK_EQUAL(mainnet.GetConsensus(245000).nHeightEffective, 245000U);
    BOOST_CHECK_EQUAL(mainnet.GetConsensus(245000).nPowTargetSpacing, 300);
    BOOST_CHECK_EQUAL(mainnet.GetConsensus(245000).nAuxpowStrictValidationHeight, 245000U);
}

BOOST_AUTO_TEST_CASE(floofy_adaptive_timestamp_boundaries)
{
    // Three blocks give a meaningful MTP: for only two blocks the
    // median is the newest one, making equality with parent invalid.
    CBlockIndex ancestor;
    ancestor.nTime = 1749999940;
    CBlockIndex grandparent;
    grandparent.pprev = &ancestor;
    grandparent.nTime = 1750000000;
    CBlockIndex parent;
    parent.pprev = &grandparent;
    parent.nTime = grandparent.nTime + 120;
    CBlockHeader candidate;
    const int64_t now = parent.nTime + 3600;
    candidate.nTime = parent.nTime;
    BOOST_CHECK(FloofyAdaptiveTimestampValid(&parent, &candidate, now));
    candidate.nTime = grandparent.nTime;
    BOOST_CHECK(!FloofyAdaptiveTimestampValid(&parent, &candidate, now));
    candidate.nTime = static_cast<uint32_t>(now + 300);
    BOOST_CHECK(FloofyAdaptiveTimestampValid(&parent, &candidate, now));
    candidate.nTime = static_cast<uint32_t>(now + 301);
    BOOST_CHECK(!FloofyAdaptiveTimestampValid(&parent, &candidate, now));
}

/*
 * FAD-v2 adversarial stress tests.
 *
 * These deliberately exercise the REAL GetNextWorkRequired() implementation.
 * The probabilistic hashrate model below estimates PoW arrivals; it is not
 * mining or a substitute for multi-node consensus integration tests.
 * The PRNG seed is fixed to keep the regression results reproducible.
 */
namespace {
class FloofyAdaptiveStressChain
{
public:
    struct Results {
        int blocks = 0;
        int64_t longestGap = 0;
        int64_t lastGap = 0;
        double finalTargetRatio = 0.0;
    };

    explicit FloofyAdaptiveStressChain(const Consensus::Params& active)
        : params(active), localTime(1750000000), state(0x8dbe934ad326fb1dULL)
    {
        // All subsequent chain indexes point into this reserved storage.
        blocks.reserve(16000);
        CBlockIndex anchor;
        anchor.nHeight = active.nHeightEffective - 1;
        anchor.nTime = static_cast<uint32_t>(localTime);
        anchor.nBits = 0x1b0ffff0;
        baseTarget.SetCompact(anchor.nBits);
        blocks.push_back(anchor);
    }

    const CBlockIndex& Tip() const { return blocks.back(); }
    int64_t Now() const { return localTime; }
    int Height() const { return Tip().nHeight; }

    unsigned int NextBits(int64_t when) const
    {
        CBlockHeader candidate;
        candidate.nTime = static_cast<uint32_t>(when);
        return GetNextWorkRequired(&Tip(), &candidate, params);
    }

    double RelativeTarget(unsigned int bits) const
    {
        arith_uint256 target;
        bool negative = false;
        bool overflow = false;
        target.SetCompact(bits, &negative, &overflow);
        if (negative || overflow || target == 0 ||
            target > UintToArith256(params.powLimit))
            throw std::runtime_error("FAD-v2 produced an invalid target");
        return target.getdouble() / baseTarget.getdouble();
    }

    // This stochastic model uses exponential interarrival probability per
    // simulated wall-clock second, at a given multiple of baseline hashrate.
    // Header timestamps are honest (now), so no future-time boost is used.
    Results MineFor(int seconds, double hashrate)
    {
        Results out;
        for (int i = 0; i < seconds; ++i) {
            ++localTime;
            CBlockHeader candidate;
            candidate.nTime = static_cast<uint32_t>(localTime);

            // A real miner must wait for time to move past its last block.
            if (!FloofyAdaptiveTimestampValid(&Tip(), &candidate, localTime))
                continue;

            const unsigned int bits =
                GetNextWorkRequired(&Tip(), &candidate, params);
            const double targetRatio = RelativeTarget(bits);
            const double eventsPerSecond =
                (hashrate / params.nPowTargetSpacing) * targetRatio;
            const double chance = 1.0 - std::exp(-eventsPerSecond);
            if (Uniform() >= chance)
                continue;

            if (blocks.size() == blocks.capacity())
                throw std::runtime_error("stress test block capacity exceeded");
            const int64_t gap = localTime - Tip().GetBlockTime();
            CBlockIndex next;
            next.pprev = &blocks.back();
            next.nHeight = next.pprev->nHeight + 1;
            next.nTime = static_cast<uint32_t>(localTime);
            next.nBits = bits;
            blocks.push_back(next);
            ++out.blocks;
            out.lastGap = gap;
            out.longestGap = std::max(out.longestGap, gap);
        }
        out.finalTargetRatio = RelativeTarget(NextBits(localTime + 1));
        return out;
    }

    void AdvanceWithoutBlocks(int seconds) { localTime += seconds; }

private:
    double Uniform()
    {
        // Fully specified xorshift64* generator; distribution is deterministic
        // independent of the standard library's RNG distribution routines.
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        const uint64_t output = state * 2685821657736338717ULL;
        return static_cast<double>(output >> 11) *
               (1.0 / 9007199254740992.0);
    }

    const Consensus::Params& params;
    std::vector<CBlockIndex> blocks;
    arith_uint256 baseTarget;
    int64_t localTime;
    uint64_t state;
};
} // namespace

BOOST_AUTO_TEST_SUITE(floofy_adaptive_stress_tests)

// Test 100x and 1000x hashrate departures. Parent-time ASERT cannot
// make the first block of a complete stall easier.
BOOST_AUTO_TEST_CASE(hashrate_joins_and_sudden_departures)
{
    SelectParams(CBaseChainParams::MAIN);
    Consensus::Params params = Params().GetConsensus(245000);
    // Keep the old single-stream ASERT regression test in isolation:
    // actual mainnet consensus at this height is dual-stream.
    params.fFloofyDualPowDifficulty = false;
    BOOST_REQUIRE(params.fFloofyAdaptiveDifficulty);

    const int rates[] = {100, 1000};
    for (int multiplier : rates) {
        FloofyAdaptiveStressChain chain(params);
        const auto initial = chain.MineFor(3600, 1.0);
        const auto burst = chain.MineFor(1800, multiplier);
        BOOST_TEST_MESSAGE("FAD stress " << multiplier << "x: baseline="
            << initial.blocks << " spike=" << burst.blocks
            << " target/initial=" << burst.finalTargetRatio);
        // At a five-minute target the expected count is 12/hour.
        // Allow deterministic interarrival variance.
        BOOST_CHECK(initial.blocks >= 3);
        BOOST_CHECK(burst.blocks > 30);
        BOOST_CHECK(burst.finalTargetRatio < initial.finalTargetRatio);

        const unsigned int immediatelyAfter = chain.NextBits(chain.Now() + 1);
        const double oldRatio = chain.RelativeTarget(immediatelyAfter);

        chain.AdvanceWithoutBlocks(2 * 3600);
        const unsigned int afterOutage = chain.NextBits(chain.Now());
        BOOST_CHECK_EQUAL(afterOutage, immediatelyAfter);
        BOOST_TEST_MESSAGE("Anchored " << multiplier
            << "x: first-block expected wait at 1x "
            << (static_cast<double>(params.nPowTargetSpacing) / oldRatio)
            << " seconds");

        // The model may find no block during the recovery period. This is a
        // genuine liveness risk, not a reason to alter the expected result.
        const auto after = chain.MineFor(24 * 3600, 1.0);
        BOOST_TEST_MESSAGE("Anchored " << multiplier
            << "x: 24h recovery blocks=" << after.blocks
            << " longest gap seconds=" << after.longestGap);
        BOOST_CHECK(after.finalTargetRatio > 0.0);
    }
}

BOOST_AUTO_TEST_CASE(repeated_hashrate_oscillations)
{
    SelectParams(CBaseChainParams::MAIN);
    Consensus::Params params = Params().GetConsensus(245000);
    // Keep the old single-stream ASERT regression test in isolation:
    // actual mainnet consensus at this height is dual-stream.
    params.fFloofyDualPowDifficulty = false;
    BOOST_REQUIRE(params.fFloofyAdaptiveDifficulty);

    FloofyAdaptiveStressChain chain(params);
    int highBlocks = 0;
    int lowBlocks = 0;
    for (int i = 0; i < 12; ++i) {
        highBlocks += chain.MineFor(10 * 60, 100.0).blocks;
        lowBlocks += chain.MineFor(30 * 60, 1.0).blocks;
        BOOST_CHECK(chain.RelativeTarget(chain.NextBits(chain.Now() + 1)) > 0.0);
    }
    BOOST_TEST_MESSAGE("FAD oscillation: 12 alternating phases, high="
        << highBlocks << " low=" << lowBlocks
        << " ending height=" << chain.Height());
    BOOST_CHECK(highBlocks > 12);
    BOOST_CHECK(chain.Height() > 244999);
}

BOOST_AUTO_TEST_CASE(multiday_stall_and_target_saturation)
{
    SelectParams(CBaseChainParams::MAIN);
    Consensus::Params params = Params().GetConsensus(245000);
    // Keep the old single-stream ASERT regression test in isolation:
    // actual mainnet consensus at this height is dual-stream.
    params.fFloofyDualPowDifficulty = false;
    Consensus::Params later = Params().GetConsensus(245001);
    later.fFloofyDualPowDifficulty = false;
    CBlockIndex anchor;
    anchor.nHeight = 244999;
    anchor.nTime = 1750000000;
    anchor.nBits = 0x1b0ffff0;
    CBlockHeader candidate;
    for (int hours = 0; hours <= 72; ++hours) {
        const int64_t now = anchor.GetBlockTime() +
            params.nPowTargetSpacing + hours * 3600;
        candidate.nTime = static_cast<uint32_t>(now);
        BOOST_REQUIRE(FloofyAdaptiveTimestampValid(&anchor, &candidate, now));
        BOOST_CHECK_EQUAL(GetNextWorkRequired(&anchor, &candidate, params),
                          anchor.nBits);
    }
    CBlockIndex found;
    found.pprev = &anchor;
    found.nHeight = 245000;
    found.nTime = anchor.nTime + 72 * 3600 + params.nPowTargetSpacing;
    found.nBits = anchor.nBits;
    candidate.nTime = found.nTime + later.nPowTargetSpacing;
    const unsigned int bits = GetNextWorkRequired(&found, &candidate, later);
    arith_uint256 target;
    target.SetCompact(bits);
    BOOST_CHECK(target > 0);
    BOOST_CHECK(target <= UintToArith256(later.powLimit));
    BOOST_CHECK_EQUAL(bits, UintToArith256(later.powLimit).GetCompact());
}

BOOST_AUTO_TEST_CASE(future_timestamp_grinding_over_512_blocks)
{
    SelectParams(CBaseChainParams::MAIN);
    Consensus::Params params = Params().GetConsensus(245000);
    // Keep the old single-stream ASERT regression test in isolation:
    // actual mainnet consensus at this height is dual-stream.
    params.fFloofyDualPowDifficulty = false;
    std::vector<CBlockIndex> honest(513), ahead(513);
    honest[0].nHeight = 244999;
    honest[0].nTime = 1750000000;
    honest[0].nBits = 0x1b0ffff0;
    ahead[0] = honest[0];
    double maxHistoricalAdvantage = 1.0;
    for (int i = 1; i <= 512; ++i) {
        const int64_t now = static_cast<int64_t>(honest[0].nTime)
            + params.nPowTargetSpacing * i;
        CBlockHeader regular, future;
        regular.nTime = static_cast<uint32_t>(now);
        future.nTime = static_cast<uint32_t>(now + 300);
        BOOST_REQUIRE(FloofyAdaptiveTimestampValid(&honest[i - 1], &regular, now));
        BOOST_REQUIRE(FloofyAdaptiveTimestampValid(&ahead[i - 1], &future, now));
        const unsigned int honestBits = GetNextWorkRequired(&honest[i - 1], &regular, params);
        const unsigned int futureBitsOnSameTip = GetNextWorkRequired(&honest[i - 1], &future, params);
        const unsigned int historicalBits = GetNextWorkRequired(&ahead[i - 1], &future, params);
        BOOST_CHECK_EQUAL(futureBitsOnSameTip, honestBits);
        arith_uint256 t0, t1;
        t0.SetCompact(honestBits);
        t1.SetCompact(historicalBits);
        const double historicalAdvantage = t1.getdouble() / t0.getdouble();
        maxHistoricalAdvantage = std::max(maxHistoricalAdvantage, historicalAdvantage);
        BOOST_CHECK(historicalAdvantage >= 1.0);
        BOOST_CHECK(historicalAdvantage < 1.065);
        honest[i].pprev = &honest[i - 1];
        honest[i].nHeight = 244999 + i;
        honest[i].nTime = regular.nTime;
        honest[i].nBits = honestBits;
        ahead[i].pprev = &ahead[i - 1];
        ahead[i].nHeight = 244999 + i;
        ahead[i].nTime = future.nTime;
        ahead[i].nBits = historicalBits;
    }
    BOOST_TEST_MESSAGE("Direct candidate timestamp advantage: zero; "
                       "largest historical timestamp bias=" << maxHistoricalAdvantage);
}

BOOST_AUTO_TEST_CASE(extreme_time_and_compact_target_math)
{
    SelectParams(CBaseChainParams::MAIN);
    Consensus::Params params = Params().GetConsensus(245000);
    // Keep the old single-stream ASERT regression test in isolation:
    // actual mainnet consensus at this height is dual-stream.
    params.fFloofyDualPowDifficulty = false;
    BOOST_REQUIRE(params.fFloofyAdaptiveDifficulty);

    CBlockIndex anchor;
    anchor.nHeight = 244999;
    anchor.nTime = 1750000000;
    CBlockIndex parent;
    parent.pprev = &anchor;
    parent.nHeight = 245000;
    const unsigned int anchors[] = {0x1e0ffff0, 0x1d00ffff,
                                    0x1b0ffff0, 0x1900ffff, 0x1700ffff};
    const uint32_t times[] = {1U, 1231006505U, 1750000060U,
                              2000000000U, 3000000000U, 4294967295U};
    CBlockHeader candidate;
    for (unsigned int anchorBits : anchors) {
        anchor.nBits = anchorBits;
        parent.nBits = anchorBits;
        for (uint32_t t : times) {
            parent.nTime = t;
            candidate.nTime = t;
            // Parent timestamps deliberately violate contextual validity;
            // this isolates integer arithmetic, not header acceptance.
            const unsigned int bits =
                GetNextWorkRequired(&parent, &candidate, params);
            arith_uint256 target;
            bool neg = false, overflow = false;
            target.SetCompact(bits, &neg, &overflow);
            BOOST_CHECK(!neg);
            BOOST_CHECK(!overflow);
            BOOST_CHECK(target > 0);
            BOOST_CHECK(target <= UintToArith256(params.powLimit));
        }
    }
}

BOOST_AUTO_TEST_SUITE_END() // floofy_adaptive_stress_tests


/** Independent-lane PoW arrival simulation against the real DAA.
 * Both mining types can solve a block during any second. No mining-type
 * gating, but proof validation, P2P and chainwork attacks aren't simulated.
 */
namespace {
class FloofyIndependentStressChain
{
public:
    struct Results { int native = 0; int auxpow = 0; };
    explicit FloofyIndependentStressChain(const Consensus::Params& active)
        : params(active), now(1750000000), rng(0x2b75e4ac91f03d66ULL)
    {
        blocks.reserve(12000);
        CBlockIndex anchor;
        anchor.nHeight = active.nHeightEffective - 1;
        anchor.nTime = static_cast<uint32_t>(now);
        anchor.nBits = 0x1b0ffff0;
        anchor.nVersion = CPureBlockHeader::VERSION_AUXPOW;
        blocks.push_back(anchor);
        base.SetCompact(anchor.nBits);
    }
    int Height() const { return blocks.back().nHeight; }
    int NativeCount() const { return blocks.back().nFloofyNativeCount; }
    int AuxpowCount() const { return blocks.back().nFloofyAuxpowCount; }

    Results MineFor(int seconds, double nativeRate, double auxRate)
    {
        Results ret;
        for (int i = 0; i < seconds; ++i) {
            ++now;
            CBlockHeader native, auxpow;
            native.SetBaseVersion(4, 666);
            auxpow.SetBaseVersion(4, 666);
            auxpow.SetAuxpowFlag(true);
            native.nTime = auxpow.nTime = static_cast<uint32_t>(now);
            if (!FloofyAdaptiveTimestampValid(&blocks.back(), &native, now))
                continue;
            const unsigned int bitsNative = GetNextWorkRequired(
                &blocks.back(), &native, params);
            const unsigned int bitsAux = GetNextWorkRequired(
                &blocks.back(), &auxpow, params);
            const auto prob = [](double rate, double factor) {
                return 1.0 - std::exp(-std::min(700.0, rate * factor / 600.0));
            };
            const bool foundNative = nativeRate > 0.0 &&
                Uniform() < prob(nativeRate, Ratio(bitsNative));
            const bool foundAux = auxRate > 0.0 &&
                Uniform() < prob(auxRate, Ratio(bitsAux));
            if (!foundNative && !foundAux)
                continue;
            const bool chooseAux = foundAux && !foundNative ? true :
                (foundNative && !foundAux ? false : (Uniform() < 0.5));
            if (blocks.size() == blocks.capacity())
                throw std::runtime_error("independent stress capacity exceeded");
            CBlockIndex next;
            next.pprev = &blocks.back();
            next.nHeight = next.pprev->nHeight + 1;
            next.nVersion = chooseAux ? auxpow.nVersion : native.nVersion;
            next.nTime = static_cast<uint32_t>(now);
            next.nBits = chooseAux ? bitsAux : bitsNative;
            blocks.push_back(next);
            blocks.back().BuildFloofyMiningHistory(true);
            if (chooseAux) ++ret.auxpow;
            else ++ret.native;
        }
        return ret;
    }
private:
    double Ratio(unsigned int bits) const
    {
        arith_uint256 target;
        bool negative = false, overflow = false;
        target.SetCompact(bits, &negative, &overflow);
        if (negative || overflow || target == 0 ||
            target > UintToArith256(params.powLimit))
            throw std::runtime_error("invalid dual DAA target");
        return target.getdouble() / base.getdouble();
    }
    double Uniform()
    {
        rng ^= rng >> 12;
        rng ^= rng << 25;
        rng ^= rng >> 27;
        const uint64_t x = rng * 2685821657736338717ULL;
        return static_cast<double>(x >> 11)
            * (1.0 / 9007199254740992.0);
    }
    const Consensus::Params& params;
    int64_t now;
    uint64_t rng;
    std::vector<CBlockIndex> blocks;
    arith_uint256 base;
};
} // namespace

BOOST_AUTO_TEST_SUITE(floofy_dual_stress_tests)

BOOST_AUTO_TEST_CASE(two_active_lanes_and_pool_departure)
{
    SelectParams(CBaseChainParams::REGTEST);
    const Consensus::Params& params = Params().GetConsensus(50);
    BOOST_REQUIRE(params.fFloofyDualPowDifficulty);

    FloofyIndependentStressChain both(params);
    const auto baseline = both.MineFor(24 * 3600, 1.0, 1.0);
    BOOST_TEST_MESSAGE("Two lanes: native=" << baseline.native
                       << " auxpow=" << baseline.auxpow);
    BOOST_CHECK(baseline.native > 0);
    BOOST_CHECK(baseline.auxpow > 0);
    BOOST_CHECK_EQUAL(both.Height(), 49 + both.NativeCount() + both.AuxpowCount());

    FloofyIndependentStressChain large(params);
    const auto spike100 = large.MineFor(6 * 3600, 1.0, 100.0);
    BOOST_TEST_MESSAGE("AuxPoW 100x: native=" << spike100.native
                      << " auxpow=" << spike100.auxpow);
    BOOST_CHECK(spike100.native > 0);
    BOOST_CHECK(spike100.auxpow > 0);
    const auto spike1000 = large.MineFor(6 * 3600, 1.0, 1000.0);
    BOOST_TEST_MESSAGE("AuxPoW 1000x: native=" << spike1000.native
                      << " auxpow=" << spike1000.auxpow);
    BOOST_CHECK(spike1000.native > 0);
    BOOST_CHECK(spike1000.auxpow > 0);

    // Independent fresh chains prevent the preceding spike from creating
    // artificially long recovery times in a no-miner liveness check.
    FloofyIndependentStressChain noAux(params);
    const auto nativeOnly = noAux.MineFor(24 * 3600, 1.0, 0.0);
    BOOST_CHECK(nativeOnly.native > 0);
    BOOST_CHECK_EQUAL(nativeOnly.auxpow, 0);
    FloofyIndependentStressChain noNative(params);
    const auto auxOnly = noNative.MineFor(24 * 3600, 0.0, 1.0);
    BOOST_CHECK(auxOnly.auxpow > 0);
    BOOST_CHECK_EQUAL(auxOnly.native, 0);
}


BOOST_AUTO_TEST_SUITE_END() // floofy_dual_stress_tests



/** Construct the historical prefix for synthetic chains rooted at height
 * 49. Production block indexes always have ancestors back to genesis;
 * constructing a block at height 49 without 0..48 and then calling
 * BuildSkip() is invalid and triggers CBlockIndex::GetAncestor assertions.
 * Keep the skiplist enabled so tests exercise real ancestor lookup.
 */
static void FloofyBuildSyntheticPreForkHistory(
    std::vector<CBlockIndex>& ancestors, CBlockIndex& forkAnchor)
{
    assert(forkAnchor.nHeight > 0);
    assert(ancestors.size() == static_cast<size_t>(forkAnchor.nHeight));
    for (int h = 0; h < forkAnchor.nHeight; ++h) {
        CBlockIndex& prior = ancestors[h];
        prior.nHeight = h;
        prior.nBits = forkAnchor.nBits;
        prior.nTime = forkAnchor.nTime
            - static_cast<uint32_t>(forkAnchor.nHeight - h) * 60;
        prior.nVersion = 4;
        if (h > 0) {
            prior.pprev = &ancestors[h - 1];
            prior.BuildSkip();
        }
    }
    forkAnchor.pprev = &ancestors.back();
    forkAnchor.BuildSkip();
}

/** Check post-fork rolling window, incremental fill, bounded difficulty
 * bias, candidate timestamp independence and per-branch consistency.
 */
BOOST_AUTO_TEST_CASE(floofy_rolling_1000_balance)
{
    SelectParams(CBaseChainParams::REGTEST);
    const Consensus::Params& params = Params().GetConsensus(50);
    BOOST_REQUIRE(params.fFloofyDualPowDifficulty);

    const unsigned int baseBits = 0x1b0ffff0;
    arith_uint256 baseTarget;
    baseTarget.SetCompact(baseBits);
    const uint32_t startTime = 1750000000;
    CBlockHeader native, auxpow;
    native.SetBaseVersion(4, 666);
    auxpow.SetBaseVersion(4, 666);
    auxpow.SetAuxpowFlag(true);
    std::vector<CBlockIndex> preFork(49); // heights 0 through 48
    std::vector<CBlockIndex> blocks(1002);
    blocks[0].nHeight = 49;
    blocks[0].nTime = startTime;
    blocks[0].nBits = baseBits;
    FloofyBuildSyntheticPreForkHistory(preFork, blocks[0]);

    uint32_t n = 42, a = 42;
    FloofyGetRollingWindowCounts(&blocks[0], params, n, a);
    BOOST_CHECK_EQUAL(n, 0U);
    BOOST_CHECK_EQUAL(a, 0U);
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&blocks[0], &native, params), baseBits);
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&blocks[0], &auxpow, params), baseBits);

    // 700 AuxPoW and 300 native in 1000 accepted blocks.
    for (int i = 1; i <= 1000; ++i) {
        CBlockIndex& b = blocks[i];
        b.pprev = &blocks[i - 1];
        b.nHeight = 49 + i;
        b.nTime = startTime + 300 * i;
        b.nBits = baseBits;
        b.nVersion = (i % 10 < 7) ? CPureBlockHeader::VERSION_AUXPOW : 4;
        b.BuildSkip();
        b.BuildFloofyMiningHistory(true);
        if (i == 1) {
            FloofyGetRollingWindowCounts(&b, params, n, a);
            BOOST_CHECK_EQUAL(n, 0U);
            BOOST_CHECK_EQUAL(a, 1U);
            // Bias ramps by just one block/1000, not a 2x startup jump.
            const unsigned int x = FloofyApplyRollingBalance(baseBits, &b, false, params);
            arith_uint256 eased;
            eased.SetCompact(x);
            BOOST_CHECK(eased > baseTarget);
            BOOST_CHECK(eased < baseTarget * 2U);
        }
    }
    FloofyGetRollingWindowCounts(&blocks[1000], params, n, a);
    BOOST_CHECK_EQUAL(n, 300U);
    BOOST_CHECK_EQUAL(a, 700U);

    // Isolate the balancing factor from the unrelated base ASERT targets.
    arith_uint256 easy, hard;
    easy.SetCompact(FloofyApplyRollingBalance(baseBits, &blocks[1000], false, params));
    hard.SetCompact(FloofyApplyRollingBalance(baseBits, &blocks[1000], true, params));
    BOOST_CHECK(easy > baseTarget);
    BOOST_CHECK(easy <= baseTarget * 2U);
    BOOST_CHECK(hard < baseTarget);
    BOOST_CHECK(hard >= (baseTarget >> 1));

    // Candidate timestamps cannot alter difficulty or window composition.
    const unsigned int nativeBits = GetNextWorkRequired(
        &blocks[1000], &native, params);
    const unsigned int auxBits = GetNextWorkRequired(
        &blocks[1000], &auxpow, params);
    native.nTime = 1;
    auxpow.nTime = 4294967295U;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&blocks[1000], &native, params), nativeBits);
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&blocks[1000], &auxpow, params), auxBits);

    // At 1001 post-fork blocks, the first AuxPoW ages out of the window.
    CBlockIndex& next = blocks[1001];
    next.pprev = &blocks[1000];
    next.nHeight = 1050;
    next.nTime = startTime + 300 * 1001;
    next.nBits = baseBits;
    next.nVersion = 4;
    next.BuildSkip();
    next.BuildFloofyMiningHistory(true);
    FloofyGetRollingWindowCounts(&next, params, n, a);
    BOOST_CHECK_EQUAL(n, 301U);
    BOOST_CHECK_EQUAL(a, 699U);
    BOOST_CHECK_EQUAL(n + a, 1000U);

    // Reorg at the same height has its own block counts, and a simulated
    // restart rebuild gives the same rolling counts.
    CBlockIndex forked;
    forked.pprev = &blocks[1000];
    forked.nHeight = 1050;
    forked.nTime = next.nTime;
    forked.nBits = baseBits;
    forked.nVersion = CPureBlockHeader::VERSION_AUXPOW;
    forked.BuildSkip();
    forked.BuildFloofyMiningHistory(true);
    FloofyGetRollingWindowCounts(&forked, params, n, a);
    BOOST_CHECK_EQUAL(n, 300U);
    BOOST_CHECK_EQUAL(a, 700U);

    CBlockIndex rebuilt;
    rebuilt.pprev = &blocks[1000];
    rebuilt.nHeight = next.nHeight;
    rebuilt.nVersion = next.nVersion;
    rebuilt.BuildSkip();
    rebuilt.BuildFloofyMiningHistory(true);
    FloofyGetRollingWindowCounts(&rebuilt, params, n, a);
    BOOST_CHECK_EQUAL(n, 301U);
    BOOST_CHECK_EQUAL(a, 699U);
}

BOOST_AUTO_TEST_CASE(floofy_rolling_1000_all_one_type_still_mines)
{
    SelectParams(CBaseChainParams::REGTEST);
    const Consensus::Params& params = Params().GetConsensus(50);
    const unsigned int baseBits = 0x1b0ffff0;
    std::vector<CBlockIndex> preFork(49); // heights 0 through 48
    std::vector<CBlockIndex> blocks(1002);
    blocks[0].nHeight = 49;
    blocks[0].nTime = 1750000000;
    blocks[0].nBits = baseBits;
    FloofyBuildSyntheticPreForkHistory(preFork, blocks[0]);
    for (int i = 1; i <= 1001; ++i) {
        blocks[i].pprev = &blocks[i - 1];
        blocks[i].nHeight = i + 49;
        blocks[i].nTime = blocks[0].nTime + i * 600;
        blocks[i].nBits = baseBits;
        blocks[i].nVersion = 4;
        blocks[i].BuildSkip();
        blocks[i].BuildFloofyMiningHistory(true);
    }

    uint32_t n, a;
    FloofyGetRollingWindowCounts(&blocks[1001], params, n, a);
    BOOST_CHECK_EQUAL(n, 1000U);
    BOOST_CHECK_EQUAL(a, 0U);
    arith_uint256 base, hard, easy;
    base.SetCompact(baseBits);
    hard.SetCompact(FloofyApplyRollingBalance(baseBits, &blocks[1001], false, params));
    easy.SetCompact(FloofyApplyRollingBalance(baseBits, &blocks[1001], true, params));
    BOOST_CHECK(hard < base);
    BOOST_CHECK(hard >= (base >> 1));
    BOOST_CHECK(easy > base);
    BOOST_CHECK(easy <= base * 2U);

    CBlockHeader native, auxpow;
    native.SetBaseVersion(4, 666);
    auxpow.SetBaseVersion(4, 666);
    auxpow.SetAuxpowFlag(true);
    // No forbidden-next-type rule exists: both have finite difficulty.
    arith_uint256 nativeTarget, auxTarget;
    nativeTarget.SetCompact(GetNextWorkRequired(&blocks[1001], &native, params));
    auxTarget.SetCompact(GetNextWorkRequired(&blocks[1001], &auxpow, params));
    BOOST_CHECK(nativeTarget > 0);
    BOOST_CHECK(auxTarget > 0);
    BOOST_CHECK(nativeTarget <= UintToArith256(params.powLimit));
    BOOST_CHECK(auxTarget <= UintToArith256(params.powLimit));

    SelectParams(CBaseChainParams::MAIN);
    BOOST_CHECK_EQUAL(Params().GetConsensus(245000).nHeightEffective, 245000U);
    BOOST_CHECK(Params().GetConsensus(245000).fFloofyDualPowDifficulty);
}

BOOST_AUTO_TEST_SUITE_END()
