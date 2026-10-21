# Independent PoW/AuxPoW DAA on FloofyChain

At mainnet height 245000, FloofyChain enables strict AuxPoW verification,
a five-minute whole-chain block target and **independent ASERT difficulty
for native Scrypt PoW and AuxPoW**.

Unlike the old strict-alternation prototype, both mining types remain
eligible for every next block. Each lane uses its latest accepted block
timestamp and its count of accepted post-fork blocks. The fixed starting
anchor is block 244999. Each lane targets 600 seconds per accepted block.
The new rule supports any valid order (PoW/PoW/AuxPoW/PoW/etc.).

After each accepted block, the node looks at the last **1000 accepted
post-fork blocks** and counts the mining-type split. It then adjusts the
next ASERT target for both eligible mining methods with a deterministic,
integer-only multiplier. The majority type's difficulty rises, and the
minority type's falls, by a factor of at most 2. During the first 1000
post-fork blocks the factor builds up gradually, then slides one block
at a time. The bias uses block types only; ASERT still uses accepted times.

The two mining histories are branch-local and reconstructed in memory
on startup, avoiding a persistent database schema change.

### Mining RPC behaviour

- Native getblocktemplate jobs are allowed on either type of parent.
- AuxPoW createauxblock/getauxblock/submitauxblock jobs are allowed
  irrespective of the last accepted mining type.
- Completed AuxPoW submissions still need valid parent proofs and
  merge-mining commitments.
- generate 1 1000000 0 or generate 1 1000000 1 on isolated regtest
  demonstrates mining of native or synthetic AuxPoW blocks.
- As with any accepted-parent DAA, a complete no-block stall does not
  automatically reduce difficulty. Neither mining type must wait for the
  other, but mining remains probabilistic.

### Limitations

A 50/50 split is a long-term target, **not a consensus quota**. A
miner participating in both methods can potentially earn from both.
Timestamp manipulation of *mining eligibility* is impossible because
eligibility is unconditional, but accepted timestamps still influence
future difficulty. Real merge-mining Stratum, chainwork/reorg security
and deployment should receive separate review before production use.
