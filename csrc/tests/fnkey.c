/* The test selection's key (make test, make test-map): a suite job is
 * replayed from a record of an earlier passing run when every function the
 * job executed there, every file it read and its recording's files are
 * unchanged. A record (tests/testmap.c writes them from a run on the
 * function coverage build, out/native/fncov) lists the job, the functions
 * each of its programs entered (a bitset over a names table) and the paths
 * it opened, probed or ran; its file name is its key.
 *
 *   fnkey key  [OPTS] REC...      the key of each record on this tree
 *   fnkey check [OPTS] < ORDER    each job of make test's order file with a
 *                                 record whose key this tree reproduces:
 *                                 "N LOGFILE", one line per job (--into DIR:
 *                                 also DIR/N, the log, and N.rc and N.hit)
 *   fnkey names REC...            the functions a record lists
 *   fnkey fn   [OPTS] BIN NAME... each function's hash (a debugging aid)
 *   OPTS: --root ROOT --out OUT --data DATA --store DIR --global STRING
 *
 * A function's hash is its machine code in the normal build's object (the
 * objects the tests link: OUT/libnetherite.a's members, OUT/obj/dev.o,
 * input_guard.o, test_X.o, play.o, play-dev.o), with every relocation in it
 * resolved to what it names: a function by name (its own code counts only
 * if the job entered it, and then it is in the record), a data object by
 * name and content (its bytes and, recursively, what its relocations name),
 * an anonymous constant (a string, a float, a jump table) by the bytes it
 * covers. All copies of a name in the program (static functions of the same
 * name, gcc's .constprop/.isra/.part/.cold clones) hash together. So a
 * header change reaches exactly the functions whose code it changes (a
 * struct's layout is in their offsets), and a constant's change the
 * functions that load it. A data object named *_fnkey_opaque counts by its
 * name only (regioncache.c's hash of every source: a cache key). The global string (compiler, flags) is in every
 * key. ELF64 x86-64 relocatable objects only: anything else has no keys, and
 * every job runs. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __APPLE__
#define ST_MTIM st_mtimespec
#else
#define ST_MTIM st_mtim
#endif

/* ---- SHA-1 ---- */
typedef struct { uint32_t h[5]; uint64_t n; uint8_t b[64]; int k; } sha1;

static uint32_t rol(uint32_t x, int s) { return x << s | x >> (32 - s); }

static void sha1_block(sha1 *c, const uint8_t *p)
{
    uint32_t w[80], a = c->h[0], b = c->h[1], d = c->h[2], e = c->h[3], f = c->h[4];
    for (int i = 0; i < 16; ++i) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    for (int i = 0; i < 80; ++i)
    {
        uint32_t g, k;
        if (i < 20) { g = (b & d) | (~b & e); k = 0x5a827999; }
        else if (i < 40) { g = b ^ d ^ e; k = 0x6ed9eba1; }
        else if (i < 60) { g = (b & d) | (b & e) | (d & e); k = 0x8f1bbcdc; }
        else { g = b ^ d ^ e; k = 0xca62c1d6; }
        uint32_t t = rol(a, 5) + g + f + k + w[i];
        f = e; e = d; d = rol(b, 30); b = a; a = t;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += d; c->h[3] += e; c->h[4] += f;
}

static void sha1_init(sha1 *c)
{
    static const uint32_t h0[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
    memcpy(c->h, h0, sizeof h0);
    c->n = 0;
    c->k = 0;
}

static void sha1_add(sha1 *c, const void *data, size_t len)
{
    const uint8_t *p = data;
    c->n += len;
    while (len)
    {
        if (c->k == 0 && len >= 64) { sha1_block(c, p); p += 64; len -= 64; continue; }
        size_t m = 64 - (size_t)c->k < len ? 64 - (size_t)c->k : len;
        memcpy(c->b + c->k, p, m);
        c->k += (int)m; p += m; len -= m;
        if (c->k == 64) { sha1_block(c, c->b); c->k = 0; }
    }
}

static void sha1_str(sha1 *c, const char *s) { sha1_add(c, s, strlen(s) + 1); }

static void sha1_u64(sha1 *c, uint64_t v) { sha1_add(c, &v, sizeof v); }

static void sha1_end(sha1 *c, uint8_t out[20])
{
    uint64_t bits = c->n * 8;
    uint8_t pad = 0x80, z = 0;
    sha1_add(c, &pad, 1);
    while (c->k != 56) sha1_add(c, &z, 1);
    uint8_t l[8];
    for (int i = 0; i < 8; ++i) l[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha1_add(c, l, 8);
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 4; ++j) out[4 * i + j] = (uint8_t)(c->h[i] >> (24 - 8 * j));
}

static void hex(const uint8_t *h, int n, char *out)
{
    for (int i = 0; i < n; ++i) sprintf(out + 2 * i, "%02x", h[i]);
}

static void *xcalloc(size_t n, size_t s)
{
    void *p = calloc(n ? n : 1, s);
    if (!p) { fprintf(stderr, "fnkey: out of memory\n"); exit(2); }
    return p;
}

/* ---- a string map (open addressing) ---- */
typedef struct { char **key; void **val; size_t cap, n; } smap;

static uint64_t strhash(const char *s)
{
    uint64_t h = 1469598103934665603ull;
    for (; *s; ++s) h = (h ^ (uint8_t)*s) * 1099511628211ull;
    return h;
}

static void **smap_slot(smap *m, const char *k, int add)
{
    if (add && (m->n + 1) * 2 > m->cap)
    {
        smap o = *m;
        m->cap = o.cap ? o.cap * 2 : 1024;
        m->key = xcalloc(m->cap, sizeof *m->key);
        m->val = xcalloc(m->cap, sizeof *m->val);
        m->n = 0;
        for (size_t i = 0; i < o.cap; ++i)
            if (o.key[i])
            {
                size_t j = strhash(o.key[i]) & (m->cap - 1);
                while (m->key[j]) j = (j + 1) & (m->cap - 1);
                m->key[j] = o.key[i];
                m->val[j] = o.val[i];
                m->n++;
            }
        free(o.key);
        free(o.val);
    }
    if (!m->cap) return NULL;
    for (size_t i = strhash(k) & (m->cap - 1);; i = (i + 1) & (m->cap - 1))
    {
        if (!m->key[i])
        {
            if (!add) return NULL;
            m->key[i] = strdup(k);
            m->n++;
            return &m->val[i];
        }
        if (!strcmp(m->key[i], k)) return &m->val[i];
    }
}

/* ---- ELF64 (own definitions: the Mac has no elf.h) ---- */
typedef struct { uint8_t ident[16]; uint16_t type, machine; uint32_t version; uint64_t entry, phoff, shoff; uint32_t flags; uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx; } ehdr;
typedef struct { uint32_t name, type; uint64_t flags, addr, offset, size; uint32_t link, info; uint64_t addralign, entsize; } shdr;
typedef struct { uint32_t name; uint8_t info, other; uint16_t shndx; uint64_t value, size; } esym;
typedef struct { uint64_t offset, info; int64_t addend; } erela;

enum { SHT_NOBITS = 8, SHT_RELA = 4, SHT_SYMTAB = 2, SHF_WRITE = 1, SHF_ALLOC = 2, SHF_EXEC = 4, SHF_MERGE = 0x10, SHF_STRINGS = 0x20,
       STT_OBJECT = 1, STT_FUNC = 2, STT_SECTION = 3, STT_FILE = 4, STT_COMMON = 5, STT_TLS = 6, STT_IFUNC = 10,
       STB_LOCAL = 0, SHN_UNDEF = 0, SHN_LORESERVE = 0xff00, SHN_COMMON = 0xfff2 };

typedef struct { int sym; uint64_t off; int64_t add; uint32_t type; } rel;

typedef struct {
    rel *r; int nr;              /* relocations against this section, by offset */
    uint64_t *bound; int nbound;  /* atom boundaries (anonymous data) */
    int *fn; int nfn;             /* function symbols in this section, by value */
    int *ds; int nds;             /* sized data symbols in this section, by value */
} secinfo;

typedef struct obj {
    char name[256];
    const uint8_t *base; size_t size;
    const shdr *sh; int nsh; const char *shstr;
    const esym *sym; int nsym; const char *str;
    secinfo *si;
    uint8_t (*fh)[20]; uint8_t *fstate;   /* per function symbol: its hash once taken (2) */
    void *dn;                              /* per data symbol: its graph node (dnode) */
    int common;                            /* in every program (the archive, dev.o, input_guard.o) */
} obj;

typedef struct { obj *o; int s; } def;
typedef struct { def *d; int n, cap; } deflist;

static smap gdefs;   /* global/weak defined symbol -> deflist */
static smap fnames;  /* function base name -> deflist (every binding) */
static int bad_elf;

static const char *sname(const obj *o, int s) { return o->str + o->sym[s].name; }
static int stype(const esym *s) { return s->info & 15; }
static int sbind(const esym *s) { return s->info >> 4; }

static void defadd(smap *m, const char *k, obj *o, int s)
{
    void **v = smap_slot(m, k, 1);
    deflist *l = *v;
    if (!l) *v = l = xcalloc(1, sizeof *l);
    if (l->n == l->cap) { l->cap = l->cap ? 2 * l->cap : 2; l->d = realloc(l->d, l->cap * sizeof *l->d); }
    l->d[l->n++] = (def){o, s};
}

static int is_fn(const obj *o, int s)
{
    const esym *y = &o->sym[s];
    int t = stype(y);
    return (t == STT_FUNC || t == STT_IFUNC) && y->shndx != SHN_UNDEF && y->shndx < o->nsh;
}

static int cmp_rel(const void *a, const void *b)
{
    const rel *x = a, *y = b;
    return x->off < y->off ? -1 : x->off > y->off;
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

static const obj *sort_obj;
static int cmp_fnsym(const void *a, const void *b)
{
    uint64_t x = sort_obj->sym[*(const int *)a].value, y = sort_obj->sym[*(const int *)b].value;
    return x < y ? -1 : x > y ? 1 : *(const int *)a - *(const int *)b;
}

static int pcrel(uint32_t t) { return t == 2 || t == 4 || t == 9 || t == 13 || t == 15 || t == 24 || t == 26 || t == 41 || t == 42; }

static obj *load_obj(const char *name, const uint8_t *p, size_t n, int common)
{
    const ehdr *e = (const ehdr *)p;
    if (n < sizeof *e || memcmp(e->ident, "\177ELF", 4) || e->ident[4] != 2 || e->ident[5] != 1 || e->type != 1 || e->machine != 62 ||
        e->shoff + (uint64_t)e->shnum * sizeof(shdr) > n || e->shstrndx >= e->shnum)
    { bad_elf = 1; return NULL; }
    obj *o = xcalloc(1, sizeof *o);
    snprintf(o->name, sizeof o->name, "%s", name);
    o->base = p; o->size = n; o->common = common;
    o->sh = (const shdr *)(p + e->shoff); o->nsh = e->shnum;
    o->shstr = (const char *)p + o->sh[e->shstrndx].offset;
    o->si = xcalloc(o->nsh, sizeof *o->si);
    for (int i = 0; i < o->nsh; ++i)
        if (o->sh[i].type == SHT_SYMTAB)
        {
            o->sym = (const esym *)(p + o->sh[i].offset);
            o->nsym = (int)(o->sh[i].size / sizeof(esym));
            o->str = (const char *)p + o->sh[o->sh[i].link].offset;
        }
    if (!o->sym) { bad_elf = 1; return NULL; }
    o->fh = xcalloc(o->nsym, sizeof *o->fh);
    o->fstate = xcalloc(o->nsym, 1);
    for (int i = 0; i < o->nsh; ++i)
    {
        const shdr *r = &o->sh[i];
        if (r->type != SHT_RELA || r->info >= (uint32_t)o->nsh || !(o->sh[r->info].flags & SHF_ALLOC)) continue;
        secinfo *s = &o->si[r->info];
        int m = (int)(r->size / sizeof(erela));
        const erela *er = (const erela *)(p + r->offset);
        s->r = realloc(s->r, (size_t)(s->nr + m) * sizeof *s->r);
        for (int k = 0; k < m; ++k)
            s->r[s->nr++] = (rel){(int)(er[k].info >> 32), er[k].offset, er[k].addend, (uint32_t)er[k].info};
        qsort(s->r, s->nr, sizeof *s->r, cmp_rel);
    }
    /* atom boundaries of each data section: its ends, its symbols' ends and
     * every place a relocation anywhere in the object points into it */
    uint64_t **bs = xcalloc(o->nsh, sizeof *bs);
    int *nb = xcalloc(o->nsh, sizeof *nb), *cb = xcalloc(o->nsh, sizeof *cb);
#define BADD(sec, v) do { int s_ = (sec); if (nb[s_] == cb[s_]) { cb[s_] = cb[s_] ? 2 * cb[s_] : 8; bs[s_] = realloc(bs[s_], cb[s_] * sizeof **bs); } bs[s_][nb[s_]++] = (v); } while (0)
    for (int i = 0; i < o->nsh; ++i)
        if ((o->sh[i].flags & SHF_ALLOC) && !(o->sh[i].flags & SHF_EXEC)) { BADD(i, 0); BADD(i, o->sh[i].size); }
    for (int k = 0; k < o->nsym; ++k)
    {
        const esym *y = &o->sym[k];
        if (y->shndx == SHN_UNDEF || y->shndx >= o->nsh || stype(y) == STT_SECTION || stype(y) == STT_FILE) continue;
        if (!(o->sh[y->shndx].flags & SHF_ALLOC)) continue;
        if (o->sh[y->shndx].flags & SHF_EXEC)
        {
            if (is_fn(o, k))
            {
                secinfo *s = &o->si[y->shndx];
                s->fn = realloc(s->fn, (size_t)(s->nfn + 1) * sizeof *s->fn);
                s->fn[s->nfn++] = k;
            }
        }
        else
        {
            BADD(y->shndx, y->value);
            BADD(y->shndx, y->value + y->size);
            if (y->size)
            {
                secinfo *s = &o->si[y->shndx];
                s->ds = realloc(s->ds, (size_t)(s->nds + 1) * sizeof *s->ds);
                s->ds[s->nds++] = k;
            }
        }
    }
    for (int i = 0; i < o->nsh; ++i)
    {
        const secinfo *s = &o->si[i];
        for (int k = 0; k < s->nr; ++k)
        {
            const rel *r = &s->r[k];
            if (r->sym <= 0 || r->sym >= o->nsym) continue;
            const esym *y = &o->sym[r->sym];
            if (y->shndx == SHN_UNDEF || y->shndx >= o->nsh || (o->sh[y->shndx].flags & SHF_EXEC)) continue;
            if (stype(y) != STT_SECTION && y->name && stype(y) != 0) continue;
            int64_t t = (int64_t)y->value + r->add + ((o->sh[i].flags & SHF_EXEC) && pcrel(r->type) ? 4 : 0);
            if (t >= 0 && (uint64_t)t <= o->sh[y->shndx].size) BADD(y->shndx, (uint64_t)t);
        }
    }
    for (int i = 0; i < o->nsh; ++i)
    {
        secinfo *s = &o->si[i];
        if (nb[i])
        {
            qsort(bs[i], nb[i], sizeof **bs, cmp_u64);
            int u = 0;
            for (int k = 0; k < nb[i]; ++k)
                if (!u || bs[i][k] != bs[i][u - 1]) bs[i][u++] = bs[i][k];
            s->bound = bs[i]; s->nbound = u;
        }
        sort_obj = o;
        if (s->nfn) qsort(s->fn, s->nfn, sizeof *s->fn, cmp_fnsym);
        if (s->nds) qsort(s->ds, s->nds, sizeof *s->ds, cmp_fnsym);
    }
    free(bs); free(nb); free(cb);
#undef BADD
    for (int k = 1; k < o->nsym; ++k)
    {
        const esym *y = &o->sym[k];
        if (y->shndx == SHN_UNDEF || stype(y) == STT_SECTION || stype(y) == STT_FILE || !y->name) continue;
        const char *nm = sname(o, k);
        if (sbind(y) != STB_LOCAL) defadd(&gdefs, nm, o, k);
        if (is_fn(o, k))
        {
            char b[1024];
            snprintf(b, sizeof b, "%s", nm);
            char *dot = strchr(b, '.');
            if (dot && dot != b) *dot = 0;
            defadd(&fnames, b, o, k);
        }
    }
    return o;
}

static obj *load_file(const char *path, int common)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) || st.st_size == 0) { close(fd); return NULL; }
    void *p = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return NULL;
    const char *b = strrchr(path, '/');
    return load_obj(b ? b + 1 : path, p, (size_t)st.st_size, common);
}

/* every member of an ar archive (GNU names: "name.o/", or "/N" into "//") */
static int load_archive(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    struct stat st;
    if (fstat(fd, &st) || st.st_size < 8) { close(fd); return 0; }
    const uint8_t *p = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (p == MAP_FAILED || memcmp(p, "!<arch>\n", 8)) return 0;
    size_t off = 8, n = (size_t)st.st_size;
    const char *longn = NULL;
    int count = 0;
    while (off + 60 <= n)
    {
        const char *h = (const char *)p + off;
        size_t sz = strtoull(h + 48, NULL, 10);
        char nm[256] = "";
        if (h[0] == '/' && h[1] == '/') longn = h + 60;
        else if (h[0] == '/' && h[1] >= '0' && h[1] <= '9' && longn)
        {
            const char *s = longn + strtoul(h + 1, NULL, 10);
            size_t k = 0;
            while (s[k] && s[k] != '/' && s[k] != '\n' && k < sizeof nm - 1) { nm[k] = s[k]; ++k; }
            nm[k] = 0;
        }
        else if (h[0] != '/')
        {
            size_t k = 0;
            while (k < 16 && h[k] != '/' && h[k] != ' ') { nm[k] = h[k]; ++k; }
            nm[k] = 0;
        }
        if (nm[0] && off + 60 + sz <= n)
        {
            /* members are only 2-aligned: the ELF structures want 8 */
            const uint8_t *m = p + off + 60;
            if ((uintptr_t)m & 7) { uint8_t *c = malloc(sz); if (!c) return 0; memcpy(c, m, sz); m = c; }
            if (load_obj(nm, m, sz, 1)) count++;
        }
        off += 60 + sz + (sz & 1);
    }
    return count;
}

/* ---- hashing ----
 * A named data object's hash covers what it reaches: its bytes, its
 * relocations, and the hashes of the named data they point at. The data
 * graph can have cycles (tables that point at each other), so the hash is
 * taken over its strongly connected components (Tarjan): a component's hash
 * is its members' shallow hashes (names in place of what they point at) and
 * its successor components' hashes, so it does not depend on which function
 * reached it first. */
typedef struct { obj *o; int s; } node;
typedef struct { node *v; int n, cap; } nodes;

static void nodes_add(nodes *l, obj *o, int s)
{
    if (!l) return;
    if (l->n == l->cap) { l->cap = l->cap ? 2 * l->cap : 8; l->v = realloc(l->v, l->cap * sizeof *l->v); }
    l->v[l->n++] = (node){o, s};
}

typedef struct { int idx, low; uint8_t on, state; uint8_t shallow[20], deep[20]; nodes succ; } dnode;

static dnode *dn(obj *o, int s)
{
    if (!o->dn) o->dn = xcalloc(o->nsym, sizeof(dnode));
    return &((dnode *)o->dn)[s];
}

static void data_deep(obj *o, int s, uint8_t out[20]);
static void hash_atoms(obj *o, int sec, uint64_t t0, uint64_t t1, sha1 *c, nodes *succ);
static void hash_relocs(obj *o, int sec, uint64_t a, uint64_t b, sha1 *c, nodes *succ);

/* a name used from FROM: its own object's definition, else one in the
 * objects every program links (strong before weak, then by object name) */
static def *resolve_global(obj *from, const char *nm)
{
    void **v = smap_slot(&gdefs, nm, 0);
    if (!v || !*v) return NULL;
    deflist *l = *v;
    def *best = NULL;
    for (int i = 0; i < l->n; ++i)
    {
        def *d = &l->d[i];
        if (d->o == from) return d;
        if (!d->o->common) continue;
        int wb = best ? sbind(&best->o->sym[best->s]) == 2 : 1, wd = sbind(&d->o->sym[d->s]) == 2;
        if (!best || (wb && !wd) || (wb == wd && strcmp(d->o->name, best->o->name) < 0)) best = d;
    }
    return best;
}

/* a data symbol's name as the hash sees it: gcc numbers a function's static
 * locals through the whole file (keys.5, OX.0), so a new static anywhere
 * above renames them; only its own function can name one, so the local's
 * name without the number, with its content, identifies it */
static const char *canon(const obj *o, int s, char *buf, size_t n)
{
    const char *nm = sname(o, s);
    const char *dot = strrchr(nm, '.');
    if (sbind(&o->sym[s]) != STB_LOCAL || !dot || dot == nm || !dot[1] || strspn(dot + 1, "0123456789") != strlen(dot + 1)) return nm;
    size_t k = (size_t)(dot - nm) < n - 1 ? (size_t)(dot - nm) : n - 1;
    memcpy(buf, nm, k);
    buf[k] = 0;
    return buf;
}

/* a named data object: its name, and with SUCC (shallow) it is noted as a
 * successor, without its deep hash in place */
static void data_ref(obj *o, int s, const char *nm, sha1 *c, nodes *succ)
{
    size_t l = strlen(nm), k = sizeof "_fnkey_opaque" - 1;
    if (l > k && !strcmp(nm + l - k, "_fnkey_opaque"))
    {   /* a value the source marks as no input of the job's (regioncache.c's build hash) */
        sha1_str(c, "opaque");
        sha1_str(c, nm);
        return;
    }
    char cb[1024];
    sha1_str(c, "O");
    sha1_str(c, o->sym[s].name && !strcmp(nm, sname(o, s)) ? canon(o, s, cb, sizeof cb) : nm);
    if (succ) nodes_add(succ, o, s);
    else
    {
        uint8_t h[20];
        data_deep(o, s, h);
        sha1_add(c, h, 20);
    }
}

static void hash_target(obj *o, int fromsec, const rel *r, sha1 *c, nodes *succ)
{
    if (r->sym <= 0 || r->sym >= o->nsym) { sha1_str(c, "none"); sha1_u64(c, (uint64_t)r->add); return; }
    const esym *y = &o->sym[r->sym];
    int t = stype(y);
    if (y->shndx == SHN_UNDEF)
    {
        const char *nm = sname(o, r->sym);
        def *d = resolve_global(o, nm);
        if (d && !is_fn(d->o, d->s)) data_ref(d->o, d->s, nm, c, succ);
        else { sha1_str(c, d ? "F" : "X"); sha1_str(c, nm); }
        sha1_u64(c, (uint64_t)r->add);
        return;
    }
    if (y->shndx == SHN_COMMON) { sha1_str(c, "M"); sha1_str(c, sname(o, r->sym)); sha1_u64(c, y->size); sha1_u64(c, (uint64_t)r->add); return; }
    if (y->shndx >= o->nsh) { sha1_str(c, "A"); sha1_u64(c, y->value); sha1_u64(c, (uint64_t)r->add); return; }
    if (is_fn(o, r->sym)) { sha1_str(c, "F"); sha1_str(c, sname(o, r->sym)); sha1_u64(c, (uint64_t)r->add); return; }
    int sec = y->shndx;
    const shdr *ts = &o->sh[sec];
    int anon = t == STT_SECTION || !y->name || t == 0;
    if (!anon && !(ts->flags & SHF_EXEC))
    {
        data_ref(o, r->sym, sname(o, r->sym), c, succ);
        sha1_u64(c, (uint64_t)r->add);
        return;
    }
    int fromcode = (o->sh[fromsec].flags & SHF_EXEC) != 0;
    int64_t tt = (int64_t)y->value + r->add + (fromcode && pcrel(r->type) ? 4 : 0);
    if (ts->flags & SHF_EXEC)
    {
        /* into code: the nearest function at or before it, and the distance */
        const secinfo *si = &o->si[sec];
        int best = -1;
        for (int lo = 0, hi = si->nfn - 1; lo <= hi;)
        {
            int mid = (lo + hi) / 2;
            if ((int64_t)o->sym[si->fn[mid]].value <= tt) { best = mid; lo = mid + 1; }
            else hi = mid - 1;
        }
        sha1_str(c, "C");
        if (best >= 0) { sha1_str(c, sname(o, si->fn[best])); sha1_u64(c, (uint64_t)(tt - (int64_t)o->sym[si->fn[best]].value)); }
        else { sha1_str(c, o->shstr + ts->name); sha1_u64(c, (uint64_t)tt); }
        return;
    }
    if (!fromcode && pcrel(r->type))
    {
        /* data pointing at data relative to itself: the whole section */
        sha1_str(c, "S");
        sha1_str(c, o->shstr + ts->name);
        hash_atoms(o, sec, 0, ts->size, c, succ);
        sha1_u64(c, (uint64_t)tt);
        return;
    }
    if (tt < 0) tt = 0;
    uint64_t span = fromcode && pcrel(r->type) ? 4 : 0;
    /* the assembler writes a reference to a local object as one to its
     * section: every sized symbol the target may be in stands for it */
    const secinfo *si = &o->si[sec];
    int hit = 0;
    for (int k = 0; k < si->nds; ++k)
    {
        const esym *z = &o->sym[si->ds[k]];
        if (z->value > (uint64_t)tt + span) break;
        if (z->value + z->size <= (uint64_t)tt) continue;
        data_ref(o, si->ds[k], sname(o, si->ds[k]), c, succ);
        sha1_u64(c, (uint64_t)tt - z->value);
        hit = 1;
    }
    if (hit) return;
    sha1_str(c, "D");
    hash_atoms(o, sec, (uint64_t)tt, (uint64_t)tt + span, c, succ);
}

static void hash_relocs(obj *o, int sec, uint64_t a, uint64_t b, sha1 *c, nodes *succ)
{
    const secinfo *si = &o->si[sec];
    int lo = 0, hi = si->nr;
    while (lo < hi) { int mid = (lo + hi) / 2; if (si->r[mid].off < a) lo = mid + 1; else hi = mid; }
    for (int k = lo; k < si->nr && si->r[k].off < b; ++k)
    {
        sha1_u64(c, si->r[k].off - a);
        sha1_u64(c, si->r[k].type);
        hash_target(o, sec, &si->r[k], c, succ);
    }
}

static void hash_bytes(obj *o, int sec, uint64_t a, uint64_t b, sha1 *c)
{
    const shdr *s = &o->sh[sec];
    if (b > s->size) b = s->size;
    if (a >= b) return;
    if (s->type == SHT_NOBITS) { sha1_str(c, "Z"); sha1_u64(c, b - a); return; }
    if (s->offset + b > o->size) { bad_elf = 1; return; }
    sha1_add(c, o->base + s->offset + a, b - a);
}

/* the anonymous data covering [t0, t1] in sec: a string through its NUL,
 * merged constants by entry, else every atom between boundaries it meets
 * (never memoized: the same start always hashes the same) */
static int atom_depth;
static void hash_atoms(obj *o, int sec, uint64_t t0, uint64_t t1, sha1 *c, nodes *succ)
{
    const shdr *s = &o->sh[sec];
    uint64_t a = t0, b;
    if ((s->flags & SHF_MERGE) && (s->flags & SHF_STRINGS) && s->type != SHT_NOBITS)
    {
        const uint8_t *p = o->base + s->offset;
        b = t1;
        while (b < s->size && p[b]) ++b;
        b = b < s->size ? b + 1 : s->size;
    }
    else if (s->flags & SHF_MERGE)
        b = t1 + (s->entsize ? s->entsize : 1);
    else
    {
        const secinfo *si = &o->si[sec];
        a = 0; b = s->size;
        for (int k = 0; k < si->nbound; ++k)
        {
            if (si->bound[k] <= t0) a = si->bound[k];
            if (si->bound[k] > t1) { b = si->bound[k]; break; }
        }
    }
    if (b > s->size) b = s->size;
    sha1_u64(c, b - a);
    hash_bytes(o, sec, a, b, c);
    if (++atom_depth < 64) hash_relocs(o, sec, a, b, c, succ);
    else sha1_str(c, "deep");
    --atom_depth;
}

static void data_shallow(obj *o, int s)
{
    dnode *d = dn(o, s);
    const esym *y = &o->sym[s];
    sha1 c;
    sha1_init(&c);
    sha1_str(&c, o->name);
    char cb[1024];
    sha1_str(&c, canon(o, s, cb, sizeof cb));
    sha1_str(&c, o->shstr + o->sh[y->shndx].name);
    sha1_u64(&c, y->size);
    sha1_u64(&c, o->sh[y->shndx].flags & (SHF_WRITE | SHF_EXEC));
    if (y->size)
    {
        hash_bytes(o, y->shndx, y->value, y->value + y->size, &c);
        hash_relocs(o, y->shndx, y->value, y->value + y->size, &c, &d->succ);
    }
    else hash_atoms(o, y->shndx, y->value, y->value, &c, &d->succ);
    sha1_end(&c, d->shallow);
}

static int tj_idx;
static nodes tj_stack;

static int cmp_node(const void *a, const void *b)
{
    const node *x = a, *y = b;
    char a1[1024], b1[1024];
    int r = strcmp(x->o->name, y->o->name);
    if (!r) r = strcmp(canon(x->o, x->s, a1, sizeof a1), canon(y->o, y->s, b1, sizeof b1));
    return r ? r : strcmp(sname(x->o, x->s), sname(y->o, y->s));
}

static int cmp_h20(const void *a, const void *b) { return memcmp(a, b, 20); }

static void tarjan(obj *o, int s)
{
    dnode *d = dn(o, s);
    d->idx = d->low = ++tj_idx;
    d->on = 1;
    d->state = 1;
    nodes_add(&tj_stack, o, s);
    data_shallow(o, s);
    for (int i = 0; i < d->succ.n; ++i)
    {
        node w = d->succ.v[i];
        dnode *e = dn(w.o, w.s);
        if (!e->state) { tarjan(w.o, w.s); d = dn(o, s); if (e->low < d->low) d->low = e->low; }
        else if (e->on && e->idx < d->low) d->low = e->idx;
    }
    if (d->low != d->idx) return;
    /* the component: every node above s on the stack */
    int k = tj_stack.n;
    while (tj_stack.v[k - 1].o != o || tj_stack.v[k - 1].s != s) --k;
    --k;
    int m = tj_stack.n - k;
    node *comp = xcalloc(m, sizeof *comp);
    memcpy(comp, tj_stack.v + k, m * sizeof *comp);
    tj_stack.n = k;
    qsort(comp, m, sizeof *comp, cmp_node);
    sha1 c;
    sha1_init(&c);
    uint8_t (*out)[20] = NULL;
    int nout = 0, cap = 0;
    for (int i = 0; i < m; ++i)
    {
        dnode *x = dn(comp[i].o, comp[i].s);
        x->on = 0;
        sha1_add(&c, x->shallow, 20);
        for (int j = 0; j < x->succ.n; ++j)
        {
            dnode *e = dn(x->succ.v[j].o, x->succ.v[j].s);
            if (e->state != 2) continue;   /* inside this component */
            if (nout == cap) { cap = cap ? 2 * cap : 16; out = realloc(out, cap * sizeof *out); }
            memcpy(out[nout++], e->deep, 20);
        }
    }
    if (nout) qsort(out, nout, 20, cmp_h20);
    for (int i = 0; i < nout; ++i)
        if (!i || memcmp(out[i], out[i - 1], 20)) sha1_add(&c, out[i], 20);
    uint8_t ch[20];
    sha1_end(&c, ch);
    for (int i = 0; i < m; ++i)
    {
        dnode *x = dn(comp[i].o, comp[i].s);
        sha1_init(&c);
        sha1_add(&c, ch, 20);
        sha1_add(&c, x->shallow, 20);
        sha1_end(&c, x->deep);
        x->state = 2;
    }
    free(out);
    free(comp);
}

static void data_deep(obj *o, int s, uint8_t out[20])
{
    dnode *d = dn(o, s);
    if (d->state != 2)
    {
        if (d->state == 1) { fprintf(stderr, "fnkey: data graph re-entered at %s\n", sname(o, s)); exit(2); }
        tarjan(o, s);
    }
    memcpy(out, dn(o, s)->deep, 20);
}

static void hash_fn_sym(obj *o, int s, uint8_t out[20])
{
    if (o->fstate[s] == 2) { memcpy(out, o->fh[s], 20); return; }
    const esym *y = &o->sym[s];
    sha1 c;
    sha1_init(&c);
    sha1_str(&c, o->shstr + o->sh[y->shndx].name);
    sha1_u64(&c, y->size);
    uint64_t end = y->value + y->size;
    if (!y->size)
    {   /* no size: to the next function of the section */
        const secinfo *si = &o->si[y->shndx];
        end = o->sh[y->shndx].size;
        for (int k = 0; k < si->nfn; ++k)
            if (o->sym[si->fn[k]].value > y->value) { end = o->sym[si->fn[k]].value; break; }
    }
    hash_bytes(o, y->shndx, y->value, end, &c);
    hash_relocs(o, y->shndx, y->value, end, &c, NULL);
    sha1_end(&c, o->fh[s]);
    o->fstate[s] = 2;
    memcpy(out, o->fh[s], 20);
}

/* ---- programs: which objects each one links ---- */
static const char *g_root, *g_out, *g_data, *g_store, *g_global = "";
static smap extras;   /* object path -> obj* (or (void *)1: missing) */

static obj *extra(const char *file)
{
    char path[4096];
    snprintf(path, sizeof path, "%s/obj/%s", g_out, file);
    void **v = smap_slot(&extras, path, 1);
    if (!*v)
    {
        obj *o = load_file(path, !strcmp(file, "dev.o") || !strcmp(file, "input_guard.o"));
        *v = o ? (void *)o : (void *)1;
    }
    return *v == (void *)1 ? NULL : *v;
}

/* the objects of BIN besides the archive: NULL when BIN is unknown */
static int program(const char *bin, obj **own, int *nown)
{
    char f[512];
    *nown = 0;
    if (!strncmp(bin, "test_", 5))
    {
        snprintf(f, sizeof f, "%s.o", bin);
        if (!(own[(*nown)++] = extra(f))) return 0;
        if (!(own[(*nown)++] = extra("dev.o"))) return 0;
        if (!(own[(*nown)++] = extra("input_guard.o"))) return 0;
        return 1;
    }
    if (!strcmp(bin, "play")) return (own[(*nown)++] = extra("play.o")) != NULL;
    if (!strcmp(bin, "play-dev"))
    {
        if (!(own[(*nown)++] = extra("play-dev.o"))) return 0;
        return (own[(*nown)++] = extra("dev.o")) != NULL;
    }
    return 0;
}

static int cmp_def(const void *a, const void *b)
{
    const def *x = a, *y = b;
    char a1[1024], b1[1024];
    int r = strcmp(x->o->name, y->o->name);
    if (!r) r = strcmp(canon(x->o, x->s, a1, sizeof a1), canon(y->o, y->s, b1, sizeof b1));
    return r ? r : strcmp(sname(x->o, x->s), sname(y->o, y->s));
}

/* every definition of NAME's base name in BIN's objects, hashed together;
 * 0 when BIN is not a known program */
static int name_hash(const char *bin, const char *name, uint8_t out[20])
{
    obj *own[4];
    int nown;
    if (!program(bin, own, &nown)) return 0;
    char b[1024];
    snprintf(b, sizeof b, "%s", name);
    char *dot = strchr(b, '.');
    if (dot && dot != b) *dot = 0;
    sha1 c;
    sha1_init(&c);
    sha1_str(&c, b);
    void **v = smap_slot(&fnames, b, 0);
    if (v && *v)
    {
        deflist *l = *v;
        def tmp[64], *ds = l->n <= 64 ? tmp : xcalloc(l->n, sizeof *ds);
        int m = 0;
        for (int i = 0; i < l->n; ++i)
        {
            int in = l->d[i].o->common && l->d[i].o != extra("dev.o") && l->d[i].o != extra("input_guard.o");
            for (int k = 0; k < nown; ++k) in |= l->d[i].o == own[k];
            if (in) ds[m++] = l->d[i];
        }
        qsort(ds, m, sizeof *ds, cmp_def);
        for (int i = 0; i < m; ++i)
        {
            uint8_t h[20];
            hash_fn_sym(ds[i].o, ds[i].s, h);
            sha1_str(&c, ds[i].o->name);
            sha1_str(&c, sname(ds[i].o, ds[i].s));
            sha1_add(&c, h, 20);
        }
        if (ds != tmp) free(ds);
    }
    sha1_end(&c, out);
    return 1;
}

/* ---- files ---- */
typedef struct { char *s; size_t n; } strv;

static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

/* find -L DIR -type f: "relpath size mtime" lines, sorted, into c */
static void walk(const char *dir, const char *rel, char ***v, int *n, int *cap, int depth)
{
    DIR *d = opendir(dir);
    if (!d || depth > 32) { if (d) closedir(d); return; }
    struct dirent *e;
    while ((e = readdir(d)))
    {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char p[4096], r[4096];
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        snprintf(r, sizeof r, "%s%s%s", rel, *rel ? "/" : "", e->d_name);
        struct stat st;
        if (stat(p, &st)) continue;
        if (S_ISDIR(st.st_mode)) { walk(p, r, v, n, cap, depth + 1); continue; }
        if (!S_ISREG(st.st_mode)) continue;
        char line[8192];
        snprintf(line, sizeof line, "%s %lld %lld.%09ld", r, (long long)st.st_size, (long long)st.ST_MTIM.tv_sec, (long)st.ST_MTIM.tv_nsec);
        if (*n == *cap) { *cap = *cap ? 2 * *cap : 64; *v = realloc(*v, *cap * sizeof **v); }
        (*v)[(*n)++] = strdup(line);
    }
    closedir(d);
}

static void dir_key(const char *dir, sha1 *c)
{
    char **v = NULL;
    int n = 0, cap = 0;
    walk(dir, "", &v, &n, &cap, 0);
    qsort(v, n, sizeof *v, cmp_str);
    for (int i = 0; i < n; ++i) { sha1_str(c, v[i]); free(v[i]); }
    free(v);
    sha1_u64(c, (uint64_t)n);
}

/* what a recorded path is now: absent, a directory (with LIST, the names
 * in it: the job read it), a file's content (small files outside out/java)
 * or its size and mtime */
static smap idmemo, listmemo;
static const char *identity(const char *path, int list)
{
    void **m = smap_slot(list ? &listmemo : &idmemo, path, 1);
    if (*m) return *m;
    char full[4096];
    if (!strncmp(path, "@/", 2)) snprintf(full, sizeof full, "%s/%s", g_root, path + 2);
    else snprintf(full, sizeof full, "%s", path);
    struct stat st;
    char out[128];
    sha1 c;
    uint8_t h[20];
    if (stat(full, &st)) snprintf(out, sizeof out, "absent");
    else if (S_ISDIR(st.st_mode) && !list) snprintf(out, sizeof out, "dir");
    else if (S_ISDIR(st.st_mode))
    {
        DIR *d = opendir(full);
        char **v = NULL;
        int n = 0, cap = 0;
        struct dirent *e;
        while (d && (e = readdir(d)))
        {
            if (n == cap) { cap = cap ? 2 * cap : 64; v = realloc(v, cap * sizeof *v); }
            v[n++] = strdup(e->d_name);
        }
        if (d) closedir(d);
        qsort(v, n, sizeof *v, cmp_str);
        sha1_init(&c);
        for (int i = 0; i < n; ++i) { sha1_str(&c, v[i]); free(v[i]); }
        free(v);
        sha1_end(&c, h);
        strcpy(out, "dir:");
        hex(h, 10, out + 4);
    }
    else if (S_ISREG(st.st_mode) && st.st_size < (16 << 20) && !strncmp(path, "@/", 2) && strncmp(path, "@/out/java/", 11))
    {
        int fd = open(full, O_RDONLY);
        sha1_init(&c);
        char buf[65536];
        ssize_t k;
        while (fd >= 0 && (k = read(fd, buf, sizeof buf)) > 0) sha1_add(&c, buf, (size_t)k);
        if (fd >= 0) close(fd);
        sha1_end(&c, h);
        strcpy(out, "sha:");
        hex(h, 10, out + 4);
    }
    else snprintf(out, sizeof out, "st:%lld:%lld.%09ld", (long long)st.st_size, (long long)st.ST_MTIM.tv_sec, (long)st.ST_MTIM.tv_nsec);
    *m = strdup(out);
    return *m;
}

/* ---- records ---- */
typedef struct { char **name; int n; uint8_t (*h)[20]; uint8_t *ok; } names;
static smap namesmemo;

static names *load_names(const char *id)
{
    void **m = smap_slot(&namesmemo, id, 1);
    if (*m) return *m == (void *)1 ? NULL : *m;
    char path[4096];
    snprintf(path, sizeof path, "%s/names/%s.txt", g_store, id);
    FILE *f = fopen(path, "r");
    if (!f) { *m = (void *)1; return NULL; }
    names *t = xcalloc(1, sizeof *t);
    char line[4096];
    int cap = 0;
    while (fgets(line, sizeof line, f))
    {
        line[strcspn(line, "\n")] = 0;
        if (t->n == cap) { cap = cap ? 2 * cap : 4096; t->name = realloc(t->name, cap * sizeof *t->name); }
        t->name[t->n++] = strdup(line);
    }
    fclose(f);
    t->h = xcalloc(t->n, sizeof *t->h);
    t->ok = xcalloc(t->n, 1);   /* 0 not hashed, 1 hashed, 2 unknown program */
    *m = t;
    return t;
}

static int hexval(int ch) { return ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1; }

/* the key of the record in PATH on this tree; 0 when it cannot have one */
static int rec_key(const char *path, char out[41])
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    sha1 c;
    sha1_init(&c);
    sha1_str(&c, "fnkey 1");
    sha1_str(&c, g_global);
    static char *line;
    static size_t cap;
    ssize_t len;
    int ok = 1, seen_job = 0;
    names *t = NULL;
    while (ok && (len = getline(&line, &cap, f)) > 0)
    {
        if (line[len - 1] == '\n') line[--len] = 0;
        if (!strncmp(line, "job ", 4))
        {
            sha1_str(&c, line);
            /* job RUNNER DIR ...: the recording's files */
            char dir[4096];
            if (sscanf(line + 4, "%*s %4095s", dir) == 1 && strcmp(dir, "-"))
            {
                char full[8192];
                snprintf(full, sizeof full, "%s/%s", g_data, dir);
                dir_key(full, &c);
            }
            seen_job = 1;
        }
        else if (!strncmp(line, "names ", 6)) { t = load_names(line + 6); ok = t != NULL; sha1_str(&c, line); }
        else if (!strncmp(line, "bits ", 5))
        {
            if (!t) { ok = 0; break; }
            const char *h = line + 5;
            size_t nh = strlen(h);
            for (size_t i = 0; i < nh && ok; ++i)
            {
                int v = hexval(h[i]);
                if (v < 0) { ok = 0; break; }
                for (int b = 0; b < 4; ++b)
                {
                    if (!(v >> (3 - b) & 1)) continue;
                    size_t idx = i * 4 + b;
                    if (idx >= (size_t)t->n) { ok = 0; break; }
                    if (!t->ok[idx])
                    {
                        char bin[512];
                        const char *sp = strchr(t->name[idx], ' ');
                        if (!sp || sp - t->name[idx] >= (long)sizeof bin) t->ok[idx] = 2;
                        else
                        {
                            memcpy(bin, t->name[idx], sp - t->name[idx]);
                            bin[sp - t->name[idx]] = 0;
                            t->ok[idx] = name_hash(bin, sp + 1, t->h[idx]) ? 1 : 2;
                        }
                    }
                    if (t->ok[idx] != 1) { ok = 0; break; }
                    sha1_str(&c, t->name[idx]);
                    sha1_add(&c, t->h[idx], 20);
                }
            }
        }
        else if (!strncmp(line, "path ", 5)) { sha1_str(&c, line); sha1_str(&c, identity(line + 5, 0)); }
        else if (!strncmp(line, "list ", 5)) { sha1_str(&c, line); sha1_str(&c, identity(line + 5, 1)); }
        else if (line[0] && line[0] != '#' && strncmp(line, "fnkey ", 6)) sha1_str(&c, line);
    }
    fclose(f);
    if (!ok || !seen_job || !t || bad_elf) return 0;
    uint8_t h[20];
    sha1_end(&c, h);
    hex(h, 20, out);
    return 1;
}

static void job_id(const char *line, char out[17])
{
    sha1 c;
    uint8_t h[20];
    char tmp[41];
    sha1_init(&c);
    sha1_str(&c, line);
    sha1_end(&c, h);
    hex(h, 8, tmp);
    memcpy(out, tmp, 16);
    out[16] = 0;
}

/* "RUNNER DIR G FLAGS...": the runner's basename, DIR under DATA */
static int job_line(char *const *f, int nf, char *out, size_t cap)
{
    if (nf < 3) return 0;
    const char *b = strrchr(f[0], '/');
    b = b ? b + 1 : f[0];
    char dir[4096];
    size_t dl = strlen(g_data);
    if (!strcmp(f[1], "-")) snprintf(dir, sizeof dir, "-");
    else if (!strncmp(f[1], g_data, dl) && f[1][dl] == '/') snprintf(dir, sizeof dir, "%s", f[1] + dl + 1);
    else return 0;
    size_t k = (size_t)snprintf(out, cap, "%s %s", b, dir);
    for (int i = 2; i < nf && k < cap; ++i) k += (size_t)snprintf(out + k, cap - k, " %s", f[i]);
    return k < cap;
}

static int load_program_objects(void)
{
    char path[4096];
    snprintf(path, sizeof path, "%s/libnetherite.a", g_out);
    if (!load_archive(path) || bad_elf) return 0;
    /* the objects besides the archive that every test links, before any
     * hash: a name the archive leaves undefined may be theirs */
    extra("dev.o");
    extra("input_guard.o");
    return !bad_elf;
}

/* --into DIR: make test's files for job N, the log as N, "0" as N.rc and
 * an empty N.hit */
static const char *g_into;
static int copy_hit(const char *log, const char *n)
{
    char p[8192];
    int in = open(log, O_RDONLY);
    if (in < 0) return 0;
    snprintf(p, sizeof p, "%s/%s", g_into, n);
    int out = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    char buf[65536];
    ssize_t k;
    int ok = out >= 0;
    while (ok && (k = read(in, buf, sizeof buf)) > 0) ok = write(out, buf, (size_t)k) == k;
    close(in);
    if (out >= 0) close(out);
    if (!ok) { unlink(p); return 0; }
    snprintf(p, sizeof p, "%s/%s.rc", g_into, n);
    FILE *f = fopen(p, "w");
    if (!f) return 0;
    fputs("0\n", f);
    fclose(f);
    snprintf(p, sizeof p, "%s/%s.hit", g_into, n);
    f = fopen(p, "w");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static int usage(void)
{
    fprintf(stderr, "usage: fnkey key|check|fn|names|jobid --root R --out O --data D --store S --global G [ARGS]\n");
    return 2;
}

int main(int argc, char **argv)
{
    if (argc < 2) return usage();
    const char *mode = argv[1];
    int i = 2;
    for (; i + 1 < argc && !strncmp(argv[i], "--", 2); i += 2)
    {
        if (!strcmp(argv[i], "--root")) g_root = argv[i + 1];
        else if (!strcmp(argv[i], "--out")) g_out = argv[i + 1];
        else if (!strcmp(argv[i], "--data")) g_data = argv[i + 1];
        else if (!strcmp(argv[i], "--store")) g_store = argv[i + 1];
        else if (!strcmp(argv[i], "--global")) g_global = argv[i + 1];
        else if (!strcmp(argv[i], "--into")) g_into = argv[i + 1];
        else return usage();
    }
    if (!g_root || !g_out || !g_data || !g_store) return usage();
    if (!strcmp(mode, "jobid"))
    {   /* the record directory of the job given as RUNNER DIR G FLAGS... */
        char line[16384], id[17];
        if (!job_line(argv + i, argc - i, line, sizeof line)) return 1;
        job_id(line, id);
        printf("%s %s\n", id, line);
        return 0;
    }
    if (!strcmp(mode, "names"))
    {   /* the functions a record lists, PROGRAM NAME per line */
        for (; i < argc; ++i)
        {
            FILE *f = fopen(argv[i], "r");
            char line[1 << 16];
            names *t = NULL;
            while (f && fgets(line, sizeof line, f))
            {
                line[strcspn(line, "\n")] = 0;
                if (!strncmp(line, "names ", 6)) t = load_names(line + 6);
                if (strncmp(line, "bits ", 5) || !t) continue;
                for (size_t k = 5; line[k]; ++k)
                    for (int b = 0; b < 4; ++b)
                        if (hexval(line[k]) >> (3 - b) & 1 && (k - 5) * 4 + b < (size_t)t->n) printf("%s\n", t->name[(k - 5) * 4 + b]);
            }
            if (f) fclose(f);
        }
        return 0;
    }
    if (!load_program_objects()) { fprintf(stderr, "fnkey: %s/libnetherite.a is not an ELF64 x86-64 archive: no keys\n", g_out); return strcmp(mode, "check") ? 1 : 0; }
    if (!strcmp(mode, "fn"))
    {
        if (i >= argc) return usage();
        for (int k = i + 1; k < argc; ++k)
        {
            uint8_t h[20];
            char x[41];
            if (!name_hash(argv[i], argv[k], h)) { printf("%s ? unknown program\n", argv[k]); continue; }
            hex(h, 20, x);
            printf("%s %s\n", argv[k], x);
        }
        return 0;
    }
    if (!strcmp(mode, "key"))
    {
        int rc = 0;
        for (; i < argc; ++i)
        {
            char k[41];
            if (rec_key(argv[i], k)) printf("%s %s\n", argv[i], k);
            else { printf("%s -\n", argv[i]); rc = 1; }
        }
        return rc;
    }
    if (!strcmp(mode, "check"))
    {
        /* stdin: make test's order lines, N RUNNER DIR G [FLAGS...] */
        char *line = NULL;
        size_t cap = 0;
        ssize_t len;
        while ((len = getline(&line, &cap, stdin)) > 0)
        {
            if (line[len - 1] == '\n') line[--len] = 0;
            char *f[64];
            int nf = 0;
            for (char *s = strtok(line, " "); s && nf < 64; s = strtok(NULL, " ")) f[nf++] = s;
            char jl[16384], id[17];
            if (nf < 4 || !job_line(f + 1, nf - 1, jl, sizeof jl)) continue;
            job_id(jl, id);
            char dir[4096];
            snprintf(dir, sizeof dir, "%s/%s", g_store, id);
            DIR *d = opendir(dir);
            if (!d) continue;
            struct dirent *e;
            char hit[8192] = "";
            while (!hit[0] && (e = readdir(d)))
            {
                size_t n = strlen(e->d_name);
                if (n != 44 || strcmp(e->d_name + 40, ".rec")) continue;
                char rp[8192], k[41];
                snprintf(rp, sizeof rp, "%s/%s", dir, e->d_name);
                if (!rec_key(rp, k) || memcmp(k, e->d_name, 40)) continue;
                snprintf(rp, sizeof rp, "%s/%.40s.log", dir, e->d_name);
                if (!access(rp, R_OK)) snprintf(hit, sizeof hit, "%s", rp);
            }
            closedir(d);
            if (!hit[0]) continue;
            if (g_into && !copy_hit(hit, f[0])) continue;
            printf("%s %s\n", f[0], hit);
        }
        return 0;
    }
    return usage();
}
