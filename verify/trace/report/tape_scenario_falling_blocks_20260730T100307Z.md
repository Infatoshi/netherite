# Tape replay: scenario_falling_blocks_20260730T100307Z

309 ticks, seed 0, world_time 6000, start (0.50,4.00,0.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=16 independent=15 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=16 available=True
- world nearby_hash: checked=16 deltas=1 available=True

**Pixel gate: FAIL** over 16 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 16 | 4418330 | 146323 |

Failed frames (worst first, top 20):

- t=0: 299318 unexplained px, clusters [{'px': 65872, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'hud'}]
- t=20: 289766 unexplained px, clusters [{'px': 37648, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 536], 'soak_from': 'hud'}, {'px': 25385, 'cls': 'UNEXPLAINED', 'bbox': [384, 539, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=60: 273520 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=80: 273520 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=100: 273520 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=120: 273520 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=140: 273520 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=160: 273520 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=180: 273520 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=200: 273520 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=220: 273520 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=240: 273520 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=260: 273520 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=280: 273520 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=300: 273520 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=40: 273486 unexplained px, clusters [{'px': 53868, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 748], 'soak_from': 'hud'}, {'px': 1899, 'cls': 'UNEXPLAINED', 'bbox': [439, 793, 479, 853], 'soak_from': 'hud'}, {'px': 812, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 89.80 | 83.27% | 110.31 |
| 20 | 91.59 | 80.99% | 106.67 |
| 40 | 86.75 | 76.99% | 109.26 |
| 60 | 87.16 | 76.99% | 109.96 |
| 80 | 87.17 | 76.99% | 109.97 |
| 100 | 87.17 | 76.99% | 109.98 |
| 120 | 87.16 | 76.99% | 109.96 |
| 140 | 87.17 | 76.99% | 109.97 |
| 160 | 87.17 | 76.99% | 109.98 |
| 180 | 87.16 | 76.99% | 109.96 |
| 200 | 87.17 | 76.99% | 109.97 |
| 220 | 87.17 | 76.99% | 109.98 |
| 240 | 87.16 | 76.99% | 109.96 |
| 260 | 87.16 | 76.99% | 109.96 |
| 280 | 87.17 | 76.99% | 109.98 |
| 300 | 87.17 | 76.99% | 109.96 |
