# Sim value explosion — bound (native BF16)

## Observation

`sim_sanity.py` checks `abs(value_{min,max}) < 50`. Under the native BF16 recipe
this fails after short training even while rollout/PPO stay finite and the
curriculum advances.

| Checkpoint | chunks (~ticks) | value_min | value_max | finite train | curriculum signal |
|---|---:|---:|---:|---|---|
| Python FP32 smoke (v3) | 10 | -0.69 | 1.82 | yes | cells 17 |
| Native FP32 smoke (v3) | 10 | -0.64 | 1.67 | yes | cells 17 |
| Native BF16 smoke (v4) | 10 / 7.86e6 | -437.5 | -2.45 | yes | cells 11 |
| Native BF16 quality proxy (v4) | 100 / 7.86e7 | -3685.9 | 638.8 | yes | stages 1–2, cells 32 |
| Native BF16 full 1.92B (v4) | 2442 / 1.920e9 | -2.321e7 | -1.599e5 | yes (all chunks finite) | cells 53; train reward -0.00886→0.06817 |

## Mechanism (bounded, not fixed in this loop)

1. Value head is an unclipped MSE on GAE returns (`0.5 * (R - V)^2`). No
   `value_clip` / return clamp exists in either Python or native recipes.
2. Advantages are normalized per minibatch; policy gradients therefore stay
   well-scaled even when |V| is large. Training telemetry stays finite.
3. BF16 compute amplifies value-head drift relative to FP32: native FP32 and
   Python FP32 stay inside ~[-2, 2] at the same 10-chunk horizon; BF16 does not.
4. Once |V| grows, GAE bootstrap targets grow with it, so the MSE loop
   self-reinforces magnitude. Policy entropy / multi-head diversity can still
   remain usable (observed on proxy).

## Bound for this campaign

- **Quality gate that still holds:** all train chunks `finite=1`, non-zero
  update norms, reward improvement, curriculum stage progression, stable GPU
  allocation (~8.65 GiB allocated / 10.29 peak).
- **Quality gate that fails under BF16:** `values_within_sane_range` in
  `sim_sanity.py` (threshold |V|<50). Treat as **known BF16 value-head debt**,
  not a train crash.
- **Do not claim transfer quality from value magnitude.** Use policy action
  diversity + stage histogram + (when available) Java pilot success rates.
- **Fix candidates (out of scope for this optloop keep/discard):** value
  clipping (PPO2-style), return normalization, FP32 value head with BF16
  backbone, or lower value loss coef. Any of these needs a fresh correctness
  oracle and re-bench; not applied to the 1.92B run.

## Post-train measurement

Fill the full-train row after convert + `sim_sanity.py` on
`final/native_1p92b.pt`. Report the measured [value_min, value_max] as the
campaign bound; do not retrofit a clip after the fact.

## Post-train measured bound

- Checkpoint: `final/native_1p92b.pt` sha256 `ecd7aa73709fa9485364fda768559a7cbc45e43352ed703f5c2ead8a373266f0`
- sim_sanity `value_min=-23213760`, `value_max=-159881.5` (all values large negative)
- `values_within_sane_range=false`, `finite_and_non_degenerate=false` (value gate only; multi-head diversity still true)
- Campaign bound: |V| reaches O(1e7) by 1.92B ticks under unclipped BF16 value MSE. Policy heads remain multi-category-diverse. Do not use value magnitude as a transfer quality signal.
