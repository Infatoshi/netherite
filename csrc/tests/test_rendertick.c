/* Gate: the per-tick client render state between frame rows of a render
 * recording: the animated atlas sprites (csrc/engine/texanim.c) and
 * EntityRenderer.updateTorchFlicker (renderstate.c), against the recording's
 * own dumps (out/java/rendertick/NAME, recorded with
 * --renderstate NAME/state; a recording without state/anim.json predates the
 * dump and is skipped).
 *
 *   test_rendertick RECORDING_DIR
 *
 *  1. The block atlas MeshProbe read back (atlas.rgba) is rebuilt byte for
 *     byte from itself with every animated sprite rewritten from anim.rgba at
 *     the state atlas.json's "anim" recorded at that moment; the item atlas
 *     (state/gui_items.rgba, read back at the first frame) likewise from the
 *     first frame row's state.
 *  2. Between two frame rows one tick apart, one texanim_tick of the earlier
 *     row's state (the dials reading the earlier row's world, position and
 *     celestial angle and the later row's yaw, which the look input sets
 *     before the tick) gives the later row's state exactly: every counter
 *     and the dial doubles bit for bit. Rows further apart check the plain
 *     sprites' counters after that many ticks.
 *  3. Between two rows one tick apart, updateTorchFlicker from the earlier
 *     row's four flicker floats over the client Math.random stream as the later
 *     row's tick started (er.cm0; off the surface the clock and the compass
 *     draw first) gives the later row's four floats bit for bit. The scenes are
 *     quiet: nothing else draws on that stream before updateRenderer.
 *  4. Negative: a frame list one tick slower than the recording's is caught;
 *     so is a dial run that leaves out the item frames' compass steps. */
#define _POSIX_C_SOURCE 200809L
#include "../engine/renderstate.h"
#include "../engine/clientstate.h"
#include "../engine/texanim.h"
#include "../engine/raster_itemframe.h"
#include "../engine/tape.h"
#include "../engine/particles_live.h"
#include "../engine/raster_particles.h"
#include "../engine/raster_tileent.h"
#include "../engine/render_blocks.h"
#include "../engine/world.h"

#include <math.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static unsigned char *read_file(const char *path, size_t want)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    unsigned char *b = malloc(want);
    size_t got = b ? fread(b, 1, want, f) : 0;
    fclose(f);
    if (got != want) { free(b); return NULL; }
    return b;
}

static struct jval *read_json(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    fclose(f);
    b[n] = 0;
    return json_parse(b);
}

static int jint(const struct jval *v)
{
    int64_t n = 0;
    json_int(v, &n);
    return (int)n;
}

static double jd(const struct jval *v)
{
    uint64_t b = 0;
    double d = 0;
    if (json_double(v, &b)) memcpy(&d, &b, 8);
    return d;
}

static float jf(const struct jval *v)
{
    uint32_t b = 0;
    float f = 0;
    if (json_float(v, &b)) memcpy(&f, &b, 4);
    return f;
}

/* Rebuild an atlas from itself at a state and compare. */
static void check_rebuild(const struct texanim *a, const struct texanim_state *st, int map,
                          const unsigned char *atlas, int w, int h, const char *what)
{
    size_t n = (size_t)w * h * 4;
    unsigned char *copy = malloc(n);
    memcpy(copy, atlas, n);
    texanim_apply(a, st, map, copy, w, h);
    size_t diff = 0, first = n;
    for (size_t i = 0; i < n; ++i)
        if (copy[i] != atlas[i]) { if (!diff) first = i; ++diff; }
    if (diff) {
        printf("FAIL %s: %zu bytes differ from the dump, first at pixel (%zu, %zu)\n", what, diff,
               first / 4 % (size_t)w, first / 4 / (size_t)w);
        ++fails;
    } else {
        printf("ok %s rebuilt byte for byte\n", what);
    }
    free(copy);
}

static struct texanim_world world_of(const struct jval *row, const struct jval *next)
{
    struct texanim_world w = {0};
    const struct jval *anim = json_get(row, "anim"), *pl = json_get(row, "pl");
    w.world = 1;
    w.surface = jint(json_get(anim, "surface"));
    w.celestial = jf(json_get(json_get(row, "g"), "ang"));
    w.spawn_x = jint(json_at(json_get(anim, "spawn"), 0));
    w.spawn_z = jint(json_at(json_get(anim, "spawn"), 2));
    w.px = jd(json_get(pl, "x"));
    w.pz = jd(json_get(pl, "z"));
    w.yaw = (double)jf(json_get(json_get(next, "pl"), "yaw"));
    return w;
}

/* The item frames holding a compass that frame ROW drew: RenderItemFrame
 * moves the compass animation on once for each (RenderGlobal.renderEntities'
 * range and frustum gate over the row's own matrices). */
static int compass_frames_drawn(const char *dir, const struct jval *row)
{
    char p[1200];
    snprintf(p, sizeof p, "%s/entities.jsonl", dir);
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    const struct jval *out = json_get(row, "out");
    float proj[16], mv[16];
    for (int i = 0; i < 16; ++i) {
        proj[i] = jf(json_at(json_get(out, "proj"), i));
        mv[i] = jf(json_at(json_get(out, "mv"), i));
    }
    const double cam[3] = {jd(json_get(json_get(out, "cam"), "x")), jd(json_get(json_get(out, "cam"), "y")),
                           jd(json_get(json_get(out, "cam"), "z"))};
    struct lines in;
    lines_init(&in);
    lines_file(&in, f);
    int n = 0;
    for (const char *s = lines_next(&in); s; s = lines_next(&in)) {
        if (!strstr(s, "\"EntityItemFrame\"")) continue;
        struct jval *e = json_parse(strdup(s));
        char *raw = json_raw(json_get(e, "nbt"));
        nbt *t = raw ? nbt_parse(raw) : NULL;
        free(raw);
        struct raster_itemframe fr;
        if (t) {
            raster_itemframe_parse(t, &fr);
            if (fr.item_id == 345 && raster_itemframe_rendered(&fr, proj, mv, cam)) ++n;
        }
        nbt_free(t);
        json_free(e);
    }
    lines_free(&in);
    fclose(f);
    return n;
}

static int compare_states(const struct texanim *a, const struct texanim_state *got,
                          const struct texanim_state *want, int dials, long long t)
{
    int bad = 0;
    for (int m = 0; m < TEXANIM_MAPS; ++m)
        for (int i = 0; i < a->n[m]; ++i) {
            if (!dials && a->s[m][i].kind != TEXANIM_SPRITE) continue;
            if (got->fc[m][i] != want->fc[m][i] || got->tc[m][i] != want->tc[m][i]) {
                printf("FAIL t=%lld %s: frameCounter %d tickCounter %d, oracle %d %d\n", t,
                       a->s[m][i].name, got->fc[m][i], got->tc[m][i], want->fc[m][i], want->tc[m][i]);
                bad = 1;
            }
        }
    if (dials) {
        const double g[4] = {got->clock_h, got->clock_i, got->compass_a, got->compass_d};
        const double w[4] = {want->clock_h, want->clock_i, want->compass_a, want->compass_d};
        const char *names[4] = {"clock field_94239_h", "clock field_94240_i",
                                "compass currentAngle", "compass angleDelta"};
        for (int k = 0; k < 4; ++k)
            if (memcmp(&g[k], &w[k], 8)) {
                printf("FAIL t=%lld %s: %.17g, oracle %.17g\n", t, names[k], g[k], w[k]);
                bad = 1;
            }
    }
    return bad;
}


/* ------------------------------------------------------------------ 5.
 * The particle tick. Between two frame rows one tick apart whose later row
 * carries the probe's spawn log (ParticleLog: every World.spawnParticle and
 * playAuxSFX of the tick with the client streams before it) and the streams
 * at the tick's start (tick0), the native EffectRenderer (particles_live.c)
 * runs the tick from the earlier row's list, in Minecraft.runTick's order:
 *   the packets (a crit's EntityCrit2FX from the dumped emitter's target, a
 *     2002 or 2003 aux effect from the logged event, the explosion's and any
 *     other packet spawn from the logged call),
 *   updateTorchFlicker's eight Math draws, addRainParticles,
 *   the entity updates' spawns (logged calls: the primed TNT's smoke, the
 *     flying eye's portal particles),
 *   WorldClient.tick's four draws on the world's Random, doVoidFogParticles
 *     (the 1000 cells, each block's randomDisplayTick),
 *   updateEffects.
 * Every spawn the native code makes is matched in order against the log
 * (name, arguments and the client streams before it, bit for bit); a logged
 * call from a source the port cannot see (the packet's explosion, the
 * entities) is replayed from its own logged state. The resulting list must
 * equal the later row's dump, every field of every FX in each layer's order,
 * getBrightnessForRender included. */

struct ptick {
    struct particles_live *pl;
    struct jval *log;              /* the later row's spawns */
    int next;                      /* the next log entry to match */
    const char *phase;             /* the src fragment the native spawns come from */
    int bad;                       /* mismatches */
    int quiet;                     /* the negative pass: count, do not print */
    int matched;
    const struct jval *mobs;       /* the target rows: the earlier row's, then the later's */
    det_rng cw;
};

static uint64_t hex48(const struct jval *v)
{
    const char *s = json_str(v);
    return s ? strtoull(s, NULL, 16) & JR_MASK : 0;
}

/* the client streams (and the world's Random) as a log entry has them */
static void resync(struct ptick *k, const struct jval *e)
{
    det_state *d = k->pl->det;
    d->seeder[DET_CLIENT].r.seed = hex48(json_get(e, "ds"));
    d->seeder[DET_CLIENT].have_next_next_gaussian = 0;
    d->math[DET_CLIENT].r.seed = hex48(json_get(e, "dm"));
    d->math[DET_CLIENT].have_next_next_gaussian = 0;
    d->next_id[DET_CLIENT] = jint(json_get(e, "did"));
    k->cw.r.seed = hex48(json_get(e, "cw"));
    k->cw.have_next_next_gaussian = 0;
    if (k->quiet) (void)det_math_random_role(d, DET_CLIENT);   /* the negative pass: one draw late */
}

static int streams_equal(struct ptick *k, const struct jval *e)
{
    det_state *d = k->pl->det;
    return d->seeder[DET_CLIENT].r.seed == hex48(json_get(e, "ds")) &&
           d->math[DET_CLIENT].r.seed == hex48(json_get(e, "dm")) &&
           d->next_id[DET_CLIENT] == jint(json_get(e, "did")) &&
           k->cw.r.seed == hex48(json_get(e, "cw"));
}

static const char *src_of(const struct jval *e)
{
    const char *s = json_str(json_get(e, "src"));
    return s ? s : "";
}

/* particles_live's spawn hook: a native spawn in phase k->phase must be the
 * next logged call, with the same streams before it */
static void ptick_on_spawn(void *ctx, const char *name, const double a[6])
{
    struct ptick *k = ctx;
    int n = json_len(k->log);
    if (k->next >= n) {
        if (k->bad++ < 8 && !k->quiet) printf("  native spawn %s past the end of the log\n", name);
        return;
    }
    const struct jval *e = json_at(k->log, k->next);
    const char *p = json_str(json_get(e, "p"));
    int ok = p && !strcmp(p, name) && strstr(src_of(e), k->phase) != NULL;
    const struct jval *args = json_get(e, "a");
    for (int i = 0; ok && i < 6; ++i) ok = jd(json_at(args, i)) == a[i] || (a[i] != a[i] && jd(json_at(args, i)) != jd(json_at(args, i)));
    int same = ok && streams_equal(k, e);
    if (!same) {
        if (k->bad++ < 8 && !k->quiet)
            printf("  spawn %d: native %s (%.17g %.17g %.17g) %s, log %s (%.17g %.17g %.17g) from %s\n", k->next,
                   name, a[0], a[1], a[2], ok ? "with other streams" : "", p ? p : "(aux)",
                   jd(json_at(args, 0)), jd(json_at(args, 1)), jd(json_at(args, 2)), src_of(e));
        resync(k, e);
    }
    ++k->matched;
    ++k->next;
}

static int ptick_target(void *ctx, int id, struct plive_target *out)
{
    struct ptick *k = ctx;
    for (int i = 0; i < json_len(k->mobs); ++i) {
        const struct jval *m = json_at(k->mobs, i);
        if (jint(json_get(m, "id")) != id) continue;
        out->x = jd(json_get(m, "x"));
        out->min_y = jd(json_get(m, "y"));
        out->z = jd(json_get(m, "z"));
        out->width = jf(json_get(m, "width"));
        out->height = jf(json_get(m, "height"));
        return 1;
    }
    return 0;
}

/* chunks.bin as a client world: the probe's chunk bytes per chunk */
static int load_world(const char *dir, struct world *w)
{
    char p[1200];
    snprintf(p, sizeof p, "%s/chunks.bin", dir);
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    world_init(w, 1);
    w->no_generate = 1;
    size_t rec = RB_CHUNK_RECORD;
    unsigned char *b = malloc(rec);
    while (b && fread(b, 1, rec, f) == rec) {
        int32_t cx, cz;
        memcpy(&cx, b, 4);
        memcpy(&cz, b + 4, 4);
        const unsigned char *c = b + 8;
        struct chunk *ch = world_insert_chunk(w, cx, cz, c + RB_CHUNK_BIOMES);
        if (!ch) break;
        chunk_cells_in(ch, (const uint16_t *)(const void *)c, c + RB_CHUNK_META, c + RB_CHUNK_SKY, c + RB_CHUNK_BLOCK);
        memcpy(ch->height, c + RB_CHUNK_HEIGHT, 1024);
        memcpy(ch->precip, c + RB_CHUNK_HEIGHT + 1024, 1024);
        memcpy(&ch->height_min, c + RB_CHUNK_HEIGHT + 2048, 4);
        memcpy(&ch->mask, c + RB_CHUNK_HEIGHT + 2052, 2);
    }
    free(b);
    fclose(f);
    return 0;
}

/* the fields of one FX against the dump; returns the number that differ */
static int print_budget;

static int compare_fx(const struct live_fx *g, const struct live_fx *w, int check_brf, const char *where)
{
    int bad = 0;
#define FXD(field) if (memcmp(&g->field, &w->field, sizeof g->field)) { if (bad++ < 3 && print_budget-- > 0) printf("  %s %s: " #field " %.17g, oracle %.17g\n", where, particles_live_class(w->kind), (double)g->field, (double)w->field); }
#define FXI(field) if (g->field != w->field) { if (bad++ < 3 && print_budget-- > 0) printf("  %s %s: " #field " %d, oracle %d\n", where, particles_live_class(w->kind), (int)g->field, (int)w->field); }
    FXI(kind) FXI(layer)
    FXD(e.pos_x) FXD(e.pos_y) FXD(e.pos_z)
    FXD(e.prev_pos_x) FXD(e.prev_pos_y) FXD(e.prev_pos_z)
    if (w->kind != PLIVE_CRIT2) { FXD(e.motion_x) FXD(e.motion_y) FXD(e.motion_z) }
    FXD(e.bounding_box.min_x) FXD(e.bounding_box.min_y) FXD(e.bounding_box.min_z)
    FXD(e.bounding_box.max_x) FXD(e.bounding_box.max_y) FXD(e.bounding_box.max_z)
    FXD(e.width) FXD(e.height) FXD(e.y_offset) FXD(e.y_size)
    FXI(e.on_ground) FXI(e.no_clip)
    FXI(age) FXI(maxage) FXI(tix) FXI(tiy)
    FXD(red) FXD(green) FXD(blue) FXD(alpha) FXD(jitx) FXD(jity) FXD(gravity)
    FXD(base_scale)
    if (w->kind != PLIVE_SMOKE && w->kind != PLIVE_FLAME && w->kind != PLIVE_REDDUST && w->kind != PLIVE_PORTAL &&
        w->kind != PLIVE_CRIT && w->kind != PLIVE_LAVA) FXD(scale)   /* renderParticle rewrites these */
    FXI(vx) FXI(vq) FXD(vs) FXI(tstart) FXI(tmax)
    FXD(home_x) FXD(home_y) FXD(home_z) FXI(spell_base) FXI(bob) FXI(lava)
    FXI(ent_id) FXI(life) FXI(mlife) FXI(magic)
    if (g->rand.r.seed != w->rand.r.seed) { if (bad++ < 3) printf("  %s %s: own Random differs\n", where, particles_live_class(w->kind)); }
    if (check_brf) FXI(brf)
#undef FXD
#undef FXI
    return bad;
}

static struct particles_live PL;

/* RenderGlobal.playAuxSFX's effects whose FX the log does not hold: 2000's
 * smoke, 2002's and 2003's through RenderGlobal.spawnParticle, 2001's
 * destroy effects through the EffectRenderer. 2004, 2005 and 2006 spawn
 * through World.spawnParticle, so each of their FX is a logged call of its
 * own; the sounds make none. */
static int aux_makes_own_fx(int id)
{
    return id >= 2000 && id <= 2003;
}

/* GameSettings.particleSetting as the recording ran: the tape header's
 * options.particles (the fx scenes ran with --particles 0, the others with the
 * pinned minimal 2); 0 when the header does not say. */
static int particle_setting(const char *dir)
{
    char p[1200];
    snprintf(p, sizeof p, "%s/tape.jsonl", dir);
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    char *line = NULL;
    size_t cap = 0;
    int setting = 0;
    if (getline(&line, &cap, f) > 0) {
        struct jval *h = json_parse(line);
        line = NULL;
        const struct jval *v = json_get(json_get(h, "options"), "particles");
        if (v) setting = jint(v);
        json_free(h);
    }
    free(line);
    fclose(f);
    return setting;
}

/* Det's client consumer pin (det.h): a recording made since the pin (DIR/pins,
 * as rawjudge.sh marks its goldens) gave the torch flicker and every
 * particle their values from the consumers' own streams, seeded per client
 * tick from the world seed (the tape header's seed); the shared streams are
 * still drawn as vanilla draws them. 0 for an older recording. */
static int pinned_seed(const char *dir, int64_t *seed)
{
    char p[1200];
    snprintf(p, sizeof p, "%s/pins", dir);
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    fclose(f);
    *seed = 0;
    snprintf(p, sizeof p, "%s/tape.jsonl", dir);
    f = fopen(p, "rb");
    if (!f) return 0;
    char *line = NULL;
    size_t cap = 0;
    if (getline(&line, &cap, f) > 0) {
        struct jval *h = json_parse(line);
        line = NULL;
        const struct jval *v = json_get(h, "seed");
        if (v) *seed = jint(v);
        json_free(h);
    }
    free(line);
    fclose(f);
    return 1;
}

/* updateTorchFlicker under the pin at client tick t: the eight draws still
 * spend the shared stream (when one is given), the values are the flicker's
 * own (play.c's live flicker) */
static void pinned_flicker(struct rs_flicker *f, det_state *shared, int64_t seed, int64_t t)
{
    det_pin pin;
    det_pin_init(&pin, DET_PIN_FLICKER, seed);
    det_rng *r = det_pin_at(&pin, t);
    double v[8];
    for (int i = 0; i < 8; ++i) {
        if (shared) (void)det_math_random_role(shared, DET_CLIENT);
        v[i] = det_rng_double(r);
    }
    renderstate_torch_flicker_step(f, v);
}

static void check_particles(const char *dir, struct jval **rows, int nrows)
{
    int setting = particle_setting(dir);
    int64_t pin_seed = 0;
    int pinned = pinned_seed(dir, &pin_seed);
    int have = 0;
    for (int r = 0; r < nrows; ++r) have |= json_get(rows[r], "spawns") != NULL;
    if (!have) return;

    struct world w;
    if (load_world(dir, &w) != 0) { printf("FAIL particles: no chunks.bin in %s\n", dir); ++fails; return; }
    det_state det;
    det_init(&det);

    int ticks = 0, fx_checked = 0, spawns_checked = 0, bad_total = 0, neg_caught = 0, negs = 0;
    for (int r = 0; r + 1 < nrows; ++r) {
        const struct jval *r0 = rows[r], *r1 = rows[r + 1];
        const struct jval *t0 = json_get(r1, "tick0");
        struct jval *log = (struct jval *)json_get(r1, "spawns");
        if (jint(json_get(r1, "t")) != jint(json_get(r0, "t")) + 1 || !t0 || !log) continue;

        for (int neg = 0; neg < 2; ++neg) {
            print_budget = neg ? 0 : 12;
            particles_live_init(&PL, &w, &det);
            PL.setting = setting;
            if (pinned) particles_live_pin(&PL, pin_seed, jint(json_get(r1, "t")));
            struct ptick k = {.pl = &PL, .log = log, .quiet = neg};
            PL.world_rand = &k.cw;
            PL.target = ptick_target;
            PL.target_ctx = &k;
            PL.on_spawn = ptick_on_spawn;
            PL.spawn_ctx = &k;
            const struct jval *fx0 = json_get(r0, "fx");
            for (int i = 0; i < json_len(fx0) && PL.n < PLIVE_CAP; ++i)
                particles_live_from_json(json_at(fx0, i), &PL.fx[PL.n++], &w);
            resync(&k, t0);
            const struct jval *p0 = json_get(r0, "pl"), *p1 = json_get(r1, "pl");
            float pt = jf(json_get(r1, "pt"));

            /* the packets: the view is the player before its update */
            PL.view_x = jd(json_get(p0, "x")); PL.view_y = jd(json_get(p0, "y")); PL.view_z = jd(json_get(p0, "z"));
            k.mobs = json_get(r0, "mobs");
            k.phase = "NetHandlerPlayClient";
            /* the new crit emitters, in the order the dump lists them */
            const struct jval *fx1 = json_get(r1, "fx");
            int crit_next = 0;
            while (k.next < json_len(log)) {
                const struct jval *e = json_at(log, k.next);
                const char *src = src_of(e);
                /* the probe logs three frames; a crit emitter's two constructors
                 * fill them, so its packet (handleAnimation) shows as <init> */
                int crit = strstr(src, "handleAnimation") || strstr(src, "EntityCrit2FX.<init>");
                if (!strstr(src, "NetHandlerPlayClient") && !crit) break;
                if (json_get(e, "ent")) { resync(&k, e); ++k.next; continue; }
                if (crit) {
                    /* EntityCrit2FX(world, entity): the next new emitter */
                    int made = 0;
                    for (; crit_next < json_len(fx1) && !made; ++crit_next) {
                        const struct jval *m = json_at(fx1, crit_next);
                        const char *cls = json_str(json_get(m, "cls"));
                        if (!cls || strcmp(cls, "EntityCrit2FX") || jint(json_get(m, "life")) != 2) continue;
                        const char *pn = json_str(json_get(m, "pname"));
                        k.phase = "EntityCrit2FX.<init>";
                        particles_live_crit(&PL, jint(json_get(m, "ent")), pn && !strcmp(pn, "magicCrit"));
                        k.phase = "NetHandlerPlayClient";
                        made = 1;
                    }
                    if (!made) { printf("  no emitter for %s\n", src); ++k.bad; ++k.next; }
                    continue;
                }
                resync(&k, e);
                ++k.next;
                if (json_get(e, "aux")) {
                    const struct jval *a = json_get(e, "a");
                    PL.on_spawn = NULL;
                    if (aux_makes_own_fx(jint(json_get(e, "aux"))))
                        particles_live_aux_sfx(&PL, jint(json_get(e, "aux")), jint(json_at(a, 0)), jint(json_at(a, 1)),
                                               jint(json_at(a, 2)), jint(json_at(a, 3)));
                    PL.on_spawn = ptick_on_spawn;
                } else {
                    const struct jval *a = json_get(e, "a");
                    PL.on_spawn = NULL;
                    particles_live_spawn(&PL, json_str(json_get(e, "p")), jd(json_at(a, 0)), jd(json_at(a, 1)),
                                         jd(json_at(a, 2)), jd(json_at(a, 3)), jd(json_at(a, 4)), jd(json_at(a, 5)));
                    PL.on_spawn = ptick_on_spawn;
                }
            }

            /* updateRenderer: the torch flicker (its eight Math draws, checked
             * against the later row), then the rain */
            {
                const struct jval *e0 = json_get(r0, "er"), *e1 = json_get(r1, "er");
                struct rs_flicker fl = {jf(json_get(e0, "tfdx")), jf(json_get(e0, "tfdy")),
                                        jf(json_get(e0, "tfx")), jf(json_get(e0, "tfy"))};
                /* off the surface the clock and the compass draw first, in
                 * the textures' tick */
                if (!jint(json_get(json_get(r1, "anim"), "surface"))) {
                    struct texanim_state dials = {0};
                    struct texanim_world tw = {.world = 1, .surface = 0};
                    texanim_dials_tick(&dials, 64, 32, &tw, &det);
                }
                if (pinned) pinned_flicker(&fl, &det, pin_seed, jint(json_get(r1, "t")));
                else renderstate_torch_flicker(&fl, &det);
                if (!neg && json_get(e0, "tfdx") && (memcmp(&fl.x, &(float){jf(json_get(e1, "tfx"))}, 4) ||
                                                   memcmp(&fl.y, &(float){jf(json_get(e1, "tfy"))}, 4))) {
                    if (k.bad++ < 8) printf("  t=%d: the torch flicker from the replayed stream differs\n", jint(json_get(r1, "t")));
                }
            }
            if (neg) (void)det_math_random_role(&det, DET_CLIENT);   /* negative: one draw late */
            float rain = 0.0f;
            {
                const struct jval *wo = json_get(r1, "wo");
                uint32_t bits = 0;
                if (json_float(json_get(wo, "rain"), &bits)) memcpy(&rain, &bits, 4);
                else { const char *s = json_str(json_get(wo, "rain")); rain = s ? strtof(s, NULL) : 0.0f; }
            }
            int sound = 0;
            k.phase = "EntityRenderer";
            particles_live_rain(&PL, rain, jint(json_get(json_get(r1, "opt"), "fancy")),
                                jint(json_get(json_get(r1, "er"), "ruc")), &sound);

            /* the entity updates: their logged calls, each from its own state */
            PL.view_x = jd(json_get(p1, "x")); PL.view_y = jd(json_get(p1, "y")); PL.view_z = jd(json_get(p1, "z"));
            while (k.next < json_len(log)) {
                const struct jval *e = json_at(log, k.next);
                const char *src = src_of(e);
                if (strstr(src, "randomDisplayTick") || strstr(src, "updateEffects")) break;
                if (json_get(e, "ent")) { resync(&k, e); ++k.next; continue; }
                resync(&k, e);
                ++k.next;
                const struct jval *a = json_get(e, "a");
                PL.on_spawn = NULL;
                if (json_get(e, "aux")) {
                    if (aux_makes_own_fx(jint(json_get(e, "aux"))))
                        particles_live_aux_sfx(&PL, jint(json_get(e, "aux")), jint(json_at(a, 0)), jint(json_at(a, 1)),
                                               jint(json_at(a, 2)), jint(json_at(a, 3)));
                }
                else
                    particles_live_spawn(&PL, json_str(json_get(e, "p")), jd(json_at(a, 0)), jd(json_at(a, 1)),
                                         jd(json_at(a, 2)), jd(json_at(a, 3)), jd(json_at(a, 4)), jd(json_at(a, 5)));
                PL.on_spawn = ptick_on_spawn;
            }

            /* WorldClient.tick: setActivePlayerChunksAndCheckLight's draws */
            (void)det_rng_int_n(&k.cw, 1);
            for (int i = 0; i < 3; ++i) (void)det_rng_int_n(&k.cw, 11);

            /* doVoidFogParticles around the updated player */
            k.phase = "randomDisplayTick";
            {
                det_rng var5 = det_new_random_role(&det, DET_CLIENT);
                int px = (int)floor(PL.view_x), py = (int)floor(PL.view_y), pz = (int)floor(PL.view_z);
                for (int i = 0; i < 1000; ++i) {
                    int bx = px + det_rng_int_n(&k.cw, 16) - det_rng_int_n(&k.cw, 16);
                    int by = py + det_rng_int_n(&k.cw, 16) - det_rng_int_n(&k.cw, 16);
                    int bz = pz + det_rng_int_n(&k.cw, 16) - det_rng_int_n(&k.cw, 16);
                    int id = world_get_block(&w, bx, by, bz) & 4095;
                    if (id == 0) {
                        if (det_rng_int_n(&k.cw, 8) > by) {
                            (void)det_rng_float(&k.cw); (void)det_rng_float(&k.cw); (void)det_rng_float(&k.cw);
                        }
                    } else {
                        particles_live_display_tick(&PL, id, bx, by, bz, &var5);
                    }
                }
            }

            /* updateEffects */
            k.phase = "updateEffects";
            k.mobs = json_get(r1, "mobs");
            particles_live_tick(&PL);
            particles_live_light_pt(&PL, pt);
            if (k.next != json_len(log)) {
                if (k.bad++ < 8) printf("  %d of %d logged calls never made natively (next from %s)\n",
                                        json_len(log) - k.next, json_len(log), src_of(json_at(log, k.next)));
            }

            /* the list against the dump, layer by layer */
            int bad = k.bad, n1 = json_len(fx1);
            struct live_fx want;
            int idx = 0;
            for (int layer = 0; layer < 4; ++layer) {
                for (int i = 0; i < PL.n; ++i) {
                    if (PL.fx[i].layer != layer) continue;
                    while (idx < n1 && jint(json_get(json_at(fx1, idx), "layer")) < layer) {
                        if (bad++ < 8) printf("  t=%d: oracle FX %d has no native counterpart\n", jint(json_get(r1, "t")), idx);
                        ++idx;
                    }
                    if (idx >= n1 || jint(json_get(json_at(fx1, idx), "layer")) != layer) {
                        if (bad++ < 8) printf("  t=%d: native %s in layer %d past the oracle's list\n", jint(json_get(r1, "t")), particles_live_class(PL.fx[i].kind), layer);
                        continue;
                    }
                    particles_live_from_json(json_at(fx1, idx), &want, &w);
                    char where[64];
                    snprintf(where, sizeof where, "t=%d fx %d", jint(json_get(r1, "t")), idx);
                    bad += neg ? (compare_fx(&PL.fx[i], &want, 1, where) != 0) : compare_fx(&PL.fx[i], &want, 1, where);
                    ++idx;
                    if (!neg) ++fx_checked;
                }
            }
            if (idx < n1 && bad++ < 8) printf("  t=%d: %d oracle FX left over\n", jint(json_get(r1, "t")), n1 - idx);
            if (neg) {
                ++negs;
                neg_caught += bad > 0;
            } else {
                if (bad) printf("FAIL particles t=%d: %d mismatches\n", jint(json_get(r1, "t")), bad);
                bad_total += bad;
                spawns_checked += k.matched;
                ++ticks;
            }
            if (neg || !(json_len(log) || PL.n)) break;
        }
    }
    printf("particles: %d ticks, %d FX compared field by field, %d native spawns matched against the log\n",
           ticks, fx_checked, spawns_checked);
    if (bad_total) { printf("FAIL particles: %d mismatches\n", bad_total); ++fails; }
    if (negs && !fx_checked && !spawns_checked)
        printf("note: no FX and no native spawn in these ticks, so no particle tick reads the Math stream\n");
    else if (negs) {
        if (!neg_caught) { printf("FAIL negative: a tick one Math draw late went unnoticed in all %d ticks\n", negs); ++fails; }
        else printf("ok negative: a particle tick one Math draw late is caught in %d of %d ticks\n", neg_caught, negs);
    }
    world_free(&w);
    det_free(&det);
}

/* ------------------------------------------------------------------ 6.
 * The tile entities' client ticks (raster_tileent.c). Between two frame rows
 * one tick apart, each chest and ender chest of the earlier row's "tes" steps
 * its lid under the later row's numPlayersUsing (S24 reaches the client before
 * the tick's tile entities update) to the later row's lidAngle and
 * prevLidAngle, bit for bit; each spawner the player is within 16 blocks of
 * counts its delay down and turns from the earlier row's rotation to the
 * later row's, and one out of range keeps both. A tile entity the chunk
 * renderer made in between is not in the earlier row and is skipped.
 * Negatives: a lid stepped under the earlier row's count where the count
 * changed, and a spawner stepped twice, are caught. */
static const struct jval *te_find_row(const struct jval *list, const struct jval *e)
{
    for (int i = 0; i < json_len(list); ++i) {
        const struct jval *o = json_at(list, i);
        if (jint(json_get(o, "x")) == jint(json_get(e, "x")) && jint(json_get(o, "y")) == jint(json_get(e, "y")) &&
            jint(json_get(o, "z")) == jint(json_get(e, "z"))) {
            const char *a = json_str(json_get(o, "k")), *b = json_str(json_get(e, "k"));
            return a && b && !strcmp(a, b) ? o : NULL;
        }
    }
    return NULL;
}

static void check_tileents(struct jval **rows, int nrows)
{
    int lids = 0, spins = 0, idle = 0, bad = 0, neg_lid = 0, neg_lid_n = 0, neg_spin = 0;
    for (int r = 0; r + 1 < nrows; ++r) {
        const struct jval *r0 = rows[r], *r1 = rows[r + 1];
        if (jint(json_get(r1, "t")) != jint(json_get(r0, "t")) + 1) continue;
        const struct jval *l0 = json_get(json_get(r0, "tes"), "list"), *l1 = json_get(json_get(r1, "tes"), "list");
        const struct jval *pl = json_get(r1, "pl");
        double px = jd(json_get(pl, "x")), py = jd(json_get(pl, "y")), pz = jd(json_get(pl, "z"));
        int t = jint(json_get(r1, "t"));
        for (int i = 0; i < json_len(l1); ++i) {
            const struct jval *e1 = json_at(l1, i), *e0 = te_find_row(l0, e1);
            const char *k = json_str(json_get(e1, "k"));
            if (!e0 || !k) continue;
            int x = jint(json_get(e1, "x")), y = jint(json_get(e1, "y")), z = jint(json_get(e1, "z"));
            if (!strcmp(k, "TileEntityChest") || !strcmp(k, "TileEntityEnderChest")) {
                const struct jval *adj = json_get(e1, "adj");
                int lead = !adj || (!jint(json_at(adj, 0)) && !jint(json_at(adj, 2)));
                float lid = jf(json_get(e0, "lid")), prev = jf(json_get(e0, "plid"));
                float want = jf(json_get(e1, "lid")), want_prev = jf(json_get(e1, "plid"));
                int u0 = jint(json_get(e0, "using")), u1 = jint(json_get(e1, "using"));
                raster_tileents_lid_tick(&lid, &prev, u1, lead);
                if (memcmp(&lid, &want, 4) || memcmp(&prev, &want_prev, 4)) {
                    printf("FAIL t=%d %s (%d %d %d): lid %.9g prev %.9g, oracle %.9g %.9g\n", t, k, x, y, z,
                           lid, prev, want, want_prev);
                    ++bad;
                }
                ++lids;
                if (u0 != u1) {
                    float nl = jf(json_get(e0, "lid")), np = jf(json_get(e0, "plid"));
                    raster_tileents_lid_tick(&nl, &np, u0, lead);
                    neg_lid += memcmp(&nl, &want, 4) != 0;
                    ++neg_lid_n;
                }
            } else if (!strcmp(k, "TileEntityMobSpawner")) {
                double rot = jd(json_get(e0, "rot")), prot = jd(json_get(e0, "prot"));
                int delay = jint(json_get(e0, "delay"));
                double want = jd(json_get(e1, "rot")), want_prev = jd(json_get(e1, "prot"));
                int want_delay = jint(json_get(e1, "delay"));
                double dx = px - ((double)x + 0.5), dy = py - ((double)y + 0.5), dz = pz - ((double)z + 0.5);
                if (dx * dx + dy * dy + dz * dz < 16.0 * 16.0) {
                    double nr = rot, np = prot;
                    int nd = delay;
                    raster_tileents_spawner_tick(&rot, &prot, &delay);
                    raster_tileents_spawner_tick(&nr, &np, &nd);
                    raster_tileents_spawner_tick(&nr, &np, &nd);
                    neg_spin += memcmp(&nr, &want, 8) != 0;
                    ++spins;
                } else
                    ++idle;
                if (memcmp(&rot, &want, 8) || memcmp(&prot, &want_prev, 8) || delay != want_delay) {
                    printf("FAIL t=%d spawner (%d %d %d): rot %.17g prev %.17g delay %d, oracle %.17g %.17g %d\n",
                           t, x, y, z, rot, prot, delay, want, want_prev, want_delay);
                    ++bad;
                }
            }
        }
    }
    printf("tile entities: %d lid steps, %d spawner turns, %d spawners out of range\n", lids, spins, idle);
    if (bad) { printf("FAIL tile entities: %d mismatches\n", bad); ++fails; }
    if (neg_lid_n) {
        if (neg_lid < neg_lid_n) { printf("FAIL negative: a lid stepped under the old count went unnoticed\n"); ++fails; }
        else printf("ok negative: a lid stepped under the earlier row's count is caught (%d)\n", neg_lid_n);
    }
    if (spins) {
        if (neg_spin < spins) { printf("FAIL negative: a spawner turned twice went unnoticed\n"); ++fails; }
        else printf("ok negative: a spawner turned twice is caught (%d)\n", spins);
    }
}

/* ------------------------------------------------------------------ 6.
 * The client view state (clientstate.c) against each row's "cs" block
 * (ClientStateProbe): at every row, getFOVMultiplier and the movementSpeed
 * attribute's value from the row's own inputs; between two rows one tick
 * apart, the tick the later row followed: its packets (the cs.pk list the
 * probe read off the receive queue at the tick's start), then
 * updateFovModifierHand (the multiplier from the earlier row's player with
 * the tick's S20), the lastLightningBolt countdown and the client bolts
 * (a bolt S2C built from the CLIENT seeder as the tick began), every living
 * entity's hurt fields, and the player's portal timer and its own Random.
 * The FOV, portal, bolt and hurt values chain from step to step while the
 * rows stay one tick apart, and each chained row's camera, lightmap, sky and
 * fog are recomputed from them (renderstate_compute) and compared with what
 * the oracle drew with. Negative controls: each piece one step wrong. */

struct cs_row {
    const struct jval *row, *cs;
    long long t;
    struct cs_fov_in fov;
    struct cs_attr attr;
    float fmt, fovnow, fmh, fmhp;
    int sprint, pid, conf;
    int splash;         /* Entity.handleWaterMovement's splash drew this tick */
    struct cs_portal portal;
    uint64_t prs, prs0, seed0;
    int lbolt;
};

static int64_t hex64(const char *s, int n)
{
    uint64_t v = 0;
    for (int i = 0; i < n && s[i]; ++i) {
        int c = s[i];
        v = v << 4 | (uint64_t)(c <= '9' ? c - '0' : (c | 32) - 'a' + 10);
    }
    return (int64_t)v;
}

/* "662a6b8d-da3e-4c1c-8813-96ea6097278d" */
static void uuid_bits(const char *u, int64_t *msb, int64_t *lsb)
{
    char h[33];
    int k = 0;
    for (const char *p = u; *p && k < 32; ++p)
        if (*p != '-') h[k++] = *p;
    h[k] = 0;
    *msb = hex64(h, 16);
    *lsb = hex64(h + 16, 16);
}

static void attr_of(const struct jval *base, const struct jval *mods, struct cs_attr *a)
{
    memset(a, 0, sizeof *a);
    a->base = jd(base);
    for (int i = 0; i < json_len(mods) && a->n < CS_ATTR_MODS; ++i) {
        const struct jval *m = json_at(mods, i);
        struct cs_attr_mod x;
        uuid_bits(json_str(json_at(m, 0)), &x.msb, &x.lsb);
        x.op = jint(json_at(m, 2));
        x.amount = jd(json_at(m, 3));
        a->m[a->n++] = x;
    }
}

static int cs_read(const struct jval *row, struct cs_row *r)
{
    memset(r, 0, sizeof *r);
    r->row = row;
    r->cs = json_get(row, "cs");
    if (!r->cs) return 0;
    const struct jval *cs = r->cs, *er = json_get(row, "er");
    r->t = jint(json_get(row, "t"));
    r->fov.flying = jint(json_get(cs, "fly"));
    r->fov.walk_speed = jf(json_get(cs, "walk"));
    attr_of(json_get(cs, "msb"), json_get(cs, "mods"), &r->attr);
    r->fov.move_speed = jd(json_get(cs, "msp"));
    r->fov.bow = jint(json_get(cs, "use")) == 261;
    r->fov.use_duration = jint(json_get(cs, "used"));
    r->fmt = jf(json_get(cs, "fmt"));
    r->fovnow = jf(json_get(cs, "fovnow"));
    r->fmh = jf(json_get(er, "fmh"));
    r->fmhp = jf(json_get(er, "fmhp"));
    r->sprint = jint(json_get(cs, "sprint"));
    r->pid = jint(json_get(cs, "pid"));
    r->conf = jint(json_get(cs, "conf"));
    r->portal.time = jf(json_get(cs, "tip"));
    r->portal.prev = jf(json_get(cs, "ptip"));
    r->portal.in_portal = jint(json_get(cs, "inportal"));
    r->portal.time_until = jint(json_get(cs, "tup"));
    r->prs = hex48(json_get(cs, "prs"));
    r->prs0 = hex48(json_get(cs, "prs0"));
    r->seed0 = hex48(json_get(cs, "seed0"));
    r->lbolt = jint(json_get(cs, "lbolt"));
    const struct jval *sp = json_get(row, "spawns");
    for (int i = 0; i < json_len(sp); ++i) {
        const char *src = json_str(json_get(json_at(sp, i), "src"));
        if (src && !strncmp(src, "Entity.handleWaterMovement", 26)) r->splash = 1;
    }
    return 1;
}

static void hurt_of(const struct jval *h, struct cs_hurt *o)
{
    memset(o, 0, sizeof *o);
    o->hurt = jint(json_get(h, "hurt"));
    o->max_hurt = jint(json_get(h, "mhurt"));
    o->res = jint(json_get(h, "hres"));
    o->max_res = jint(json_get(h, "mhres"));
    o->attacked_yaw = jf(json_get(h, "aaty"));
    o->limb = jf(json_get(h, "limba"));
    o->health = jf(json_get(h, "hp"));
    o->last_damage = jf(json_get(h, "ldmg"));
}

static const struct jval *hurt_by_id(const struct jval *cs, int id)
{
    const struct jval *a = json_get(cs, "hurt");
    for (int i = 0; i < json_len(a); ++i)
        if (jint(json_get(json_at(a, i), "id")) == id) return json_at(a, i);
    return NULL;
}

static void weather_of(const struct cs_row *r, struct cs_weather *w)
{
    memset(w, 0, sizeof *w);
    w->last_bolt = r->lbolt;
    const struct jval *a = json_get(r->cs, "bolts");
    for (int i = 0; i < json_len(a) && w->n < CS_BOLTS; ++i) {
        const struct jval *b = json_at(a, i);
        struct cs_bolt *x = &w->b[w->n++];
        x->id = jint(json_get(b, "id"));
        x->state = jint(json_get(b, "ls"));
        x->living = jint(json_get(b, "blt"));
        x->vertex = strtoll(json_str(json_get(b, "bv")), NULL, 10);
        x->rand.seed = hex48(json_get(b, "rs"));
        x->age = jint(json_get(b, "age"));
    }
}

static int weather_equal(const struct cs_weather *g, const struct cs_row *r, int print)
{
    struct cs_weather w;
    weather_of(r, &w);
    int bad = g->last_bolt != w.last_bolt || g->n != w.n;
    for (int i = 0; !bad && i < w.n; ++i)
        bad = g->b[i].id != w.b[i].id || g->b[i].state != w.b[i].state || g->b[i].living != w.b[i].living ||
              g->b[i].vertex != w.b[i].vertex || g->b[i].rand.seed != w.b[i].rand.seed || g->b[i].age != w.b[i].age;
    if (bad && print) {
        printf("FAIL t=%lld weather: lastLightningBolt %d, %d bolts; oracle %d, %d bolts\n", r->t, g->last_bolt, g->n,
               w.last_bolt, w.n);
        for (int i = 0; i < g->n || i < w.n; ++i)
            printf("  bolt %d: native id %d state %d living %d rand %012llx | oracle id %d state %d living %d rand %012llx\n",
                   i, i < g->n ? g->b[i].id : -1, i < g->n ? g->b[i].state : 0, i < g->n ? g->b[i].living : 0,
                   i < g->n ? (unsigned long long)g->b[i].rand.seed : 0ULL, i < w.n ? w.b[i].id : -1,
                   i < w.n ? w.b[i].state : 0, i < w.n ? w.b[i].living : 0,
                   i < w.n ? (unsigned long long)w.b[i].rand.seed : 0ULL);
    }
    return !bad;
}

/* the packets of the tick that led to row R, in order */
static void cs_packets(const struct cs_row *r, struct cs_attr *attr, struct cs_weather *w, struct cs_hurt *hs,
                       const int *ids, int nh, jrand *prand, int *has_set, int late_seed)
{
    const struct jval *pk = json_get(r->cs, "pk");
    det_state d;
    det_init(&d);
    d.seeder[DET_CLIENT].r.seed = r->seed0;
    if (late_seed) (void)det_seeder_next_long(&d, DET_CLIENT);
    for (int i = 0; i < json_len(pk); ++i) {
        const struct jval *p = json_at(pk, i);
        const char *k = json_str(json_get(p, "p"));
        int id = jint(json_get(p, "id"));
        if (!k) continue;
        if (!strcmp(k, "S20") && attr && id == r->pid) {
            const struct jval *as = json_get(p, "attrs");
            for (int j = 0; j < json_len(as); ++j) {
                const struct jval *s = json_at(as, j);
                const char *n = json_str(json_get(s, "n"));
                if (n && !strcmp(n, "generic.movementSpeed")) attr_of(json_get(s, "base"), json_get(s, "mods"), attr);
            }
        } else if (!strcmp(k, "S2C") && w && jint(json_get(p, "type")) == 1) {
            clientstate_bolt_spawn(w, id, &d);
        } else if (!strcmp(k, "S19") && hs) {
            for (int j = 0; j < nh; ++j)
                if (ids[j] == id) clientstate_health_update(&hs[j], jint(json_get(p, "st")), id == r->pid ? prand : NULL);
        } else if (!strcmp(k, "S0B") && hs && jint(json_get(p, "an")) == 1) {
            for (int j = 0; j < nh; ++j)
                if (ids[j] == id) clientstate_hurt_animation(&hs[j]);
        } else if (!strcmp(k, "S1C") && hs) {
            /* the data watcher's health: set as sent, before any S06 */
            for (int j = 0; j < nh; ++j)
                if (ids[j] == id) hs[j].health = jf(json_get(p, "hp"));
        } else if (!strcmp(k, "S06") && hs) {
            for (int j = 0; j < nh; ++j)
                if (ids[j] == r->pid) clientstate_set_sp_health(&hs[j], jf(json_get(p, "hp")), has_set, NULL);
        }
    }
    det_free(&d);
}

/* renderstate_compute from the row with the native client state in place of
 * the recorded; 0 when every compared output is the oracle's */
static int cs_camera_diff(const struct jval *row, float fmh, float fmhp, float portal, float pportal, int lbolt,
                          const struct cs_hurt *ph, const char **what)
{
    struct rs_in in;
    if (!renderstate_in_from_row(row, &in)) { *what = "inputs unreadable"; return 1; }
    in.fmh = fmh; in.fmhp = fmhp;
    in.portal = portal; in.pportal = pportal;
    in.lbolt = lbolt;
    if (ph) { in.hurt = ph->hurt; in.mhurt = ph->max_hurt; in.aaty = ph->attacked_yaw; in.hp = ph->health; }
    struct rs_out o;
    renderstate_compute(&in, &o);
    const struct jval *out = json_get(row, "out"), *g = json_get(row, "g");
    for (int i = 0; i < 16; ++i) {
        float a = jf(json_at(json_get(out, "proj"), i)), b = jf(json_at(json_get(out, "mv"), i));
        if (memcmp(&a, &o.proj[i], 4)) { *what = "out.proj"; return 1; }
        if (memcmp(&b, &o.mv[i], 4)) { *what = "out.mv"; return 1; }
    }
    for (int i = 0; i < 256; ++i)
        if (jint(json_at(json_get(out, "lm"), i)) != o.lm[i]) { *what = "out.lm"; return 1; }
    for (int i = 0; i < 3; ++i) {
        double s = jd(json_at(json_get(g, "sky"), i));
        float c = jf(json_at(json_get(json_get(out, "glfog"), "c"), i));
        if (memcmp(&s, &o.sky[i], 8)) { *what = "g.sky"; return 1; }
        if (memcmp(&c, &o.gfogc[i], 4)) { *what = "glfog.c"; return 1; }
    }
    return 0;
}

static void check_clientstate(struct jval **rows, int nrows)
{
    if (nrows == 0 || !json_get(rows[0], "cs")) return;
    struct cs_row *R = calloc((size_t)nrows, sizeof *R);
    for (int r = 0; r < nrows; ++r) cs_read(rows[r], &R[r]);

    /* at every row: the pure functions */
    int pure = 0, bow_rows = 0, bow_caught = 0;
    for (int r = 0; r < nrows; ++r) {
        double v = clientstate_attr_value(&R[r].attr);
        if (memcmp(&v, &R[r].fov.move_speed, 8)) {
            printf("FAIL t=%lld movementSpeed %.17g from its base and %d modifiers, oracle %.17g\n", R[r].t, v,
                   R[r].attr.n, R[r].fov.move_speed);
            ++fails;
        }
        float m = clientstate_fov_multiplier(&R[r].fov);
        if (memcmp(&m, &R[r].fovnow, 4)) {
            printf("FAIL t=%lld getFOVMultiplier %.9g, oracle %.9g\n", R[r].t, m, R[r].fovnow);
            ++fails;
        }
        if (R[r].fov.bow && R[r].fov.use_duration > 1 && R[r].fov.use_duration < 20) {
            /* negative: the bow's draw not squared */
            struct cs_fov_in lin = R[r].fov;
            float l = 1.0F;
            l = (float)((double)l * ((lin.move_speed / (double)lin.walk_speed + 1.0) / 2.0));
            l *= 1.0F - (float)lin.use_duration / 20.0F * 0.15F;
            bow_caught |= memcmp(&l, &R[r].fovnow, 4) != 0;
            ++bow_rows;
        }
        ++pure;
    }

    /* the ticks between rows */
    int steps = 0, fov_steps = 0, attr_steps = 0, portal_draws = 0, rand_checked = 0, hurt_checked = 0;
    int hurt_events = 0, bolts_spawned = 0, bolt_steps = 0, cams = 0;
    int late_fmt_caught = 0, late_fmt_tried = 0, nodraw_caught = 0, notick_caught = 0, notick_tried = 0;
    int lateseed_caught = 0, nocount_caught = 0, nocount_tried = 0, fmh1_caught = 0, fmh1_tried = 0;
    int noportal_tried = 0, noportal_caught = 0, nobolt_tried = 0, nobolt_caught = 0, nohurt_tried = 0, nohurt_caught = 0;
    int have = 0;
    float fmh = 0, fmhp = 0;
    struct cs_portal portal = {0};
    struct cs_weather w = {0};
    for (int r = 0; r + 1 < nrows; ++r) {
        struct cs_row *a = &R[r], *b = &R[r + 1];
        if (!a->cs || !b->cs) continue;
        if (b->t != a->t + 1) { have = 0; continue; }
        if (!have) {
            fmh = a->fmh; fmhp = a->fmhp;
            portal = a->portal;
            weather_of(a, &w);
            have = 1;
        }
        ++steps;

        /* updateFovModifierHand: the earlier row's player after this tick's S20 */
        struct cs_attr attr = a->attr;
        struct cs_weather w1 = w;
        cs_packets(b, &attr, NULL, NULL, NULL, 0, NULL, NULL, 0);
        struct cs_fov_in fi = a->fov;
        fi.move_speed = clientstate_attr_value(&attr);
        /* a bow let go stops in runTick's input handling, before
         * updateRenderer; a draw begun there reads a duration of 0 */
        fi.bow = a->fov.bow && b->fov.bow;
        float mult = clientstate_fov_multiplier(&fi);
        if (memcmp(&mult, &b->fmt, 4)) {
            printf("FAIL t=%lld fovMultiplierTemp %.9g, oracle %.9g\n", b->t, mult, b->fmt);
            ++fails;
        }
        if (memcmp(&a->fovnow, &b->fmt, 4)) {
            /* negative: the multiplier as the earlier frame had it (the tick's
             * S20 or bow release left out) */
            ++late_fmt_tried;
            late_fmt_caught |= memcmp(&a->fovnow, &mult, 4) != 0;
        }
        clientstate_fov_hand(&fmh, &fmhp, mult);
        if (memcmp(&fmh, &b->fmh, 4) || memcmp(&fmhp, &b->fmhp, 4)) {
            printf("FAIL t=%lld fovModifierHand %.9g prev %.9g, oracle %.9g %.9g\n", b->t, fmh, fmhp, b->fmh, b->fmhp);
            ++fails;
            fmh = b->fmh; fmhp = b->fmhp;
        }
        ++fov_steps;
        /* setSprinting after updateRenderer: the attribute the later row holds */
        if (a->sprint != b->sprint) clientstate_attr_sprint(&attr, b->sprint);
        {
            double v = clientstate_attr_value(&attr);
            if (memcmp(&v, &b->fov.move_speed, 8)) {
                printf("FAIL t=%lld movementSpeed after the tick %.17g, oracle %.17g\n", b->t, v, b->fov.move_speed);
                ++fails;
            }
            ++attr_steps;
        }

        /* the hurt fields: every living entity in both rows */
        const struct jval *hl = json_get(b->cs, "hurt");
        int nh = json_len(hl), ids[256];
        struct cs_hurt hs[256], plain[256];
        if (nh > 256) nh = 256;
        for (int i = 0; i < nh; ++i) {
            ids[i] = jint(json_get(json_at(hl, i), "id"));
            const struct jval *h0 = hurt_by_id(a->cs, ids[i]);
            if (!h0) { ids[i] = -1; continue; }
            hurt_of(h0, &hs[i]);
        }
        jrand prand = {b->prs0};
        int has_set = 1;
        cs_packets(b, NULL, &w1, hs, ids, nh, &prand, &has_set, 0);
        for (int i = 0; i < nh; ++i) {
            if (ids[i] < 0) continue;
            struct cs_hurt want;
            hurt_of(json_at(hl, i), &want);
            if (hs[i].hurt == 10) ++hurt_events;
            plain[i] = hs[i];
            clientstate_hurt_tick(&hs[i]);
            int player = ids[i] == b->pid;
            if (player) {
                const struct jval *pl = json_get(b->row, "pl");
                clientstate_limb_step(&hs[i], jd(json_get(pl, "x")) - jd(json_get(pl, "px")),
                                      jd(json_get(pl, "z")) - jd(json_get(pl, "pz")));
            }
            int bad = hs[i].hurt != want.hurt || hs[i].max_hurt != want.max_hurt || hs[i].res != want.res ||
                      memcmp(&hs[i].attacked_yaw, &want.attacked_yaw, 4);
            if (player)
                bad |= memcmp(&hs[i].health, &want.health, 4) || memcmp(&hs[i].last_damage, &want.last_damage, 4) ||
                       memcmp(&hs[i].limb, &want.limb, 4);
            if (bad) {
                printf("FAIL t=%lld entity %d hurt %d/%d res %d yaw %g hp %g limb %.9g; oracle %d/%d res %d yaw %g hp %g limb %.9g\n",
                       b->t, ids[i], hs[i].hurt, hs[i].max_hurt, hs[i].res, hs[i].attacked_yaw, hs[i].health, hs[i].limb,
                       want.hurt, want.max_hurt, want.res, want.attacked_yaw, want.health, want.limb);
                ++fails;
            }
            if (plain[i].hurt > 0) {
                /* negative: no onEntityUpdate countdown */
                ++notick_tried;
                notick_caught |= plain[i].hurt != want.hurt;
            }
            ++hurt_checked;
        }

        /* the weather: this tick's S2C bolts, the countdown, the bolts' updates */
        {
            struct cs_weather late = w;
            int spawned = 0;
            const struct jval *pk = json_get(b->cs, "pk");
            for (int i = 0; i < json_len(pk); ++i)
                spawned += json_str(json_get(json_at(pk, i), "p")) && !strcmp(json_str(json_get(json_at(pk, i), "p")), "S2C");
            bolts_spawned += spawned;
            clientstate_weather_tick(&w1);
            if (!weather_equal(&w1, b, 1)) { ++fails; weather_of(b, &w1); }
            if (spawned) {
                cs_packets(b, NULL, &late, NULL, NULL, 0, NULL, NULL, 1);
                clientstate_weather_tick(&late);
                lateseed_caught |= !weather_equal(&late, b, 0);
            }
            if (w.last_bolt > 0 || w.n) {
                /* negative: lastLightningBolt never counted down */
                struct cs_weather nc = w;
                cs_packets(b, NULL, &nc, NULL, NULL, 0, NULL, NULL, 0);
                int lb = nc.last_bolt;
                clientstate_weather_tick(&nc);
                if (nc.last_bolt != 2) nc.last_bolt = lb;
                ++nocount_tried;
                nocount_caught |= !weather_equal(&nc, b, 0);
            }
            w = w1;
            if (w.n || w.last_bolt) ++bolt_steps;
        }

        /* the portal timer, and the player's Random over the tick */
        {
            struct cs_portal p = portal;
            p.in_portal = a->portal.in_portal;
            p.time_until = a->portal.time_until;
            jrand pr = prand;
            int draws = clientstate_portal_tick(&p, a->conf, &pr);
            portal_draws += draws;
            if (memcmp(&p.time, &b->portal.time, 4) || memcmp(&p.prev, &b->portal.prev, 4)) {
                printf("FAIL t=%lld timeInPortal %.9g prev %.9g, oracle %.9g %.9g\n", b->t, p.time, p.prev,
                       b->portal.time, b->portal.prev);
                ++fails;
                p.time = b->portal.time; p.prev = b->portal.prev;
            }
            /* the player's own Random: quiet ticks only (a sprint's particles,
             * an effect's particles and a splash into water draw on it too) */
            if (!a->sprint && !b->sprint && !b->splash && !json_len(json_get(a->cs, "eff")) &&
                !json_len(json_get(b->cs, "eff"))) {
                if (pr.seed != b->prs) {
                    printf("FAIL t=%lld the player's Random %012llx after the tick, oracle %012llx\n", b->t,
                           (unsigned long long)pr.seed, (unsigned long long)b->prs);
                    ++fails;
                }
                if (draws) nodraw_caught |= prand.seed != b->prs;
                ++rand_checked;
            }
            portal = p;
        }

        /* the camera from the native state */
        {
            const struct cs_hurt *ph = NULL;
            for (int i = 0; i < nh; ++i)
                if (ids[i] == b->pid) ph = &hs[i];
            const char *what = NULL;
            if (cs_camera_diff(b->row, fmh, fmhp, portal.time, portal.prev, w.last_bolt, ph, &what)) {
                printf("FAIL t=%lld the frame from the native client state: %s differs\n", b->t, what);
                ++fails;
            }
            ++cams;
            if (fmh != 1.0F || fmhp != 1.0F) {
                ++fmh1_tried;
                fmh1_caught |= cs_camera_diff(b->row, 1.0F, 1.0F, portal.time, portal.prev, w.last_bolt, ph, &what);
            }
            if (portal.time > 0.0F || portal.prev > 0.0F) {
                ++noportal_tried;
                noportal_caught |= cs_camera_diff(b->row, fmh, fmhp, 0.0F, 0.0F, w.last_bolt, ph, &what);
            }
            if (w.last_bolt > 0) {
                ++nobolt_tried;
                nobolt_caught |= cs_camera_diff(b->row, fmh, fmhp, portal.time, portal.prev, 0, ph, &what);
            }
            if (ph && ph->hurt > 0) {
                struct cs_hurt calm = *ph;
                calm.hurt = 0;
                ++nohurt_tried;
                nohurt_caught |= cs_camera_diff(b->row, fmh, fmhp, portal.time, portal.prev, w.last_bolt, &calm, &what);
            }
        }
    }
    printf("client state: %d rows (getFOVMultiplier, movementSpeed), %d one-tick steps: %d FOV, %d attribute, "
           "%d hurt fields (%d hurts), %d weather (%d bolt spawns), %d portal draws, %d player Random checks, "
           "%d cameras from the native state\n",
           pure, steps, fov_steps, attr_steps, hurt_checked, hurt_events, bolt_steps, bolts_spawned, portal_draws,
           rand_checked, cams);
#define NEG(tried, caught, what)                                                                                     \
    if (tried) {                                                                                                     \
        if (!(caught)) { printf("FAIL negative: %s went unnoticed\n", what); ++fails; }                             \
        else printf("ok negative: %s is caught\n", what);                                                            \
    }
    NEG(bow_rows, bow_caught, "a bow draw not squared")
    NEG(late_fmt_tried, late_fmt_caught, "an FOV multiplier without the tick's own S20 or bow release")
    NEG(portal_draws && rand_checked, nodraw_caught, "a portal entry without its sound draw")
    NEG(notick_tried, notick_caught, "hurtTime without its countdown")
    NEG(bolts_spawned, lateseed_caught, "a bolt built one seeder draw late")
    NEG(nocount_tried, nocount_caught, "lastLightningBolt without its countdown")
    NEG(fmh1_tried, fmh1_caught, "a frame without fovModifierHand")
    NEG(noportal_tried, noportal_caught, "a frame without timeInPortal")
    NEG(nobolt_tried, nobolt_caught, "a frame without lastLightningBolt")
    NEG(nohurt_tried, nohurt_caught, "a frame without hurtTime")
#undef NEG
    free(R);
}

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: test_rendertick RECORDING_DIR\n"); return 2; }
    const char *dir = argv[1];
    char p[1200];
    snprintf(p, sizeof p, "%s/state", dir);
    struct texanim a;
    if (texanim_load(&a, p) != 0) {
        printf("SKIP rendertick %s: no state/anim.json\n", dir);
        return 0;
    }

    /* 1. the dumps */
    snprintf(p, sizeof p, "%s/atlas.json", dir);
    struct jval *aj = read_json(p);
    int aw = jint(json_get(aj, "atlas_width")), ah = jint(json_get(aj, "atlas_height"));
    snprintf(p, sizeof p, "%s/atlas.rgba", dir);
    unsigned char *atlas = aw > 0 && ah > 0 ? read_file(p, (size_t)aw * ah * 4) : NULL;
    struct texanim_state st;
    if (!atlas || texanim_state_read(&a, json_get(aj, "anim"), &st) != 0) {
        printf("FAIL %s: atlas.rgba or atlas.json's anim state unreadable\n", dir);
        ++fails;
    } else {
        check_rebuild(&a, &st, TEXANIM_BLOCKS, atlas, aw, ah, "block atlas");
    }
    free(atlas);
    json_free(aj);

    snprintf(p, sizeof p, "%s/state/frames.jsonl", dir);
    FILE *f = fopen(p, "rb");
    if (!f) { printf("FAIL %s: no frames.jsonl\n", dir); return 1; }
    struct lines in;
    lines_init(&in);
    lines_file(&in, f);
    struct jval *rows[4096];
    int nrows = 0;
    for (const char *s = lines_next(&in); s && nrows < 4096; s = lines_next(&in))
        if (s[0]) rows[nrows++] = json_parse(strdup(s));
    lines_free(&in);
    fclose(f);

    if (nrows > 0) {
        snprintf(p, sizeof p, "%s/state/gui.json", dir);
        struct jval *gui = read_json(p);
        int iw = jint(json_get(json_get(gui, "items"), "w")), ih = jint(json_get(json_get(gui, "items"), "h"));
        snprintf(p, sizeof p, "%s/state/gui_items.rgba", dir);
        unsigned char *items = iw > 0 && ih > 0 ? read_file(p, (size_t)iw * ih * 4) : NULL;
        if (items && texanim_state_read(&a, json_get(rows[0], "anim"), &st) == 0)
            check_rebuild(&a, &st, TEXANIM_ITEMS, items, iw, ih, "item atlas");
        else
            printf("note: no item atlas dump to rebuild in %s\n", dir);
        free(items);
        json_free(gui);
    }

    /* 2. the ticks between rows */
    int steps = 0, spans = 0, compass_steps = 0, bare_caught = 0;
    for (int r = 0; r + 1 < nrows; ++r) {
        long long t0 = jint(json_get(rows[r], "t")), t1 = jint(json_get(rows[r + 1], "t"));
        struct texanim_state s0, s1;
        if (texanim_state_read(&a, json_get(rows[r], "anim"), &s0) != 0 ||
            texanim_state_read(&a, json_get(rows[r + 1], "anim"), &s1) != 0) {
            printf("FAIL t=%lld: a row's anim state does not match anim.json\n", t0);
            ++fails;
            continue;
        }
        if (t1 <= t0) continue;
        struct texanim_world w = world_of(rows[r], rows[r + 1]);
        /* the frame drawn at row r first: its item frames' compass steps, the
         * player as the row has it */
        struct texanim_world wr = w;
        wr.yaw = (double)jf(json_get(json_get(rows[r], "pl"), "yaw"));
        struct texanim_state bare = s0;
        int drawn = compass_frames_drawn(dir, rows[r]);
        for (int k = 0; k < drawn; ++k) texanim_compass_render_step(&a, &s0, &wr, NULL);
        compass_steps += drawn;
        /* off the surface the clock and the compass draw on the client Math
         * stream, which a one-tick step has from the later row's er.cm0 */
        det_state dd;
        det_init(&dd);
        const char *cm0 = json_str(json_get(json_get(rows[r + 1], "er"), "cm0"));
        int off = !w.surface && t1 == t0 + 1 && cm0;
        if (off) dd.math[DET_CLIENT].r.seed = strtoull(cm0, NULL, 16) & JR_MASK;
        for (long long t = t0; t < t1; ++t) texanim_tick(&a, &s0, &w, off ? &dd : NULL);
        det_free(&dd);
        if (drawn && t1 == t0 + 1) {
            for (long long t = t0; t < t1; ++t) texanim_tick(&a, &bare, &w, NULL);
            bare_caught |= memcmp(&bare.compass_a, &s1.compass_a, 8) != 0;
        }
        fails += compare_states(&a, &s0, &s1, t1 == t0 + 1, t1);
        if (t1 == t0 + 1) ++steps; else ++spans;
    }
    printf("ticks: %d one-tick steps (every counter and dial), %d longer spans (sprite counters), "
           "%d item-frame compass steps\n", steps, spans, compass_steps);
    if (compass_steps) {
        if (!bare_caught) { printf("FAIL negative: the item-frame compass steps changed nothing\n"); ++fails; }
        else printf("ok negative: without the item-frame compass steps the compass differs\n");
    }

    /* 3. the torch flicker */
    int flickers = 0, late_caught = 0;
    int64_t flick_seed = 0;
    int flick_pinned = pinned_seed(dir, &flick_seed);
    for (int r = 0; r + 1 < nrows; ++r) {
        const struct jval *e0 = json_get(rows[r], "er"), *e1 = json_get(rows[r + 1], "er");
        long long t0 = jint(json_get(rows[r], "t")), t1 = jint(json_get(rows[r + 1], "t"));
        const char *cm0 = json_str(json_get(e1, "cm0"));
        if (t1 != t0 + 1 || !cm0 || !json_get(e0, "tfdx")) continue;
        /* a tick whose packets drew on the stream first (spawned particles or
         * entities): the particle tick checks its flicker from the replayed
         * stream instead */
        {
            const struct jval *sp = json_get(rows[r + 1], "spawns");
            int packets = 0;
            for (int i = 0; i < json_len(sp); ++i)
                packets |= strstr(src_of(json_at(sp, i)), "NetHandlerPlayClient") != NULL ||
                           strstr(src_of(json_at(sp, i)), "EntityCrit2FX.<init>") != NULL;
            if (packets) continue;
        }
        det_state d;
        det_init(&d);
        d.math[DET_CLIENT].r.seed = strtoull(cm0, NULL, 16) & JR_MASK;
        if (!jint(json_get(json_get(rows[r + 1], "anim"), "surface"))) {
            struct texanim_state dials = {0};
            struct texanim_world w = {.world = 1, .surface = 0};
            texanim_dials_tick(&dials, 64, 32, &w, &d);
        }
        struct rs_flicker f = {jf(json_get(e0, "tfdx")), jf(json_get(e0, "tfdy")),
                               jf(json_get(e0, "tfx")), jf(json_get(e0, "tfy"))};
        if (flick_pinned) pinned_flicker(&f, NULL, flick_seed, t1);
        else renderstate_torch_flicker(&f, &d);
        const float want[4] = {jf(json_get(e1, "tfdx")), jf(json_get(e1, "tfdy")),
                               jf(json_get(e1, "tfx")), jf(json_get(e1, "tfy"))};
        const float got[4] = {f.dx, f.dy, f.x, f.y};
        const char *names[4] = {"torchFlickerDX", "torchFlickerDY", "torchFlickerX", "torchFlickerY"};
        for (int k = 0; k < 4; ++k)
            if (memcmp(&got[k], &want[k], 4)) {
                printf("FAIL t=%lld %s: %.9g, oracle %.9g\n", t1, names[k], got[k], want[k]);
                ++fails;
            }
        /* negative: the same step one draw late */
        det_state late;
        det_init(&late);
        late.math[DET_CLIENT].r.seed = strtoull(cm0, NULL, 16) & JR_MASK;
        (void)det_math_random_role(&late, DET_CLIENT);
        struct rs_flicker g = {jf(json_get(e0, "tfdx")), jf(json_get(e0, "tfdy")),
                               jf(json_get(e0, "tfx")), jf(json_get(e0, "tfy"))};
        /* under the pin: the flicker's stream of the tick before */
        if (flick_pinned) pinned_flicker(&g, NULL, flick_seed, t1 - 1);
        else renderstate_torch_flicker(&g, &late);
        late_caught |= memcmp(&g.x, &want[2], 4) != 0;
        det_free(&late);
        det_free(&d);
        ++flickers;
    }
    printf("torch flicker: %d one-tick steps\n", flickers);
    if (flickers) {
        if (!late_caught) { printf("FAIL negative: a flicker one draw late went unnoticed\n"); ++fails; }
        else printf("ok negative: a flicker one draw late is caught\n");
    }

    /* 5. the particle tick */
    check_particles(dir, rows, nrows);

    /* 6. the client view state */
    check_clientstate(rows, nrows);

    /* 6. the tile entities' client ticks */
    check_tileents(rows, nrows);

    /* 4. negative: one frame a tick slower must be caught at the first step */
    if (nrows > 1 && a.n[TEXANIM_BLOCKS] > 0) {
        struct texanim b = a;
        for (int i = 0; i < b.n[TEXANIM_BLOCKS]; ++i)
            if (b.s[TEXANIM_BLOCKS][i].kind == TEXANIM_SPRITE)
                for (int k = 0; k < b.s[TEXANIM_BLOCKS][i].nframes; ++k) ++b.s[TEXANIM_BLOCKS][i].time[k];
        int caught = 0;
        for (int r = 0; r + 1 < nrows && !caught; ++r) {
            struct texanim_state s0, s1;
            texanim_state_read(&a, json_get(rows[r], "anim"), &s0);
            texanim_state_read(&a, json_get(rows[r + 1], "anim"), &s1);
            long long t0 = jint(json_get(rows[r], "t")), t1 = jint(json_get(rows[r + 1], "t"));
            struct texanim_world w = world_of(rows[r], rows[r + 1]);
            for (long long t = t0; t < t1; ++t) texanim_tick(&b, &s0, &w, NULL);
            for (int i = 0; i < b.n[TEXANIM_BLOCKS]; ++i)
                caught |= s0.fc[TEXANIM_BLOCKS][i] != s1.fc[TEXANIM_BLOCKS][i] ||
                          s0.tc[TEXANIM_BLOCKS][i] != s1.tc[TEXANIM_BLOCKS][i];
        }
        if (!caught) { printf("FAIL negative: slower frame times went unnoticed\n"); ++fails; }
        else printf("ok negative: slower frame times caught\n");
    }

    for (int r = 0; r < nrows; ++r) json_free(rows[r]);
    texanim_free(&a);
    if (fails) { printf("FAIL rendertick %s: %d\n", dir, fails); return 1; }
    printf("PASS rendertick %s\n", dir);
    return 0;
}
