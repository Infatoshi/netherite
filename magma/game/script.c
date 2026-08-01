#include "game/script.h"
#include "game/runtime.h"
/* after runtime.h: entity_render.h's guarded GmEntityView redecl must see
 * game.h's full definition (MAGMA_GAME_H) or the types conflict. */
#include "game/entity_render.h"
#include "game/frame_capture.h"
#include "game/hand.h"
#include "game/particles_live.h"
#include "game/screen.h"
#include "game/sel_box.h"
#include "game/window_compose.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JL_FIELDS 32
#define JL_KEY 32
#define JL_VALUE 128

typedef struct { char key[JL_KEY]; char value[JL_VALUE]; int string; } JlField;
typedef struct { JlField f[JL_FIELDS]; int n; } JlObject;

static const char *skip_ws(const char *p) { while (*p && isspace((unsigned char)*p)) p++; return p; }

static int parse_string(const char **pp, char *out, int cap) {
    const char *p = *pp; int n = 0;
    if (*p++ != '"') return 0;
    while (*p && *p != '"') {
        if (*p == '\\' || (unsigned char)*p < 32 || n + 1 >= cap) return 0;
        out[n++] = *p++;
    }
    if (*p++ != '"') return 0;
    out[n] = 0; *pp = p; return 1;
}

static int parse_object(const char *line, JlObject *o, char *err, int cap) {
    const char *p = skip_ws(line); o->n = 0;
    if (*p++ != '{') { snprintf(err, cap, "expected JSON object"); return 0; }
    p = skip_ws(p);
    if (*p == '}') p++;
    else for (;;) {
        if (o->n >= JL_FIELDS) { snprintf(err, cap, "too many fields"); return 0; }
        JlField *f = &o->f[o->n++];
        if (!parse_string(&p, f->key, sizeof f->key)) { snprintf(err, cap, "invalid key"); return 0; }
        p = skip_ws(p);
        if (*p++ != ':') { snprintf(err, cap, "expected colon"); return 0; }
        p = skip_ws(p); f->string = *p == '"';
        if (f->string) {
            if (!parse_string(&p, f->value, sizeof f->value)) { snprintf(err, cap, "invalid string"); return 0; }
        } else {
            int n = 0;
            while (*p && *p != ',' && *p != '}' && !isspace((unsigned char)*p)) {
                if (n + 1 >= (int)sizeof f->value) { snprintf(err, cap, "value too long"); return 0; }
                f->value[n++] = *p++;
            }
            f->value[n] = 0;
            if (!n) { snprintf(err, cap, "missing value"); return 0; }
        }
        p = skip_ws(p);
        if (*p == '}') { p++; break; }
        if (*p++ != ',') { snprintf(err, cap, "expected comma"); return 0; }
        p = skip_ws(p);
    }
    p = skip_ws(p);
    if (*p) { snprintf(err, cap, "trailing JSON data"); return 0; }
    for (int i = 0; i < o->n; ++i)
        for (int j = i + 1; j < o->n; ++j)
            if (!strcmp(o->f[i].key, o->f[j].key)) {
                snprintf(err, cap, "duplicate field: %s", o->f[i].key); return 0;
            }
    return 1;
}

static const JlField *field(const JlObject *o, const char *key) {
    for (int i = 0; i < o->n; ++i) if (!strcmp(o->f[i].key, key)) return &o->f[i];
    return NULL;
}

static int keys_only(const JlObject *o, const char *const *keys, int n,
                     char *err, int cap) {
    for (int i = 0; i < o->n; ++i) {
        int ok = 0;
        for (int j = 0; j < n; ++j) if (!strcmp(o->f[i].key,keys[j])) { ok=1; break; }
        if (!ok) { snprintf(err,cap,"unknown or forbidden field: %s",o->f[i].key); return 0; }
    }
    return 1;
}

static int as_i64(const JlField *f, long long *v) {
    char *e = NULL; errno = 0;
    if (!f || f->string || !f->value[0]) return 0;
    long long x = strtoll(f->value, &e, 10);
    if (errno || e == f->value || *e) return 0;
    *v = x; return 1;
}
static int as_double(const JlField *f, double *v) {
    char *e = NULL; errno = 0;
    if (!f || f->string || !f->value[0]) return 0;
    double x = strtod(f->value, &e);
    if (errno || e == f->value || *e || !isfinite(x)) return 0;
    *v = x; return 1;
}
static int as_string(const JlField *f, const char **v) {
    if (!f || !f->string) return 0;
    *v = f->value;
    return 1;
}

static int as_rule_bool(const JlField *f, int *v) {
    if (!f) return 1;
    if (!strcmp(f->value,"true")) { *v=1; return 1; }
    if (!strcmp(f->value,"false")) { *v=0; return 1; }
    return 0;
}

static int known_action_key(const char *k) {
    static const char *keys[] = {"tick","type","forward","strafe","dyaw","dpitch",
        "jump","sneak","sprint","attack","use","do_break","do_place","hotbar",
        "inv_slot","inv_button","inv_type"};
    for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; ++i) if (!strcmp(k, keys[i])) return 1;
    return 0;
}

static int parse_action(const JlObject *o, GmAction *a, char *err, int cap) {
    memset(a, 0, sizeof *a); a->hotbar_sel = -1;
    for (int i = 0; i < o->n; ++i)
        if (!known_action_key(o->f[i].key)) {
            snprintf(err, cap, "unknown or forbidden action field: %s", o->f[i].key); return 0;
        }
    double d; long long n;
#define NUM(K, DST) do { const JlField *q=field(o,K); if(q){if(!as_double(q,&d)){snprintf(err,cap,"invalid %s",K);return 0;} DST=(float)d;} }while(0)
#define INT(K, DST) do { const JlField *q=field(o,K); if(q){if(!as_i64(q,&n)){snprintf(err,cap,"invalid %s",K);return 0;} DST=(int)n;} }while(0)
    NUM("forward", a->forward); NUM("strafe", a->strafe); NUM("dyaw", a->dyaw); NUM("dpitch", a->dpitch);
    INT("jump", a->jump); INT("sneak", a->sneak); INT("sprint", a->sprint);
    INT("attack", a->attack); INT("use", a->use); INT("do_break", a->do_break);
    INT("do_place", a->do_place); INT("hotbar", a->hotbar_sel);
#undef NUM
#undef INT
    /* Container.slotClick as a SURVIVAL action: inv_slot present -> one click of
     * (inv_slot, inv_button, inv_type) through gm_container_click this tick. */
    { const JlField *sf = field(o, "inv_slot");
      if (sf) {
          long long slot, button = 0, ctype = 0;
          if (!as_i64(sf, &slot) ||
              !(slot == GMC_OUTSIDE || (slot >= 0 && slot < GMC_SLOT_COUNT))) {
              snprintf(err, cap, "invalid inv_slot"); return 0;
          }
          const JlField *bf = field(o, "inv_button");
          const JlField *tf = field(o, "inv_type");
          if ((bf && (!as_i64(bf, &button) || (button != 0 && button != 1))) ||
              (tf && (!as_i64(tf, &ctype) ||
                      (ctype != 0 && ctype != 1 && ctype != 4)))) {
              snprintf(err, cap, "invalid inv_button/inv_type"); return 0;
          }
          a->inv_click = 1; a->inv_slot = (int)slot;
          a->inv_button = (int)button; a->inv_type = (int)ctype;
      } else if (field(o, "inv_button") || field(o, "inv_type")) {
          snprintf(err, cap, "inv_button/inv_type require inv_slot"); return 0;
      } }
    if (a->forward < -1 || a->forward > 1 || a->strafe < -1 || a->strafe > 1 ||
        a->hotbar_sel < -1 || a->hotbar_sel > 8) {
        snprintf(err, cap, "action value out of range"); return 0;
    }
    return 1;
}

static int parse_craft(const JlObject *o, int *width, int slots[9], char *err, int cap) {
    long long n;
    const JlField *wf = field(o, "width");
    if (!as_i64(wf, &n) || (n != 2 && n != 3)) { snprintf(err,cap,"craft width must be 2 or 3"); return 0; }
    *width = (int)n;
    for (int i = 0; i < 9; ++i) slots[i] = -1;
    for (int i = 0; i < o->n; ++i) {
        const char *k = o->f[i].key;
        if (!strcmp(k,"tick") || !strcmp(k,"type") || !strcmp(k,"width")) continue;
        if (strncmp(k,"grid",4) || strlen(k) != 5 || k[4] < '0' || k[4] > '8') {
            snprintf(err,cap,"unknown or forbidden craft field: %s",k); return 0;
        }
        if (!as_i64(&o->f[i],&n) || n < -1 || n >= ISR_MAIN_SLOTS) {
            snprintf(err,cap,"invalid %s inventory slot",k); return 0;
        }
        slots[k[4]-'0'] = (int)n;
    }
    return 1;
}

/* FNV-1a over the 9x9x9 id/meta volume around the player. Anchored at the
 * double-precision sim feet position (not the float render view): the Java
 * recorder computes the identical digest from floor(posX/Y/Z), and a float
 * round-trip can flip floor() at block boundaries. Java mirror:
 * Recorder.recordTick "wfnv". Iteration order and value packing must stay
 * bit-equal on both sides.
 * The basis below is NOT standard FNV-1a (last digit of ...6037 dropped,
 * historic); it only has to keep matching the Java mirror. */
static unsigned long long nearby_hash(const GmRuntime *r, int anchor[3]) {
    unsigned long long h = 1469598103934665603ULL;
    int cx = (int)floor(r->player.ent.posX + (double)r->ox);
    int cy = (int)floor(r->player.ent.posY);
    int cz = (int)floor(r->player.ent.posZ + (double)r->oz);
    anchor[0] = cx; anchor[1] = cy; anchor[2] = cz;
    for (int z = cz - 4; z <= cz + 4; ++z)
        for (int y = cy - 4; y <= cy + 4; ++y)
            for (int x = cx - 4; x <= cx + 4; ++x) {
                unsigned s = (unsigned)(gm_world_block(r->world,x,y,z) << 4 |
                                        gm_world_meta(r->world,x,y,z));
                h ^= s; h *= 1099511628211ULL;
            }
    return h;
}

static void write_state(FILE *out, const GmRuntime *r) {
    GmPlayerView v; gm_runtime_view(r, &v);
    fprintf(out, "{\"version\":1,\"tick\":%lld,\"dim\":%d,\"world_time\":%lld,",
            r->tick,r->dimension,(long long)r->clock.world_time);
    fprintf(out, "\"weather\":{\"raining\":%d,\"thundering\":%d,"
                 "\"rain_time\":%d,\"thunder_time\":%d},",
            r->clock.raining,r->clock.thundering,
            r->clock.rain_time,r->clock.thunder_time);
    /* Position from the double-precision sim state, NOT the float render view:
     * MC positions are doubles and the tape differ works at 1e-9, so the float
     * round-trip alone showed up as a fake tick-0 divergence (3e-6). */
    fprintf(out, "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,",
            r->player.ent.posX + (double)r->ox,
            r->player.ent.posY,
            r->player.ent.posZ + (double)r->oz);
    fprintf(out, "\"yaw\":%.9g,\"pitch\":%.9g,", (double)v.yaw,(double)v.pitch);
    fprintf(out, "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,",
            r->player.ent.motionX,r->player.ent.motionY,r->player.ent.motionZ);
    fprintf(out, "\"on_ground\":%d,\"health\":%.9g,\"food\":%.9g,\"xp_level\":%d,\"xp_frac\":%.9g,",
            v.on_ground,(double)v.health,(double)v.food,v.xp_level,(double)v.xp_frac);
    { int hx,hy,hz,ax,ay,az;
      int hit=gm_raycast_sel(r->window,&r->sin_table,&r->player,
                             &hx,&hy,&hz,&ax,&ay,&az);
      if(hit>=0) fprintf(out,"\"look\":{\"x\":%d,\"y\":%d,\"z\":%d,\"id\":%d},",
          hx+r->ox,hy,hz+r->oz,gm_world_block(r->world,hx+r->ox,hy,hz+r->oz));
      else fprintf(out,"\"look\":null,"); }
    fprintf(out, "\"dead\":%d,\"deaths\":%d,\"won\":%s,\"credits\":%d,\"container\":%d,",
            v.dead,v.deaths,r->won?"true":"false",r->credits,r->container);
    fprintf(out, "\"inventory\":[");
    {
        int first_inv = 1;
        for (int i = 0; i < ISR_MAIN_SLOTS; ++i) {
            ICStack s = isr_get_stack(&r->player.inv, i);
            fprintf(out, "%s{\"slot\":%d,\"item\":%d,\"count\":%d,\"meta\":%d}",
                    first_inv ? "" : ",", i, s.item, s.count, s.meta);
            first_inv = 0;
        }
        for (int i = 0; i < ISR_ARMOR_SLOTS; ++i) {
            ICStack s = isr_get_stack(&r->player.inv, ISR_ARMOR0 + i);
            fprintf(out, "%s{\"slot\":%d,\"item\":%d,\"count\":%d,\"meta\":%d}",
                    first_inv ? "" : ",", ISR_ARMOR0 + i, s.item, s.count, s.meta);
            first_inv = 0;
        }
        {
            ICStack oh = isr_get_stack(&r->player.inv, ISR_OFFHAND_SLOT);
            fprintf(out, "%s{\"slot\":%d,\"item\":%d,\"count\":%d,\"meta\":%d}",
                    first_inv ? "" : ",", ISR_OFFHAND_SLOT, oh.item, oh.count, oh.meta);
        }
    }
    fprintf(out, "],");
    { ICStack c = gm_player_cursor();
      fprintf(out, "\"cursor\":[%d,%d,%d],\"grid\":[", c.item, c.count, c.meta);
      for (int i = 0; i < 9; ++i)
          fprintf(out, "%s[%d,%d,%d]", i ? "," : "",
                  r->craft_grid[i].item, r->craft_grid[i].count, r->craft_grid[i].meta);
      ICStack res = gm_container_result(r);
      fprintf(out, "],\"craft_result\":[%d,%d,%d],", res.item, res.count, res.meta); }
    fprintf(out, "\"entities\":[");
    int first = 1;
    for (int i = 0; i < GM_LIVE_MAX; ++i) if (r->entities.ents[i].active) {
        const GmLiveEnt *e = &r->entities.ents[i];
        fprintf(out, "%s{\"type\":%d,\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                     "\"item\":%d,\"count\":%d,\"meta\":%d}",
                first ? "" : ",", e->type,e->x,e->y,e->z,e->item,e->count,e->meta);
        first = 0;
    }
    GmEntityView bosses[ED_NUM_CRYSTALS+1];
    int nboss=gm_dragon_fill_views(&r->dragon,bosses,ED_NUM_CRYSTALS+1);
    for(int i=0;i<nboss;++i){
        const GmEntityView *e=&bosses[i];
        fprintf(out,"%s{\"type\":%d,\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,\"health\":%.9g}",
                first?"":",",e->type,(double)e->x,(double)e->y,(double)e->z,(double)e->health);
        first=0;
    }
    GmEntityView mobs[EW_MAX_ENTITIES];
    int nmobs=gm_mobs_fill_views(&r->mobs,mobs,EW_MAX_ENTITIES);
    for(int i=0;i<nmobs;++i){
        const GmEntityView *e=&mobs[i];
        fprintf(out,"%s{\"type\":%d,\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,\"health\":%.9g}",
                first?"":",",e->type,(double)e->x,(double)e->y,(double)e->z,(double)e->health);
        first=0;
    }
    GmEntityView projectiles[GM_RUNTIME_PROJECTILES];
    int nprojectiles=gm_runtime_projectile_views(r,projectiles,GM_RUNTIME_PROJECTILES);
    for(int i=0;i<nprojectiles;++i){
        const GmEntityView *e=&projectiles[i];
        fprintf(out,"%s{\"type\":%d,\"x\":%.17g,\"y\":%.17g,\"z\":%.17g}",
                first?"":",",e->type,(double)e->x,(double)e->y,(double)e->z);
        first=0;
    }
    fprintf(out, "],\"furnace\":");
    if (r->active_furnace >= 0) {
        const FurnaceLive *f=&r->furnaces[r->active_furnace].state;
        fprintf(out,"{\"input\":[%d,%d,%d],\"fuel\":[%d,%d,%d],"
                    "\"output\":[%d,%d,%d],\"burn\":%d,\"cook\":%d,\"cook_total\":%d}",
                f->input.item,f->input.count,f->input.meta,
                f->fuel.item,f->fuel.count,f->fuel.meta,
                f->output.item,f->output.count,f->output.meta,
                f->burn_time,f->cook_time,f->total_cook);
    } else fprintf(out,"null");
    /* Tape-driven render ghosts, exactly as ingested: the Java-truth entity
     * rows come back out so replay can assert the tape->magma pipeline did
     * not drop, cap, or corrupt them (positions are float32 of the taped
     * doubles). */
    fprintf(out, ",\"ghost_views\":[");
    {
        GmEntityView ghosts[GM_RUNTIME_GHOST_VIEWS];
        int ng = gm_runtime_ghost_views(r, ghosts, GM_RUNTIME_GHOST_VIEWS);
        for (int i = 0; i < ng; ++i)
            fprintf(out, "%s{\"type\":%d,\"x\":%.9g,\"y\":%.9g,\"z\":%.9g}",
                    i ? "," : "", ghosts[i].type, (double)ghosts[i].x,
                    (double)ghosts[i].y, (double)ghosts[i].z);
    }
    fprintf(out, "]");
    int anchor[3];
    unsigned long long nh = nearby_hash(r, anchor);
    fprintf(out, ",\"nearby_anchor\":[%d,%d,%d]", anchor[0],anchor[1],anchor[2]);
    fprintf(out, ",\"nearby_hash\":\"%016llx\",\"terminal\":%s}\n",
            nh, v.dead ? "\"death\"" : (r->won?"\"won\"":"null"));
}

/* MAGMA_STATE_PROF=1: batched-env sizing census, printed to stderr at run
 * end. Distinct packed states (id<<4|meta, air included) per chunk and per
 * 3x3-chunk window bound the u8-palette budget; non-air 16^3 sections per
 * chunk bound section elision. Scans the 9x9 chunks around the player (must
 * be inside the generated radius). */
static void prof_scan(GmRuntime *r) {
    int pcx = (int)floor((r->player.ent.posX + (double)r->ox) / 16.0);
    int pcz = (int)floor((r->player.ent.posZ + (double)r->oz) / 16.0);
    /* render-off runs only generate the physics window; a census over
     * ungenerated (all-air) chunks would undercount everything. */
    gm_world_ensure(r->world, pcx, pcz, 5);
    static unsigned char seen[65536];
    int worst_chunk = 0, sec_tot = 0, sec_max = 0, nchunks = 0;
    for (int dz = -4; dz <= 4; ++dz) for (int dx = -4; dx <= 4; ++dx) {
        int cx = pcx + dx, cz = pcz + dz, nsec = 0, ndist = 0;
        memset(seen, 0, sizeof seen);
        for (int s = 0; s < 16; ++s) {
            int has = 0;
            for (int y = s * 16; y < s * 16 + 16; ++y)
                for (int lz = 0; lz < 16; ++lz)
                    for (int lx = 0; lx < 16; ++lx) {
                        int wx = cx * 16 + lx, wz = cz * 16 + lz;
                        unsigned st = (unsigned)((gm_world_block(r->world, wx, y, wz) << 4) |
                                                 gm_world_meta(r->world, wx, y, wz));
                        if (st) has = 1;
                        if (!seen[st]) { seen[st] = 1; ndist++; }
                    }
            nsec += has;
        }
        nchunks++; sec_tot += nsec;
        if (nsec > sec_max) sec_max = nsec;
        if (ndist > worst_chunk) worst_chunk = ndist;
    }
    int worst_win = 0;
    for (int wz = -1; wz <= 1; ++wz) for (int wx = -1; wx <= 1; ++wx) {
        int ndist = 0;
        memset(seen, 0, sizeof seen);
        for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) {
            int cx = pcx + wx + dx, cz = pcz + wz + dz;
            for (int y = 0; y < 256; ++y)
                for (int lz = 0; lz < 16; ++lz)
                    for (int lx = 0; lx < 16; ++lx) {
                        unsigned st = (unsigned)((gm_world_block(r->world, cx * 16 + lx, y, cz * 16 + lz) << 4) |
                                                 gm_world_meta(r->world, cx * 16 + lx, y, cz * 16 + lz));
                        if (!seen[st]) { seen[st] = 1; ndist++; }
                    }
        }
        if (ndist > worst_win) worst_win = ndist;
    }
    fprintf(stderr, "[state_prof] census 9x9@(%d,%d): distinct states max %d/chunk, "
            "max %d/3x3-window; non-air sections mean %.1f max %d of 16\n",
            pcx, pcz, worst_chunk, worst_win, (double)sec_tot / nchunks, sec_max);
}

int gm_script_run(const GmConfig *cfg) {
    FILE *in = NULL, *out = stdout;
    if (cfg->script_path) { in = fopen(cfg->script_path, "r"); if (!in) { perror("script"); return 1; } }
    if (cfg->state_out_path) { out = fopen(cfg->state_out_path, "w"); if (!out) { perror("state-out"); if(in)fclose(in); return 1; } }
    GmRuntime r; char err[256];
    if (!gm_runtime_init(&r, cfg, err, sizeof err)) { fprintf(stderr,"runtime: %s\n",err); return 1; }
    /* Tape replay: never run live random-tick engine (oracle world RNG is
     * unseedable; terrain evolution is carried by snapshots, not re-simulated). */
    r.randtick_enabled = 0;
    GmFrameCapture *frames=NULL;
    GmWindowCompose *window_frames=NULL;
    GmParticlesLive replay_particles;
    uint64_t replay_particle_seed =
        (uint64_t)cfg->seed ^ UINT64_C(0x7061727469636c65);
    gm_particles_live_init(&replay_particles, replay_particle_seed);
    if(cfg->frames_out_dir){
        /* The oracle's frames carry the survival HUD (hearts/hunger/hotbar/
         * crosshair); headless frames must draw it too or every whole-frame
         * diff eats the missing overlay. gm_hud_draw silently no-ops until
         * gm_hud_init has run - the interactive path inits it, this script
         * path never did (that WAS the largest pixel cluster on the 12k-tape
         * poses: 2.1/ch of pose A's 3.42, 3.8/ch of pose B's 9.01). */
        gm_hud_init();
        if(cfg->compose==GM_COMPOSE_WINDOW){
            window_frames=gm_window_compose_open(cfg,err,sizeof err);
            if(window_frames)
                gm_window_compose_bind(window_frames,&r,&replay_particles);
        }else{
            frames=gm_frame_capture_open(cfg,err,sizeof err);
            if(frames)gm_frame_capture_bind_particles(frames,&replay_particles);
        }
        if(!frames&&!window_frames){fprintf(stderr,"frames-out: %s\n",err);gm_runtime_destroy(&r);if(in)fclose(in);if(out!=stdout)fclose(out);return 1;}
    }
    char line[2048] = {0}; long line_no = 0; JlObject pending; int have = 0;
    long long pending_tick = -1;
    /* Saturated FoodStats regeneration is server-side, but tape rows are
     * client ticks. Preserve an early local heal's hidden exhaustion/timer
     * effects while deferring its visible health until the recorded packet. */
    float held_regen = 0.0f;
    int continue_after_death = 0;
    /* MAGMA_STATE_PROF: per-tick world-edit rate. gm_world_block_gen counts
     * every block edit (set_block_meta + populate gen events), so its per-tick
     * delta = journal entries/tick for a dirty-edit journal. Baseline taken
     * here so worldgen's one-shot fill is excluded. */
    int prof_on = getenv("MAGMA_STATE_PROF") != NULL;
    long long prof_last = 0, prof_tot = 0, prof_max = 0, prof_maxt = -1, prof_nz = 0;
    long long prof_h[5] = {0};   /* buckets: 0, 1-8, 9-64, 65-512, 513+ */
    if (prof_on) prof_last = gm_world_block_gen(r.world);
    /* A tape can contain GuiGameOver followed by SPacketRespawn on the next
     * row. Keep consuming scripted events while dead; gm_runtime_tick itself
     * remains inert until an authoritative positive set_vitals revives it. */
    for (int tick = 0; tick < cfg->ticks && !r.won &&
         (!r.dead || continue_after_death); ++tick) {
        /* renderable ghost entities are per-tick state: last tick's recorded
         * entities must not linger into a tick whose tape row has none. */
        gm_runtime_ent_views_clear(&r);
        /* same for open GUI screen views (divergence #9). */
        gm_runtime_gui_view_clear(&r);
        GmAction action; memset(&action,0,sizeof action); action.hotbar_sel=-1;
        int have_look = 0; double look_yaw = 0.0, look_pitch = 0.0;
        int have_vitals_post = 0; double vitals_health = 20.0; long long vitals_food = 20;
        int have_regen_post = 0; double regen_health = 20.0, regen_exhaustion = 0.0;
        long long regen_food = 20;
        int have_hold_regen_post = 0;
        int clear_hurt_velocity_post = 0;
        int hold_fall_damage_post = 0;
        int have_food_stats_post = 0;
        double food_stats_saturation = 5.0, food_stats_exhaustion = 0.0;
        int have_pose_post = 0, pose_on_ground = 0;
        double pose_x = 0.0, pose_y = 0.0, pose_z = 0.0;
        double pose_yaw = 0.0, pose_pitch = 0.0;
        double pose_vx = 0.0, pose_vy = 0.0, pose_vz = 0.0, pose_fall = 0.0;
        for (;;) {
            if (!have && in && fgets(line,sizeof line,in)) {
                line_no++;
                err[0] = 0;
                if (!strchr(line,'\n') && !feof(in)) { fprintf(stderr,"script:%ld: line too long\n",line_no); goto bad; }
                char *nl=strchr(line,'\n'); if(nl)*nl=0;
                if (!parse_object(line,&pending,err,sizeof err) ||
                    !as_i64(field(&pending,"tick"),&pending_tick) || pending_tick < 0) {
                    fprintf(stderr,"script:%ld: %s\n",line_no,err[0]?err:"invalid tick"); goto bad;
                }
                have=1;
            }
            if (!have || pending_tick > tick) break;
            if (pending_tick < tick) { fprintf(stderr,"script:%ld: events must be tick-sorted\n",line_no); goto bad; }
            const char *type;
            if (!as_string(field(&pending,"type"),&type)) { fprintf(stderr,"script:%ld: missing string type\n",line_no); goto bad; }
            if (!strcmp(type,"continue_after_death")) {
                static const char *const keys[]={"tick","type"};
                if(!keys_only(&pending,keys,2,err,sizeof err)){
                    fprintf(stderr,"script:%ld: invalid continue_after_death\n",line_no);
                    goto bad;
                }
                continue_after_death=1;
            } else if (!strcmp(type,"action")) {
                if (!parse_action(&pending,&action,err,sizeof err)) { fprintf(stderr,"script:%ld: %s\n",line_no,err); goto bad; }
            } else if (!strcmp(type,"set_pose")) {
                double x,y,z,yaw,pitch;
                static const char *const keys[]={"tick","type","x","y","z","yaw","pitch"};
                if (!keys_only(&pending,keys,7,err,sizeof err)||
                    !as_double(field(&pending,"x"),&x)||!as_double(field(&pending,"y"),&y)||
                    !as_double(field(&pending,"z"),&z)||!as_double(field(&pending,"yaw"),&yaw)||
                    !as_double(field(&pending,"pitch"),&pitch)) {
                    fprintf(stderr,"script:%ld: %s\n",line_no,err[0]?err:"invalid set_pose"); goto bad;
                }
                gm_runtime_set_pose(&r,x,y,z,(float)yaw,(float)pitch);
            } else if (!strcmp(type,"set_look")) {
                double yaw,pitch;
                static const char *const keys[]={"tick","type","yaw","pitch"};
                if (!keys_only(&pending,keys,4,err,sizeof err)||
                    !as_double(field(&pending,"yaw"),&yaw)||
                    !as_double(field(&pending,"pitch"),&pitch)) {
                    fprintf(stderr,"script:%ld: invalid set_look\n",line_no); goto bad;
                }
                /* DEFERRED to after gm_runtime_tick: the tape records yaw/pitch
                 * POST-tick (the qrl bridge applies the quantized turn after the
                 * tick's physics, before recordTick; mouse look likewise lands
                 * between ticks). Tick t's move must run with the PREVIOUS look;
                 * the new look takes effect for state/frame capture at t and for
                 * tick t+1's physics. Found at t371 of the fresh-world tape: a
                 * mid-walk 15-degree turn accelerated magma along the new yaw
                 * one tick early (accel fit: oracle 0.070711 = yaw 0, magma
                 * 0.086603 = yaw -15). */
                have_look = 1; look_yaw = yaw; look_pitch = pitch;
            } else if (!strcmp(type,"set_look_pre")) {
                double yaw,pitch;
                static const char *const keys[]={"tick","type","yaw","pitch"};
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !as_double(field(&pending,"yaw"),&yaw)||
                   !as_double(field(&pending,"pitch"),&pitch)){
                    fprintf(stderr,"script:%ld: invalid set_look_pre\n",line_no);goto bad;
                }
                gm_runtime_set_look(&r,(float)yaw,(float)pitch);
            } else if (!strcmp(type,"set_pose_post")) {
                long long og;
                static const char *const keys[]={"tick","type","x","y","z","yaw","pitch",
                    "vx","vy","vz","on_ground","fall"};
                if(!keys_only(&pending,keys,12,err,sizeof err)||
                   !as_double(field(&pending,"x"),&pose_x)||
                   !as_double(field(&pending,"y"),&pose_y)||
                   !as_double(field(&pending,"z"),&pose_z)||
                   !as_double(field(&pending,"yaw"),&pose_yaw)||
                   !as_double(field(&pending,"pitch"),&pose_pitch)||
                   !as_double(field(&pending,"vx"),&pose_vx)||
                   !as_double(field(&pending,"vy"),&pose_vy)||
                   !as_double(field(&pending,"vz"),&pose_vz)||
                   !as_i64(field(&pending,"on_ground"),&og)||(og!=0&&og!=1)||
                   !as_double(field(&pending,"fall"),&pose_fall)||pose_fall<0){
                    fprintf(stderr,"script:%ld: invalid set_pose_post\n",line_no);goto bad;
                }
                pose_on_ground=(int)og;have_pose_post=1;
            } else if (!strcmp(type,"set_vitals")) {
                double health; long long food;
                static const char *const keys[]={"tick","type","health","food"};
                if (!keys_only(&pending,keys,4,err,sizeof err)||
                    !as_double(field(&pending,"health"),&health)||
                    !as_i64(field(&pending,"food"),&food)||
                    health<0||health>20||food<0||food>20) {
                    fprintf(stderr,"script:%ld: invalid set_vitals\n",line_no); goto bad;
                }
                gm_runtime_set_vitals(&r,(float)health,(int)food);
                held_regen=0.0f;
            } else if (!strcmp(type,"set_vitals_post")) {
                static const char *const keys[]={"tick","type","health","food"};
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !as_double(field(&pending,"health"),&vitals_health)||
                   !as_i64(field(&pending,"food"),&vitals_food)||
                   vitals_health<0||vitals_health>20||vitals_food<0||vitals_food>20){
                    fprintf(stderr,"script:%ld: invalid set_vitals_post\n",line_no);goto bad;
                }
                have_vitals_post=1;
            } else if (!strcmp(type,"set_regen_post")) {
                static const char *const keys[]={"tick","type","health","food","exhaustion"};
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_double(field(&pending,"health"),&regen_health)||
                   !as_i64(field(&pending,"food"),&regen_food)||
                   !as_double(field(&pending,"exhaustion"),&regen_exhaustion)||
                   regen_health<0||regen_health>20||regen_food<0||regen_food>20||
                   regen_exhaustion<0||regen_exhaustion>6){
                    fprintf(stderr,"script:%ld: invalid set_regen_post\n",line_no);goto bad;
                }
                have_regen_post=1;
            } else if (!strcmp(type,"hold_regen_post")) {
                static const char *const keys[]={"tick","type"};
                if(!keys_only(&pending,keys,2,err,sizeof err)){
                    fprintf(stderr,"script:%ld: invalid hold_regen_post\n",line_no);goto bad;
                }
                have_hold_regen_post=1;
            } else if (!strcmp(type,"clear_hurt_velocity_post")) {
                static const char *const keys[]={"tick","type"};
                if(!keys_only(&pending,keys,2,err,sizeof err)){
                    fprintf(stderr,"script:%ld: invalid clear_hurt_velocity_post\n",line_no);goto bad;
                }
                clear_hurt_velocity_post=1;
            } else if (!strcmp(type,"hold_fall_damage_post")) {
                static const char *const keys[]={"tick","type"};
                if(!keys_only(&pending,keys,2,err,sizeof err)){
                    fprintf(stderr,"script:%ld: invalid hold_fall_damage_post\n",line_no);goto bad;
                }
                hold_fall_damage_post=1;
            } else if (!strcmp(type,"set_food_stats_post")) {
                static const char *const keys[]={"tick","type","saturation","exhaustion"};
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !as_double(field(&pending,"saturation"),&food_stats_saturation)||
                   !as_double(field(&pending,"exhaustion"),&food_stats_exhaustion)||
                   food_stats_saturation<0||food_stats_saturation>20||
                   food_stats_exhaustion<0||food_stats_exhaustion>4){
                    fprintf(stderr,"script:%ld: invalid set_food_stats_post\n",line_no);goto bad;
                }
                have_food_stats_post=1;
            } else if (!strcmp(type,"set_dimension")) {
                long long dimension;
                static const char *const keys[]={"tick","type","dimension"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"dimension"),&dimension)||
                   !gm_runtime_set_dimension(&r,(int)dimension)){
                    fprintf(stderr,"script:%ld: invalid set_dimension\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"set_velocity")) {
                /* optional on_ground: a mid-session tape can start with
                 * residual motion while standing - first-tick friction is
                 * 0.546 on ground vs 0.91 airborne, and a fresh player
                 * defaults to airborne, so tick 0 diverges in vx without it. */
                double x,y,z; long long og=-1;
                static const char *const keys[]={"tick","type","x","y","z","on_ground"};
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_double(field(&pending,"x"),&x)||!as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   (field(&pending,"on_ground")&&
                    (!as_i64(field(&pending,"on_ground"),&og)||(og!=0&&og!=1)))){
                    fprintf(stderr,"script:%ld: invalid set_velocity\n",line_no);goto bad;
                }
                gm_runtime_set_velocity(&r,x,y,z);
                if(og>=0)r.player.ent.onGround=(int)og;
            } else if (!strcmp(type,"set_packet_velocity")) {
                double x,y,z;
                static const char *const keys[]={"tick","type","x","y","z"};
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_double(field(&pending,"x"),&x)||!as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)){
                    fprintf(stderr,"script:%ld: invalid set_packet_velocity\n",line_no);goto bad;
                }
                gm_runtime_set_packet_velocity(&r,x,y,z);
            } else if (!strcmp(type,"add_velocity")) {
                /* SPacketExplosion knockback: handleExplosion ADDS the
                 * packet motion to the local player, unlike pvel. */
                double x,y,z;
                static const char *const keys[]={"tick","type","x","y","z"};
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_double(field(&pending,"x"),&x)||!as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)){
                    fprintf(stderr,"script:%ld: invalid add_velocity\n",line_no);goto bad;
                }
                gm_runtime_add_velocity(&r,x,y,z);
            } else if (!strcmp(type,"ent_box")) {
                /* Tape replay ghost pusher: recorded oracle entity box (world
                 * coords, feet y, width, height) applied as a vanilla
                 * applyEntityCollision player push during this tick. */
                double x,y,z,w,h;
                static const char *const keys[]={"tick","type","x","y","z","w","h"};
                if(!keys_only(&pending,keys,7,err,sizeof err)||
                   !as_double(field(&pending,"x"),&x)||!as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||!as_double(field(&pending,"w"),&w)||
                   !as_double(field(&pending,"h"),&h)||w<=0||h<=0){
                    fprintf(stderr,"script:%ld: invalid ent_box\n",line_no);goto bad;
                }
                gm_runtime_ent_box(&r,x,y,z,w,h);
            } else if (!strcmp(type,"dragon_contact")) {
                /* Recorded EntityDragon part query. Wing boxes carry 5 damage
                 * (collideWithEntities); head/neck boxes carry 10
                 * (attackEntitiesInList). Damage lands before FoodStats.onUpdate. */
                double x0,y0,z0,x1,y1,z1,damage;
                static const char *const keys[]={"tick","type","min_x","min_y","min_z",
                    "max_x","max_y","max_z","damage"};
                if(!keys_only(&pending,keys,9,err,sizeof err)||
                   !as_double(field(&pending,"min_x"),&x0)||
                   !as_double(field(&pending,"min_y"),&y0)||
                   !as_double(field(&pending,"min_z"),&z0)||
                   !as_double(field(&pending,"max_x"),&x1)||
                   !as_double(field(&pending,"max_y"),&y1)||
                   !as_double(field(&pending,"max_z"),&z1)||
                   !as_double(field(&pending,"damage"),&damage)||damage<=0){
                    fprintf(stderr,"script:%ld: invalid dragon_contact\n",line_no);goto bad;
                }
                (void)gm_runtime_dragon_contact(&r,x0,y0,z0,x1,y1,z1,(float)damage);
            } else if (!strcmp(type,"mob_damage")) {
                double damage;
                static const char *const keys[]={"tick","type","damage"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_double(field(&pending,"damage"),&damage)||damage<=0){
                    fprintf(stderr,"script:%ld: invalid mob_damage\n",line_no);goto bad;
                }
                (void)gm_mobs_attack_player(&r.mobs,
                    (struct PvStats *)&r.vitals, &r.player.inv,
                    (float)damage, 0);
                r.player.health=r.vitals.health;
            } else if (!strcmp(type,"ent_view")) {
                /* Tape replay renderable ghost entity (divergence #10): the
                 * recorded oracle entity is drawn by frame capture through
                 * the live-entity model path. Render-only; the physics-push
                 * ghost stays the separate ent_box event above. Types with
                 * no magma model are skipped (logged once per type). */
                const char *ent;double x,y,z,yaw,hp=-1.0,d;
                long long eid=-1,n;
                static const char *const keys[]={"tick","type","ent","x","y","z","yaw","hp","id",
                    "tape_pose","head_yaw","pitch","swing","hurt","death","body_yaw","flags",
                    "sheared","fleece","graze_y","graze_x","item","item_meta","count","age",
                    "hover","has_hover","crystal_rot","show_bottom","beam_x","beam_y","beam_z",
                    "anim_time","death_ticks","phase_id","stationary",
                    /* EntityXPOrb: item=xpValue, item_meta=xpColor, age=xpOrbAge */
                    "xp_value","xp_color",
                    /* Entity.ticksExisted (client): crystal-beam UV scroll and
                     * the beam-origin pulse (RenderDragon.renderCrystalBeams). */
                    "ticks_existed",
                    /* EntityArmorStand equipment + saved display flags. */
                    "armor_feet","armor_legs","armor_chest","armor_head","stand_flags"};
                if(!keys_only(&pending,keys,44,err,sizeof err)||
                   !as_string(field(&pending,"ent"),&ent)||
                   !as_double(field(&pending,"x"),&x)||!as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||!as_double(field(&pending,"yaw"),&yaw)||
                   (field(&pending,"hp")&&!as_double(field(&pending,"hp"),&hp))||
                   (field(&pending,"id")&&!as_i64(field(&pending,"id"),&eid))){
                    fprintf(stderr,"script:%ld: invalid ent_view\n",line_no);goto bad;
                }
                GmEntityView view;memset(&view,0,sizeof view);
                view.type=!strcmp(ent,"EntityItem")&&field(&pending,"item")
                    ?GM_VIEW_ITEM:gm_entity_type_for_name(ent);
                /* EntityFallingBlock: fallTile id/meta travel as item/item_meta
                 * (RenderFallingBlock). Legacy 7-field rows have no state; NBT
                 * load default is sand (EntityFallingBlock.java:328). */
                if(view.type==GM_VIEW_FALLING_BLOCK&&!field(&pending,"item")){
                    view.item_id=12; /* Blocks.SAND */
                    view.item_meta=0;
                }
                view.skin=gm_entity_skin_for_name(ent);
                if(view.type==GM_VIEW_BILLBOARD||
                   view.type==GM_VIEW_DRAGON_FIREBALL)
                    view.item_id=gm_entity_billboard_item(ent);
                if(!strcmp(ent,"EntityLargeFireball"))
                    view.item_meta=2; /* RenderFireball scale 2 + large fire layers */
                view.x=(float)x;view.y=(float)y;view.z=(float)z;view.yaw=(float)yaw;
                view.health=(float)hp;view.ent_id=(int)eid;
                if (view.type == EW_TYPE_BOAT)
                    gm_runtime_tape_boat_view(&r, (int)eid, x, y, z, yaw);
#define OPT_I64(K,DST) do{if(field(&pending,K)){if(!as_i64(field(&pending,K),&n)){fprintf(stderr,"script:%ld: invalid ent_view %s\n",line_no,K);goto bad;}DST=(int)n;}}while(0)
#define OPT_DBL(K,DST) do{if(field(&pending,K)){if(!as_double(field(&pending,K),&d)){fprintf(stderr,"script:%ld: invalid ent_view %s\n",line_no,K);goto bad;}DST=(float)d;}}while(0)
                OPT_I64("tape_pose",view.tape_pose);OPT_DBL("head_yaw",view.head_yaw);
                OPT_DBL("pitch",view.pitch);OPT_DBL("swing",view.swing_progress);
                OPT_I64("hurt",view.hurt_time);OPT_I64("death",view.death_time);
                OPT_DBL("body_yaw",view.yaw);OPT_I64("flags",view.flags);
                OPT_I64("sheared",view.sheared);OPT_I64("fleece",view.fleece_color);
                OPT_DBL("graze_y",view.graze_y);OPT_DBL("graze_x",view.graze_x);
                OPT_I64("item",view.item_id);OPT_I64("item_meta",view.item_meta);
                OPT_I64("count",view.item_count);OPT_I64("age",view.age);
                /* Explicit XP orb aliases (same fields as item/item_meta). */
                OPT_I64("xp_value",view.item_id);OPT_I64("xp_color",view.item_meta);
                OPT_DBL("hover",view.hover_start);OPT_I64("has_hover",view.has_hover_start);
                view.beam_x=view.beam_y=view.beam_z=-1;
                OPT_DBL("crystal_rot",view.crystal_rot);OPT_I64("show_bottom",view.show_bottom);
                OPT_I64("beam_x",view.beam_x);OPT_I64("beam_y",view.beam_y);
                OPT_I64("beam_z",view.beam_z);
                OPT_DBL("anim_time",view.anim_time);OPT_I64("death_ticks",view.death_ticks);
                OPT_I64("phase_id",view.phase_id);OPT_I64("stationary",view.stationary);
                OPT_I64("ticks_existed",view.ticks_existed);
                OPT_I64("armor_feet",view.armor_feet);OPT_I64("armor_legs",view.armor_legs);
                OPT_I64("armor_chest",view.armor_chest);OPT_I64("armor_head",view.armor_head);
                OPT_I64("stand_flags",view.stand_flags);
#undef OPT_I64
#undef OPT_DBL
                int vt=view.type;
                if(vt<0){
                    static char warned[16][JL_VALUE];static int nwarned=0;
                    int seen=0;
                    for(int i=0;i<nwarned;++i)if(!strcmp(warned[i],ent)){seen=1;break;}
                    if(!seen&&nwarned<16){
                        snprintf(warned[nwarned++],JL_VALUE,"%s",ent);
                        fprintf(stderr,"script: ent_view %s: no model, skipped\n",ent);
                    }
                }else if(view.item_id<0||
                         (view.item_id>4095&&
                          !(view.type==GM_VIEW_DRAGON_FIREBALL&&
                            view.item_id==9003))||view.item_meta<0||
                         view.item_meta>32767||view.item_count<0||view.item_count>64||
                         view.fleece_color<0||view.fleece_color>15||
                         (view.flags&~15)||view.hurt_time<0||view.death_time<0){
                    fprintf(stderr,"script:%ld: invalid ent_view state\n",line_no);goto bad;
                }else if(view.armor_feet<0||view.armor_feet>4095||
                         view.armor_legs<0||view.armor_legs>4095||
                         view.armor_chest<0||view.armor_chest>4095||
                         view.armor_head<0||view.armor_head>4095||
                         (view.stand_flags&~7)){
                    fprintf(stderr,"script:%ld: invalid armor stand state\n",line_no);goto bad;
                }else gm_runtime_ent_view(&r,&view);
            } else if (!strcmp(type,"gui_view")) {
                /* Tape replay open container GUI (divergence #9): draw-only.
                 * Maps vanilla GuiScreen simple name -> container kind; mx/my
                 * are ScaledResolution coords (converted to fb px at draw).
                 * Unmapped screens (pause, chat, ...) are logged once + skipped. */
                const char *gui; long long mx = -1, my = -1;
                static const char *const keys[]={"tick","type","gui","mx","my"};
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_string(field(&pending,"gui"),&gui)||
                   (field(&pending,"mx")&&!as_i64(field(&pending,"mx"),&mx))||
                   (field(&pending,"my")&&!as_i64(field(&pending,"my"),&my))){
                    fprintf(stderr,"script:%ld: invalid gui_view\n",line_no);goto bad;
                }
                int kind = gm_screen_kind_for_gui(gui);
                if(kind < 0){
                    static char warned[16][JL_VALUE]; static int nwarned = 0;
                    int seen = 0;
                    for(int i=0;i<nwarned;++i)if(!strcmp(warned[i],gui)){seen=1;break;}
                    if(!seen&&nwarned<16){
                        snprintf(warned[nwarned++],JL_VALUE,"%s",gui);
                        fprintf(stderr,"script: gui_view %s: no container screen, skipped\n",gui);
                    }
                }else{
                    /* default mouse to gui-space center when gmx/gmy absent */
                    if(mx < 0 || my < 0){
                        int s = gm_screen_gui_scale(cfg->height > 0 ? cfg->height : 480);
                        int gw = ((cfg->width > 0 ? cfg->width : 854) + s - 1) / s;
                        int gh = ((cfg->height > 0 ? cfg->height : 480) + s - 1) / s;
                        if(mx < 0) mx = gw / 2;
                        if(my < 0) my = gh / 2;
                    }
                    gm_runtime_gui_view(&r, kind, (int)mx, (int)my);
                }
            } else if (!strcmp(type,"gui_slot_view") || !strcmp(type,"gui_cursor_view")) {
                /* Optional StoredEnchantments subset: n_ench + e0..e7 packed as
                 * (id<<16)|level. Absent => n_enchants=0 (backward compatible). */
                long long slot=0,item,count,meta,n_ench=0;
                int is_slot = !strcmp(type,"gui_slot_view");
                static const char *const keys_slot[]={
                    "tick","type","slot","item","count","meta","n_ench",
                    "e0","e1","e2","e3","e4","e5","e6","e7"
                };
                static const char *const keys_cur[]={
                    "tick","type","item","count","meta","n_ench",
                    "e0","e1","e2","e3","e4","e5","e6","e7"
                };
                ICStack st;
                if (is_slot) {
                    if(!keys_only(&pending,keys_slot,15,err,sizeof err)||
                       !as_i64(field(&pending,"slot"),&slot)||
                       !as_i64(field(&pending,"item"),&item)||
                       !as_i64(field(&pending,"count"),&count)||
                       !as_i64(field(&pending,"meta"),&meta)){
                        fprintf(stderr,"script:%ld: invalid gui_slot_view\n",line_no);goto bad;
                    }
                } else {
                    if(!keys_only(&pending,keys_cur,14,err,sizeof err)||
                       !as_i64(field(&pending,"item"),&item)||
                       !as_i64(field(&pending,"count"),&count)||
                       !as_i64(field(&pending,"meta"),&meta)){
                        fprintf(stderr,"script:%ld: invalid gui_cursor_view\n",line_no);goto bad;
                    }
                }
                st = count == 0 ? ic_empty() : ic_mk((i32)item,(i32)count,(i32)meta);
                if (field(&pending,"n_ench")) {
                    char ek[4];
                    int ei;
                    if (!as_i64(field(&pending,"n_ench"),&n_ench) ||
                        n_ench < 0 || n_ench > IC_MAX_ENCHANTS) {
                        fprintf(stderr,"script:%ld: invalid n_ench\n",line_no);goto bad;
                    }
                    st.n_enchants = (i32)n_ench;
                    for (ei = 0; ei < (int)n_ench; ++ei) {
                        long long packed = 0;
                        snprintf(ek, sizeof ek, "e%d", ei);
                        if (!as_i64(field(&pending, ek), &packed)) {
                            fprintf(stderr,"script:%ld: missing %s\n",line_no,ek);goto bad;
                        }
                        st.enchants[ei].id = (i16)((packed >> 16) & 0xffff);
                        st.enchants[ei].level = (i16)(packed & 0xffff);
                    }
                }
                if (is_slot) {
                    if (!gm_runtime_tape_gui_slot_stack(&r,(int)slot,st)) {
                        fprintf(stderr,"script:%ld: invalid gui_slot_view\n",line_no);goto bad;
                    }
                } else if (!gm_runtime_tape_gui_cursor_stack(&r,st)) {
                    fprintf(stderr,"script:%ld: invalid gui_cursor_view\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"gui_furnace_view")) {
                long long burn,current,cook,total;
                static const char *const keys[]={"tick","type","burn","current_burn","cook","total_cook"};
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"burn"),&burn)||
                   !as_i64(field(&pending,"current_burn"),&current)||
                   !as_i64(field(&pending,"cook"),&cook)||
                   !as_i64(field(&pending,"total_cook"),&total)||
                   burn>2147483647LL||current>2147483647LL||
                   cook>2147483647LL||total>2147483647LL||
                   !gm_runtime_tape_furnace(&r,(int)burn,(int)current,(int)cook,(int)total)){
                    fprintf(stderr,"script:%ld: invalid gui_furnace_view\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"set_time")) {
                long long value;
                static const char *const keys[]={"tick","type","value"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"value"),&value)||value<0){
                    fprintf(stderr,"script:%ld: invalid set_time\n",line_no);goto bad;
                }
                gm_runtime_set_time(&r,value);
            } else if (!strcmp(type,"set_total_time")) {
                long long value;
                static const char *const keys[]={"tick","type","value"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"value"),&value)||value<0){
                    fprintf(stderr,"script:%ld: invalid set_total_time\n",line_no);goto bad;
                }
                gm_runtime_set_total_time(&r,value);
            } else if (!strcmp(type,"set_elytra")) {
                long long equipped;
                static const char *const keys[]={"tick","type","equipped"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"equipped"),&equipped)||
                   (equipped!=0&&equipped!=1)){
                    fprintf(stderr,"script:%ld: invalid set_elytra\n",line_no);goto bad;
                }
                gm_runtime_set_elytra(&r,(int)equipped);
            } else if (!strcmp(type,"set_skin")) {
                /* first-person arm variant: offline players get steve or alex
                 * by username-UUID hash; the tape header records which. */
                const char *skin;
                static const char *const keys[]={"tick","type","skin"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_string(field(&pending,"skin"),&skin)||
                   (strcmp(skin,"default")&&strcmp(skin,"slim"))){
                    fprintf(stderr,"script:%ld: invalid set_skin\n",line_no);goto bad;
                }
                gm_hand_set_skin(!strcmp(skin,"slim"));
            } else if (!strcmp(type,"set_weather")) {
                long long raining,thundering,rain_time,thunder_time;
                static const char *const keys[]={"tick","type","raining","thundering",
                                                 "rain_time","thunder_time"};
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"raining"),&raining)||
                   !as_i64(field(&pending,"thundering"),&thundering)||
                   !as_i64(field(&pending,"rain_time"),&rain_time)||
                   !as_i64(field(&pending,"thunder_time"),&thunder_time)||
                   (raining!=0&&raining!=1)||(thundering!=0&&thundering!=1)||
                   rain_time<0||rain_time>2147483647LL||
                   thunder_time<0||thunder_time>2147483647LL) {
                    fprintf(stderr,"script:%ld: invalid set_weather\n",line_no);goto bad;
                }
                gm_runtime_set_weather(&r,(int)raining,(int)thundering,
                                       (int)rain_time,(int)thunder_time);
            } else if (!strcmp(type,"set_gamerules")) {
                McGameRules gamerules=r.gamerules;
                /* The recorder emits all string-backed rules. These three
                 * currently have magma runtime mechanics; every other string
                 * field is intentionally consumed without effect. */
                for(int i=0;i<pending.n;++i){
                    const JlField *rf=&pending.f[i];
                    if(!strcmp(rf->key,"tick")||!strcmp(rf->key,"type"))continue;
                    if(!rf->string){
                        fprintf(stderr,"script:%ld: gamerule %s must be a string\n",
                                line_no,rf->key);goto bad;
                    }
                }
                if(!as_rule_bool(field(&pending,"naturalRegeneration"),
                                 &gamerules.naturalRegeneration)||
                   !as_rule_bool(field(&pending,"doDaylightCycle"),
                                 &gamerules.doDaylightCycle)||
                   !as_rule_bool(field(&pending,"doWeatherCycle"),
                                 &gamerules.doWeatherCycle)){
                    fprintf(stderr,"script:%ld: invalid honored gamerule\n",line_no);goto bad;
                }
                gm_runtime_set_gamerules(&r,&gamerules);
            } else if (!strcmp(type,"set_block")) {
                long long x,y,z,id,meta;
                static const char *const keys[]={"tick","type","x","y","z","id","meta"};
                if(!keys_only(&pending,keys,7,err,sizeof err)||
                   !as_i64(field(&pending,"x"),&x)||!as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||!as_i64(field(&pending,"id"),&id)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   x<-2147483647LL-1||x>2147483647LL||z<-2147483647LL-1||z>2147483647LL||
                   y<0||y>255||id<0||id>4095||meta<0||meta>15||
                   !gm_runtime_set_block(&r,(int)x,(int)y,(int)z,(int)id,(int)meta)) {
                    fprintf(stderr,"script:%ld: invalid set_block\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"snapshot_block")) {
                long long x,y,z,id,meta,dimension=0;
                static const char *const keys[]={"tick","type","x","y","z","id","meta","dim"};
                if(!keys_only(&pending,keys,8,err,sizeof err)||
                   !as_i64(field(&pending,"x"),&x)||!as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||!as_i64(field(&pending,"id"),&id)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   (field(&pending,"dim")&&!as_i64(field(&pending,"dim"),&dimension))||
                   dimension < -1||dimension > 1||
                   x<-2147483647LL-1||x>2147483647LL||z<-2147483647LL-1||z>2147483647LL||
                   y<0||y>255||id<0||id>4095||meta<0||meta>15||
                   !gm_runtime_load_block_dim(&r,(int)dimension,(int)x,(int)y,(int)z,
                                              (int)id,(int)meta)){
                    fprintf(stderr,"script:%ld: invalid snapshot_block\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"snapshot_region")) {
                long long cx,cz,radius,dimension=0;
                static const char *const keys[]={"tick","type","cx","cz","radius","dim"};
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"cx"),&cx)||!as_i64(field(&pending,"cz"),&cz)||
                   !as_i64(field(&pending,"radius"),&radius)||
                   (field(&pending,"dim")&&!as_i64(field(&pending,"dim"),&dimension))||
                   dimension < -1||dimension > 1||
                   cx<-134217728LL||cx>134217727LL||cz<-134217728LL||cz>134217727LL||
                   !gm_runtime_snapshot_region_dim(&r,(int)dimension,(int)cx,(int)cz,
                                                  (int)radius)){
                    fprintf(stderr,"script:%ld: invalid snapshot_region\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"set_inventory")) {
                long long slot,item,count,meta;
                static const char *const keys[]={"tick","type","slot","item","count","meta"};
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"slot"),&slot)||
                   !as_i64(field(&pending,"item"),&item)||
                   !as_i64(field(&pending,"count"),&count)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   !((slot>=0&&slot<ISR_MAIN_SLOTS)||
                     (slot>=ISR_ARMOR0&&slot<ISR_ARMOR0+ISR_ARMOR_SLOTS)||
                     slot==ISR_OFFHAND_SLOT)||item<0||item>4095||
                   count<0||count>64||meta<0||meta>32767||
                   !gm_runtime_set_inventory(&r,(int)slot,(int)item,(int)count,(int)meta)) {
                    fprintf(stderr,"script:%ld: invalid set_inventory\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"inv_view")) {
                long long slot,item,count,meta;
                static const char *const keys[]={"tick","type","slot","item","count","meta"};
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"slot"),&slot)||
                   !as_i64(field(&pending,"item"),&item)||
                   !as_i64(field(&pending,"count"),&count)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   !gm_runtime_tape_inventory(&r,(int)slot,(int)item,(int)count,(int)meta)){
                    fprintf(stderr,"script:%ld: invalid inv_view\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"player_view")) {
                long long xp,air,portal_frame=-1,portal_phase=0,loading=0,pinned=0;
                long long fire=0,creative=0,hurt=0,max_hurt=10;
                double frac,portal=0.0,hurt_yaw=0.0,attack_cooldown=1.0;
                static const char *const keys[]={"tick","type","xp_level","xp_frac","air",
                    "portal","portal_frame","portal_phase","loading","texture_animations_pinned",
                    "fire","creative","hurt","max_hurt","hurt_yaw","attack_cooldown"};
                if(!keys_only(&pending,keys,16,err,sizeof err)||
                   !as_i64(field(&pending,"xp_level"),&xp)||
                   !as_double(field(&pending,"xp_frac"),&frac)||
                   !as_i64(field(&pending,"air"),&air)||
                   (field(&pending,"portal")&&!as_double(field(&pending,"portal"),&portal))||
                   (field(&pending,"portal_frame")&&!as_i64(field(&pending,"portal_frame"),&portal_frame))||
                   (field(&pending,"portal_phase")&&!as_i64(field(&pending,"portal_phase"),&portal_phase))||
                   (field(&pending,"loading")&&!as_i64(field(&pending,"loading"),&loading))||
                   (field(&pending,"texture_animations_pinned")&&
                    !as_i64(field(&pending,"texture_animations_pinned"),&pinned))||
                   (field(&pending,"fire")&&!as_i64(field(&pending,"fire"),&fire))||
                   (field(&pending,"creative")&&!as_i64(field(&pending,"creative"),&creative))||
                   (field(&pending,"hurt")&&!as_i64(field(&pending,"hurt"),&hurt))||
                   (field(&pending,"max_hurt")&&!as_i64(field(&pending,"max_hurt"),&max_hurt))||
                   (field(&pending,"hurt_yaw")&&!as_double(field(&pending,"hurt_yaw"),&hurt_yaw))||
                   (field(&pending,"attack_cooldown")&&
                    !as_double(field(&pending,"attack_cooldown"),&attack_cooldown))||
                   /* vanilla drowning runs air down to -20 (damage pulse then
                    * resets it to 0), so negative values are legitimate tape data */
                   xp<0||xp>21863||frac<0||frac>1||air<-20||air>300||
                   portal<0||portal>1||portal_frame < -1||
                   portal_phase<0||loading<0||loading>2||pinned<0||pinned>1||
                   fire<0||fire>1||creative<0||creative>1||hurt<0||hurt>20||
                   max_hurt<0||max_hurt>20||attack_cooldown<0||attack_cooldown>1){
                    fprintf(stderr,"script:%ld: invalid player_view\n",line_no);goto bad;
                }
                gm_runtime_tape_player_view(&r,(int)xp,(float)frac,(int)air,
                    (float)portal,(int)portal_frame,(int)portal_phase,(int)loading,
                    (int)pinned,(int)fire,(int)creative,(int)hurt,
                    (int)max_hurt,(float)hurt_yaw,(float)attack_cooldown);
            } else if (!strcmp(type,"potion_clear")) {
                static const char *const keys[]={"tick","type"};
                if(!keys_only(&pending,keys,2,err,sizeof err)){
                    fprintf(stderr,"script:%ld: invalid potion_clear\n",line_no);goto bad;
                }
                gm_runtime_tape_potions_clear(&r);
            } else if (!strcmp(type,"potion_view")) {
                long long id,amplifier,duration,show=1;
                /* show_particles is optional: tapes recorded before the flag
                 * existed carry visible effects only by ASSUMPTION, so the
                 * legacy default stays 1 (the PotionEffect ctor default). */
                static const char *const keys[]={"tick","type","id","amplifier",
                                                 "duration","show_particles"};
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"id"),&id)||
                   !as_i64(field(&pending,"amplifier"),&amplifier)||
                   !as_i64(field(&pending,"duration"),&duration)||
                   (field(&pending,"show_particles")&&
                    !as_i64(field(&pending,"show_particles"),&show))||
                   !gm_runtime_tape_potion(&r,(int)id,(int)amplifier,(int)duration,
                                           (int)show)){
                    fprintf(stderr,"script:%ld: invalid potion_view\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"armor_view")) {
                /* Recorded ForgeHooks.getTotalArmorValue; -1 clears. */
                long long points;
                static const char *const keys[]={"tick","type","points"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"points"),&points)||points<-1||points>20){
                    fprintf(stderr,"script:%ld: invalid armor_view\n",line_no);goto bad;
                }
                gm_runtime_tape_armor(&r,(int)points);
            } else if (!strcmp(type,"spawn_entity")) {
                long long entity;double x,y,z;
                static const char *const keys[]={"tick","type","entity","x","y","z"};
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"entity"),&entity)||
                   !as_double(field(&pending,"x"),&x)||!as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   gm_mobs_spawn(&r.mobs,(int)entity,x,y,z)<0){
                    fprintf(stderr,"script:%ld: invalid spawn_entity\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_particle")) {
                long long id;double x,y,z,vx,vy,vz;
                static const char *const keys[]={"tick","type","id","x","y","z",
                                                 "vx","vy","vz"};
                if(!keys_only(&pending,keys,9,err,sizeof err)||
                   !as_i64(field(&pending,"id"),&id)||id<0||id>2||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !gm_particles_live_spawn_recorded(&replay_particles,(int)id,
                       x,y,z,vx,vy,vz,
                       gm_world_sky_light(r.world,(int)floor(x),(int)floor(y),
                                          (int)floor(z)),
                       gm_world_block_light(r.world,(int)floor(x),(int)floor(y),
                                            (int)floor(z)))){
                    fprintf(stderr,"script:%ld: invalid spawn_particle\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"craft")) {
                int width, slots[9];
                if (!parse_craft(&pending,&width,slots,err,sizeof err) ||
                    !gm_runtime_craft(&r,width,slots)) {
                    fprintf(stderr,"script:%ld: %s\n",line_no,err[0]?err:"craft failed"); goto bad;
                }
            } else if (!strcmp(type,"use_block")) {
                long long x,y,z;
                static const char *const keys[]={"tick","type","x","y","z"};
                if (!keys_only(&pending,keys,5,err,sizeof err)||
                    !as_i64(field(&pending,"x"),&x)||!as_i64(field(&pending,"y"),&y)||
                    !as_i64(field(&pending,"z"),&z)||
                    !gm_runtime_use_block(&r,(int)x,(int)y,(int)z)) {
                    fprintf(stderr,"script:%ld: use_block failed\n",line_no); goto bad;
                }
            } else if (!strcmp(type,"furnace_insert")) {
                static const char *const keys[]={"tick","type","slot","inventory","count"};
                long long slot,inv,count;
                if (!keys_only(&pending,keys,5,err,sizeof err)||
                    !as_i64(field(&pending,"slot"),&slot)||
                    !as_i64(field(&pending,"inventory"),&inv)||
                    !as_i64(field(&pending,"count"),&count)||
                    gm_runtime_furnace_insert(&r,(int)slot,(int)inv,(int)count)<=0) {
                    fprintf(stderr,"script:%ld: %s\n",line_no,err[0]?err:"furnace insert failed"); goto bad;
                }
            } else if (!strcmp(type,"furnace_extract")) {
                static const char *const keys[]={"tick","type","slot","count"};
                long long slot,count;
                if (!keys_only(&pending,keys,4,err,sizeof err)||
                    !as_i64(field(&pending,"slot"),&slot)||
                    !as_i64(field(&pending,"count"),&count)||
                    gm_runtime_furnace_extract(&r,(int)slot,(int)count)<=0) {
                    fprintf(stderr,"script:%ld: %s\n",line_no,err[0]?err:"furnace extract failed"); goto bad;
                }
            } else { fprintf(stderr,"script:%ld: unknown or forbidden type: %s\n",line_no,type); goto bad; }
            have=0;
        }
        float health_before_tick=r.vitals.health;
        int food_before_tick=r.vitals.foodLevel;
        gm_runtime_tick(&r,action);
        if(clear_hurt_velocity_post)gm_player_clear_inferred_hurt_velocity();
        if (prof_on) {
            long long bg = gm_world_block_gen(r.world), d = bg - prof_last;
            if (d < 0) d = 0;   /* dimension switch swapped in a fresh world */
            prof_last = bg; prof_tot += d;
            if (d > prof_max) { prof_max = d; prof_maxt = tick; }
            if (d) prof_nz++;
            prof_h[d == 0 ? 0 : d <= 8 ? 1 : d <= 64 ? 2 : d <= 512 ? 3 : 4]++;
        }
        if (have_pose_post)
            gm_runtime_set_pose_state(&r,pose_x,pose_y,pose_z,
                (float)pose_yaw,(float)pose_pitch,pose_vx,pose_vy,pose_vz,
                pose_on_ground,(float)pose_fall);
        if (have_look) gm_runtime_set_look(&r,(float)look_yaw,(float)look_pitch);
        if(hold_fall_damage_post &&
           r.vitals.health < health_before_tick-1e-6f)
            gm_runtime_set_vitals(&r,health_before_tick,r.vitals.foodLevel);
        if (have_hold_regen_post &&
            r.vitals.health > health_before_tick + 1e-6f) {
            held_regen += r.vitals.health-health_before_tick;
            gm_runtime_set_vitals(&r,health_before_tick,r.vitals.foodLevel);
        }
        if(have_hold_regen_post&&held_regen>0.0f&&
           r.vitals.foodLevel<food_before_tick)
            gm_runtime_set_vitals(&r,r.vitals.health,food_before_tick);
        if (have_vitals_post) {
            if(r.vitals.health+1e-6f<(float)vitals_health&&held_regen>0.0f){
                float visible=(float)vitals_health-r.vitals.health;
                held_regen=held_regen>visible?held_regen-visible:0.0f;
            }
            gm_runtime_set_vitals(&r,(float)vitals_health,(int)vitals_food);
        }
        if (have_regen_post && r.gamerules.naturalRegeneration) {
            if (r.vitals.health + 1e-6f < (float)regen_health) {
                float visible=(float)regen_health-r.vitals.health;
                if (held_regen + 1e-6f >= visible) {
                    held_regen-=visible;
                    if(held_regen<1e-6f)held_regen=0.0f;
                } else {
                    held_regen=0.0f;
                    pv_add_exhaustion(&r.vitals,(float)regen_exhaustion);
                    r.vitals.foodTimer=0;
                }
            }
            gm_runtime_set_vitals(&r,(float)regen_health,(int)regen_food);
        }
        if (have_food_stats_post) {
            r.vitals.saturation=(float)food_stats_saturation;
            r.vitals.exhaustion=(float)food_stats_exhaustion;
        }
        /* Flywheel probe: MAGMA_DUMP="tick,x0,x1,y0,y1,z0,z1" dumps id/meta of
         * a world region to stderr at that tick - the way to see magma's LIVE
         * world state mid-replay (fluid CA etc.), which no state-out field has. */
        {
            const char *dbg = getenv("MAGMA_DUMP");
            if (dbg) {
                int dt,dx0,dx1,dy0,dy1,dz0,dz1;
                if (sscanf(dbg,"%d,%d,%d,%d,%d,%d,%d",&dt,&dx0,&dx1,&dy0,&dy1,&dz0,&dz1)==7 &&
                    (long long)dt==r.tick) {
                    for (int y=dy1;y>=dy0;--y){
                        fprintf(stderr,"[dump t%d y=%d]",dt,y);
                        for (int z=dz0;z<=dz1;++z){
                            fprintf(stderr," z%d:",z);
                            for (int x=dx0;x<=dx1;++x)
                                fprintf(stderr," %d/%d",
                                    gm_world_block(r.world,x,y,z),
                                    gm_world_meta(r.world,x,y,z));
                        }
                        fprintf(stderr,"\n");
                    }
                }
            }
        }
        /* Flywheel probe: MAGMA_WORLDDUMP="tick,cx0,cz0,ncx,ncz,path[;...]" writes
         * the LIVE world's canonical vanilla states for a chunk range in exactly
         * the trace/world_dump --states "CRWS" layout, so snapshot_patch.py can
         * diff the save against THE GAME'S OWN generation instead of world_dump's.
         * The two do not agree by construction: populate windows seed each other
         * with their neighbours' out-of-bounds spill (world/populate_mc.c
         * build_window), so decoration depends on the order windows were built,
         * and the game builds them around a walking player while world_dump
         * sweeps. Semicolon-separated specs let one run dump several ticks - the
         * dimension is whichever one the replay is in at that tick, and a range
         * wider than the resident pool needs several player-centred rectangles.
         * Non-resident chunks dump as all-zero and are counted on stderr: a
         * silent all-zero tile would read as "the game generated air here" and
         * would patch a whole real chunk away. */
        {
            const char *dbg = getenv("MAGMA_WORLDDUMP");
            for (const char *spec = dbg; spec && *spec; ) {
                int dt,cx0,cz0,ncx,ncz; char path[512];
                const char *next = strchr(spec, ';');
                if (sscanf(spec,"%d,%d,%d,%d,%d,%511[^;]",&dt,&cx0,&cz0,&ncx,&ncz,path)==6 &&
                    (long long)dt==r.tick && ncx>0 && ncz>0) {
                    FILE *wf=fopen(path,"wb");
                    if (!wf) { fprintf(stderr,"MAGMA_WORLDDUMP: cannot open %s\n",path); }
                    else {
                        long long zero=0; int32_t hdr[4]={cx0,cz0,ncx,ncz};
                        fwrite("CRWS",1,4,wf); fwrite(&zero,8,1,wf);
                        fwrite(hdr,sizeof(int32_t),4,wf);
                        static unsigned short blk[16*256*16];
                        static int32_t bio[16*16];
                        int missing=0;
                        for (int ix=0;ix<ncx;++ix) for (int iz=0;iz<ncz;++iz) {
                            int cx=cx0+ix, cz=cz0+iz, any=0;
                            for (int lx=0;lx<16;++lx) for (int lz=0;lz<16;++lz) {
                                bio[lx*16+lz]=gm_world_biome(r.world,cx*16+lx,cz*16+lz);
                                for (int y=0;y<256;++y) {
                                    int id=gm_world_block(r.world,cx*16+lx,y,cz*16+lz);
                                    int mt=gm_world_meta(r.world,cx*16+lx,y,cz*16+lz);
                                    blk[lx*4096+lz*256+y]=(unsigned short)((id<<4)|(mt&15));
                                    if (id) any=1;
                                }
                            }
                            if (!any) ++missing;
                            fwrite(blk,sizeof(unsigned short),16*256*16,wf);
                            fwrite(bio,sizeof(int32_t),16*16,wf);
                        }
                        fclose(wf);
                        fprintf(stderr,"[worlddump t%d] %d chunks (%d,%d)+%dx%d -> %s "
                                "(%d empty/non-resident)\n",
                                dt,ncx*ncz,cx0,cz0,ncx,ncz,path,missing);
                    }
                }
                spec = next ? next + 1 : NULL;
            }
        }
        /* Same, for light: MAGMA_DUMP_LIGHT="tick,x0,x1,y0,y1,z0,z1" dumps
         * "wx wy wz sky blk" lines (matches the qrl sample_light CSV columns)
         * so live-game light can be diffed cell-for-cell against magma's. */
        {
            const char *dbg = getenv("MAGMA_DUMP_LIGHT");
            if (dbg) {
                int dt,dx0,dx1,dy0,dy1,dz0,dz1;
                if (sscanf(dbg,"%d,%d,%d,%d,%d,%d,%d",&dt,&dx0,&dx1,&dy0,&dy1,&dz0,&dz1)==7 &&
                    (long long)dt==r.tick) {
                    fprintf(stderr,"[dumplight t%d] wx wy wz sky blk\n",dt);
                    for (int y=dy0;y<=dy1;++y)
                        for (int z=dz0;z<=dz1;++z)
                            for (int x=dx0;x<=dx1;++x)
                                fprintf(stderr,"%d %d %d %d %d\n",x,y,z,
                                    gm_world_sky_light(r.world,x,y,z),
                                    gm_world_block_light(r.world,x,y,z));
                }
            }
        }
        /* Same, for grass tint: MAGMA_DUMP_GRASS="tick,x0,x1,y,z0,z1" dumps
         * "wx wz biome grass" per column (grass = blended 0xRRGGBB as decimal,
         * matching qrl capture_biome golden.txt) at Java BlockPos y. */
        {
            const char *dbg = getenv("MAGMA_DUMP_GRASS");
            if (dbg) {
                int dt,dx0,dx1,dy,dz0,dz1;
                if (sscanf(dbg,"%d,%d,%d,%d,%d,%d",&dt,&dx0,&dx1,&dy,&dz0,&dz1)==6 &&
                    (long long)dt==r.tick) {
                    fprintf(stderr,"[dumpgrass t%d] wx wz biome grass\n",dt);
                    for (int z=dz0;z<=dz1;++z)
                        for (int x=dx0;x<=dx1;++x)
                            fprintf(stderr,"%d %d %d %d\n",x,z,
                                gm_world_biome(r.world,x,z),
                                gm_world_grass_color(r.world,x,dy,z));
                }
            }
        }
        write_state(out,&r);
        gm_particles_live_tick(&replay_particles,r.window,r.ox,r.oz);
        if(window_frames){
            int render = tick >= cfg->frame_offset &&
                         (tick - cfg->frame_offset) % cfg->frame_every == 0;
            GmPlayerView view;
            gm_runtime_view(&r,&view);
            gm_runtime_apply_tape_view(&r,&view);
            gm_window_compose_advance(window_frames,&view,&action,1);
            if(render){
                GmWindowComposeFrame wf={
                    .view=&view,.camera_view=&view,.partial_ticks=1.0f,
                    .interactive=1,.screen_open=0,
                    .mouse_x=cfg->width/2,.mouse_y=cfg->height/2,.stamp=NULL
                };
                if(!gm_window_compose_draw(window_frames,&wf,NULL,err,sizeof err)||
                   !gm_window_compose_emit_frame(window_frames,tick,err,sizeof err)){
                    fprintf(stderr,"frames-out: %s\n",err);goto bad;
                }
            }
        }else if(frames){
            int render = tick >= cfg->frame_offset &&
                         (tick - cfg->frame_offset) % cfg->frame_every == 0;
            if(!gm_frame_capture_write(frames,&r,&action,render,err,sizeof err)){
                fprintf(stderr,"frames-out: %s\n",err);goto bad;
            }
        }
    }
    if (have || (in && fgets(line,sizeof line,in))) { fprintf(stderr,"script: event lies beyond --ticks\n"); goto bad; }
    if (prof_on && r.tick > 0) {
        fprintf(stderr, "[state_prof] edits: total %lld over %lld ticks (mean %.2f/tick), "
                "max %lld @t%lld, nonzero ticks %lld (%.1f%%), "
                "hist 0|1-8|9-64|65-512|513+ = %lld|%lld|%lld|%lld|%lld\n",
                prof_tot, r.tick, (double)prof_tot / (double)r.tick,
                prof_max, prof_maxt, prof_nz, 100.0 * (double)prof_nz / (double)r.tick,
                prof_h[0], prof_h[1], prof_h[2], prof_h[3], prof_h[4]);
        prof_scan(&r);
    }
    gm_frame_capture_close(frames);gm_window_compose_close(window_frames);gm_runtime_destroy(&r); if(in)fclose(in); if(out!=stdout)fclose(out); return 0;
bad:
    gm_frame_capture_close(frames);gm_window_compose_close(window_frames);gm_runtime_destroy(&r); if(in)fclose(in); if(out!=stdout)fclose(out); return 2;
}
