# Tape replay: scenario_soulsand_ice_20260723T001810Z

503 ticks, seed 0, world_time 6000, start (-10.50,4.00,0.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=1 mismatches=0 available=True pass=True
- entities: checked=26 available=True
- world nearby_hash: checked=26 deltas=5 available=True

**Pixel gate: FAIL** over 51 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 1 | 1226 | 370 |
| hud | 1 | 13451 | 13451 |
| particles | 1 | 99 | 99 |
| viewmodel | 3 | 3924 | 3507 |

Failed frames (worst first, top 20):

- t=50: 1226 unexplained px, clusters [{'px': 370, 'cls': 'UNEXPLAINED', 'bbox': [259, 398, 280, 444]}, {'px': 101, 'cls': 'UNEXPLAINED', 'bbox': [287, 391, 299, 411]}, {'px': 191, 'cls': 'UNEXPLAINED', 'bbox': [287, 411, 299, 444]}, {'px': 78, 'cls': 'UNEXPLAINED', 'bbox': [301, 374, 305, 409]}]
- t=60: 0 unexplained px, clusters []
- t=80: 0 unexplained px, clusters []
- t=90: 0 unexplained px, clusters []
- t=100: 0 unexplained px, clusters []
- t=110: 0 unexplained px, clusters []
- t=120: 0 unexplained px, clusters []
- t=130: 0 unexplained px, clusters []
- t=140: 0 unexplained px, clusters []
- t=150: 0 unexplained px, clusters []
- t=160: 0 unexplained px, clusters []
- t=170: 0 unexplained px, clusters []
- t=180: 0 unexplained px, clusters []
- t=190: 0 unexplained px, clusters []
- t=200: 0 unexplained px, clusters []
- t=210: 0 unexplained px, clusters []
- t=220: 0 unexplained px, clusters []
- t=230: 0 unexplained px, clusters []
- t=240: 0 unexplained px, clusters []
- t=250: 0 unexplained px, clusters []

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 3.14 | 94.84% | 3.06 |
| 10 | 1.28 | 91.02% | 1.41 |
| 20 | 1.17 | 89.18% | 1.26 |
| 30 | 1.14 | 89.30% | 1.20 |
| 40 | 1.11 | 78.19% | 1.16 |
| 50 | 3.77 | 79.67% | 4.63 |
| 60 | 3.95 | 91.73% | 4.56 |
| 70 | 2.34 | 91.77% | 2.49 |
| 80 | 2.55 | 91.26% | 2.42 |
| 90 | 2.60 | 81.48% | 2.35 |
| 100 | 2.71 | 91.20% | 2.46 |
| 110 | 2.74 | 91.14% | 2.49 |
| 120 | 2.75 | 81.54% | 2.52 |
| 130 | 2.73 | 81.51% | 2.47 |
| 140 | 2.75 | 81.28% | 2.48 |
| 150 | 2.75 | 81.56% | 2.47 |
| 160 | 2.75 | 81.50% | 2.46 |
| 170 | 2.81 | 81.56% | 2.55 |
| 180 | 2.77 | 81.48% | 2.50 |
| 190 | 2.72 | 81.35% | 2.43 |
| 200 | 2.71 | 81.56% | 2.43 |
| 210 | 2.76 | 81.47% | 2.49 |
| 220 | 2.71 | 81.30% | 2.43 |
| 230 | 2.71 | 81.52% | 2.43 |
| 240 | 2.76 | 81.43% | 2.51 |
| 250 | 2.71 | 81.56% | 2.45 |
| 260 | 2.76 | 81.49% | 2.51 |
| 270 | 2.71 | 81.33% | 2.44 |
| 280 | 2.71 | 81.58% | 2.44 |
| 290 | 2.71 | 81.49% | 2.44 |
| 300 | 2.71 | 81.33% | 2.44 |
| 310 | 2.71 | 81.54% | 2.45 |
| 320 | 2.80 | 81.42% | 2.56 |
| 330 | 2.81 | 81.54% | 2.55 |
| 340 | 2.74 | 81.49% | 2.44 |
| 350 | 2.73 | 81.29% | 2.45 |
| 360 | 2.72 | 81.55% | 2.46 |
| 370 | 2.73 | 81.53% | 2.46 |
| 380 | 2.74 | 81.45% | 2.45 |
| 390 | 2.74 | 81.51% | 2.44 |
| 400 | 2.79 | 81.32% | 2.54 |
| 410 | 2.71 | 80.08% | 2.38 |
| 420 | 2.59 | 80.09% | 2.26 |
| 430 | 2.59 | 80.09% | 2.26 |
| 440 | 2.59 | 80.09% | 2.26 |
| 450 | 2.59 | 80.09% | 2.26 |
| 460 | 2.59 | 80.09% | 2.26 |
| 470 | 2.59 | 80.09% | 2.26 |
| 480 | 2.59 | 80.09% | 2.26 |
| 490 | 2.59 | 80.09% | 2.26 |
| 500 | 2.59 | 80.09% | 2.26 |
