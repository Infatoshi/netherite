# Phase 1 — BF16 native training health (300 chunks)

Config: `N_ENVS=6144 T_CHUNK=32 EPOCHS=2 MB=8192 NATIVE_BF16=1 NATIVE_BF16_UPDATE=1 NATIVE_CHANNELS_LAST=0 RNG_SEED=0`, all fix flags off. GPU0 lock, sm_120. Telemetry: `baseline_telemetry.jsonl` (300 lines). Steady wall median **1322.35 ms/chunk** (n=299 after first).

Ticks: 235,929,600 (~236M). All chunks `finite=1`.

## Rollout V magnitude (primary debt)

| chunk | v_min | v_max | max\|V\| | ret_max | reward_mean | cells |
|------:|------:|------:|--------:|--------:|------------:|------:|
| 1 | -0.072 | 0.006 | 0.072 | 2.43 | -0.00886 | 11 |
| 10 | -0.980 | 0.045 | 0.980 | ~3 | -7.2e-5 | 11 |
| 50 | -3.14 | 4.38 | 4.38 | ~ | 0.00415 | 32 |
| 100 | -2.13 | 11.31 | 11.31 | ~ | 0.00947 | 32 |
| 200 | -2.63 | 26.25 | 26.25 | ~ | 0.0215 | 34 |
| 300 | -3.50 | 38.25 | **38.25** | 39.58 | 0.0306 | 39 |

**Verdict: DEGRADING (value head).** max\|V\| grows ~monotone with returns; at 300 chunks still under sim_sanity threshold 50 but slope is positive and not flattening. Matches the known BF16 value MSE + GAE bootstrap self-reinforcement mechanism (archive bound reaches O(1e7) by 1.92B ticks). Note: archived quality-proxy sim_sanity \|V\|~3.7e3 at 100 chunks is a different measurement surface (checkpoint eval obs); rollout buffer here is milder but the same disease.

## Channel verdicts

| channel | first → last (mean f10 → l10) | verdict | notes |
|---------|-------------------------------|---------|-------|
| **v_min / v_max / v_mean** | \|V\|max 0.07→38.25; v_mean -0.03→6.18 | **DEGRADING** | Core debt. ret_max tracks v_max. |
| **ret_min / ret_max** | ret_max 2.43→39.58 | **DEGRADING** | Bootstrap coupled to V. |
| **value_loss** | 0.089→0.429 (f10 0.111 → l10 0.353) | **DEGRADING** | Unclipped 0.5(R-V)^2 grows with scale. |
| **value_grad_norm** | 0.029→2.36 (f10 0.036 → l10 2.03) | **DEGRADING** | Value head grads climb with \|V\|. |
| **grad_norm_pre** (pre-clip global) | 0.12→10.4 (max over run 18.3; grad_norm_max peaks 50.2) | **DEGRADING** | Driven largely by value scale; clip hit rate → 1.0 by end. |
| **grad_clip_frac** | 0.02→1.0 (l10 all 1.0) | **DEGRADING** | Clip always engaged late; masks true grad scale. |
| **param_norm** | 11.7→70.0 | **DEGRADING / expected growth** | Monotone growth; not explosive vs \|V\|. |
| **update_norm** | 2.71→0.84 | **STABLE/healthy** | Steps shrink as LR decays + clip saturates. |
| **policy_loss** | -0.0075→-0.0004 | **STABLE** | Near zero; no policy collapse. |
| **approx_kl** | 0.0058→0.0049 (range 0.0032–0.0090) | **STABLE** | Well under typical 0.02–0.05 alert. |
| **clip_frac** (policy) | 0.064→0.048 (range 0.017–0.126) | **STABLE** | Healthy PPO clip usage. |
| **entropy** | 10.31→7.76 | **EXPECTED DECAY** | Smooth; multi-head still high enough (not collapse). |
| **reward_mean** | -0.00886→0.0306 | **IMPROVING** | Curriculum reward signal healthy. |
| **available_cells** | 11→39 | **IMPROVING** | Stage unlocks progressing. |
| **wall_ms** | median 1322.35; l10 mean 1320 | **STABLE** | First chunk 1624 (warmup); then flat. |

## Answer: is anything else sick besides value magnitude?

**No critical non-value disease at 300 chunks.**

- Policy-side PPO signals (KL, policy clip_frac, policy_loss, entropy decay shape) are healthy.
- Reward and curriculum advance.
- All updates finite; no NaN/stall.
- Secondary effects of the value disease: global grad norm inflation and perpetual grad clipping, value_loss growth, param_norm growth partly from value weights. These are downstream of \|V\| scale, not independent policy failure.

## Speed pin (for Phase 2)

- Steady median wall_ms/chunk: **1322.35**
- 2% band: **[1295.9, 1348.8]** ms/chunk

## Receipts

- `baseline_telemetry.jsonl`, `baseline_curve.csv`, `baseline_300.nckpt`
- `logs/baseline_300.log`
