
#include "burn.h"
#include "amount.h"
#include "base58.h"
#include "coins.h"
#include "policy/policy.h"
#include "primitives/transaction.h"
#include "script/script.h"
#include "script/standard.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

#include <stdexcept>
#include <string>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(burn_tests, BasicTestingSetup)

// --------------------------------------------------
// Test 1: Basic burn without a message
// --------------------------------------------------

BOOST_AUTO_TEST_CASE(burn_basic)
{
    const CAmount amount = 1000 * COIN;

    const CScript script = FloofyBurnScript("");

    const CTxOut output(amount, script);

    std::string message = "old value";

    BOOST_CHECK(ParseFloofyBurn(output, message));

    BOOST_CHECK(message.empty());

    BOOST_CHECK_EQUAL(output.nValue, amount);

    BOOST_CHECK(script.IsUnspendable());
}

// --------------------------------------------------
// Test 2: Burn with a user-controlled message
// --------------------------------------------------

BOOST_AUTO_TEST_CASE(burn_with_message)
{
    const std::string original =
        "For the Floofy community";

    const CScript script =
        FloofyBurnScript(original);

    const CTxOut output(5000 * COIN, script);

    std::string decoded;

    BOOST_CHECK(ParseFloofyBurn(output, decoded));

    BOOST_CHECK_EQUAL(decoded, original);

    BOOST_CHECK(script.IsUnspendable());
}

// --------------------------------------------------
// Test 3: Maximum permitted message length
// --------------------------------------------------

BOOST_AUTO_TEST_CASE(burn_maximum_message)
{
    const std::string message(
        FLOOF_BURN_MAX_MESSAGE, 'F'
    );

    BOOST_CHECK(IsValidFloofyBurnMessage(message));

    const CScript script =
        FloofyBurnScript(message);

    const CTxOut output(100 * COIN, script);

    std::string decoded;

    BOOST_CHECK(ParseFloofyBurn(output, decoded));

    BOOST_CHECK_EQUAL(decoded, message);
}

// --------------------------------------------------
// Test 4: Reject messages exceeding the limit
// --------------------------------------------------

BOOST_AUTO_TEST_CASE(burn_oversized_message)
{
    const std::string message(
        FLOOF_BURN_MAX_MESSAGE + 1, 'F'
    );

    BOOST_CHECK(!IsValidFloofyBurnMessage(message));

    BOOST_CHECK_THROW(
        FloofyBurnScript(message),
        std::runtime_error
    );
}

// --------------------------------------------------
// Test 5: Reject invalid message characters
// --------------------------------------------------

BOOST_AUTO_TEST_CASE(burn_invalid_characters)
{
    const std::string newlineMessage = "Hello\nFloofy";

    BOOST_CHECK(
        !IsValidFloofyBurnMessage(newlineMessage)
    );

    BOOST_CHECK_THROW(
        FloofyBurnScript(newlineMessage),
        std::runtime_error
    );

    const std::string nullMessage("A\0B", 3);

    BOOST_CHECK(
        !IsValidFloofyBurnMessage(nullMessage)
    );

    const std::string nonAscii("\xC2\xA3", 2);

    BOOST_CHECK(
        !IsValidFloofyBurnMessage(nonAscii)
    );
}

// --------------------------------------------------
// Test 6: Reject outputs containing zero FLOOF
// --------------------------------------------------

BOOST_AUTO_TEST_CASE(burn_zero_amount)
{
    const CScript script = FloofyBurnScript("");

    const CTxOut output(0, script);

    std::string message;

    BOOST_CHECK(
        !ParseFloofyBurn(output, message)
    );
}

// --------------------------------------------------
// Test 7: Reject negative output amounts
// --------------------------------------------------

BOOST_AUTO_TEST_CASE(burn_negative_amount)
{
    const CScript script = FloofyBurnScript("");

    const CTxOut output(-1, script);

    std::string message;

    BOOST_CHECK(
        !ParseFloofyBurn(output, message)
    );
}

// --------------------------------------------------
// Test 8: Reject ordinary transactions
// --------------------------------------------------

BOOST_AUTO_TEST_CASE(burn_normal_transaction)
{
    const CScript script =
        CScript() << OP_TRUE;

    const CTxOut output(1000 * COIN, script);

    std::string message;

    BOOST_CHECK(
        !ParseFloofyBurn(output, message)
    );

    BOOST_CHECK(!script.IsUnspendable());
}

// --------------------------------------------------
// Test 9: Reject OP_RETURN with incorrect marker
// --------------------------------------------------

BOOST_AUTO_TEST_CASE(burn_invalid_marker)
{
    const std::string invalidTag =
        "NOT:FLOOF:BURN";

    const std::vector<unsigned char> payload(
        invalidTag.begin(),
        invalidTag.end()
    );

    const CScript script =
        CScript() << OP_RETURN << payload;

    const CTxOut output(100 * COIN, script);

    std::string message;

    BOOST_CHECK(
        !ParseFloofyBurn(output, message)
    );
}

// --------------------------------------------------
// Test 10: Reject additional script operations
// --------------------------------------------------

BOOST_AUTO_TEST_CASE(burn_extra_opcode)
{
    CScript script = FloofyBurnScript("");

    script << OP_DROP;

    const CTxOut output(100 * COIN, script);

    std::string message;

    BOOST_CHECK(
        !ParseFloofyBurn(output, message)
    );
}

// --------------------------------------------------
// Test 11: Reject noncanonical push encoding
// --------------------------------------------------

BOOST_AUTO_TEST_CASE(burn_noncanonical_push)
{
    const std::string tag = FLOOF_BURN_TAG;

    CScript script;

    script << OP_RETURN;
    script << OP_PUSHDATA1;

    script.push_back(
        static_cast<unsigned char>(tag.size())
    );

    script.insert(
        script.end(),
        tag.begin(),
        tag.end()
    );

    const CTxOut output(100 * COIN, script);

    std::string message;

    BOOST_CHECK(
        !ParseFloofyBurn(output, message)
    );
}

// --------------------------------------------------
// Test 12: Legacy address must remain separate
// --------------------------------------------------

BOOST_AUTO_TEST_CASE(burn_legacy_address)
{
    const std::string legacyAddress =
        "F9116Y1RWWyHEgP4TC5e5vtcvhjdf1f6fg";

    const CBitcoinAddress address(legacyAddress);

    BOOST_REQUIRE(address.IsValid());

    const CScript script =
        GetScriptForDestination(address.Get());

    const CTxOut output(5000 * COIN, script);

    std::string message;

    // Legacy transfers are not OP_RETURN burns.
    BOOST_CHECK(
        !ParseFloofyBurn(output, message)
    );

    BOOST_CHECK(!script.IsUnspendable());
}

// --------------------------------------------------
// Test 13: Burned outputs excluded from UTXO set
// --------------------------------------------------

BOOST_AUTO_TEST_CASE(burn_utxo_exclusion)
{
    CMutableTransaction mutableTx;

    mutableTx.vout.push_back(
        CTxOut(
            1000 * COIN,
            FloofyBurnScript("")
        )
    );

    mutableTx.vout.push_back(
        CTxOut(
            500 * COIN,
            CScript() << OP_TRUE
        )
    );

    const CTransaction tx(mutableTx);

    CCoins coins(tx, 100);

    BOOST_REQUIRE_EQUAL(coins.vout.size(), 2U);

    // OP_RETURN output is removed.
    BOOST_CHECK(coins.vout[0].IsNull());

    // The ordinary output remains in the UTXO set.
    BOOST_CHECK(!coins.vout[1].IsNull());

    BOOST_CHECK_EQUAL(
        coins.vout[1].nValue,
        500 * COIN
    );
}

// --------------------------------------------------
// Test 14: Standard relay policy
// --------------------------------------------------

BOOST_AUTO_TEST_CASE(burn_standard_policy)
{
    const CScript script =
        FloofyBurnScript("Floofy burn test");

    txnouttype outputType = TX_NONSTANDARD;

    BOOST_CHECK(
        IsStandard(script, outputType, false)
    );

    BOOST_CHECK(outputType == TX_NULL_DATA);

    const CTxOut output(100 * COIN, script);

    BOOST_CHECK(!output.IsDust(DEFAULT_DUST_LIMIT));
}

// --------------------------------------------------
// Test 15: Multiple independent burn amounts
// --------------------------------------------------

BOOST_AUTO_TEST_CASE(burn_multiple_amounts)
{
    const CAmount amounts[] = {
        1,
        1 * COIN,
        100 * COIN,
        1000 * COIN,
        1000000 * COIN
    };

    CAmount total = 0;

    for (const CAmount amount : amounts)
    {
        const CTxOut output(
            amount,
            FloofyBurnScript("")
        );

        std::string message;

        BOOST_CHECK(
            ParseFloofyBurn(output, message)
        );

        total += output.nValue;
    }

    const CAmount expected =
        1 +
        1 * COIN +
        100 * COIN +
        1000 * COIN +
        1000000 * COIN;

    BOOST_CHECK_EQUAL(total, expected);
}

// --------------------------------------------------
// Test 16: Empty message and malformed suffix
// --------------------------------------------------

BOOST_AUTO_TEST_CASE(burn_invalid_empty_suffix)
{
    const std::string invalidPayload =
        FLOOF_BURN_TAG + ":";

    const std::vector<unsigned char> data(
        invalidPayload.begin(),
        invalidPayload.end()
    );

    const CScript script =
        CScript() << OP_RETURN << data;

    const CTxOut output(100 * COIN, script);

    std::string message;

    BOOST_CHECK(
        !ParseFloofyBurn(output, message)
    );

    BOOST_CHECK(message.empty());
}

BOOST_AUTO_TEST_SUITE_END()
