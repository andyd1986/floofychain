// Copyright (c) 2022 The Floofy Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Utility functions for RPC commands
 */
#ifndef IllUCOIN_WALLET_UTIL_H
#define IllUCOIN_WALLET_UTIL_H

#include "fs.h"
#include "util.h"

fs::path GetBackupDirFromInput(std::string strUserFilename);

#endif // IllUCOIN_WALLET_UTIL_H
