# Tape replay: scenario_ladder_climb_20260730T104155Z

307 ticks, seed 0, world_time 6000, start (0.50,4.00,2.50).

**FIRST DIVERGENCE: tick 29, field `z`** oracle=4.8308892313696425 magma=4.69999998807907 |d|=0.131; inputs {'f': 1.0, 's': 0.0, 'jump': 0, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 1.1260 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=31 independent=30 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=16 available=True
- world nearby_hash: checked=16 deltas=4 available=True

**Pixel gate: FAIL** over 31 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 31 | 2910626 | 141910 |
| bossbar | 31 | 358925 | 34998 |
| hud | 31 | 829469 | 30254 |
| particles | 8 | 5041 | 740 |
| thinline | 1 | 808 | 808 |
| viewmodel | 20 | 340836 | 38559 |

Failed frames (worst first, top 20):

- t=50: 183328 unexplained px, clusters [{'px': 40123, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 814], 'soak_from': 'viewmodel'}, {'px': 141910, 'cls': 'UNEXPLAINED', 'bbox': [45, 39, 383, 814]}, {'px': 1155, 'cls': 'UNEXPLAINED', 'bbox': [61, 819, 93, 853]}, {'px': 140, 'cls': 'UNEXPLAINED', 'bbox': [143, 819, 146, 853]}]
- t=60: 181108 unexplained px, clusters [{'px': 50382, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 814], 'soak_from': 'viewmodel'}, {'px': 1330, 'cls': 'UNEXPLAINED', 'bbox': [281, 819, 318, 853], 'soak_from': 'viewmodel'}, {'px': 430, 'cls': 'UNEXPLAINED', 'bbox': [379, 513, 383, 598], 'soak_from': 'viewmodel'}, {'px': 128126, 'cls': 'UNEXPLAINED', 'bbox': [45, 39, 383, 814]}]
- t=80: 166337 unexplained px, clusters [{'px': 34002, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 814], 'soak_from': 'viewmodel'}, {'px': 1776, 'cls': 'UNEXPLAINED', 'bbox': [319, 525, 342, 598], 'soak_from': 'viewmodel'}, {'px': 4450, 'cls': 'UNEXPLAINED', 'bbox': [357, 445, 383, 626], 'soak_from': 'viewmodel'}, {'px': 125884, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853]}]
- t=90: 166337 unexplained px, clusters [{'px': 34002, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 814], 'soak_from': 'viewmodel'}, {'px': 1776, 'cls': 'UNEXPLAINED', 'bbox': [319, 525, 342, 598], 'soak_from': 'viewmodel'}, {'px': 4450, 'cls': 'UNEXPLAINED', 'bbox': [357, 445, 383, 626], 'soak_from': 'viewmodel'}, {'px': 125884, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853]}]
- t=100: 166337 unexplained px, clusters [{'px': 34002, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 814], 'soak_from': 'viewmodel'}, {'px': 1776, 'cls': 'UNEXPLAINED', 'bbox': [319, 525, 342, 598], 'soak_from': 'viewmodel'}, {'px': 4450, 'cls': 'UNEXPLAINED', 'bbox': [357, 445, 383, 626], 'soak_from': 'viewmodel'}, {'px': 125884, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853]}]
- t=110: 166337 unexplained px, clusters [{'px': 34002, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 814], 'soak_from': 'viewmodel'}, {'px': 1776, 'cls': 'UNEXPLAINED', 'bbox': [319, 525, 342, 598], 'soak_from': 'viewmodel'}, {'px': 4450, 'cls': 'UNEXPLAINED', 'bbox': [357, 445, 383, 626], 'soak_from': 'viewmodel'}, {'px': 125884, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853]}]
- t=120: 166337 unexplained px, clusters [{'px': 34002, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 814], 'soak_from': 'viewmodel'}, {'px': 1776, 'cls': 'UNEXPLAINED', 'bbox': [319, 525, 342, 598], 'soak_from': 'viewmodel'}, {'px': 4450, 'cls': 'UNEXPLAINED', 'bbox': [357, 445, 383, 626], 'soak_from': 'viewmodel'}, {'px': 125884, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853]}]
- t=150: 164881 unexplained px, clusters [{'px': 39239, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 814], 'soak_from': 'viewmodel'}, {'px': 1400, 'cls': 'UNEXPLAINED', 'bbox': [233, 819, 272, 853], 'soak_from': 'viewmodel'}, {'px': 122842, 'cls': 'UNEXPLAINED', 'bbox': [45, 39, 383, 814]}, {'px': 1400, 'cls': 'UNEXPLAINED', 'bbox': [233, 0, 272, 34]}]
- t=140: 164400 unexplained px, clusters [{'px': 38303, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 814], 'soak_from': 'viewmodel'}, {'px': 1645, 'cls': 'UNEXPLAINED', 'bbox': [272, 819, 318, 853], 'soak_from': 'viewmodel'}, {'px': 148, 'cls': 'UNEXPLAINED', 'bbox': [319, 525, 320, 598], 'soak_from': 'viewmodel'}, {'px': 123398, 'cls': 'UNEXPLAINED', 'bbox': [45, 39, 383, 814]}]
- t=40: 148560 unexplained px, clusters [{'px': 35226, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 814], 'soak_from': 'viewmodel'}, {'px': 1715, 'cls': 'UNEXPLAINED', 'bbox': [251, 819, 299, 853], 'soak_from': 'viewmodel'}, {'px': 360, 'cls': 'UNEXPLAINED', 'bbox': [319, 513, 348, 524], 'soak_from': 'viewmodel'}, {'px': 3382, 'cls': 'UNEXPLAINED', 'bbox': [363, 445, 383, 616], 'soak_from': 'viewmodel'}]
- t=70: 145702 unexplained px, clusters [{'px': 40290, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 814], 'soak_from': 'viewmodel'}, {'px': 3626, 'cls': 'UNEXPLAINED', 'bbox': [321, 525, 369, 598], 'soak_from': 'viewmodel'}, {'px': 100038, 'cls': 'UNEXPLAINED', 'bbox': [45, 39, 383, 814]}, {'px': 1680, 'cls': 'UNEXPLAINED', 'bbox': [77, 819, 124, 853]}]
- t=130: 124242 unexplained px, clusters [{'px': 122842, 'cls': 'UNEXPLAINED', 'bbox': [45, 39, 383, 814]}, {'px': 1400, 'cls': 'UNEXPLAINED', 'bbox': [233, 0, 272, 34]}]
- t=170: 123954 unexplained px, clusters [{'px': 123954, 'cls': 'UNEXPLAINED', 'bbox': [45, 39, 383, 814]}]
- t=180: 123954 unexplained px, clusters [{'px': 123954, 'cls': 'UNEXPLAINED', 'bbox': [45, 39, 383, 814]}]
- t=190: 123954 unexplained px, clusters [{'px': 123954, 'cls': 'UNEXPLAINED', 'bbox': [45, 39, 383, 814]}]
- t=200: 123954 unexplained px, clusters [{'px': 123954, 'cls': 'UNEXPLAINED', 'bbox': [45, 39, 383, 814]}]
- t=160: 123534 unexplained px, clusters [{'px': 123398, 'cls': 'UNEXPLAINED', 'bbox': [45, 39, 383, 814]}, {'px': 68, 'cls': 'UNEXPLAINED', 'bbox': [60, 0, 75, 33]}, {'px': 68, 'cls': 'UNEXPLAINED', 'bbox': [60, 820, 75, 853]}]
- t=210: 66733 unexplained px, clusters [{'px': 6222, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 86]}, {'px': 300, 'cls': 'UNEXPLAINED', 'bbox': [45, 149, 63, 169]}, {'px': 51362, 'cls': 'UNEXPLAINED', 'bbox': [45, 269, 383, 584]}, {'px': 69, 'cls': 'UNEXPLAINED', 'bbox': [45, 756, 53, 766]}]
- t=30: 65635 unexplained px, clusters [{'px': 56420, 'cls': 'UNEXPLAINED', 'bbox': [45, 251, 383, 602]}, {'px': 7751, 'cls': 'UNEXPLAINED', 'bbox': [56, 617, 192, 853]}, {'px': 528, 'cls': 'UNEXPLAINED', 'bbox': [61, 806, 71, 853]}, {'px': 120, 'cls': 'UNEXPLAINED', 'bbox': [233, 0, 237, 23]}]
- t=220: 53649 unexplained px, clusters [{'px': 24209, 'cls': 'UNEXPLAINED', 'bbox': [45, 129, 383, 233]}, {'px': 17251, 'cls': 'UNEXPLAINED', 'bbox': [45, 374, 383, 475]}, {'px': 10870, 'cls': 'UNEXPLAINED', 'bbox': [45, 620, 192, 702]}, {'px': 75, 'cls': 'UNEXPLAINED', 'bbox': [62, 290, 71, 299]}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 4.77 | 90.25% | 5.53 |
| 10 | 3.52 | 88.03% | 4.52 |
| 20 | 4.02 | 88.88% | 5.18 |
| 30 | 14.61 | 94.26% | 15.55 |
| 40 | 17.29 | 94.26% | 19.03 |
| 50 | 16.82 | 94.24% | 19.97 |
| 60 | 17.50 | 94.25% | 18.44 |
| 70 | 17.71 | 94.26% | 18.65 |
| 80 | 19.69 | 94.25% | 20.29 |
| 90 | 19.69 | 94.25% | 20.29 |
| 100 | 19.69 | 94.25% | 20.29 |
| 110 | 19.69 | 94.25% | 20.29 |
| 120 | 19.69 | 94.25% | 20.29 |
| 130 | 16.98 | 94.24% | 19.97 |
| 140 | 16.96 | 94.14% | 19.71 |
| 150 | 16.91 | 93.95% | 19.54 |
| 160 | 16.74 | 94.20% | 19.54 |
| 170 | 17.89 | 94.24% | 21.08 |
| 180 | 17.89 | 94.24% | 21.08 |
| 190 | 17.89 | 94.24% | 21.08 |
| 200 | 17.89 | 94.24% | 21.08 |
| 210 | 16.68 | 96.22% | 15.43 |
| 220 | 19.73 | 93.41% | 23.80 |
| 230 | 12.07 | 93.34% | 14.78 |
| 240 | 9.47 | 92.70% | 10.38 |
| 250 | 10.23 | 85.77% | 10.88 |
| 260 | 10.21 | 83.78% | 10.86 |
| 270 | 10.24 | 91.19% | 10.90 |
| 280 | 10.22 | 84.04% | 10.87 |
| 290 | 10.22 | 84.06% | 10.87 |
| 300 | 10.22 | 84.07% | 10.87 |
