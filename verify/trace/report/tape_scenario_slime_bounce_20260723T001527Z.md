# Tape replay: scenario_slime_bounce_20260723T001527Z

402 ticks, seed 0, world_time 6000, start (0.50,6.96,0.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=1 independent=0 seeded_only=True mismatches=0 available=True pass=True
- entities: checked=21 available=True
- world nearby_hash: checked=21 deltas=6 available=True

**Pixel gate: FAIL** over 41 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 9 | 6709 | 386 |
| hud | 21 | 461531 | 40732 |
| particles | 38 | 346474 | 25111 |
| viewmodel | 27 | 297821 | 21207 |

Failed frames (worst first, top 20):

- t=50: 757 unexplained px, clusters [{'px': 306, 'cls': 'UNEXPLAINED', 'bbox': [245, 180, 250, 357]}, {'px': 65, 'cls': 'UNEXPLAINED', 'bbox': [245, 392, 248, 444]}, {'px': 386, 'cls': 'UNEXPLAINED', 'bbox': [246, 0, 253, 179]}]
- t=60: 744 unexplained px, clusters [{'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [244, 347, 247, 394]}, {'px': 115, 'cls': 'UNEXPLAINED', 'bbox': [244, 396, 249, 444]}, {'px': 278, 'cls': 'UNEXPLAINED', 'bbox': [245, 143, 251, 310]}, {'px': 297, 'cls': 'UNEXPLAINED', 'bbox': [246, 0, 253, 141]}]
- t=70: 744 unexplained px, clusters [{'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [244, 347, 247, 394]}, {'px': 115, 'cls': 'UNEXPLAINED', 'bbox': [244, 396, 249, 444]}, {'px': 278, 'cls': 'UNEXPLAINED', 'bbox': [245, 143, 251, 310]}, {'px': 297, 'cls': 'UNEXPLAINED', 'bbox': [246, 0, 253, 141]}]
- t=80: 744 unexplained px, clusters [{'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [244, 347, 247, 394]}, {'px': 115, 'cls': 'UNEXPLAINED', 'bbox': [244, 396, 249, 444]}, {'px': 278, 'cls': 'UNEXPLAINED', 'bbox': [245, 143, 251, 310]}, {'px': 297, 'cls': 'UNEXPLAINED', 'bbox': [246, 0, 253, 141]}]
- t=90: 744 unexplained px, clusters [{'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [244, 347, 247, 394]}, {'px': 115, 'cls': 'UNEXPLAINED', 'bbox': [244, 396, 249, 444]}, {'px': 278, 'cls': 'UNEXPLAINED', 'bbox': [245, 143, 251, 310]}, {'px': 297, 'cls': 'UNEXPLAINED', 'bbox': [246, 0, 253, 141]}]
- t=100: 744 unexplained px, clusters [{'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [244, 347, 247, 394]}, {'px': 115, 'cls': 'UNEXPLAINED', 'bbox': [244, 396, 249, 444]}, {'px': 278, 'cls': 'UNEXPLAINED', 'bbox': [245, 143, 251, 310]}, {'px': 297, 'cls': 'UNEXPLAINED', 'bbox': [246, 0, 253, 141]}]
- t=110: 744 unexplained px, clusters [{'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [244, 347, 247, 394]}, {'px': 115, 'cls': 'UNEXPLAINED', 'bbox': [244, 396, 249, 444]}, {'px': 278, 'cls': 'UNEXPLAINED', 'bbox': [245, 143, 251, 310]}, {'px': 297, 'cls': 'UNEXPLAINED', 'bbox': [246, 0, 253, 141]}]
- t=120: 744 unexplained px, clusters [{'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [244, 347, 247, 394]}, {'px': 115, 'cls': 'UNEXPLAINED', 'bbox': [244, 396, 249, 444]}, {'px': 278, 'cls': 'UNEXPLAINED', 'bbox': [245, 143, 251, 310]}, {'px': 297, 'cls': 'UNEXPLAINED', 'bbox': [246, 0, 253, 141]}]
- t=130: 744 unexplained px, clusters [{'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [244, 347, 247, 394]}, {'px': 115, 'cls': 'UNEXPLAINED', 'bbox': [244, 396, 249, 444]}, {'px': 278, 'cls': 'UNEXPLAINED', 'bbox': [245, 143, 251, 310]}, {'px': 297, 'cls': 'UNEXPLAINED', 'bbox': [246, 0, 253, 141]}]
- t=0: 0 unexplained px, clusters []
- t=40: 0 unexplained px, clusters []
- t=140: 0 unexplained px, clusters []
- t=150: 0 unexplained px, clusters []
- t=160: 0 unexplained px, clusters []
- t=170: 0 unexplained px, clusters []

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 7.18 | 96.46% | 6.57 |
| 10 | 1.45 | 93.18% | 1.65 |
| 20 | 0.97 | 89.11% | 1.06 |
| 30 | 4.40 | 86.48% | 4.43 |
| 40 | 4.66 | 86.75% | 4.76 |
| 50 | 7.21 | 77.92% | 8.00 |
| 60 | 7.15 | 78.07% | 7.86 |
| 70 | 7.58 | 78.26% | 8.29 |
| 80 | 7.15 | 78.10% | 7.86 |
| 90 | 7.15 | 78.10% | 7.86 |
| 100 | 7.15 | 78.10% | 7.86 |
| 110 | 7.15 | 78.11% | 7.86 |
| 120 | 7.15 | 78.11% | 7.86 |
| 130 | 7.15 | 78.11% | 7.86 |
| 140 | 5.60 | 79.07% | 6.04 |
| 150 | 5.33 | 79.07% | 5.61 |
| 160 | 5.07 | 79.08% | 5.35 |
| 170 | 4.84 | 79.07% | 5.00 |
| 180 | 4.66 | 79.08% | 4.71 |
| 190 | 4.30 | 79.07% | 4.06 |
| 200 | 3.82 | 79.07% | 3.26 |
| 210 | 2.71 | 79.05% | 1.81 |
| 220 | 1.52 | 79.03% | 0.77 |
| 230 | 0.76 | 77.94% | 0.79 |
| 240 | 0.82 | 78.25% | 0.91 |
| 250 | 0.79 | 77.80% | 0.89 |
| 260 | 0.81 | 78.45% | 0.91 |
| 270 | 0.90 | 78.44% | 1.04 |
| 280 | 0.79 | 78.25% | 0.84 |
| 290 | 0.79 | 77.49% | 0.86 |
| 300 | 0.85 | 76.70% | 0.93 |
| 310 | 0.73 | 76.70% | 0.76 |
| 320 | 0.73 | 76.70% | 0.76 |
| 330 | 0.73 | 76.70% | 0.76 |
| 340 | 0.73 | 76.70% | 0.76 |
| 350 | 0.73 | 76.70% | 0.76 |
| 360 | 0.73 | 76.70% | 0.76 |
| 370 | 0.73 | 76.70% | 0.76 |
| 380 | 0.73 | 76.70% | 0.76 |
| 390 | 0.73 | 76.70% | 0.76 |
| 400 | 0.73 | 76.70% | 0.76 |
