# magma - full C/CUDA software rasterizer for the Minecraft C engine

The standalone game's supported product surface, survival completion route, launch
settings, test-hook boundary, and deliberate cuts are defined in `PRODUCT.md`. This
SPEC remains the renderer architecture and verification contract.

Location: `magma` in this monorepo (with `../blaze`, `../java/render-opt`).
Build/run on anvil (Linux, headless). Display over Xvfb :1 (VNC) or dump PPM frames.
Product contract: `PRODUCT.md`. Verification: `VERIFY.md`. History: `../../docs/DEVLOG.md`.

## Goal
Render the Minecraft world with a TRUE software/CUDA rasterizer: the triangle -> pixel
step is our own C and CUDA code, NOT OpenGL. OpenGL/GL is banned in the render path.
SDL2 (or raw X11) is allowed ONLY to open a window and blit a finished RGBA buffer, and
to read keyboard/mouse. No GL, no GPU rasterization API - we write the z-buffer loop.

Two backends that must agree: a C reference (`cpu/`) and a CUDA port (`cuda/`).
Philosophy inherited from blaze/render-opt: CPU == CUDA, verified. For the color
buffer, match bit-exact where feasible (compile with `-ffp-contract=off`, identical op
order in C and CUDA); otherwise tolerance <= 1 LSB per channel on <0.1% of pixels.

## Data flow
```
blaze worldgen -> chunk voxels -> render-opt meshing kernels -> CrVertex[] (world space)
   -> transform.c (MVP + near-clip + viewport)   -> CrScreenTri[]
   -> raster (cpu/cuda: edge fns, z-buffer, persp-correct interp, calls cr_shade)
   -> CrFramebuffer -> present.c (blit + input) -> camera update -> loop
```
All types and the exact module prototypes are in `core/types.h`. That header is the
CONTRACT. Do not change a signature without updating this file.

## Module ownership (one owner per file; do not edit another module's files)
- `core/types.h`        - CONTRACT (owned by orchestrator; read-only for agents).
- `core/math.c`         - mat4/vec math + camera matrices (owner: TRANSFORM agent).
- `transform.c`         - cr_transform: MVP, near-plane clip, viewport (TRANSFORM agent).
- `cpu/raster_cpu.c`    - cr_raster_cpu, cr_fb_* helpers (RASTER agent).
- `cuda/raster_cuda.cu` - cr_raster_cuda (RASTER agent).
- `core/shade.c`        - cr_shade, cr_atlas_sample (SHADE agent).
- `present/present.c`   - SDL2 window, blit, input (PRESENT agent).
- `demo/demo_cube.c`    - main(): spinning textured cube, exercises whole spine (PRESENT agent).
- `tests/`              - per-module self-tests (each agent adds its own test file).

## Rasterizer requirements (raster_cpu.c / raster_cuda.cu)
- Input CrScreenTri already in pixel space with z in [0,1] and invw = 1/clip.w.
- Top-left fill rule via integer/edge-function coverage; no double-shaded shared edges.
- Depth test: keep fragment if z < depth[i]; then write depth and color.
- Perspective-correct: interpolate invw and attr_w (attr/w) across the tri, recover
  attr = attr_w / invw at each covered pixel.
- Backface cull CCW-front (skip triangles with negative signed area) - make the winding
  a named constant so it can be flipped once during integration.
- Build a CrFragment (uv, light, ao, tint, eye_dist) and call `cr_shade`.
- `cr_shade` and `cr_atlas_sample` are CR_HD in `core/shade.c`, shared by both backends;
  the .cu #includes `core/shade.c` so device code uses the identical function.
- CUDA: one thread per pixel over each tri's bounding box, or tiled; free choice, but
  output must equal the CPU path. Target `-arch=sm_120` (RTX PRO 6000) and sm_86.

## Transform requirements (transform.c, core/math.c)
- MC camera: yaw about Y, pitch about X; +Z... use standard right-handed view; document
  the convention in a comment. Vertical FOV from cam->fov_deg, aspect = fb_w/fb_h.
- Near-plane clip in clip space (w+z >= 0 style) BEFORE perspective divide; emit 0/1/2 tris.
- Viewport: ndc [-1,1] -> pixel [0,W]/[0,H], y flipped (y=0 top). z: ndc[-1,1]->[0,1].
- Fill CrScreenVert.invw, uv_w, light_w, ao_w (attr * invw) for the raster stage.

## Shade requirements (core/shade.c)
- `cr_atlas_sample`: nearest-neighbour fetch, clamp, from CrTexture level 0.
- `cr_shade`: texel = sample(uv); if alpha_test and texel.a<128 -> return fully transparent
  (raster must skip writing). color = texel * tint * light * ao (per channel, /255 math),
  then fog lerp toward fog_color by clamp((eye_dist-fog_start)/(fog_end-fog_start)) if
  enable_fog. Keep integer/float math identical for CPU and CUDA (CR_HD, no libm beyond
  fminf/fmaxf/floorf). Return CrRgba.

## Present requirements (present/present.c, demo/demo_cube.c)
- SDL2 window (works under Xvfb :1). `cr_window_present` uploads fb->color as an
  SDL_Texture / streaming surface and blits (this is a memcpy+present, NOT rendering).
- `cr_window_poll` fills CrInput: WASD, space/shift/ctrl, relative mouse, quit (window
  close or ESC). Support headless: if SDL_VIDEODRIVER unset and no display, allow a
  "dummy" mode that no-ops present so demo can run in CI.
- `demo/demo_cube.c`: create fb, a small procedural 16x16 CrTexture, a CrCamera; each
  frame clear fb, build 12 tris of a unit cube (as CrVertex), rotate it, cr_transform ->
  cr_raster_cpu -> cr_window_present; ESC quits. Add `--frames N --ppm out.ppm` to render
  N frames headless and dump the last as PPM for verification. This is the integration
  smoke test of the whole spine.

## Build
`make` builds: `magma_demo` (cpu spine), `raster_cuda.o`, and per-module tests.
Compile flags: `-O2 -ffp-contract=off -Wall`. CUDA: `nvcc -O2 --fmad=false -arch=sm_120`.
Each agent must ensure `make <their-target>` compiles and their test passes before done.

## Verification
Two independent checks, matching this repo's philosophy:
- Self-consistency: `tests/test_raster_parity` renders a fixed scene with cr_raster_cpu
  and cr_raster_cuda and asserts the framebuffers match (currently bit-exact, 0 diff).
  This proves the CPU and CUDA backends agree; it does NOT prove correctness.
- Correctness (KernelBench-style, golden = real GL): `make raster-verify` rasterizes a
  fixed clip-space scene with real Mesa software GL (OSMesa golden, the same llvmpipe
  path MC uses on anvil) and with cr_raster_cpu (candidate) on identical inputs, then
  tolerance-diffs (render-opt/wholeframe/diff_frame.py). See verify/README.md.
  Metric is pixel diff, NOT bitwise: GL triangle->pixel is hardware/driver-defined
  (subpixel fill rule, interpolation precision, filtering, blend rounding), so the
  standard is "at the fill-rule/subpixel noise floor". Current result: mean 0.003/ch,
  0.05% pixels differ, 7 hard-diff silhouette pixels on a 256x256 scene.
- End goal: feed the exact DefaultVertexFormats.BLOCK buffers that render-opt kernels
  23/24/28 already produce bit-exactly (with MC GL state + real atlas) and diff against
  MC's own captured frame - then seed -> blocks -> verified buffers -> our raster is
  golden-checked end to end.

## Non-goals for wave 1
World/mesh feed from blaze + render-opt is WAVE 2. Wave 1 proves the raster spine with
a synthetic textured cube. Do not depend on blaze/render-opt in wave-1 files.
