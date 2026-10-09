
#ifndef FLOOFY_BURN_H
#define FLOOFY_BURN_H

#include "amount.h"
#include "primitives/transaction.h"
#include "script/script.h"

#include <stdexcept>
#include <string>
#include <vector>

// Versioned FloofyChain voluntary burn protocol.
// No consensus-rule changes are required.

static const std::string FLOOF_BURN_TAG = "FLOOF:BURN:1";
static const size_t FLOOF_BURN_MAX_MESSAGE = 48;

static inline bool IsValidFloofyBurnMessage(
    const std::string& message)
{
    if (message.size() > FLOOF_BURN_MAX_MESSAGE)
        return false;

    for (unsigned char c : message) {
        if (c < 32 || c > 126)
            return false;
    }

    return true;
}

static inline CScript FloofyBurnScript(
    const std::string& message)
{
    if (!IsValidFloofyBurnMessage(message))
        throw std::runtime_error(
            "Burn message must contain at most "
            "48 printable ASCII bytes");

    std::string payload = FLOOF_BURN_TAG;

    if (!message.empty()) {
        payload += ":";
        payload += message;
    }

    std::vector<unsigned char> data(
        payload.begin(),
        payload.end()
    );

    return CScript() << OP_RETURN << data;
}

static inline bool ParseFloofyBurn(
    const CTxOut& output,
    std::string& message)
{
    message.clear();

    if (output.nValue <= 0)
        return false;

    const CScript& script = output.scriptPubKey;

    if (script.empty() || script[0] != OP_RETURN)
        return false;

    CScript::const_iterator pc = script.begin();

    opcodetype opcode;
    std::vector<unsigned char> data;

    if (!script.GetOp(pc, opcode) || opcode != OP_RETURN)
        return false;

    if (!script.GetOp(pc, opcode, data))
        return false;

    if (pc != script.end())
        return false;

    // Accept only canonical single-push encoding.
    const CScript canonical =
        CScript() << OP_RETURN << data;

    if (script != canonical)
        return false;

    const std::string payload(data.begin(), data.end());

    if (payload == FLOOF_BURN_TAG)
        return true;

    const std::string prefix = FLOOF_BURN_TAG + ":";

    if (payload.compare(0, prefix.size(), prefix) != 0)
        return false;

    message = payload.substr(prefix.size());

    if (message.empty() ||
        !IsValidFloofyBurnMessage(message)) {
        message.clear();
        return false;
    }

    return true;
}

#endif // FLOOFY_BURN_H
