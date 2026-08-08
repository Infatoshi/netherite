# RNG protocol: fused multi-head sampling

`FUSED_SAMPLE=1` replaces the nine `torch.distributions.Categorical` objects
in the rollout with one Gumbel-argmax over a padded `[N, NHEAD, WMAX]` logit
block. This document states exactly what is and is not preserved, because
"mathematically identical sampling" is a claim that has to be bounded.

## What is exactly preserved

**The sampling distribution.** For a head with logits `l_i`,

    argmax_i (l_i + G_i),  G_i iid Gumbel(0,1)

selects category `i` with probability `exp(l_i) / sum_j exp(l_j)` — the
softmax. This is the Gumbel-max theorem and it is exact, not an
approximation. Padding columns are set to `-inf` before the Gumbel is added,
so a pad column can never win an argmax: `-inf + g = -inf` for any finite
`g`.

**The joint log-probability.** `logp = sum_h log_softmax(l_h)[a_h]`, computed
as one `log_softmax` over the padded block followed by a gather and a sum
over heads. Padding does not perturb it: `log_softmax` over a row whose pad
entries are `-inf` gives `-inf` in the pad columns and exactly the unpadded
values elsewhere, because `exp(-inf) = 0` contributes nothing to the
normaliser. Measured agreement against the nine-`Categorical` sum is
1.9e-06 absolute (`check_correctness.py::fused_logp`), which is reduction
order over at most ten elements.

**The summed entropy.** Same argument; the pad terms enter as `p*log p` with
`p = 0`, forced to exactly zero via `nan_to_num(..., neginf=0.0)` so that
`0 * -inf` does not become NaN. Measured agreement 1.9e-06 absolute.

**The burn-in override ordering.** The eager path samples, overwrites the
burn-in lanes with the no-op row, and then takes the log-prob of the
*overwritten* row. The fused path does the same, in that order. The only
change is `torch.where` instead of a boolean-mask assignment, which is
required because a masked assignment has a data-dependent output shape and
cannot be captured into a CUDA graph. The two produce identical results.

## What is NOT preserved

**Draw-for-draw equality with the old sampler.** `Categorical.sample` calls
`torch.multinomial`, which consumes a different number of Philox values, in a
different order, than `torch.rand` over an `[N, NHEAD, WMAX]` block. Seeding
both paths with `RNG_SEED=0` therefore does NOT give the same action
sequence. There is no way to make it do so short of reimplementing
`multinomial`'s internal offset arithmetic, and that would buy nothing: the
two streams are equidistributed against the same target.

The consequence for reproducibility is concrete and worth stating plainly: a
run launched with `FUSED_SAMPLE=1` is not replayable from a checkpoint of a
`FUSED_SAMPLE=0` run and vice versa, even at the same `RNG_SEED`. The flag
belongs in the run's provenance alongside the seed.

**Consumption count.** The fused sampler draws `N*NHEAD*WMAX` uniforms per
decision (10 per head-slot including padding) where the old path drew
roughly `N*NHEAD`. Downstream RNG-dependent state (`torch.randperm` for the
minibatch permutation) therefore lands on a different stream position. This
is why the 30-chunk smoke comparison is a *trend* test and not a
chunk-for-chunk equality test; see `compare_smoke.py`.

## How the claim is checked

`check_correctness.py::fused_dist` draws 200,000 samples from a fixed logit
vector and requires every category's empirical frequency to sit inside 5
binomial sigma of its softmax probability, for every head. Measured worst
case: 2.71 sigma over 200,000 draws.

That test is what caught the one real bug in this lane. The first version of
the Gumbel expression was

    -torch.log(-torch.log(u.clamp_min(1e-20)).clamp_min(1e-20))

in which the unary minus binds after `.clamp_min`, so the inner term is
`-(log(u).clamp_min(1e-20))`, which is negative, and the outer `log` of a
negative number is NaN. `argmax` over an all-NaN row returns index 0, so
every head deterministically emitted category 0 — and nothing else in the
pipeline noticed. It ran, it was finite downstream, and it benchmarked
*faster*. Only the distribution test failed it.
