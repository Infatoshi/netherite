# Rasterizer verification (KernelBench-style, golden = real GL)

> Verification entry point: `magma/VERIFY.md`. This dir is the GL-golden
> rasterizer gate (`make raster-verify`), stable and rarely re-run.

The triangle-to-pixel step is a kernel like any other: arbitrary inputs, a golden
output, a diff. What differs from the arithmetic kernels (render-opt 12-15, 21-24,
28, 31-35, which are bit-exact vs decompiled Java) is the golden and the metric.

- Golden = OpenGL. `gl_golden.c` rasterizes the scene with OSMesa, i.e. Mesa's
  llvmpipe/swrast software GL - the SAME GL path Minecraft uses on anvil under
  `LIBGL_ALWAYS_SOFTWARE=1`. Fully offscreen, `glReadPixels` readback.
- Candidate = `c_candidate.c` feeds the identical scene through `cr_raster_cpu`,
  applying the same GL viewport mapping so ONLY the rasterizer differs.
- Metric = tolerance pixel diff (`render-opt/wholeframe/diff_frame.py`), NOT bitwise.
  GPU/driver rasterization is hardware-defined: fixed-point subpixel coordinates,
  its own top-left fill rule, attribute-interpolation precision, texture filtering,
  blend rounding. You cannot bit-replicate it the way you replicate a Java formula,
  so the honest standard is "at the fill-rule/subpixel noise floor".

## Scope isolation
Inputs are in CLIP space (`scene.h`), so the transform stage is out of scope.
Per-vertex attribute is a scalar brightness (both paths interpolate it
perspective-correctly) plus a uv into a shared nearest-sampled atlas. Shading is
`texel * light` (GL_MODULATE with `glColor(light,light,light)`; our `cr_shade` with
tint=white, ao=1). No blend, no MSAA, no dither, no mipmap; depth func LESS.

## Run
```
make raster-verify
```
Builds golden + candidate, renders `/tmp/raster_golden.ppm` and
`/tmp/raster_candidate.ppm`, prints the diff, writes a heatmap to `/tmp/raster_diff`.

## Result (256x256 fixed scene, 4 tris incl. a w=3 perspective tri + a z-buffer overlap)
```
whole: max/ch=125  mean=0.003  rmse=0.483  differ=32/65536 (0.05%)   (thr=0)
                                            differ= 7/65536 (0.01%)   (thr>2 LSB)
```
Interior is essentially identical (mean 0.003/channel). The ~7 hard-diff pixels are
silhouette pixels where GL's subpixel fill rule flips one boundary pixel to a
different texel. That is the rasterization noise floor vs real GL.

## Next
Grow the scene toward MC-real inputs: feed the exact `DefaultVertexFormats.BLOCK`
vertex buffers that render-opt kernels 23/24/28 already produce bit-exactly, with
MC's GL state (blend for TRANSLUCENT, alpha test for CUTOUT, the real atlas), and
diff against MC's own captured frame (render-opt/wholeframe). Then the whole path
seed -> blocks -> verified buffers -> our raster is golden-checked end to end.
