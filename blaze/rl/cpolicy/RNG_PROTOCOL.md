# RNG protocol: C/CUDA cpolicy sampling

`CPOLICY=1` replaces the torch rollout path (obs float convert + ChainPolicy
forward + fused Gumbel sample) with one C/CUDA fused kernel chain in
`cpolicy_fwd.so`. The PPO update stays in torch and consumes the C path's
`actions` / `logp` / `value` as ordinary tensors.

## What is exactly preserved

**The sampling distribution (Gumbel-max).** For a head with logits `l_i`,

    argmax_i (l_i + G_i),  G_i iid Gumbel(0,1)

selects category `i` with probability `exp(l_i) / sum_j exp(l_j)`. Pad is
not used: each head is sampled over its true width only. Gumbel is formed as

    u ~ Uniform(0,1) via a per-(env, head, category) hash of a uint64 seed
    G = -log(-log u)   with the same 1e-20 clamps as `fused_rollout`

so the distribution claim is the same Gumbel-max theorem as the torch fused
sampler.

**Joint log-probability of the emitted acts.** Computed as
`sum_h (logit[a_h] - logsumexp(logits_h))` inside the sample kernel. The
equivalence gate recomputes the same quantity with torch's
`fused_logp_entropy` on the SAME action tensor and requires agreement
within `9 * 1e-3` absolute (linear accumulation of the per-logit budget).

**Burn-in override ordering.** Sample (or argmax), then overwrite burn-in
lanes with the no-op row, then take log-prob of the final row. Matches the
eager / fused torch order.

**Greedy mode.** `mode=1` is pure argmax per head (no RNG). The equivalence
gate requires >= 99.9% action agreement against torch argmax on a 6144-env
batch; residual disagreements must sit at near-ties (torch top1-top2 gap
< 5e-3).

## What is NOT preserved

**Draw-for-draw equality with torch.** The C path does not call
`torch.rand` and does not share Philox state with the torch generator. Its
uniforms come from a fixed mix64 hash of `(seed, env, head, category)`.
Seeding both paths with `RNG_SEED=0` therefore does NOT give the same action
sequence. A run with `CPOLICY=1` is not replayable from a `CPOLICY=0`
checkpoint at the same seed, and vice versa. The flag belongs in the run's
provenance.

**Consumption of the torch RNG stream.** Because the C path draws no torch
uniforms, downstream torch consumers (`torch.randperm` for the minibatch
shuffle, etc.) land on a different Philox offset than a fused-torch rollout
would. The 30-chunk smoke comparison is therefore a trend test, not a
chunk-for-chunk equality test (same as `FUSED_SAMPLE` vs the 9-Categorical
path; see `blaze/rl/flywheel/RNG_PROTOCOL.md`).

**Bit-identity of logits with cuDNN.** The convolutions are hand-written
direct kernels. fp32 reduction order differs from cuDNN; absolute logit
drift is gated at 1e-3 (see `check_equiv.py` TOLERANCE_RATIONALE).

## How the claim is checked

`blaze/rl/cpolicy/check_equiv.py`:

1. logits / value max abs vs torch eager forward <= 1e-3
2. C logp vs torch recompute of the same acts
3. greedy agreement >= 99.9% with near-tie margins on disagreements
4. finite tensors
