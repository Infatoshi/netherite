# Tape replay: scenario_stone_slab_halves_20260730T102746Z

328 ticks, seed 0, world_time 6000, start (0.50,4.00,-1.50).

**FIRST DIVERGENCE: tick 69, field `y`** oracle=4.5 magma=4 |d|=0.5; inputs {'f': 1.0, 's': 0.0, 'jump': 0, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 28.1501 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=17 independent=16 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=17 available=True
- world nearby_hash: checked=17 deltas=7 available=True

**Pixel gate: FAIL** over 17 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 13 | 1528559 | 71053 |
| hud | 15 | 541256 | 34694 |
| particles | 16 | 51572 | 14219 |
| viewmodel | 5 | 53376 | 29823 |

Failed frames (worst first, top 20):

- t=320: 134849 unexplained px, clusters [{'px': 63796, 'cls': 'UNEXPLAINED', 'bbox': [199, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 71053, 'cls': 'UNEXPLAINED', 'bbox': [198, 0, 383, 444]}]
- t=220: 134375 unexplained px, clusters [{'px': 64591, 'cls': 'UNEXPLAINED', 'bbox': [200, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 69784, 'cls': 'UNEXPLAINED', 'bbox': [198, 0, 383, 444]}]
- t=300: 131804 unexplained px, clusters [{'px': 66487, 'cls': 'UNEXPLAINED', 'bbox': [197, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 65317, 'cls': 'UNEXPLAINED', 'bbox': [198, 0, 383, 444]}]
- t=240: 130051 unexplained px, clusters [{'px': 67018, 'cls': 'UNEXPLAINED', 'bbox': [198, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 63033, 'cls': 'UNEXPLAINED', 'bbox': [199, 0, 383, 444]}]
- t=140: 129127 unexplained px, clusters [{'px': 59752, 'cls': 'UNEXPLAINED', 'bbox': [202, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 69375, 'cls': 'UNEXPLAINED', 'bbox': [206, 0, 383, 444]}]
- t=200: 129076 unexplained px, clusters [{'px': 59701, 'cls': 'UNEXPLAINED', 'bbox': [205, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 69375, 'cls': 'UNEXPLAINED', 'bbox': [203, 0, 383, 444]}]
- t=180: 129026 unexplained px, clusters [{'px': 59677, 'cls': 'UNEXPLAINED', 'bbox': [203, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 69349, 'cls': 'UNEXPLAINED', 'bbox': [204, 0, 383, 444]}]
- t=160: 129025 unexplained px, clusters [{'px': 59685, 'cls': 'UNEXPLAINED', 'bbox': [205, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 69340, 'cls': 'UNEXPLAINED', 'bbox': [207, 0, 383, 444]}]
- t=280: 123457 unexplained px, clusters [{'px': 68208, 'cls': 'UNEXPLAINED', 'bbox': [197, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 55180, 'cls': 'UNEXPLAINED', 'bbox': [197, 0, 383, 444]}, {'px': 69, 'cls': 'UNEXPLAINED', 'bbox': [318, 0, 325, 21]}]
- t=260: 119464 unexplained px, clusters [{'px': 68646, 'cls': 'UNEXPLAINED', 'bbox': [198, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 50705, 'cls': 'UNEXPLAINED', 'bbox': [198, 0, 383, 444]}, {'px': 58, 'cls': 'UNEXPLAINED', 'bbox': [314, 16, 320, 33]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [343, 0, 347, 22]}]
- t=120: 106697 unexplained px, clusters [{'px': 49709, 'cls': 'UNEXPLAINED', 'bbox': [206, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [351, 511, 353, 532], 'soak_from': 'viewmodel'}, {'px': 56, 'cls': 'UNEXPLAINED', 'bbox': [354, 776, 356, 817], 'soak_from': 'viewmodel'}, {'px': 81, 'cls': 'UNEXPLAINED', 'bbox': [361, 790, 366, 819], 'soak_from': 'viewmodel'}]
- t=100: 87787 unexplained px, clusters [{'px': 37520, 'cls': 'UNEXPLAINED', 'bbox': [205, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 83, 'cls': 'UNEXPLAINED', 'bbox': [322, 706, 330, 741], 'soak_from': 'viewmodel'}, {'px': 63, 'cls': 'UNEXPLAINED', 'bbox': [329, 593, 335, 621], 'soak_from': 'viewmodel'}, {'px': 390, 'cls': 'UNEXPLAINED', 'bbox': [336, 607, 359, 654], 'soak_from': 'viewmodel'}]
- t=80: 43821 unexplained px, clusters [{'px': 5128, 'cls': 'UNEXPLAINED', 'bbox': [172, 0, 186, 853], 'soak_from': 'particles'}, {'px': 36837, 'cls': 'UNEXPLAINED', 'bbox': [207, 0, 383, 444], 'soak_from': 'particles'}, {'px': 1221, 'cls': 'UNEXPLAINED', 'bbox': [269, 231, 316, 314]}, {'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [271, 258, 274, 291]}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 4.66 | 79.14% | 6.10 |
| 20 | 3.33 | 76.86% | 5.13 |
| 40 | 3.33 | 76.85% | 5.13 |
| 60 | 8.92 | 76.87% | 14.62 |
| 80 | 20.28 | 78.84% | 26.98 |
| 100 | 20.96 | 78.88% | 26.27 |
| 120 | 19.90 | 78.74% | 26.73 |
| 140 | 23.30 | 79.07% | 28.32 |
| 160 | 23.11 | 79.05% | 28.23 |
| 180 | 23.16 | 79.05% | 28.33 |
| 200 | 23.18 | 79.05% | 28.31 |
| 220 | 23.89 | 78.81% | 29.20 |
| 240 | 24.15 | 78.80% | 28.75 |
| 260 | 22.10 | 78.77% | 26.36 |
| 280 | 22.67 | 78.79% | 27.16 |
| 300 | 24.17 | 78.81% | 28.97 |
| 320 | 23.87 | 78.80% | 29.42 |
