/* The device mesher (cuda/meshing, kernels.h): render_blocks*.c compiled for
 * the device as they are (clang, nvptx64; cuda/meshing/meshing.mk), plus what
 * they reach there that a host gets from its C library, and the kernels.
 * One thread meshes one section pass exactly as rb_mesh_section does on
 * the host: the same C, the same operation order, no FMA contraction
 * (llc -nvptx-fma-level=0, ptxas --fmad=false), IEEE division and square
 * root, the mesher's own sine table; libm's cos, sin and atan2, whose
 * roundings are glibc's, come from the host's values (rb_trig). */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../../engine/render_blocks.h"
#include "../../engine/render_blocks_int.h"
#include "../../engine/blocks.h"
#include "../../engine/world.h"
#include "kernels.h"
#include "table.h"

#define TID() ((int)(__nvvm_read_ptx_sreg_ctaid_x() * __nvvm_read_ptx_sreg_ntid_x() + __nvvm_read_ptx_sreg_tid_x()))

/* ------------------------------------------------------------ the C library */

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) ++a, ++b;
    return (unsigned char)*a - (unsigned char)*b;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n]) ++n;
    return n;
}

char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++)) ;
    return r;
}

/* the one format the mesh path prints (door_upper_uv's copy of a name) */
int snprintf(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    size_t k = 0;
    for (const char *f = fmt; *f; ++f)
    {
        const char *s = NULL;
        char one[2] = {*f, 0};
        if (f[0] == '%' && f[1] == 's') { s = va_arg(ap, const char *); ++f; }
        else if (f[0] == '%') __builtin_trap();
        else s = one;
        for (; *s; ++s, ++k) if (k + 1 < n) buf[k] = *s;
    }
    if (n) buf[k < n ? k : n - 1] = 0;
    va_end(ap);
    return (int)k;
}

/* the error paths (an icon atlas.json lacks: the host stops there too) */
FILE *stderr;
int fprintf(FILE *f, const char *fmt, ...)
{
    (void)f; (void)fmt;
    return 0;
}
void exit(int rc)
{
    (void)rc;
    __builtin_trap();
}

/* render_blocks.c's block class table: built by mesh_setup's one thread
 * before any mesher runs (the once flag is then set for every thread; a
 * later kernel sees it, so a plain load, cached, not a volatile one to L2
 * on every block_class_of) */
int pthread_once(int *once, void (*fn)(void))
{
    if (*once == 0)
    {
        fn();
        *once = 1;
    }
    return 0;
}

/* the live world's tile entities: a device world has none (its flower pots
 * come as the request's list, rb_world.te) */
struct tile_entity *world_tile_entity(struct world *w, int x, int y, int z)
{
    (void)w; (void)x; (void)y; (void)z;
    return NULL;
}

/* ------------------------------------------------------------ libm, looked up */

static int trig_find(const struct rb_trig *t, int n, const struct rb_trig *key, double *v)
{
    int lo = 0, hi = n - 1;
    while (lo <= hi)
    {
        int mid = (lo + hi) / 2;
        int c = rb_trig_cmp(&t[mid], key);
        if (c == 0) { *v = t[mid].v; return 1; }
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}

static double trig(const struct rb_mesher *m, int fn, double a, double b)
{
    struct meshing_ctx *c = (struct meshing_ctx *)m->dev;
    double v = 0.0;
    struct rb_trig key = {.fn = fn, .a = a, .b = b};
    if (trig_find(c->t1, c->n1, &key, &v) || trig_find(c->t2, c->n2, &key, &v)) return v;
    c->err |= MESHING_ERR_TRIG;
    return 0.0;
}

double rb_cos(const struct rb_mesher *m, double x) { return trig(m, RB_TRIG_COS, x, 0.0); }
double rb_sin(const struct rb_mesher *m, double x) { return trig(m, RB_TRIG_SIN, x, 0.0); }
double rb_atan2(const struct rb_mesher *m, double y, double x) { return trig(m, RB_TRIG_ATAN2, y, x); }

/* ------------------------------------------------------------ copies */

/* N bytes (a multiple of 8, both 8-aligned) copied, or zeroed, 8 at a
 * time: llc lowers a struct copy or a memset over 128 bytes into a loop of
 * byte loads and stores (1464 of each for the mesher; kernels.c is built
 * -fno-builtin so these loops stay loops) */
static void copy8(void *restrict d, const void *restrict s, size_t n)
{
    /* eight loads, then eight stores: the loads wait on memory together */
    size_t i = 0;
    for (; i + 64 <= n; i += 64)
    {
        uint64_t v[8];
        for (int j = 0; j < 8; ++j) __builtin_memcpy(&v[j], __builtin_assume_aligned((const char *)s + i + 8 * j, 8), 8);
        for (int j = 0; j < 8; ++j) __builtin_memcpy(__builtin_assume_aligned((char *)d + i + 8 * j, 8), &v[j], 8);
    }
    for (; i < n; i += 8)
    {
        uint64_t v;
        __builtin_memcpy(&v, __builtin_assume_aligned((const char *)s + i, 8), 8);
        __builtin_memcpy(__builtin_assume_aligned((char *)d + i, 8), &v, 8);
    }
}

/* the same 16 bytes at a time (N a multiple of 16, both 16-aligned) */
static void copy16(void *restrict d, const void *restrict s, size_t n)
{
    size_t i = 0;
    for (; i + 64 <= n; i += 64)
    {
        uint32_t v[4][4];
        for (int j = 0; j < 4; ++j) __builtin_memcpy(v[j], __builtin_assume_aligned((const char *)s + i + 16 * j, 16), 16);
        for (int j = 0; j < 4; ++j) __builtin_memcpy(__builtin_assume_aligned((char *)d + i + 16 * j, 16), v[j], 16);
    }
    for (; i < n; i += 16)
    {
        uint32_t v[4];
        __builtin_memcpy(v, __builtin_assume_aligned((const char *)s + i, 16), 16);
        __builtin_memcpy(__builtin_assume_aligned((char *)d + i, 16), v, 16);
    }
}

static void zero8(void *d, size_t n)
{
    const uint64_t z = 0;
    for (size_t i = 0; i < n; i += 8) __builtin_memcpy(__builtin_assume_aligned((char *)d + i, 8), &z, 8);
}
_Static_assert(sizeof(struct rb_mesher) % 8 == 0 && sizeof(struct rb_tess) % 8 == 0, "the mesher and Tessellator copied 8 bytes at a time");

/* request Q's band table (rb_world bands): entry i of 27 */
static void band_put(struct rb_band *t, const struct meshing_req *q, int i)
{
    const struct chunk *k = (const struct chunk *)q->chunk[i / 3];
    int s = q->s - 1 + i % 3;
    struct rb_band b = {NULL, 0, 0};
    if (k && s >= 0 && s < 16)
    {
        b.kind = k->mask >> s & 1 ? 1 : 2;
        b.band = chunk_sec_at(k, s);
    }
    else if (k) b.kind = 2;
    t[i] = b;
}

/* ------------------------------------------------------------ the kernels */

/* one thread: the block class table (pthread_once's first caller) */
__attribute__((nvptx_kernel)) void mesh_setup(const struct meshing_launch *L)
{
    if (TID() != 0) return;
    struct rb_mesher *m = (struct rb_mesher *)L->scratch;
    copy8(m, (const void *)L->mesher, sizeof *m);
    struct rb_world w = {0};
    struct rb_tess t;
    zero8(&t, sizeof t);
    struct meshing_ctx c = {0};
    m->w = &w;
    m->t = &t;
    m->dev = &c;
    w.rows = 3;
    w.margin = 1;
    w.chunks = (struct chunk **)L->res;      /* nine zeros */
    rb_mesh_section(m, 0, 0, 0, 0);
}

/* One block of MESHING_WARPS warps a request, its cells shared out among
 * the block's NL lanes.
 *
 * rb_mesh_section is begin, then kind and cell for every cell in y, z, x
 * order, and a cell's draw reads what the draws before it left in the
 * mesher and its Tessellator (bounds, the AO scratch, the last colour), so
 * the cells are not independent. The lanes classify the 4096 cells 32 at a
 * time (rb_mesh_cell_kind: air, the other pass and the hidden cubes are
 * most of a section) into two masks and cut the cells to visit into NL
 * runs of about equal work. Each lane draws its run from a guess of the
 * state it starts in: the state the WARM cells before its run leave when
 * drawn from the section's start (a drawn cell only begun: guess_as). Every
 * guess is then held against the end
 * state of the lane before, and a lane whose guess was wrong draws its run
 * again from that end state, until each lane's start is its predecessor's
 * end: each run is then drawn from the state rb_mesh_section draws it in.
 *
 * The state compared is every word of the mesher but its three pointers
 * (world, Tessellator, context) and the Tessellator's translation and
 * normal. The Tessellator's texture coordinates, colour, brightness and
 * has_* flags are carried apart: only a vertex written to a buffer reads
 * them, and each is replaced whole by a setter (a flag only ever goes to 1
 * after the section's start), so a count does not depend on them. Each run
 * starts them at a mark no setter writes; the count pass sums the lanes'
 * counts. The write pass gives each lane the values of the nearest lane
 * before it whose run set each one (the section's start where none did; a
 * flag, whether any did), draws each run at its offset, and holds each
 * lane's values against its predecessor's end again, drawing again until
 * they agree: a value a setter did write equal to the mark costs a draw,
 * never a wrong vertex.
 *
 * A cell's kind reads the mesher's state only through render_all_faces,
 * which is 0 outside a block's draw: a lane classifies a hidden cube again
 * when it is not. A lane reads the lane before's words by a shuffle, and
 * the first lane of a warp the last lane of the warp before's through
 * shared memory. The block's world and masks live in shared memory, each
 * lane's mesher and Tessellator in its own stack. The words are read and
 * written through memcpy (a uint32_t pointer into a struct of doubles
 * breaks C's aliasing rules, and the optimizer then keeps stale fields);
 * the functions that shuffle are convergent (meshing.mk). */
enum { WARPS = MESHING_WARPS, NL = 32 * MESHING_WARPS, WARM = 1, W_TWO = 16 /* a draw's weight against a hidden cube's */ };

/* the Tessellator's state a draw reads and leaves, compared */
struct tstate {
    double xoff, yoff, zoff;
    int32_t normal, pad;
};
/* and what only a written vertex reads */
struct tcarry {
    double u, v;
    int32_t color, brightness;
    int32_t has_texture, has_color, has_brightness, has_normals;
};
enum { MW = (int)(sizeof(struct rb_mesher) / 4), TW = (int)(sizeof(struct tstate) / 4), CW = (int)(sizeof(struct tcarry) / 4) };
/* the mesher's words a draw writes: min_x up to mat_snow (bounds, flags,
 * face scratch) and override; SW of them. The rest are set at init and
 * checked once a request against the template (MESHING_ERR_STATE). */
enum { REG0 = (int)(offsetof(struct rb_mesher, min_x) / 4), REG1 = (int)(offsetof(struct rb_mesher, mat_snow) / 4),
       OVR = (int)(offsetof(struct rb_mesher, override) / 4), SW = REG1 - REG0 + 2 };
typedef char rb_mesher_override_word[offsetof(struct rb_mesher, override) % 4 == 0 && sizeof(void *) == 8 ? 1 : -1];

/* the mesher word of state word K */
static int sw_at(int k)
{
    return k < REG1 - REG0 ? REG0 + k : OVR + (k - (REG1 - REG0));
}
/* the block's shared part */
struct meshing_blk {
    struct rb_world w;
    struct chunk *near[9];
    struct rb_band bands[27];           /* the request's band table (rb_world bands) */
    uint32_t any[128], two[128];        /* bit c % 32 of word c / 32: cell c's kind is not 0; is 2 */
    uint32_t l0[128];                   /* a hidden cube that sets the brightness (draw_cells_as) */
    int a_of[NL];                       /* each lane's first cell */
    uint32_t bnd[WARPS][SW + TW];       /* each warp's last lane's mesher and Tessellator state */
    uint32_t cbnd[WARPS][CW];           /* and its carried values */
    uint32_t etab[NL][CW];              /* every lane's carried values at its run's end */
    uint32_t setm[8][WARPS];            /* the lanes whose run set each carried field */
    int wsum[WARPS];
    unsigned long long base;
    int slot;                           /* the pool slot the block holds, -1 none */
    int err, flags, reruns;
    int gen;                            /* a cell to draw is not a standard block (mesh_run's, not mesh_run_cube's) */
};
static __attribute__((address_space(3))) struct meshing_blk meshing_sh;

/* the carried values' fields as words [from, to), and which are flags */
static const int CF[8][3] = {{0, 2, 0}, {2, 4, 0}, {4, 5, 0}, {5, 6, 0}, {6, 7, 1}, {7, 8, 1}, {8, 9, 1}, {9, 10, 1}};
/* the mark: a NaN no arithmetic makes, a colour without alpha (every
 * setter's alpha is 255), a negative brightness; no flag set */
static const struct tcarry MARK = {__builtin_nan("0x4dead0000beef"), __builtin_nan("0x4dead0000beef"), 0x00dead01, (int32_t)0x80000000,
                                   0, 0, 0, 0};
/* the same with every flag a setter sets on: a run drawn into the lane's own
 * buffer writes every field, the marked ones where its own setter has not
 * yet run (has_normals has no setter) */
static const struct tcarry PMARK = {__builtin_nan("0x4dead0000beef"), __builtin_nan("0x4dead0000beef"), 0x00dead01, (int32_t)0x80000000,
                                    1, 1, 1, 0};
/* the vertices a lane's own buffer holds (a run with more goes the count
 * and draw way) */
enum { PK = MESHING_PK };

static uint32_t fbits32(float f)
{
    uint32_t w;
    __builtin_memcpy(&w, &f, 4);
    return w;
}

/* word K of a struct (4-aligned: one load, not four byte loads) */
static uint32_t wget(const void *p, int k)
{
    uint32_t w;
    __builtin_memcpy(&w, __builtin_assume_aligned((const char *)p + 4 * k, 4), 4);
    return w;
}

static void wput(void *p, int k, uint32_t w)
{
    __builtin_memcpy(__builtin_assume_aligned((char *)p + 4 * k, 4), &w, 4);
}


static void tstate_get(struct tstate *s, const struct rb_tess *t)
{
    *s = (struct tstate){t->xoff, t->yoff, t->zoff, t->normal, 0};
}

static void tstate_put(struct rb_tess *t, const struct tstate *s)
{
    t->xoff = s->xoff;
    t->yoff = s->yoff;
    t->zoff = s->zoff;
    t->normal = s->normal;
}

static void tcarry_get(struct tcarry *c, const struct rb_tess *t)
{
    *c = (struct tcarry){t->u, t->v, t->color, t->brightness, t->has_texture, t->has_color, t->has_brightness, t->has_normals};
}

static void tcarry_put(struct rb_tess *t, const struct tcarry *c)
{
    t->u = c->u;
    t->v = c->v;
    t->color = c->color;
    t->brightness = c->brightness;
    t->has_texture = c->has_texture;
    t->has_color = c->has_color;
    t->has_brightness = c->has_brightness;
    t->has_normals = c->has_normals;
}

/* the mesher's words neither a draw nor the request sets: the template's
 * for every lane (not its pointers, the camera block, the state words) */
static int fixed_word(int k)
{
#define IN(f, n) (k * 4 >= (int)offsetof(struct rb_mesher, f) && k * 4 < (int)offsetof(struct rb_mesher, f) + (n))
    return !(IN(w, 8) || IN(t, 8) || IN(dev, 8) || IN(px, 12) || (k >= REG0 && k < REG1) || k == OVR || k == OVR + 1);
#undef IN
}

/* a word fixed_word names that differs from the template's (both
 * 16-byte aligned: 16 bytes a load, the other words masked out) */
static int state_changed(const void *m, const void *tp)
{
    _Static_assert(MW % 4 == 0, "the mesher in 16-byte parts");
    uint32_t d = 0;
#pragma unroll
    for (int i = 0; i < MW / 4; ++i)
    {
        uint32_t a[4], b[4];
        __builtin_memcpy(a, __builtin_assume_aligned((const char *)m + 16 * i, 16), 16);
        __builtin_memcpy(b, __builtin_assume_aligned((const char *)tp + 16 * i, 16), 16);
#pragma unroll
        for (int j = 0; j < 4; ++j)
            if (fixed_word(4 * i + j)) d |= a[j] ^ b[j];
    }
    return d != 0;
}

/* the mesher's special icons (read-only tables every lane's mesher points
 * to) against the copy made with them: a draw that wrote one. Every
 * thread G of a block of NT compares a share of the words (a barrier: the
 * whole block calls it) */
static int specials_changed_block(const struct meshing_launch *L, int g, int nt)
{
    int d = 0;
    if (L->spec0)
    {
        const struct rb_mesher *tp = (const struct rb_mesher *)L->mesher;
        int n = (int)((RB_SPECIALS + RB2_SPECIALS + RB3_SPECIALS) * sizeof(struct rb_uv) / 4);
        for (int k = g; k < n; k += nt) d |= wget(tp->special, k) != wget((const void *)L->spec0, k);
    }
    return __nvvm_bar0_or(d);
}

/* a lane's word from the lane before */
static uint32_t up1(uint32_t v)
{
    return (uint32_t)__nvvm_shfl_sync_up_i32(0xffffffffu, (int)v, 1, 0);
}

/* the cells to visit in [a, b), in rb_mesh_section's order; CUBE: every
 * cell drawn is a hidden cube or a standard block (rb_mesh_cell_standard:
 * the cube kernel's copy reaches no other render type).
 *
 * A hidden cube's draw (hidden_cube_state) sets words the next one sets
 * again, whatever they held, but for the brightness, which it sets (to one
 * constant) only when its block has no light: of a stretch of hidden cubes
 * with nothing drawn between, only the last and the last that sets the
 * brightness (L0, mesh_class's mask) change what the stretch leaves, so
 * only those two are drawn, in order, before the cell after the stretch
 * (or at the range's end). The cells to draw come from a queue of at most
 * three the scan fills between calls.
 *
 * One loop, a call an iteration, and no branch around a call (lane/
 * meshcrash): ptxas gives the render calls' tree every convergence barrier
 * it has (B0..B9, B11 at the calls), so a branch around a call here got a
 * barrier spilled to a register (BMOV.32.CLEAR) and restored after the
 * call, and the lanes that came back first (a hidden cube returns sooner
 * than a drawn block) restored it while the others still used it inside:
 * the warp left the draw split, and the code after it, compiled for a
 * whole warp, broke (the mesher's address in a uniform register,
 * overwritten by lane_end's state_changed in the lanes done first: an
 * illegal address at 0x...00000040 at ptxas -O1,
 * renderprec-anvil-cumesh). The general kernels' render_block tree still
 * spills, so every draw is followed by a block barrier (draw_lane,
 * run_block). render_all_faces, rb_mesh_cell_kind's other read, is 0
 * between cells (render_blocks.h): MESHING_ERR_STATE if not. */
static inline __attribute__((always_inline)) void draw_cells_as(struct rb_mesher *m, const uint32_t *any, const uint32_t *two,
                                                                const uint32_t *l0, const struct meshing_req *q, int a, int b, int cube)
{
    int bx = q->cx << 4, bz = q->cz << 4, y0 = q->s << 4;
    int wd = a >> 5, hc = -1, hl = -1, done = a >= b;
    uint32_t bits = a < b ? any[wd] & ~0u << (a & 31) : 0;
    int e0 = -1, e1 = -1, e2 = -1;          /* the queue, e0 first */
    for (;;)
    {
        /* the scan: the next drawn cell, the hidden stretch before it */
        while (e0 < 0 && !done)
        {
            while (!bits && ++wd << 5 < b) bits = any[wd];
            int c = bits ? wd << 5 | __builtin_ctz(bits) : b;
            if (c >= b)
            {
                done = 1;
                c = -1;
            }
            else bits &= bits - 1;
            if (c >= 0 && !(two[c >> 5] >> (c & 31) & 1))
            {
                hc = c;
                if (l0[c >> 5] >> (c & 31) & 1) hl = c;
                continue;
            }
            /* hl (when not hc), hc, then c, each when there */
            int q3[3] = {hl >= 0 && hl != hc ? hl : -1, hc, c}, n = 0;
            for (int k = 0; k < 3; ++k)
                if (q3[k] >= 0)
                {
                    if (n == 0) e0 = q3[k];
                    else if (n == 1) e1 = q3[k];
                    else e2 = q3[k];
                    ++n;
                }
            hc = hl = -1;
        }
        if (e0 < 0) break;
        int c = e0;
        e0 = e1;
        e1 = e2;
        e2 = -1;
        if (m->render_all_faces) ((struct meshing_ctx *)m->dev)->err |= MESHING_ERR_STATE;
        int x = bx + (c & 15), z = bz + ((c >> 4) & 15), y = y0 + (c >> 8), k = two[c >> 5] >> (c & 31) & 1 ? 2 : 1;
        int id = rb_world_block(m->w, x, y, z), meta = rb_world_meta(m->w, x, y, z);
        if (cube) rb_mesh_cell_standard(m, k, id, meta, x, y, z);
        else rb_mesh_cell(m, k, id, meta, x, y, z);
    }
}

static void draw_cells(struct rb_mesher *m, const uint32_t *any, const uint32_t *two, const uint32_t *l0, const struct meshing_req *q,
                       int a, int b)
{
    draw_cells_as(m, any, two, l0, q, a, b, 0);
}

static void draw_cells_cube(struct rb_mesher *m, const uint32_t *any, const uint32_t *two, const uint32_t *l0, const struct meshing_req *q,
                            int a, int b)
{
    draw_cells_as(m, any, two, l0, q, a, b, 1);
}

/* the first of the WARM cells to visit before cell A (0 when fewer) */
static int warm_start(const uint32_t *any, int a)
{
    int left = WARM, c = a;
    while (left > 0 && c > 0)
    {
        int wd = (c - 1) >> 5;
        uint32_t bits = any[wd] & (0xffffffffu >> (31 - ((c - 1) & 31)));
        while (bits && left > 0)
        {
            int j = 31 - __builtin_clz(bits);
            bits &= ~(1u << j);
            c = wd << 5 | j;
            --left;
        }
        if (left > 0) c = wd << 5;
    }
    return left > 0 ? 0 : c;
}

/* the guess: the state the WARM cells before cell A leave when drawn from
 * the request's start (none for a request's first lane: LATER 0). A single
 * cell (WARM 1): a hidden cube's state, a drawn cell only begun
 * (render_blocks.c rb_mesh_guess_cell: render_block_begin, and for stairs,
 * doors and fences their last part's bounds): measured over the
 * microbenchmark and pipe_bench's three kinds, 0 wrong starts in 16.6 M
 * lanes of the begun types, the three caught by the check before they got
 * their states; a wrong guess costs a redraw, never a wrong vertex (the
 * lanes' check). One call, whatever the cell (no branch around a call:
 * draw_cells_as). */
static inline __attribute__((always_inline)) void guess_as(struct rb_mesher *m, const uint32_t *any, const uint32_t *two,
                                                           const struct meshing_req *q, int a, int later)
{
    _Static_assert(WARM == 1, "the guess is one cell");
    int g0 = later ? warm_start(any, a) : a, k = 0;
    if (g0 < a && (any[g0 >> 5] >> (g0 & 31) & 1)) k = two[g0 >> 5] >> (g0 & 31) & 1 ? 2 : 1;
    int x = (q->cx << 4) + (g0 & 15), z = (q->cz << 4) + ((g0 >> 4) & 15), y = (q->s << 4) + (g0 >> 8);
    int id = k ? rb_world_block(m->w, x, y, z) : 0, meta = k ? rb_world_meta(m->w, x, y, z) : 0;
    rb_mesh_guess_cell(m, k, id, meta, x, y, z);
}

/* the lane's run drawn from its start: the mesher words SM, the
 * Tessellator's ST and carried values CA */
static inline __attribute__((always_inline)) void redraw_as(struct rb_mesher *m, struct rb_tess *t, struct meshing_ctx *c, const uint32_t *sm,
                                                            const struct tstate *st, const struct tcarry *ca, const uint32_t *any,
                                                            const uint32_t *two, const uint32_t *l0, const struct meshing_req *q, int a, int b,
                                                            int cube)
{
    for (int k = 0; k < SW; ++k) wput(m, sw_at(k), sm[k]);
    tstate_put(t, st);
    tcarry_put(t, ca);
    t->n = t->vertex_count = 0;
    t->dropped = 0;
    c->err = 0;
    if (cube) draw_cells_cube(m, any, two, l0, q, a, b);
    else draw_cells(m, any, two, l0, q, a, b);
}

static void redraw(struct rb_mesher *m, struct rb_tess *t, struct meshing_ctx *c, const uint32_t *sm, const struct tstate *st,
                   const struct tcarry *ca, const uint32_t *any, const uint32_t *two, const uint32_t *l0, const struct meshing_req *q, int a,
                   int b)
{
    redraw_as(m, t, c, sm, st, ca, any, two, l0, q, a, b, 0);
}

/* the block's barrier; and the barrier that tells whether any lane's P is set */
static void bsync(void)
{
    __nvvm_barrier_sync(0);
}

static int bany(int p)
{
    return __nvvm_bar0_or(p);
}

/* mesh_run's block, CUBE: mesh_run_cube's copy, which meshes the
 * requests whose cells to draw are all hidden cubes and standard blocks
 * (the cube kernel reaches no other render type); mesh_run meshes the
 * others and copies the write pass's vertices. A count launch comes
 * classified and ordered (mesh_order: each kernel's blocks take its own
 * requests, heaviest first); in a write pass both classify a request's
 * cells and the one that does not own it returns. */
static inline __attribute__((always_inline)) void run_block(const struct meshing_launch *L, int cube)
{
    int lane = (int)__nvvm_read_ptx_sreg_laneid();
    int warp = (int)(__nvvm_read_ptx_sreg_tid_x() / 32);
    int g = (int)__nvvm_read_ptx_sreg_tid_x();
    int i = (int)__nvvm_read_ptx_sreg_ctaid_x();
    /* an ordered launch: block i takes its kernel's i-th request */
    if (L->ordered)
    {
        const int32_t *P = (const int32_t *)L->plan;
        int n = L->n, nc = P[3 * n];
        if (cube ? i >= nc : i >= n - nc) return;
        i = P[2 * n + (cube ? i : nc + i)];
    }
    if (i >= L->n) return;
    /* the table mode's count pass: only a request the table cannot mesh */
    if (L->tab && !(((const int32_t *)L->tflag)[i] & MTAB_REDO)) return;
    const unsigned all = 0xffffffffu, below = (1u << lane) - 1u;
    struct meshing_blk *B = (struct meshing_blk *)&meshing_sh;
    const struct meshing_req *q = (const struct meshing_req *)L->req + i;
    /* the write pass of a request the count pass wrote: mesh_wcopy's */
    if (q->out && L->voff && ((const long long *)L->voff)[q->key] >= 0) return;
    if (g == 0)
    {
        for (int k = 0; k < 9; ++k) B->near[k] = (struct chunk *)q->chunk[k];
        struct rb_world *w = &B->w;
        *w = (struct rb_world){0};
        w->chunks = B->near;
        w->margin = 1;
        w->rows = 3;
        w->origin_cx = q->cx;
        w->origin_cz = q->cz;
        w->te = (struct rb_te *)q->te;
        w->te_n = q->te_n;
        w->no_sky = q->no_sky;
        w->bands = B->bands;
        w->band_s0 = q->s - 1;
        w->band_x0 = (q->cx - 1) * 16;
        w->band_z0 = (q->cz - 1) * 16;
        w->band_y0 = (q->s - 1) * 16;
        B->err = B->flags = B->reruns = 0;
        B->slot = -1;
        B->gen = 0;
    }
    if (g < 27) band_put(B->bands, q, g);
    /* this lane's mesher: the template, at the section's start */
    struct rb_mesher m __attribute__((aligned(16)));
    struct rb_tess t;
    zero8(&t, sizeof t);
    struct meshing_ctx c = {(const struct rb_trig *)q->trig, (const struct rb_trig *)L->trig, q->ntrig, L->ntrig, 0};
    copy16(&m, (const void *)L->mesher, sizeof m);
    m.w = &B->w;
    m.t = &t;
    m.dev = &c;
    m.px = q->px;
    m.py = q->py;
    m.pz = q->pz;
    rb_mesh_section_begin(&m, q->cx, q->cz, q->s);
    struct tcarry c0;
    tcarry_get(&c0, &t);
    bsync();

    /* the kinds, NL cells at a time */
    int bx = q->cx << 4, bz = q->cz << 4, y0 = q->s << 4;
    for (int base = 0; base < 4096; base += NL)
    {
        int cl = base + g, id = 0, meta = 0;
        int kind = rb_mesh_cell_kind(&m, bx + (cl & 15), y0 + (cl >> 8), bz + ((cl >> 4) & 15), q->pass, &id, &meta);
        unsigned any = __nvvm_vote_ballot_sync(all, kind != 0), two = __nvvm_vote_ballot_sync(all, kind == 2);
        unsigned gen = __nvvm_vote_ballot_sync(all, kind == 2 && BLOCKS[id].render_type != 0);
        unsigned l0 = __nvvm_vote_ballot_sync(all, kind == 1 && m.tab->ao != 0 && m.tab->props[id].light == 0);
        if (lane == 0)
        {
            B->any[(base >> 5) + warp] = any;
            B->two[(base >> 5) + warp] = two;
            B->l0[(base >> 5) + warp] = l0;
            if (gen) B->gen = 1;
        }
    }
    bsync();
    /* the other kernel's request (an ordered launch's are its own) */
    if (!L->ordered && (cube ? B->gen : !B->gen)) return;
    /* a pool slot for the count pass: a free bit of the bitmap (at most
     * nslot blocks of the two kernels are resident, so one is free) */
    if (g == 0 && !q->out && L->voff && L->vbuf && L->pool)
    {
        unsigned *bits = (unsigned *)L->slots;
        int nw = (L->nslot + 31) / 32, w0 = (int)(__nvvm_read_ptx_sreg_smid() % (unsigned)nw);
        for (int tries = 0; B->slot < 0; ++tries)
        {
            int w = (w0 + tries) % nw;
            unsigned cur = __atomic_load_n(&bits[w], __ATOMIC_RELAXED);
            int lim = L->nslot - w * 32 < 32 ? L->nslot - w * 32 : 32;
            unsigned freeb = ~cur & (lim == 32 ? 0xffffffffu : (1u << lim) - 1u);
            if (!freeb) continue;
            int bit = __builtin_ctz(freeb);
            if (__atomic_compare_exchange_n(&bits[w], &cur, cur | 1u << bit, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
                B->slot = w * 32 + bit;
        }
    }
    bsync();

    /* this lane's run [a, b): from the cell holding weight g * total / NL
     * to the next lane's first */
    int total = 0;
    for (int wd = 0; wd < 128; ++wd) total += __builtin_popcount(B->any[wd]) + (W_TWO - 1) * __builtin_popcount(B->two[wd]);
    int a = 4096;
    if (g == 0) a = 0;
    else
    {
        int target = (int)(((long long)total * g) / NL), acc = 0;
        for (int wd = 0; wd < 128 && a == 4096; ++wd)
        {
            int ww = __builtin_popcount(B->any[wd]) + (W_TWO - 1) * __builtin_popcount(B->two[wd]);
            if (acc + ww <= target) { acc += ww; continue; }
            uint32_t bits = B->any[wd];
            while (bits)
            {
                int j = __builtin_ctz(bits);
                bits &= bits - 1;
                acc += B->two[wd] >> j & 1 ? W_TWO : 1;
                if (acc > target) { a = wd << 5 | j; break; }
            }
        }
    }
    B->a_of[g] = a;
    bsync();
    int b = g + 1 < NL ? B->a_of[g + 1] : 4096;

    /* the guess: the WARM cells before the run, from the section's start;
     * the run counted from it */
    guess_as(&m, B->any, B->two, q, a, g > 0);
    /* each warp one again after a draw (draw_lane) */
    bsync();
    uint32_t sm[SW];
    struct tstate st;
    for (int k = 0; k < SW; ++k) sm[k] = wget(&m, sw_at(k));
    tstate_get(&st, &t);
    /* the count pass draws each run once, into the lane's own buffer: the
     * vertices are copied to their place once the counts are known */
    int priv = B->slot >= 0;
    int32_t *pv = priv ? (int32_t *)L->pool + ((size_t)B->slot * NL + (size_t)g) * PK * 8 : NULL;
    const struct tcarry *crun = priv ? &PMARK : &MARK;
    t.raw = priv ? pv : NULL;
    t.cap = priv ? PK * 8 : 0;
    redraw_as(&m, &t, &c, sm, &st, crun, B->any, B->two, B->l0, q, a, b, cube);
    int reruns = 0;
    /* each start against the end of the lane before; the wrong ones again */
    for (;;)
    {
        struct tstate et;
        tstate_get(&et, &t);
        bsync();
        if (lane == 31)
        {
            for (int k = 0; k < SW; ++k) B->bnd[warp][k] = wget(&m, sw_at(k));
            for (int k = 0; k < TW; ++k) B->bnd[warp][SW + k] = wget(&et, k);
        }
        bsync();
        int eq = 1;
        for (int k = 0; k < SW; ++k)
        {
            uint32_t p = up1(wget(&m, sw_at(k)));
            if (lane == 0 && warp > 0) p = B->bnd[warp - 1][k];
            eq &= sm[k] == p;
        }
        for (int k = 0; k < TW; ++k)
        {
            uint32_t p = up1(wget(&et, k));
            if (lane == 0 && warp > 0) p = B->bnd[warp - 1][SW + k];
            eq &= wget(&st, k) == p;
        }
        int wrong = g > 0 && !eq;
        if (!bany(wrong)) break;
        for (int k = 0; k < SW; ++k)
        {
            uint32_t p = up1(wget(&m, sw_at(k)));
            if (lane == 0 && warp > 0) p = B->bnd[warp - 1][k];
            if (wrong) sm[k] = p;
        }
        for (int k = 0; k < TW; ++k)
        {
            uint32_t p = up1(wget(&et, k));
            if (lane == 0 && warp > 0) p = B->bnd[warp - 1][SW + k];
            if (wrong) wput(&st, k, p);
        }
        if (wrong)
        {
            ++reruns;
            redraw_as(&m, &t, &c, sm, &st, crun, B->any, B->two, B->l0, q, a, b, cube);
        }
    }
    /* a run past its buffer: every lane counts again */
    if (priv && bany(t.dropped))
    {
        priv = 0;
        t.raw = NULL;
        t.cap = 0;
        redraw_as(&m, &t, &c, sm, &st, &MARK, B->any, B->two, B->l0, q, a, b, cube);
    }
    /* the warps one again before the shuffles (the redraw above can leave
     * one split, and ptxas gave the shuffles no warp sync of their own) */
    bsync();

    /* the lanes' counts: this lane's offset, the section's */
    int cnt = t.vertex_count, incl = cnt;
    for (int d = 1; d < 32; d <<= 1)
    {
        int v = __nvvm_shfl_sync_up_i32(all, incl, d, 0);
        if (lane >= d) incl += v;
    }
    if (lane == 31) B->wsum[warp] = incl;
    bsync();
    int off = incl - cnt, count = 0;
    for (int w = 0; w < WARPS; ++w)
    {
        if (w < warp) off += B->wsum[w];
        count += B->wsum[w];
    }
    struct tcarry e;
    tcarry_get(&e, &t);
    /* where the vertices go: the write pass's block, or in the count pass
     * the launch's vbuf when they fit */
    int32_t *out = (int32_t *)q->out;
    int cap = q->cap;
    if (!out && L->voff)
    {
        if (g == 0)
        {
            unsigned long long base = 0;
            if (count > 0 && L->vbuf) base = __atomic_fetch_add((unsigned long long *)L->vcur, (unsigned long long)count, __ATOMIC_RELAXED);
            B->base = base;
            int fits = count > 0 && L->vbuf && base + (unsigned long long)count <= (unsigned long long)L->vcap;
            ((long long *)L->voff)[q->key] = fits ? (long long)base : -1;
            if (count > 0 && !fits) B->reruns |= MESHING_RES_REDRAW;
        }
        bsync();
        unsigned long long base = B->base;
        if (count > 0 && L->vbuf && base + (unsigned long long)count <= (unsigned long long)L->vcap)
        {
            out = (int32_t *)L->vbuf + (size_t)base * 8;
            cap = count;
        }
    }
    int flags = (e.has_texture ? 1 : 0) | (e.has_color ? 2 : 0) | (e.has_brightness ? 4 : 0) | (e.has_normals ? 8 : 0);
    struct tcarry ca = c0;
    if (out || priv)
    {
        /* the carried values each lane starts with: each field the nearest
         * lane's before it that set it, or the section's start; a flag set
         * when any lane before set it (a run into its own buffer ran with
         * the flags on: a flag set with its value) */
        for (int k = 0; k < CW; ++k) B->etab[g][k] = wget(&e, k);
        int set[8];
        for (int f = 0; f < 8; ++f)
        {
            set[f] = 0;
            for (int k = CF[f][0]; k < CF[f][1]; ++k) set[f] |= wget(&e, k) != wget(&MARK, k);
        }
        if (priv)
        {
            set[4] = set[0];
            set[5] = set[2];
            set[6] = set[3];
            set[7] = e.has_normals != 0;
        }
        for (int f = 0; f < 8; ++f)
        {
            unsigned sm_ = __nvvm_vote_ballot_sync(all, set[f]);
            if (lane == 0) B->setm[f][warp] = sm_;
        }
        bsync();
        if (priv)
        {
            flags = 0;
            for (int w = 0; w < WARPS; ++w)
                flags |= (B->setm[4][w] ? 1 : 0) | (B->setm[5][w] ? 2 : 0) | (B->setm[6][w] ? 4 : 0) | (B->setm[7][w] ? 8 : 0);
            flags |= (c0.has_texture ? 1 : 0) | (c0.has_color ? 2 : 0) | (c0.has_brightness ? 4 : 0) | (c0.has_normals ? 8 : 0);
        }
        for (int f = 0; f < 8; ++f)
        {
            int from = -1;
            unsigned before = B->setm[f][warp] & below;
            if (before) from = warp * 32 + 31 - __builtin_clz(before);
            else
                for (int w = warp - 1; w >= 0 && from < 0; --w)
                    if (B->setm[f][w]) from = w * 32 + 31 - __builtin_clz(B->setm[f][w]);
            for (int k = CF[f][0]; k < CF[f][1]; ++k)
            {
                if (CF[f][2]) wput(&ca, k, wget(&c0, k) | (from >= 0));
                else if (from >= 0) wput(&ca, k, B->etab[from][k]);
            }
        }
    }
    if (out && priv)
    {
        /* the lane's vertices to their place, each marked field (a vertex
         * before the lane's own setter) given the value the lane starts with;
         * a marked field after a set value would be a setter writing the
         * mark: loud */
        int32_t *dst = out + (size_t)off * 8;
        const uint32_t mu = fbits32((float)MARK.u), mv = fbits32((float)MARK.v);
        const uint32_t pu = fbits32((float)ca.u), pvv = fbits32((float)ca.v);
        int seen = 0;
        for (int j = 0; j < cnt; ++j)
        {
            int32_t *v = pv + j * 8;
            uint32_t u = (uint32_t)v[3], vv = (uint32_t)v[4], col = (uint32_t)v[5], br = (uint32_t)v[7];
            if (u == mu) { if (seen & 1) c.err |= MESHING_ERR_MARK; if (ca.has_texture) u = pu; } else seen |= 1;
            if (vv == mv) { if (seen & 2) c.err |= MESHING_ERR_MARK; if (ca.has_texture) vv = pvv; } else seen |= 2;
            if (col == (uint32_t)MARK.color) { if (seen & 4) c.err |= MESHING_ERR_MARK; if (ca.has_color) col = (uint32_t)ca.color; } else seen |= 4;
            if (br == (uint32_t)MARK.brightness) { if (seen & 8) c.err |= MESHING_ERR_MARK; if (ca.has_brightness) br = (uint32_t)ca.brightness; } else seen |= 8;
            dst[j * 8 + 0] = v[0];
            dst[j * 8 + 1] = v[1];
            dst[j * 8 + 2] = v[2];
            dst[j * 8 + 3] = (int32_t)u;
            dst[j * 8 + 4] = (int32_t)vv;
            dst[j * 8 + 5] = (int32_t)col;
            dst[j * 8 + 6] = v[6];
            dst[j * 8 + 7] = (int32_t)br;
        }
    }
    else if (out)
    {
        t.raw = out + (size_t)off * 8;
        t.cap = cap * 8 - off * 8;
        redraw_as(&m, &t, &c, sm, &st, &ca, B->any, B->two, B->l0, q, a, b, cube);
        /* each lane's carried values against the end of the lane before */
        for (;;)
        {
            tcarry_get(&e, &t);
            bsync();
            if (lane == 31)
                for (int k = 0; k < CW; ++k) B->cbnd[warp][k] = wget(&e, k);
            bsync();
            int eq = 1;
            for (int k = 0; k < CW; ++k)
            {
                uint32_t p = up1(wget(&e, k));
                if (lane == 0 && warp > 0) p = B->cbnd[warp - 1][k];
                eq &= wget(&ca, k) == p;
            }
            int wrong = g > 0 && !eq;
            if (!bany(wrong)) break;
            for (int k = 0; k < CW; ++k)
            {
                uint32_t p = up1(wget(&e, k));
                if (lane == 0 && warp > 0) p = B->cbnd[warp - 1][k];
                if (wrong) wput(&ca, k, p);
            }
            if (wrong)
            {
                ++reruns;
                redraw_as(&m, &t, &c, sm, &st, &ca, B->any, B->two, B->l0, q, a, b, cube);
            }
        }
    }
    int err = c.err | (!priv && t.dropped ? MESHING_ERR_FULL : 0);
    if (state_changed(&m, (const void *)L->mesher)) err |= MESHING_ERR_STATE;
    if (!priv) flags = (e.has_texture ? 1 : 0) | (e.has_color ? 2 : 0) | (e.has_brightness ? 4 : 0) | (e.has_normals ? 8 : 0);
    for (int d = 16; d > 0; d >>= 1)
    {
        err |= __nvvm_shfl_sync_down_i32(all, err, d, 0x1f);
        flags |= __nvvm_shfl_sync_down_i32(all, flags, d, 0x1f);
        reruns += __nvvm_shfl_sync_down_i32(all, reruns, d, 0x1f);
    }
    if (lane == 0)
    {
        __atomic_fetch_or(&B->err, err, __ATOMIC_RELAXED);
        __atomic_fetch_or(&B->flags, flags, __ATOMIC_RELAXED);
        __atomic_fetch_add(&B->reruns, reruns, __ATOMIC_RELAXED);
    }
    int spc = specials_changed_block(L, g, NL);
    if (g == 0 && B->slot >= 0)
    {
        unsigned *bits = (unsigned *)L->slots;
        __atomic_fetch_and(&bits[B->slot / 32], ~(1u << (B->slot % 32)), __ATOMIC_RELEASE);
    }
    if (g == 0)
    {
        struct meshing_res *res = (struct meshing_res *)L->res + i;
        res->count = count;
        res->flags = B->flags;
        res->err = B->err | (spc ? MESHING_ERR_STATE : 0);
        res->pad = B->reruns;
    }
}

__attribute__((nvptx_kernel)) void mesh_run(const struct meshing_launch *L)
{
    run_block(L, 0);
}

__attribute__((nvptx_kernel)) void mesh_run_cube(const struct meshing_launch *L)
{
    run_block(L, 1);
}

/* ------------------------------------------------------------ the spread count pass
 *
 * mesh_run gives every request one block of NL lanes, so a launch of a
 * few hundred sections is one wave that lasts as long as its heaviest
 * section, its lanes waiting at the block's barriers for the slowest. The
 * spread pass shares the launch's work out over lcap lanes (the device's
 * resident threads) instead, each request's lanes in proportion to its
 * weight: mesh_class the kinds of each request's cells (a block a
 * request), mesh_plan each request's lanes (one block), mesh_cut,
 * mesh_pack and mesh_place each lane's run (the lanes whose run is
 * empty dropped), mesh_keys and mesh_sort the lanes in the order of
 * what their runs draw (a warp then draws alike; the lanes that draw only
 * hidden cubes and standard blocks first), mesh_draw_cube those lanes'
 * runs and mesh_draw the others' at once (two streams; the cube kernel
 * reaches renderStandardBlock alone, so ptxas sizes it for that path), each
 * run drawn from its guessed start into its pool buffer (the same guess,
 * run and state words as mesh_run: a lane is a lane of mesh_run whose
 * neighbours live in global memory), mesh_check
 * each lane's start against the end of the lane before, mesh_fix a warp a
 * request: the wrong ones in order, each drawn again from the end before it
 * (the chain mesh_run's rounds settle on), the lane after a redrawn one
 * held again, then the counts, flags and carried values, and the
 * request's place in vbuf; mesh_vcopy a warp a lane: its vertices to
 * their place, each marked field given the value the lane starts with. A
 * request with a lane past its buffer is counted again and left to the
 * write pass (voff -1), which draws it the mesh_run way. */

/* a lane's record (kernels.h MESHING_LANE_WORDS), word w of lane k at
 * lanes[w * lcap + k]: its request, run, count, whether its run passed its
 * buffer, its error bits, its vertices' offset in the request, which
 * carried fields its run set (bits 0..7: CF's fields), whether its start
 * is not the end of the lane before (mesh_check), its run's draw kind
 * (mesh_cut), then its start and
 * end state (SW mesher words, TW Tessellator words), its carried values at
 * the end, and those it starts with (mesh_draw: the request's start;
 * mesh_fix: resolved) */
enum { LR_SEC = 0, LR_A, LR_B, LR_CNT, LR_DROP, LR_ERR, LR_OFF, LR_SET, LR_BAD, LR_KEY,
       LR_S = 12, LR_E = LR_S + SW + TW, LR_CE = LR_E + SW + TW, LR_CA = LR_CE + (CW + 3) / 4 * 4, LR_END = LR_CA + CW,
       LSW = (LR_END - LR_S + 3) / 4 * 4, LW = MESHING_LANE_WORDS };
typedef char meshing_lane_words_fit[LR_S + LSW <= LW && (SW + TW) % 4 == 0 && REG0 % 4 == 0 ? 1 : -1];
/* the words before LR_S at lanes[w * lcap + k] (a thread a lane reads them
 * together), the state words (LR_S on) a lane's own block of LSW after
 * them, 16-byte aligned: a lane's states are written and compared 16 bytes
 * at a time */
#define LSTATE(R) ((uint32_t *)L->lanes + (size_t)LR_S * (size_t)L->lcap + (size_t)((const uint32_t *)(R) - (const uint32_t *)L->lanes) * LSW)
#define LREC(R, w) (*((w) < LR_S ? (R) + (size_t)(w) * (size_t)L->lcap : LSTATE(R) + ((w) - LR_S)))

/* a thread's world (its request's chunks): mesh_fix's in shared memory,
 * the draw kernels' in the lane's own (local) memory, which leaves those
 * kernels no shared memory and L1 the whole carveout */
struct lanew {
    struct rb_world w;
    struct chunk *near[9];
};
static __attribute__((address_space(3))) struct lanew meshing_lw[NL];
/* mesh_class's block: its request's world and a mesher to classify with
 * (rb_mesh_cell_kind only reads it), the sums */
struct classblk {
    struct lanew lw;
    struct rb_band bands[27];
    struct rb_mesher m __attribute__((aligned(16)));
};
static __attribute__((address_space(3))) struct classblk meshing_cb;
/* mesh_plan's scan */
static __attribute__((address_space(3))) long long meshing_ps[1024];

static void world_of(struct lanew *lw, const struct meshing_req *q, const struct rb_band *bands)
{
    for (int k = 0; k < 9; ++k) lw->near[k] = (struct chunk *)q->chunk[k];
    struct rb_world *w = &lw->w;
    *w = (struct rb_world){0};
    w->bands = bands;
    w->band_s0 = q->s - 1;
    w->chunks = lw->near;
    w->margin = 1;
    w->rows = 3;
    w->origin_cx = q->cx;
    w->origin_cz = q->cz;
    w->te = (struct rb_te *)q->te;
    w->te_n = q->te_n;
    w->no_sky = q->no_sky;
    w->band_x0 = (q->cx - 1) * 16;
    w->band_z0 = (q->cz - 1) * 16;
    w->band_y0 = (q->s - 1) * 16;
}

/* request S's cache (mesh_class's) under its world, when the launch has
 * them */
enum { WC_CC = RB_CACHE * RB_CACHE * RB_CACHE, WC_CB = RB_CACHE * RB_CACHE };
_Static_assert(MESHING_WC_BIOME == WC_CC * 4 && MESHING_WC_NOISE >= MESHING_WC_BIOME + WC_CB && MESHING_WC_NOISE % 8 == 0 &&
               MESHING_WC_NOISE + 2 * WC_CB * 8 <= MESHING_WC_BYTES, "the world cache's parts");
static void world_cached(struct rb_world *w, const struct meshing_launch *L, const struct meshing_req *q, int s)
{
    if (!L->wcache) return;
    const char *wc = (const char *)L->wcache + (size_t)s * MESHING_WC_BYTES;
    w->cache = (const uint32_t *)wc;
    w->bcache = (const uint8_t *)(wc + MESHING_WC_BIOME);
    w->ncache = (const double *)(wc + MESHING_WC_NOISE);
    w->cache_x0 = (q->cx << 4) - 2;
    w->cache_y0 = (q->s << 4) - 2;
    w->cache_z0 = (q->cz << 4) - 2;
}

/* a lane's mesher at its request's start */
static void mesher_at(struct rb_mesher *m, struct rb_tess *t, struct meshing_ctx *c, const struct meshing_launch *L,
                      const struct meshing_req *q, const struct rb_world *w)
{
    zero8(t, sizeof *t);
    *c = (struct meshing_ctx){(const struct rb_trig *)q->trig, (const struct rb_trig *)L->trig, q->ntrig, L->ntrig, 0};
    copy16(m, (const void *)L->mesher, sizeof *m);
    m->w = w;
    m->t = t;
    m->dev = c;
    m->px = q->px;
    m->py = q->py;
    m->pz = q->pz;
    rb_mesh_section_begin(m, q->cx, q->cz, q->s);
}

/* the spread pass's weight of a cell by its key (cell_key): about what its
 * draw costs a lane, in quarter microseconds of one lane alone on the RTX
 * PRO 6000 (a fit of 2.1 M lanes' cycles to the kinds of cells they drew,
 * lane/meshclimb): a hidden cube 1 (most are not drawn: draw_cells_as), a
 * standard block 34 and 32 a face it draws, another render type its own
 * (those not measured 160). The cut gives lanes equal weights, so a lane
 * of costly cells gets fewer of them (weights of 1, 16 and 64 put 12 to 16
 * crossed squares or standard blocks in a lane beside lanes of one) */
static const uint16_t KEY_RT_WEIGHT[190] = {
    [1] = 214, [2] = 250, [3] = 198, [4] = 33, [6] = 90, [7] = 393, [10] = 319, [11] = 627, [20] = 38, [28] = 180, [31] = 66,
    [40] = 54, [41] = 114,
};
static int key_weight(int key)
{
    if (key >= 254) return key == 254;
    if (key < 64) return 34 + 32 * __builtin_popcount((unsigned)key);
    int w = KEY_RT_WEIGHT[key - 64];
    return w ? w : 160;
}

/* the first cell of lane J of NL_ over a request's cells (weight TOTAL;
 * GEN the masks' third part, PRE each word's weight before it): the word
 * whose weight passes the lane's share found by bisection, then its cells */
static int run_start(const uint32_t *any, const uint8_t *ck, const uint32_t *pre, int total, int j, int nl)
{
    if (j == 0) return 0;
    int target = (int)(((long long)total * j) / nl);
    int lo = 0, hi = 127;
    while (lo < hi)
    {
        int mid = (lo + hi) / 2;
        if ((mid < 127 ? (int)pre[mid + 1] : total) > target) hi = mid;
        else lo = mid + 1;
    }
    if ((lo < 127 ? (int)pre[lo + 1] : total) <= target) return 4096;
    int acc = (int)pre[lo];
    uint32_t bits = any[lo];
    while (bits)
    {
        int k = __builtin_ctz(bits);
        bits &= bits - 1;
        acc += key_weight(ck[lo << 5 | k]);
        if (acc > target) return lo << 5 | k;
    }
    return 4096;
}

/* the mesher's state words and the Tessellator's into record words AT..
 * (eight loads, then eight stores) */
static void rec_state(uint32_t *restrict R, int at, const struct rb_mesher *restrict m, const struct rb_tess *restrict t,
                      const struct meshing_launch *L)
{
    /* the words REG0..REG1 16 bytes at a time (a lane's mesher is 16-byte
     * aligned), then the rest from registers */
    uint32_t *restrict d = __builtin_assume_aligned(&LREC(R, at), 16);
    const char *src = __builtin_assume_aligned((const char *)m + REG0 * 4, 16);
    enum { V = (REG1 - REG0) / 4 };
    for (int i = 0; i < V; ++i)
    {
        uint32_t v[4];
        __builtin_memcpy(v, __builtin_assume_aligned(src + 16 * i, 16), 16);
        __builtin_memcpy(__builtin_assume_aligned(d + 4 * i, 16), v, 16);
    }
    struct tstate st;
    tstate_get(&st, t);
    uint32_t v[SW + TW - 4 * V];
    for (int w = 4 * V; w < SW; ++w) v[w - 4 * V] = wget(m, sw_at(w));
    for (int k = 0; k < TW; ++k) v[SW - 4 * V + k] = wget(&st, k);
    for (int i = 0; i < (SW + TW - 4 * V) / 4; ++i) __builtin_memcpy(__builtin_assume_aligned(d + 4 * V + 4 * i, 16), v + 4 * i, 16);
}

/* CW carried words V into the record's words AT.. (LR_CE or LR_CA: 16-byte
 * aligned, with the block's pad after them) 16 bytes at a time */
enum { CW4 = (CW + 3) / 4 * 4 };
typedef char meshing_carry_pad[LR_CA - LR_CE >= CW4 && LR_S + LSW - LR_CA >= CW4 && (LR_CE - LR_S) % 4 == 0 && (LR_CA - LR_S) % 4 == 0 ? 1 : -1];
static void rec_carry(uint32_t *R, int at, const uint32_t *v, const struct meshing_launch *L)
{
    uint32_t *d = __builtin_assume_aligned(&LREC(R, at), 16);
    uint32_t p[CW4];
    for (int w = 0; w < CW4; ++w) p[w] = w < CW ? v[w] : 0;
    for (int i = 0; i < CW4 / 4; ++i) __builtin_memcpy(__builtin_assume_aligned(d + 4 * i, 16), p + 4 * i, 16);
}

/* a lane's end into its record R: the state words, the carried values and
 * the fields its run set (a run into its own buffer runs with the flags
 * on: a flag is set with its value), its count, its errors */
static void lane_end(uint32_t *R, const struct rb_mesher *m, const struct rb_tess *t, const struct meshing_ctx *c,
                     const struct meshing_launch *L)
{
    rec_state(R, LR_E, m, t, L);
    struct tcarry e;
    tcarry_get(&e, t);
    uint32_t ev[CW];
    for (int k = 0; k < CW; ++k) ev[k] = wget(&e, k);
    rec_carry(R, LR_CE, ev, L);
    int set = 0;
    for (int f = 0; f < 4; ++f)
        for (int k = CF[f][0]; k < CF[f][1]; ++k)
            if (wget(&e, k) != wget(&MARK, k)) set |= 1 << f;
    set |= (set & 1) << 4 | (set >> 2 & 1) << 5 | (set >> 3 & 1) << 6 | (e.has_normals ? 1 << 7 : 0);
    LREC(R, LR_SET) = (uint32_t)set;
    LREC(R, LR_CNT) = (uint32_t)t->vertex_count;
    LREC(R, LR_DROP) = (uint32_t)t->dropped;
    int err = c->err;
    if (state_changed(m, (const void *)L->mesher)) err |= MESHING_ERR_STATE;
    LREC(R, LR_ERR) = (uint32_t)err;
}

/* lane K's run drawn again from the start its record holds, into its
 * buffer (or only counted: RAW 0) */
static void lane_redraw(int k, int raw, const struct meshing_launch *L, struct lanew *lw)
{
    uint32_t *R = (uint32_t *)L->lanes + k;
    int s = (int)LREC(R, LR_SEC);
    const struct meshing_req *q = (const struct meshing_req *)L->req + s;
    const uint32_t *any = (const uint32_t *)L->masks + (size_t)s * MESHING_MASK_WORDS, *two = any + 128, *l0 = any + 384;
    world_of(lw, q, (const struct rb_band *)L->bands + (size_t)s * 27);
    world_cached(&lw->w, L, q, s);
    struct rb_mesher m __attribute__((aligned(16)));
    struct rb_tess t;
    struct meshing_ctx c;
    mesher_at(&m, &t, &c, L, q, &lw->w);
    uint32_t sm[SW];
    struct tstate st;
    for (int w = 0; w < SW; ++w) sm[w] = LREC(R, LR_S + w);
    for (int w = 0; w < TW; ++w) wput(&st, w, LREC(R, LR_S + SW + w));
    t.raw = raw ? (int32_t *)L->pool + (size_t)k * PK * 8 : NULL;
    t.cap = raw ? PK * 8 : 0;
    redraw(&m, &t, &c, sm, &st, &PMARK, any, two, l0, q, (int)LREC(R, LR_A), (int)LREC(R, LR_B));
    if (raw) lane_end(R, &m, &t, &c, L);
    else LREC(R, LR_CNT) = (uint32_t)t.vertex_count;
}

/* what a cell's draw does, to put lanes that draw alike in one warp: a
 * standard block (render type 0) the faces beside a cell that is not an
 * opaque cube (bits 0..5: y-, y+, z-, z+, x-, x+), another render type 64 +
 * the type, a hidden cube 254, nothing 255 */
static int cell_key(struct rb_mesher *m, int kind, int id, int x, int y, int z)
{
    if (kind != 2) return kind == 1 ? 254 : 255;
    int rt = BLOCKS[id].render_type;
    if (rt != 0) return 64 + (rt < 189 ? rt : 189);
    return !opaque_cube(m, x, y - 1, z) | !opaque_cube(m, x, y + 1, z) << 1 | !opaque_cube(m, x, y, z - 1) << 2 |
           !opaque_cube(m, x, y, z + 1) << 3 | !opaque_cube(m, x - 1, y, z) << 4 | !opaque_cube(m, x + 1, y, z) << 5;
}

/* CLASS_PARTS blocks of MESHING_CLASS_THREADS a request (a thread a cell):
 * its cells' kinds into masks and keys; block 0 writes its band table */
enum { CLASS_PARTS = 4096 / MESHING_CLASS_THREADS };
__attribute__((nvptx_kernel)) void mesh_class(const struct meshing_launch *L)
{
    int lane = (int)__nvvm_read_ptx_sreg_laneid();
    int g = (int)__nvvm_read_ptx_sreg_tid_x();
    int i = (int)__nvvm_read_ptx_sreg_ctaid_x(), part = (int)__nvvm_read_ptx_sreg_ctaid_y();
    if (i >= L->n) return;
    const unsigned all = 0xffffffffu;
    struct classblk *C = (struct classblk *)&meshing_cb;
    const struct meshing_req *q = (const struct meshing_req *)L->req + i;
    for (int k = g * 8; k < (int)sizeof C->m; k += MESHING_CLASS_THREADS * 8)
    {
        uint64_t v;
        __builtin_memcpy(&v, __builtin_assume_aligned((const char *)L->mesher + k, 8), 8);
        __builtin_memcpy(__builtin_assume_aligned((char *)&C->m + k, 8), &v, 8);
    }
    if (g < 27)
    {
        band_put(C->bands, q, g);
        if (part == 0) ((struct rb_band *)L->bands)[(size_t)i * 27 + g] = C->bands[g];
    }
    bsync();
    if (g == 0)
    {
        world_of(&C->lw, q, C->bands);
        C->m.w = &C->lw.w;
        C->m.px = q->px;
        C->m.py = q->py;
        C->m.pz = q->pz;
    }
    bsync();
    uint32_t *mk = (uint32_t *)L->masks + (size_t)i * MESHING_MASK_WORDS;
    int bx = q->cx << 4, bz = q->cz << 4, y0 = q->s << 4;
    int cl = part * MESHING_CLASS_THREADS + g, id = 0, meta = 0;
    int x = bx + (cl & 15), y = y0 + (cl >> 8), z = bz + ((cl >> 4) & 15);
    int kind = rb_mesh_cell_kind(&C->m, x, y, z, q->pass, &id, &meta), key = cell_key(&C->m, kind, id, x, y, z);
    ((uint8_t *)L->ckey)[(size_t)i * 4096 + cl] = (uint8_t)key;
    unsigned any = __nvvm_vote_ballot_sync(all, kind != 0), two = __nvvm_vote_ballot_sync(all, kind == 2);
    unsigned gen = __nvvm_vote_ballot_sync(all, kind == 2 && key >= 64);
    unsigned l0 = __nvvm_vote_ballot_sync(all, kind == 1 && C->m.tab->ao != 0 && C->m.tab->props[id].light == 0);
    /* the word's weight (mesh_wsum makes it the weight before it) */
    unsigned ww = __nvvm_redux_sync_add(kind ? (unsigned)key_weight(key) : 0u, all);
    if (lane == 0)
    {
        mk[(cl >> 5)] = any;
        mk[128 + (cl >> 5)] = two;
        mk[256 + (cl >> 5)] = gen;
        mk[384 + (cl >> 5)] = l0;
        mk[512 + (cl >> 5)] = ww;
    }
    /* the spread pass's cache of the request's world (the table mode's
     * mesh_tab_cache, from the same accessors), its biomes and its columns'
     * noise: the drawing lanes read each once there */
    if (L->wcache)
    {
        char *wc = (char *)L->wcache + (size_t)i * MESHING_WC_BYTES;
        int x0 = bx - 2, yc = y0 - 2, z0 = bz - 2;
        for (int e = cl; e < WC_CC + 3 * WC_CB; e += 4096)
        {
            if (e < WC_CC)
            {
                int cx = x0 + e / WC_CB, cz = z0 + e / RB_CACHE % RB_CACHE, cy = yc + e % RB_CACHE;
                ((uint32_t *)wc)[e] = (uint32_t)rb_world_block(&C->lw.w, cx, cy, cz) | (uint32_t)rb_world_meta(&C->lw.w, cx, cy, cz) << 12 |
                                      (uint32_t)rb_world_sky(&C->lw.w, cx, cy, cz) << 16 | (uint32_t)rb_world_blocklight(&C->lw.w, cx, cy, cz) << 20;
            }
            else if (e < WC_CC + WC_CB)
            {
                int j = e - WC_CC;
                ((uint8_t *)wc)[MESHING_WC_BIOME + j] = (uint8_t)rb_world_biome(&C->lw.w, x0 + j / RB_CACHE, z0 + j % RB_CACHE);
            }
            else
            {
                int j = e - WC_CC - WC_CB, c = j % WC_CB;
                ((double *)(wc + MESHING_WC_NOISE))[j] = rb_column_noise(&C->m, j / WC_CB, x0 + c / RB_CACHE, z0 + c % RB_CACHE);
            }
        }
    }
}

/* a warp a request, after mesh_class: each mask word's weight before it
 * (masks 512..639), the request's weight and four times its drawn cells
 * (the plan's first two parts: lanes_of) */
__attribute__((nvptx_kernel)) void mesh_wsum(const struct meshing_launch *L)
{
    int lane = (int)__nvvm_read_ptx_sreg_laneid();
    int s = (int)(__nvvm_read_ptx_sreg_ctaid_x() * (__nvvm_read_ptx_sreg_ntid_x() / 32) + __nvvm_read_ptx_sreg_tid_x() / 32), n = L->n;
    if (s >= n) return;
    const unsigned all = 0xffffffffu;
    uint32_t *mk = (uint32_t *)L->masks + (size_t)s * MESHING_MASK_WORDS;
    int ww[4], sum = 0, cells = 0;
    for (int k = 0; k < 4; ++k)
    {
        int wd = 4 * lane + k;
        uint32_t two = mk[128 + wd];
        ww[k] = (int)mk[512 + wd];
        sum += ww[k];
        cells += 4 * __builtin_popcount(two);
    }
    int incl = sum;
    for (int d = 1; d < 32; d <<= 1)
    {
        int v = __nvvm_shfl_sync_up_i32(all, incl, d, 0);
        if (lane >= d) incl += v;
    }
    int at = incl - sum;
    for (int k = 0; k < 4; ++k)
    {
        mk[512 + 4 * lane + k] = (uint32_t)at;
        at += ww[k];
    }
    for (int d = 16; d > 0; d >>= 1) cells += __nvvm_shfl_sync_bfly_i32(all, cells, d, 0x1f);
    if (lane == 31)
    {
        int32_t *P = (int32_t *)L->plan;
        P[s] = incl;
        P[n + s] = cells;
    }
}

/* one block of 1024, after mesh_class: a count launch of mesh_run
 * blocks in order, heaviest request first (by log2 of its weight), those
 * with no cell of another render type to draw (mesh_run_cube's) apart:
 * the plan's third part the cube requests then the others, P[3n] the cube
 * count. A launch of many sections is several waves of blocks, and one
 * that starts a heavy section last lasts as long as that section. */
enum { ORDER_B = 24 };
struct orderblk {
    int cnt[2][ORDER_B], at[2][ORDER_B];
};
static __attribute__((address_space(3))) struct orderblk meshing_ob;

__attribute__((nvptx_kernel)) void mesh_order(const struct meshing_launch *L)
{
    int g = (int)__nvvm_read_ptx_sreg_tid_x(), n = L->n;
    int32_t *P = (int32_t *)L->plan;
    struct orderblk *O = (struct orderblk *)&meshing_ob;
    if (g < 2 * ORDER_B) (&O->cnt[0][0])[g] = 0;
    bsync();
    for (int i = g; i < n; i += 1024)
    {
        const uint32_t *gm = (const uint32_t *)L->masks + (size_t)i * MESHING_MASK_WORDS + 256;
        uint32_t any = 0;
        for (int w = 0; w < 128; ++w) any |= gm[w];
        int b = ORDER_B - 1 - (31 - __builtin_clz((unsigned)P[i] + 1u));
        __atomic_fetch_add(&O->cnt[any != 0][b < 0 ? 0 : b], 1, __ATOMIC_RELAXED);
    }
    bsync();
    if (g == 0)
    {
        int at = 0;
        for (int c = 0; c < 2; ++c)
            for (int b = 0; b < ORDER_B; ++b)
            {
                O->at[c][b] = at;
                at += O->cnt[c][b];
            }
        P[3 * n] = O->at[1][0];
    }
    bsync();
    for (int i = g; i < n; i += 1024)
    {
        const uint32_t *gm = (const uint32_t *)L->masks + (size_t)i * MESHING_MASK_WORDS + 256;
        uint32_t any = 0;
        for (int w = 0; w < 128; ++w) any |= gm[w];
        int b = ORDER_B - 1 - (31 - __builtin_clz((unsigned)P[i] + 1u));
        P[2 * n + __atomic_fetch_add(&O->at[any != 0][b < 0 ? 0 : b], 1, __ATOMIC_RELAXED)] = i;
    }
}

/* each request's lanes: at least one, at most four a drawn cell (the
 * plan's cells: a lane a visited cell spent more on the lanes than on their
 * runs, hidden cubes being nearly free; one or two a drawn cell left the
 * cube lanes two standard blocks; eight measured the same as four), in
 * proportion to its weight, budget in all */
static long long lanes_of(const int32_t *P, int n, int s, long long total, long long budget)
{
    long long nl = 1 + (total > 0 ? (long long)P[s] * budget / total : 0);
    if (nl > P[n + s]) nl = P[n + s] > 0 ? P[n + s] : 1;
    return nl;
}

/* one block of 1024: each request's first lane */
__attribute__((nvptx_kernel)) void mesh_plan(const struct meshing_launch *L)
{
    int g = (int)__nvvm_read_ptx_sreg_tid_x(), n = L->n;
    int32_t *P = (int32_t *)L->plan, *first = P + 2 * n;
    long long *ps = (long long *)meshing_ps;
    long long w = 0;
    for (int s = g; s < n; s += 1024) w += P[s];
    ps[g] = w;
    bsync();
    for (int d = 512; d > 0; d >>= 1)
    {
        if (g < d) ps[g] += ps[g + d];
        bsync();
    }
    long long total = ps[0], budget = L->lcap - n;
    bsync();
    /* each thread a contiguous share of the requests */
    int s0 = (int)(((long long)n * g) / 1024), s1 = (int)(((long long)n * (g + 1)) / 1024);
    long long mine = 0;
    for (int s = s0; s < s1; ++s) mine += lanes_of(P, n, s, total, budget);
    ps[g] = mine;
    bsync();
    for (int d = 1; d < 1024; d <<= 1)
    {
        long long v = g >= d ? ps[g - d] : 0;
        bsync();
        ps[g] += v;
        bsync();
    }
    long long at = ps[g] - mine;
    for (int s = s0; s < s1; ++s)
    {
        first[s] = (int32_t)at;
        at += lanes_of(P, n, s, total, budget);
    }
    if (g == 1023) first[n] = (int32_t)at;
}

/* the lanes' keys (mesh_place) */
enum { KEY_HID = 64, KEY_NONE = 65, KEY_GEN = 66 };
typedef char meshing_keys_fit[KEY_GEN + 189 < MESHING_NKEY ? 1 : -1];

/* The cut, in three kernels. A lane whose run is empty (its first cell is
 * the next lane's: a cell weighs more than a lane's share) would draw its
 * guess cell for nothing, so only the lanes with a run are kept (and each
 * request's first): mesh_cut each planned lane's request and run into
 * L->cut (a thread a lane), mesh_pack the kept lanes' places and the
 * requests' first lanes among them (one block), mesh_place the kept lanes'
 * records and keys. A kept run ends where the next kept one starts. */
enum { CUT_KEEP = 1 << 26, CUT_LATER = 1 << 27 };

__attribute__((nvptx_kernel)) void mesh_cut(const struct meshing_launch *L)
{
    int k = (int)(__nvvm_read_ptx_sreg_ctaid_x() * __nvvm_read_ptx_sreg_ntid_x() + __nvvm_read_ptx_sreg_tid_x()), n = L->n;
    int lane = (int)__nvvm_read_ptx_sreg_laneid();
    const int32_t *P = (const int32_t *)L->plan, *first = P + 2 * n;
    uint32_t *C = (uint32_t *)L->cut;
    int keep = 0;
    int s = 0, j = 0, nl = 0, a = 4096;
    const uint32_t *any = NULL, *pre = NULL;
    const uint8_t *ck = NULL;
    if (k < first[n])
    {
        int lo = 0, hi = n - 1;
        while (lo < hi)
        {
            int mid = (lo + hi + 1) / 2;
            if (first[mid] <= k) lo = mid; else hi = mid - 1;
        }
        s = lo;
        j = k - first[s];
        nl = first[s + 1] - first[s];
        any = (const uint32_t *)L->masks + (size_t)s * MESHING_MASK_WORDS;
        pre = any + 512;
        ck = (const uint8_t *)L->ckey + (size_t)s * 4096;
        a = run_start(any, ck, pre, P[s], j, nl);
    }
    /* The next planned lane has this lane's end. A warp's last lane looks
     * it up itself; every lane reaches the shuffle, including unused ones. */
    int next_a = __nvvm_shfl_sync_down_i32(0xffffffffu, a, 1, 0x1f);
    /* a lane past the plan's: not kept (the word is the last launch's) */
    if (k < L->lcap && k >= first[n]) C[L->lcap + k] = 0;
    else if (k < L->lcap)
    {
        int b = j + 1 < nl ? (lane == 31 ? run_start(any, ck, pre, P[s], j + 1, nl) : next_a) : 4096;
        keep = j == 0 || a < b;
        C[k] = (uint32_t)s;
        C[L->lcap + k] = (uint32_t)a | (uint32_t)b << 13 | (keep ? CUT_KEEP : 0) | (j > 0 ? CUT_LATER : 0);
    }
    /* the block's kept lanes (mesh_pack makes them its first place) */
    int cnt = __nvvm_bar0_popc(keep);
    if (__nvvm_read_ptx_sreg_tid_x() == 0) C[2 * L->lcap + __nvvm_read_ptx_sreg_ctaid_x()] = (uint32_t)cnt;
}

/* one block of 1024: each mesh_cut block's first place among the kept
 * lanes (L->cut's third part), and their count */
__attribute__((nvptx_kernel)) void mesh_pack(const struct meshing_launch *L)
{
    int g = (int)__nvvm_read_ptx_sreg_tid_x(), n = L->n;
    int32_t *first = (int32_t *)L->plan + 2 * n;
    uint32_t *C = (uint32_t *)L->cut + 2 * L->lcap;
    int nb = L->lcap / (32 * MESHING_WARPS), b0 = (int)(((long long)nb * g) / 1024), b1 = (int)(((long long)nb * (g + 1)) / 1024);
    long long *ps = (long long *)meshing_ps;
    long long mine = 0;
    for (int b = b0; b < b1; ++b) mine += C[b];
    ps[g] = mine;
    bsync();
    for (int d = 1; d < 1024; d <<= 1)
    {
        long long v = g >= d ? ps[g - d] : 0;
        bsync();
        ps[g] += v;
        bsync();
    }
    long long at = ps[g] - mine;
    for (int b = b0; b < b1; ++b)
    {
        uint32_t c = C[b];
        C[b] = (uint32_t)at;
        at += c;
    }
    if (g == 1023) first[n] = (int32_t)ps[1023];
}

/* a thread a planned lane: a kept lane's record at its place (its request,
 * its run to the next kept lane's start) and its key counted. The keys: a
 * cube lane (its guess cell and run draw only hidden cubes and standard
 * blocks: mesh_draw_cube's work) the open faces of its run's first
 * standard block (cell_key), else KEY_HID when its run visits a hidden cube,
 * else KEY_NONE; any other lane KEY_GEN + the render type of its first cell
 * of another type (cell_key - 64). The cube lanes sort first, KEY_NONE's
 * end is their count. */
static __attribute__((address_space(3))) int meshing_pw[MESHING_WARPS];

__attribute__((nvptx_kernel)) void mesh_place(const struct meshing_launch *L)
{
    int k = (int)(__nvvm_read_ptx_sreg_ctaid_x() * __nvvm_read_ptx_sreg_ntid_x() + __nvvm_read_ptx_sreg_tid_x());
    int lane = (int)__nvvm_read_ptx_sreg_laneid(), warp = (int)(__nvvm_read_ptx_sreg_tid_x() / 32);
    const uint32_t *C = (const uint32_t *)L->cut;
    uint32_t w = k < L->lcap ? C[L->lcap + k] : 0;
    /* the lane's place: its block's first (mesh_pack), then the kept
     * lanes before it in the block */
    unsigned kb = __nvvm_vote_ballot_sync(0xffffffffu, (w & CUT_KEEP) != 0);
    int *pw = (int *)meshing_pw;
    if (lane == 0) pw[warp] = __builtin_popcount(kb);
    bsync();
    int at = (int)C[2 * L->lcap + __nvvm_read_ptx_sreg_ctaid_x()] + __builtin_popcount(kb & ((1u << lane) - 1u));
    for (int v = 0; v < warp; ++v) at += pw[v];
    if (!(w & CUT_KEEP)) return;
    int s = (int)C[k], a = (int)(w & 8191), b = (int)(w >> 13 & 8191);
    const uint32_t *any = (const uint32_t *)L->masks + (size_t)s * MESHING_MASK_WORDS, *two = any + 128;
    const uint8_t *ck = (const uint8_t *)L->ckey + (size_t)s * 4096;
    /* from the guess cell on when the guess draws it (a hidden cube: a
     * drawn one is only begun, guess_as), else from the run */
    int w0 = a, key = KEY_NONE, gen = -1;
    if (w & CUT_LATER)
    {
        int g0 = warm_start(any, a);
        if (g0 < a && !(two[g0 >> 5] >> (g0 & 31) & 1)) w0 = g0;
    }
    for (int wd = w0 >> 5; wd < 128 && wd << 5 < b && gen < 0; ++wd)
    {
        uint32_t bits = any[wd];
        if (wd == w0 >> 5) bits &= ~0u << (w0 & 31);
        if (b < (wd + 1) << 5) bits &= (1u << (b & 31)) - 1u;
        for (uint32_t tb = bits & two[wd]; tb && gen < 0; tb &= tb - 1)
        {
            int c = wd << 5 | __builtin_ctz(tb), kc = ck[c];
            if (kc >= 64) gen = kc - 64;
            else if (key >= KEY_HID && c >= a) key = kc;
        }
        uint32_t own = bits & ~two[wd];
        if (wd == a >> 5) own &= ~0u << (a & 31);
        if (key == KEY_NONE && wd >= a >> 5 && own) key = KEY_HID;
    }
    if (gen >= 0) key = KEY_GEN + gen;
    /* the request's first lane (kept: its place) and its flags for
     * mesh_check (the plan's cells, which no kernel reads after
     * mesh_cut) */
    if (!(w & CUT_LATER))
    {
        ((int32_t *)L->plan)[L->n + s] = 0;
        ((int32_t *)L->plan)[2 * L->n + s] = at;
    }
    uint32_t *R = (uint32_t *)L->lanes + at;
    LREC(R, LR_SEC) = (uint32_t)s;
    LREC(R, LR_A) = (uint32_t)a;
    LREC(R, LR_B) = (uint32_t)b;
    LREC(R, LR_KEY) = (uint32_t)key;
    /* the key counted: one atomic for the warp's lanes that share it */
    unsigned peers = __nvvm_match_any_sync_i32(__nvvm_activemask(), key);
    if (lane == __builtin_ctz(peers)) __atomic_fetch_add((int32_t *)L->perm + L->lcap + key, __builtin_popcount(peers), __ATOMIC_RELAXED);
}

/* one block of 1024: the keys' counts into their first places */
__attribute__((nvptx_kernel)) void mesh_keys(const struct meshing_launch *L)
{
    enum { PER = (MESHING_NKEY + 1023) / 1024 };
    int g = (int)__nvvm_read_ptx_sreg_tid_x();
    int32_t *h = (int32_t *)L->perm + L->lcap + g * PER;
    int own = g * PER < MESHING_NKEY ? PER : 0;
    long long *ps = (long long *)meshing_ps;
    int v = 0;
    for (int k = 0; k < own; ++k) v += h[k];
    ps[g] = v;
    bsync();
    for (int d = 1; d < 1024; d <<= 1)
    {
        long long u = g >= d ? ps[g - d] : 0;
        bsync();
        ps[g] += u;
        bsync();
    }
    int at = (int)(ps[g] - v);
    for (int k = 0; k < own; ++k)
    {
        int c = h[k];
        h[k] = at;
        at += c;
    }
}

/* a thread a lane: its place in key order */
__attribute__((nvptx_kernel)) void mesh_sort(const struct meshing_launch *L)
{
    int k = (int)(__nvvm_read_ptx_sreg_ctaid_x() * __nvvm_read_ptx_sreg_ntid_x() + __nvvm_read_ptx_sreg_tid_x()), n = L->n;
    const int32_t *first = (const int32_t *)L->plan + 2 * n;
    if (k >= first[n]) return;
    const uint32_t *R = (const uint32_t *)L->lanes + k;
    /* the warp's lanes of one key take their places with one atomic */
    int key = (int)LREC(R, LR_KEY), lane = (int)__nvvm_read_ptx_sreg_laneid();
    unsigned act = __nvvm_activemask(), peers = __nvvm_match_any_sync_i32(act, key);
    int lead = __builtin_ctz(peers), at = 0;
    if (lane == lead) at = __atomic_fetch_add((int32_t *)L->perm + L->lcap + key, __builtin_popcount(peers), __ATOMIC_RELAXED);
    at = __nvvm_shfl_sync_idx_i32(peers, at, lead, 0x1f) + __builtin_popcount(peers & ((1u << lane) - 1u));
    ((int32_t *)L->perm)[at] = k;
}

/* a thread a lane (in key order): its run from its guessed start into its
 * buffer. The cube lanes (the first ncube of the key order) are
 * mesh_draw_cube's, the rest mesh_draw's: CUBE the cube kernel's copy,
 * which reaches only the hidden cubes' state and renderStandardBlock, so
 * ptxas gives its registers and stack to that path alone */
/* lane T0 of the key order drawn (ACT: this thread has one this round;
 * every thread of the block meets the two barriers, bar.sync counting
 * them all) */
static inline __attribute__((always_inline)) void draw_one(const struct meshing_launch *L, int cube, int t0, int act)
{
    int n = L->n;
    const int32_t *P = (const int32_t *)L->plan, *first = P + 2 * n;
    int k = act ? ((const int32_t *)L->perm)[t0] : 0;
    uint32_t *R = (uint32_t *)L->lanes + k;
    int s = act ? (int)LREC(R, LR_SEC) : 0, a = act ? (int)LREC(R, LR_A) : 0, b = act ? (int)LREC(R, LR_B) : 0, j = k - first[s];
    const struct meshing_req *q = (const struct meshing_req *)L->req + s;
    const uint32_t *any = (const uint32_t *)L->masks + (size_t)s * MESHING_MASK_WORDS, *two = any + 128, *l0 = any + 384;
    struct lanew lwl, *lw = &lwl;
    struct rb_mesher m __attribute__((aligned(16)));
    struct rb_tess t;
    struct meshing_ctx c;
    if (act)
    {
        world_of(lw, q, (const struct rb_band *)L->bands + (size_t)s * 27);
        world_cached(&lw->w, L, q, s);
        mesher_at(&m, &t, &c, L, q, &lw->w);
        struct tcarry c0;
        tcarry_get(&c0, &t);
        uint32_t cv[CW];
        for (int w = 0; w < CW; ++w) cv[w] = wget(&c0, w);
        rec_carry(R, LR_CA, cv, L);
        /* the guess, from the request's start */
        guess_as(&m, any, two, q, a, j > 0);
    }
    /* the warp one again after a draw: a draw can leave it split (a call
     * from a branch in the render tree whose convergence barrier ptxas
     * spilled; draw_cells_as), and ptxas compiles what follows for a whole
     * warp (a warp sync here it drops as already met). A block barrier is
     * kept, and the lanes leave it together (lane/meshcrash) */
    bsync();
    if (act)
    {
        rec_state(R, LR_S, &m, &t, L);
        /* the run from there (redraw without the state put back: it is the
         * mesher's) */
        tcarry_put(&t, &PMARK);
        t.raw = (int32_t *)L->pool + (size_t)k * PK * 8;
        t.cap = PK * 8;
        t.n = t.vertex_count = 0;
        t.dropped = 0;
        c.err = 0;
        if (cube) draw_cells_cube(&m, any, two, l0, q, a, b);
        else draw_cells(&m, any, two, l0, q, a, b);
    }
    bsync();
    if (act) lane_end(R, &m, &t, &c, L);
}

/* a block a warp (32 threads: a block holds its registers until its last
 * warp ends, and a draw's warps end far apart), as many as the launch has
 * (about what the SMs hold at once beside the other kernel): a warp's lanes
 * run their paths one after another where they differ, so the kernel's
 * lanes are shared out a few to a warp, at most 32, consecutive in the key
 * order; a round's barriers (draw_one) met by the whole warp */
static inline __attribute__((always_inline)) void draw_lane(const struct meshing_launch *L, int cube)
{
    int g = (int)__nvvm_read_ptx_sreg_tid_x(), w = (int)__nvvm_read_ptx_sreg_ctaid_x(), nw = (int)__nvvm_read_ptx_sreg_nctaid_x();
    const int32_t *first = (const int32_t *)L->plan + 2 * L->n;
    int ncube = ((const int32_t *)L->perm)[L->lcap + KEY_NONE];
    int nk = cube ? ncube : first[L->n] - ncube;
    int per = (nk + nw - 1) / nw;
    per = per < 1 ? 1 : per > 32 ? 32 : per;
    for (int r0 = w * per; r0 < nk; r0 += nw * per)
    {
        int act = g < per && r0 + g < nk;
        draw_one(L, cube, act ? (cube ? r0 + g : r0 + g + ncube) : 0, act);
    }
}

__attribute__((nvptx_kernel)) void mesh_draw(const struct meshing_launch *L)
{
    draw_lane(L, 0);
}

__attribute__((nvptx_kernel)) void mesh_draw_cube(const struct meshing_launch *L)
{
    draw_lane(L, 1);
}

/* a warp a lane (four words a thread, 16 bytes a load): its start
 * against the end of the lane before in its request (a request's first
 * lane starts at the request's start) */
__attribute__((nvptx_kernel)) void mesh_check(const struct meshing_launch *L)
{
    int lane = (int)__nvvm_read_ptx_sreg_laneid();
    int k = (int)__nvvm_read_ptx_sreg_ctaid_x() * WARPS + (int)(__nvvm_read_ptx_sreg_tid_x() / 32), n = L->n;
    const int32_t *first = (const int32_t *)L->plan + 2 * n;
    if (k >= first[n]) return;
    uint32_t *R = (uint32_t *)L->lanes + k;
    int s = (int)LREC(R, LR_SEC);
    uint32_t d = 0;
    if (k > first[s] && 4 * lane < SW + TW)
    {
        uint32_t x[4], y[4];
        __builtin_memcpy(x, __builtin_assume_aligned(&LREC(R, LR_S) + 4 * lane, 16), 16);
        __builtin_memcpy(y, __builtin_assume_aligned(&LREC(R - 1, LR_E) + 4 * lane, 16), 16);
        for (int j = 0; j < 4; ++j) d |= x[j] ^ y[j];
    }
    _Static_assert(SW + TW <= 128, "a lane's state in a warp's four words a thread");
    int bad = __nvvm_vote_any_sync(0xffffffffu, d != 0);
    if (lane == 0)
    {
        int fl = bad | (LREC(R, LR_DROP) ? 2 : 0);
        LREC(R, LR_BAD) = (uint32_t)bad;
        if (fl) __atomic_fetch_or((int32_t *)L->plan + n + s, fl, __ATOMIC_RELAXED);
    }
}

/* the block's sums in mesh_fix */
enum { FW = MESHING_FIX_WARPS, FNL = 32 * FW, FK = 4 };
struct fixblk {
    int wsum[FW];
    int last[8][FW];                    /* each warp's last lane that set each carried field, -1 none */
    int from[8];                        /* and the chunks' before */
    int total, flags, err, drop, reruns;
};
static __attribute__((address_space(3))) struct fixblk meshing_fb;

/* a block a request: its first warp draws the wrong lanes again in order
 * (each from the end before it, the lane after a redrawn one held again
 * against its new end) and counts again a run past its buffer; then the
 * block its lanes' offsets, flags and errors, and the request's place in
 * vbuf */
__attribute__((nvptx_kernel)) void mesh_fix(const struct meshing_launch *L)
{
    int lane = (int)__nvvm_read_ptx_sreg_laneid(), g = (int)__nvvm_read_ptx_sreg_tid_x(), warp = g / 32;
    int s = (int)__nvvm_read_ptx_sreg_ctaid_x(), n = L->n;
    if (s >= n) return;
    const unsigned all = 0xffffffffu;
    const int32_t *P = (const int32_t *)L->plan, *first = P + 2 * n;
    int l0 = first[s], nl = first[s + 1] - l0;
    uint32_t *R0 = (uint32_t *)L->lanes + l0;
    struct fixblk *F = (struct fixblk *)&meshing_fb;
    /* mesh_check's flags: a lane of the request starts wrong (1), a run
     * passed its buffer (2) */
    const int fl = P[n + s];
    if (warp == 0)
    {
        struct lanew *lw = &((struct lanew *)meshing_lw)[g];
        int reruns = 0, carry = 0;
        for (int j0 = 1; j0 < nl && (fl & 1); j0 += 32)
        {
            int j = j0 + lane;
            unsigned wrong = __nvvm_vote_ballot_sync(all, j < nl && LREC(R0 + j, LR_BAD));
            /* the chunk before's last lane was drawn again: this chunk's first
             * lane against its new end (mesh_check held it against the old) */
            if (carry && !(wrong & 1))
            {
                int eq = 1;
                for (int w = lane; w < SW + TW; w += 32) eq &= LREC(R0 + j0, LR_S + w) == LREC(R0 + j0 - 1, LR_E + w);
                if (!__nvvm_vote_all_sync(all, eq)) wrong |= 1;
            }
            carry = 0;
            while (wrong)
            {
                int x = __builtin_ctz(wrong), jx = j0 + x;
                wrong &= wrong - 1;
                uint32_t *R = R0 + jx;
                for (int w = lane; w < SW + TW; w += 32) LREC(R, LR_S + w) = LREC(R - 1, LR_E + w);
                __nvvm_bar_warp_sync(all);
                if (lane == 0) lane_redraw(l0 + jx, 1, L, lw);
                __nvvm_bar_warp_sync(all);
                ++reruns;
                /* the next lane's start against this new end (the next chunk's
                 * first lane with its chunk: carry) */
                if (x == 31) carry = 1;
                else if (jx + 1 < nl && !(wrong >> (x + 1) & 1))
                {
                    int eq = 1;
                    for (int w = lane; w < SW + TW; w += 32) eq &= LREC(R + 1, LR_S + w) == LREC(R, LR_E + w);
                    if (!__nvvm_vote_all_sync(all, eq)) wrong |= 1u << (x + 1);
                }
            }
        }
        /* a run past its buffer: counted again, the request left to the write pass */
        int drop = 0;
        for (int j0 = 0; j0 < nl && (fl & 2); j0 += 32)
        {
            unsigned d = __nvvm_vote_ballot_sync(all, j0 + lane < nl && LREC(R0 + j0 + lane, LR_DROP));
            if (d) drop = 1;
            while (d)
            {
                int x = __builtin_ctz(d);
                d &= d - 1;
                if (lane == 0) lane_redraw(l0 + j0 + x, 0, L, lw);
                __nvvm_bar_warp_sync(all);
            }
        }
        if (lane == 0)
        {
            F->total = F->err = F->flags = 0;
            F->drop = drop;
            F->reruns = reruns;
        }
        if (lane < 8) F->from[lane] = -1;
    }
    bsync();
    /* the request's start carried values (lane 0's); each lane's offset in
     * the request and the carried values it starts with (each field the
     * nearest lane's before it that set it, or the start's; a flag set when
     * any lane before set it: a run into its own buffer runs with the flags
     * on, a flag set with its value), FK consecutive lanes a thread, FNL * FK
     * at a time */
    struct tcarry c0;
    for (int w = 0; w < CW; ++w) wput(&c0, w, LREC(R0, LR_CA + w));
    const unsigned below = (1u << lane) - 1u;
    int total = 0, sets = 0, err = 0;
    for (int j0 = 0; j0 < nl; j0 += FNL * FK)
    {
        int jb = j0 + g * FK, cnt[FK], setv[FK], tsum = 0;
        for (int i = 0; i < FK; ++i)
        {
            int in = jb + i < nl;
            const uint32_t *R = R0 + (in ? jb + i : 0);
            cnt[i] = in ? (int)LREC(R, LR_CNT) : 0;
            setv[i] = in ? (int)LREC(R, LR_SET) : 0;
            if (in) err |= (int)LREC(R, LR_ERR);
            tsum += cnt[i];
            sets |= setv[i];
        }
        int incl = tsum;
        for (int d = 1; d < 32; d <<= 1)
        {
            int v = __nvvm_shfl_sync_up_i32(all, incl, d, 0);
            if (lane >= d) incl += v;
        }
        if (lane == 31) F->wsum[warp] = incl;
        /* each field's last setter among the thread's lanes, the warp's, and
         * the one before the thread's first lane in the warp */
        int prev[8];
        for (int f = 0; f < 8; ++f)
        {
            int tl = -1;
            for (int i = 0; i < FK; ++i)
                if (setv[i] >> f & 1) tl = jb + i;
            unsigned b = __nvvm_vote_ballot_sync(all, tl >= 0), bb = b & below;
            int wl = __nvvm_shfl_sync_idx_i32(all, tl, b ? 31 - __builtin_clz(b) : 0, 0x1f);
            int pl = __nvvm_shfl_sync_idx_i32(all, tl, bb ? 31 - __builtin_clz(bb) : 0, 0x1f);
            if (lane == 0) F->last[f][warp] = b ? wl : -1;
            prev[f] = bb ? pl : -1;
        }
        bsync();
        int off = total + incl - tsum;
        for (int w = 0; w < FW; ++w)
        {
            if (w < warp) off += F->wsum[w];
            total += F->wsum[w];
        }
        /* the thread's first lane's values, then each lane's from the one
         * before it where that one set them (words in registers: every
         * loop over the fields and their words unrolled) */
        uint32_t ca[CW];
#pragma unroll
        for (int w = 0; w < CW; ++w) ca[w] = wget(&c0, w);
#pragma unroll
        for (int f = 0; f < 8; ++f)
        {
            int src = F->from[f];
            for (int w = 0; w < warp; ++w)
                if (F->last[f][w] >= 0) src = F->last[f][w];
            if (prev[f] >= 0) src = prev[f];
#pragma unroll
            for (int w = CF[f][0]; w < CF[f][1]; ++w)
            {
                if (CF[f][2]) ca[w] = wget(&c0, w) | (src >= 0);
                else if (src >= 0) ca[w] = LREC(R0 + src, LR_CE + w);
            }
        }
        bsync();
        if (g < 8)
            for (int w = 0; w < FW; ++w)
                if (F->last[g][w] >= 0) F->from[g] = F->last[g][w];
#pragma unroll
        for (int i = 0; i < FK; ++i)
        {
            if (jb + i >= nl) break;
            uint32_t *R = R0 + jb + i;
            rec_carry(R, LR_CA, ca, L);
            LREC(R, LR_OFF) = (uint32_t)off;
            off += cnt[i];
#pragma unroll
            for (int f = 0; f < 8; ++f)
                if (setv[i] >> f & 1)
#pragma unroll
                    for (int w = CF[f][0]; w < CF[f][1]; ++w) ca[w] = CF[f][2] ? wget(&c0, w) | 1u : LREC(R, LR_CE + w);
        }
        bsync();
    }
    /* the flags and errors, a warp's first (the block's shared words take
     * one atomic a warp) */
    int fw = __nvvm_redux_sync_or((unsigned)((sets >> 4) & 15), all), ew = __nvvm_redux_sync_or((unsigned)err, all);
    if (lane == 0)
    {
        __atomic_fetch_or(&F->flags, fw | (c0.has_texture ? 1 : 0) | (c0.has_color ? 2 : 0) | (c0.has_brightness ? 4 : 0) | (c0.has_normals ? 8 : 0),
                          __ATOMIC_RELAXED);
        __atomic_fetch_or(&F->err, ew, __ATOMIC_RELAXED);
    }
    int spc = specials_changed_block(L, g, FNL);
    if (g == 0)
    {
        const struct meshing_req *q = (const struct meshing_req *)L->req + s;
        unsigned long long base = 0;
        if (total > 0 && L->vbuf) base = __atomic_fetch_add((unsigned long long *)L->vcur, (unsigned long long)total, __ATOMIC_RELAXED);
        int fits = !F->drop && total > 0 && L->vbuf && base + (unsigned long long)total <= (unsigned long long)L->vcap;
        ((long long *)L->voff)[q->key] = fits ? (long long)base : -1;
        struct meshing_res *res = (struct meshing_res *)L->res + s;
        *res = (struct meshing_res){total, F->flags, F->err | (spc ? MESHING_ERR_STATE : 0),
                                   F->reruns | (total > 0 && !fits ? MESHING_RES_REDRAW : 0)};
    }
}

/* a warp a lane: its vertices from its buffer to their place in vbuf, each
 * marked field (a vertex before the lane's own setter) given the value the
 * lane starts with; a marked field after a set value would be a setter
 * writing the mark: loud */
__attribute__((nvptx_kernel)) void mesh_vcopy(const struct meshing_launch *L)
{
    int lane = (int)__nvvm_read_ptx_sreg_laneid();
    int k = (int)__nvvm_read_ptx_sreg_ctaid_x() * WARPS + (int)(__nvvm_read_ptx_sreg_tid_x() / 32), n = L->n;
    const int32_t *first = (const int32_t *)L->plan + 2 * n;
    if (k >= first[n]) return;
    const unsigned all = 0xffffffffu;
    const uint32_t *R = (const uint32_t *)L->lanes + k;
    int s = (int)LREC(R, LR_SEC), cnt = (int)LREC(R, LR_CNT);
    const struct meshing_req *q = (const struct meshing_req *)L->req + s;
    long long base = ((const long long *)L->voff)[q->key];
    if (base < 0 || cnt == 0) return;
    struct tcarry ca;
    for (int w = 0; w < CW; ++w) wput(&ca, w, LREC(R, LR_CA + w));
    const int32_t *pv = (const int32_t *)L->pool + (size_t)k * PK * 8;
    int32_t *dst = (int32_t *)L->vbuf + ((size_t)base + LREC(R, LR_OFF)) * 8;
    const uint32_t mark[4] = {fbits32((float)MARK.u), fbits32((float)MARK.v), (uint32_t)MARK.color, (uint32_t)MARK.brightness};
    const int have[4] = {ca.has_texture, ca.has_texture, ca.has_color, ca.has_brightness};
    const uint32_t put[4] = {fbits32((float)ca.u), fbits32((float)ca.v), (uint32_t)ca.color, (uint32_t)ca.brightness};
    unsigned seen = 0;
    int bad = 0;
    for (int j0 = 0; j0 < cnt; j0 += 32)
    {
        int j = j0 + lane, in = j < cnt;
        int32_t v[8] = {0};
        if (in)
            for (int w = 0; w < 8; ++w) v[w] = pv[j * 8 + w];
        uint32_t f[4] = {(uint32_t)v[3], (uint32_t)v[4], (uint32_t)v[5], (uint32_t)v[7]};
        for (int x = 0; x < 4; ++x)
        {
            int mk = in && f[x] == mark[x];
            unsigned M = __nvvm_vote_ballot_sync(all, mk), N = __nvvm_vote_ballot_sync(all, in && !mk);
            if (M && (seen >> x & 1)) bad = 1;
            else if (M && N && (M & ~((2u << __builtin_ctz(N)) - 1u))) bad = 1;
            if (N) seen |= 1u << x;
            if (mk && have[x]) f[x] = put[x];
        }
        if (in)
        {
            dst[j * 8 + 0] = v[0];
            dst[j * 8 + 1] = v[1];
            dst[j * 8 + 2] = v[2];
            dst[j * 8 + 3] = (int32_t)f[0];
            dst[j * 8 + 4] = (int32_t)f[1];
            dst[j * 8 + 5] = (int32_t)f[2];
            dst[j * 8 + 6] = v[6];
            dst[j * 8 + 7] = (int32_t)f[3];
        }
    }
    if (bad && lane == 0) __atomic_fetch_or(&((struct meshing_res *)L->res + s)->err, MESHING_ERR_MARK, __ATOMIC_RELAXED);
}

/* the write pass: a block a request the count pass wrote into vbuf, its
 * vertices copied to their place 16 bytes at a time (the rest are
 * mesh_run's) */
__attribute__((nvptx_kernel)) void mesh_wcopy(const struct meshing_launch *L)
{
    int i = (int)__nvvm_read_ptx_sreg_ctaid_x(), g = (int)__nvvm_read_ptx_sreg_tid_x(), nt = (int)__nvvm_read_ptx_sreg_ntid_x();
    if (i >= L->n) return;
    const struct meshing_req *q = (const struct meshing_req *)L->req + i;
    long long v = ((const long long *)L->voff)[q->key];
    if (!q->out || v < 0) return;
    const char *src = (const char *)L->vbuf + (size_t)v * 32;
    char *dst = (char *)q->out;
    size_t nb = (size_t)q->cap * 32;
    if (((uintptr_t)src | (uintptr_t)dst) % 16 == 0)
        for (size_t k = (size_t)g * 16; k < nb; k += (size_t)nt * 16)
        {
            uint64_t a, b;
            __builtin_memcpy(&a, __builtin_assume_aligned(src + k, 16), 8);
            __builtin_memcpy(&b, __builtin_assume_aligned(src + k + 8, 8), 8);
            __builtin_memcpy(__builtin_assume_aligned(dst + k, 16), &a, 8);
            __builtin_memcpy(__builtin_assume_aligned(dst + k + 8, 8), &b, 8);
        }
    else
        for (size_t k = (size_t)g * 4; k < nb; k += (size_t)nt * 4)
            *(int32_t *)(dst + k) = *(const int32_t *)(src + k);
    if (g == 0)
    {
        struct meshing_res *res = (struct meshing_res *)L->res + i;
        *res = (struct meshing_res){q->cap, 0, 0, 0};
    }
}

/* the uploads: one block a copy */
__attribute__((nvptx_kernel)) void mesh_copy(const struct meshing_copy *ops, const unsigned char *staged, int n)
{
    int k = (int)__nvvm_read_ptx_sreg_ctaid_x();
    if (k >= n) return;
    const struct meshing_copy *op = &ops[k];
    unsigned char *dst = (unsigned char *)op->dst;
    const unsigned char *src = staged + op->src;
    for (uint64_t j = __nvvm_read_ptx_sreg_tid_x(); j < op->len; j += __nvvm_read_ptx_sreg_ntid_x()) dst[j] = src[j];
}
