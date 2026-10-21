# FloofyChain 3.0.3 — consensus activation at block 245000

Mainnet activation: **245000** (prior rules through 244999).
All new rules activate together:
- Strict AuxPoW verification (historical compatibility shortcut disabled).
- Parent-accepted-timestamp anchored ASERT-style difficulty with one-hour half-life.
- Native Scrypt PoW and AuxPoW may both mine the next block, including
  several consecutive blocks of the same type.
- Each mining method has its own accepted-block count and difficulty
  history, targeting 600 seconds per block for that mining type.
- With steady participation from both, the combined target is 300 seconds.
- A block-count-based rolling 1000-block balancing adjustment, evaluated
  after every accepted block, pushes underrepresented types towards a larger
  (easier) target and overrepresented types towards a smaller (harder) target.
  The adjustment is bounded between 0.5x and 2x the independent ASERT target.
  On the fork it starts neutral and builds proportionally as blocks arrive.
  Once the window fills, only the last 1000 accepted blocks count.

Both streams share the accepted block at 244999 as their initial fixed anchor.
No hard 50/50 quota, enforced alternation or 30-minute timeout is used.
Block-count balancing uses only accepted block types, not block timestamps.
The aim is approximately 50/50 when both types are actively mining.

The memory-only histories are stored on CBlockIndex, are calculated per
branch and are rebuilt from accepted block indexes on node restart.
Block serialization and existing block history remain unchanged.

**Important:** An activation at 245000 cannot rescue a chain stopped
below that height. Before activation the historical rules and difficulty
continue unchanged. An ASERT target cannot ease until another valid
block is accepted. Acceptance-based DAA timestamps remain subject to
normal consensus limits but do not provide zero timestamp influence.
Per-lane difficulty is not proof of perfectly equal economic rewards or
of reorganization resistance when two mining types share chainwork.

Node version 3.0.3 is configured in src/clientversion.h,
src/config/bitcoin-config.h and the authoritative configure.ac.
Regenerate the generated configuration locally when building.

For regtest and tests, see doc/regtest-fork-testing.md.
