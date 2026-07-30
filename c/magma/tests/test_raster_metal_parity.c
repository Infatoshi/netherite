#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "core/types.h"
#include "raster/backend.h"
#include "raster/verify/scene.h"

static CrScreenVert to_screen(const ScnVert *v, int width, int height) {
    float invw = 1.0f / v->w;
    float nx = v->x * invw, ny = v->y * invw, nz = v->z * invw;
    CrScreenVert s;
    memset(&s, 0, sizeof s);
    s.spos.x = (nx * 0.5f + 0.5f) * (float)width;
    s.spos.y = (0.5f - 0.5f * ny) * (float)height;
    s.spos.z = nz * 0.5f + 0.5f;
    s.invw = invw;
    s.uv_w.x = v->u * invw; s.uv_w.y = v->v * invw;
    s.light_w = v->light * invw; s.ao_w = invw;
    s.eye_dist_w = v->w * invw;
    s.tint_r_w = s.tint_g_w = s.tint_b_w = s.tint_a_w = 255.0f * invw;
    s.blk_w = 0.0f;
    return s;
}

static int build_tris(const ScnVert *verts, int nverts, int width, int height,
                      CrScreenTri *tris) {
    int n = 0;
    for (int t = 0; t < nverts / 3; ++t) {
        CrScreenVert a = to_screen(&verts[t * 3], width, height);
        CrScreenVert b = to_screen(&verts[t * 3 + 1], width, height);
        CrScreenVert c = to_screen(&verts[t * 3 + 2], width, height);
        tris[n++] = (CrScreenTri){{a, b, c}};
        tris[n++] = (CrScreenTri){{a, c, b}};
    }
    return n;
}

static int compare(const char *name, int run, const CrFramebuffer *cpu,
                   const CrFramebuffer *metal) {
    int color_bad = 0, depth_bad = 0, max_diff = 0, first = -1;
    int count = cpu->w * cpu->h;
    for (int i = 0; i < count; ++i) {
        const unsigned char *a = (const unsigned char *)&cpu->color[i];
        const unsigned char *b = (const unsigned char *)&metal->color[i];
        for (int c = 0; c < 4; ++c) {
            int d = abs((int)a[c] - (int)b[c]);
            if (d) { ++color_bad; if (first < 0) first = i; }
            if (d > max_diff) max_diff = d;
        }
        if (memcmp(&cpu->depth[i], &metal->depth[i], sizeof(float))) {
            ++depth_bad; if (first < 0) first = i;
        }
    }
    if (color_bad || depth_bad) {
        unsigned cpu_z = 0, metal_z = 0;
        if (first >= 0) {
            memcpy(&cpu_z, &cpu->depth[first], sizeof cpu_z);
            memcpy(&metal_z, &metal->depth[first], sizeof metal_z);
        }
        fprintf(stderr,
                "FAIL %-18s run=%d size=%dx%d first=(%d,%d) color_bad=%d maxdiff=%d depth_bad=%d z=%08x/%08x rgba=%u,%u,%u/%u,%u,%u\n",
                name, run, cpu->w, cpu->h,
                first < 0 ? -1 : first % cpu->w,
                first < 0 ? -1 : first / cpu->w,
                color_bad, max_diff, depth_bad, cpu_z, metal_z,
                first < 0 ? 0 : cpu->color[first].r,
                first < 0 ? 0 : cpu->color[first].g,
                first < 0 ? 0 : cpu->color[first].b,
                first < 0 ? 0 : metal->color[first].r,
                first < 0 ? 0 : metal->color[first].g,
                first < 0 ? 0 : metal->color[first].b);
        return 0;
    }
    return 1;
}

typedef struct {
    const char *name;
    const ScnVert *vertices;
    int nvertices;
    CrShadeCtx shade;
} Case;

static const ScnVert EDGE_VERTS[] = {
    /* Collinear: degenerate after viewport transform. */
    {-0.8f, -0.8f, 0.2f, 1.f, 0.f, 0.f, 1.f},
    { 0.0f,  0.0f, 0.2f, 1.f, 0.5f, 0.5f, 1.f},
    { 0.8f,  0.8f, 0.2f, 1.f, 1.f, 1.f, 1.f},
    /* Valid winding but wholly beyond the right edge. */
    { 2.0f, -0.5f, 0.2f, 1.f, 0.f, 0.f, 1.f},
    { 3.0f, -0.5f, 0.2f, 1.f, 1.f, 0.f, 1.f},
    { 3.0f,  0.5f, 0.2f, 1.f, 1.f, 1.f, 1.f},
};

static int run_size(int width, int height, Case *cases, int ncases) {
    CrFramebuffer cpu, metal;
    CrRasterBackend *backend = NULL;
    char error[512];
    int ok = 1;
    cr_fb_alloc(&cpu, width, height); cr_fb_alloc(&metal, width, height);
    if (!cpu.color || !cpu.depth || !metal.color || !metal.depth) {
        fprintf(stderr, "framebuffer allocation failed\n");
        return 0;
    }
    if (!cr_backend_open(&backend, GM_BACKEND_METAL, width, height, 300,
                         error, sizeof error)) {
        fprintf(stderr, "Metal backend open failed: %s\n", error);
        cr_fb_free(&cpu); cr_fb_free(&metal);
        return 0;
    }
    for (int c = 0; c < ncases; ++c) {
        CrScreenTri tris[64];
        int ntris = build_tris(cases[c].vertices, cases[c].nvertices,
                               width, height, tris);
        for (int run = 0; run < 3; ++run) {
            CrRgba clear = {(u8)(20 + run), 30, 40, 255};
            cr_fb_clear(&cpu, clear); cr_fb_clear(&metal, clear);
            cr_raster_cpu(&cpu, tris, ntris, &cases[c].shade);
            if (!cr_backend_frame_begin(backend, &metal) ||
                !cr_backend_raster(backend, &metal, tris, ntris, &cases[c].shade) ||
                !cr_backend_frame_end(backend, &metal)) {
                fprintf(stderr, "Metal %-18s failed: %s\n", cases[c].name,
                        cr_backend_last_error(backend));
                ok = 0; break;
            }
            if (!compare(cases[c].name, run, &cpu, &metal)) ok = 0;
        }
        if (ok) printf("PASS %-18s %dx%d repeated=3\n",
                       cases[c].name, width, height);
    }
    cr_backend_close(backend); cr_fb_free(&cpu); cr_fb_free(&metal);
    return ok;
}

static int run_count_matrix(CrTexture *atlas, CrRgba fog) {
    enum { WIDTH = 17, HEIGHT = 19, MAX_TRIS = 300 };
    const int counts[] = {0, 1, 255, 256, 257};
    CrFramebuffer cpu, metal, wrong;
    CrRasterBackend *backend = NULL;
    CrScreenTri source[2], tris[MAX_TRIS + 1];
    CrShadeCtx shade = {.atlas=atlas, .fog_color=fog,
                        .layer=CR_LAYER_SOLID};
    char error[512];
    int ok = 1;

    cr_fb_alloc(&cpu, WIDTH, HEIGHT);
    cr_fb_alloc(&metal, WIDTH, HEIGHT);
    cr_fb_alloc(&wrong, WIDTH + 1, HEIGHT);
    if (!cpu.color || !cpu.depth || !metal.color || !metal.depth ||
        !wrong.color || !wrong.depth ||
        build_tris(SCN_VERTS, 3, WIDTH, HEIGHT, source) != 2 ||
        !cr_backend_open(&backend, GM_BACKEND_METAL, WIDTH, HEIGHT, MAX_TRIS,
                         error, sizeof error)) {
        fprintf(stderr, "triangle-count setup failed: %s\n",
                backend ? "framebuffer/source" : error);
        cr_backend_close(backend); cr_fb_free(&cpu); cr_fb_free(&metal);
        cr_fb_free(&wrong);
        return 0;
    }
    for (int i = 0; i <= MAX_TRIS; ++i) {
        tris[i] = source[0];
        /* Every later triangle is nearer, so work on both sides of each
         * 256-triangle compaction boundary remains observable. */
        for (int v = 0; v < 3; ++v)
            tris[i].v[v].spos.z = 0.9f - 0.002f * (float)i;
    }
    for (unsigned ci = 0; ci < sizeof counts / sizeof counts[0]; ++ci) {
        int ntris = counts[ci];
        CrRgba clear = {(u8)(50 + ci), 60, 70, 255};
        cr_fb_clear(&cpu, clear); cr_fb_clear(&metal, clear);
        cr_raster_cpu(&cpu, tris, ntris, &shade);
        if (!cr_backend_frame_begin(backend, &metal) ||
            !cr_backend_raster(backend, &metal, tris, ntris, &shade) ||
            !cr_backend_frame_end(backend, &metal) ||
            !compare("triangle-count", ntris, &cpu, &metal)) {
            fprintf(stderr, "FAIL triangle-count n=%d: %s\n", ntris,
                    cr_backend_last_error(backend));
            ok = 0;
            break;
        }
        printf("PASS triangle-count     n=%d\n", ntris);
    }

    cr_fb_clear(&metal, (CrRgba){1, 2, 3, 255});
    if (!cr_backend_frame_begin(backend, &metal) ||
        cr_backend_raster(backend, &metal, tris, MAX_TRIS + 1, &shade) ||
        strstr(cr_backend_last_error(backend), "exceeds") == NULL ||
        !cr_backend_frame_end(backend, &metal)) {
        fprintf(stderr, "FAIL triangle-cap error path: %s\n",
                cr_backend_last_error(backend));
        ok = 0;
    } else {
        puts("PASS triangle-cap error path");
    }

    if (cr_backend_frame_begin(backend, &wrong) ||
        strstr(cr_backend_last_error(backend), "does not match") == NULL) {
        fprintf(stderr, "FAIL framebuffer-begin error path: %s\n",
                cr_backend_last_error(backend));
        ok = 0;
    } else if (!cr_backend_frame_begin(backend, &metal) ||
               cr_backend_frame_end(backend, &wrong) ||
               !cr_backend_frame_end(backend, &metal)) {
        fprintf(stderr, "FAIL framebuffer-end error/recovery path: %s\n",
                cr_backend_last_error(backend));
        ok = 0;
    } else {
        puts("PASS framebuffer mismatch error paths");
    }

    {
        CrRgba *saved_color = metal.color;
        if (!cr_backend_frame_begin(backend, &metal)) {
            fprintf(stderr, "FAIL null-storage frame-end setup: %s\n",
                    cr_backend_last_error(backend));
            ok = 0;
        } else {
            metal.color = NULL;
            if (cr_backend_frame_end(backend, &metal) ||
                strstr(cr_backend_last_error(backend), "framebuffer") == NULL) {
                fprintf(stderr, "FAIL null-storage frame-end error path: %s\n",
                        cr_backend_last_error(backend));
                ok = 0;
            }
            metal.color = saved_color;
            if (!cr_backend_frame_end(backend, &metal)) {
                fprintf(stderr, "FAIL null-storage frame-end recovery: %s\n",
                        cr_backend_last_error(backend));
                ok = 0;
            } else {
                puts("PASS null-storage frame-end error/recovery path");
            }
        }
    }

    cr_backend_close(backend); cr_fb_free(&cpu); cr_fb_free(&metal);
    cr_fb_free(&wrong);
    return ok;
}

static int run_size_error_paths(void) {
    CrFramebuffer fb = {123, 456, (CrRgba *)(uintptr_t)1,
                        (float *)(uintptr_t)1};
    CrRasterBackend *backend = NULL;
    char error[512] = {0};
    int ok = 1;
    if (cr_fb_alloc(&fb, INT_MAX, INT_MAX) || fb.w || fb.h || fb.color ||
        fb.depth) {
        fprintf(stderr, "FAIL oversized CPU framebuffer was not rejected safely\n");
        ok = 0;
    } else {
        puts("PASS oversized CPU framebuffer error path");
    }
    if (cr_backend_open(&backend, GM_BACKEND_CPU, INT_MAX, INT_MAX, 1,
                        error, sizeof error) || backend ||
        strstr(error, "exceeds") == NULL || strstr(error, "cap") == NULL) {
        fprintf(stderr, "FAIL oversized backend error path: %s\n", error);
        cr_backend_close(backend);
        ok = 0;
    } else {
        puts("PASS oversized backend error path");
    }
    error[0] = 0;
    if (cr_backend_open(&backend, GM_BACKEND_METAL, 1, 1, INT_MAX,
                        error, sizeof error) || backend ||
        strstr(error, "budget") == NULL) {
        fprintf(stderr, "FAIL Metal persistent-budget error path: %s\n", error);
        cr_backend_close(backend);
        backend = NULL;
        ok = 0;
    } else {
        puts("PASS Metal persistent-budget error path");
    }
    {
        CrFramebuffer missing = {1, 1, NULL, NULL};
        CrShadeCtx shade = {0};
        if (!cr_backend_open(&backend, GM_BACKEND_CPU, 1, 1, 1,
                             error, sizeof error)) {
            fprintf(stderr, "FAIL null-storage backend setup: %s\n", error);
            ok = 0;
        } else if (cr_backend_raster(backend, &missing, NULL, 0, &shade) ||
                   strstr(cr_backend_last_error(backend), "invalid raster") == NULL) {
            fprintf(stderr, "FAIL null-storage raster error path: %s\n",
                    cr_backend_last_error(backend));
            ok = 0;
        } else {
            puts("PASS null-storage raster error path");
        }
        cr_backend_close(backend);
        backend = NULL;
    }
    return ok;
}

int main(void) {
    uint8_t atlas[ATLAS_W * ATLAS_H * 4];
    uint8_t cut[ATLAS_W * ATLAS_H * 4];
    uint8_t trans[ATLAS_W * ATLAS_H * 4];
    uint8_t m1[8 * 8 * 4], m2[4 * 4 * 4], m3[2 * 2 * 4], m4[4];
    CrRgba lightmap[256];
    scn_fill_atlas(atlas); scn_fill_cutout(cut); scn_fill_translucent(trans);
    scn_build_mips(atlas, m1, m2, m3, m4);
    for (int i = 0; i < 256; ++i)
        lightmap[i] = (CrRgba){(u8)i, (u8)(255 - i), (u8)(i ^ 0x55), 255};
    CrTexture solid = {ATLAS_W, ATLAS_H, (const CrRgba *)atlas, 16, 0, {0}, {0}, {0}};
    CrTexture cutout = {ATLAS_W, ATLAS_H, (const CrRgba *)cut, 16, 0, {0}, {0}, {0}};
    CrTexture translucent = {ATLAS_W, ATLAS_H, (const CrRgba *)trans, 16, 0, {0}, {0}, {0}};
    CrTexture mip = {ATLAS_W, ATLAS_H, (const CrRgba *)atlas, 16, 4,
        {(const CrRgba *)m1, (const CrRgba *)m2, (const CrRgba *)m3, (const CrRgba *)m4},
        {8, 4, 2, 1}, {8, 4, 2, 1}};
    CrRgba fog = {135, 206, 235, 255};
    Case cases[] = {
        {"solid", SCN_VERTS, SCN_NVERTS,
            {.atlas=&solid,.fog_color=fog,.layer=CR_LAYER_SOLID}},
        {"cutout", SCN_CUT_VERTS, SCN_CUT_NVERTS,
            {.atlas=&cutout,.fog_color=fog,.alpha_test=1,.layer=CR_LAYER_CUTOUT}},
        {"src-over", SCN_TRANS_VERTS, SCN_TRANS_NVERTS,
            {.atlas=&translucent,.fog_color=fog,.layer=CR_LAYER_TRANSLUCENT,.blend=1}},
        {"multiply", SCN_TRANS_VERTS, SCN_TRANS_NVERTS,
            {.atlas=&translucent,.fog_color=fog,.layer=CR_LAYER_TRANSLUCENT,.blend=2}},
        {"additive", SCN_TRANS_VERTS, SCN_TRANS_NVERTS,
            {.atlas=&translucent,.fog_color=fog,.layer=CR_LAYER_TRANSLUCENT,.blend=3}},
        {"src-over-depth", SCN_TRANS_VERTS, SCN_TRANS_NVERTS,
            {.atlas=&translucent,.fog_color=fog,.layer=CR_LAYER_TRANSLUCENT,.blend=4}},
        {"mips", SCN_MIP_VERTS, SCN_MIP_NVERTS,
            {.atlas=&mip,.fog_color=fog,.layer=CR_LAYER_SOLID,.use_mips=1}},
        {"linear-fog", SCN_VERTS, SCN_NVERTS,
            {.atlas=&solid,.fog_color=fog,.fog_start=0,.fog_end=10,
             .enable_fog=1,.layer=CR_LAYER_SOLID}},
        {"exp-fog", SCN_VERTS, SCN_NVERTS,
            {.atlas=&solid,.fog_color=fog,.enable_fog=1,.layer=CR_LAYER_SOLID,
             .fog_exp_density=.1f}},
        {"lightmap", SCN_VERTS, SCN_NVERTS,
            {.atlas=&solid,.fog_color=fog,.layer=CR_LAYER_SOLID,.lightmap=lightmap}},
        {"cover-eps", SCN_VERTS, SCN_NVERTS,
            {.atlas=&solid,.fog_color=fog,.layer=CR_LAYER_SOLID,
             .depth_lequal=1,.cover_eps=.15f,.sample_mode=1}},
        {"color-trunc", SCN_VERTS, SCN_NVERTS,
            {.atlas=&solid,.fog_color=fog,.layer=CR_LAYER_SOLID,
             .color_trunc=1,.sample_mode=1}},
        {"alpha-ref", SCN_CUT_VERTS, SCN_CUT_NVERTS,
            {.atlas=&cutout,.fog_color=fog,.alpha_test=1,.alpha_ref=.1f,
             .layer=CR_LAYER_CUTOUT}},
        {"untextured-add", SCN_TRANS_VERTS, SCN_TRANS_NVERTS,
            {.atlas=&solid,.fog_color=fog,.layer=CR_LAYER_TRANSLUCENT,
             .blend=3,.untextured=1}},
        {"degenerate-offscreen", EDGE_VERTS,
            (int)(sizeof EDGE_VERTS / sizeof EDGE_VERTS[0]),
            {.atlas=&solid,.fog_color=fog,.layer=CR_LAYER_SOLID}},
    };
    int ncases = (int)(sizeof cases / sizeof cases[0]);
    int ok = run_size(17, 19, cases, ncases);
    ok &= run_size(257, 259, cases, ncases);
    ok &= run_count_matrix(&solid, fog);
    ok &= run_size_error_paths();
    puts(ok ? "METAL PARITY PASS" : "METAL PARITY FAIL");
    return ok ? 0 : 1;
}
