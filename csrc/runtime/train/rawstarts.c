/* rawstarts (lane/rawskill): the starts of the raw-controls mining task, made from the seed set's spawns (the
 * oracle's fresh world of each seed, out/java/try/seedset-KEY/sN, captured once, lane/seeds). Each start is a dev
 * start (rlgui.h NETH_RL_DEV): its directory links every file of the seed's snapshot but tape.jsonl, whose header gains
 * "dev":true and a setup the engine applies at the snapshot's tick (csrc/engine/dev.h; the oracle's script takes the
 * same Dev lines after its Snapshot line, so an episode from the start replays on the Java client):
 *   give    a stone pickaxe (hotbar slot 0, the one in hand)
 *   setblock the task's ore on the ground in front of the player: a cell of air with solid ground under it, at the
 *           feet's level or one above or below, RS_DMIN to RS_DMAX blocks away (horizontally, from the feet), the line
 *           from the eye to its middle through air alone, and the walk to it on solid ground with no step over one
 *           block and no liquid
 *   tp      the player where it stands, looking at the ore's middle, turned by up to --turn degrees of yaw (at least
 *           --turn-min, either side: the ore out of view) and --pitch-noise of pitch (0: straight on); with --cave
 *           the player in a cave near the spawn instead (cave_cell), the ore placed around it the same way, and a
 *           torch on the floor at the player's feet (setblock 50:5; a cave is too dark to see the ore at 16x16;
 *           --no-torch leaves the cave as it is: the torch is a change to the task)
 * The world is read from the snapshot itself (snapshot_load: the chunks the server holds at its tick). A seed with no
 * such cell in RS_TRIES draws is skipped and said so.
 * lane/rawtree: --embed sets the ore into the rock beside that air cell instead of on it (embed_cell: a wall at the
 * feet's or the head's level, or the floor, one face showing toward the eye), the look aimed at the face shown: ore
 * partly hidden, as a cave's own ore is.
 *
 *   rawstarts --set DIR --seeds A-B[,C...] --out LIB [--per K] [--dmin D] [--dmax D] [--turn DEG] [--turn-min DEG]
 *             [--pitch-noise DEG] [--cave [--no-torch]] [--embed] [--ore ID] [--seed S] [--biome ID|NAME]
 *   rawstarts --logs --set DIR --seeds A-B --out LIB [--log-dmax D] [--log-view DEG]   (log_scan: spawns unchanged)
 *   rawstarts --near-block ID:DIST --set DIR --seeds A-B --out LIB [--log-view DEG] [--biome ID|NAME|any]
 *             (lane/nether-polish: --logs for any block within DIST, rows labelled near:ID; nether harvest --env's
 *             spawn mode; --biome keeps a spawn only in that biome, any or absent every one)
 *
 * LIB/sSEED-K/ the starts, LIB/index.tsv the library's index (train/starts.c's columns, stage spawn: ntloop
 * --library LIB, starts.library), its provenance column the placement (d the distance, a the yaw from the look to
 * the ore, the ore's cell); the last line RAWSTARTS and the counts. */
#define _GNU_SOURCE
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>

#include "../../engine/biomes.h"
#include "../../engine/nbtjson.h"
#include "../../engine/snapshot.h"
#include "../../engine/tape.h"
#include "../../engine/world.h"

#define RS_DMIN 2.0         /* the ore's least horizontal distance from the feet (blocks), --dmin */
#define RS_DMAX 4.0         /* and the most, --dmax */
#define RS_TRIES 400        /* draws of a placement before a seed's start is given up */
#define RS_STEP 0.2         /* the walk's and the sight line's samples (blocks) */
#define RS_PICK 274         /* the stone pickaxe */
#define RS_ORE 15           /* iron ore, --ore */

static uint64_t rs_rng;
static double unif(void)
{
    rs_rng ^= rs_rng << 13;
    rs_rng ^= rs_rng >> 7;
    rs_rng ^= rs_rng << 17;
    return (double)(rs_rng >> 11) * (1.0 / 9007199254740992.0);
}

/* a cell a player walks through and sees through: air, plants, snow layers, torches */
static int air_like(int b)
{
    return b == 0 || b == 31 || b == 32 || b == 37 || b == 38 || b == 39 || b == 40 || b == 78 || b == 175 ||
           b == 106 || b == 83 || b == 6 || b == 50;
}

static int liquid(int b) { return b >= 8 && b <= 11; }

static int blk(struct world *w, int x, int y, int z)
{
    if (y < 0 || y > 255) return 0;
    struct chunk *c = world_chunk(w, x >> 4, z >> 4);
    return c ? chunk_xyz_id(c, x & 15, y, z & 15) : -1;
}

static int solid(int b) { return b > 0 && !air_like(b) && !liquid(b); }

/* the walk from the feet (x, z at feet level fy) to just short of the ore: every sample's feet and head cells air,
 * the ground under it solid, or a step of one block up (its cell solid, the two over it air) or down (air, solid
 * under the one below) */
static int walkable(struct world *w, double x0, double z0, double x1, double z1, int fy)
{
    double dx = x1 - x0, dz = z1 - z0, d = sqrt(dx * dx + dz * dz);
    int n = (int)(d / RS_STEP);
    int y = fy;
    for (int i = 1; i <= n; ++i)
    {
        double t = (double)i / (double)(n + 1);
        int x = (int)floor(x0 + dx * t), z = (int)floor(z0 + dz * t);
        int f = blk(w, x, y, z), h = blk(w, x, y + 1, z), g = blk(w, x, y - 1, z);
        if (f < 0 || h < 0 || g < 0) return 0;
        if (liquid(f) || liquid(h) || liquid(g)) return 0;
        if (air_like(f) && air_like(h) && solid(g)) continue;
        if (solid(f) && air_like(h) && air_like(blk(w, x, y + 2, z)) && y + 1 <= fy + 1) { ++y; continue; }
        if (air_like(f) && air_like(h) && air_like(g) && solid(blk(w, x, y - 2, z)) && y - 1 >= fy - 1) { --y; continue; }
        return 0;
    }
    return 1;
}

/* the sight line from the eye to the cell's middle: air-like cells only, the cell itself aside */
static int in_sight(struct world *w, double ex, double ey, double ez, int bx, int by, int bz)
{
    double tx = bx + 0.5, ty = by + 0.5, tz = bz + 0.5;
    double dx = tx - ex, dy = ty - ey, dz = tz - ez, d = sqrt(dx * dx + dy * dy + dz * dz);
    int n = (int)(d / (RS_STEP * 0.5));
    for (int i = 1; i < n; ++i)
    {
        double t = (double)i / (double)n;
        int x = (int)floor(ex + dx * t), y = (int)floor(ey + dy * t), z = (int)floor(ez + dz * t);
        if (x == bx && y == by && z == bz) return 1;
        if (!air_like(blk(w, x, y, z))) return 0;
    }
    return 1;
}

/* --embed (lane/rawtree): the sight line from the eye to a point inside the air cell (ax, ay, az) beside the ore,
 * air-like cells only until it reaches that cell */
static int in_sight_pt(struct world *w, double ex, double ey, double ez, double tx, double ty, double tz, int ax,
                       int ay, int az)
{
    double dx = tx - ex, dy = ty - ey, dz = tz - ez, d = sqrt(dx * dx + dy * dy + dz * dz);
    int n = (int)(d / (RS_STEP * 0.5));
    for (int i = 1; i <= n; ++i)
    {
        double t = (double)i / (double)n;
        int x = (int)floor(ex + dx * t), y = (int)floor(ey + dy * t), z = (int)floor(ez + dz * t);
        if (x == ax && y == ay && z == az) return 1;
        if (!air_like(blk(w, x, y, z))) return 0;
    }
    return 0;
}

/* --embed: a cell the ore may replace, the cave's or the ground's own rock and soil (stone, grass, dirt, gravel,
 * sandstone, clay) */
static int embeddable(int b) { return b == 1 || b == 2 || b == 3 || b == 13 || b == 24 || b == 82; }

/* --embed: the ore set into a solid neighbour of the air cell (ax, ay, az) instead of on it: the four walls at the air
 * cell's level and the one above it, or the floor under it, a cell embeddable() whose face toward the air cell the eye
 * sees (the eye on the face's side, the line to the face's middle through air); one of those drawn at random. On
 * success the ore's cell and the face's middle (the look's target) */
#define RS_EMBED_N 9
static int embed_cell(struct world *w, double ex, double ey, double ez, int ax, int ay, int az, int *ox, int *oy,
                      int *oz, double *fx, double *fy, double *fz)
{
    /* each neighbour: the air cell it borders (0 the feet's, 1 the head's over it) and the step from that cell to
     * the ore's (sx, sy, sz) */
    static const int nb[RS_EMBED_N][4] = {{0, 1, 0, 0}, {0, -1, 0, 0}, {0, 0, 0, 1}, {0, 0, 0, -1}, {1, 1, 0, 0},
                                          {1, -1, 0, 0}, {1, 0, 0, 1}, {1, 0, 0, -1}, {0, 0, -1, 0}};
    int ok[RS_EMBED_N], n = 0;
    double px[RS_EMBED_N], py[RS_EMBED_N], pz[RS_EMBED_N];
    for (int i = 0; i < RS_EMBED_N; ++i)
    {
        int cy = ay + nb[i][0], sx = nb[i][1], sy = nb[i][2], sz = nb[i][3];
        if (!embeddable(blk(w, ax + sx, cy + sy, az + sz))) continue;
        /* the face's middle, a hair inside the air cell; the eye must be on the air's side of the face */
        double cx = ax + 0.5 + 0.49 * sx, cyy = cy + 0.5 + 0.49 * sy, cz = az + 0.5 + 0.49 * sz;
        if ((ex - cx) * -sx + (ey - cyy) * -sy + (ez - cz) * -sz <= 0.05) continue;
        if (!in_sight_pt(w, ex, ey, ez, cx, cyy, cz, ax, cy, az)) continue;
        ok[n] = i, px[n] = cx, py[n] = cyy, pz[n] = cz;
        ++n;
    }
    if (n == 0) return 0;
    int k = (int)floor(unif() * n);
    if (k >= n) k = n - 1;
    int i = ok[k];
    *ox = ax + nb[i][1], *oy = ay + nb[i][0] + nb[i][2], *oz = az + nb[i][3];
    *fx = px[k], *fy = py[k], *fz = pz[k];
    return 1;
}

static double degrees(double r) { return r * (180.0 / 3.14159265358979323846); }

/* --logs (lane/rawtree): the seed's spawn as it is (no setup, no dev flag: the library's row is the seed set's own
 * snapshot directory), kept when a log (17) is seen from the spawn's eye: a face of
 * it with an air-like cell beside it and the line from the eye to that face's middle through air-like cells, within
 * RS_LOG_R blocks horizontally and RS_LOG_DY below to RS_LOG_UP above the feet. --log-dmax D keeps the spawn only
 * when the nearest such log is within D blocks of the eye; --log-view DEG only when one within D lies within DEG of
 * the look in yaw and pitch (the spawn's own Rotation: the log on screen); 0 (the default) any direction. The scan
 * only selects: what it reads of the world names a spawn, the start is the seed's spawn unchanged */
#define RS_LOG_R 24
#define RS_LOG_DY 4
#define RS_LOG_UP 12
static int spawn_of(const char *dir, double *x, double *y, double *z, char **header);
static float nbt_f(const nbt *v)
{
    uint32_t b = nbt_float_bits(v);
    float f;
    memcpy(&f, &b, 4);
    return f;
}
struct log_seen { int n; double d, a, p; int x, y, z; };
static double wrap180(double a)
{
    a = fmod(a, 360.0);
    if (a > 180.0) a -= 360.0;
    if (a < -180.0) a += 360.0;
    return a;
}
/* 17 only: the cast's seen (rlbind.c OVER_IDS) and the task's item are block 17; acacia and dark oak (162) are not.
 * --near-block ID:DIST (lane/nether-polish) scans for block ID instead, out to at least DIST blocks (scan_r), and
 * labels the rows near:ID; --logs alone keeps 17, RS_LOG_R and its label, so it writes the rows it always wrote */
static int scan_block = 17, scan_r = RS_LOG_R;
static char scan_label[32] = "rawtree:log";
static int is_log(int b) { return b == scan_block; }

/* --biome ID|NAME|any (lane/nether-polish): a start is kept only when the biome of the column the feet stand in is that
 * one (want_biome; -1 any). A name matches BIOMES[i].name ignoring case, spaces and underscores (extreme_hills) */
static int want_biome = -1;
static int biome_at(struct world *w, double px, double pz)
{
    int x = (int)floor(px), z = (int)floor(pz);
    struct chunk *c = world_chunk(w, x >> 4, z >> 4);
    return c ? c->biome[(z & 15) << 4 | (x & 15)] : -1;
}
static int name_eq(const char *a, const char *b)
{
    for (;; ++a, ++b)
    {
        while (*a == ' ' || *a == '_') ++a;
        while (*b == ' ' || *b == '_') ++b;
        if (!*a || !*b) return !*a && !*b;
        int ca = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a, cb = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if (ca != cb) return 0;
    }
}
/* the biome's id, -1 for any, -2 for no such biome */
static int biome_parse(const char *s)
{
    if (!strcmp(s, "any")) return -1;
    char *e;
    long v = strtol(s, &e, 10);
    if (!*e && e != s && v >= 0 && v < 256 && BIOMES[v].name) return (int)v;
    for (int i = 0; i < 256; ++i)
        if (BIOMES[i].name && name_eq(BIOMES[i].name, s)) return i;
    return -2;
}
static const char *biome_name(int b) { return b >= 0 && b < 256 && BIOMES[b].name ? BIOMES[b].name : "none"; }
static void log_scan(struct world *w, double px, double py, double pz, double yaw, double pitch, double view,
                     struct log_seen *o)
{
    static const int f6[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    double ex = px, ey = py + 1.62, ez = pz;
    int fy = (int)floor(py + 1e-6), x0 = (int)floor(px), z0 = (int)floor(pz);
    memset(o, 0, sizeof *o);
    o->d = 1e9;
    for (int x = x0 - scan_r; x <= x0 + scan_r; ++x)
        for (int z = z0 - scan_r; z <= z0 + scan_r; ++z)
            for (int y = fy - RS_LOG_DY; y <= fy + RS_LOG_UP; ++y)
            {
                if (!is_log(blk(w, x, y, z))) continue;
                double tx = x + 0.5 - ex, ty = y + 0.5 - ey, tz = z + 0.5 - ez;
                double d = sqrt(tx * tx + ty * ty + tz * tz);
                if (d >= o->d) continue;
                double a = wrap180(degrees(atan2(tz, tx)) - 90.0 - yaw);
                double p = -degrees(atan2(ty, sqrt(tx * tx + tz * tz))) - pitch;
                if (view > 0 && (fabs(a) > view || fabs(p) > view)) continue;
                int seen = 0;
                for (int f = 0; f < 6 && !seen; ++f)
                {
                    int ax = x + f6[f][0], ay = y + f6[f][1], az = z + f6[f][2];
                    if (!air_like(blk(w, ax, ay, az))) continue;
                    double cx = x + 0.5 + 0.51 * f6[f][0], cy = y + 0.5 + 0.51 * f6[f][1], cz = z + 0.5 + 0.51 * f6[f][2];
                    if ((ex - cx) * f6[f][0] + (ey - cy) * f6[f][1] + (ez - cz) * f6[f][2] <= 0.0) continue;
                    if ((int)floor(ex) == ax && (int)floor(ey) == ay && (int)floor(ez) == az) seen = 1;
                    else seen = in_sight_pt(w, ex, ey, ez, cx, cy, cz, ax, ay, az);
                }
                if (!seen) continue;
                o->n = 1, o->d = d, o->a = a, o->p = p, o->x = x, o->y = y, o->z = z;
            }
}

static int logs_main(const char *set, const int *seeds, int ns, const char *out, double dmax, double view)
{
    if (mkdir(out, 0775) && errno != EEXIST) return fprintf(stderr, "rawstarts: %s: %s\n", out, strerror(errno)), 1;
    char ip[4200];
    snprintf(ip, sizeof ip, "%s/index.tsv", out);
    FILE *idx = fopen(ip, "w");
    if (!idx) return fprintf(stderr, "rawstarts: %s: %s\n", ip, strerror(errno)), 1;
    fprintf(idx, "id\tseed\tstage\tmilestones\tdimension\titems\tprovenance\tbytes\tpath\n");
    int made = 0, skipped = 0;
    for (int si = 0; si < ns; ++si)
    {
        char dir[4200];
        snprintf(dir, sizeof dir, "%s/s%d", set, seeds[si]);
        double px, py, pz;
        char *hdr = NULL;
        int bad = spawn_of(dir, &px, &py, &pz, &hdr);
        free(hdr);
        struct snapshot s;
        memset(&s, 0, sizeof s);
        if (bad || !snapshot_load(&s, dir))
        {
            printf("seed %d: no snapshot at %s (skipped)\n", seeds[si], dir);
            ++skipped;
            continue;
        }
        double yaw = 0.0, pitch = 0.0;
        const nbt *rot = s.player_server ? nbt_get(s.player_server, "Rotation") : NULL;
        if (rot && nbt_kind(rot) == NBT_LIST && nbt_list_size(rot) == 2)
            yaw = nbt_f(nbt_list_get(rot, 0)), pitch = nbt_f(nbt_list_get(rot, 1));
        int bio = biome_at(&s.world, px, pz);
        if (want_biome >= 0 && bio != want_biome)
        {
            printf("seed %d: biome %s, not %s (skipped)\n", seeds[si], biome_name(bio), biome_name(want_biome));
            snapshot_free(&s);
            ++skipped;
            continue;
        }
        struct log_seen L;
        log_scan(&s.world, px, py, pz, yaw, pitch, view, &L);
        snapshot_free(&s);
        if (dmax > 0 && (!L.n || L.d > dmax))
        {
            ++skipped;
            continue;
        }
        char bsuf[64] = "";
        if (want_biome >= 0) snprintf(bsuf, sizeof bsuf, ",biome=%s", biome_name(bio));
        char real[4096];
        if (!realpath(dir, real)) snprintf(real, sizeof real, "%.4000s", dir);
        if (L.n)
            fprintf(idx, "s%d\t%d\tspawn\t-\toverworld\t-\t%s,d=%.2f,a=%.1f,p=%.1f,log=%d/%d/%d%s\t0\t%s\n",
                    seeds[si], seeds[si], scan_label, L.d, L.a, L.p, L.x, L.y, L.z, bsuf, real);
        else
            fprintf(idx, "s%d\t%d\tspawn\t-\toverworld\t-\t%s,none%s\t0\t%s\n", seeds[si], seeds[si], scan_label, bsuf, real);
        ++made;
    }
    fclose(idx);
    printf("RAWSTARTS logs %d kept, %d skipped, %d seeds: %s\n", made, skipped, ns, ip);
    return 0;
}

/* --cave: the player's feet in a cave instead of at the spawn: a cell of air (0, cave air) with the head's air over it
 * and solid ground under it, RS_CAVE_Y0 to RS_CAVE_Y1, within RS_CAVE_R blocks of the spawn horizontally, no liquid
 * in the 3x3x3 around the feet, and a solid roof within RS_CAVE_ROOF blocks over the head; the feet at the cell's
 * middle. 0 when RS_CAVE_TRIES draws find none */
#define RS_CAVE_R 40
#define RS_CAVE_Y0 8
#define RS_CAVE_Y1 50
#define RS_CAVE_ROOF 12
#define RS_CAVE_TRIES 200000
#define RS_CAVE_CELLS 40    /* cave cells tried a start, RS_TRIES ore placements each */
static int cave_cell(struct world *w, double sx, double sz, double *x, double *y, double *z)
{
    for (int t = 0; t < RS_CAVE_TRIES; ++t)
    {
        int cx = (int)floor(sx) + (int)floor((2.0 * unif() - 1.0) * RS_CAVE_R);
        int cz = (int)floor(sz) + (int)floor((2.0 * unif() - 1.0) * RS_CAVE_R);
        int cy = RS_CAVE_Y0 + (int)floor(unif() * (RS_CAVE_Y1 - RS_CAVE_Y0));
        if (blk(w, cx, cy, cz) != 0 || blk(w, cx, cy + 1, cz) != 0 || !solid(blk(w, cx, cy - 1, cz))) continue;
        int wet = 0;
        for (int dx = -1; dx <= 1 && !wet; ++dx)
            for (int dy = -1; dy <= 1 && !wet; ++dy)
                for (int dz = -1; dz <= 1 && !wet; ++dz)
                {
                    int b = blk(w, cx + dx, cy + dy, cz + dz);
                    wet = b < 0 || liquid(b);
                }
        if (wet) continue;
        int roof = 0;
        for (int dy = 2; dy <= RS_CAVE_ROOF && !roof; ++dy) roof = solid(blk(w, cx, cy + dy, cz));
        if (!roof) continue;
        *x = cx + 0.5, *y = (double)cy, *z = cz + 0.5;
        return 1;
    }
    return 0;
}

/* the seed's spawn: the tape's row 1 (the state at the snapshot's tick 2), its server player's feet */
static int spawn_of(const char *dir, double *x, double *y, double *z, char **header)
{
    char p[4200];
    snprintf(p, sizeof p, "%.4000s/tape.jsonl", dir);
    FILE *f = fopen(p, "r");
    if (!f) return -1;
    char *line = NULL;
    size_t cap = 0;
    int got = 0, ln = 0;
    *header = NULL;
    while (!got && getline(&line, &cap, f) > 0)
    {
        if (ln++ == 0)
        {
            *header = strdup(line);
            continue;
        }
        struct jval *j = json_parse(line);
        int64_t t = -1;
        if (j && json_int(json_get(j, "t"), &t) && t == 1)
        {
            const struct jval *sp = json_get(j, "sp");
            uint64_t bx, by, bz;
            if (sp && json_double(json_get(sp, "x"), &bx) && json_double(json_get(sp, "y"), &by) &&
                json_double(json_get(sp, "z"), &bz))
            {
                memcpy(x, &bx, 8), memcpy(y, &by, 8), memcpy(z, &bz, 8);
                got = 1;
            }
        }
        json_free(j);
        line = NULL;
        cap = 0;
    }
    free(line);
    fclose(f);
    return got ? 0 : -1;
}

/* the start: links to the seed's files, tape.jsonl's header with the setup, then its rows */
static int write_start(const char *src, const char *dst, const char *setup)
{
    if (mkdir(dst, 0775) && errno != EEXIST) return -1;
    DIR *d = opendir(src);
    if (!d) return -1;
    struct dirent *e;
    char a[4400], b[4400];
    while ((e = readdir(d)) != NULL)
    {
        if (e->d_name[0] == '.' || !strcmp(e->d_name, "tape.jsonl")) continue;
        snprintf(a, sizeof a, "%.4000s/%.255s", src, e->d_name);
        snprintf(b, sizeof b, "%.4000s/%.255s", dst, e->d_name);
        unlink(b);
        char real[4096];
        if (!realpath(a, real) || symlink(real, b)) { closedir(d); return -1; }
    }
    closedir(d);
    snprintf(a, sizeof a, "%s/tape.jsonl", src);
    snprintf(b, sizeof b, "%s/tape.jsonl", dst);
    FILE *in = fopen(a, "r"), *out = fopen(b, "w");
    if (!in || !out) { if (in) fclose(in); if (out) fclose(out); return -1; }
    char *line = NULL;
    size_t cap = 0;
    ssize_t l;
    int first = 1;
    while ((l = getline(&line, &cap, in)) > 0)
    {
        if (first)
        {
            /* the header object without its closing brace, then the dev flag and the setup */
            ssize_t k = l;
            while (k > 0 && line[k - 1] != '}') --k;
            fprintf(out, "%.*s,\"dev\":true,\"setup\":[%s]}\n", (int)(k - 1), line, setup);
            first = 0;
        }
        else
            fwrite(line, 1, (size_t)l, out);
    }
    free(line);
    fclose(in);
    return fclose(out);
}

static int parse_seeds(const char *spec, int *out, int max)
{
    int n = 0;
    char b[1024];
    snprintf(b, sizeof b, "%s", spec);
    for (char *sv, *t = strtok_r(b, ",", &sv); t; t = strtok_r(NULL, ",", &sv))
    {
        int a, c;
        if (sscanf(t, "%d-%d", &a, &c) == 2)
            for (int s = a; s <= c && n < max; ++s) out[n++] = s;
        else if (n < max)
            out[n++] = atoi(t);
    }
    return n;
}

int main(int argc, char **argv)
{
    const char *set = NULL, *seeds_spec = NULL, *out = NULL;
    int per = 1, ore = RS_ORE;
    double dmin = RS_DMIN, dmax = RS_DMAX, turn = 0.0, tmin = 0.0, pn = 0.0, log_dmax = 0.0, log_view = 0.0;
    int logs = 0;
    int cave = 0, notorch = 0, embed = 0;
    uint64_t rseed = 1;
    for (int i = 1; i < argc; ++i)
    {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--set") && v) set = v, ++i;
        else if (!strcmp(a, "--seeds") && v) seeds_spec = v, ++i;
        else if (!strcmp(a, "--out") && v) out = v, ++i;
        else if (!strcmp(a, "--per") && v) per = atoi(v), ++i;
        else if (!strcmp(a, "--dmin") && v) dmin = atof(v), ++i;
        else if (!strcmp(a, "--dmax") && v) dmax = atof(v), ++i;
        else if (!strcmp(a, "--turn") && v) turn = atof(v), ++i;
        else if (!strcmp(a, "--turn-min") && v) tmin = atof(v), ++i;
        else if (!strcmp(a, "--cave")) cave = 1;
        else if (!strcmp(a, "--no-torch")) notorch = 1;
        else if (!strcmp(a, "--embed")) embed = 1;
        else if (!strcmp(a, "--logs")) logs = 1;
        else if (!strcmp(a, "--log-dmax") && v) log_dmax = atof(v), ++i;
        else if (!strcmp(a, "--log-view") && v) log_view = atof(v), ++i;
        else if (!strcmp(a, "--pitch-noise") && v) pn = atof(v), ++i;
        else if (!strcmp(a, "--ore") && v) ore = atoi(v), ++i;
        else if (!strcmp(a, "--seed") && v) rseed = strtoull(v, NULL, 10), ++i;
        else if (!strcmp(a, "--near-block") && v)
        {
            /* ID:DIST: the --logs scan for block ID within DIST blocks of the eye */
            char *e;
            long id = strtol(v, &e, 10);
            double d = *e == ':' ? strtod(e + 1, &e) : -1.0;
            if (id < 1 || id > 4095 || d <= 0 || *e)
                return fprintf(stderr, "rawstarts: --near-block ID:DIST (a block id and a distance in blocks): %s\n", v), 2;
            logs = 1, scan_block = (int)id, log_dmax = d;
            if ((int)ceil(d) > scan_r) scan_r = (int)ceil(d);
            snprintf(scan_label, sizeof scan_label, "near:%ld", id);
            ++i;
        }
        else if (!strcmp(a, "--biome") && v)
        {
            want_biome = biome_parse(v);
            if (want_biome == -2) return fprintf(stderr, "rawstarts: --biome %s: no such biome (an id, a name or any)\n", v), 2;
            ++i;
        }
        else
        {
            fprintf(stderr, "usage: rawstarts --set DIR --seeds A-B --out LIB [--per K] [--dmin D] [--dmax D] "
                    "[--turn DEG] [--turn-min DEG] [--pitch-noise DEG] [--cave [--no-torch]] [--embed] [--ore ID] "
                    "[--seed S] [--biome ID|NAME]\n"
                    "       rawstarts --logs | --near-block ID:DIST --set DIR --seeds A-B --out LIB [--log-dmax D] "
                    "[--log-view DEG] [--biome ID|NAME|any]\n");
            return 2;
        }
    }
    if (!set || !seeds_spec || !out || per < 1 || dmin <= 0.5 || dmax < dmin || tmin < 0 || (turn > 0 && tmin > turn))
        return fprintf(stderr, "rawstarts: --set, --seeds and --out, per >= 1, 0.5 < dmin <= dmax, 0 <= turn-min <= turn\n"), 2;
    static int seeds[4096];
    int ns = parse_seeds(seeds_spec, seeds, 4096);
    if (logs) return logs_main(set, seeds, ns, out, log_dmax, log_view);
    if (mkdir(out, 0775) && errno != EEXIST) return fprintf(stderr, "rawstarts: %s: %s\n", out, strerror(errno)), 1;
    char ip[4200];
    snprintf(ip, sizeof ip, "%s/index.tsv", out);
    FILE *idx = fopen(ip, "w");
    if (!idx) return fprintf(stderr, "rawstarts: %s: %s\n", ip, strerror(errno)), 1;
    fprintf(idx, "id\tseed\tstage\tmilestones\tdimension\titems\tprovenance\tbytes\tpath\n");
    int made = 0, skipped = 0;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int si = 0; si < ns; ++si)
    {
        char dir[4200];
        snprintf(dir, sizeof dir, "%s/s%d", set, seeds[si]);
        double px, py, pz;
        char *hdr = NULL;
        if (spawn_of(dir, &px, &py, &pz, &hdr))
        {
            printf("seed %d: no snapshot at %s (skipped)\n", seeds[si], dir);
            free(hdr);
            ++skipped;
            continue;
        }
        free(hdr);
        struct snapshot s;
        memset(&s, 0, sizeof s);
        if (!snapshot_load(&s, dir))
        {
            printf("seed %d: the snapshot does not load (skipped)\n", seeds[si]);
            ++skipped;
            continue;
        }
        struct world *w = &s.world;
        if (want_biome >= 0 && biome_at(w, px, pz) != want_biome)
        {
            printf("seed %d: biome %s, not %s (skipped)\n", seeds[si], biome_name(biome_at(w, px, pz)), biome_name(want_biome));
            snapshot_free(&s);
            ++skipped;
            continue;
        }
        double sx0 = px, sy0 = py, sz0 = pz;
        for (int k = 0; k < per; ++k)
        {
            rs_rng = 0x9e3779b97f4a7c15ull ^ (rseed * 0x2545f4914f6cdd1dull) ^ ((uint64_t)seeds[si] << 20) ^ (uint64_t)k;
            for (int w0 = 0; w0 < 8; ++w0) unif();
            int ok = 0, bx = 0, by = 0, bz = 0, fy = 0;
            double d = 0, phi = 0, ex = 0, ey = 0, ez = 0, lx = 0, ly = 0, lz = 0;
            for (int ct = 0; ct < (cave ? RS_CAVE_CELLS : 1) && !ok; ++ct)
            {
            px = sx0, py = sy0, pz = sz0;
            if (cave && !cave_cell(w, sx0, sz0, &px, &py, &pz)) continue;
            fy = (int)floor(py + 1e-6);
            ex = px, ey = py + 1.62, ez = pz;
            for (int tr = 0; tr < RS_TRIES && !ok; ++tr)
            {
                d = dmin + (dmax - dmin) * unif();
                phi = 360.0 * unif();
                double r = phi * (3.14159265358979323846 / 180.0);
                double tx = px - sin(r) * d, tz = pz + cos(r) * d;
                bx = (int)floor(tx), bz = (int)floor(tz);
                if (bx == (int)floor(px) && bz == (int)floor(pz)) continue;
                for (by = fy + 1; by >= fy - 1; --by)
                {
                    int c = blk(w, bx, by, bz), g = blk(w, bx, by - 1, bz), u = blk(w, bx, by + 1, bz);
                    if (c < 0 || g < 0) break;
                    if (air_like(c) && solid(g) && air_like(u)) break;
                }
                if (by < fy - 1 || blk(w, bx, by, bz) < 0) continue;
                if (!air_like(blk(w, bx, by, bz)) || !solid(blk(w, bx, by - 1, bz))) continue;
                /* the walk to the cell before the ore's (stopping 0.8 short), the sight line to its middle */
                double sx = bx + 0.5 - px, sz = bz + 0.5 - pz, sl = sqrt(sx * sx + sz * sz);
                if (sl < 1.0) continue;
                double wx = px + sx * (sl - 0.8) / sl, wz = pz + sz * (sl - 0.8) / sl;
                if (!walkable(w, px, pz, wx, wz, fy)) continue;
                if (!in_sight(w, ex, ey, ez, bx, by, bz)) continue;
                if (embed)
                {
                    /* the ore into the rock beside the air cell; the air cell stays the walk's goal */
                    int ox, oy, oz;
                    if (!embed_cell(w, ex, ey, ez, bx, by, bz, &ox, &oy, &oz, &lx, &ly, &lz)) continue;
                    bx = ox, by = oy, bz = oz;
                }
                else
                    lx = bx + 0.5, ly = by + 0.5, lz = bz + 0.5;
                ok = 1;
            }
            }
            if (!ok)
            {
                printf("seed %d start %d: no cell in %d draws (skipped)\n", seeds[si], k, RS_TRIES);
                ++skipped;
                continue;
            }
            /* the look at the ore's middle (vecenv.c look_at's convention), then the turn and the pitch noise */
            double dx = lx - ex, dy = ly - ey, dz = lz - ez, h = sqrt(dx * dx + dz * dz);
            double yaw0 = degrees(atan2(dz, dx)) - 90.0, pitch0 = -degrees(atan2(dy, h));
            double off = 0.0;
            if (turn > 0)
            {
                /* at least --turn-min of yaw (the ore out of view), either side */
                double m = tmin + (turn - tmin) * unif();
                off = unif() < 0.5 ? -m : m;
            }
            double poff = pn > 0 ? pn * (2.0 * unif() - 1.0) : 0.0;
            double yaw = yaw0 + off, pitch = pitch0 + poff;
            pitch = pitch > 90.0 ? 90.0 : pitch < -90.0 ? -90.0 : pitch;
            /* whole-number hundredths: the script's text and the engine's parse the same doubles */
            yaw = round(yaw * 100.0) / 100.0, pitch = round(pitch * 100.0) / 100.0;
            if (yaw == 0.0) yaw = 0.0;      /* no negative zero (ledger bp-negzero-yaw) */
            if (pitch == 0.0) pitch = 0.0;
            int64_t tick = s.tick;
            char setup[1024], torch[256] = "";
            /* --cave: a torch on the floor at the feet (a cave is dark: without it the cast shows the ore near black,
             * and 0 of 19 cave starts were ever won in training) */
            if (cave && !notorch)
                snprintf(torch, sizeof torch,
                         ",{\"class\":\"Dev\",\"tick\":%lld,\"cmd\":{\"op\":\"setblock\",\"x\":%d,\"y\":%d,\"z\":%d,"
                         "\"id\":50,\"meta\":5}}",
                         (long long)s.tick, (int)floor(px), fy, (int)floor(pz));
            /* the tp first: the give's item lands where the player is when it runs, and a give before a tp away (a
             * cave start) left the pickaxe at the spawn: every cave episode played bare-handed (0 of 1,919 won) */
            snprintf(setup, sizeof setup,
                     "{\"class\":\"Dev\",\"tick\":%lld,\"cmd\":{\"op\":\"tp\",\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                     "\"yaw\":%.2f,\"pitch\":%.2f}},"
                     "{\"class\":\"Dev\",\"tick\":%lld,\"cmd\":{\"op\":\"give\",\"item\":%d,\"count\":1,\"meta\":0}},"
                     "{\"class\":\"Dev\",\"tick\":%lld,\"cmd\":{\"op\":\"setblock\",\"x\":%d,\"y\":%d,\"z\":%d,"
                     "\"id\":%d,\"meta\":0}}%s",
                     (long long)tick, px, py, pz, yaw, pitch, (long long)tick, RS_PICK, (long long)tick, bx, by, bz,
                     ore, torch);
            char sd[4300];
            snprintf(sd, sizeof sd, "%s/s%d-%d", out, seeds[si], k);
            if (write_start(dir, sd, setup))
            {
                printf("seed %d start %d: %s not written: %s\n", seeds[si], k, sd, strerror(errno));
                snapshot_free(&s);
                fclose(idx);
                return 1;
            }
            char real[4096];
            if (!realpath(sd, real)) snprintf(real, sizeof real, "%.4000s", sd);
            fprintf(idx, "s%d-%d\t%d\tspawn\t-\toverworld\t-\trawskill:d=%.2f,a=%.1f,ore=%d/%d/%d,dy=%d%s\t0\t%s\n",
                    seeds[si], k, seeds[si], d, off, bx, by, bz, by - fy, cave ? (embed ? ",cave,embed" : ",cave") :
                    embed ? ",embed" : "", real);
            ++made;
        }
        snapshot_free(&s);
    }
    fclose(idx);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("RAWSTARTS %d made, %d skipped, %d seeds, %.2f s: %s\n", made, skipped, ns,
           (double)(t1.tv_sec - t0.tv_sec) + 1e-9 * (double)(t1.tv_nsec - t0.tv_nsec), ip);
    return 0;
}
