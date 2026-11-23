/* pxdiff: zoom into a golden-vs-candidate pixel diff and name its cause.
 *
 * The hardest thing a model does on the pixel path is look at two frames and say
 * WHY they differ. Do not hand-roll pixel loops for that. This tool clusters the
 * differing pixels, runs the discriminators that separate the divergence families
 * 1.7.10 rendering actually hits, and names a cause. Start every pixel
 * investigation with `survey`; trust `selftest` first.
 *
 * Built as out/native/pxdiff (make -C csrc). The C port of tools/pxdiff.py
 * (lane/cport): the same clusters, verdicts, numbers and files; numpy's
 * reductions are reproduced (integer sums exact, float sums pairwise as numpy
 * adds them, the 90th percentile's lerp), so every printed and JSON number is
 * the Python tool's. Two pieces are not bit-for-bit: the two-vector dot
 * products (BLAS in numpy) are summed in order here, which can move only the
 * last bits before rounding; and survey's overview.png labels its boxes with
 * a built-in digit font where PIL drew its default font.
 *
 * Sources (every subcommand but selftest takes one):
 *   --a A.png --b B.png          any pair; A is golden (the oracle), B candidate
 *   --tape NAME --tick N         golden out/java/frames/<NAME>/f_%06d.png vs
 *                                candidate out/native/frames/<NAME>/f_%06d.png
 *
 * Subcommands:
 *   survey     one-shot triage: overview.png with numbered boxes + a zoom
 *              triptych per top cluster + survey.json. START HERE.
 *   clusters   labeled cluster table + a cause verdict per cluster
 *   zoom       a golden|candidate|heat triptych PNG, zoomed to a cluster or an
 *              explicit rect, nearest-neighbour with a texel grid
 *   pixels     exact RGB pairs for a small rect, as text
 *   probe      every discriminator for one cluster or rect, verbose
 *   frames     rank two frame directories tick by tick by unexplained pixels
 *   stats      mean/max absolute RGB error, optionally for --rect X0,Y0,X1,Y1
 *   selftest   synthetic mutations with known causes; verifies the verdicts
 *
 * Reading the output:
 *   px counts  survey/clusters count connected-component MEMBERS; probe and
 *              pixels count every differing pixel in the padded rect, so probe's
 *              count is >= the cluster's px. Both are correct.
 *   unresolved big unresolved clusters are auto-refined into tile verdicts
 *              (children field); frame-level notes flag the two whole-frame
 *              patterns no single cluster can name (global shift, camera/pose).
 *              Never report unresolved as a diagnosis, and never claim a cause
 *              the tool did not measure.
 *
 * Verdicts (what the discriminators mean):
 *   texel-selection  candidate values are golden values from the local
 *                    neighbourhood, reshuffled: nearest-neighbour minification
 *                    picking a different source texel. Either zero-mean, high
 *                    sigma, or a LOCAL one-texel slip (best_shift (-1,0)/(0,-1)
 *                    with a high texel_selection_tol4).
 *   shading-offset   uniform signed bias, low sigma, structure_corr high: a
 *                    lighting / fog / tint scalar is wrong, geometry and texture
 *                    right. Uniform bias but structure gone (corr<=0.5) is content.
 *   registration     an integer pixel shift beats dx=dy=0 by a wide margin: the
 *                    content is right and placed wrong.
 *   content          one side has structure the other lacks: missing or extra
 *                    geometry, the only family that is a hard bug by itself.
 *   edge             differing pixels hug golden gradients: silhouette / AA.
 *   cutout-sky+/-    a real coverage difference: >15% of the differing pixels
 *                    have one side at the background colour and the other not, so
 *                    a CUTOUT surface (foliage, cross plants) lets through more
 *                    (+) or less (-) background than the oracle.
 *
 * Usage:
 *   out/native/pxdiff selftest
 *   out/native/pxdiff survey --a golden.png --b native.png -o OUTDIR
 *   out/native/pxdiff probe  --a g.png --b n.png --at 120,80 --size 48x32
 * Options: --a --b --tape --tick --dir-a --dir-b --cluster --at --size (48x32)
 * --rect --scale (8) --pad (4) --no-grid --thresh (25) --min-px (50) --top (12)
 * -o/--out (/tmp/pxdiff.png) --json */
#define _GNU_SOURCE
#include "../csrc/engine/pngread.h"
#include "../csrc/engine/raster.h"
#include "../csrc/tests/tool.h"

enum { DIFF_THRESH = 25, MIN_CLUSTER = 50 };

/* a frame: H x W x 3 values (the int16 array numpy held) */
struct frame {
    int h, w;
    short *p;
};

#define PX(f, y, x, k) ((f)->p[((size_t)(y) * (f)->w + (x)) * 3 + (k)])

static void *xm(size_t n) { return xmalloc(n); }

/* ------------------------------------------------------------ numpy's sums */

/* numpy's pairwise summation of n doubles (what add.reduce does over a contiguous run) */
static double pairwise(const double *a, long n)
{
    if (n < 8) {
        double res = -0.0; /* numpy's start: a sum of signed zeros keeps its sign */
        for (long i = 0; i < n; ++i) res += a[i];
        return res;
    }
    if (n <= 128) {
        double r[8];
        for (int k = 0; k < 8; ++k) r[k] = a[k];
        long i;
        for (i = 8; i < n - (n % 8); i += 8)
            for (int k = 0; k < 8; ++k) r[k] += a[i + k];
        double res = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
        for (; i < n; ++i) res += a[i];
        return res;
    }
    long n2 = n / 2;
    n2 -= n2 % 8;
    return pairwise(a, n2) + pairwise(a + n2, n - n2);
}

static double np_mean1(const double *a, long n) { return (0.0 + pairwise(a, n)) / (double)n; }

/* x.std() of a contiguous float64 vector */
static double np_std1(const double *a, long n)
{
    double m = np_mean1(a, n);
    double *x = xm((size_t)(n ? n : 1) * sizeof *x);
    for (long i = 0; i < n; ++i) {
        double d = a[i] - m;
        x[i] = d * d;
    }
    double v = (0.0 + pairwise(x, n)) / (double)n;
    free(x);
    return sqrt(v);
}

static double r2(double x, int n) { return py_round(x, n); }

/* ------------------------------------------------------------ the per-frame quantities */

/* the golden's sky colour (median of its top 8 rows) and its edge threshold */
struct gcache {
    const struct frame *g;
    double sky[3];
    double *mag;
    double thr;
};
static struct gcache GC;

static int cmp_short(const void *a, const void *b) { return *(const short *)a - *(const short *)b; }
static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static void gcache(const struct frame *g)
{
    if (GC.g == g) return;
    free(GC.mag);
    GC.g = g;
    int rows = g->h < 8 ? g->h : 8;
    long n = (long)rows * g->w;
    short *v = xm((size_t)(n ? n : 1) * sizeof *v);
    for (int k = 0; k < 3; ++k) {
        long m = 0;
        for (int y = 0; y < rows; ++y)
            for (int x = 0; x < g->w; ++x) v[m++] = PX(g, y, x, k);
        qsort(v, (size_t)n, sizeof *v, cmp_short);
        if (n % 2) GC.sky[k] = v[n / 2];
        else GC.sky[k] = ((double)v[n / 2 - 1] + (double)v[n / 2]) / 2;
    }
    free(v);
    /* lum, np.gradient, np.hypot, the 90th percentile */
    int H = g->h, W = g->w;
    double *lum = xm((size_t)H * W * sizeof *lum);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) lum[(size_t)y * W + x] = ((0.0 + PX(g, y, x, 0)) + PX(g, y, x, 1) + PX(g, y, x, 2)) / 3;
    GC.mag = xm((size_t)H * W * sizeof *GC.mag);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            double gy, gx;
            if (H < 2) gy = 0;
            else if (y == 0) gy = (lum[(size_t)1 * W + x] - lum[x]) / 1.0;
            else if (y == H - 1) gy = (lum[(size_t)(H - 1) * W + x] - lum[(size_t)(H - 2) * W + x]) / 1.0;
            else gy = (lum[(size_t)(y + 1) * W + x] - lum[(size_t)(y - 1) * W + x]) / 2.0;
            if (W < 2) gx = 0;
            else if (x == 0) gx = (lum[(size_t)y * W + 1] - lum[(size_t)y * W]) / 1.0;
            else if (x == W - 1) gx = (lum[(size_t)y * W + W - 1] - lum[(size_t)y * W + W - 2]) / 1.0;
            else gx = (lum[(size_t)y * W + x + 1] - lum[(size_t)y * W + x - 1]) / 2.0;
            GC.mag[(size_t)y * W + x] = hypot(gy, gx);
        }
    free(lum);
    long N = (long)H * W;
    double *s = xm((size_t)N * sizeof *s);
    memcpy(s, GC.mag, (size_t)N * sizeof *s);
    qsort(s, (size_t)N, sizeof *s, cmp_double);
    double vi = (double)(N - 1) * 0.9;
    long prev, next;
    if (vi >= (double)(N - 1)) prev = next = N - 1;
    else if (vi < 0) prev = next = 0;
    else prev = (long)floor(vi), next = prev + 1;
    double gamma = vi - (double)(long)floor(vi < 0 ? 0 : vi);
    if (vi >= (double)(N - 1)) gamma = vi - (double)(N - 1); /* (previous set to -1: the last value either way) */
    double a = s[prev], b = s[next], diff = b - a, p90;
    if (gamma >= 0.5) p90 = b - diff * (1 - gamma);
    else p90 = a + diff * gamma;
    free(s);
    GC.thr = p90 > 8.0 ? p90 : 8.0;
}

/* ------------------------------------------------------------ discriminators */

struct facts {
    double mean_delta[3], sigma[3], sel, sel_tol;
    int shift[2];
    double shift_mean, zero_shift_mean, edge_frac, bias, bias_sigma, sky_align, hole, fill, corr, lum_std[2], clip_frac;
};

static double texel_selection_frac(const struct frame *g, const struct frame *c, const int *ys, const int *xs, long n, int tol)
{
    if (!n) return 0.0;
    long hits = 0;
    for (long i = 0; i < n; ++i) {
        int hit = 0;
        for (int dy = -1; dy <= 1 && !hit; ++dy)
            for (int dx = -1; dx <= 1 && !hit; ++dx) {
                int iy = ys[i] + dy, ix = xs[i] + dx;
                iy = iy < 0 ? 0 : iy > g->h - 1 ? g->h - 1 : iy;
                ix = ix < 0 ? 0 : ix > g->w - 1 ? g->w - 1 : ix;
                int m = 0;
                for (int k = 0; k < 3; ++k) {
                    int d = abs(PX(g, iy, ix, k) - PX(c, ys[i], xs[i], k));
                    if (d > m) m = d;
                }
                hit = m <= tol;
            }
        hits += hit;
    }
    return (double)hits / (double)n;
}

/* the best integer (dy,dx) alignment inside a padded box, and its mean abs; the zero shift's */
static void best_shift(const struct frame *g, const struct frame *c, const int *box, int *bdy, int *bdx, double *bmean, double *zero)
{
    int span = 3, y0 = box[0], x0 = box[1], y1 = box[2], x1 = box[3], h = g->h, w = g->w, p = span + 1;
    int Y0 = y0 - p > 0 ? y0 - p : 0, X0 = x0 - p > 0 ? x0 - p : 0;
    int Y1 = y1 + p + 1 < h ? y1 + p + 1 : h, X1 = x1 + p + 1 < w ? x1 + p + 1 : w;
    int R = Y1 - Y0, C = X1 - X0;
    if (R < 0) R = 0;
    if (C < 0) C = 0;
    int py = (R - 1) / 3, px = (C - 1) / 3;
    if (R - 1 < 0) py = -1; /* floor division of -1 by 3 */
    if (C - 1 < 0) px = -1;
    py = py > 0 ? py : 0, px = px > 0 ? px : 0;
    py = py < p ? py : p, px = px < p ? px : p;
    int ry0 = py, ry1 = R - py == 0 ? R : R - py, rx0 = px, rx1 = C - px == 0 ? C : C - px;
    if (ry1 < ry0) ry1 = ry0;
    if (rx1 < rx0) rx1 = rx0;
    long cnt = (long)(ry1 - ry0) * (rx1 - rx0) * 3;
    int have = 0;
    for (int dy = -span; dy <= span; ++dy)
        for (int dx = -span; dx <= span; ++dx) {
            long long sum = 0;
            for (int i = ry0; i < ry1; ++i)
                for (int j = rx0; j < rx1; ++j) {
                    int si = ((i - dy) % R + R) % R, sj = ((j - dx) % C + C) % C;
                    for (int k = 0; k < 3; ++k) sum += abs(PX(g, Y0 + si, X0 + sj, k) - PX(c, Y0 + i, X0 + j, k));
                }
            double m = cnt ? (double)sum / (double)cnt : NAN;
            if (!have || m < *bmean) *bdy = dy, *bdx = dx, *bmean = m, have = 1;
        }
    long long sum = 0;
    for (int i = ry0; i < ry1; ++i)
        for (int j = rx0; j < rx1; ++j)
            for (int k = 0; k < 3; ++k) sum += abs(PX(g, Y0 + i, X0 + j, k) - PX(c, Y0 + i, X0 + j, k));
    *zero = cnt ? (double)sum / (double)cnt : NAN;
}

static double dot3(const double *a, const double *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static const char *verdict(const struct frame *g, const struct frame *c, const int *ys, const int *xs, long n, const int *box,
                           struct facts *F)
{
    gcache(g);
    memset(F, 0, sizeof *F);
    double mean[3], sigma[3];
    long anyclip = 0;
    char *clip = xm((size_t)(n ? n : 1) * 3);
    for (long i = 0; i < n; ++i) {
        int a = 0;
        for (int k = 0; k < 3; ++k) {
            int gv = PX(g, ys[i], xs[i], k), cv = PX(c, ys[i], xs[i], k);
            clip[i * 3 + k] = gv <= 0 || gv >= 255 || cv <= 0 || cv >= 255;
            a |= clip[i * 3 + k];
        }
        anyclip += a;
    }
    for (int k = 0; k < 3; ++k) {
        long long su = 0, sa = 0;
        long cu = 0;
        for (long i = 0; i < n; ++i) {
            int d = PX(c, ys[i], xs[i], k) - PX(g, ys[i], xs[i], k);
            sa += d;
            if (!clip[i * 3 + k]) su += d, ++cu;
        }
        /* the masked moments where any channel value is unclipped, else the plain ones */
        double m = cu > 0 ? (double)su / (double)cu : (double)sa / (double)n;
        mean[k] = m;
        long cnt = cu > 1 ? cu : n;
        double mm = cu > 1 ? (double)su / (double)cu : (double)sa / (double)n, v = 0;
        for (long i = 0; i < n; ++i) {
            if (cu > 1 && clip[i * 3 + k]) continue;
            double d = (double)(PX(c, ys[i], xs[i], k) - PX(g, ys[i], xs[i], k)) - mm;
            v += d * d;
        }
        sigma[k] = sqrt(v / (double)cnt);
    }
    free(clip);
    double clip_frac = n ? (double)anyclip / (double)n : 0.0;
    double sel = texel_selection_frac(g, c, ys, xs, n, 0), sel_tol = texel_selection_frac(g, c, ys, xs, n, 4);
    int bdy, bdx;
    double bmean, zero;
    best_shift(g, c, box, &bdy, &bdx, &bmean, &zero);
    long eh = 0;
    for (long i = 0; i < n; ++i) eh += GC.mag[(size_t)ys[i] * g->w + xs[i]] > GC.thr;
    double ef = n ? (double)eh / (double)n : 0.0;
    /* content_frac */
    double bias = 0, spread = 0;
    double *gd = xm((size_t)(n ? n : 1) * sizeof *gd), *cd = xm((size_t)(n ? n : 1) * sizeof *cd);
    for (long i = 0; i < n; ++i) {
        gd[i] = ((0.0 + PX(g, ys[i], xs[i], 0)) + PX(g, ys[i], xs[i], 1) + PX(g, ys[i], xs[i], 2)) / 3;
        cd[i] = ((0.0 + PX(c, ys[i], xs[i], 0)) + PX(c, ys[i], xs[i], 1) + PX(c, ys[i], xs[i], 2)) / 3;
    }
    if (n) {
        double *df = xm((size_t)n * sizeof *df), *ad = xm((size_t)n * sizeof *ad);
        for (long i = 0; i < n; ++i) df[i] = gd[i] - cd[i], ad[i] = fabs(df[i]);
        bias = np_mean1(df, n);
        spread = np_std1(ad, n);
        free(df), free(ad);
    }
    /* sky_alignment */
    double sky_al = 0;
    {
        long long sg[3] = {0}, sdl[3] = {0};
        for (long i = 0; i < n; ++i)
            for (int k = 0; k < 3; ++k) {
                sg[k] += PX(g, ys[i], xs[i], k);
                sdl[k] += PX(c, ys[i], xs[i], k) - PX(g, ys[i], xs[i], k);
            }
        double axis[3], d[3];
        for (int k = 0; k < 3; ++k) axis[k] = GC.sky[k] - (double)sg[k] / (double)n, d[k] = (double)sdl[k] / (double)n;
        double na = sqrt(dot3(axis, axis)), nd = sqrt(dot3(d, d));
        if (!(na < 1e-6 || nd < 1e-6)) {
            double u[3], v[3];
            for (int k = 0; k < 3; ++k) u[k] = axis[k] / na, v[k] = d[k] / nd;
            sky_al = dot3(u, v);
        }
    }
    /* sky_coverage */
    double hole = 0, fill = 0;
    if (n) {
        long nh = 0, nf = 0;
        for (long i = 0; i < n; ++i) {
            double dg = 0, dc = 0;
            for (int k = 0; k < 3; ++k) {
                double a = fabs(PX(g, ys[i], xs[i], k) - GC.sky[k]), b = fabs(PX(c, ys[i], xs[i], k) - GC.sky[k]);
                dg = a > dg ? a : dg, dc = b > dc ? b : dc;
            }
            nh += dc < 30 && dg > 60;
            nf += dg < 30 && dc > 60;
        }
        hole = (double)nh / (double)n, fill = (double)nf / (double)n;
    }
    /* structure_corr */
    double corr, gs = n ? np_std1(gd, n) : NAN, cs = n ? np_std1(cd, n) : NAN;
    if (gs < 2 && cs < 2) corr = 1.0;
    else if (gs < 2 || cs < 2) corr = 0.0;
    else {
        double ag = np_mean1(gd, n), ac = np_mean1(cd, n), sgg = 0, scc = 0, sgc = 0;
        for (long i = 0; i < n; ++i) {
            double a = gd[i] - ag, b = cd[i] - ac;
            sgg += a * a, scc += b * b, sgc += a * b;
        }
        double f = 1.0 / (double)(n - 1);
        sgg *= f, scc *= f, sgc *= f;
        corr = sgc / sqrt(sgg) / sqrt(scc);
        if (corr > 1) corr = 1;
        if (corr < -1) corr = -1;
    }
    free(gd), free(cd);
    for (int k = 0; k < 3; ++k) F->mean_delta[k] = r2(mean[k], 2), F->sigma[k] = r2(sigma[k], 1);
    F->sel = r2(sel, 3), F->sel_tol = r2(sel_tol, 3);
    F->shift[0] = bdy, F->shift[1] = bdx;
    F->shift_mean = r2(bmean, 2), F->zero_shift_mean = r2(zero, 2), F->edge_frac = r2(ef, 3);
    F->bias = r2(bias, 2), F->bias_sigma = r2(spread, 1), F->sky_align = r2(sky_al, 3);
    F->hole = r2(hole, 3), F->fill = r2(fill, 3), F->corr = r2(corr, 3);
    F->lum_std[0] = r2(gs, 1), F->lum_std[1] = r2(cs, 1), F->clip_frac = r2(clip_frac, 3);
    double am = 0, sm = sigma[0];
    for (int k = 0; k < 3; ++k) am = fabs(mean[k]) > am ? fabs(mean[k]) : am, sm = sigma[k] > sm ? sigma[k] : sm;
    int biased = am > 0.6 * sm && am > 3;
    int local = (double)(box[2] - box[0] + 1) * (box[3] - box[1] + 1) < 0.05 * g->h * g->w;
    if ((bdy || bdx) && bmean < 0.6 * zero)
        return local && (abs(bdy) > abs(bdx) ? abs(bdy) : abs(bdx)) <= 1 && sel_tol > 0.55 ? "texel-selection" : "registration";
    if ((sel > 0.55 || sel_tol > 0.75) && !biased) return "texel-selection";
    if (biased && sm < 12) return corr > 0.5 ? "shading-offset" : "content";
    if ((hole > fill ? hole : fill) > 0.15 && fabs(sky_al) > 0.9) return hole >= fill ? "cutout-sky+" : "cutout-sky-";
    if ((fabs(bias) > 40 && spread > 25) || (am > 40 && corr <= 0.5)) return "content";
    if (ef > 0.6) return "edge";
    return "unresolved";
}

/* ------------------------------------------------------------ clusters */

struct child {
    char cause[24];
    double frac;
};

struct cluster {
    long px;
    int box[4];
    const char *cause;
    struct facts f;
    int nchild;
    struct child child[8];
};

static int maxdiff(const struct frame *g, const struct frame *c, int y, int x)
{
    int m = 0;
    for (int k = 0; k < 3; ++k) {
        int d = abs(PX(c, y, x, k) - PX(g, y, x, k));
        m = d > m ? d : m;
    }
    return m;
}

/* the connected components (8-connectivity) of the max-channel diff over
 * thresh, labelled in raster order, each with its cause; sorted by size */
static struct cluster *cluster_list(const struct frame *g, const struct frame *c, int thresh, int min_px, int *ncl)
{
    int H = g->h, W = g->w;
    long N = (long)H * W;
    int *lab = xcalloc((size_t)N, sizeof *lab);
    int nl = 0;
    long *stack = xm((size_t)N * sizeof *stack);
    for (long i = 0; i < N; ++i) {
        if (lab[i] || maxdiff(g, c, (int)(i / W), (int)(i % W)) <= thresh) continue;
        lab[i] = ++nl;
        long sp = 0;
        stack[sp++] = i;
        while (sp) {
            long q = stack[--sp];
            int y = (int)(q / W), x = (int)(q % W);
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    int yy = y + dy, xx = x + dx;
                    if (yy < 0 || yy >= H || xx < 0 || xx >= W) continue;
                    long r = (long)yy * W + xx;
                    if (lab[r] || maxdiff(g, c, yy, xx) <= thresh) continue;
                    lab[r] = nl;
                    stack[sp++] = r;
                }
        }
    }
    free(stack);
    long *size = xcalloc((size_t)nl + 1, sizeof *size), *start = xcalloc((size_t)nl + 2, sizeof *start);
    for (long i = 0; i < N; ++i) size[lab[i]]++;
    for (int l = 1; l <= nl; ++l) start[l + 1] = start[l] + size[l];
    int *ys = xm((size_t)(N ? N : 1) * sizeof *ys), *xs = xm((size_t)(N ? N : 1) * sizeof *xs);
    long *fillp = xcalloc((size_t)nl + 2, sizeof *fillp);
    for (long i = 0; i < N; ++i)
        if (lab[i]) {
            long at = start[lab[i]] + fillp[lab[i]]++;
            ys[at] = (int)(i / W), xs[at] = (int)(i % W);
        }
    struct cluster *out = xcalloc((size_t)nl + 1, sizeof *out);
    int n = 0;
    for (int l = 1; l <= nl; ++l) {
        if (size[l] < min_px) continue;
        const int *cy = ys + start[l], *cx = xs + start[l];
        struct cluster *k = &out[n++];
        k->px = size[l];
        k->box[0] = k->box[2] = cy[0], k->box[1] = k->box[3] = cx[0];
        for (long i = 0; i < size[l]; ++i) {
            if (cy[i] < k->box[0]) k->box[0] = cy[i];
            if (cy[i] > k->box[2]) k->box[2] = cy[i];
            if (cx[i] < k->box[1]) k->box[1] = cx[i];
            if (cx[i] > k->box[3]) k->box[3] = cx[i];
        }
        k->cause = verdict(g, c, cy, cx, size[l], k->box, &k->f);
    }
    /* sorted by -px, stably */
    for (int a = 1; a < n; ++a) {
        struct cluster x = out[a];
        int b = a - 1;
        while (b >= 0 && out[b].px < x.px) out[b + 1] = out[b], --b;
        out[b + 1] = x;
    }
    free(lab), free(size), free(start), free(fillp), free(ys), free(xs);
    *ncl = n;
    return out;
}

static void refine_unresolved(const struct frame *g, const struct frame *c, struct cluster *r, int thresh)
{
    int y0 = r->box[0], x0 = r->box[1], y1 = r->box[2], x1 = r->box[3];
    int hh = y1 - y0 + 1, ww = x1 - x0 + 1;
    int ny = hh / 40, nx = ww / 40;
    ny = ny < 1 ? 1 : ny > 4 ? 4 : ny, nx = nx < 1 ? 1 : nx > 4 ? 4 : nx;
    if (ny * nx < 2) return;
    struct child cnt[16];
    long cv[16];
    int nc = 0;
    int *ys = xm((size_t)hh * ww * sizeof *ys), *xs = xm((size_t)hh * ww * sizeof *xs);
    for (int iy = 0; iy < ny; ++iy)
        for (int ix = 0; ix < nx; ++ix) {
            int ty0 = y0 + iy * hh / ny, ty1 = y0 + (iy + 1) * hh / ny - 1;
            int tx0 = x0 + ix * ww / nx, tx1 = x0 + (ix + 1) * ww / nx - 1;
            long m = 0;
            for (int y = ty0; y <= ty1; ++y)
                for (int x = tx0; x <= tx1; ++x)
                    if (y >= 0 && y < g->h && x >= 0 && x < g->w && maxdiff(g, c, y, x) >= thresh) ys[m] = y, xs[m] = x, ++m;
            if (m < 30) continue;
            int box[4] = {ty0, tx0, ty1, tx1};
            struct facts F;
            const char *cause = verdict(g, c, ys, xs, m, box, &F);
            int k;
            for (k = 0; k < nc; ++k)
                if (!strcmp(cnt[k].cause, cause)) break;
            if (k == nc) snprintf(cnt[nc].cause, sizeof cnt[nc].cause, "%s", cause), cv[nc++] = 0;
            cv[k] += m;
        }
    free(ys), free(xs);
    if (!nc) return;
    long total = 0;
    for (int k = 0; k < nc; ++k) total += cv[k];
    for (int a = 1; a < nc; ++a) { /* by count, most first, stably */
        struct child x = cnt[a];
        long xv = cv[a];
        int b = a - 1;
        while (b >= 0 && cv[b] < xv) cnt[b + 1] = cnt[b], cv[b + 1] = cv[b], --b;
        cnt[b + 1] = x, cv[b + 1] = xv;
    }
    r->nchild = nc < 8 ? nc : 8;
    for (int k = 0; k < r->nchild; ++k) r->child[k] = cnt[k], r->child[k].frac = r2((double)cv[k] / (double)total, 2);
}

static char *frame_shift_note(const struct cluster *cl, int n)
{
    int sh[64][2], cnt[64], ns = 0, any = 0;
    for (int i = 0; i < n; ++i) {
        const struct facts *f = &cl[i].f;
        if ((f->shift[0] || f->shift[1]) && f->shift_mean < 0.6 * f->zero_shift_mean) {
            any = 1;
            int k;
            for (k = 0; k < ns; ++k)
                if (sh[k][0] == f->shift[0] && sh[k][1] == f->shift[1]) break;
            if (k == ns && ns < 64) sh[ns][0] = f->shift[0], sh[ns][1] = f->shift[1], cnt[ns++] = 0;
            if (k < 64) cnt[k]++;
        }
    }
    if (n >= 3 && any) {
        int best = 0;
        for (int k = 1; k < ns; ++k)
            if (cnt[k] > cnt[best]) best = k;
        if (cnt[best] >= 3 && cnt[best] >= 0.6 * n)
            return xasprintf("frame-level: %d/%d clusters prefer shift (%d, %d) - a whole-frame registration error fragments into local "
                             "texel-selection verdicts; suspect camera/projection before texture sampling",
                             cnt[best], n, sh[best][0], sh[best][1]);
    }
    return NULL;
}

static char *frame_pose_note(const struct cluster *cl, int n, long frame_px)
{
    int big = 0;
    for (int i = 0; i < n; ++i)
        if (cl[i].px > 0.15 * frame_px && !strcmp(cl[i].cause, "unresolved") && cl[i].f.corr <= 0 && !cl[i].f.shift[0] && !cl[i].f.shift[1])
            ++big;
    if (big)
        return xasprintf("frame-level: %d unresolved cluster(s) span >15%% of the frame with structure_corr<=0 and no shift win - the scene "
                         "itself moved. Diff the camera pose first (the tape's x/y/z/yaw/pitch for this tick), then re-triage.",
                         big);
    return NULL;
}

/* ------------------------------------------------------------ output */

static char *fr(double x)
{
    static char b[16][64];
    static int i;
    return py_repr(x, b[i++ & 15]);
}

/* Python's str() of a list of floats */
static char *flist(const double *v, int n)
{
    static char b[8][256];
    static int i;
    char *o = b[i++ & 7];
    int k = snprintf(o, 256, "[");
    for (int j = 0; j < n; ++j) k += snprintf(o + k, 256 - (size_t)k, "%s%s", j ? ", " : "", fr(v[j]));
    snprintf(o + k, 256 - (size_t)k, "]");
    return o;
}

/* a cluster as json.dumps(..., indent=1) writes it at depth d */
static void cluster_json(struct sb *b, const struct cluster *r, int d, const char *extra)
{
    const struct facts *f = &r->f;
    char in[64];
    snprintf(in, sizeof in, "%*s", d + 1, "");
    sb_printf(b, "{\n%s\"px\": %ld,\n%s\"box\": [\n%s %d,\n%s %d,\n%s %d,\n%s %d\n%s],\n", in, r->px, in, in, r->box[0], in, r->box[1], in,
              r->box[2], in, r->box[3], in);
    sb_printf(b, "%s\"cause\": ", in);
    json_esc(b, r->cause);
    sb_puts(b, ",\n");
    sb_printf(b, "%s\"mean_delta\": [\n%s %s,\n%s %s,\n%s %s\n%s],\n", in, in, fr(f->mean_delta[0]), in, fr(f->mean_delta[1]), in,
              fr(f->mean_delta[2]), in);
    sb_printf(b, "%s\"sigma\": [\n%s %s,\n%s %s,\n%s %s\n%s],\n", in, in, fr(f->sigma[0]), in, fr(f->sigma[1]), in, fr(f->sigma[2]), in);
    sb_printf(b, "%s\"texel_selection_frac\": %s,\n%s\"texel_selection_tol4\": %s,\n", in, fr(f->sel), in, fr(f->sel_tol));
    sb_printf(b, "%s\"best_shift\": [\n%s %d,\n%s %d\n%s],\n", in, in, f->shift[0], in, f->shift[1], in);
    sb_printf(b, "%s\"shift_mean\": %s,\n%s\"zero_shift_mean\": %s,\n%s\"edge_frac\": %s,\n", in, fr(f->shift_mean), in, fr(f->zero_shift_mean),
              in, fr(f->edge_frac));
    sb_printf(b, "%s\"bias\": %s,\n%s\"bias_sigma\": %s,\n%s\"sky_align\": %s,\n", in, fr(f->bias), in, fr(f->bias_sigma), in, fr(f->sky_align));
    sb_printf(b, "%s\"sky_hole_frac\": %s,\n%s\"sky_fill_frac\": %s,\n%s\"structure_corr\": %s,\n", in, fr(f->hole), in, fr(f->fill), in,
              fr(f->corr));
    sb_printf(b, "%s\"lum_std\": [\n%s %s,\n%s %s\n%s],\n%s\"clip_frac\": %s", in, in, fr(f->lum_std[0]), in, fr(f->lum_std[1]), in, in,
              fr(f->clip_frac));
    if (r->nchild) {
        sb_printf(b, ",\n%s\"children\": {", in);
        for (int k = 0; k < r->nchild; ++k) {
            sb_printf(b, "%s\n%s ", k ? "," : "", in);
            json_esc(b, r->child[k].cause);
            sb_printf(b, ": %s", fr(r->child[k].frac));
        }
        sb_printf(b, "\n%s}", in);
    }
    if (extra) sb_puts(b, extra);
    sb_printf(b, "\n%*s}", d, "");
}

static void notes_json(struct sb *b, char **notes, int nn, int d)
{
    if (!nn) { sb_puts(b, "[]"); return; }
    sb_putc(b, '[');
    for (int i = 0; i < nn; ++i) {
        sb_printf(b, "%s\n%*s", i ? "," : "", d + 1, "");
        json_esc(b, notes[i]);
    }
    sb_printf(b, "\n%*s]", d, "");
}

/* "{k} {v:.0%}" for each child */
static void kids_line(const struct cluster *r)
{
    printf("           refined (tile majority): ");
    for (int k = 0; k < r->nchild; ++k) printf("%s%s %.0f%%", k ? ", " : "", r->child[k].cause, r->child[k].frac * 100);
    printf("\n");
}

static void put_png(const char *path, int w, int h, const unsigned char *rgb)
{
    if (raster_png(path, w, h, rgb)) die("pxdiff: cannot write %s", path);
}

/* a golden|candidate|heat triptych, scale x, a texel grid */
static unsigned char *render_zoom(const struct frame *g, const struct frame *c, const int *box, int scale, int grid, int pad, int *used, int *ow,
                                  int *oh)
{
    int h = g->h, w = g->w;
    int y0 = box[0] - pad > 0 ? box[0] - pad : 0, x0 = box[1] - pad > 0 ? box[1] - pad : 0;
    int y1 = box[2] + pad < h - 1 ? box[2] + pad : h - 1, x1 = box[3] + pad < w - 1 ? box[3] + pad : w - 1;
    int ch = y1 - y0 + 1, cw = x1 - x0 + 1;
    if (ch <= 0 || cw <= 0) die("pxdiff: the rect is outside the frame");
    int sw = cw * 3 + 4;
    unsigned char *strip = xm((size_t)ch * sw * 3);
    for (int y = 0; y < ch; ++y)
        for (int x = 0; x < sw; ++x) {
            unsigned char *o = strip + ((size_t)y * sw + x) * 3;
            int yy = y0 + y;
            if (x < cw) {
                for (int k = 0; k < 3; ++k) o[k] = (unsigned char)PX(g, yy, x0 + x, k);
            } else if (x < cw + 2) o[0] = o[1] = o[2] = 60;
            else if (x < 2 * cw + 2) {
                for (int k = 0; k < 3; ++k) o[k] = (unsigned char)PX(c, yy, x0 + x - cw - 2, k);
            } else if (x < 2 * cw + 4) o[0] = o[1] = o[2] = 60;
            else {
                int d = maxdiff(g, c, yy, x0 + x - 2 * cw - 4);
                int r = d * 4, gg = d * 4 - 255;
                o[0] = (unsigned char)(r < 0 ? 0 : r > 255 ? 255 : r);
                o[1] = (unsigned char)(gg < 0 ? 0 : gg > 255 ? 255 : gg);
                o[2] = 0;
            }
        }
    int BW = sw * scale, BH = ch * scale;
    unsigned char *big = xm((size_t)BW * BH * 3);
    for (int y = 0; y < BH; ++y)
        for (int x = 0; x < BW; ++x) memcpy(big + ((size_t)y * BW + x) * 3, strip + ((size_t)(y / scale) * sw + x / scale) * 3, 3);
    if (grid && scale >= 4) { /* uint8 + 40 wraps, as numpy's does */
        for (int y = 0; y < BH; y += scale)
            for (int x = 0; x < BW * 3; ++x) big[(size_t)y * BW * 3 + x] = (unsigned char)(big[(size_t)y * BW * 3 + x] + 40);
        for (int y = 0; y < BH; ++y)
            for (int x = 0; x < BW; x += scale)
                for (int k = 0; k < 3; ++k) big[((size_t)y * BW + x) * 3 + k] = (unsigned char)(big[((size_t)y * BW + x) * 3 + k] + 40);
    }
    free(strip);
    used[0] = y0, used[1] = x0, used[2] = y1, used[3] = x1;
    *ow = BW, *oh = BH;
    return big;
}

/* the digits of the overview's labels, 3x5 */
static const char *const DIGIT[10] = {"111101101101111", "010110010010111", "111001111100111", "111001111001111", "101101111001001",
                                      "111100111001111", "111100111101111", "111001001001001", "111101111101111", "111101111001111"};

static void draw_label(unsigned char *rgb, int w, int h, int x, int y, int n)
{
    char s[16];
    snprintf(s, sizeof s, "%d", n);
    for (int i = 0; s[i]; ++i)
        for (int r = 0; r < 5; ++r)
            for (int q = 0; q < 3; ++q) {
                if (DIGIT[s[i] - '0'][r * 3 + q] != '1') continue;
                for (int sy = 0; sy < 2; ++sy)
                    for (int sx = 0; sx < 2; ++sx) {
                        int xx = x + i * 8 + q * 2 + sx, yy = y + r * 2 + sy;
                        if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
                        unsigned char *o = rgb + ((size_t)yy * w + xx) * 3;
                        o[0] = 255, o[1] = 0, o[2] = 255;
                    }
            }
}

/* ------------------------------------------------------------ sources */

static char REPO[4096];

static struct frame load(const char *path)
{
    struct frame f = {0};
    unsigned char *rgba = png_read_rgba(path, &f.w, &f.h);
    if (!rgba) die("pxdiff: cannot read %s (an 8-bit RGB or RGBA PNG)", path);
    f.p = xm((size_t)f.w * f.h * 3 * sizeof *f.p);
    for (size_t i = 0; i < (size_t)f.w * f.h; ++i)
        for (int k = 0; k < 3; ++k) f.p[i * 3 + k] = rgba[i * 4 + k];
    free(rgba);
    return f;
}

static double whole_mean(const struct frame *g, const struct frame *c)
{
    long long s = 0;
    size_t n = (size_t)g->w * g->h * 3;
    for (size_t i = 0; i < n; ++i) s += abs(c->p[i] - g->p[i]);
    return (double)s / (double)n;
}

/* ------------------------------------------------------------ selftest */

typedef unsigned __int128 u128;
struct nprng {
    u128 state, inc;
    int has32;
    uint32_t u32;
};

static uint32_t ss_hashmix(uint32_t v, uint32_t *hc)
{
    v ^= *hc;
    *hc *= 0x931e8875U;
    v *= *hc;
    v ^= v >> 16;
    return v;
}

static uint32_t ss_mix(uint32_t x, uint32_t y)
{
    uint32_t r = 0xca01f9ddU * x - 0x4973f715U * y;
    r ^= r >> 16;
    return r;
}

/* numpy's default_rng(seed): SeedSequence's state words into PCG64 */
static void np_seed(struct nprng *r, uint32_t seed)
{
    uint32_t pool[4], hc = 0x43b0d7e5U;
    for (int i = 0; i < 4; ++i) pool[i] = ss_hashmix(i < 1 ? seed : 0, &hc);
    for (int s = 0; s < 4; ++s)
        for (int d = 0; d < 4; ++d)
            if (s != d) pool[d] = ss_mix(pool[d], ss_hashmix(pool[s], &hc));
    uint32_t st[8], hb = 0x8b51f9ddU;
    for (int i = 0; i < 8; ++i) {
        uint32_t v = pool[i % 4];
        v ^= hb;
        hb *= 0x58f38dedU;
        v *= hb;
        v ^= v >> 16;
        st[i] = v;
    }
    uint64_t w[4];
    for (int i = 0; i < 4; ++i) w[i] = (uint64_t)st[2 * i] | (uint64_t)st[2 * i + 1] << 32;
    u128 initstate = (u128)w[0] << 64 | w[1], initseq = (u128)w[2] << 64 | w[3];
    const u128 mult = (u128)0x2360ED051FC65DA4ULL << 64 | 0x4385DF649FCCF645ULL;
    r->state = 0;
    r->inc = initseq << 1 | 1;
    r->state = r->state * mult + r->inc;
    r->state += initstate;
    r->state = r->state * mult + r->inc;
    r->has32 = 0;
}

static uint64_t np_next64(struct nprng *r)
{
    const u128 mult = (u128)0x2360ED051FC65DA4ULL << 64 | 0x4385DF649FCCF645ULL;
    r->state = r->state * mult + r->inc;
    uint64_t hi = (uint64_t)(r->state >> 64), lo = (uint64_t)r->state, x = hi ^ lo;
    unsigned rot = (unsigned)(hi >> 58);
    return (x >> rot) | (x << ((64 - rot) & 63));
}

static uint32_t np_next32(struct nprng *r)
{
    if (r->has32) {
        r->has32 = 0;
        return r->u32;
    }
    uint64_t n = np_next64(r);
    r->has32 = 1;
    r->u32 = (uint32_t)(n >> 32);
    return (uint32_t)n;
}

static double np_random(struct nprng *r) { return (double)(np_next64(r) >> 11) * (1.0 / 9007199254740992.0); }

/* integers(low, high): Lemire's bounded draw on 32 bits */
static int np_integer(struct nprng *r, int low, int high)
{
    uint32_t rng = (uint32_t)(high - 1 - low), excl = rng + 1;
    uint64_t m = (uint64_t)np_next32(r) * excl;
    uint32_t left = (uint32_t)m;
    if (left < excl) {
        uint32_t th = (uint32_t)(0xFFFFFFFFU - rng) % excl;
        while (left < th) {
            m = (uint64_t)np_next32(r) * excl;
            left = (uint32_t)m;
        }
    }
    return low + (int)(m >> 32);
}

static struct frame fnew(int h, int w)
{
    struct frame f = {h, w, xcalloc((size_t)h * w * 3, sizeof(short))};
    return f;
}

static struct frame fcopy(const struct frame *a)
{
    struct frame f = fnew(a->h, a->w);
    memcpy(f.p, a->p, (size_t)a->h * a->w * 3 * sizeof(short));
    return f;
}

static int selftest(void)
{
    struct nprng R;
    np_seed(&R, 7);
    int H = 120, W = 160;
    struct frame base = fnew(H, W);
    for (int i = 0; i < H * W * 3; ++i) base.p[i] = (short)np_integer(&R, 40, 200);
    struct frame tex = fcopy(&base);
    int *ys = xm((size_t)H * W * sizeof *ys), *xs = xm((size_t)H * W * sizeof *xs), n = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (np_random(&R) < 0.35) ys[n] = y, xs[n] = x, ++n;
    int k = 0;
    for (int i = 0; i < n; ++i)
        if (ys[i] > 1 && ys[i] < 118 && xs[i] > 1 && xs[i] < 158) ys[k] = ys[i], xs[k] = xs[i], ++k;
    n = k;
    int *dy = xm((size_t)(n ? n : 1) * sizeof *dy), *dx = xm((size_t)(n ? n : 1) * sizeof *dx);
    for (int i = 0; i < n; ++i) dy[i] = np_integer(&R, -1, 2);
    for (int i = 0; i < n; ++i) dx[i] = np_integer(&R, -1, 2);
    for (int i = 0; i < n; ++i)
        for (int c = 0; c < 3; ++c) PX(&tex, ys[i], xs[i], c) = PX(&base, ys[i] + dy[i], xs[i] + dx[i], c);
    struct frame sha = fcopy(&base);
    for (int y = 20; y < 70; ++y)
        for (int x = 30; x < 130; ++x)
            for (int c = 0; c < 3; ++c) {
                int v = PX(&sha, y, x, c) + 30;
                PX(&sha, y, x, c) = (short)(v > 255 ? 255 : v);
            }
    struct frame reg = fnew(H, W);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            for (int c = 0; c < 3; ++c) PX(&reg, y, x, c) = PX(&base, y, (x - 1 + W) % W, c);
    struct frame con = fcopy(&base);
    for (int y = 40; y < 80; ++y)
        for (int x = 40; x < 90; ++x)
            for (int c = 0; c < 3; ++c) PX(&con, y, x, c) = 250;
    struct frame flat = fcopy(&base);
    for (int y = 40; y < 80; ++y)
        for (int x = 40; x < 90; ++x)
            for (int c = 0; c < 3; ++c) PX(&flat, y, x, c) = (short)(100 + np_integer(&R, -8, 9));
    struct frame conf = fcopy(&flat);
    for (int y = 40; y < 80; ++y)
        for (int x = 40; x < 90; ++x)
            for (int c = 0; c < 3; ++c) PX(&conf, y, x, c) = 180;
    struct frame texn = fcopy(&tex);
    for (int i = 0; i < n; ++i)
        for (int c = 0; c < 3; ++c) {
            int v = PX(&texn, ys[i], xs[i], c) + 2;
            PX(&texn, ys[i], xs[i], c) = (short)(v > 255 ? 255 : v);
        }
    static const short SKY[3] = {140, 180, 255};
    struct frame cut = fcopy(&base);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < W; ++x)
            for (int c = 0; c < 3; ++c) PX(&cut, y, x, c) = SKY[c];
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (np_random(&R) < 0.6 && y > 50 && y < 90 && x > 30 && x < 120)
                for (int c = 0; c < 3; ++c) PX(&cut, y, x, c) = SKY[c];
    struct frame basec = fcopy(&base);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < W; ++x)
            for (int c = 0; c < 3; ++c) PX(&basec, y, x, c) = SKY[c];
    struct {
        const char *want;
        struct frame *cand, *ref;
    } cases[] = {{"texel-selection", &tex, &base}, {"texel-selection", &texn, &base}, {"shading-offset", &sha, &base},
                 {"registration", &reg, &base},     {"content", &con, &base},          {"content", &conf, &flat},
                 {"cutout-sky+", &cut, &basec}};
    int ok = 1;
    for (size_t i = 0; i < sizeof cases / sizeof *cases; ++i) {
        int nc;
        struct cluster *cl = cluster_list(cases[i].ref, cases[i].cand, DIFF_THRESH, MIN_CLUSTER, &nc);
        const char *got = nc ? cl[0].cause : "no-cluster";
        int good = !strcmp(got, cases[i].want);
        ok &= good;
        char sel[64], shift[64];
        if (nc) snprintf(sel, sizeof sel, "%s", fr(cl[0].f.sel)), snprintf(shift, sizeof shift, "[%d, %d]", cl[0].f.shift[0], cl[0].f.shift[1]);
        else strcpy(sel, "-"), strcpy(shift, "-");
        printf("  %s %-16s -> %-16s px=%ld sel=%s shift=%s\n", good ? "ok " : "FAIL", cases[i].want, got, nc ? cl[0].px : 0L, sel, shift);
        free(cl);
    }
    struct frame same = fcopy(&base);
    int nc;
    struct cluster *cl = cluster_list(&base, &same, DIFF_THRESH, MIN_CLUSTER, &nc);
    if (nc) {
        printf("  FAIL identical pair produced clusters\n");
        ok = 0;
    } else printf("  ok  identical         -> no clusters\n");
    free(cl);
    printf("selftest: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

/* ------------------------------------------------------------ main */

struct args {
    const char *cmd, *a, *b, *tape, *dir_a, *dir_b, *at, *size, *rect, *out;
    int has_tick, tick, has_cluster, cluster, scale, pad, no_grid, thresh, min_px, top, json;
};

static void usage(void)
{
    fputs("usage: pxdiff {clusters,zoom,pixels,probe,frames,survey,stats,selftest} [--a A] [--b B] [--tape T] [--tick N]\n"
          "  [--dir-a D] [--dir-b D] [--cluster N] [--at X,Y] [--size WxH] [--rect X0,Y0,X1,Y1] [--scale 8] [--pad 4]\n"
          "  [--no-grid] [--thresh 25] [--min-px 50] [--top 12] [-o OUT] [--json]\n",
          stderr);
    exit(2);
}

static int find_repo(const char *argv0)
{
    char cwd[4096];
    if (getcwd(cwd, sizeof cwd))
        for (;;) {
            char *a = xasprintf("%s/tools/pxdiff.c", cwd);
            int ok = is_file(a);
            free(a);
            if (ok) { snprintf(REPO, sizeof REPO, "%s", cwd); return 1; }
            char *s = strrchr(cwd, '/');
            if (!s || s == cwd) break;
            *s = 0;
        }
    char rp[4096];
    if (strchr(argv0, '/') && realpath(argv0, rp)) {
        for (int up = 0; up < 3; ++up) {
            char *s = strrchr(rp, '/');
            if (!s) return 0;
            *s = 0;
        }
        snprintf(REPO, sizeof REPO, "%s", rp);
        return 1;
    }
    return 0;
}

static int parse_int(const char *s, const char *what)
{
    char *e;
    long v = strtol(s, &e, 10);
    if (e == s || *e) {
        fprintf(stderr, "pxdiff: argument %s: invalid int value: '%s'\n", what, s);
        exit(2);
    }
    return (int)v;
}

/* the pair, its label */
static char *load_pair(const struct args *A, struct frame *g, struct frame *c)
{
    if (A->a && A->b && *A->a && *A->b) {
        *g = load(A->a), *c = load(A->b);
        if (g->h != c->h || g->w != c->w) die("shape mismatch: (%d, %d, 3) vs (%d, %d, 3)", g->h, g->w, c->h, c->w);
        return xasprintf("%s vs %s", base_name(A->a), base_name(A->b));
    }
    if (!(A->tape && *A->tape && A->has_tick)) die("need --a/--b or --tape/--tick");
    char *gp = xasprintf("%s/out/java/frames/%s/f_%06d.png", REPO, A->tape, A->tick);
    char *cp = xasprintf("%s/out/native/frames/%s/f_%06d.png", REPO, A->tape, A->tick);
    if (!path_exists(gp)) die("no golden frame: %s", gp);
    if (!path_exists(cp)) die("no candidate frame: %s (native pixel path not built yet)", cp);
    *g = load(gp), *c = load(cp);
    return xasprintf("%s t=%d", A->tape, A->tick);
}

static void resolve_box(const struct args *A, const struct cluster *cl, int ncl, int *box)
{
    if (A->at && *A->at) {
        int cx, cy, bw, bh;
        if (sscanf(A->at, "%d,%d", &cx, &cy) != 2) die("pxdiff: --at wants X,Y");
        char *s = xstrdup(A->size);
        for (char *p = s; *p; ++p) *p = (char)(*p >= 'A' && *p <= 'Z' ? *p + 32 : *p);
        if (sscanf(s, "%dx%d", &bw, &bh) != 2) die("pxdiff: --size wants WxH");
        /* Python's floor division */
        int hb = bh >= 0 ? bh / 2 : -((-bh + 1) / 2), wb = bw >= 0 ? bw / 2 : -((-bw + 1) / 2);
        box[0] = cy - hb, box[1] = cx - wb, box[2] = cy + hb, box[3] = cx + wb;
        return;
    }
    if (A->has_cluster) {
        if (A->cluster >= ncl) die("only %d clusters", ncl);
        int i = A->cluster < 0 ? A->cluster + ncl : A->cluster;
        if (i < 0) die("pxdiff: no cluster %d", A->cluster);
        memcpy(box, cl[i].box, sizeof cl[i].box);
        return;
    }
    die("need --cluster N or --at X,Y");
}

int main(int argc, char **argv)
{
    struct args A = {0};
    A.size = "48x32", A.out = "/tmp/pxdiff.png", A.scale = 8, A.pad = 4, A.thresh = DIFF_THRESH, A.min_px = MIN_CLUSTER, A.top = 12;
    static const char *const CMDS[] = {"clusters", "zoom", "pixels", "probe", "frames", "survey", "stats", "selftest", NULL};
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i], *eq = strchr(a, '=');
        char name[64];
        snprintf(name, sizeof name, "%.*s", a[0] == '-' && eq ? (int)(eq - a) : (int)strlen(a), a);
        if (a[0] != '-') {
            if (A.cmd) usage();
            int ok = 0;
            for (int k = 0; CMDS[k]; ++k) ok |= !strcmp(CMDS[k], a);
            if (!ok) usage();
            A.cmd = a;
            continue;
        }
        if (!strcmp(name, "--no-grid")) { A.no_grid = 1; continue; }
        if (!strcmp(name, "--json")) { A.json = 1; continue; }
        if (!strcmp(name, "-h") || !strcmp(name, "--help")) usage();
        const char *v = eq ? eq + 1 : i + 1 < argc ? argv[++i] : NULL;
        if (!v) usage();
        if (!strcmp(name, "--a")) A.a = v;
        else if (!strcmp(name, "--b")) A.b = v;
        else if (!strcmp(name, "--tape")) A.tape = v;
        else if (!strcmp(name, "--tick")) A.tick = parse_int(v, "--tick"), A.has_tick = 1;
        else if (!strcmp(name, "--dir-a")) A.dir_a = v;
        else if (!strcmp(name, "--dir-b")) A.dir_b = v;
        else if (!strcmp(name, "--cluster")) A.cluster = parse_int(v, "--cluster"), A.has_cluster = 1;
        else if (!strcmp(name, "--at")) A.at = v;
        else if (!strcmp(name, "--size")) A.size = v;
        else if (!strcmp(name, "--rect")) A.rect = v;
        else if (!strcmp(name, "--scale")) A.scale = parse_int(v, "--scale");
        else if (!strcmp(name, "--pad")) A.pad = parse_int(v, "--pad");
        else if (!strcmp(name, "--thresh")) A.thresh = parse_int(v, "--thresh");
        else if (!strcmp(name, "--min-px")) A.min_px = parse_int(v, "--min-px");
        else if (!strcmp(name, "--top")) A.top = parse_int(v, "--top");
        else if (!strcmp(name, "-o") || !strcmp(name, "--out")) A.out = v;
        else usage();
    }
    if (!A.cmd) usage();
    if (!strcmp(A.cmd, "selftest")) return selftest();
    find_repo(argv[0]);

    if (!strcmp(A.cmd, "frames")) {
        if (!(A.dir_a && *A.dir_a && A.dir_b && *A.dir_b)) die("frames needs --dir-a GOLDEN_DIR --dir-b CANDIDATE_DIR");
        struct sv all = list_dir(A.dir_a, 1), names = {0};
        for (int i = 0; i < all.n; ++i) {
            char *pb = path_join(A.dir_b, all.v[i]);
            if (ends_with(all.v[i], ".png") && path_exists(pb)) sv_push(&names, all.v[i]);
            free(pb);
        }
        struct row {
            char *f;
            long px;
            double mean;
            const char *cause;
        } *rows = xcalloc((size_t)names.n + 1, sizeof *rows);
        int nr = 0;
        for (int i = 0; i < names.n; ++i) {
            char *pa = path_join(A.dir_a, names.v[i]), *pb = path_join(A.dir_b, names.v[i]);
            struct frame g = load(pa), c = load(pb);
            if (g.h != c.h || g.w != c.w) continue;
            int nc;
            struct cluster *cl = cluster_list(&g, &c, A.thresh, A.min_px, &nc);
            long s = 0;
            for (int k = 0; k < nc; ++k) s += cl[k].px;
            rows[nr++] = (struct row){names.v[i], s, whole_mean(&g, &c), nc ? cl[0].cause : "-"};
            free(cl);
            free(g.p), free(c.p);
            GC.g = NULL;
        }
        for (int a = 1; a < nr; ++a) {
            struct row x = rows[a];
            int b = a - 1;
            while (b >= 0 && rows[b].px < x.px) rows[b + 1] = rows[b], --b;
            rows[b + 1] = x;
        }
        if (A.json) {
            struct sb b = {0};
            if (!nr) sb_puts(&b, "[]");
            else {
                sb_putc(&b, '[');
                for (int i = 0; i < nr; ++i) {
                    sb_printf(&b, "%s\n {\n  \"frame\": ", i ? "," : "");
                    json_esc(&b, rows[i].f);
                    sb_printf(&b, ",\n  \"cluster_px\": %ld,\n  \"mean_abs\": %s,\n  \"top_cause\": ", rows[i].px, fr(rows[i].mean));
                    json_esc(&b, rows[i].cause);
                    sb_puts(&b, "\n }");
                }
                sb_puts(&b, "\n]");
            }
            printf("%s\n", b.s);
        } else {
            printf("%20s %9s %8s  top cause\n", "frame", "clust_px", "mean/ch");
            for (int i = 0; i < nr && i < (A.top > 0 ? A.top : 0); ++i) printf("%20s %9ld %8.2f  %s\n", rows[i].f, rows[i].px, rows[i].mean, rows[i].cause);
        }
        return 0;
    }

    struct frame g, c;
    char *label = load_pair(&A, &g, &c);
    if (!strcmp(A.cmd, "stats")) {
        int x0 = 0, y0 = 0, x1 = g.w, y1 = g.h;
        if (A.rect && *A.rect) {
            if (sscanf(A.rect, "%d,%d,%d,%d", &x0, &y0, &x1, &y1) != 4) die("pxdiff: --rect wants X0,Y0,X1,Y1");
            if (!(0 <= x0 && x0 < x1 && x1 <= g.w && 0 <= y0 && y0 < y1 && y1 <= g.h)) die("invalid --rect bounds");
        }
        long long s[3] = {0};
        int mx[3] = {0};
        long exact = 0;
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                int all0 = 1;
                for (int k = 0; k < 3; ++k) {
                    int d = abs(PX(&c, y, x, k) - PX(&g, y, x, k));
                    s[k] += d;
                    mx[k] = d > mx[k] ? d : mx[k];
                    all0 &= d == 0;
                }
                exact += all0;
            }
        long np = (long)(y1 - y0) * (x1 - x0);
        struct sb b = {0};
        sb_puts(&b, "{\n \"source\": ");
        json_esc(&b, label);
        sb_puts(&b, ",\n \"rect\": ");
        json_esc(&b, A.rect && *A.rect ? A.rect : "whole frame");
        sb_printf(&b, ",\n \"pixels\": %ld,\n \"mean_abs_rgb\": [\n  %s,\n  %s,\n  %s\n ],\n \"max_abs_rgb\": [\n  %d,\n  %d,\n  %d\n ],\n \"exact_pixels\": %ld\n}",
                  np, fr(r2((double)s[0] / np, 6)), fr(r2((double)s[1] / np, 6)), fr(r2((double)s[2] / np, 6)), mx[0], mx[1], mx[2], exact);
        printf("%s\n", b.s);
        return 0;
    }
    int ncl;
    struct cluster *cl = cluster_list(&g, &c, A.thresh, A.min_px, &ncl);
    if (!strcmp(A.cmd, "clusters") || !strcmp(A.cmd, "survey"))
        for (int i = 0; i < ncl; ++i)
            if (!strcmp(cl[i].cause, "unresolved") && cl[i].px >= 4L * A.min_px) refine_unresolved(&g, &c, &cl[i], A.thresh);
    char *notes[2];
    int nn = 0;
    char *n1 = frame_shift_note(cl, ncl), *n2 = frame_pose_note(cl, ncl, (long)g.h * g.w);
    if (n1) notes[nn++] = n1;
    if (n2) notes[nn++] = n2;

    if (!strcmp(A.cmd, "clusters")) {
        if (A.json) {
            struct sb b = {0};
            sb_puts(&b, "{\n \"source\": ");
            json_esc(&b, label);
            sb_puts(&b, ",\n \"clusters\": ");
            if (!ncl) sb_puts(&b, "[]");
            else {
                sb_putc(&b, '[');
                for (int i = 0; i < ncl; ++i) {
                    sb_printf(&b, "%s\n  ", i ? "," : "");
                    cluster_json(&b, &cl[i], 2, NULL);
                }
                sb_puts(&b, "\n ]");
            }
            sb_puts(&b, ",\n \"frame_notes\": ");
            notes_json(&b, notes, nn, 1);
            sb_puts(&b, "\n}");
            printf("%s\n", b.s);
            return 0;
        }
        printf("%s: %d clusters >= %dpx at thresh %d, whole frame mean %.3f/ch\n", label, ncl, A.min_px, A.thresh, whole_mean(&g, &c));
        printf("%3s %6s  %-22s %-16s sel  tol4  shift  mean_delta\n", "#", "px", "bbox y0,x0,y1,x1", "cause");
        for (int i = 0; i < ncl; ++i) {
            char bb[96], sh[32];
            snprintf(bb, sizeof bb, "%d,%d,%d,%d", cl[i].box[0], cl[i].box[1], cl[i].box[2], cl[i].box[3]);
            snprintf(sh, sizeof sh, "(%d, %d)", cl[i].f.shift[0], cl[i].f.shift[1]);
            printf("%3d %6ld  %-22s %-16s %.2f %.2f %7s %s\n", i, cl[i].px, bb, cl[i].cause, cl[i].f.sel, cl[i].f.sel_tol, sh,
                   flist(cl[i].f.mean_delta, 3));
            if (cl[i].nchild) kids_line(&cl[i]);
        }
        for (int i = 0; i < nn; ++i) printf("%s\n", notes[i]);
        return 0;
    }

    if (!strcmp(A.cmd, "survey")) {
        char *outdir = ends_with(A.out, ".png") ? xasprintf("%.*s_survey", (int)strlen(A.out) - 4, A.out) : xstrdup(A.out);
        mkdirs(outdir);
        int k = ncl < A.top ? ncl : A.top;
        if (k > 5) k = 5;
        if (k < 0) k = 0;
        unsigned char *over = xm((size_t)g.w * g.h * 3);
        for (size_t i = 0; i < (size_t)g.w * g.h * 3; ++i) over[i] = (unsigned char)c.p[i];
        for (int i = 0; i < k; ++i) {
            int x0 = cl[i].box[1] - 1, y0 = cl[i].box[0] - 1, x1 = cl[i].box[3] + 1, y1 = cl[i].box[2] + 1;
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    if (!(y == y0 || y == y1 || x == x0 || x == x1)) continue;
                    if (x < 0 || y < 0 || x >= g.w || y >= g.h) continue;
                    unsigned char *o = over + ((size_t)y * g.w + x) * 3;
                    o[0] = 255, o[1] = 0, o[2] = 255;
                }
            draw_label(over, g.w, g.h, cl[i].box[1] + 2, cl[i].box[0] - 11 > 0 ? cl[i].box[0] - 11 : 0, i);
        }
        char *over_path = path_join(outdir, "overview.png");
        put_png(over_path, g.w, g.h, over);
        struct sb b = {0};
        sb_puts(&b, "{\n \"source\": ");
        json_esc(&b, label);
        sb_puts(&b, ",\n \"frame_notes\": ");
        notes_json(&b, notes, nn, 1);
        sb_puts(&b, ",\n \"clusters\": ");
        char **zp = xcalloc((size_t)k + 1, sizeof *zp);
        if (!k) sb_puts(&b, "[]");
        else sb_putc(&b, '[');
        for (int i = 0; i < k; ++i) {
            int cw = (cl[i].box[3] - cl[i].box[1] + 1 + 2 * A.pad) * 3 + 4;
            int scale = 1200 / (cw > 1 ? cw : 1);
            /* Python's // floors: a negative width cannot happen for a cluster's box */
            scale = scale > 12 ? 12 : scale, scale = scale < 2 ? 2 : scale;
            int used[4], ow, oh;
            unsigned char *big = render_zoom(&g, &c, cl[i].box, scale, !A.no_grid, A.pad, used, &ow, &oh);
            char zn[32];
            snprintf(zn, sizeof zn, "zoom_%d.png", i);
            zp[i] = path_join(outdir, zn);
            put_png(zp[i], ow, oh, big);
            free(big);
            struct sb ex = {0};
            sb_printf(&ex, ",\n   \"zoom\": ");
            json_esc(&ex, zp[i]);
            sb_printf(&ex, ",\n   \"zoom_scale\": %d,\n   \"zoom_rect\": [\n    %d,\n    %d,\n    %d,\n    %d\n   ]", scale, used[0], used[1], used[2],
                      used[3]);
            sb_printf(&b, "%s\n  ", i ? "," : "");
            cluster_json(&b, &cl[i], 2, ex.s);
            sb_free(&ex);
        }
        if (k) sb_puts(&b, "\n ]");
        sb_puts(&b, "\n}");
        char *sj = path_join(outdir, "survey.json");
        if (write_file(sj, b.s, b.n)) die("pxdiff: cannot write %s", sj);
        printf("%s: top %d of %d clusters -> %s\n", label, k, ncl, outdir);
        printf("%3s %6s  %-22s %-16s zoom\n", "#", "px", "bbox y0,x0,y1,x1", "cause");
        for (int i = 0; i < k; ++i) {
            char bb[96];
            snprintf(bb, sizeof bb, "%d,%d,%d,%d", cl[i].box[0], cl[i].box[1], cl[i].box[2], cl[i].box[3]);
            printf("%3d %6ld  %-22s %-16s %s\n", i, cl[i].px, bb, cl[i].cause, zp[i]);
            if (cl[i].nchild) kids_line(&cl[i]);
        }
        for (int i = 0; i < nn; ++i) printf("%s\n", notes[i]);
        printf("overview (numbered boxes): %s\n", over_path);
        return 0;
    }

    int box[4];
    resolve_box(&A, cl, ncl, box);

    if (!strcmp(A.cmd, "zoom")) {
        int used[4], ow, oh;
        unsigned char *big = render_zoom(&g, &c, box, A.scale, !A.no_grid, A.pad, used, &ow, &oh);
        put_png(A.out, ow, oh, big);
        printf("%s: golden | candidate | heat  rect y[%d,%d] x[%d,%d] at %dx -> %s\n", label, used[0], used[2], used[1], used[3], A.scale, A.out);
        return 0;
    }

    if (!strcmp(A.cmd, "pixels")) {
        int y0 = box[0], x0 = box[1], y1 = box[2], x1 = box[3], n = 0;
        for (int y = y0 > 0 ? y0 : 0; y < (g.h < y1 + 1 ? g.h : y1 + 1); ++y)
            for (int x = x0 > 0 ? x0 : 0; x < (g.w < x1 + 1 ? g.w : x1 + 1); ++x) {
                int gv[3], cv[3], same = 1;
                for (int k = 0; k < 3; ++k) gv[k] = PX(&g, y, x, k), cv[k] = PX(&c, y, x, k), same &= gv[k] == cv[k];
                if (same) continue;
                char a[64], b2[64];
                snprintf(a, sizeof a, "(%d, %d, %d)", gv[0], gv[1], gv[2]);
                snprintf(b2, sizeof b2, "(%d, %d, %d)", cv[0], cv[1], cv[2]);
                printf("(%4d,%4d) golden %18s  cand %18s  d (%d, %d, %d)\n", x, y, a, b2, cv[0] - gv[0], cv[1] - gv[1], cv[2] - gv[2]);
                if (++n >= 400) {
                    printf("... truncated at 400 differing pixels\n");
                    return 0;
                }
            }
        printf("%d differing pixels in rect\n", n);
        return 0;
    }

    if (!strcmp(A.cmd, "probe")) {
        int y0 = box[0], x0 = box[1], y1 = box[2], x1 = box[3];
        long N = (long)g.h * g.w, n = 0;
        int *ys = xm((size_t)N * sizeof *ys), *xs = xm((size_t)N * sizeof *xs);
        for (int y = y0 > 0 ? y0 : 0; y < (g.h < y1 + 1 ? g.h : y1 + 1); ++y)
            for (int x = x0 > 0 ? x0 : 0; x < (g.w < x1 + 1 ? g.w : x1 + 1); ++x)
                if (maxdiff(&g, &c, y, x) >= A.thresh) ys[n] = y, xs[n] = x, ++n;
        if (!n) {
            printf("no differing pixels in rect\n");
            return 0;
        }
        struct facts F;
        const char *cause = verdict(&g, &c, ys, xs, n, box, &F);
        printf("%s  rect y[%d,%d] x[%d,%d]  %ld differing px\n", label, y0, y1, x0, x1, n);
        printf("  cause: %s\n", cause);
        printf("    %-22s %s\n", "mean_delta", flist(F.mean_delta, 3));
        printf("    %-22s %s\n", "sigma", flist(F.sigma, 3));
        printf("    %-22s %s\n", "texel_selection_frac", fr(F.sel));
        printf("    %-22s %s\n", "texel_selection_tol4", fr(F.sel_tol));
        printf("    %-22s [%d, %d]\n", "best_shift", F.shift[0], F.shift[1]);
        printf("    %-22s %s\n", "shift_mean", fr(F.shift_mean));
        printf("    %-22s %s\n", "zero_shift_mean", fr(F.zero_shift_mean));
        printf("    %-22s %s\n", "edge_frac", fr(F.edge_frac));
        printf("    %-22s %s\n", "bias", fr(F.bias));
        printf("    %-22s %s\n", "bias_sigma", fr(F.bias_sigma));
        printf("    %-22s %s\n", "sky_align", fr(F.sky_align));
        printf("    %-22s %s\n", "sky_hole_frac", fr(F.hole));
        printf("    %-22s %s\n", "sky_fill_frac", fr(F.fill));
        printf("    %-22s %s\n", "structure_corr", fr(F.corr));
        printf("    %-22s %s\n", "lum_std", flist(F.lum_std, 2));
        printf("    %-22s %s\n", "clip_frac", fr(F.clip_frac));
        return 0;
    }
    return 0;
}
