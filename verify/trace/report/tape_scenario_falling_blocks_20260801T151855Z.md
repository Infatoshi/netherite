# Tape replay: scenario_falling_blocks_20260801T151855Z

310 ticks, seed 0, world_time 6000, start (0.50,4.00,0.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=16 independent=15 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=16 ghost_ticks=310 mismatches=0 verified=True available=True pass=True
- world hash: mode=java compared=310 anchor_skips=0 mismatches=20 deltas=1 verified=True available=True pass=False

**Pixel gate: FAIL** over 16 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 16 | 4607990 | 157817 |
| bossbar | 16 | 116352 | 7272 |

Failed frames (worst first, top 20):

- t=0: 310812 unexplained px, clusters [{'px': 65872, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'hud'}]
- t=40: 308394 unexplained px, clusters [{'px': 65800, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'hud'}]
- t=80: 285014 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=100: 285014 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=120: 285014 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=140: 285014 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=160: 285014 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=180: 285014 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=200: 285014 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=220: 285014 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=240: 285014 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=260: 285014 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=280: 285014 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=300: 285014 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=60: 284776 unexplained px, clusters [{'px': 53796, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=20: 283840 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 87.90 | 86.49% | 104.53 |
| 20 | 84.97 | 80.21% | 103.69 |
| 40 | 96.17 | 86.49% | 111.28 |
| 60 | 86.20 | 80.21% | 106.01 |
| 80 | 87.56 | 80.21% | 108.08 |
| 100 | 87.56 | 80.21% | 108.07 |
| 120 | 87.58 | 80.21% | 108.11 |
| 140 | 87.57 | 80.21% | 108.09 |
| 160 | 87.56 | 80.21% | 108.08 |
| 180 | 87.57 | 80.21% | 108.10 |
| 200 | 87.58 | 80.21% | 108.11 |
| 220 | 87.56 | 80.21% | 108.08 |
| 240 | 87.56 | 80.21% | 108.09 |
| 260 | 87.58 | 80.21% | 108.12 |
| 280 | 87.56 | 80.21% | 108.07 |
| 300 | 87.56 | 80.21% | 108.08 |
