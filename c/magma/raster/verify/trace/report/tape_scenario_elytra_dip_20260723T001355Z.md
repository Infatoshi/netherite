# Tape replay: scenario_elytra_dip_20260723T001355Z

505 ticks, seed 0, world_time 6000, start (0.50,24.00,0.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=5 independent=4 seeded_only=False mismatches=0 available=True pass=True
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
| 0 | 4.39 | 94.34% | 4.97 |
| 10 | 2.16 | 92.06% | 2.92 |
| 20 | 1.81 | 86.42% | 2.52 |
| 30 | 1.88 | 85.87% | 2.70 |
| 40 | 2.87 | 64.94% | 3.77 |
| 50 | 3.39 | 65.50% | 4.75 |
| 60 | 10.62 | 94.26% | 9.81 |
| 70 | 6.09 | 91.38% | 6.59 |
| 80 | 2.71 | 88.16% | 3.23 |
| 90 | 1.25 | 86.33% | 1.41 |
| 100 | 1.00 | 85.43% | 1.09 |
| 110 | 0.97 | 80.82% | 1.15 |
| 120 | 0.86 | 75.66% | 0.92 |
| 130 | 1.01 | 77.15% | 1.19 |
| 140 | 7.04 | 80.44% | 8.31 |
| 150 | 6.91 | 80.30% | 8.27 |
| 160 | 7.06 | 80.47% | 8.51 |
| 170 | 7.14 | 80.35% | 8.57 |
| 180 | 7.03 | 80.35% | 8.31 |
| 190 | 6.98 | 80.44% | 8.28 |
| 200 | 7.08 | 80.31% | 8.45 |
| 210 | 6.92 | 80.36% | 8.26 |
| 220 | 7.07 | 80.35% | 8.49 |
| 230 | 7.09 | 80.40% | 8.40 |
| 240 | 7.10 | 80.38% | 8.53 |
| 250 | 7.00 | 80.25% | 8.30 |
| 260 | 7.10 | 80.40% | 8.33 |
| 270 | 6.93 | 80.40% | 8.23 |
| 280 | 7.16 | 80.44% | 8.37 |
| 290 | 7.16 | 80.44% | 8.36 |
| 300 | 7.16 | 80.44% | 8.36 |
| 310 | 7.16 | 80.44% | 8.36 |
| 320 | 7.16 | 80.44% | 8.36 |
| 330 | 7.16 | 80.44% | 8.36 |
| 340 | 7.16 | 80.44% | 8.36 |
| 350 | 7.16 | 80.44% | 8.36 |
| 360 | 7.16 | 80.44% | 8.36 |
| 370 | 7.16 | 80.44% | 8.36 |
| 380 | 7.16 | 80.44% | 8.36 |
| 390 | 7.16 | 80.44% | 8.36 |
| 400 | 7.16 | 80.44% | 8.36 |
| 410 | 7.16 | 80.44% | 8.36 |
| 420 | 7.16 | 80.44% | 8.36 |
| 430 | 7.16 | 80.44% | 8.36 |
| 440 | 7.16 | 80.44% | 8.36 |
| 450 | 7.16 | 80.44% | 8.36 |
| 460 | 7.16 | 80.44% | 8.36 |
| 470 | 7.16 | 80.44% | 8.36 |
| 480 | 7.16 | 80.44% | 8.36 |
| 490 | 7.16 | 80.44% | 8.36 |
| 500 | 7.16 | 80.44% | 8.36 |
