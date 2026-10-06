// Copyright (c) 2014-2015 Daniel Kraft
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "auxpow.h"
#include "chainparams.h"
#include "coins.h"
#include "consensus/merkle.h"
#include "floofy.h"
#include "primitives/block.h"
#include "script/script.h"
#include "uint256.h"
#include "utilstrencodings.h"
#include "validation.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(auxpow_tests, BasicTestingSetup)

/* ************************************************************************** */

/**
 * Tamper with a uint256 (modify it).
 * @param num The number to modify.
 */
static void
tamperWith(uint256& num)
{
    arith_uint256 modifiable = UintToArith256(num);
    modifiable += 1;
    num = ArithToUint256(modifiable);
}

/**
 * Utility class to construct auxpow's and manipulate them.  This is used
 * to simulate various scenarios.
 */
class CAuxpowBuilder
{
public:
    /** The parent block (with coinbase, not just header).  */
    CBlock parentBlock;

    /** The auxpow's merkle branch (connecting it to the coinbase).  */
    std::vector<uint256> auxpowChainMerkleBranch;
    /** The auxpow's merkle tree index.  */
    int auxpowChainIndex;

    /**
   * Initialise everything.
   * @param baseVersion The parent block's base version to use.
   * @param chainId The parent block's chain ID to use.
   */
    CAuxpowBuilder(int baseVersion, int chainId);

    /**
   * Set the coinbase's script.
   * @param scr Set it to this script.
   */
    void setCoinbase(const CScript& scr);

    /**
   * Build the auxpow merkle branch.  The member variables will be
   * set accordingly.  This has to be done before constructing the coinbase
   * itself (which must contain the root merkle hash).  When we have the
   * coinbase afterwards, the member variables can be used to initialise
   * the CAuxPow object from it.
   * @param hashAux The merge-mined chain's block hash.
   * @param h Height of the merkle tree to build.
   * @param index Index to use in the merkle tree.
   * @return The root hash, with reversed endian.
   */
    std::vector<unsigned char> buildAuxpowChain(const uint256& hashAux, unsigned h, int index);

    /**
   * Build the finished CAuxPow object.  We assume that the auxpowChain
   * member variables are already set.  We use the passed in transaction
   * as the base.  It should (probably) be the parent block's coinbase.
   * @param tx The base tx to use.
   * @return The constructed CAuxPow object.
   */
    CAuxPow get(const CTransactionRef tx) const;

    /**
   * Build the finished CAuxPow object from the parent block's coinbase.
   * @return The constructed CAuxPow object.
   */
    inline CAuxPow
    get() const
    {
        assert(!parentBlock.vtx.empty());
        return get(parentBlock.vtx[0]);
    }

    /**
   * Build a data vector to be included in the coinbase.  It consists
   * of the aux hash, the merkle tree size and the nonce.  Optionally,
   * the header can be added as well.
   * @param header Add the header?
   * @param hashAux The aux merkle root hash.
   * @param h Height of the merkle tree.
   * @param nonce The nonce value to use.
   * @return The constructed data.
   */
    static std::vector<unsigned char> buildCoinbaseData(bool header, const std::vector<unsigned char>& auxRoot, unsigned h, int nonce);
};

CAuxpowBuilder::CAuxpowBuilder(int baseVersion, int chainId)
    : auxpowChainIndex(-1)
{
    parentBlock.SetBaseVersion(baseVersion, chainId);
}

void CAuxpowBuilder::setCoinbase(const CScript& scr)
{
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();
    mtx.vin[0].scriptSig = scr;

    parentBlock.vtx.clear();
    parentBlock.vtx.push_back(MakeTransactionRef(std::move(mtx)));
    parentBlock.hashMerkleRoot = BlockMerkleRoot(parentBlock);
}

std::vector<unsigned char>
CAuxpowBuilder::buildAuxpowChain(const uint256& hashAux, unsigned h, int index)
{
    auxpowChainIndex = index;

    /* Just use "something" for the branch.  Doesn't really matter.  */
    auxpowChainMerkleBranch.clear();
    for (unsigned i = 0; i < h; ++i)
        auxpowChainMerkleBranch.push_back(ArithToUint256(arith_uint256(i)));

    const uint256 hash = CAuxPow::CheckMerkleBranch(hashAux, auxpowChainMerkleBranch, index);

    std::vector<unsigned char> res = ToByteVector(hash);
    std::reverse(res.begin(), res.end());

    return res;
}

CAuxPow
CAuxpowBuilder::get(const CTransactionRef tx) const
{
    LOCK(cs_main);
    CAuxPow res(tx);
    res.InitMerkleBranch(parentBlock, 0);

    res.vChainMerkleBranch = auxpowChainMerkleBranch;
    res.nChainIndex = auxpowChainIndex;
    res.parentBlock = parentBlock;

    return res;
}

std::vector<unsigned char>
CAuxpowBuilder::buildCoinbaseData(bool header, const std::vector<unsigned char>& auxRoot, unsigned h, int nonce)
{
    std::vector<unsigned char> res;

    if (header)
        res.insert(res.end(), UBEGIN(pchMergedMiningHeader),
            UEND(pchMergedMiningHeader));
    res.insert(res.end(), auxRoot.begin(), auxRoot.end());

    const int size = (1 << h);
    res.insert(res.end(), UBEGIN(size), UEND(size));
    res.insert(res.end(), UBEGIN(nonce), UEND(nonce));

    return res;
}

/* ************************************************************************** */

BOOST_AUTO_TEST_CASE(check_auxpow)
{
    const Consensus::Params& params = Params().GetConsensus(371337);
    CAuxpowBuilder builder(5, 42);
    CAuxPow auxpow;

    const uint256 hashAux = ArithToUint256(arith_uint256(12345));
    const int32_t ourChainId = params.nAuxpowChainId;
    const unsigned height = 30;
    const int nonce = 7;
    int index;

    std::vector<unsigned char> auxRoot, data;
    CScript scr;

    /* Build a correct auxpow.  The height is the maximally allowed one.  */
    index = CAuxPow::getExpectedIndex(nonce, ourChainId, height);
    auxRoot = builder.buildAuxpowChain(hashAux, height, index);
    data = CAuxpowBuilder::buildCoinbaseData(true, auxRoot, height, nonce);
    scr = (CScript() << 2809 << 2013) + COINBASE_FLAGS;
    scr = (scr << OP_2 << data);
    builder.setCoinbase(scr);
    BOOST_CHECK(builder.get().check(hashAux, ourChainId, params));

    /* An auxpow without any inputs in the parent coinbase tx should be
     handled gracefully (and be considered invalid).  */
    CMutableTransaction mtx(*builder.parentBlock.vtx[0]);
    mtx.vin.clear();
    builder.parentBlock.vtx.clear();
    builder.parentBlock.vtx.push_back (MakeTransactionRef(std::move (mtx)));
    builder.parentBlock.hashMerkleRoot = BlockMerkleRoot(builder.parentBlock);
    BOOST_CHECK(!builder.get().check(hashAux, ourChainId, params));

    /* Check that the auxpow is invalid if we change either the aux block's
     hash or the chain ID.  */
    uint256 modifiedAux(hashAux);
    tamperWith(modifiedAux);
    BOOST_CHECK(!builder.get().check(modifiedAux, ourChainId, params));
    BOOST_CHECK(!builder.get().check(hashAux, ourChainId + 1, params));

    /* Non-coinbase parent tx should fail.  Note that we can't just copy
     the coinbase literally, as we have to get a tx with different hash.  */
    const CTransactionRef oldCoinbase = builder.parentBlock.vtx[0];
    builder.setCoinbase(scr << 5);
    builder.parentBlock.vtx.push_back(oldCoinbase);
    builder.parentBlock.hashMerkleRoot = BlockMerkleRoot(builder.parentBlock);
    auxpow = builder.get(builder.parentBlock.vtx[0]);
    BOOST_CHECK(auxpow.check(hashAux, ourChainId, params));
    auxpow = builder.get(builder.parentBlock.vtx[1]);
    BOOST_CHECK(!auxpow.check(hashAux, ourChainId, params));

    /* The parent chain can't have the same chain ID.  */
    CAuxpowBuilder builder2(builder);
    builder2.parentBlock.SetChainId(100);
    BOOST_CHECK(builder2.get().check(hashAux, ourChainId, params));
    builder2.parentBlock.SetChainId(ourChainId);
    BOOST_CHECK(!builder2.get().check(hashAux, ourChainId, params));

    /* Disallow too long merkle branches.  */
    builder2 = builder;
    index = CAuxPow::getExpectedIndex(nonce, ourChainId, height + 1);
    auxRoot = builder2.buildAuxpowChain(hashAux, height + 1, index);
    data = CAuxpowBuilder::buildCoinbaseData(true, auxRoot, height + 1, nonce);
    scr = (CScript() << 2809 << 2013) + COINBASE_FLAGS;
    scr = (scr << OP_2 << data);
    builder2.setCoinbase(scr);
    BOOST_CHECK(!builder2.get().check(hashAux, ourChainId, params));

    /* Verify that we compare correctly to the parent block's merkle root.  */
    builder2 = builder;
    BOOST_CHECK(builder2.get().check(hashAux, ourChainId, params));
    tamperWith(builder2.parentBlock.hashMerkleRoot);
    BOOST_CHECK(!builder2.get().check(hashAux, ourChainId, params));

    /* Build a non-header legacy version and check that it is also accepted.  */
    builder2 = builder;
    index = CAuxPow::getExpectedIndex(nonce, ourChainId, height);
    auxRoot = builder2.buildAuxpowChain(hashAux, height, index);
    data = CAuxpowBuilder::buildCoinbaseData(false, auxRoot, height, nonce);
    scr = (CScript() << 2809 << 2013) + COINBASE_FLAGS;
    scr = (scr << OP_2 << data);
    builder2.setCoinbase(scr);
    BOOST_CHECK(builder2.get().check(hashAux, ourChainId, params));

    /* However, various attempts at smuggling two roots in should be detected.  */

    const std::vector<unsigned char> wrongAuxRoot = builder2.buildAuxpowChain(modifiedAux, height, index);
    std::vector<unsigned char> data2 = CAuxpowBuilder::buildCoinbaseData(false, wrongAuxRoot, height, nonce);
    builder2.setCoinbase(CScript() << data << data2);
    BOOST_CHECK(builder2.get().check(hashAux, ourChainId, params));
    builder2.setCoinbase(CScript() << data2 << data);
    BOOST_CHECK(!builder2.get().check(hashAux, ourChainId, params));

    data2 = CAuxpowBuilder::buildCoinbaseData(true, wrongAuxRoot, height, nonce);
    builder2.setCoinbase(CScript() << data << data2);
    BOOST_CHECK(!builder2.get().check(hashAux, ourChainId, params));
    builder2.setCoinbase(CScript() << data2 << data);
    BOOST_CHECK(!builder2.get().check(hashAux, ourChainId, params));

    data = CAuxpowBuilder::buildCoinbaseData(true, auxRoot, height, nonce);
    builder2.setCoinbase(CScript() << data << data2);
    BOOST_CHECK(!builder2.get().check(hashAux, ourChainId, params));
    builder2.setCoinbase(CScript() << data2 << data);
    BOOST_CHECK(!builder2.get().check(hashAux, ourChainId, params));

    data2 = CAuxpowBuilder::buildCoinbaseData(false, wrongAuxRoot,
        height, nonce);
    builder2.setCoinbase(CScript() << data << data2);
    BOOST_CHECK(builder2.get().check(hashAux, ourChainId, params));
    builder2.setCoinbase(CScript() << data2 << data);
    BOOST_CHECK(builder2.get().check(hashAux, ourChainId, params));

    /* Verify that the appended nonce/size values are checked correctly.  */

    data = CAuxpowBuilder::buildCoinbaseData(true, auxRoot, height, nonce);
    builder2.setCoinbase(CScript() << data);
    BOOST_CHECK(builder2.get().check(hashAux, ourChainId, params));

    data.pop_back();
    builder2.setCoinbase(CScript() << data);
    BOOST_CHECK(!builder2.get().check(hashAux, ourChainId, params));

    data = CAuxpowBuilder::buildCoinbaseData(true, auxRoot, height - 1, nonce);
    builder2.setCoinbase(CScript() << data);
    BOOST_CHECK(!builder2.get().check(hashAux, ourChainId, params));

    data = CAuxpowBuilder::buildCoinbaseData(true, auxRoot, height, nonce + 3);
    builder2.setCoinbase(CScript() << data);
    BOOST_CHECK(!builder2.get().check(hashAux, ourChainId, params));

    /* Put the aux hash in an invalid merkle tree position.  */

    auxRoot = builder.buildAuxpowChain(hashAux, height, index + 1);
    data = CAuxpowBuilder::buildCoinbaseData(true, auxRoot, height, nonce);
    builder2.setCoinbase(CScript() << data);
    BOOST_CHECK(!builder2.get().check(hashAux, ourChainId, params));

    auxRoot = builder.buildAuxpowChain(hashAux, height, index);
    data = CAuxpowBuilder::buildCoinbaseData(true, auxRoot, height, nonce);
    builder2.setCoinbase(CScript() << data);
    BOOST_CHECK(builder2.get().check(hashAux, ourChainId, params));
}

/* ************************************************************************** */

/**
 * Mine a block (assuming minimal difficulty) that either matches
 * or doesn't match the difficulty target specified in the block header.
 * @param block The block to mine (by updating nonce).
 * @param ok Whether the block should be ok for PoW.
 * @param nBits Use this as difficulty if specified.
 */
static void
mineBlock(CBlockHeader& block, bool ok, int nBits = -1)
{
    if (nBits == -1)
        nBits = block.nBits;

    arith_uint256 target;
    target.SetCompact(nBits);

    block.nNonce = 0;
    while (true) {
        const bool nowOk = (UintToArith256(block.GetPoWHash()) <= target);
        if ((ok && nowOk) || (!ok && !nowOk))
            break;

        ++block.nNonce;
    }

    if (ok)
        BOOST_CHECK(CheckProofOfWork(block.GetPoWHash(), nBits, Params().GetConsensus(0)));
    else
        BOOST_CHECK(!CheckProofOfWork(block.GetPoWHash(), nBits, Params().GetConsensus(0)));
}

BOOST_AUTO_TEST_CASE(auxpow_pow)
{
    /* Use regtest parameters to allow mining with easy difficulty.  */
    SelectParams(CBaseChainParams::REGTEST);
    const Consensus::Params& params = Params().GetConsensus(371337);

    const arith_uint256 target = (~arith_uint256(0) >> 1);
    CBlockHeader block;
    block.nBits = target.GetCompact();

    /* Verify the block version checks.  */

    block.nVersion = 1;
    mineBlock(block, true);
    BOOST_CHECK(CheckAuxPowProofOfWork(block, params));

    // Floofy block version 2 can be both AuxPoW and regular, so test 3

    block.nVersion = 3;
    mineBlock(block, true);
    BOOST_CHECK(!CheckAuxPowProofOfWork(block, params));

    block.SetBaseVersion(2, params.nAuxpowChainId);
    mineBlock(block, true);
    BOOST_CHECK(CheckAuxPowProofOfWork(block, params));

    block.SetChainId(params.nAuxpowChainId + 1);
    mineBlock(block, true);
    BOOST_CHECK(!CheckAuxPowProofOfWork(block, params));

    /* Check the case when the block does not have auxpow (this is true
     right now).  */

    block.SetChainId(params.nAuxpowChainId);
    block.SetAuxpowFlag(true);
    mineBlock(block, true);
    BOOST_CHECK(!CheckAuxPowProofOfWork(block, params));

    block.SetAuxpowFlag(false);
    mineBlock(block, true);
    BOOST_CHECK(CheckAuxPowProofOfWork(block, params));
    mineBlock(block, false);
    BOOST_CHECK(!CheckAuxPowProofOfWork(block, params));

    /* ****************************************** */
    /* Check the case that the block has auxpow.  */

    CAuxpowBuilder builder(5, 42);
    CAuxPow auxpow;
    const int32_t ourChainId = params.nAuxpowChainId;
    const unsigned height = 3;
    const int nonce = 7;
    const int index = CAuxPow::getExpectedIndex(nonce, ourChainId, height);
    std::vector<unsigned char> auxRoot, data;

    /* Valid auxpow, PoW check of parent block.  */
    block.SetAuxpowFlag(true);
    auxRoot = builder.buildAuxpowChain(block.GetHash(), height, index);
    data = CAuxpowBuilder::buildCoinbaseData(true, auxRoot, height, nonce);
    builder.setCoinbase(CScript() << data);
    mineBlock(builder.parentBlock, false, block.nBits);
    block.SetAuxpow(new CAuxPow(builder.get()));
    BOOST_CHECK(!CheckAuxPowProofOfWork(block, params));
    mineBlock(builder.parentBlock, true, block.nBits);
    block.SetAuxpow(new CAuxPow(builder.get()));
    BOOST_CHECK(CheckAuxPowProofOfWork(block, params));

    /* Mismatch between auxpow being present and block.nVersion.  Note that
     block.SetAuxpow sets also the version and that we want to ensure
     that the block hash itself doesn't change due to version changes.
     This requires some work arounds.  */
    block.SetAuxpowFlag(false);
    const uint256 hashAux = block.GetHash();
    auxRoot = builder.buildAuxpowChain(hashAux, height, index);
    data = CAuxpowBuilder::buildCoinbaseData(true, auxRoot, height, nonce);
    builder.setCoinbase(CScript() << data);
    mineBlock(builder.parentBlock, true, block.nBits);
    block.SetAuxpow(new CAuxPow(builder.get()));
    BOOST_CHECK(hashAux != block.GetHash());
    block.SetAuxpowFlag(false);
    BOOST_CHECK(hashAux == block.GetHash());
    BOOST_CHECK(!CheckAuxPowProofOfWork(block, params));

    /* Modifying the block invalidates the PoW.  */
    block.SetAuxpowFlag(true);
    auxRoot = builder.buildAuxpowChain(block.GetHash(), height, index);
    data = CAuxpowBuilder::buildCoinbaseData(true, auxRoot, height, nonce);
    builder.setCoinbase(CScript() << data);
    mineBlock(builder.parentBlock, true, block.nBits);
    block.SetAuxpow(new CAuxPow(builder.get()));
    BOOST_CHECK(CheckAuxPowProofOfWork(block, params));
    tamperWith(block.hashMerkleRoot);
    BOOST_CHECK(!CheckAuxPowProofOfWork(block, params));
}

/* ************************************************************************** */
/* ************************************************************************** */

/**
 * Test the mainnet AuxPoW consensus transition at block 500000.
 *
 * This test deliberately does NOT perform mainnet Proof-of-Work mining.
 * Proof-of-Work validation is already covered by auxpow_pow using regtest.
 *
 * This test is specifically responsible for checking:
 *
 *   1. Mainnet consensus selection at heights:
 *
 *          499999 -> historical AuxPoW rule
 *          500000 -> new strict AuxPoW rule
 *          500001 -> new strict AuxPoW rule
 *
 *   2. Historical malformed chain-ID-666 AuxPoW:
 *
 *          499999 -> accepted
 *          500000 -> rejected
 *          500001 -> rejected
 *
 *   3. Correctly constructed chain-ID-666 AuxPoW:
 *
 *          499999 -> accepted
 *          500000 -> accepted
 *          500001 -> accepted
 *
 *   4. Tampered AuxPoW:
 *
 *          499999 -> accepted by historical compatibility rule
 *          500000 -> rejected by strict validation
 *          500001 -> rejected by strict validation
 */
BOOST_AUTO_TEST_CASE(auxpow_mainnet_activation)
{
    /*
     * Use the real Floofy mainnet consensus parameters.
     */
    SelectParams(CBaseChainParams::MAIN);

    /*
     * Obtain the actual consensus parameters selected by the consensus
     * tree immediately before, exactly at, and immediately after the
     * activation height.
     */
    const Consensus::Params& params499999 =
        Params().GetConsensus(499999);

    const Consensus::Params& params500000 =
        Params().GetConsensus(500000);

    const Consensus::Params& params500001 =
        Params().GetConsensus(500001);

    /*
     * ================================================================
     * PART 1
     *
     * Verify that the correct mainnet chain ID is active at all three
     * heights.
     * ================================================================
     */

    BOOST_TEST_MESSAGE(
        "Checking mainnet AuxPoW chain ID around activation height"
    );

    BOOST_CHECK_EQUAL(
        params499999.nAuxpowChainId,
        666
    );

    BOOST_CHECK_EQUAL(
        params500000.nAuxpowChainId,
        666
    );

    BOOST_CHECK_EQUAL(
        params500001.nAuxpowChainId,
        666
    );

    /*
     * ================================================================
     * PART 2
     *
     * Verify the actual height-aware consensus transition.
     *
     * Block 499999 must still use the historical compatibility rule.
     *
     * Block 500000 is the first block using full AuxPoW validation.
     *
     * Block 500001 must continue using full AuxPoW validation.
     * ================================================================
     */

    BOOST_TEST_MESSAGE(
        "Checking AuxPoW consensus rule at height 499999"
    );

    BOOST_CHECK(
        params499999.fAuxpowLegacyRule
    );

    BOOST_TEST_MESSAGE(
        "Checking AuxPoW consensus rule at height 500000"
    );

    BOOST_CHECK(
        !params500000.fAuxpowLegacyRule
    );

    BOOST_TEST_MESSAGE(
        "Checking AuxPoW consensus rule at height 500001"
    );

    BOOST_CHECK(
        !params500001.fAuxpowLegacyRule
    );

    /*
     * All three consensus parameter sets must still be configured for
     * strict chain-ID checking.
     */
    BOOST_CHECK(
        params499999.fStrictChainId
    );

    BOOST_CHECK(
        params500000.fStrictChainId
    );

    BOOST_CHECK(
        params500001.fStrictChainId
    );

    /*
     * ================================================================
     * PART 3
     *
     * Build a deliberately malformed historical AuxPoW.
     *
     * This simulates the type of AuxPoW that the historical chain-ID-666
     * compatibility rule allowed.
     *
     * There is a parent coinbase transaction so CAuxPow is structurally
     * usable, but the coinbase DOES NOT contain a valid merged-mining
     * commitment for hashAux.
     *
     * Therefore:
     *
     *   old rule -> returns true because chain ID 666 is grandfathered
     *
     *   new rule -> continues into normal CAuxPow validation and fails
     * ================================================================
     */

    const int32_t ourChainId = 666;

    const uint256 hashAux =
        ArithToUint256(
            arith_uint256(123456789)
        );

    CAuxpowBuilder legacyBuilder(5, 42);

    /*
     * Construct a perfectly usable parent coinbase, but deliberately
     * omit the merged-mining header/root/tree-size/nonce commitment.
     */
    CScript legacyScript;

    legacyScript
        << 2809
        << 2013
        << OP_2;

    legacyBuilder.setCoinbase(
        legacyScript
    );

    const CAuxPow legacyAuxpow =
        legacyBuilder.get();

    /*
     * ------------------------------------------------
     * Height 499999
     *
     * Historical compatibility rule is active.
     *
     * The malformed AuxPoW must therefore be accepted.
     * ------------------------------------------------
     */

    BOOST_TEST_MESSAGE(
        "Legacy malformed AuxPoW at height 499999 should PASS"
    );

    BOOST_CHECK(
        legacyAuxpow.check(
            hashAux,
            ourChainId,
            params499999
        )
    );

    /*
     * ------------------------------------------------
     * Height 500000
     *
     * Historical compatibility rule has been disabled.
     *
     * Full CAuxPow validation must detect that the parent coinbase does
     * not contain a valid merged-mining commitment.
     * ------------------------------------------------
     */

    BOOST_TEST_MESSAGE(
        "Legacy malformed AuxPoW at height 500000 should FAIL"
    );

    BOOST_CHECK(
        !legacyAuxpow.check(
            hashAux,
            ourChainId,
            params500000
        )
    );

    /*
     * ------------------------------------------------
     * Height 500001
     *
     * Full validation must remain active.
     * ------------------------------------------------
     */

    BOOST_TEST_MESSAGE(
        "Legacy malformed AuxPoW at height 500001 should FAIL"
    );

    BOOST_CHECK(
        !legacyAuxpow.check(
            hashAux,
            ourChainId,
            params500001
        )
    );

    /*
     * ================================================================
     * PART 4
     *
     * Construct a completely valid AuxPoW commitment.
     *
     * Unlike the previous object, this parent coinbase contains:
     *
     *     merged-mining header
     *     AuxPoW chain merkle root
     *     tree size
     *     nonce
     *
     * and uses the correct expected chain-merkle index.
     * ================================================================
     */

    CAuxpowBuilder validBuilder(5, 42);

    const unsigned merkleHeight = 3;

    const int nonce = 7;

    const int index =
        CAuxPow::getExpectedIndex(
            nonce,
            ourChainId,
            merkleHeight
        );

    /*
     * Build the AuxPoW chain merkle branch from the child block hash.
     */
    const std::vector<unsigned char> auxRoot =
        validBuilder.buildAuxpowChain(
            hashAux,
            merkleHeight,
            index
        );

    /*
     * Build the actual merged-mining commitment.
     */
    const std::vector<unsigned char> coinbaseData =
        CAuxpowBuilder::buildCoinbaseData(
            true,
            auxRoot,
            merkleHeight,
            nonce
        );

    /*
     * Put the merged-mining commitment into the parent coinbase.
     */
    CScript validScript;

    validScript =
        (CScript()
            << 2809
            << 2013)
        + COINBASE_FLAGS;

    validScript =
        validScript
        << OP_2
        << coinbaseData;

    validBuilder.setCoinbase(
        validScript
    );

    const CAuxPow validAuxpow =
        validBuilder.get();

    /*
     * ================================================================
     * PART 5
     *
     * The correctly constructed AuxPoW must work both before and after
     * activation.
     * ================================================================
     */

    BOOST_TEST_MESSAGE(
        "Fully valid AuxPoW at height 499999 should PASS"
    );

    BOOST_CHECK(
        validAuxpow.check(
            hashAux,
            ourChainId,
            params499999
        )
    );

    BOOST_TEST_MESSAGE(
        "Fully valid AuxPoW at height 500000 should PASS"
    );

    BOOST_CHECK(
        validAuxpow.check(
            hashAux,
            ourChainId,
            params500000
        )
    );

    BOOST_TEST_MESSAGE(
        "Fully valid AuxPoW at height 500001 should PASS"
    );

    BOOST_CHECK(
        validAuxpow.check(
            hashAux,
            ourChainId,
            params500001
        )
    );

    /*
     * ================================================================
     * PART 6
     *
     * Deliberately corrupt the chain-merkle index.
     *
     * This proves that full validation after activation is genuinely
     * checking the AuxPoW structure.
     * ================================================================
     */

    CAuxPow badIndexAuxpow =
        validAuxpow;

    badIndexAuxpow.nChainIndex++;

    /*
     * Historical behaviour at 499999 should still bypass this error.
     */
    BOOST_TEST_MESSAGE(
        "Tampered chain index at height 499999 should PASS under legacy rule"
    );

    BOOST_CHECK(
        badIndexAuxpow.check(
            hashAux,
            ourChainId,
            params499999
        )
    );

    /*
     * At the activation block this corruption must be rejected.
     */
    BOOST_TEST_MESSAGE(
        "Tampered chain index at height 500000 should FAIL"
    );

    BOOST_CHECK(
        !badIndexAuxpow.check(
            hashAux,
            ourChainId,
            params500000
        )
    );

    /*
     * And it must continue to be rejected after activation.
     */
    BOOST_TEST_MESSAGE(
        "Tampered chain index at height 500001 should FAIL"
    );

    BOOST_CHECK(
        !badIndexAuxpow.check(
            hashAux,
            ourChainId,
            params500001
        )
    );

    /*
     * ================================================================
     * PART 7
     *
     * Restore the correct AuxPoW and prove that the rejection above was
     * specifically caused by the corrupted chain index.
     * ================================================================
     */

    BOOST_TEST_MESSAGE(
        "Restored valid AuxPoW at height 500000 should PASS"
    );

    BOOST_CHECK(
        validAuxpow.check(
            hashAux,
            ourChainId,
            params500000
        )
    );

    BOOST_TEST_MESSAGE(
        "Restored valid AuxPoW at height 500001 should PASS"
    );

    BOOST_CHECK(
        validAuxpow.check(
            hashAux,
            ourChainId,
            params500001
        )
    );
}

/* ************************************************************************** */
BOOST_AUTO_TEST_SUITE_END()
