# Tape replay: scenario_slime_bounce_20260723T001527Z

402 ticks, seed 0, world_time 6000, start (0.50,6.96,0.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=1 mismatches=0 available=True pass=True
- entities: checked=21 available=True
- world nearby_hash: checked=21 deltas=6 available=True

**Pixel gate: FAIL** over 41 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 11 | 64230 | 57391 |
| hud | 21 | 750065 | 34653 |
| particles | 38 | 547877 | 39899 |
| viewmodel | 27 | 453406 | 33114 |

Failed frames (worst first, top 20):

- t=0: 57391 unexplained px, clusters [{'px': 57391, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}]
- t=50: 757 unexplained px, clusters [{'px': 306, 'cls': 'UNEXPLAINED', 'bbox': [245, 180, 250, 357]}, {'px': 65, 'cls': 'UNEXPLAINED', 'bbox': [245, 392, 248, 444]}, {'px': 386, 'cls': 'UNEXPLAINED', 'bbox': [246, 0, 253, 179]}]
- t=60: 744 unexplained px, clusters [{'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [244, 347, 247, 394]}, {'px': 115, 'cls': 'UNEXPLAINED', 'bbox': [244, 396, 249, 444]}, {'px': 278, 'cls': 'UNEXPLAINED', 'bbox': [245, 143, 251, 310]}, {'px': 297, 'cls': 'UNEXPLAINED', 'bbox': [246, 0, 253, 141]}]
- t=70: 744 unexplained px, clusters [{'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [244, 347, 247, 394]}, {'px': 115, 'cls': 'UNEXPLAINED', 'bbox': [244, 396, 249, 444]}, {'px': 278, 'cls': 'UNEXPLAINED', 'bbox': [245, 143, 251, 310]}, {'px': 297, 'cls': 'UNEXPLAINED', 'bbox': [246, 0, 253, 141]}]
- t=80: 744 unexplained px, clusters [{'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [244, 347, 247, 394]}, {'px': 115, 'cls': 'UNEXPLAINED', 'bbox': [244, 396, 249, 444]}, {'px': 278, 'cls': 'UNEXPLAINED', 'bbox': [245, 143, 251, 310]}, {'px': 297, 'cls': 'UNEXPLAINED', 'bbox': [246, 0, 253, 141]}]
- t=90: 744 unexplained px, clusters [{'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [244, 347, 247, 394]}, {'px': 115, 'cls': 'UNEXPLAINED', 'bbox': [244, 396, 249, 444]}, {'px': 278, 'cls': 'UNEXPLAINED', 'bbox': [245, 143, 251, 310]}, {'px': 297, 'cls': 'UNEXPLAINED', 'bbox': [246, 0, 253, 141]}]
- t=100: 744 unexplained px, clusters [{'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [244, 347, 247, 394]}, {'px': 115, 'cls': 'UNEXPLAINED', 'bbox': [244, 396, 249, 444]}, {'px': 278, 'cls': 'UNEXPLAINED', 'bbox': [245, 143, 251, 310]}, {'px': 297, 'cls': 'UNEXPLAINED', 'bbox': [246, 0, 253, 141]}]
- t=110: 744 unexplained px, clusters [{'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [244, 347, 247, 394]}, {'px': 115, 'cls': 'UNEXPLAINED', 'bbox': [244, 396, 249, 444]}, {'px': 278, 'cls': 'UNEXPLAINED', 'bbox': [245, 143, 251, 310]}, {'px': 297, 'cls': 'UNEXPLAINED', 'bbox': [246, 0, 253, 141]}]
- t=120: 744 unexplained px, clusters [{'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [244, 347, 247, 394]}, {'px': 115, 'cls': 'UNEXPLAINED', 'bbox': [244, 396, 249, 444]}, {'px': 278, 'cls': 'UNEXPLAINED', 'bbox': [245, 143, 251, 310]}, {'px': 297, 'cls': 'UNEXPLAINED', 'bbox': [246, 0, 253, 141]}]
- t=130: 744 unexplained px, clusters [{'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [244, 347, 247, 394]}, {'px': 115, 'cls': 'UNEXPLAINED', 'bbox': [244, 396, 249, 444]}, {'px': 278, 'cls': 'UNEXPLAINED', 'bbox': [245, 143, 251, 310]}, {'px': 297, 'cls': 'UNEXPLAINED', 'bbox': [246, 0, 253, 141]}]
- t=290: 130 unexplained px, clusters [{'px': 66, 'cls': 'UNEXPLAINED', 'bbox': [350, 372, 359, 386]}, {'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [354, 319, 361, 342]}]
- t=30: 0 unexplained px, clusters []
- t=40: 0 unexplained px, clusters []
- t=140: 0 unexplained px, clusters []
- t=150: 0 unexplained px, clusters []
- t=160: 0 unexplained px, clusters []
- t=170: 0 unexplained px, clusters []
- t=180: 0 unexplained px, clusters []
- t=190: 0 unexplained px, clusters []

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 8.35 | 96.47% | 7.62 |
| 10 | 1.67 | 93.18% | 1.90 |
| 20 | 1.22 | 91.47% | 1.28 |
| 30 | 6.70 | 91.29% | 6.43 |
| 40 | 5.09 | 91.19% | 4.93 |
| 50 | 9.32 | 80.23% | 10.02 |
| 60 | 9.40 | 80.33% | 10.11 |
| 70 | 9.83 | 80.51% | 10.54 |
| 80 | 9.40 | 80.34% | 10.11 |
| 90 | 9.40 | 80.34% | 10.11 |
| 100 | 9.40 | 80.34% | 10.11 |
| 110 | 9.40 | 80.34% | 10.11 |
| 120 | 9.40 | 80.34% | 10.11 |
| 130 | 9.40 | 80.34% | 10.11 |
| 140 | 8.52 | 81.99% | 9.01 |
| 150 | 8.17 | 81.98% | 8.47 |
| 160 | 7.90 | 81.99% | 8.01 |
| 170 | 7.52 | 81.98% | 7.45 |
| 180 | 7.01 | 81.99% | 6.71 |
| 190 | 6.24 | 81.97% | 5.63 |
| 200 | 5.36 | 81.96% | 4.41 |
| 210 | 3.88 | 81.93% | 2.38 |
| 220 | 2.21 | 81.92% | 0.97 |
| 230 | 1.02 | 80.82% | 0.99 |
| 240 | 1.09 | 81.13% | 1.11 |
| 250 | 1.06 | 80.68% | 1.08 |
| 260 | 1.08 | 81.34% | 1.11 |
| 270 | 1.17 | 81.32% | 1.23 |
| 280 | 1.06 | 81.13% | 1.03 |
| 290 | 4.95 | 80.04% | 5.47 |
| 300 | 1.10 | 79.39% | 1.13 |
| 310 | 0.96 | 79.38% | 0.92 |
| 320 | 0.96 | 79.38% | 0.92 |
| 330 | 0.96 | 79.38% | 0.92 |
| 340 | 0.96 | 79.38% | 0.92 |
| 350 | 0.96 | 79.38% | 0.92 |
| 360 | 0.96 | 79.38% | 0.92 |
| 370 | 0.96 | 79.38% | 0.92 |
| 380 | 0.96 | 79.38% | 0.92 |
| 390 | 0.96 | 79.38% | 0.92 |
| 400 | 0.96 | 79.38% | 0.92 |
