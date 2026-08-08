/* entity_trace_verify.c - REAL-GAME per-tick ENTITY verifier (PORT_MATRIX P1). Loads a
 * scenario JSONL captured from the live Java game (verify/entity_trace/capture_entities.py),
 * rebuilds the static block arena, initializes the entity from frame 0, forward-integrates
 * the C entity model tick-for-tick, and diffs every captured frame BIT-EXACTLY (doubles are
 * compared on their raw Double.doubleToRawLongBits bits, per the project's exact-double rule).
 *
 * VERIFICATION CONTRACT
 *   - ARROW (kind "arrow"): projectile_motion.h (EntityArrow.onUpdate in-air/on-hit port) is
 *     a faithful deterministic port for a dry sealed arena - NoAI world-rand draws do not
 *     touch arrow motion (crit particles are the only arrow rand use and our arrows are not
 *     critical). Acceptance = posX/Y/Z + motionX/Y/Z + inGround bit-exact for every frame.
 *     First divergent (tick, field, Java, C) is reported. yaw/pitch are TRIMMED in
 *     projectile_motion.h (rotation smoothing) -> reported informationally, never a FAIL.
 *   - ZOMBIE (kind "zombie"): the generic Entity/EntityLivingBase tick spine is NOT ported
 *     (PORT_MATRIX P2). There is no faithful C EntityLivingBase.travel; the only mob body
 *     motion in C (entities_world.h ew_mob_step) is AI-waypoint-coupled, not a generic drop.
 *     So this harness does NOT tick the zombie - it prints the observed Java trajectory as the
 *     P2 golden/target and reports the gap. Exit stays 0 (documented gap, not a regression).
 *
 * Not a CPU==CUDA oracle kernel (reads external JSONL at runtime); built CPU-only like the
 * TICKTRACE/GAMERULES self-tests. Usage: entity_trace_verify <scenario.jsonl> [...].
 * Exit 0 iff every ARROW scenario is bit-exact (zombie scenarios are informational). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/projectile_motion.h"
#include "../core/living_base.h"      /* EntityLivingBase tick spine (PORT_MATRIX P2) */

#define ET_MAXVOL   131072    /* max arena cells (largest scenario ~5760) */
#define ET_MAXFRAME 260       /* max captured frames (largest scenario 201) */

typedef struct {
    char name[48], kind[16];
    int ai, ox, oy, oz, nx, ny, nz, ticks;
    u16 *blocks;              /* [nx*ny*nz] static arena, getblocks order y,z,x */
    int nframes;
    int   toff[ET_MAXFRAME];
    /* first (only) entity per frame, raw bits */
    i64   x[ET_MAXFRAME], y[ET_MAXFRAME], z[ET_MAXFRAME];
    i64   mx[ET_MAXFRAME], my[ET_MAXFRAME], mz[ET_MAXFRAME];
    int   inGround[ET_MAXFRAME], ticksInAir[ET_MAXFRAME];
    int   onGround[ET_MAXFRAME];
    i32   yaw[ET_MAXFRAME], pitch[ET_MAXFRAME];
} Scen;

/* ---- minimal JSONL parsing ---- */

static int find_int(const char *line, const char *key, int dflt) {
    const char *p = strstr(line, key);
    if (!p) return dflt;
    p += strlen(key);
    while (*p && (*p == '"' || *p == ':' || *p == ' ')) ++p;
    return (int)strtol(p, NULL, 10);
}
static i64 find_i64(const char *line, const char *key, i64 dflt) {
    const char *p = strstr(line, key);
    if (!p) return dflt;
    p += strlen(key);
    while (*p && (*p == '"' || *p == ':' || *p == ' ')) ++p;
    return (i64)strtoll(p, NULL, 10);   /* signed: raw bits of negative doubles set bit 63 */
}
static void find_str(const char *line, const char *key, char *out, int cap) {
    const char *p = strstr(line, key);
    int i = 0;
    out[0] = 0;
    if (!p) return;
    p += strlen(key);
    while (*p && *p != '"') ++p;
    if (*p == '"') ++p;
    while (*p && *p != '"' && i < cap - 1) out[i++] = *p++;
    out[i] = 0;
}
static int parse_blocks(const char *line, u16 *out, int vol) {
    const char *p = strstr(line, "\"blocks\"");
    int n = 0;
    if (!p) return -1;
    p = strchr(p, '[');
    if (!p) return -1;
    ++p;
    while (*p && *p != ']' && n < vol) {
        while (*p == ' ' || *p == ',') ++p;
        if (*p == ']' || !*p) break;
        out[n++] = (u16)strtol(p, (char **)&p, 10);
    }
    return n;
}

static double b2d(i64 bits) { union { i64 i; double d; } u; u.i = bits; return u.d; }
static float  b2f(i32 bits) { union { i32 i; float f; } u; u.i = bits; return u.f; }

/* Shared MathHelper sin/cos table, initialized once (CPU-only harness). */
static const McSinTable *sin_table_ptr(void) {
    static McSinTable st;
    static int init = 0;
    if (!init) { mc_sin_table_init(&st); init = 1; }
    return &st;
}

static int load_scen(const char *path, Scen *s) {
    FILE *f = fopen(path, "r");
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;
    int vol, ti = 0, got;
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return -1; }
    if ((len = getline(&line, &cap, f)) <= 0) { fclose(f); return -1; }
    find_str(line, "\"name\"", s->name, sizeof s->name);
    find_str(line, "\"kind\"", s->kind, sizeof s->kind);
    s->ai = find_int(line, "\"ai\"", 0);
    s->ox = find_int(line, "\"ox\"", 0);
    s->oy = find_int(line, "\"oy\"", 100);
    s->oz = find_int(line, "\"oz\"", 0);
    s->nx = find_int(line, "\"nx\"", 0);
    s->ny = find_int(line, "\"ny\"", 0);
    s->nz = find_int(line, "\"nz\"", 0);
    s->ticks = find_int(line, "\"ticks\"", 0);
    vol = s->nx * s->ny * s->nz;
    if (vol <= 0 || vol > ET_MAXVOL) { fprintf(stderr, "bad vol %d\n", vol); fclose(f); return -1; }
    s->blocks = (u16 *)malloc((size_t)vol * sizeof(u16));
    got = parse_blocks(line, s->blocks, vol);
    if (got != vol) { fprintf(stderr, "%s: header blocks %d != %d\n", path, got, vol); fclose(f); return -1; }
    while ((len = getline(&line, &cap, f)) > 0 && ti < ET_MAXFRAME) {
        if (!strstr(line, "\"eid\"")) continue;   /* skip any blank/trailing line */
        s->toff[ti]       = find_int(line, "\"t\"", ti);
        s->x[ti]          = find_i64(line, "\"x\"", 0);
        s->y[ti]          = find_i64(line, "\"y\"", 0);
        s->z[ti]          = find_i64(line, "\"z\"", 0);
        s->mx[ti]         = find_i64(line, "\"mx\"", 0);
        s->my[ti]         = find_i64(line, "\"my\"", 0);
        s->mz[ti]         = find_i64(line, "\"mz\"", 0);
        s->yaw[ti]        = (i32)find_i64(line, "\"yaw\"", 0);
        s->pitch[ti]      = (i32)find_i64(line, "\"pitch\"", 0);
        s->inGround[ti]   = find_int(line, "\"inGround\"", -1);
        s->ticksInAir[ti] = find_int(line, "\"ticksInAir\"", -1);
        s->onGround[ti]   = find_int(line, "\"onGround\"", -1);
        ++ti;
    }
    free(line);
    fclose(f);
    s->nframes = ti;
    return vol;
}

/* ---- world-space arena getter + ray trace (world coords; reuses the header helpers) ---- */

static u16 et_get(const Scen *s, int x, int y, int z) {
    int lx = x - s->ox, ly = y - s->oy, lz = z - s->oz;
    if (lx < 0 || lx >= s->nx || ly < 0 || ly >= s->ny || lz < 0 || lz >= s->nz)
        return mc_state(BLK_AIR, 0);
    return s->blocks[(ly * s->nz + lz) * s->nx + lx];
}

/* World.rayTraceBlocks(stopOnLiquid=false, ignoreNoBB=true) over the arena. Same DDA as
 * projectile_motion.h::pm_ray_trace_blocks, but reads world coords via et_get so an arrow can
 * fly past the local 16-cube. pm_block_raytrace / pm_can_collide are already world-general. */
static int et_ray_trace(const Scen *s, double sx, double sy, double sz,
                        double ex, double ey, double ez, PmRayHit *out) {
    int i, j, k, l, i1, j1, k1;
    double curX, curY, curZ;
    u16 st;
    out->hit = 0;
    if (sx != sx || sy != sy || sz != sz) return 0;
    if (ex != ex || ey != ey || ez != ez) return 0;
    i = mc_floor(ex); j = mc_floor(ey); k = mc_floor(ez);
    l = mc_floor(sx); i1 = mc_floor(sy); j1 = mc_floor(sz);
    st = et_get(s, l, i1, j1);
    if (pm_can_collide(st) && pm_block_raytrace(l, i1, j1, sx, sy, sz, ex, ey, ez, out)) return 1;
    curX = sx; curY = sy; curZ = sz;
    k1 = 200;
    while (k1-- >= 0) {
        int flag2 = 1, flag = 1, flag1 = 1, nf;
        double d0 = 999.0, d1 = 999.0, d2 = 999.0, d3 = 999.0, d4 = 999.0, d5 = 999.0;
        double d6, d7, d8;
        if (curX != curX || curY != curY || curZ != curZ) return 0;
        if (l == i && i1 == j && j1 == k) return 0;
        if (i > l)      d0 = (double)l + 1.0; else if (i < l) d0 = (double)l + 0.0; else flag2 = 0;
        if (j > i1)     d1 = (double)i1 + 1.0; else if (j < i1) d1 = (double)i1 + 0.0; else flag = 0;
        if (k > j1)     d2 = (double)j1 + 1.0; else if (k < j1) d2 = (double)j1 + 0.0; else flag1 = 0;
        d6 = ex - curX; d7 = ey - curY; d8 = ez - curZ;
        if (flag2) d3 = (d0 - curX) / d6;
        if (flag)  d4 = (d1 - curY) / d7;
        if (flag1) d5 = (d2 - curZ) / d8;
        if (d3 == -0.0) d3 = -1.0E-4;
        if (d4 == -0.0) d4 = -1.0E-4;
        if (d5 == -0.0) d5 = -1.0E-4;
        if (d3 < d4 && d3 < d5) {
            nf = (i > l) ? 4 : 5; curX = d0; curY += d7 * d3; curZ += d8 * d3;
        } else if (d4 < d5) {
            nf = (j > i1) ? 0 : 1; curX += d6 * d4; curY = d1; curZ += d8 * d4;
        } else {
            nf = (k > j1) ? 2 : 3; curX += d6 * d5; curY += d7 * d5; curZ = d2;
        }
        l  = mc_floor(curX) - (nf == 5 ? 1 : 0);
        i1 = mc_floor(curY) - (nf == 1 ? 1 : 0);
        j1 = mc_floor(curZ) - (nf == 3 ? 1 : 0);
        st = et_get(s, l, i1, j1);
        if (pm_can_collide(st) && pm_block_raytrace(l, i1, j1, curX, curY, curZ, ex, ey, ez, out)) return 1;
    }
    return 0;
}

/* One EntityArrow.onUpdate tick (in-air branch), arena world ray trace. Mirrors
 * pm_arrow_tick but with et_ray_trace; reuses pm_arrow_on_hit_block (world-general). */
static void et_arrow_tick(McArrow *a, const Scen *s) {
    double endX, endY, endZ, f1;
    PmRayHit hit;
    if (a->inGround) return;
    a->ticksInAir++;
    endX = a->posX + a->motionX; endY = a->posY + a->motionY; endZ = a->posZ + a->motionZ;
    if (et_ray_trace(s, a->posX, a->posY, a->posZ, endX, endY, endZ, &hit))
        pm_arrow_on_hit_block(a, &hit);
    a->posX += a->motionX; a->posY += a->motionY; a->posZ += a->motionZ;
    f1 = (double)0.99f;
    a->motionX *= f1; a->motionY *= f1; a->motionZ *= f1;
    a->motionY -= 0.05000000074505806;
}

/* ---- arrow verdict ---- */

static int cmp_field(const char *name, int tick, i64 jbits, double cval,
                     int *first_tick, char *first_field, i64 *first_j, i64 *first_c) {
    union { double d; i64 i; } u; u.d = cval;
    if (u.i == jbits) return 0;
    if (*first_tick < 0) {
        *first_tick = tick; strncpy(first_field, name, 7); first_field[7] = 0;
        *first_j = jbits; *first_c = u.i;
    }
    return 1;
}

static int run_arrow(Scen *s) {
    McArrow a;
    int t, pass = 1, first_tick = -1;
    char first_field[8] = {0};
    i64 first_j = 0, first_c = 0;
    int rot_diff = 0, last_ok = 0;
    /* init from frame 0's full raw state (arrow may already have 1 update applied) */
    a.posX = b2d(s->x[0]); a.posY = b2d(s->y[0]); a.posZ = b2d(s->z[0]);
    a.motionX = b2d(s->mx[0]); a.motionY = b2d(s->my[0]); a.motionZ = b2d(s->mz[0]);
    a.inGround = s->inGround[0] > 0 ? 1 : 0;
    a.ticksInAir = s->ticksInAir[0] >= 0 ? s->ticksInAir[0] : 0;

    for (t = 1; t < s->nframes; ++t) {
        int nd = s->toff[t] - s->toff[t - 1], d, bad = 0;
        if (nd < 1) nd = 1;
        for (d = 0; d < nd; ++d) et_arrow_tick(&a, s);
        bad |= cmp_field("posX", s->toff[t], s->x[t], a.posX, &first_tick, first_field, &first_j, &first_c);
        bad |= cmp_field("posY", s->toff[t], s->y[t], a.posY, &first_tick, first_field, &first_j, &first_c);
        bad |= cmp_field("posZ", s->toff[t], s->z[t], a.posZ, &first_tick, first_field, &first_j, &first_c);
        bad |= cmp_field("motX", s->toff[t], s->mx[t], a.motionX, &first_tick, first_field, &first_j, &first_c);
        bad |= cmp_field("motY", s->toff[t], s->my[t], a.motionY, &first_tick, first_field, &first_j, &first_c);
        bad |= cmp_field("motZ", s->toff[t], s->mz[t], a.motionZ, &first_tick, first_field, &first_j, &first_c);
        if ((s->inGround[t] > 0 ? 1 : 0) != a.inGround) {
            if (first_tick < 0) { first_tick = s->toff[t]; strcpy(first_field, "inGrnd");
                first_j = s->inGround[t]; first_c = a.inGround; }
            bad = 1;
        }
        if (b2f(s->yaw[t]) != 0.0f || s->pitch[t] != s->pitch[t - 1]) rot_diff = 1;
        if (bad) pass = 0; else last_ok = s->toff[t];
    }
    printf("=== %-16s [arrow]  arena %dx%dx%d  %d frames (through tick %d) ===\n",
           s->name, s->nx, s->ny, s->nz, s->nframes, s->toff[s->nframes - 1]);
    if (pass) {
        printf("  BIT-EXACT: C == Java pos+motion+inGround for all %d frames\n", s->nframes);
        printf("  final: inGround=%d ticksInAir(C)=%d posY=%.9f\n",
               a.inGround, a.ticksInAir, a.posY);
    } else {
        printf("  DIVERGENCE at tick %d, field %s:\n", first_tick, first_field);
        printf("    Java = %.17g  (raw %lld)\n", b2d(first_j), (long long)first_j);
        printf("    C    = %.17g  (raw %lld)\n", b2d(first_c), (long long)first_c);
        printf("    last bit-exact tick: %d\n", last_ok);
    }
    if (rot_diff)
        printf("  note: yaw/pitch present in Java trace but TRIMMED in projectile_motion.h "
               "(rotation smoothing) - informational, not part of the pass criterion\n");
    printf("  VERDICT: %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

/* ---- zombie: forward-integrate the EntityLivingBase spine, diff bit-exact ---- */

/* Collect the solid arena cells the entity's motion-expanded box can touch, as world-coord
 * PcfBlocks for pcf_entity_move. Capped at PCF_MAX_BLOCKS (the local box only spans a few
 * cells, so a sealed pen never overflows). */
static int et_collect_blocks(const Scen *s, const EbLiving *e, PcfBlock *out) {
    McAABB q = mc_aabb_addcoord(&e->base.phys.box, e->base.phys.motionX,
                               e->base.phys.motionY, e->base.phys.motionZ);
    int x0 = mc_floor(q.minX) - 1, x1 = mc_floor(q.maxX) + 1;
    int y0 = mc_floor(q.minY) - 1, y1 = mc_floor(q.maxY) + 1;
    int z0 = mc_floor(q.minZ) - 1, z1 = mc_floor(q.maxZ) + 1;
    int x, y, z, n = 0;
    for (y = y0; y <= y1; ++y)
        for (z = z0; z <= z1; ++z)
            for (x = x0; x <= x1; ++x) {
                int id = et_get(s, x, y, z) >> 4;
                if (id == 0) continue;
                if (n >= PCF_MAX_BLOCKS) return n;
                out[n].block_id = id;
                out[n].ox = (double)x; out[n].oy = (double)y; out[n].oz = (double)z;
                out[n].ladder_facing = 0;
                ++n;
            }
    return n;
}

/* Raw slipperiness of the block under the feet (posX, box.minY-1, posZ), as read by
 * moveEntityWithHeading. Default 0.6; ice family 0.98; slime 0.8. */
static float et_ground_slip(const Scen *s, const EbLiving *e) {
    int bx = mc_floor(e->base.phys.posX);
    int by = mc_floor(e->base.phys.box.minY - 1.0);
    int bz = mc_floor(e->base.phys.posZ);
    int id = et_get(s, bx, by, bz) >> 4;
    if (id == 79 || id == 174 || id == 212) return 0.98f; /* ice / packed ice / frosted ice */
    if (id == 165) return 0.8f;                           /* slime block */
    return 0.6f;
}

/* zombie_drop: ai=0 header -> PASS-REQUIRED regression gate (bit-exact vertical physics).
 * zombie_ai_pen: ai=1 header -> INFORMATIONAL divergence profile (no faithful AI this round).
 * The header `ai` field selects the verdict mode ONLY - it never gates the physics (the
 * 1.11.2 isServerWorld NoAI gate is a distinct concept; the drop was captured AI-on/player-far,
 * so we drive isServerWorld=1 with zero AI intents). */
static int run_zombie(Scen *s) {
    EbLiving e;
    PcfBlock blocks[PCF_MAX_BLOCKS];
    int t, informational = s->ai, pass = 1, first_tick = -1;
    char first_field[8] = {0};
    i64 first_j = 0, first_c = 0;
    int last_ok = 0;

    /* EntityZombie.setSize(0.6F, 1.95F). Init from frame 0's raw state (motion already has one
     * server update applied at capture time, exactly like the arrow path). */
    elb_init(&e, 0.6f, 1.95f, b2d(s->x[0]), b2d(s->y[0]), b2d(s->z[0]));
    e.base.phys.motionX = b2d(s->mx[0]);
    e.base.phys.motionY = b2d(s->my[0]);
    e.base.phys.motionZ = b2d(s->mz[0]);
    e.base.phys.onGround = s->onGround[0] > 0 ? 1 : 0;
    e.base.rotationYaw = b2f(s->yaw[0]);
    e.isServerWorld = 1;               /* AI-enabled server mob (see note above) */

    for (t = 1; t < s->nframes; ++t) {
        int nd = s->toff[t] - s->toff[t - 1], d, bad = 0;
        if (nd < 1) nd = 1;
        for (d = 0; d < nd; ++d) {
            float slip = et_ground_slip(s, &e);
            int nb = et_collect_blocks(s, &e, blocks);
            /* No faithful AI wired: intents stay 0 (zombie_drop has no target in range; the
             * zombie_ai_pen pursuit is the documented informational divergence). */
            eb_tick_living(&e, slip, 0 /*isMovementBlocked*/, blocks, nb, sin_table_ptr());
        }
        bad |= cmp_field("posX", s->toff[t], s->x[t], e.base.phys.posX, &first_tick, first_field, &first_j, &first_c);
        bad |= cmp_field("posY", s->toff[t], s->y[t], e.base.phys.posY, &first_tick, first_field, &first_j, &first_c);
        bad |= cmp_field("posZ", s->toff[t], s->z[t], e.base.phys.posZ, &first_tick, first_field, &first_j, &first_c);
        bad |= cmp_field("motX", s->toff[t], s->mx[t], e.base.phys.motionX, &first_tick, first_field, &first_j, &first_c);
        bad |= cmp_field("motY", s->toff[t], s->my[t], e.base.phys.motionY, &first_tick, first_field, &first_j, &first_c);
        bad |= cmp_field("motZ", s->toff[t], s->mz[t], e.base.phys.motionZ, &first_tick, first_field, &first_j, &first_c);
        if ((s->onGround[t] > 0 ? 1 : 0) != e.base.phys.onGround) {
            if (first_tick < 0) { first_tick = s->toff[t]; strcpy(first_field, "onGrnd");
                first_j = s->onGround[t]; first_c = e.base.phys.onGround; }
            bad = 1;
        }
        if (bad) pass = 0; else last_ok = s->toff[t];
    }

    printf("=== %-16s [zombie ai=%d]  arena %dx%dx%d  %d frames (through tick %d) ===\n",
           s->name, s->ai, s->nx, s->ny, s->nz, s->nframes, s->toff[s->nframes - 1]);

    if (informational) {
        /* zombie_ai_pen: report the AI divergence profile (spine has no faithful AI). */
        int first_move = -1;
        double x0 = b2d(s->x[0]), z0 = b2d(s->z[0]);
        double xf = b2d(s->x[s->nframes - 1]), zf = b2d(s->z[s->nframes - 1]);
        for (t = 1; t < s->nframes; ++t)
            if (s->x[t] != s->x[0] || s->z[t] != s->z[0]) { first_move = s->toff[t]; break; }
        printf("  INFORMATIONAL: EntityLivingBase physics spine ticks, but AI (target/path/melee)\n");
        printf("    is NOT ported this round - intents stay 0, so the spine only falls+rests.\n");
        if (first_tick >= 0) {
            printf("  first divergence vs Java at tick %d, field %s:\n", first_tick, first_field);
            printf("    Java = %.17g  (raw %lld)\n", b2d(first_j), (long long)first_j);
            printf("    C    = %.17g  (raw %lld)\n", b2d(first_c), (long long)first_c);
        } else {
            printf("  (no divergence in window - unexpected for an AI scenario)\n");
        }
        printf("  Java AI pursuit: first horizontal move at tick %d; start (x=%.4f,z=%.4f) -> "
               "end (x=%.4f,z=%.4f) over %d ticks (C spine stays in its x/z column: pursuit vs idle)\n",
               first_move, x0, z0, xf, zf, s->toff[s->nframes - 1]);
        printf("  VERDICT: INFORMATIONAL (AI divergence baseline - not counted as a regression)\n");
        return 0;
    }

    /* zombie_drop: bit-exact regression gate. */
    if (pass) {
        printf("  BIT-EXACT: C == Java pos+motion+onGround for all %d frames\n", s->nframes);
        printf("  final: onGround=%d posY=%.9f motionY=%.12f\n",
               e.base.phys.onGround, e.base.phys.posY, e.base.phys.motionY);
    } else {
        printf("  DIVERGENCE at tick %d, field %s:\n", first_tick, first_field);
        printf("    Java = %.17g  (raw %lld)\n", b2d(first_j), (long long)first_j);
        printf("    C    = %.17g  (raw %lld)\n", b2d(first_c), (long long)first_c);
        printf("    last bit-exact tick: %d\n", last_ok);
    }
    printf("  VERDICT: %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

static int run_scenario(const char *path) {
    Scen *s = (Scen *)calloc(1, sizeof(Scen));
    int vol = load_scen(path, s), rc;
    if (vol < 0) { free(s); return 2; }
    if (!strcmp(s->kind, "arrow")) rc = run_arrow(s);
    else if (!strcmp(s->kind, "zombie")) rc = run_zombie(s);
    else {
        /* item / xporb scenarios belong to item_trace_verify.c - skip here so a glob
         * of scenarios/ stays a clean gate for the arrow+zombie harness. */
        printf("=== %-16s [%s]  SKIP (use item_trace_verify)\n", s->name, s->kind);
        rc = 0;
    }
    free(s->blocks); free(s);
    return rc;
}

int main(int argc, char **argv) {
    int i, rc = 0;
    if (argc < 2) { fprintf(stderr, "usage: %s <scenario.jsonl> [...]\n", argv[0]); return 2; }
    for (i = 1; i < argc; ++i) {
        int r = run_scenario(argv[i]);
        if (r > rc) rc = r;
    }
    return rc;
}
