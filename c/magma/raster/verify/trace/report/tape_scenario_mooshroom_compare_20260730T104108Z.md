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
| UNEXPLAINED | 36 | 1055354 | 75055 |
| particles | 32 | 5961 | 283 |
| thinline | 1 | 212 | 212 |
| viewmodel | 25 | 17127 | 912 |

Failed frames (worst first, top 20):

- t=0: 217950 unexplained px, clusters [{'px': 64814, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'hud'}]
- t=10: 201172 unexplained px, clusters [{'px': 41979, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 602], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=20: 201172 unexplained px, clusters [{'px': 41979, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 602], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=30: 201172 unexplained px, clusters [{'px': 41979, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 602], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=40: 201172 unexplained px, clusters [{'px': 41979, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 602], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 58.40 | 94.67% | 65.73 |
| 10 | 58.11 | 80.65% | 65.90 |
| 20 | 58.59 | 80.47% | 66.43 |
| 30 | 58.90 | 80.42% | 66.79 |
| 40 | 59.09 | 89.93% | 67.02 |
| 50 | 1.38 | 79.47% | 1.50 |
| 60 | 1.29 | 79.32% | 1.50 |
| 70 | 1.27 | 79.31% | 1.52 |
| 80 | 1.22 | 79.34% | 1.55 |
| 90 | 1.19 | 79.28% | 1.50 |
| 100 | 1.15 | 79.32% | 1.43 |
| 110 | 1.12 | 79.26% | 1.38 |
| 120 | 1.10 | 79.29% | 1.36 |
| 130 | 1.09 | 79.24% | 1.33 |
| 140 | 1.09 | 79.23% | 1.34 |
| 150 | 1.11 | 79.31% | 1.37 |
| 160 | 1.14 | 79.21% | 1.42 |
| 170 | 1.19 | 79.27% | 1.50 |
| 180 | 1.21 | 79.35% | 1.54 |
| 190 | 1.17 | 79.27% | 1.47 |
| 200 | 1.24 | 79.33% | 1.59 |
| 210 | 1.22 | 79.32% | 1.55 |
| 220 | 1.19 | 79.31% | 1.51 |
| 230 | 1.22 | 79.45% | 1.55 |
| 240 | 1.19 | 79.30% | 1.50 |
| 250 | 1.21 | 79.39% | 1.53 |
| 260 | 1.23 | 79.45% | 1.57 |
| 270 | 1.29 | 79.32% | 1.67 |
| 280 | 1.32 | 79.48% | 1.73 |
| 290 | 1.33 | 79.44% | 1.74 |
| 300 | 1.34 | 79.47% | 1.67 |
| 310 | 1.38 | 79.52% | 1.28 |
| 320 | 1.33 | 79.42% | 0.97 |
| 330 | 1.16 | 79.39% | 0.78 |
| 340 | 0.98 | 79.24% | 0.77 |
| 350 | 0.80 | 79.17% | 0.76 |
| 360 | 0.76 | 79.23% | 0.77 |
