# slime_translucent oracle draw capture

Live Minecraft 1.11.2 evidence for the slime_bounce shell-inset contradiction.
Not a C renderer fix. Re-derive:

```bash
bash verify/mc_capture/capture_slime_translucent.sh
```

Requires the Java oracle on anvil (`/tmp/qrl_25575.lock`, Xvfb `:1`). Writes:

| file | what |
|------|------|
| `camera.json` | eye pose + GL modelview/projection after `setupCameraTransform(1,0)` |
| `model_census.json` | every slime in the pad: `generalQuads` vs per-face quads vs `shouldSideBeRendered` |
| `chunks.json` | rebuilt `RenderChunk` origins + per-layer vertex counts |
| `quads.jsonl` | TRANSLUCENT layer after `sortVertexData` (draw order = line order) |
| `coverage.json` | post-transform fragment visit: pixels covered by 1 vs 2+ quads |
| `slime_translucent_{a,b}.png` | same-pose `frame_pair` context |

`n_general_quads` / `n_face_quads` answer whether vanilla emits the 12
null-cullface quads. `coverage.json n_multi` answers whether rim pixels
receive two translucent fragments in that draw order.

Recorded 2026-08-21 on anvil, git `856c3ee`, pose feet (0.5, 4.0, 0.5)
yaw=0 pitch=0 on the 21x21 slime pad (`/fill -10 3 -10 10 3 10 slime`):

| field | value |
|-------|-------|
| n_slime | 441 |
| n_general_quads | 5292 (= 441 * 12) |
| n_face_quads | 0 |
| should_side | UP only (Breakable culls shared sides; never consulted for generalQuads) |
| n_translucent_quads | 5292 |
| coverage n_single | 0 |
| coverage n_multi | 331012 |
| frame_pair A/B | identical (maxch=0), `fog_restored=1`, pass_a==pass_b lightmap/fog |

VertexBuffer stores chunk-relative positions (`setTranslation(-origin)`).
The raster adds `ox,oy,oz` before MVP. Line order is post-`sortVertexData`
inside `RenderChunk.postRenderBlocks`.
