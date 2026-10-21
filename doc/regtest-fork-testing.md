# FloofyChain 3.0.3: isolated regtest fork map

| Regtest height | Consensus |
|---|---|
| 10 | DigiShield |
| 20 | Historical AuxPoW path |
| 30 | Strict AuxPoW proof validation |
| 40 | Parent-anchored ASERT-style difficulty |
| 50 | Two independent always-eligible PoW/AuxPoW difficulty streams and rolling 1000-block balancing |

Mainnet consolidates these proposed changes at block **245000**.
Regtest fork heights remain short to make testing repeatable.

After regtest block 50, **either** mining type can produce the next
valid block. Their next difficulty targets also incorporate the relative
shares of PoW and AuxPoW in the most recent 1000 accepted post-fork blocks.
Run one of the following from the experimental build's src
directory, using your dedicated regtest RPC configuration:

```bash
./floofy-cli -regtest -datadir=/root/floofy-regtest-forks/regtest -conf=floofy.conf generate 1 1000000 0
./floofy-cli -regtest -datadir=/root/floofy-regtest-forks/regtest -conf=floofy.conf generate 1 1000000 1
```

The last parameter is 0 for native PoW, 1 for locally generated
synthetic AuxPoW. Both paths can mine several consecutive blocks.
Real pool integration uses createauxblock and submitauxblock.

Test locally before deploying a mainnet binary:

```bash
./src/test/test_floofy --run_test=pow_tests --report_level=detailed
./src/test/test_floofy --run_test=auxpow_tests --report_level=detailed
```

Keep regtest data and production blockchain databases separate.
