# testnet4 reset — 2026-09-23

testnet4 restarts from a new genesis block at `1790186400`
(2026-09-23 18:00:00 UTC). The existing chain, at h~19100, is abandoned. Coins
mined on it are gone; they were test coins.

## Why

`CalculateNextWorkRequired()` computed the next target as
`sum_target * weighted_solvetime_sum / denominator`, doing the multiplication
first to keep precision. On a chain whose `powLimit` is permissive — testnet3,
testnet4 and signet all use `0x7fff...` — `sum_target` reaches ~2^254 and that
multiply wraps mod 2^256, producing an arbitrary target. Fixed in `917d097`.

testnet4 has been in that state since the beginning. Height 90 is the first
block at which the 90-block LWMA window is full, and height 90 is itself a
min-difficulty block, so the window has overflowed from block 91 onward. Block
91 carries nBits `1e220aaa`; the corrected rule computes `200ccccc` for it.
`ContextualCheckBlockHeader()` requires `nBits` to equal `GetNextWorkRequired()`
exactly, so a node built from `917d097` or later rejects testnet4 at height 91
and cannot sync the chain at all.

Keeping the existing chain would mean keeping the overflow as a consensus rule
below some activation height. That is not worth doing to preserve a test chain.

## What else changes

`fPowAllowMinDifficultyBlocks` becomes false on testnet4.

The rule — a block arriving more than 2x the target spacing late may be mined at
`powLimit` — is inherited from Bitcoin, where testnet retargets only every 2016
blocks and a departing miner can strand the chain for weeks. LWMA retargets
every block, so the situation it exists for largely does not arise.

What the rule does instead is write `powLimit` nBits into the 90-block averaging
window, where it dominates the mean for its full residency. Simulating the fixed
retarget: one late block inflates the target by ~607000x, and 89 of the next 90
blocks solve in under ten seconds. Ninety blocks that should take three hours
take six seconds. On the live chain, 112 of the 251 blocks between 18777 and
19027 carried `207fffff`.

The overflow was masking this. A wrapped target came out harder than intended --
at h19091 the wrapped value was `1e0dceb5` against a correct `203e745b` -- so
the bug was working against the rule's collapse. Fixing one without removing the
other would make block storms considerably worse than what the chain shows
today.

Against that, the stall the rule exists to prevent is short under LWMA. Modelled
recovery with the rule off: a 2x hashrate loss recovers in one block, 5x in 29
blocks (~2.9h), 10x in 39 blocks (~5.3h).

Mainnet has the rule off. Turning it off on testnet4 means both chains run the
same difficulty code, which is most of the value of having a test chain before a
launch.

Network magic changes from `fac4b8d4`, so old and new nodes do not connect and
then disagree about genesis. The P2P port stays 49555.

## What operators need to do

1. Pull master and rebuild. The reset commit (new genesis, new magic, the flag
   change) lands before 18:00 UTC on the 23rd; building earlier than that gets
   you a node that cannot sync either chain.
2. Stop your node.
3. Back up `wallet.dat` or your wallet directory first if you want it, then
   delete the testnet4 data directory — `~/.truenorth/testnet4` by default.
   Addresses and keys survive; the coins do not.
4. Restart after 18:00 UTC.

A node that is not upgraded stays on the old chain and will not find peers on
the new one.

## Mining

The genesis timestamp is the reset time itself, which gates the chain: a block
must be timestamped after its parent's median-time-past and no more than two
hours ahead of the clock, so no block can be mined before ~16:00 UTC whatever
anyone does. Upgrading and restarting early is therefore safe — nodes will sit
at height 0 until the window opens rather than racing.

Difficulty starts at `powLimit` and the first 90 blocks are mined there, since
the LWMA has no window to average until height 90. Expect the first blocks to
come fast and the target to settle over the following couple of hours. Nothing
to configure.

## Mainnet

Unaffected. Mainnet's `powLimit` is 2^224 and the widest possible LWMA multiply
there is 2^245.5, so the overflow was never reachable and `917d097` changes no
mainnet behaviour. The launch target remains 2026-10-05.
