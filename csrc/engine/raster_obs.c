/* raster_obs.h's file: a gzip stream of the frame's numbers, its draws with
 * their quads, the textures, then the C renderer's frame. The layout is this
 * build's structs as they are in memory (the file is written and read on
 * one machine, by the device renderer's gate). */
#include "raster_obs.h"
#include "meshfeed.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static const char MAGIC[8] = "NWOBS7\n";

struct obs_owned {
    struct raster_obs_draw *draws;
    int32_t *data;
    unsigned char *tex;
    float *stars;
    struct raster_obs_cmd *cmd;
    struct raster_obs_equad *equad;
    struct raster_obs_estate *estate;
    struct raster_obs_tex *etex;
    unsigned char *etex_bytes;
    uint32_t (*lm)[256];
    struct raster_obs_crack *crack;
    struct raster_obs_portal *portal;
    struct raster_obs_line *line;
    /* the device mesher's feed and the host's meshes of it */
    struct meshfeed_out mesh;
    void *mesh_bytes;
    struct raster_obs_hostmesh *mesh_host;
    int32_t *mesh_raw;
    char *assets;
};

static int put(gzFile z, const void *p, size_t n)
{
    return n == 0 || gzwrite(z, p, (unsigned)n) == (int)n;
}

static int get(gzFile z, void *p, size_t n)
{
    return n == 0 || gzread(z, p, (unsigned)n) == (int)n;
}

int raster_obs_write(const char *path, const struct raster_obs *o, const unsigned char *rgb)
{
    return raster_obs_write_depth(path, o, rgb, NULL);
}

int raster_obs_write_depth(const char *path, const struct raster_obs *o, const unsigned char *rgb, const float *depth)
{
    gzFile z = gzopen(path, "wb1");
    if (!z) return -1;
    int ok = put(z, MAGIC, sizeof MAGIC);
    int32_t head[5] = {o->w, o->h, o->nopaque, o->nwater, o->prec};
    ok &= put(z, head, sizeof head);
    ok &= put(z, o->proj, sizeof o->proj) && put(z, o->mv, sizeof o->mv) && put(z, o->cam, sizeof o->cam);
    ok &= put(z, o->fog, sizeof o->fog) && put(z, &o->fogs, sizeof o->fogs) && put(z, &o->foge, sizeof o->foge);
    ok &= put(z, &o->fogd, sizeof o->fogd) && put(z, &o->fogm, sizeof o->fogm) && put(z, o->lm, sizeof o->lm);
    ok &= put(z, &o->sky, sizeof o->sky);
    for (int i = 0; i < o->nopaque + o->nwater; ++i) {
        const struct raster_obs_draw *d = i < o->nopaque ? &o->opaque[i] : &o->water[i - o->nopaque];
        int has_raw = d->count > 0 && d->raw;
        int32_t e[10] = {d->cx, d->s, d->cz, d->flags, d->count, (int32_t)d->key, (int32_t)d->version,
                         d->order != NULL && has_raw, d->device, has_raw};
        ok &= put(z, e, sizeof e);
        if (has_raw) ok &= put(z, d->raw, (size_t)d->count * 8 * sizeof(int32_t));
        if (d->order && has_raw) ok &= put(z, d->order, (size_t)(d->count / 4) * sizeof(int32_t));
    }
    int32_t tex[4] = {o->atlas_w, o->atlas_h, o->end_sky != NULL, o->nstars};
    ok &= put(z, tex, sizeof tex);
    ok &= put(z, o->atlas, (size_t)o->atlas_w * o->atlas_h * 4);
    ok &= put(z, o->sun, 32 * 32 * 4) && put(z, o->moon, 128 * 64 * 4) && put(z, o->clouds, 256 * 256 * 4);
    if (o->end_sky) ok &= put(z, o->end_sky, 128 * 128 * 3);
    ok &= put(z, o->stars, (size_t)o->nstars * 12 * sizeof(float));
    ok &= put(z, &o->nanim, sizeof o->nanim) && put(z, o->anim, sizeof o->anim);
    int32_t rec[8] = {o->ncmd, o->nequad, o->nestate, o->ntex, o->nlm, o->ncrack, o->nportal, o->nline};
    ok &= put(z, rec, sizeof rec);
    ok &= put(z, o->cmd, (size_t)o->ncmd * sizeof *o->cmd);
    if (o->nebox || o->neitem) {
        /* the file holds every quad: the boxes' and the items' made here */
        struct raster_obs_equad *eq = malloc((size_t)(o->nequad + 1) * sizeof *eq);
        if (!eq) exit(1);
        memcpy(eq, o->equad, (size_t)o->nequad * sizeof *eq);
        for (int i = 0; i < o->nebox; ++i) {
            const struct raster_obs_ebox *b = &o->ebox[i];
            struct entity_clip_vertex q[6][4];
            raster_ebox_quads(b, o->ebox_xf[b->xf], o->ebox_xf[b->xf] + 16, q);
            for (int f = 0; f < 6; ++f) memcpy(eq[b->first + f].v, q[f], sizeof q[f]);
        }
        for (int i = 0; i < o->neitem; ++i) {
            const struct raster_obs_eitem *it = &o->eitem[i];
            for (int k = 0; k < raster_eitem_quads(it); ++k)
                raster_eitem_quad(it, o->ebox_xf[it->xf], o->ebox_xf[it->xf] + 16, k, eq[it->first + k].v);
        }
        ok &= put(z, eq, (size_t)o->nequad * sizeof *eq);
        free(eq);
    }
    else ok &= put(z, o->equad, (size_t)o->nequad * sizeof *o->equad);
    ok &= put(z, o->estate, (size_t)o->nestate * sizeof *o->estate);
    for (int i = 0; i < o->ntex; ++i) {
        int32_t wh[2] = {o->tex[i].w, o->tex[i].h};
        ok &= put(z, wh, sizeof wh) && put(z, o->tex[i].rgba, (size_t)wh[0] * wh[1] * 4);
    }
    ok &= put(z, o->lm_table, (size_t)o->nlm * sizeof *o->lm_table);
    ok &= put(z, o->crack, (size_t)o->ncrack * sizeof *o->crack);
    ok &= put(z, o->portal, (size_t)o->nportal * sizeof *o->portal);
    ok &= put(z, o->line, (size_t)o->nline * sizeof *o->line);
    int32_t has = rgb != NULL;
    ok &= put(z, &has, sizeof has);
    if (rgb) ok &= put(z, rgb, (size_t)o->w * o->h * 3);
    has = depth != NULL;
    ok &= put(z, &has, sizeof has);
    if (depth) ok &= put(z, depth, (size_t)o->w * o->h * sizeof *depth);
    /* the device mesher's work: the feed, the host's meshes, the scene */
    const struct meshfeed_out *f = o->mesh;
    int32_t mh[16] = {f != NULL};
    if (f) {
        mh[1] = (int32_t)f->epoch; mh[2] = (int32_t)f->seq; mh[3] = f->chunk_slots; mh[4] = f->band_slots;
        mh[5] = f->nreq; mh[6] = f->nte; mh[7] = f->ntrig; mh[8] = f->nop;
        mh[9] = f->cx0; mh[10] = f->cz0; mh[11] = f->cx1; mh[12] = f->cz1;
        mh[13] = o->mesh_host != NULL;
        mh[14] = o->assets ? (int32_t)strlen(o->assets) : 0;
    }
    ok &= put(z, mh, sizeof mh);
    if (f) {
        uint64_t np = f->npayload;
        ok &= put(z, &np, sizeof np);
        ok &= put(z, f->req, (size_t)f->nreq * sizeof *f->req) && put(z, f->te, (size_t)f->nte * sizeof *f->te);
        ok &= put(z, f->trig, (size_t)f->ntrig * sizeof *f->trig) && put(z, f->op, (size_t)f->nop * sizeof *f->op);
        ok &= put(z, f->payload, f->npayload);
        for (int i = 0; o->mesh_host && i < f->nreq; ++i) {
            const struct raster_obs_hostmesh *hm = &o->mesh_host[i];
            int32_t cf[2] = {hm->count, hm->flags};
            ok &= put(z, cf, sizeof cf);
            if (hm->count > 0) ok &= put(z, hm->raw, (size_t)hm->count * 8 * sizeof(int32_t));
        }
        if (mh[14]) ok &= put(z, o->assets, (size_t)mh[14]);
    }
    ok &= gzclose(z) == Z_OK;
    return ok ? 0 : -1;
}

int raster_obs_read(const char *path, struct raster_obs *o, unsigned char **rgb)
{
    return raster_obs_read_depth(path, o, rgb, NULL);
}

int raster_obs_read_depth(const char *path, struct raster_obs *o, unsigned char **rgb, float **depth)
{
    memset(o, 0, sizeof *o);
    if (rgb) *rgb = NULL;
    if (depth) *depth = NULL;
    gzFile z = gzopen(path, "rb");
    if (!z) return -1;
    struct obs_owned *own = calloc(1, sizeof *own);
    char magic[8];
    int32_t head[5] = {0, 0, 0, 0, 0};
    /* NWOBS6, before the precision: exact */
    int ok = own && get(z, magic, sizeof magic);
    int v6 = ok && !memcmp(magic, "NWOBS6\n", 8);
    ok = ok && (v6 || !memcmp(magic, MAGIC, sizeof MAGIC)) && get(z, head, v6 ? 4 * sizeof *head : sizeof head);
    if (ok) {
        o->w = head[0]; o->h = head[1]; o->nopaque = head[2]; o->nwater = head[3]; o->prec = head[4];
        ok = o->w > 0 && o->h > 0 && o->nopaque >= 0 && o->nwater >= 0;
    }
    ok = ok && get(z, o->proj, sizeof o->proj) && get(z, o->mv, sizeof o->mv) && get(z, o->cam, sizeof o->cam);
    ok = ok && get(z, o->fog, sizeof o->fog) && get(z, &o->fogs, sizeof o->fogs) && get(z, &o->foge, sizeof o->foge);
    ok = ok && get(z, &o->fogd, sizeof o->fogd) && get(z, &o->fogm, sizeof o->fogm) && get(z, o->lm, sizeof o->lm);
    ok = ok && get(z, &o->sky, sizeof o->sky);
    int n = ok ? o->nopaque + o->nwater : 0;
    if (ok) {
        own->draws = calloc((size_t)(n ? n : 1), sizeof *own->draws);
        ok = own->draws != NULL;
    }
    /* the quads and orders of every draw, one buffer, sized as they come */
    size_t cap = 0, used = 0;
    size_t *at = ok ? calloc((size_t)(n ? n : 1) * 2, sizeof *at) : NULL;
    ok = ok && at;
    for (int i = 0; ok && i < n; ++i) {
        int32_t e[10];
        ok = get(z, e, sizeof e) && e[4] >= -1 && (e[4] < 0 || e[4] % 4 == 0) && (e[9] || !e[7]);
        if (!ok) break;
        struct raster_obs_draw *d = &own->draws[i];
        d->cx = e[0]; d->s = e[1]; d->cz = e[2]; d->flags = e[3]; d->count = e[4];
        d->key = (uint32_t)e[5]; d->version = (uint32_t)e[6];
        d->device = e[8];
        size_t nv = e[9] ? (size_t)d->count : 0;
        size_t need = nv * 8 + (e[7] ? nv / 4 : 0);
        if (used + need > cap) {
            cap = (used + need) * 2 + 1024;
            int32_t *grown = realloc(own->data, cap * sizeof(int32_t));
            if (!grown) { ok = 0; break; }
            own->data = grown;
        }
        at[i * 2] = e[9] ? used : (size_t)-1;
        ok = get(z, own->data + used, nv * 8 * sizeof(int32_t));
        used += nv * 8;
        at[i * 2 + 1] = e[7] ? used : (size_t)-1;
        if (ok && e[7]) {
            ok = get(z, own->data + used, (nv / 4) * sizeof(int32_t));
            used += nv / 4;
        }
    }
    for (int i = 0; ok && i < n; ++i) {
        own->draws[i].raw = at[i * 2] == (size_t)-1 ? NULL : own->data + at[i * 2];
        own->draws[i].order = at[i * 2 + 1] == (size_t)-1 ? NULL : own->data + at[i * 2 + 1];
    }
    free(at);
    int32_t tex[4];
    ok = ok && get(z, tex, sizeof tex) && tex[0] > 0 && tex[1] > 0 && tex[3] >= 0;
    if (ok) {
        o->atlas_w = tex[0]; o->atlas_h = tex[1]; o->nstars = tex[3];
        size_t atlas = (size_t)tex[0] * tex[1] * 4;
        size_t all = atlas + 32 * 32 * 4 + 128 * 64 * 4 + 256 * 256 * 4 + 128 * 128 * 3;
        own->tex = malloc(all);
        own->stars = malloc((size_t)(tex[3] ? tex[3] : 1) * 12 * sizeof(float));
        ok = own->tex && own->stars;
        if (ok) {
            unsigned char *p = own->tex;
            o->atlas = p; ok = get(z, p, atlas); p += atlas;
            o->sun = p; ok = ok && get(z, p, 32 * 32 * 4); p += 32 * 32 * 4;
            o->moon = p; ok = ok && get(z, p, 128 * 64 * 4); p += 128 * 64 * 4;
            o->clouds = p; ok = ok && get(z, p, 256 * 256 * 4); p += 256 * 256 * 4;
            o->end_sky = tex[2] ? p : NULL;
            if (tex[2]) ok = ok && get(z, p, 128 * 128 * 3);
            o->stars = own->stars;
            ok = ok && get(z, own->stars, (size_t)tex[3] * 12 * sizeof(float));
            ok = ok && get(z, &o->nanim, sizeof o->nanim) && get(z, o->anim, sizeof o->anim) &&
                 o->nanim >= 0 && o->nanim <= RASTER_OBS_ANIM;
            int32_t rec[8];
            ok = ok && get(z, rec, sizeof rec);
            for (int i = 0; ok && i < 8; ++i) ok = rec[i] >= 0;
            if (ok) {
                o->ncmd = rec[0]; o->nequad = rec[1]; o->nestate = rec[2];
                o->ntex = rec[3]; o->nlm = rec[4]; o->ncrack = rec[5]; o->nportal = rec[6]; o->nline = rec[7];
                own->line = malloc((size_t)(o->nline + 1) * sizeof *own->line);
                own->portal = malloc((size_t)(o->nportal + 1) * sizeof *own->portal);
                ok = own->portal != NULL;
                own->cmd = malloc((size_t)(o->ncmd + 1) * sizeof *own->cmd);
                own->equad = malloc((size_t)(o->nequad + 1) * sizeof *own->equad);
                own->estate = malloc((size_t)(o->nestate + 1) * sizeof *own->estate);
                own->etex = calloc((size_t)(o->ntex + 1), sizeof *own->etex);
                own->lm = malloc((size_t)(o->nlm + 1) * sizeof *own->lm);
                own->crack = malloc((size_t)(o->ncrack + 1) * sizeof *own->crack);
                ok = ok && own->cmd && own->equad && own->estate && own->etex && own->lm && own->crack && own->line;
            }
            ok = ok && get(z, own->cmd, (size_t)o->ncmd * sizeof *own->cmd);
            ok = ok && get(z, own->equad, (size_t)o->nequad * sizeof *own->equad);
            ok = ok && get(z, own->estate, (size_t)o->nestate * sizeof *own->estate);
            size_t tb = 0, tcap = 0;
            for (int i = 0; ok && i < o->ntex; ++i) {
                int32_t wh[2];
                ok = get(z, wh, sizeof wh) && wh[0] > 0 && wh[1] > 0;
                if (!ok) break;
                size_t need = (size_t)wh[0] * wh[1] * 4;
                if (tb + need > tcap) {
                    tcap = (tb + need) * 2;
                    unsigned char *g = realloc(own->etex_bytes, tcap);
                    if (!g) { ok = 0; break; }
                    own->etex_bytes = g;
                }
                own->etex[i].w = wh[0]; own->etex[i].h = wh[1];
                own->etex[i].rgba = (const unsigned char *)(uintptr_t)tb;
                ok = get(z, own->etex_bytes + tb, need);
                tb += need;
            }
            for (int i = 0; ok && i < o->ntex; ++i) {
                own->etex[i].rgba = own->etex_bytes + (uintptr_t)own->etex[i].rgba;
                own->etex[i].key = raster_obs_tex_key(own->etex[i].rgba, own->etex[i].w, own->etex[i].h);
            }
            ok = ok && get(z, own->lm, (size_t)o->nlm * sizeof *own->lm);
            ok = ok && get(z, own->crack, (size_t)o->ncrack * sizeof *own->crack);
            ok = ok && get(z, own->portal, (size_t)o->nportal * sizeof *own->portal);
            ok = ok && get(z, own->line, (size_t)o->nline * sizeof *own->line);
            if (ok) {
                o->cmd = own->cmd; o->equad = own->equad; o->estate = own->estate;
                o->tex = own->etex; o->lm_table = (const uint32_t (*)[256])own->lm; o->crack = own->crack;
                o->portal = own->portal;
                o->line = own->line;
            }
        }
    }
    int32_t has = 0;
    ok = ok && get(z, &has, sizeof has);
    if (ok && has) {
        unsigned char *frame = malloc((size_t)o->w * o->h * 3);
        ok = frame && get(z, frame, (size_t)o->w * o->h * 3);
        if (rgb) *rgb = frame;
        else free(frame);
    }
    has = 0;
    ok = ok && get(z, &has, sizeof has);
    if (ok && has) {
        float *dp = malloc((size_t)o->w * o->h * sizeof *dp);
        ok = dp && get(z, dp, (size_t)o->w * o->h * sizeof *dp);
        if (depth) *depth = dp;
        else free(dp);
    }
    int32_t mh[16];
    ok = ok && get(z, mh, sizeof mh);
    if (ok && mh[0]) {
        struct meshfeed_out *f = &own->mesh;
        uint64_t np = 0;
        f->epoch = (uint32_t)mh[1]; f->seq = (uint32_t)mh[2]; f->chunk_slots = mh[3]; f->band_slots = mh[4];
        f->nreq = mh[5]; f->nte = mh[6]; f->ntrig = mh[7]; f->nop = mh[8];
        f->cx0 = mh[9]; f->cz0 = mh[10]; f->cx1 = mh[11]; f->cz1 = mh[12];
        ok = get(z, &np, sizeof np) && f->nreq >= 0 && f->nte >= 0 && f->ntrig >= 0 && f->nop >= 0 && mh[14] >= 0;
        size_t sr = (size_t)f->nreq * sizeof *f->req, st = (size_t)f->nte * sizeof *f->te;
        size_t sg = (size_t)f->ntrig * sizeof *f->trig, so = (size_t)f->nop * sizeof *f->op;
        unsigned char *b = ok ? malloc(sr + st + sg + so + np + 16) : NULL;
        own->mesh_bytes = b;
        ok = ok && b && get(z, b, sr) && get(z, b + sr, st) && get(z, b + sr + st, sg) &&
             get(z, b + sr + st + sg, so) && get(z, b + sr + st + sg + so, np);
        if (ok) {
            f->req = (const void *)b; f->te = (const void *)(b + sr); f->trig = (const void *)(b + sr + st);
            f->op = (const void *)(b + sr + st + sg); f->payload = b + sr + st + sg + so; f->npayload = np;
        }
        if (ok && mh[13]) {
            own->mesh_host = calloc((size_t)(f->nreq ? f->nreq : 1), sizeof *own->mesh_host);
            size_t rcap = 0, rn = 0;
            size_t *roff = calloc((size_t)(f->nreq ? f->nreq : 1), sizeof *roff);
            ok = own->mesh_host && roff;
            for (int i = 0; ok && i < f->nreq; ++i) {
                int32_t cf[2];
                ok = get(z, cf, sizeof cf);
                if (!ok) break;
                own->mesh_host[i].count = cf[0];
                own->mesh_host[i].flags = cf[1];
                roff[i] = rn;
                if (cf[0] <= 0) continue;
                size_t need = (size_t)cf[0] * 8;
                if (rn + need > rcap) {
                    rcap = (rn + need) * 2;
                    int32_t *g = realloc(own->mesh_raw, rcap * sizeof(int32_t));
                    if (!g) { ok = 0; break; }
                    own->mesh_raw = g;
                }
                ok = get(z, own->mesh_raw + rn, need * sizeof(int32_t));
                rn += need;
            }
            for (int i = 0; ok && i < f->nreq; ++i)
                own->mesh_host[i].raw = own->mesh_host[i].count > 0 ? own->mesh_raw + roff[i] : NULL;
            free(roff);
            if (ok) o->mesh_host = own->mesh_host;
        }
        if (ok && mh[14]) {
            own->assets = calloc((size_t)mh[14] + 1, 1);
            ok = own->assets && get(z, own->assets, (size_t)mh[14]);
            o->assets = own->assets;
        }
        if (ok) o->mesh = f;
    }
    gzclose(z);
    o->opaque = own ? own->draws : NULL;
    o->water = own && own->draws ? own->draws + o->nopaque : NULL;
    o->owned = own;
    if (!ok) {
        if (rgb) { free(*rgb); *rgb = NULL; }
        if (depth) { free(*depth); *depth = NULL; }
        raster_obs_free(o);
        return -1;
    }
    return 0;
}

void raster_obs_free(struct raster_obs *o)
{
    struct obs_owned *own = o->owned;
    if (own) {
        free(own->draws); free(own->data); free(own->tex); free(own->stars);
        free(own->cmd); free(own->equad); free(own->estate); free(own->etex); free(own->etex_bytes);
        free(own->lm); free(own->crack); free(own->portal); free(own->line);
        free(own->mesh_bytes); free(own->mesh_host); free(own->mesh_raw); free(own->assets);
        free(own);
    }
    memset(o, 0, sizeof *o);
}

/* The key: the texture cut into KEY_TILE x KEY_TILE texel tiles, each
 * tile's rows through four 64-bit multiply-xor lanes, the tiles' hashes
 * mixed in order with the size: equal bytes, equal keys (a collision would
 * draw another texture, at odds of 2^-64 a pair). Tiles, so a recording
 * whose animated atlas changed a few sprites rehashes only those
 * (rec_tex). */
enum { KEY_TILE = 16 };
static const uint64_t KEY_K = 0x9E3779B97F4A7C15ULL;

static int key_tiles_x(int w) { return (w + KEY_TILE - 1) / KEY_TILE; }
static int key_tiles_y(int h) { return (h + KEY_TILE - 1) / KEY_TILE; }

static uint64_t key_tile(const unsigned char *p, int w, int h, int tx, int ty)
{
    const uint64_t K = KEY_K;
    int x0 = tx * KEY_TILE, y0 = ty * KEY_TILE;
    int tw = w - x0 < KEY_TILE ? w - x0 : KEY_TILE, th = h - y0 < KEY_TILE ? h - y0 : KEY_TILE;
    size_t n = (size_t)tw * 4;
    uint64_t seed = ((uint64_t)(uint32_t)tx << 32 | (uint32_t)ty) * K ^ 0x243F6A8885A308D3ULL;
    uint64_t a = seed ^ n, b = seed + K, c = seed * 31 + 7, d = ~seed;
    for (int y = 0; y < th; ++y) {
        const unsigned char *row = p + ((size_t)(y0 + y) * (size_t)w + (size_t)x0) * 4;
        size_t i = 0;
        for (; i + 32 <= n; i += 32) {
            uint64_t x[4];
            memcpy(x, row + i, 32);
            a = (a ^ x[0]) * K; a ^= a >> 29;
            b = (b ^ x[1]) * K; b ^= b >> 29;
            c = (c ^ x[2]) * K; c ^= c >> 29;
            d = (d ^ x[3]) * K; d ^= d >> 29;
        }
        for (; i < n; ++i) { a ^= row[i]; a *= 1099511628211ULL; }
    }
    uint64_t y = a ^ (b * 3) ^ (c * 5) ^ (d * 7);
    y ^= y >> 31; y *= K; y ^= y >> 29;
    return y;
}

static uint64_t key_begin(int w, int h) { return 1469598103934665603ULL ^ ((uint64_t)(uint32_t)w << 32 | (uint32_t)h); }
static uint64_t key_add(uint64_t k, uint64_t t) { k = (k ^ t) * KEY_K; return k ^ (k >> 29); }
static uint64_t key_end(uint64_t k)
{
    k ^= k >> 31; k *= KEY_K; k ^= k >> 29;
    return k ? k : 1;
}

uint64_t raster_obs_tex_key(const unsigned char *p, int w, int h)
{
    uint64_t k = key_begin(w, h);
    for (int ty = 0; ty < key_tiles_y(h); ++ty)
        for (int tx = 0; tx < key_tiles_x(w); ++tx) k = key_add(k, key_tile(p, w, h, tx, ty));
    return key_end(k);
}

/* ------------------------------------------------------------ the recorder */

/* a recorded texture: its source, and the copy taken when a quad first
 * sampled it; w, h and key (0 unknown) describe the copy's bytes, and stay
 * with the copy into later frames, whose entry of the same source takes this
 * copy back when the bytes still match. checked: the thread's free count
 * for the copy's size (raster_rec_free_count) when the copy last matched
 * the source; written: a write named the source since (REC_WHOLE: anywhere;
 * REC_TILES: in the tiles tdirty marks). */
enum { REC_CLEAN, REC_WHOLE, REC_TILES };
struct rec_tex {
    const unsigned char *src;
    int stale, written;
    unsigned char *copy;
    size_t cap;
    int w, h;
    uint64_t key, checked;
    uint64_t *tiles;                   /* the copy's tile hashes (the key's), valid with key */
    unsigned char *tdirty;             /* ntiles of them: written in the tile since */
    int ntiles;
};

struct raster_rec {
    struct rec_tex *rt;                /* ntex of them, beside tex */
    int caprt;
    struct raster_obs_cmd *cmd;
    struct raster_obs_equad *equad;
    struct raster_obs_estate *estate;
    struct raster_obs_tex *tex;
    uint32_t (*lm)[256];
    struct raster_obs_crack *crack;
    struct raster_obs_portal *portal;
    struct raster_obs_line *line;
    int ncmd, nequad, nestate, ntex, nlm, ncrack, nportal, nline;
    int capcmd, capequad, capestate, captex, caplm, capcrack, capportal, capline;
    struct raster_obs_ebox *ebox;      /* the model boxes, the flat items, their matrices */
    struct raster_obs_eitem *eitem;
    float (*xf)[32];
    int nebox, neitem, nxf, capebox, capeitem, capxf;
    unsigned char *dirty;              /* rec_tex's changed tiles */
    int capdirty;
};

/* the recorder the hooks append to (renderer state, never the tick's), and
 * every recorder there is (a texture's write is named to each) */
static struct raster_rec *rec_active;
static struct raster_rec **rec_all;
static int rec_nall, rec_capall;
/* the recording thread's free count (raster_rec_free_count), NULL none */
static uint64_t (*rec_frees)(const void *p, size_t bytes);
/* raster_rec_verify: every copy taken back unchecked is compared anyway */
static int rec_verify;
static uint64_t rec_verified, rec_verify_bad;

void raster_rec_free_count(uint64_t (*count)(const void *p, size_t bytes)) { rec_frees = count; }

void raster_rec_verify(int on) { rec_verify = on; }

void raster_rec_verify_stats(uint64_t *checked, uint64_t *bad)
{
    *checked = rec_verified;
    *bad = rec_verify_bad;
}

static void *grow(void *p, int *cap, int need, size_t size)
{
    if (need <= *cap) return p;
    int n = *cap ? *cap * 2 : 64;
    while (n < need) n *= 2;
    void *q = realloc(p, (size_t)n * size);
    if (!q) { fprintf(stderr, "raster_rec: allocation failed\n"); exit(1); }
    *cap = n;
    return q;
}

struct raster_rec *raster_rec_new(void)
{
    struct raster_rec *r = calloc(1, sizeof *r);
    if (!r) exit(1);
    rec_all = grow(rec_all, &rec_capall, rec_nall + 1, sizeof *rec_all);
    rec_all[rec_nall++] = r;
    return r;
}

void raster_rec_free(struct raster_rec *r)
{
    if (!r) return;
    if (rec_active == r) rec_active = NULL;
    for (int i = 0; i < rec_nall; ++i)
        if (rec_all[i] == r) { rec_all[i] = rec_all[--rec_nall]; break; }
    for (int i = 0; i < r->caprt; ++i) { free(r->rt[i].copy); free(r->rt[i].tiles); free(r->rt[i].tdirty); }
    free(r->dirty);
    free(r->rt);
    free(r->cmd); free(r->equad); free(r->estate); free(r->tex); free(r->lm); free(r->crack); free(r->portal);
    free(r->line);
    free(r->ebox);
    free(r->eitem);
    free(r->xf);
    free(r);
}

void raster_rec_begin(struct raster_rec *r)
{
    r->ncmd = r->nequad = r->nestate = r->ntex = r->nlm = r->ncrack = r->nportal = r->nline = 0;
    r->nebox = r->neitem = r->nxf = 0;
    rec_active = r;
}

struct raster_rec *raster_rec_hold(void)
{
    struct raster_rec *r = rec_active;
    rec_active = NULL;
    return r;
}

void raster_rec_restore(struct raster_rec *r) { rec_active = r; }

void raster_rec_end(void) { rec_active = NULL; }
int raster_rec_on(void) { return rec_active != NULL; }

static int rec_only;
void raster_rec_set_only(int on) { rec_only = on; }
int raster_rec_only(void) { return rec_only; }

static void rec_cmd(struct raster_rec *r, int kind, int index)
{
    struct raster_obs_cmd *c = r->ncmd ? &r->cmd[r->ncmd - 1] : NULL;
    int run = kind == RASTER_CMD_QUAD || kind == RASTER_CMD_CRACK || kind == RASTER_CMD_PORTAL || kind == RASTER_CMD_LINE;
    if (c && c->kind == kind && run && c->first + c->count == index) {
        ++c->count;
        return;
    }
    r->cmd = grow(r->cmd, &r->capcmd, r->ncmd + 1, sizeof *r->cmd);
    r->cmd[r->ncmd++] = (struct raster_obs_cmd){kind, index, run ? 1 : 0};
}

/* a write to RGBA (of AW x AH) in [X0, X1) x [Y0, Y1), the whole of it when
 * AW is 0: the recording's entries of it take a new copy at their next use;
 * every recorder's copies of it (this frame's and the ones kept from earlier
 * frames) are compared with it, where it was written, before they are taken
 * back */
static void rec_written(const unsigned char *rgba, int aw, int ah, int x0, int y0, int x1, int y1)
{
    for (int k = 0; k < rec_nall; ++k) {
        struct raster_rec *r = rec_all[k];
        for (int i = 0; i < r->caprt; ++i) {
            struct rec_tex *c = &r->rt[i];
            if (c->src != rgba) continue;
            if (r == rec_active && i < r->ntex) c->stale = 1;
            if (c->written == REC_WHOLE) continue;
            int ntx = key_tiles_x(c->w), n = ntx * key_tiles_y(c->h);
            if (!aw || aw != c->w || ah != c->h || !c->key || c->ntiles < n || x0 >= x1 || y0 >= y1) {
                c->written = REC_WHOLE;
                continue;
            }
            if (!c->tdirty) {
                c->tdirty = calloc((size_t)c->ntiles, 1);
                if (!c->tdirty) exit(1);
            }
            if (c->written == REC_CLEAN) memset(c->tdirty, 0, (size_t)n);
            c->written = REC_TILES;
            for (int ty = y0 / KEY_TILE; ty <= (y1 - 1) / KEY_TILE; ++ty)
                for (int tx = x0 / KEY_TILE; tx <= (x1 - 1) / KEY_TILE; ++tx) c->tdirty[ty * ntx + tx] = 1;
        }
    }
}

void raster_rec_texture_changed(const unsigned char *rgba) { rec_written(rgba, 0, 0, 0, 0, 0, 0); }

void raster_rec_texture_patched(const unsigned char *rgba, int aw, int ah, int x, int y, int w, int h)
{
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > aw || y + h > ah) rec_written(rgba, 0, 0, 0, 0, 0, 0);
    else rec_written(rgba, aw, ah, x, y, x + w, y + h);
}

void raster_rec_marker(int kind)
{
    if (rec_active) rec_cmd(rec_active, kind, 0);
}

static int rec_lm(struct raster_rec *r, const uint32_t *lm)
{
    if (!lm) return -1;
    for (int i = r->nlm - 1; i >= 0; --i)
        if (!memcmp(r->lm[i], lm, sizeof r->lm[i])) return i;
    r->lm = grow(r->lm, &r->caplm, r->nlm + 1, sizeof *r->lm);
    memcpy(r->lm[r->nlm], lm, sizeof r->lm[r->nlm]);
    return r->nlm++;
}

static int rec_tex(struct raster_rec *r, const unsigned char *tex, int tex_w, int tex_h);

void raster_rec_equad(const struct raster_obs_estate *state, const uint32_t *lm, const unsigned char *tex,
                      int tex_w, int tex_h, const struct entity_clip_vertex v[4])
{
    struct raster_rec *r = rec_active;
    if (!r) return;
    struct raster_obs_estate st = *state;
    st.lm = lm ? rec_lm(r, lm) : -1;
    int si = r->nestate - 1;
    if (si < 0 || memcmp(&r->estate[si], &st, sizeof st)) {
        r->estate = grow(r->estate, &r->capestate, r->nestate + 1, sizeof *r->estate);
        r->estate[r->nestate] = st;
        si = r->nestate++;
    }
    int ti = rec_tex(r, tex, tex_w, tex_h);
    r->equad = grow(r->equad, &r->capequad, r->nequad + 1, sizeof *r->equad);
    struct raster_obs_equad *q = &r->equad[r->nequad];
    q->state = si;
    q->tex = ti;
    memcpy(q->v, v, sizeof q->v);
    rec_cmd(r, RASTER_CMD_QUAD, r->nequad++);
}

/* a generated record's state, texture and matrices as each of its N quads
 * would record them, and its quads' places (their vertices are the
 * consumer's to make): *SI, *TI, *XI; the first quad's index */
static int rec_gen(struct raster_rec *r, const struct raster_obs_estate *state, const uint32_t *lm,
                   const unsigned char *tex, int tex_w, int tex_h, const float *mv, const float *proj, int n,
                   int *si_out, int *ti_out, int *xi_out)
{
    struct raster_obs_estate st = *state;
    st.lm = lm ? rec_lm(r, lm) : -1;
    int si = r->nestate - 1;
    if (si < 0 || memcmp(&r->estate[si], &st, sizeof st)) {
        r->estate = grow(r->estate, &r->capestate, r->nestate + 1, sizeof *r->estate);
        r->estate[r->nestate] = st;
        si = r->nestate++;
    }
    int ti = rec_tex(r, tex, tex_w, tex_h);
    int xi = r->nxf - 1;
    if (xi < 0 || memcmp(r->xf[xi], mv, 16 * sizeof(float)) || memcmp(r->xf[xi] + 16, proj, 16 * sizeof(float))) {
        r->xf = grow(r->xf, &r->capxf, r->nxf + 1, sizeof *r->xf);
        memcpy(r->xf[r->nxf], mv, 16 * sizeof(float));
        memcpy(r->xf[r->nxf] + 16, proj, 16 * sizeof(float));
        xi = r->nxf++;
    }
    int first = r->nequad;
    r->equad = grow(r->equad, &r->capequad, r->nequad + n, sizeof *r->equad);
    for (int f = 0; f < n; ++f) {
        r->equad[r->nequad].state = si;
        r->equad[r->nequad].tex = ti;
        rec_cmd(r, RASTER_CMD_QUAD, r->nequad++);
    }
    *si_out = si;
    *ti_out = ti;
    *xi_out = xi;
    return first;
}

void raster_rec_ebox(const struct raster_obs_estate *state, const uint32_t *lm, const unsigned char *tex, int tex_w,
                     int tex_h, const struct raster_obs_ebox *b, const float *mv, const float *proj)
{
    struct raster_rec *r = rec_active;
    if (!r) return;
    struct raster_obs_ebox e = *b;
    e.first = rec_gen(r, state, lm, tex, tex_w, tex_h, mv, proj, 6, &e.state, &e.tex, &e.xf);
    r->ebox = grow(r->ebox, &r->capebox, r->nebox + 1, sizeof *r->ebox);
    r->ebox[r->nebox++] = e;
}

void raster_rec_eitem(const struct raster_obs_estate *state, const uint32_t *lm, const unsigned char *tex, int tex_w,
                      int tex_h, const struct raster_obs_eitem *it, const float *mv, const float *proj)
{
    struct raster_rec *r = rec_active;
    if (!r) return;
    struct raster_obs_eitem e = *it;
    e.first = rec_gen(r, state, lm, tex, tex_w, tex_h, mv, proj, raster_eitem_quads(it), &e.state, &e.tex, &e.xf);
    r->eitem = grow(r->eitem, &r->capeitem, r->neitem + 1, sizeof *r->eitem);
    r->eitem[r->neitem++] = e;
}

int raster_eitem_quads(const struct raster_obs_eitem *it) { return 2 + 2 * it->w + 2 * it->h; }

void raster_eitem_quad(const struct raster_obs_eitem *it, const float mv[16], const float proj[16], int k,
                       struct entity_clip_vertex q[4])
{
    const float p1 = it->p1, p2 = it->p2, p3 = it->p3, p4 = it->p4;
    const int w = it->w, h = it->h;
    float t = 0.0f - it->thick;
    float p[4][3], uv[4][2];
    int dir;
    if (k == 0) {
        const float pp[4][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
        const float uu[4][2] = {{p1, p4}, {p3, p4}, {p3, p2}, {p1, p2}};
        memcpy(p, pp, sizeof p); memcpy(uv, uu, sizeof uv); dir = 0;
    }
    else if (k == 1) {
        const float pp[4][3] = {{0, 1, t}, {1, 1, t}, {1, 0, t}, {0, 0, t}};
        const float uu[4][2] = {{p1, p2}, {p3, p2}, {p3, p4}, {p1, p4}};
        memcpy(p, pp, sizeof p); memcpy(uv, uu, sizeof uv); dir = 1;
    }
    else if (k < 2 + 2 * w) {
        int i = k < 2 + w ? k - 2 : k - 2 - w;
        float v8 = 0.5f * (p1 - p3) / (float)w;
        float x = (float)i / (float)w;
        float u = p1 + (p3 - p1) * x - v8;
        if (k < 2 + w) {
            const float pp[4][3] = {{x, 0, t}, {x, 0, 0}, {x, 1, 0}, {x, 1, t}};
            const float uu[4][2] = {{u, p4}, {u, p4}, {u, p2}, {u, p2}};
            memcpy(p, pp, sizeof p); memcpy(uv, uu, sizeof uv); dir = 2;
        }
        else {
            float x1 = x + 1.0f / (float)w;
            const float pp[4][3] = {{x1, 1, t}, {x1, 1, 0}, {x1, 0, 0}, {x1, 0, t}};
            const float uu[4][2] = {{u, p2}, {u, p2}, {u, p4}, {u, p4}};
            memcpy(p, pp, sizeof p); memcpy(uv, uu, sizeof uv); dir = 3;
        }
    }
    else {
        int i = k < 2 + 2 * w + h ? k - 2 - 2 * w : k - 2 - 2 * w - h;
        float v9 = 0.5f * (p4 - p2) / (float)h;
        float y = (float)i / (float)h;
        float v = p4 + (p2 - p4) * y - v9;
        if (k < 2 + 2 * w + h) {
            float y1 = y + 1.0f / (float)h;
            const float pp[4][3] = {{0, y1, 0}, {1, y1, 0}, {1, y1, t}, {0, y1, t}};
            const float uu[4][2] = {{p1, v}, {p3, v}, {p3, v}, {p1, v}};
            memcpy(p, pp, sizeof p); memcpy(uv, uu, sizeof uv); dir = 4;
        }
        else {
            const float pp[4][3] = {{1, y, 0}, {0, y, 0}, {0, y, t}, {1, y, t}};
            const float uu[4][2] = {{p3, v}, {p1, v}, {p1, v}, {p3, v}};
            memcpy(p, pp, sizeof p); memcpy(uv, uu, sizeof uv); dir = 5;
        }
    }
    /* raster_glx.c vertex() */
    for (int c = 0; c < 4; ++c) {
        struct entity_clip_vertex *out = &q[c];
        memset(out, 0, sizeof *out);
        float eye[4], wv[4] = {p[c][0], p[c][1], p[c][2], 1.0f};
        for (int r = 0; r < 4; ++r)
            eye[r] = mv[r] * wv[0] + mv[4 + r] * wv[1] + mv[8 + r] * wv[2] + mv[12 + r] * wv[3];
        for (int r = 0; r < 4; ++r)
            out->clip[r] = proj[r] * eye[0] + proj[4 + r] * eye[1] + proj[8 + r] * eye[2] + proj[12 + r] * eye[3];
        out->fogcoord = sqrtf(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);
        float u = uv[c][0], v = uv[c][1];
        if (it->tex_matrix) {
            float tu = it->tm[0] * u + it->tm[1] * v + it->tm[2];
            float tv = it->tm[3] * u + it->tm[4] * v + it->tm[5];
            u = tu;
            v = tv;
        }
        out->u = u;
        out->v = v;
        out->diffuse = it->diffuse[dir];
        out->color[0] = out->color[1] = out->color[2] = out->color[3] = 1.0f;
        out->alpha = it->alpha;
        out->light[0] = it->light[0];
        out->light[1] = it->light[1];
    }
}

float raster_ebox_diffuse(float nx, float ny, float nz)
{
    const float n = 1.236931687687298f;
    float d0 = (nx * 0.2f + ny - nz * 0.7f) / n;
    float d1 = (-nx * 0.2f + ny + nz * 0.7f) / n;
    float d = 0.4f + 0.6f * fmaxf(0, d0) + 0.6f * fmaxf(0, d1);
    /* GL clamps after glColorMaterial applies the fleece colour. */
    return fmaxf(0, d);
}

/* raster_mobs.c clip_vertex's position part: a corner through the
 * modelview and the projection, and its eye distance */
static struct entity_clip_vertex ebox_corner(const struct raster_obs_ebox *b, const float *mv, const float *proj,
                                             const float v[3])
{
    struct entity_clip_vertex out;
    memset(&out, 0, sizeof out);
    out.color[0] = out.color[1] = out.color[2] = out.color[3] = 1.0f;
    out.light[0] = b->light[0];
    out.light[1] = b->light[1];
    float w[4] = {b->base[0] + v[0], b->base[1] + v[1], b->base[2] + v[2], 1};
    if (b->outer)
        w[0] = v[0], w[1] = v[1], w[2] = v[2];
    float eye[4];
    for (int r = 0; r < 4; ++r)
        eye[r] = mv[r] * w[0] + mv[4 + r] * w[1] + mv[8 + r] * w[2] + mv[12 + r];
    for (int r = 0; r < 4; ++r)
        out.clip[r] = proj[r] * eye[0] + proj[4 + r] * eye[1] + proj[8 + r] * eye[2] + proj[12 + r] * eye[3];
    out.fogcoord = sqrtf(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);
    return out;
}

void raster_ebox_quads(const struct raster_obs_ebox *b, const float mv[16], const float proj[16],
                       struct entity_clip_vertex q[6][4])
{
    const int face[6][4] = {{5, 1, 2, 6}, {0, 4, 7, 3}, {5, 4, 0, 1},
                            {2, 3, 7, 6}, {1, 0, 3, 2}, {4, 5, 6, 7}};
    const int u = b->u, vv = b->vv, dx = b->dx, dy = b->dy, dz = b->dz;
    /* each corner projected once; the faces set their own texture position
     * and shade */
    struct entity_clip_vertex corner[8];
    for (int k = 0; k < 8; ++k) corner[k] = ebox_corner(b, mv, proj, b->v[k]);
    int uv[6][4] = {{u + dz + dx, vv + dz, u + dz + dx + dz, vv + dz + dy},
                    {u, vv + dz, u + dz, vv + dz + dy},
                    {u + dz, vv, u + dz + dx, vv + dz},
                    {u + dz + dx, vv + dz, u + dz + 2 * dx, vv},
                    {u + dz, vv + dz, u + dz + dx, vv + dz + dy},
                    {u + 2 * dz + dx, vv + dz, u + 2 * dz + 2 * dx, vv + dz + dy}};
    for (int f = 0; f < 6; ++f)
    {
        int ids[4];
        for (int k = 0; k < 4; ++k)
            ids[k] = face[f][b->mirror ? 3 - k : k];
        float a[3], c[3], n[3];
        for (int j = 0; j < 3; ++j)
        {
            a[j] = b->v[ids[1]][j] - b->v[ids[0]][j];
            c[j] = b->v[ids[1]][j] - b->v[ids[2]][j];
        }
        n[0] = c[1] * a[2] - c[2] * a[1];
        n[1] = c[2] * a[0] - c[0] * a[2];
        n[2] = c[0] * a[1] - c[1] * a[0];
        float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (b->flip_normal)
        {
            n[0] = -n[0];
            n[1] = -n[1];
            n[2] = -n[2];
        }
        float d = len > 0 ? raster_ebox_diffuse(n[0] / len, n[1] / len, n[2] / len) : 1;
        float us[4] = {(float)uv[f][2], (float)uv[f][0], (float)uv[f][0], (float)uv[f][2]};
        /* ModelBox's mirror swaps the x corners and keeps each corner's own
         * texture position, so the uv follows the unmirrored corner. This loop
         * instead reverses the corner order for the winding, which leaves u
         * where ModelBox puts it but needs v flipped inside the face. */
        float vs_lo = (float)uv[f][1], vs_hi = (float)uv[f][3];
        float vs[4];
        if (b->mirror)
            vs[0] = vs[1] = vs_hi, vs[2] = vs[3] = vs_lo;
        else
            vs[0] = vs[1] = vs_lo, vs[2] = vs[3] = vs_hi;
        for (int k = 0; k < 4; ++k)
        {
            float qu = us[k] / b->tex_w + b->tex_u, qv = vs[k] / b->tex_h + b->tex_v;
            if (b->tex_matrix)
            {
                float ta = b->tm[0] * qu + b->tm[2] * qv + b->tm[4];
                float tb = b->tm[1] * qu + b->tm[3] * qv + b->tm[5];
                qu = ta;
                qv = tb;
            }
            q[f][k] = corner[ids[k]];
            q[f][k].u = qu;
            q[f][k].v = qv;
            q[f][k].diffuse = d;
        }
    }
}

/* 1 when no free since C's copy last matched its source could have put
 * other bytes at the source's address: no free of a block that could have
 * held BYTES (raster_rec_free_count). Without the count, a big texture
 * (over 64 KB) is trusted within a frame (WITHIN), a small one never. */
static int rec_unmoved(const struct rec_tex *c, size_t bytes, int within)
{
    return !rec_frees ? within && bytes > 65536 : c->checked == rec_frees(c->src, bytes);
}

/* raster_rec_verify: C's copy, taken back unchecked (where no write named),
 * against its source */
static void rec_verify_copy(const struct rec_tex *c, const unsigned char *src, size_t bytes)
{
    ++rec_verified;
    if (memcmp(c->copy, src, bytes)) {
        ++rec_verify_bad;
        fprintf(stderr, "raster_rec: a %dx%d texture's copy taken back unchecked differs from its source\n", c->w, c->h);
    }
}

/* C's copy matches its source now */
static void rec_checked(struct rec_tex *c, size_t bytes)
{
    c->written = REC_CLEAN;
    c->checked = rec_frees ? rec_frees(c->src, bytes) : 0;
}

/* C's copy (with its tile hashes and key) made SRC's bytes again in the
 * tiles DIRTY marks, those that differ copied and rehashed */
static void rec_retile(struct rec_tex *c, const unsigned char *src, const unsigned char *dirty)
{
    int ntx = key_tiles_x(c->w), nty = key_tiles_y(c->h), moved = 0;
    size_t rowb = (size_t)c->w * 4;
    for (int t = 0; t < ntx * nty; ++t) {
        if (!dirty[t]) continue;
        int tx = t % ntx, y0 = t / ntx * KEY_TILE, y1 = y0 + KEY_TILE < c->h ? y0 + KEY_TILE : c->h;
        size_t off = (size_t)tx * KEY_TILE * 4, len = rowb - off < KEY_TILE * 4 ? rowb - off : KEY_TILE * 4;
        int diff = 0;
        for (int y = y0; y < y1 && !diff; ++y) diff = memcmp(c->copy + (size_t)y * rowb + off, src + (size_t)y * rowb + off, len) != 0;
        if (!diff) continue;
        for (int y = y0; y < y1; ++y) memcpy(c->copy + (size_t)y * rowb + off, src + (size_t)y * rowb + off, len);
        c->tiles[t] = key_tile(c->copy, c->w, c->h, tx, t / ntx);
        moved = 1;
    }
    if (!moved) return;
    uint64_t k = key_begin(c->w, c->h);
    for (int t = 0; t < ntx * nty; ++t) k = key_add(k, c->tiles[t]);
    c->key = key_end(k);
}

/* the texture's entry: the copy taken when a primitive first sampled it */
static int rec_tex(struct raster_rec *r, const unsigned char *tex, int tex_w, int tex_h)
{
    size_t bytes = (size_t)tex_w * tex_h * 4;
    int ti = -1;
    for (int i = r->ntex - 1; i >= 0; --i)
        if (r->rt[i].src == tex && !r->rt[i].stale && r->tex[i].w == tex_w && r->tex[i].h == tex_h) {
            /* the bytes checked again once a block that could have held
             * them may have been freed and allocated anew at their address
             * (a write in place says so, and makes the entry stale) */
            if (!rec_unmoved(&r->rt[i], bytes, 1)) {
                if (memcmp(r->rt[i].copy, tex, bytes)) { r->rt[i].stale = 1; break; }
                rec_checked(&r->rt[i], bytes);
            } else if (rec_verify) rec_verify_copy(&r->rt[i], tex, bytes);
            ti = i;
            break;
        }
    if (ti < 0) {
        r->tex = grow(r->tex, &r->captex, r->ntex + 1, sizeof *r->tex);
        if (r->captex > r->caprt) {
            r->rt = realloc(r->rt, (size_t)r->captex * sizeof *r->rt);
            if (!r->rt) exit(1);
            memset(r->rt + r->caprt, 0, (size_t)(r->captex - r->caprt) * sizeof *r->rt);
            r->caprt = r->captex;
        }
        /* an earlier frame's copy of this source moves to the new entry;
         * with none, the unused entry whose buffer fits it most closely
         * (the smallest that holds it, else the largest): a 512 KB buffer
         * left on an entry that took 8 KB textures after it kept the 6.5
         * MB an env held where its frames used 0.5 to 4 (lane/heapprof) */
        int pick = -1;
        for (int j = r->ntex; j < r->caprt; ++j) {
            const struct rec_tex *e = &r->rt[j];
            if (e->src == tex && e->key && e->w == tex_w && e->h == tex_h) { pick = j; break; }
            if (pick < 0) { pick = j; continue; }
            size_t pc = r->rt[pick].cap;
            if (e->cap >= bytes ? pc < bytes || e->cap < pc : pc < bytes && e->cap > pc) pick = j;
        }
        if (pick > r->ntex) {
            struct rec_tex t = r->rt[pick];
            r->rt[pick] = r->rt[r->ntex];
            r->rt[r->ntex] = t;
        }
        struct rec_tex *c = &r->rt[r->ntex];
        int kept = c->src == tex && c->key && c->w == tex_w && c->h == tex_h;
        int unmoved = kept && rec_unmoved(c, bytes, 0);
        if (unmoved && c->written == REC_TILES) {
            /* an earlier frame's copy, written since only in known tiles */
            rec_retile(c, tex, c->tdirty);
            if (rec_verify) rec_verify_copy(c, tex, bytes);
        } else if (unmoved && c->written == REC_CLEAN) {
            /* nothing that could change its bytes happened since it matched */
            if (rec_verify) rec_verify_copy(c, tex, bytes);
        } else if (kept && memcmp(c->copy, tex, bytes)) {
            /* the tiles whose rows changed: copied again and rehashed */
            int ntx = key_tiles_x(tex_w), nty = key_tiles_y(tex_h);
            r->dirty = grow(r->dirty, &r->capdirty, ntx * nty, 1);
            memset(r->dirty, 0, (size_t)ntx * nty);
            size_t rowb = (size_t)tex_w * 4;
            for (int y = 0; y < tex_h; ++y) {
                const unsigned char *a = c->copy + (size_t)y * rowb, *b = tex + (size_t)y * rowb;
                if (!memcmp(a, b, rowb)) continue;
                for (int tx = 0; tx < ntx; ++tx) {
                    size_t off = (size_t)tx * KEY_TILE * 4, len = rowb - off < KEY_TILE * 4 ? rowb - off : KEY_TILE * 4;
                    if (memcmp(a + off, b + off, len)) r->dirty[(y / KEY_TILE) * ntx + tx] = 1;
                }
            }
            rec_retile(c, tex, r->dirty);
        }
        if (!kept) {
            if (c->cap < bytes) {
                free(c->copy);
                c->copy = malloc(bytes);
                if (!c->copy) exit(1);
                c->cap = bytes;
            }
            memcpy(c->copy, tex, bytes);
            c->key = 0;
        }
        c->src = tex;
        c->w = tex_w;
        c->h = tex_h;
        c->stale = 0;
        rec_checked(c, bytes);
        r->tex[r->ntex] = (struct raster_obs_tex){tex_w, tex_h, c->copy, c->key};
        ti = r->ntex++;
    }
    return ti;
}

void raster_rec_portal(const struct raster_obs_portal *p, const unsigned char *tex, int tex_w, int tex_h)
{
    struct raster_rec *r = rec_active;
    if (!r) return;
    int ti = rec_tex(r, tex, tex_w, tex_h);
    r->portal = grow(r->portal, &r->capportal, r->nportal + 1, sizeof *r->portal);
    r->portal[r->nportal] = *p;
    r->portal[r->nportal].tex = ti;
    rec_cmd(r, RASTER_CMD_PORTAL, r->nportal++);
}

void raster_rec_crack(const float p[3][7], const float *fog, float fogs, float foge, float fogd, int fogm)
{
    struct raster_rec *r = rec_active;
    if (!r) return;
    r->crack = grow(r->crack, &r->capcrack, r->ncrack + 1, sizeof *r->crack);
    struct raster_obs_crack *c = &r->crack[r->ncrack];
    memset(c, 0, sizeof *c);
    memcpy(c->p, p, sizeof c->p);
    c->fog_on = fog != NULL;
    for (int i = 0; fog && i < 3; ++i) c->fog[i] = fog[i];
    c->fogs = fogs; c->foge = foge; c->fogd = fogd; c->fogm = fogm;
    rec_cmd(r, RASTER_CMD_CRACK, r->ncrack++);
}

void raster_rec_line(const struct raster_obs_line *l)
{
    struct raster_rec *r = rec_active;
    if (!r) return;
    r->line = grow(r->line, &r->capline, r->nline + 1, sizeof *r->line);
    r->line[r->nline] = *l;
    rec_cmd(r, RASTER_CMD_LINE, r->nline++);
}

void raster_rec_attach(struct raster_rec *r, struct raster_obs *o)
{
    /* the textures' keys, on the recording's thread */
    for (int i = 0; i < r->ntex; ++i) {
        if (r->tex[i].key) continue;
        struct rec_tex *c = &r->rt[i];
        int ntx = key_tiles_x(c->w), n = ntx * key_tiles_y(c->h);
        if (c->ntiles < n) {
            free(c->tiles);
            free(c->tdirty);
            c->tiles = malloc((size_t)n * sizeof *c->tiles);
            c->tdirty = NULL;
            if (!c->tiles) exit(1);
            c->ntiles = n;
        }
        uint64_t k = key_begin(c->w, c->h);
        for (int t = 0; t < n; ++t) k = key_add(k, c->tiles[t] = key_tile(c->copy, c->w, c->h, t % ntx, t / ntx));
        c->key = r->tex[i].key = key_end(k);
    }
    o->ncmd = r->ncmd; o->nequad = r->nequad; o->nestate = r->nestate;
    o->equads_in_range = 1;
    o->ntex = r->ntex; o->nlm = r->nlm; o->ncrack = r->ncrack;
    o->cmd = r->cmd; o->equad = r->equad; o->estate = r->estate;
    o->tex = r->tex; o->lm_table = (const uint32_t (*)[256])r->lm; o->crack = r->crack;
    o->nportal = r->nportal; o->portal = r->portal;
    o->nline = r->nline; o->line = r->line;
    o->nebox = r->nebox; o->ebox = r->ebox;
    o->neitem = r->neitem; o->eitem = r->eitem;
    o->nebox_xf = r->nxf; o->ebox_xf = (const float (*)[32])r->xf;
}
