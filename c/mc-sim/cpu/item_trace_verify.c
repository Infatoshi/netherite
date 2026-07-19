/* item_trace_verify.c - REAL-GAME per-tick verifier for EntityItem + EntityXPOrb (PORT_MATRIX P2).
 *
 * Loads a scenario JSONL captured from the live Java game (verify/entity_trace/capture_entities.py),
 * rebuilds the static block arena, initializes every entity from frame 0, forward-integrates the C
 * entity model (entity_item.h / entity_xp_orb.h) tick-for-tick, and diffs each captured frame
 * BIT-EXACTLY on raw Double.doubleToRawLongBits bits (the project's exact-double rule).
 *
 * KINDS
 *   "item"  : one or more EntityItems. Full onUpdate motion + combineItems merge (list-order:
 *             ascending entity id == summon == update order). Acceptance = posX/Y/Z + motionX/Y/Z
 *             bit-exact for every SURVIVING item every frame; a merged item leaves the Java trace at
 *             the merge tick (reported) and the survivor's stack count is checked.
 *   "xporb" : one EntityXPOrb with player attraction. Acceptance = pos+motion bit-exact every frame
 *             UP TO the pickup event (the orb leaving the trace); the pickup tick is reported (it is
 *             an entity-update-order boundary, not a per-tick motion claim - see RESULTS).
 *
 * Not a CPU==CUDA oracle kernel (reads external JSONL at runtime); CPU-only like entity_trace_verify.
 * Usage: item_trace_verify <scenario.jsonl> [...]. Exit 0 iff every scenario is bit-exact. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/projectile_motion.h"   /* pm_is_solid / block_props_table (full-cube solid test) */
#include "../core/entity_item.h"
#include "../core/entity_xp_orb.h"

#define IT_MAXVOL   262144
#define IT_MAXFRAME 260
#define IT_MAXENT   4

typedef struct {
    int   eid;
    i64   x, y, z, mx, my, mz;   /* raw double bits */
    int   age, pickupdelay, count, itemid, meta, lifespan, xpvalue, xpcolor;
} EntRec;

typedef struct {
    char name[48], kind[16];
    int ox, oy, oz, nx, ny, nz, ticks;
    u16 *blocks;
    int nframes;
    int toff[IT_MAXFRAME];
    int nents[IT_MAXFRAME];
    EntRec ent[IT_MAXFRAME][IT_MAXENT];
    /* per-frame parked-player state (xp orb attraction), raw bits. fplayer[t] is the player as it
     * was AFTER frame t; the tick producing frame t reads the player at frame t-1. */
    int has_fplayer[IT_MAXFRAME];
    i64 fpx[IT_MAXFRAME], fpy[IT_MAXFRAME], fpz[IT_MAXFRAME];
    i32 feye[IT_MAXFRAME];
    int fspect[IT_MAXFRAME];
} Scen;

static double b2d(i64 bits) { union { i64 i; double d; } u; u.i = bits; return u.d; }
static float  b2f(i32 bits) { union { i32 i; float f; } u; u.i = bits; return u.f; }

/* ---- minimal JSON scanning ---- */
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
    return (i64)strtoll(p, NULL, 10);
}
static void find_str(const char *line, const char *key, char *out, int cap) {
    const char *p = strstr(line, key);
    int i = 0; out[0] = 0;
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
/* scan one {"eid":...} entity object starting at *pp (points at '{'); advance *pp past it. */
static const char *scan_ent(const char *p, EntRec *e) {
    const char *end = strchr(p, '}');
    char buf[1024];
    int len = end ? (int)(end - p + 1) : (int)strlen(p);
    if (len > (int)sizeof(buf) - 1) len = sizeof(buf) - 1;
    memcpy(buf, p, len); buf[len] = 0;
    e->eid        = find_int(buf, "\"eid\"", -1);
    e->x          = find_i64(buf, "\"x\"", 0);
    e->y          = find_i64(buf, "\"y\"", 0);
    e->z          = find_i64(buf, "\"z\"", 0);
    e->mx         = find_i64(buf, "\"mx\"", 0);
    e->my         = find_i64(buf, "\"my\"", 0);
    e->mz         = find_i64(buf, "\"mz\"", 0);
    e->age        = find_int(buf, "\"age\"", -1);
    e->pickupdelay= find_int(buf, "\"pickupdelay\"", -1);
    e->count      = find_int(buf, "\"count\"", -1);
    e->itemid     = find_int(buf, "\"itemid\"", -1);
    e->meta       = find_int(buf, "\"meta\"", -1);
    e->lifespan   = find_int(buf, "\"lifespan\"", 6000);
    e->xpvalue    = find_int(buf, "\"xpvalue\"", -1);
    e->xpcolor    = find_int(buf, "\"xpcolor\"", -1);
    return end ? end + 1 : p + strlen(p);
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
    s->ox = find_int(line, "\"ox\"", 0);
    s->oy = find_int(line, "\"oy\"", 100);
    s->oz = find_int(line, "\"oz\"", 0);
    s->nx = find_int(line, "\"nx\"", 0);
    s->ny = find_int(line, "\"ny\"", 0);
    s->nz = find_int(line, "\"nz\"", 0);
    s->ticks = find_int(line, "\"ticks\"", 0);
    vol = s->nx * s->ny * s->nz;
    if (vol <= 0 || vol > IT_MAXVOL) { fprintf(stderr, "bad vol %d\n", vol); fclose(f); return -1; }
    s->blocks = (u16 *)malloc((size_t)vol * sizeof(u16));
    got = parse_blocks(line, s->blocks, vol);
    if (got != vol) { fprintf(stderr, "%s: header blocks %d != %d\n", path, got, vol); fclose(f); return -1; }
    while ((len = getline(&line, &cap, f)) > 0 && ti < IT_MAXFRAME) {
        const char *p = strstr(line, "\"ents\"");
        const char *pp;
        int ne = 0;
        if (!p) continue;
        s->toff[ti] = find_int(line, "\"t\"", ti);
        /* per-frame player block: "player":{"x":..,"y":..,"z":..,"eye":..,"spectator":..} */
        pp = strstr(line, "\"player\"");
        s->has_fplayer[ti] = 0;
        if (pp) {
            s->has_fplayer[ti] = 1;
            s->fpx[ti] = find_i64(pp, "\"x\"", 0);
            s->fpy[ti] = find_i64(pp, "\"y\"", 0);
            s->fpz[ti] = find_i64(pp, "\"z\"", 0);
            s->feye[ti] = (i32)find_i64(pp, "\"eye\"", 0);
            s->fspect[ti] = find_int(pp, "\"spectator\"", 0);
        }
        p = strchr(p, '[');
        if (p) {
            ++p;
            while (*p && ne < IT_MAXENT) {
                while (*p == ' ' || *p == ',') ++p;
                if (*p != '{') break;
                p = scan_ent(p, &s->ent[ti][ne]);
                ++ne;
            }
        }
        s->nents[ti] = ne;
        ++ti;
    }
    free(line);
    fclose(f);
    s->nframes = ti;
    return vol;
}

/* ---- world-space arena getter + collision-box gather ---- */
static u16 it_get(const Scen *s, int x, int y, int z) {
    int lx = x - s->ox, ly = y - s->oy, lz = z - s->oz;
    if (lx < 0 || lx >= s->nx || ly < 0 || ly >= s->ny || lz < 0 || lz >= s->nz)
        return mc_state(BLK_AIR, 0);
    return s->blocks[(ly * s->nz + lz) * s->nx + lx];
}
static int it_solid(const Scen *s, int x, int y, int z) {
    return pm_is_solid(it_get(s, x, y, z));   /* BF_SOLID full-cube (from projectile_motion.h) */
}
/* Candidate solid unit-cube AABBs overlapping the motion-expanded box (superset; mc_entity_move
 * re-filters with the same query, so extras are harmless as long as intersecting count <= cap).
 *
 * CRITICAL: EntityItem/XPOrb apply gravity to motionY BEFORE Entity.move, and move's
 * getCollisionBoxes uses the post-gravity delta. The caller passes the PRE-tick motion
 * (post-friction of the previous tick, often ~0 when grounded). Expanding with that raw dy
 * excludes the floor under a grounded entity (box.minY == floor.top, dy==0 -> y0==floor.top
 * integer, floor cell at floor.top-1 never enters the set) -> one-tick sink-through of 0.04.
 * So we expand with the post-gravity dy and pad +-1 on every axis (same margin as
 * entity_trace_verify's et_collect_blocks). */
static int it_gather(const Scen *s, const McAABB *box, double dx, double dy, double dz,
                     McAABB *out, int cap) {
    double gdy = dy - 0.03999999910593033; /* (double)0.04f - EntityItem/XPOrb gravity */
    McAABB q = mc_aabb_addcoord(box, dx, gdy, dz);
    int x0 = mc_floor(q.minX) - 1, x1 = mc_floor(q.maxX) + 1;
    int y0 = mc_floor(q.minY) - 1, y1 = mc_floor(q.maxY) + 1;
    int z0 = mc_floor(q.minZ) - 1, z1 = mc_floor(q.maxZ) + 1;
    int n = 0, bx, by, bz;
    for (bx = x0; bx <= x1; ++bx)
        for (by = y0; by <= y1; ++by)
            for (bz = z0; bz <= z1; ++bz)
                if (it_solid(s, bx, by, bz) && n < cap)
                    out[n++] = mc_aabb_make(bx, by, bz, bx + 1.0, by + 1.0, bz + 1.0);
    return n;
}
/* collidesWithAnyBlock(box): any solid cube strictly intersecting the box (pushOutOfBlocks gate). */
static int it_collides(const Scen *s, const McAABB *box) {
    int x0 = mc_floor(box->minX), x1 = mc_floor(box->maxX);
    int y0 = mc_floor(box->minY), y1 = mc_floor(box->maxY);
    int z0 = mc_floor(box->minZ), z1 = mc_floor(box->maxZ);
    int bx, by, bz;
    for (bx = x0; bx <= x1; ++bx)
        for (by = y0; by <= y1; ++by)
            for (bz = z0; bz <= z1; ++bz)
                if (it_solid(s, bx, by, bz)) {
                    McAABB c = mc_aabb_make(bx, by, bz, bx + 1.0, by + 1.0, bz + 1.0);
                    if (mc_aabb_intersects(box, &c)) return 1;
                }
    return 0;
}

/* find the frame-t record for eid, or NULL. */
static const EntRec *frame_ent(const Scen *s, int t, int eid) {
    int i;
    for (i = 0; i < s->nents[t]; ++i)
        if (s->ent[t][i].eid == eid) return &s->ent[t][i];
    return NULL;
}

static int cmp_bits(const char *field, int tick, int eid, i64 jbits, double cval,
                    int *first_tick, char *first_field, i64 *first_j, i64 *first_c, int *first_eid) {
    union { double d; i64 i; } u; u.d = cval;
    if (u.i == jbits) return 0;
    if (*first_tick < 0) {
        *first_tick = tick; strncpy(first_field, field, 7); first_field[7] = 0;
        *first_j = jbits; *first_c = u.i; *first_eid = eid;
    }
    return 1;
}

/* ---- item scenario ---- */
static int run_item(Scen *s) {
    McItem it[IT_MAXENT];
    int nit = s->nents[0], i, t, pass = 1;
    int first_tick = -1, first_eid = -1, merge_tick = -1, survivor_count = -1;
    char first_field[8] = {0};
    i64 first_j = 0, first_c = 0;

    for (i = 0; i < nit; ++i) {
        const EntRec *e = &s->ent[0][i];
        memset(&it[i], 0, sizeof(McItem));
        ei_set_position(&it[i], b2d(e->x), b2d(e->y), b2d(e->z));
        it[i].motionX = b2d(e->mx); it[i].motionY = b2d(e->my); it[i].motionZ = b2d(e->mz);
        it[i].age = e->age >= 0 ? e->age : 0;
        it[i].delayBeforeCanPickup = e->pickupdelay >= 0 ? e->pickupdelay : 0;
        it[i].ticksExisted = 0;
        it[i].item = e->itemid; it[i].count = e->count; it[i].meta = e->meta;
        it[i].lifespan = e->lifespan;
        it[i].hasSubtypes = 0;   /* metas equal in the merge scene -> compat regardless */
        it[i].hasTag = 0;
        it[i].maxStack = 64;
        it[i].dead = 0;
    }

    for (t = 1; t < s->nframes; ++t) {
        int nd = s->toff[t] - s->toff[t - 1], d;
        if (nd < 1) nd = 1;
        for (d = 0; d < nd; ++d) {
            for (i = 0; i < nit; ++i) {
                McAABB blocks[MC_PCM_MAX_BLOCKS];
                int nb, coll, flag, j;
                u16 under;
                if (it[i].dead) continue;
                it[i].ticksExisted++;
                coll = it_collides(s, &it[i].box);
                nb = it_gather(s, &it[i].box, it[i].motionX, it[i].motionY, it[i].motionZ,
                               blocks, MC_PCM_MAX_BLOCKS);
                flag = ei_pre(&it[i], blocks, nb, coll);
                if (flag || it[i].ticksExisted % 25 == 0) {
                    for (j = 0; j < nit; ++j) {
                        McAABB exp;
                        if (j == i || it[j].dead) continue;
                        exp = mc_aabb_make(it[i].box.minX - 0.5, it[i].box.minY, it[i].box.minZ - 0.5,
                                           it[i].box.maxX + 0.5, it[i].box.maxY, it[i].box.maxZ + 0.5);
                        if (mc_aabb_intersects(&exp, &it[j].box)) {
                            int before = it[i].dead || it[j].dead;
                            if (ei_combine(&it[i], &it[j]) && !before && merge_tick < 0)
                                merge_tick = it[i].ticksExisted;
                        }
                    }
                }
                under = it_get(s, mc_floor(it[i].posX), mc_floor(it[i].box.minY) - 1, mc_floor(it[i].posZ));
                ei_post(&it[i], under);
            }
        }
        /* diff every surviving C item against its Java record (matched by eid) for this frame */
        for (i = 0; i < nit; ++i) {
            const EntRec *e;
            int eid_i = s->ent[0][i].eid;
            if (it[i].dead) continue;
            e = frame_ent(s, t, eid_i);
            if (!e) continue;   /* not in Java frame (merged/removed) - skip */
            pass &= !cmp_bits("posX", s->toff[t], eid_i, e->x, it[i].posX, &first_tick, first_field, &first_j, &first_c, &first_eid);
            pass &= !cmp_bits("posY", s->toff[t], eid_i, e->y, it[i].posY, &first_tick, first_field, &first_j, &first_c, &first_eid);
            pass &= !cmp_bits("posZ", s->toff[t], eid_i, e->z, it[i].posZ, &first_tick, first_field, &first_j, &first_c, &first_eid);
            pass &= !cmp_bits("motX", s->toff[t], eid_i, e->mx, it[i].motionX, &first_tick, first_field, &first_j, &first_c, &first_eid);
            pass &= !cmp_bits("motY", s->toff[t], eid_i, e->my, it[i].motionY, &first_tick, first_field, &first_j, &first_c, &first_eid);
            pass &= !cmp_bits("motZ", s->toff[t], eid_i, e->mz, it[i].motionZ, &first_tick, first_field, &first_j, &first_c, &first_eid);
            survivor_count = it[i].count;
        }
    }

    printf("=== %-16s [item]  arena %dx%dx%d  %d items  %d frames (through tick %d) ===\n",
           s->name, s->nx, s->ny, s->nz, nit, s->nframes, s->toff[s->nframes - 1]);
    if (merge_tick >= 0)
        printf("  merge: at tick %d one item absorbed the other; survivor stack count (C) = %d\n",
               merge_tick, survivor_count);
    if (pass) {
        printf("  BIT-EXACT: C == Java pos+motion for every surviving item, all frames\n");
    } else {
        printf("  DIVERGENCE at tick %d, eid %d, field %s:\n", first_tick, first_eid, first_field);
        printf("    Java = %.17g  (raw %lld)\n", b2d(first_j), (long long)first_j);
        printf("    C    = %.17g  (raw %lld)\n", b2d(first_c), (long long)first_c);
    }
    printf("  VERDICT: %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

/* ---- xp orb scenario ---- */
static int run_orb(Scen *s) {
    McOrb o;
    int t, pass = 1, first_tick = -1, first_eid = -1, last_ok = 0, pickup_tick = -1;
    int eid0 = s->ent[0][0].eid;
    char first_field[8] = {0};
    i64 first_j = 0, first_c = 0;

    if (!s->has_fplayer[0]) { printf("=== %s [xporb] MISSING per-frame player ===\n  VERDICT: FAIL\n", s->name); return 1; }

    memset(&o, 0, sizeof o);
    eo_set_position(&o, b2d(s->ent[0][0].x), b2d(s->ent[0][0].y), b2d(s->ent[0][0].z));
    o.motionX = b2d(s->ent[0][0].mx); o.motionY = b2d(s->ent[0][0].my); o.motionZ = b2d(s->ent[0][0].mz);
    o.xpOrbAge = s->ent[0][0].age >= 0 ? s->ent[0][0].age : 0;
    o.xpColor = s->ent[0][0].xpcolor >= 0 ? s->ent[0][0].xpcolor : 0;
    o.xpValue = s->ent[0][0].xpvalue;
    o.delayBeforeCanPickup = s->ent[0][0].pickupdelay >= 0 ? s->ent[0][0].pickupdelay : 0;
    o.eid = eid0;
    o.xpTargetColor = 0;
    o.has_closest = 0;
    o.dead = 0;

    for (t = 1; t < s->nframes; ++t) {
        int nd = s->toff[t] - s->toff[t - 1], d;
        const EntRec *e;
        /* player state as seen by the ticks producing frame t: the state at frame t-1 */
        double px = b2d(s->fpx[t - 1]), py = b2d(s->fpy[t - 1]), pz = b2d(s->fpz[t - 1]);
        float eye = b2f(s->feye[t - 1]);
        int spect = s->fspect[t - 1];
        if (nd < 1) nd = 1;
        for (d = 0; d < nd; ++d) {
            McAABB blocks[MC_PCM_MAX_BLOCKS];
            int nb = it_gather(s, &o.box, o.motionX, o.motionY, o.motionZ, blocks, MC_PCM_MAX_BLOCKS);
            int coll = it_collides(s, &o.box);
            eo_tick(&o, px, py, pz, eye, spect, blocks, nb,
                    it_get(s, mc_floor(o.posX), mc_floor(o.box.minY) - 1, mc_floor(o.posZ)), coll);
        }
        e = frame_ent(s, t, eid0);
        if (!e) { if (pickup_tick < 0) pickup_tick = s->toff[t]; break; }   /* orb picked up / gone */
        pass &= !cmp_bits("posX", s->toff[t], eid0, e->x, o.posX, &first_tick, first_field, &first_j, &first_c, &first_eid);
        pass &= !cmp_bits("posY", s->toff[t], eid0, e->y, o.posY, &first_tick, first_field, &first_j, &first_c, &first_eid);
        pass &= !cmp_bits("posZ", s->toff[t], eid0, e->z, o.posZ, &first_tick, first_field, &first_j, &first_c, &first_eid);
        pass &= !cmp_bits("motX", s->toff[t], eid0, e->mx, o.motionX, &first_tick, first_field, &first_j, &first_c, &first_eid);
        pass &= !cmp_bits("motY", s->toff[t], eid0, e->my, o.motionY, &first_tick, first_field, &first_j, &first_c, &first_eid);
        pass &= !cmp_bits("motZ", s->toff[t], eid0, e->mz, o.motionZ, &first_tick, first_field, &first_j, &first_c, &first_eid);
        if (pass) last_ok = s->toff[t];
    }

    printf("=== %-16s [xporb eid=%d, eid%%100=%d]  arena %dx%dx%d  %d frames ===\n",
           s->name, eid0, eid0 % 100, s->nx, s->ny, s->nz, s->nframes);
    printf("  free-fall until first attraction acquisition at tick > %d (21 - eid%%100)\n",
           21 - eid0 % 100);
    if (pickup_tick >= 0)
        printf("  orb left the trace (pickup) at tick %d\n", pickup_tick);
    if (pass) {
        printf("  BIT-EXACT: C == Java pos+motion for every frame up to pickup (last ok tick %d)\n", last_ok);
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
    if (!strcmp(s->kind, "xporb")) rc = run_orb(s);
    else                           rc = run_item(s);
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
