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
 * Test the mainnet AuxPoW consensus transition at block 245000.
 *
 * This test deliberately does NOT perform mainnet Proof-of-Work mining.
 * Proof-of-Work validation is already covered by auxpow_pow using regtest.
 *
 * This test is specifically responsible for checking:
 *
 *   1. Mainnet consensus selection at heights:
 *
 *          244999 -> historical AuxPoW rule
 *          245000 -> new strict AuxPoW rule
 *          245001 -> new strict AuxPoW rule
 *
 *   2. Historical malformed chain-ID-666 AuxPoW:
 *
 *          244999 -> accepted
 *          245000 -> rejected
 *          245001 -> rejected
 *
 *   3. Correctly constructed chain-ID-666 AuxPoW:
 *
 *          244999 -> accepted
 *          245000 -> accepted
 *          245001 -> accepted
 *
 *   4. Tampered AuxPoW:
 *
 *          244999 -> accepted by historical compatibility rule
 *          245000 -> rejected by strict validation
 *          245001 -> rejected by strict validation
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
    const Consensus::Params& params244999 =
        Params().GetConsensus(244999);

    const Consensus::Params& params245000 =
        Params().GetConsensus(245000);

    const Consensus::Params& params245001 =
        Params().GetConsensus(245001);

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
        params244999.nAuxpowChainId,
        666
    );

    BOOST_CHECK_EQUAL(
        params245000.nAuxpowChainId,
        666
    );

    BOOST_CHECK_EQUAL(
        params245001.nAuxpowChainId,
        666
    );

    /*
     * ================================================================
     * PART 2
     *
     * Verify the actual height-aware consensus transition.
     *
     * Block 244999 must still use the historical compatibility rule.
     *
     * Block 245000 is the first block using full AuxPoW validation.
     *
     * Block 245001 must continue using full AuxPoW validation.
     * ================================================================
     */

    BOOST_TEST_MESSAGE(
        "Checking AuxPoW consensus rule at height 244999"
    );

    BOOST_CHECK(
        params244999.fAuxpowLegacyRule
    );

    BOOST_TEST_MESSAGE(
        "Checking AuxPoW consensus rule at height 245000"
    );

    BOOST_CHECK(
        !params245000.fAuxpowLegacyRule
    );

    BOOST_TEST_MESSAGE(
        "Checking AuxPoW consensus rule at height 245001"
    );

    BOOST_CHECK(
        !params245001.fAuxpowLegacyRule
    );

    /*
     * All three consensus parameter sets must still be configured for
     * strict chain-ID checking.
     */
    BOOST_CHECK(
        params244999.fStrictChainId
    );

    BOOST_CHECK(
        params245000.fStrictChainId
    );

    BOOST_CHECK(
        params245001.fStrictChainId
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
     * Height 244999
     *
     * Historical compatibility rule is active.
     *
     * The malformed AuxPoW must therefore be accepted.
     * ------------------------------------------------
     */

    BOOST_TEST_MESSAGE(
        "Legacy malformed AuxPoW at height 244999 should PASS"
    );

    BOOST_CHECK(
        legacyAuxpow.check(
            hashAux,
            ourChainId,
            params244999
        )
    );

    /*
     * ------------------------------------------------
     * Height 245000
     *
     * Historical compatibility rule has been disabled.
     *
     * Full CAuxPow validation must detect that the parent coinbase does
     * not contain a valid merged-mining commitment.
     * ------------------------------------------------
     */

    BOOST_TEST_MESSAGE(
        "Legacy malformed AuxPoW at height 245000 should FAIL"
    );

    BOOST_CHECK(
        !legacyAuxpow.check(
            hashAux,
            ourChainId,
            params245000
        )
    );

    /*
     * ------------------------------------------------
     * Height 245001
     *
     * Full validation must remain active.
     * ------------------------------------------------
     */

    BOOST_TEST_MESSAGE(
        "Legacy malformed AuxPoW at height 245001 should FAIL"
    );

    BOOST_CHECK(
        !legacyAuxpow.check(
            hashAux,
            ourChainId,
            params245001
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
        "Fully valid AuxPoW at height 244999 should PASS"
    );

    BOOST_CHECK(
        validAuxpow.check(
            hashAux,
            ourChainId,
            params244999
        )
    );

    BOOST_TEST_MESSAGE(
        "Fully valid AuxPoW at height 245000 should PASS"
    );

    BOOST_CHECK(
        validAuxpow.check(
            hashAux,
            ourChainId,
            params245000
        )
    );

    BOOST_TEST_MESSAGE(
        "Fully valid AuxPoW at height 245001 should PASS"
    );

    BOOST_CHECK(
        validAuxpow.check(
            hashAux,
            ourChainId,
            params245001
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
     * Historical behaviour at 244999 should still bypass this error.
     */
    BOOST_TEST_MESSAGE(
        "Tampered chain index at height 244999 should PASS under legacy rule"
    );

    BOOST_CHECK(
        badIndexAuxpow.check(
            hashAux,
            ourChainId,
            params244999
        )
    );

    /*
     * At the activation block this corruption must be rejected.
     */
    BOOST_TEST_MESSAGE(
        "Tampered chain index at height 245000 should FAIL"
    );

    BOOST_CHECK(
        !badIndexAuxpow.check(
            hashAux,
            ourChainId,
            params245000
        )
    );

    /*
     * And it must continue to be rejected after activation.
     */
    BOOST_TEST_MESSAGE(
        "Tampered chain index at height 245001 should FAIL"
    );

    BOOST_CHECK(
        !badIndexAuxpow.check(
            hashAux,
            ourChainId,
            params245001
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
        "Restored valid AuxPoW at height 245000 should PASS"
    );

    BOOST_CHECK(
        validAuxpow.check(
            hashAux,
            ourChainId,
            params245000
        )
    );

    BOOST_TEST_MESSAGE(
        "Restored valid AuxPoW at height 245001 should PASS"
    );

    BOOST_CHECK(
        validAuxpow.check(
            hashAux,
            ourChainId,
            params245001
        )
    );

    /*
     * PART 8: Exercise CheckAuxPowProofOfWork across the same boundary.
     * Copy the real mainnet parameters and change ONLY powLimit to make
     * synthetic parent blocks cheap to mine.  This is not a mainnet block
     * submission or a test of mainnet difficulty/retargeting.
     */
    const arith_uint256 easyLimit = (~arith_uint256(0) >> 1);
    const unsigned int easyBits = easyLimit.GetCompact();
    arith_uint256 easyTarget;
    easyTarget.SetCompact(easyBits);

    /* Do not use mineBlock here: it checks against the selected network's
     * original powLimit, whereas these tests deliberately use a copy. */
    const auto setParentProof = [&](CBlockHeader& parent, bool valid) {
        parent.nBits = easyBits;
        for (unsigned int candidate = 0; candidate < 100000; ++candidate) {
            parent.nNonce = candidate;
            if ((UintToArith256(parent.GetPoWHash()) <= easyTarget) == valid)
                return true;
        }
        return false;
    };

    const int testHeights[] = {244999, 245000, 245001};
    for (const int blockHeight : testHeights) {
        BOOST_TEST_MESSAGE(
            "Checking AuxPoW structure AND parent proof-of-work at height "
            << blockHeight);

        Consensus::Params powParams = Params().GetConsensus(blockHeight);
        powParams.powLimit = ArithToUint256(easyLimit);
        const bool expectLegacy = (blockHeight < 245000);
        BOOST_REQUIRE_EQUAL(powParams.fAuxpowLegacyRule, expectLegacy);

        CBlockHeader child;
        child.SetBaseVersion(4, ourChainId);
        child.SetAuxpowFlag(true);
        child.nBits = easyBits;

        /* Valid merged-mining commitment for this exact child header. */
        CAuxpowBuilder powBuilder(5, 42);
        const std::vector<unsigned char> powRoot =
            powBuilder.buildAuxpowChain(child.GetHash(), merkleHeight, index);
        const std::vector<unsigned char> powData =
            CAuxpowBuilder::buildCoinbaseData(true, powRoot, merkleHeight, nonce);
        powBuilder.setCoinbase(CScript() << powData);

        BOOST_REQUIRE(setParentProof(powBuilder.parentBlock, true));
        BOOST_REQUIRE(CheckProofOfWork(powBuilder.parentBlock.GetPoWHash(),
                                      child.nBits, powParams));
        child.SetAuxpow(new CAuxPow(powBuilder.get()));
        BOOST_CHECK_MESSAGE(CheckAuxPowProofOfWork(child, powParams),
                            "Valid AuxPoW with valid parent PoW must pass at "
                            << blockHeight);

        /* The same commitment, but a parent hash above the child's target.
         * Even the historical structural bypass must not bypass PoW. */
        BOOST_REQUIRE(setParentProof(powBuilder.parentBlock, false));
        BOOST_REQUIRE(!CheckProofOfWork(powBuilder.parentBlock.GetPoWHash(),
                                       child.nBits, powParams));
        child.SetAuxpow(new CAuxPow(powBuilder.get()));
        BOOST_CHECK_MESSAGE(!CheckAuxPowProofOfWork(child, powParams),
                            "Invalid parent PoW must fail at " << blockHeight);

        /* Restore valid parent PoW, then corrupt only the chain index. */
        BOOST_REQUIRE(setParentProof(powBuilder.parentBlock, true));
        CAuxPow corruptPow = powBuilder.get();
        ++corruptPow.nChainIndex;
        child.SetAuxpow(new CAuxPow(corruptPow));
        BOOST_CHECK_MESSAGE(
            CheckAuxPowProofOfWork(child, powParams) == expectLegacy,
            "Tampered chain index must follow the activation rule at "
            << blockHeight);

        child.SetAuxpow(new CAuxPow(powBuilder.get()));
        BOOST_CHECK_MESSAGE(CheckAuxPowProofOfWork(child, powParams),
                            "Restored valid AuxPoW must pass at " << blockHeight);

        /* Missing merged-mining commitment, with otherwise valid PoW. */
        CAuxpowBuilder malformedBuilder(5, 42);
        malformedBuilder.setCoinbase(CScript() << 2809 << 2013 << OP_2);
        BOOST_REQUIRE(setParentProof(malformedBuilder.parentBlock, true));
        child.SetAuxpow(new CAuxPow(malformedBuilder.get()));
        BOOST_CHECK_MESSAGE(
            CheckAuxPowProofOfWork(child, powParams) == expectLegacy,
            "Malformed commitment with valid PoW must follow the activation rule at "
            << blockHeight);

        BOOST_REQUIRE(setParentProof(malformedBuilder.parentBlock, false));
        child.SetAuxpow(new CAuxPow(malformedBuilder.get()));
        BOOST_CHECK_MESSAGE(!CheckAuxPowProofOfWork(child, powParams),
                            "Malformed commitment with invalid PoW must fail at "
                            << blockHeight);
    }
}

/* ************************************************************************** */

/* ************************************************************************** */
/**
 * Regression: native Scrypt PoW is independent of the AuxPoW legacy switch.
 * Uses synthetic easy difficulty; this does not mine mainnet blocks.
 */
BOOST_AUTO_TEST_CASE(native_pow_unchanged_at_auxpow_activation)
{
    SelectParams(CBaseChainParams::MAIN);
    const arith_uint256 easyLimit = (~arith_uint256(0) >> 1);
    const unsigned int easyBits = easyLimit.GetCompact();
    const int heights[] = {244999, 245000, 245001};

    for (const int height : heights) {
        Consensus::Params params = Params().GetConsensus(height);
        params.powLimit = ArithToUint256(easyLimit);

        CBlockHeader native;
        native.SetBaseVersion(4, params.nAuxpowChainId);
        native.SetAuxpowFlag(false);
        native.nBits = easyBits;
        native.nTime = 1790000000;

        bool foundValid = false;
        for (unsigned int nonce = 0; nonce < 100000; ++nonce) {
            native.nNonce = nonce;
            if (CheckProofOfWork(native.GetPoWHash(), native.nBits, params)) {
                foundValid = true;
                break;
            }
        }
        BOOST_REQUIRE_MESSAGE(foundValid, "Could not mine synthetic native PoW at " << height);
        BOOST_CHECK_MESSAGE(CheckAuxPowProofOfWork(native, params),
                            "Native PoW should pass at " << height);

        bool foundInvalid = false;
        for (unsigned int nonce = 0; nonce < 100000; ++nonce) {
            native.nNonce = nonce;
            if (!CheckProofOfWork(native.GetPoWHash(), native.nBits, params)) {
                foundInvalid = true;
                break;
            }
        }
        BOOST_REQUIRE_MESSAGE(foundInvalid, "Could not find invalid native PoW at " << height);
        BOOST_CHECK_MESSAGE(!CheckAuxPowProofOfWork(native, params),
                            "Invalid native PoW should fail at " << height);
    }
}

/**
 * Regression: prove the *complete* AuxPoW PoW verifier enforces the
 * mainnet activation switch, not merely CAuxPow::check().
 * A synthetic parent is mined at easy difficulty with a copied powLimit.
 */
BOOST_AUTO_TEST_CASE(auxpow_full_verifier_legacy_boundary)
{
    SelectParams(CBaseChainParams::MAIN);
    const arith_uint256 easyLimit = (~arith_uint256(0) >> 1);
    const unsigned int easyBits = easyLimit.GetCompact();

    CBlockHeader child;
    child.SetBaseVersion(4, 666);
    child.SetAuxpowFlag(true);
    child.nBits = easyBits;
    child.nTime = 1790000000;

    CAuxpowBuilder builder(5, 42);
    builder.setCoinbase(CScript() << 2809 << 2013 << OP_2);

    bool found = false;
    for (unsigned int nonce = 0; nonce < 100000; ++nonce) {
        builder.parentBlock.nNonce = nonce;
        if (UintToArith256(builder.parentBlock.GetPoWHash()) <= easyLimit) {
            found = true;
            break;
        }
    }
    BOOST_REQUIRE(found);
    child.SetAuxpow(new CAuxPow(builder.get()));

    const int heights[] = {244999, 245000, 245001};
    for (const int height : heights) {
        Consensus::Params params = Params().GetConsensus(height);
        params.powLimit = ArithToUint256(easyLimit);
        const bool expected = height < 245000;
        BOOST_CHECK_MESSAGE(CheckAuxPowProofOfWork(child, params) == expected,
                            "Malformed AuxPoW full verifier at " << height
                            << " should " << (expected ? "accept" : "reject"));
    }
}


/* ************************************************************************** */
/**
 * The single fork activates strict AuxPoW rules and contextual enforcement at height 245000.
 * This test checks the consensus predicates and the synthetic verifier
 * inputs at the boundary.  It is NOT a ProcessNewBlock integration test:
 * the production contextual gate is tested separately by the source audit.
 */
BOOST_AUTO_TEST_CASE(auxpow_contextual_enforcement_245000_boundary)
{
    SelectParams(CBaseChainParams::MAIN);
    static const int kEnforcementHeight = 245000;
    const arith_uint256 easyLimit = (~arith_uint256(0) >> 1);
    const unsigned int easyBits = easyLimit.GetCompact();
    const int heights[] = {244999, 245000, 245001};

    CBlockHeader child;
    child.SetBaseVersion(4, 666);
    child.SetAuxpowFlag(true);
    child.nBits = easyBits;
    child.nTime = 1790000000;

    CAuxpowBuilder builder(5, 42);
    builder.setCoinbase(CScript() << 2809 << 2013 << OP_2);
    bool mined = false;
    for (unsigned int nonce = 0; nonce < 100000; ++nonce) {
        builder.parentBlock.nNonce = nonce;
        if (UintToArith256(builder.parentBlock.GetPoWHash()) <= easyLimit) {
            mined = true;
            break;
        }
    }
    BOOST_REQUIRE(mined);
    child.SetAuxpow(new CAuxPow(builder.get()));

    for (const int height : heights) {
        Consensus::Params params = Params().GetConsensus(height);
        params.powLimit = ArithToUint256(easyLimit);
        const bool gateActive = height >= kEnforcementHeight;

        // The same activation height controls both the consensus transition
        // and the additional contextual enforcement.
        BOOST_REQUIRE_MESSAGE(params.fAuxpowLegacyRule == !gateActive,
                              "Legacy consensus must switch at " << height);
        BOOST_REQUIRE_MESSAGE(CheckAuxPowProofOfWork(child, params) == !gateActive,
                              "Malformed AuxPoW must follow the single fork at " << height);

        // This checks the expected predicate, not ProcessNewBlock itself.
        const bool contextualWouldReject =
            gateActive && child.IsAuxpow() && !CheckAuxPowProofOfWork(child, params);
        BOOST_CHECK_MESSAGE(contextualWouldReject == gateActive,
                            "Contextual rejection boundary at " << height);

        // Parent PoW remains valid, so any rejection is structural.
        BOOST_CHECK_MESSAGE(
            CheckProofOfWork(builder.parentBlock.GetPoWHash(), child.nBits, params),
            "Synthetic parent PoW valid at " << height);
    }
}

/**
 * Valid AuxPoW must continue to pass after the new enforcement height.
 * Test the real AuxPoW verifier with easy synthetic difficulty.
 */
BOOST_AUTO_TEST_CASE(auxpow_valid_at_245000_boundary)
{
    SelectParams(CBaseChainParams::MAIN);
    const arith_uint256 easyLimit = (~arith_uint256(0) >> 1);
    const unsigned int easyBits = easyLimit.GetCompact();
    const int heights[] = {244999, 245000, 245001};

    CBlockHeader child;
    child.SetBaseVersion(4, 666);
    child.SetAuxpowFlag(true);
    child.nBits = easyBits;
    child.nTime = 1790000000;

    const unsigned merkleHeight = 3;
    const int nonce = 7;
    const int index = CAuxPow::getExpectedIndex(nonce, 666, merkleHeight);
    CAuxpowBuilder builder(5, 42);
    const std::vector<unsigned char> root =
        builder.buildAuxpowChain(child.GetHash(), merkleHeight, index);
    const std::vector<unsigned char> data =
        CAuxpowBuilder::buildCoinbaseData(true, root, merkleHeight, nonce);
    builder.setCoinbase(CScript() << data);

    bool mined = false;
    for (unsigned int candidate = 0; candidate < 100000; ++candidate) {
        builder.parentBlock.nNonce = candidate;
        if (UintToArith256(builder.parentBlock.GetPoWHash()) <= easyLimit) {
            mined = true;
            break;
        }
    }
    BOOST_REQUIRE(mined);
    child.SetAuxpow(new CAuxPow(builder.get()));

    for (const int height : heights) {
        Consensus::Params params = Params().GetConsensus(height);
        params.powLimit = ArithToUint256(easyLimit);
        BOOST_CHECK_MESSAGE(CheckAuxPowProofOfWork(child, params),
                            "Valid AuxPoW must pass at " << height);

        CAuxPow corrupted = builder.get();
        ++corrupted.nChainIndex;
        child.SetAuxpow(new CAuxPow(corrupted));
        BOOST_CHECK_MESSAGE(CheckAuxPowProofOfWork(child, params) == (height < 245000),
                            "Tampered chain index must follow the single fork at " << height);
        child.SetAuxpow(new CAuxPow(builder.get()));
    }
}


/**
 * Same real AuxPoW verifier as mainnet, but with the private regtest
 * activation boundary 29 -> 30.  Parent proof of work is actually mined
 * against the regtest target rather than bypassed with a mock verifier.
 */
BOOST_AUTO_TEST_CASE(auxpow_regtest_strict_fork_30_boundary)
{
    SelectParams(CBaseChainParams::REGTEST);
    const CChainParams& chain = Params();
    const unsigned int bits =
        UintToArith256(chain.GetConsensus(30).powLimit).GetCompact();

    CBlockHeader child;
    child.SetBaseVersion(4, 666);
    child.nBits = bits;
    child.nTime = 1750000000;
    CAuxPow::initAuxPow(child);
    BOOST_REQUIRE(child.IsAuxpow());
    BOOST_REQUIRE(child.auxpow);

    // Solve synthetic parent work with the real PoW hash function.
    bool mined = false;
    for (unsigned int nonce = 0; nonce < 100000; ++nonce) {
        child.auxpow->parentBlock.nNonce = nonce;
        if (CheckProofOfWork(
                child.auxpow->parentBlock.GetPoWHash(), bits,
                chain.GetConsensus(30))) {
            mined = true;
            break;
        }
    }
    BOOST_REQUIRE(mined);

    const int heights[] = {29, 30, 31};
    for (int height : heights) {
        const Consensus::Params& params = chain.GetConsensus(height);
        BOOST_CHECK_EQUAL(params.nAuxpowChainId, 666);
        BOOST_CHECK_EQUAL(params.nAuxpowStrictValidationHeight, 30U);
        BOOST_CHECK_EQUAL(params.fAuxpowLegacyRule, height < 30);
        BOOST_CHECK(CheckAuxPowProofOfWork(child, params));

        // Invalid merkle-chain index: legacy accepts; strict rejects.
        child.auxpow->nChainIndex = 1;
        BOOST_CHECK_MESSAGE(
            CheckAuxPowProofOfWork(child, params) == (height < 30),
            "Malformed AuxPoW at regtest height " << height);
        child.auxpow->nChainIndex = 0;
    }
}

/**
 * Regression: block 20 AuxPoW templates are constructed before their
 * mined parent proof exists. An AuxPoW version flag with a null payload
 * aborts in CBlockHeader::SerializationOp() while checking block size.
 * The temporary CAuxPow must be serialisable; a real proof can replace
 * it later without changing the child header's hash.
 */
BOOST_AUTO_TEST_CASE(auxpow_template_serialisation_without_mined_parent)
{
    CBlock block;
    block.SetBaseVersion(4, 666);
    block.nTime = 1750000000;
    block.nBits = 0x207fffff;
    block.SetAuxpow(new CAuxPow());

    BOOST_REQUIRE(block.IsAuxpow());
    BOOST_REQUIRE(block.auxpow != nullptr);
    BOOST_CHECK(::GetSerializeSize(
        block, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) > 80U);

    const uint256 childHash = block.GetHash();
    CAuxPow::initAuxPow(block);
    BOOST_REQUIRE(block.auxpow != nullptr);
    BOOST_CHECK(block.IsAuxpow());
    BOOST_CHECK(block.GetHash() == childHash);
    BOOST_CHECK(::GetSerializeSize(
        block, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) > 80U);
}

BOOST_AUTO_TEST_SUITE_END()
