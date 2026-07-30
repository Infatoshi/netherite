# Tape replay: scenario_cave_spider_pen_20260730T103930Z

407 ticks, seed 0, world_time 18000, start (0.50,4.00,2.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=41 independent=40 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=21 available=True
- world nearby_hash: checked=21 deltas=11 available=True

**Pixel gate: FAIL** over 41 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 24 | 1367274 | 55108 |
| bossbar | 24 | 12340 | 816 |
| hud | 25 | 663892 | 27494 |
| particles | 33 | 10061 | 254 |
| thinline | 24 | 32619 | 2526 |
| viewmodel | 24 | 198147 | 39533 |

Failed frames (worst first, top 20):

- t=70: 97239 unexplained px, clusters [{'px': 45663, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 51576, 'cls': 'UNEXPLAINED', 'bbox': [192, 0, 383, 444]}]
- t=300: 96522 unexplained px, clusters [{'px': 46517, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 135, 'cls': 'UNEXPLAINED', 'bbox': [373, 745, 383, 768], 'soak_from': 'viewmodel'}, {'px': 49870, 'cls': 'UNEXPLAINED', 'bbox': [188, 0, 383, 444]}]
- t=80: 95892 unexplained px, clusters [{'px': 48151, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 52, 'cls': 'UNEXPLAINED', 'bbox': [110, 3, 114, 27]}, {'px': 47689, 'cls': 'UNEXPLAINED', 'bbox': [188, 0, 383, 444]}]
- t=290: 94304 unexplained px, clusters [{'px': 49246, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 45058, 'cls': 'UNEXPLAINED', 'bbox': [188, 0, 383, 444]}]
- t=90: 92264 unexplained px, clusters [{'px': 50157, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 136, 'cls': 'UNEXPLAINED', 'bbox': [374, 746, 383, 769], 'soak_from': 'viewmodel'}, {'px': 41903, 'cls': 'UNEXPLAINED', 'bbox': [188, 0, 383, 444]}, {'px': 68, 'cls': 'UNEXPLAINED', 'bbox': [258, 1, 265, 11]}]
- t=280: 90605 unexplained px, clusters [{'px': 50882, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 101, 'cls': 'UNEXPLAINED', 'bbox': [86, 0, 94, 18]}, {'px': 93, 'cls': 'UNEXPLAINED', 'bbox': [115, 5, 124, 16]}, {'px': 39392, 'cls': 'UNEXPLAINED', 'bbox': [188, 0, 383, 451]}]
- t=100: 88625 unexplained px, clusters [{'px': 52146, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 116, 'cls': 'UNEXPLAINED', 'bbox': [83, 0, 92, 23]}, {'px': 157, 'cls': 'UNEXPLAINED', 'bbox': [86, 26, 94, 72]}, {'px': 92, 'cls': 'UNEXPLAINED', 'bbox': [115, 59, 124, 69]}]
- t=270: 86779 unexplained px, clusters [{'px': 52081, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 136, 'cls': 'UNEXPLAINED', 'bbox': [374, 735, 383, 759], 'soak_from': 'viewmodel'}, {'px': 77, 'cls': 'UNEXPLAINED', 'bbox': [65, 6, 72, 16]}, {'px': 150, 'cls': 'UNEXPLAINED', 'bbox': [83, 3, 94, 30]}]
- t=110: 83555 unexplained px, clusters [{'px': 53058, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 75, 'cls': 'UNEXPLAINED', 'bbox': [65, 61, 72, 71]}, {'px': 150, 'cls': 'UNEXPLAINED', 'bbox': [83, 57, 94, 84]}, {'px': 150, 'cls': 'UNEXPLAINED', 'bbox': [83, 93, 92, 120]}]
- t=260: 80922 unexplained px, clusters [{'px': 53592, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 76, 'cls': 'UNEXPLAINED', 'bbox': [65, 104, 72, 114]}, {'px': 150, 'cls': 'UNEXPLAINED', 'bbox': [83, 100, 94, 127]}, {'px': 150, 'cls': 'UNEXPLAINED', 'bbox': [83, 135, 92, 163]}]
- t=120: 77043 unexplained px, clusters [{'px': 53740, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 121, 'cls': 'UNEXPLAINED', 'bbox': [198, 445, 211, 467], 'soak_from': 'viewmodel'}, {'px': 134, 'cls': 'UNEXPLAINED', 'bbox': [374, 738, 383, 761], 'soak_from': 'viewmodel'}, {'px': 76, 'cls': 'UNEXPLAINED', 'bbox': [65, 159, 72, 168]}]
- t=250: 73604 unexplained px, clusters [{'px': 54284, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 76, 'cls': 'UNEXPLAINED', 'bbox': [65, 202, 72, 211]}, {'px': 149, 'cls': 'UNEXPLAINED', 'bbox': [83, 196, 94, 223]}, {'px': 150, 'cls': 'UNEXPLAINED', 'bbox': [83, 233, 92, 260]}]
- t=130: 69668 unexplained px, clusters [{'px': 55108, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 77, 'cls': 'UNEXPLAINED', 'bbox': [65, 257, 72, 266]}, {'px': 150, 'cls': 'UNEXPLAINED', 'bbox': [83, 250, 94, 277]}, {'px': 152, 'cls': 'UNEXPLAINED', 'bbox': [83, 287, 92, 314]}]
- t=240: 65939 unexplained px, clusters [{'px': 54924, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 115, 'cls': 'UNEXPLAINED', 'bbox': [374, 730, 383, 751], 'soak_from': 'viewmodel'}, {'px': 76, 'cls': 'UNEXPLAINED', 'bbox': [65, 300, 72, 309]}, {'px': 152, 'cls': 'UNEXPLAINED', 'bbox': [83, 293, 94, 320]}]
- t=140: 60264 unexplained px, clusters [{'px': 54255, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 80, 'cls': 'UNEXPLAINED', 'bbox': [65, 355, 72, 364]}, {'px': 154, 'cls': 'UNEXPLAINED', 'bbox': [83, 384, 92, 411]}, {'px': 90, 'cls': 'UNEXPLAINED', 'bbox': [115, 440, 124, 448]}]
- t=230: 55394 unexplained px, clusters [{'px': 106, 'cls': 'UNEXPLAINED', 'bbox': [193, 496, 212, 508], 'soak_from': 'viewmodel'}, {'px': 49795, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 75, 'cls': 'UNEXPLAINED', 'bbox': [249, 445, 257, 460], 'soak_from': 'viewmodel'}, {'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [258, 478, 265, 485], 'soak_from': 'viewmodel'}]
- t=150: 48186 unexplained px, clusters [{'px': 44171, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [216, 451, 221, 459], 'soak_from': 'viewmodel'}, {'px': 71, 'cls': 'UNEXPLAINED', 'bbox': [240, 445, 241, 484], 'soak_from': 'viewmodel'}, {'px': 68, 'cls': 'UNEXPLAINED', 'bbox': [258, 526, 265, 534], 'soak_from': 'viewmodel'}]
- t=220: 2198 unexplained px, clusters [{'px': 77, 'cls': 'UNEXPLAINED', 'bbox': [65, 496, 72, 505]}, {'px': 150, 'cls': 'UNEXPLAINED', 'bbox': [83, 486, 94, 514]}, {'px': 147, 'cls': 'UNEXPLAINED', 'bbox': [83, 524, 92, 551]}, {'px': 157, 'cls': 'UNEXPLAINED', 'bbox': [86, 552, 94, 598]}]
- t=160: 1764 unexplained px, clusters [{'px': 78, 'cls': 'UNEXPLAINED', 'bbox': [65, 551, 72, 560]}, {'px': 150, 'cls': 'UNEXPLAINED', 'bbox': [83, 540, 94, 568]}, {'px': 150, 'cls': 'UNEXPLAINED', 'bbox': [83, 578, 92, 606]}, {'px': 156, 'cls': 'UNEXPLAINED', 'bbox': [86, 606, 94, 652]}]
- t=170: 1628 unexplained px, clusters [{'px': 76, 'cls': 'UNEXPLAINED', 'bbox': [65, 649, 72, 658]}, {'px': 149, 'cls': 'UNEXPLAINED', 'bbox': [83, 637, 94, 665]}, {'px': 151, 'cls': 'UNEXPLAINED', 'bbox': [83, 674, 92, 703]}, {'px': 158, 'cls': 'UNEXPLAINED', 'bbox': [86, 702, 94, 749]}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 3.28 | 87.00% | 3.10 |
| 10 | 1.34 | 84.72% | 1.69 |
| 20 | 1.34 | 84.72% | 1.69 |
| 30 | 1.35 | 84.72% | 1.70 |
| 40 | 1.35 | 84.66% | 1.70 |
| 50 | 1.35 | 84.65% | 1.70 |
| 60 | 1.35 | 84.68% | 1.71 |
| 70 | 27.95 | 86.41% | 33.27 |
| 80 | 27.71 | 86.60% | 33.33 |
| 90 | 27.01 | 86.70% | 32.29 |
| 100 | 26.66 | 86.79% | 31.10 |
| 110 | 25.68 | 86.26% | 30.31 |
| 120 | 23.45 | 84.15% | 27.84 |
| 130 | 20.46 | 82.75% | 24.63 |
| 140 | 17.59 | 82.47% | 21.47 |
| 150 | 14.93 | 81.83% | 17.01 |
| 160 | 12.09 | 81.13% | 12.67 |
| 170 | 9.46 | 80.64% | 8.79 |
| 180 | 7.69 | 80.17% | 5.96 |
| 190 | 6.29 | 79.89% | 5.37 |
| 200 | 8.75 | 80.44% | 7.34 |
| 210 | 10.96 | 80.95% | 10.89 |
| 220 | 13.40 | 81.31% | 15.08 |
| 230 | 16.47 | 82.49% | 19.42 |
| 240 | 19.21 | 82.40% | 23.22 |
| 250 | 22.07 | 83.45% | 26.63 |
| 260 | 24.82 | 85.22% | 29.30 |
| 270 | 26.35 | 86.83% | 30.60 |
| 280 | 26.90 | 86.71% | 31.94 |
| 290 | 27.42 | 86.66% | 32.63 |
| 300 | 27.70 | 86.49% | 33.10 |
| 310 | 1.34 | 84.69% | 1.68 |
| 320 | 1.33 | 84.70% | 1.67 |
| 330 | 1.33 | 84.77% | 1.68 |
| 340 | 1.33 | 84.77% | 1.68 |
| 350 | 1.33 | 84.77% | 1.68 |
| 360 | 1.33 | 84.77% | 1.68 |
| 370 | 1.33 | 84.77% | 1.68 |
| 380 | 1.33 | 84.77% | 1.68 |
| 390 | 1.33 | 84.77% | 1.68 |
| 400 | 1.33 | 84.77% | 1.68 |
