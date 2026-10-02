/* Gate: the native per-frame render state against the oracle's renderstate
 * dump (oracle/harness/netherite/oracle/RenderStateProbe.java; recorded with
 * `bash tests/rs.sh <tape> <frame-every> <outdir>`, which replays the tape with
 * --frame-every and --renderstate).
 *
 * For every frame in frames.jsonl: rebuild the inputs from the dump, run
 * renderstate_compute, and compare every output number bit for bit against
 * what the real client computed for that frame. Stops at the first differing
 * field, naming the frame (its tape row t) and the field. The dump's g object
 * is the oracle's own World getter values, lm are the lightmap texture's ints,
 * proj/mv are read back from the real GL with glGetFloat, and glfog is the GL
 * fog state after the prepareterrain setupFog(0).
 *
 * GL keeps unset state between calls, so a branch of setupFog that does not
 * write a field reads the previous frame's value, not ours: start/end are
 * compared on linear-fog frames (mode 0x0901) and density on exponential ones
 * (0x0800). The fog color and mode are set on every frame.
 *
 * Also checks each frame's tick against the tape row with the same t.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/renderstate.h"
#include "../engine/jmath.h"
#include "../engine/tape.h"

static int fails;
static long long frame_t;

static void fail(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(stderr, "FAIL (frame t=%lld): ", frame_t);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    ++fails;
}

/* Bit casts without type punning (memcpy), so -O2's strict aliasing cannot
 * reorder them. */
static inline float f_of(uint32_t b) { float f; memcpy(&f, &b, 4); return f; }
static inline double d_of(uint64_t b) { double d; memcpy(&d, &b, 8); return d; }
static inline uint32_t bits_f(float f) { uint32_t b; memcpy(&b, &f, 4); return b; }
static inline uint64_t bits_d(double d) { uint64_t b; memcpy(&b, &d, 8); return b; }

static void cmp_f(const char *name, uint32_t want, uint32_t got)
{
    if (want == got) return;
    fail("%s: oracle %.9g (f:%08x) native %.9g (f:%08x)",
         name, f_of(want), want, f_of(got), got);
}

static void cmp_d(const char *name, uint64_t want, uint64_t got)
{
    if (want == got) return;
    fail("%s: oracle %.17g (d:%016llx) native %.17g (d:%016llx)",
         name, d_of(want), (unsigned long long)want,
         d_of(got), (unsigned long long)got);
}

/* Array member helpers: the probe writes hex-bit strings via farr/JsonArray. */
static int get_f(const struct jval *o, const char *key, uint32_t *bits)
{
    return json_float(json_get(o, key), bits);
}

static int get_d(const struct jval *o, const char *key, uint64_t *bits)
{
    return json_double(json_get(o, key), bits);
}

static int at_f(const struct jval *a, int i, uint32_t *bits)
{
    return json_float(json_at(a, i), bits);
}

static int at_d(const struct jval *a, int i, uint64_t *bits)
{
    return json_double(json_at(a, i), bits);
}

/* g/out/glfog comparisons. GL keeps unset state between calls, so a branch of
 * setupFog that does not write a field reads the previous frame's value, not
 * ours: start/end are compared on linear-fog frames (GL_LINEAR, 0x2601) and
 * density on exponential ones (GL_EXP, 0x0800). The fog color and mode are set
 * every frame. */
static void check(const struct jval *row, const struct rs_out *o)
{
    const struct jval *g = json_get(row, "g"), *out = json_get(row, "out"),
                       *gf = json_get(out, "glfog"), *cam;
    uint32_t wf, wg;
    uint64_t wd;
    char name[64];

    if (!g || !out || !gf) { fail("row is missing g/out/glfog"); return; }

    static const char *vecs[3] = {"sky", "fog", "cloud"};
    const double *nv[3] = {o->sky, o->fog, o->cloud};
    for (int k = 0; k < 3; ++k)
    {
        const struct jval *a = json_get(g, vecs[k]);
        if (!a || json_len(a) != 3) { fail("g.%s is missing", vecs[k]); continue; }
        for (int i = 0; i < 3; ++i)
        {
            if (!at_d(a, i, &wd)) { fail("g.%s[%d] unreadable", vecs[k], i); continue; }
            snprintf(name, sizeof name, "g.%s[%d]", vecs[k], i);
            cmp_d(name, wd, bits_d(nv[k][i]));
        }
    }

    if (!get_f(g, "ang", &wf)) { fail("g.ang missing"); return; }
    cmp_f("g.ang", wf, bits_f(o->ang));
    if (get_f(g, "angr", &wf)) cmp_f("g.angr", wf, bits_f(o->angr));
    if (get_f(g, "sun", &wf)) cmp_f("g.sun", wf, bits_f(o->sun));
    if (get_f(g, "star", &wf)) cmp_f("g.star", wf, bits_f(o->star));

    const struct jval *rise = json_get(g, "rise");
    if (rise && rise->kind == J_NULL) rise = NULL;
    if (!rise && o->rise_ok) fail("g.rise: oracle null, native computed one");
    else if (rise && !o->rise_ok) fail("g.rise: oracle computed, native null");
    else if (rise)
    {
        for (int i = 0; i < 4; ++i)
        {
            if (!at_f(rise, i, &wf)) { fail("g.rise[%d] unreadable", i); continue; }
            snprintf(name, sizeof name, "g.rise[%d]", i);
            cmp_f(name, wf, bits_f(o->rise[i]));
        }
    }

    if (!get_f(out, "fcr", &wf)) { fail("out.fcr missing"); return; }
    cmp_f("out.fcr", wf, bits_f(o->fcr));
    if (get_f(out, "fcg", &wf)) cmp_f("out.fcg", wf, bits_f(o->fcg));
    if (get_f(out, "fcb", &wf)) cmp_f("out.fcb", wf, bits_f(o->fcb));

    const struct jval *lm = json_get(out, "lm");
    if (lm && json_len(lm) == 256)
    {
        for (int i = 0; i < 256; ++i)
        {
            int64_t li;
            if (!json_int(json_at(lm, i), &li)) { fail("out.lm[%d] unreadable", i); break; }
            if ((int)li != o->lm[i]) { fail("out.lm[%d]: oracle %d (0x%08x) native %d (0x%08x)", i, (int)li, (unsigned)(int)li, o->lm[i], (unsigned)o->lm[i]); break; }
        }
    }
    else fail("out.lm is missing");

    static const char *mats[2] = {"proj", "mv"};
    const float *mm[2] = {o->proj, o->mv};
    for (int k = 0; k < 2; ++k)
    {
        const struct jval *a = json_get(out, mats[k]);
        if (!a || json_len(a) != 16) { fail("out.%s is missing", mats[k]); continue; }
        for (int i = 0; i < 16; ++i)
        {
            if (!at_f(a, i, &wf)) { fail("out.%s[%d] unreadable", mats[k], i); continue; }
            snprintf(name, sizeof name, "out.%s[%d]", mats[k], i);
            cmp_f(name, wf, bits_f(mm[k][i]));
        }
    }

    cam = json_get(out, "cam");
    if (cam && get_d(cam, "x", &wd)) cmp_d("out.cam.x", wd, bits_d(o->camx));
    else fail("out.cam.x missing");
    if (cam && get_d(cam, "y", &wd)) cmp_d("out.cam.y", wd, bits_d(o->camy));
    else fail("out.cam.y missing");
    if (cam && get_d(cam, "z", &wd)) cmp_d("out.cam.z", wd, bits_d(o->camz));
    else fail("out.cam.z missing");

    const struct jval *ca = json_get(gf, "c");
    int64_t mode = 0;
    if (!ca || json_len(ca) != 4) fail("glfog.c is missing");
    else
        for (int i = 0; i < 4; ++i)
        {
            if (!at_f(ca, i, &wg)) { fail("glfog.c[%d] unreadable", i); continue; }
            snprintf(name, sizeof name, "glfog.c[%d]", i);
            cmp_f(name, wg, bits_f(o->gfogc[i]));
        }
    if (!json_int(json_get(gf, "m"), &mode)) fail("glfog.m is missing");
    else if (mode != o->gfogm) fail("glfog.m: want %lld got %d", (long long)mode, o->gfogm);

    if (mode == 0x2601)
    {
        if (get_f(gf, "s", &wg)) cmp_f("glfog.s", wg, bits_f(o->gfogs));
        if (get_f(gf, "e", &wg)) cmp_f("glfog.e", wg, bits_f(o->gfoge));
    }
    else if (mode == 0x0800)
    {
        if (get_f(gf, "d", &wg)) cmp_f("glfog.d", wg, bits_f(o->gfogd));
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    jmath_init();
    const char *dir0 = argv[1];

    if (!dir0)
    {
        fprintf(stderr, "FAIL: no dump directory given\n");
        return 1;
    }

    /* The tape this dump was recorded from: every frame's tick must name a row. */
    long long *tapes = NULL;
    int ntapes = 0, tape_cap = 0;
    {
        char p[4096];
        snprintf(p, sizeof p, "%s/tape.jsonl", dir0);
        FILE *fp = fopen(p, "r");
        if (fp)
        {
            struct lines l;
            lines_init(&l);
            lines_file(&l, fp);
            const char *s;
            while ((s = lines_next(&l)))
            {
                struct jval *r = json_parse(strdup(s));
                if (!r) continue;
                int64_t t;
                if (json_int(json_get(r, "t"), &t))
                {
                    if (ntapes == tape_cap)
                    {
                        tape_cap = tape_cap ? tape_cap * 2 : 4096;
                        tapes = realloc(tapes, (size_t)tape_cap * sizeof *tapes);
                    }
                    tapes[ntapes++] = t;
                }
                json_free(r);
            }
            fclose(fp);
        }
    }

    char p[4096];
    snprintf(p, sizeof p, "%s/frames.jsonl", dir0);
    FILE *fp = fopen(p, "r");
    if (!fp)
    {
        fprintf(stderr, "FAIL: cannot open %s\n", p);
        return 1;
    }

    struct lines l;
    lines_init(&l);
    lines_file(&l, fp);
    const char *s;
    int frames = 0;
    uint32_t last_far = 0;
    int have_far = 0;

    while ((s = lines_next(&l)))
    {
        struct jval *row = json_parse(strdup(s));
        if (!row) { fail("bad row: %.60s", s); break; }

        int64_t t = -1;
        json_int(json_get(row, "t"), &t);
        frame_t = (long long)t;
        ++frames;

        int known = 0;
        for (int i = 0; i < ntapes; ++i)
            if (tapes[i] == t) { known = 1; break; }
        if (!known) { fail("tick %lld has no tape row", frame_t); json_free(row); break; }

        struct rs_in in;
        if (!renderstate_in_from_row(row, &in))
        {
            json_free(row);
            fail("cannot read the frame's inputs");
            break;
        }

        struct rs_out o;
        renderstate_compute(&in, &o);
        check(row, &o);

        /* The dump snapshots farPlaneDistance after updateFogColor but before
         * this frame's setupCameraTransform sets it, so the oracle's value is
         * the previous frame's farPlaneDistance; the first frame still has 0. */
        {
            uint32_t ob;
            if (get_f(json_get(row, "out"), "far", &ob))
            {
                if (have_far) cmp_f("out.far(prev)", ob, last_far);
                last_far = bits_f(o.far);
                have_far = 1;
            }
        }

        json_free(row);
        if (fails) break;
    }
    fclose(fp);
    free(tapes);

    if (!fails) printf("renderstate: %d frames exact (%s)\n", frames, dir0);
    return fails != 0;
}