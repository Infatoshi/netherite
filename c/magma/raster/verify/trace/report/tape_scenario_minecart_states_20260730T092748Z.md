# Tape replay: scenario_minecart_states_20260730T092748Z

406 ticks, seed 0, world_time 6000, start (-7.50,4.00,0.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=21 independent=20 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=21 available=True
- world nearby_hash: checked=21 deltas=0 available=True

**Pixel gate: FAIL** over 21 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 11 | 4901 | 165 |
| hud | 21 | 215609 | 16944 |
| particles | 14 | 127139 | 9927 |
| viewmodel | 19 | 155302 | 12701 |

Failed frames (worst first, top 20):

- t=240: 828 unexplained px, clusters [{'px': 57, 'cls': 'UNEXPLAINED', 'bbox': [308, 31, 318, 47]}, {'px': 77, 'cls': 'UNEXPLAINED', 'bbox': [328, 13, 334, 37]}, {'px': 108, 'cls': 'UNEXPLAINED', 'bbox': [343, 396, 357, 413]}, {'px': 51, 'cls': 'UNEXPLAINED', 'bbox': [348, 138, 357, 152]}]
- t=80: 655 unexplained px, clusters [{'px': 123, 'cls': 'UNEXPLAINED', 'bbox': [245, 117, 253, 144]}, {'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [301, 411, 308, 438]}, {'px': 51, 'cls': 'UNEXPLAINED', 'bbox': [339, 247, 347, 259]}, {'px': 73, 'cls': 'UNEXPLAINED', 'bbox': [344, 371, 357, 389]}]
- t=140: 546 unexplained px, clusters [{'px': 69, 'cls': 'UNEXPLAINED', 'bbox': [342, 399, 347, 427]}, {'px': 90, 'cls': 'UNEXPLAINED', 'bbox': [346, 194, 354, 216]}, {'px': 53, 'cls': 'UNEXPLAINED', 'bbox': [348, 43, 357, 54]}, {'px': 83, 'cls': 'UNEXPLAINED', 'bbox': [355, 375, 366, 389]}]
- t=60: 540 unexplained px, clusters [{'px': 165, 'cls': 'UNEXPLAINED', 'bbox': [243, 45, 253, 87]}, {'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [339, 220, 346, 233]}, {'px': 98, 'cls': 'UNEXPLAINED', 'bbox': [351, 248, 363, 265]}, {'px': 52, 'cls': 'UNEXPLAINED', 'bbox': [353, 36, 362, 47]}]
- t=180: 438 unexplained px, clusters [{'px': 66, 'cls': 'UNEXPLAINED', 'bbox': [348, 50, 355, 68]}, {'px': 65, 'cls': 'UNEXPLAINED', 'bbox': [355, 333, 366, 349]}, {'px': 72, 'cls': 'UNEXPLAINED', 'bbox': [356, 47, 362, 79]}, {'px': 68, 'cls': 'UNEXPLAINED', 'bbox': [356, 202, 365, 226]}]
- t=120: 433 unexplained px, clusters [{'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [339, 209, 345, 223]}, {'px': 62, 'cls': 'UNEXPLAINED', 'bbox': [339, 371, 347, 395]}, {'px': 61, 'cls': 'UNEXPLAINED', 'bbox': [348, 111, 357, 122]}, {'px': 73, 'cls': 'UNEXPLAINED', 'bbox': [351, 243, 363, 254]}]
- t=160: 371 unexplained px, clusters [{'px': 62, 'cls': 'UNEXPLAINED', 'bbox': [338, 20, 347, 40]}, {'px': 114, 'cls': 'UNEXPLAINED', 'bbox': [348, 84, 363, 104]}, {'px': 56, 'cls': 'UNEXPLAINED', 'bbox': [348, 166, 357, 177]}, {'px': 60, 'cls': 'UNEXPLAINED', 'bbox': [353, 360, 357, 388]}]
- t=220: 348 unexplained px, clusters [{'px': 63, 'cls': 'UNEXPLAINED', 'bbox': [321, 148, 327, 173]}, {'px': 51, 'cls': 'UNEXPLAINED', 'bbox': [323, 180, 335, 192]}, {'px': 70, 'cls': 'UNEXPLAINED', 'bbox': [341, 373, 352, 387]}, {'px': 60, 'cls': 'UNEXPLAINED', 'bbox': [353, 72, 362, 83]}]
- t=100: 264 unexplained px, clusters [{'px': 129, 'cls': 'UNEXPLAINED', 'bbox': [347, 95, 362, 119]}, {'px': 58, 'cls': 'UNEXPLAINED', 'bbox': [356, 314, 366, 330]}, {'px': 77, 'cls': 'UNEXPLAINED', 'bbox': [366, 320, 376, 337]}]
- t=260: 258 unexplained px, clusters [{'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [296, 54, 304, 69]}, {'px': 57, 'cls': 'UNEXPLAINED', 'bbox': [346, 123, 355, 143]}, {'px': 67, 'cls': 'UNEXPLAINED', 'bbox': [356, 352, 366, 366]}, {'px': 79, 'cls': 'UNEXPLAINED', 'bbox': [380, 37, 383, 72]}]
- t=200: 220 unexplained px, clusters [{'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [329, 50, 334, 70]}, {'px': 51, 'cls': 'UNEXPLAINED', 'bbox': [339, 148, 352, 167]}, {'px': 66, 'cls': 'UNEXPLAINED', 'bbox': [355, 52, 362, 71]}, {'px': 53, 'cls': 'UNEXPLAINED', 'bbox': [367, 74, 376, 89]}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 3.78 | 81.54% | 3.14 |
| 20 | 2.48 | 79.26% | 2.17 |
| 40 | 2.48 | 79.26% | 2.17 |
| 60 | 7.15 | 79.62% | 7.75 |
| 80 | 7.37 | 79.61% | 8.02 |
| 100 | 7.54 | 79.62% | 8.35 |
| 120 | 7.72 | 79.66% | 8.56 |
| 140 | 7.96 | 79.66% | 8.87 |
| 160 | 7.93 | 79.65% | 9.39 |
| 180 | 7.96 | 79.67% | 9.35 |
| 200 | 7.97 | 79.58% | 9.19 |
| 220 | 7.85 | 79.64% | 8.95 |
| 240 | 7.71 | 79.62% | 8.58 |
| 260 | 7.57 | 79.64% | 8.36 |
| 280 | 2.91 | 79.04% | 2.79 |
| 300 | 2.91 | 79.04% | 2.79 |
| 320 | 2.91 | 79.04% | 2.79 |
| 340 | 2.91 | 79.04% | 2.79 |
| 360 | 2.91 | 79.04% | 2.79 |
| 380 | 2.91 | 79.04% | 2.79 |
| 400 | 2.91 | 79.04% | 2.79 |
