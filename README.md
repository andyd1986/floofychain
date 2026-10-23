# 🐾 FloofyChain (FLOOF)

FloofyChain is a Scrypt-based cryptocurrency supporting both **native Proof-of-Work (PoW)** and **Auxiliary Proof-of-Work (AuxPoW)** mining.

The network uses block rewards, scheduled reward multiplier events, and a mining consensus designed to balance opportunities between native and merge miners.

**Website:** https://floofychain.com  
**Block Explorer:** https://explorer.floofychain.com

## ⚙️ Key Features

- **Coin:** FloofyChain (FLOOF)
- **Core version:** 3.0.3
- **Algorithm:** Scrypt
- **Mining:** Native PoW and AuxPoW (merge mining)
- **Block target:** 5 minutes under the new consensus
- **Mining distribution:** approximately 50/50 long-term target when both mining types participate
- **Difficulty:** Independent ASERT plus a rolling 1,000-block balance
- **AuxPoW chain ID:** 666
- **Fees:** No transaction tax, automatic burn fee, or developer fee
- **Rewards:** Block rewards with scheduled multiplier events

## ⛏️ Independent Native PoW and AuxPoW Mining

Both native Scrypt PoW and AuxPoW miners can compete to mine **every block**. There is **no forced alternation or waiting period**.

Each mining type has a separate ASERT difficulty target designed for roughly one block every 10 minutes when participation is steady. A **rolling 1,000-block balancing adjustment** increases the difficulty of whichever type has mined more of the recent blocks and reduces it for the underrepresented type. With both types active, the combined aim is a **five-minute average** and approximately **50/50 over time**, not a mandatory quota.

If a merge-mining pool goes offline, native miners can continue mining without waiting for a timeout. Likewise, AuxPoW miners can keep mining when native miners are unavailable. Every block still needs a valid proof of work.

## 📈 ASERT Difficulty Adjustment

FloofyChain uses **separate anchored ASERT-style difficulty calculations** for native PoW and AuxPoW:

- **5-minute combined block target** when both mining types have stable participation.
- **10-minute average target per mining type** (600 seconds).
- **Independent histories:** difficulty uses the previous accepted blocks of its own type, including runs of consecutive blocks.
- **1-hour half-life:** difficulty adjusts gradually as block production runs ahead of or behind schedule.
- **Common starting anchor:** both difficulty calculations start from block **244,999**.

After **every accepted block**, each mining type's next target is recalculated using its own ASERT history and the preceding **1,000 accepted blocks**. The balancing history starts at block 245,000 and grows gradually until full; it then slides forward one block at a time. When the last 1,000 blocks are evenly split, the balancing factor is neutral. At its maximum, a lane's additional adjustment ranges from **half to twice its ASERT target**. The block-count balancing step never uses candidate timestamps. Accepted historical timestamps still inform ASERT.

## 🔐 Strict AuxPoW Validation

AuxPoW blocks must provide a valid merged-mining commitment and parent proof of work. The new consensus verifies the AuxPoW structure, chain ID, and required difficulty instead of relying on the historical compatibility shortcut.

## 🔄 Consensus Fork — Block 245,000

The new mining rules activate together at **mainnet block 245,000**.

| Activation | Change |
|---|---|
| **245,000** | Strict AuxPoW proof validation |
| **245,000** | Anchored ASERT difficulty and a five-minute combined block target |
| **245,000** | Independent native PoW and AuxPoW difficulty calculations |
| **245,000** | Rolling 1,000-block capped balancing adjustment |
| **245,000** | Both mining types remain eligible for consecutive blocks |

Blocks through **244,999** retain their historical rules. Block **244,999** anchors both new difficulty histories. Nodes and mining pools need compatible software for the consensus transition.

A height-based upgrade cannot change difficulty or unblock mining below its activation height.

## 🎉 Mining Rewards

FloofyChain rewards miners through block subsidies and transaction fees, with **scheduled multiplier events** during selected block ranges.

- Reward multiplier events are scheduled at 20,000-block intervals.
- Regular event windows last approximately 5,000 blocks.
- Community involvement in future multiplier events is part of the project roadmap.

The **mining consensus fork at block 245,000** is separate from the roadmap's community-voting milestone associated with **block 500,000**.

## 🤖 Community

FloofyChain also has a Discord wallet bot and community tools for activities such as tipping, wallet balances, and other supported services.

For additional protocol information, see [the consensus source](src/chainparams.cpp), [difficulty implementation](src/pow.cpp), and [block validation](src/validation.cpp).

---

**What a bunch of floof.** 🐾
## 🔥 FloofyChain Voluntary Burning & Leaderboard

FloofyChain includes a voluntary coin-burning system that allows holders to permanently remove FLOOF from circulation through provably unspendable blockchain transactions.

The wallet also features a **Verified Burn Leaderboard**, giving the community a transparent way to track individual burning contributions.

### Features

| Feature | Description |
|---|---|
| Provable burning | FLOOF permanently removed using OP_RETURN |
| Minimum burn | 1,000 FLOOF under the active consensus rule |
| Verified addresses | Signed transaction inputs establish funding-address attribution |
| All-time leaderboard | Top verified burners throughout blockchain history |
| 30-day leaderboard | Rolling monthly rankings |
| 7-day leaderboard | Rolling weekly rankings |
| Transaction information | Latest TXID, message, burn count and total |
| Automatic refresh | Every 10 minutes |
| Manual refresh | Available from the Burn page |
| Global statistics | Existing network-wide burned totals preserved |

### How to Burn FLOOF

1. Open FloofyChain Qt Wallet.
2. Navigate to **Burn**.
3. Enter the FLOOF amount.
4. Optionally select a funded leaderboard address.
5. Enter an optional message within the permitted character limit.
6. Preview the transaction and estimated fee.
7. Confirm the irreversible burn.

Once confirmed on the blockchain, an eligible signed burn contributes to the leaderboard.

### CLI / RPC

**Global burn statistics**

```bash
floofy-cli getburnedtotal
```

**All-time leaderboard**

```bash
floofy-cli getburnleaderboard 0
```

**Last 30 days**

```bash
floofy-cli getburnleaderboard 30
```

**Last 7 days**

```bash
floofy-cli getburnleaderboard 7
```

**Signed burn preview**

```bash
floofy-cli burn 1000 false "" true "YOUR_FLOOF_ADDRESS"
```

Set the preview argument to `false` only when intentionally submitting an irreversible burn.

### Important Notes

- Burning destroys FLOOF permanently.
- Transaction fees are additional to the amount burned.
- Existing unverified historical burns remain included in global totals.
- Only successfully verified, confirmed address-attributed burns appear in the leaderboard.
- The leaderboard does not introduce any new consensus rules.
- Full historical leaderboard scans require locally available blockchain data.

### Blockchain Explorer

Follow confirmed transactions and their OP_RETURN burn records:

https://explorer.floofychain.com

**🔥 Every verified burn counts. Every FLOOF burned is gone forever.**