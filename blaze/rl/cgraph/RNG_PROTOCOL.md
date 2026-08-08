# cgraph-v1 RNG protocol

Python `ppo_chain_cu.StageCurriculum` uses `numpy.random.default_rng(RNG_SEED)`
(PCG64).

Cgraph-v1 uses `std::mt19937_64(RNG_SEED)` for the same curriculum decisions
(seed choice, T0_SHARE coin, and discrete frontier weights). The policy uses
the C++ Torch CUDA generator from `torch::manual_seed` and
`torch::cuda::manual_seed_all`. Its captured sampler draws one padded
`[N, 9, 10]` uniform tensor and applies the fused Gumbel argmax. CUDA graph
replay advances the captured generator state.

Neither stream is bit-identical to the Python trainer's combined NumPy and
Torch consumption order, so trajectory identity is not claimed. The
equivalence gate fixes actions and rollout tensors to judge forward, loss, and
gradient math independently of sampling. The 30-chunk smoke judges learning
under the independent candidate stream.
