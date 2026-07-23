/* app/main.c - playable walkable/flyable voxel world for magma.
 *
 * Ties the world feed in world/ to the software raster spine (transform.c,
 * cpu/raster_cpu.c, core/shade.c, present/present.c) through the two contract
 * headers core/types.h and world/world.h. NO OpenGL in the render path: every
 * triangle goes world CrVertex[] -> cr_transform -> cr_raster_cpu -> framebuffer
 * -> cr_window_present (a plain RGBA blit).
 *
 * Camera convention (see core/math.c):
 *   forward = (-sin(yaw)cos(pitch), sin(pitch), -cos(yaw)cos(pitch))
 *   yaw=0,pitch=0 looks down -Z; +pitch tilts UP; +yaw turns toward -X.
 *
 * Args:
 *   --seed N     worldgen seed              (default 0)
 *   --w W --h H  framebuffer size           (default 800x600)
 *   --radius R   chunk load/render radius   (default 4)
 *   --frames N   headless: run N frames then exit (walks the camera forward)
 *   --ppm PATH   write the final frame as binary P6 PPM
 *   --fly        start in fly mode
 * Default (no --frames): interactive loop until ESC / window close.
 */
#include "core/types.h"
#include "world/world.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define CHUNK        16      /* world blocks per chunk edge (x,z) */
#define MAX_CHUNKS   4096    /* cached chunk meshes */
#define MAX_TRIS     (2 * 1024 * 1024) /* reused screen-tri scratch, one malloc */
#define MESH_SCRATCH (1 << 20)          /* per-chunk meshing scratch verts */
#define EYE_HEIGHT   1.62f
#define PITCH_LIMIT  (89.0f * (float)M_PI / 180.0f)

/* ---- one cached chunk mesh (world-space CrVertex triangle list) ---- */
typedef struct {
    int       used;
    int       cx, cz;    /* chunk coords */
    CrVertex *verts;
    int       nverts;
} ChunkMesh;

static ChunkMesh   g_chunks[MAX_CHUNKS];
static CrVertex   *g_mesh_scratch;   /* reused across world_mesh_chunk calls */

/* floor division that works for negative numerators (block -> chunk coord). */
static int floordiv(int a, int b)
{
    int q = a / b, r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) --q;
    return q;
}

/* ---- camera basis from yaw/pitch (matches core/math.c) ----
 * horizontal forward (ground plane, normalized) for WASD */
static CrVec3 cam_forward_flat(float yaw)
{
    CrVec3 f = { -sinf(yaw), 0.0f, -cosf(yaw) };
    return f;
}
/* right = cross(forward_flat, up); +X at yaw=0 */
static CrVec3 cam_right_flat(float yaw)
{
    CrVec3 r = { cosf(yaw), 0.0f, -sinf(yaw) };
    return r;
}

/* ---- chunk cache lookup / insert ---- */
static ChunkMesh *chunk_find(int cx, int cz)
{
    for (int i = 0; i < MAX_CHUNKS; ++i)
        if (g_chunks[i].used && g_chunks[i].cx == cx && g_chunks[i].cz == cz)
            return &g_chunks[i];
    return NULL;
}

static ChunkMesh *chunk_slot(void)
{
    for (int i = 0; i < MAX_CHUNKS; ++i)
        if (!g_chunks[i].used)
            return &g_chunks[i];
    return NULL;
}

/* Mesh a chunk once and cache it. No-op if already present. */
static void chunk_load(CrWorld *w, int cx, int cz)
{
    if (chunk_find(cx, cz))
        return;
    ChunkMesh *slot = chunk_slot();
    if (!slot) {
        fprintf(stderr, "magma: chunk cache full, dropping (%d,%d)\n", cx, cz);
        return;
    }
    int n = world_mesh_chunk(w, cx, cz, g_mesh_scratch, MESH_SCRATCH);
    if (n < 0) n = 0;
    slot->used   = 1;
    slot->cx     = cx;
    slot->cz     = cz;
    slot->nverts = n;
    slot->verts  = NULL;
    if (n > 0) {
        slot->verts = (CrVertex *)malloc((size_t)n * sizeof(CrVertex));
        if (!slot->verts) {
            fprintf(stderr, "magma: OOM caching chunk (%d,%d)\n", cx, cz);
            slot->nverts = 0;
        } else {
            memcpy(slot->verts, g_mesh_scratch, (size_t)n * sizeof(CrVertex));
        }
    }
}

/* Drop cached chunks far outside the render window so flying stays bounded. */
static void chunk_evict(int ccx, int ccz, int keep)
{
    for (int i = 0; i < MAX_CHUNKS; ++i) {
        if (!g_chunks[i].used)
            continue;
        int dx = g_chunks[i].cx - ccx, dz = g_chunks[i].cz - ccz;
        if (abs(dx) > keep || abs(dz) > keep) {
            free(g_chunks[i].verts);
            g_chunks[i].verts = NULL;
            g_chunks[i].used  = 0;
            g_chunks[i].nverts = 0;
        }
    }
}

/* Generate + mesh every chunk within `radius` of the camera's chunk. Meshing a
 * chunk needs its 8 neighbours generated, so we ensure radius+1. */
static void world_stream(CrWorld *w, int ccx, int ccz, int radius)
{
    world_ensure(w, ccx, ccz, radius + 1);
    for (int dz = -radius; dz <= radius; ++dz)
        for (int dx = -radius; dx <= radius; ++dx)
            chunk_load(w, ccx + dx, ccz + dz);
    chunk_evict(ccx, ccz, radius + 2);
}

/* Write fb->color as binary P6 PPM (opaque RGB, top-down). Returns 0 on ok. */
static int write_ppm(const char *path, const CrFramebuffer *fb)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "magma: cannot open %s for writing\n", path);
        return -1;
    }
    fprintf(f, "P6\n%d %d\n255\n", fb->w, fb->h);
    size_t n = (size_t)fb->w * (size_t)fb->h;
    u8 *rgb = (u8 *)malloc(n * 3);
    if (!rgb) { fclose(f); return -1; }
    for (size_t i = 0; i < n; ++i) {
        rgb[i * 3 + 0] = fb->color[i].r;
        rgb[i * 3 + 1] = fb->color[i].g;
        rgb[i * 3 + 2] = fb->color[i].b;
    }
    size_t wrote = fwrite(rgb, 3, n, f);
    free(rgb);
    fclose(f);
    if (wrote != n) {
        fprintf(stderr, "magma: short write to %s\n", path);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    long long   seed        = 0;
    int         fb_w        = 800, fb_h = 600;
    int         radius      = 4;
    int         want_frames = -1;      /* -1 = interactive */
    const char *ppm_path    = NULL;
    int         fly         = 0;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            seed = atoll(argv[++i]);
        } else if (!strcmp(argv[i], "--w") && i + 1 < argc) {
            fb_w = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--h") && i + 1 < argc) {
            fb_h = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--radius") && i + 1 < argc) {
            radius = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            want_frames = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--ppm") && i + 1 < argc) {
            ppm_path = argv[++i];
        } else if (!strcmp(argv[i], "--fly")) {
            fly = 1;
        } else {
            fprintf(stderr,
                "usage: %s [--seed N] [--w W --h H] [--radius R] "
                "[--frames N] [--ppm PATH] [--fly]\n", argv[0]);
            return 2;
        }
    }
    if (fb_w < 1 || fb_h < 1 || radius < 1) {
        fprintf(stderr, "magma: bad dimensions/radius\n");
        return 2;
    }

    /* --- framebuffer --- */
    CrFramebuffer fb;
    cr_fb_alloc(&fb, fb_w, fb_h);
    if (!fb.color || !fb.depth) {
        fprintf(stderr, "magma: cr_fb_alloc failed\n");
        return 1;
    }

    /* --- scratch buffers (allocated once, reused every frame) --- */
    g_mesh_scratch = (CrVertex *)malloc((size_t)MESH_SCRATCH * sizeof(CrVertex));
    CrScreenTri *tris =
        (CrScreenTri *)malloc((size_t)MAX_TRIS * sizeof(CrScreenTri));
    if (!g_mesh_scratch || !tris) {
        fprintf(stderr, "magma: scratch allocation failed\n");
        return 1;
    }

    /* --- world --- */
    CrWorld *world = world_create(seed);
    if (!world) {
        fprintf(stderr, "magma: world_create failed\n");
        return 1;
    }

    /* --- spawn: a few blocks above the surface at the origin column --- */
    int surface = world_surface_y(world, 0, 0);
    CrCamera cam = {0};
    cam.pos     = (CrVec3){0.5f, (float)surface + 3.0f, 0.5f};
    cam.yaw     = 0.0f;
    cam.pitch   = -0.35f;                    /* look slightly downward */
    cam.fov_deg = 70.0f;
    cam.aspect  = (float)fb_w / (float)fb_h;
    cam.znear   = 0.05f;
    cam.zfar    = (float)(radius + 1) * (float)CHUNK;

    /* --- shading context: procedural atlas + mild distance fog --- */
    CrTexture atlas = world_atlas(world);
    const CrRgba sky = {135, 206, 235, 255};   /* Minecraft day-sky blue */
    CrShadeCtx sh;
    memset(&sh, 0, sizeof(sh));
    sh.atlas      = &atlas;
    sh.fog_color  = sky;
    sh.fog_start  = (float)(radius - 1) * (float)CHUNK;
    sh.fog_end    = (float)radius * (float)CHUNK;
    sh.alpha_test = 0;
    sh.enable_fog = 1;

    /* --- initial stream around the spawn chunk --- */
    int ccx = floordiv((int)floorf(cam.pos.x), CHUNK);
    int ccz = floordiv((int)floorf(cam.pos.z), CHUNK);
    world_stream(world, ccx, ccz, radius);

    /* --- window (headless-safe: present layer uses the SDL dummy driver) --- */
    CrWindow *win = cr_window_open(fb_w, fb_h, "magma - world");
    if (!win) {
        fprintf(stderr, "magma: cr_window_open failed\n");
        return 1;
    }

    const float MOVE  = 0.35f;    /* blocks per frame while a key is held */
    const float SENS  = 0.0025f;  /* radians per mouse count */
    int   prev_ctrl = 0;
    int   frame = 0, running = 1;

    while (running) {
        /* ---- input / camera update ---- */
        if (want_frames >= 0) {
            /* headless: walk forward slowly to prove animation */
            CrVec3 f = cam_forward_flat(cam.yaw);
            cam.pos.x += f.x * 0.4f;
            cam.pos.z += f.z * 0.4f;
            if (!fly)
                cam.pos.y = (float)world_surface_y(world,
                                (int)floorf(cam.pos.x),
                                (int)floorf(cam.pos.z)) + EYE_HEIGHT;
        } else {
            CrInput in;
            cr_window_poll(win, &in);
            if (in.quit) { running = 0; break; }

            /* toggle fly/walk on ctrl press edge */
            if (in.key_ctrl && !prev_ctrl) fly = !fly;
            prev_ctrl = in.key_ctrl;

            /* mouse look: dx right -> yaw down (turn right); dy down -> pitch down */
            cam.yaw   -= (float)in.mouse_dx * SENS;
            cam.pitch -= (float)in.mouse_dy * SENS;
            if (cam.pitch >  PITCH_LIMIT) cam.pitch =  PITCH_LIMIT;
            if (cam.pitch < -PITCH_LIMIT) cam.pitch = -PITCH_LIMIT;

            CrVec3 f = cam_forward_flat(cam.yaw);
            CrVec3 r = cam_right_flat(cam.yaw);
            float mx = 0.0f, mz = 0.0f;
            if (in.key_w) { mx += f.x; mz += f.z; }
            if (in.key_s) { mx -= f.x; mz -= f.z; }
            if (in.key_d) { mx += r.x; mz += r.z; }
            if (in.key_a) { mx -= r.x; mz -= r.z; }
            cam.pos.x += mx * MOVE;
            cam.pos.z += mz * MOVE;

            if (fly) {
                if (in.key_space) cam.pos.y += MOVE;
                if (in.key_shift) cam.pos.y -= MOVE;
            } else {
                cam.pos.y = (float)world_surface_y(world,
                                (int)floorf(cam.pos.x),
                                (int)floorf(cam.pos.z)) + EYE_HEIGHT;
            }
        }

        /* ---- stream chunks as we cross into a new chunk ---- */
        int ncx = floordiv((int)floorf(cam.pos.x), CHUNK);
        int ncz = floordiv((int)floorf(cam.pos.z), CHUNK);
        if (ncx != ccx || ncz != ccz) {
            ccx = ncx; ccz = ncz;
            world_stream(world, ccx, ccz, radius);
        }

        /* ---- render: clear sky, transform+raster each cached chunk mesh ---- */
        cr_fb_clear(&fb, sky);
        for (int i = 0; i < MAX_CHUNKS; ++i) {
            ChunkMesh *cm = &g_chunks[i];
            if (!cm->used || cm->nverts < 3)
                continue;
            int ntris = cr_transform(cm->verts, cm->nverts, NULL, 0,
                                     &cam, fb.w, fb.h, tris, MAX_TRIS);
            if (ntris > 0)
                cr_raster_cpu(&fb, tris, ntris, &sh);
        }
        cr_window_present(win, &fb);

        ++frame;
        if (want_frames >= 0 && frame >= want_frames)
            running = 0;
    }

    if (ppm_path)
        write_ppm(ppm_path, &fb);

    /* ---- teardown ---- */
    for (int i = 0; i < MAX_CHUNKS; ++i)
        if (g_chunks[i].used) free(g_chunks[i].verts);
    free(tris);
    free(g_mesh_scratch);
    world_destroy(world);
    cr_window_close(win);
    cr_fb_free(&fb);
    return 0;
}
