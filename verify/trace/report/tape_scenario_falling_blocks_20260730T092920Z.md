# Tape replay: scenario_falling_blocks_20260730T092920Z

303 ticks, seed 0, world_time 6000, start (0.50,4.00,0.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=16 independent=15 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=16 available=True
- world nearby_hash: checked=16 deltas=0 available=True

**Pixel gate: FAIL** over 16 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 16 | 3815364 | 100925 |
| bossbar | 16 | 49536 | 3096 |

Failed frames (worst first, top 20):

- t=20: 254132 unexplained px, clusters [{'px': 65872, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'hud'}]
- t=0: 253074 unexplained px, clusters [{'px': 64814, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'hud'}]
- t=40: 236297 unexplained px, clusters [{'px': 41979, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 602], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=60: 236297 unexplained px, clusters [{'px': 41979, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 602], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=80: 236297 unexplained px, clusters [{'px': 41979, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 602], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=100: 236297 unexplained px, clusters [{'px': 41979, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 602], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=120: 236297 unexplained px, clusters [{'px': 41979, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 602], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=140: 236297 unexplained px, clusters [{'px': 41979, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 602], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=160: 236297 unexplained px, clusters [{'px': 41979, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 602], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=180: 236297 unexplained px, clusters [{'px': 41979, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 602], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=200: 236297 unexplained px, clusters [{'px': 41979, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 602], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=220: 236297 unexplained px, clusters [{'px': 41979, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 602], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=240: 236297 unexplained px, clusters [{'px': 41979, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 602], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=260: 236297 unexplained px, clusters [{'px': 41979, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 602], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=280: 236297 unexplained px, clusters [{'px': 41979, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 602], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=300: 236297 unexplained px, clusters [{'px': 41979, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 602], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 73.34 | 85.27% | 84.53 |
| 20 | 78.76 | 85.45% | 88.72 |
| 40 | 73.18 | 83.24% | 84.88 |
| 60 | 73.18 | 83.23% | 84.88 |
| 80 | 73.18 | 83.23% | 84.88 |
| 100 | 73.18 | 83.23% | 84.88 |
| 120 | 73.18 | 83.23% | 84.88 |
| 140 | 73.18 | 83.23% | 84.88 |
| 160 | 73.18 | 83.23% | 84.88 |
| 180 | 73.18 | 83.23% | 84.88 |
| 200 | 73.18 | 83.23% | 84.88 |
| 220 | 73.18 | 83.23% | 84.88 |
| 240 | 73.18 | 83.23% | 84.88 |
| 260 | 73.18 | 83.23% | 84.88 |
| 280 | 73.18 | 83.23% | 84.88 |
| 300 | 73.18 | 83.23% | 84.88 |
