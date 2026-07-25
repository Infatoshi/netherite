# Tape replay: scenario_elytra_dip_20260723T001355Z

505 ticks, seed 0, world_time 6000, start (0.50,24.00,0.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=1 mismatches=1 available=True pass=False
- entities: checked=26 available=True
- world nearby_hash: checked=26 deltas=5 available=True

**Pixel gate: FAIL** over 51 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 42 | 131114 | 520 |
| bossbar | 1 | 8584 | 2951 |
| hud | 42 | 303567 | 14254 |
| particles | 51 | 204340 | 20522 |
| viewmodel | 51 | 241631 | 8135 |

Failed frames (worst first, top 20):

- t=190: 4222 unexplained px, clusters [{'px': 66, 'cls': 'UNEXPLAINED', 'bbox': [267, 144, 273, 172]}, {'px': 57, 'cls': 'UNEXPLAINED', 'bbox': [283, 35, 287, 60]}, {'px': 94, 'cls': 'UNEXPLAINED', 'bbox': [286, 106, 292, 147]}, {'px': 74, 'cls': 'UNEXPLAINED', 'bbox': [287, 82, 292, 111]}]
- t=180: 3953 unexplained px, clusters [{'px': 67, 'cls': 'UNEXPLAINED', 'bbox': [250, 81, 262, 100]}, {'px': 78, 'cls': 'UNEXPLAINED', 'bbox': [262, 355, 272, 383]}, {'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [271, 323, 278, 338]}, {'px': 91, 'cls': 'UNEXPLAINED', 'bbox': [272, 211, 279, 239]}]
- t=280: 3830 unexplained px, clusters [{'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [270, 336, 276, 350]}, {'px': 109, 'cls': 'UNEXPLAINED', 'bbox': [276, 56, 286, 82]}, {'px': 121, 'cls': 'UNEXPLAINED', 'bbox': [280, 82, 292, 113]}, {'px': 57, 'cls': 'UNEXPLAINED', 'bbox': [286, 22, 291, 48]}]
- t=170: 3809 unexplained px, clusters [{'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [273, 294, 279, 310]}, {'px': 58, 'cls': 'UNEXPLAINED', 'bbox': [277, 18, 283, 39]}, {'px': 92, 'cls': 'UNEXPLAINED', 'bbox': [280, 39, 290, 65]}, {'px': 67, 'cls': 'UNEXPLAINED', 'bbox': [283, 66, 292, 91]}]
- t=290: 3775 unexplained px, clusters [{'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [276, 56, 279, 79]}, {'px': 98, 'cls': 'UNEXPLAINED', 'bbox': [280, 82, 292, 103]}, {'px': 83, 'cls': 'UNEXPLAINED', 'bbox': [286, 136, 295, 165]}, {'px': 85, 'cls': 'UNEXPLAINED', 'bbox': [296, 273, 300, 306]}]
- t=300: 3775 unexplained px, clusters [{'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [276, 56, 279, 79]}, {'px': 98, 'cls': 'UNEXPLAINED', 'bbox': [280, 82, 292, 103]}, {'px': 83, 'cls': 'UNEXPLAINED', 'bbox': [286, 136, 295, 165]}, {'px': 85, 'cls': 'UNEXPLAINED', 'bbox': [296, 273, 300, 306]}]
- t=310: 3775 unexplained px, clusters [{'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [276, 56, 279, 79]}, {'px': 98, 'cls': 'UNEXPLAINED', 'bbox': [280, 82, 292, 103]}, {'px': 83, 'cls': 'UNEXPLAINED', 'bbox': [286, 136, 295, 165]}, {'px': 85, 'cls': 'UNEXPLAINED', 'bbox': [296, 273, 300, 306]}]
- t=320: 3775 unexplained px, clusters [{'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [276, 56, 279, 79]}, {'px': 98, 'cls': 'UNEXPLAINED', 'bbox': [280, 82, 292, 103]}, {'px': 83, 'cls': 'UNEXPLAINED', 'bbox': [286, 136, 295, 165]}, {'px': 85, 'cls': 'UNEXPLAINED', 'bbox': [296, 273, 300, 306]}]
- t=330: 3775 unexplained px, clusters [{'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [276, 56, 279, 79]}, {'px': 98, 'cls': 'UNEXPLAINED', 'bbox': [280, 82, 292, 103]}, {'px': 83, 'cls': 'UNEXPLAINED', 'bbox': [286, 136, 295, 165]}, {'px': 85, 'cls': 'UNEXPLAINED', 'bbox': [296, 273, 300, 306]}]
- t=340: 3775 unexplained px, clusters [{'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [276, 56, 279, 79]}, {'px': 98, 'cls': 'UNEXPLAINED', 'bbox': [280, 82, 292, 103]}, {'px': 83, 'cls': 'UNEXPLAINED', 'bbox': [286, 136, 295, 165]}, {'px': 85, 'cls': 'UNEXPLAINED', 'bbox': [296, 273, 300, 306]}]
- t=350: 3775 unexplained px, clusters [{'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [276, 56, 279, 79]}, {'px': 98, 'cls': 'UNEXPLAINED', 'bbox': [280, 82, 292, 103]}, {'px': 83, 'cls': 'UNEXPLAINED', 'bbox': [286, 136, 295, 165]}, {'px': 85, 'cls': 'UNEXPLAINED', 'bbox': [296, 273, 300, 306]}]
- t=360: 3775 unexplained px, clusters [{'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [276, 56, 279, 79]}, {'px': 98, 'cls': 'UNEXPLAINED', 'bbox': [280, 82, 292, 103]}, {'px': 83, 'cls': 'UNEXPLAINED', 'bbox': [286, 136, 295, 165]}, {'px': 85, 'cls': 'UNEXPLAINED', 'bbox': [296, 273, 300, 306]}]
- t=370: 3775 unexplained px, clusters [{'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [276, 56, 279, 79]}, {'px': 98, 'cls': 'UNEXPLAINED', 'bbox': [280, 82, 292, 103]}, {'px': 83, 'cls': 'UNEXPLAINED', 'bbox': [286, 136, 295, 165]}, {'px': 85, 'cls': 'UNEXPLAINED', 'bbox': [296, 273, 300, 306]}]
- t=380: 3775 unexplained px, clusters [{'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [276, 56, 279, 79]}, {'px': 98, 'cls': 'UNEXPLAINED', 'bbox': [280, 82, 292, 103]}, {'px': 83, 'cls': 'UNEXPLAINED', 'bbox': [286, 136, 295, 165]}, {'px': 85, 'cls': 'UNEXPLAINED', 'bbox': [296, 273, 300, 306]}]
- t=390: 3775 unexplained px, clusters [{'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [276, 56, 279, 79]}, {'px': 98, 'cls': 'UNEXPLAINED', 'bbox': [280, 82, 292, 103]}, {'px': 83, 'cls': 'UNEXPLAINED', 'bbox': [286, 136, 295, 165]}, {'px': 85, 'cls': 'UNEXPLAINED', 'bbox': [296, 273, 300, 306]}]
- t=400: 3775 unexplained px, clusters [{'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [276, 56, 279, 79]}, {'px': 98, 'cls': 'UNEXPLAINED', 'bbox': [280, 82, 292, 103]}, {'px': 83, 'cls': 'UNEXPLAINED', 'bbox': [286, 136, 295, 165]}, {'px': 85, 'cls': 'UNEXPLAINED', 'bbox': [296, 273, 300, 306]}]
- t=410: 3775 unexplained px, clusters [{'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [276, 56, 279, 79]}, {'px': 98, 'cls': 'UNEXPLAINED', 'bbox': [280, 82, 292, 103]}, {'px': 83, 'cls': 'UNEXPLAINED', 'bbox': [286, 136, 295, 165]}, {'px': 85, 'cls': 'UNEXPLAINED', 'bbox': [296, 273, 300, 306]}]
- t=420: 3775 unexplained px, clusters [{'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [276, 56, 279, 79]}, {'px': 98, 'cls': 'UNEXPLAINED', 'bbox': [280, 82, 292, 103]}, {'px': 83, 'cls': 'UNEXPLAINED', 'bbox': [286, 136, 295, 165]}, {'px': 85, 'cls': 'UNEXPLAINED', 'bbox': [296, 273, 300, 306]}]
- t=430: 3775 unexplained px, clusters [{'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [276, 56, 279, 79]}, {'px': 98, 'cls': 'UNEXPLAINED', 'bbox': [280, 82, 292, 103]}, {'px': 83, 'cls': 'UNEXPLAINED', 'bbox': [286, 136, 295, 165]}, {'px': 85, 'cls': 'UNEXPLAINED', 'bbox': [296, 273, 300, 306]}]
- t=440: 3775 unexplained px, clusters [{'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [276, 56, 279, 79]}, {'px': 98, 'cls': 'UNEXPLAINED', 'bbox': [280, 82, 292, 103]}, {'px': 83, 'cls': 'UNEXPLAINED', 'bbox': [286, 136, 295, 165]}, {'px': 85, 'cls': 'UNEXPLAINED', 'bbox': [296, 273, 300, 306]}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 4.56 | 94.34% | 5.24 |
| 10 | 2.35 | 92.06% | 3.14 |
| 20 | 1.97 | 88.94% | 2.66 |
| 30 | 2.02 | 88.94% | 2.81 |
| 40 | 3.00 | 65.83% | 3.85 |
| 50 | 3.51 | 66.35% | 4.83 |
| 60 | 10.65 | 94.26% | 9.81 |
| 70 | 6.16 | 91.38% | 6.60 |
| 80 | 2.93 | 88.16% | 3.26 |
| 90 | 1.48 | 86.48% | 1.45 |
| 100 | 1.20 | 84.34% | 1.12 |
| 110 | 1.18 | 83.62% | 1.19 |
| 120 | 1.06 | 76.78% | 0.95 |
| 130 | 1.20 | 78.29% | 1.22 |
| 140 | 7.10 | 80.02% | 8.33 |
| 150 | 6.97 | 79.87% | 8.28 |
| 160 | 7.11 | 80.05% | 8.52 |
| 170 | 7.20 | 79.93% | 8.58 |
| 180 | 7.09 | 79.93% | 8.32 |
| 190 | 7.04 | 80.02% | 8.30 |
| 200 | 7.14 | 79.89% | 8.46 |
| 210 | 6.98 | 79.94% | 8.27 |
| 220 | 7.13 | 79.93% | 8.50 |
| 230 | 7.15 | 79.98% | 8.41 |
| 240 | 7.16 | 79.96% | 8.54 |
| 250 | 7.05 | 79.83% | 8.31 |
| 260 | 7.15 | 79.98% | 8.34 |
| 270 | 6.99 | 79.98% | 8.24 |
| 280 | 7.22 | 80.02% | 8.38 |
| 290 | 7.22 | 80.02% | 8.37 |
| 300 | 7.22 | 80.02% | 8.37 |
| 310 | 7.22 | 80.02% | 8.37 |
| 320 | 7.22 | 80.02% | 8.37 |
| 330 | 7.22 | 80.02% | 8.37 |
| 340 | 7.22 | 80.02% | 8.37 |
| 350 | 7.22 | 80.02% | 8.37 |
| 360 | 7.22 | 80.02% | 8.37 |
| 370 | 7.22 | 80.02% | 8.37 |
| 380 | 7.22 | 80.02% | 8.37 |
| 390 | 7.22 | 80.02% | 8.37 |
| 400 | 7.22 | 80.02% | 8.37 |
| 410 | 7.22 | 80.02% | 8.37 |
| 420 | 7.22 | 80.02% | 8.37 |
| 430 | 7.22 | 80.02% | 8.37 |
| 440 | 7.22 | 80.02% | 8.37 |
| 450 | 7.22 | 80.02% | 8.37 |
| 460 | 7.22 | 80.02% | 8.37 |
| 470 | 7.22 | 80.02% | 8.37 |
| 480 | 7.22 | 80.02% | 8.37 |
| 490 | 7.22 | 80.02% | 8.37 |
| 500 | 7.22 | 80.02% | 8.37 |
