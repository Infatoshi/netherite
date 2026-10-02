/* Field-by-field replay of DragonProbe's server-side state trace. */
#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../engine/dragon.h"
#include "../engine/env.h"
#include "../engine/jmath.h"
#include "../engine/world.h"
#include "../engine/item_entity.h"
#include "../engine/explosion.h"

static uint64_t dbits(double v) { uint64_t u; memcpy(&u, &v, 8); return u; }
static uint32_t fbits(float v) { uint32_t u; memcpy(&u, &v, 4); return u; }
static char *tok[256];
static int ntok;
static int tail_offset;
static int fail(int t, const char *field, uint64_t want, uint64_t got);
struct orb_env { ie_world iew; };
struct blast_ctx { struct dragon_state *dragon; struct orb_env *orbs; struct aabb box; };

static int blast_query(void *ctx, struct aabb query, struct expl_extra *out, int cap)
{
    struct blast_ctx *b=ctx;
    if (cap<1 || b->dragon->dead || !aabb_intersects(&b->box,&query)) return 0;
    out[0].ent=b->dragon; out[0].pos=&b->dragon->x;
    out[0].eye_height=8.0f*0.85f; out[0].box=&b->box;
    out[0].attack_from=NULL; out[0].add_motion=NULL; out[0].armor=NULL;
    return 1;
}

static void blast_attack(void *ctx, float amount) { (void)ctx; (void)amount; /* EntityDragon.attackEntityFrom returns false. */ }
static void blast_motion(void *ctx, double mx, double my, double mz)
{
    struct dragon_state *d=ctx; d->mx+=mx; d->my+=my; d->mz+=mz;
}

static int dragon_blast_query(void *ctx, struct aabb query, struct expl_extra *out, int cap)
{
    int n=blast_query(ctx,query,out,cap);
    if (n) { out[0].attack_from=blast_attack; out[0].add_motion=blast_motion; }
    return n;
}

static void crystal_explode(void *ctx, double x, double y, double z)
{
    struct blast_ctx *b=ctx;
    struct dragon_state *d=b->dragon;
    b->box=aabb_make(d->bb_min_x,d->bb_min_y,d->bb_min_z,d->bb_max_x,d->bb_min_y+8.0,d->bb_max_z);
    expl_run_extras(d->world,d->det,DET_OTHER,&b->orbs->iew.world_rand.r,&b->orbs->iew,NULL,
                    x,y,z,6.0f,0,1,NULL,dragon_blast_query,b);
}

static int xp_split(int n)
{
    static const int sizes[] = {2477,1237,617,307,149,73,37,17,7,3,1};
    for (size_t i = 0; i < sizeof sizes/sizeof sizes[0]; ++i) if (n >= sizes[i]) return sizes[i];
    return 1;
}

static void spawn_xp(void *ctx, double x, double y, double z, int amount)
{
    struct orb_env *env = ctx;
    while (amount > 0) {
        int xp = xp_split(amount);
        amount -= xp;
        ie_ent *en = ie_spawn_orb(&env->iew, x, y, z, xp);
        if (!en) { fprintf(stderr,"orb pool full\n"); exit(2); }
        ie_added_to_world(&env->iew, en);
    }
}

static void read_det_start(const char *dir, det_state *det, uint64_t *world_rand)
{
    char path[1024], line[2048];
    snprintf(path, sizeof path, "%s/start.txt", dir);
    FILE *f = fopen(path,"r"); if (!f) { perror(path); exit(2); }
    uint64_t seeder[DET_ROLES] = {0}, math[DET_ROLES] = {0};
    int32_t ids[DET_ROLES] = {0};
    int64_t seed = 0;
    while (fgets(line, sizeof line, f)) {
        long long v; int role; unsigned long long a,b,c;
        if (sscanf(line,"worldSeed %lld",&v)==1) { seed=v; continue; }
        if (sscanf(line,"nextId %d %d %d %d",&ids[0],&ids[1],&ids[2],&ids[3])==4) continue;
        if (sscanf(line,"digest %d %llx %llx %llx",&role,&a,&b,&c)==4 && role>=0 && role<DET_ROLES) {
            seeder[role]=a; math[role]=b; continue;
        }
        if (sscanf(line,"worldRand %llx",&a)==1) { *world_rand=a; continue; }
    }
    det_init(det); det_load(det,seed,seeder,math,ids); det_set_role(det,DET_OTHER);
    rewind(f);
    while (fgets(line,sizeof line,f)) {
        char name[DET_NAME_MAX]; unsigned long long s0,s1,s2,s3;
        int u0,u1,u2,u3;
        if (sscanf(line,"split %127s %llx %llx %llx %llx %d %d %d %d",name,&s0,&s1,&s2,&s3,&u0,&u1,&u2,&u3)==9) {
            const uint64_t states[DET_ROLES]={s0,s1,s2,s3};
            const uint8_t used[DET_ROLES]={(uint8_t)u0,(uint8_t)u1,(uint8_t)u2,(uint8_t)u3};
            det_split_add(det,name,states,used);
        }
    }
    fclose(f);
}

static int compare_det(FILE *ref, int tick, det_state *det, uint64_t world_rand)
{
    char line[1024];
    if (!fgets(line,sizeof line,ref)) { fprintf(stderr,"FAIL tick %d missing Det row\n",tick); return 1; }
    char *v[32]={0}; int n=0;
    for (char *s=strtok(line," \t\r\n"); s && n<32; s=strtok(NULL," \t\r\n")) v[n++]=s;
    if (n!=28 || strcmp(v[0],"t") || atoi(v[1])!=tick) { fprintf(stderr,"FAIL tick %d Det row format\n",tick); return 1; }
    for (int role=0;role<DET_ROLES;++role) {
        int i=2+role*5;
        if (strcmp(v[i],"role") || atoi(v[i+1])!=role) return fail(tick,"Det role header",role,atoi(v[i+1]));
        uint64_t want=strtoull(v[i+2],NULL,16),got=det_seeder_state(det,role);
        if (want!=got) return fail(tick,"Det seeder",want,got);
        want=strtoull(v[i+3],NULL,16); got=det_math_state(det,role);
        if (want!=got) return fail(tick,"Det math",want,got);
        want=strtoull(v[i+4],NULL,16); got=det_split_state(det,role);
        if (want!=got) return fail(tick,"Det split",want,got);
    }
    if (strcmp(v[22],"nextId") || atoi(v[23])!=det->next_id[DET_OTHER])
        return fail(tick,"Det nextId OTHER",atoi(v[23]),det->next_id[DET_OTHER]);
    uint64_t want=strtoull(v[25],NULL,16),got=world_rand;
    if (strcmp(v[24],"worldRand") || want!=got) return fail(tick,"world Random",want,got);
    return 0;
}

static int compare_orbs(FILE *ref, int tick, ie_world *iew)
{
    char line[512];
    for (int j = 0; j < iew->n; ++j) {
        if (!fgets(line,sizeof line,ref)) { fprintf(stderr,"FAIL tick %d orb %d missing oracle row\n",tick,j); return 1; }
        char *v[20] = {0}; int n=0;
        for (char *s=strtok(line," \t\r\n"); s && n<20; s=strtok(NULL," \t\r\n")) v[n++]=s;
        if (n!=16 || atoi(v[0])!=tick || atoi(v[1])!=j) { fprintf(stderr,"FAIL tick %d orb %d row header\n",tick,j); return 1; }
        ie_ent *e=ie_ent_at(iew->slot[j]);
#define OI(I,NAME,VAL) do { uint64_t want=strtoull(v[I],NULL,10),got=(uint64_t)(VAL); if(want!=got) { fprintf(stderr,"orb %d: ",j); return fail(tick,NAME,want,got); } } while(0)
#define OD(I,NAME,VAL) do { uint64_t want=strtoull(v[I],NULL,16),got=dbits(VAL); if(want!=got) { fprintf(stderr,"orb %d: ",j); return fail(tick,NAME,want,got); } } while(0)
#define OF(I,NAME,VAL) do { uint64_t want=strtoull(v[I],NULL,16),got=fbits(VAL); if(want!=got) { fprintf(stderr,"orb %d: ",j); return fail(tick,NAME,want,got); } } while(0)
        OI(2,"orb id",e->entity_id); OI(3,"orb xpValue",e->xp_value);
        OD(4,"orb x",e->e.pos_x); OD(5,"orb y",e->e.pos_y); OD(6,"orb z",e->e.pos_z);
        OD(7,"orb motionX",e->e.motion_x); OD(8,"orb motionY",e->e.motion_y); OD(9,"orb motionZ",e->e.motion_z);
        OF(10,"orb yaw",e->rotation_yaw); OI(11,"orb age",e->age); OI(12,"orb health",e->health);
        OI(13,"orb rand",det_rng_state(&e->rand)); OI(14,"orb dead",e->is_dead); OI(15,"orb in chunk",e->added_to_chunk);
#undef OI
#undef OD
#undef OF
    }
    long pos = ftell(ref);
    if (fgets(line,sizeof line,ref)) {
        int row_tick = atoi(line);
        if (row_tick == tick) { fprintf(stderr,"FAIL tick %d extra oracle orb\n",tick); return 1; }
        fseek(ref,pos,SEEK_SET);
    }
    return 0;
}

static int compare_extra(FILE *ref, int tick, const struct dragon_state *d)
{
    if (d->n_crystals<2) return 0;
    char line[256];
    if (!fgets(line,sizeof line,ref)) { fprintf(stderr,"FAIL tick %d missing second crystal row\n",tick); return 1; }
    char *v[8]={0}; int n=0;
    for (char *s=strtok(line," \t\r\n"); s && n<8; s=strtok(NULL," \t\r\n")) v[n++]=s;
    if (n!=8 || atoi(v[0])!=tick) { fprintf(stderr,"FAIL tick %d second crystal row\n",tick); return 1; }
    const struct dragon_crystal_state *c=&d->crystals[1];
    if (strtoull(v[1],NULL,16)!=dbits(c->x)) return fail(tick,"second crystal x",strtoull(v[1],NULL,16),dbits(c->x));
    if (strtoull(v[2],NULL,16)!=dbits(c->y)) return fail(tick,"second crystal y",strtoull(v[2],NULL,16),dbits(c->y));
    if (strtoull(v[3],NULL,16)!=dbits(c->z)) return fail(tick,"second crystal z",strtoull(v[3],NULL,16),dbits(c->z));
    if (atoi(v[4])!=c->inner_rotation) return fail(tick,"second crystal rotation",atoi(v[4]),c->inner_rotation);
    if (atoi(v[5])!=c->health) return fail(tick,"second crystal health",atoi(v[5]),c->health);
    if (atoi(v[6])!=c->dead) return fail(tick,"second crystal dead",atoi(v[6]),c->dead);
    if (strtoull(v[7],NULL,10)!=det_rng_state(&c->rand)) return fail(tick,"second crystal rand",strtoull(v[7],NULL,10),det_rng_state(&c->rand));
    return 0;
}
struct write_check { FILE *ref; int tick, failures, count; };

static void on_write(void *ctx, int x, int y, int z, int id, int meta)
{
    struct write_check *c = ctx;
    char line[160] = {0}; int rt, rx, ry, rz, rid, rm;
    if (id == 0xffff) id = -1;
    if (!fgets(line, sizeof line, c->ref) || sscanf(line, "%d %d %d %d %d %d", &rt, &rx, &ry, &rz, &rid, &rm) != 6 ||
        rt != c->tick || rx != x || ry != y || rz != z || rid != id || rm != meta) {
        fprintf(stderr, "FAIL tick %d block write %d: native (%d,%d,%d)=%d:%d, oracle %s", c->tick, c->count, x,y,z,id,meta,line);
        ++c->failures;
    }
    ++c->count;
}

static int split(char *line)
{
    ntok = 0;
    for (char *s = strtok(line, " \t\r\n"); s && ntok < 256; s = strtok(NULL, " \t\r\n")) tok[ntok++] = s;
    return ntok;
}

static int fail(int t, const char *field, uint64_t want, uint64_t got)
{
    fprintf(stderr, "FAIL tick %d dragon %s: oracle %016" PRIx64 " native %016" PRIx64 "\n", t, field, want, got);
    return 1;
}

#define CD(I, FIELD, VALUE) do { uint64_t w = strtoull(tok[I], NULL, 16), g = dbits(VALUE); if (w != g) return fail(t, FIELD, w, g); } while (0)
#define CF(I, FIELD, VALUE) do { uint64_t w = strtoull(tok[I], NULL, 16), g = fbits(VALUE); if (w != g) return fail(t, FIELD, w, g); } while (0)
#define CI(I, FIELD, VALUE) do { uint64_t w = strtoull(tok[I], NULL, 10), g = (uint64_t)(VALUE); if (w != g) return fail(t, FIELD, w, g); } while (0)

static int compare(int t, struct dragon_state *d)
{
    CD(13,"targetX",d->tx); CD(14,"targetY",d->ty); CD(15,"targetZ",d->tz);
    CF(7,"yaw",d->yaw); CF(11,"randomYawVelocity",d->yaw_velocity);
    CD(4,"motionX",d->mx); CD(5,"motionY",d->my); CD(6,"motionZ",d->mz);
    CD(190+tail_offset,"bbox.minX",d->bb_min_x); CD(191+tail_offset,"bbox.maxX",d->bb_max_x);
    CD(1,"x",d->x); CD(2,"y",d->y); CD(3,"z",d->z);
    CF(8,"prevYaw",d->prev_yaw);
    CF(9,"animTime",d->anim); CF(10,"prevAnimTime",d->prev_anim);
    CF(12,"renderYawOffset",d->render_yaw);
    CI(16,"forceNewTarget",d->force_target); CI(17,"slowed",d->slowed);
    CI(18,"ringIndex",d->ring_index); CI(19,"ticksExisted",d->ticks);
    CF(20,"health",d->health); CI(21,"hurtTime",d->hurt_time);
    CI(22,"deathTime",d->death_time); CI(23,"deathTicks",d->death_ticks);
    CI(24,"rand",det_rng_state(&d->rand)); CI(25,"target",d->has_target); CI(26,"healingCrystal",d->healing_crystal_index==0);
    for (int i = 0; i < 64; ++i) { CD(27+i*2,"ring yaw",d->ring[i][0]); CD(28+i*2,"ring y",d->ring[i][1]); }
    for (int i = 0; i < 7; ++i) {
        int j = 155+i*5;
        CD(j,"part x",d->part[i].x); CD(j+1,"part y",d->part[i].y); CD(j+2,"part z",d->part[i].z);
        CF(j+3,"part width",d->part[i].width); CF(j+4,"part height",d->part[i].height);
    }
    if (d->n_crystals) {
        const struct dragon_crystal_state *c=&d->crystals[0];
        CD(190,"crystal x",c->x); CD(191,"crystal y",c->y); CD(192,"crystal z",c->z);
        CI(193,"crystal innerRotation",c->inner_rotation); CI(194,"crystal health",c->health);
        CI(195,"crystal dead",c->dead); CI(196,"crystal rand",det_rng_state(&c->rand));
    }
    if (d->has_player) {
        int j = 195+tail_offset;
        CD(j,"player x",d->player_x); CD(j+1,"player y",d->player_y); CD(j+2,"player z",d->player_z);
        CD(j+3,"player motionX",d->player_mx); CD(j+4,"player motionY",d->player_my); CD(j+5,"player motionZ",d->player_mz);
        CF(j+6,"player health",d->player_health); CI(j+7,"player hurtTime",d->player_hurt_time);
        CI(j+8,"player hurtResistantTime",d->player_hurt_resistant_time);
        CI(j+9,"player rand",det_rng_state(&d->player_rand)); CD(j+10,"player bbox.minY",d->player_min_y);
    }
    CI(ntok-6,"hurtResistantTime",d->hurt_resistant_time);
    CF(ntok-5,"lastDamage",d->last_damage); CF(ntok-4,"prevHealth",d->prev_health);
    CI(ntok-3,"dead",d->dead_flag); CI(ntok-2,"isDead",d->dead);
    CI(ntok-1,"healing crystal index",d->healing_crystal_index);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) { fprintf(stderr,"usage: test_dragon DIR [--negative=draw]\n"); return 2; }
    char path[1024]; snprintf(path, sizeof path, "%s/ticks.txt", argv[1]);
    FILE *fp = fopen(path, "r");
    if (!fp) { printf("skip %s: no dragon trace\n", argv[1]); return 0; }
    jmath_init();
    nw_env->cfg.dragon_negative_draw = argc==3 && !strcmp(argv[2],"--negative=draw");
    char *line = NULL; size_t cap = 0;
    if (getline(&line, &cap, fp) < 0 || split(line) < 190) { fprintf(stderr,"invalid initial dragon record\n"); return 2; }
    tail_offset = (ntok == 208 || ntok == 219) ? 7 : 0;
    struct dragon_state d;
    snprintf(path, sizeof path, "%s/manifest.json", argv[1]);
    FILE *mf = fopen(path, "r");
    if (!mf) { perror(path); return 2; }
    char manifest[512]; if (!fgets(manifest, sizeof manifest, mf)) return 2;
    fclose(mf);
    char *seedp = strstr(manifest, "\"seed\":");
    if (!seedp) { fprintf(stderr,"manifest lacks seed\n"); return 2; }
    long long seed = strtoll(seedp + 7, NULL, 10);
    struct world w;
    world_init(&w, seed);
    w.dim = 1;
    for (int cx = -12; cx <= 12; ++cx)
        for (int cz = -12; cz <= 12; ++cz) world_load_chunk(&w, cx, cz);
    snprintf(path, sizeof path, "%s/shapes.txt", argv[1]);
    FILE *shapes = fopen(path, "r");
    if (!shapes) { perror(path); return 2; }
    int sx, sy, sz, sid, smeta;
    while (fscanf(shapes, "%d %d %d %d %d", &sx, &sy, &sz, &sid, &smeta) == 5)
        world_set_block(&w, sx, sy, sz, sid, smeta, 2);
    fclose(shapes);
    det_state det; uint64_t world_rand=0;
    read_det_start(argv[1],&det,&world_rand);
    dragon_init(&d,&w,&det,0.0,128.0,0.0,25.0f);
    if (strstr(manifest,"\"mode\":\"no_grief\"")) d.mob_griefing=0;
    if (tail_offset) dragon_crystal_init(&d,&det,8.0,128.0,0.0);
    if (strstr(manifest,"\"mode\":\"multi_crystal\"")) dragon_crystal_init(&d,&det,-8.0,128.0,0.0);
    if (ntok >= 206+tail_offset) {
        d.has_player=1;
        (void)det_next_entity_id_role(&det,DET_OTHER);
        d.player_rand=det_new_random_role(&det,DET_OTHER);
        int64_t msb,lsb; det_uuid_role(&det,DET_OTHER,&msb,&lsb);
        for (int k=0;k<3;++k) (void)det_math_random_role(&det,DET_OTHER);
        d.player_x=0.5; d.player_y=strstr(manifest,"\"mode\":\"pillar\"") ? 128.0 : 100.0;
        d.player_z=0.5; d.player_min_y=d.player_y; d.player_health=20.0f;
    }
    if (compare(-1,&d)) return 1;
    snprintf(path,sizeof path,"%s/det.txt",argv[1]);
    FILE *det_ref=fopen(path,"r"); if (!det_ref) { perror(path); return 2; }
    if (compare_det(det_ref,-1,&det,world_rand)) return 1;
    snprintf(path,sizeof path,"%s/extra.txt",argv[1]);
    FILE *extra_ref=fopen(path,"r"); if (!extra_ref) { perror(path); return 2; }
    if (compare_extra(extra_ref,-1,&d)) return 1;
    struct orb_env orbs;
    ie_init(&orbs.iew,&w,&det);
    orbs.iew.world_rand.r.seed=world_rand & 0xFFFFFFFFFFFFULL;
    d.spawn_xp=spawn_xp; d.spawn_xp_ctx=&orbs;
    struct blast_ctx blast={.dragon=&d,.orbs=&orbs};
    d.crystal_explode=crystal_explode; d.crystal_explode_ctx=&blast;
    snprintf(path,sizeof path,"%s/orbs.txt",argv[1]);
    FILE *orb_ref=fopen(path,"r"); if (!orb_ref) { perror(path); return 2; }
    snprintf(path, sizeof path, "%s/writes.txt", argv[1]);
    struct write_check wc = {0};
    wc.ref = fopen(path, "r");
    if (!wc.ref) { perror(path); return 2; }
    w.on_block = on_write; w.on_block_ctx = &wc;
    int t = 0;
    while (getline(&line, &cap, fp) >= 0) {
        if (split(line) < 190) { fprintf(stderr,"short dragon record %d\n",t); return 2; }
        wc.tick = t;
        if (strstr(manifest, "\"mode\":\"parts\"") && t >= 20 && t <= 260 && (t - 20) % 40 == 0)
            dragon_attack_part(&d, (t - 20) / 40, 8.0f, 1);
        if (strstr(manifest, "\"mode\":\"parts\"") && t==21) dragon_attack_part(&d,1,8.0f,1);
        if (strstr(manifest, "\"mode\":\"parts\"") && t==22) dragon_attack_part(&d,0,12.0f,1);
        if (strstr(manifest, "\"mode\":\"death\"") && t >= 20 && t <= 180 && (t - 20) % 40 == 0)
            dragon_attack_part(&d, 0, 40.0f, 2);
        if (strstr(manifest, "\"mode\":\"healing\"") && t==20) dragon_attack_part(&d,0,40.0f,2);
        if (strstr(manifest, "\"mode\":\"crystal_kill\"") && t==100) dragon_crystal_attack(&d,0);
        if (strstr(manifest, "\"mode\":\"multi_crystal\"") && t==150) dragon_crystal_attack(&d,1);
        if (strstr(manifest, "\"mode\":\"force_target\"") && (t==50 || t==150)) d.force_target=1;
        dragon_tick(&d);
        for (int j=0; j<orbs.iew.n; ++j) { int nrem=0; ie_tick_one(&orbs.iew,ie_ent_at(orbs.iew.slot[j]),t,NULL,0,&nrem); }
        for (int i=0;i<d.n_crystals;++i) dragon_crystal_tick(&d,i);
        if (compare(t, &d)) return 1;
        if (compare_det(det_ref,t,&det,orbs.iew.world_rand.r.seed)) return 1;
        if (compare_extra(extra_ref,t,&d)) return 1;
        if (compare_orbs(orb_ref,t,&orbs.iew)) return 1;
        if (wc.failures) return 1;
        ++t;
    }
    if (fgetc(wc.ref) != EOF) { fprintf(stderr,"FAIL unconsumed oracle block writes\n"); return 1; }
    fclose(wc.ref);
    if (fgetc(orb_ref)!=EOF) { fprintf(stderr,"FAIL unconsumed oracle orbs\n"); return 1; }
    fclose(orb_ref);
    if (fgetc(det_ref)!=EOF) { fprintf(stderr,"FAIL unconsumed Det rows\n"); return 1; }
    fclose(det_ref);
    if (fgetc(extra_ref)!=EOF) { fprintf(stderr,"FAIL unconsumed second crystal rows\n"); return 1; }
    fclose(extra_ref);
    free(line); fclose(fp);
    printf("PASS dragon %s %d ticks\n", argv[1], t);
    return 0;
}
