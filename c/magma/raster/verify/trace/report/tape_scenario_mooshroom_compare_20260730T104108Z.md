# Tape replay: scenario_mooshroom_compare_20260730T104108Z

366 ticks, seed 0, world_time 6000, start (-6.50,4.00,0.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=37 independent=36 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=19 available=True
- world nearby_hash: checked=19 deltas=7 available=True

**Pixel gate: FAIL** over 37 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 36 | 1078629 | 75055 |
| hud | 32 | 238240 | 4375 |
| particles | 32 | 23965 | 1015 |
| thinline | 21 | 35950 | 2752 |
| viewmodel | 32 | 78667 | 2339 |

Failed frames (worst first, top 20):

- t=0: 217950 unexplained px, clusters [{'px': 64814, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'hud'}]
- t=10: 201246 unexplained px, clusters [{'px': 42053, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 607], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=20: 201246 unexplained px, clusters [{'px': 42053, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 607], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=30: 201246 unexplained px, clusters [{'px': 42053, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 607], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=40: 201246 unexplained px, clusters [{'px': 42053, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 607], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=300: 4876 unexplained px, clusters [{'px': 1683, 'cls': 'UNEXPLAINED', 'bbox': [197, 0, 222, 253]}, {'px': 2869, 'cls': 'UNEXPLAINED', 'bbox': [231, 0, 282, 444]}, {'px': 144, 'cls': 'UNEXPLAINED', 'bbox': [241, 52, 250, 71]}, {'px': 60, 'cls': 'UNEXPLAINED', 'bbox': [251, 85, 267, 93]}]
- t=280: 4769 unexplained px, clusters [{'px': 1703, 'cls': 'UNEXPLAINED', 'bbox': [197, 112, 222, 444]}, {'px': 51, 'cls': 'UNEXPLAINED', 'bbox': [221, 156, 229, 165]}, {'px': 3015, 'cls': 'UNEXPLAINED', 'bbox': [231, 0, 281, 444]}]
- t=290: 4686 unexplained px, clusters [{'px': 1645, 'cls': 'UNEXPLAINED', 'bbox': [197, 0, 223, 240]}, {'px': 2887, 'cls': 'UNEXPLAINED', 'bbox': [231, 0, 282, 444]}, {'px': 154, 'cls': 'UNEXPLAINED', 'bbox': [243, 141, 268, 154]}]
- t=320: 4384 unexplained px, clusters [{'px': 1650, 'cls': 'UNEXPLAINED', 'bbox': [197, 3, 223, 229]}, {'px': 95, 'cls': 'UNEXPLAINED', 'bbox': [232, 29, 242, 56]}, {'px': 2557, 'cls': 'UNEXPLAINED', 'bbox': [237, 0, 283, 444]}, {'px': 82, 'cls': 'UNEXPLAINED', 'bbox': [246, 61, 262, 67]}]
- t=80: 3939 unexplained px, clusters [{'px': 1005, 'cls': 'UNEXPLAINED', 'bbox': [198, 264, 228, 444]}, {'px': 123, 'cls': 'UNEXPLAINED', 'bbox': [225, 72, 235, 98]}, {'px': 1143, 'cls': 'UNEXPLAINED', 'bbox': [230, 0, 279, 168]}, {'px': 1617, 'cls': 'UNEXPLAINED', 'bbox': [236, 159, 279, 444]}]
- t=70: 3805 unexplained px, clusters [{'px': 901, 'cls': 'UNEXPLAINED', 'bbox': [198, 224, 228, 339]}, {'px': 2842, 'cls': 'UNEXPLAINED', 'bbox': [230, 0, 279, 444]}, {'px': 62, 'cls': 'UNEXPLAINED', 'bbox': [257, 84, 269, 93]}]
- t=90: 3772 unexplained px, clusters [{'px': 986, 'cls': 'UNEXPLAINED', 'bbox': [198, 254, 228, 444]}, {'px': 127, 'cls': 'UNEXPLAINED', 'bbox': [225, 103, 238, 127]}, {'px': 112, 'cls': 'UNEXPLAINED', 'bbox': [230, 136, 241, 165]}, {'px': 1481, 'cls': 'UNEXPLAINED', 'bbox': [235, 187, 279, 444]}]
- t=270: 2996 unexplained px, clusters [{'px': 1878, 'cls': 'UNEXPLAINED', 'bbox': [197, 0, 224, 444]}, {'px': 936, 'cls': 'UNEXPLAINED', 'bbox': [231, 236, 280, 292]}, {'px': 105, 'cls': 'UNEXPLAINED', 'bbox': [261, 232, 272, 245]}, {'px': 77, 'cls': 'UNEXPLAINED', 'bbox': [267, 302, 274, 316]}]
- t=250: 2753 unexplained px, clusters [{'px': 1369, 'cls': 'UNEXPLAINED', 'bbox': [197, 241, 222, 444]}, {'px': 1173, 'cls': 'UNEXPLAINED', 'bbox': [231, 316, 280, 392]}, {'px': 109, 'cls': 'UNEXPLAINED', 'bbox': [239, 400, 248, 444]}, {'px': 102, 'cls': 'UNEXPLAINED', 'bbox': [264, 387, 278, 402]}]
- t=60: 2692 unexplained px, clusters [{'px': 1990, 'cls': 'UNEXPLAINED', 'bbox': [198, 0, 237, 313]}, {'px': 106, 'cls': 'UNEXPLAINED', 'bbox': [230, 46, 241, 75]}, {'px': 75, 'cls': 'UNEXPLAINED', 'bbox': [237, 101, 245, 112]}, {'px': 521, 'cls': 'UNEXPLAINED', 'bbox': [242, 0, 279, 77]}]
- t=100: 2433 unexplained px, clusters [{'px': 965, 'cls': 'UNEXPLAINED', 'bbox': [198, 239, 228, 444]}, {'px': 92, 'cls': 'UNEXPLAINED', 'bbox': [225, 133, 236, 156]}, {'px': 1321, 'cls': 'UNEXPLAINED', 'bbox': [234, 213, 279, 444]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [257, 170, 269, 177]}]
- t=260: 1879 unexplained px, clusters [{'px': 1341, 'cls': 'UNEXPLAINED', 'bbox': [197, 256, 222, 444]}, {'px': 253, 'cls': 'UNEXPLAINED', 'bbox': [236, 349, 250, 444]}, {'px': 119, 'cls': 'UNEXPLAINED', 'bbox': [241, 267, 249, 285]}, {'px': 99, 'cls': 'UNEXPLAINED', 'bbox': [264, 334, 278, 348]}]
- t=310: 1846 unexplained px, clusters [{'px': 1532, 'cls': 'UNEXPLAINED', 'bbox': [197, 34, 222, 228]}, {'px': 151, 'cls': 'UNEXPLAINED', 'bbox': [242, 3, 251, 23]}, {'px': 84, 'cls': 'UNEXPLAINED', 'bbox': [246, 90, 268, 98]}, {'px': 79, 'cls': 'UNEXPLAINED', 'bbox': [252, 40, 267, 50]}]
- t=110: 1730 unexplained px, clusters [{'px': 1181, 'cls': 'UNEXPLAINED', 'bbox': [198, 192, 228, 444]}, {'px': 89, 'cls': 'UNEXPLAINED', 'bbox': [225, 163, 234, 185]}, {'px': 352, 'cls': 'UNEXPLAINED', 'bbox': [230, 193, 274, 228]}, {'px': 108, 'cls': 'UNEXPLAINED', 'bbox': [256, 226, 277, 246]}]
- t=50: 1628 unexplained px, clusters [{'px': 921, 'cls': 'UNEXPLAINED', 'bbox': [198, 222, 228, 288]}, {'px': 195, 'cls': 'UNEXPLAINED', 'bbox': [240, 20, 249, 44]}, {'px': 338, 'cls': 'UNEXPLAINED', 'bbox': [243, 0, 280, 37]}, {'px': 112, 'cls': 'UNEXPLAINED', 'bbox': [258, 256, 271, 268]}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 59.44 | 94.67% | 66.22 |
| 10 | 59.15 | 80.65% | 66.38 |
| 20 | 59.63 | 80.47% | 66.91 |
| 30 | 59.94 | 80.42% | 67.27 |
| 40 | 60.13 | 89.93% | 67.49 |
| 50 | 5.74 | 79.86% | 5.84 |
| 60 | 5.67 | 79.85% | 5.76 |
| 70 | 5.51 | 79.86% | 5.64 |
| 80 | 5.38 | 79.80% | 5.71 |
| 90 | 5.20 | 79.79% | 5.51 |
| 100 | 5.02 | 79.76% | 5.26 |
| 110 | 4.89 | 79.75% | 5.11 |
| 120 | 4.82 | 79.76% | 5.01 |
| 130 | 4.76 | 79.70% | 4.92 |
| 140 | 4.72 | 79.72% | 4.89 |
| 150 | 4.74 | 79.72% | 4.91 |
| 160 | 4.76 | 79.74% | 4.95 |
| 170 | 4.80 | 79.73% | 5.03 |
| 180 | 4.86 | 79.76% | 5.10 |
| 190 | 2.47 | 79.27% | 2.20 |
| 200 | 2.54 | 79.33% | 2.32 |
| 210 | 2.51 | 79.32% | 2.28 |
| 220 | 2.49 | 79.31% | 2.24 |
| 230 | 4.87 | 79.90% | 5.10 |
| 240 | 4.81 | 79.89% | 5.03 |
| 250 | 4.80 | 79.86% | 5.01 |
| 260 | 4.82 | 79.91% | 5.04 |
| 270 | 4.88 | 79.89% | 5.15 |
| 280 | 4.94 | 79.89% | 5.23 |
| 290 | 5.00 | 79.97% | 5.32 |
| 300 | 5.07 | 79.93% | 5.20 |
| 310 | 5.18 | 80.01% | 4.89 |
| 320 | 5.18 | 79.94% | 4.79 |
| 330 | 5.17 | 79.85% | 4.69 |
| 340 | 5.13 | 79.84% | 4.76 |
| 350 | 5.00 | 79.70% | 4.93 |
| 360 | 5.00 | 79.72% | 5.11 |
