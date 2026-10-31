🐾 FloofyChain (FLOOF)
A Scrypt cryptocurrency where native Proof-of-Work and AuxPoW merge miners compete for every block.

FloofyChain is an open-source, Scrypt-based Layer 1 cryptocurrency featuring native PoW mining, AuxPoW merged mining, adaptive difficulty, scheduled mining-reward multipliers, and voluntary on-chain coin burning. The V4 network uses a shared ASERT difficulty baseline with a capped balancing adjustment designed to give underrepresented mining methods a better chance of finding blocks — without enforcing a 50/50 split.

Website · Block Explorer · Source Code
V4 network notice: Older clients using the previous mainnet network identifier are incompatible with the upgraded V4 peer network. Back up your wallet before installing new software or resetting blockchain data. See Upgrading to V4 below.

⚙️ Network at a glance
Property	FloofyChain
Coin / ticker	FloofyChain / FLOOF
Source-code client version	4.0.4 (V4 series)
Mining algorithm	Scrypt
Mining methods	Native PoW and AuxPoW (merged mining)
Target block interval	300 seconds (5 minutes) under the upgraded rules
Difficulty adjustment	One shared anchored ASERT baseline, plus per-method rolling balance
ASERT half-life	3,600 seconds (1 hour)
Balancing window	Previous 1,000 accepted blocks
Mining eligibility	Both methods may mine every block; no alternation or quota
AuxPoW chain ID	666 (0x029A)
Mainnet P2P port	6661
New mainnet PCH / message start	d4 6c b2 91
Mining consensus transition	Block 245,000
Shared-ASERT transition	Block 245,122
Minimum-burn activation	Block 250,000, minimum 1,000 FLOOF
Fees	No automatic transaction tax, developer tax, or mandatory burn tax
Supply / rewards	Ongoing block subsidies with scheduled reward-multiplier events


Block intervals are a target average, not a guarantee: individual blocks may arrive earlier or later.
⛏️ Native PoW and AuxPoW: real competition, no forced turns
FloofyChain supports two ways to find a valid block:
- Native PoW: Miners perform Scrypt work directly against the FloofyChain block target.
- AuxPoW: Merge miners commit to a FloofyChain block through a valid parent-chain proof of work.
Both methods are eligible for every next block. A PoW miner can find several consecutive blocks; so can an AuxPoW miner. The protocol does not alternate methods, reserve mining slots, or wait for a method-specific timeout.
How V4 difficulty balancing works
1. Shared ASERT follows the timing of the whole accepted blockchain, aiming for a five-minute average. When overall blocks arrive too quickly, the shared difficulty rises; when too slowly, it falls as blocks are accepted.
2. Rolling balance counts how many of the previous 1,000 accepted blocks were found by each method.
3. If AuxPoW has won more blocks, native PoW receives an easier target and AuxPoW a harder one. If native PoW dominates, the adjustment reverses.
4. If block counts are equal, the balancing adjustment is neutral.
The adjustment is deliberately capped. At maximum dominance, the underrepresented method's target may become up to 2× easier, while the dominant method's target may become up to 2× harder, subject to the network proof-of-work limit. In difficulty terms, that is roughly half the base difficulty for the minority method and twice the base difficulty for the majority method.

This is not a 50/50 rewards promise. A 1 GH/s native miner can still be heavily outmatched by a 1 TH/s merge-mining pool. The system improves the smaller miner's relative chance of winning a block; it does not guarantee one, or seek to make unequal hashrates equal.
ASERT can adjust the target only after a valid block is accepted. If a large pool disappears suddenly, the first replacement block may still take considerably longer than five minutes.

📈 Shared ASERT difficulty
The upgraded difficulty calculation uses:
- A single anchored ASERT baseline shared by both mining methods.
- A 300-second expected interval for each accepted block in the shared stream.
- A one-hour half-life for responding to accumulated block-time error.
- Accepted-chain history, not the candidate block's timestamp, to set the candidate's own difficulty.
- An additional, deterministic 1,000-block balancing multiplier chosen by the candidate's mining method.
The calculation is performed for the next candidate block each time the chain tip changes. No fixed clock timer changes difficulty independently of accepted blocks.
Consensus activation history
Mainnet height	Consensus change
Through 244,999	Historical rules retained.
245,000	New strict AuxPoW validation and the initial anchored adaptive, dual-method difficulty rules activate.
245,121	Pinned checkpoint anchoring the selected upgraded chain history.
245,122	Shared five-minute ASERT baseline plus capped rolling PoW/AuxPoW balance replaces the initial independent difficulty lanes.
250,000	Consensus-enforced 1,000 FLOOF minimum for canonical burns.


The checkpoint at 245,121 identifies the intended V4 chain history. The upgraded release must agree on the checkpoint and the same consensus settings across all nodes.
🔐 AuxPoW validation and network separation
V4 enforces strict merged-mining proof validation, including the expected chain ID, AuxPoW commitment structure, parent proof of work, and the appropriate block target.

The upgraded mainnet also uses the P2P message-start bytes d4 6c b2 91, replacing the old a3 f1 c7 9e identifier. Older nodes may continue mining independently, but their P2P messages are rejected by the upgraded network.

Changing PCH isolates P2P networks; consensus rules and checkpoints determine block validity. Miners, wallets, pools, and explorers should upgrade to a compatible V4 build and connect to verified V4 peers.
🎉 Block rewards and multiplier events
FloofyChain distributes ongoing block subsidies and applicable transaction fees to miners. Selected block ranges receive scheduled block-reward multiplier events. The exact reward depends on the block height and the active consensus rules.

Community participation in future reward events remains part of the project roadmap. Any planned community-voting milestone is separate from the V4 mining-consensus activation heights.
🔥 Voluntary burning and verified leaderboard
FloofyChain offers optional, irreversible FLOOF burning through provably unspendable OP_RETURN outputs. Burning is a voluntary user action, not a tax charged on transfers or mining rewards.

From mainnet block 250,000, consensus rejects canonical burn transactions below 1,000 FLOOF. Network transaction fees are additional to the burned amount.

The Qt wallet's Burn page provides:
Feature	Description
Provable burn	Permanently removes FLOOF from spendable circulation using OP_RETURN.
Minimum amount	1,000 FLOOF after the activation height.
Burn preview	Inspect the transaction before irreversible submission.
Optional burn message	Up to 48 printable ASCII bytes, including any leaderboard marker.
Verified attribution	A special signed-input burn can attribute eligible burns to a wallet-controlled funding address.
All-time ranking	Confirmed, address-verified leaderboard burns.
Recent rankings	Last 30 days and last 7 days.
Leaderboard refresh	Automatic Qt refresh every 10 minutes; manual refresh also available.
Global statistics	Includes legacy supported burn methods, even when not eligible for address attribution.


Burn from Qt: Open the wallet, select Burn, choose an amount, optionally choose a funded leaderboard address and message, preview the transaction, and confirm. Confirmed eligible burns appear in the relevant leaderboard after a scan.
Burn RPC examples
# Total confirmed network burns (requires full historical blocks)
floofy-cli getburnedtotal

# Verified leaderboards
floofy-cli getburnleaderboard 0   # All time
floofy-cli getburnleaderboard 30  # Last 30 days
floofy-cli getburnleaderboard 7   # Last 7 days

# Preview a regular 1,000 FLOOF burn; does not broadcast
floofy-cli burn 1000 false "" true

# Preview with signed-input leaderboard attribution (fifth parameter)
floofy-cli burn 1000 false "" true "YOUR_FLOOF_ADDRESS"
Warning: Changing the preview argument (true) to false deliberately submits an irreversible burn. Always verify the amount, funding address, and fee first. Full historical leaderboard results require locally available, unpruned block data.
🔄 Upgrading to V4 — important
Existing users must move from the old peer network to V4 and synchronise with upgraded peers. Due to the recovered mainnet branch and changed PCH, old chain data or saved peers may point to an incompatible network.

Protect your wallet first:
1. Stop FloofyChain completely and confirm the wallet/daemon has exited.
2. Back up wallet.dat and floofy.conf to a separate safe location. If your installation uses a wallets/ directory or additional wallet files, back up all of them. Keep any keys, recovery material and offline backups. Do not share backups or RPC credentials.
3. Confirm the backups are readable and safely stored before removing or renaming anything.
4. With the application closed, move aside or remove old blockchain and network caches, such as blocks/, chainstate/, optional index directories, and peers.dat. Do not delete the whole data directory: that can erase wallet.dat, wallets/, configuration and other important user data. Renaming old data for later recovery is safer than permanent deletion.
5. Install the compatible V4 release, keep or restore your wallet and correct configuration, and ensure you connect to verified upgraded V4 nodes. Remove outdated addnode/`connect` entries pointing at old-network peers.
6. Launch the node and allow a full resynchronisation. Do not send funds or depend on an unconfirmed balance until the wallet and node have caught up with the correct V4 chain.
Never delete wallet.dat, the wallets/ directory, private keys, seed/recovery material, or your only backup. The network update does not require generating a new wallet address or abandoning existing keys.

To confirm the upgraded tip on a command-line node:
floofy-cli getblockchaininfo
floofy-cli getbestblockhash
floofy-cli getpeerinfo
Compare the best block hash and height with another trusted V4 node once synchronised. If you have wallet funds or run a mining pool/exchange, keep a recoverable copy of your old data until the migration is validated.
🧪 Testing and validation
V4 development has included tests of native PoW and AuxPoW acceptance, ASERT calculations, rolling dominance adjustments, hash-rate and block-time changes, node synchronisation, competing-chain reorganisations, restart behaviour, and simulated two-miner competition.

These tests improve confidence but are not a guarantee against all future faults. Operators should monitor live block timing, tip hashes, peers, and mining distribution after upgrading.
🤖 Ecosystem and community
The FloofyChain ecosystem includes a blockchain explorer, Qt wallet, community Discord bot, and additional community-oriented tools. Features may require separate services, compatible software, and their own configuration.
- Official site: https://floofychain.com
- Block explorer: https://explorer.floofychain.com
- Main source repository: https://github.com/andyd1986/floofychain
- Consensus parameters: [`src/chainparams.cpp`](src/chainparams.cpp)
- Difficulty and balancing: [`src/pow.cpp`](src/pow.cpp)
- Block validation: [`src/validation.cpp`](src/validation.cpp)
💜 Support FloofyChain — Donations
FloofyChain is a community-driven project. If you would like to help support ongoing development, community activities, infrastructure and outreach, voluntary donations are appreciated.
Coin	Donation address
FLOOF	FBgtAgpr7LsChn3dggAKSUrNiJhdRHSHAB
DOGE	DGpnrvtjnhaF866XKbVUpVAJsBJDKZdr6c
LTC	MCREWjsAVoNzzRAijPWfgRT26LniSBaLH1
BTC	3GUraGiEyvgX58cU45qRme6fdYNPM5vN5i
ETH	0x70Ad65A242CB24cB648A7f81024EC0dA9db61024


Donation allocation
Allocation	Share	Purpose
Team	20%	Support the people helping operate and maintain FloofyChain.
Development	30%	Core improvements, wallet development, software maintenance and technical infrastructure.
Community	30%	Community programmes, events and ecosystem initiatives.
Marketing	20%	Artwork, communications, media and promotional activities.
Total	100%	


These percentages describe the project's intended allocation of donated funds. They are not an automatic blockchain distribution, transaction tax, or deduction from mining rewards. Always verify the destination address before sending; cryptocurrency donations may be irreversible. Send each asset only on its appropriate network.

🐾 Contributors & Credits
A special thank-you to the people contributing to FloofyChain:
- @andyd1986 — Development: Core blockchain and wallet development, protocol improvements, and technical contributions.
- @eddyc — Graphics, Artwork, Marketing & Media: Floofy visual designs, graphics, artwork, promotional media and marketing contributions.
Your support helps keep FloofyChain growing. Thank you, Floofonauts! 💙
What a bunch of floof. 🐾 Keep mining, Floofonauts!