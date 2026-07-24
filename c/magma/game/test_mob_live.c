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
    gm_runtime_set_pose(&r,8.5,5.0,12.5,0.0f,10.0f);
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
    for(int i=0;i<60;++i)gm_runtime_tick(&r,idle);
    {GmEntityView vv[EW_MAX_ENTITIES];int nn=gm_mobs_fill_views(&r.mobs,vv,EW_MAX_ENTITIES);
        int found=0;for(int k=0;k<nn;++k)if(vv[k].type==GM_MOB_GHAST){
            found=1;CHECK(vv[k].y>4.0f,"ghast remains airborne");}}
    CHECK(gm_mobs_alive(&r.mobs)==1,"ghast survives flight ticks");
    (void)gy0;
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

    /* Slime: size-1 drop slime ball. */
    if(!init_flat(&r))return 1;
    CHECK(gm_mobs_spawn_sized(&r.mobs,GM_MOB_SLIME,8.5,5.0,10.5,1)>=0,"spawn size-1 slime");
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

    /* Boat: place, mount, move, break drop. */
    if(!init_flat(&r))return 1;
    CHECK(gm_mobs_place_boat(&r.mobs,8.5,5.0,8.5,0.0f)>=0,"place boat");
    gm_runtime_set_pose(&r,8.5,5.0,8.5,0.0f,10.0f);
    CHECK(gm_mobs_boat_mount(&r.mobs,(struct PsvPlayer *)&r.player,r.ox,r.oz),
          "mount boat");
    CHECK(gm_mobs_boat_riding(&r.mobs),"boat riding flag");
    for(int i=0;i<10;++i)gm_runtime_tick(&r,idle);
    gm_mobs_boat_dismount(&r.mobs,(struct PsvPlayer *)&r.player,r.ox,r.oz);
    CHECK(!gm_mobs_boat_riding(&r.mobs),"dismount clears ride");
    /* Break: stand on the boat and hit until hull drops the item. */
    {
        GmEntityView vv[EW_MAX_ENTITIES];
        int nn=gm_mobs_fill_views(&r.mobs,vv,EW_MAX_ENTITIES),bi=-1;
        for(int k=0;k<nn;++k)if(vv[k].type==GM_ENTITY_BOAT)bi=k;
        CHECK(bi>=0,"boat still present after dismount");
        /* Stand slightly back and look down into the hull. */
        gm_runtime_set_pose(&r,vv[bi].x,vv[bi].y+1.0,vv[bi].z-1.0,0.0f,30.0f);
    }
    isr_set_stack(&r.player.inv,0,ic_mk(276,1,0));
    for(int i=0;i<12;++i){
        gm_mobs_player_attack(&r.mobs,(const struct PsvPlayer *)&r.player,
                              r.ox,r.oz,&r.entities);
        r.mobs.player_attack_cooldown=0;
    }
    int boat_item=0;for(int i=0;i<GM_LIVE_MAX;++i)
        if(r.entities.ents[i].active&&r.entities.ents[i].item==333)boat_item=1;
    int boat_alive=0;{const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
        for(int i=1;i<EW_MAX_ENTITIES;++i)
            if(s->alive[i]&&s->type[i]==GM_ENTITY_BOAT)boat_alive=1;}
    CHECK(boat_item&&!boat_alive,"broken boat drops boat item");
    gm_runtime_destroy(&r);

    /* Wither skeleton: already covered for damage; natural type ok. */
    if(!init_flat(&r))return 1;
    CHECK(gm_mobs_spawn(&r.mobs,GM_MOB_WITHER_SKELETON,8.5,5.0,10.5)>=0,
          "spawn wither skeleton type");
    CHECK(gm_mobs_alive(&r.mobs)==1,"wither skeleton lives");
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

    if(fail)return 1;
    fprintf(stderr,"mob_live: PASS\n");
    return 0;
}
