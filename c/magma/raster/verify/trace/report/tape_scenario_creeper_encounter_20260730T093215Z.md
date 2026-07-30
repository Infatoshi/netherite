# Tape replay: scenario_creeper_encounter_20260730T093215Z

403 ticks, seed 0, world_time 6000, start (0.50,4.00,0.50).

**FIRST DIVERGENCE: tick 109, field `x`** oracle=0.48600126150995493 magma=0.5 |d|=0.014; inputs {'f': 0.0, 's': 0.0, 'jump': 0, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 2.2421 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=202 independent=201 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=21 available=True
- world nearby_hash: checked=21 deltas=0 available=True

**Pixel gate: FAIL** over 202 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 176 | 17386495 | 168643 |
| bossbar | 147 | 1338185 | 20686 |
| hud | 166 | 4206919 | 31907 |
| particles | 9 | 74361 | 13739 |
| thinline | 2 | 298 | 196 |
| viewmodel | 153 | 1285948 | 18449 |

Failed frames (worst first, top 20):

- t=116: 242374 unexplained px, clusters [{'px': 61013, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 746, 'cls': 'UNEXPLAINED', 'bbox': [193, 596, 220, 644], 'soak_from': 'viewmodel'}, {'px': 106, 'cls': 'UNEXPLAINED', 'bbox': [193, 646, 200, 669], 'soak_from': 'viewmodel'}, {'px': 166269, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 613], 'soak_from': 'particles'}]
- t=118: 232770 unexplained px, clusters [{'px': 61052, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 823], 'soak_from': 'viewmodel'}, {'px': 87, 'cls': 'UNEXPLAINED', 'bbox': [363, 840, 375, 853], 'soak_from': 'viewmodel'}, {'px': 66, 'cls': 'UNEXPLAINED', 'bbox': [378, 833, 383, 853], 'soak_from': 'viewmodel'}, {'px': 168643, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 684], 'soak_from': 'particles'}]
- t=120: 230578 unexplained px, clusters [{'px': 61779, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 848], 'soak_from': 'viewmodel'}, {'px': 168111, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 744], 'soak_from': 'particles'}, {'px': 116, 'cls': 'UNEXPLAINED', 'bbox': [45, 736, 61, 747], 'soak_from': 'particles'}, {'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [54, 363, 64, 370], 'soak_from': 'particles'}]
- t=114: 220081 unexplained px, clusters [{'px': 63371, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 156221, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 714], 'soak_from': 'particles'}, {'px': 129, 'cls': 'UNEXPLAINED', 'bbox': [71, 397, 85, 412], 'soak_from': 'particles'}, {'px': 126, 'cls': 'UNEXPLAINED', 'bbox': [94, 713, 111, 729], 'soak_from': 'particles'}]
- t=122: 207866 unexplained px, clusters [{'px': 50459, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 845], 'soak_from': 'viewmodel'}, {'px': 167, 'cls': 'UNEXPLAINED', 'bbox': [273, 649, 286, 671], 'soak_from': 'viewmodel'}, {'px': 463, 'cls': 'UNEXPLAINED', 'bbox': [277, 689, 305, 724], 'soak_from': 'viewmodel'}, {'px': 69, 'cls': 'UNEXPLAINED', 'bbox': [292, 656, 301, 670], 'soak_from': 'viewmodel'}]
- t=112: 187661 unexplained px, clusters [{'px': 51084, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 136507, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 691], 'soak_from': 'particles'}, {'px': 70, 'cls': 'UNEXPLAINED', 'bbox': [205, 89, 216, 101], 'soak_from': 'particles'}]
- t=110: 171310 unexplained px, clusters [{'px': 40066, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 60, 'cls': 'UNEXPLAINED', 'bbox': [193, 570, 198, 580], 'soak_from': 'viewmodel'}, {'px': 130503, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 444], 'soak_from': 'particles'}, {'px': 108, 'cls': 'UNEXPLAINED', 'bbox': [129, 0, 149, 10]}]
- t=124: 134928 unexplained px, clusters [{'px': 118313, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 444], 'soak_from': 'particles'}, {'px': 1775, 'cls': 'UNEXPLAINED', 'bbox': [45, 473, 98, 544], 'soak_from': 'particles'}, {'px': 82, 'cls': 'UNEXPLAINED', 'bbox': [45, 580, 53, 602], 'soak_from': 'particles'}, {'px': 72, 'cls': 'UNEXPLAINED', 'bbox': [47, 385, 55, 392], 'soak_from': 'particles'}]
- t=126: 132372 unexplained px, clusters [{'px': 114943, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 444], 'soak_from': 'particles'}, {'px': 79, 'cls': 'UNEXPLAINED', 'bbox': [45, 431, 54, 443], 'soak_from': 'particles'}, {'px': 1081, 'cls': 'UNEXPLAINED', 'bbox': [45, 475, 84, 523], 'soak_from': 'particles'}, {'px': 257, 'cls': 'UNEXPLAINED', 'bbox': [45, 635, 66, 660], 'soak_from': 'particles'}]
- t=128: 130448 unexplained px, clusters [{'px': 114168, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 444], 'soak_from': 'particles'}, {'px': 221, 'cls': 'UNEXPLAINED', 'bbox': [45, 259, 57, 293], 'soak_from': 'particles'}, {'px': 240, 'cls': 'UNEXPLAINED', 'bbox': [45, 351, 59, 366], 'soak_from': 'particles'}, {'px': 563, 'cls': 'UNEXPLAINED', 'bbox': [45, 483, 71, 525], 'soak_from': 'particles'}]
- t=130: 126109 unexplained px, clusters [{'px': 113629, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 444], 'soak_from': 'particles'}, {'px': 1146, 'cls': 'UNEXPLAINED', 'bbox': [45, 316, 88, 385], 'soak_from': 'particles'}, {'px': 481, 'cls': 'UNEXPLAINED', 'bbox': [45, 628, 73, 660], 'soak_from': 'particles'}, {'px': 498, 'cls': 'UNEXPLAINED', 'bbox': [47, 750, 80, 776]}]
- t=132: 123989 unexplained px, clusters [{'px': 112644, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 444], 'soak_from': 'particles'}, {'px': 555, 'cls': 'UNEXPLAINED', 'bbox': [45, 330, 60, 386], 'soak_from': 'particles'}, {'px': 68, 'cls': 'UNEXPLAINED', 'bbox': [45, 654, 48, 670], 'soak_from': 'particles'}, {'px': 316, 'cls': 'UNEXPLAINED', 'bbox': [45, 755, 71, 774]}]
- t=134: 122121 unexplained px, clusters [{'px': 112985, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 444], 'soak_from': 'particles'}, {'px': 275, 'cls': 'UNEXPLAINED', 'bbox': [45, 577, 69, 596], 'soak_from': 'particles'}, {'px': 252, 'cls': 'UNEXPLAINED', 'bbox': [45, 760, 62, 779]}, {'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [56, 371, 63, 378]}]
- t=136: 120574 unexplained px, clusters [{'px': 113319, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 444], 'soak_from': 'particles'}, {'px': 160, 'cls': 'UNEXPLAINED', 'bbox': [45, 579, 57, 599], 'soak_from': 'particles'}, {'px': 175, 'cls': 'UNEXPLAINED', 'bbox': [50, 303, 69, 317], 'soak_from': 'particles'}, {'px': 1059, 'cls': 'UNEXPLAINED', 'bbox': [65, 432, 119, 477], 'soak_from': 'particles'}]
- t=138: 119457 unexplained px, clusters [{'px': 112895, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 444], 'soak_from': 'particles'}, {'px': 1060, 'cls': 'UNEXPLAINED', 'bbox': [52, 433, 105, 478], 'soak_from': 'particles'}, {'px': 169, 'cls': 'UNEXPLAINED', 'bbox': [69, 595, 83, 617], 'soak_from': 'particles'}, {'px': 56, 'cls': 'UNEXPLAINED', 'bbox': [86, 523, 92, 530], 'soak_from': 'particles'}]
- t=140: 117509 unexplained px, clusters [{'px': 112119, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 444], 'soak_from': 'particles'}, {'px': 745, 'cls': 'UNEXPLAINED', 'bbox': [45, 433, 92, 463], 'soak_from': 'particles'}, {'px': 380, 'cls': 'UNEXPLAINED', 'bbox': [57, 265, 75, 284], 'soak_from': 'particles'}, {'px': 112, 'cls': 'UNEXPLAINED', 'bbox': [57, 597, 71, 611], 'soak_from': 'particles'}]
- t=142: 117247 unexplained px, clusters [{'px': 112192, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 444], 'soak_from': 'particles'}, {'px': 260, 'cls': 'UNEXPLAINED', 'bbox': [45, 264, 57, 283], 'soak_from': 'particles'}, {'px': 575, 'cls': 'UNEXPLAINED', 'bbox': [45, 434, 78, 464], 'soak_from': 'particles'}, {'px': 120, 'cls': 'UNEXPLAINED', 'bbox': [45, 598, 59, 613], 'soak_from': 'particles'}]
- t=144: 115434 unexplained px, clusters [{'px': 112170, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 444], 'soak_from': 'particles'}, {'px': 330, 'cls': 'UNEXPLAINED', 'bbox': [45, 434, 65, 464], 'soak_from': 'particles'}, {'px': 66, 'cls': 'UNEXPLAINED', 'bbox': [69, 697, 81, 705]}, {'px': 57, 'cls': 'UNEXPLAINED', 'bbox': [70, 739, 78, 751]}]
- t=146: 114875 unexplained px, clusters [{'px': 112076, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 444], 'soak_from': 'particles'}, {'px': 120, 'cls': 'UNEXPLAINED', 'bbox': [45, 450, 52, 464], 'soak_from': 'particles'}, {'px': 60, 'cls': 'UNEXPLAINED', 'bbox': [61, 740, 69, 752]}, {'px': 63, 'cls': 'UNEXPLAINED', 'bbox': [63, 698, 74, 706]}]
- t=148: 114323 unexplained px, clusters [{'px': 111721, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 444], 'soak_from': 'particles'}, {'px': 265, 'cls': 'UNEXPLAINED', 'bbox': [47, 351, 65, 379], 'soak_from': 'particles'}, {'px': 265, 'cls': 'UNEXPLAINED', 'bbox': [55, 831, 77, 853], 'soak_from': 'particles'}, {'px': 72, 'cls': 'UNEXPLAINED', 'bbox': [56, 698, 68, 707]}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 2.45 | 88.90% | 2.21 |
| 2 | 1.11 | 81.23% | 1.20 |
| 4 | 1.11 | 81.24% | 1.20 |
| 6 | 1.11 | 81.25% | 1.20 |
| 8 | 1.11 | 81.25% | 1.20 |
| 10 | 1.11 | 81.26% | 1.20 |
| 12 | 1.11 | 81.26% | 1.20 |
| 14 | 1.11 | 81.26% | 1.20 |
| 16 | 1.14 | 86.74% | 1.23 |
| 18 | 1.12 | 81.33% | 1.21 |
| 20 | 1.12 | 81.34% | 1.21 |
| 22 | 1.12 | 81.35% | 1.21 |
| 24 | 1.12 | 81.35% | 1.21 |
| 26 | 1.15 | 81.36% | 1.26 |
| 28 | 1.17 | 81.35% | 1.29 |
| 30 | 1.16 | 81.34% | 1.28 |
| 32 | 1.15 | 81.34% | 1.26 |
| 34 | 1.17 | 81.34% | 1.29 |
| 36 | 1.19 | 81.35% | 1.33 |
| 38 | 1.20 | 81.35% | 1.34 |
| 40 | 1.18 | 81.36% | 1.31 |
| 42 | 1.17 | 81.35% | 1.30 |
| 44 | 1.21 | 81.36% | 1.37 |
| 46 | 1.23 | 81.36% | 1.40 |
| 48 | 1.21 | 81.35% | 1.36 |
| 50 | 1.13 | 81.32% | 1.23 |
| 52 | 1.20 | 81.35% | 1.34 |
| 54 | 1.24 | 81.36% | 1.42 |
| 56 | 1.23 | 81.35% | 1.40 |
| 58 | 1.14 | 81.30% | 1.24 |
| 60 | 1.26 | 81.35% | 1.44 |
| 62 | 1.31 | 81.35% | 1.53 |
| 64 | 1.31 | 81.34% | 1.53 |
| 66 | 1.21 | 81.22% | 1.37 |
| 68 | 1.31 | 81.30% | 1.54 |
| 70 | 1.45 | 81.33% | 1.76 |
| 72 | 1.49 | 81.32% | 1.83 |
| 74 | 1.39 | 81.26% | 1.64 |
| 76 | 1.33 | 81.24% | 1.50 |
| 78 | 1.50 | 81.34% | 1.60 |
| 80 | 1.46 | 81.40% | 1.52 |
| 82 | 5.30 | 81.64% | 7.59 |
| 84 | 5.27 | 81.65% | 7.59 |
| 86 | 1.34 | 81.29% | 1.53 |
| 88 | 5.60 | 81.66% | 8.09 |
| 90 | 5.53 | 81.66% | 7.99 |
| 92 | 1.38 | 81.27% | 1.60 |
| 94 | 5.49 | 81.71% | 7.91 |
| 96 | 1.87 | 81.45% | 2.38 |
| 98 | 2.17 | 81.55% | 2.86 |
| 100 | 6.09 | 81.92% | 8.84 |
| 102 | 2.72 | 81.85% | 3.71 |
| 104 | 3.14 | 82.05% | 4.36 |
| 106 | 7.48 | 82.43% | 10.96 |
| 108 | 4.01 | 82.55% | 5.75 |
| 110 | 60.36 | 86.02% | 73.77 |
| 112 | 61.65 | 87.39% | 75.00 |
| 114 | 70.07 | 88.74% | 81.91 |
| 116 | 63.59 | 91.73% | 71.46 |
| 118 | 65.95 | 91.85% | 78.26 |
| 120 | 64.96 | 90.16% | 77.12 |
| 122 | 55.02 | 87.88% | 62.32 |
| 124 | 45.40 | 85.66% | 49.52 |
| 126 | 43.88 | 85.03% | 46.71 |
| 128 | 42.38 | 84.73% | 44.77 |
| 130 | 40.36 | 84.15% | 42.51 |
| 132 | 38.67 | 83.86% | 41.22 |
| 134 | 37.80 | 83.62% | 40.40 |
| 136 | 36.81 | 83.24% | 39.47 |
| 138 | 36.50 | 82.97% | 39.34 |
| 140 | 35.75 | 82.77% | 38.48 |
| 142 | 35.33 | 82.73% | 38.10 |
| 144 | 34.02 | 82.66% | 36.13 |
| 146 | 33.59 | 82.59% | 35.67 |
| 148 | 33.26 | 82.50% | 35.39 |
| 150 | 33.01 | 82.30% | 35.16 |
| 152 | 32.94 | 82.20% | 35.07 |
| 154 | 32.92 | 82.11% | 35.01 |
| 156 | 32.90 | 82.14% | 34.97 |
| 158 | 32.80 | 82.08% | 34.82 |
| 160 | 32.77 | 82.11% | 34.81 |
| 162 | 32.78 | 82.10% | 34.87 |
| 164 | 32.74 | 82.09% | 34.81 |
| 166 | 32.70 | 82.08% | 34.78 |
| 168 | 32.66 | 82.08% | 34.75 |
| 170 | 32.61 | 82.08% | 34.68 |
| 172 | 32.62 | 82.08% | 34.67 |
| 174 | 32.62 | 82.08% | 34.66 |
| 176 | 32.62 | 82.08% | 34.66 |
| 178 | 32.60 | 82.08% | 34.64 |
| 180 | 32.57 | 82.08% | 34.61 |
| 182 | 32.57 | 82.08% | 34.61 |
| 184 | 32.56 | 82.08% | 34.58 |
| 186 | 32.56 | 82.08% | 34.59 |
| 188 | 32.54 | 82.08% | 34.55 |
| 190 | 32.55 | 82.08% | 34.54 |
| 192 | 32.56 | 82.08% | 34.55 |
| 194 | 32.57 | 82.08% | 34.55 |
| 196 | 32.58 | 82.08% | 34.54 |
| 198 | 32.58 | 82.07% | 34.54 |
| 200 | 32.57 | 82.08% | 34.54 |
| 202 | 32.55 | 82.08% | 34.54 |
| 204 | 32.54 | 82.08% | 34.54 |
| 206 | 32.53 | 82.08% | 34.54 |
| 208 | 32.54 | 82.08% | 34.55 |
| 210 | 32.53 | 82.08% | 34.54 |
| 212 | 32.52 | 82.06% | 34.54 |
| 214 | 32.51 | 82.08% | 34.53 |
| 216 | 32.51 | 82.08% | 34.54 |
| 218 | 32.51 | 82.08% | 34.54 |
| 220 | 32.52 | 82.08% | 34.54 |
| 222 | 32.52 | 82.08% | 34.54 |
| 224 | 32.53 | 82.08% | 34.54 |
| 226 | 32.53 | 82.08% | 34.53 |
| 228 | 32.53 | 82.08% | 34.53 |
| 230 | 32.54 | 82.08% | 34.53 |
| 232 | 32.54 | 82.08% | 34.54 |
| 234 | 32.54 | 82.08% | 34.53 |
| 236 | 32.56 | 82.08% | 34.55 |
| 238 | 32.56 | 82.08% | 34.55 |
| 240 | 32.56 | 82.08% | 34.56 |
| 242 | 32.55 | 82.08% | 34.55 |
| 244 | 32.54 | 82.08% | 34.55 |
| 246 | 32.54 | 82.07% | 34.55 |
| 248 | 32.54 | 82.08% | 34.55 |
| 250 | 32.54 | 82.08% | 34.54 |
| 252 | 32.55 | 82.08% | 34.54 |
| 254 | 32.55 | 82.08% | 34.55 |
| 256 | 32.55 | 82.05% | 34.55 |
| 258 | 32.56 | 82.08% | 34.55 |
| 260 | 32.55 | 82.07% | 34.54 |
| 262 | 32.55 | 82.08% | 34.54 |
| 264 | 32.55 | 82.08% | 34.54 |
| 266 | 32.54 | 82.08% | 34.54 |
| 268 | 32.54 | 82.08% | 34.54 |
| 270 | 32.53 | 82.08% | 34.54 |
| 272 | 32.52 | 82.08% | 34.54 |
| 274 | 32.50 | 82.08% | 34.53 |
| 276 | 32.50 | 82.08% | 34.53 |
| 278 | 32.50 | 82.08% | 34.53 |
| 280 | 32.51 | 82.01% | 34.53 |
| 282 | 32.52 | 82.08% | 34.54 |
| 284 | 32.53 | 82.05% | 34.54 |
| 286 | 32.53 | 82.08% | 34.54 |
| 288 | 32.52 | 82.08% | 34.54 |
| 290 | 32.51 | 82.08% | 34.53 |
| 292 | 32.51 | 82.07% | 34.53 |
| 294 | 32.51 | 82.08% | 34.53 |
| 296 | 32.51 | 82.08% | 34.54 |
| 298 | 32.53 | 82.08% | 34.54 |
| 300 | 32.53 | 82.08% | 34.54 |
| 302 | 32.53 | 82.08% | 34.55 |
| 304 | 32.53 | 82.08% | 34.55 |
| 306 | 32.52 | 82.04% | 34.55 |
| 308 | 32.52 | 82.08% | 34.55 |
| 310 | 32.53 | 82.08% | 34.55 |
| 312 | 32.53 | 82.08% | 34.55 |
| 314 | 32.54 | 82.08% | 34.54 |
| 316 | 32.55 | 82.08% | 34.54 |
| 318 | 32.56 | 82.08% | 34.55 |
| 320 | 32.57 | 82.08% | 34.55 |
| 322 | 32.58 | 82.08% | 34.54 |
| 324 | 32.58 | 82.08% | 34.54 |
| 326 | 32.57 | 82.08% | 34.54 |
| 328 | 32.55 | 82.08% | 34.54 |
| 330 | 32.54 | 82.07% | 34.54 |
| 332 | 32.53 | 82.08% | 34.54 |
| 334 | 32.54 | 82.08% | 34.55 |
| 336 | 32.53 | 82.08% | 34.54 |
| 338 | 32.52 | 82.08% | 34.54 |
| 340 | 32.51 | 82.07% | 34.53 |
| 342 | 32.50 | 82.08% | 34.53 |
| 344 | 32.51 | 82.07% | 34.54 |
| 346 | 32.52 | 82.08% | 34.54 |
| 348 | 32.52 | 82.08% | 34.54 |
| 350 | 32.53 | 82.01% | 34.54 |
| 352 | 32.53 | 82.08% | 34.53 |
| 354 | 32.54 | 82.06% | 34.53 |
| 356 | 32.54 | 82.08% | 34.53 |
| 358 | 32.54 | 82.08% | 34.54 |
| 360 | 32.55 | 82.08% | 34.54 |
| 362 | 32.56 | 82.08% | 34.54 |
| 364 | 32.56 | 82.08% | 34.55 |
| 366 | 32.56 | 82.08% | 34.55 |
| 368 | 32.55 | 82.08% | 34.55 |
| 370 | 32.54 | 82.08% | 34.55 |
| 372 | 32.54 | 82.07% | 34.55 |
| 374 | 32.54 | 82.06% | 34.55 |
| 376 | 32.54 | 82.08% | 34.54 |
| 378 | 32.55 | 82.07% | 34.54 |
| 380 | 32.55 | 82.08% | 34.54 |
| 382 | 32.55 | 82.08% | 34.55 |
| 384 | 32.56 | 82.08% | 34.55 |
| 386 | 32.55 | 82.07% | 34.54 |
| 388 | 32.55 | 82.08% | 34.54 |
| 390 | 32.55 | 82.08% | 34.54 |
| 392 | 32.54 | 82.08% | 34.54 |
| 394 | 32.53 | 82.08% | 34.54 |
| 396 | 32.53 | 82.08% | 34.54 |
| 398 | 32.52 | 82.08% | 34.54 |
| 400 | 32.51 | 82.08% | 34.53 |
| 402 | 32.50 | 82.08% | 34.53 |
