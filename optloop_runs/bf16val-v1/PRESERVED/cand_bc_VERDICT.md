# Combo B+C — VALUE_CLIP=1 + RET_NORM=1

## Purpose

Damp the early RET_NORM adaptation spike with PPO2 value clipping.

## 300-chunk receipt

| metric | baseline | B+C | C alone |
|--------|---------:|----:|--------:|
| max\|V\| at 300 | 38.25 | 7.94 | 2.94 |
| peak \|V\| | 38.25 | **24.63** (<50) | 77 |
| grad peak | 18.3 | 162 | 4643 |
| reward last | 0.0306 | **0.0121** | 0.0294 |
| cells | 39 | 33 | 33 |
| med wall_ms | 1322 | 1316 | 1313 |

## Criteria

1. |V| under 50 and flat: **PASS** (cleaner peak than C)
2. Reward not degraded: **FAIL** (0.012 vs 0.031)
3. Speed: **PASS**

## Verdict

Spike control works; reward cost is too high vs C alone. Not recommended.
