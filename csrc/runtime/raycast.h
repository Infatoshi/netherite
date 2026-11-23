/* The textured ray cast (lane/raycast): a camera the CPU draws, for a policy
 * trained at small observation sizes where the device renderer's per-section
 * work costs more than a ray a pixel (DEVLOG 2026-10-15, CPU or GPU at the
 * small sizes). client.camera raycast; the frame is RGB uint8 [h][w][3] in
 * host memory, row 0 at the top, written by the env's worker after its step.
 * No renderer runs (no device frames, no frame record, no meshing).
 *
 * The camera is the semantic camera's (semcam.h): a pinhole at the client
 * player's eye, vertical field of view 70 degrees, the frame's aspect, the
 * look from MathHelper's sin table; one ray a pixel centre, walking the
 * client world's cells (Amanatides and Woo) out to the render distance's far
 * plane (render_distance * 16 blocks). What a ray sees:
 *   - blocks by their texture: the face icon the renderer's table gives
 *     (getIcon(side, meta), the play scene's table.bin and terrain atlas:
 *     out/java/render/forest-fast), the texel nearest the hit point, times
 *     the biome's grass, foliage or water colour (the biome's own, at its
 *     base temperature: no 3x3 average, no height term); graphics fast draws
 *     leaves with their opaque icon, fancy with the cutout one;
 *   - shapes: a block whose bounds are the whole cell is a cube; crossed
 *     plants (render types 1, 6, 19 and 40) are their two diagonal planes,
 *     both sides; every other block is its bounds box (the renderer's table:
 *     setBlockBoundsBasedOnState's) with its side icons. Every texture is
 *     alpha tested (alpha above 0.1, vanilla's alpha function): a ray goes on
 *     through a transparent texel;
 *   - light: the sky and block light of the cell the ray left to enter the
 *     face (a hit inside the cell, a crossed plane or a box face off the
 *     cell's boundary, takes the cell's own), the block light at least the
 *     block's own emission, through the frame's lightmap
 *     (EntityRenderer.updateLightmap for the client world's time, weather,
 *     dimension and client.gamma), times vanilla's face shading (top 1.0,
 *     bottom 0.5, north and south 0.8, east and west 0.6); no smooth light;
 *   - water translucent: where the ray enters water its surface texel is
 *     blended at the texel's alpha and the ray goes on through the water;
 *     lava opaque (its emission makes it full bright); the eye in water (or
 *     lava) passes through that liquid and takes vanilla's exponential fog;
 *   - entities: the client world's living copies and fireballs, then the
 *     ender dragon (the client's copy, pickobj.c's; lane/parity), and the
 *     player's dimension's items, XP orbs and arrows (the server's: the
 *     engine keeps no client copy of those), each its bounding box in one
 *     flat colour a kind (the mean of its texture's opaque texels in
 *     out/assets), face shaded and lit by the light at 0.66 of its height;
 *     a ray tests only the boxes whose screen rectangle holds its pixel;
 *   - sky and fog: a ray that meets nothing takes the sky plane's colour
 *     (World.getSkyColor, 16 blocks up, fogged from 0 to 0.8 of the far
 *     plane as renderSky's setupFog(-1)), the fog colour below the horizon
 *     and in the Nether, the End sky's mean colour in the End; every hit is
 *     fogged by its eye-space depth with setupFog(0)'s linear fog
 *     (updateFogColor's colour).
 * Not drawn: the HUD, the hand, particles, clouds, the sun, moon and stars,
 * sunrise colours, the selection box, block cracks, tile entity models.
 *
 * Read only: the cast changes nothing in the env (a row never changes).
 *
 * The two instances (lane/castf32, Elliot 2026-10-31: the tests in double, the production runs in float): one
 * source (raycast.c, compiled as itself and through raycast_f32.c; cuda/raycast/cast.cu a template on the scalar),
 * two scalars, chosen by the spec's fp (client.cast_precision):
 *   FP64 (fp 64, the default): the reference, every gate's and pinned digest's: every ray's arithmetic in double
 *     from the eye's world position, as before the lane.
 *   FP32 (fp 32): production. Every operation from the eye's offset on is an IEEE float operation, rounded to
 *     nearest, in the order below (C with -ffp-contract=off, CUDA with --fmad=false: no fused multiply-add; only
 *     + - * /, sqrtf, compares and float <-> int conversions; Java strictfp float gives the same bits). Host and
 *     device give the same bytes; FP64 and FP32 frames differ where a float decision does (a ray grazing an edge).
 * FP32, operation by operation (fl(): one float rounding; integers exact):
 *   the eye: w = the client player's position, y + (double)(yOffset - 1.62f) (double, as vanilla); per axis the
 *     cell e = floor(w) and the offset o = fl(w - e) (w - e exact in double); o == 1 makes e + 1 and o 0. Every
 *     coordinate below is in the frame: a world coordinate less e, so a cell i is the float (i - e), exact.
 *   the camera: f = (f2 f3, f4, f1 f3) and r = (f1, 0, -f2) from MathHelper's sin table as Entity.getLook (floats
 *     already); u = r x f: ux = ry fz - rz fy, uy = rz fx - rx fz, uz = rx fy - ry fx (each product rounded, then
 *     the difference); aspect = (float)w / (float)h; T = 0.700207531f (tan 35 degrees); sxs = T * aspect.
 *   pixel (i, j): sx = (2 (i + 0.5f) / W - 1) sxs, sy = (1 - 2 (j + 0.5f) / H) T, each operation in that order;
 *     d = (f + sx r) + sy u per component; inv = 1 / sqrtf((dx dx + dy dy) + dz dz); the ray d = d inv; fwd =
 *     (d0 fx + d1 fy) + d2 fz (eye-space depth per unit t); id = 1 / d (0 where d is +-0).
 *   the walk (Amanatides and Woo): an axis' step s = sign(d); its next boundary's t is ((i - e + (s > 0)) - o) id
 *     from the cell i alone, never accumulated; the step takes the least t, ties to x, then y, then z. A jump over
 *     an empty 16-cell box (the fast path) lands where the steps would: each other axis' cell from p = o + tE d,
 *     floor(p) + e, trusted 1/1024 clear of a boundary, else settled by the boundaries' own t (the steps' compares).
 *   a hit at t: the point h = (o + d t) - (i - e) per axis; a texel (int)(u 16) clamped to 0..15, u = h or 1 - h
 *     by face; a box face by the slab test (b - (o - (i - e))) id with b the bounds rounded to float; a liquid's
 *     surface h = 1 - (level + 1) / 9.0f, its t ((iy - ey) + h - oy) idy; a crossed plane's t -((lx0 - lz0) / (dx -
 *     dz)) and (1 - lx0 - lz0) / (dx + dz), lx0 = ox - (ix - ex), its u (qx - 0.05f) / 0.9f.
 *   the fog: depth = t fwd; linear (fe - depth) fspan clamped; exponential exp_neg32(-fd depth): 0 at x <= -40,
 *     else n = (int)(x 1.44269502f - 0.5f), r = (x - n 0.693145752f) - n 1.42860677e-6f, p = 1 + r (1 + r (0.5 +
 *     r (0.166666672f + r (0.0416666679f + r (0.00833333377f + r 0.00138888892f))))), p 2^n. The sky plane's z
 *     = (16 / dy) fwd. Light, sky and fog colours, the lightmap and the bytes are the float arithmetic both
 *     instances share (lit_apply, pixel_out).
 *   entities: each box less e in double, rounded to float once; its slab test as a block box's, from o; the
 *     screen rectangle a box can cover from its corners less o projected in float, a pixel more each way.
 *   the semantic camera's FP32 cast (semcam.h) has its own definition there: the same eye and camera.
 *
 * The instances (lane/castexact, Elliot 2026-10-31: "is there a faster way to do this with fewer bits"): two more,
 * each byte-equal host and device, each with its own pinned digests (gates.tsv *-fx, *-f16); raycast_fx.c and
 * raycast_f16.c are raycast.c again, as raycast_f32.c is. The semantic camera has FP64 and FP32 alone (another fp
 * casts FP64 there).
 *   fixed (client.cast_precision fixed, fp RAYCAST_FP_FIXED): FP32 above with two changes. The eye's offset o is
 *     rounded to F = RC_FIXF = 24 fractional bits (nearest, ties even: O = o 2^F a whole number) before anything
 *     reads it, and each ray's unit direction d to G = RC_FIXG = 24 (D = d 2^G), so every float the walk computes
 *     comes from these exact rationals (1 / d, t, the hit point as FP32's). Then every choice of the walk's order (the
 *     axis a step takes, ties x, y, z; whether a boundary comes before a jump's exit event; a jump's exit axis, ties to
 *     the first) is the order of the exact t = (b - o) / d of the two boundaries, b their whole-number coordinates in
 *     the frame: t_a - t_b has the sign of (P_a D_b - P_b D_a) sign(D_a D_b), P = b 2^F - O, a product of at most
 *     9 + F + G = 57 bits (|b| < 256), so 64-bit integers decide it with no rounding and nothing accumulated. The
 *     floats' own order decides wherever two t are more than 2^-20 apart relative (each is at most three roundings,
 *     2^-22, off its exact value), the integers where closer (rc_fx_cmp). Distances, texels, boxes, liquids and fog are
 *     FP32's expressions on o and d. Bits (lane/castexact's sweep): the exact order of the eye and direction rounded
 *     to F and G bits; at fewer bits the observation moves from FP64's by the rounding of the ray itself (DEVLOG).
 *     Which pairs the integers decide changes no byte so long as it holds every pair within 2^-20 (they are exact): so
 *     the device's air loop takes FP32's three compares and its select-based step as they are, and only where one of
 *     the three pairs is near the step's decisions by the integers, out of line (cast.cu RC_FXSTEP: the kernel 16 to
 *     27% faster than a branch a comparison); a test by units in the last place is a knob (RC_FXNEAR 1), the same
 *     bytes and no faster (lane/cxfix). Measured: a build where the integers
 *     decide every comparison (RC_FX_ALWAYS, 8.53 G of them) gives the default's bytes in every frame of
 *     raycast-gate's six recordings at 16x16, 32x32 and 64x64 (47,490 rows; DEVLOG lane/cxfix). raycast_rays (above) is how cast-cmp counts the
 *     rays that differ. A 16-bit lane form (16-bit P and D, VPMADDWD's 32-bit P_a D_b - P_b D_a, 32 to a register)
 *     needs 1 + 7 + F <= 16 at the 64-block range: F 8, G 14, an eye on a 1/256-block grid, which moves 28,136 pixels
 *     a million from FP64's at 32x32 against 675 at F = G = 24: not taken.
 *   FP16 (client.cast_precision 16): FP32's expressions with every real an IEEE half and every operation rounded to
 *     half (the host: _Float16 under gcc's -fexcess-precision=16, the scalar walk alone, Zen 5 having no FP16
 *     arithmetic; the device: cast.cu's h16, + - * and the compares by the half instructions (RC_H16, lane/cxf16), /
 *     and sqrt float's rounded to half; all exact, 24 >= 2 x 11 + 2), literals rounded from double once, a jump's
 *     guess never trusted (its advance settled by the boundaries' own t, which any rounding keeps monotone in the
 *     cell), and a unit direction's component whose reciprocal overflows a half (|d| under 1/65504) taken as 0, the
 *     ray not moving on that axis (lane/cxf16: its inf times the eye's own offset on a boundary, 0, was a NaN t that
 *     the reference and the runs ended differently). The device's packed variant (cast.cu RC_H2 1, lane/cxh2): two
 *     pixels' rays a thread, their first air loop in half2 (HSET2, HADD2, HMUL2: each lane's operation the scalar's,
 *     rounded the same, so no operation here changed order and the bytes are the scalar kernel's and the host's), the
 *     cells and Morton indices two to a register; the rest of each walk scalar. Off by default: 1.5 to 1.8x the
 *     scalar kernel's time (DEVLOG lane/cxh2). Its error is large by design: a half's ulp is
 *     1/32 of a block at 32 blocks, 1/16 at 64, so texels (1/16 of a block) and cell edges past a few blocks are
 *     decided by its roundings; measured, not hidden (DEVLOG lane/castexact).
 *   BF16: decided out (Elliot, 2026-10-31). Its one advantage, range, is unused (the geometry is eye-relative and held
 *     to the range); its 8-bit mantissa is under FP16's 11 and the ~14 bits a 1/256-block distance needs over 64
 *     blocks; and neither chip computes it faster (the 3090's non-tensor BF16 runs at its FP16 rate; Zen 5 has BF16 dot
 *     products and conversions only, avx512_bf16, no avx512_fp16). Nothing is built for it.
 *
 * Two casts that give the same bytes: raycast_ref, the reference (each cell
 * through world_chunk and chunk_get_block, the light through the chunk's
 * own arrays the same way, the frame's lightmap, sky and fog from
 * renderstate_compute itself), and raycast, the fast path the runtime runs
 * (a per-cast table of the sections around the eye, a ray through an empty
 * section crossed without reads, and renderstate's light, sky and fog
 * computed by this file's port). pool_gate --raycast holds them equal on
 * every tick of its recordings. */
#ifndef NETHERITE_RUNTIME_RAYCAST_H
#define NETHERITE_RUNTIME_RAYCAST_H

#include <stddef.h>
#include <stdint.h>

struct session;

struct raycast_spec {
    int w, h;                   /* rays across and down (client.observation) */
    int ch;                     /* bytes a pixel: 3 the RGB frame, 1 its luma (client.channels) */
    int rd;                     /* render distance in chunks (0: the session's, else 4); the range is rd * 16 blocks */
    float gamma;                /* client.gamma */
    int fancy;                  /* client.graphics fancy */
    int fp;                     /* client.cast_precision: 32 the FP32 instance, else (0, 64) the FP64 reference */
    int range;                  /* client.cast_radius: opt-in far-plane cap in blocks (0: rd * 16, vanilla) */
};

/* the textures and tables, once a process, from ROOT (the checkout: its
 * out/java/render/forest-fast and out/assets; NULL: the working directory).
 * 0, or -1 with a message in err; the casts load them themselves on first
 * use (from the working directory) when nothing has. */
int raycast_load(const char *root, char *err, size_t n);

/* env's client view from its client player into rgb[h * w * 3], with neth_env
 * the env's. 0, or -1 when the session has no client world or the tables
 * did not load (rgb is then black). */
int raycast(const struct raycast_spec *sp, struct session *ss, uint8_t *rgb);
int raycast_ref(const struct raycast_spec *sp, struct session *ss, uint8_t *rgb);

/* what a cast drew (lane/seen): for each block id of ids, the pixels whose
 * colour is that block's opaque cube face (the ray's first, so never a block
 * behind a wall) and the nearest such cell with its distance from the eye
 * (d -1: none in view). raycast_seen is raycast with it, the same bytes. */
#define RC_SEEN 4
#define RC_SEEN_LOGS (-17)      /* lane/wood: an id of ids that is either log block, 17 or 162 (the host casts only) */
struct raycast_seen {
    int32_t n[RC_SEEN];
    int32_t x[RC_SEEN], y[RC_SEEN], z[RC_SEEN];
    float d[RC_SEEN];
};
int raycast_seen(const struct raycast_spec *sp, struct session *ss, uint8_t *rgb, const int16_t ids[RC_SEEN],
                 struct raycast_seen *seen);

/* The kept frame (lane/castreuse): raycast_seen with a keep for env id
 * takes the frame env id's last cast with the keep made, and its seen
 * record, when every input that frame's pixels read is the same now (the
 * camera, the entity boxes, the window's chunks by address and write
 * sequence, the lightmap entries, fog and sky its pixels took), else casts
 * and keeps the new frame: the same bytes either way. epoch names the env's
 * run (pool.h pool_epoch): a reset puts the start's write sequences back,
 * so a new epoch never takes a frame kept before it. A frame whose walks
 * are the kept frame's under other light, fog or sky colours is shaded again
 * from its record, and one where entity boxes or chunks some rays reach
 * changed is walked again at those pixels alone. One cast of an env at a
 * time; raycast_keep_new(n) holds n envs' frames (the process's heap: about
 * 10 KB and a frame each, and from an env's first cast with the camera still
 * its record, 150 bytes a pixel). */
struct raycast_keep;
struct raycast_keep *raycast_keep_new(int n);
void raycast_keep_free(struct raycast_keep *kp);
int raycast_seen_keep(const struct raycast_spec *sp, struct session *ss, uint8_t *rgb, const int16_t ids[RC_SEEN],
                      struct raycast_seen *seen, struct raycast_keep *kp, int id, uint32_t epoch);

/* The device's cast (lane/rcdev, cuda/raycast): raycast_head is the cast's
 * setup alone (no rays) into rchead.h's plain form, ids the seen record's
 * (NULL: none); -1 (h->ok 0) when there is no client world or no tables.
 * raycast_tables gives the tables' host addresses (loading them), -1 when
 * they did not load. */
struct rcd_head;
struct rcd_tabsrc;
int raycast_head(const struct raycast_spec *sp, struct session *ss, const int16_t *ids, struct rcd_head *h);
int raycast_tables(struct rcd_tabsrc *o);

/* the FP32 instance's own entry points (raycast_f32.c); the ones above call them for a spec with fp 32 */
struct raycast_keep32;
int raycast_load_f32(const char *root, char *err, size_t n);
int raycast_f32(const struct raycast_spec *sp, struct session *ss, uint8_t *rgb);
int raycast_ref_f32(const struct raycast_spec *sp, struct session *ss, uint8_t *rgb);
int raycast_seen_f32(const struct raycast_spec *sp, struct session *ss, uint8_t *rgb, const int16_t ids[RC_SEEN],
                     struct raycast_seen *seen);
struct raycast_keep32 *raycast_keep_new_f32(int n);
void raycast_keep_free_f32(struct raycast_keep32 *kp);
int raycast_seen_keep_f32(const struct raycast_spec *sp, struct session *ss, uint8_t *rgb, const int16_t ids[RC_SEEN],
                          struct raycast_seen *seen, struct raycast_keep32 *kp, int id, uint32_t epoch);
int raycast_head_f32(const struct raycast_spec *sp, struct session *ss, const int16_t *ids, struct rcd_head *h);

/* Each pixel's ray as its walk ended (lane/cxfix, for pool_gate --cast-cmp's textured rays): kind 0 a block, its cell,
 * the axis the ray entered it across and the entry's distance in 1/256 of a block (floor(t 256)); 1 the sky or fog
 * (nothing else kept); 2 an entity box, x its slot, axis its face, dist its t's. raycast_rays(r): this thread's casts
 * from now on (any instance's, the fast path's and the reference's) write pixel i's into r[i]; NULL stops. Not a
 * production path (a pointer test a pixel). */
struct raycast_ray {
    int32_t x, y, z, dist;
    uint8_t kind, axis, pad[2];
};
void raycast_rays(struct raycast_ray *r);

/* the fixed instance (raycast_fx.c, spec fp RAYCAST_FP_FIXED) and the FP16 instance (raycast_f16.c, fp 16) */
#define RAYCAST_FP_FIXED 1
#define RAYCAST_FP_FAST16 17
#define RAYCAST_FP_MIXED16 18
#define RAYCAST_FP_FAST32 33
struct raycast_keepfx;
struct raycast_keep16;
int raycast_load_fx(const char *root, char *err, size_t n);
int raycast_fx(const struct raycast_spec *sp, struct session *ss, uint8_t *rgb);
int raycast_ref_fx(const struct raycast_spec *sp, struct session *ss, uint8_t *rgb);
int raycast_seen_fx(const struct raycast_spec *sp, struct session *ss, uint8_t *rgb, const int16_t ids[RC_SEEN],
                    struct raycast_seen *seen);
struct raycast_keepfx *raycast_keep_new_fx(int n);
void raycast_keep_free_fx(struct raycast_keepfx *kp);
int raycast_seen_keep_fx(const struct raycast_spec *sp, struct session *ss, uint8_t *rgb, const int16_t ids[RC_SEEN],
                         struct raycast_seen *seen, struct raycast_keepfx *kp, int id, uint32_t epoch);
int raycast_head_fx(const struct raycast_spec *sp, struct session *ss, const int16_t *ids, struct rcd_head *h);
int raycast_load_f16(const char *root, char *err, size_t n);
int raycast_f16(const struct raycast_spec *sp, struct session *ss, uint8_t *rgb);
int raycast_ref_f16(const struct raycast_spec *sp, struct session *ss, uint8_t *rgb);
int raycast_seen_f16(const struct raycast_spec *sp, struct session *ss, uint8_t *rgb, const int16_t ids[RC_SEEN],
                     struct raycast_seen *seen);
struct raycast_keep16 *raycast_keep_new_f16(int n);
void raycast_keep_free_f16(struct raycast_keep16 *kp);
int raycast_seen_keep_f16(const struct raycast_spec *sp, struct session *ss, uint8_t *rgb, const int16_t ids[RC_SEEN],
                          struct raycast_seen *seen, struct raycast_keep16 *kp, int id, uint32_t epoch);
int raycast_head_f16(const struct raycast_spec *sp, struct session *ss, const int16_t *ids, struct rcd_head *h);

#endif
