#include "game/runtime.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fail;
#define CHECK(C, M) do { if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail=1; } } while (0)

static int init_flat(GmRuntime *r) {
    GmConfig c; char err[256];
    gm_config_defaults(&c); c.world=GM_WORLD_SUPERFLAT; c.view_distance=1;
    if(!gm_runtime_init(r,&c,err,sizeof err)){fprintf(stderr,"init: %s\n",err);return 0;}
    gm_runtime_set_pose(r,8.5,5.0,8.5,0.0f,10.0f);
    return 1;
}

int main(void) {
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
    for(int i=0;i<35&&gm_mobs_alive(&r.mobs);++i)gm_runtime_tick(&r,attack);
    int rod=0;for(int i=0;i<GM_LIVE_MAX;++i)if(r.entities.ents[i].active&&r.entities.ents[i].item==369)rod=1;
    for(int i=0;i<200&&r.mobs.xp_total<10;++i)gm_runtime_tick(&r,idle);
    CHECK(gm_mobs_alive(&r.mobs)==0&&r.mobs.xp_total==10,"blaze XP entities reach the player");
    CHECK(rod,"blaze deterministic loot roll can produce a blaze rod");
    gm_runtime_destroy(&r);

    if(!init_flat(&r))return 1;
    isr_set_stack(&r.player.inv,0,ic_mk(276,1,0));
    CHECK(gm_mobs_spawn(&r.mobs,GM_MOB_SHEEP,8.5,5.0,10.5)>=0,"spawn component sheep");
    /* Sheep panic-flee when hurt now: chase it like kill_hook_mob does. */
    double sheep_z0=10.5,sheep_run=0.0;
    for(int i=0;i<80;++i){
        n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);int mi=-1;
        for(int k=0;k<n;++k)if(v[k].type==GM_MOB_SHEEP)mi=k;
        if(mi<0)break;
        if(v[mi].z-sheep_z0>sheep_run)sheep_run=v[mi].z-sheep_z0;
        gm_runtime_set_pose(&r,v[mi].x,v[mi].y,v[mi].z-2.0,0.0f,10.0f);
        gm_runtime_tick(&r,attack);
    }
    CHECK(sheep_run>0.5,"passive panics away from damage source when hurt");
    int wool=0,mutton=0;for(int i=0;i<GM_LIVE_MAX;++i)if(r.entities.ents[i].active){
        wool|=r.entities.ents[i].item==35;mutton|=r.entities.ents[i].item==423;
    }
    for(int i=0;i<200&&r.mobs.xp_total<1;++i)gm_runtime_tick(&r,idle);
    CHECK(wool&&mutton&&r.mobs.xp_total==1,"sheep death creates wool, food, and collectible XP entities");
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
    r.clock.world_time=13000;
    for(int i=0;i<200&&!gm_mobs_alive(&r.mobs);++i)gm_runtime_tick(&r,idle);
    CHECK(gm_mobs_alive(&r.mobs)>0,"night cycle naturally spawns a light-gated hostile");
    gm_runtime_destroy(&r);

    if(fail)return 1;
    fprintf(stderr,"mob_live: PASS\n");
    return 0;
}
