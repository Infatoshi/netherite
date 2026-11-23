/* A RenderStateProbe frame row (frames.jsonl) as renderstate_compute's
 * inputs: the opt, er, pl, wo, vp and disp objects and the partial tick, read
 * bit for bit (floats and doubles travel as raw-bit hex). Shared by
 * test_renderstate, which checks every output against the row, and
 * test_rendertick, which recomputes a row's camera from the client state the
 * native steps (clientstate.c). */
#include <stdint.h>
#include <string.h>

#include "renderstate.h"
#include "tape.h"

static inline float f_of(uint32_t b) { float f; memcpy(&f, &b, 4); return f; }
static inline double d_of(uint64_t b) { double d; memcpy(&d, &b, 8); return d; }

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

/* The frame row's objects into rs_in. */
int renderstate_in_from_row(const struct jval *row, struct rs_in *in)
{
    const struct jval *opt = json_get(row, "opt"), *er = json_get(row, "er"),
                       *pl = json_get(row, "pl"), *wo = json_get(row, "wo"),
                       *vp = json_get(row, "vp"), *disp = json_get(row, "disp");
    const struct jval *lbt;
    int64_t v;
    uint32_t fb;
    uint64_t db;

    if (!opt || !er || !pl || !wo || !vp || !disp) return 0;

    memset(in, 0, sizeof *in);
    if (!json_int(json_get(opt, "rd"), &v)) return 0;
    in->rd = (int)v;
    if (!json_int(json_get(opt, "bob"), &v)) return 0;
    in->bob = (int)v;
    if (!json_int(json_get(opt, "tpv"), &v)) return 0;
    in->tpv = (int)v;
    if (!json_int(json_get(opt, "ana"), &v)) return 0;
    in->ana = (int)v;
    if (!json_int(json_get(opt, "dbgcam"), &v)) return 0;
    in->dbgcam = (int)v;
    if (!get_f(opt, "fov", &fb)) return 0;
    in->fov = f_of(fb);
    if (!get_f(opt, "gamma", &fb)) return 0;
    in->gamma = f_of(fb);
    if (!json_int(json_at(disp, 0), &v)) return 0;
    in->dw = (int)v;
    if (!json_int(json_at(disp, 1), &v)) return 0;
    in->dh = (int)v;

    if (!get_f(er, "tfx", &fb)) return 0;
    in->tfx = f_of(fb);
    if (!get_f(er, "tfy", &fb)) return 0;
    in->tfy = f_of(fb);
    if (!get_f(er, "fc1", &fb)) return 0;
    in->fc1 = f_of(fb);
    if (!get_f(er, "fc2", &fb)) return 0;
    in->fc2 = f_of(fb);
    if (!get_f(er, "fmh", &fb)) return 0;
    in->fmh = f_of(fb);
    if (!get_f(er, "fmhp", &fb)) return 0;
    in->fmhp = f_of(fb);
    if (!json_int(json_get(er, "ruc"), &v)) return 0;
    in->ruc = (int)v;
    if (!get_f(er, "roll", &fb)) return 0;
    in->roll = f_of(fb);
    if (!get_f(er, "proll", &fb)) return 0;
    in->proll = f_of(fb);
    if (!get_d(er, "zoom", &db)) return 0;
    in->zoom = d_of(db);
    if (!json_int(json_get(er, "dvd"), &v)) return 0;
    in->dvd = (int)v;
    if (!json_int(json_get(er, "lbolt"), &v)) return 0;
    in->lbolt = (int)v;

    if (!get_d(pl, "px", &db)) return 0;   /* the probe's px/py/pz are prevPos */
    in->ppx = d_of(db);
    if (!get_d(pl, "py", &db)) return 0;
    in->ppy = d_of(db);
    if (!get_d(pl, "pz", &db)) return 0;
    in->ppz = d_of(db);
    if (!get_d(pl, "x", &db)) return 0;
    in->px = d_of(db);
    if (!get_d(pl, "y", &db)) return 0;
    in->py = d_of(db);
    if (!get_d(pl, "z", &db)) return 0;
    in->pz = d_of(db);
    if (!get_f(pl, "yaw", &fb)) return 0;
    in->yaw = f_of(fb);
    if (!get_f(pl, "pyaw", &fb)) return 0;
    in->pyaw = f_of(fb);
    if (!get_f(pl, "pit", &fb)) return 0;
    in->pit = f_of(fb);
    if (!get_f(pl, "ppit", &fb)) return 0;
    in->ppit = f_of(fb);
    if (!get_f(pl, "yoff", &fb)) return 0;
    in->yoff = f_of(fb);
    if (!get_f(pl, "dwm", &fb)) return 0;
    in->dwm = f_of(fb);
    if (!get_f(pl, "pdwm", &fb)) return 0;
    in->pdwm = f_of(fb);
    if (!get_f(pl, "cyaw", &fb)) return 0;
    in->ecyaw = f_of(fb);
    if (!get_f(pl, "pcyaw", &fb)) return 0;
    in->pecyaw = f_of(fb);
    if (!get_f(pl, "cpit", &fb)) return 0;
    in->ecpit = f_of(fb);
    if (!get_f(pl, "pcpit", &fb)) return 0;
    in->pepit = f_of(fb);
    if (!json_int(json_get(pl, "hurt"), &v)) return 0;
    in->hurt = (int)v;
    if (!json_int(json_get(pl, "mhurt"), &v)) return 0;
    in->mhurt = (int)v;
    if (!json_int(json_get(pl, "death"), &v)) return 0;
    in->death = (int)v;
    if (!get_f(pl, "aaty", &fb)) return 0;
    in->aaty = f_of(fb);
    if (!get_f(pl, "hp", &fb)) return 0;
    in->hp = f_of(fb);
    if (!get_f(pl, "portal", &fb)) return 0;
    in->portal = f_of(fb);
    if (!get_f(pl, "pportal", &fb)) return 0;
    in->pportal = f_of(fb);
    if (!json_int(json_get(pl, "sleep"), &v)) return 0;
    in->sleep = (int)v;
    if (!json_int(json_get(pl, "crea"), &v)) return 0;
    in->crea = (int)v;
    if (!json_int(json_get(pl, "brf"), &v)) return 0;
    in->brf = (int)v;
    if (!json_int(json_get(pl, "resp"), &v)) return 0;
    in->resp = (int)v;
    if (!json_int(json_get(pl, "nv"), &v)) return 0;
    in->nv = (int)v;
    if (!json_int(json_get(pl, "nvd"), &v)) return 0;
    in->nvd = (int)v;
    if (!json_int(json_get(pl, "blind"), &v)) return 0;
    in->blind = (int)v;
    if (!json_int(json_get(pl, "bldur"), &v)) return 0;
    in->bldur = (int)v;
    if (!json_int(json_get(pl, "wb"), &v)) return 0;
    in->wb = (int)v;
    if (!json_int(json_get(pl, "conf"), &v)) return 0;
    in->conf = (int)v;

    if (!json_int(json_get(wo, "wt"), &v)) return 0;
    in->wt = (int64_t)v;
    if (!get_f(wo, "rain", &fb)) return 0;
    in->rain = f_of(fb);
    if (!get_f(wo, "prain", &fb)) return 0;
    in->prain = f_of(fb);
    if (!get_f(wo, "thu", &fb)) return 0;
    in->thu = f_of(fb);
    if (!get_f(wo, "pthu", &fb)) return 0;
    in->pthu = f_of(fb);
    if (!json_int(json_get(wo, "cloud"), &v)) return 0;
    in->cloud = (long long)v;
    if (!json_int(json_get(wo, "dim"), &v)) return 0;
    in->dim = (int)v;
    if (!json_int(json_get(wo, "nosky"), &v)) return 0;
    in->nosky = (int)v;
    if (!json_int(json_get(wo, "voidp"), &v)) return 0;
    in->voidp = (int)v;
    if (!json_int(json_get(wo, "xzfog"), &v)) return 0;
    in->xzfog = (int)v;
    if (!get_d(wo, "voidf", &db)) return 0;
    in->voidf = d_of(db);
    if (!get_f(wo, "temp", &fb)) return 0;
    in->temp = f_of(fb);
    if (!json_int(json_get(wo, "skytemp"), &v)) return 0;
    in->skytemp = (int)v;
    lbt = json_get(wo, "lbt");
    if (!lbt || json_len(lbt) != 16) return 0;
    for (int i = 0; i < 16; ++i)
    {
        if (!at_f(lbt, i, &fb)) return 0;
        in->lbt[i] = f_of(fb);
    }

    if (!json_int(json_get(vp, "mat"), &v)) return 0;
    in->mat = (int)v;

    if (!get_f(row, "pt", &fb)) return 0;
    in->pt = f_of(fb);
    return 1;
}
