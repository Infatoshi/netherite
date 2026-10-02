/* See regioncache.h. */
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#include "regioncache.h"
#include "env.h"

#include "itemtag.h"
#include "sha1.h"
#include "tileentity.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef REGIONCACHE_BUILD
#define REGIONCACHE_BUILD "unknown"
#endif

/* the build hash in an object of its own, read through a pointer the
 * compiler cannot see into: the test selection (csrc/tests/fnkey.c) takes
 * a *_fnkey_opaque object by its name only. The hash names every source,
 * so otherwise every replay that keys the cache would rerun on any change;
 * the image it keys is the build's, whichever code made it. */
static const char rc_build_fnkey_opaque[] = REGIONCACHE_BUILD;

#define RC_MAGIC "NWRCIMG1"

static int64_t rc_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static size_t rc_pad(size_t n)
{
    return (n + 7) & ~(size_t)7;
}

/* The image's own memory is the process's (envheap.h proc_malloc): only
 * the chunks and tile entities it makes are the environment's. */
static void *rc_alloc(size_t n)
{
    void *p = proc_malloc(n ? n : 1);
    if (p == NULL) abort();
    return p;
}

static void *rc_zalloc(size_t n)
{
    void *p = proc_calloc(1, n ? n : 1);
    if (p == NULL) abort();
    return p;
}

static void *rc_grow(void *p, size_t old, size_t n)
{
    void *q = rc_alloc(n);
    if (p != NULL) memcpy(q, p, old);
    proc_free(p);
    return q;
}

/* ------------------------------------------------------------ the key */

static void rc_key(uint8_t out[20], int64_t seed, int quiet_join)
{
    uint8_t m[1536];
    size_t n = 0;
#define PUT(p, len) do { memcpy(m + n, (p), (len)); n += (len); } while (0)
    PUT("nw-regioncache-1", 16);
    const char *build = rc_build_fnkey_opaque;
    __asm__("" : "+r"(build));
    uint32_t blen = (uint32_t)strlen(build);
    PUT(&blen, sizeof blen);
    PUT(build, blen);
    uint32_t sizes[5] = {
        (uint32_t)sizeof(struct chunk), (uint32_t)sizeof(struct tile_entity),
        (uint32_t)sizeof(struct sw_entity), (uint32_t)sizeof(struct tick_entry), (uint32_t)sizeof(void *)
    };
    PUT(sizes, sizeof sizes);
    PUT(&seed, sizeof seed);
    int32_t q = quiet_join != 0;
    PUT(&q, sizeof q);
    PUT(&nw_env->cfg, sizeof nw_env->cfg);
    PUT(nw_env->decorator.bigtree_state, sizeof nw_env->decorator.bigtree_state);
#undef PUT
    sha1(m, n, out);
}

static void rc_hex(const uint8_t key[20], char out[41])
{
    for (int i = 0; i < 20; ++i) snprintf(out + i * 2, 3, "%02x", key[i]);
}

/* ------------------------------------------------------ the directory */

static int rc_mode(char *dir, size_t n)
{
    int mode = nw_env->regioncache.mode;
    if (mode == 1) return 0;
    const char *v = nw_env->regioncache.dir;
    if (v != NULL) snprintf(dir, n, "%s", v);
    else
    {
        const char *home = getenv("HOME");
        if (home == NULL || home[0] == 0 || strlen(home) > 900) return 0;
        snprintf(dir, n, "%s/.cache", home);
        mkdir(dir, 0755);
        strcat(dir, "/netherite-regioncache");
    }
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) return 0;
    return mode == 2 ? 2 : 1;
}

int regioncache_flag(const char *v)
{
    if (v == NULL) return 0;
    if (strcmp(v, "on") == 0) nw_env->regioncache.mode = 0;
    else if (strcmp(v, "off") == 0) nw_env->regioncache.mode = 1;
    else if (strcmp(v, "verify") == 0) nw_env->regioncache.mode = 2;
    else if (strncmp(v, "verify:/", 8) == 0)
    {
        nw_env->regioncache.mode = 2;
        nw_env->regioncache.dir = v + 7;
    }
    else if (v[0] == '/' && strlen(v) < 900)
    {
        nw_env->regioncache.mode = 0;
        nw_env->regioncache.dir = v;
    }
    else return 0;
    return 1;
}

static void rc_path(const struct rc_image *img, const char *suffix, char *out, size_t n)
{
    char hex[41];
    rc_hex(img->key, hex);
    snprintf(out, n, "%s/%s%s", img->dir, hex, suffix);
}

/* The image file of the key, mapped, when it is there and whole. */
static int rc_load(struct rc_image *img)
{
    char path[1200];
    rc_path(img, ".img", path, sizeof path);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < 40)
    {
        close(fd);
        return 0;
    }
    void *p = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (p == MAP_FAILED)
    {
        close(fd);
        return 0;
    }
    /* the least recently used goes first */
    futimens(fd, NULL);
    close(fd);
    if (memcmp(p, RC_MAGIC, 8) != 0 || memcmp((uint8_t *)p + 8, img->key, 20) != 0)
    {
        munmap(p, (size_t)st.st_size);
        return 0;
    }
    img->buf = p;
    img->n = (size_t)st.st_size;
    img->mapped = 1;
    return 1;
}

__attribute__((noinline)) static void rc_evict(const struct rc_image *img)
{
    DIR *d = opendir(img->dir);
    if (d == NULL) return;
    struct ent { char name[256]; off_t size; struct timespec mtime; } *v = NULL;
    int nv = 0, cap = 0;
    uint64_t total = 0;
    struct dirent *de;
    time_t day_ago = time(NULL) - 86400;
    while ((de = readdir(d)) != NULL)
    {
        size_t len = strlen(de->d_name);
        if (len < 5 || len >= 256) continue;
        char path[1400];
        snprintf(path, sizeof path, "%s/%s", img->dir, de->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        /* a crashed writer's temporary and a lock a day idle go too */
        if (strcmp(de->d_name + len - 4, ".tmp") == 0 || strcmp(de->d_name + len - 5, ".lock") == 0)
        {
            if (st.st_mtime < day_ago) unlink(path);
            continue;
        }
        if (strcmp(de->d_name + len - 4, ".img") != 0) continue;
        if (nv == cap)
        {
            cap = cap ? cap * 2 : 64;
            v = rc_grow(v, (size_t)nv * sizeof *v, (size_t)cap * sizeof *v);
        }
        snprintf(v[nv].name, sizeof v[nv].name, "%s", de->d_name);
        v[nv].size = st.st_size;
#if defined(__APPLE__)
        v[nv].mtime = st.st_mtimespec;
#else
        v[nv].mtime = st.st_mtim;
#endif
        total += (uint64_t)st.st_size;
        ++nv;
    }
    closedir(d);
    uint64_t cap_bytes = (uint64_t)REGIONCACHE_CAP_MB << 20;
    while (total > cap_bytes && nv > 0)
    {
        int old = 0;
        for (int i = 1; i < nv; ++i)
            if (v[i].mtime.tv_sec < v[old].mtime.tv_sec ||
                (v[i].mtime.tv_sec == v[old].mtime.tv_sec && v[i].mtime.tv_nsec < v[old].mtime.tv_nsec))
                old = i;
        char path[1400];
        snprintf(path, sizeof path, "%s/%s", img->dir, v[old].name);
        unlink(path);
        total -= (uint64_t)v[old].size;
        v[old] = v[--nv];
    }
    proc_free(v);
}

static void rc_log(const struct rc_image *img, const char *what)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/stats.tsv", img->dir);
    FILE *f = fopen(path, "a");
    if (f == NULL) return;
    char hex[41];
    rc_hex(img->key, hex);
    fprintf(f, "%lld\t%s\t%.12s\t%.3f\t%zu\n", (long long)time(NULL), what, hex, (double)img->ns / 1e6, img->n);
    fclose(f);
}

/* ------------------------------------------------------ the image */

/* The image layout: RC_MAGIC, the key, then (each part padded to 8 bytes)
 *   u32 nchunks, nents, njournal, pad; u64 the chunk stamp's advance;
 *   the big-tree state after the build;
 *   per journal tag: u32 length, the text;
 *   the entities (struct sw_entity each);
 *   per chunk: the struct (pointers cleared), u32 the tile entity count,
 *   u32 the pending tick count, the packed blob (packed_n bytes), per tile
 *   entity its struct (pointers cleared, stack tags as journal position +
 *   1) and its two strings (i32 length, -1 for none, then the bytes), the
 *   ticks (struct tick_entry each). */

struct rc_out {
    uint8_t *p;
    size_t n, cap;
};

static void out_put(struct rc_out *o, const void *p, size_t n)
{
    if (o->p != NULL) memcpy(o->p + o->n, p, n);
    o->n += n;
    size_t pad = rc_pad(o->n) - o->n;
    if (pad && o->p != NULL) memset(o->p + o->n, 0, pad);
    o->n += pad;
}

static void out_str(struct rc_out *o, const char *s)
{
    int32_t len = s ? (int32_t)strlen(s) : -1;
    if (o->p != NULL) memcpy(o->p + o->n, &len, sizeof len);
    o->n += sizeof len;
    if (len > 0) { if (o->p != NULL) memcpy(o->p + o->n, s, (size_t)len); o->n += (size_t)len; }
    size_t pad = rc_pad(o->n) - o->n;
    if (pad && o->p != NULL) memset(o->p + o->n, 0, pad);
    o->n += pad;
}

/* A tile entity's stack arrays, the ones te_free clears. */
static struct te_stack *te_slots(struct tile_entity *te, int *n)
{
    switch (te->kind)
    {
    case TE_FURNACE: *n = 3; return te->u.furnace.slots;
    case TE_CHEST: *n = CHEST_SLOTS; return te->u.chest.slots;
    case TE_HOPPER: *n = 5; return te->u.hopper.slots;
    case TE_BREWING_STAND: *n = 4; return te->u.brewing.slots;
    case TE_DISPENSER: *n = DISPENSER_SLOTS; return te->u.dispenser.slots;
    default: *n = 0; return NULL;
    }
}

/* The journal's distinct tags in first-use order; a tag the build did not
 * journal (none should be) goes at the end. */
struct rc_journal {
    int *v;
    int n, cap;
};

static int journal_pos(struct rc_journal *j, int tag)
{
    for (int i = 0; i < j->n; ++i)
        if (j->v[i] == tag) return i;
    if (j->n == j->cap)
    {
        j->cap = j->cap ? j->cap * 2 : 64;
        j->v = rc_grow(j->v, (size_t)j->n * sizeof *j->v, (size_t)j->cap * sizeof *j->v);
    }
    j->v[j->n] = tag;
    return j->n++;
}

static void rc_serialize(struct rc_out *o, const struct rc_image *img, const struct seedworld *sw,
                         struct rc_journal *jr, uint64_t stamp_delta)
{
    const struct world *w = &sw->p.world;
    uint32_t nchunks = 0;
    for (size_t i = 0; i < w->cap; ++i)
        if (w->slot[i]) ++nchunks;

    out_put(o, RC_MAGIC, 8);
    /* the key is 20 bytes: padded to 24 */
    out_put(o, img->key, 20);
    uint32_t head[4] = {nchunks, (uint32_t)sw->nents, 0, 0};
    size_t head_at = o->n;
    out_put(o, head, sizeof head);
    out_put(o, &stamp_delta, sizeof stamp_delta);
    out_put(o, img->bigtree_after, sizeof img->bigtree_after);

    /* the tile entities' tags join the journal before it is written */
    for (size_t i = 0; i < w->cap; ++i)
    {
        const struct chunk *c = chunk_ptr(w->slot[i]);
        if (c == NULL) continue;
        for (int t = 0; t < c->tes.n; ++t)
        {
            int ns;
            struct te_stack *s = te_slots(c->tes.v[t], &ns);
            for (int k = 0; k < ns; ++k)
                if (s[k].tag) journal_pos(jr, s[k].tag);
        }
    }
    if (o->p != NULL) memcpy(o->p + head_at + 8, &jr->n, sizeof(uint32_t));
    for (int k = 0; k < jr->n; ++k) out_str(o, itag_text(jr->v[k]));

    /* the entities, their Random's padding cleared (the struct copies that
     * set it carry a stack frame's bytes there) */
    for (int k = 0; k < sw->nents; ++k)
    {
        struct sw_entity e = sw->ents[k];
        det_rng r = e.rand;
        memset(&e.rand, 0, sizeof e.rand);
        e.rand.r = r.r;
        e.rand.have_next_next_gaussian = r.have_next_next_gaussian;
        e.rand.next_next_gaussian = r.next_next_gaussian;
        if (o->p != NULL) memcpy(o->p + o->n, &e, sizeof e);
        o->n += sizeof e;
    }
    o->n = rc_pad(o->n);

    for (size_t i = 0; i < w->cap; ++i)
    {
        const struct chunk *c = chunk_ptr(w->slot[i]);
        if (c == NULL) continue;
        /* the struct, its two pointers cleared in the copy */
        size_t c_at = o->n;
        out_put(o, c, sizeof *c);
        if (o->p != NULL)
        {
            struct chunk *cc = (struct chunk *)(o->p + c_at);
            cc->packed = NULL;
            cc->tes.v = NULL;
        }
        int nticks = ticks_chunk_updates(c->cx, c->cz, NULL, 0);
        uint32_t counts[2] = {(uint32_t)c->tes.n, (uint32_t)nticks};
        out_put(o, counts, sizeof counts);
        if (c->packed_n) out_put(o, c->packed, c->packed_n);
        for (int t = 0; t < c->tes.n; ++t)
        {
            const struct tile_entity *src = c->tes.v[t];
            const char *raw = src->raw, *mob = src->kind == TE_MOB_SPAWNER ? src->u.spawner.mob_id : NULL;
            size_t te_at = o->n;
            out_put(o, src, sizeof *src);
            if (o->p != NULL)
            {
                /* the copy's strings follow it; its tags are journal positions */
                struct tile_entity *te = (struct tile_entity *)(o->p + te_at);
                te->raw = NULL;
                if (te->kind == TE_MOB_SPAWNER) te->u.spawner.mob_id = NULL;
                int ns;
                struct te_stack *s = te_slots(te, &ns);
                for (int k = 0; k < ns; ++k)
                    if (s[k].tag) s[k].tag = journal_pos(jr, s[k].tag) + 1;
            }
            out_str(o, raw);
            out_str(o, mob);
        }
        if (nticks > 0)
        {
            size_t bytes = (size_t)nticks * sizeof(struct tick_entry);
            struct tick_entry *tmp = rc_alloc(bytes);
            ticks_chunk_updates(c->cx, c->cz, tmp, nticks);
            out_put(o, tmp, bytes);
            proc_free(tmp);
        }
    }
}

/* The offsets of the parts; 0 when the bytes do not hold a whole image. */
static int rc_parse(struct rc_image *img)
{
    const uint8_t *b = img->buf;
    size_t at = 8 + rc_pad(20);
    uint32_t head[4];
    if (img->n < at + sizeof head) return 0;
    memcpy(head, b + at, sizeof head);
    at += sizeof head;
    img->nchunks = (int)head[0];
    img->nents = (int)head[1];
    img->njournal = (int)head[2];
    at += 8 + sizeof img->bigtree_after;
    img->tagmap = rc_zalloc(((size_t)img->njournal + 1) * sizeof *img->tagmap);
    /* the journal is read again by regioncache_tags; skip it here */
    for (int k = 0; k < img->njournal; ++k)
    {
        int32_t len;
        if (at + 4 > img->n) return 0;
        memcpy(&len, b + at, 4);
        at = rc_pad(at + 4 + (size_t)(len > 0 ? len : 0));
    }
    img->ents_off = at;
    at += rc_pad((size_t)img->nents * sizeof(struct sw_entity));
    img->chunk_off = rc_zalloc(((size_t)img->nchunks + 1) * sizeof *img->chunk_off);
    for (int i = 0; i < img->nchunks; ++i)
    {
        if (at > img->n || img->n - at < rc_pad(sizeof(struct chunk)) + 8) return 0;
        img->chunk_off[i] = at;
        struct chunk c;
        memcpy(&c, b + at, sizeof c);
        at += rc_pad(sizeof c);
        uint32_t counts[2];
        memcpy(counts, b + at, sizeof counts);
        at += sizeof counts;
        at += rc_pad(c.packed_n);
        if (at > img->n) return 0;
        for (uint32_t t = 0; t < counts[0]; ++t)
        {
            at += rc_pad(sizeof(struct tile_entity));
            if (at > img->n) return 0;
            for (int s = 0; s < 2; ++s)
            {
                int32_t len;
                if (at + 4 > img->n) return 0;
                memcpy(&len, b + at, 4);
                at = rc_pad(at + 4 + (size_t)(len > 0 ? len : 0));
            }
        }
        at += rc_pad((size_t)counts[1] * sizeof(struct tick_entry));
        if (at > img->n) return 0;
    }
    return at == img->n;
}

/* ------------------------------------------------------------ the API */

int regioncache_begin(struct rc_image *img, int64_t seed, int quiet_join)
{
    img->lock_fd = -1;
    img->t0_ns = rc_now_ns();
    img->stamp_before = nw_env->entity.chunk_stamp;
    rc_key(img->key, seed, quiet_join);
    img->mode = rc_mode(img->dir, sizeof img->dir);
    if (img->mode == 1 && rc_load(img))
    {
        if (rc_parse(img))
        {
            img->ns = rc_now_ns() - img->t0_ns;
            return 1;
        }
        munmap(img->buf, img->n);
        img->buf = NULL;
        img->n = 0;
        img->mapped = 0;
        proc_free(img->tagmap);
        proc_free(img->chunk_off);
        img->tagmap = NULL;
        img->chunk_off = NULL;
    }
    if (img->mode != 0)
    {
        char path[1200];
        rc_path(img, ".lock", path, sizeof path);
        img->lock_fd = open(path, O_RDWR | O_CREAT, 0644);
        if (img->lock_fd >= 0)
        {
            futimens(img->lock_fd, NULL);
            flock(img->lock_fd, LOCK_EX);
        }
        /* a concurrent miss of the same key may have stored it meanwhile */
        if (img->mode == 1 && rc_load(img))
        {
            if (rc_parse(img))
            {
                img->ns = rc_now_ns() - img->t0_ns;
                flock(img->lock_fd, LOCK_UN);
                close(img->lock_fd);
                img->lock_fd = -1;
                return 1;
            }
            munmap(img->buf, img->n);
            img->buf = NULL;
            img->n = 0;
            img->mapped = 0;
            proc_free(img->tagmap);
            proc_free(img->chunk_off);
            img->tagmap = NULL;
            img->chunk_off = NULL;
        }
    }
    img->fresh = 1;
    if (img->mode == 2)
    {
        img->env_before = rc_alloc(offsetof(struct env, scratch));
        memcpy(img->env_before, nw_env, offsetof(struct env, scratch));
    }
    nw_env->itemtag.njournal = 0;
    nw_env->itemtag.journal_on = 1;
    return 0;
}

void regioncache_note_bigtree(struct rc_image *img)
{
    memcpy(img->bigtree_after, nw_env->decorator.bigtree_state, sizeof img->bigtree_after);
}

void regioncache_capture(struct rc_image *img, const struct seedworld *sw)
{
    nw_env->itemtag.journal_on = 0;
    struct rc_journal jr = {0};
    for (int k = 0; k < nw_env->itemtag.njournal; ++k) journal_pos(&jr, nw_env->itemtag.journal[k]);
    free(nw_env->itemtag.journal);
    nw_env->itemtag.journal = NULL;
    nw_env->itemtag.njournal = nw_env->itemtag.capjournal = 0;

    /* every chunk parked, as the region store keeps them */
    const struct world *w = &sw->p.world;
    for (size_t i = 0; i < w->cap; ++i)
    {
        struct chunk *c = chunk_ptr(w->slot[i]);
        if (c == NULL) continue;
        chunk_pack(c);
        if (c->packed == NULL)
            for (int s = 0; s < 16; ++s)
                if (c->band[s])
                {
                    fprintf(stderr, "region cache: chunk %d,%d did not pack\n", c->cx, c->cz);
                    abort();
                }
    }

    uint64_t delta = nw_env->entity.chunk_stamp - img->stamp_before;
    struct rc_out o = {0};
    struct rc_journal sizing = {0};
    sizing.v = rc_alloc((size_t)(jr.n + 1) * sizeof *sizing.v);
    memcpy(sizing.v, jr.v, (size_t)jr.n * sizeof *jr.v);
    sizing.n = jr.n;
    sizing.cap = jr.n + 1;
    rc_serialize(&o, img, sw, &sizing, delta);
    proc_free(sizing.v);
    o.cap = o.n;
    o.p = rc_alloc(o.cap);
    o.n = 0;
    rc_serialize(&o, img, sw, &jr, delta);
    proc_free(jr.v);
    img->buf = o.p;
    img->n = o.n;
    img->mapped = 0;
    if (!rc_parse(img))
    {
        fprintf(stderr, "region cache: the image does not parse back\n");
        abort();
    }

    if (img->mode != 0)
    {
        char path[1200], tmp[1300];
        rc_path(img, ".img", path, sizeof path);
        int have = 0;
        if (img->mode == 2)
        {
            struct rc_image *old = rc_zalloc(sizeof *old);
            memcpy(old->key, img->key, sizeof old->key);
            memcpy(old->dir, img->dir, sizeof old->dir);
            if (rc_load(old))
            {
                have = 1;
                if (old->n != img->n || memcmp(old->buf, img->buf, img->n) != 0)
                {
                    size_t at = 0;
                    while (at < old->n && at < img->n && old->buf[at] == img->buf[at]) ++at;
                    fprintf(stderr, "region cache: verify FAILED: %s holds %zu bytes, the build %zu, first difference at byte %zu\n",
                            path, old->n, img->n, at);
                    exit(3);
                }
                munmap(old->buf, old->n);
            }
            proc_free(old);
        }
        if (!have)
        {
            snprintf(tmp, sizeof tmp, "%s.%ld.tmp", path, (long)getpid());
            FILE *f = fopen(tmp, "wb");
            if (f != NULL)
            {
                int ok = fwrite(img->buf, 1, img->n, f) == img->n;
                ok = fclose(f) == 0 && ok;
                if (ok) rename(tmp, path);
                else unlink(tmp);
            }
            rc_evict(img);
        }
    }
    if (img->lock_fd >= 0)
    {
        flock(img->lock_fd, LOCK_UN);
        close(img->lock_fd);
        img->lock_fd = -1;
    }
    img->ns = rc_now_ns() - img->t0_ns;
}

void regioncache_apply_env(const struct rc_image *img)
{
    const uint8_t *b = img->buf;
    size_t at = 8 + rc_pad(20) + 16;
    uint64_t delta;
    memcpy(&delta, b + at, sizeof delta);
    at += 8;
    memcpy(nw_env->decorator.bigtree_state, b + at, sizeof nw_env->decorator.bigtree_state);
    nw_env->entity.chunk_stamp += delta;
}

void regioncache_tags(struct rc_image *img)
{
    const uint8_t *b = img->buf;
    size_t at = 8 + rc_pad(20) + 16 + 8 + sizeof img->bigtree_after;
    for (int k = 0; k < img->njournal; ++k)
    {
        int32_t len;
        memcpy(&len, b + at, 4);
        char *text = rc_alloc((size_t)(len > 0 ? len : 0) + 1);
        if (len > 0) memcpy(text, b + at + 4, (size_t)len);
        text[len > 0 ? len : 0] = 0;
        img->tagmap[k + 1] = len >= 0 ? itag_from_text(text) : 0;
        proc_free(text);
        at = rc_pad(at + 4 + (size_t)(len > 0 ? len : 0));
    }
}

void regioncache_chunk_pos(const struct rc_image *img, int i, int *cx, int *cz)
{
    int32_t v[2];
    memcpy(v, img->buf + img->chunk_off[i] + offsetof(struct chunk, cx), sizeof(int32_t));
    memcpy(v + 1, img->buf + img->chunk_off[i] + offsetof(struct chunk, cz), sizeof(int32_t));
    *cx = v[0];
    *cz = v[1];
}

static char *rc_str(const uint8_t *b, size_t *at)
{
    int32_t len;
    memcpy(&len, b + *at, 4);
    char *s = NULL;
    if (len >= 0)
    {
        s = malloc((size_t)len + 1);
        if (s == NULL) abort();
        memcpy(s, b + *at + 4, (size_t)len);
        s[len] = 0;
    }
    *at = rc_pad(*at + 4 + (size_t)(len > 0 ? len : 0));
    return s;
}

struct chunk *regioncache_chunk(const struct rc_image *img, int i)
{
    const uint8_t *b = img->buf;
    size_t at = img->chunk_off[i];
    struct chunk *c = chunk_new();
    memcpy(c, b + at, sizeof *c);
    at += rc_pad(sizeof *c);
    uint32_t counts[2];
    memcpy(counts, b + at, sizeof counts);
    at += sizeof counts;
    if (c->packed_n)
    {
        c->packed = malloc(c->packed_n);
        if (c->packed == NULL) abort();
        memcpy(c->packed, b + at, c->packed_n);
        at += rc_pad(c->packed_n);
    }
    if (c->tes.cap > 0)
    {
        c->tes.v = calloc((size_t)c->tes.cap, sizeof *c->tes.v);
        if (c->tes.v == NULL) abort();
    }
    for (uint32_t t = 0; t < counts[0]; ++t)
    {
        struct tile_entity *te = calloc(1, sizeof *te);
        if (te == NULL) abort();
        memcpy(te, b + at, sizeof *te);
        at += rc_pad(sizeof *te);
        te->raw = rc_str(b, &at);
        char *mob = rc_str(b, &at);
        if (te->kind == TE_MOB_SPAWNER) te->u.spawner.mob_id = mob;
        else free(mob);
        int ns;
        struct te_stack *s = te_slots(te, &ns);
        for (int k = 0; k < ns; ++k)
            if (s[k].tag) s[k].tag = img->tagmap[s[k].tag];
        c->tes.v[t] = te;
    }
    return c;
}

int regioncache_ticks(const struct rc_image *img, int i, struct tick_entry **out)
{
    const uint8_t *b = img->buf;
    size_t at = img->chunk_off[i];
    struct chunk c;
    memcpy(&c, b + at, sizeof c);
    at += rc_pad(sizeof c);
    uint32_t counts[2];
    memcpy(counts, b + at, sizeof counts);
    at += sizeof counts;
    at += rc_pad(c.packed_n);
    for (uint32_t t = 0; t < counts[0]; ++t)
    {
        at += rc_pad(sizeof(struct tile_entity));
        for (int s = 0; s < 2; ++s)
        {
            int32_t len;
            memcpy(&len, b + at, 4);
            at = rc_pad(at + 4 + (size_t)(len > 0 ? len : 0));
        }
    }
    *out = NULL;
    if (counts[1] == 0) return 0;
    *out = malloc((size_t)counts[1] * sizeof **out);
    if (*out == NULL) abort();
    memcpy(*out, b + at, (size_t)counts[1] * sizeof **out);
    return (int)counts[1];
}

void regioncache_entity(const struct rc_image *img, int k, struct sw_entity *out)
{
    memcpy(out, img->buf + img->ents_off + (size_t)k * sizeof *out, sizeof *out);
}

/* What a build may leave changed in struct env (regioncache_end's audit). */
#define RC_SPAN(m) {offsetof(struct env, m), sizeof(((struct env *)0)->m)}
static const struct { size_t off, n; } RC_LEAVES[] = {
    RC_SPAN(arena),               /* pool counters and free chains: addresses */
    RC_SPAN(bwl),                 /* the block worklist's frames: scratch */
    RC_SPAN(entity.chunk_stamp),  /* carried: the stamp's advance */
    RC_SPAN(populate),            /* sr_saved_context's, hit or miss */
    RC_SPAN(decorator),           /* carried: the big-tree state */
    RC_SPAN(world),               /* the pack buffer */
    RC_SPAN(nbtjson),             /* the NBT parse pools */
    RC_SPAN(nbtbin),              /* the NBT memo: scratch */
    RC_SPAN(itemtag),             /* carried: the journal's tags */
    RC_SPAN(light),               /* the light-cap statistics */
    RC_SPAN(phase),               /* the profiler's and tracer's counters */
    /* the stronghold's biome box: a memo keyed by the box it holds */
    {offsetof(struct env, stronghold.box), offsetof(struct env, stronghold.box_valid) + sizeof(int) - offsetof(struct env, stronghold.box)},
};

static void rc_audit(const struct rc_image *img)
{
    const uint8_t *a = img->env_before, *b = (const uint8_t *)nw_env;
    size_t n = offsetof(struct env, scratch);
    int bad = 0;
    for (size_t i = 0; i < n; ++i)
    {
        if (a[i] == b[i]) continue;
        int left = 0;
        for (size_t k = 0; k < sizeof RC_LEAVES / sizeof RC_LEAVES[0] && !left; ++k)
            left = i >= RC_LEAVES[k].off && i < RC_LEAVES[k].off + RC_LEAVES[k].n;
        if (left) continue;
        size_t j = i;
        while (j + 1 < n && a[j + 1] != b[j + 1]) ++j;
        fprintf(stderr, "region cache: verify FAILED: the build changed struct env bytes %zu..%zu, which the image does not carry\n", i, j);
        bad = 1;
        i = j;
    }
    if (bad) exit(3);
}

void regioncache_end(struct rc_image *img)
{
    if (img->env_before != NULL)
    {
        rc_audit(img);
        proc_free(img->env_before);
        img->env_before = NULL;
    }
    if (img->mode != 0) rc_log(img, img->fresh ? (img->mode == 2 ? "verify" : "miss") : "hit");
    if (img->mapped) munmap(img->buf, img->n);
    else proc_free(img->buf);
    proc_free(img->tagmap);
    proc_free(img->chunk_off);
    img->buf = NULL;
    img->tagmap = NULL;
    img->chunk_off = NULL;
}
