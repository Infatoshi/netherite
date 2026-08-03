# Native BF16 curriculum RNG protocol

Python `ppo_chain_cu.StageCurriculum` uses `numpy.random.default_rng(RNG_SEED)`
(PCG64).

Native-v1 uses `std::mt19937_64(RNG_SEED)` for the same algorithmic decisions
(seed choice, T0_SHARE coin, discrete frontier weights). Streams are **not**
bit-identical to PCG64.

This is an explicitly different RNG protocol for the native-v1 BF16 recipe.
Trajectory identity versus Python is not claimed. BF16 numeric oracle identity
covers policy/PPO math on a fixed fixture only.
