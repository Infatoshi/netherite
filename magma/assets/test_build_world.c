#define _POSIX_C_SOURCE 200809L
#include "build.h"
#include "image.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int g_fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        g_fails++;
    }
}

static void expect_i(int got, int want, const char *msg) {
    if (got != want) {
        fprintf(stderr, "FAIL: %s (got %d want %d)\n", msg, got, want);
        g_fails++;
    }
}

static char *read_file(const char *path, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    long n;
    char *buf;
    if (!fp)
        return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    n = ftell(fp);
    if (n < 0) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    buf = (char *)malloc((size_t)n + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, fp) != (size_t)n) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    buf[n] = '\0';
    if (out_len)
        *out_len = (size_t)n;
    return buf;
}

static int find_define_int(const char *text, const char *name, int *out) {
    char key[128];
    const char *p;
    snprintf(key, sizeof(key), "#define %s ", name);
    p = strstr(text, key);
    if (!p)
        return -1;
    p += strlen(key);
    *out = (int)strtol(p, NULL, 10);
    return 0;
}

static const char *find_sprite_rect(const char *text, const char *name, int *x0, int *y0,
                                    int *x1, int *y1) {
    char key[160];
    const char *p;
    snprintf(key, sizeof(key), "{ \"%s\", ", name);
    p = strstr(text, key);
    if (!p)
        return NULL;
    p += strlen(key);
    if (sscanf(p, "%d, %d, %d, %d", x0, y0, x1, y1) != 4)
        return NULL;
    return p;
}

static int parse_u8_array_after(const char *text, const char *marker, unsigned char *out,
                                size_t want) {
    const char *p = strstr(text, marker);
    size_t n = 0;
    if (!p)
        return -1;
    p = strchr(p, '{');
    if (!p)
        return -1;
    p++;
    while (*p && n < want) {
        char *ep = NULL;
        long v;
        while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == ',')
            p++;
        if (*p == '}')
            break;
        v = strtol(p, &ep, 10);
        if (ep == p)
            return -1;
        out[n++] = (unsigned char)v;
        p = ep;
    }
    return n == want ? 0 : -1;
}

static int count_sprite_macros(const char *text) {
    int n = 0;
    const char *p = text;
    while ((p = strstr(p, "#define CR_SPRITE_")) != NULL) {
        n++;
        p += 17;
    }
    return n;
}

static void check_loading(AssetJar *jar, const char *dir) {
    char path[512];
    char *hdr;
    unsigned char px[1024];
    AssetImage img;
    int i;

    snprintf(path, sizeof(path), "%s/loading_bg.h", dir);
    hdr = read_file(path, NULL);
    expect(hdr != NULL, "loading_bg.h exists");
    if (!hdr)
        return;
    expect(strstr(hdr, "make -C magma assets") != NULL, "loading comment names make");
    expect(strstr(hdr, "CR_LOADING_BG_RGBA[1024]") != NULL, "loading array size 1024");
    expect(parse_u8_array_after(hdr, "CR_LOADING_BG_RGBA[1024]", px, 1024) == 0,
           "parse loading pixels");
    expect(asset_image_load(jar, "assets/minecraft/textures/gui/options_background.png",
                            &img) == 0,
           "load options_background");
    expect_i(img.w, 16, "loading w");
    expect_i(img.h, 16, "loading h");
    for (i = 0; i < 1024; i++) {
        if (px[i] != img.rgba[i]) {
            fprintf(stderr, "FAIL: loading pixel mismatch at %d (%u vs %u)\n", i, px[i],
                    img.rgba[i]);
            g_fails++;
            break;
        }
    }
    asset_image_free(&img);
    free(hdr);
}

static void check_underwater(AssetJar *jar, const char *dir) {
    char path[512];
    char *hdr;
    int w = 0, h = 0;
    unsigned char *px;
    AssetImage img;
    size_t n;
    size_t i;

    snprintf(path, sizeof(path), "%s/underwater_tex.h", dir);
    hdr = read_file(path, NULL);
    expect(hdr != NULL, "underwater_tex.h exists");
    if (!hdr)
        return;
    expect(find_define_int(hdr, "CR_UNDERWATER_TEX_W", &w) == 0, "underwater W macro");
    expect(find_define_int(hdr, "CR_UNDERWATER_TEX_H", &h) == 0, "underwater H macro");
    expect(asset_image_load(jar, "assets/minecraft/textures/misc/underwater.png", &img) == 0,
           "load underwater");
    expect_i(w, img.w, "underwater W");
    expect_i(h, img.h, "underwater H");
    n = (size_t)w * (size_t)h * 4u;
    px = (unsigned char *)malloc(n);
    expect(px != NULL, "alloc underwater");
    if (px) {
        expect(parse_u8_array_after(hdr, "CR_UNDERWATER_TEX[]", px, n) == 0,
               "parse underwater pixels");
        for (i = 0; i < n; i++) {
            if (px[i] != img.rgba[i]) {
                fprintf(stderr, "FAIL: underwater pixel %zu\n", i);
                g_fails++;
                break;
            }
        }
        free(px);
    }
    asset_image_free(&img);
    free(hdr);
}

static void check_portal(AssetJar *jar, const char *dir) {
    char path[512];
    char *hdr;
    int frames = 0, tw = 0, th = 0;
    AssetImage img;
    int f;

    snprintf(path, sizeof(path), "%s/portal_tex.h", dir);
    hdr = read_file(path, NULL);
    expect(hdr != NULL, "portal_tex.h exists");
    if (!hdr)
        return;
    expect(find_define_int(hdr, "CR_PORTAL_TEX_FRAMES", &frames) == 0, "portal frames");
    expect(find_define_int(hdr, "CR_PORTAL_TEX_W", &tw) == 0, "portal W");
    expect(find_define_int(hdr, "CR_PORTAL_TEX_H", &th) == 0, "portal H");
    expect(asset_image_load(jar, "assets/minecraft/textures/blocks/portal.png", &img) == 0,
           "load portal");
    expect_i(tw, 16, "portal tex w");
    expect_i(th, 16, "portal tex h");
    expect_i(frames, img.h / img.w, "portal frame count");
    expect_i(img.w, 16, "portal source w");
    /* Check first and last frame bytes against crop. */
    for (f = 0; f < frames; f += frames > 1 ? frames - 1 : 1) {
        AssetImage tile;
        unsigned char px[1024];
        char marker[64];
        const char *p;
        int k;
        expect(asset_image_crop(&img, 0, f * 16, 16, 16, &tile) == 0, "crop portal frame");
        /* Find f-th `{` inside CR_PORTAL_TEX */
        p = strstr(hdr, "CR_PORTAL_TEX[CR_PORTAL_TEX_FRAMES][1024]");
        expect(p != NULL, "portal array marker");
        if (!p) {
            asset_image_free(&tile);
            break;
        }
        p = strchr(p, '{');
        for (k = 0; k <= f && p; k++) {
            p = strchr(p + 1, '{');
        }
        expect(p != NULL, "portal frame brace");
        if (p) {
            snprintf(marker, sizeof(marker), "frame%d", f);
            /* parse from this brace */
            {
                const char *q = p + 1;
                size_t n = 0;
                while (*q && n < 1024) {
                    char *ep = NULL;
                    long v;
                    while (*q == ' ' || *q == '\n' || *q == '\r' || *q == '\t' || *q == ',')
                        q++;
                    if (*q == '}')
                        break;
                    v = strtol(q, &ep, 10);
                    if (ep == q)
                        break;
                    px[n++] = (unsigned char)v;
                    q = ep;
                }
                expect_i((int)n, 1024, "portal frame byte count");
                for (k = 0; k < 1024; k++) {
                    if (px[k] != tile.rgba[k]) {
                        fprintf(stderr, "FAIL: portal frame %d byte %d\n", f, k);
                        g_fails++;
                        break;
                    }
                }
            }
        }
        asset_image_free(&tile);
        if (frames <= 1)
            break;
    }
    asset_image_free(&img);
    free(hdr);
}

static void check_colormap(AssetJar *jar, const char *dir) {
    char path[512];
    char *hdr;
    int dim = 0;
    AssetImage grass, foliage;
    const char *p;
    int i, mismatches = 0;

    snprintf(path, sizeof(path), "%s/colormap_gen.h", dir);
    hdr = read_file(path, NULL);
    expect(hdr != NULL, "colormap_gen.h exists");
    if (!hdr)
        return;
    expect(find_define_int(hdr, "CR_COLORMAP_DIM", &dim) == 0, "colormap dim macro");
    expect_i(dim, 256, "colormap dim");
    expect(asset_image_load(jar, "assets/minecraft/textures/colormap/grass.png", &grass) == 0,
           "load grass colormap");
    expect(asset_image_load(jar, "assets/minecraft/textures/colormap/foliage.png",
                            &foliage) == 0,
           "load foliage colormap");
    expect_i(grass.w, 256, "grass w");
    expect_i(grass.h, 256, "grass h");
    p = strstr(hdr, "CR_GRASS_COLORMAP[65536]");
    expect(p != NULL, "grass table");
    if (p) {
        p = strchr(p, '{');
        expect(p != NULL, "grass brace");
        if (p) {
            p++;
            for (i = 0; i < 65536; i++) {
                char *ep = NULL;
                unsigned long v;
                unsigned int want;
                while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == ',')
                    p++;
                v = strtoul(p, &ep, 0);
                if (ep == p) {
                    fprintf(stderr, "FAIL: grass parse at %d\n", i);
                    g_fails++;
                    break;
                }
                want = ((unsigned int)grass.rgba[i * 4] << 16) |
                       ((unsigned int)grass.rgba[i * 4 + 1] << 8) |
                       (unsigned int)grass.rgba[i * 4 + 2];
                if ((unsigned int)v != want) {
                    if (mismatches < 3)
                        fprintf(stderr, "FAIL: grass[%d] 0x%06lx vs 0x%06x\n", i, v, want);
                    mismatches++;
                }
                p = ep;
            }
            expect_i(mismatches, 0, "grass pixel match");
        }
    }
    /* sample foliage corners */
    p = strstr(hdr, "CR_FOLIAGE_COLORMAP[65536]");
    expect(p != NULL, "foliage table");
    if (p) {
        unsigned int samples[4];
        int idx[4] = {0, 255, 256 * 255, 65535};
        int si;
        p = strchr(p, '{');
        p++;
        for (i = 0; i < 65536; i++) {
            char *ep = NULL;
            unsigned long v;
            while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == ',')
                p++;
            v = strtoul(p, &ep, 0);
            for (si = 0; si < 4; si++)
                if (i == idx[si])
                    samples[si] = (unsigned int)v;
            p = ep;
        }
        for (si = 0; si < 4; si++) {
            int ix = idx[si];
            unsigned int want = ((unsigned int)foliage.rgba[ix * 4] << 16) |
                                ((unsigned int)foliage.rgba[ix * 4 + 1] << 8) |
                                (unsigned int)foliage.rgba[ix * 4 + 2];
            if (samples[si] != want) {
                fprintf(stderr, "FAIL: foliage sample %d\n", ix);
                g_fails++;
            }
        }
    }
    asset_image_free(&grass);
    asset_image_free(&foliage);
    free(hdr);
}

static void check_sky(AssetJar *jar, const char *dir) {
    char path[512];
    char *hdr;
    int sw = 0, sh = 0, mw = 0, mh = 0, cw = 0, ch = 0, ew = 0, eh = 0;
    AssetImage sun, moon_strip, moon, clouds, end_sky;
    unsigned char px[4096];
    int i;

    snprintf(path, sizeof(path), "%s/sky_atlas.h", dir);
    hdr = read_file(path, NULL);
    expect(hdr != NULL, "sky_atlas.h exists");
    if (!hdr)
        return;
    find_define_int(hdr, "CR_SUN_W", &sw);
    find_define_int(hdr, "CR_SUN_H", &sh);
    find_define_int(hdr, "CR_MOON_W", &mw);
    find_define_int(hdr, "CR_MOON_H", &mh);
    find_define_int(hdr, "CR_CLOUDS_W", &cw);
    find_define_int(hdr, "CR_CLOUDS_H", &ch);
    find_define_int(hdr, "CR_END_SKY_W", &ew);
    find_define_int(hdr, "CR_END_SKY_H", &eh);
    expect(asset_image_load(jar, "assets/minecraft/textures/environment/sun.png", &sun) == 0,
           "sun load");
    expect(asset_image_load(jar, "assets/minecraft/textures/environment/moon_phases.png",
                            &moon_strip) == 0,
           "moon strip load");
    expect(asset_image_load(jar, "assets/minecraft/textures/environment/clouds.png",
                            &clouds) == 0,
           "clouds load");
    expect(asset_image_load(jar, "assets/minecraft/textures/environment/end_sky.png",
                            &end_sky) == 0,
           "end_sky load");
    expect(asset_image_crop(&moon_strip, 0, 0, moon_strip.w / 4, moon_strip.h / 2, &moon) ==
               0,
           "moon crop");
    expect_i(sw, sun.w, "sun W");
    expect_i(sh, sun.h, "sun H");
    expect_i(mw, moon.w, "moon W");
    expect_i(mh, moon.h, "moon H");
    expect_i(cw, clouds.w, "clouds W");
    expect_i(ch, clouds.h, "clouds H");
    expect_i(ew, end_sky.w, "end_sky W");
    expect_i(eh, end_sky.h, "end_sky H");
    expect(parse_u8_array_after(hdr, "CR_SUN_RGBA[", px, (size_t)sw * sh * 4) == 0,
           "parse sun");
    for (i = 0; i < sw * sh * 4; i++) {
        if (px[i] != sun.rgba[i]) {
            fprintf(stderr, "FAIL: sun px %d\n", i);
            g_fails++;
            break;
        }
    }
    expect(parse_u8_array_after(hdr, "CR_MOON_RGBA[", px, (size_t)mw * mh * 4) == 0,
           "parse moon");
    for (i = 0; i < mw * mh * 4; i++) {
        if (px[i] != moon.rgba[i]) {
            fprintf(stderr, "FAIL: moon px %d\n", i);
            g_fails++;
            break;
        }
    }
    /* sample clouds corners */
    {
        size_t n = (size_t)cw * (size_t)ch * 4u;
        unsigned char *cpx = (unsigned char *)malloc(n);
        expect(cpx != NULL, "clouds alloc");
        if (cpx) {
            expect(parse_u8_array_after(hdr, "CR_CLOUDS_RGBA[", cpx, n) == 0, "parse clouds");
            expect(memcmp(cpx, clouds.rgba, n) == 0, "clouds bytes");
            free(cpx);
        }
    }
    {
        size_t n = (size_t)ew * (size_t)eh * 4u;
        unsigned char *epx = (unsigned char *)malloc(n);
        expect(epx != NULL, "end_sky alloc");
        if (epx) {
            expect(parse_u8_array_after(hdr, "CR_END_SKY_RGBA[", epx, n) == 0, "parse end_sky");
            expect(memcmp(epx, end_sky.rgba, n) == 0, "end_sky bytes");
            free(epx);
        }
    }
    asset_image_free(&sun);
    asset_image_free(&moon_strip);
    asset_image_free(&moon);
    asset_image_free(&clouds);
    asset_image_free(&end_sky);
    free(hdr);
}

static int is_flow_name(const char *n) {
    return strcmp(n, "water_flow") == 0 || strcmp(n, "lava_flow") == 0;
}

static void check_atlas(AssetJar *jar, const char *dir) {
    char path[512];
    char *hdr;
    int aw = 0, ah = 0, tile = 0, count = 0;
    int n_macros;
    int x0, y0, x1, y1;
    AssetImage stone, end_portal_src, end_portal_16;
    unsigned char *atlas_px;
    size_t atlas_n;
    int i;

    snprintf(path, sizeof(path), "%s/atlas_gen.h", dir);
    hdr = read_file(path, NULL);
    expect(hdr != NULL, "atlas_gen.h exists");
    if (!hdr)
        return;
    expect(find_define_int(hdr, "CR_ATLAS_W", &aw) == 0, "atlas W");
    expect(find_define_int(hdr, "CR_ATLAS_H", &ah) == 0, "atlas H");
    expect(find_define_int(hdr, "CR_ATLAS_TILE", &tile) == 0, "atlas tile");
    expect(find_define_int(hdr, "CR_ATLAS_SPRITE_COUNT", &count) == 0, "atlas count");
    expect_i(aw, 256, "atlas width");
    expect_i(ah, 256, "atlas height");
    expect_i(tile, 16, "atlas tile size");
    expect_i(count, 164, "atlas sprite count");
    n_macros = count_sprite_macros(hdr);
    expect_i(n_macros, 164, "CR_SPRITE macro count");

    expect(find_sprite_rect(hdr, "water_flow", &x0, &y0, &x1, &y1) != NULL, "water_flow rect");
    expect_i(x0, 0, "water_flow x0");
    expect_i(y0, 224, "water_flow y0");
    expect_i(x1, 32, "water_flow x1");
    expect_i(y1, 256, "water_flow y1");
    expect(find_sprite_rect(hdr, "lava_flow", &x0, &y0, &x1, &y1) != NULL, "lava_flow rect");
    expect_i(x0, 32, "lava_flow x0");
    expect_i(y0, 224, "lava_flow y0");
    expect_i(x1, 64, "lava_flow x1");
    expect_i(y1, 256, "lava_flow y1");

    expect(find_sprite_rect(hdr, "end_portal", &x0, &y0, &x1, &y1) != NULL, "end_portal rect");
    expect_i(x1 - x0, 16, "end_portal w");
    expect_i(y1 - y0, 16, "end_portal h");

    /* stone pixels vs jar first frame */
    expect(asset_image_load(jar, "assets/minecraft/textures/blocks/stone.png", &stone) == 0,
           "load stone");
    expect(find_sprite_rect(hdr, "stone", &x0, &y0, &x1, &y1) != NULL, "stone rect");
    atlas_n = (size_t)aw * (size_t)ah * 4u;
    atlas_px = (unsigned char *)malloc(atlas_n);
    expect(atlas_px != NULL, "atlas px alloc");
    if (atlas_px &&
        parse_u8_array_after(hdr, "CR_ATLAS_RGBA[", atlas_px, atlas_n) == 0) {
        int mismatch = 0;
        for (i = 0; i < 16 * 16; i++) {
            int sx = i % 16, sy = i / 16;
            size_t ai = ((size_t)(y0 + sy) * (size_t)aw + (size_t)(x0 + sx)) * 4u;
            if (memcmp(atlas_px + ai, stone.rgba + (size_t)i * 4u, 4) != 0)
                mismatch++;
        }
        expect_i(mismatch, 0, "stone texels in atlas");

        /* end_portal: entity 256 -> nearest 16 */
        expect(asset_image_load(jar, "assets/minecraft/textures/entity/end_portal.png",
                                &end_portal_src) == 0,
               "load end_portal entity");
        expect(asset_image_resize_nearest(&end_portal_src, 16, 16, &end_portal_16) == 0,
               "resize end_portal");
        expect(find_sprite_rect(hdr, "end_portal", &x0, &y0, &x1, &y1) != NULL,
               "end_portal rect2");
        mismatch = 0;
        for (i = 0; i < 16 * 16; i++) {
            int sx = i % 16, sy = i / 16;
            size_t ai = ((size_t)(y0 + sy) * (size_t)aw + (size_t)(x0 + sx)) * 4u;
            if (memcmp(atlas_px + ai, end_portal_16.rgba + (size_t)i * 4u, 4) != 0)
                mismatch++;
        }
        expect_i(mismatch, 0, "end_portal texels in atlas");
        asset_image_free(&end_portal_src);
        asset_image_free(&end_portal_16);

        /* flow first frame 32x32 at parked coords */
        {
            AssetImage wf, w0;
            expect(asset_image_load(jar, "assets/minecraft/textures/blocks/water_flow.png",
                                    &wf) == 0,
                   "load water_flow");
            expect(asset_image_crop(&wf, 0, 0, 32, 32, &w0) == 0, "crop water_flow f0");
            expect(find_sprite_rect(hdr, "water_flow", &x0, &y0, &x1, &y1) != NULL,
                   "wf rect");
            mismatch = 0;
            for (i = 0; i < 32 * 32; i++) {
                int sx = i % 32, sy = i / 32;
                size_t ai = ((size_t)(y0 + sy) * (size_t)aw + (size_t)(x0 + sx)) * 4u;
                if (memcmp(atlas_px + ai, w0.rgba + (size_t)i * 4u, 4) != 0)
                    mismatch++;
            }
            expect_i(mismatch, 0, "water_flow frame0 in atlas");
            asset_image_free(&w0);
            asset_image_free(&wf);
        }
        free(atlas_px);
    }
    asset_image_free(&stone);
    free(hdr);
    (void)is_flow_name;
}

static void check_animations(AssetJar *jar, const char *dir) {
    char path[512];
    char *hdr;
    int frames, ft, seq_len, w, h;
    char name[64];
    static const char *specs[] = {"WATER_STILL",  "WATER_FLOW",   "LAVA_STILL",
                                  "LAVA_FLOW",    "FIRE_LAYER_0", "FIRE_LAYER_1"};
    static const char *files[] = {"water_still",  "water_flow",   "lava_still",
                                  "lava_flow",    "fire_layer_0", "fire_layer_1"};
    int s;

    snprintf(path, sizeof(path), "%s/water_frames.h", dir);
    hdr = read_file(path, NULL);
    expect(hdr != NULL, "water_frames.h exists");
    if (!hdr)
        return;

    for (s = 0; s < 6; s++) {
        AssetImage strip;
        char member[160];
        char key[80];
        int count;
        int want_size;

        snprintf(name, sizeof(name), "%s", specs[s]);
        snprintf(key, sizeof(key), "CR_%s_FRAMES", name);
        expect(find_define_int(hdr, key, &frames) == 0, key);
        snprintf(key, sizeof(key), "CR_%s_FRAMETIME", name);
        expect(find_define_int(hdr, key, &ft) == 0, key);
        snprintf(key, sizeof(key), "CR_%s_SEQUENCE_LEN", name);
        expect(find_define_int(hdr, key, &seq_len) == 0, key);
        snprintf(key, sizeof(key), "CR_%s_W", name);
        expect(find_define_int(hdr, key, &w) == 0, key);
        snprintf(key, sizeof(key), "CR_%s_H", name);
        expect(find_define_int(hdr, key, &h) == 0, key);

        snprintf(member, sizeof(member), "assets/minecraft/textures/blocks/%s.png",
                 files[s]);
        expect(asset_image_load(jar, member, &strip) == 0, member);
        count = strip.h / strip.w;
        expect_i(frames, count, files[s]);
        want_size = (strcmp(files[s], "water_flow") == 0 ||
                     strcmp(files[s], "lava_flow") == 0)
                        ? 32
                        : 16;
        expect_i(w, want_size, "anim W");
        expect_i(h, want_size, "anim H");

        /* known meta */
        if (strcmp(files[s], "water_still") == 0) {
            expect_i(ft, 2, "water_still frametime");
            expect_i(seq_len, 32, "water_still seq");
        } else if (strcmp(files[s], "water_flow") == 0) {
            expect_i(ft, 1, "water_flow frametime");
            expect_i(seq_len, 32, "water_flow seq");
        } else if (strcmp(files[s], "lava_still") == 0) {
            expect_i(ft, 2, "lava_still frametime");
            expect_i(seq_len, 38, "lava_still seq");
            expect_i(frames, 20, "lava_still frames");
        } else if (strcmp(files[s], "lava_flow") == 0) {
            expect_i(ft, 3, "lava_flow frametime");
            expect_i(seq_len, 16, "lava_flow seq");
        } else if (strcmp(files[s], "fire_layer_0") == 0) {
            expect_i(ft, 1, "fire0 frametime");
            expect_i(seq_len, 32, "fire0 seq");
        } else if (strcmp(files[s], "fire_layer_1") == 0) {
            expect_i(ft, 1, "fire1 frametime");
            expect_i(seq_len, 32, "fire1 seq");
        }

        /* parse sequence */
        {
            char seq_marker[96];
            const char *p;
            int seq[64];
            int n = 0;
            snprintf(seq_marker, sizeof(seq_marker), "CR_%s_SEQUENCE[%d]", name, seq_len);
            p = strstr(hdr, seq_marker);
            expect(p != NULL, seq_marker);
            if (p) {
                p = strchr(p, '{');
                p++;
                while (*p && *p != '}' && n < 64) {
                    char *ep = NULL;
                    long v;
                    while (*p == ' ' || *p == ',' || *p == '\n')
                        p++;
                    if (*p == '}')
                        break;
                    v = strtol(p, &ep, 10);
                    seq[n++] = (int)v;
                    p = ep;
                }
                expect_i(n, seq_len, "sequence length parsed");
                if (strcmp(files[s], "lava_still") == 0 && n == 38) {
                    expect_i(seq[0], 0, "lava_still seq0");
                    expect_i(seq[19], 19, "lava_still seq19");
                    expect_i(seq[20], 18, "lava_still seq20");
                    expect_i(seq[37], 1, "lava_still seq37");
                }
                if (strcmp(files[s], "fire_layer_0") == 0 && n == 32) {
                    expect_i(seq[0], 16, "fire0 seq0");
                    expect_i(seq[16], 0, "fire0 seq16");
                }
            }
        }

        /* check frame 0 pixels */
        {
            AssetImage tile, resized;
            unsigned char *px;
            size_t nbytes;
            char rgba_marker[96];
            const char *p;
            memset(&resized, 0, sizeof(resized));
            expect(asset_image_crop(&strip, 0, 0, strip.w, strip.w, &tile) == 0,
                   "crop anim f0");
            if (strip.w != want_size) {
                expect(asset_image_resize_nearest(&tile, want_size, want_size, &resized) == 0,
                       "resize anim f0");
                asset_image_free(&tile);
                tile = resized;
            }
            nbytes = (size_t)want_size * (size_t)want_size * 4u;
            px = (unsigned char *)malloc(nbytes);
            snprintf(rgba_marker, sizeof(rgba_marker), "CR_%s_RGBA[", name);
            p = strstr(hdr, rgba_marker);
            expect(p != NULL, rgba_marker);
            if (p && px) {
                /* first frame brace after array `[` */
                p = strchr(p, '{'); /* outer */
                expect(p != NULL, "outer brace");
                if (p) {
                    p = strchr(p + 1, '{'); /* first frame */
                    expect(p != NULL, "frame0 brace");
                    if (p) {
                        const char *q = p + 1;
                        size_t n = 0;
                        while (*q && n < nbytes) {
                            char *ep = NULL;
                            long v;
                            while (*q == ' ' || *q == '\n' || *q == '\r' || *q == '\t' ||
                                   *q == ',')
                                q++;
                            if (*q == '}')
                                break;
                            v = strtol(q, &ep, 10);
                            if (ep == q)
                                break;
                            px[n++] = (unsigned char)v;
                            q = ep;
                        }
                        expect_i((int)n, (int)nbytes, "anim f0 byte count");
                        if (n == nbytes)
                            expect(memcmp(px, tile.rgba, nbytes) == 0, files[s]);
                    }
                }
            }
            free(px);
            asset_image_free(&tile);
        }
        asset_image_free(&strip);
    }
    free(hdr);
}

static void check_api_only(AssetJar *jar, const char *base) {
    char dir[512];
    char path[560];
    struct stat st;

    snprintf(dir, sizeof(dir), "%s/only_portal", base);
    mkdir(dir, 0700);
    expect(asset_build_world(jar, dir, "portal") == 0, "only portal");
    snprintf(path, sizeof(path), "%s/portal_tex.h", dir);
    expect(stat(path, &st) == 0, "only portal wrote portal_tex.h");
    snprintf(path, sizeof(path), "%s/atlas_gen.h", dir);
    expect(stat(path, &st) != 0, "only portal did not write atlas");
    expect(asset_build_world(jar, dir, "not_a_real_name") != 0, "unknown only fails");
}

int main(int argc, char **argv) {
    const char *jar_path;
    AssetJar *jar;
    const char *outdir;
    char cmd_note[64];

    if (argc != 3) {
        fprintf(stderr, "usage: %s <minecraft-1.11.2.jar> <out_dir>\n", argv[0]);
        return 2;
    }
    jar_path = argv[1];
    outdir = argv[2];
    jar = asset_jar_open(jar_path);
    if (!jar) {
        fprintf(stderr, "cannot open jar: %s\n", jar_path);
        return 2;
    }
    if (mkdir(outdir, 0700) != 0 && errno != EEXIST) {
        perror(outdir);
        asset_jar_close(jar);
        return 2;
    }
    fprintf(stderr, "play dir: %s\n", outdir);
    fprintf(stderr, "jar: %s\n", jar_path);

    expect(asset_build_world(jar, outdir, NULL) == 0, "asset_build_world all");

    check_loading(jar, outdir);
    check_underwater(jar, outdir);
    check_portal(jar, outdir);
    check_colormap(jar, outdir);
    check_sky(jar, outdir);
    check_atlas(jar, outdir);
    check_animations(jar, outdir);
    check_api_only(jar, outdir);

    /* image op smoke */
    {
        AssetImage a, b, c;
        expect(asset_image_new(&a, 4, 4) == 0, "image_new");
        a.rgba[0] = 1;
        a.rgba[1] = 2;
        a.rgba[2] = 3;
        a.rgba[3] = 4;
        expect(asset_image_crop(&a, 0, 0, 2, 2, &b) == 0, "crop");
        expect_i(b.w, 2, "crop w");
        expect(asset_image_resize_nearest(&a, 2, 2, &c) == 0, "resize");
        expect_i(c.w, 2, "resize w");
        asset_image_free(&a);
        asset_image_free(&b);
        asset_image_free(&c);
    }

    asset_jar_close(jar);
    snprintf(cmd_note, sizeof(cmd_note), "%d", g_fails);
    if (g_fails) {
        fprintf(stderr, "%d failure(s)\n", g_fails);
        return 1;
    }
    fprintf(stderr, "OK\n");
    return 0;
}
