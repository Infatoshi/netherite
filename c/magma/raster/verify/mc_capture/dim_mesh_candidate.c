/* dim_mesh_candidate - mesh+raster a qrl dumpblocks window (vanilla id<<4|meta)
 * for nether/end/portal pixel compare. Maps vanilla ids onto CBX_* atlas models
 * (netherrack/portal/end_stone/...). Clears sky with dim-appropriate fog.
 *
 * Usage:
 *   dim_mesh_candidate --mcbd path --cx0 .. --cz1 .. --eye x y z
 *     --yaw DEG --pitch DEG [--fov 77] [--ppm out.ppm]
 *     [--clear R G B] [--no-sky-light]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "core/types.h"
#include "world/mesh_mc.h"
#include "world/light.h"
#include "game/sky.h"

/* Keep in sync with assets/blockmodels.c CBX_* dim ids. */
enum {
    CB_AIR = 0, CB_STONE = 1, CB_WATER = 2, CB_GRASS = 3, CB_DIRT = 4,
    CB_BEDROCK = 5, CB_GRAVEL = 6, CB_SAND = 7, CB_SANDSTONE = 8,
    CB_LAVA = 11, CB_FLOWING_LAVA = 12,
    PB_OBSIDIAN = 89,
    CBX_NETHERRACK = 210, CBX_PORTAL = 211, CBX_END_STONE = 212,
    CBX_FIRE = 213, CBX_GLOWSTONE = 214, CBX_SOUL_SAND = 215,
    CBX_END_FRAME = 216, CBX_QUARTZ_ORE = 217,
    CBX_BROWN_MUSHROOM = 218, CBX_RED_MUSHROOM = 219, CBX_MAGMA = 220,
    CBX_IRON_BARS = 221, CBX_TORCH = 222
};

static int map_vanilla(int vid) {
    switch (vid) {
    case 0: return CB_AIR;
    case 1: return CB_STONE;
    case 2: return CB_GRASS;
    case 3: return CB_DIRT;
    case 7: return CB_BEDROCK;
    case 8: case 9: return CB_WATER;
    case 10: return CB_FLOWING_LAVA;
    case 11: return CB_LAVA;
    case 12: return CB_SAND;
    case 13: return CB_GRAVEL;
    case 39: return CBX_BROWN_MUSHROOM;
    case 40: return CBX_RED_MUSHROOM;
    case 49: return PB_OBSIDIAN;
    case 50: return CBX_TORCH;
    case 51: return CBX_FIRE;
    case 87: return CBX_NETHERRACK;
    case 88: return CBX_SOUL_SAND;
    case 89: return CBX_GLOWSTONE;
    case 90: return CBX_PORTAL;
    case 101: return CBX_IRON_BARS;
    case 119: return 234; /* CBX_END_PORTAL */
    case 120: return CBX_END_FRAME;
    case 121: return CBX_END_STONE;
    case 153: return CBX_QUARTZ_ORE;
    case 213: return CBX_MAGMA;
    default: return vid == 0 ? CB_AIR : -1;
    }
}

/* Metadata is only accepted where this renderer has a semantic consumer. A
 * copied input-nibble count is not evidence of support: fluid levels alter the
 * surface, portal axis alters the panel, and End-frame bits alter facing/eye. */
static int metadata_supported(int vid) {
    switch (vid) {
    case 10: case 11: case 50: case 51: case 90: case 120:
        return 1;
    default:
        return 0;
    }
}

extern CrLight *worldmc_light(CrWorldMC *w);

static void render_layer(CrFramebuffer *fb, CrVertex *verts, int nv,
                         const CrCamera *cam, int W, int H, const CrShadeCtx *sh) {
    if (nv < 3) return;
    int max_tris = (nv / 3) * 2;
    CrScreenTri *tris = malloc(sizeof(CrScreenTri) * (size_t)max_tris);
    int n = cr_transform(verts, nv, NULL, 0, cam, W, H, tris, max_tris);
    cr_raster_cpu(fb, tris, n, sh);
    free(tris);
}

static int write_ppm(const char *path, const CrFramebuffer *fb) {
    FILE *po = fopen(path, "wb");
    if (!po) { perror(path); return 0; }
    fprintf(po, "P6\n%d %d\n255\n", fb->w, fb->h);
    for (int i = 0; i < fb->w * fb->h; ++i) {
        fputc(fb->color[i].r, po);
        fputc(fb->color[i].g, po);
        fputc(fb->color[i].b, po);
    }
    fclose(po);
    return 1;
}

int main(int argc, char **argv) {
    const char *mcbd = NULL;
    int cx0 = 0, cz0 = 0, cx1 = 0, cz1 = 0;
    float eye_x = 0, eye_y = 64, eye_z = 0, yaw_deg = 0, pitch_deg = 0, fov = 77;
    int W = 854, H = 480;
    const char *out = "/tmp/dim_mesh.ppm";
    const char *background_out = NULL;
    const char *mask_out = NULL;
    int clear_r = 20, clear_g = 4, clear_b = 4;
    int dimension = -1;
    float torch_flicker_x = 0.0f, gamma = 0.0f;
    float far_plane = 128.0f, fog_start = -1.0f, fog_end = -1.0f;
    int disable_fire = 0;
    int mesh_radius = -1;
    long long seed = 0;
    const float pi = 3.14159265358979323846f;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--mcbd") && i + 1 < argc) mcbd = argv[++i];
        else if (!strcmp(argv[i], "--cx0") && i + 1 < argc) cx0 = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--cz0") && i + 1 < argc) cz0 = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--cx1") && i + 1 < argc) cx1 = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--cz1") && i + 1 < argc) cz1 = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--eye") && i + 3 < argc) {
            eye_x = (float)atof(argv[++i]);
            eye_y = (float)atof(argv[++i]);
            eye_z = (float)atof(argv[++i]);
        } else if (!strcmp(argv[i], "--yaw") && i + 1 < argc)
            yaw_deg = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--pitch") && i + 1 < argc)
            pitch_deg = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--fov") && i + 1 < argc)
            fov = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--ppm") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--background-ppm") && i + 1 < argc)
            background_out = argv[++i];
        else if (!strcmp(argv[i], "--clear") && i + 3 < argc) {
            clear_r = atoi(argv[++i]); clear_g = atoi(argv[++i]); clear_b = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--seed") && i + 1 < argc)
            seed = atoll(argv[++i]);
        else if (!strcmp(argv[i], "--dimension") && i + 1 < argc)
            dimension = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--torch-flicker") && i + 1 < argc)
            torch_flicker_x = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--gamma") && i + 1 < argc)
            gamma = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--far-plane") && i + 1 < argc)
            far_plane = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--fog-start") && i + 1 < argc)
            fog_start = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--fog-end") && i + 1 < argc)
            fog_end = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--disable-fire")) disable_fire = 1;
        else if (!strcmp(argv[i], "--mesh-radius") && i + 1 < argc)
            mesh_radius = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mask") && i + 1 < argc)
            mask_out = argv[++i];
    }
    if (!mcbd) { fprintf(stderr, "need --mcbd\n"); return 2; }

    FILE *f = fopen(mcbd, "rb");
    if (!f) { perror(mcbd); return 2; }
    int ncx = cx1 - cx0 + 1, ncz = cz1 - cz0 + 1;
    size_t total = (size_t)ncx * ncz * 16ull * 16 * 256 * 2;
    unsigned char *raw = malloc(total);
    if (fread(raw, 1, total, f) != total) {
        fprintf(stderr, "short mcbd need %zu\n", total); return 2;
    }
    fclose(f);

    CrWorldMC *w = worldmc_create(seed);
    int ccx = (int)floorf(eye_x / 16.f), ccz = (int)floorf(eye_z / 16.f);
    if (mesh_radius >= 0 &&
        (cx0 > ccx - mesh_radius - 1 || cx1 < ccx + mesh_radius + 1 ||
         cz0 > ccz - mesh_radius - 1 || cz1 < ccz + mesh_radius + 1)) {
        fprintf(stderr, "dump lacks one-chunk apron for mesh radius %d\n", mesh_radius);
        return 2;
    }
    int rad = 0;
    for (int cz = cz0; cz <= cz1; ++cz)
        for (int cx = cx0; cx <= cx1; ++cx) {
            int d = abs(cx - ccx);
            if (abs(cz - ccz) > d) d = abs(cz - ccz);
            if (d > rad) rad = d;
        }
    rad += 1;
    worldmc_ensure(w, ccx, ccz, rad);
    CrLight *L = worldmc_light(w);

    size_t off = 0;
    int setn = 0, fallback_nonair = 0;
    int supported_nonzero_meta = 0, unsupported_nonzero_meta = 0;
    for (int cz = cz0; cz <= cz1; ++cz)
        for (int cx = cx0; cx <= cx1; ++cx)
            for (int y = 0; y < 256; ++y)
                for (int z = 0; z < 16; ++z)
                    for (int x = 0; x < 16; ++x) {
                        int lo = raw[off++], hi = raw[off++];
                        int v = lo | (hi << 8);
                        int vid = v >> 4;
                        int meta = v & 15;
                        int cb = map_vanilla(vid);
                        if (cb < 0) { cb = CB_STONE; fallback_nonair++; }
                        light_debug_set_block_meta(L, cx * 16 + x, y, cz * 16 + z, cb, meta);
                        if (cb) setn++;
                        if (meta) {
                            if (metadata_supported(vid)) supported_nonzero_meta++;
                            else unsupported_nonzero_meta++;
                        }
                    }
    free(raw);
    /* Explicit provider state: Nether and End both have no stored skylight, but
     * use different brightness tables / RGB lightmap formulas. */
    light_set_render_state(L, dimension, torch_flicker_x, gamma);
    worldmc_set_fire_mesh_enabled(!disable_fire);
    fprintf(stderr,
            "dim_mesh: dim=%d nonair=%d fallback=%d meta_supported=%d "
            "meta_unsupported=%d r=%d center=(%d,%d)\n",
            dimension, setn, fallback_nonair, supported_nonzero_meta,
            unsupported_nonzero_meta, rad, ccx, ccz);
    if (fallback_nonair != 0 || unsupported_nonzero_meta != 0) {
        fprintf(stderr, "dim_mesh: unsupported non-air ids=%d metadata=%d\n",
                fallback_nonair, unsupported_nonzero_meta);
        return 3;
    }

    CrCamera cam;
    memset(&cam, 0, sizeof(cam));
    cam.pos.x = eye_x; cam.pos.y = eye_y; cam.pos.z = eye_z;
    cam.yaw = yaw_deg * (pi / 180.f);
    cam.pitch = pitch_deg * (pi / 180.f);
    cam.fov_deg = fov;
    cam.aspect = (float)W / (float)H;
    cam.znear = 0.05f;
    cam.zfar = 181.01933f;

    CrVertex *verts[4] = {0};
    int nverts[4] = {0}, cap[4] = {0};
    for (int cz = cz0; cz <= cz1; ++cz)
        for (int cx = cx0; cx <= cx1; ++cx) {
            if (mesh_radius >= 0 &&
                (abs(cx - ccx) > mesh_radius || abs(cz - ccz) > mesh_radius))
                continue;
            CrChunkMeshMC m;
            memset(&m, 0, sizeof(m));
            worldmc_mesh_chunk(w, cx, cz, &m);
            for (int l = 0; l < 4; ++l) {
                int add = m.nverts[l];
                if (add <= 0) continue;
                if (nverts[l] + add > cap[l]) {
                    int nc = cap[l] ? cap[l] : 4096;
                    while (nverts[l] + add > nc) nc *= 2;
                    verts[l] = realloc(verts[l], (size_t)nc * sizeof(CrVertex));
                    cap[l] = nc;
                }
                memcpy(verts[l] + nverts[l], m.verts[l], (size_t)add * sizeof(CrVertex));
                nverts[l] += add;
            }
            worldmc_free_mesh(&m);
        }
    fprintf(stderr, "mesh verts: %d %d %d %d\n",
            nverts[0], nverts[1], nverts[2], nverts[3]);

    CrFramebuffer fb;
    cr_fb_alloc(&fb, W, H);
    cr_fb_clear(&fb, (CrRgba){(unsigned char)clear_r, (unsigned char)clear_g,
                              (unsigned char)clear_b, 255});
    if (dimension == 1) gm_end_sky_draw(&fb, &cam);
    if (background_out && !write_ppm(background_out, &fb)) return 2;
    CrTexture atlas = worldmc_atlas(w);
    CrRgba fog = {(unsigned char)clear_r, (unsigned char)clear_g,
                  (unsigned char)clear_b, 255};
    if (fog_start < 0.0f || fog_end <= fog_start) {
        /* EntityRenderer.setupFog(0): Hell's doesXZShowFog uses 5%-to-50%
         * dense fog. End/Overworld use the normal 75%-to-100% range unless
         * the caller records a boss overlay and supplies explicit values. */
        if (dimension == -1) {
            fog_start = far_plane * 0.05f;
            fog_end = fminf(far_plane, 192.0f) * 0.5f;
        } else {
            fog_start = far_plane * 0.75f;
            fog_end = far_plane;
        }
    }
    CrShadeCtx sh0 = {
            .atlas = &atlas, .fog_color = fog, .fog_start = fog_start,
            .fog_end = fog_end, .alpha_test = 0, .enable_fog = 1,
            .layer = CR_LAYER_SOLID, .blend = 0, .use_mips = 0,
            .mip_bias = 0.f };
    CrShadeCtx sh1 = {
            .atlas = &atlas, .fog_color = fog, .fog_start = fog_start,
            .fog_end = fog_end, .alpha_test = 1, .enable_fog = 1,
            .layer = CR_LAYER_CUTOUT_MIPPED, .blend = 0, .use_mips = 0,
            .mip_bias = 0.f };
    CrShadeCtx sh2 = {
            .atlas = &atlas, .fog_color = fog, .fog_start = fog_start,
            .fog_end = fog_end, .alpha_test = 1, .enable_fog = 1,
            .layer = CR_LAYER_CUTOUT, .blend = 0, .use_mips = 0,
            .mip_bias = 0.f };
    CrShadeCtx sh3 = {
            .atlas = &atlas, .fog_color = fog, .fog_start = fog_start,
            .fog_end = fog_end, .alpha_test = 0, .enable_fog = 1,
            .layer = CR_LAYER_TRANSLUCENT, .blend = 1, .use_mips = 0,
            .mip_bias = 0.f };
    render_layer(&fb, verts[0], nverts[0], &cam, W, H, &sh0);
    render_layer(&fb, verts[1], nverts[1], &cam, W, H, &sh1);
    render_layer(&fb, verts[2], nverts[2], &cam, W, H, &sh2);
    render_layer(&fb, verts[3], nverts[3], &cam, W, H, &sh3);

    if (!write_ppm(out, &fb)) return 2;
    if (mask_out) {
        FILE *mo = fopen(mask_out, "wb");
        if (!mo) { perror(mask_out); return 2; }
        fprintf(mo, "P5\n%d %d\n255\n", W, H);
        for (int i = 0; i < W * H; ++i)
            fputc(fb.depth[i] < 1.0f ? 255 : 0, mo);
        fclose(mo);
    }
    fprintf(stderr, "wrote %s\n", out);
    for (int l = 0; l < 4; ++l) free(verts[l]);
    worldmc_destroy(w);
    cr_fb_free(&fb);
    return 0;
}
