/* The native replay's state against an oracle snapshot of the same tick
 * (statediff.h). Everything is compared through the forms the gates already
 * trust: an entity through sr_entry_nbt (what d.ents hashes), a chunk
 * through snapshot_chunk_bytes (the Probe layout the snapshot recorded), a
 * tile entity through te_render, the scalars through worlds.nbt's and
 * det.nbt's own keys. */
#define _XOPEN_SOURCE 700
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "endfight.h"
#include "nbtjson.h"
#include "player.h"
#include "serverreplay.h"
#include "snapshot.h"
#include "statediff.h"
#include "env.h"
#include "tileentity.h"
#include "ticks.h"

/* ---------------------------------------------------------------- NBT paths */

static char *leaf_text(const nbt *v)
{
    char *t = nbt_render(v);
    nbt_type k = nbt_kind(v);
    if (k == NBT_DOUBLE || k == NBT_FLOAT)
    {
        /* the raw bits are exact; the decimal is for the reader */
        double d;
        if (k == NBT_DOUBLE) { uint64_t b = nbt_double_bits(v); memcpy(&d, &b, 8); }
        else { uint32_t b = nbt_float_bits(v); float f; memcpy(&f, &b, 4); d = f; }
        size_t n = strlen(t) + 40;
        char *u = malloc(n);
        snprintf(u, n, "%s (%.17g)", t, d);
        free(t);
        return u;
    }
    if (strlen(t) > 160) { t[157] = '.'; t[158] = '.'; t[159] = '.'; t[160] = 0; }
    return t;
}

struct nbtd { int n, max; const char *prefix; FILE *out; };

static void nbtd_line(struct nbtd *d, const char *path, const nbt *a, const nbt *b)
{
    if (d->n++ >= d->max || !d->out) return;
    char *wa = a ? leaf_text(a) : strdup("(absent)");
    char *wb = b ? leaf_text(b) : strdup("(absent)");
    fprintf(d->out, "%s%s: want %s got %s\n", d->prefix, path[0] ? path : "(root)", wa, wb);
    free(wa);
    free(wb);
}

static void nbtd_rec(struct nbtd *d, const nbt *a, const nbt *b, char *path, size_t cap)
{
    size_t len = strlen(path);
    if (nbt_kind(a) != nbt_kind(b)) { nbtd_line(d, path, a, b); return; }
    if (nbt_kind(a) == NBT_COMPOUND)
    {
        for (int i = 0; i < nbt_field_count(a); ++i)
        {
            const char *k = nbt_field_key(a, i);
            snprintf(path + len, cap - len, "%s%s", len ? "." : "", k);
            const nbt *bv = nbt_get(b, k);
            if (!bv) nbtd_line(d, path, nbt_field_value(a, i), NULL);
            else nbtd_rec(d, nbt_field_value(a, i), bv, path, cap);
            path[len] = 0;
        }
        for (int i = 0; i < nbt_field_count(b); ++i)
        {
            const char *k = nbt_field_key(b, i);
            if (nbt_get(a, k)) continue;
            snprintf(path + len, cap - len, "%s%s", len ? "." : "", k);
            nbtd_line(d, path, NULL, nbt_field_value(b, i));
            path[len] = 0;
        }
        return;
    }
    if (nbt_kind(a) == NBT_LIST)
    {
        int na = nbt_list_size(a), nb = nbt_list_size(b);
        if (na != nb)
        {
            if (d->n++ < d->max && d->out)
                fprintf(d->out, "%s%s: want %d entries got %d\n", d->prefix, path, na, nb);
        }
        for (int i = 0; i < na && i < nb; ++i)
        {
            snprintf(path + len, cap - len, "[%d]", i);
            nbtd_rec(d, nbt_list_get(a, i), nbt_list_get(b, i), path, cap);
            path[len] = 0;
        }
        return;
    }
    char *ta = nbt_render(a), *tb = nbt_render(b);
    if (strcmp(ta, tb)) nbtd_line(d, path, a, b);
    free(ta);
    free(tb);
}

int statediff_nbt(const void *want, const void *got, const char *prefix, int max, FILE *out)
{
    struct nbtd d = {0, max, prefix, out};
    char path[512] = "";
    nbtd_rec(&d, want, got, path, sizeof path);
    return d.n;
}

/* ------------------------------------------------------------------ scalars */

static int64_t tag_long(const nbt *comp, const char *key, int *ok)
{
    const nbt *v = comp ? nbt_get(comp, key) : NULL;
    if (!v) { *ok = 0; return 0; }
    *ok = 1;
    switch (nbt_kind(v))
    {
        case NBT_FLOAT: return (int64_t)nbt_float_bits(v);
        case NBT_DOUBLE: return (int64_t)nbt_double_bits(v);
        default: return (int64_t)nbt_int_value(v);
    }
}

static uint32_t fbits(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}

struct sd {
    FILE *out;
    int lines;
    struct statediff_sum *sum;
    int total;
};

static void scalar(struct sd *d, const char *where, const char *key, const nbt *comp, int64_t got, int is_float)
{
    int ok;
    int64_t want = tag_long(comp, key, &ok);
    if (!ok) return;
    if (is_float) { want = (uint32_t)want; got = (uint32_t)got; }
    if (want == got) return;
    ++d->sum->scalars;
    ++d->total;
    if (d->sum->scalars > d->lines) return;
    if (is_float)
    {
        float fw, fg;
        uint32_t bw = (uint32_t)want, bg = (uint32_t)got;
        memcpy(&fw, &bw, 4);
        memcpy(&fg, &bg, 4);
        fprintf(d->out, "  %s %s: want %.9g got %.9g\n", where, key, fw, fg);
    }
    else fprintf(d->out, "  %s %s: want %lld got %lld\n", where, key, (long long)want, (long long)got);
}

static void diff_scalars(struct sd *d, const struct snapshot *want, struct serverreplay *sr, const char *dir)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/worlds.nbt", dir);
    FILE *f = fopen(path, "rb");
    nbt *worlds = NULL;
    if (f)
    {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *text = malloc((size_t)n + 1);
        if (fread(text, 1, (size_t)n, f) == (size_t)n)
        {
            while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r')) --n;
            text[n] = 0;
            worlds = nbt_parse(text);
        }
        free(text);
        fclose(f);
    }

    fprintf(d->out, "world scalars:\n");
    int before = d->sum->scalars;
    if (!worlds)
    {
        fprintf(d->out, "  %s does not load: the worlds' scalars are not compared\n", path);
        ++d->sum->scalars;
        ++d->total;
    }
    for (int i = 0; worlds && i < sr->nworlds; ++i)
    {
        const struct servertick *st = &sr->w[i].st;
        const nbt *w = NULL;
        char key[8];
        for (int k = 0; k < 3 && !w; ++k)
        {
            snprintf(key, sizeof key, "w%d", k);
            const nbt *c = nbt_get(worlds, key);
            int ok;
            if (c && tag_long(c, "dim", &ok) == sr->w[i].dim && ok) w = c;
        }
        if (!w) continue;
        char where[32];
        snprintf(where, sizeof where, "dim %d", sr->w[i].dim);
        /* the Nether's and the End's WorldInfo is a DerivedWorldInfo: its
         * clocks and weather are the overworld's, compared there */
        if (sr->w[i].dim == 0)
        {
            scalar(d, where, "time", w, st->world_time, 0);
            scalar(d, where, "total", w, st->total_time, 0);
            scalar(d, where, "rainTime", w, st->rain_time, 0);
            scalar(d, where, "thunderTime", w, st->thunder_time, 0);
            scalar(d, where, "raining", w, st->raining, 0);
            scalar(d, where, "thundering", w, st->thundering, 0);
        }
        scalar(d, where, "rand", w, (int64_t)(ST_RAND(st).seed & ((1ULL << 48) - 1)), 0);
        scalar(d, where, "updateLCG", w, ST_LCG(st), 0);
        /* a world with no sky recomputes skylightSubtracted at the head of
         * every WorldServer.tick, before its entities read it (isDaytime), and
         * its only earlier read (the spawner's light) has no sky to subtract
         * from: between ticks it is dead state there (the Nether and the End
         * have no sky; the native flag is set only in a ticked world) */
        if (sr->w[i].dim == 0) scalar(d, where, "skylightSubtracted", w, st->skylight_subtracted, 0);
        scalar(d, where, "rainingStrength", w, fbits(st->raining_strength), 1);
        scalar(d, where, "prevRainingStrength", w, fbits(st->prev_raining_strength), 1);
        scalar(d, where, "thunderingStrength", w, fbits(st->thundering_strength), 1);
        scalar(d, where, "prevThunderingStrength", w, fbits(st->prev_thundering_strength), 1);
        scalar(d, where, "ambientTickCountdown", w, st->ambient_tick_countdown, 0);
        scalar(d, where, "difficulty", w, st->difficulty, 0);
        /* WorldServer.updateEntities only asks updateEntityTick++ >= 1200 (and
         * resets it): every value from 1200 on is the same state */
        {
            int ok;
            int64_t wv = tag_long(w, "updateEntityTick", &ok), gv = sr->w[i].update_entity_tick;
            if (ok && (wv < 1200 || gv < 1200) && wv != gv)
            {
                /* the counter of a world with no entity on either side has had
                 * no effect yet (the native replay does not enter an empty
                 * world's pass, so it does not count there): a note, not a
                 * difference, until an entity arrives */
                int java_ents = 0;
                for (int k = 0; k < want->nents; ++k) java_ents += want->ents[k].dim == sr->w[i].dim;
                if (java_ents == 0 && serverreplay_dim_nents(sr, sr->w[i].dim) == 0)
                    fprintf(d->out, "  note: dim %d updateEntityTick want %lld got %lld (no entity in that world on either side)\n",
                            sr->w[i].dim, (long long)wv, (long long)gv);
                else scalar(d, where, "updateEntityTick", w, gv, 0);
            }
        }
        if (i == 0)
        {
            scalar(d, where, "nextTickEntryID", w, ticks_next_entry_id(), 0);
            scalar(d, where, "blkHash", w, (int64_t)sr->blk_hash, 0);
        }
    }
    nbt_free(worlds);

    /* Det: the server role's seeder and Math stream and entity id counter,
     * then each split stream's server state by name (what d.sseed, d.smath
     * and d.sstat digest; the client's streams draw for particles and other
     * client work the native replay does not model, and no row checks them) */
    static const char *roles[4] = {"CLIENT", "SERVER", "OTHER", "RENDER"};
    const nbt *seeder = nbt_get(want->det, "seeder"), *math = nbt_get(want->det, "math"), *ids = nbt_get(want->det, "nextId");
    for (int r = DET_SERVER; r == DET_SERVER && seeder && math && ids; ++r)
    {
        int64_t ws = (int64_t)nbt_int_value(nbt_list_get(seeder, r)), wm = (int64_t)nbt_int_value(nbt_list_get(math, r));
        int64_t wi = (int64_t)nbt_int_value(nbt_list_get(ids, r));
        struct { const char *k; int64_t w, g; } v[3] = {
            {"seeder", ws, (int64_t)det_rng_state(&SR_DET(sr).seeder[r])},
            {"math", wm, (int64_t)det_rng_state(&SR_DET(sr).math[r])},
            {"nextId", wi, SR_DET(sr).next_id[r]},
        };
        for (int k = 0; k < 3; ++k)
        {
            if (v[k].w == v[k].g) continue;
            ++d->sum->det;
            ++d->total;
            if (d->sum->det <= d->lines)
            {
                if (k == 2)
                    fprintf(d->out, "  det %s %s: want %lld got %lld (%+lld ids drawn natively)\n", roles[r], v[k].k,
                            (long long)v[k].w, (long long)v[k].g, (long long)(v[k].g - v[k].w));
                else
                    fprintf(d->out, "  det %s %s: want %012llx got %012llx\n", roles[r], v[k].k,
                            (unsigned long long)v[k].w, (unsigned long long)v[k].g);
            }
        }
    }
    const nbt *splits = nbt_get(want->det, "splits");
    for (int i = 0; splits && i < nbt_list_size(splits); ++i)
    {
        const nbt *e = nbt_list_get(splits, i);
        const nbt *nv = nbt_get(e, "name");
        const char *name = nv && nbt_kind(nv) == NBT_STRING ? nbt_string_value(nv) : NULL;
        const nbt *states = nbt_get(e, "state"), *used = nbt_get(e, "used");
        if (!name || !states) continue;
        const det_split *sp = SR_DET(sr).splits;
        while (sp && strcmp(sp->name, name)) sp = sp->next;
        for (int r = DET_SERVER; r == DET_SERVER; ++r)
        {
            /* a stream neither side has drawn from on the server (Java lists
             * every split it registered; the native one exists once drawn) */
            if (!(used && nbt_int_value(nbt_list_get(used, r))) && !(sp && sp->used[r])) continue;
            uint64_t w = (uint64_t)nbt_int_value(nbt_list_get(states, r));
            uint64_t g = sp ? det_rng_state(&sp->d[r]) : ~0ULL;
            if (w == g) continue;
            ++d->sum->det;
            ++d->total;
            if (d->sum->det <= d->lines)
                fprintf(d->out, "  det split %s %s: want %012llx got %012llx\n", name, roles[r],
                        (unsigned long long)w, (unsigned long long)g);
        }
    }
    if (d->sum->scalars + d->sum->det == before) fprintf(d->out, "  all equal\n");
}

/* ----------------------------------------------------------------- entities */

struct nent { int id, dim, k; };

static void pos_of(const nbt *tag, char *buf, size_t n)
{
    const nbt *p = tag ? nbt_get(tag, "Pos") : NULL;
    double v[3] = {0, 0, 0};
    for (int i = 0; p && i < 3 && i < nbt_list_size(p); ++i)
    {
        uint64_t b = nbt_double_bits(nbt_list_get(p, i));
        memcpy(&v[i], &b, 8);
    }
    if (p) snprintf(buf, n, "(%.2f, %.2f, %.2f)", v[0], v[1], v[2]);
    else snprintf(buf, n, "(no Pos)");
}

static const char *nbt_id(const nbt *tag)
{
    const nbt *v = tag ? nbt_get(tag, "id") : NULL;
    const char *s = v && nbt_kind(v) == NBT_STRING ? nbt_string_value(v) : NULL;
    return s ? s : "(no id in its NBT)";
}

static void diff_entities(struct sd *d, const struct snapshot *start, const struct snapshot *want,
                          struct serverreplay *sr, const struct server_player *sp)
{
    static const int dims[3] = {0, -1, 1};
    fprintf(d->out, "entities:\n");
    int shown_missing = 0, shown_extra = 0, shown_diff = 0, shown_dup = 0;
    for (int di = 0; di < 3; ++di)
    {
        int dim = dims[di];
        int nwant = 0;
        for (int i = 0; i < want->nents; ++i) if (want->ents[i].dim == dim) ++nwant;
        int ngot = serverreplay_dim_nents(sr, dim);
        if (nwant == 0 && ngot == 0) continue;
        serverreplay_enter(sr, dim);

        /* the native list in pass order, each entry's id and tree */
        int n = sr->d->nents;
        int *gid = malloc((size_t)(n ? n : 1) * sizeof *gid);
        nbt **gtag = malloc((size_t)(n ? n : 1) * sizeof *gtag);
        int *matched = calloc((size_t)(n ? n : 1), sizeof *matched);
        int live = 0;
        for (int k = 0; k < n; ++k)
        {
            gtag[k] = NULL;
            gid[k] = -1;
            if (sr->d->ents[k].pool == 3 && sp->sv.removed) continue;
            gtag[k] = sr_entry_nbt(start, sp, &sr->d->ents[k], &gid[k]);
            ++live;
        }
        int missing = 0, extra = 0, differ = 0, order = 0, wi = 0;
        for (int i = 0; i < want->nents; ++i)
        {
            const struct snap_entity *we = &want->ents[i];
            if (we->dim != dim) continue;
            int k = 0;
            while (k < n && (gid[k] != we->id || !gtag[k])) ++k;
            if (k == n)
            {
                ++missing;
                if (d->sum->first_entity < 0) d->sum->first_entity = we->id;
                if (shown_missing++ < d->lines)
                {
                    char pos[80], npos[80];
                    pos_of(we->tag, pos, sizeof pos);
                    /* the same entity under another id: a native one with no
                     * java match at exactly this position (an id drawn on one
                     * side only, earlier) */
                    int twin = -1;
                    for (int k = 0; k < n && twin < 0; ++k)
                    {
                        if (!gtag[k] || gid[k] == we->id || matched[k]) continue;
                        pos_of(gtag[k], npos, sizeof npos);
                        if (!strcmp(pos, npos) && strcmp(pos, "(no Pos)")) twin = k;
                    }
                    fprintf(d->out, "  dim %d: id %d %s at %s is missing natively (java list index %d)", dim, we->id,
                            we->cls, pos, wi);
                    if (twin >= 0)
                        fprintf(d->out, "; native has an entity at that position as id %d (%+d: an id drawn on one side only before it)",
                                gid[twin], gid[twin] - we->id);
                    fprintf(d->out, "\n");
                }
                ++wi;
                continue;
            }
            matched[k] = 1;
            if (k != wi) ++order;
            ++wi;
            char head[160];
            snprintf(head, sizeof head, "    ");
            int nd = statediff_nbt(we->tag, gtag[k], head, 0, NULL);
            if (nd)
            {
                ++differ;
                if (d->sum->first_entity < 0) d->sum->first_entity = we->id;
                if (shown_diff++ < d->lines)
                {
                    char pos[80];
                    pos_of(we->tag, pos, sizeof pos);
                    fprintf(d->out, "  dim %d: id %d %s at %s: %d NBT paths differ\n", dim, we->id, we->cls, pos, nd);
                    statediff_nbt(we->tag, gtag[k], "    ", d->lines > 8 ? d->lines : 8, d->out);
                }
            }
        }
        for (int k = 0; k < n; ++k)
        {
            if (!gtag[k] || matched[k]) continue;
            ++extra;
            if (d->sum->first_entity < 0) d->sum->first_entity = gid[k];
            if (shown_extra++ < d->lines)
            {
                char pos[80];
                pos_of(gtag[k], pos, sizeof pos);
                fprintf(d->out, "  dim %d: id %d %s at %s is extra natively (native list index %d)\n", dim, gid[k],
                        nbt_id(gtag[k]), pos, k);
            }
        }
        /* an id the native list holds twice: a draw the other side made */
        for (int k = 0; k < n; ++k)
            for (int j = k + 1; gtag[k] && j < n; ++j)
                if (gtag[j] && gid[j] == gid[k] && shown_dup++ < d->lines)
                    fprintf(d->out, "  dim %d: the native list holds id %d twice (index %d and %d)\n", dim, gid[k], k, j);
        for (int k = 0; k < n; ++k) nbt_free(gtag[k]);
        free(gtag);
        free(gid);
        free(matched);
        fprintf(d->out, "  dim %d: java %d, native %d; %d missing natively, %d extra, %d differ%s\n", dim, nwant, live,
                missing, extra, differ, order && !missing && !extra ? ", list order differs" : "");
        d->sum->ents_missing += missing;
        d->sum->ents_extra += extra;
        d->sum->ents_differ += differ;
        if (!missing && !extra && order) ++d->sum->ents_order;
        d->total += missing + extra + differ + (!missing && !extra && order ? 1 : 0);
    }
    serverreplay_enter(sr, serverreplay_player_dim(sr));
}

/* ------------------------------------------------------------------- chunks */

static struct world *native_world(struct serverreplay *sr, int dim)
{
    return dim == -1 ? &sr->hell.world : dim == 1 ? &sr->sky.world : &sr->pop.world;
}

static void diff_chunks(struct sd *d, const struct snapshot *want, struct serverreplay *sr)
{
    static const int dims[3] = {0, -1, 1};
    fprintf(d->out, "chunks:\n");
    uint8_t *got = malloc(SNAP_CHUNK_BYTES), *wbuf = malloc(SNAP_CHUNK_BYTES);
    int shown = 0, shown_tiles = 0, shown_set = 0;
    for (int di = 0; di < 3; ++di)
    {
        int dim = dims[di];
        struct world *w = native_world(sr, dim);
        int nwant = 0, differ = 0, missing = 0, extra = 0, tiles = 0, inhab = 0;
        for (int i = 0; i < want->nchunks; ++i)
        {
            const struct snap_chunk *sc = &want->chunks[i];
            if (sc->dim != dim || sc->saved) continue;
            ++nwant;
            struct chunk *c = world_chunk(w, sc->cx, sc->cz);
            if (!c)
            {
                ++missing;
                if (shown_set++ < d->lines) fprintf(d->out, "  dim %d: chunk (%d,%d) is loaded in java, not natively\n", dim, sc->cx, sc->cz);
                continue;
            }
            snapshot_chunk_bytes(c, got);
            const uint8_t *wb = snapshot_chunk_want(sc, wbuf);
            char what[256];
            if (wb && snapshot_chunk_diff(wb, got, what, sizeof what))
            {
                ++differ;
                if (shown++ < d->lines)
                {
                    /* the cells that differ, per field */
                    int nid = 0, nmeta = 0, nsky = 0, nbl = 0;
                    for (int k = 0; k < 65536; ++k)
                    {
                        nid += wb[2 * k] != got[2 * k] || wb[2 * k + 1] != got[2 * k + 1];
                        nmeta += wb[131072 + k] != got[131072 + k];
                        nsky += wb[196608 + k] != got[196608 + k];
                        nbl += wb[262144 + k] != got[262144 + k];
                    }
                    fprintf(d->out, "  dim %d: chunk (%d,%d): %d id, %d meta, %d sky, %d block-light cells differ; first %s\n",
                            dim, sc->cx, sc->cz, nid, nmeta, nsky, nbl, what);
                }
            }
            if (c->inhabited_time != sc->inhabited_time)
            {
                ++inhab;
                if (shown++ < d->lines)
                    fprintf(d->out, "  dim %d: chunk (%d,%d) inhabitedTime want %lld got %lld\n", dim, sc->cx, sc->cz,
                            (long long)sc->inhabited_time, (long long)c->inhabited_time);
            }
            /* tile entities by position */
            int nte = c->tes.n;
            if (nte != sc->ntiles)
            {
                ++tiles;
                if (shown_tiles++ < d->lines)
                    fprintf(d->out, "  dim %d: chunk (%d,%d): %d tile entities in java, %d natively\n", dim, sc->cx, sc->cz,
                            sc->ntiles, nte);
            }
            for (int t = 0; t < sc->ntiles; ++t)
            {
                const nbt *wt = sc->tiles[t].tag;
                int x = (int)nbt_int_value(nbt_get(wt, "x")), y = (int)nbt_int_value(nbt_get(wt, "y")),
                    z = (int)nbt_int_value(nbt_get(wt, "z"));
                const struct tile_entity *mine = NULL;
                for (int k = 0; k < nte && !mine; ++k)
                    if (c->tes.v[k]->x == x && c->tes.v[k]->y == y && c->tes.v[k]->z == z) mine = c->tes.v[k];
                if (!mine) continue;
                char *gt = te_render(mine);
                nbt *gtag = gt ? nbt_parse(gt) : NULL;
                if (gtag && statediff_nbt(wt, gtag, "", 0, NULL))
                {
                    ++tiles;
                    if (shown_tiles++ < d->lines)
                    {
                        fprintf(d->out, "  dim %d: tile entity %s at (%d,%d,%d):\n", dim, nbt_id(wt), x, y, z);
                        statediff_nbt(wt, gtag, "    ", 6, d->out);
                    }
                }
                nbt_free(gtag);
                free(gt);
            }
        }
        /* the native world's own chunks java does not have loaded */
        for (size_t i = 0; i < w->cap; ++i)
        {
            const struct chunk *c = chunk_ptr(w->slot[i]);
            if (!c) continue;
            int found = 0;
            for (int k = 0; k < want->nchunks && !found; ++k)
                found = want->chunks[k].dim == dim && !want->chunks[k].saved && want->chunks[k].cx == c->cx &&
                    want->chunks[k].cz == c->cz;
            if (found) continue;
            ++extra;
            if (shown_set++ < d->lines) fprintf(d->out, "  dim %d: chunk (%d,%d) is loaded natively, not in java\n", dim, c->cx, c->cz);
        }
        if (nwant == 0 && w->used == 0) continue;
        fprintf(d->out, "  dim %d: java %d loaded, native %zu; %d missing natively, %d extra, %d differ in bytes, "
                "%d in inhabitedTime, %d tile entity differences\n", dim, nwant, w->used, missing, extra, differ, inhab, tiles);
        d->sum->chunks_missing += missing;
        d->sum->chunks_extra += extra;
        d->sum->chunks_differ += differ + inhab;
        d->sum->tiles_differ += tiles;
        d->total += missing + extra + differ + inhab + tiles;
    }
    free(got);
    free(wbuf);
}

/* ------------------------------------------------------------ pending ticks */

struct tick_walk { struct tick_entry *v; int n, cap; };

static void tick_collect(void *ctx, const struct tick_entry *e)
{
    struct tick_walk *w = ctx;
    if (w->n == w->cap)
    {
        w->cap = w->cap ? 2 * w->cap : 1024;
        w->v = realloc(w->v, (size_t)w->cap * sizeof *w->v);
    }
    w->v[w->n++] = *e;
}

static void diff_ticks(struct sd *d, const struct snapshot *want, struct serverreplay *sr)
{
    static const int dims[3] = {0, -1, 1};
    fprintf(d->out, "pending block ticks:\n");
    int printed = 0;
    for (int di = 0; di < 3; ++di)
    {
        int dim = dims[di];
        int in_sr = 0;
        for (int i = 0; i < sr->nworlds; ++i) in_sr |= sr->w[i].dim == dim;
        if (!in_sr) continue;
        serverreplay_enter(sr, dim);
        if (sr->here != dim) continue;
        struct tick_walk w = {0};
        ticks_walk(tick_collect, &w);
        int nwant = 0, first = -1;
        for (int i = 0; i < want->nticks; ++i)
        {
            const struct snap_tick *t = &want->ticks[i];
            if (t->dim != dim || t->list != 0) continue;
            if (first < 0 && (nwant >= w.n || w.v[nwant].x != t->x || w.v[nwant].y != t->y || w.v[nwant].z != t->z ||
                              w.v[nwant].block != t->id || w.v[nwant].time != t->scheduled ||
                              w.v[nwant].priority != t->priority || w.v[nwant].entry != t->entry))
            {
                first = nwant;
                fprintf(d->out, "  dim %d: entry %d want (%d,%d,%d) id %d at %lld pri %d eid %lld", dim, nwant, t->x, t->y,
                        t->z, t->id, (long long)t->scheduled, t->priority, (long long)t->entry);
                if (nwant < w.n)
                    fprintf(d->out, " got (%d,%d,%d) id %d at %lld pri %d eid %lld\n", w.v[nwant].x, w.v[nwant].y,
                            w.v[nwant].z, w.v[nwant].block, (long long)w.v[nwant].time, w.v[nwant].priority,
                            (long long)w.v[nwant].entry);
                else fprintf(d->out, " got (none)\n");
            }
            ++nwant;
        }
        if (nwant != w.n && first < 0) first = nwant < w.n ? nwant : w.n;
        if (nwant == 0 && w.n == 0) { free(w.v); continue; }
        fprintf(d->out, "  dim %d: java %d, native %d%s\n", dim, nwant, w.n, first >= 0 ? "" : ", all equal");
        printed = 1;
        if (first >= 0) { ++d->sum->ticks_differ; ++d->total; }
        free(w.v);
    }
    if (!printed) fprintf(d->out, "  none in either\n");
    serverreplay_enter(sr, serverreplay_player_dim(sr));
}

int statediff_run(const struct snapshot *start, struct serverreplay *sr, const struct server_player *sp,
                  const char *dir, int lines, FILE *out, struct statediff_sum *sum)
{
    struct snapshot *wantp = calloc(1, sizeof *wantp); /* large: off the stack */
    if (!wantp) return -1;
    if (!snapshot_load(wantp, dir)) { free(wantp); return -1; }
    struct statediff_sum local;
    if (!sum) sum = &local;
    memset(sum, 0, sizeof *sum);
    sum->first_entity = -1;
    struct sd d = {out, lines, sum, 0};
    fprintf(out, "state at the head of tick %lld against %s\n", (long long)wantp->tick, dir);
    diff_scalars(&d, wantp, sr, dir);
    diff_entities(&d, start, wantp, sr, sp);
    diff_chunks(&d, wantp, sr);
    diff_ticks(&d, wantp, sr);
    snapshot_free(wantp);
    free(wantp);
    return d.total;
}
