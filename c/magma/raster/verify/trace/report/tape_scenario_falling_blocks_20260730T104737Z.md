# Tape replay: scenario_falling_blocks_20260730T104737Z

309 ticks, seed 0, world_time 6000, start (0.50,4.00,0.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=16 independent=15 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=16 available=True
- world nearby_hash: checked=16 deltas=1 available=True

**Pixel gate: FAIL** over 16 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 16 | 4613770 | 157817 |
| bossbar | 16 | 116352 | 7272 |

Failed frames (worst first, top 20):

- t=0: 310812 unexplained px, clusters [{'px': 65872, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'hud'}]
- t=40: 308504 unexplained px, clusters [{'px': 65659, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'hud'}]
- t=80: 285541 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=100: 285541 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=120: 285541 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=140: 285541 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=160: 285541 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=180: 285541 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=200: 285541 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=220: 285541 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=240: 285541 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=260: 285541 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=280: 285541 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=300: 285541 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=20: 284443 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=60: 283519 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 88.66 | 86.49% | 104.88 |
| 20 | 85.84 | 80.79% | 104.04 |
| 40 | 96.78 | 86.49% | 111.59 |
| 60 | 87.26 | 80.79% | 106.91 |
| 80 | 88.43 | 80.79% | 108.42 |
| 100 | 88.42 | 80.79% | 108.41 |
| 120 | 88.45 | 80.79% | 108.45 |
| 140 | 88.44 | 80.79% | 108.43 |
| 160 | 88.43 | 80.79% | 108.42 |
| 180 | 88.44 | 80.79% | 108.44 |
| 200 | 88.45 | 80.79% | 108.45 |
| 220 | 88.43 | 80.79% | 108.42 |
| 240 | 88.43 | 80.79% | 108.43 |
| 260 | 88.45 | 80.79% | 108.46 |
| 280 | 88.42 | 80.79% | 108.42 |
| 300 | 88.43 | 80.79% | 108.42 |
