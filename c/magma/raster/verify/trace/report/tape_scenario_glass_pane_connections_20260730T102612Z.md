# Tape replay: scenario_glass_pane_connections_20260730T102612Z

308 ticks, seed 0, world_time 6000, start (-7.50,4.00,0.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=16 independent=15 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=16 available=True
- world nearby_hash: checked=16 deltas=5 available=True

**Pixel gate: FAIL** over 16 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 13 | 610952 | 72276 |
| hud | 13 | 169406 | 12494 |
| particles | 13 | 156538 | 23041 |
| viewmodel | 13 | 106490 | 11229 |

Failed frames (worst first, top 20):

- t=0: 212714 unexplained px, clusters [{'px': 64814, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'hud'}]
- t=20: 196011 unexplained px, clusters [{'px': 42053, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 607], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=40: 196011 unexplained px, clusters [{'px': 42053, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 607], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=100: 919 unexplained px, clusters [{'px': 59, 'cls': 'UNEXPLAINED', 'bbox': [323, 74, 328, 102]}, {'px': 74, 'cls': 'UNEXPLAINED', 'bbox': [343, 110, 347, 146]}, {'px': 80, 'cls': 'UNEXPLAINED', 'bbox': [343, 378, 347, 409]}, {'px': 57, 'cls': 'UNEXPLAINED', 'bbox': [350, 87, 354, 106]}]
- t=60: 876 unexplained px, clusters [{'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [323, 405, 331, 427]}, {'px': 76, 'cls': 'UNEXPLAINED', 'bbox': [343, 147, 347, 182]}, {'px': 80, 'cls': 'UNEXPLAINED', 'bbox': [343, 237, 347, 269]}, {'px': 65, 'cls': 'UNEXPLAINED', 'bbox': [348, 126, 354, 147]}]
- t=80: 808 unexplained px, clusters [{'px': 53, 'cls': 'UNEXPLAINED', 'bbox': [310, 39, 314, 62]}, {'px': 51, 'cls': 'UNEXPLAINED', 'bbox': [336, 44, 339, 75]}, {'px': 82, 'cls': 'UNEXPLAINED', 'bbox': [343, 264, 347, 295]}, {'px': 79, 'cls': 'UNEXPLAINED', 'bbox': [343, 353, 347, 384]}]
- t=200: 654 unexplained px, clusters [{'px': 68, 'cls': 'UNEXPLAINED', 'bbox': [262, 410, 275, 427]}, {'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [323, 410, 331, 432]}, {'px': 71, 'cls': 'UNEXPLAINED', 'bbox': [343, 63, 347, 100]}, {'px': 80, 'cls': 'UNEXPLAINED', 'bbox': [343, 333, 347, 365]}]
- t=160: 607 unexplained px, clusters [{'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [323, 339, 330, 371]}, {'px': 73, 'cls': 'UNEXPLAINED', 'bbox': [343, 100, 347, 136]}, {'px': 57, 'cls': 'UNEXPLAINED', 'bbox': [350, 76, 354, 96]}, {'px': 62, 'cls': 'UNEXPLAINED', 'bbox': [352, 272, 354, 296]}]
- t=120: 552 unexplained px, clusters [{'px': 79, 'cls': 'UNEXPLAINED', 'bbox': [343, 227, 347, 259]}, {'px': 63, 'cls': 'UNEXPLAINED', 'bbox': [351, 209, 354, 234]}, {'px': 65, 'cls': 'UNEXPLAINED', 'bbox': [352, 26, 354, 54]}, {'px': 57, 'cls': 'UNEXPLAINED', 'bbox': [352, 311, 354, 333]}]
- t=140: 485 unexplained px, clusters [{'px': 81, 'cls': 'UNEXPLAINED', 'bbox': [343, 343, 347, 375]}, {'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [344, 390, 347, 416]}, {'px': 65, 'cls': 'UNEXPLAINED', 'bbox': [352, 149, 354, 174]}, {'px': 60, 'cls': 'UNEXPLAINED', 'bbox': [362, 155, 364, 177]}]
- t=180: 402 unexplained px, clusters [{'px': 79, 'cls': 'UNEXPLAINED', 'bbox': [343, 216, 347, 249]}, {'px': 77, 'cls': 'UNEXPLAINED', 'bbox': [348, 197, 354, 223]}, {'px': 53, 'cls': 'UNEXPLAINED', 'bbox': [362, 304, 364, 322]}, {'px': 72, 'cls': 'UNEXPLAINED', 'bbox': [362, 340, 364, 364]}]
- t=260: 0 unexplained px, clusters []

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 53.65 | 82.61% | 59.74 |
| 20 | 52.37 | 80.33% | 58.77 |
| 40 | 52.37 | 80.33% | 58.77 |
| 60 | 8.57 | 79.21% | 8.79 |
| 80 | 8.80 | 79.23% | 9.04 |
| 100 | 8.88 | 79.27% | 9.37 |
| 120 | 9.12 | 79.31% | 9.66 |
| 140 | 9.44 | 79.39% | 10.11 |
| 160 | 9.46 | 79.31% | 10.61 |
| 180 | 9.53 | 79.34% | 10.83 |
| 200 | 9.55 | 79.31% | 10.84 |
| 220 | 4.86 | 78.01% | 5.75 |
| 240 | 4.86 | 78.01% | 5.75 |
| 260 | 8.45 | 78.04% | 9.62 |
| 280 | 9.34 | 78.92% | 5.87 |
| 300 | 7.87 | 78.54% | 5.57 |
