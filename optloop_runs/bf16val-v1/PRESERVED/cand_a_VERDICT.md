# Candidate A — FP32_VALUE_HEAD=1

## Implementation

Value head (256→1 linear) runs with autocast forced off and hidden cast to float32. Backbone/policy heads stay BF16 autocast. Default `FP32_VALUE_HEAD=0` preserves oracle parity.

## 300-chunk receipt (vs Phase 1 baseline)

| metric | baseline | FP32_VALUE_HEAD | delta |
|--------|---------:|----------------:|------:|
| max\|V\| at chunk 300 | 38.25 | 31.64 | -17% |
| max\|V\| over run | 38.25 | 31.67 | -17% |
| \|V\| at chunk 100 | 11.31 | 10.61 | slight |
| reward last | 0.0306 | 0.0224 | -0.0082 |
| reward last10 mean | 0.0305 | 0.0219 | worse |
| cells last | 39 | 37 | -2 |
| steady wall_ms median | 1322.35 | 1325.89 | +0.27% |

Trajectory max\|V\|: 0.07 → 1.0 (c10) → 5.3 (c50) → 10.6 (c100) → 21.3 (c200) → 31.6 (c300). Still rising; not flat.

## Criteria

1. **|V| bounded + flat:** FAIL (under 50, but not flat; only slower growth)
2. **Reward/curriculum not degraded:** MARGINAL FAIL (reward and cells slightly below baseline)
3. **Speed within 2%:** PASS (+0.27%)

## Rank

Growth rate better than baseline, worse than VALUE_CLIP. Does not kill the bootstrap self-reinforcement loop alone — BF16 on the tiny value linear is not the dominant amplifier at this horizon.
