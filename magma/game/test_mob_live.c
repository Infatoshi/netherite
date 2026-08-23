#include "game/runtime.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fail;
#define CHECK(C, M) do { if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail=1; } } while (0)

static int init_flat_seed(GmRuntime *r, long long seed) {
    GmConfig c; char err[256];
    gm_config_defaults(&c); c.world=GM_WORLD_SUPERFLAT; c.view_distance=1;
    c.seed=seed;
    if(!gm_runtime_init(r,&c,err,sizeof err)){fprintf(stderr,"init: %s\n",err);return 0;}
    gm_runtime_set_pose(r,8.5,5.0,8.5,0.0f,10.0f);
    return 1;
}

static int init_flat(GmRuntime *r) { return init_flat_seed(r,0); }

typedef struct {
    float x, z, yaw, head_yaw, pitch;
    int eat_time;
} SheepSample;

typedef struct {
    SheepSample samples[512];
    int nsamples;
    int head_onsets[16];
    int nhead_onsets;
    int panic_tick;
    double panic_step;
} SheepTrace;

static EwStore *test_mob_store(GmMobLive *m) {
    return m->current ? &m->b : &m->a;
}

static int sheep_view(GmRuntime *r, GmEntityView *out) {
    GmEntityView views[EW_MAX_ENTITIES];
    int n=gm_mobs_fill_views(&r->mobs,views,EW_MAX_ENTITIES);
    for(int i=0;i<n;++i)if(views[i].type==GM_MOB_SHEEP){*out=views[i];return 1;}
    return 0;
}

static double oracle_sheep_panic_first_step(void) {
    /* EntityAIPanic navigator speed 1.25; EntityMoveHelper multiplies the
     * 0.23000000417232513 attribute and EntityLiving writes the resulting
     * float to both landMovementFactor and moveForward. Ground travel damps
     * forward by 0.98 and applies the 0.6*0.91 slipperiness factor. */
    float ai=(float)(1.25*0.23000000417232513);
    float friction=0.6f*0.91f;
    float accel=0.16277136f/(friction*friction*friction);
    return (double)((ai*0.98f)*(ai*accel));
}

static int run_sheep_trace(SheepTrace *trace) {
    GmRuntime r;memset(trace,0,sizeof *trace);
    trace->panic_tick=-1;
    if(!init_flat_seed(&r,0))return 0;
    gm_runtime_set_pose(&r,8.5,5.0,8.5,0.0f,10.0f);
    int slot=gm_mobs_spawn(&r.mobs,GM_MOB_SHEEP,8.5,5.0,16.5);
    if(slot<0){gm_runtime_destroy(&r);return 0;}
    GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
    int looking=0;
    for(int tick=0;tick<400;++tick){
        gm_runtime_tick(&r,idle);
        GmEntityView v;if(!sheep_view(&r,&v)){gm_runtime_destroy(&r);return 0;}
        SheepSample *s=&trace->samples[trace->nsamples++];
        s->x=v.x;s->z=v.z;s->yaw=v.yaw;s->head_yaw=v.head_yaw;s->pitch=v.pitch;
        s->eat_time=r.mobs.passive_eat_time[slot];
        int now_looking=fabsf(v.head_yaw-v.yaw)>0.5f||fabsf(v.pitch)>0.5f;
        if(now_looking&&!looking&&trace->nhead_onsets<16)
            trace->head_onsets[trace->nhead_onsets++]=tick;
        looking=now_looking;
    }

    /* Component reset isolates the first post-hit MOVE_TO acceleration from
     * any residual wander momentum; the hit and AI path are still ordinary
     * live-sim code. Keep both double buffers coherent. */
    EwStore *cur=test_mob_store(&r.mobs),*other=r.mobs.current?&r.mobs.a:&r.mobs.b;
    cur->vx[slot]=cur->vz[slot]=0.0;cur->path_len[slot]=0;
    other->vx[slot]=other->vz[slot]=0.0;other->path_len[slot]=0;
    r.mobs.passive_tasks[slot]=0;r.mobs.passive_task_tick[slot]=0;
    GmEntityView before;if(!sheep_view(&r,&before)){gm_runtime_destroy(&r);return 0;}
    if(!gm_mobs_damage_near(&r.mobs,before.x,before.y+0.5,before.z,1.0,1.0f,&r.entities)){
        gm_runtime_destroy(&r);return 0;
    }
    for(int tick=0;tick<60;++tick){
        GmEntityView prev;if(!sheep_view(&r,&prev)){gm_runtime_destroy(&r);return 0;}
        gm_runtime_tick(&r,idle);
        GmEntityView next;if(!sheep_view(&r,&next)){gm_runtime_destroy(&r);return 0;}
        double dx=(double)next.x-prev.x,dz=(double)next.z-prev.z;
        double step=sqrt(dx*dx+dz*dz);
        if(step>1e-6){trace->panic_tick=tick;trace->panic_step=step;break;}
    }
    gm_runtime_destroy(&r);return 1;
}

typedef struct {
    int charged_on, charged_off;
    int transitions[8], transition_state[8], ntransitions;
    int shots[16], nshots;
    int first_100_shots;
} BlazeSchedule;

static int run_blaze_ticks(int ticks, FILE *receipt, BlazeSchedule *schedule) {
    GmRuntime r;
    if(!init_flat(&r))return 0;
    int slot=gm_mobs_spawn(&r.mobs,GM_MOB_BLAZE,8.5,5.0,20.5);
    if(slot<0){gm_runtime_destroy(&r);return 0;}
    memset(schedule,0,sizeof *schedule);
    if(receipt)
        fprintf(receipt,"tick\tattackStep\tattackTime\tcharged\tfireball_spawn\n");
    int previous=-1;
    for(int tick=0;tick<ticks;++tick){
        gm_runtime_set_pose(&r,8.5,5.0,8.5,0.0f,0.0f);
        gm_mobs_tick(&r.mobs,r.world,(const struct McSinTable *)&r.sin_table,
                     (struct PsvPlayer *)&r.player,(struct PvStats *)&r.vitals,
                     r.ox,r.oz,r.dimension,r.clock.world_time,&r.entities,
                     0.0f,0.0f,1);
        double x,y,z,vx,vy,vz;
        int fireball=gm_mobs_take_fireball(&r.mobs,&x,&y,&z,&vx,&vy,&vz)==3;
        EwStore *store=test_mob_store(&r.mobs);
        GmEntityView views[EW_MAX_ENTITIES];
        int nviews=gm_mobs_fill_views(&r.mobs,views,EW_MAX_ENTITIES),charged=0;
        for(int i=0;i<nviews;++i)
            if(views[i].type==GM_MOB_BLAZE){charged=(views[i].flags&1)!=0;break;}
        if(charged)schedule->charged_on++;
        else schedule->charged_off++;
        if(charged!=previous){
            if(schedule->ntransitions<(int)(sizeof schedule->transitions/sizeof schedule->transitions[0])){
                schedule->transitions[schedule->ntransitions]=tick;
                schedule->transition_state[schedule->ntransitions]=charged;
            }
            schedule->ntransitions++;
            previous=charged;
        }
        if(fireball){
            if(schedule->nshots<(int)(sizeof schedule->shots/sizeof schedule->shots[0]))
                schedule->shots[schedule->nshots]=tick;
            schedule->nshots++;
            if(tick<100)schedule->first_100_shots++;
        }
        if(receipt)
            fprintf(receipt,"%d\t%d\t%d\t%d\t%d\n",tick,r.mobs.charge[slot],
                    store->attack_time[slot],charged,fireball);
    }
    gm_runtime_destroy(&r);
    return 1;
}

static int legacy_flat_shots(int ticks, int *first_100) {
    /* Baseline 99007ad^: mob_live reloaded attack_time=40 whenever <=0,
     * then runtime spawned a blaze fireball on each reload edge. */
    int attack_time=0,shots=0;
    *first_100=0;
    for(int tick=0;tick<ticks;++tick){
        if(attack_time>0)--attack_time;
        if(attack_time<=0)attack_time=40;
        if(attack_time==40){++shots;if(tick<100)++*first_100;}
    }
    return shots;
}

static int blaze_task_reset_test(void) {
    GmRuntime r;
    if(!init_flat(&r))return 0;
    int slot=gm_mobs_spawn(&r.mobs,GM_MOB_BLAZE,8.5,5.0,20.5);
    if(slot<0){gm_runtime_destroy(&r);return 0;}
    for(int tick=0;tick<10;++tick){
        gm_mobs_tick(&r.mobs,r.world,(const struct McSinTable *)&r.sin_table,
                     (struct PsvPlayer *)&r.player,(struct PvStats *)&r.vitals,
                     r.ox,r.oz,r.dimension,r.clock.world_time,&r.entities,
                     0.0f,0.0f,1);
        (void)gm_mobs_take_fireball(&r.mobs,NULL,NULL,NULL,NULL,NULL,NULL);
    }
    int attack_time=test_mob_store(&r.mobs)->attack_time[slot];
    gm_runtime_set_pose(&r,8.5,5.0,70.5,0.0f,0.0f); /* outside follow range */
    gm_mobs_tick(&r.mobs,r.world,(const struct McSinTable *)&r.sin_table,
                 (struct PsvPlayer *)&r.player,(struct PvStats *)&r.vitals,
                 r.ox,r.oz,r.dimension,r.clock.world_time,&r.entities,
                 0.0f,0.0f,1);
    int paused=test_mob_store(&r.mobs)->attack_time[slot];
    int reset=!r.mobs.blaze_on_fire[slot]&&r.mobs.charge[slot]==0;
    gm_runtime_set_pose(&r,8.5,5.0,8.5,0.0f,0.0f);
    gm_mobs_tick(&r.mobs,r.world,(const struct McSinTable *)&r.sin_table,
                 (struct PsvPlayer *)&r.player,(struct PvStats *)&r.vitals,
                 r.ox,r.oz,r.dimension,r.clock.world_time,&r.entities,
                 0.0f,0.0f,1);
    int resumed=test_mob_store(&r.mobs)->attack_time[slot];
    gm_runtime_destroy(&r);
    return attack_time==51&&paused==51&&reset&&resumed==50;
}

static int blaze_schedule_receipt(FILE *receipt) {
    BlazeSchedule two_cycles,long_run;
    int saved=fail;
    fail=0;
    CHECK(run_blaze_ticks(356,receipt,&two_cycles),
          "deterministic blaze schedule executes");
    if(fail){fail=saved||fail;return 0;}
    CHECK(two_cycles.charged_on==156&&two_cycles.charged_off==200,
          "two blaze cycles reproduce 78 charged / 100 uncharged ticks each");
    CHECK(two_cycles.ntransitions==4&&
          two_cycles.transitions[0]==0&&two_cycles.transition_state[0]==1&&
          two_cycles.transitions[1]==78&&two_cycles.transition_state[1]==0&&
          two_cycles.transitions[2]==178&&two_cycles.transition_state[2]==1&&
          two_cycles.transitions[3]==256&&two_cycles.transition_state[3]==0,
          "blaze charged transitions reproduce the 178-tick vanilla cycle");
    CHECK(two_cycles.nshots==6&&
          two_cycles.shots[0]==60&&two_cycles.shots[1]==66&&two_cycles.shots[2]==72&&
          two_cycles.shots[3]==238&&two_cycles.shots[4]==244&&two_cycles.shots[5]==250,
          "blaze fires three-shot volleys after charge at six-tick spacing");
    CHECK(blaze_task_reset_test(),
          "resetTask clears charge while inactive attackTime stays paused");
    CHECK(run_blaze_ticks(3560,NULL,&long_run),
          "long-run live blaze cadence executes");
    int old_first_100=0,old_shots=legacy_flat_shots(3560,&old_first_100);
    CHECK(old_shots==89&&long_run.nshots==60,
          "old and vanilla live cadence counts match exact common horizon");
    CHECK(old_first_100==3&&long_run.first_100_shots==3,
          "both schedules spawn three fireballs in the first 100 aggro ticks");
    if(receipt){
        fprintf(receipt,"# charged_on=%d charged_off=%d cycles=2 duty=78_on/100_off\n",
                two_cycles.charged_on,two_cycles.charged_off);
        fprintf(receipt,"# transitions=0:on,78:off,178:on,256:off\n");
        fprintf(receipt,"# fireballs=60,66,72,238,244,250\n");
        fprintf(receipt,"# cadence_horizon_ticks=3560 old_flat_shots=%d "
                        "new_vanilla_shots=%d old_per_100=%.9f new_per_100=%.9f\n",
                old_shots,long_run.nshots,old_shots*100.0/3560.0,
                long_run.nshots*100.0/3560.0);
        fprintf(receipt,"# first_100 old_flat_shots=%d new_vanilla_shots=%d\n",
                old_first_100,long_run.first_100_shots);
    }
    fprintf(stderr,"mob_live: blaze duty on=%d off=%d shots=[%d,%d,%d,%d,%d,%d] "
                   "cadence/100 old=%.9f new=%.9f\n",
            two_cycles.charged_on,two_cycles.charged_off,
            two_cycles.shots[0],two_cycles.shots[1],two_cycles.shots[2],
            two_cycles.shots[3],two_cycles.shots[4],two_cycles.shots[5],
            old_shots*100.0/3560.0,long_run.nshots*100.0/3560.0);
    {
        int ok=!fail;
        fail=saved||!ok;
        return ok;
    }
}

int main(int argc, char **argv) {
    if(argc==3&&!strcmp(argv[1],"--blaze-receipt")){
        FILE *receipt=fopen(argv[2],"w");
        if(!receipt){perror(argv[2]);return 1;}
        int ok=blaze_schedule_receipt(receipt);
        if(fclose(receipt)!=0){perror(argv[2]);return 1;}
        return ok?0:1;
    }
    if(argc!=1){fprintf(stderr,"usage: %s [--blaze-receipt PATH]\n",argv[0]);return 2;}
    GmRuntime r;
    if(!init_flat(&r))return 1;
    CHECK(gm_mobs_spawn(&r.mobs,EW_TYPE_ZOMBIE,8.5,5.0,14.5)>=0,"spawn component zombie");
    GmEntityView v[EW_MAX_ENTITIES];
    int n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES); double z0=n?v[0].z:0.0;
    GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
    for(int i=0;i<8;++i)gm_runtime_tick(&r,idle);
    n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
    fprintf(stderr,"mob_live: chase z %.6f -> %.6f y %.6f\n",z0,n?v[0].z:0.0,n?v[0].y:0.0);
    CHECK(n==1 && v[0].z<z0,"hostile chases through living-base physics");
    CHECK(v[0].y>=3.99f,"hostile collision does not fall through superflat floor");
    gm_runtime_destroy(&r);

    /* EntityDragon.collideWithEntities: a recorded expanded wing-part query
     * applies causeMobDamage(5), while a non-overlapping part does nothing.
     * A stronger raw hit inside hurtResistantTime applies only the delta, per
     * EntityLivingBase.attackEntityFrom. */
    if(!init_flat(&r))return 1;
    CHECK(gm_runtime_dragon_contact(&r,7.0,4.0,7.0,10.0,8.0,10.0,5.0f),
          "dragon wing contact query hits player");
    CHECK(r.vitals.health==15.0f,"dragon wing contact subtracts exact 5 hp");
    CHECK(!gm_runtime_dragon_contact(&r,30.0,4.0,30.0,34.0,8.0,34.0,5.0f),
          "non-overlapping dragon part does not damage player");
    CHECK(gm_runtime_dragon_contact(&r,7.0,4.0,7.0,10.0,8.0,10.0,6.0f),
          "stronger contact passes hurt resistance");
    CHECK(r.vitals.health==14.0f,
          "hurt-resistant stronger contact applies lastDamage delta");
    gm_runtime_destroy(&r);

    /* EntityZombie.applyEntityAttributes ATTACK_DAMAGE=3.0;
     * EntityPlayer.attackEntityFrom leaves it unchanged on NORMAL. After ten
     * FoodStats.onUpdate ticks, saturation regen heals 5/6 exactly, for a net
     * first-hit loss of 3 - 5/6 = 13/6. */
    if(!init_flat(&r))return 1;
    CHECK(gm_mobs_spawn(&r.mobs,EW_TYPE_ZOMBIE,8.5,5.0,10.5)>=0,"spawn damage-model zombie");
    gm_runtime_tick(&r,idle);
    CHECK(r.vitals.health==17.0f,"normal zombie melee subtracts exact 3 hp");
    for(int i=0;i<10;++i)gm_runtime_tick(&r,idle);
    CHECK(r.vitals.health==17.0f+5.0f/6.0f,
          "first zombie hit plus saturation regen loses exact 13/6 hp");
    gm_runtime_destroy(&r);

    /* EntityWitherSkeleton.onInitialSpawn sets base ATTACK_DAMAGE=4, and its
     * stone sword supplies a +4 ItemSword attribute modifier. The freshly
     * applied duration-200 wither tick is rejected by hurtResistantTime; the
     * duration-160 tick drains one hp after the player leaves melee reach. */
    if(!init_flat(&r))return 1;
    r.vitals.foodLevel=0;r.vitals.saturation=0.0f;
    CHECK(gm_mobs_spawn(&r.mobs,GM_MOB_WITHER_SKELETON,8.5,5.0,10.5)>=0,
          "spawn damage-model wither skeleton");
    gm_runtime_tick(&r,idle);
    CHECK(r.vitals.health==12.0f,"wither skeleton melee subtracts exact 4+4 hp");
    CHECK(r.mobs.player_wither_ticks==200,"wither skeleton applies 200-tick wither");
    gm_runtime_set_pose(&r,8.5,5.0,40.5,0.0f,10.0f);
    for(int i=0;i<40;++i)gm_runtime_tick(&r,idle);
    CHECK(r.vitals.health==12.0f,
          "duration-200 wither pulse is blocked by melee hurt resistance");
    gm_runtime_tick(&r,idle);
    CHECK(r.vitals.health==11.0f&&r.mobs.player_wither_ticks==159,
          "MobEffects.WITHER duration-160 pulse drains one hp");
    gm_runtime_destroy(&r);

    if(!init_flat(&r))return 1;
    isr_set_stack(&r.player.inv,0,ic_mk(276,1,0));
    CHECK(gm_mobs_spawn(&r.mobs,EW_TYPE_ZOMBIE,8.5,5.0,10.5)>=0,"spawn combat zombie");
    GmAction attack;memset(&attack,0,sizeof attack);attack.attack=1;attack.hotbar_sel=0;
    for(int i=0;i<35 && gm_mobs_alive(&r.mobs);++i)gm_runtime_tick(&r,attack);
    float post_combat_health=r.vitals.health;
    CHECK(gm_mobs_alive(&r.mobs)==0,"held attack kills hostile under cooldown");
    n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
    int xp_visible=0;for(int i=0;i<n;++i)xp_visible|=v[i].type==GM_ENTITY_XP_ORB;
    CHECK(xp_visible&&r.mobs.xp_total==0,"hostile death creates XP entities before pickup");
    for(int i=0;i<200&&r.mobs.xp_total<5;++i)gm_runtime_tick(&r,idle);
    CHECK(r.mobs.xp_total==5,"XP entities attract, collide, and award route XP");
    int flesh=0;
    for(int i=0;i<GM_LIVE_MAX;++i)if(r.entities.ents[i].active&&r.entities.ents[i].item==367)flesh=1;
    CHECK(flesh,"zombie death creates rotten-flesh item entity");
    CHECK(post_combat_health<20.0f&&post_combat_health>0.0f,
          "in-reach hostile damages player without killing test");
    gm_runtime_destroy(&r);

    if(!init_flat(&r))return 1;
    isr_set_stack(&r.player.inv,0,ic_mk(276,1,0));
    CHECK(gm_mobs_spawn(&r.mobs,GM_MOB_BLAZE,8.5,5.0,10.5)>=0,"spawn component blaze");
    attack.forward = 1;
    for(int i=0;i<80&&gm_mobs_alive(&r.mobs);++i)gm_runtime_tick(&r,attack);
    int rod=0;for(int i=0;i<GM_LIVE_MAX;++i)if(r.entities.ents[i].active&&r.entities.ents[i].item==369)rod=1;
    for(int i=0;i<200&&r.mobs.xp_total<10;++i)gm_runtime_tick(&r,idle);
    CHECK(gm_mobs_alive(&r.mobs)==0&&r.mobs.xp_total==10,"blaze XP entities reach the player");
    CHECK(rod,"blaze deterministic loot roll can produce a blaze rod");
    gm_runtime_destroy(&r);

    SheepTrace sheep_a,sheep_b;
    CHECK(run_sheep_trace(&sheep_a)&&run_sheep_trace(&sheep_b),
          "deterministic sheep scenarios execute");
    CHECK(!memcmp(&sheep_a,&sheep_b,sizeof sheep_a),
          "same seed and world produce byte-identical passive AI traces");
    fprintf(stderr,"mob_live: sheep head onsets=%d first=[%d,%d,%d] panic_tick=%d step=%.9f oracle=%.9f\n",
            sheep_a.nhead_onsets,sheep_a.head_onsets[0],sheep_a.head_onsets[1],
            sheep_a.head_onsets[2],sheep_a.panic_tick,sheep_a.panic_step,
            oracle_sheep_panic_first_step());
    CHECK(sheep_a.nhead_onsets>=1,
          "idle sheep produces look-idle yaw events");
    CHECK(sheep_a.panic_tick>=0,
          "hurt sheep starts a panic dest and moves");

    /* EntityAIEatGrass is OUT on the generic (det_entity_rng off) path:
     * shearing/wool-regrow needs the A* scheduler (GPU_MOB_AI.md). */
    if(!init_flat(&r))return 1;
    int grazer=gm_mobs_spawn(&r.mobs,GM_MOB_SHEEP,8.5,5.0,10.5);
    CHECK(grazer>=0,"spawn sheep");
    r.mobs.passive_sheared[grazer]=1;
    gm_runtime_tick(&r,idle);
    CHECK(r.mobs.passive_sheared[grazer]==1,
          "generic path does not run EntityAIEatGrass");
    gm_runtime_destroy(&r);

    if(!init_flat(&r))return 1;
    CHECK(gm_mobs_spawn(&r.mobs,GM_MOB_SHEEP,8.5,5.0,10.5)>=0,"spawn loot sheep");
    CHECK(gm_mobs_damage_near(&r.mobs,8.5,5.5,10.5,1.0,100.0f,&r.entities),
          "component lethal hit reaches sheep");
    int wool=0,mutton=0;for(int i=0;i<GM_LIVE_MAX;++i)if(r.entities.ents[i].active){
        wool|=r.entities.ents[i].item==35;mutton|=r.entities.ents[i].item==423;
    }
    for(int i=0;i<200&&r.mobs.xp_total<1;++i)gm_runtime_tick(&r,idle);
    CHECK(wool&&mutton&&r.mobs.xp_total>=1&&r.mobs.xp_total<=3,
          "sheep death creates wool, mutton, and 1+nextInt(3) XP");
    gm_runtime_destroy(&r);

    /* (a) hostile beyond follow range ignores the player and wanders. */
    if(!init_flat(&r))return 1;
    CHECK(gm_mobs_spawn(&r.mobs,EW_TYPE_CREEPER,8.5,5.0,45.5)>=0,"spawn far creeper");
    double cw_x0=8.5,cw_z0=45.5;
    double moved=0.0,mind=37.0;int seen=0;
    for(int i=0;i<300;++i){
        gm_runtime_set_pose(&r,8.5,5.0,8.5,0.0f,10.0f);
        gm_runtime_tick(&r,idle);
        n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
        seen=0;
        for(int k=0;k<n;++k)if(v[k].type==EW_TYPE_CREEPER){
            seen=1;
            double mx=v[k].x-cw_x0,mz=v[k].z-cw_z0;
            double dd=sqrt(mx*mx+mz*mz);if(dd>moved)moved=dd;
            double dp=sqrt((v[k].x-8.5)*(v[k].x-8.5)+(v[k].z-8.5)*(v[k].z-8.5));
            if(dp<mind)mind=dp;
        }
        if(!seen)break;
    }
    fprintf(stderr,"mob_live: wander moved %.3f mind %.3f\n",moved,mind);
    CHECK(seen,"out-of-range hostile neither despawns nor explodes in 300 ticks");
    CHECK(moved>1.0,"out-of-range hostile wanders (position changes)");
    CHECK(mind>16.0,"out-of-range hostile never approaches within follow range");
    gm_runtime_destroy(&r);

    /* (c) zombie under open daytime sky burns and dies with drops. */
    if(!init_flat(&r))return 1;
    CHECK(gm_mobs_spawn(&r.mobs,EW_TYPE_ZOMBIE,8.5,5.0,10.5)>=0,"spawn daylight zombie");
    int burned_dead=0;
    for(int i=0;i<900;++i){
        gm_runtime_set_pose(&r,8.5,30.0,8.5,0.0f,10.0f); /* out of melee, in range */
        gm_runtime_tick(&r,idle);
        if(gm_mobs_alive(&r.mobs)==0){burned_dead=1;break;}
    }
    CHECK(burned_dead,"surface zombie burns to death in daytime");
    int burn_flesh=0;
    for(int i=0;i<GM_LIVE_MAX;++i)if(r.entities.ents[i].active&&r.entities.ents[i].item==367)burn_flesh=1;
    CHECK(burn_flesh,"daylight burn death still drops loot");
    gm_runtime_destroy(&r);

    /* Recorder flags bit 0 follows generic Entity.isBurning for living mobs. */
    if(!init_flat(&r))return 1;
    int burning_zombie=gm_mobs_spawn(&r.mobs,EW_TYPE_ZOMBIE,8.5,5.0,10.5);
    CHECK(burning_zombie>=0,"spawn daylight-burning view zombie");
    gm_runtime_tick(&r,idle);
    n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
    int burning_view=0;
    for(int k=0;k<n;++k)
        if(v[k].type==EW_TYPE_ZOMBIE&&(v[k].flags&1))burning_view=1;
    CHECK(r.mobs.fire_ticks[burning_zombie]>0&&burning_view,
          "generic live fire_ticks populate recorder-compatible flags bit 0");
    gm_runtime_destroy(&r);

    /* (d) zombie under a stone roof does NOT burn in daytime. */
    if(!init_flat(&r))return 1;
    for(int x=-12;x<=30;++x)for(int z=-12;z<=30;++z)gm_world_set_block(r.world,x,9,z,1);
    CHECK(gm_mobs_spawn(&r.mobs,EW_TYPE_ZOMBIE,8.5,5.0,10.5)>=0,"spawn roofed zombie");
    for(int i=0;i<900;++i){
        gm_runtime_set_pose(&r,8.5,30.0,8.5,0.0f,10.0f);
        gm_runtime_tick(&r,idle);
    }
    CHECK(gm_mobs_alive(&r.mobs)==1,"roofed zombie survives the whole day loop");
    n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
    int zi=-1;for(int k=0;k<n;++k)if(v[k].type==EW_TYPE_ZOMBIE)zi=k;
    CHECK(zi>=0&&v[zi].health>=19.0f,"roofed zombie takes no burn damage");
    gm_runtime_destroy(&r);

    /* (e) hostile beyond 128 blocks despawns instantly. */
    if(!init_flat(&r))return 1;
    CHECK(gm_mobs_spawn(&r.mobs,EW_TYPE_ZOMBIE,8.5,5.0,10.5)>=0,"spawn despawn zombie");
    CHECK(gm_mobs_alive(&r.mobs)==1,"despawn zombie starts alive");
    for(int i=0;i<3;++i){
        gm_runtime_set_pose(&r,8.5,5.0,160.5,0.0f,10.0f);
        gm_runtime_tick(&r,idle);
    }
    CHECK(gm_mobs_alive(&r.mobs)==0,"hostile beyond 128 blocks despawns");
    gm_runtime_destroy(&r);

    if(!init_flat(&r))return 1;
    {
        int nspawn=0;
        gm_runtime_set_time(&r,18000);
        for(int i=0;i<50;++i){
            gm_runtime_set_pose(&r,8.5,5.0,8.5,0.0f,10.0f);
            gm_runtime_tick(&r,idle);
        }
        nspawn=gm_mobs_alive(&r.mobs);
        CHECK(nspawn==0,"natural_spawn default off plants nothing over night ticks");
        gm_runtime_destroy(&r);
    }

    if(!init_flat(&r))return 1;
    r.natural_spawn = 1;
    r.mobs.natural_spawn = 1;
    gm_runtime_set_time(&r,18000);
    for(int i=0;i<400&&!gm_mobs_alive(&r.mobs);++i){
        gm_runtime_set_pose(&r,8.5,5.0,8.5,0.0f,10.0f);
        gm_runtime_tick(&r,idle);
    }
    CHECK(gm_mobs_alive(&r.mobs)>0,"night cycle naturally spawns a light-gated hostile");
    gm_runtime_destroy(&r);

    /* ---- New roster types ---- */

    /* Pigman: neutral until hurt, then group anger. */
    if(!init_flat(&r))return 1;
    r.vitals.foodLevel=0;r.vitals.saturation=0.0f;
    CHECK(gm_mobs_spawn(&r.mobs,GM_MOB_PIGMAN,8.5,5.0,14.5)>=0,"spawn pigman A");
    CHECK(gm_mobs_spawn(&r.mobs,GM_MOB_PIGMAN,10.5,5.0,14.5)>=0,"spawn pigman B");
    for(int i=0;i<10;++i)gm_runtime_tick(&r,idle);
    CHECK(r.vitals.health==20.0f,"neutral pigmen do not attack");
    isr_set_stack(&r.player.inv,0,ic_mk(276,1,0));
    GmAction hit;memset(&hit,0,sizeof hit);hit.attack=1;hit.hotbar_sel=0;
    /* After idle the pigmen rest on the superflat top at y=4; pitch 10 aims
     * over their hitbox and is a dig MISS that arms leftClickCounter. Use a
     * pitch that actually intersects the living AABB so clickMouse ENTITY runs. */
    gm_runtime_set_pose(&r,8.5,5.0,12.5,0.0f,20.0f);
    for(int i=0;i<5;++i)gm_runtime_tick(&r,hit);
    CHECK(r.mobs.anger[1]>0||r.mobs.anger[2]>0,"hurt pigman becomes angry");
    int both_angry=(r.mobs.anger[1]>0)+(r.mobs.anger[2]>0);
    CHECK(both_angry>=2,"nearby pigman group-angers");
    gm_runtime_set_pose(&r,8.5,5.0,10.5,0.0f,10.0f);
    float hp0=r.vitals.health;
    for(int i=0;i<40;++i)gm_runtime_tick(&r,idle);
    CHECK(r.vitals.health<hp0,"angry pigman melee damages player");
    gm_runtime_destroy(&r);

    /* Ghast: flight + fireball pending. */
    if(!init_flat(&r))return 1;
    CHECK(gm_mobs_spawn(&r.mobs,GM_MOB_GHAST,8.5,10.0,20.5)>=0,"spawn ghast");
    double gy0=0;{GmEntityView vv[EW_MAX_ENTITIES];int nn=gm_mobs_fill_views(&r.mobs,vv,EW_MAX_ENTITIES);
        for(int k=0;k<nn;++k)if(vv[k].type==GM_MOB_GHAST)gy0=vv[k].y;}
    int saw_fireball=0,rendered_fireball=0;
    for(int i=0;i<60;++i){
        gm_runtime_tick(&r,idle);
        for(int k=0;k<GM_RUNTIME_PROJECTILES;++k)
            if(r.projectiles[k].active&&r.projectiles[k].type==5){
                GmEntityView shots[GM_RUNTIME_PROJECTILES];
                int sn=gm_runtime_projectile_views(&r,shots,GM_RUNTIME_PROJECTILES);
                saw_fireball=1;
                for(int q=0;q<sn;++q)
                    if(shots[q].type==GM_VIEW_BILLBOARD&&shots[q].item_id==385)
                        rendered_fireball=1;
            }
    }
    {GmEntityView vv[EW_MAX_ENTITIES];int nn=gm_mobs_fill_views(&r.mobs,vv,EW_MAX_ENTITIES);
        int found=0;for(int k=0;k<nn;++k)if(vv[k].type==GM_MOB_GHAST){
            found=1;CHECK(vv[k].y>4.0f,"ghast remains airborne");}
        CHECK(found,"ghast remains in live entity store after flight ticks");
    }
    CHECK(gm_mobs_alive(&r.mobs)==1,"ghast survives flight ticks");
    CHECK(saw_fireball,"ghast charge produces a live large-fireball projectile");
    CHECK(rendered_fireball,"ghast large fireball uses the live fire-charge render path");
    (void)gy0;
    gm_runtime_destroy(&r);

    /* A saturated runtime projectile pool must backpressure, not discard, a
     * pending ghast shot. fireball_pending stores type (5=large). */
    if(!init_flat(&r))return 1;
    r.mobs.fireball_pending=5;
    r.mobs.fireball_x=8.5;r.mobs.fireball_y=8.0;r.mobs.fireball_z=12.5;
    r.mobs.fireball_vz=-0.5;
    for(int k=0;k<GM_RUNTIME_PROJECTILES;++k){
        r.projectiles[k].active=1;r.projectiles[k].type=4;
        r.projectiles[k].x=1000.0+k;r.projectiles[k].y=100.0;
        r.projectiles[k].z=1000.0;r.projectiles[k].vz=0.1;
    }
    gm_runtime_tick(&r,idle);
    CHECK(r.mobs.fireball_pending,"full projectile pool retains pending ghast fireball");
    r.projectiles[0].active=0;
    gm_runtime_tick(&r,idle);
    CHECK(!r.mobs.fireball_pending&&r.projectiles[0].active&&r.projectiles[0].type==5,
          "pending ghast fireball spawns once a projectile slot is free");
    gm_runtime_destroy(&r);

    /* Magma cube: unlike slime, every size attacks for size + 2. */
    if(!init_flat(&r))return 1;
    r.vitals.foodLevel=0;r.vitals.saturation=0.0f;
    CHECK(gm_mobs_spawn_sized(&r.mobs,GM_MOB_MAGMA,8.5,5.0,9.5,1)>=0,
          "spawn size-1 damage-model magma");
    gm_runtime_tick(&r,idle);
    CHECK(r.vitals.health==17.0f,"size-1 magma melee deals size + 2 damage");
    gm_runtime_destroy(&r);

    /* Magma cube: size, jump, split on death. */
    if(!init_flat(&r))return 1;
    CHECK(gm_mobs_spawn_sized(&r.mobs,GM_MOB_MAGMA,8.5,5.0,10.5,2)>=0,"spawn size-2 magma");
    CHECK(r.mobs.size[1]==2,"magma size stored");
    /* Deterministic death via damage_near (player melee reach is flaky on hoppers). */
    CHECK(gm_mobs_damage_near(&r.mobs,8.5,5.5,10.5,2.0,100.0f,&r.entities),
          "magma takes lethal damage");
    int smalls=0;
    {const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
        for(int i=1;i<EW_MAX_ENTITIES;++i)
            if(s->alive[i]&&s->type[i]==GM_MOB_MAGMA&&r.mobs.size[i]==1)++smalls;}
    CHECK(smalls>=2,"magma size-2 death splits into two size-1 cubes");
    gm_runtime_destroy(&r);

    /* Slime: size-1 drop slime ball. EntityLiving.dropFewItems nextInt(3). */
    if(!init_flat(&r))return 1;
    {
        int slot=gm_mobs_spawn_sized(&r.mobs,GM_MOB_SLIME,8.5,5.0,10.5,1);
        CHECK(slot>=0,"spawn size-1 slime");
        r.mobs.ent_jr_seed[slot]=1; /* nextInt(3) > 0 at this cursor */
    }
    CHECK(gm_mobs_damage_near(&r.mobs,8.5,5.5,10.5,2.0,100.0f,&r.entities),
          "slime takes lethal damage");
    int ball=0;for(int i=0;i<GM_LIVE_MAX;++i)
        if(r.entities.ents[i].active&&r.entities.ents[i].item==341)ball=1;
    CHECK(gm_mobs_alive(&r.mobs)==0&&ball,"size-1 slime drops slime ball");
    gm_runtime_destroy(&r);

    /* Silverfish: melee + spawner entity id. */
    if(!init_flat(&r))return 1;
    r.vitals.foodLevel=0;r.vitals.saturation=0.0f;
    CHECK(gm_mobs_spawn(&r.mobs,GM_MOB_SILVERFISH,8.5,5.0,10.5)>=0,"spawn silverfish");
    gm_runtime_tick(&r,idle);
    CHECK(r.vitals.health==19.0f,"silverfish melee deals 1 damage");
    gm_runtime_destroy(&r);

    if(!init_flat(&r))return 1;
    gm_world_set_block(r.world,12,4,12,52);
    CHECK(gm_mobs_register_spawner(&r.mobs,12,4,12,GM_MOB_SILVERFISH)>=0,
          "register silverfish spawner");
    CHECK(r.mobs.spawners[0].entity_type==GM_MOB_SILVERFISH,
          "spawner stores silverfish entity id not blaze");
    r.mobs.spawners[0].delay=0;
    gm_runtime_set_pose(&r,12.5,5.0,12.5,0.0f,10.0f);
    for(int i=0;i<5;++i)gm_runtime_tick(&r,idle);
    int sf=0;{const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
        for(int i=1;i<EW_MAX_ENTITIES;++i)
            if(s->alive[i]&&s->type[i]==GM_MOB_SILVERFISH)sf=1;}
    CHECK(sf,"silverfish spawner produces silverfish from stored entity id");
    gm_runtime_destroy(&r);

    /* Blaze spawner still works (not any-block-52=blaze for overworld). */
    if(!init_flat(&r))return 1;
    gm_world_set_block(r.world,14,4,14,52);
    CHECK(gm_mobs_register_spawner(&r.mobs,14,4,14,GM_MOB_BLAZE)>=0,"register blaze spawner");
    r.mobs.spawners[0].delay=0;
    gm_runtime_set_pose(&r,14.5,5.0,14.5,0.0f,10.0f);
    for(int i=0;i<5;++i)gm_runtime_tick(&r,idle);
    int bl=0;{const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
        for(int i=1;i<EW_MAX_ENTITIES;++i)
            if(s->alive[i]&&s->type[i]==GM_MOB_BLAZE)bl=1;}
    CHECK(bl,"blaze spawner still selects blaze from stored entity id");
    gm_runtime_destroy(&r);

    /* Boat: place, mount, no-autothrust, controlBoat thrust, water status,
     * deltaRotation turn, land AABB, break drop. */
    if(!init_flat(&r))return 1;
    CHECK(gm_mobs_place_boat(&r.mobs,8.5,5.0,8.5,0.0f)>=0,"place boat");
    gm_runtime_set_pose(&r,8.5,5.0,8.5,0.0f,10.0f);
    CHECK(gm_mobs_boat_mount(&r.mobs,(struct PsvPlayer *)&r.player,r.ox,r.oz),
          "mount boat");
    CHECK(gm_mobs_boat_riding(&r.mobs),"boat riding flag");
    /* Idle: no forced forward thrust while mounted. */
    {
        double bx0=0,bz0=0;
        {const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
         for(int i=1;i<EW_MAX_ENTITIES;++i)
             if(s->alive[i]&&s->type[i]==GM_ENTITY_BOAT){bx0=s->x[i];bz0=s->z[i];}}
        for(int i=0;i<20;++i)gm_runtime_tick(&r,idle);
        double bx1=0,bz1=0;
        {const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
         for(int i=1;i<EW_MAX_ENTITIES;++i)
             if(s->alive[i]&&s->type[i]==GM_ENTITY_BOAT){bx1=s->x[i];bz1=s->z[i];}}
        double drift=sqrt((bx1-bx0)*(bx1-bx0)+(bz1-bz0)*(bz1-bz0));
        CHECK(drift<0.15,"mounted boat without forward input does not auto-thrust");
    }
    /* Forward input moves the boat along look yaw (controlBoat f+=0.04). */
    {
        double bx0=0,bz0=0;
        {const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
         for(int i=1;i<EW_MAX_ENTITIES;++i)
             if(s->alive[i]&&s->type[i]==GM_ENTITY_BOAT){bx0=s->x[i];bz0=s->z[i];}}
        GmAction fwd;memset(&fwd,0,sizeof fwd);fwd.forward=1.0f;fwd.hotbar_sel=-1;
        for(int i=0;i<30;++i)gm_runtime_tick(&r,fwd);
        double bx1=0,bz1=0;
        {const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
         for(int i=1;i<EW_MAX_ENTITIES;++i)
             if(s->alive[i]&&s->type[i]==GM_ENTITY_BOAT){bx1=s->x[i];bz1=s->z[i];}}
        double moved=sqrt((bx1-bx0)*(bx1-bx0)+(bz1-bz0)*(bz1-bz0));
        fprintf(stderr,"mob_live: boat land forward travel %.4f\n",moved);
        CHECK(moved>0.3,"mounted boat with forward=1 travels under controlBoat thrust");
    }
    /* Turn-only: left input accumulates deltaRotation (oracle: +/-1 per tick). */
    {
        float yaw0=0;
        {const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
         for(int i=1;i<EW_MAX_ENTITIES;++i)
             if(s->alive[i]&&s->type[i]==GM_ENTITY_BOAT)yaw0=s->yaw[i];}
        GmAction left;memset(&left,0,sizeof left);left.strafe=-1.0f;left.hotbar_sel=-1;
        for(int i=0;i<10;++i)gm_runtime_tick(&r,left);
        float yaw1=yaw0;
        {const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
         for(int i=1;i<EW_MAX_ENTITIES;++i)
             if(s->alive[i]&&s->type[i]==GM_ENTITY_BOAT)yaw1=s->yaw[i];}
        fprintf(stderr,"mob_live: boat deltaRotation yaw %.3f -> %.3f\n",yaw0,yaw1);
        CHECK(yaw1 < yaw0 - 5.0f,
              "left input applies controlBoat deltaRotation (yaw decreases)");
    }
    gm_mobs_boat_dismount(&r.mobs,(struct PsvPlayer *)&r.player,r.ox,r.oz);
    CHECK(!gm_mobs_boat_riding(&r.mobs),"dismount clears ride");
    /* Break: stand on the boat and hit until hull drops the item. */
    {
        GmEntityView vv[EW_MAX_ENTITIES];
        int nn=gm_mobs_fill_views(&r.mobs,vv,EW_MAX_ENTITIES),bi=-1;
        for(int k=0;k<nn;++k)if(vv[k].type==GM_ENTITY_BOAT)bi=k;
        CHECK(bi>=0,"boat still present after dismount");
        gm_runtime_set_pose(&r,vv[bi].x,vv[bi].y+1.0,vv[bi].z-1.0,0.0f,30.0f);
    }
    isr_set_stack(&r.player.inv,0,ic_mk(276,1,0));
    GmAction boat_hit;memset(&boat_hit,0,sizeof boat_hit);
    boat_hit.attack=1;boat_hit.hotbar_sel=0;
    for(int i=0;i<60;++i){
        GmEntityView vv[EW_MAX_ENTITIES];
        int nn=gm_mobs_fill_views(&r.mobs,vv,EW_MAX_ENTITIES),bi=-1;
        for(int k=0;k<nn;++k)if(vv[k].type==GM_ENTITY_BOAT)bi=k;
        if(bi<0)break;
        gm_runtime_set_pose(&r,vv[bi].x,vv[bi].y+1.0,vv[bi].z-1.0,0.0f,30.0f);
        gm_runtime_tick(&r,boat_hit);
    }
    int boat_item=0;for(int i=0;i<GM_LIVE_MAX;++i)
        if(r.entities.ents[i].active&&r.entities.ents[i].item==333)boat_item=1;
    int boat_alive=0;{const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
        for(int i=1;i<EW_MAX_ENTITIES;++i)
            if(s->alive[i]&&s->type[i]==GM_ENTITY_BOAT)boat_alive=1;}
    CHECK(boat_item&&!boat_alive,"broken boat drops boat item");
    gm_runtime_destroy(&r);

    /* Water boat: IN_WATER momentum 0.9, controlBoat forward, no autothrust. */
    if(!init_flat(&r))return 1;
    /* Large water basin so 20-tick thrust stays inside the pool. */
    for(int x=0;x<=24;++x)for(int z=0;z<=24;++z){
        gm_world_set_block(r.world,x,4,z,1); /* floor */
        gm_world_set_block(r.world,x,5,z,9); /* source water */
        gm_world_set_block(r.world,x,6,z,0);
    }
    CHECK(gm_mobs_place_boat(&r.mobs,12.5,5.2,12.5,0.0f)>=0,"place water boat");
    gm_runtime_set_pose(&r,12.5,5.6,12.5,0.0f,10.0f);
    CHECK(gm_mobs_boat_mount(&r.mobs,(struct PsvPlayer *)&r.player,r.ox,r.oz),
          "mount water boat");
    {
        double bx0=0,bz0=0;
        {const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
         for(int i=1;i<EW_MAX_ENTITIES;++i)
             if(s->alive[i]&&s->type[i]==GM_ENTITY_BOAT){bx0=s->x[i];bz0=s->z[i];}}
        for(int i=0;i<20;++i)gm_runtime_tick(&r,idle);
        double bx1=0,bz1=0,by1=0;
        {const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
         for(int i=1;i<EW_MAX_ENTITIES;++i)
             if(s->alive[i]&&s->type[i]==GM_ENTITY_BOAT){
                 bx1=s->x[i];by1=s->y[i];bz1=s->z[i];}}
        double drift=sqrt((bx1-bx0)*(bx1-bx0)+(bz1-bz0)*(bz1-bz0));
        CHECK(drift<0.2,"water boat without input does not auto-thrust");
        CHECK(by1>4.5&&by1<7.0,"idle water boat stays near surface (IN_WATER buoyancy)");
    }
    {
        double bx0=0,bz0=0,by0=0;
        {const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
         for(int i=1;i<EW_MAX_ENTITIES;++i)
             if(s->alive[i]&&s->type[i]==GM_ENTITY_BOAT){
                 bx0=s->x[i];by0=s->y[i];bz0=s->z[i];}}
        GmAction fwd;memset(&fwd,0,sizeof fwd);fwd.forward=1.0f;fwd.hotbar_sel=-1;
        for(int i=0;i<20;++i)gm_runtime_tick(&r,fwd);
        double bx1=0,bz1=0,by1=0;
        {const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
         for(int i=1;i<EW_MAX_ENTITIES;++i)
             if(s->alive[i]&&s->type[i]==GM_ENTITY_BOAT){
                 bx1=s->x[i];by1=s->y[i];bz1=s->z[i];}}
        double moved=sqrt((bx1-bx0)*(bx1-bx0)+(bz1-bz0)*(bz1-bz0));
        fprintf(stderr,"mob_live: water boat forward travel %.4f y %.3f->%.3f\n",
                moved,by0,by1);
        /* Oracle-valued: 20 ticks of 0.04 thrust with 0.9 water momentum is
         * well above 0.3 block horizontal travel; boat stays in the water body. */
        CHECK(moved>0.3,"water boat controlBoat forward travels under 0.9 momentum");
        CHECK(by1>4.5&&by1<7.0,"water boat remains near water surface (buoyancy)");
    }
    gm_runtime_destroy(&r);

    /* Entity ownership is dimension-scoped even for authoritative scripted
     * transfers that do not rebuild the live store. */
    if(!init_flat(&r))return 1;
    CHECK(gm_mobs_spawn(&r.mobs,EW_TYPE_ZOMBIE,8.5,5.0,10.5)>=0,
          "spawn Overworld dimension-owned zombie");
    r.mobs.active_dimension=-1;
    n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
    CHECK(n==0,"Nether view excludes Overworld-owned entities");
    CHECK(gm_mobs_spawn(&r.mobs,GM_MOB_BLAZE,8.5,5.0,10.5)>=0,
          "spawn Nether dimension-owned blaze");
    n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
    CHECK(n==1&&v[0].type==GM_MOB_BLAZE,"Nether view contains only Nether entities");
    r.mobs.active_dimension=0;
    n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
    CHECK(n==1&&v[0].type==EW_TYPE_ZOMBIE,
          "returning Overworld view restores only Overworld entities");
    gm_runtime_destroy(&r);

    /* Wither skeleton: already covered for damage; natural type ok. */
    if(!init_flat(&r))return 1;
    CHECK(gm_mobs_spawn(&r.mobs,GM_MOB_WITHER_SKELETON,8.5,5.0,10.5)>=0,
          "spawn wither skeleton type");
    CHECK(gm_mobs_alive(&r.mobs)==1,"wither skeleton lives");
    gm_runtime_destroy(&r);

    /* Low-profile mobs remain hittable through the runtime attack ray. */
    if(!init_flat(&r))return 1;
    CHECK(gm_mobs_spawn(&r.mobs,EW_TYPE_SPIDER,8.5,5.0,10.5)>=0,
          "spawn low-profile spider");
    gm_runtime_set_pose(&r,8.5,6.0,10.5,0.0f,90.0f);
    isr_set_stack(&r.player.inv,0,ic_mk(276,1,0));
    {GmAction a;memset(&a,0,sizeof a);a.attack=1;a.hotbar_sel=0;gm_runtime_tick(&r,a);}
    {const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
     CHECK(s->health[1]<20.0f,"runtime attack ray hits low-profile spider");}
    gm_runtime_destroy(&r);

    /* Capacity > 7: spawn 12 zombies. */
    if(!init_flat(&r))return 1;
    int spawned=0;
    for(int i=0;i<12;++i){
        if(gm_mobs_spawn(&r.mobs,EW_TYPE_ZOMBIE,8.5+i*0.01,5.0,14.5+i*0.5)>=0)
            ++spawned;
    }
    CHECK(spawned==12,"capacity allows more than 7 living entities");
    CHECK(gm_mobs_living_count(&r.mobs)==12,"living_count reports 12");
    CHECK(GM_MOB_CAPACITY>7,"product capacity constant exceeds legacy 7");
    gm_runtime_destroy(&r);

    /* ---- Mobs-on autonomy: live spawn/AI/combat cadences (Java-derived) ----
     * Not ent_view ghosts and not injected vitals. Pose placement only. */

    /* Zombie melee: EntityZombie ATTACK_DAMAGE=3.0; EntityLiving.attackTime
     * cooldown 20 after a hit (MAZ_ATTACK_COOLDOWN / EW path in magma). */
    if(!init_flat(&r))return 1;
    r.vitals.foodLevel=0;r.vitals.saturation=0.0f;r.player.food=0;
    CHECK(gm_mobs_spawn(&r.mobs,EW_TYPE_ZOMBIE,8.5,5.0,10.5)>=0,
          "autonomy: spawn melee zombie");
    float zhp0=r.vitals.health;
    gm_runtime_tick(&r,idle);
    CHECK(r.vitals.health==zhp0-3.0f,"autonomy: zombie first melee is exactly 3 hp");
    float zhp1=r.vitals.health;
    for(int i=0;i<19;++i)gm_runtime_tick(&r,idle);
    CHECK(r.vitals.health==zhp1,
          "autonomy: zombie melee silent through attack_time 19 of 20");
    gm_runtime_tick(&r,idle);
    CHECK(r.vitals.health==zhp1-3.0f,
          "autonomy: zombie second melee lands on the 20-tick cadence");
    gm_runtime_destroy(&r);

    /* Enderman acquisition: no look-trigger in this sim; revenge sets
     * hurt_aggro, after which follow_range (16) chase engages. */
    if(!init_flat(&r))return 1;
    CHECK(gm_mobs_spawn(&r.mobs,EW_TYPE_ENDERMAN,8.5,5.0,18.5)>=0,
          "autonomy: spawn enderman");
    double ez0=18.5;
    for(int i=0;i<30;++i){
        gm_runtime_set_pose(&r,8.5,5.0,8.5,0.0f,10.0f);
        gm_runtime_tick(&r,idle);
    }
    n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
    int ei=-1;for(int k=0;k<n;++k)if(v[k].type==EW_TYPE_ENDERMAN)ei=k;
    CHECK(ei>=0&&fabs(v[ei].z-ez0)<1.5,
          "autonomy: unhurt enderman does not acquire the player");
    isr_set_stack(&r.player.inv,0,ic_mk(268,1,0));
    GmAction poke;memset(&poke,0,sizeof poke);poke.attack=1;poke.hotbar_sel=0;
    gm_runtime_set_pose(&r,8.5,5.0,16.5,0.0f,10.0f);
    gm_runtime_tick(&r,poke);
    double ez1=0.0;int acquired=0;
    for(int i=0;i<80;++i){
        gm_runtime_set_pose(&r,8.5,5.0,8.5,0.0f,10.0f);
        gm_runtime_tick(&r,idle);
        n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
        ei=-1;for(int k=0;k<n;++k)if(v[k].type==EW_TYPE_ENDERMAN)ei=k;
        if(ei<0)break;
        ez1=v[ei].z;
        if(ez1<ez0-1.0){acquired=1;break;}
    }
    fprintf(stderr,"mob_live: enderman acquisition z %.3f -> %.3f\n",ez0,ez1);
    CHECK(acquired,"autonomy: hurt enderman acquires and chases the player");
    gm_runtime_destroy(&r);

    /* Blaze burst: AIFireballAttack charge 60 then 3-shot volley (type-3). */
    if(!init_flat(&r))return 1;
    r.dimension=-1;r.mobs.active_dimension=-1;
    CHECK(gm_mobs_spawn(&r.mobs,GM_MOB_BLAZE,8.5,5.0,14.5)>=0,"autonomy: spawn blaze");
    int fireballs=0,first_age=-1;
    for(int i=0;i<120;++i){
        gm_runtime_set_pose(&r,8.5,5.0,8.5,0.0f,10.0f);
        gm_runtime_tick(&r,idle);
        for(int p=0;p<GM_RUNTIME_PROJECTILES;++p){
            if(r.projectiles[p].active&&r.projectiles[p].type==3){
                if(r.projectiles[p].age==0||r.projectiles[p].age==1){
                    if(first_age<0)first_age=i;
                    ++fireballs;
                }
            }
        }
    }
    fprintf(stderr,"mob_live: blaze fireballs=%d first_tick=%d\n",fireballs,first_age);
    CHECK(fireballs>=1,"autonomy: blaze fires at least one live fireball");
    CHECK(first_age>=0&&first_age<=70,
          "autonomy: first blaze shot is within the 60-tick charge window");
    gm_runtime_destroy(&r);

    /* Skeleton: type-specific keep-away + ranged (not shared melee stand). */
    if(!init_flat(&r))return 1;
    r.vitals.foodLevel=0;r.vitals.saturation=0.0f;
    CHECK(gm_mobs_spawn(&r.mobs,EW_TYPE_SKELETON,8.5,5.0,10.5)>=0,
          "type AI: spawn skeleton");
    int sk_arrows=0;double sk_z_max=10.5;
    for(int i=0;i<100;++i){
        gm_runtime_set_pose(&r,8.5,5.0,8.5,0.0f,10.0f);
        gm_runtime_tick(&r,idle);
        for(int p=0;p<GM_RUNTIME_PROJECTILES;++p)
            if(r.projectiles[p].active&&r.projectiles[p].type==2&&
               (r.projectiles[p].age==0||r.projectiles[p].age==1))
                ++sk_arrows;
        n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
        for(int k=0;k<n;++k)
            if(v[k].type==EW_TYPE_SKELETON&&v[k].z>sk_z_max)sk_z_max=v[k].z;
    }
    CHECK(sk_arrows>=1,"type AI: skeleton fires arrows on ranged cadence");
    /* Close skeleton backs off along -player direction (z increases here). */
    CHECK(sk_z_max>11.5,
          "type AI: close skeleton keep-away navigates outward (not zombie melee stand)");
    gm_runtime_destroy(&r);

    /* WorldEntitySpawner biome list still draws enderman; roster insert is
     * zombie/skeleton/creeper only so the live table never holds enderman. */
    if(!init_flat(&r))return 1;
    r.natural_spawn = 1;
    r.mobs.natural_spawn = 1;
    gm_runtime_set_time(&r,14000);
    int counts[32];memset(counts,0,sizeof counts);
    for(int trial=0;trial<40;++trial){
        /* Clear living hostiles between trials. */
        {EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
         for(int i=1;i<EW_MAX_ENTITIES;++i)
             if(s->alive[i]){s->alive[i]=0;s->type[i]=EW_TYPE_NONE;}}
        r.mobs.tick=20*(trial+1); /* natural_spawn gates on tick%20==0 */
        for(int i=0;i<25;++i){
            gm_runtime_set_pose(&r,8.5,5.0,8.5,0.0f,10.0f);
            gm_runtime_tick(&r,idle);
        }
        n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
        for(int k=0;k<n;++k){
            int t=v[k].type;
            if(t>=0&&t<32)counts[t]++;
        }
    }
    int common=counts[EW_TYPE_ZOMBIE]+counts[EW_TYPE_SKELETON]+
               counts[EW_TYPE_CREEPER]+counts[EW_TYPE_SPIDER];
    fprintf(stderr,"mob_live: natural spawn common=%d enderman=%d\n",
            common,counts[EW_TYPE_ENDERMAN]);
    CHECK(common>0,"type AI: night natural spawn produces common hostiles");
    CHECK(counts[EW_TYPE_ENDERMAN]<=common,
          "type AI: enderman weight is not above the common hostiles combined");
    gm_runtime_destroy(&r);

    /* Real spawner: natural_spawn Nether branch reads block id 52 and emits
     * blazes on a 200-tick cadence when under the alive_count cap. */
    if(!init_flat(&r))return 1;
    r.dimension=-1;r.mobs.active_dimension=-1;
    for(int x=6;x<=10;++x)for(int z=6;z<=10;++z){
        gm_world_set_block(r.world,x,4,z,1);
        gm_world_set_block(r.world,x,5,z,0);
        gm_world_set_block(r.world,x,6,z,0);
    }
    gm_world_set_block(r.world,8,5,8,52);
    gm_world_set_block(r.world,9,5,8,112);
    gm_runtime_set_pose(&r,8.5,5.0,8.5,0.0f,10.0f);
    int spawns=0;
    for(int i=0;i<450;++i){
        int before=gm_mobs_alive(&r.mobs);
        gm_runtime_tick(&r,idle);
        int after=gm_mobs_alive(&r.mobs);
        if(after>before){
            n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
            int blaze=0;for(int k=0;k<n;++k)if(v[k].type==GM_MOB_BLAZE)blaze=1;
            CHECK(blaze,"autonomy: spawner emits blaze entities");
            ++spawns;
        }
    }
    fprintf(stderr,"mob_live: spawner blaze spawns=%d\n",spawns);
    CHECK(spawns>=1,"autonomy: real blaze spawner produces live mobs");
    gm_runtime_destroy(&r);

    CHECK(blaze_schedule_receipt(NULL),
          "AIFireballAttack exact duty cycle and fireball cadence receipt");

    /* leftClickCounter gates entity clickMouse the same way as dig.
     * Survival press-miss arms 10; runtime must not damage a mob (or clear
     * the counter via action.attack=0) until the post-decrement value is 0.
     * Change look via player yaw/pitch only - gm_runtime_set_pose resets dig. */
    fprintf(stderr,"mob_live: leftClickCounter vs entity attack matrix\n");
    if(!init_flat(&r))return 1;
    r.vitals.foodLevel=0;r.vitals.saturation=0.0f;
    isr_set_stack(&r.player.inv,0,ic_mk(276,1,0));
    gm_runtime_set_pose(&r,8.5,5.0,8.5,0.0f,-89.0f);
    CHECK(gm_mobs_spawn(&r.mobs,EW_TYPE_ZOMBIE,8.5,5.0,10.5)>=0,
          "spawn leftClickCounter zombie");
    {
        GmAction miss;memset(&miss,0,sizeof miss);
        miss.attack=1;miss.hotbar_sel=0;
        /* Look straight up: ray miss arms counter=10. */
        r.player.yaw=0.0f;r.player.pitch=-89.0f;
        gm_runtime_tick(&r,miss);
        {
            GmPlayerCtlSnap snap;gm_player_ctl_dig_export(&snap);
            CHECK(snap.left_click_counter==10,
                  "runtime press-miss arms leftClickCounter to 10");
        }
        /* Hold attack while looking at the zombie: freeze damage + keep counter. */
        r.player.yaw=0.0f;r.player.pitch=10.0f;
        for(int t=0;t<9;++t){
            gm_runtime_tick(&r,miss);
            n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
            int zi=-1;for(int k=0;k<n;++k)if(v[k].type==EW_TYPE_ZOMBIE)zi=k;
            CHECK(zi>=0&&v[zi].health>=19.9f,
                  "entity attack freezes while leftClickCounter is positive");
        }
        {
            GmPlayerCtlSnap snap;gm_player_ctl_dig_export(&snap);
            CHECK(snap.left_click_counter==1,
                  "held entity aim does not clear leftClickCounter (physical bit)");
        }
        /* 10th post-arm hold: post-decrement hits 0, clickMouse can land. */
        gm_runtime_tick(&r,miss);
        n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
        int zi=-1;for(int k=0;k<n;++k)if(v[k].type==EW_TYPE_ZOMBIE)zi=k;
        CHECK(zi>=0&&v[zi].health<20.0f,
              "entity attack lands on the tick leftClickCounter reaches 0");
        {
            GmPlayerCtlSnap snap;gm_player_ctl_dig_export(&snap);
            CHECK(snap.left_click_counter==0,
                  "counter is 0 after the gated entity hit tick");
            CHECK(snap.dig_hitting==0,
                  "entity hit takes resetBlockRemoving dig path");
        }
    }
    gm_runtime_destroy(&r);

    /* Falling-block entity selection under a live counter must freeze dig, not
     * clear the counter as if the attack key released. */
    if(!init_flat(&r))return 1;
    {
        GmAction miss;memset(&miss,0,sizeof miss);miss.attack=1;miss.hotbar_sel=0;
        gm_runtime_set_pose(&r,8.5,5.0,8.5,0.0f,-89.0f);
        isr_set_stack(&r.player.inv,0,ic_mk(257,1,0));
        r.player.yaw=0.0f;r.player.pitch=-89.0f;
        gm_runtime_tick(&r,miss);
        /* Inject EntityFallingBlock on the look ray toward +Z. */
        {
            GmLiveEnt *e=NULL;
            for(int i=0;i<GM_LIVE_MAX;++i)if(!r.entities.ents[i].active){
                e=&r.entities.ents[i];break;
            }
            CHECK(e!=NULL,"falling-block slot available");
            if(e){
                memset(e,0,sizeof *e);
                e->active=1;e->type=2;
                e->x=8.5;e->y=6.0;e->z=10.5;
                e->item=12;e->lifespan=600;
                r.entities.n_active++;
            }
        }
        r.player.yaw=0.0f;r.player.pitch=25.0f;
        for(int t=0;t<5;++t)gm_runtime_tick(&r,miss);
        {
            GmPlayerCtlSnap snap;gm_player_ctl_dig_export(&snap);
            CHECK(snap.left_click_counter==5,
                  "falling-block aim preserves leftClickCounter while held");
            CHECK(snap.dig_hitting==0&&snap.dig_progress==0.0f,
                  "falling-block + counter freezes progressive dig");
        }
    }
    gm_runtime_destroy(&r);

    if(fail)return 1;
    fprintf(stderr,"mob_live: PASS\n");
    return 0;
}
