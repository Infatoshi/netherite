# Candidate B — VALUE_CLIP=1

## Implementation

PPO2-style clipped value loss against rollout `V_old` (same clip range as policy: 0.2):

```
v_clipped = V_old + clamp(V - V_old, -0.2, +0.2)
value_loss = 0.5 * max((R - V)^2, (R - v_clipped)^2).mean()
```

Default `VALUE_CLIP=0` preserves oracle parity (exact unclipped MSE).

## 300-chunk receipt (vs Phase 1 baseline)

| metric | baseline | VALUE_CLIP | delta |
|--------|---------:|-----------:|------:|
| max\|V\| at chunk 300 | 38.25 | 25.13 | -34% |
| max\|V\| over run | 38.25 | 30.75 | -20% |
| \|V\| at chunk 100 | 11.31 | 13.00 | +15% mid |
| reward last | 0.0306 | 0.0219 | -0.0087 |
| reward last10 mean | 0.0305 | 0.0214 | worse |
| cells last | 39 | 33 | -6 |
| steady wall_ms median | 1322.35 | 1320.63 | -0.13% |

Trajectory max\|V\|: 0.07 → 1.0 (c10) → 5.0 (c50) → 13 (c100) → 23.4 (c200) → 25.1 (c300). Growth slows after c200 vs baseline but still positive.

## Criteria

1. **|V| bounded + flat:** FAIL (under 50; slowest growth of A/B but not flat)
2. **Reward/curriculum not degraded:** MARGINAL FAIL (reward and cells below baseline)
3. **Speed within 2%:** PASS (-0.13%)

## Rank

Best growth control of single-flag A/B. Still does not stop bootstrap scale-up of returns once V_old itself ratchets upward (clip only limits per-update step, not absolute level).
