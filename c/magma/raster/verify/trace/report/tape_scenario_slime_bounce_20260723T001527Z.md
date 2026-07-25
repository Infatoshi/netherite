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
| 0 | 8.40 | 96.47% | 7.51 |
| 10 | 1.45 | 93.18% | 1.65 |
| 20 | 0.97 | 89.11% | 1.06 |
| 30 | 6.46 | 86.49% | 6.24 |
| 40 | 4.85 | 86.76% | 4.76 |
| 50 | 9.09 | 77.93% | 9.87 |
| 60 | 9.17 | 78.11% | 9.96 |
| 70 | 9.60 | 78.30% | 10.39 |
| 80 | 9.17 | 78.14% | 9.96 |
| 90 | 9.17 | 78.15% | 9.96 |
| 100 | 9.17 | 78.15% | 9.96 |
| 110 | 9.17 | 78.15% | 9.96 |
| 120 | 9.17 | 78.15% | 9.96 |
| 130 | 9.17 | 78.15% | 9.96 |
| 140 | 8.25 | 79.11% | 8.81 |
| 150 | 7.90 | 79.10% | 8.28 |
| 160 | 7.63 | 79.11% | 7.82 |
| 170 | 7.26 | 79.09% | 7.25 |
| 180 | 6.74 | 79.10% | 6.51 |
| 190 | 5.98 | 79.08% | 5.43 |
| 200 | 5.10 | 79.08% | 4.22 |
| 210 | 3.61 | 79.05% | 2.18 |
| 220 | 1.94 | 79.03% | 0.77 |
| 230 | 0.76 | 77.94% | 0.79 |
| 240 | 0.82 | 78.25% | 0.91 |
| 250 | 0.79 | 77.80% | 0.89 |
| 260 | 0.81 | 78.45% | 0.91 |
| 270 | 0.90 | 78.44% | 1.04 |
| 280 | 0.79 | 78.25% | 0.84 |
| 290 | 4.71 | 78.25% | 5.25 |
| 300 | 0.87 | 76.70% | 0.96 |
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
