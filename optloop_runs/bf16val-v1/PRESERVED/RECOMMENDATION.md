# BF16val recommendation

## Production fix

**Enable `RET_NORM=1` for BF16 native training.** Leave `FP32_VALUE_HEAD=0` and `VALUE_CLIP=0`. All three flags default to **0** so `check_correctness.sh` / oracle parity is unchanged.

```bash
RET_NORM=1 NATIVE_BF16=1 N_ENVS=6144 T_CHUNK=32 EPOCHS=2 MB=8192 \
  blaze/rl/native/build/ppo_native
```

### What RET_NORM does

Target-side popart-lite on the value MSE only:

- EMA (`momentum=0.99`) of return mean/variance over valid transitions each chunk
- Value targets: `(R - mu) / (sigma + 1e-8)`
- Advantages still minibatch-normalized; GAE still uses raw V
- Policy path untouched

### Three-criteria receipt (300 chunks, ~236M ticks, GPU0)

| criterion | result |
|-----------|--------|
| (1) \|V\| bounded under 50 and flat | **PASS** — end max\|V\|=**2.94**, last-50 mean **3.54** (baseline end **38.25**, last-50 mean **31.7**, still climbing) |
| (2) reward / curriculum | **PASS** — reward last **0.0294** vs baseline **0.0306**; cells 33 vs 39 (mild) |
| (3) speed within 2% | **PASS** — median wall_ms **1312.6** vs baseline **1322.4** (−0.74%) |

### Residual risk (honest)

Unclamped RET_NORM has an **early transient** (~chunks 5–20): max\|V\| peaked at **77** and grad_norm_pre at **~4.6e3**, then recovered. Final checkpoint and steady-state health are fine. A clamp-on-targets variant (`cand_c2`) removed the spike (peak 15.6) but left steady `|V|~11` and cut reward to 0.017 — rejected for production. Optional future work: full PopArt denormalize for GAE, or a short ret-norm warmup.

## Candidate ranking (growth / bound quality)

| rank | candidate | \|V\|@300 | peak\|V\| | reward@300 | med ms | criteria |
|-----:|-----------|----------:|----------:|-----------:|-------:|----------|
| 1 | **RET_NORM** | 2.94 | 77 (early) | 0.0294 | 1313 | 1✓ 2✓ 3✓ |
| 2 | VALUE_CLIP+RET_NORM | 7.94 | 24.6 | 0.0121 | 1316 | 1✓ 2✗ 3✓ |
| 3 | RET_NORM+clamp (c2) | 11.1 | 15.6 | 0.0169 | 1303 | 1✓ 2✗ 3✓ |
| 4 | VALUE_CLIP | 25.1 | 30.8 | 0.0219 | 1321 | 1✗ 2~ 3✓ |
| 5 | FP32_VALUE_HEAD | 31.6 | 31.7 | 0.0224 | 1326 | 1✗ 2~ 3✓ |
| — | baseline | 38.3 | 38.3 | 0.0306 | 1322 | 1✗ — — |

If NONE had bounded: would rank by growth rate B < A < baseline. C is the only single-flag flat bound.

## Phase 1 health (non-value)

Policy-side healthy: approx_kl stable ~0.004–0.006, policy clip_frac ~0.04–0.08, policy_loss ~0, entropy smooth 10.3→7.8, reward and curriculum improving. Secondary degradation (global grad clip always on, value_loss climb) is downstream of \|V\| scale. Details: `HEALTH.md`.

## Correctness

`bash blaze/rl/native/check_correctness.sh` — **ORACLE_RESULT pass=1** with all fix flags at default 0 (receipt: `correctness.log`). Fix flags intentionally change value-loss math; keep defaults 0 for parity. Fixture checksum matches `native_fixture.sha256`.

## Telemetry

`NATIVE_TELEMETRY=1` (default 0) requires `NATIVE_TELEMETRY_JSON=<path>`; one JSON object per chunk. Channels: policy_loss, value_loss, entropy, approx_kl, clip_frac, grad_norm_pre (pre-clip), grad_clip_frac, value_grad_norm, param_norm, v_min/max/mean, ret_min/max, reward_mean, wall_ms.

## Files touched

- `blaze/rl/native/ppo_native.cpp` — telemetry + FP32_VALUE_HEAD / VALUE_CLIP / RET_NORM flags
- `optloop_runs/bf16val-v1/PRESERVED/*` — health, verdicts, telemetry jsonl, logs

Binary checkpoints (`.nckpt`) and the oracle fixture copy are **not** committed (binaries / large artifacts).
