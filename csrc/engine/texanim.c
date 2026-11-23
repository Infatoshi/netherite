#define _POSIX_C_SOURCE 200809L
#include "texanim.h"
#include "raster_obs.h"
#include "smath.h"
#include "tape.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const double PI2 = 3.141592653589793 * 2.0;   /* Math.PI * 2D */

static char *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = n >= 0 ? malloc((size_t)n + 1) : NULL;
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    fclose(f);
    b[n] = 0;
    if (len) *len = (size_t)n;
    return b;
}

static int jint(const struct jval *v)
{
    int64_t n = 0;
    json_int(v, &n);
    return (int)n;
}

static double jdbl(const struct jval *v)
{
    uint64_t b = 0;
    double d = 0.0;
    if (json_double(v, &b)) memcpy(&d, &b, 8);
    return d;
}

int texanim_load(struct texanim *a, const char *dir)
{
    memset(a, 0, sizeof *a);
    char p[1200];
    size_t len = 0;
    snprintf(p, sizeof p, "%s/anim.rgba", dir);
    a->rgba = (unsigned char *)slurp(p, &len);
    snprintf(p, sizeof p, "%s/anim.json", dir);
    char *text = slurp(p, NULL);
    if (!a->rgba || !text) { free(text); texanim_free(a); return -1; }
    struct jval *root = json_parse(text);
    const struct jval *maps = json_get(root, "maps");
    int bad = !maps || json_len(maps) != TEXANIM_MAPS;
    for (int m = 0; !bad && m < TEXANIM_MAPS; ++m) {
        const struct jval *list = json_at(maps, m);
        a->n[m] = json_len(list);
        if (a->n[m] > TEXANIM_MAX_SPRITES) { bad = 1; break; }
        for (int i = 0; i < a->n[m]; ++i) {
            const struct jval *j = json_at(list, i);
            struct texanim_sprite *s = &a->s[m][i];
            const char *name = json_str(json_get(j, "n")), *kind = json_str(json_get(j, "kind"));
            snprintf(s->name, sizeof s->name, "%s", name ? name : "");
            s->kind = kind && !strcmp(kind, "clock") ? TEXANIM_CLOCK
                    : kind && !strcmp(kind, "compass") ? TEXANIM_COMPASS : TEXANIM_SPRITE;
            s->x = jint(json_get(j, "x")); s->y = jint(json_get(j, "y"));
            s->w = jint(json_get(j, "w")); s->h = jint(json_get(j, "h"));
            s->frametime = jint(json_get(j, "frametime"));
            const struct jval *fl = json_get(j, "frames"), *data = json_get(j, "data");
            s->nframes = json_len(fl);
            s->ndata = json_len(data);
            if (s->nframes > TEXANIM_MAX_FRAMES || s->ndata > TEXANIM_MAX_FRAMES) { bad = 1; break; }
            for (int k = 0; k < s->nframes; ++k) {
                s->index[k] = (int16_t)jint(json_at(json_at(fl, k), 0));
                s->time[k] = (int16_t)jint(json_at(json_at(fl, k), 1));
            }
            size_t frame = (size_t)s->w * s->h * 4;
            for (int k = 0; k < s->ndata; ++k) {
                int64_t off = -1;
                json_int(json_at(data, k), &off);
                if (off < 0) continue;
                if ((size_t)off + frame > len) { bad = 1; break; }
                s->data[k] = a->rgba + off;
            }
        }
    }
    json_free(root);
    if (bad) { texanim_free(a); return -1; }
    return 0;
}

void texanim_free(struct texanim *a)
{
    free(a->rgba);
    memset(a, 0, sizeof *a);
}

int texanim_state_read(const struct texanim *a, const struct jval *anim, struct texanim_state *st)
{
    memset(st, 0, sizeof *st);
    const struct jval *maps = json_get(anim, "maps");
    if (!maps || json_len(maps) != TEXANIM_MAPS) return -1;
    for (int m = 0; m < TEXANIM_MAPS; ++m) {
        const struct jval *list = json_at(maps, m);
        if (json_len(list) != a->n[m]) return -1;
        for (int i = 0; i < a->n[m]; ++i) {
            const struct jval *j = json_at(list, i);
            const char *name = json_str(json_get(j, "n"));
            if (!name || strcmp(name, a->s[m][i].name)) return -1;
            st->fc[m][i] = jint(json_get(j, "fc"));
            st->tc[m][i] = jint(json_get(j, "tc"));
            if (a->s[m][i].kind == TEXANIM_CLOCK) {
                st->clock_h = jdbl(json_get(j, "h"));
                st->clock_i = jdbl(json_get(j, "i"));
            }
            if (a->s[m][i].kind == TEXANIM_COMPASS) {
                st->compass_a = jdbl(json_get(j, "a"));
                st->compass_d = jdbl(json_get(j, "d"));
            }
        }
    }
    return 0;
}

/* (int)d the way Java narrows a double: toward zero, NaN to 0, saturating. */
static int java_d2i(double d)
{
    if (d != d) return 0;
    if (d >= 2147483647.0) return 2147483647;
    if (d <= -2147483648.0) return (int)-2147483647 - 1;
    return (int)d;
}

/* TextureAtlasSprite.updateAnimation: the counters only (the upload is
 * texanim_apply's). */
static void sprite_tick(const struct texanim_sprite *s, int *fc, int *tc)
{
    ++*tc;
    if (*tc >= s->time[*fc]) {
        int count = s->nframes == 0 ? s->ndata : s->nframes;
        *fc = (*fc + 1) % count;
        *tc = 0;
    }
}

/* TextureClock.updateAnimation */
static void clock_tick(double *h, double *i, int *fc, int frames,
                       const struct texanim_world *w, det_state *det)
{
    if (frames <= 0) return;
    double target = 0.0;
    if (w && w->world) {
        target = (double)w->celestial;
        if (!w->surface) target = det ? det_math_random_role(det, DET_CLIENT) : 0.0;
    }
    double d;
    for (d = target - *h; d < -0.5; ++d) {}
    while (d >= 0.5) --d;
    if (d < -1.0) d = -1.0;
    if (d > 1.0) d = 1.0;
    *i += d * 0.1;
    *i *= 0.8;
    *h += *i;
    int k;
    for (k = java_d2i((*h + 1.0) * (double)frames) % frames; k < 0; k = (k + frames) % frames) {}
    *fc = k;
}

/* TextureCompass.updateCompass's target angle: the spawn bearing against the
 * yaw, or a Math.random off the surface; 0 with no world. */
static double compass_target(const struct texanim_world *w, det_state *det, int role)
{
    if (!w || !w->world) return 0.0;
    double dx = (double)w->spawn_x - w->px;
    double dz = (double)w->spawn_z - w->pz;
    double yaw = fmod(w->yaw, 360.0);
    double a = -((yaw - 90.0) * 3.141592653589793 / 180.0 - fd_atan2(dz, dx));
    if (!w->surface) a = det ? det_math_random_role(det, role) * 3.141592653589793 * 2.0 : 0.0;
    return a;
}

static int compass_index(double a, int frames)
{
    int k;
    for (k = java_d2i((a / PI2 + 1.0) * (double)frames) % frames; k < 0; k = (k + frames) % frames) {}
    return k;
}

/* TextureCompass.updateAnimation: updateCompass(..., false, false) */
static void compass_tick(double *a, double *d, int *fc, int frames,
                         const struct texanim_world *w, det_state *det, int role)
{
    if (frames <= 0) return;
    double target = compass_target(w, det, role);
    double e;
    for (e = target - *a; e < -3.141592653589793; e += PI2) {}
    while (e >= 3.141592653589793) e -= PI2;
    if (e < -1.0) e = -1.0;
    if (e > 1.0) e = 1.0;
    *d += e * 0.1;
    *d *= 0.8;
    *a += *d;
    *fc = compass_index(*a, frames);
}

int texanim_compass_frame(const struct texanim_world *w, int frames)
{
    if (frames <= 0) return 0;
    return compass_index(compass_target(w, NULL, DET_RENDER), frames);
}

void texanim_tick(const struct texanim *a, struct texanim_state *st,
                  const struct texanim_world *w, det_state *det)
{
    for (int m = 0; m < TEXANIM_MAPS; ++m)
        for (int i = 0; i < a->n[m]; ++i) {
            const struct texanim_sprite *s = &a->s[m][i];
            if (s->kind == TEXANIM_CLOCK)
                clock_tick(&st->clock_h, &st->clock_i, &st->fc[m][i], s->ndata, w, det);
            else if (s->kind == TEXANIM_COMPASS)
                compass_tick(&st->compass_a, &st->compass_d, &st->fc[m][i], s->ndata, w, det, DET_CLIENT);
            else
                sprite_tick(s, &st->fc[m][i], &st->tc[m][i]);
        }
}

void texanim_sprites_from_start(const struct texanim *a, struct texanim_state *st, int64_t n)
{
    for (int m = 0; m < TEXANIM_MAPS; ++m)
        for (int i = 0; i < a->n[m]; ++i) {
            const struct texanim_sprite *s = &a->s[m][i];
            if (s->kind != TEXANIM_SPRITE) continue;
            st->fc[m][i] = st->tc[m][i] = 0;
            for (int64_t k = 0; k < n; ++k) sprite_tick(s, &st->fc[m][i], &st->tc[m][i]);
        }
}

void texanim_dials_tick(struct texanim_state *st, int clock_frames, int compass_frames,
                        const struct texanim_world *w, det_state *det)
{
    clock_tick(&st->clock_h, &st->clock_i, &st->fc[TEXANIM_ITEMS][0], clock_frames, w, det);
    compass_tick(&st->compass_a, &st->compass_d, &st->fc[TEXANIM_ITEMS][1], compass_frames, w, det, DET_CLIENT);
}

int texanim_shown(const struct texanim *a, const struct texanim_state *st, int map, int i)
{
    const struct texanim_sprite *s = &a->s[map][i];
    int fc = st->fc[map][i];
    if (s->kind != TEXANIM_SPRITE) return fc;
    return fc >= 0 && fc < s->nframes ? s->index[fc] : fc;
}

/* sprite S's frame K into the atlas; 1 when a byte changed (the rows are
 * compared first, so a frame already in place writes nothing) */
static int blit(const struct texanim_sprite *s, int k, unsigned char *atlas, int aw, int ah)
{
    if (k < 0 || k >= s->ndata || !s->data[k]) return 0;
    if (s->x < 0 || s->y < 0 || s->x + s->w > aw || s->y + s->h > ah) return 0;
    int changed = 0;
    for (int y = 0; y < s->h; ++y) {
        unsigned char *row = atlas + ((size_t)(s->y + y) * aw + s->x) * 4;
        const unsigned char *src = s->data[k] + (size_t)y * s->w * 4;
        if (!memcmp(row, src, (size_t)s->w * 4)) continue;
        memcpy(row, src, (size_t)s->w * 4);
        changed = 1;
    }
    return changed;
}

void texanim_apply(const struct texanim *a, const struct texanim_state *st, int map,
                   unsigned char *atlas, int aw, int ah)
{
    /* a recording's copy of the sprite's place is stale (raster_obs.h);
     * bytes rewritten with themselves leave it as it is */
    for (int i = 0; i < a->n[map]; ++i) {
        const struct texanim_sprite *s = &a->s[map][i];
        if (blit(s, texanim_shown(a, st, map, i), atlas, aw, ah))
            raster_rec_texture_patched(atlas, aw, ah, s->x, s->y, s->w, s->h);
    }
}

void texanim_compass_render_step(const struct texanim *a, struct texanim_state *st,
                                 const struct texanim_world *w, det_state *det)
{
    for (int i = 0; i < a->n[TEXANIM_ITEMS]; ++i)
        if (a->s[TEXANIM_ITEMS][i].kind == TEXANIM_COMPASS)
            compass_tick(&st->compass_a, &st->compass_d, &st->fc[TEXANIM_ITEMS][i],
                         a->s[TEXANIM_ITEMS][i].ndata, w, det, DET_RENDER);
}

/* One scene at a time: every reader of the same scene shares the load and
 * the state, which the item frames' compass steps move on. */
static pthread_mutex_t scene_lock = PTHREAD_MUTEX_INITIALIZER;
static char scene_dir[1024];
static int scene_ok;
static struct texanim scene_anim;
static struct texanim_state scene_state;

int texanim_open(const char *dir, struct texanim *a, struct texanim_state *st)
{
    if (texanim_load(a, dir) != 0) return -1;
    char p[1200];
    snprintf(p, sizeof p, "%s/frames.jsonl", dir);
    char *text = slurp(p, NULL);
    if (!text) { texanim_free(a); return -1; }
    char *nl = strchr(text, '\n');
    if (nl) *nl = 0;
    struct jval *row = json_parse(text);
    int ok = row && texanim_state_read(a, json_get(row, "anim"), st) == 0;
    json_free(row);
    if (!ok) { texanim_free(a); return -1; }
    return 0;
}

static const struct texanim *live_anim;
static const struct texanim_state *live_state;

void texanim_live_set(const struct texanim *a, const struct texanim_state *st)
{
    pthread_mutex_lock(&scene_lock);
    live_anim = a;
    live_state = st;
    pthread_mutex_unlock(&scene_lock);
}

static void scene_load(const char *scene)
{
    if (!strcmp(scene_dir, scene)) return;
    snprintf(scene_dir, sizeof scene_dir, "%s", scene);
    texanim_free(&scene_anim);
    char p[1200];
    snprintf(p, sizeof p, "%s/state", scene);
    scene_ok = texanim_open(p, &scene_anim, &scene_state) == 0;
}

int texanim_patch_scene(const char *scene, int map, unsigned char *atlas, int aw, int ah)
{
    if (!scene || !atlas) return 0;
    pthread_mutex_lock(&scene_lock);
    int ok;
    if (live_anim) {
        texanim_apply(live_anim, live_state, map, atlas, aw, ah);
        ok = 1;
    } else {
        scene_load(scene);
        ok = scene_ok;
        if (ok) texanim_apply(&scene_anim, &scene_state, map, atlas, aw, ah);
    }
    pthread_mutex_unlock(&scene_lock);
    return ok;
}

/* texanim_apply for the sprites whose frame is not the one MEMO says ATLAS
 * holds already */
void texanim_apply_memo(const struct texanim *a, const struct texanim_state *st, int map, unsigned char *atlas,
                        int aw, int ah, struct texanim_memo *memo)
{
    int n = a->n[map], same = memo->atlas == atlas && memo->a == a && memo->map == map && memo->n == n;
    for (int i = 0; i < n; ++i) {
        int k = texanim_shown(a, st, map, i);
        if (same && memo->shown[i] == k) continue;
        const struct texanim_sprite *s = &a->s[map][i];
        if (blit(s, k, atlas, aw, ah)) raster_rec_texture_patched(atlas, aw, ah, s->x, s->y, s->w, s->h);
        memo->shown[i] = k;
    }
    memo->atlas = atlas;
    memo->a = a;
    memo->map = map;
    memo->n = n;
}

int texanim_patch_scene_memo(const char *scene, int map, unsigned char *atlas, int aw, int ah, struct texanim_memo *memo)
{
    if (!scene || !atlas) return 0;
    pthread_mutex_lock(&scene_lock);
    int ok;
    if (live_anim) {
        texanim_apply_memo(live_anim, live_state, map, atlas, aw, ah, memo);
        ok = 1;
    } else {
        scene_load(scene);
        ok = scene_ok;
        if (ok) texanim_apply_memo(&scene_anim, &scene_state, map, atlas, aw, ah, memo);
    }
    pthread_mutex_unlock(&scene_lock);
    return ok;
}

int texanim_scene(const char *scene, const struct texanim **a, struct texanim_state **st)
{
    pthread_mutex_lock(&scene_lock);
    scene_load(scene);
    int ok = scene_ok;
    pthread_mutex_unlock(&scene_lock);
    if (ok) { *a = &scene_anim; *st = &scene_state; }
    return ok;
}
