# Tape replay: scenario_ghast_encounter_20260730T092447Z

620 ticks, seed 0, world_time 6000, start (0.50,101.00,-0.50).

**FIRST DIVERGENCE: tick 23, field `vx`** oracle=0.0 magma=-0.01753191914490137 |d|=0.0175; inputs {'f': 0.0, 's': 0.0, 'jump': 0, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 0.0001 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=31 independent=30 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=31 available=True
- world nearby_hash: checked=31 deltas=10 available=True

**Pixel gate: FAIL** over 31 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 3 | 102494 | 58631 |
| bossbar | 3 | 15530 | 8736 |
| hud | 3 | 39994 | 27620 |
| particles | 15 | 116026 | 24949 |
| viewmodel | 6 | 24265 | 10441 |

Skipped renderable entity rows (more than 4 fails the gate):

- `EntityLargeFireball`: 256 rows (FAIL)

Failed frames (worst first, top 20):

- t=560: 58631 unexplained px, clusters [{'px': 58631, 'cls': 'UNEXPLAINED', 'bbox': [190, 0, 383, 378], 'soak_from': 'particles'}]
- t=500: 43355 unexplained px, clusters [{'px': 43355, 'cls': 'UNEXPLAINED', 'bbox': [45, 322, 282, 568], 'soak_from': 'particles'}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 0.96 | 69.22% | 1.18 |
| 20 | 7.43 | 69.93% | 12.44 |
| 40 | 0.34 | 69.66% | 0.43 |
| 60 | 0.35 | 69.67% | 0.45 |
| 80 | 1.55 | 69.67% | 2.48 |
| 100 | 0.39 | 69.33% | 0.51 |
| 120 | 0.49 | 68.28% | 0.69 |
| 140 | 0.51 | 68.83% | 0.66 |
| 160 | 0.37 | 68.33% | 0.49 |
| 180 | 0.51 | 66.31% | 0.71 |
| 200 | 6.75 | 66.32% | 11.22 |
| 220 | 0.35 | 66.06% | 0.44 |
| 240 | 0.51 | 66.06% | 0.72 |
| 260 | 7.01 | 70.39% | 11.17 |
| 280 | 0.47 | 66.84% | 0.65 |
| 300 | 0.69 | 66.80% | 1.02 |
| 320 | 8.64 | 68.18% | 7.74 |
| 340 | 1.12 | 65.74% | 1.77 |
| 360 | 1.84 | 65.31% | 2.98 |
| 380 | 4.64 | 69.17% | 6.65 |
| 400 | 0.39 | 68.84% | 0.52 |
| 420 | 0.46 | 68.76% | 0.62 |
| 440 | 2.27 | 65.87% | 3.69 |
| 460 | 0.35 | 65.84% | 0.45 |
| 480 | 0.59 | 65.84% | 0.85 |
| 500 | 19.33 | 66.14% | 27.06 |
| 520 | 0.50 | 65.82% | 0.70 |
| 540 | 1.10 | 65.82% | 1.71 |
| 560 | 28.30 | 73.40% | 28.84 |
| 580 | 0.62 | 65.56% | 0.90 |
| 600 | 1.00 | 65.53% | 1.55 |
