# Tape replay: scenario_boat_ride_20260730T104241Z

503 ticks, seed 0, world_time 6000, start (0.50,3.00,0.50).

**FIRST DIVERGENCE: tick 18, field `y`** oracle=3.109952960148778 magma=3 |d|=0.11; inputs {'f': 0.0, 's': 0.0, 'jump': 0, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 1, 'hb': 0}. End-of-tape euclid 0.0000 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=51 independent=50 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=26 available=True
- world nearby_hash: checked=26 deltas=13 available=True

**Pixel gate: FAIL** over 51 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 38 | 5200747 | 111521 |
| hud | 24 | 637195 | 44582 |
| particles | 34 | 37985 | 796 |
| thinline | 27 | 44456 | 8814 |
| transit | 1 | 23617 | 12520 |
| viewmodel | 26 | 343339 | 33523 |

Skipped renderable entity rows (more than 4 fails the gate):

- `EntityBoat`: 503 rows (FAIL)

Failed frames (worst first, top 20):

- t=80: 261015 unexplained px, clusters [{'px': 64674, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 931, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'hud'}]
- t=110: 258740 unexplained px, clusters [{'px': 64124, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 1013, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 902, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'hud'}]
- t=70: 255330 unexplained px, clusters [{'px': 59312, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1015, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'hud'}]
- t=90: 254697 unexplained px, clusters [{'px': 58508, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 1020, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 878, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'hud'}]
- t=100: 254484 unexplained px, clusters [{'px': 58561, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 1013, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 824, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'hud'}]
- t=120: 251566 unexplained px, clusters [{'px': 59227, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'hud'}]
- t=60: 250306 unexplained px, clusters [{'px': 53464, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'hud'}]
- t=130: 248576 unexplained px, clusters [{'px': 61420, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'hud'}]
- t=140: 242073 unexplained px, clusters [{'px': 64296, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 943, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'hud'}]
- t=270: 236253 unexplained px, clusters [{'px': 61441, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 418, 'cls': 'UNEXPLAINED', 'bbox': [442, 264, 473, 281], 'soak_from': 'hud'}, {'px': 752, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}, {'px': 724, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'hud'}]
- t=260: 210063 unexplained px, clusters [{'px': 48416, 'cls': 'UNEXPLAINED', 'bbox': [384, 65, 479, 853], 'soak_from': 'hud'}, {'px': 350, 'cls': 'UNEXPLAINED', 'bbox': [406, 59, 428, 101], 'soak_from': 'hud'}, {'px': 1376, 'cls': 'UNEXPLAINED', 'bbox': [418, 0, 477, 112], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}]
- t=150: 185360 unexplained px, clusters [{'px': 194, 'cls': 'UNEXPLAINED', 'bbox': [384, 53, 394, 84], 'soak_from': 'hud'}, {'px': 53289, 'cls': 'UNEXPLAINED', 'bbox': [384, 65, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=160: 184243 unexplained px, clusters [{'px': 62220, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'hud'}]
- t=190: 177400 unexplained px, clusters [{'px': 64382, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'hud'}]
- t=240: 147124 unexplained px, clusters [{'px': 45956, 'cls': 'UNEXPLAINED', 'bbox': [384, 47, 479, 806], 'soak_from': 'hud'}, {'px': 136, 'cls': 'UNEXPLAINED', 'bbox': [398, 0, 411, 28], 'soak_from': 'hud'}, {'px': 183, 'cls': 'UNEXPLAINED', 'bbox': [435, 847, 473, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}]
- t=250: 145462 unexplained px, clusters [{'px': 63323, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'hud'}]
- t=50: 139443 unexplained px, clusters [{'px': 60926, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'transit'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'transit'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'transit'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'transit'}]
- t=30: 135931 unexplained px, clusters [{'px': 63482, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'transit'}, {'px': 80, 'cls': 'UNEXPLAINED', 'bbox': [424, 568, 429, 585], 'soak_from': 'transit'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'transit'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'transit'}]
- t=40: 114466 unexplained px, clusters [{'px': 526, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 393, 66], 'soak_from': 'transit'}, {'px': 43875, 'cls': 'UNEXPLAINED', 'bbox': [384, 65, 479, 788], 'soak_from': 'transit'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'transit'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'transit'}]
- t=220: 110221 unexplained px, clusters [{'px': 59877, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 164, 'cls': 'UNEXPLAINED', 'bbox': [402, 542, 419, 559], 'soak_from': 'hud'}, {'px': 164, 'cls': 'UNEXPLAINED', 'bbox': [402, 558, 419, 575], 'soak_from': 'hud'}, {'px': 164, 'cls': 'UNEXPLAINED', 'bbox': [402, 574, 419, 591], 'soak_from': 'hud'}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 8.52 | 80.59% | 9.01 |
| 10 | 4.86 | 78.36% | 6.06 |
| 20 | 18.06 | 83.96% | 21.88 |
| 30 | 22.97 | 85.05% | 23.16 |
| 40 | 19.60 | 84.64% | 23.04 |
| 50 | 23.44 | 84.79% | 26.76 |
| 60 | 56.46 | 85.08% | 73.38 |
| 70 | 59.38 | 85.04% | 76.36 |
| 80 | 57.61 | 85.02% | 74.36 |
| 90 | 59.34 | 85.01% | 76.40 |
| 100 | 59.26 | 85.00% | 76.28 |
| 110 | 57.12 | 85.00% | 74.06 |
| 120 | 58.16 | 84.99% | 74.42 |
| 130 | 56.96 | 84.99% | 72.23 |
| 140 | 53.90 | 84.99% | 67.32 |
| 150 | 41.72 | 84.68% | 52.47 |
| 160 | 30.27 | 84.95% | 33.72 |
| 170 | 21.42 | 84.46% | 24.02 |
| 180 | 17.90 | 84.64% | 20.54 |
| 190 | 29.86 | 84.68% | 33.59 |
| 200 | 21.90 | 83.92% | 26.67 |
| 210 | 21.16 | 84.73% | 24.69 |
| 220 | 23.02 | 83.41% | 23.38 |
| 230 | 21.00 | 85.14% | 23.54 |
| 240 | 32.40 | 83.21% | 39.95 |
| 250 | 25.80 | 85.09% | 28.14 |
| 260 | 31.42 | 84.04% | 39.69 |
| 270 | 37.57 | 85.27% | 43.58 |
| 280 | 26.37 | 84.64% | 31.84 |
| 290 | 32.02 | 84.57% | 39.10 |
| 300 | 28.11 | 84.57% | 26.08 |
| 310 | 21.38 | 84.57% | 23.76 |
| 320 | 21.43 | 84.57% | 23.79 |
| 330 | 27.57 | 84.57% | 25.69 |
| 340 | 21.45 | 84.57% | 23.82 |
| 350 | 21.97 | 84.57% | 23.83 |
| 360 | 25.73 | 84.57% | 24.54 |
| 370 | 11.94 | 79.04% | 13.01 |
| 380 | 11.99 | 79.51% | 13.05 |
| 390 | 3.57 | 79.33% | 2.65 |
| 400 | 3.57 | 79.19% | 2.65 |
| 410 | 3.57 | 79.47% | 2.65 |
| 420 | 3.57 | 79.54% | 2.65 |
| 430 | 3.58 | 79.66% | 2.66 |
| 440 | 3.58 | 79.65% | 2.66 |
| 450 | 3.58 | 79.65% | 2.66 |
| 460 | 3.58 | 79.65% | 2.66 |
| 470 | 3.58 | 79.65% | 2.66 |
| 480 | 3.58 | 79.65% | 2.66 |
| 490 | 3.58 | 79.65% | 2.66 |
| 500 | 3.58 | 79.65% | 2.66 |
