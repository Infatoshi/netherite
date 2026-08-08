#include "game/runtime.h"
#include "game/portal_live.h"
#include "game/structures_live.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail;
#define CHECK(C, M) do { if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail = 1; } } while (0)

static const unsigned char empty_shulker_item_tag_nbt[] = {
    10,0,0,10,0,14,'B','l','o','c','k','E','n','t','i','t','y','T','a','g',
    0,0,
};

static uint64_t java_lcg_steps(uint64_t seed, int steps) {
    for (int i = 0; i < steps; ++i)
        seed = (seed * UINT64_C(0x5DEECE66D) + UINT64_C(0xB))
            & ((UINT64_C(1) << 48) - UINT64_C(1));
    return seed;
}

static float chicken_sound_pitch(uint64_t seed48) {
    JavaRandom random;
    float a, b;
    jrand_set_seed48(&random, seed48);
    (void)jrand_double(&random);
    a = jrand_float(&random);
    b = jrand_float(&random);
    return (a - b) * 0.2F + 1.0F;
}

static int pose_hits(GmRuntime *r,int tx,int ty,int tz,double sx,double sy,double sz,float yaw,float pitch){
    int hx,hy,hz,ax,ay,az;
    gm_runtime_set_pose(r,sx,sy,sz,yaw,pitch);
    gm_world_fill_window(r->world,r->ccx,r->ccz,(struct Chunk *)r->window);
    int hit=psv_raycast(r->window,&r->sin_table,&r->player,&hx,&hy,&hz,&ax,&ay,&az);
    return hit>=0 && hx+r->ox==tx && hy==ty && hz+r->oz==tz;
}

static int down_piston_pig_shape_probe_at_y(
        GmRuntime *r,GmAction idle,int block,int meta,
        int neighbor_mask,int neighbor_block,int neighbor_meta,
        double start_y,double x,double z,int eid,double out_y[2]){
    static const int dx[4]={0,1,0,-1};
    static const int dz[4]={-1,0,1,0};
    const EwStore *mobs;
    for(int y=78;y<=82;++y)
        for(int zz=18;zz<=22;++zz)
            for(int xx=18;xx<=22;++xx)
                gm_world_set_block_meta(r->world,xx,y,zz,0,0);
    gm_mobs_init(&r->mobs,0);
    r->mobs_enabled=0;
    r->piston_count=1;
    memset(r->pistons,0,sizeof r->pistons);
    r->pistons[0]=(GmRuntimePiston){
        .active=1,.dimension=r->dimension,
        .x=20,.y=80,.z=20,
        .moved_block=34,.moved_meta=0,
        .facing=0,.extending=1,.source=1,
        .progress=0.0f,.last_progress=0.0f
    };
    gm_world_set_block_meta(r->world,20,80,20,36,0);
    gm_world_set_block_meta(r->world,20,79,20,block,meta);
    gm_world_set_block_meta(r->world,20,78,20,1,0);
    for(int direction=0;direction<4;++direction)
        if(neighbor_mask&(1<<direction))
            gm_world_set_block_meta(
                r->world,20+dx[direction],79,20+dz[direction],
                neighbor_block,neighbor_meta);
    if(!gm_runtime_spawn_mob_fixture(
            r,GM_MOB_PIG,eid,x,start_y,z,
            0.0,0.0,0.0,0.0f,10.0f,1,0,0,0))
        return 0;
    gm_runtime_tick(r,idle);
    mobs=r->mobs.current?&r->mobs.b:&r->mobs.a;
    out_y[0]=mobs->y[1];
    gm_runtime_tick(r,idle);
    mobs=r->mobs.current?&r->mobs.b:&r->mobs.a;
    out_y[1]=mobs->y[1];
    return 1;
}

static int down_piston_pig_shape_probe(
        GmRuntime *r,GmAction idle,int block,int meta,
        int neighbor_mask,int neighbor_block,int neighbor_meta,
        double x,double z,int eid,double out_y[2]){
    return down_piston_pig_shape_probe_at_y(
        r,idle,block,meta,neighbor_mask,neighbor_block,neighbor_meta,
        80.5,x,z,eid,out_y);
}

static int down_piston_item_shape_probe_with_below_at_y(
        GmRuntime *r,GmAction idle,int block,int meta,
        int below_block,int below_meta,
        int neighbor_mask,int neighbor_block,int neighbor_meta,
        double start_y,double x,double z,int eid,double out_y[2]){
    static const int dx[4]={0,1,0,-1};
    static const int dz[4]={-1,0,1,0};
    for(int y=78;y<=82;++y)
        for(int zz=18;zz<=22;++zz)
            for(int xx=18;xx<=22;++xx)
                gm_world_set_block_meta(r->world,xx,y,zz,0,0);
    memset(&r->entities,0,sizeof r->entities);
    r->piston_count=1;
    memset(r->pistons,0,sizeof r->pistons);
    r->pistons[0]=(GmRuntimePiston){
        .active=1,.dimension=r->dimension,
        .x=20,.y=80,.z=20,
        .moved_block=34,.moved_meta=0,
        .facing=0,.extending=1,.source=1,
        .progress=0.0f,.last_progress=0.0f
    };
    gm_world_set_block_meta(r->world,20,80,20,36,0);
    gm_world_set_block_meta(r->world,20,79,20,block,meta);
    if(below_block)
        gm_world_set_block_meta(
            r->world,20,78,20,below_block,below_meta);
    for(int direction=0;direction<4;++direction)
        if(neighbor_mask&(1<<direction))
            gm_world_set_block_meta(
                r->world,20+dx[direction],79,20+dz[direction],
                neighbor_block,neighbor_meta);
    if(!gm_live_spawn_item_exact(
            &r->entities,eid,x,start_y,z,
            0.0,0.0,0.0,0.0f,1,1,0,0,32767,1))
        return 0;
    gm_runtime_tick(r,idle);
    out_y[0]=r->entities.ents[0].y;
    gm_runtime_tick(r,idle);
    out_y[1]=r->entities.ents[0].y;
    return 1;
}

static int down_piston_item_shape_probe_at_y(
        GmRuntime *r,GmAction idle,int block,int meta,
        int neighbor_mask,int neighbor_block,int neighbor_meta,
        double start_y,double x,double z,int eid,double out_y[2]){
    return down_piston_item_shape_probe_with_below_at_y(
        r,idle,block,meta,0,0,
        neighbor_mask,neighbor_block,neighbor_meta,
        start_y,x,z,eid,out_y);
}

static int down_piston_item_shape_probe(
        GmRuntime *r,GmAction idle,int block,int meta,
        int neighbor_mask,int neighbor_block,int neighbor_meta,
        double x,double z,int eid,double out_y[2]){
    return down_piston_item_shape_probe_at_y(
        r,idle,block,meta,neighbor_mask,neighbor_block,neighbor_meta,
        80.5,x,z,eid,out_y);
}

static int down_piston_item_door_shape_probe(
        GmRuntime *r,GmAction idle,int block,
        int lower_meta,int upper_meta,
        double x,double z,int eid,double out_y[2]){
    gm_world_set_block_meta(r->world,20,77,20,1,0);
    return down_piston_item_shape_probe_with_below_at_y(
        r,idle,block,upper_meta,block,lower_meta,
        0,0,0,80.5,x,z,eid,out_y);
}

static int east_piston_item_shape_probe_with_neighbor(
        GmRuntime *r,GmAction idle,int block,int meta,
        int neighbor_dx,int neighbor_dy,int neighbor_dz,
        int neighbor_block,int neighbor_meta,
        double y,double z,int eid,double out_x[2]){
    for(int yy=78;yy<=82;++yy)
        for(int zz=18;zz<=22;++zz)
            for(int xx=18;xx<=22;++xx)
                gm_world_set_block_meta(r->world,xx,yy,zz,0,0);
    memset(&r->entities,0,sizeof r->entities);
    r->piston_count=1;
    memset(r->pistons,0,sizeof r->pistons);
    r->pistons[0]=(GmRuntimePiston){
        .active=1,.dimension=r->dimension,
        .x=20,.y=80,.z=20,
        .moved_block=34,.moved_meta=5,
        .facing=5,.extending=1,.source=1,
        .progress=0.0f,.last_progress=0.0f
    };
    gm_world_set_block_meta(r->world,20,80,20,36,5);
    gm_world_set_block_meta(r->world,21,80,20,block,meta);
    if(neighbor_block)
        gm_world_set_block_meta(
            r->world,21+neighbor_dx,80+neighbor_dy,20+neighbor_dz,
            neighbor_block,neighbor_meta);
    if(!gm_live_spawn_item_exact(
            &r->entities,eid,20.5,y,z,
            0.0,0.0,0.0,0.0f,1,1,0,0,32767,1))
        return 0;
    gm_runtime_tick(r,idle);
    out_x[0]=r->entities.ents[0].x;
    gm_runtime_tick(r,idle);
    out_x[1]=r->entities.ents[0].x;
    return 1;
}

static int east_piston_item_shape_probe(
        GmRuntime *r,GmAction idle,int block,int meta,
        double y,double z,int eid,double out_x[2]){
    return east_piston_item_shape_probe_with_neighbor(
        r,idle,block,meta,0,0,0,0,0,y,z,eid,out_x);
}

static int east_piston_pig_shape_probe_at_z(
        GmRuntime *r,GmAction idle,int block,int meta,
        double y,double z,int eid,double out_x[2]){
    const EwStore *mobs;
    for(int yy=78;yy<=82;++yy)
        for(int z=18;z<=22;++z)
            for(int x=18;x<=22;++x)
                gm_world_set_block_meta(r->world,x,yy,z,0,0);
    gm_mobs_init(&r->mobs,0);
    r->mobs_enabled=0;
    r->piston_count=1;
    memset(r->pistons,0,sizeof r->pistons);
    r->pistons[0]=(GmRuntimePiston){
        .active=1,.dimension=r->dimension,
        .x=20,.y=80,.z=20,
        .moved_block=34,.moved_meta=5,
        .facing=5,.extending=1,.source=1,
        .progress=0.0f,.last_progress=0.0f
    };
    gm_world_set_block_meta(r->world,20,80,20,36,5);
    gm_world_set_block_meta(r->world,21,80,20,block,meta);
    if(!gm_runtime_spawn_mob_fixture(
            r,GM_MOB_PIG,eid,20.5,y,z,
            0.0,0.0,0.0,0.0f,10.0f,1,0,0,0))
        return 0;
    gm_runtime_tick(r,idle);
    mobs=r->mobs.current?&r->mobs.b:&r->mobs.a;
    out_x[0]=mobs->x[1];
    gm_runtime_tick(r,idle);
    mobs=r->mobs.current?&r->mobs.b:&r->mobs.a;
    out_x[1]=mobs->x[1];
    return 1;
}

static int east_piston_pig_shape_probe(
        GmRuntime *r,GmAction idle,int block,int meta,
        double y,int eid,double out_x[2]){
    return east_piston_pig_shape_probe_at_z(
        r,idle,block,meta,y,20.5,eid,out_x);
}

int main(void) {
    GmConfig cfg;
    gm_config_defaults(&cfg);
    cfg.view_distance = 1;
    GmRuntime r;
    char err[256];
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err), "runtime initializes");
    if (fail) return 1;
    {
        ChestLive chest;
        chest_live_init(&chest);
        CHECK(chest_live_comparator_strength(&chest) == 0,
              "empty single chest has comparator strength zero");
        chest_live_set(&chest, 0, ic_mk(1, 64, 0));
        CHECK(chest_live_comparator_strength(&chest) == 1,
              "one full stack in one of 27 chest slots has strength one");
        for (int slot = 1; slot < CHEST_LIVE_SLOTS; ++slot)
            chest_live_set(&chest, slot, ic_mk(1, 64, 0));
        CHECK(chest_live_comparator_strength(&chest) == 15,
              "27 full chest slots have comparator strength fifteen");
        chest_live_init(&chest);
        chest_live_set(&chest, 0, ic_mk(403, 1, 0));
        CHECK(chest_live_comparator_strength(&chest) == 1,
              "one non-stackable item uses its item stack limit");
        ChestLive second;
        chest_live_init(&chest);
        chest_live_init(&second);
        for (int slot = 0; slot < 4; ++slot)
            chest_live_set(&chest, slot, ic_mk(1, 64, 0));
        CHECK(chest_live_comparator_strength(&chest) == 3,
              "four full single-chest slots have strength three");
        CHECK(chest_live_double_comparator_strength(
                  &chest, &second) == 2,
              "four full slots across a double chest have strength two");
    }
    {
        FurnaceLive furnace;
        furnace_live_init(&furnace);
        CHECK(furnace_live_comparator_strength(&furnace) == 0,
              "empty furnace has comparator strength zero");
        furnace.input = sr_mk(1, 64, 0);
        CHECK(furnace_live_comparator_strength(&furnace) == 5,
              "one full furnace slot has comparator strength five");
        furnace.fuel = sr_mk(1, 64, 0);
        furnace.output = sr_mk(1, 64, 0);
        CHECK(furnace_live_comparator_strength(&furnace) == 15,
              "three full furnace slots have comparator strength fifteen");
        furnace_live_init(&furnace);
        furnace.input = sr_mk(403, 1, 0);
        CHECK(furnace_live_comparator_strength(&furnace) == 5,
              "one non-stackable furnace item uses its item stack limit");
    }
    {
        GmRuntimeStaticContainer container;
        gm_world_set_block_meta(r.world, 30, 100, 30, 23, 3);
        CHECK(gm_runtime_static_container_set_slot(
                  &r, 0, 30, 100, 30, 0, 1, 64, 0),
              "nine-slot dispenser tile materializes in the cold pool");
        CHECK(gm_runtime_static_container_count(&r) == 1
                  && gm_runtime_static_container_get(&r, 0, &container)
                  && container.block == 23 && container.size == 9
                  && container.slots[0].item == 1
                  && container.slots[0].count == 64,
              "dispenser inventory state round-trips exactly");
        CHECK(!gm_runtime_static_container_set_slot(
                  &r, 0, 30, 100, 30, 9, 1, 64, 0),
              "dispenser rejects a slot outside its exact nine-slot shape");
        gm_world_set_block_meta(r.world, 31, 100, 30, 84, 1);
        CHECK(gm_runtime_static_container_set_slot(
                  &r, 0, 31, 100, 30, 0, 2256, 1, 0),
              "one-record jukebox tile materializes in the cold pool");
        CHECK(gm_runtime_static_container_count(&r) == 2
                  && gm_runtime_static_container_get(&r, 1, &container)
                  && container.block == 84 && container.size == 1
                  && container.slots[0].item == 2256
                  && container.slots[0].count == 1,
              "jukebox record state round-trips exactly");
        CHECK(!gm_runtime_static_container_set_slot(
                  &r, 0, 31, 100, 30, 0, 2255, 1, 0),
              "jukebox rejects a non-record item");
        gm_world_set_block_meta(r.world, 32, 100, 30, 229, 5);
        CHECK(gm_runtime_static_container_set_slot(
                  &r, 0, 32, 100, 30, 0, 1, 64, 0),
              "plain shulker inventory materializes in the cold pool");
        CHECK(gm_runtime_static_container_count(&r) == 3
                  && gm_runtime_static_container_get(&r, 2, &container)
                  && container.block == 229 && container.size == 27
                  && container.slots[0].item == 1
                  && container.slots[0].count == 64,
              "shulker block color and plain inventory round-trip exactly");
        CHECK(gm_runtime_shulker_set_item_tag_nbt(
                  &r, 0, 32, 100, 30,
                  empty_shulker_item_tag_nbt,
                  sizeof empty_shulker_item_tag_nbt)
                  && gm_runtime_static_container_get(&r, 2, &container)
                  && container.item_tag.len
                      == sizeof empty_shulker_item_tag_nbt
                  && !memcmp(container.item_tag.data,
                      empty_shulker_item_tag_nbt,
                      sizeof empty_shulker_item_tag_nbt),
              "shulker retains its bounded complete ItemStack tag blob");
        CHECK(!gm_runtime_shulker_set_item_tag_nbt(
                  &r, 0, 32, 100, 30,
                  empty_shulker_item_tag_nbt,
                  sizeof empty_shulker_item_tag_nbt - 1)
                  && gm_runtime_static_container_get(&r, 2, &container)
                  && container.item_tag.len
                      == sizeof empty_shulker_item_tag_nbt,
              "malformed shulker NBT is rejected without mutation");
        CHECK(!gm_runtime_static_container_set_slot(
                  &r, 0, 32, 100, 30, 0, 219, 2, 0),
              "shulker item obeys its one-item stack limit");
        CHECK(gm_runtime_set_block(&r, 32, 100, 30, 0, 0)
                  && gm_runtime_static_container_count(&r) == 2,
              "replacing a shulker retires its represented tile");
        CHECK(gm_runtime_set_block(&r, 33, 100, 30, 154, 0)
                  && gm_runtime_static_container_count(&r) == 3
                  && gm_runtime_static_container_get(&r, 2, &container)
                  && container.block == 154 && container.size == 5,
              "block insertion materializes an empty five-slot hopper tile");
        CHECK(!gm_runtime_static_container_set_slot(
                  &r, 0, 33, 100, 30, 5, 1, 1, 0),
              "hopper rejects a slot outside its exact five-slot inventory");
        {
            GmAction idle;
            int projectile = 0;
            memset(&idle, 0, sizeof idle);
            idle.hotbar_sel = -1;
            gm_world_set_block_meta(r.world, 35, 100, 30, 23, 3);
            gm_world_set_block_meta(r.world, 35, 100, 31, 0, 0);
            CHECK(gm_runtime_static_container_set_slot(
                      &r, 0, 35, 100, 30, 0, 262, 3, 0),
                  "arrow dispenser accepts its projectile stack");
            CHECK(gm_runtime_schedule_tick(
                      &r, 35, 100, 30, 23,
                      r.clock.total_time + 1, 0, 0),
                  "arrow dispenser callback enters scheduler");
            gm_runtime_tick(&r, idle);
            for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
                if (r.projectiles[i].active
                        && r.projectiles[i].type == 1)
                    ++projectile;
            CHECK(projectile == 1,
                  "arrow dispenser creates one ballistic arrow entity");
            CHECK(gm_runtime_static_container_count(&r) == 4
                      && gm_runtime_static_container_get(&r, 3, &container)
                      && container.slots[0].item == 262
                      && container.slots[0].count == 2,
                  "arrow dispenser consumes exactly one arrow");
            memset(r.projectiles, 0, sizeof r.projectiles);
            CHECK(gm_runtime_set_block(&r, 35, 100, 30, 0, 0)
                      && gm_runtime_static_container_count(&r) == 3,
                  "arrow dispenser fixture retires its tile");
        }
    }
    {
        GmRuntimeCommandBlock command;
        gm_world_set_block_meta(r.world, 32, 100, 30, 137, 2);
        CHECK(gm_runtime_command_block_set_success(
                  &r, 0, 32, 100, 30, 7),
              "inert command tile materializes in the cold pool");
        CHECK(gm_runtime_command_block_count(&r) == 1
                  && gm_runtime_command_block_get(&r, 0, &command)
                  && command.block == 137
                  && command.success_count == 7,
              "command success count round-trips exactly");
        CHECK(!gm_runtime_command_block_set_success(
                  &r, 0, 32, 100, 30, 16),
              "command tile rejects success count above redstone range");
        CHECK(gm_runtime_set_block(&r, 32, 100, 30, 0, 0)
                  && gm_runtime_command_block_count(&r) == 0,
              "replacing a command block retires its represented tile");
    }
    {
        GmRuntimeFlowerPot pot;
        CHECK(gm_runtime_set_block(&r, 33, 100, 30, 140, 0)
                  && gm_runtime_flower_pot_count(&r) == 1
                  && gm_runtime_flower_pot_get(&r, 0, &pot)
                  && pot.item == 0 && pot.meta == 0,
              "block insertion materializes an empty flower-pot tile");
        CHECK(gm_runtime_flower_pot_set(
                  &r, 0, 33, 100, 30, 38, 2),
              "occupied flower-pot tile materializes in the cold pool");
        CHECK(gm_runtime_flower_pot_count(&r) == 1
                  && gm_runtime_flower_pot_get(&r, 0, &pot)
                  && pot.item == 38 && pot.meta == 2,
              "flower-pot item and data round-trip exactly");
        CHECK(!gm_runtime_flower_pot_set(
                  &r, 0, 33, 100, 30, 0, 2),
              "empty flower pot rejects nonzero item metadata");
        CHECK(gm_runtime_set_block(&r, 33, 100, 30, 0, 0)
                  && gm_runtime_flower_pot_count(&r) == 0,
              "replacing a flower pot retires its represented tile");
    }
    {
        GmRuntimeSkull skull;
        static const unsigned char profile_nbt[] = {
            10, 0, 0, 8, 0, 4, 'N', 'a', 'm', 'e',
            0, 10, 'P', 'a', 'r', 'i', 't', 'y', 'H', 'e', 'a', 'd', 0,
        };
        static const unsigned char malformed_nbt[] = {
            10, 0, 0, 8, 0, 4, 'N', 'a', 'm', 'e', 0, 4, 'b', 'a',
        };
        CHECK(gm_runtime_set_block(&r, 34, 100, 30, 144, 1)
                  && gm_runtime_skull_count(&r) == 1
                  && gm_runtime_skull_get(&r, 0, &skull)
                  && skull.type == 0 && skull.rotation == 0,
              "live skull placement materializes the default tile");
        CHECK(gm_runtime_skull_set(
                  &r, 0, 34, 100, 30, 5, 11),
              "ownerless skull tile materializes in the cold pool");
        CHECK(gm_runtime_skull_count(&r) == 1
                  && gm_runtime_skull_get(&r, 0, &skull)
                  && skull.type == 5 && skull.rotation == 11,
              "skull type and rotation round-trip exactly");
        CHECK(!gm_runtime_skull_set(
                  &r, 0, 34, 100, 30, 6, 0),
              "skull tile rejects type outside the 1.11.2 domain");
        CHECK(gm_runtime_skull_set_profile_nbt(
                  &r, 0, 34, 100, 30, 3, 7,
                  profile_nbt, sizeof profile_nbt)
                  && gm_runtime_skull_get(&r, 0, &skull)
                  && skull.type == 3 && skull.rotation == 7
                  && skull.owner_profile.len == sizeof profile_nbt
                  && !memcmp(
                      skull.owner_profile.data,
                      profile_nbt, sizeof profile_nbt),
              "player skull retains a lossless root-compound profile blob");
        CHECK(!gm_runtime_skull_set_profile_nbt(
                  &r, 0, 34, 100, 30, 5, 7,
                  profile_nbt, sizeof profile_nbt)
                  && !gm_runtime_skull_set_profile_nbt(
                      &r, 0, 34, 100, 30, 3, 7,
                      malformed_nbt, sizeof malformed_nbt)
                  && gm_runtime_skull_get(&r, 0, &skull)
                  && skull.owner_profile.len == sizeof profile_nbt
                  && !memcmp(
                      skull.owner_profile.data,
                      profile_nbt, sizeof profile_nbt),
              "profile validation rejects wrong-type and truncated NBT "
              "without changing the existing skull");
        CHECK(gm_runtime_set_block(&r, 34, 100, 30, 0, 0)
                  && gm_runtime_skull_count(&r) == 0,
              "replacing a skull retires its represented tile");
    }
    {
        GmRuntimeItemFrame frame;
        gm_world_set_block_meta(r.world, 34, 100, 30, 0, 0);
        gm_world_set_block_meta(r.world, 35, 100, 30, 1, 0);
        CHECK(gm_runtime_item_frame_set(
                  &r, 0, 94, 34.96875, 100.5, 30.5,
                  34, 100, 30, 4, 1, 1, 0, 6),
              "exact WEST item-frame source materializes in the cold pool");
        CHECK(gm_runtime_item_frame_count(&r) == 1
                  && gm_runtime_item_frame_get(&r, 0, &frame)
                  && frame.eid == 94 && frame.hanging_x == 34
                  && frame.facing == 4 && frame.item == 1
                  && frame.count == 1 && frame.rotation == 6,
              "item-frame comparator source state round-trips exactly");
        CHECK(!gm_runtime_item_frame_set(
                  &r, 0, 95, 34.96875, 100.5, 30.5,
                  34, 100, 30, 4, 1, 1, 0, 8),
              "item frame rejects rotation outside the vanilla 0..7 range");
        CHECK(gm_runtime_set_block(&r, 35, 100, 30, 0, 0)
                  && gm_runtime_item_frame_count(&r) == 0,
              "replacing an item-frame support retires its represented source");
    }
    {
        GmPlayerCtlSnap ctl;memset(&ctl,0,sizeof ctl);ctl.hurt_vel_reset=1;
        gm_player_ctl_dig_import(&ctl);
        gm_player_clear_inferred_hurt_velocity();
        gm_player_ctl_dig_export(&ctl);
        CHECK(!ctl.hurt_vel_reset,
              "recorded EntityTracker velocity clears inferred fall resend");
    }
    CHECK(isr_hotbar_total(&r.player.inv) + isr_main_total(&r.player.inv) == 0,
          "authoritative runtime starts with empty inventory");
    r.player_fire_ticks = 2;
    {
        GmPlayerView fv;gm_runtime_view(&r,&fv);
        CHECK(fv.fire==1&&fv.creative==0,
              "live Entity fire ticks expose first-person burning state");
    }
    r.player_fire_ticks = 0;
    CHECK(gm_runtime_tape_inventory(&r,0,17,2,0),"tape inventory accepts hotbar stack");
    CHECK(gm_runtime_tape_inventory(&r,40,442,1,0),"tape inventory accepts offhand stack");
    CHECK(gm_runtime_tape_inventory(&r,38,443,1,12),
          "tape inventory accepts elytra chest slot 38 with meta");
    CHECK(isr_get_stack(&r.tape_inv,38).meta==12,
          "tape chest elytra preserves durability meta");
    gm_runtime_set_elytra(&r, 1);
    CHECK(r.player.elytra_equipped == 1,
          "set_elytra arms EntityEquipmentSlot.CHEST == Items.ELYTRA for travel");
    gm_runtime_set_elytra(&r, 0);
    CHECK(r.player.elytra_equipped == 0, "set_elytra clears chest equipment flag");
    r.player.elytra_flying_pending = 1;
    gm_runtime_set_elytra_flag7(&r, 1);
    CHECK(r.player.elytra_flag7_recorded == 1 && r.player.elytra_flying == 1 &&
          r.player.elytra_flying_pending == 0,
          "recorded flag-7 event enables authoritative mode and clears prediction");
    gm_runtime_set_elytra_flag7(&r, 0);
    CHECK(r.player.elytra_flying == 0,
          "recorded flag-7 clear applies the metadata value");
    CHECK(gm_runtime_set_inventory(&r,38,443,1,0),"live set_inventory places elytra in chest");
    CHECK(r.player.elytra_equipped==1,"chest elytra arms flight eligibility");
    CHECK(gm_runtime_set_inventory(&r,38,0,0,0),"clear chest");
    /* empty chest leaves set_elytra hook; clear explicitly for the next checks */
    gm_runtime_set_elytra(&r, 0);
    gm_runtime_tape_player_view(&r,7,0.625f,123,0.5f,17,1234,1,1,1,0,9,
                                10,27.5f,0.4f);
    gm_runtime_tape_potions_clear(&r);
    CHECK(gm_runtime_tape_potion(&r,20,0,157,1),
          "tape potion accepts wither effect");
    CHECK(gm_runtime_tape_potion(&r,11,4,1000,0),
          "tape potion accepts a hidden-particle effect");
    {
        GmPlayerView tv;gm_runtime_view(&r,&tv);gm_runtime_apply_tape_view(&r,&tv);
        CHECK(tv.hotbar_ids[0]==17&&tv.hotbar_counts[0]==2,
              "post-tick tape inventory overrides render hotbar only");
        CHECK(isr_get_stack(&r.player.inv,0).item==0,
              "render inventory does not mutate current-tick simulation state");
        CHECK(tv.xp_level==7&&fabsf(tv.xp_frac-0.625f)<1e-6f&&tv.air==123,
              "recorded XP and air override the rendered player view");
        /* GuiIngame.renderPotionEffects gates the icon on doesShowParticles. */
        CHECK(tv.potion_count==2&&tv.potions[0].hide_particles==0&&
              tv.potions[1].hide_particles==1,
              "recorded showParticles flag reaches the HUD view");
        /* AttributeModifiers NBT can zero an armor item: the tape wins. */
        CHECK(tv.armor_points==0,"no armor override leaves the derived value");
        gm_runtime_tape_armor(&r,0);
        {
            GmPlayerView av;gm_runtime_view(&r,&av);gm_runtime_apply_tape_view(&r,&av);
            CHECK(av.armor_points==0,"recorded armor total 0 overrides the guess");
        }
        gm_runtime_tape_armor(&r,7);
        {
            GmPlayerView av;gm_runtime_view(&r,&av);gm_runtime_apply_tape_view(&r,&av);
            CHECK(av.armor_points==7,"recorded armor total overrides the guess");
        }
        gm_runtime_tape_armor(&r,-1);
        CHECK(tv.portal==0.5f&&tv.portal_frame==17&&tv.portal_phase==1234&&tv.loading==1&&
              tv.texture_animations_pinned==1,
              "recorded portal and loading state override the rendered player view");
        CHECK(tv.fire==1&&tv.creative==0&&tv.hurt_time==9&&
              tv.max_hurt_time==10&&fabsf(tv.hurt_yaw-27.5f)<1e-6f,
              "recorded fire and hurt state override the rendered player view");
        CHECK(fabsf(tv.attack_cooldown-0.4f)<1e-6f&&tv.potion_count==2&&
              tv.potions[0].id==20&&tv.potions[0].duration==157,
              "recorded cooldown and potion state override the rendered player view");
    }
    {
        ICStack got;
        CHECK(gm_runtime_tape_gui_slot(&r,GMC_GRID0,5,3,2),
              "tape GUI slot accepts exact stack");
        CHECK(gm_runtime_tape_gui_cursor(&r,17,2,1),
              "tape GUI cursor accepts exact stack");
        CHECK(gm_runtime_tape_furnace(&r,80,1600,100,200),
              "tape furnace progress accepts nonnegative fields");
        CHECK(gm_runtime_tape_gui_slot_get(&r,GMC_GRID0,&got)&&
              got.item==5&&got.count==3&&got.meta==2,
              "tape GUI slot round-trips");
        CHECK(gm_runtime_tape_gui_cursor_get(&r,&got)&&
              got.item==17&&got.count==2&&got.meta==1,
              "tape GUI cursor round-trips");
        {
            ICStack book = ic_mk(403, 1, 0);
            book.n_enchants = 2;
            book.enchants[0].id = 16; book.enchants[0].level = 3;
            book.enchants[1].id = 34; book.enchants[1].level = 1;
            CHECK(gm_runtime_tape_gui_slot_stack(&r, GMC_CHEST0, book),
                  "tape GUI slot accepts StoredEnchantments subset");
            CHECK(gm_runtime_tape_gui_cursor_stack(&r, book),
                  "tape GUI cursor accepts StoredEnchantments subset");
            CHECK(gm_runtime_tape_gui_slot_get(&r, GMC_CHEST0, &got) &&
                  got.item == 403 && got.n_enchants == 2 &&
                  got.enchants[0].id == 16 && got.enchants[0].level == 3 &&
                  got.enchants[1].id == 34 && got.enchants[1].level == 1,
                  "tape GUI slot retains multi-enchant payload");
            CHECK(gm_runtime_tape_gui_cursor_get(&r, &got) &&
                  got.n_enchants == 2 && got.enchants[1].id == 34,
                  "tape GUI cursor retains multi-enchant payload");
        }
        gm_runtime_gui_view_clear(&r);
        CHECK(!gm_runtime_tape_gui_slot_get(&r,GMC_GRID0,&got)&&
              !gm_runtime_tape_gui_cursor_get(&r,&got)&&!r.tape_furnace_active,
              "per-tick GUI render truth clears atomically");
    }
    {
        GmEntityView src;memset(&src,0,sizeof src);
        src.type=10;src.x=1;src.y=64;src.z=2;src.yaw=30;src.health=8;src.ent_id=91;
        src.tape_pose=1;src.head_yaw=55;src.pitch=12;src.hurt_time=4;
        src.death_time=2;src.flags=3;src.sheared=1;src.fleece_color=14;
        src.graze_y=0.75f;src.graze_x=1.1f;
        gm_runtime_ent_view(&r,&src);
        GmEntityView got[1];
        CHECK(gm_runtime_ghost_views(&r,got,1)==1&&got[0].head_yaw==55&&
              got[0].hurt_time==4&&got[0].sheared==1&&got[0].fleece_color==14,
              "oracle entity pose/state survives runtime without inference");
        gm_runtime_ent_views_clear(&r);
    }
    {
        GmAction use; memset(&use,0,sizeof use); use.use=1; use.hotbar_sel=-1;
        GmAction idle; memset(&idle,0,sizeof idle); idle.hotbar_sel=-1;
        GmAction sneak=idle; sneak.sneak=1;
        gm_runtime_set_pose(&r,0.5,3.0,0.5,0,20);
        gm_runtime_tape_boat_view(&r,10163,0.5,3.56,3.5,0);
        gm_runtime_tick(&r,use);
        CHECK(r.tape_boat_ride_id<0&&r.tape_boat_mount_pending==10163,
              "tape boat click waits one client tick for mount response");
        gm_runtime_ent_views_clear(&r);
        gm_runtime_tape_boat_view(&r,10163,0.5,3.55,3.5,0);
        gm_runtime_tick(&r,idle);
        CHECK(r.tape_boat_ride_id==10163&&
              fabs(r.player.ent.posX+r.ox-0.5)<1e-12&&
              fabs(r.player.ent.posY-
                   (3.55-0.44999998807907104))<1e-12&&
              fabs(r.player.ent.posZ+r.oz-3.5)<1e-12,
              "mounted tape player follows exact boat pose at vanilla offset");
        gm_runtime_ent_views_clear(&r);
        gm_runtime_tape_boat_view(&r,10163,0.5,3.54,3.5,0);
        gm_runtime_tick(&r,sneak);
        CHECK(r.tape_boat_ride_id==10163&&r.tape_boat_dismount_pending,
              "tape boat sneak waits one client tick for dismount response");
        gm_runtime_ent_views_clear(&r);
        gm_runtime_tape_boat_view(&r,10163,0.5,3.53,3.5,0);
        gm_runtime_tick(&r,idle);
        CHECK(r.tape_boat_ride_id<0&&!r.tape_boat_dismount_pending,
              "tape boat dismount response clears passenger relationship");
    }
    {
        GmEntityView src; memset(&src,0,sizeof src);
        src.type=EW_TYPE_CREEPER;src.health=20;src.ent_id=9201;
        src.x=(float)(r.player.ent.posX+r.ox);
        src.y=(float)r.player.ent.posY;
        src.z=(float)(r.player.ent.posZ+r.oz)+2.5f;
        GmEntityView got[1]; int first=-1,last=-1;
        for(int t=0;t<12;++t){
            gm_runtime_ent_view(&r,&src);
            CHECK(gm_runtime_ghost_views(&r,got,1)==1,
                  "near tape creeper remains a render-only ghost");
            if(t==0)first=got[0].creeper_fuse;
            if(t==11)last=got[0].creeper_fuse;
            gm_runtime_ent_views_clear(&r);
        }
        CHECK(first==0&&last==11,
              "tape creeper fuse starts one frame after the proximity transition");
    }
    {
        GmEntityView fireball; memset(&fireball,0,sizeof fireball);
        fireball.type=GM_VIEW_DRAGON_FIREBALL;fireball.item_id=385;
        fireball.item_meta=2;fireball.ent_id=3578;
        fireball.x=(float)(r.player.ent.posX+r.ox)+1;
        fireball.y=(float)r.player.ent.posY+1;
        fireball.z=(float)(r.player.ent.posZ+r.oz);
        gm_runtime_ent_view(&r,&fireball);
        gm_runtime_ent_views_clear(&r);
        gm_runtime_ent_view(&r,&fireball);
        gm_runtime_ent_views_clear(&r);
        gm_runtime_ent_views_clear(&r);
        GmEntityView got[1];
        CHECK(gm_runtime_ghost_views(&r,got,1)==1&&
              got[0].type==GM_VIEW_EXPLOSION_LARGE&&got[0].ent_id==3578&&
              got[0].age==1,
              "nearby large-fireball removal latches its impact particle");
        gm_runtime_ent_views_clear(&r);
        CHECK(gm_runtime_ghost_views(&r,got,1)==0,
              "large-fireball impact latch is limited to its anchored puff");
    }

    /* Pin a naturally exposed iron vein for the next binary progression tape. */
    int iron_found=0, iron_x=0, iron_y=0, iron_z=0;
    double iron_sx=0,iron_sy=0,iron_sz=0; float iron_yaw=0,iron_pitch=0;
    static const int qdx[4]={0,0,-1,1}, qdz[4]={-1,1,0,0};
    static const float qyaw[4]={0,180,-90,90};
    for(int x=-32;x<=47 && !iron_found;++x) for(int z=-32;z<=47 && !iron_found;++z)
      for(int y=2;y<80 && !iron_found;++y) if(gm_world_block(r.world,x,y,z)==15)
        for(int d=0;d<4 && !iron_found;++d){
          int sx=x+qdx[d]*2,sz=z+qdz[d]*2;
          if(gm_world_block(r.world,x+qdx[d],y,z+qdz[d])!=0) continue;
          for(int fy=y-2;fy<=y+1 && !iron_found;++fy)
            if(gm_world_block(r.world,sx,fy-1,sz)!=0 &&
               gm_world_block(r.world,sx,fy,sz)==0 && gm_world_block(r.world,sx,fy+1,sz)==0){
              float p=(float)(atan2((fy+PSV_EYE_HEIGHT)-(y+0.5),2.0)*180.0/3.14159265358979323846);
              if(!pose_hits(&r,x,y,z,sx+0.5,fy,sz+0.5,qyaw[d],p))continue;
              iron_found=1;iron_x=x;iron_y=y;iron_z=z;
              iron_sx=sx+0.5;iron_sy=fy;iron_sz=sz+0.5;iron_yaw=qyaw[d];iron_pitch=p;
            }
        }
    CHECK(iron_found,"default streamed world contains naturally exposed iron ore");
    if(iron_found) fprintf(stderr,"runtime: exposed iron=(%d,%d,%d) stand=(%.1f,%.1f,%.1f) yaw=%.1f pitch=%.2f\n",
        iron_x,iron_y,iron_z,iron_sx,iron_sy,iron_sz,iron_yaw,iron_pitch);
    int stone_found=0,stone_x=0,stone_y=0,stone_z=0; double stone_sx=0,stone_sy=0,stone_sz=0;
    float stone_yaw=0,stone_pitch=0;
    for(int x=-32;x<=47 && !stone_found;++x) for(int z=-32;z<=47 && !stone_found;++z)
      for(int y=2;y<80 && !stone_found;++y) if(gm_world_block(r.world,x,y,z)==1)
        for(int d=0;d<4 && !stone_found;++d){int sx=x+qdx[d]*2,sz=z+qdz[d]*2;
          if(gm_world_block(r.world,x+qdx[d],y,z+qdz[d])!=0)continue;
          for(int fy=y-2;fy<=y+1 && !stone_found;++fy)
            if(gm_world_block(r.world,sx,fy-1,sz)!=0&&gm_world_block(r.world,sx,fy,sz)==0&&gm_world_block(r.world,sx,fy+1,sz)==0){
              stone_found=1;stone_x=x;stone_y=y;stone_z=z;stone_sx=sx+0.5;stone_sy=fy;stone_sz=sz+0.5;stone_yaw=qyaw[d];
              stone_pitch=(float)(atan2((fy+PSV_EYE_HEIGHT)-(y+0.5),2.0)*180.0/3.14159265358979323846);}}
    CHECK(stone_found,"default streamed world contains naturally exposed stone");
    if(stone_found)fprintf(stderr,"runtime: exposed stone=(%d,%d,%d) stand=(%.1f,%.1f,%.1f) yaw=%.1f pitch=%.2f\n",
      stone_x,stone_y,stone_z,stone_sx,stone_sy,stone_sz,stone_yaw,stone_pitch);

    int lx = 0, ly = 0, lz = 0, found = 0;
    double stand_x = 0.0, stand_y = 0.0, stand_z = 0.0;
    float stand_yaw = 0.0f, stand_pitch = 0.0f;
    static const int dx[4] = {0, 0, -1, 1};
    static const int dz[4] = {-1, 1, 0, 0};
    static const float yaw[4] = {0.0f, 180.0f, -90.0f, 90.0f};
    for (int x = 0; x < 32 && !found; ++x)
        for (int z = 0; z < 32 && !found; ++z)
            for (int y = 1; y < 128; ++y)
                if (gm_world_block(r.world, x, y, z) == 17) {
                    for (int d = 0; d < 4 && !found; ++d) {
                        int sx = x + dx[d] * 2, sz = z + dz[d] * 2;
                        if (gm_world_block(r.world, x + dx[d], y, z + dz[d]) != 0)
                            continue;
                        for (int fy = y - 2; fy <= y + 1 && !found; ++fy) {
                            if (fy < 1 || gm_world_block(r.world, sx, fy - 1, sz) == 0 ||
                                gm_world_block(r.world, sx, fy, sz) != 0 ||
                                gm_world_block(r.world, sx, fy + 1, sz) != 0) continue;
                            lx = x; ly = y; lz = z;
                            stand_x = sx + 0.5; stand_y = fy; stand_z = sz + 0.5;
                            stand_yaw = yaw[d];
                            stand_pitch = (float)(atan2((fy + PSV_EYE_HEIGHT) - (y + 0.5), 2.0)
                                                * 180.0 / 3.14159265358979323846);
                            found = 1;
                        }
                    }
                    if (found) break;
                }
    CHECK(found, "default streamed world contains a generated oak log");
    if (found) {
        fprintf(stderr, "runtime: generated log=(%d,%d,%d) stand=(%.1f,%.1f,%.1f) yaw=%.1f pitch=%.2f\n",
                lx, ly, lz, stand_x, stand_y, stand_z, stand_yaw, stand_pitch);
        /* Stand south of the generated log and look at its center. Travel is a
         * legal test hook; the log and all progression remain natural. */
        gm_runtime_set_pose(&r, stand_x, stand_y, stand_z, stand_yaw, stand_pitch);
        GmAction a; memset(&a, 0, sizeof a); a.attack = 1; a.hotbar_sel = -1;
        for (int t = 0; t < 200 && gm_world_block(r.world, lx, ly, lz) == 17; ++t)
            gm_runtime_tick(&r, a);
        CHECK(gm_world_block(r.world, lx, ly, lz) == 0,
              "shared runtime breaks the generated log");
        CHECK(isr_hotbar_total(&r.player.inv) + isr_main_total(&r.player.inv) == 0,
              "generated log drop is not injected into inventory");
        int log_slot = -1;
        for (int i = 0; i < GM_LIVE_MAX; ++i)
            if (r.entities.ents[i].active
                    && r.entities.ents[i].item == 17) {
                log_slot = i;
                break;
            }
        CHECK(log_slot >= 0, "generated log creates a live log item entity");
        if (log_slot >= 0) {
            GmLiveEnt *e = &r.entities.ents[log_slot];
            gm_runtime_set_pose(&r, e->x, e->y, e->z, 0.0f, 0.0f);
            memset(&a, 0, sizeof a); a.hotbar_sel = -1;
            for (int t = 0; t < 20 && e->active; ++t)
                gm_runtime_tick(&r, a);
            ICStack got = isr_get_stack(&r.player.inv, 0);
            CHECK(got.item == 17 && got.count == 1,
                  "player collects the natural generated-log entity");
            int grid[9]; for (int i = 0; i < 9; ++i) grid[i] = -1;
            grid[0] = 0;
            CHECK(gm_runtime_craft(&r, 2, grid), "player 2x2 crafts collected log");
            got = isr_get_stack(&r.player.inv, 0);
            CHECK(got.item == 5 && got.count == 4, "log becomes four oak planks");
            CHECK(!gm_runtime_craft(&r, 3, grid), "3x3 crafting rejected without table container");

            grid[0]=grid[1]=grid[3]=grid[4]=0;
            CHECK(gm_runtime_craft(&r,2,grid), "2x2 crafts a crafting table");
            got=isr_get_stack(&r.player.inv,0);
            CHECK(got.item==58 && got.count==1, "crafting table output retained");
            (void)isr_decr_stack_size(&r.player.inv,0,1);
            gm_world_set_block_meta(r.world,1,72,2,58,0);
            gm_runtime_set_pose(&r,0.5,72,2.5,0,0);
            CHECK(gm_runtime_use_block(&r,1,72,2), "reachable crafting table opens 3x3 container");
            isr_set_stack(&r.player.inv,1,ic_mk(5,3,0));
            isr_set_stack(&r.player.inv,2,ic_mk(280,2,0));
            for (int i=0;i<9;++i) grid[i]=-1;
            grid[0]=grid[1]=grid[2]=1; grid[4]=grid[7]=2;
            CHECK(gm_runtime_craft(&r,3,grid), "table container crafts wooden pickaxe");
            int have_pick=0;
            for (int i=0;i<ISR_MAIN_SLOTS;++i) if (isr_get_stack(&r.player.inv,i).item==270) have_pick=1;
            CHECK(have_pick, "wooden pickaxe output retained");

            gm_world_set_block_meta(r.world, 1,72,2,61,3);
            gm_runtime_set_pose(&r,0.5,72,2.5,0,0);
            CHECK(gm_runtime_use_block(&r,1,72,2), "reachable furnace opens live container");
            isr_set_stack(&r.player.inv,10,ic_mk(15,1,0));
            isr_set_stack(&r.player.inv,11,ic_mk(263,1,0));
            CHECK(gm_runtime_furnace_insert(&r,0,10,1)==1, "insert iron ore from inventory");
            CHECK(gm_runtime_furnace_insert(&r,1,11,1)==1, "insert coal from inventory");
            memset(&a,0,sizeof a); a.hotbar_sel=-1;
            for (int t=0;t<200;++t) gm_runtime_tick(&r,a);
            CHECK(r.furnaces[r.active_furnace].state.output.item==265 &&
                  r.furnaces[r.active_furnace].state.output.count==1,
                  "authoritative world ticks smelt iron ingot");
            CHECK(gm_runtime_furnace_extract(&r,2,1)==1,
                  "extract furnace result to inventory");
            int have_ingot=0;
            for (int i=0;i<ISR_MAIN_SLOTS;++i) {
                got=isr_get_stack(&r.player.inv,i);
                if (got.item==265 && got.count==1) have_ingot=1;
            }
            CHECK(have_ingot, "extracted iron ingot retained");
        }
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),"route recipe runtime initializes");
    if(r.world){
        gm_world_set_block(r.world,9,4,8,58);gm_runtime_set_pose(&r,8.5,4,8.5,-90,0);
        CHECK(gm_runtime_use_block(&r,9,4,8),"route recipe table opens");
        int grid[9];for(int i=0;i<9;++i)grid[i]=-1;
        isr_set_stack(&r.player.inv,0,ic_mk(265,3,0));
        grid[0]=grid[2]=grid[4]=0;
        CHECK(gm_runtime_craft(&r,3,grid),"three iron ingots craft an empty bucket");
        isr_set_stack(&r.player.inv,1,ic_mk(35,3,0));
        isr_set_stack(&r.player.inv,2,ic_mk(5,3,0));
        for(int i=0;i<9;++i)grid[i]=-1;
        grid[0]=grid[1]=grid[2]=1;grid[3]=grid[4]=grid[5]=2;
        CHECK(gm_runtime_craft(&r,3,grid),"wool and planks craft a bed");
        isr_set_stack(&r.player.inv,3,ic_mk(369,1,0));
        for(int i=0;i<9;++i)grid[i]=-1;
        grid[0]=3;
        CHECK(gm_runtime_craft(&r,2,grid),"blaze rod crafts two blaze powder");
        isr_set_stack(&r.player.inv,4,ic_mk(368,1,0));
        int powder=-1;for(int i=0;i<ISR_MAIN_SLOTS;++i)
            if(isr_get_stack(&r.player.inv,i).item==377)powder=i;
        for(int i=0;i<9;++i)grid[i]=-1;
        grid[0]=4;grid[1]=powder;
        CHECK(powder>=0&&gm_runtime_craft(&r,2,grid),"pearl and blaze powder craft an eye of ender");
        int bucket=0,bed=0,eye=0;
        for(int i=0;i<ISR_MAIN_SLOTS;++i){ICStack s=isr_get_stack(&r.player.inv,i);
            bucket+=s.item==325?s.count:0;bed+=s.item==355?s.count:0;eye+=s.item==381?s.count:0;}
        CHECK(bucket==1&&bed==1&&eye==1,"all route recipe outputs enter survival inventory");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),"eye throw runtime initializes");
    if(r.world){
        isr_set_stack(&r.player.inv,0,ic_mk(381,2,0));
        gm_runtime_set_pose(&r,8.5,72.0,8.5,0,0);
        GmAction use;memset(&use,0,sizeof use);use.do_place=1;use.use=1;use.hotbar_sel=0;
        gm_runtime_tick(&r,use);
        int eyes=0;for(int i=0;i<GM_RUNTIME_PROJECTILES;++i)
            if(r.projectiles[i].active&&r.projectiles[i].type==4)eyes++;
        CHECK(eyes==1&&isr_get_stack(&r.player.inv,0).count==1,
              "survival use throws and consumes one eye of ender");
        int sx,sz;CHECK(gm_stronghold_locate(r.seed,0,&sx,&sz),"eye target stronghold locates");
        GmRuntimeProjectile *eye=NULL;for(int i=0;i<GM_RUNTIME_PROJECTILES;++i)
            if(r.projectiles[i].active&&r.projectiles[i].type==4)eye=&r.projectiles[i];
        CHECK(eye&&((eye->vx>0)==(sx>8))&&((eye->vz>0)==(sz>8)),
              "eye flies toward the generated seed stronghold");
    }
    gm_runtime_destroy(&r);

    cfg.world=GM_WORLD_SUPERFLAT;
    {
        int saved_mobs = cfg.mobs, saved_weather = cfg.weather;
        cfg.mobs = 0;
        cfg.weather = 0;
        CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
              "block-place audio runtime initializes");
        if(r.world){
            GmRuntimeSoundEvent sound, landing;
            GmAction place;memset(&place,0,sizeof place);
            gm_world_set_block_meta(r.world,8,6,11,1,0);
            isr_set_stack(&r.player.inv,0,ic_mk(3,1,0));
            gm_runtime_set_pose(&r,8.5,5.0,8.5,0,0);
            place.do_place=1;place.hotbar_sel=0;
            gm_runtime_tick(&r,place);
            CHECK(gm_world_block(r.world,8,6,10)==3,
                  "runtime applies the successful ItemBlock edit");
            CHECK(gm_runtime_sound_event_count(&r)==1 &&
                  gm_runtime_sound_event_get(&r,0,&sound) &&
                  sound.sound==GM_SOUND_BLOCK_GRAVEL_PLACE &&
                  sound.category==GM_SOUND_CATEGORY_BLOCKS &&
                  sound.x==8.5 && sound.y==6.5 && sound.z==10.5 &&
                  sound.volume==1.0F && sound.pitch==0.8F,
                  "runtime emits exact placed material, center, and scalars");
            isr_set_stack(&r.player.inv,0,ic_empty());
            gm_runtime_set_pose(&r,8.5,5.0,8.5,0,0);
            memset(&place,0,sizeof place);place.attack=1;
            gm_runtime_tick(&r,place);
            CHECK(gm_runtime_sound_event_count(&r)==2 &&
                  gm_runtime_sound_event_get(&r,1,&sound) &&
                  sound.sound==GM_SOUND_BLOCK_GRAVEL_HIT &&
                  sound.category==GM_SOUND_CATEGORY_NEUTRAL &&
                  sound.x==8.5 && sound.y==6.5 && sound.z==10.5 &&
                  sound.volume==0.25F && sound.pitch==0.5F,
                  "runtime emits exact progressive-mining hit audio");
            gm_world_set_block_meta(r.world,8,6,8,1,0);
            gm_runtime_set_pose(&r,8.5,15.0,8.5,0,0);
            memset(&place,0,sizeof place);
            for(int t=0;t<80 && gm_runtime_sound_event_count(&r)<4;++t)
                gm_runtime_tick(&r,place);
            CHECK(gm_runtime_sound_event_count(&r)==4 &&
                  gm_runtime_sound_event_get(&r,2,&sound) &&
                  gm_runtime_sound_event_get(&r,3,&landing) &&
                  sound.sound==GM_SOUND_PLAYER_BIG_FALL &&
                  landing.sound==GM_SOUND_BLOCK_STONE_FALL &&
                  sound.category==GM_SOUND_CATEGORY_PLAYERS &&
                  landing.category==GM_SOUND_CATEGORY_PLAYERS &&
                  sound.x==8.5 && sound.y==7.0 && sound.z==8.5 &&
                  landing.x==8.5 && landing.y==7.0 && landing.z==8.5 &&
                  sound.volume==1.0F && sound.pitch==1.0F &&
                  landing.volume==0.5F && landing.pitch==0.75F,
                  "runtime emits ordered player and material landing audio");
            for(int x=7;x<=9;++x)
                for(int z=7;z<=24;++z)
                    gm_world_set_block_meta(r.world,x,6,z,1,0);
            gm_runtime_set_pose(&r,8.5,7.0,8.5,0,0);
            GmAction walk;memset(&walk,0,sizeof walk);
            walk.forward=1.0F;walk.hotbar_sel=-1;
            for(int t=0;t<120 && gm_runtime_sound_event_count(&r)<5;++t)
                gm_runtime_tick(&r,walk);
            CHECK(gm_runtime_sound_event_count(&r)==5 &&
                  gm_runtime_sound_event_get(&r,4,&sound) &&
                  sound.sound==GM_SOUND_BLOCK_STONE_STEP &&
                  sound.category==GM_SOUND_CATEGORY_PLAYERS &&
                  sound.x==r.player.ent.posX+(double)r.ox &&
                  sound.y==r.player.ent.posY &&
                  sound.z==r.player.ent.posZ+(double)r.oz &&
                  sound.volume==0.15F && sound.pitch==1.0F,
                  "runtime emits distance-gated material footstep audio");
        }
        gm_runtime_destroy(&r);
        cfg.mobs = saved_mobs;
        cfg.weather = saved_weather;
    }

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),"portal runtime initializes");
    if(r.world){
        for(int x=6;x<10;++x){gm_world_set_block(r.world,x,4,8,49);gm_world_set_block(r.world,x,8,8,49);}
        for(int y=5;y<8;++y){gm_world_set_block(r.world,6,y,8,49);gm_world_set_block(r.world,9,y,8,49);}
        CHECK(gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x123456789ABC)) &&
              gm_runtime_set_block(&r,7,5,8,51,0),
              "runtime fire placement enters BlockFire.onBlockAdded");
        int portal_cells=0;
        for(int x=7;x<=8;++x)for(int y=5;y<=7;++y)
            portal_cells+=gm_world_block(r.world,x,y,8)==90 &&
                gm_world_meta(r.world,x,y,8)==1;
        CHECK(portal_cells==6 &&
              gm_runtime_scheduled_tick_count(&r)==0 &&
              r.world_random_seed48==UINT64_C(0x123456789ABC),
              "fire atomically lights six X-axis portals without RNG/queue");
        for(int z=6;z<=9;++z){
            gm_world_set_block(r.world,20,4,z,49);
            gm_world_set_block(r.world,20,8,z,49);
        }
        for(int y=5;y<=7;++y){
            gm_world_set_block(r.world,20,y,6,49);
            gm_world_set_block(r.world,20,y,9,49);
        }
        CHECK(gm_runtime_set_block(&r,20,5,7,51,0),
              "runtime new fire checks a Z-axis portal frame");
        portal_cells=0;
        for(int z=7;z<=8;++z)for(int y=5;y<=7;++y)
            portal_cells+=gm_world_block(r.world,20,y,z)==90 &&
                gm_world_meta(r.world,20,y,z)==2;
        CHECK(portal_cells==6 &&
              gm_runtime_scheduled_tick_count(&r)==0 &&
              r.world_random_seed48==UINT64_C(0x123456789ABC),
              "fire atomically lights six Z-axis portals without RNG/queue");
        for(int x=30;x<=33;++x){
            gm_world_set_block(r.world,x,4,8,49);
            gm_world_set_block(r.world,x,21,8,49);
        }
        for(int y=5;y<=20;++y){
            gm_world_set_block(r.world,30,y,8,49);
            gm_world_set_block(r.world,33,y,8,49);
        }
        CHECK(gm_runtime_set_block(&r,31,5,8,51,0),
              "runtime new fire checks a legal height-16 portal frame");
        portal_cells=0;
        for(int x=31;x<=32;++x)for(int y=5;y<=20;++y)
            portal_cells+=gm_world_block(r.world,x,y,8)==90 &&
                gm_world_meta(r.world,x,y,8)==1;
        CHECK(portal_cells==32 &&
              gm_runtime_scheduled_tick_count(&r)==0 &&
              r.world_random_seed48==UINT64_C(0x123456789ABC),
              "aligned staging lights height-16 portal without RNG/queue");
        gm_runtime_set_pose(&r,7.5,5.0,8.5,0,0);
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
        for(int t=0;t<82&&r.dimension==0;++t)gm_runtime_tick(&r,idle);
        CHECK(r.dimension==-1,"standing in portal performs client-visible Nether transition");
        CHECK(r.world==r.worlds[0]&&r.world!=r.worlds[1],"runtime swaps to persistent Nether world");
        GmPlayerView nv;gm_runtime_view(&r,&nv);
        CHECK(gm_world_block(r.world,(int)floor(nv.x),(int)floor(nv.y),(int)floor(nv.z))==90,
              "destination portal is linked at scaled Nether coordinate");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),"End transition runtime initializes");
    if(r.world){
        gm_world_set_block(r.world,8,4,8,49);gm_world_set_block(r.world,8,5,8,119);
        gm_runtime_set_pose(&r,8.5,5.0,8.5,0,0);
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
        gm_runtime_tick(&r,idle);
        CHECK(r.dimension==1&&r.world==r.worlds[2],"End portal block transitions into persistent End world");
        CHECK(gm_world_block(r.world,100,48,0)==49,"End arrival platform is generated");
        r.dragon.state.arena.dragon.health=0.0f; /* component hook: death sequence is under test */
        for(int t=0;t<200;++t)gm_runtime_tick(&r,idle);
        CHECK(r.dragon.state.death_processed&&r.dragon.state.arena.dragon.death_ticks==200,
              "dragon runs the full 200-tick death animation");
        CHECK(gm_world_block(r.world,1,63,0)==119,"dragon death creates active exit podium");
        int dragon_xp=0,dragon_orbs=0;
        for(int i=0;i<GM_XP_ORBS;++i)if(!r.mobs.xp_orbs[i].dead&&r.mobs.xp_orbs[i].xpValue>0){
            dragon_xp+=r.mobs.xp_orbs[i].xpValue;++dragon_orbs;
        }
        CHECK(dragon_orbs>1&&dragon_xp==12000,
              "dragon death splits the exact completion XP budget into live orbs");
        gm_runtime_set_pose(&r,1.5,63.0,0.5,0,0);gm_runtime_tick(&r,idle);
        CHECK(r.won&&r.credits,"entering generated exit portal reaches credits and won terminal");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),"bucket runtime initializes");
    if(r.world){
        gm_world_set_block_meta(r.world,8,5,10,9,0);
        isr_set_stack(&r.player.inv,0,ic_mk(325,1,0));
        gm_runtime_set_pose(&r,8.5,5.0,8.5,0,29.25f);
        GmAction use;memset(&use,0,sizeof use);use.do_place=1;use.hotbar_sel=0;
        gm_runtime_tick(&r,use);
        CHECK(isr_get_stack(&r.player.inv,0).item==326&&gm_world_block(r.world,8,5,10)==0,
              "empty bucket collects a looked-at water source");
        gm_world_set_block_meta(r.world,9,4,10,10,0);
        /* ItemBucket.onItemRightClick uses Item.rayTrace(..., false) for a
         * filled bucket, so liquids are ignored and the terrain hit face
         * chooses the placement cell. Aim at the ground's top face: 46.7
         * degrees hits the next ground block's side and vanilla refuses the
         * resulting solid placement target in tryPlaceContainedLiquid. */
        gm_runtime_set_pose(&r,8.5,5.0,8.5,0,50.0f);gm_runtime_tick(&r,use);
        CHECK(isr_get_stack(&r.player.inv,0).item==325,"water bucket returns empty bucket after placement");
        CHECK(gm_world_block(r.world,9,4,10)==49,"water-lava source reaction creates obsidian");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),"potion runtime initializes");
    if(r.world){
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
        CHECK(gm_runtime_potion_add(&r,1,1,2),
              "Speed II fixture enters the bounded active-effect list");
        CHECK(fabs(r.player.movement_speed_multiplier-
                   1.4000000059604645)<1e-12,
              "Speed II applies its exact operation-2 movement modifier");
        gm_runtime_tick(&r,idle);
        GmPlayerView view;gm_runtime_view(&r,&view);
        CHECK(view.potion_count==1&&view.potions[0].id==1&&
              view.potions[0].amplifier==1&&view.potions[0].duration==1,
              "active potion duration decrements once at the tick boundary");
        gm_runtime_tick(&r,idle);
        gm_runtime_view(&r,&view);
        CHECK(view.potion_count==0&&
              fabs(r.player.movement_speed_multiplier-1.0)<1e-15,
              "expired speed attribute is removed before same-tick movement");

        double levitation_base =
            (double)gm_world_surface_y(r.world,8,8);
        gm_runtime_set_pose_state(
            &r,8.5,levitation_base,8.5,180.0f,0.0f,
            0.0,-0.0784000015258789,0.0,1,0.0f);
        CHECK(gm_runtime_potion_add(&r,25,0,3)
                  && r.player.levitation_amplifier==0
                  && r.server_player.levitation_amplifier==0,
              "Levitation fixture enters both player travel paths");
        gm_runtime_tick(&r,idle);
        gm_runtime_view(&r,&view);
        CHECK(view.y==levitation_base
                  && r.server_player.ent.motionY==0.009800000190734865,
              "Levitation replaces ground gravity with the exact rise impulse");
        gm_runtime_tick(&r,idle);
        gm_runtime_view(&r,&view);
        CHECK(fabs(view.y-(levitation_base+0.009800000190734865))<1e-5
                  && r.server_player.ent.motionY==0.017483200489807137,
              "Levitation keeps exact motion across the local packet boundary");
        gm_runtime_tick(&r,idle);
        gm_runtime_view(&r,&view);
        CHECK(view.potion_count==0&&r.player.levitation_amplifier==-1
                  && r.server_player.levitation_amplifier==-1,
              "expired levitation is removed before same-tick travel");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "scheduled-tick runtime initializes");
    if(r.world){
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
        gm_runtime_set_total_time(&r,42);
        CHECK(gm_runtime_schedule_tick(&r,8,4,8,1,45,2,17),
              "cold capsule inserts exact pending stone update");
        CHECK(gm_runtime_schedule_tick(&r,8,4,8,1,99,-3,99) &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "pending queue deduplicates position plus block");
        GmRuntimeScheduledTick pending;
        CHECK(gm_runtime_scheduled_tick_get(&r,0,&pending) &&
              pending.time==45&&pending.priority==2&&pending.order==17,
              "pending queue preserves due time, priority, and insertion id");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==1,
              "pending update remains before its absolute due time");
        gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==0,
              "due inert update drains on the exact world-total-time boundary");
        CHECK(!gm_runtime_schedule_tick(&r,8,4,8,8,50,0,18),
              "unsupported water shape is rejected, not guessed");
        for(int dz=-5;dz<=5;++dz)for(int dx=-5;dx<=5;++dx)
            if(abs(dx)+abs(dz)<=5)
                CHECK(gm_runtime_load_block(&r,10+dx,77,8+dz,1,0) &&
                      gm_runtime_load_block(&r,10+dx,78,8+dz,0,0) &&
                      gm_runtime_load_block(&r,10+dx,79,8+dz,0,0),
                      "flat water fixture cell loads cold");
        CHECK(gm_runtime_load_block(&r,10,78,8,8,0),
              "exact water source loads cold");
        CHECK(gm_runtime_schedule_tick(&r,10,78,8,8,48,0,18),
              "proof-safe active water update enters the pending queue");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==1,
              "water source remains pending before due time");
        gm_runtime_tick(&r,idle);
        CHECK(gm_world_block(r.world,10,78,7)==8 &&
              gm_world_meta(r.world,10,78,7)==1 &&
              gm_world_block(r.world,10,78,9)==8 &&
              gm_world_meta(r.world,10,78,9)==1 &&
              gm_world_block(r.world,9,78,8)==8 &&
              gm_world_meta(r.world,9,78,8)==1 &&
              gm_world_block(r.world,11,78,8)==8 &&
              gm_world_meta(r.world,11,78,8)==1,
              "due water source creates four exact level-1 neighbors");
        CHECK(gm_runtime_scheduled_tick_count(&r)==5,
              "water dispatch replaces its entry with five +5 child updates");
        CHECK(gm_runtime_scheduled_tick_get(&r,0,&pending) &&
              pending.x==10&&pending.y==78&&pending.z==7&&
              pending.time==53&&pending.priority==0,
              "water child queue begins with north at the exact due time");
        CHECK(gm_runtime_scheduled_tick_get(&r,1,&pending) &&
              pending.x==10&&pending.y==78&&pending.z==8,
              "north notification requeues the dynamic source second");
        for(int tick=0;tick<4;++tick)gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==5,
              "level-1 children remain pending through total time 52");
        gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==12,
              "five ordered child updates produce twelve level-2 updates");
        CHECK(gm_world_block(r.world,10,78,8)==9 &&
              gm_world_meta(r.world,10,78,8)==0,
              "source settles static after the ordered child batch");
        CHECK(gm_world_block(r.world,10,78,6)==8 &&
              gm_world_meta(r.world,10,78,6)==2 &&
              gm_world_block(r.world,8,78,8)==8 &&
              gm_world_meta(r.world,8,78,8)==2,
              "second dispatch creates the exact level-2 outer ring");
        CHECK(gm_runtime_scheduled_tick_get(&r,0,&pending) &&
              pending.x==10&&pending.y==78&&pending.z==6&&
              pending.time==58,
              "second-generation queue starts northward at +5");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "downward-water runtime initializes");
    if(r.world){
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
        gm_runtime_set_total_time(&r,42);
        for(int dz=-5;dz<=5;++dz)for(int dx=-5;dx<=5;++dx)
            if(abs(dx)+abs(dz)<=5)
                CHECK(gm_runtime_load_block(&r,10+dx,77,8+dz,1,0) &&
                      gm_runtime_load_block(&r,10+dx,78,8+dz,0,0) &&
                      gm_runtime_load_block(&r,10+dx,79,8+dz,0,0) &&
                      gm_runtime_load_block(&r,10+dx,80,8+dz,0,0),
                      "two-layer water basin cell loads cold");
        CHECK(gm_runtime_load_block(&r,10,79,8,8,0),
              "raised water source loads cold");
        CHECK(gm_runtime_schedule_tick(&r,10,79,8,8,45,0,0),
              "raised water source enters the pending queue");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==1,
              "raised source remains pending before due time");
        gm_runtime_tick(&r,idle);
        CHECK(gm_world_block(r.world,10,78,8)==8 &&
              gm_world_meta(r.world,10,78,8)==8,
              "source creates exact falling-water metadata below");
        CHECK(gm_runtime_scheduled_tick_count(&r)==2,
              "downward dispatch leaves falling cell plus woken source");
        GmRuntimeScheduledTick pending;
        CHECK(gm_runtime_scheduled_tick_get(&r,0,&pending) &&
              pending.x==10&&pending.y==78&&pending.z==8&&
              pending.time==50&&pending.order==1,
              "falling cell is first in the exact +5 queue");
        CHECK(gm_runtime_scheduled_tick_get(&r,1,&pending) &&
              pending.x==10&&pending.y==79&&pending.z==8&&
              pending.time==50&&pending.order==2,
              "falling water wakes and requeues its source second");
        for(int tick=0;tick<4;++tick)gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==2,
              "downward children remain pending through total time 49");
        gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==10,
              "two ordered downward children produce ten descendants");
        CHECK(gm_world_block(r.world,10,78,7)==8 &&
              gm_world_meta(r.world,10,78,7)==1 &&
              gm_world_block(r.world,10,79,7)==8 &&
              gm_world_meta(r.world,10,79,7)==1,
              "falling and source layers spread exact level-1 rings");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "lava-source runtime initializes");
    if(r.world){
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
        gm_runtime_set_total_time(&r,42);
        for(int dz=-5;dz<=5;++dz)for(int dx=-5;dx<=5;++dx)
            if(abs(dx)+abs(dz)<=5)
                CHECK(gm_runtime_load_block(&r,10+dx,77,8+dz,1,0) &&
                      gm_runtime_load_block(&r,10+dx,78,8+dz,0,0) &&
                      gm_runtime_load_block(&r,10+dx,79,8+dz,0,0),
                      "flat lava fixture cell loads cold");
        CHECK(gm_runtime_load_block(&r,10,78,8,10,0),
              "exact dynamic lava source loads cold");
        CHECK(gm_runtime_schedule_tick(&r,10,78,8,10,45,0,0),
              "proof-safe lava source enters the pending queue");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==1,
              "lava source remains pending before due time");
        gm_runtime_tick(&r,idle);
        CHECK(gm_world_block(r.world,10,78,7)==10 &&
              gm_world_meta(r.world,10,78,7)==2 &&
              gm_world_block(r.world,10,78,9)==10 &&
              gm_world_meta(r.world,10,78,9)==2 &&
              gm_world_block(r.world,9,78,8)==10 &&
              gm_world_meta(r.world,9,78,8)==2 &&
              gm_world_block(r.world,11,78,8)==10 &&
              gm_world_meta(r.world,11,78,8)==2,
              "due lava source creates four exact level-2 neighbors");
        CHECK(gm_runtime_scheduled_tick_count(&r)==5,
              "lava dispatch leaves five +30 child updates");
        GmRuntimeScheduledTick pending;
        CHECK(gm_runtime_scheduled_tick_get(&r,0,&pending) &&
              pending.x==10&&pending.y==78&&pending.z==7&&
              pending.time==75&&pending.order==1,
              "lava child queue begins north at the exact +30 due time");
        CHECK(gm_runtime_scheduled_tick_get(&r,1,&pending) &&
              pending.x==10&&pending.y==78&&pending.z==8&&
              pending.order==2,
              "north lava notification requeues the source second");
        for(int tick=0;tick<29;++tick)gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==5,
              "lava children remain pending through total time 74");
        gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==12,
              "five ordered lava children produce twelve descendants");
        CHECK(gm_world_block(r.world,10,78,8)==11 &&
              gm_world_meta(r.world,10,78,8)==0,
              "lava source settles static after the ordered child batch");
        CHECK(gm_world_block(r.world,10,78,6)==10 &&
              gm_world_meta(r.world,10,78,6)==4 &&
              gm_world_block(r.world,8,78,8)==10 &&
              gm_world_meta(r.world,8,78,8)==4,
              "second lava dispatch creates the exact level-4 outer ring");
        CHECK(gm_runtime_scheduled_tick_get(&r,0,&pending) &&
              pending.x==10&&pending.y==78&&pending.z==6&&
              pending.time==105,
              "second-generation lava queue starts northward at +30");
        CHECK(gm_runtime_load_block(&r,20,78,8,10,2) &&
              !gm_runtime_schedule_tick(&r,20,78,8,10,75,0,6),
              "non-source lava remains outside the promoted exact slice");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "lava-water reaction runtime initializes");
    if(r.world){
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
        gm_runtime_set_total_time(&r,42);
        CHECK(gm_runtime_load_block(&r,10,77,8,1,0) &&
              gm_runtime_load_block(&r,10,78,7,1,0) &&
              gm_runtime_load_block(&r,10,78,9,1,0) &&
              gm_runtime_load_block(&r,9,78,8,1,0) &&
              gm_runtime_load_block(&r,11,78,8,1,0) &&
              gm_runtime_load_block(&r,10,78,8,8,0) &&
              gm_runtime_load_block(&r,10,79,8,10,0),
              "enclosed water and raised lava pair loads cold");
        CHECK(gm_runtime_schedule_tick(&r,10,78,8,8,45,0,0) &&
              gm_runtime_schedule_tick(&r,10,79,8,10,70,0,1),
              "reaction pair restores both ordered pending entries");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==2,
              "reaction pair remains pending before the water boundary");
        gm_runtime_tick(&r,idle);
        CHECK(gm_world_block(r.world,10,78,8)==9 &&
              gm_world_meta(r.world,10,78,8)==0 &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "enclosed water settles static without spreading");
        for(int tick=0;tick<24;++tick)gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==1,
              "raised lava remains pending through total time 69");
        gm_runtime_tick(&r,idle);
        CHECK(gm_world_block(r.world,10,78,8)==1,
              "downward lava-water contact creates stone");
        CHECK(gm_world_block(r.world,10,79,8)==10 &&
              gm_world_meta(r.world,10,79,8)==0,
              "stone notification wakes the raised lava source");
        GmRuntimeScheduledTick pending;
        CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&pending) &&
              pending.block==10&&pending.x==10&&pending.y==79&&
              pending.z==8&&pending.time==100,
              "woken lava source is requeued at the exact +30 boundary");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "falling-sand runtime initializes");
    if(r.world){
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
        gm_runtime_set_total_time(&r,42);
        CHECK(gm_runtime_load_block(&r,12,77,8,1,0) &&
              gm_runtime_load_block(&r,12,78,8,0,0) &&
              gm_runtime_load_block(&r,12,79,8,0,0) &&
              gm_runtime_load_block(&r,12,80,8,12,0),
              "clear falling-sand column loads cold");
        CHECK(gm_runtime_set_entity_id_cursor(&r,309092),
              "falling fixture restores Java's next entity id");
        CHECK(gm_runtime_schedule_tick(&r,12,80,8,12,45,0,0),
              "proof-safe falling sand enters the pending queue");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
              r.falling_block_count==0,
              "sand remains a block before its due boundary");
        gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==0 &&
              r.falling_block_count==1 &&
              gm_world_block(r.world,12,80,8)==0,
              "due sand update becomes one transient entity");
        CHECK(r.falling_blocks[0].eid==309092 &&
              r.next_entity_id==309093 &&
              r.falling_blocks[0].fall_time==1,
              "falling entity consumes the exact Java id cursor once");
        CHECK(fabs(r.falling_blocks[0].y-79.96999999135733)<1e-14 &&
              fabs(r.falling_blocks[0].vy-
                   -0.03919999988675116)<1e-14,
              "first falling tick matches Java gravity, movement, and drag");
        {
            GmEntityView falling[1];
            CHECK(gm_runtime_falling_block_views(&r,falling,1)==1 &&
                  falling[0].type==GM_VIEW_FALLING_BLOCK &&
                  falling[0].ent_id==309092 &&
                  falling[0].item_id==12,
                  "live falling sand reaches the terrain-atlas render path");
        }
        for(int tick=0;tick<8;++tick)gm_runtime_tick(&r,idle);
        CHECK(r.falling_block_count==1 &&
              r.falling_blocks[0].fall_time==9 &&
              fabs(r.falling_blocks[0].y-78.30271925449955)<1e-13,
              "falling sand follows Java's exact nine-tick trajectory");
        gm_runtime_tick(&r,idle);
        CHECK(r.falling_block_count==0 &&
              gm_world_block(r.world,12,78,8)==12 &&
              gm_world_meta(r.world,12,78,8)==0,
              "falling sand lands as the exact block state");
        GmRuntimeScheduledTick pending;
        CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&pending) &&
              pending.block==12&&pending.x==12&&pending.y==78&&
              pending.z==8&&pending.time==56,
              "landed sand schedules its vanilla +2 stability update");
        gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==1,
              "landed-sand stability update remains pending for one tick");
        gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==0 &&
              gm_world_block(r.world,12,78,8)==12,
              "stable sand drains its due update without moving");
        CHECK(gm_runtime_load_block(&r,14,80,8,12,1) &&
              !gm_runtime_schedule_tick(&r,14,80,8,12,60,0,1),
              "nonzero sand metadata remains outside the exact slice");

        {
            long long gravel_base = r.clock.total_time;
            CHECK(gm_runtime_load_block(&r,16,77,8,1,0) &&
                  gm_runtime_load_block(&r,16,78,8,0,0) &&
                  gm_runtime_load_block(&r,16,79,8,0,0) &&
                  gm_runtime_load_block(&r,16,80,8,13,0),
                  "clear falling-gravel column loads cold");
            CHECK(gm_runtime_set_entity_id_cursor(&r,309094),
                  "gravel fixture restores Java's next entity id");
            CHECK(gm_runtime_schedule_tick(
                      &r,16,80,8,13,gravel_base+3,0,2),
                  "proof-safe falling gravel enters the pending queue");
            gm_runtime_tick(&r,idle);
            gm_runtime_tick(&r,idle);
            CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
                  r.falling_block_count==0,
                  "gravel remains a block before its due boundary");
            gm_runtime_tick(&r,idle);
            CHECK(gm_runtime_scheduled_tick_count(&r)==0 &&
                  r.falling_block_count==1 &&
                  gm_world_block(r.world,16,80,8)==0,
                  "due gravel update becomes one transient entity");
            CHECK(r.falling_blocks[0].eid==309094 &&
                  r.next_entity_id==309095 &&
                  r.falling_blocks[0].block==13 &&
                  r.falling_blocks[0].fall_time==1,
                  "falling gravel preserves identity and consumes one id");
            CHECK(fabs(r.falling_blocks[0].y-79.96999999135733)<1e-14 &&
                  fabs(r.falling_blocks[0].vy-
                       -0.03919999988675116)<1e-14,
                  "first gravel tick matches Java's exact trajectory");
            {
                GmEntityView falling[1];
                CHECK(gm_runtime_falling_block_views(&r,falling,1)==1 &&
                      falling[0].type==GM_VIEW_FALLING_BLOCK &&
                      falling[0].ent_id==309094 &&
                      falling[0].item_id==13,
                      "live falling gravel reaches its terrain texture");
            }
            for(int tick=0;tick<8;++tick)gm_runtime_tick(&r,idle);
            CHECK(r.falling_block_count==1 &&
                  r.falling_blocks[0].fall_time==9 &&
                  fabs(r.falling_blocks[0].y-78.30271925449955)<1e-13,
                  "falling gravel follows Java's exact nine-tick trajectory");
            gm_runtime_tick(&r,idle);
            CHECK(r.falling_block_count==0 &&
                  gm_world_block(r.world,16,78,8)==13 &&
                  gm_world_meta(r.world,16,78,8)==0,
                  "falling gravel lands as the exact block state");
            CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                  pending.block==13&&pending.x==16&&pending.y==78&&
                  pending.z==8&&pending.time==gravel_base+14,
                  "landed gravel schedules its vanilla +2 stability update");
            gm_runtime_tick(&r,idle);
            CHECK(gm_runtime_scheduled_tick_count(&r)==1,
                  "landed-gravel stability update remains pending one tick");
            gm_runtime_tick(&r,idle);
            CHECK(gm_runtime_scheduled_tick_count(&r)==0 &&
                  gm_world_block(r.world,16,78,8)==13,
                  "stable gravel drains its due update without moving");
            CHECK(gm_runtime_load_block(&r,18,80,8,13,1) &&
                  !gm_runtime_schedule_tick(
                      &r,18,80,8,13,r.clock.total_time+3,0,3),
                  "nonzero gravel metadata remains outside the exact slice");
        }

        {
            const int passthrough[3]={9,11,51};
            const int falling_id[3]={12,13,12};
            const int column_x[3]={20,22,24};
            uint64_t world_seed=r.world_random_seed48;
            for(int material=0;material<3;++material){
                int x=column_x[material];
                int block=falling_id[material];
                long long base=r.clock.total_time;
                int eid=309100+material;
                CHECK(gm_runtime_load_block(&r,x,77,8,1,0) &&
                      gm_runtime_load_block(
                          &r,x,78,8,passthrough[material],0) &&
                      gm_runtime_load_block(&r,x,79,8,0,0) &&
                      gm_runtime_load_block(&r,x,80,8,block,0),
                      "falling passthrough column loads cold");
                CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                      gm_runtime_schedule_tick(
                          &r,x,80,8,block,base+3,0,4+material),
                      "water/lava/fire falling callback restores exactly");
                gm_runtime_tick(&r,idle);
                gm_runtime_tick(&r,idle);
                CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
                      r.falling_block_count==0,
                      "passthrough callback remains pending before due");
                gm_runtime_tick(&r,idle);
                CHECK(gm_runtime_scheduled_tick_count(&r)==0 &&
                      r.falling_block_count==1 &&
                      r.falling_blocks[0].eid==eid &&
                      r.falling_blocks[0].block==block &&
                      r.falling_blocks[0].fall_time==1 &&
                      gm_world_block(r.world,x,80,8)==0,
                      "passthrough callback spawns the exact falling state");
                for(int tick=0;tick<8;++tick)gm_runtime_tick(&r,idle);
                CHECK(r.falling_block_count==1 &&
                      r.falling_blocks[0].fall_time==9 &&
                      fabs(r.falling_blocks[0].y-
                           78.30271925449955)<1e-13,
                      "passthrough material keeps the nine-tick trajectory");
                gm_runtime_tick(&r,idle);
                CHECK(r.falling_block_count==0 &&
                      gm_world_block(r.world,x,78,8)==block &&
                      gm_world_meta(r.world,x,78,8)==0 &&
                      gm_runtime_scheduled_tick_count(&r)==1 &&
                      gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                      pending.block==block&&pending.x==x&&pending.y==78&&
                      pending.z==8&&pending.time==base+14,
                      "falling block replaces passthrough and queues stability");
                gm_runtime_tick(&r,idle);
                gm_runtime_tick(&r,idle);
                CHECK(gm_runtime_scheduled_tick_count(&r)==0 &&
                      gm_world_block(r.world,x,78,8)==block,
                      "passthrough landing drains its stable callback");
            }
            CHECK(r.world_random_seed48==world_seed,
                  "falling passthrough lifecycle consumes no World.rand");
        }

        {
            const int x=25;
            long long base=r.clock.total_time;
            CHECK(gm_runtime_load_block(&r,x,77,8,44,8) &&
                  gm_runtime_load_block(&r,x,78,8,0,0) &&
                  gm_runtime_load_block(&r,x,79,8,0,0) &&
                  gm_runtime_load_block(&r,x,80,8,12,0) &&
                  gm_runtime_set_entity_id_cursor(&r,499999),
                  "top-slab falling-sand column loads cold");
            CHECK(gm_runtime_schedule_tick(
                      &r,x,80,8,12,base+3,0,7),
                  "top-slab sand callback restores exactly");
            gm_runtime_tick(&r,idle);
            gm_runtime_tick(&r,idle);
            gm_runtime_tick(&r,idle);
            CHECK(r.falling_block_count==1 &&
                  r.falling_blocks[0].eid==499999 &&
                  r.falling_blocks[0].landing_y==78.0 &&
                  !r.falling_blocks[0].drop_on_land,
                  "top slab exposes its full-height landing plane");
            for(int tick=0;tick<8;++tick)gm_runtime_tick(&r,idle);
            CHECK(r.falling_block_count==1 &&
                  r.falling_blocks[0].fall_time==9 &&
                  fabs(r.falling_blocks[0].y-
                       78.30271925449955)<1e-13,
                  "top-slab sand preserves exact nine-tick trajectory");
            gm_runtime_tick(&r,idle);
            CHECK(r.falling_block_count==0 &&
                  gm_world_block(r.world,x,77,8)==44 &&
                  gm_world_meta(r.world,x,77,8)==8 &&
                  gm_world_block(r.world,x,78,8)==12 &&
                  gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                  pending.block==12&&pending.x==x&&pending.y==78&&
                  pending.z==8&&pending.time==base+14,
                  "sand lands above top slab and queues exact stability");
            gm_runtime_tick(&r,idle);
            gm_runtime_tick(&r,idle);
            CHECK(gm_runtime_scheduled_tick_count(&r)==0 &&
                  gm_world_block(r.world,x,78,8)==12,
                  "top-slab landing drains its stable callback");
        }

        {
            const int x=26;
            const uint64_t math_seed=UINT64_C(0x123456789ABC);
            const uint64_t world_seed=r.world_random_seed48;
            long long base=r.clock.total_time;
            memset(&r.entities,0,sizeof r.entities);
            CHECK(gm_runtime_load_block(&r,x,77,8,44,0) &&
                  gm_runtime_load_block(&r,x,78,8,0,0) &&
                  gm_runtime_load_block(&r,x,79,8,0,0) &&
                  gm_runtime_load_block(&r,x,80,8,12,0),
                  "bottom-slab falling-drop column loads cold");
            CHECK(gm_runtime_set_entity_id_cursor(&r,500000) &&
                  gm_runtime_set_math_random_seed48(&r,math_seed) &&
                  gm_runtime_schedule_tick(
                      &r,x,80,8,12,base+3,0,8),
                  "bottom-slab sand callback restores exact event cursors");
            gm_runtime_tick(&r,idle);
            gm_runtime_tick(&r,idle);
            CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
                  r.falling_block_count==0,
                  "bottom-slab sand remains pending before its due tick");
            gm_runtime_tick(&r,idle);
            CHECK(gm_runtime_scheduled_tick_count(&r)==0 &&
                  r.falling_block_count==1 &&
                  r.falling_blocks[0].eid==500000 &&
                  r.falling_blocks[0].drop_on_land==1 &&
                  r.falling_blocks[0].landing_y==77.5 &&
                  r.falling_blocks[0].fall_time==1 &&
                  gm_world_block(r.world,x,80,8)==0,
                  "bottom-slab callback spawns the exact drop-path entity");
            for(int tick=0;tick<10;++tick)
                gm_runtime_tick(&r,idle);
            CHECK(r.falling_block_count==1 &&
                  r.falling_blocks[0].fall_time==11 &&
                  fabs(r.falling_blocks[0].y-
                       77.53832751878592)<1e-13 &&
                  r.entities.n_active==0,
                  "drop-path sand follows Java through fallTime 11");
            gm_runtime_tick(&r,idle);
            CHECK(r.falling_block_count==0 &&
                  gm_world_block(r.world,x,80,8)==0 &&
                  gm_world_block(r.world,x,77,8)==44 &&
                  gm_world_meta(r.world,x,77,8)==0 &&
                  gm_runtime_scheduled_tick_count(&r)==0,
                  "failed placement retires sand without replacing the slab");
            CHECK(r.entities.n_active==1 &&
                  r.entities.ents[0].active &&
                  r.entities.ents[0].eid==500001 &&
                  r.entities.ents[0].item==12 &&
                  r.entities.ents[0].count==1 &&
                  r.entities.ents[0].meta==0 &&
                  r.entities.ents[0].age==1 &&
                  r.entities.ents[0].pickup_delay==9 &&
                  r.entities.ents[0].health==5,
                  "failed placement creates and ticks one exact sand item");
            CHECK(fabs(r.entities.ents[0].x-
                       26.512723377905786)<1e-14 &&
                  fabs(r.entities.ents[0].y-
                       77.6600000038743)<1e-13 &&
                  fabs(r.entities.ents[0].z-
                       8.565416760742664)<1e-14 &&
                  fabs(r.entities.ents[0].mx-
                       0.012468910590349491)<1e-15 &&
                  fabs(r.entities.ents[0].my-
                       0.15680000684857376)<1e-15 &&
                  fabs(r.entities.ents[0].mz-
                       0.06410842677553674)<1e-15 &&
                  r.entities.ents[0].yaw==346.55627f,
                  "drop item motion and yaw match Java after its first tick");
            CHECK(r.next_entity_id==500002 &&
                  r.math_random_seed48==UINT64_C(52327523130500) &&
                  r.world_random_seed48==world_seed,
                  "falling drop consumes two ids and four Math.random calls only");
        }

        {
            const int x=27;
            const uint64_t math_seed=UINT64_C(0x123456789ABC);
            const uint64_t world_seed=r.world_random_seed48;
            long long base=r.clock.total_time;
            memset(&r.entities,0,sizeof r.entities);
            CHECK(gm_runtime_load_block(&r,x,77,8,208,0) &&
                  gm_runtime_load_block(&r,x,78,8,0,0) &&
                  gm_runtime_load_block(&r,x,79,8,0,0) &&
                  gm_runtime_load_block(&r,x,80,8,12,0),
                  "grass-path falling-drop column loads cold");
            CHECK(gm_runtime_set_entity_id_cursor(&r,500100) &&
                  gm_runtime_set_math_random_seed48(&r,math_seed) &&
                  gm_runtime_schedule_tick(
                      &r,x,80,8,12,base+3,0,9),
                  "grass-path sand callback restores exact event cursors");
            gm_runtime_tick(&r,idle);
            gm_runtime_tick(&r,idle);
            CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
                  r.falling_block_count==0,
                  "grass-path sand remains pending before its due tick");
            gm_runtime_tick(&r,idle);
            CHECK(gm_runtime_scheduled_tick_count(&r)==0 &&
                  r.falling_block_count==1 &&
                  r.falling_blocks[0].eid==500100 &&
                  r.falling_blocks[0].drop_on_land==1 &&
                  r.falling_blocks[0].landing_y==77.9375 &&
                  r.falling_blocks[0].fall_time==1 &&
                  gm_world_block(r.world,x,80,8)==0,
                  "grass-path callback spawns the exact drop-path entity");
            for(int tick=0;tick<8;++tick)
                gm_runtime_tick(&r,idle);
            CHECK(r.falling_block_count==1 &&
                  r.falling_blocks[0].fall_time==9 &&
                  fabs(r.falling_blocks[0].y-
                       78.30271925449955)<1e-13 &&
                  r.entities.n_active==0,
                  "grass-path sand follows Java through fallTime 9");
            gm_runtime_tick(&r,idle);
            CHECK(r.falling_block_count==0 &&
                  gm_world_block(r.world,x,80,8)==0 &&
                  gm_world_block(r.world,x,77,8)==208 &&
                  gm_world_meta(r.world,x,77,8)==0 &&
                  gm_runtime_scheduled_tick_count(&r)==0,
                  "failed grass-path placement retires sand without replacement");
            CHECK(r.entities.n_active==1 &&
                  r.entities.ents[0].active &&
                  r.entities.ents[0].eid==500101 &&
                  r.entities.ents[0].item==12 &&
                  r.entities.ents[0].count==1 &&
                  r.entities.ents[0].meta==0 &&
                  r.entities.ents[0].age==1 &&
                  r.entities.ents[0].pickup_delay==9 &&
                  r.entities.ents[0].health==5,
                  "grass-path placement failure creates one exact sand item");
            CHECK(fabs(r.entities.ents[0].x-
                       27.512723377905786)<1e-14 &&
                  fabs(r.entities.ents[0].y-
                       78.0975000038743)<1e-13 &&
                  fabs(r.entities.ents[0].z-
                       8.565416760742664)<1e-14 &&
                  fabs(r.entities.ents[0].mx-
                       0.012468910590349491)<1e-15 &&
                  fabs(r.entities.ents[0].my-
                       0.15680000684857376)<1e-15 &&
                  fabs(r.entities.ents[0].mz-
                       0.06410842677553674)<1e-15 &&
                  r.entities.ents[0].yaw==346.55627f,
                  "grass-path item motion and yaw match its first Java tick");
            CHECK(r.next_entity_id==500102 &&
                  r.math_random_seed48==UINT64_C(52327523130500) &&
                  r.world_random_seed48==world_seed,
                  "grass-path drop consumes two ids and four Math.random calls only");
        }

        {
            const int x=28;
            const uint64_t math_seed=UINT64_C(0x123456789ABC);
            const uint64_t world_seed=r.world_random_seed48;
            long long base=r.clock.total_time;
            memset(&r.entities,0,sizeof r.entities);
            CHECK(gm_runtime_load_block(&r,x,77,8,88,0) &&
                  gm_runtime_load_block(&r,x,78,8,0,0) &&
                  gm_runtime_load_block(&r,x,79,8,0,0) &&
                  gm_runtime_load_block(&r,x,80,8,12,0),
                  "soul-sand falling-drop column loads cold");
            CHECK(gm_runtime_set_entity_id_cursor(&r,500200) &&
                  gm_runtime_set_math_random_seed48(&r,math_seed) &&
                  gm_runtime_schedule_tick(
                      &r,x,80,8,12,base+3,0,10),
                  "soul-sand callback restores exact event cursors");
            gm_runtime_tick(&r,idle);
            gm_runtime_tick(&r,idle);
            CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
                  r.falling_block_count==0,
                  "soul-sand sand remains pending before its due tick");
            gm_runtime_tick(&r,idle);
            CHECK(gm_runtime_scheduled_tick_count(&r)==0 &&
                  r.falling_block_count==1 &&
                  r.falling_blocks[0].eid==500200 &&
                  r.falling_blocks[0].drop_on_land==1 &&
                  r.falling_blocks[0].landing_y==77.875 &&
                  r.falling_blocks[0].fall_time==1 &&
                  gm_world_block(r.world,x,80,8)==0,
                  "soul-sand callback spawns the exact drop-path entity");
            for(int tick=0;tick<9;++tick)
                gm_runtime_tick(&r,idle);
            CHECK(r.falling_block_count==1 &&
                  r.falling_blocks[0].fall_time==10 &&
                  fabs(r.falling_blocks[0].y-
                       77.93686484559572)<1e-13 &&
                  r.entities.n_active==0,
                  "soul-sand sand follows Java through fallTime 10");
            gm_runtime_tick(&r,idle);
            CHECK(r.falling_block_count==0 &&
                  gm_world_block(r.world,x,80,8)==0 &&
                  gm_world_block(r.world,x,77,8)==88 &&
                  gm_world_meta(r.world,x,77,8)==0 &&
                  gm_runtime_scheduled_tick_count(&r)==0,
                  "failed soul-sand placement retires sand without replacement");
            CHECK(r.entities.n_active==1 &&
                  r.entities.ents[0].active &&
                  r.entities.ents[0].eid==500201 &&
                  r.entities.ents[0].item==12 &&
                  r.entities.ents[0].count==1 &&
                  r.entities.ents[0].meta==0 &&
                  r.entities.ents[0].age==1 &&
                  r.entities.ents[0].pickup_delay==9 &&
                  r.entities.ents[0].health==5,
                  "soul-sand placement failure creates one exact sand item");
            CHECK(fabs(r.entities.ents[0].x-
                       28.512723377905786)<1e-14 &&
                  fabs(r.entities.ents[0].y-
                       78.0350000038743)<1e-13 &&
                  fabs(r.entities.ents[0].z-
                       8.565416760742664)<1e-14 &&
                  fabs(r.entities.ents[0].mx-
                       0.012468910590349491)<1e-15 &&
                  fabs(r.entities.ents[0].my-
                       0.15680000684857376)<1e-15 &&
                  fabs(r.entities.ents[0].mz-
                       0.06410842677553674)<1e-15 &&
                  r.entities.ents[0].yaw==346.55627f,
                  "soul-sand item motion and yaw match its first Java tick");
            CHECK(r.next_entity_id==500202 &&
                  r.math_random_seed48==UINT64_C(52327523130500) &&
                  r.world_random_seed48==world_seed,
                  "soul-sand drop consumes two ids and four Math.random calls only");
        }

        {
            const int x=29;
            const uint64_t math_seed=UINT64_C(0x123456789ABC);
            const uint64_t world_seed=r.world_random_seed48;
            long long base=r.clock.total_time;
            memset(&r.entities,0,sizeof r.entities);
            CHECK(gm_runtime_load_block(&r,x,77,8,116,0) &&
                  gm_runtime_load_block(&r,x,78,8,0,0) &&
                  gm_runtime_load_block(&r,x,79,8,0,0) &&
                  gm_runtime_load_block(&r,x,80,8,12,0),
                  "enchanting-table falling-drop column loads cold");
            CHECK(gm_runtime_set_entity_id_cursor(&r,500300) &&
                  gm_runtime_set_math_random_seed48(&r,math_seed) &&
                  gm_runtime_schedule_tick(
                      &r,x,80,8,12,base+3,0,11),
                  "enchanting-table callback restores exact event cursors");
            gm_runtime_tick(&r,idle);
            gm_runtime_tick(&r,idle);
            CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
                  r.falling_block_count==0,
                  "enchanting-table sand remains pending before its due tick");
            gm_runtime_tick(&r,idle);
            CHECK(gm_runtime_scheduled_tick_count(&r)==0 &&
                  r.falling_block_count==1 &&
                  r.falling_blocks[0].eid==500300 &&
                  r.falling_blocks[0].drop_on_land==1 &&
                  r.falling_blocks[0].landing_y==77.75 &&
                  r.falling_blocks[0].fall_time==1 &&
                  gm_world_block(r.world,x,80,8)==0,
                  "enchanting-table callback spawns the exact drop-path entity");
            for(int tick=0;tick<9;++tick)
                gm_runtime_tick(&r,idle);
            CHECK(r.falling_block_count==1 &&
                  r.falling_blocks[0].fall_time==10 &&
                  fabs(r.falling_blocks[0].y-
                       77.93686484559572)<1e-13 &&
                  r.entities.n_active==0,
                  "enchanting-table sand follows Java through fallTime 10");
            gm_runtime_tick(&r,idle);
            CHECK(r.falling_block_count==0 &&
                  gm_world_block(r.world,x,80,8)==0 &&
                  gm_world_block(r.world,x,77,8)==116 &&
                  gm_world_meta(r.world,x,77,8)==0 &&
                  gm_runtime_scheduled_tick_count(&r)==0,
                  "failed enchanting-table placement retires sand without replacement");
            CHECK(r.entities.n_active==1 &&
                  r.entities.ents[0].active &&
                  r.entities.ents[0].eid==500301 &&
                  r.entities.ents[0].item==12 &&
                  r.entities.ents[0].count==1 &&
                  r.entities.ents[0].meta==0 &&
                  r.entities.ents[0].age==1 &&
                  r.entities.ents[0].pickup_delay==9 &&
                  r.entities.ents[0].health==5,
                  "enchanting-table failure creates one exact sand item");
            CHECK(fabs(r.entities.ents[0].x-
                       29.512723377905786)<1e-14 &&
                  fabs(r.entities.ents[0].y-
                       77.9100000038743)<1e-13 &&
                  fabs(r.entities.ents[0].z-
                       8.565416760742664)<1e-14 &&
                  fabs(r.entities.ents[0].mx-
                       0.012468910590349491)<1e-15 &&
                  fabs(r.entities.ents[0].my-
                       0.15680000684857376)<1e-15 &&
                  fabs(r.entities.ents[0].mz-
                       0.06410842677553674)<1e-15 &&
                  r.entities.ents[0].yaw==346.55627f,
                  "enchanting-table item motion and yaw match its first Java tick");
            CHECK(r.next_entity_id==500302 &&
                  r.math_random_seed48==UINT64_C(52327523130500) &&
                  r.world_random_seed48==world_seed,
                  "enchanting-table drop consumes two ids and four Math.random calls only");
        }

        {
            const int x=30;
            const uint64_t math_seed=UINT64_C(0x123456789ABC);
            const uint64_t world_seed=r.world_random_seed48;
            long long base=r.clock.total_time;
            memset(&r.entities,0,sizeof r.entities);
            CHECK(gm_runtime_load_block(&r,x,76,8,1,0) &&
                  gm_runtime_load_block(&r,x,77,8,171,0) &&
                  gm_runtime_load_block(&r,x,78,8,0,0) &&
                  gm_runtime_load_block(&r,x,79,8,0,0) &&
                  gm_runtime_load_block(&r,x,80,8,12,0),
                  "supported-carpet falling-drop column loads cold");
            CHECK(gm_runtime_set_entity_id_cursor(&r,500400) &&
                  gm_runtime_set_math_random_seed48(&r,math_seed) &&
                  gm_runtime_schedule_tick(
                      &r,x,80,8,12,base+3,0,12),
                  "carpet sand callback restores exact event cursors");
            gm_runtime_tick(&r,idle);
            gm_runtime_tick(&r,idle);
            CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
                  r.falling_block_count==0,
                  "carpet sand remains pending before its due tick");
            gm_runtime_tick(&r,idle);
            CHECK(gm_runtime_scheduled_tick_count(&r)==0 &&
                  r.falling_block_count==1 &&
                  r.falling_blocks[0].eid==500400 &&
                  r.falling_blocks[0].drop_on_land==1 &&
                  r.falling_blocks[0].landing_y==77.0625 &&
                  r.falling_blocks[0].fall_time==1 &&
                  gm_world_block(r.world,x,80,8)==0,
                  "carpet callback spawns the exact drop-path entity");
            for(int tick=0;tick<11;++tick)
                gm_runtime_tick(&r,idle);
            CHECK(r.falling_block_count==1 &&
                  r.falling_blocks[0].fall_time==12 &&
                  fabs(r.falling_blocks[0].y-
                       77.10776093180491)<1e-13 &&
                  r.entities.n_active==0,
                  "carpet sand follows Java through fallTime 12");
            gm_runtime_tick(&r,idle);
            CHECK(r.falling_block_count==0 &&
                  gm_world_block(r.world,x,80,8)==0 &&
                  gm_world_block(r.world,x,77,8)==171 &&
                  gm_world_meta(r.world,x,77,8)==0 &&
                  gm_world_block(r.world,x,76,8)==1 &&
                  gm_runtime_scheduled_tick_count(&r)==0,
                  "failed carpet placement retires sand without replacement");
            CHECK(r.entities.n_active==1 &&
                  r.entities.ents[0].active &&
                  r.entities.ents[0].eid==500401 &&
                  r.entities.ents[0].item==12 &&
                  r.entities.ents[0].count==1 &&
                  r.entities.ents[0].meta==0 &&
                  r.entities.ents[0].age==1 &&
                  r.entities.ents[0].pickup_delay==9 &&
                  r.entities.ents[0].health==5,
                  "carpet placement failure creates one exact sand item");
            CHECK(fabs(r.entities.ents[0].x-
                       30.512723377905786)<1e-14 &&
                  fabs(r.entities.ents[0].y-
                       77.2225000038743)<1e-13 &&
                  fabs(r.entities.ents[0].z-
                       8.565416760742664)<1e-14 &&
                  fabs(r.entities.ents[0].mx-
                       0.012468910590349491)<1e-15 &&
                  fabs(r.entities.ents[0].my-
                       0.15680000684857376)<1e-15 &&
                  fabs(r.entities.ents[0].mz-
                       0.06410842677553674)<1e-15 &&
                  r.entities.ents[0].yaw==346.55627f,
                  "carpet item motion and yaw match its first Java tick");
            CHECK(r.next_entity_id==500402 &&
                  r.math_random_seed48==UINT64_C(52327523130500) &&
                  r.world_random_seed48==world_seed,
                  "carpet drop consumes two ids and four Math.random calls only");
        }

        {
            const int x=31;
            const uint64_t math_seed=UINT64_C(0x123456789ABC);
            const uint64_t world_seed=r.world_random_seed48;
            long long base=r.clock.total_time;
            memset(&r.entities,0,sizeof r.entities);
            CHECK(gm_runtime_load_block(&r,x,76,8,1,0) &&
                  gm_runtime_load_block(&r,x,77,8,78,0) &&
                  gm_runtime_load_block(&r,x,78,8,0,0) &&
                  gm_runtime_load_block(&r,x,79,8,0,0) &&
                  gm_runtime_load_block(&r,x,80,8,12,0),
                  "one-layer-snow falling column loads with valid support");
            CHECK(gm_runtime_set_entity_id_cursor(&r,500500) &&
                  gm_runtime_set_math_random_seed48(&r,math_seed) &&
                  gm_runtime_schedule_tick(
                      &r,x,80,8,12,base+3,0,13),
                  "one-layer-snow sand callback restores exactly");
            gm_runtime_tick(&r,idle);
            gm_runtime_tick(&r,idle);
            CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
                  r.falling_block_count==0,
                  "one-layer-snow sand remains pending before due");
            gm_runtime_tick(&r,idle);
            CHECK(gm_runtime_scheduled_tick_count(&r)==0 &&
                  r.falling_block_count==1 &&
                  r.falling_blocks[0].eid==500500 &&
                  !r.falling_blocks[0].drop_on_land &&
                  r.falling_blocks[0].landing_y==77.0 &&
                  r.falling_blocks[0].fall_time==1 &&
                  gm_world_block(r.world,x,80,8)==0,
                  "one-layer snow exposes its zero-height collision plane");
            for(int tick=0;tick<11;++tick)
                gm_runtime_tick(&r,idle);
            CHECK(r.falling_block_count==1 &&
                  r.falling_blocks[0].fall_time==12 &&
                  fabs(r.falling_blocks[0].y-
                       77.10776093180491)<1e-13 &&
                  r.entities.n_active==0,
                  "one-layer-snow sand follows Java through fallTime 12");
            gm_runtime_tick(&r,idle);
            CHECK(r.falling_block_count==0 &&
                  gm_world_block(r.world,x,80,8)==0 &&
                  gm_world_block(r.world,x,77,8)==12 &&
                  gm_world_meta(r.world,x,77,8)==0 &&
                  gm_world_block(r.world,x,76,8)==1 &&
                  r.entities.n_active==0 &&
                  gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                  pending.block==12&&pending.x==x&&pending.y==77&&
                  pending.z==8&&pending.time==base+17,
                  "sand replaces one-layer snow and queues exact stability");
            gm_runtime_tick(&r,idle);
            gm_runtime_tick(&r,idle);
            CHECK(gm_runtime_scheduled_tick_count(&r)==0 &&
                  gm_world_block(r.world,x,77,8)==12 &&
                  r.next_entity_id==500501 &&
                  r.math_random_seed48==math_seed &&
                  r.world_random_seed48==world_seed,
                  "snow replacement drains without item or cursor draws");
        }

        {
            const int x=32;
            const uint64_t math_seed=UINT64_C(0x123456789ABC);
            const uint64_t world_seed=r.world_random_seed48;
            long long base=r.clock.total_time;
            memset(&r.entities,0,sizeof r.entities);
            CHECK(gm_runtime_load_block(&r,x,76,8,1,0) &&
                  gm_runtime_load_block(&r,x,77,8,78,7) &&
                  gm_runtime_load_block(&r,x,78,8,0,0) &&
                  gm_runtime_load_block(&r,x,79,8,0,0) &&
                  gm_runtime_load_block(&r,x,80,8,12,0),
                  "eight-layer-snow falling-drop column loads cold");
            CHECK(gm_runtime_set_entity_id_cursor(&r,500600) &&
                  gm_runtime_set_math_random_seed48(&r,math_seed) &&
                  gm_runtime_schedule_tick(
                      &r,x,80,8,12,base+3,0,14),
                  "eight-layer-snow callback restores exact event cursors");
            gm_runtime_tick(&r,idle);
            gm_runtime_tick(&r,idle);
            CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
                  r.falling_block_count==0,
                  "eight-layer-snow sand remains pending before due");
            gm_runtime_tick(&r,idle);
            CHECK(gm_runtime_scheduled_tick_count(&r)==0 &&
                  r.falling_block_count==1 &&
                  r.falling_blocks[0].eid==500600 &&
                  r.falling_blocks[0].drop_on_land==1 &&
                  r.falling_blocks[0].landing_y==77.875 &&
                  r.falling_blocks[0].fall_time==1 &&
                  gm_world_block(r.world,x,80,8)==0,
                  "eight-layer-snow callback spawns exact drop-path entity");
            for(int tick=0;tick<9;++tick)
                gm_runtime_tick(&r,idle);
            CHECK(r.falling_block_count==1 &&
                  r.falling_blocks[0].fall_time==10 &&
                  fabs(r.falling_blocks[0].y-
                       77.93686484559572)<1e-13 &&
                  r.entities.n_active==0,
                  "eight-layer-snow sand follows Java through fallTime 10");
            gm_runtime_tick(&r,idle);
            CHECK(r.falling_block_count==0 &&
                  gm_world_block(r.world,x,80,8)==0 &&
                  gm_world_block(r.world,x,77,8)==78 &&
                  gm_world_meta(r.world,x,77,8)==7 &&
                  gm_world_block(r.world,x,76,8)==1 &&
                  gm_runtime_scheduled_tick_count(&r)==0,
                  "failed eight-layer-snow placement leaves snow unchanged");
            CHECK(r.entities.n_active==1 &&
                  r.entities.ents[0].active &&
                  r.entities.ents[0].eid==500601 &&
                  r.entities.ents[0].item==12 &&
                  r.entities.ents[0].count==1 &&
                  r.entities.ents[0].meta==0 &&
                  r.entities.ents[0].age==1 &&
                  r.entities.ents[0].pickup_delay==9 &&
                  r.entities.ents[0].health==5,
                  "eight-layer-snow failure creates one exact sand item");
            CHECK(fabs(r.entities.ents[0].x-
                       32.512723377905786)<1e-14 &&
                  fabs(r.entities.ents[0].y-
                       78.0350000038743)<1e-13 &&
                  fabs(r.entities.ents[0].z-
                       8.565416760742664)<1e-14 &&
                  fabs(r.entities.ents[0].mx-
                       0.012468910590349491)<1e-15 &&
                  fabs(r.entities.ents[0].my-
                       0.15680000684857376)<1e-15 &&
                  fabs(r.entities.ents[0].mz-
                       0.06410842677553674)<1e-15 &&
                  r.entities.ents[0].yaw==346.55627f,
                  "eight-layer-snow item motion matches its first Java tick");
            CHECK(r.next_entity_id==500602 &&
                  r.math_random_seed48==UINT64_C(52327523130500) &&
                  r.world_random_seed48==world_seed,
                  "eight-layer-snow drop consumes exact event cursors");
        }

        {
            static const int support_id[3]={60,60,92};
            static const int support_meta[3]={0,7,0};
            static const int drop_step[3]={10,10,12};
            static const double landing_y[3]={77.9375,77.9375,77.5};
            static const double active_y[3]={
                78.30271925449955,78.30271925449955,77.53832751878592};
            const uint64_t math_seed=UINT64_C(0x123456789ABC);
            for(int config=0;config<3;++config){
                int x=33+config;
                int eid=500700+config*100;
                const uint64_t world_seed=r.world_random_seed48;
                long long base=r.clock.total_time;
                memset(&r.entities,0,sizeof r.entities);
                CHECK(gm_runtime_load_block(&r,x,76,8,1,0) &&
                      gm_runtime_load_block(
                          &r,x,77,8,support_id[config],support_meta[config]) &&
                      gm_runtime_load_block(&r,x,78,8,0,0) &&
                      gm_runtime_load_block(&r,x,79,8,0,0) &&
                      gm_runtime_load_block(&r,x,80,8,12,0),
                      "farmland/cake falling-drop column loads cold");
                CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                      gm_runtime_set_math_random_seed48(&r,math_seed) &&
                      gm_runtime_schedule_tick(
                          &r,x,80,8,12,base+3,0,15+config),
                      "farmland/cake callback restores exact event cursors");
                gm_runtime_tick(&r,idle);
                gm_runtime_tick(&r,idle);
                CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
                      r.falling_block_count==0,
                      "farmland/cake sand remains pending before due");
                gm_runtime_tick(&r,idle);
                CHECK(gm_runtime_scheduled_tick_count(&r)==0 &&
                      r.falling_block_count==1 &&
                      r.falling_blocks[0].eid==eid &&
                      r.falling_blocks[0].drop_on_land==1 &&
                      r.falling_blocks[0].landing_y==landing_y[config] &&
                      r.falling_blocks[0].fall_time==1 &&
                      gm_world_block(r.world,x,80,8)==0,
                      "farmland/cake callback spawns exact drop-path entity");
                for(int tick=0;tick<drop_step[config]-2;++tick)
                    gm_runtime_tick(&r,idle);
                CHECK(r.falling_block_count==1 &&
                      r.falling_blocks[0].fall_time==drop_step[config]-1 &&
                      fabs(r.falling_blocks[0].y-active_y[config])<1e-13 &&
                      r.entities.n_active==0,
                      "farmland/cake sand keeps exact pre-drop trajectory");
                gm_runtime_tick(&r,idle);
                CHECK(r.falling_block_count==0 &&
                      gm_world_block(r.world,x,80,8)==0 &&
                      gm_world_block(r.world,x,77,8)==support_id[config] &&
                      gm_world_meta(r.world,x,77,8)==support_meta[config] &&
                      gm_world_block(r.world,x,76,8)==1 &&
                      gm_runtime_scheduled_tick_count(&r)==0,
                      "failed farmland/cake placement leaves support unchanged");
                CHECK(r.entities.n_active==1 &&
                      r.entities.ents[0].active &&
                      r.entities.ents[0].eid==eid+1 &&
                      r.entities.ents[0].item==12 &&
                      r.entities.ents[0].count==1 &&
                      r.entities.ents[0].meta==0 &&
                      r.entities.ents[0].age==1 &&
                      r.entities.ents[0].pickup_delay==9 &&
                      r.entities.ents[0].health==5,
                      "farmland/cake failure creates one exact sand item");
                CHECK(fabs(r.entities.ents[0].x-
                           ((double)x+0.512723377905786))<1e-14 &&
                      fabs(r.entities.ents[0].y-
                           (landing_y[config]+0.16000000387430191))<1e-13 &&
                      fabs(r.entities.ents[0].z-
                           8.565416760742664)<1e-14 &&
                      fabs(r.entities.ents[0].mx-
                           0.012468910590349491)<1e-15 &&
                      fabs(r.entities.ents[0].my-
                           0.15680000684857376)<1e-15 &&
                      fabs(r.entities.ents[0].mz-
                           0.06410842677553674)<1e-15 &&
                      r.entities.ents[0].yaw==346.55627f,
                      "farmland/cake item motion matches first Java tick");
                CHECK(r.next_entity_id==eid+2 &&
                      r.math_random_seed48==UINT64_C(52327523130500) &&
                      r.world_random_seed48==world_seed,
                      "farmland/cake drops consume exact event cursors");
            }
        }

        {
            const int x=36;
            const uint64_t math_seed=UINT64_C(0x123456789ABC);
            const uint64_t world_seed=r.world_random_seed48;
            long long base=r.clock.total_time;
            memset(&r.entities,0,sizeof r.entities);
            CHECK(r.do_entity_drops==1 &&
                  gm_runtime_set_do_entity_drops(&r,0) &&
                  !gm_runtime_set_do_entity_drops(&r,2) &&
                  r.do_entity_drops==0,
                  "doEntityDrops defaults true and accepts only boolean state");
            CHECK(r.do_mob_loot==1 &&
                  gm_runtime_set_do_mob_loot(&r,0) &&
                  !gm_runtime_set_do_mob_loot(&r,2) &&
                  r.do_mob_loot==0 &&
                  gm_runtime_set_do_mob_loot(&r,1),
                  "doMobLoot defaults true and accepts only boolean state");
            CHECK(gm_runtime_load_block(&r,x,77,8,44,0) &&
                  gm_runtime_load_block(&r,x,78,8,0,0) &&
                  gm_runtime_load_block(&r,x,79,8,0,0) &&
                  gm_runtime_load_block(&r,x,80,8,12,0) &&
                  gm_runtime_set_entity_id_cursor(&r,501000) &&
                  gm_runtime_set_math_random_seed48(&r,math_seed) &&
                  gm_runtime_schedule_tick(
                      &r,x,80,8,12,base+3,0,18),
                  "disabled entity-drop falling fixture loads exactly");
            gm_runtime_tick(&r,idle);
            gm_runtime_tick(&r,idle);
            gm_runtime_tick(&r,idle);
            CHECK(r.falling_block_count==1 &&
                  r.falling_blocks[0].eid==501000 &&
                  r.falling_blocks[0].drop_on_land==1 &&
                  r.falling_blocks[0].landing_y==77.5 &&
                  r.falling_blocks[0].fall_time==1 &&
                  gm_world_block(r.world,x,80,8)==0,
                  "disabled-drop callback still creates the falling entity");
            for(int tick=0;tick<10;++tick)
                gm_runtime_tick(&r,idle);
            CHECK(r.falling_block_count==1 &&
                  r.falling_blocks[0].fall_time==11 &&
                  fabs(r.falling_blocks[0].y-
                       77.53832751878592)<1e-13 &&
                  r.entities.n_active==0,
                  "disabled-drop sand keeps the exact pre-drop trajectory");
            gm_runtime_tick(&r,idle);
            CHECK(r.falling_block_count==0 &&
                  r.entities.n_active==0 &&
                  gm_world_block(r.world,x,80,8)==0 &&
                  gm_world_block(r.world,x,77,8)==44 &&
                  gm_world_meta(r.world,x,77,8)==0 &&
                  gm_runtime_scheduled_tick_count(&r)==0 &&
                  r.next_entity_id==501001 &&
                  r.math_random_seed48==math_seed &&
                  r.world_random_seed48==world_seed,
                  "doEntityDrops false retires failed placement without item "
                  "or cursor draws");
            CHECK(gm_runtime_set_do_entity_drops(&r,1),
                  "falling regression restores the default entity-drop rule");
        }

        {
            const uint64_t math_seed=UINT64_C(0x123456789ABC);
            const double constructor_offset=
                (double)((1.0f-0.98f)/2.0f);
            for(int timeout_case=0;timeout_case<6;++timeout_case){
                const int timeout_mode=timeout_case/2;
                const int low=timeout_mode==1;
                const int age=timeout_mode==2;
                const int entity_drops=(timeout_case&1)==0;
                const int eid=502000+timeout_case*10;
                const double start_y=(age?128.0:(low?1.0:258.0))
                    +constructor_offset;
                const double expected_y=age?128.00999999046326:
                    (low?0.9699999913573265:257.96999999135733);
                const int snapshot_y=age?128:(low?1:250);
                int low_block[3]={0,0,0};
                int low_meta[3]={0,0,0};
                if(low)
                    for(int clear_y=0;clear_y<=2;++clear_y){
                        low_block[clear_y]=
                            gm_world_block(r.world,40,clear_y,8);
                        low_meta[clear_y]=
                            gm_world_meta(r.world,40,clear_y,8);
                        gm_world_set_block_meta(
                            r.world,40,clear_y,8,0,0);
                    }
                const int before_block=
                    gm_world_block(r.world,40,snapshot_y,8);
                const int before_meta=
                    gm_world_meta(r.world,40,snapshot_y,8);
                const uint64_t world_seed=r.world_random_seed48;
                memset(&r.entities,0,sizeof r.entities);
                memset(r.falling_blocks,0,sizeof r.falling_blocks);
                r.falling_block_count=0;
                CHECK(gm_runtime_set_do_entity_drops(&r,entity_drops) &&
                      gm_runtime_set_entity_id_cursor(&r,eid+1) &&
                      gm_runtime_set_math_random_seed48(&r,math_seed) &&
                      gm_runtime_spawn_falling_fixture(
                          &r,eid,12,0,age?600:100,
                          40.5,start_y,8.5,0.0,0.0,0.0,age,1),
                      "falling timeout fixture restores exact hidden state");
                CHECK(r.falling_block_count==1 &&
                      r.falling_blocks[0].active &&
                      r.falling_blocks[0].eid==eid &&
                      r.falling_blocks[0].fall_time==(age?600:100) &&
                      r.falling_blocks[0].should_drop_item==1 &&
                      r.falling_blocks[0].no_gravity==age &&
                      r.falling_blocks[0].no_ground==1 &&
                      r.next_entity_id==eid+1,
                      "falling timeout fixture does not consume event cursors");
                gm_runtime_tick(&r,idle);
                CHECK(r.falling_block_count==0 &&
                      !r.falling_blocks[0].active &&
                      r.falling_blocks[0].fall_time==(age?601:101) &&
                      fabs(r.falling_blocks[0].x-40.5)<1e-15 &&
                      fabs(r.falling_blocks[0].y-expected_y)<1e-15 &&
                      fabs(r.falling_blocks[0].z-8.5)<1e-15 &&
                      fabs(r.falling_blocks[0].vx)<1e-15 &&
                      fabs(r.falling_blocks[0].vy-
                           (age?0.0:-0.039199999886751158))<1e-15 &&
                      fabs(r.falling_blocks[0].vz)<1e-15,
                      "falling timeout retires at the exact Java cutoff state");
                CHECK(gm_world_block(r.world,40,snapshot_y,8)==before_block &&
                      gm_world_meta(r.world,40,snapshot_y,8)==before_meta &&
                      r.world_random_seed48==world_seed,
                      "falling timeout changes no blocks or World.rand state");
                if(entity_drops){
                    CHECK(r.entities.n_active==1 &&
                          r.entities.ents[0].active &&
                          r.entities.ents[0].eid==eid+1 &&
                          r.entities.ents[0].item==12 &&
                          r.entities.ents[0].count==1 &&
                          r.entities.ents[0].meta==0 &&
                          r.entities.ents[0].age==1 &&
                          r.entities.ents[0].pickup_delay==9 &&
                          r.entities.ents[0].health==5,
                          "enabled falling timeout creates one exact sand item");
                    CHECK(fabs(r.entities.ents[0].x-
                               40.512723377905786)<1e-14 &&
                          fabs(r.entities.ents[0].y-
                               (age?128.16999999433756:(low?
                                    1.1299999952316284:
                                    258.12999999523163)))<1e-13 &&
                          fabs(r.entities.ents[0].z-
                               8.565416760742664)<1e-14 &&
                          fabs(r.entities.ents[0].mx-
                               0.012468910590349491)<1e-15 &&
                          fabs(r.entities.ents[0].my-
                               0.15680000684857376)<1e-15 &&
                          fabs(r.entities.ents[0].mz-
                               0.06410842677553674)<1e-15 &&
                          r.entities.ents[0].yaw==346.55627f &&
                          r.next_entity_id==eid+2 &&
                          r.math_random_seed48==UINT64_C(52327523130500),
                          "falling timeout item tick and cursors match Java");
                }else{
                    CHECK(r.entities.n_active==0 &&
                          r.next_entity_id==eid+1 &&
                          r.math_random_seed48==math_seed,
                          "disabled timeout drop consumes no item cursor state");
                }
                if(low)
                    for(int restore_y=0;restore_y<=2;++restore_y)
                        gm_world_set_block_meta(
                            r.world,40,restore_y,8,
                            low_block[restore_y],low_meta[restore_y]);
            }
            memset(&r.entities,0,sizeof r.entities);
            for(int slot=0;slot<GM_LIVE_MAX;++slot){
                r.entities.ents[slot].active=1;
                r.entities.ents[slot].eid=600000+slot;
                r.entities.ents[slot].x=100.5;
                r.entities.ents[slot].y=300.0;
                r.entities.ents[slot].z=100.5;
                r.entities.ents[slot].item=1;
                r.entities.ents[slot].count=1;
                r.entities.ents[slot].health=5;
                r.entities.ents[slot].controlled_stationary=1;
            }
            r.entities.n_active=GM_LIVE_MAX;
            memset(r.falling_blocks,0,sizeof r.falling_blocks);
            r.falling_block_count=0;
            CHECK(gm_runtime_set_do_entity_drops(&r,1) &&
                  gm_runtime_set_entity_id_cursor(&r,502091) &&
                  gm_runtime_set_math_random_seed48(&r,math_seed) &&
                  gm_runtime_spawn_falling_fixture(
                      &r,502090,12,0,600,
                      40.5,128.0+constructor_offset,8.5,
                      0.0,0.0,0.0,1,1),
                  "full-item-pool timeout fixture loads exactly");
            gm_runtime_tick(&r,idle);
            CHECK(r.falling_block_count==0 &&
                  !r.falling_blocks[0].active &&
                  r.falling_blocks[0].fall_time==601 &&
                  r.entities.n_active==GM_LIVE_MAX &&
                  r.next_entity_id==502091 &&
                  r.math_random_seed48==math_seed,
                  "full item pool retires timeout instead of retrying forever");
            memset(&r.entities,0,sizeof r.entities);
            CHECK(!gm_runtime_spawn_falling_fixture(
                      &r,502100,12,0,601,
                      40.5,128.0,8.5,0.0,0.0,0.0,1,1),
                  "falling timeout fixture rejects invalid hidden state");
        }

        {
            const int origin_x=40,origin_z=8,base_y=220;
            const uint64_t math_seed=UINT64_C(0x123456789ABC);
            const uint64_t world_seed=UINT64_C(0x23456789ABCD);
            const double constructor_y=(double)base_y+
                (double)((1.0f-0.98f)/2.0f);
            for(int lateral_case=0;lateral_case<2;++lateral_case){
                const int wall=lateral_case==1;
                const int eid=503000+lateral_case*10;
                memset(&r.entities,0,sizeof r.entities);
                memset(r.falling_blocks,0,sizeof r.falling_blocks);
                r.falling_block_count=0;
                r.scheduled_tick_count=0;
                r.scheduled_tick_next_order=0;
                for(int y=base_y-5;y<=base_y+1;++y)
                    for(int z=origin_z-2;z<=origin_z+2;++z)
                        for(int x=origin_x-2;x<=origin_x+9;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                for(int x=origin_x-2;x<=origin_x+9;++x)
                    for(int z=origin_z-2;z<=origin_z+2;++z)
                        gm_world_set_block_meta(
                            r.world,x,base_y-4,z,1,0);
                if(wall)
                    for(int y=base_y-1;y<=base_y+1;++y)
                        gm_world_set_block_meta(
                            r.world,origin_x+1,y,origin_z,1,0);
                gm_world_set_block_meta(
                    r.world,origin_x,base_y,origin_z,12,0);
                CHECK(gm_runtime_set_entity_id_cursor(&r,eid+1) &&
                      gm_runtime_set_math_random_seed48(&r,math_seed) &&
                      gm_runtime_set_world_random_seed48(&r,world_seed) &&
                      gm_runtime_schedule_tick(
                          &r,origin_x+9,base_y-4,origin_z+2,1,
                          r.clock.total_time+15,0,0) &&
                      gm_runtime_spawn_falling_fixture(
                          &r,eid,12,0,0,
                          origin_x+0.5,constructor_y,origin_z+0.5,
                          wall?0.75:0.35,0.0,wall?0.0:0.15,0,0),
                      "lateral falling fixture restores exact moving state");
                gm_runtime_tick(&r,idle);
                CHECK(r.falling_block_count==1 &&
                      r.falling_blocks[0].fall_time==1 &&
                      !r.falling_blocks[0].on_ground &&
                      !r.falling_blocks[0].collided_vertically &&
                      r.falling_blocks[0].collided_horizontally==wall &&
                      r.falling_blocks[0].fall_distance==0.04f &&
                      fabs(r.falling_blocks[0].x-(wall?
                          origin_x+0.5099999904632568:
                          origin_x+0.85))<1e-13 &&
                      fabs(r.falling_blocks[0].z-(wall?
                          origin_z+0.5:origin_z+0.65))<1e-13,
                      "lateral falling first move resolves exact wall flags");
                for(int tick=2;tick<=12;++tick)
                    gm_runtime_tick(&r,idle);
                CHECK(r.falling_block_count==1 &&
                      r.falling_blocks[0].fall_time==12 &&
                      fabs(r.falling_blocks[0].y-
                           217.1077609318049)<1e-13 &&
                      !r.falling_blocks[0].on_ground,
                      "lateral falling retains the exact airborne prefix");
                gm_runtime_tick(&r,idle);
                CHECK(r.falling_block_count==0 &&
                      !r.falling_blocks[0].active &&
                      r.falling_blocks[0].fall_time==13 &&
                      r.falling_blocks[0].on_ground &&
                      !r.falling_blocks[0].collided_horizontally &&
                      r.falling_blocks[0].collided_vertically &&
                      r.falling_blocks[0].fall_distance==0.0f &&
                      fabs(r.falling_blocks[0].y-217.0)<1e-15 &&
                      fabs(r.falling_blocks[0].x-(wall?
                          origin_x+0.5099999904632568:
                          origin_x+4.542108637745933))<1e-13 &&
                      fabs(r.falling_blocks[0].z-(wall?
                          origin_z+0.5:
                          origin_z+2.232332273319686))<1e-13,
                      "lateral falling lands in the exact dynamic cell");
                int landing_x=wall?origin_x:origin_x+4;
                int landing_z=wall?origin_z:origin_z+2;
                GmRuntimeScheduledTick landing_pending;
                CHECK(gm_world_block(
                          r.world,origin_x,base_y,origin_z)==0 &&
                      gm_world_block(
                          r.world,landing_x,base_y-3,landing_z)==12 &&
                      gm_runtime_scheduled_tick_count(&r)==2 &&
                      gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                      pending.x==origin_x+9&&pending.y==base_y-4&&
                      pending.z==origin_z+2&&pending.block==1&&
                      pending.time==r.clock.total_time+2&&
                      pending.priority==0&&pending.order==0 &&
                      gm_runtime_scheduled_tick_get(
                          &r,1,&landing_pending) &&
                      landing_pending.x==landing_x&&
                      landing_pending.y==base_y-3&&
                      landing_pending.z==landing_z&&
                      landing_pending.block==12&&
                      landing_pending.time==r.clock.total_time+2&&
                      landing_pending.priority==0&&
                      landing_pending.order==1 &&
                      r.entities.n_active==0 &&
                      r.next_entity_id==eid+1 &&
                      r.math_random_seed48==math_seed &&
                      r.world_random_seed48==world_seed,
                      "lateral landing changes only destination and callback");
            }
            r.scheduled_tick_count=0;
            r.scheduled_tick_next_order=0;
            for(int y=base_y-5;y<=base_y+1;++y)
                for(int z=origin_z-2;z<=origin_z+2;++z)
                    for(int x=origin_x-2;x<=origin_x+9;++x)
                        gm_world_set_block_meta(r.world,x,y,z,0,0);
        }

        {
            const int origin_x=56,origin_z=20,base_y=220;
            const uint64_t math_seed=UINT64_C(0x123456789ABC);
            const uint64_t world_seed=UINT64_C(0x23456789ABCD);
            GmRuntimeScheduledTick egg_pending;
            long long start;
            memset(&r.entities,0,sizeof r.entities);
            memset(r.falling_blocks,0,sizeof r.falling_blocks);
            r.falling_block_count=0;
            memset(r.world_events,0,sizeof r.world_events);
            r.world_event_head=0;
            r.world_event_count=0;
            r.world_event_next_seq=0;
            r.world_event_dropped=0;
            r.scheduled_tick_count=0;
            r.scheduled_tick_next_order=0;
            for(int y=base_y-5;y<=base_y+1;++y)
                for(int z=origin_z-2;z<=origin_z+2;++z)
                    for(int x=origin_x-2;x<=origin_x+2;++x)
                        gm_world_set_block_meta(r.world,x,y,z,0,0);
            for(int x=origin_x-2;x<=origin_x+2;++x)
                for(int z=origin_z-2;z<=origin_z+2;++z)
                    gm_world_set_block_meta(r.world,x,base_y-4,z,1,0);
            gm_world_set_block_meta(
                r.world,origin_x,base_y-1,origin_z,1,0);
            start=r.clock.total_time;
            CHECK(gm_runtime_set_entity_id_cursor(&r,520000) &&
                  gm_runtime_set_math_random_seed48(&r,math_seed) &&
                  gm_runtime_set_world_random_seed48(&r,world_seed) &&
                  gm_runtime_set_block(
                      &r,origin_x,base_y,origin_z,122,0) &&
                  gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&egg_pending) &&
                  egg_pending.block==122&&egg_pending.time==start+5&&
                  egg_pending.priority==0&&egg_pending.order==0,
                  "dragon egg onBlockAdded schedules exact +5 callback");
            CHECK(gm_runtime_schedule_tick(
                      &r,origin_x,base_y,origin_z,122,start+9,0,77) &&
                  gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&egg_pending) &&
                  egg_pending.time==start+5&&egg_pending.order==0,
                  "dragon egg duplicate callback preserves first due time");
            for(int tick=0;tick<4;++tick)gm_runtime_tick(&r,idle);
            CHECK(gm_world_block(
                      r.world,origin_x,base_y,origin_z)==122 &&
                  gm_runtime_scheduled_tick_count(&r)==1 &&
                  r.falling_block_count==0,
                  "supported dragon egg remains pending before +5");
            gm_runtime_tick(&r,idle);
            CHECK(gm_world_block(
                      r.world,origin_x,base_y,origin_z)==122 &&
                  gm_runtime_scheduled_tick_count(&r)==0 &&
                  r.falling_block_count==0 &&
                  r.next_entity_id==520000 &&
                  r.math_random_seed48==math_seed &&
                  r.world_random_seed48==world_seed,
                  "supported dragon egg drains without entity or cursors");

            r.scheduled_tick_count=0;
            r.scheduled_tick_next_order=0;
            for(int y=base_y-5;y<=base_y+1;++y)
                for(int z=origin_z-2;z<=origin_z+2;++z)
                    for(int x=origin_x-2;x<=origin_x+2;++x)
                        gm_world_set_block_meta(r.world,x,y,z,0,0);
            for(int x=origin_x-2;x<=origin_x+2;++x)
                for(int z=origin_z-2;z<=origin_z+2;++z)
                    gm_world_set_block_meta(r.world,x,base_y-4,z,1,0);
            gm_world_set_block_meta(
                r.world,origin_x,base_y-1,origin_z,1,0);
            CHECK(gm_runtime_set_entity_id_cursor(&r,520010) &&
                  gm_runtime_set_math_random_seed48(&r,math_seed) &&
                  gm_runtime_set_world_random_seed48(&r,world_seed) &&
                  gm_runtime_set_block(
                      &r,origin_x,base_y,origin_z,122,0) &&
                  gm_runtime_scheduled_tick_get(&r,0,&egg_pending),
                  "unsupported dragon egg fixture stages through onBlockAdded");
            long long original_due=egg_pending.time;
            CHECK(gm_runtime_set_block(
                      &r,origin_x,base_y-1,origin_z,0,0) &&
                  gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&egg_pending) &&
                  egg_pending.time==original_due&&egg_pending.order==0,
                  "support-loss neighbor callback does not postpone egg");
            for(int tick=0;tick<4;++tick)gm_runtime_tick(&r,idle);
            CHECK(gm_world_block(
                      r.world,origin_x,base_y,origin_z)==122 &&
                  r.falling_block_count==0,
                  "unsupported dragon egg still waits for due callback");
            gm_runtime_tick(&r,idle);
            CHECK(gm_world_block(
                      r.world,origin_x,base_y,origin_z)==0 &&
                  r.falling_block_count==1 &&
                  r.falling_blocks[0].block==122 &&
                  r.falling_blocks[0].fall_time==1 &&
                  r.falling_blocks[0].eid==520010 &&
                  r.next_entity_id==520011,
                  "due dragon egg spawns and removes source on entity tick one");
            for(int tick=2;tick<=13;++tick)gm_runtime_tick(&r,idle);
            CHECK(r.falling_block_count==0 &&
                  r.falling_blocks[0].fall_time==13 &&
                  r.falling_blocks[0].on_ground &&
                  r.falling_blocks[0].collided_vertically &&
                  gm_world_block(
                      r.world,origin_x,base_y-3,origin_z)==122 &&
                  gm_world_meta(
                      r.world,origin_x,base_y-3,origin_z)==0 &&
                  gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&egg_pending) &&
                  egg_pending.x==origin_x&&egg_pending.y==base_y-3&&
                  egg_pending.z==origin_z&&egg_pending.block==122&&
                  egg_pending.time==r.clock.total_time+5&&
                  egg_pending.priority==0&&egg_pending.order==1 &&
                  r.entities.n_active==0 &&
                  r.math_random_seed48==math_seed &&
                  r.world_random_seed48==world_seed,
                  "falling dragon egg lands through setBlockState and queues +5");

            r.scheduled_tick_count=0;
            r.scheduled_tick_next_order=0;
            gm_world_set_block_meta(
                r.world,origin_x,base_y,origin_z,122,1);
            CHECK(!gm_runtime_schedule_tick(
                      &r,origin_x,base_y,origin_z,122,
                      r.clock.total_time+5,0,0),
                  "dragon egg schedule restore rejects noncanonical metadata");
            gm_world_set_block_meta(
                r.world,origin_x,base_y,origin_z,0,0);
            gm_world_set_block_meta(
                r.world,origin_x,base_y-3,origin_z,0,0);
            gm_world_set_block_meta(
                r.world,origin_x,base_y-1,origin_z,1,0);
            CHECK(gm_runtime_set_block(
                      &r,origin_x,base_y,origin_z,122,0) &&
                  gm_runtime_set_block(
                      &r,origin_x,base_y-1,origin_z,0,0) &&
                  gm_runtime_set_entity_id_cursor(&r,520020),
                  "full falling-pool dragon egg fixture stages");
            for(int slot=0;slot<GM_RUNTIME_FALLING_BLOCKS;++slot){
                int fixture_x=origin_x-8+slot;
                int fixture_z=origin_z+8;
                gm_world_set_block_meta(
                    r.world,fixture_x,200,fixture_z,122,0);
                CHECK(gm_world_block(
                          r.world,fixture_x,200,fixture_z)==122 &&
                      gm_runtime_spawn_falling_fixture(
                          &r,600000+slot,122,0,0,
                          fixture_x+0.5,200.0,fixture_z+0.5,
                          0.0,0.0,0.0,1,1),
                      "dragon egg falling pool fills exactly");
            }
            for(int tick=0;tick<5;++tick)gm_runtime_tick(&r,idle);
            CHECK(gm_runtime_scheduled_tick_count(&r)==0 &&
                  gm_world_block(
                      r.world,origin_x,base_y,origin_z)==122 &&
                  r.next_entity_id==520020,
                  "full falling pool drains callback but preserves source and cursor");
            memset(r.falling_blocks,0,sizeof r.falling_blocks);
            r.falling_block_count=0;
            r.scheduled_tick_count=0;
            r.scheduled_tick_next_order=0;
        }

        {
            const int origin_x=62,origin_z=20,base_y=220;
            const uint64_t math_seed=UINT64_C(0x123456789ABC);
            const uint64_t world_seed=UINT64_C(0x23456789ABCD);
            const uint64_t no_damage_seed=UINT64_C(0x23456789ABCD);
            const uint64_t player_seed=UINT64_C(0x6789ABCDEF01);
            const int input_meta[3]={8,0,8};
            const uint64_t entity_seed[3]={
                no_damage_seed,UINT64_C(0),UINT64_C(0)};
            const int landed_block[3]={145,145,0};
            const int landed_meta[3]={8,4,0};
            GmRuntimeScheduledTick pending;

            memset(r.falling_blocks,0,sizeof r.falling_blocks);
            r.falling_block_count=0;
            r.scheduled_tick_count=0;
            r.scheduled_tick_next_order=0;
            for(int y=base_y-5;y<=base_y+1;++y)
                for(int z=origin_z-2;z<=origin_z+2;++z)
                    for(int x=origin_x-2;x<=origin_x+6;++x)
                        gm_world_set_block_meta(r.world,x,y,z,0,0);
            gm_world_set_block_meta(
                r.world,origin_x,base_y-1,origin_z,1,0);
            long long start=r.clock.total_time;
            CHECK(gm_runtime_set_entity_id_cursor(&r,521000) &&
                  gm_runtime_set_math_random_seed48(&r,math_seed) &&
                  gm_runtime_set_world_random_seed48(&r,world_seed) &&
                  gm_runtime_set_next_falling_random_seed48(
                      &r,no_damage_seed) &&
                  gm_runtime_set_block(
                      &r,origin_x,base_y,origin_z,145,8) &&
                  gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                  pending.block==145&&pending.time==start+2&&
                  pending.priority==0&&pending.order==0,
                  "anvil onBlockAdded schedules exact +2 callback");
            gm_runtime_tick(&r,idle);
            gm_runtime_tick(&r,idle);
            CHECK(gm_world_block(
                      r.world,origin_x,base_y,origin_z)==145 &&
                  gm_runtime_scheduled_tick_count(&r)==0 &&
                  r.falling_block_count==0 &&
                  r.next_entity_id==521000 &&
                  r.next_falling_random_valid &&
                  r.next_falling_random_seed48==no_damage_seed &&
                  gm_runtime_world_event_count(&r)==0 &&
                  r.math_random_seed48==math_seed &&
                  r.world_random_seed48==world_seed,
                  "supported anvil drains without entity or RNG cursor");

            for(int which=0;which<3;++which){
                GmRuntimeWorldEvent world_event;
                memset(r.falling_blocks,0,sizeof r.falling_blocks);
                r.falling_block_count=0;
                r.scheduled_tick_count=0;
                r.scheduled_tick_next_order=0;
                r.next_falling_random_valid=0;
                for(int y=base_y-5;y<=base_y+1;++y)
                    for(int z=origin_z-2;z<=origin_z+2;++z)
                        for(int x=origin_x-2;x<=origin_x+2;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                for(int z=origin_z-2;z<=origin_z+2;++z)
                    for(int x=origin_x-2;x<=origin_x+2;++x)
                        gm_world_set_block_meta(
                            r.world,x,base_y-4,z,1,0);
                gm_world_set_block_meta(
                    r.world,origin_x,base_y-1,origin_z,1,0);
                CHECK(gm_runtime_set_entity_id_cursor(&r,521010+which) &&
                      gm_runtime_set_math_random_seed48(&r,math_seed) &&
                      gm_runtime_set_world_random_seed48(&r,world_seed) &&
                      gm_runtime_set_next_falling_random_seed48(
                          &r,entity_seed[which]) &&
                      gm_runtime_set_block(
                          &r,origin_x,base_y,origin_z,
                          145,input_meta[which]) &&
                      gm_runtime_set_block(
                          &r,origin_x,base_y-1,origin_z,0,0) &&
                      gm_runtime_scheduled_tick_count(&r)==1,
                      "falling anvil stages one non-postponed +2 callback");
                gm_runtime_tick(&r,idle);
                gm_runtime_tick(&r,idle);
                CHECK(r.falling_block_count==1 &&
                      r.falling_blocks[0].fall_time==1 &&
                      r.falling_blocks[0].hurt_entities &&
                      r.falling_blocks[0].random_seed48==entity_seed[which],
                      "due anvil spawns with captured Entity.rand cursor");
                for(int tick=2;tick<=13;++tick)
                    gm_runtime_tick(&r,idle);
                CHECK(r.falling_block_count==0 &&
                      !r.falling_blocks[0].active &&
                      r.falling_blocks[0].fall_time==13 &&
                      r.falling_blocks[0].on_ground &&
                      r.falling_blocks[0].collided_vertically &&
                      r.falling_blocks[0].fall_distance==0.0f &&
                      r.falling_blocks[0].impact_fall_distance==
                          2.90223908f &&
                      r.falling_blocks[0].random_seed48==
                          java_lcg_steps(entity_seed[which],1) &&
                      r.falling_blocks[0].dont_set_block==
                          (landed_block[which]==0) &&
                      gm_world_block(
                          r.world,origin_x,base_y-3,origin_z)==
                          landed_block[which] &&
                      gm_world_meta(
                          r.world,origin_x,base_y-3,origin_z)==
                          landed_meta[which] &&
                      gm_runtime_scheduled_tick_count(&r)==
                          (landed_block[which]!=0) &&
                      gm_runtime_world_event_count(&r)==which+1 &&
                      gm_runtime_world_event_get(
                          &r,which,&world_event) &&
                      world_event.seq==(uint64_t)which &&
                      world_event.id==(which==2?1029:1031) &&
                      world_event.dimension==r.dimension &&
                      world_event.x==origin_x &&
                      world_event.y==base_y-3 &&
                      world_event.z==origin_z &&
                      world_event.data==0 &&
                      r.world_event_dropped==0 &&
                      r.next_entity_id==521011+which &&
                      r.math_random_seed48==math_seed &&
                      r.world_random_seed48==world_seed,
                      "anvil impact preserves, damages, or breaks by exact RNG");
                if(landed_block[which]!=0)
                    CHECK(gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                          pending.x==origin_x&&pending.y==base_y-3&&
                          pending.z==origin_z&&pending.block==145&&
                          pending.time==r.clock.total_time+2&&
                          pending.priority==0&&pending.order==1,
                          "landed anvil schedules exact supported +2 callback");
            }
            {
                GmRuntimeWorldEvent ignored;
                CHECK(!gm_runtime_world_event_get(&r,-1,&ignored) &&
                      !gm_runtime_world_event_get(&r,3,&ignored) &&
                      !gm_runtime_world_event_get(NULL,0,&ignored) &&
                      !gm_runtime_world_event_get(&r,0,NULL),
                      "anvil world-event reader rejects invalid indices");
            }

            {
                /* Two sequential NoAI pigs, one cow, one sheep, and one
                 * chicken share the landing cell.
                 * Ascending fresh slots reproduce this bounded Java insertion
                 * order. The sheep consumes its EntityLiving ambient roll on
                 * each ordinary world tick. Impact writes 20/10 for each,
                 * then the living phase ages all five to the public boundary;
                 * the lethal chicken also begins deathTime at one. */
                const int eid=521045,pig_eid=521046,pig_eid2=521047,
                    cow_eid=521048,sheep_eid=521049,chicken_eid=521050;
                const uint64_t pig_seed=UINT64_C(0x3456789ABCDE);
                const uint64_t pig_seed2=UINT64_C(0x456789ABCDEF);
                const uint64_t cow_seed=UINT64_C(0x56789ABCDEF0);
                const uint64_t sheep_seed=UINT64_C(0x789ABCDEF012);
                const uint64_t chicken_seed=UINT64_C(0x89ABCDEF0123);
                const int impact_x=r.ox+8,impact_z=r.oz+8;
                GmMobLive mobs_before=r.mobs;
                const int event_count_before=gm_mobs_event_count(&r.mobs);
                const uint64_t event_seq_before=r.mobs.event_next_seq;
                int controlled_mobs_before=r.controlled_mobs_enabled;
                int do_mob_loot_before=r.do_mob_loot;
                memset(r.falling_blocks,0,sizeof r.falling_blocks);
                r.falling_block_count=0;
                r.scheduled_tick_count=0;
                r.scheduled_tick_next_order=0;
                r.next_falling_random_valid=0;
                for(int y=base_y-5;y<=base_y+1;++y)
                    for(int z=impact_z-2;z<=impact_z+2;++z)
                        for(int x=impact_x-2;x<=impact_x+2;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                for(int z=impact_z-2;z<=impact_z+2;++z)
                    for(int x=impact_x-2;x<=impact_x+2;++x)
                        gm_world_set_block_meta(
                            r.world,x,base_y-4,z,1,0);
                gm_world_set_block_meta(
                    r.world,impact_x,base_y-1,impact_z,1,0);
                CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                      gm_runtime_set_math_random_seed48(&r,math_seed) &&
                      gm_runtime_set_world_random_seed48(&r,world_seed) &&
                      gm_runtime_set_next_falling_random_seed48(
                          &r,no_damage_seed) &&
                      gm_runtime_set_do_mob_loot(&r,0) &&
                      gm_runtime_set_block(
                          &r,impact_x,base_y,impact_z,145,0) &&
                      gm_runtime_set_block(
                          &r,impact_x,base_y-1,impact_z,0,0),
                      "NoAI pig/anvil impact fixture stages");
                gm_runtime_tick(&r,idle);
                gm_runtime_tick(&r,idle);
                CHECK(gm_runtime_spawn_mob_fixture(
                          &r,GM_MOB_PIG,pig_eid,
                          impact_x+0.5,base_y-3.0,impact_z+0.5,
                          0.0,0.0,0.0,0.0f,10.0f,1,0,0,0) &&
                      gm_mobs_set_entity_random_state(
                          &r.mobs,pig_eid,pig_seed,0,0.0) &&
                      gm_runtime_spawn_mob_fixture(
                          &r,GM_MOB_PIG,pig_eid2,
                          impact_x+0.5,base_y-3.0,impact_z+0.5,
                          0.0,0.0,0.0,0.0f,10.0f,1,0,0,0) &&
                      gm_mobs_set_entity_random_state(
                          &r.mobs,pig_eid2,pig_seed2,0,0.0) &&
                      gm_runtime_spawn_mob_fixture(
                          &r,GM_MOB_COW,cow_eid,
                          impact_x+0.5,base_y-3.0,impact_z+0.5,
                          0.0,0.0,0.0,0.0f,10.0f,1,0,0,0) &&
                      gm_mobs_set_entity_random_state(
                          &r.mobs,cow_eid,cow_seed,0,0.0) &&
                      gm_runtime_spawn_mob_fixture(
                          &r,GM_MOB_SHEEP,sheep_eid,
                          impact_x+0.5,base_y-3.0,impact_z+0.5,
                          0.0,0.0,0.0,0.0f,8.0f,1,0,0,0) &&
                      gm_mobs_set_entity_random_state(
                          &r.mobs,sheep_eid,sheep_seed,0,0.0) &&
                      gm_runtime_spawn_mob_fixture(
                          &r,GM_MOB_CHICKEN,chicken_eid,
                          impact_x+0.5,base_y-3.0,impact_z+0.5,
                          0.0,0.0,0.0,0.0f,4.0f,1,0,0,0) &&
                      gm_mobs_set_entity_random_state(
                          &r.mobs,chicken_eid,chicken_seed,0,0.0) &&
                      gm_runtime_set_entity_id_cursor(&r,eid+6) &&
                      gm_runtime_set_math_random_seed48(
                          &r,java_lcg_steps(math_seed,30)),
                      "NoAI passive targets follow falling constructor order");
                for(int tick=2;tick<=13;++tick)
                    gm_runtime_tick(&r,idle);
                {
                    const EwStore *mobs=r.mobs.current?&r.mobs.b:&r.mobs.a;
                    int pig_slot=-1,pig_slot2=-1,cow_slot=-1,sheep_slot=-1,
                        chicken_slot=-1;
                    GmMobEvent passive_events[11];
                    static const int event_kind[11]={
                        GM_MOB_EVENT_ENTITY_STATUS,GM_MOB_EVENT_SOUND,
                        GM_MOB_EVENT_ENTITY_STATUS,GM_MOB_EVENT_SOUND,
                        GM_MOB_EVENT_ENTITY_STATUS,GM_MOB_EVENT_SOUND,
                        GM_MOB_EVENT_ENTITY_STATUS,GM_MOB_EVENT_SOUND,
                        GM_MOB_EVENT_ENTITY_STATUS,GM_MOB_EVENT_SOUND,
                        GM_MOB_EVENT_ENTITY_STATUS};
                    const int event_eid[11]={
                        pig_eid,pig_eid,pig_eid2,pig_eid2,
                        cow_eid,cow_eid,sheep_eid,sheep_eid,
                        chicken_eid,chicken_eid,chicken_eid};
                    static const int event_data[11]={
                        2,GM_MOB_SOUND_PIG_HURT,
                        2,GM_MOB_SOUND_PIG_HURT,
                        2,GM_MOB_SOUND_COW_HURT,
                        2,GM_MOB_SOUND_SHEEP_HURT,
                        2,GM_MOB_SOUND_CHICKEN_DEATH,3};
                    int events_ok=r.mobs.event_count==event_count_before+11&&
                        r.mobs.event_next_seq==event_seq_before+11;
                    for(int i=0;i<11;++i)
                        events_ok=events_ok&&gm_mobs_event_get(
                            &r.mobs,event_count_before+i,&passive_events[i])&&
                            passive_events[i].seq==event_seq_before+(uint64_t)i&&
                            passive_events[i].kind==event_kind[i]&&
                            passive_events[i].eid==event_eid[i]&&
                            passive_events[i].data==event_data[i];
                    for(int slot=1;slot<EW_MAX_ENTITIES;++slot)
                        if(mobs->alive[slot]){
                            if(mobs->id[slot]==pig_eid) pig_slot=slot;
                            if(mobs->id[slot]==pig_eid2) pig_slot2=slot;
                            if(mobs->id[slot]==cow_eid) cow_slot=slot;
                            if(mobs->id[slot]==sheep_eid) sheep_slot=slot;
                            if(mobs->id[slot]==chicken_eid) chicken_slot=slot;
                        }
                    CHECK(chicken_slot>sheep_slot && chicken_slot>0,
                          "falling anvil chicken fixture keeps slot order");
                    CHECK(chicken_slot>0 &&
                          mobs->type[chicken_slot]==GM_MOB_CHICKEN &&
                          mobs->health[chicken_slot]==0.0f,
                          "falling anvil chicken reaches lethal health state");
                    CHECK(chicken_slot>0 &&
                          r.mobs.entity_hurt_resistant[chicken_slot]==19 &&
                          r.mobs.entity_hurt_time[chicken_slot]==9 &&
                          r.mobs.entity_last_damage[chicken_slot]==4.0f,
                          "falling anvil chicken ages public hurt state");
                    CHECK(chicken_slot>0 &&
                          r.mobs.entity_death_time[chicken_slot]==1,
                          "falling anvil chicken begins deathTime one");
                    CHECK(chicken_slot>0 && r.mobs.entity_dead[chicken_slot],
                          "falling anvil chicken keeps living dead flag");
                    int passive_events_exact=events_ok&&
                          passive_events[1].volume==1.0F&&
                          passive_events[1].pitch==
                              chicken_sound_pitch(
                                  java_lcg_steps(pig_seed,11))&&
                          passive_events[3].volume==1.0F&&
                          passive_events[3].pitch==
                              chicken_sound_pitch(
                                  java_lcg_steps(pig_seed2,11))&&
                          passive_events[5].volume==0.4F&&
                          passive_events[5].pitch==
                              chicken_sound_pitch(
                                  java_lcg_steps(cow_seed,11))&&
                          passive_events[7].volume==1.0F&&
                          passive_events[7].pitch==
                              chicken_sound_pitch(
                                  java_lcg_steps(sheep_seed,11))&&
                          passive_events[9].volume==1.0F&&
                          passive_events[9].pitch==
                              chicken_sound_pitch(
                                  java_lcg_steps(chicken_seed,11))&&
                          passive_events[1].x==impact_x+0.5&&
                          passive_events[1].y==base_y-3.0&&
                          passive_events[1].z==impact_z+0.5;
                    CHECK(passive_events_exact,
                          "falling anvil passives emit exact ordered events");
                    CHECK(chicken_slot>0 &&
                          r.mobs.entity_random[chicken_slot].random.seed==
                              java_lcg_steps(chicken_seed,15),
                          "falling anvil chicken advances death-sound RNG");
                    CHECK(r.next_entity_id==eid+6 &&
                          r.math_random_seed48==java_lcg_steps(math_seed,40),
                          "falling anvil five-passive cursors advance exactly");
                    int passives_exact=r.falling_block_count==0 && pig_slot>0 &&
                          pig_slot2>pig_slot && cow_slot>pig_slot2 &&
                          sheep_slot>cow_slot &&
                          chicken_slot>sheep_slot &&
                          r.falling_blocks[0].fall_time==13 &&
                          r.falling_blocks[0].impact_fall_distance==2.90223908f &&
                          mobs->type[pig_slot]==GM_MOB_PIG &&
                          mobs->type[pig_slot2]==GM_MOB_PIG &&
                          mobs->type[cow_slot]==GM_MOB_COW &&
                          mobs->type[sheep_slot]==GM_MOB_SHEEP &&
                          mobs->type[chicken_slot]==GM_MOB_CHICKEN &&
                          mobs->health[pig_slot]==6.0f &&
                          mobs->health[pig_slot2]==6.0f &&
                          mobs->health[cow_slot]==6.0f &&
                          mobs->health[sheep_slot]==4.0f &&
                          mobs->health[chicken_slot]==0.0f &&
                          r.mobs.entity_hurt_resistant[pig_slot]==19 &&
                          r.mobs.entity_hurt_resistant[pig_slot2]==19 &&
                          r.mobs.entity_hurt_resistant[cow_slot]==19 &&
                          r.mobs.entity_hurt_resistant[sheep_slot]==19 &&
                          r.mobs.entity_hurt_resistant[chicken_slot]==19 &&
                          r.mobs.entity_hurt_time[pig_slot]==9 &&
                          r.mobs.entity_hurt_time[pig_slot2]==9 &&
                          r.mobs.entity_hurt_time[cow_slot]==9 &&
                          r.mobs.entity_hurt_time[sheep_slot]==9 &&
                          r.mobs.entity_hurt_time[chicken_slot]==9 &&
                          r.mobs.entity_last_damage[pig_slot]==4.0f &&
                          r.mobs.entity_last_damage[pig_slot2]==4.0f &&
                          r.mobs.entity_last_damage[cow_slot]==4.0f &&
                          r.mobs.entity_last_damage[sheep_slot]==4.0f &&
                          r.mobs.entity_last_damage[chicken_slot]==4.0f &&
                          r.mobs.entity_death_time[chicken_slot]==1 &&
                          r.mobs.entity_dead[chicken_slot] &&
                          r.mobs.entity_random[pig_slot].random.seed==
                              java_lcg_steps(pig_seed,16) &&
                          r.mobs.entity_random[pig_slot2].random.seed==
                              java_lcg_steps(pig_seed2,16) &&
                          r.mobs.entity_random[cow_slot].random.seed==
                              java_lcg_steps(cow_seed,16) &&
                          r.mobs.entity_random[sheep_slot].random.seed==
                              java_lcg_steps(sheep_seed,16) &&
                          r.mobs.entity_random[chicken_slot].random.seed==
                              java_lcg_steps(chicken_seed,15) &&
                          r.next_entity_id==eid+6 &&
                          r.falling_blocks[0].random_seed48==
                              java_lcg_steps(no_damage_seed,1) &&
                          r.math_random_seed48==java_lcg_steps(math_seed,40) &&
                          r.world_random_seed48==world_seed &&
                          gm_runtime_scheduled_tick_count(&r)==1 &&
                          gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                          pending.x==impact_x&&pending.y==base_y-3&&
                          pending.z==impact_z&&pending.block==145&&
                          pending.time==r.clock.total_time+2&&
                          pending.priority==0&&pending.order==1;
                    CHECK(passives_exact,
                          "falling anvil damages ordered NoAI passives with Java RNG state");
                }
                r.mobs=mobs_before;
                r.controlled_mobs_enabled=controlled_mobs_before;
                r.do_mob_loot=do_mob_loot_before;
            }

            {
                /* EntityLivingBase.onDeath runs the chicken loot table inside
                 * the landing update. Preserve that immediate constructor
                 * boundary before either chicken or EntityItems receive their
                 * ordinary public tick. */
                const int eid=521060,chicken_eid=521061;
                const uint64_t chicken_seed=UINT64_C(0x23456789ABCD);
                const int impact_x=r.ox+10,impact_z=r.oz+10;
                int chicken_slot=-1;
                GmMobLive mobs_before=r.mobs;
                GmLiveSim entities_before=r.entities;
                int controlled_mobs_before=r.controlled_mobs_enabled;
                int mobs_enabled_before=r.mobs_enabled;
                int do_mob_loot_before=r.do_mob_loot;
                int next_entity_id_before=r.next_entity_id;
                uint64_t math_before=r.math_random_seed48;
                uint64_t world_before=r.world_random_seed48;

                gm_mobs_init(&r.mobs,0);
                memset(&r.entities,0,sizeof r.entities);
                memset(r.falling_blocks,0,sizeof r.falling_blocks);
                r.mobs_enabled=0;
                r.controlled_mobs_enabled=0;
                r.falling_block_count=0;
                r.scheduled_tick_count=0;
                r.scheduled_tick_next_order=0;
                r.next_falling_random_valid=0;
                for(int y=base_y-5;y<=base_y+1;++y)
                    for(int z=impact_z-2;z<=impact_z+2;++z)
                        for(int x=impact_x-2;x<=impact_x+2;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                for(int z=impact_z-2;z<=impact_z+2;++z)
                    for(int x=impact_x-2;x<=impact_x+2;++x)
                        gm_world_set_block_meta(
                            r.world,x,base_y-4,z,1,0);
                gm_world_set_block_meta(
                    r.world,impact_x,base_y-1,impact_z,1,0);
                CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                      gm_runtime_set_math_random_seed48(&r,math_seed) &&
                      gm_runtime_set_world_random_seed48(&r,world_seed) &&
                      gm_runtime_set_next_falling_random_seed48(
                          &r,no_damage_seed) &&
                      gm_runtime_set_do_mob_loot(&r,1) &&
                      gm_runtime_set_block(
                          &r,impact_x,base_y,impact_z,145,0) &&
                      gm_runtime_set_block(
                          &r,impact_x,base_y-1,impact_z,0,0),
                      "loot chicken/anvil impact fixture stages");
                gm_runtime_tick(&r,idle);
                gm_runtime_tick(&r,idle);
                CHECK(gm_runtime_spawn_mob_fixture(
                          &r,GM_MOB_CHICKEN,chicken_eid,
                          impact_x+0.5,base_y-3.0,impact_z+0.5,
                          0.0,0.0,0.0,0.0f,4.0f,1,0,0,0) &&
                      gm_mobs_set_entity_random_state(
                          &r.mobs,chicken_eid,chicken_seed,0,0.0) &&
                      gm_runtime_set_mob_fire_ticks(
                          &r,chicken_eid,100) &&
                      !gm_runtime_set_mob_fire_ticks(
                          &r,chicken_eid,-21) &&
                      !gm_runtime_set_mob_fire_ticks(
                          &r,chicken_eid+100,0) &&
                      gm_runtime_set_entity_id_cursor(&r,eid+2) &&
                      gm_runtime_set_math_random_seed48(
                          &r,java_lcg_steps(math_seed,6)),
                      "loot chicken follows falling constructor order");
                for(int tick=2;tick<=12;++tick)
                    gm_runtime_tick(&r,idle);
                gm_runtime_tick_falling_fixture_phase(&r);
                {
                    const EwStore *mobs=
                        r.mobs.current?&r.mobs.b:&r.mobs.a;
                    const GmLiveEnt *feather=&r.entities.ents[0];
                    const GmLiveEnt *chicken=&r.entities.ents[1];
                    GmMobEvent event0,event1,event2;
                    for(int slot=1;slot<EW_MAX_ENTITIES;++slot)
                        if(mobs->alive[slot]&&mobs->id[slot]==chicken_eid)
                            chicken_slot=slot;
                    int loot_death_exact=chicken_slot>0&&
                          mobs->health[chicken_slot]==0.0f&&
                          r.mobs.entity_dead[chicken_slot]&&
                          r.mobs.entity_death_time[chicken_slot]==0&&
                          r.mobs.entity_hurt_resistant[chicken_slot]==20&&
                          r.mobs.entity_hurt_time[chicken_slot]==10&&
                          r.mobs.entity_last_damage[chicken_slot]==4.0f&&
                          r.mobs.fire_ticks[chicken_slot]==100&&
                          r.mobs.entity_random[chicken_slot].random.seed==
                              java_lcg_steps(chicken_seed,18)&&
                          gm_mobs_terminal_particle_count(&r.mobs)==0&&
                          r.mobs.terminal_particle_next_seq==0&&
                          r.mobs.terminal_particle_dropped==0;
                    CHECK(loot_death_exact,
                          "loot chicken dies immediately with exact target RNG");
                    CHECK(r.mobs.event_count==3&&
                          r.mobs.event_dropped==0&&
                          r.mobs.event_next_seq==3&&
                          gm_mobs_event_get(&r.mobs,0,&event0)&&
                          gm_mobs_event_get(&r.mobs,1,&event1)&&
                          gm_mobs_event_get(&r.mobs,2,&event2)&&
                          !gm_mobs_event_get(&r.mobs,-1,&event0)&&
                          !gm_mobs_event_get(&r.mobs,3,&event0)&&
                          event0.seq==0&&
                          event0.kind==GM_MOB_EVENT_ENTITY_STATUS&&
                          event0.eid==chicken_eid&&event0.data==2&&
                          event1.seq==1&&event1.kind==GM_MOB_EVENT_SOUND&&
                          event1.eid==chicken_eid&&
                          event1.data==GM_MOB_SOUND_CHICKEN_DEATH&&
                          event1.x==impact_x+0.5&&
                          event1.y==base_y-3.0&&
                          event1.z==impact_z+0.5&&
                          event1.volume==1.0F&&
                          event1.pitch==chicken_sound_pitch(
                              java_lcg_steps(chicken_seed,11))&&
                          event2.seq==2&&
                          event2.kind==GM_MOB_EVENT_ENTITY_STATUS&&
                          event2.eid==chicken_eid&&event2.data==3,
                          "loot chicken emits exact causal event order");
                    CHECK(r.entities.n_active==2&&feather->active&&
                          feather->eid==eid+2&&feather->item==288&&
                          feather->count==2&&feather->meta==0&&
                          feather->x==impact_x+0.5&&
                          feather->y==base_y-3.0&&
                          feather->z==impact_z+0.5&&
                          feather->mx==-0.063530094921588898&&
                          feather->my==0.20000000298023224&&
                          feather->mz==0.072716355323791504&&
                          feather->yaw==115.765297f&&
                          feather->has_hover_start&&
                          feather->hover_start==5.36388826f&&
                          feather->age==0&&feather->pickup_delay==10&&
                          feather->health==5&&feather->lifespan==6000,
                          "loot chicken emits exact feather stack constructor");
                    CHECK(chicken->active&&chicken->eid==eid+3&&
                          chicken->item==366&&chicken->count==1&&
                          chicken->meta==0&&chicken->x==impact_x+0.5&&
                          chicken->y==base_y-3.0&&
                          chicken->z==impact_z+0.5&&
                          chicken->mx==0.089378565549850464&&
                          chicken->my==0.20000000298023224&&
                          chicken->mz==-0.0049703046679496765&&
                          chicken->yaw==281.542725f&&
                          chicken->has_hover_start&&
                          chicken->hover_start==3.43248844f&&
                          chicken->age==0&&chicken->pickup_delay==10&&
                          chicken->health==5&&chicken->lifespan==6000,
                          "loot chicken emits exact cooked-chicken constructor");
                    CHECK(r.next_entity_id==eid+4&&
                          r.math_random_seed48==
                              java_lcg_steps(math_seed,24)&&
                          r.world_random_seed48==world_seed&&
                          r.falling_blocks[0].random_seed48==
                              java_lcg_steps(no_damage_seed,1)&&
                          r.falling_block_count==0,
                          "loot chicken advances exact global cursors");
                }
                gm_runtime_tick(&r,idle);
                {
                    CHECK(chicken_slot>0&&
                          r.mobs.entity_death_time[chicken_slot]==1&&
                          r.mobs.entity_hurt_resistant[chicken_slot]==19&&
                          r.mobs.entity_hurt_time[chicken_slot]==9&&
                          r.mobs.fire_ticks[chicken_slot]==99&&
                          r.mobs.entity_random[chicken_slot].random.seed==
                              java_lcg_steps(chicken_seed,18)&&
                          gm_mobs_terminal_particle_count(&r.mobs)==0&&
                          r.entities.ents[0].active&&
                          r.entities.ents[0].age==1&&
                          r.entities.ents[0].pickup_delay==9&&
                          r.entities.ents[1].active&&
                          r.entities.ents[1].age==1&&
                          r.entities.ents[1].pickup_delay==9,
                          "loot chicken and both drops advance once publicly");
                }
                for(int tick=2;tick<=20;++tick){
                    gm_runtime_tick(&r,idle);
                    if(tick==10||tick==19){
                        const EwStore *mobs=
                            r.mobs.current?&r.mobs.b:&r.mobs.a;
                        CHECK(mobs->alive[chicken_slot]&&
                              mobs->type[chicken_slot]==GM_MOB_CHICKEN&&
                              r.mobs.entity_death_time[chicken_slot]==tick&&
                              r.mobs.entity_hurt_resistant[chicken_slot]==
                                  20-tick&&
                              r.mobs.entity_hurt_time[chicken_slot]==
                                  (tick<10?10-tick:0)&&
                              r.mobs.fire_ticks[chicken_slot]==100-tick&&
                              r.mobs.entity_random[chicken_slot].random.seed==
                                  java_lcg_steps(chicken_seed,18)&&
                              gm_mobs_terminal_particle_count(&r.mobs)==0&&
                              r.entities.ents[0].active&&
                              r.entities.ents[0].age==tick&&
                              r.entities.ents[0].pickup_delay==
                                  (tick<10?10-tick:0)&&
                              r.entities.ents[1].active&&
                              r.entities.ents[1].age==tick&&
                              r.entities.ents[1].pickup_delay==
                                  (tick<10?10-tick:0),
                              "loot chicken preserves pre-terminal death state");
                    }
                }
                {
                    const EwStore *mobs=
                        r.mobs.current?&r.mobs.b:&r.mobs.a;
                    GmMobTerminalParticles particle_batch,ignored_batch;
                    JavaGaussianRandom expected_particle_random;
                    int particles_ok=gm_mobs_terminal_particle_get(
                        &r.mobs,0,&particle_batch);
                    jrand_gaussian_set_state(
                        &expected_particle_random,
                        java_lcg_steps(chicken_seed,18),0,0.0);
                    for(int particle=0;particle<20&&particles_ok;++particle){
                        double vx=jrand_gaussian_next(
                            &expected_particle_random)*0.02;
                        double vy=jrand_gaussian_next(
                            &expected_particle_random)*0.02;
                        double vz=jrand_gaussian_next(
                            &expected_particle_random)*0.02;
                        float offset_x=jrand_float(
                            &expected_particle_random.random)*0.4F*2.0F;
                        float offset_y=jrand_float(
                            &expected_particle_random.random)*0.7F;
                        float offset_z=jrand_float(
                            &expected_particle_random.random)*0.4F*2.0F;
                        const GmTerminalParticle *actual=
                            &particle_batch.particles[particle];
                        particles_ok=actual->x==impact_x+0.5+
                                (double)offset_x-(double)0.4F&&
                            actual->y==base_y-3.0+(double)offset_y&&
                            actual->z==impact_z+0.5+
                                (double)offset_z-(double)0.4F&&
                            actual->vx==vx&&actual->vy==vy&&actual->vz==vz;
                    }
                    int xp_count=0;
                    for(int slot=0;slot<GM_XP_ORBS;++slot)
                        if(!r.mobs.xp_orbs[slot].dead&&
                                r.mobs.xp_orbs[slot].xpValue>0)
                            ++xp_count;
                    CHECK(!mobs->alive[chicken_slot]&&
                          mobs->type[chicken_slot]==EW_TYPE_NONE&&
                          r.mobs.entity_dead[chicken_slot]&&
                          r.mobs.entity_death_time[chicken_slot]==20&&
                          r.mobs.entity_hurt_resistant[chicken_slot]==0&&
                          r.mobs.entity_hurt_time[chicken_slot]==0&&
                          r.mobs.fire_ticks[chicken_slot]==80&&
                          !r.mobs.controlled_no_ai[chicken_slot]&&
                          !r.mobs.controlled_block_collisions[chicken_slot]&&
                          r.mobs.entity_random[chicken_slot].random.seed==
                              java_lcg_steps(chicken_seed,218)&&
                          !r.mobs.entity_random[chicken_slot]
                              .have_next_next_gaussian&&
                          r.entities.ents[0].active&&
                          r.entities.ents[0].age==20&&
                          r.entities.ents[0].pickup_delay==0&&
                          r.entities.ents[1].active&&
                          r.entities.ents[1].age==20&&
                          r.entities.ents[1].pickup_delay==0&&
                          gm_mobs_terminal_particle_count(&r.mobs)==1&&
                          r.mobs.terminal_particle_next_seq==1&&
                          r.mobs.terminal_particle_dropped==0&&
                          particles_ok&&particle_batch.seq==0&&
                          particle_batch.eid==chicken_eid&&
                          particle_batch.dimension==r.dimension&&
                          particle_batch.particle_id==0&&
                          particle_batch.ignore_range==1&&
                          particle_batch.parameter_count==0&&
                          expected_particle_random.random.seed==
                              java_lcg_steps(chicken_seed,218)&&
                          !expected_particle_random.have_next_next_gaussian&&
                          !gm_mobs_terminal_particle_get(
                              &r.mobs,-1,&ignored_batch)&&
                          !gm_mobs_terminal_particle_get(
                              &r.mobs,1,&ignored_batch)&&
                          !gm_mobs_terminal_particle_get(NULL,0,&ignored_batch)&&
                          !gm_mobs_terminal_particle_get(&r.mobs,0,NULL)&&
                          xp_count==0&&r.next_entity_id==eid+4&&
                          r.math_random_seed48==
                              java_lcg_steps(math_seed,24)&&
                          r.world_random_seed48==world_seed,
                          "loot chicken removes exactly at terminal tick");
                }

                r.mobs=mobs_before;
                r.entities=entities_before;
                r.mobs_enabled=mobs_enabled_before;
                r.controlled_mobs_enabled=controlled_mobs_before;
                r.do_mob_loot=do_mob_loot_before;
                r.next_entity_id=next_entity_id_before;
                r.math_random_seed48=math_before;
                r.world_random_seed48=world_before;
                memset(r.falling_blocks,0,sizeof r.falling_blocks);
                r.falling_block_count=0;
                r.scheduled_tick_count=0;
                r.scheduled_tick_next_order=0;
            }

            {
                /* The fixed product item table is not a Java limit. If it
                 * cannot hold this seed's two stacks, reject the controlled
                 * target before damage, RNG, or ID mutation. */
                GmMobLive capacity_mobs;
                GmLiveSim capacity_items;
                McAABB impact_box=mc_aabb_make(
                    0.0,64.0,0.0,1.0,65.0,1.0);
                uint64_t capacity_math=java_lcg_steps(math_seed,6);
                int capacity_next=530002;
                gm_mobs_init(&capacity_mobs,0);
                capacity_mobs.active_dimension=0;
                memset(&capacity_items,0,sizeof capacity_items);
                for(int slot=0;slot<GM_LIVE_MAX-1;++slot){
                    capacity_items.ents[slot].active=1;
                    capacity_items.ents[slot].type=0;
                }
                capacity_items.n_active=GM_LIVE_MAX-1;
                CHECK(gm_mobs_spawn_exact(
                          &capacity_mobs,GM_MOB_CHICKEN,530001,
                          0.5,64.0,0.5,0.0,0.0,0.0,0.0f,
                          4.0f,1,0,0,0)>0&&
                      gm_mobs_set_entity_random_state(
                          &capacity_mobs,530001,
                          UINT64_C(0x23456789ABCD),0,0.0),
                      "loot chicken capacity fixture stages");
                {
                    const EwStore *before=capacity_mobs.current?
                        &capacity_mobs.b:&capacity_mobs.a;
                    float health_before=before->health[1];
                    uint64_t target_before=
                        capacity_mobs.entity_random[1].random.seed;
                    uint64_t math_cursor_before=capacity_math;
                    int next_before=capacity_next;
                    CHECK(gm_mobs_falling_anvil_damage_controlled_passives(
                              &capacity_mobs,0,&impact_box,4.0f,
                              &capacity_math,&capacity_items,
                              &capacity_next,1)==0&&
                          before->health[1]==health_before&&
                          !capacity_mobs.entity_dead[1]&&
                          capacity_mobs.entity_hurt_resistant[1]==0&&
                          capacity_mobs.entity_hurt_time[1]==0&&
                          capacity_mobs.entity_last_damage[1]==0.0f&&
                          capacity_mobs.entity_random[1].random.seed==
                              target_before&&
                          capacity_math==math_cursor_before&&
                          capacity_next==next_before&&
                          capacity_items.n_active==GM_LIVE_MAX-1&&
                          capacity_mobs.event_count==0&&
                          capacity_mobs.event_dropped==0&&
                          capacity_mobs.event_next_seq==0,
                          "loot chicken capacity rejection is atomic");
                }
            }

            {
                /* Cover every chicken feather cardinality and the
                 * EntityOnFire furnace-smelt branch without repeating the
                 * full falling trajectory. The helper is the same product
                 * phase called by runtime landing. */
                static const struct {
                    uint64_t seed;
                    int fire_ticks;
                    int feather_count;
                    int meat_item;
                    int item_count;
                } variants[] = {
                    {UINT64_C(2),-1,0,365,1},
                    {UINT64_C(3),-1,1,365,2},
                    {UINT64_C(0x23456789ABCD),-1,2,365,2},
                    {UINT64_C(0x23456789ABCD),100,2,366,2}
                };
                McAABB impact_box=mc_aabb_make(
                    0.0,64.0,0.0,1.0,65.0,1.0);
                for(size_t variant=0;
                        variant<sizeof variants/sizeof variants[0];
                        ++variant){
                    GmMobLive variant_mobs;
                    GmLiveSim variant_items;
                    uint64_t variant_math=java_lcg_steps(math_seed,6);
                    int variant_next=540002;
                    const int variant_eid=540001;
                    const EwStore *store;
                    int meat_slot;
                    gm_mobs_init(&variant_mobs,0);
                    variant_mobs.active_dimension=0;
                    memset(&variant_items,0,sizeof variant_items);
                    CHECK(gm_mobs_spawn_exact(
                              &variant_mobs,GM_MOB_CHICKEN,variant_eid,
                              0.5,64.0,0.5,0.0,0.0,0.0,0.0f,
                              4.0f,1,0,0,0)>0&&
                          gm_mobs_set_entity_random_state(
                              &variant_mobs,variant_eid,
                              variants[variant].seed,0,0.0)&&
                          gm_mobs_set_entity_fire_ticks(
                              &variant_mobs,variant_eid,
                              variants[variant].fire_ticks)&&
                          gm_mobs_falling_anvil_damage_controlled_passives(
                              &variant_mobs,0,&impact_box,4.0f,
                              &variant_math,&variant_items,
                              &variant_next,1)==1,
                          "chicken loot variant applies lethal impact");
                    store=variant_mobs.current?
                        &variant_mobs.b:&variant_mobs.a;
                    meat_slot=variants[variant].feather_count>0?1:0;
                    CHECK(store->health[1]==0.0f&&
                          variant_mobs.entity_dead[1]&&
                          variant_mobs.entity_random[1].random.seed==
                              java_lcg_steps(variants[variant].seed,7)&&
                          variant_mobs.fire_ticks[1]==
                              variants[variant].fire_ticks&&
                          variant_items.n_active==
                              variants[variant].item_count&&
                          variant_items.ents[meat_slot].active&&
                          variant_items.ents[meat_slot].eid==
                              540002+meat_slot&&
                          variant_items.ents[meat_slot].item==
                              variants[variant].meat_item&&
                          variant_items.ents[meat_slot].count==1&&
                          variant_next==
                              540002+variants[variant].item_count&&
                          variant_math==java_lcg_steps(
                              math_seed,
                              8+8*variants[variant].item_count)&&
                          variant_mobs.event_count==3&&
                          variant_mobs.event_dropped==0&&
                          variant_mobs.events[0].kind==
                              GM_MOB_EVENT_ENTITY_STATUS&&
                          variant_mobs.events[0].data==2&&
                          variant_mobs.events[1].kind==GM_MOB_EVENT_SOUND&&
                          variant_mobs.events[1].data==
                              GM_MOB_SOUND_CHICKEN_DEATH&&
                          variant_mobs.events[1].pitch==
                              chicken_sound_pitch(variants[variant].seed)&&
                          variant_mobs.events[2].kind==
                              GM_MOB_EVENT_ENTITY_STATUS&&
                          variant_mobs.events[2].data==3,
                          "chicken loot variant preserves stacks and cursors");
                    if(variants[variant].feather_count>0)
                        CHECK(variant_items.ents[0].active&&
                              variant_items.ents[0].eid==540002&&
                              variant_items.ents[0].item==288&&
                              variant_items.ents[0].count==
                                  variants[variant].feather_count,
                              "chicken loot variant preserves feather stack");
                }
            }

            {
                GmMobLive pig_mobs;
                GmLiveSim pig_items;
                McAABB impact_box=mc_aabb_make(
                    0.0,64.0,0.0,1.0,65.0,1.0);
                const uint64_t pig_seed=UINT64_C(0x3456789ABCDE);
                uint64_t pig_math=java_lcg_steps(math_seed,6);
                int pig_next=545002;
                gm_mobs_init(&pig_mobs,0);
                pig_mobs.active_dimension=0;
                memset(&pig_items,0,sizeof pig_items);
                CHECK(gm_mobs_spawn_exact(
                          &pig_mobs,GM_MOB_PIG,545001,
                          0.5,64.0,0.5,0.0,0.0,0.0,0.0f,
                          4.0f,1,0,0,0)>0&&
                      gm_mobs_set_entity_random_state(
                          &pig_mobs,545001,pig_seed,0,0.0)&&
                      gm_mobs_set_entity_fire_ticks(
                          &pig_mobs,545001,-1)&&
                      gm_mobs_set_pig_saddled(
                          &pig_mobs,545001,1)&&
                      gm_mobs_falling_anvil_damage_controlled_passives(
                          &pig_mobs,0,&impact_box,4.0f,
                          &pig_math,&pig_items,&pig_next,1)==1,
                      "pig loot fixture applies lethal impact");
                {
                    const EwStore *store=pig_mobs.current?
                        &pig_mobs.b:&pig_mobs.a;
                    CHECK(store->health[1]==0.0f&&
                          pig_mobs.entity_dead[1]&&
                          pig_mobs.entity_random[1].random.seed==
                              java_lcg_steps(pig_seed,6)&&
                          pig_items.n_active==2&&pig_items.ents[0].active&&
                          pig_items.ents[0].eid==545002&&
                          pig_items.ents[0].item==319&&
                          pig_items.ents[0].count==3&&
                          pig_items.ents[0].pickup_delay==10&&
                          pig_items.ents[1].active&&
                          pig_items.ents[1].eid==545003&&
                          pig_items.ents[1].item==329&&
                          pig_items.ents[1].count==1&&
                          pig_items.ents[1].pickup_delay==10&&
                          pig_next==545004&&
                          pig_math==java_lcg_steps(math_seed,24)&&
                          pig_mobs.event_count==3&&
                          pig_mobs.events[1].kind==GM_MOB_EVENT_SOUND&&
                          pig_mobs.events[1].data==
                              GM_MOB_SOUND_PIG_DEATH&&
                          pig_mobs.events[1].pitch==
                              chicken_sound_pitch(pig_seed),
                          "pig loot precedes saddle with exact cursors");
                }
            }

            {
                GmMobLive pig_mobs;
                GmLiveSim pig_items;
                McAABB impact_box=mc_aabb_make(
                    0.0,64.0,0.0,1.0,65.0,1.0);
                const uint64_t pig_seed=UINT64_C(0x456789ABCDEF);
                uint64_t pig_math=UINT64_C(0x23456789ABCD);
                int pig_next=545102;
                gm_mobs_init(&pig_mobs,0);
                pig_mobs.active_dimension=0;
                memset(&pig_items,0,sizeof pig_items);
                CHECK(gm_mobs_spawn_exact(
                          &pig_mobs,GM_MOB_PIG,545101,
                          0.5,64.0,0.5,0.0,0.0,0.0,0.0f,
                          4.0f,1,0,0,0)>0&&
                      gm_mobs_set_entity_random_state(
                          &pig_mobs,545101,pig_seed,0,0.0)&&
                      gm_mobs_set_pig_saddled(
                          &pig_mobs,545101,1)&&
                      gm_mobs_falling_anvil_damage_controlled_passives(
                          &pig_mobs,0,&impact_box,4.0f,
                          &pig_math,&pig_items,&pig_next,0)==1,
                      "saddled pig no-loot fixture applies lethal impact");
                CHECK(pig_mobs.entity_dead[1]&&
                      pig_mobs.entity_random[1].random.seed==
                          java_lcg_steps(pig_seed,4)&&
                      pig_items.n_active==1&&
                      pig_items.ents[0].active&&
                      pig_items.ents[0].eid==545102&&
                      pig_items.ents[0].item==329&&
                      pig_items.ents[0].count==1&&
                      pig_next==545103&&
                      pig_math==java_lcg_steps(
                          UINT64_C(0x23456789ABCD),10),
                      "saddled pig drops saddle outside doMobLoot");
            }

            {
                GmMobLive pig_mobs;
                GmLiveSim full_items;
                McAABB impact_box=mc_aabb_make(
                    0.0,64.0,0.0,1.0,65.0,1.0);
                const uint64_t pig_seed=UINT64_C(0x456789ABCDEF);
                uint64_t pig_math=UINT64_C(0x23456789ABCD);
                int pig_next=545202;
                gm_mobs_init(&pig_mobs,0);
                pig_mobs.active_dimension=0;
                memset(&full_items,0,sizeof full_items);
                for(int i=0;i<GM_LIVE_MAX;++i){
                    full_items.ents[i].active=1;
                    full_items.ents[i].type=0;
                }
                full_items.n_active=GM_LIVE_MAX;
                CHECK(gm_mobs_spawn_exact(
                          &pig_mobs,GM_MOB_PIG,545201,
                          0.5,64.0,0.5,0.0,0.0,0.0,0.0f,
                          4.0f,1,0,0,0)>0&&
                      gm_mobs_set_entity_random_state(
                          &pig_mobs,545201,pig_seed,0,0.0)&&
                      gm_mobs_set_pig_saddled(
                          &pig_mobs,545201,1),
                      "saddled pig no-loot capacity fixture stages");
                CHECK(gm_mobs_falling_anvil_damage_controlled_passives(
                          &pig_mobs,0,&impact_box,4.0f,
                          &pig_math,&full_items,&pig_next,0)==0&&
                      (pig_mobs.current?pig_mobs.b.health[1]:
                          pig_mobs.a.health[1])==4.0f&&
                      !pig_mobs.entity_dead[1]&&
                      pig_mobs.entity_random[1].random.seed==pig_seed&&
                      pig_math==UINT64_C(0x23456789ABCD)&&
                      pig_next==545202&&pig_mobs.event_count==0,
                      "saddle-only capacity rejection is atomic");
            }

            {
                GmMobLive pig_mobs;
                GmLiveSim full_items;
                McAABB impact_box=mc_aabb_make(
                    0.0,64.0,0.0,1.0,65.0,1.0);
                uint64_t pig_math=UINT64_C(0x123456789ABC);
                int pig_next=546002;
                gm_mobs_init(&pig_mobs,0);
                pig_mobs.active_dimension=0;
                memset(&full_items,0,sizeof full_items);
                for(int i=0;i<GM_LIVE_MAX;++i){
                    full_items.ents[i].active=1;
                    full_items.ents[i].type=0;
                }
                full_items.n_active=GM_LIVE_MAX;
                CHECK(gm_mobs_spawn_exact(
                          &pig_mobs,GM_MOB_PIG,546001,
                          0.5,64.0,0.5,0.0,0.0,0.0,0.0f,
                          4.0f,1,0,0,0)>0&&
                      gm_mobs_set_entity_random_state(
                          &pig_mobs,546001,
                          UINT64_C(0x3456789ABCDE),0,0.0),
                      "pig loot full-capacity fixture stages");
                {
                    const uint64_t target_before=
                        pig_mobs.entity_random[1].random.seed;
                    const uint64_t math_before=pig_math;
                    const int next_before=pig_next;
                    CHECK(gm_mobs_falling_anvil_damage_controlled_passives(
                              &pig_mobs,0,&impact_box,4.0f,
                              &pig_math,&full_items,&pig_next,1)==0&&
                          (pig_mobs.current?pig_mobs.b.health[1]:
                              pig_mobs.a.health[1])==4.0f&&
                          !pig_mobs.entity_dead[1]&&
                          pig_mobs.entity_random[1].random.seed==
                              target_before&&pig_math==math_before&&
                          pig_next==next_before&&
                          full_items.n_active==GM_LIVE_MAX&&
                          pig_mobs.event_count==0,
                          "pig loot full-capacity rejection is atomic");
                }
            }

            {
                /* Cow loot has two ordered pools: optional leather followed
                 * by raw/cooked beef. This seed emits one of each. */
                static const struct {
                    int fire_ticks;
                    int beef_item;
                } variants[] = {{-1,363},{100,364}};
                const uint64_t cow_seed=UINT64_C(0x56789ABCDEF0);
                McAABB impact_box=mc_aabb_make(
                    0.0,64.0,0.0,1.0,65.0,1.0);
                for(size_t variant=0;
                        variant<sizeof variants/sizeof variants[0];
                        ++variant){
                    GmMobLive cow_mobs;
                    GmLiveSim cow_items;
                    uint64_t cow_math=java_lcg_steps(math_seed,6);
                    int cow_next=547002;
                    gm_mobs_init(&cow_mobs,0);
                    cow_mobs.active_dimension=0;
                    memset(&cow_items,0,sizeof cow_items);
                    CHECK(gm_mobs_spawn_exact(
                              &cow_mobs,GM_MOB_COW,547001,
                              0.5,64.0,0.5,0.0,0.0,0.0,0.0f,
                              4.0f,1,0,0,0)>0&&
                          gm_mobs_set_entity_random_state(
                              &cow_mobs,547001,cow_seed,0,0.0)&&
                          gm_mobs_set_entity_fire_ticks(
                              &cow_mobs,547001,
                              variants[variant].fire_ticks)&&
                          gm_mobs_falling_anvil_damage_controlled_passives(
                              &cow_mobs,0,&impact_box,4.0f,
                              &cow_math,&cow_items,&cow_next,1)==1,
                          "cow loot fixture applies lethal impact");
                    {
                        const EwStore *store=cow_mobs.current?
                            &cow_mobs.b:&cow_mobs.a;
                        CHECK(store->health[1]==0.0f&&
                              cow_mobs.entity_dead[1]&&
                              cow_mobs.entity_random[1].random.seed==
                                  java_lcg_steps(cow_seed,8)&&
                              cow_items.n_active==2&&
                              cow_items.ents[0].active&&
                              cow_items.ents[0].eid==547002&&
                              cow_items.ents[0].item==334&&
                              cow_items.ents[0].count==1&&
                              cow_items.ents[1].active&&
                              cow_items.ents[1].eid==547003&&
                              cow_items.ents[1].item==
                                  variants[variant].beef_item&&
                              cow_items.ents[1].count==1&&
                              cow_next==547004&&
                              cow_math==java_lcg_steps(math_seed,24)&&
                              cow_mobs.event_count==3&&
                              cow_mobs.events[1].kind==GM_MOB_EVENT_SOUND&&
                              cow_mobs.events[1].data==
                                  GM_MOB_SOUND_COW_DEATH&&
                              cow_mobs.events[1].volume==0.4f&&
                              cow_mobs.events[1].pitch==
                                  chicken_sound_pitch(cow_seed),
                              "cow loot preserves pool order, smelt, and cursors");
                    }
                }
            }

            {
                GmMobLive cow_mobs;
                GmLiveSim one_free_item;
                McAABB impact_box=mc_aabb_make(
                    0.0,64.0,0.0,1.0,65.0,1.0);
                const uint64_t cow_seed=UINT64_C(0x56789ABCDEF0);
                uint64_t cow_math=UINT64_C(0x123456789ABC);
                int cow_next=548002;
                gm_mobs_init(&cow_mobs,0);
                cow_mobs.active_dimension=0;
                memset(&one_free_item,0,sizeof one_free_item);
                for(int i=0;i<GM_LIVE_MAX-1;++i){
                    one_free_item.ents[i].active=1;
                    one_free_item.ents[i].type=0;
                }
                one_free_item.n_active=GM_LIVE_MAX-1;
                CHECK(gm_mobs_spawn_exact(
                          &cow_mobs,GM_MOB_COW,548001,
                          0.5,64.0,0.5,0.0,0.0,0.0,0.0f,
                          4.0f,1,0,0,0)>0&&
                      gm_mobs_set_entity_random_state(
                          &cow_mobs,548001,cow_seed,0,0.0),
                      "cow loot capacity fixture stages");
                {
                    const uint64_t target_before=
                        cow_mobs.entity_random[1].random.seed;
                    const uint64_t math_before=cow_math;
                    const int next_before=cow_next;
                    CHECK(gm_mobs_falling_anvil_damage_controlled_passives(
                              &cow_mobs,0,&impact_box,4.0f,
                              &cow_math,&one_free_item,&cow_next,1)==0&&
                          (cow_mobs.current?cow_mobs.b.health[1]:
                              cow_mobs.a.health[1])==4.0f&&
                          !cow_mobs.entity_dead[1]&&
                          cow_mobs.entity_random[1].random.seed==
                              target_before&&cow_math==math_before&&
                          cow_next==next_before&&
                          one_free_item.n_active==GM_LIVE_MAX-1&&
                          cow_mobs.event_count==0,
                          "cow loot one-slot rejection is atomic");
                }
            }

            {
                /* Adult sheep select a fleece table unless sheared; that
                 * table emits colored wool before nested raw/cooked mutton. */
                static const struct {
                    int fire_ticks;
                    int mutton_item;
                    int fleece_color;
                    int sheared;
                } variants[] = {
                    {-1,423,0,0},{100,424,0,0},{-1,423,14,0},{-1,423,14,1}
                };
                const uint64_t sheep_seed=UINT64_C(0x789ABCDEF012);
                McAABB impact_box=mc_aabb_make(
                    0.0,64.0,0.0,1.0,65.0,1.0);
                for(size_t variant=0;
                        variant<sizeof variants/sizeof variants[0];
                        ++variant){
                    GmMobLive sheep_mobs;
                    GmLiveSim sheep_items;
                    uint64_t sheep_math=java_lcg_steps(math_seed,6);
                    int sheep_next=549002;
                    gm_mobs_init(&sheep_mobs,0);
                    sheep_mobs.active_dimension=0;
                    memset(&sheep_items,0,sizeof sheep_items);
                    CHECK(gm_mobs_spawn_exact(
                              &sheep_mobs,GM_MOB_SHEEP,549001,
                              0.5,64.0,0.5,0.0,0.0,0.0,0.0f,
                              4.0f,1,0,0,0)>0&&
                          gm_mobs_set_entity_random_state(
                              &sheep_mobs,549001,sheep_seed,0,0.0)&&
                          gm_mobs_set_entity_fire_ticks(
                              &sheep_mobs,549001,
                              variants[variant].fire_ticks)&&
                          gm_mobs_set_sheep_state(
                              &sheep_mobs,549001,
                              variants[variant].fleece_color,
                              variants[variant].sheared)&&
                          gm_mobs_falling_anvil_damage_controlled_passives(
                              &sheep_mobs,0,&impact_box,4.0f,
                              &sheep_math,&sheep_items,&sheep_next,1)==1,
                          "sheep loot fixture applies lethal impact");
                    {
                        const EwStore *store=sheep_mobs.current?
                            &sheep_mobs.b:&sheep_mobs.a;
                        int item_count=variants[variant].sheared?1:2;
                        int mutton_index=variants[variant].sheared?0:1;
                        CHECK(store->health[1]==0.0f&&
                              sheep_mobs.entity_dead[1]&&
                              sheep_mobs.entity_random[1].random.seed==
                                  java_lcg_steps(sheep_seed,
                                      variants[variant].sheared?6:8)&&
                              sheep_items.n_active==item_count&&
                              sheep_items.ents[0].active&&
                              sheep_items.ents[0].eid==549002&&
                              (variants[variant].sheared||
                                  (sheep_items.ents[0].item==35&&
                                   sheep_items.ents[0].count==1&&
                                   sheep_items.ents[0].meta==
                                       variants[variant].fleece_color))&&
                              sheep_items.ents[mutton_index].item==
                                  variants[variant].mutton_item&&
                              sheep_items.ents[mutton_index].count==2&&
                              sheep_items.ents[mutton_index].meta==0&&
                              sheep_next==549002+item_count&&
                              sheep_math==java_lcg_steps(
                                  math_seed,8+8*item_count)&&
                              sheep_mobs.event_count==3&&
                              sheep_mobs.events[1].kind==GM_MOB_EVENT_SOUND&&
                              sheep_mobs.events[1].data==
                                  GM_MOB_SOUND_SHEEP_DEATH&&
                              sheep_mobs.events[1].volume==1.0f&&
                              sheep_mobs.events[1].pitch==
                                  chicken_sound_pitch(sheep_seed),
                              "sheep color/sheared loot preserves smelt and cursors");
                    }
                }
            }

            {
                GmMobLive sheep_mobs;
                GmLiveSim one_free_item;
                McAABB impact_box=mc_aabb_make(
                    0.0,64.0,0.0,1.0,65.0,1.0);
                const uint64_t sheep_seed=UINT64_C(0x789ABCDEF012);
                uint64_t sheep_math=UINT64_C(0x123456789ABC);
                int sheep_next=549102;
                gm_mobs_init(&sheep_mobs,0);
                sheep_mobs.active_dimension=0;
                memset(&one_free_item,0,sizeof one_free_item);
                for(int i=0;i<GM_LIVE_MAX-1;++i){
                    one_free_item.ents[i].active=1;
                    one_free_item.ents[i].type=0;
                }
                one_free_item.n_active=GM_LIVE_MAX-1;
                CHECK(gm_mobs_spawn_exact(
                          &sheep_mobs,GM_MOB_SHEEP,549101,
                          0.5,64.0,0.5,0.0,0.0,0.0,0.0f,
                          4.0f,1,0,0,0)>0&&
                      gm_mobs_set_entity_random_state(
                          &sheep_mobs,549101,sheep_seed,0,0.0),
                      "sheep loot capacity fixture stages");
                {
                    const uint64_t target_before=
                        sheep_mobs.entity_random[1].random.seed;
                    const uint64_t math_before=sheep_math;
                    const int next_before=sheep_next;
                    CHECK(gm_mobs_falling_anvil_damage_controlled_passives(
                              &sheep_mobs,0,&impact_box,4.0f,
                              &sheep_math,&one_free_item,&sheep_next,1)==0&&
                          (sheep_mobs.current?sheep_mobs.b.health[1]:
                              sheep_mobs.a.health[1])==4.0f&&
                          !sheep_mobs.entity_dead[1]&&
                          sheep_mobs.entity_random[1].random.seed==
                              target_before&&sheep_math==math_before&&
                          sheep_next==next_before&&
                          one_free_item.n_active==GM_LIVE_MAX-1&&
                          sheep_mobs.event_count==0,
                          "sheep loot one-slot rejection is atomic");
                    CHECK(gm_mobs_set_sheep_state(
                              &sheep_mobs,549101,14,1)&&
                          gm_mobs_falling_anvil_damage_controlled_passives(
                              &sheep_mobs,0,&impact_box,4.0f,
                              &sheep_math,&one_free_item,&sheep_next,1)==1&&
                          (sheep_mobs.current?sheep_mobs.b.health[1]:
                              sheep_mobs.a.health[1])==0.0f&&
                          sheep_mobs.entity_dead[1]&&
                          sheep_mobs.entity_random[1].random.seed==
                              java_lcg_steps(sheep_seed,6)&&
                          sheep_math==java_lcg_steps(math_before,10)&&
                          sheep_next==next_before+1&&
                          one_free_item.n_active==GM_LIVE_MAX&&
                          one_free_item.ents[GM_LIVE_MAX-1].item==423&&
                          one_free_item.ents[GM_LIVE_MAX-1].count==2&&
                          sheep_mobs.event_count==3,
                          "sheared sheep accepts the same single free slot");
                }
            }

            {
                GmMobLive sheep_mobs;
                GmEntityView view;
                gm_mobs_init(&sheep_mobs,0);
                sheep_mobs.active_dimension=0;
                CHECK(gm_mobs_spawn_exact(
                          &sheep_mobs,GM_MOB_SHEEP,549201,
                          0.5,64.0,0.5,0.0,0.0,0.0,0.0f,
                          8.0f,1,0,0,0)>0&&
                      !gm_mobs_set_sheep_state(
                          &sheep_mobs,549201,-1,0)&&
                      !gm_mobs_set_sheep_state(
                          &sheep_mobs,549201,16,0)&&
                      !gm_mobs_set_sheep_state(
                          &sheep_mobs,549201,14,2)&&
                      gm_mobs_set_sheep_state(
                          &sheep_mobs,549201,14,1)&&
                      gm_mobs_fill_views(&sheep_mobs,&view,1)==1&&
                      view.fleece_color==14&&view.sheared,
                      "sheep state validates and reaches the live render view");
                {
                    EwStore *store=sheep_mobs.current?
                        &sheep_mobs.b:&sheep_mobs.a;
                    store->alive[1]=0;
                    store->type[1]=EW_TYPE_NONE;
                    CHECK(gm_mobs_spawn_exact(
                              &sheep_mobs,GM_MOB_SHEEP,549202,
                              0.5,64.0,0.5,0.0,0.0,0.0,0.0f,
                              8.0f,1,0,0,0)==1&&
                          sheep_mobs.sheep_data[1]==0&&
                          gm_mobs_fill_views(&sheep_mobs,&view,1)==1&&
                          view.fleece_color==0&&!view.sheared,
                          "reused sheep slots reset fleece and sheared state");
                }
            }

            {
                /* Fresh nonlethal damage sends status 2 and the hurt sound.
                 * A stronger hit inside hurt resistance applies only the
                 * delta and emits status 3 without replaying fresh-hit RNG. */
                GmMobLive event_mobs;
                GmLiveSim event_items;
                McAABB impact_box=mc_aabb_make(
                    0.0,64.0,0.0,1.0,65.0,1.0);
                const uint64_t target_seed=UINT64_C(0x13579BDF0246);
                uint64_t event_math=UINT64_C(0x2468ACE01357);
                int event_next=550002;
                GmMobEvent event0,event1,event2;
                gm_mobs_init(&event_mobs,0);
                event_mobs.active_dimension=0;
                memset(&event_items,0,sizeof event_items);
                CHECK(gm_mobs_spawn_exact(
                          &event_mobs,GM_MOB_CHICKEN,550001,
                          0.5,64.0,0.5,0.0,0.0,0.0,0.0f,
                          4.0f,1,0,0,0)>0&&
                      gm_mobs_set_entity_random_state(
                          &event_mobs,550001,target_seed,0,0.0)&&
                      gm_mobs_falling_anvil_damage_controlled_passives(
                          &event_mobs,0,&impact_box,2.0f,&event_math,
                          &event_items,&event_next,0)==1&&
                      event_mobs.event_count==2&&
                      gm_mobs_event_get(&event_mobs,0,&event0)&&
                      gm_mobs_event_get(&event_mobs,1,&event1)&&
                      event0.kind==GM_MOB_EVENT_ENTITY_STATUS&&
                      event0.data==2&&
                      event1.kind==GM_MOB_EVENT_SOUND&&
                      event1.data==GM_MOB_SOUND_CHICKEN_HURT&&
                      event1.pitch==chicken_sound_pitch(target_seed),
                      "fresh nonlethal chicken emits hurt events");
                {
                    uint64_t target_after=
                        event_mobs.entity_random[1].random.seed;
                    uint64_t math_after=event_math;
                    CHECK(gm_mobs_falling_anvil_damage_controlled_passives(
                              &event_mobs,0,&impact_box,2.0f,&event_math,
                              &event_items,&event_next,0)==0&&
                          event_mobs.event_count==2&&
                          event_mobs.entity_random[1].random.seed==
                              target_after&&event_math==math_after,
                          "rejected equal chicken damage emits no events");
                    CHECK(gm_mobs_falling_anvil_damage_controlled_passives(
                              &event_mobs,0,&impact_box,4.0f,&event_math,
                              &event_items,&event_next,0)==1&&
                          event_mobs.event_count==3&&
                          gm_mobs_event_get(&event_mobs,2,&event2)&&
                          event2.kind==GM_MOB_EVENT_ENTITY_STATUS&&
                          event2.data==3&&event2.eid==550001&&
                          event_mobs.entity_random[1].random.seed==
                              target_after&&event_math==math_after,
                          "lethal hurt delta emits only terminal status");
                }
            }

            {
                /* Fill the advertised three-events-per-slot budget, then add
                 * one nonfresh terminal status to exercise one-record wrap. */
                GmMobLive ring_mobs;
                GmLiveSim ring_items;
                McAABB impact_box=mc_aabb_make(
                    0.0,64.0,0.0,1.0,65.0,1.0);
                uint64_t ring_math=UINT64_C(0x112233445566);
                int ring_next=560001;
                GmMobEvent first_event,last_event;
                gm_mobs_init(&ring_mobs,0);
                ring_mobs.active_dimension=0;
                memset(&ring_items,0,sizeof ring_items);
                for(int i=0;i<GM_MOB_CAPACITY;++i){
                    EwStore *store=ring_mobs.current?
                        &ring_mobs.b:&ring_mobs.a;
                    if(i>0){
                        store->alive[1]=0;
                        store->type[1]=EW_TYPE_NONE;
                    }
                    CHECK(gm_mobs_spawn_exact(
                              &ring_mobs,GM_MOB_CHICKEN,560001+i,
                              0.5,64.0,0.5,0.0,0.0,0.0,0.0f,
                              4.0f,1,0,0,0)>0&&
                          gm_mobs_set_entity_random_state(
                              &ring_mobs,560001+i,
                              UINT64_C(0x102030405060)+i,0,0.0)&&
                          gm_mobs_falling_anvil_damage_controlled_passives(
                              &ring_mobs,0,&impact_box,4.0f,&ring_math,
                              &ring_items,&ring_next,0)==1,
                          "mob event ring fill impact applies");
                }
                {
                    EwStore *store=ring_mobs.current?
                        &ring_mobs.b:&ring_mobs.a;
                    store->alive[1]=0;
                    store->type[1]=EW_TYPE_NONE;
                }
                CHECK(ring_mobs.event_count==GM_MOB_EVENT_CAPACITY&&
                      ring_mobs.event_dropped==0&&
                      ring_mobs.event_next_seq==GM_MOB_EVENT_CAPACITY&&
                      gm_mobs_spawn_exact(
                          &ring_mobs,GM_MOB_CHICKEN,560999,
                          0.5,64.0,0.5,0.0,0.0,0.0,0.0f,
                          2.0f,1,0,0,20)>0,
                      "mob event ring reaches exact clean capacity");
                ring_mobs.entity_last_damage[1]=2.0F;
                CHECK(gm_mobs_falling_anvil_damage_controlled_passives(
                          &ring_mobs,0,&impact_box,4.0f,&ring_math,
                          &ring_items,&ring_next,0)==1&&
                      ring_mobs.event_count==GM_MOB_EVENT_CAPACITY&&
                      ring_mobs.event_dropped==1&&
                      ring_mobs.event_next_seq==
                          GM_MOB_EVENT_CAPACITY+1&&
                      gm_mobs_event_get(&ring_mobs,0,&first_event)&&
                      gm_mobs_event_get(
                          &ring_mobs,GM_MOB_EVENT_CAPACITY-1,
                          &last_event)&&
                      first_event.seq==1&&
                      last_event.seq==GM_MOB_EVENT_CAPACITY&&
                      last_event.kind==GM_MOB_EVENT_ENTITY_STATUS&&
                      last_event.data==3&&last_event.eid==560999,
                      "mob event ring overwrites oldest record observably");
            }

            {
                /* One terminal particle batch is atomic, so the ring holds
                 * one batch per represented living slot. Fill it in one
                 * controlled tick, then prove oldest-first overwrite. */
                GmMobLive particle_ring_mobs;
                GmMobTerminalParticles first_batch,last_batch;
                gm_mobs_init(&particle_ring_mobs,0);
                particle_ring_mobs.active_dimension=0;
                for(int i=0;i<GM_MOB_TERMINAL_PARTICLE_CAPACITY;++i){
                    int slot=gm_mobs_spawn_exact(
                        &particle_ring_mobs,GM_MOB_CHICKEN,570001+i,
                        0.5,64.0,0.5,0.0,0.0,0.0,0.0f,
                        1.0f,1,0,19,0);
                    EwStore *store=particle_ring_mobs.current?
                        &particle_ring_mobs.b:&particle_ring_mobs.a;
                    CHECK(slot>0&&
                          gm_mobs_set_entity_random_state(
                              &particle_ring_mobs,570001+i,
                              UINT64_C(0x102030405060)+i,0,0.0),
                          "terminal particle ring fixture stages");
                    if(slot>0){
                        store->health[slot]=0.0F;
                        particle_ring_mobs.entity_dead[slot]=1;
                    }
                }
                gm_mobs_tick_controlled(
                    &particle_ring_mobs,r.world,NULL,
                    (struct PsvPlayer *)&r.player,
                    r.ox,r.oz,0,0,NULL,NULL,NULL);
                CHECK(particle_ring_mobs.terminal_particle_count==
                          GM_MOB_TERMINAL_PARTICLE_CAPACITY&&
                      particle_ring_mobs.terminal_particle_dropped==0&&
                      particle_ring_mobs.terminal_particle_next_seq==
                          GM_MOB_TERMINAL_PARTICLE_CAPACITY&&
                      gm_mobs_terminal_particle_get(
                          &particle_ring_mobs,0,&first_batch)&&
                      gm_mobs_terminal_particle_get(
                          &particle_ring_mobs,
                          GM_MOB_TERMINAL_PARTICLE_CAPACITY-1,
                          &last_batch)&&
                      first_batch.seq==0&&first_batch.eid==570001&&
                      last_batch.seq==
                          GM_MOB_TERMINAL_PARTICLE_CAPACITY-1&&
                      last_batch.eid==
                          570000+GM_MOB_TERMINAL_PARTICLE_CAPACITY,
                      "terminal particle ring reaches exact clean capacity");
                {
                    int slot=gm_mobs_spawn_exact(
                        &particle_ring_mobs,GM_MOB_CHICKEN,570999,
                        0.5,64.0,0.5,0.0,0.0,0.0,0.0f,
                        1.0f,1,0,19,0);
                    EwStore *store=particle_ring_mobs.current?
                        &particle_ring_mobs.b:&particle_ring_mobs.a;
                    CHECK(slot>0&&
                          gm_mobs_set_entity_random_state(
                              &particle_ring_mobs,570999,
                              UINT64_C(0x203040506070),0,0.0),
                          "terminal particle ring overflow fixture stages");
                    if(slot>0){
                        store->health[slot]=0.0F;
                        particle_ring_mobs.entity_dead[slot]=1;
                    }
                }
                gm_mobs_tick_controlled(
                    &particle_ring_mobs,r.world,NULL,
                    (struct PsvPlayer *)&r.player,
                    r.ox,r.oz,0,0,NULL,NULL,NULL);
                CHECK(particle_ring_mobs.terminal_particle_count==
                          GM_MOB_TERMINAL_PARTICLE_CAPACITY&&
                      particle_ring_mobs.terminal_particle_dropped==1&&
                      particle_ring_mobs.terminal_particle_next_seq==
                          GM_MOB_TERMINAL_PARTICLE_CAPACITY+1&&
                      gm_mobs_terminal_particle_get(
                          &particle_ring_mobs,0,&first_batch)&&
                      gm_mobs_terminal_particle_get(
                          &particle_ring_mobs,
                          GM_MOB_TERMINAL_PARTICLE_CAPACITY-1,
                          &last_batch)&&
                      first_batch.seq==1&&first_batch.eid==570002&&
                      last_batch.seq==GM_MOB_TERMINAL_PARTICLE_CAPACITY&&
                      last_batch.eid==570999&&last_batch.particle_id==0&&
                      last_batch.ignore_range==1&&
                      last_batch.parameter_count==0,
                      "terminal particle ring overwrites oldest batch observably");
            }

            {
                /* EntityAnimal's player-credit XP decision happens at
                 * deathTime 20, before terminal particles.  Exercise the
                 * exact positive boundary and the orb's same-world-tick
                 * constructor/update payload without unrelated collisions or
                 * player attraction. */
                GmMobLive xp_mobs;
                const double x=r.ox+200.5,y=200.0,z=r.oz+200.5;
                const uint64_t world_start=UINT64_C(0x23456789ABCD);
                const uint64_t math_start=java_lcg_steps(
                    UINT64_C(0x123456789ABC),24);
                uint64_t world_cursor=world_start,math_cursor=math_start;
                int next_eid=580002;
                int slot;
                JavaRandom expected_math;
                double draw_yaw,draw_x,draw_y,draw_z;
                float expected_yaw,constructor_x,constructor_y,constructor_z;
                double tick_y;
                McOrb *orb;

                for(int yy=198;yy<=202;++yy)
                    for(int zz=r.oz+198;zz<=r.oz+202;++zz)
                        for(int xx=r.ox+198;xx<=r.ox+202;++xx)
                            gm_world_set_block_meta(r.world,xx,yy,zz,0,0);
                gm_mobs_init(&xp_mobs,0);
                xp_mobs.active_dimension=0;
                slot=gm_mobs_spawn_exact(
                    &xp_mobs,GM_MOB_CHICKEN,580001,
                    x,y,z,0.0,0.0,0.0,0.0f,1.0f,1,0,19,0);
                if(slot>0){
                    EwStore *store=xp_mobs.current?&xp_mobs.b:&xp_mobs.a;
                    store->health[slot]=0.0F;
                    xp_mobs.entity_dead[slot]=1;
                }
                CHECK(slot>0&&gm_mobs_set_recent_hit_state(
                          &xp_mobs,580001,1,1),
                      "terminal XP positive fixture stages");
                jrand_set_seed48(&expected_math,math_start);
                draw_yaw=jrand_double(&expected_math);
                draw_x=jrand_double(&expected_math);
                draw_y=jrand_double(&expected_math);
                draw_z=jrand_double(&expected_math);
                expected_yaw=(float)(draw_yaw*360.0);
                constructor_x=(float)(draw_x*0.20000000298023224-
                    0.10000000149011612)*2.0F;
                constructor_y=(float)(draw_y*0.2)*2.0F;
                constructor_z=(float)(draw_z*0.20000000298023224-
                    0.10000000149011612)*2.0F;
                tick_y=(double)constructor_y-0.029999999329447746;
                gm_mobs_tick_controlled(
                    &xp_mobs,r.world,NULL,(struct PsvPlayer *)&r.player,
                    r.ox,r.oz,0,1,&world_cursor,&math_cursor,&next_eid);
                orb=&xp_mobs.xp_orbs[0];
                CHECK(orb->eid==580002&&orb->xpValue==3&&
                      orb->health==5&&!orb->dead&&orb->xpOrbAge==1&&
                      orb->delayBeforeCanPickup==0&&orb->xpColor==1&&
                      orb->xpTargetColor==0&&orb->yaw==expected_yaw&&
                      orb->posX==x+(double)constructor_x&&
                      orb->posY==y+tick_y&&
                      orb->posZ==z+(double)constructor_z&&
                      orb->motionX==(double)constructor_x*
                          0.9800000190734863&&
                      orb->motionY==tick_y*0.9800000190734863&&
                      orb->motionZ==(double)constructor_z*
                          0.9800000190734863&&
                      xp_mobs.orb_dimension[0]==0&&
                      xp_mobs.entity_recently_hit[slot]==0&&
                      xp_mobs.entity_attacking_player[slot]==1&&
                      world_cursor==java_lcg_steps(world_start,1)&&
                      math_cursor==expected_math.seed&&next_eid==580003&&
                      gm_mobs_terminal_particle_count(&xp_mobs)==1,
                      "terminal XP positive boundary is exact");
            }

            {
                GmMobLive expired_mobs;
                uint64_t world_cursor=UINT64_C(0x23456789ABCD);
                uint64_t math_cursor=java_lcg_steps(
                    UINT64_C(0x123456789ABC),24);
                const uint64_t world_before=world_cursor;
                const uint64_t math_before=math_cursor;
                int next_eid=581002;
                int slot;
                gm_mobs_init(&expired_mobs,0);
                expired_mobs.active_dimension=0;
                slot=gm_mobs_spawn_exact(
                    &expired_mobs,GM_MOB_CHICKEN,581001,
                    r.ox+210.5,200.0,r.oz+210.5,
                    0.0,0.0,0.0,0.0f,1.0f,1,0,0,0);
                if(slot>0){
                    EwStore *store=expired_mobs.current?
                        &expired_mobs.b:&expired_mobs.a;
                    store->health[slot]=0.0F;
                    expired_mobs.entity_dead[slot]=1;
                }
                CHECK(slot>0&&gm_mobs_set_recent_hit_state(
                          &expired_mobs,581001,19,1),
                      "terminal XP expired fixture stages");
                for(int tick=0;tick<20;++tick)
                    gm_mobs_tick_controlled(
                        &expired_mobs,r.world,NULL,
                        (struct PsvPlayer *)&r.player,r.ox,r.oz,0,1,
                        &world_cursor,&math_cursor,&next_eid);
                {
                    int xp_count=0;
                    for(int i=0;i<GM_XP_ORBS;++i)
                        if(!expired_mobs.xp_orbs[i].dead&&
                                expired_mobs.xp_orbs[i].xpValue>0)
                            ++xp_count;
                    CHECK(xp_count==0&&world_cursor==world_before&&
                          math_cursor==math_before&&next_eid==581002&&
                          expired_mobs.entity_recently_hit[slot]==0&&
                          expired_mobs.entity_attacking_player[slot]==0&&
                          gm_mobs_terminal_particle_count(&expired_mobs)==1,
                          "expired terminal XP credit consumes no cursors");
                }
            }

            {
                GmMobLive full_xp_mobs;
                uint64_t world_cursor=UINT64_C(0x23456789ABCD);
                uint64_t math_cursor=java_lcg_steps(
                    UINT64_C(0x123456789ABC),24);
                const uint64_t world_before=world_cursor;
                const uint64_t math_before=math_cursor;
                int next_eid=582002;
                int slot;
                gm_mobs_init(&full_xp_mobs,0);
                full_xp_mobs.active_dimension=0;
                for(int i=0;i<GM_XP_ORBS;++i){
                    full_xp_mobs.xp_orbs[i].eid=590000+i;
                    full_xp_mobs.xp_orbs[i].xpValue=1;
                    full_xp_mobs.xp_orbs[i].health=5;
                    full_xp_mobs.orb_dimension[i]=1;
                }
                slot=gm_mobs_spawn_exact(
                    &full_xp_mobs,GM_MOB_CHICKEN,582001,
                    r.ox+220.5,200.0,r.oz+220.5,
                    0.0,0.0,0.0,0.0f,1.0f,1,0,19,0);
                if(slot>0){
                    EwStore *store=full_xp_mobs.current?
                        &full_xp_mobs.b:&full_xp_mobs.a;
                    store->health[slot]=0.0F;
                    full_xp_mobs.entity_dead[slot]=1;
                }
                CHECK(slot>0&&gm_mobs_set_recent_hit_state(
                          &full_xp_mobs,582001,1,1),
                      "terminal XP full-pool fixture stages");
                gm_mobs_tick_controlled(
                    &full_xp_mobs,r.world,NULL,(struct PsvPlayer *)&r.player,
                    r.ox,r.oz,0,1,&world_cursor,&math_cursor,&next_eid);
                CHECK(world_cursor==world_before&&math_cursor==math_before&&
                      next_eid==582002&&
                      gm_mobs_terminal_particle_count(&full_xp_mobs)==1,
                      "full XP pool rejects atomically before RNG or ID use");
            }

            {
                const int eid=521050;
                const int impact_x=r.ox+8,impact_z=r.oz+8;
                PsvPlayer player_before=r.player;
                PsvPlayer server_player_before=r.server_player;
                PvStats vitals_before=r.vitals;
                int dead_before=r.dead;
                int hurt_before=r.mobs.player_hurt_time;
                int resistant_before=r.mobs.player_hurt_resistant;
                float last_damage_before=r.mobs.player_last_damage;
                float absorption_before=r.mobs.player_absorption;
                JavaRandom player_random_before=r.mobs.player_random;
                memset(r.falling_blocks,0,sizeof r.falling_blocks);
                r.falling_block_count=0;
                r.scheduled_tick_count=0;
                r.scheduled_tick_next_order=0;
                r.next_falling_random_valid=0;
                for(int y=base_y-5;y<=base_y+1;++y)
                    for(int z=impact_z-2;z<=impact_z+2;++z)
                        for(int x=impact_x-2;x<=impact_x+2;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                for(int z=impact_z-2;z<=impact_z+2;++z)
                    for(int x=impact_x-2;x<=impact_x+2;++x)
                        gm_world_set_block_meta(
                            r.world,x,base_y-4,z,1,0);
                gm_world_set_block_meta(
                    r.world,impact_x,base_y-1,impact_z,1,0);
                isr_init(&r.player.inv);
                isr_init(&r.server_player.inv);
                r.dead=0;
                r.player.ent.posX=impact_x+0.5-r.ox;
                r.player.ent.posY=base_y-3.0;
                r.player.ent.posZ=impact_z+0.5-r.oz;
                r.player.ent.box=psv_player_box(
                    r.player.ent.posX,r.player.ent.posY,r.player.ent.posZ);
                r.player.ent.motionX=r.player.ent.motionY=
                    r.player.ent.motionZ=0.0;
                r.player.ent.onGround=1;
                r.player.fall_distance=0.0f;
                r.server_player=r.player;
                r.vitals.health=20.0f;
                r.vitals.maxHealth=20.0f;
                r.vitals.foodLevel=20;
                r.vitals.saturation=5.0f;
                r.vitals.exhaustion=0.0f;
                r.vitals.foodTimer=0;
                r.player.health=r.server_player.health=20.0f;
                r.mobs.player_hurt_time=0;
                r.mobs.player_hurt_resistant=0;
                r.mobs.player_last_damage=0.0f;
                r.mobs.player_absorption=0.0f;
                CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                      gm_runtime_set_math_random_seed48(&r,math_seed) &&
                      gm_runtime_set_world_random_seed48(&r,world_seed) &&
                      gm_runtime_set_player_random_seed48(&r,player_seed) &&
                      gm_runtime_set_next_falling_random_seed48(
                          &r,no_damage_seed) &&
                      gm_runtime_set_block(
                          &r,impact_x,base_y,impact_z,145,0) &&
                      gm_runtime_set_block(
                          &r,impact_x,base_y-1,impact_z,0,0),
                      "unarmored player/anvil impact fixture stages");
                gm_runtime_tick(&r,idle);
                gm_runtime_tick(&r,idle);
                for(int tick=2;tick<=13;++tick)
                    gm_runtime_tick(&r,idle);
                CHECK(r.falling_block_count==0 &&
                      r.falling_blocks[0].fall_time==13 &&
                      r.falling_blocks[0].impact_fall_distance==2.90223908f &&
                      gm_world_block(
                          r.world,impact_x,base_y-3,impact_z)==145 &&
                      gm_world_meta(
                          r.world,impact_x,base_y-3,impact_z)==0 &&
                      r.next_entity_id==eid+1 &&
                      r.falling_blocks[0].random_seed48==
                          java_lcg_steps(no_damage_seed,1) &&
                      r.math_random_seed48==
                          java_lcg_steps(math_seed,2) &&
                      r.mobs.player_random.seed==
                          java_lcg_steps(player_seed,4) &&
                      r.world_random_seed48==world_seed &&
                      gm_runtime_scheduled_tick_count(&r)==1 &&
                      gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                      pending.x==impact_x&&pending.y==base_y-3&&
                      pending.z==impact_z&&pending.block==145&&
                      pending.time==r.clock.total_time+2&&
                      pending.priority==0&&
                      pending.order==1,
                      "anvil player impact preserves landing and event cursors");
                CHECK(r.vitals.health==16.0f &&
                      r.mobs.player_absorption==0.0f &&
                      r.mobs.player_hurt_resistant==20 &&
                      r.mobs.player_hurt_time==10 &&
                      r.mobs.player_last_damage==4.0f &&
                      fabsf(r.vitals.exhaustion-0.1f)<1.0e-7f,
                      "unarmored falling anvil applies exact damage lifecycle");
                r.player=player_before;
                r.server_player=server_player_before;
                r.vitals=vitals_before;
                r.dead=dead_before;
                r.mobs.player_hurt_time=hurt_before;
                r.mobs.player_hurt_resistant=resistant_before;
                r.mobs.player_last_damage=last_damage_before;
                r.mobs.player_absorption=absorption_before;
                r.mobs.player_random=player_random_before;
            }

            {
                /* Vanilla consumes absorption before health for an anvil hit,
                 * but still accepts the raw 4-point damage lifecycle and its
                 * source-less Math.random double. */
                const int eid=521055;
                const int impact_x=r.ox+8,impact_z=r.oz+8;
                PsvPlayer player_before=r.player;
                PsvPlayer server_player_before=r.server_player;
                PvStats vitals_before=r.vitals;
                int dead_before=r.dead;
                int hurt_before=r.mobs.player_hurt_time;
                int resistant_before=r.mobs.player_hurt_resistant;
                float last_damage_before=r.mobs.player_last_damage;
                float absorption_before=r.mobs.player_absorption;
                JavaRandom player_random_before=r.mobs.player_random;
                memset(r.falling_blocks,0,sizeof r.falling_blocks);
                r.falling_block_count=0;
                r.scheduled_tick_count=0;
                r.scheduled_tick_next_order=0;
                r.next_falling_random_valid=0;
                for(int y=base_y-5;y<=base_y+1;++y)
                    for(int z=impact_z-2;z<=impact_z+2;++z)
                        for(int x=impact_x-2;x<=impact_x+2;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                for(int z=impact_z-2;z<=impact_z+2;++z)
                    for(int x=impact_x-2;x<=impact_x+2;++x)
                        gm_world_set_block_meta(
                            r.world,x,base_y-4,z,1,0);
                gm_world_set_block_meta(
                    r.world,impact_x,base_y-1,impact_z,1,0);
                isr_init(&r.player.inv);
                isr_init(&r.server_player.inv);
                r.dead=0;
                r.player.ent.posX=impact_x+0.5-r.ox;
                r.player.ent.posY=base_y-3.0;
                r.player.ent.posZ=impact_z+0.5-r.oz;
                r.player.ent.box=psv_player_box(
                    r.player.ent.posX,r.player.ent.posY,r.player.ent.posZ);
                r.player.ent.motionX=r.player.ent.motionY=
                    r.player.ent.motionZ=0.0;
                r.player.ent.onGround=1;
                r.player.fall_distance=0.0f;
                r.server_player=r.player;
                r.vitals.health=20.0f;
                r.vitals.maxHealth=20.0f;
                r.vitals.foodLevel=20;
                r.vitals.saturation=5.0f;
                r.vitals.exhaustion=0.0f;
                r.vitals.foodTimer=0;
                r.player.health=r.server_player.health=20.0f;
                r.mobs.player_hurt_time=0;
                r.mobs.player_hurt_resistant=0;
                r.mobs.player_last_damage=0.0f;
                r.mobs.player_absorption=4.0f;
                CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                      gm_runtime_set_math_random_seed48(&r,math_seed) &&
                      gm_runtime_set_world_random_seed48(&r,world_seed) &&
                      gm_runtime_set_player_random_seed48(&r,player_seed) &&
                      gm_runtime_set_next_falling_random_seed48(
                          &r,no_damage_seed) &&
                      gm_runtime_set_block(
                          &r,impact_x,base_y,impact_z,145,4) &&
                      gm_runtime_set_block(
                          &r,impact_x,base_y-1,impact_z,0,0),
                      "absorption/anvil impact fixture stages");
                gm_runtime_tick(&r,idle);
                gm_runtime_tick(&r,idle);
                for(int tick=2;tick<=13;++tick)
                    gm_runtime_tick(&r,idle);
                CHECK(r.falling_block_count==0 &&
                      r.falling_blocks[0].fall_time==13 &&
                      r.falling_blocks[0].impact_fall_distance==2.90223908f &&
                      gm_world_block(
                          r.world,impact_x,base_y-3,impact_z)==145 &&
                      gm_world_meta(
                          r.world,impact_x,base_y-3,impact_z)==4 &&
                      r.next_entity_id==eid+1 &&
                      r.falling_blocks[0].random_seed48==
                          java_lcg_steps(no_damage_seed,1) &&
                      r.math_random_seed48==
                          java_lcg_steps(math_seed,2) &&
                      r.mobs.player_random.seed==
                          java_lcg_steps(player_seed,4) &&
                      r.world_random_seed48==world_seed &&
                      gm_runtime_scheduled_tick_count(&r)==1 &&
                      gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                      pending.x==impact_x&&pending.y==base_y-3&&
                      pending.z==impact_z&&pending.block==145&&
                      pending.time==r.clock.total_time+2&&
                      pending.priority==0&&pending.order==1,
                      "absorption/anvil preserves landing and event cursors");
                CHECK(r.vitals.health==20.0f &&
                      r.player.health==20.0f &&
                      r.server_player.health==20.0f &&
                      r.mobs.player_absorption==0.0f &&
                      r.mobs.player_hurt_resistant==20 &&
                      r.mobs.player_hurt_time==10 &&
                      r.mobs.player_last_damage==4.0f &&
                      r.vitals.exhaustion==0.0f,
                      "absorption fully consumes anvil damage without hunger cost");
                r.player=player_before;
                r.server_player=server_player_before;
                r.vitals=vitals_before;
                r.dead=dead_before;
                r.mobs.player_hurt_time=hurt_before;
                r.mobs.player_hurt_resistant=resistant_before;
                r.mobs.player_last_damage=last_damage_before;
                r.mobs.player_absorption=absorption_before;
                r.mobs.player_random=player_random_before;
            }

            {
                /* Resistance I reduces the post-armor anvil residual by 20%,
                 * while EntityLivingBase retains raw damage in lastDamage. */
                const int eid=521058;
                const int impact_x=r.ox+8,impact_z=r.oz+8;
                PsvPlayer player_before=r.player;
                PsvPlayer server_player_before=r.server_player;
                PvStats vitals_before=r.vitals;
                GmPotionEffectView potions_before[GM_MAX_POTION_EFFECTS];
                int potion_count_before=r.potion_count;
                int runtime_resistance_before=r.resistance_amplifier;
                int mob_resistance_before=r.mobs.player_resistance_amplifier;
                int dead_before=r.dead;
                int hurt_before=r.mobs.player_hurt_time;
                int resistant_before=r.mobs.player_hurt_resistant;
                float last_damage_before=r.mobs.player_last_damage;
                float absorption_before=r.mobs.player_absorption;
                JavaRandom player_random_before=r.mobs.player_random;
                memcpy(potions_before,r.potions,sizeof potions_before);
                memset(r.falling_blocks,0,sizeof r.falling_blocks);
                r.falling_block_count=0;
                r.scheduled_tick_count=0;
                r.scheduled_tick_next_order=0;
                r.next_falling_random_valid=0;
                for(int y=base_y-5;y<=base_y+1;++y)
                    for(int z=impact_z-2;z<=impact_z+2;++z)
                        for(int x=impact_x-2;x<=impact_x+2;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                for(int z=impact_z-2;z<=impact_z+2;++z)
                    for(int x=impact_x-2;x<=impact_x+2;++x)
                        gm_world_set_block_meta(
                            r.world,x,base_y-4,z,1,0);
                gm_world_set_block_meta(
                    r.world,impact_x,base_y-1,impact_z,1,0);
                isr_init(&r.player.inv);
                isr_init(&r.server_player.inv);
                r.dead=0;
                r.player.ent.posX=impact_x+0.5-r.ox;
                r.player.ent.posY=base_y-3.0;
                r.player.ent.posZ=impact_z+0.5-r.oz;
                r.player.ent.box=psv_player_box(
                    r.player.ent.posX,r.player.ent.posY,r.player.ent.posZ);
                r.player.ent.motionX=r.player.ent.motionY=
                    r.player.ent.motionZ=0.0;
                r.player.ent.onGround=1;
                r.player.fall_distance=0.0f;
                r.server_player=r.player;
                r.vitals.health=20.0f;
                r.vitals.maxHealth=20.0f;
                r.vitals.foodLevel=20;
                r.vitals.saturation=5.0f;
                r.vitals.exhaustion=0.0f;
                r.vitals.foodTimer=0;
                r.player.health=r.server_player.health=20.0f;
                r.mobs.player_hurt_time=0;
                r.mobs.player_hurt_resistant=0;
                r.mobs.player_last_damage=0.0f;
                r.mobs.player_absorption=0.0f;
                gm_runtime_potions_clear(&r);
                CHECK(gm_runtime_potion_add(&r,11,0,1000) &&
                      gm_runtime_set_entity_id_cursor(&r,eid) &&
                      gm_runtime_set_math_random_seed48(&r,math_seed) &&
                      gm_runtime_set_world_random_seed48(&r,world_seed) &&
                      gm_runtime_set_player_random_seed48(&r,player_seed) &&
                      gm_runtime_set_next_falling_random_seed48(
                          &r,no_damage_seed) &&
                      gm_runtime_set_block(
                          &r,impact_x,base_y,impact_z,145,0) &&
                      gm_runtime_set_block(
                          &r,impact_x,base_y-1,impact_z,0,0),
                      "Resistance I/anvil impact fixture stages");
                gm_runtime_tick(&r,idle);
                gm_runtime_tick(&r,idle);
                for(int tick=2;tick<=13;++tick)
                    gm_runtime_tick(&r,idle);
                CHECK(r.falling_block_count==0 &&
                      r.falling_blocks[0].fall_time==13 &&
                      r.falling_blocks[0].impact_fall_distance==2.90223908f &&
                      gm_world_block(
                          r.world,impact_x,base_y-3,impact_z)==145 &&
                      gm_world_meta(
                          r.world,impact_x,base_y-3,impact_z)==0 &&
                      r.next_entity_id==eid+1 &&
                      r.falling_blocks[0].random_seed48==
                          java_lcg_steps(no_damage_seed,1) &&
                      r.math_random_seed48==java_lcg_steps(math_seed,2) &&
                      r.mobs.player_random.seed==
                          java_lcg_steps(player_seed,4) &&
                      r.world_random_seed48==world_seed &&
                      gm_runtime_scheduled_tick_count(&r)==1 &&
                      gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                      pending.x==impact_x&&pending.y==base_y-3&&
                      pending.z==impact_z&&pending.block==145&&
                      pending.time==r.clock.total_time+2&&
                      pending.priority==0&&pending.order==1,
                      "Resistance I/anvil preserves landing and event cursors");
                CHECK(r.vitals.health==16.8f &&
                      r.player.health==16.8f &&
                      r.server_player.health==16.8f &&
                      r.mobs.player_absorption==0.0f &&
                      r.mobs.player_hurt_resistant==20 &&
                      r.mobs.player_hurt_time==10 &&
                      r.mobs.player_last_damage==4.0f &&
                      fabsf(r.vitals.exhaustion-0.1f)<1.0e-7f &&
                      r.mobs.player_resistance_amplifier==0,
                      "Resistance I reduces anvil health damage but not raw hurt state");
                gm_runtime_potions_clear(&r);
                for(int i=0;i<potion_count_before;++i)
                    CHECK(gm_runtime_potion_add(
                              &r,potions_before[i].id,
                              potions_before[i].amplifier,
                              potions_before[i].duration),
                          "Resistance I/anvil restores potion fixture");
                r.player=player_before;
                r.server_player=server_player_before;
                r.vitals=vitals_before;
                r.dead=dead_before;
                r.mobs.player_hurt_time=hurt_before;
                r.mobs.player_hurt_resistant=resistant_before;
                r.mobs.player_last_damage=last_damage_before;
                r.mobs.player_absorption=absorption_before;
                r.mobs.player_random=player_random_before;
                r.resistance_amplifier=runtime_resistance_before;
                r.mobs.player_resistance_amplifier=mob_resistance_before;
            }

            {
                /* A plain diamond chestplate is the first bounded armor
                 * target.  It has no enchantment RNG: raw four damages the
                 * piece once, then armor 8/toughness 2 leaves exact float
                 * health bits 0x41883127. */
                const int eid=521059;
                const int impact_x=r.ox+8,impact_z=r.oz+8;
                PsvPlayer player_before=r.player;
                PsvPlayer server_player_before=r.server_player;
                PvStats vitals_before=r.vitals;
                int dead_before=r.dead;
                int hurt_before=r.mobs.player_hurt_time;
                int resistant_before=r.mobs.player_hurt_resistant;
                float last_damage_before=r.mobs.player_last_damage;
                float absorption_before=r.mobs.player_absorption;
                JavaRandom player_random_before=r.mobs.player_random;
                memset(r.falling_blocks,0,sizeof r.falling_blocks);
                r.falling_block_count=0;
                r.scheduled_tick_count=0;
                r.scheduled_tick_next_order=0;
                r.next_falling_random_valid=0;
                for(int y=base_y-5;y<=base_y+1;++y)
                    for(int z=impact_z-2;z<=impact_z+2;++z)
                        for(int x=impact_x-2;x<=impact_x+2;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                for(int z=impact_z-2;z<=impact_z+2;++z)
                    for(int x=impact_x-2;x<=impact_x+2;++x)
                        gm_world_set_block_meta(
                            r.world,x,base_y-4,z,1,0);
                gm_world_set_block_meta(
                    r.world,impact_x,base_y-1,impact_z,1,0);
                isr_init(&r.player.inv);
                isr_init(&r.server_player.inv);
                CHECK(gm_runtime_set_inventory_stack(
                          &r,ISR_ARMOR_CHEST,ic_mk(311,1,0)),
                      "diamond chestplate/anvil inventory stages");
                r.dead=0;
                r.player.ent.posX=impact_x+0.5-r.ox;
                r.player.ent.posY=base_y-3.0;
                r.player.ent.posZ=impact_z+0.5-r.oz;
                r.player.ent.box=psv_player_box(
                    r.player.ent.posX,r.player.ent.posY,r.player.ent.posZ);
                r.player.ent.motionX=r.player.ent.motionY=
                    r.player.ent.motionZ=0.0;
                r.player.ent.onGround=1;
                r.player.fall_distance=0.0f;
                r.server_player=r.player;
                r.vitals.health=20.0f;
                r.vitals.maxHealth=20.0f;
                r.vitals.foodLevel=20;
                r.vitals.saturation=5.0f;
                r.vitals.exhaustion=0.0f;
                r.vitals.foodTimer=0;
                r.player.health=r.server_player.health=20.0f;
                r.mobs.player_hurt_time=0;
                r.mobs.player_hurt_resistant=0;
                r.mobs.player_last_damage=0.0f;
                r.mobs.player_absorption=0.0f;
                CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                      gm_runtime_set_math_random_seed48(&r,math_seed) &&
                      gm_runtime_set_world_random_seed48(&r,world_seed) &&
                      gm_runtime_set_player_random_seed48(&r,player_seed) &&
                      gm_runtime_set_next_falling_random_seed48(
                          &r,no_damage_seed) &&
                      gm_runtime_set_block(
                          &r,impact_x,base_y,impact_z,145,0) &&
                      gm_runtime_set_block(
                          &r,impact_x,base_y-1,impact_z,0,0),
                      "diamond chestplate/anvil impact fixture stages");
                gm_runtime_tick(&r,idle);
                gm_runtime_tick(&r,idle);
                for(int tick=2;tick<=13;++tick)
                    gm_runtime_tick(&r,idle);
                ICStack chest=isr_get_stack(
                    &r.player.inv,ISR_ARMOR_CHEST);
                CHECK(r.falling_block_count==0 &&
                      r.falling_blocks[0].fall_time==13 &&
                      r.falling_blocks[0].impact_fall_distance==2.90223908f &&
                      gm_world_block(
                          r.world,impact_x,base_y-3,impact_z)==145 &&
                      gm_world_meta(
                          r.world,impact_x,base_y-3,impact_z)==0 &&
                      r.next_entity_id==eid+1 &&
                      r.falling_blocks[0].random_seed48==
                          java_lcg_steps(no_damage_seed,1) &&
                      r.math_random_seed48==java_lcg_steps(math_seed,2) &&
                      r.mobs.player_random.seed==
                          java_lcg_steps(player_seed,4) &&
                      r.world_random_seed48==world_seed &&
                      gm_runtime_scheduled_tick_count(&r)==1 &&
                      gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                      pending.x==impact_x&&pending.y==base_y-3&&
                      pending.z==impact_z&&pending.block==145&&
                      pending.time==r.clock.total_time+2&&
                      pending.priority==0&&pending.order==1,
                      "diamond chestplate/anvil preserves landing and cursors");
                CHECK(r.vitals.health==17.02400016784668f &&
                      r.player.health==17.02400016784668f &&
                      r.server_player.health==17.02400016784668f &&
                      r.mobs.player_absorption==0.0f &&
                      r.mobs.player_hurt_resistant==20 &&
                      r.mobs.player_hurt_time==10 &&
                      r.mobs.player_last_damage==4.0f &&
                      fabsf(r.vitals.exhaustion-0.1f)<1.0e-7f &&
                      chest.item==311&&chest.count==1&&chest.meta==1 &&
                      isr_get_stack(&r.player.inv,ISR_ARMOR0).item==0 &&
                      isr_get_stack(&r.player.inv,ISR_ARMOR0+1).item==0 &&
                      isr_get_stack(&r.player.inv,ISR_ARMOR0+3).item==0,
                      "diamond chestplate absorbs and takes exact durability");
                r.player=player_before;
                r.server_player=server_player_before;
                r.vitals=vitals_before;
                r.dead=dead_before;
                r.mobs.player_hurt_time=hurt_before;
                r.mobs.player_hurt_resistant=resistant_before;
                r.mobs.player_last_damage=last_damage_before;
                r.mobs.player_absorption=absorption_before;
                r.mobs.player_random=player_random_before;
            }

            {
                /* ANVIL damage has a head-slot pre-hook before hurt immunity:
                 * the pinned nextFloat makes 19 durability, scales raw four
                 * to three, then ordinary helmet armor adds durability one. */
                const int eid=521090;
                const int impact_x=r.ox+8,impact_z=r.oz+8;
                PsvPlayer player_before=r.player;
                PsvPlayer server_player_before=r.server_player;
                PvStats vitals_before=r.vitals;
                int dead_before=r.dead;
                int hurt_before=r.mobs.player_hurt_time;
                int resistant_before=r.mobs.player_hurt_resistant;
                float last_damage_before=r.mobs.player_last_damage;
                float absorption_before=r.mobs.player_absorption;
                JavaRandom player_random_before=r.mobs.player_random;
                memset(r.falling_blocks,0,sizeof r.falling_blocks);
                r.falling_block_count=0;
                r.scheduled_tick_count=0;
                r.scheduled_tick_next_order=0;
                r.next_falling_random_valid=0;
                for(int y=base_y-5;y<=base_y+1;++y)
                    for(int z=impact_z-2;z<=impact_z+2;++z)
                        for(int x=impact_x-2;x<=impact_x+2;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                for(int z=impact_z-2;z<=impact_z+2;++z)
                    for(int x=impact_x-2;x<=impact_x+2;++x)
                        gm_world_set_block_meta(
                            r.world,x,base_y-4,z,1,0);
                gm_world_set_block_meta(
                    r.world,impact_x,base_y-1,impact_z,1,0);
                isr_init(&r.player.inv);
                isr_init(&r.server_player.inv);
                CHECK(gm_runtime_set_inventory_stack(
                          &r,ISR_ARMOR0+3,ic_mk(310,1,0)),
                      "diamond helmet/anvil inventory stages");
                r.dead=0;
                r.player.ent.posX=impact_x+0.5-r.ox;
                r.player.ent.posY=base_y-3.0;
                r.player.ent.posZ=impact_z+0.5-r.oz;
                r.player.ent.box=psv_player_box(
                    r.player.ent.posX,r.player.ent.posY,r.player.ent.posZ);
                r.player.ent.motionX=r.player.ent.motionY=
                    r.player.ent.motionZ=0.0;
                r.player.ent.onGround=1;
                r.player.fall_distance=0.0f;
                r.server_player=r.player;
                r.vitals.health=20.0f;
                r.vitals.maxHealth=20.0f;
                r.vitals.foodLevel=20;
                r.vitals.saturation=5.0f;
                r.vitals.exhaustion=0.0f;
                r.vitals.foodTimer=0;
                r.player.health=r.server_player.health=20.0f;
                r.mobs.player_hurt_time=0;
                r.mobs.player_hurt_resistant=0;
                r.mobs.player_last_damage=0.0f;
                r.mobs.player_absorption=0.0f;
                CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                      gm_runtime_set_math_random_seed48(&r,math_seed) &&
                      gm_runtime_set_world_random_seed48(&r,world_seed) &&
                      gm_runtime_set_player_random_seed48(&r,player_seed) &&
                      gm_runtime_set_next_falling_random_seed48(
                          &r,no_damage_seed) &&
                      gm_runtime_set_block(
                          &r,impact_x,base_y,impact_z,145,0) &&
                      gm_runtime_set_block(
                          &r,impact_x,base_y-1,impact_z,0,0),
                      "diamond helmet/anvil impact fixture stages");
                gm_runtime_tick(&r,idle);
                gm_runtime_tick(&r,idle);
                for(int tick=2;tick<=13;++tick)
                    gm_runtime_tick(&r,idle);
                ICStack helmet=isr_get_stack(
                    &r.player.inv,ISR_ARMOR0+3);
                CHECK(r.falling_block_count==0 &&
                      r.falling_blocks[0].fall_time==13 &&
                      r.falling_blocks[0].impact_fall_distance==2.90223908f &&
                      gm_world_block(
                          r.world,impact_x,base_y-3,impact_z)==145 &&
                      gm_world_meta(
                          r.world,impact_x,base_y-3,impact_z)==0 &&
                      r.next_entity_id==eid+1 &&
                      r.falling_blocks[0].random_seed48==
                          java_lcg_steps(no_damage_seed,1) &&
                      r.math_random_seed48==java_lcg_steps(math_seed,2) &&
                      r.mobs.player_random.seed==
                          java_lcg_steps(player_seed,5) &&
                      r.world_random_seed48==world_seed &&
                      gm_runtime_scheduled_tick_count(&r)==1 &&
                      gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                      pending.x==impact_x&&pending.y==base_y-3&&
                      pending.z==impact_z&&pending.block==145&&
                      pending.time==r.clock.total_time+2&&
                      pending.priority==0&&pending.order==1,
                      "diamond helmet/anvil preserves landing and cursors");
                CHECK(r.vitals.health==17.215999603271484f &&
                      r.player.health==17.215999603271484f &&
                      r.server_player.health==17.215999603271484f &&
                      r.mobs.player_absorption==0.0f &&
                      r.mobs.player_hurt_resistant==20 &&
                      r.mobs.player_hurt_time==10 &&
                      r.mobs.player_last_damage==3.0f &&
                      fabsf(r.vitals.exhaustion-0.1f)<1.0e-7f &&
                      helmet.item==310&&helmet.count==1&&helmet.meta==20 &&
                      isr_get_stack(&r.player.inv,ISR_ARMOR0).item==0 &&
                      isr_get_stack(&r.player.inv,ISR_ARMOR0+1).item==0 &&
                      isr_get_stack(&r.player.inv,ISR_ARMOR0+2).item==0,
                      "diamond helmet pre-hook scales damage and durability");
                r.player=player_before;
                r.server_player=server_player_before;
                r.vitals=vitals_before;
                r.dead=dead_before;
                r.mobs.player_hurt_time=hurt_before;
                r.mobs.player_hurt_resistant=resistant_before;
                r.mobs.player_last_damage=last_damage_before;
                r.mobs.player_absorption=absorption_before;
                r.mobs.player_random=player_random_before;
            }

            {
                /* EntityLivingBase ages a pre-existing hurt pair before the
                 * falling entity reaches its landed AABB.  Seed 21/11 just
                 * before that last tick, so the impact sees vanilla's 20/10
                 * immunity window.  Equal raw damage must be rejected; a
                 * larger hit applies only the raw delta and keeps that pair. */
                const int eid=521060;
                const int impact_x=r.ox+8,impact_z=r.oz+8;
                const float prior_damage[2]={4.0f,2.0f};
                const float expected_health[2]={20.0f,18.0f};
                const float expected_exhaustion[2]={0.0f,0.1f};
                PsvPlayer player_before=r.player;
                PsvPlayer server_player_before=r.server_player;
                PvStats vitals_before=r.vitals;
                int dead_before=r.dead;
                int hurt_before=r.mobs.player_hurt_time;
                int resistant_before=r.mobs.player_hurt_resistant;
                float last_damage_before=r.mobs.player_last_damage;
                float absorption_before=r.mobs.player_absorption;
                JavaRandom player_random_before=r.mobs.player_random;
                for(int which=0;which<2;++which){
                    memset(r.falling_blocks,0,sizeof r.falling_blocks);
                    r.falling_block_count=0;
                    r.scheduled_tick_count=0;
                    r.scheduled_tick_next_order=0;
                    r.next_falling_random_valid=0;
                    for(int y=base_y-5;y<=base_y+1;++y)
                        for(int z=impact_z-2;z<=impact_z+2;++z)
                            for(int x=impact_x-2;x<=impact_x+2;++x)
                                gm_world_set_block_meta(r.world,x,y,z,0,0);
                    for(int z=impact_z-2;z<=impact_z+2;++z)
                        for(int x=impact_x-2;x<=impact_x+2;++x)
                            gm_world_set_block_meta(
                                r.world,x,base_y-4,z,1,0);
                    gm_world_set_block_meta(
                        r.world,impact_x,base_y-1,impact_z,1,0);
                    isr_init(&r.player.inv);
                    isr_init(&r.server_player.inv);
                    r.dead=0;
                    r.player.ent.posX=impact_x+0.5-r.ox;
                    r.player.ent.posY=base_y-3.0;
                    r.player.ent.posZ=impact_z+0.5-r.oz;
                    r.player.ent.box=psv_player_box(
                        r.player.ent.posX,r.player.ent.posY,r.player.ent.posZ);
                    r.player.ent.motionX=r.player.ent.motionY=
                        r.player.ent.motionZ=0.0;
                    r.player.ent.onGround=1;
                    r.player.fall_distance=0.0f;
                    r.server_player=r.player;
                    r.vitals.health=20.0f;
                    r.vitals.maxHealth=20.0f;
                    r.vitals.foodLevel=20;
                    r.vitals.saturation=5.0f;
                    r.vitals.exhaustion=0.0f;
                    r.vitals.foodTimer=0;
                    r.player.health=r.server_player.health=20.0f;
                    r.mobs.player_hurt_time=0;
                    r.mobs.player_hurt_resistant=0;
                    r.mobs.player_last_damage=0.0f;
                    r.mobs.player_absorption=0.0f;
                    CHECK(gm_runtime_set_entity_id_cursor(&r,eid+which) &&
                          gm_runtime_set_math_random_seed48(&r,math_seed) &&
                          gm_runtime_set_world_random_seed48(&r,world_seed) &&
                          gm_runtime_set_player_random_seed48(&r,player_seed) &&
                          gm_runtime_set_next_falling_random_seed48(
                              &r,no_damage_seed) &&
                          gm_runtime_set_block(
                              &r,impact_x,base_y,impact_z,145,0) &&
                          gm_runtime_set_block(
                              &r,impact_x,base_y-1,impact_z,0,0),
                          "anvil hurt-immunity fixture stages");
                    gm_runtime_tick(&r,idle);
                    gm_runtime_tick(&r,idle);
                    for(int tick=2;tick<=12;++tick)
                        gm_runtime_tick(&r,idle);
                    r.vitals.health=20.0f;
                    r.player.health=r.server_player.health=20.0f;
                    r.vitals.exhaustion=0.0f;
                    r.mobs.player_hurt_resistant=21;
                    r.mobs.player_hurt_time=11;
                    r.mobs.player_last_damage=prior_damage[which];
                    gm_runtime_tick(&r,idle);
                    CHECK(r.falling_block_count==0 &&
                          r.falling_blocks[0].fall_time==13 &&
                          r.falling_blocks[0].impact_fall_distance==
                              2.90223908f &&
                          gm_world_block(
                              r.world,impact_x,base_y-3,impact_z)==145 &&
                          gm_world_meta(
                              r.world,impact_x,base_y-3,impact_z)==0 &&
                          r.next_entity_id==eid+which+1 &&
                          r.falling_blocks[0].random_seed48==
                              java_lcg_steps(no_damage_seed,1) &&
                          r.math_random_seed48==math_seed &&
                          r.mobs.player_random.seed==player_seed &&
                          r.world_random_seed48==world_seed &&
                          gm_runtime_scheduled_tick_count(&r)==1 &&
                          gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                          pending.x==impact_x&&pending.y==base_y-3&&
                          pending.z==impact_z&&pending.block==145&&
                          pending.time==r.clock.total_time+2&&
                          pending.priority==0&&pending.order==1,
                          "anvil hurt-immunity impact preserves landing and cursors");
                    CHECK(r.vitals.health==expected_health[which] &&
                          r.player.health==expected_health[which] &&
                          r.server_player.health==expected_health[which] &&
                          r.mobs.player_absorption==0.0f &&
                          r.mobs.player_hurt_resistant==20 &&
                          r.mobs.player_hurt_time==10 &&
                          r.mobs.player_last_damage==4.0f &&
                          fabsf(r.vitals.exhaustion-
                                expected_exhaustion[which])<1.0e-7f,
                          "anvil immunity rejects equal raw damage and applies larger delta");
                }
                r.player=player_before;
                r.server_player=server_player_before;
                r.vitals=vitals_before;
                r.dead=dead_before;
                r.mobs.player_hurt_time=hurt_before;
                r.mobs.player_hurt_resistant=resistant_before;
                r.mobs.player_last_damage=last_damage_before;
                r.mobs.player_absorption=absorption_before;
                r.mobs.player_random=player_random_before;
            }

            {
                const int drop_input_meta[4]={0,1,4,8};
                const int drop_item_meta[4]={0,0,1,2};
                for(int which=0;which<4;++which){
                    int eid=521100+which*2;
                    memset(&r.entities,0,sizeof r.entities);
                    memset(r.falling_blocks,0,sizeof r.falling_blocks);
                    r.falling_block_count=0;
                    r.scheduled_tick_count=0;
                    r.scheduled_tick_next_order=0;
                    r.next_falling_random_valid=0;
                    gm_runtime_set_do_entity_drops(&r,1);
                    for(int y=base_y-5;y<=base_y+1;++y)
                        for(int z=origin_z-2;z<=origin_z+2;++z)
                            for(int x=origin_x-2;x<=origin_x+2;++x)
                                gm_world_set_block_meta(r.world,x,y,z,0,0);
                    for(int z=origin_z-2;z<=origin_z+2;++z)
                        for(int x=origin_x-2;x<=origin_x+2;++x)
                            gm_world_set_block_meta(
                                r.world,x,base_y-4,z,1,0);
                    gm_world_set_block_meta(
                        r.world,origin_x,base_y-4,origin_z,44,0);
                    gm_world_set_block_meta(
                        r.world,origin_x,base_y-1,origin_z,1,0);
                    CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                          gm_runtime_set_math_random_seed48(&r,math_seed) &&
                          gm_runtime_set_world_random_seed48(&r,world_seed) &&
                          gm_runtime_set_next_falling_random_seed48(
                              &r,no_damage_seed) &&
                          gm_runtime_set_block(
                              &r,origin_x,base_y,origin_z,
                              145,drop_input_meta[which]) &&
                          gm_runtime_set_block(
                              &r,origin_x,base_y-1,origin_z,0,0),
                          "failed-placement anvil stages scheduled fall");
                    gm_runtime_tick(&r,idle);
                    gm_runtime_tick(&r,idle);
                    for(int tick=2;tick<=14;++tick)
                        gm_runtime_tick(&r,idle);
                    CHECK(r.falling_block_count==0 &&
                          r.falling_blocks[0].fall_time==14 &&
                          r.falling_blocks[0].impact_fall_distance==
                              3.36419439f &&
                          r.falling_blocks[0].random_seed48==
                              java_lcg_steps(no_damage_seed,1) &&
                          gm_world_block(
                              r.world,origin_x,base_y-4,origin_z)==44 &&
                          gm_world_meta(
                              r.world,origin_x,base_y-4,origin_z)==0 &&
                          gm_runtime_scheduled_tick_count(&r)==0 &&
                          r.entities.n_active==1 &&
                          r.entities.ents[0].eid==eid+1 &&
                          r.entities.ents[0].item==145 &&
                          r.entities.ents[0].count==1 &&
                          r.entities.ents[0].meta==drop_item_meta[which] &&
                          r.entities.ents[0].age==1 &&
                          r.entities.ents[0].pickup_delay==9 &&
                          r.next_entity_id==eid+2 &&
                          r.math_random_seed48==
                              java_lcg_steps(math_seed,8) &&
                          r.world_random_seed48==world_seed,
                          "failed-placement anvil drops damage tier, not facing");
                }
            }

            {
                const int instant_meta[4]={0,1,4,8};
                for(int which=0;which<4;++which){
                    int eid=521200+which;
                    memset(&r.entities,0,sizeof r.entities);
                    memset(r.falling_blocks,0,sizeof r.falling_blocks);
                    r.falling_block_count=0;
                    r.scheduled_tick_count=0;
                    r.scheduled_tick_next_order=0;
                    r.next_falling_random_valid=0;
                    for(int y=base_y-5;y<=base_y+1;++y)
                        for(int z=origin_z-2;z<=origin_z+2;++z)
                            for(int x=origin_x-2;x<=origin_x+2;++x)
                                gm_world_set_block_meta(r.world,x,y,z,0,0);
                    for(int z=origin_z-2;z<=origin_z+2;++z)
                        for(int x=origin_x-2;x<=origin_x+2;++x)
                            gm_world_set_block_meta(
                                r.world,x,base_y-4,z,1,0);
                    gm_world_set_block_meta(
                        r.world,origin_x,base_y-1,origin_z,1,0);
                    long long due=r.clock.total_time+2;
                    CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                          gm_runtime_set_math_random_seed48(&r,math_seed) &&
                          gm_runtime_set_world_random_seed48(&r,world_seed) &&
                          gm_runtime_set_next_falling_random_seed48(
                              &r,no_damage_seed) &&
                          gm_runtime_set_falling_instant(&r,1) &&
                          gm_runtime_set_block(
                              &r,origin_x,base_y,origin_z,
                              145,instant_meta[which]) &&
                          gm_runtime_set_block(
                              &r,origin_x,base_y-1,origin_z,0,0),
                          "instant anvil stages scheduled callback");
                    gm_runtime_tick(&r,idle);
                    gm_runtime_tick(&r,idle);
                    CHECK(r.falling_block_count==0 &&
                          gm_world_block(
                              r.world,origin_x,base_y,origin_z)==0 &&
                          gm_world_block(
                              r.world,origin_x,base_y-3,origin_z)==145 &&
                          gm_world_meta(
                              r.world,origin_x,base_y-3,origin_z)==
                              instant_meta[which] &&
                          gm_runtime_scheduled_tick_count(&r)==1 &&
                          gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                          pending.x==origin_x&&pending.y==base_y-3&&
                          pending.z==origin_z&&pending.block==145&&
                          pending.time==due+2&&pending.order==1&&
                          r.next_entity_id==eid &&
                          r.next_falling_random_valid &&
                          r.next_falling_random_seed48==no_damage_seed &&
                          r.math_random_seed48==math_seed &&
                          r.world_random_seed48==world_seed,
                          "instant anvil scans, preserves state, and uses no entity");
                }

                memset(r.falling_blocks,0,sizeof r.falling_blocks);
                r.falling_block_count=0;
                r.scheduled_tick_count=0;
                r.scheduled_tick_next_order=0;
                for(int y=base_y-5;y<=base_y+1;++y)
                    gm_world_set_block_meta(
                        r.world,origin_x+4,y,origin_z,0,0);
                gm_world_set_block_meta(
                    r.world,origin_x+4,base_y-4,origin_z,1,0);
                gm_world_set_block_meta(
                    r.world,origin_x+4,base_y,origin_z,12,0);
                long long sand_due=r.clock.total_time+2;
                int sand_eid=r.next_entity_id;
                CHECK(gm_runtime_schedule_tick(
                          &r,origin_x+4,base_y,origin_z,12,
                          sand_due,0,0),
                      "instant sand restores supported callback");
                gm_runtime_tick(&r,idle);
                gm_runtime_tick(&r,idle);
                CHECK(r.falling_block_count==0 &&
                      gm_world_block(
                          r.world,origin_x+4,base_y,origin_z)==0 &&
                      gm_world_block(
                          r.world,origin_x+4,base_y-3,origin_z)==12 &&
                      gm_runtime_scheduled_tick_count(&r)==1 &&
                      gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                      pending.block==12&&pending.y==base_y-3&&
                      pending.time==sand_due+2&&pending.order==1&&
                      r.next_entity_id==sand_eid,
                      "instant generic falling path preserves sand state");
                CHECK(gm_runtime_set_falling_instant(&r,0) &&
                      !gm_runtime_set_falling_instant(&r,2),
                      "instant falling mode validates and restores boolean state");
            }

            memset(r.falling_blocks,0,sizeof r.falling_blocks);
            r.falling_block_count=0;
            r.scheduled_tick_count=0;
            r.scheduled_tick_next_order=0;
            r.next_falling_random_valid=0;
            gm_world_set_block_meta(
                r.world,origin_x+4,base_y,origin_z,145,0);
            gm_world_set_block_meta(
                r.world,origin_x+4,base_y-1,origin_z,0,0);
            gm_world_set_block_meta(
                r.world,origin_x+4,base_y-4,origin_z,1,0);
            CHECK(gm_world_block(
                      r.world,origin_x+4,base_y-1,origin_z)==0 &&
                  !gm_runtime_schedule_tick(
                      &r,origin_x+4,base_y,origin_z,145,
                      r.clock.total_time+2,0,0) &&
                  gm_runtime_set_next_falling_random_seed48(
                      &r,no_damage_seed),
                  "falling anvil schedule restore rejects unavailable RNG state");
            gm_world_set_block_meta(
                r.world,origin_x+4,base_y-1,origin_z,1,0);
            CHECK(gm_runtime_schedule_tick(
                      &r,origin_x+4,base_y,origin_z,145,
                      r.clock.total_time+2,0,0) &&
                  r.next_falling_random_valid &&
                  r.next_falling_random_seed48==no_damage_seed,
                  "supported anvil schedule restore needs no entity RNG");
            CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                  pending.block==145&&pending.x==origin_x+4&&
                  pending.y==base_y&&pending.z==origin_z,
                  "supported anvil restore retains exact callback identity");
            r.scheduled_tick_count=0;
            r.scheduled_tick_next_order=0;
            gm_world_set_block_meta(
                r.world,origin_x+4,base_y,origin_z,145,12);
            gm_world_set_block_meta(
                r.world,origin_x+4,base_y-1,origin_z,1,0);
            CHECK(!gm_runtime_schedule_tick(
                      &r,origin_x+4,base_y,origin_z,145,
                      r.clock.total_time+2,0,0) &&
                  !gm_runtime_spawn_falling_fixture(
                      &r,521099,145,12,0,
                      origin_x+4.5,base_y,origin_z+0.5,
                      0.0,0.0,0.0,0,0),
                  "anvil restore and fixture reject noncanonical damage meta");
            r.next_falling_random_valid=0;
            r.scheduled_tick_count=0;
            r.scheduled_tick_next_order=0;
        }

        memset(r.falling_blocks,0,sizeof r.falling_blocks);
        r.falling_block_count=0;
        memset(r.projectiles,0,sizeof r.projectiles);
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;
        for(int y=76;y<=83;++y)
            for(int z=6;z<=10;++z)
                for(int x=8;x<=36;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,8,77,8,1,0);
        gm_runtime_set_pose_state(
            &r,8.5,78.0,8.5,0.0f,0.0f,
            0.0,0.0,0.0,1,0.0f);
        /* Four null-collision controls lie over valid stone supports. Each
         * falling entity overlaps its control before settlement, while the
         * small fireball is an inherited non-caller of doBlockCollisions. */
        const int control_x[4]={12,16,20,24};
        const int control_block[4]={132,70,72,147};
        long long falling_base=r.clock.total_time;
        for(int i=0;i<4;++i){
            gm_world_set_block_meta(
                r.world,control_x[i],77,8,1,0);
            if(control_block[i]!=132)
                gm_world_set_block_meta(
                    r.world,control_x[i],79,8,1,0);
            gm_world_set_block_meta(
                r.world,control_x[i],80,8,control_block[i],0);
            gm_world_set_block_meta(
                r.world,control_x[i],82,8,12,0);
            CHECK(gm_runtime_schedule_tick(
                      &r,control_x[i],82,8,12,
                      falling_base+1,0,i),
                  "falling-sand redstone control enters the exact queue");
        }
        gm_world_set_block_meta(r.world,28,80,8,132,0);
        CHECK(gm_runtime_spawn_small_fireball_fixture(
                  &r,309096,28.5,80.0,8.5,
                  0.0,0.0,0.0,0.0,0.0,0.0),
              "spawn zero-motion small-fireball tripwire negative");
        for(int tick=0;tick<10;++tick)
            gm_runtime_tick(&r,idle);
        CHECK(r.falling_block_count==1 &&
              r.falling_blocks[0].fall_time==10 &&
              fabs(r.falling_blocks[0].y-79.93686484559572)<1e-13,
              "supported-plate sand settles after the exact overlap tick");
        CHECK(gm_world_meta(r.world,12,80,8)==1,
              "falling sand activates tripwire");
        CHECK(gm_world_block(r.world,16,80,8)==70 &&
              gm_world_block(r.world,20,80,8)==72 &&
              gm_world_block(r.world,24,80,8)==147,
              "settled sand preserves nonreplaceable pressure plates");
        CHECK(gm_world_meta(r.world,28,80,8)==0 &&
              r.projectiles[0].active && r.projectiles[0].type==3,
              "small fireball remains a tripwire non-trigger");
        {
            int sand_ticks=0,stone_ticks=0,wood_ticks=0,gold_ticks=0;
            for(int i=0;i<gm_runtime_scheduled_tick_count(&r);++i){
                GmRuntimeScheduledTick pending;
                CHECK(gm_runtime_scheduled_tick_get(&r,i,&pending),
                      "falling control callback remains inspectable");
                sand_ticks+=pending.block==12;
                stone_ticks+=pending.block==70;
                wood_ticks+=pending.block==72;
                gold_ticks+=pending.block==147;
            }
            CHECK(gm_runtime_scheduled_tick_count(&r)==3 &&
                  sand_ticks==0&&stone_ticks==0&&
                  wood_ticks==1&&gold_ticks==1,
                  "falling controls retain exact sensitivity before settle");
        }
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;
        gm_world_set_block_meta(r.world,28,80,8,132,1);
        long long fireball_hold_base=r.clock.total_time;
        CHECK(gm_runtime_schedule_tick(
                  &r,28,80,8,132,fireball_hold_base+1,0,0),
              "powered fireball-overlapped wire schedules its hold poll");
        gm_runtime_tick(&r,idle);
        CHECK(gm_world_meta(r.world,28,80,8)==1,
              "small fireball holds an already-powered tripwire");
        CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
              r.scheduled_ticks[0].block==132 &&
              r.scheduled_ticks[0].time==fireball_hold_base+11,
              "small-fireball hold poll reschedules at the exact wire rate");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "redstone notification runtime initializes");
    if(r.world){
        {
            GmRuntimeScheduledTick torch_tick;
            for(int lit=0;lit<=1;++lit){
                for(int y=77;y<=80;++y)
                    for(int z=7;z<=10;++z)
                        for(int x=11;x<=14;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_world_set_block_meta(r.world,12,78,8,1,0);
                gm_world_set_block_meta(r.world,13,77,8,1,0);
                gm_world_set_block_meta(r.world,12,78,9,1,0);
                gm_world_set_block_meta(r.world,13,78,8,94,3);
                gm_world_set_block_meta(r.world,12,79,9,93,2);
                r.scheduled_tick_count=0;
                r.scheduled_tick_next_order=0;
                long long torch_due=r.clock.total_time+2;
                CHECK(gm_runtime_set_world_random_seed48(
                          &r,UINT64_C(0)) &&
                      gm_runtime_set_math_random_seed48(
                          &r,UINT64_C(0x0FEDCBA98765)) &&
                      gm_runtime_set_block(
                          &r,12,79,8,lit?76:75,5) &&
                      gm_runtime_scheduled_tick_count(&r)==lit*3 &&
                      r.entities.n_active==0 &&
                      r.world_random_seed48==UINT64_C(0) &&
                      r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
                      "direct torch add preserves exact callback count");
                if(lit){
                    CHECK(gm_runtime_scheduled_tick_get(
                              &r,0,&torch_tick) &&
                          torch_tick.block==94&&torch_tick.x==13&&
                          torch_tick.y==78&&torch_tick.z==8&&
                          torch_tick.time==torch_due&&
                          torch_tick.priority==-2 &&
                          gm_runtime_scheduled_tick_get(
                              &r,1,&torch_tick) &&
                          torch_tick.block==93&&torch_tick.x==12&&
                          torch_tick.y==79&&torch_tick.z==9&&
                          torch_tick.time==torch_due&&
                          torch_tick.priority==-1 &&
                          gm_runtime_scheduled_tick_get(
                              &r,2,&torch_tick) &&
                          torch_tick.block==76&&torch_tick.x==12&&
                          torch_tick.y==79&&torch_tick.z==8&&
                          torch_tick.time==torch_due&&
                          torch_tick.priority==0,
                          "lit torch add preserves callback priority order");
                }
            }
        }
        {
            static const int torch_id[3]={76,76,75};
            static const int indirect_power[3]={1,0,1};
            static const int settled_id[3]={75,76,75};
            static const int toggle_count[3]={1,0,0};
            GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
            for(int index=0;index<3;++index){
                GmRuntimeScheduledTick pending;
                for(int y=76;y<=79;++y)
                    for(int z=6;z<=10;++z)
                        for(int x=10;x<=15;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                for(int z=6;z<=10;++z)
                    for(int x=10;x<=15;++x)
                        gm_world_set_block_meta(r.world,x,77,z,1,0);
                r.scheduled_tick_count=0;
                r.scheduled_tick_next_order=0;
                r.redstone_torch_toggle_count=0;
                if(indirect_power[index]){
                    CHECK(gm_runtime_load_block(&r,13,76,8,1,0) &&
                          gm_runtime_load_block(&r,14,77,8,152,0) &&
                          gm_runtime_load_block(&r,13,77,8,94,3),
                          "saved indirect torch power source loads cold");
                }
                CHECK(gm_runtime_load_block(
                          &r,12,78,8,torch_id[index],5),
                      "saved floor torch state loads cold");
                long long due=r.clock.total_time+2;
                CHECK(gm_runtime_schedule_tick(
                          &r,12,78,8,torch_id[index],due,0,0) &&
                      gm_runtime_scheduled_tick_count(&r)==1 &&
                      gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                      pending.block==torch_id[index] &&
                      pending.x==12&&pending.y==78&&pending.z==8 &&
                      pending.time==due&&pending.priority==0&&pending.order==0,
                      "saved indirect or stale torch callback restores exactly");
                gm_runtime_tick(&r,idle);
                CHECK(gm_world_block(r.world,12,78,8)==torch_id[index] &&
                      gm_runtime_scheduled_tick_count(&r)==1,
                      "saved torch callback remains pending through +1");
                gm_runtime_tick(&r,idle);
                CHECK(gm_world_block(r.world,12,78,8)==settled_id[index] &&
                      gm_world_meta(r.world,12,78,8)==5 &&
                      gm_runtime_scheduled_tick_count(&r)==0 &&
                      r.redstone_torch_toggle_count==toggle_count[index],
                      "saved indirect or stale torch callback settles at +2");
            }
            gm_world_set_block_meta(r.world,12,77,8,0,0);
            gm_world_set_block_meta(r.world,12,78,8,76,5);
            CHECK(!gm_runtime_schedule_tick(
                      &r,12,78,8,76,r.clock.total_time+2,0,0),
                  "unsupported saved torch callback remains rejected");
        }
        {
            static const int support_dx[5]={0,-1,1,0,0};
            static const int support_dz[5]={0,0,0,-1,1};
            GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
            for(int meta=1;meta<=4;++meta){
                for(int powered=0;powered<=1;++powered){
                    GmRuntimeScheduledTick pending;
                    int torch=powered?76:75;
                    int settled=powered?75:76;
                    for(int y=78;y<=80;++y)
                        for(int z=6;z<=10;++z)
                            for(int x=10;x<=14;++x)
                                gm_world_set_block_meta(r.world,x,y,z,0,0);
                    r.scheduled_tick_count=0;
                    r.scheduled_tick_next_order=0;
                    r.redstone_torch_toggle_count=0;
                    CHECK(gm_runtime_load_block(
                              &r,12+support_dx[meta],79,
                              8+support_dz[meta],powered?152:1,0) &&
                          gm_runtime_load_block(&r,12,79,8,torch,meta),
                          "saved wall-torch fixture loads cold");
                    long long due=r.clock.total_time+2;
                    CHECK(gm_runtime_schedule_tick(
                              &r,12,79,8,torch,due,0,0) &&
                          gm_runtime_scheduled_tick_count(&r)==1 &&
                          gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                          pending.block==torch&&pending.x==12&&
                          pending.y==79&&pending.z==8&&
                          pending.time==due&&pending.priority==0&&
                          pending.order==0,
                          "saved wall-torch callback restores exactly");
                    gm_runtime_tick(&r,idle);
                    CHECK(gm_world_block(r.world,12,79,8)==torch &&
                          gm_runtime_scheduled_tick_count(&r)==1,
                          "saved wall-torch callback remains pending at +1");
                    gm_runtime_tick(&r,idle);
                    CHECK(gm_world_block(r.world,12,79,8)==settled &&
                          gm_world_meta(r.world,12,79,8)==meta &&
                          gm_runtime_scheduled_tick_count(&r)==0 &&
                          r.redstone_torch_toggle_count==powered,
                          "saved wall-torch callback settles exactly at +2");
                }
            }
            gm_world_set_block_meta(r.world,12,79,9,0,0);
            gm_world_set_block_meta(r.world,12,79,8,76,4);
            CHECK(!gm_runtime_schedule_tick(
                      &r,12,79,8,76,r.clock.total_time+2,0,0),
                  "unsupported saved wall-torch callback remains rejected");
        }
        {
            static const int torch_meta[16]={
                5,5,5,5,1,1,5,5,5,5,5,5,5,5,5,5};
            static const int support_dx[16]={
                0,0,0,0,-1,-1,0,0,0,0,0,0,0,0,0,0};
            static const int support_dy[16]={
                -1,-1,-1,-1,0,0,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
            static const int support_id[16]={
                44,53,78,154,60,53,85,113,188,189,190,191,192,20,95,139};
            static const int support_meta[16]={
                8,4,7,0,0,0,0,0,0,0,0,0,0,0,0,0};
            for(int index=0;index<16;++index){
                GmRuntimeScheduledTick pending;
                for(int y=77;y<=80;++y)
                    for(int z=6;z<=10;++z)
                        for(int x=10;x<=14;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                r.scheduled_tick_count=0;
                r.scheduled_tick_next_order=0;
                r.redstone_torch_toggle_count=0;
                CHECK(gm_runtime_load_block(
                          &r,12+support_dx[index],79+support_dy[index],8,
                          support_id[index],support_meta[index]) &&
                      gm_runtime_load_block(
                          &r,12,79,8,75,torch_meta[index]),
                      "directional non-cube torch support loads cold");
                long long due=r.clock.total_time+2;
                CHECK(gm_runtime_schedule_tick(
                          &r,12,79,8,75,due,0,0) &&
                      gm_runtime_scheduled_tick_count(&r)==1 &&
                      gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                      pending.block==75&&pending.x==12&&pending.y==79&&
                      pending.z==8&&pending.time==due&&
                      pending.priority==0&&pending.order==0,
                      "directional non-cube torch callback is admitted exactly");
            }
        }
        {
            static const int torch_meta[6]={5,5,5,1,1,1};
            static const int support_dx[6]={0,0,0,-1,-1,-1};
            static const int support_dy[6]={-1,-1,-1,0,0,0};
            static const int support_id[6]={44,53,78,154,53,85};
            static const int support_meta[6]={0,0,6,0,1,0};
            for(int index=0;index<6;++index){
                for(int y=77;y<=80;++y)
                    for(int z=6;z<=10;++z)
                        for(int x=10;x<=14;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                r.scheduled_tick_count=0;
                r.scheduled_tick_next_order=0;
                CHECK(gm_runtime_load_block(
                          &r,12+support_dx[index],79+support_dy[index],8,
                          support_id[index],support_meta[index]) &&
                      gm_runtime_load_block(
                          &r,12,79,8,75,torch_meta[index]) &&
                      !gm_runtime_schedule_tick(
                          &r,12,79,8,75,r.clock.total_time+2,0,0) &&
                      gm_runtime_scheduled_tick_count(&r)==0,
                      "invalid directional torch support rejects saved callback");
            }
        }
        {
            static const int requested[3]={124,123,123};
            static const int powered[3]={0,1,0};
            static const int settled[3]={123,124,123};
            for(int index=0;index<3;++index){
                memset(&r.entities,0,sizeof r.entities);
                r.scheduled_tick_count=0;
                r.scheduled_tick_next_order=0;
                for(int y=77;y<=79;++y)
                    for(int z=7;z<=9;++z)
                        for(int x=11;x<=13;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                if(powered[index])
                    gm_world_set_block_meta(r.world,11,78,8,152,0);
                CHECK(gm_runtime_set_world_random_seed48(
                          &r,UINT64_C(0)) &&
                      gm_runtime_set_math_random_seed48(
                          &r,UINT64_C(0x0FEDCBA98765)) &&
                      gm_runtime_set_block(
                          &r,12,78,8,requested[index],0) &&
                      gm_world_block(r.world,12,78,8)==settled[index] &&
                      gm_world_meta(r.world,12,78,8)==0 &&
                      gm_runtime_scheduled_tick_count(&r)==0 &&
                      r.entities.n_active==0 &&
                      r.world_random_seed48==UINT64_C(0) &&
                      r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
                      "direct lamp add normalizes immediately without work");
            }
        }
        {
            const uint64_t math_seed=UINT64_C(0x0123456789AB);
            GmAction tnt_idle;memset(&tnt_idle,0,sizeof tnt_idle);
            GmPlayerView tnt_player_before;
            int tnt_hotbar_before=r.player.inv.current_item;
            ICStack tnt_slot0_before=isr_get_stack(&r.player.inv,0);
            gm_runtime_view(&r,&tnt_player_before);
            tnt_idle.hotbar_sel=-1;
            memset(r.primed_tnt,0,sizeof r.primed_tnt);
            r.primed_tnt_count=0;
            for(int y=77;y<=80;++y)
                for(int z=7;z<=9;++z)
                    for(int x=11;x<=13;++x)
                        gm_world_set_block_meta(r.world,x,y,z,0,0);
            gm_world_set_block_meta(r.world,12,77,8,1,0);
            CHECK(gm_runtime_load_block(&r,12,78,8,46,0) &&
                  gm_runtime_set_entity_id_cursor(&r,9001) &&
                  gm_runtime_set_math_random_seed48(&r,math_seed) &&
                  gm_runtime_set_block(&r,11,78,8,152,0) &&
                  gm_world_block(r.world,12,78,8)==0 &&
                  r.primed_tnt_count==1 && r.primed_tnt[0].active &&
                  r.primed_tnt[0].eid==9001 &&
                  r.primed_tnt[0].fuse==80 &&
                  r.next_entity_id==9002 &&
                  r.math_random_seed48==java_lcg_steps(math_seed,2),
                  "powered TNT primes atomically with exact constructor cursors");
            for(int tick=0;tick<79;++tick)
                gm_runtime_tick(&r,tnt_idle);
            CHECK(r.primed_tnt_count==1 && r.primed_tnt[0].active &&
                  r.primed_tnt[0].fuse==1 &&
                  r.primed_tnt[0].y==78.0,
                  "primed TNT retains the exact 79-tick pre-explosion fuse");
            memset(r.primed_tnt,0,sizeof r.primed_tnt);
            r.primed_tnt_count=0;
            gm_world_set_block_meta(r.world,12,78,8,0,0);
            CHECK(gm_runtime_set_entity_id_cursor(&r,9101) &&
                  gm_runtime_set_math_random_seed48(&r,math_seed) &&
                  gm_runtime_set_block(&r,12,78,8,46,0) &&
                  gm_world_block(r.world,12,78,8)==0 &&
                  r.primed_tnt_count==1 && r.primed_tnt[0].active &&
                  r.primed_tnt[0].eid==9101 &&
                  r.primed_tnt[0].fuse==80 &&
                  r.next_entity_id==9102 &&
                  r.math_random_seed48==java_lcg_steps(math_seed,2),
                  "direct powered TNT placement primes during on-add");
            memset(r.primed_tnt,0,sizeof r.primed_tnt);
            r.primed_tnt_count=0;
            gm_world_set_block_meta(r.world,11,78,8,0,0);
            gm_world_set_block_meta(r.world,12,78,8,46,0);
            gm_runtime_set_pose(&r,12.5,78.0,10.5,0.0f,0.0f);
            r.player.inv.current_item=0;
            isr_set_stack(&r.player.inv,0,ic_mk(259,1,0));
            CHECK(gm_runtime_set_entity_id_cursor(&r,9201) &&
                  gm_runtime_set_math_random_seed48(&r,math_seed) &&
                  gm_runtime_use_block(&r,12,78,8) &&
                  gm_world_block(r.world,12,78,8)==0 &&
                  r.primed_tnt_count==1 && r.primed_tnt[0].active &&
                  r.primed_tnt[0].eid==9201 &&
                  r.primed_tnt[0].fuse==80 &&
                  isr_get_stack(&r.player.inv,0).item==259 &&
                  isr_get_stack(&r.player.inv,0).count==1 &&
                  isr_get_stack(&r.player.inv,0).meta==1 &&
                  r.next_entity_id==9202 &&
                  r.math_random_seed48==java_lcg_steps(math_seed,2),
                  "flint-and-steel TNT use primes and damages the tool once");
            memset(r.primed_tnt,0,sizeof r.primed_tnt);
            r.primed_tnt_count=0;
            gm_world_set_block_meta(r.world,12,78,8,46,0);
            isr_set_stack(&r.player.inv,0,ic_mk(385,1,0));
            CHECK(gm_runtime_set_entity_id_cursor(&r,9301) &&
                  gm_runtime_set_math_random_seed48(&r,math_seed) &&
                  gm_runtime_use_block(&r,12,78,8) &&
                  gm_world_block(r.world,12,78,8)==0 &&
                  r.primed_tnt_count==1 && r.primed_tnt[0].active &&
                  r.primed_tnt[0].eid==9301 &&
                  isr_get_stack(&r.player.inv,0).count==0 &&
                  r.next_entity_id==9302 &&
                  r.math_random_seed48==java_lcg_steps(math_seed,2),
                  "fire-charge TNT use primes and consumes the last charge");
            memset(r.primed_tnt,0,sizeof r.primed_tnt);
            r.primed_tnt_count=0;
            gm_world_set_block_meta(r.world,12,78,8,46,0);
            isr_set_stack(&r.player.inv,0,ic_mk(1,1,0));
            CHECK(gm_runtime_set_entity_id_cursor(&r,9401) &&
                  gm_runtime_set_math_random_seed48(&r,math_seed) &&
                  !gm_runtime_use_block(&r,12,78,8) &&
                  gm_world_block(r.world,12,78,8)==46 &&
                  r.primed_tnt_count==0 &&
                  isr_get_stack(&r.player.inv,0).item==1 &&
                  r.next_entity_id==9401 &&
                  r.math_random_seed48==math_seed,
                  "non-igniter TNT use is an exact refusal");
            for(int index=0;index<GM_RUNTIME_PRIMED_TNT;++index){
                memset(&r.primed_tnt[index],0,sizeof r.primed_tnt[index]);
                r.primed_tnt[index].active=1;
            }
            r.primed_tnt_count=GM_RUNTIME_PRIMED_TNT;
            isr_set_stack(&r.player.inv,0,ic_mk(259,1,7));
            CHECK(gm_runtime_set_entity_id_cursor(&r,9501) &&
                  gm_runtime_set_math_random_seed48(&r,math_seed) &&
                  !gm_runtime_use_block(&r,12,78,8) &&
                  gm_world_block(r.world,12,78,8)==46 &&
                  isr_get_stack(&r.player.inv,0).item==259 &&
                  isr_get_stack(&r.player.inv,0).count==1 &&
                  isr_get_stack(&r.player.inv,0).meta==7 &&
                  r.next_entity_id==9501 &&
                  r.math_random_seed48==math_seed,
                  "full primed-TNT pool rejects player ignition atomically");
            memset(r.primed_tnt,0,sizeof r.primed_tnt);
            r.primed_tnt_count=0;
            memset(r.projectiles,0,sizeof r.projectiles);
            gm_world_set_block_meta(r.world,12,78,8,46,0);
            CHECK(gm_runtime_set_entity_id_cursor(&r,9601) &&
                  gm_runtime_set_math_random_seed48(&r,math_seed) &&
                  gm_runtime_spawn_arrow_fixture(
                      &r,9600,12.5,78.0,8.5,0.0,0.0,0.0,1,100),
                  "stationary burning-arrow TNT fixture spawns");
            gm_runtime_tick(&r,tnt_idle);
            CHECK(gm_world_block(r.world,12,78,8)==0 &&
                  r.projectiles[0].active &&
                  r.projectiles[0].age==1 &&
                  r.projectiles[0].fire_ticks==99 &&
                  r.primed_tnt_count==1 && r.primed_tnt[0].active &&
                  r.primed_tnt[0].eid==9601 &&
                  r.primed_tnt[0].fuse==79 &&
                  r.next_entity_id==9602 &&
                  r.math_random_seed48==java_lcg_steps(math_seed,2),
                  "burning arrow collision primes and ticks TNT in one boundary");
            memset(r.primed_tnt,0,sizeof r.primed_tnt);
            r.primed_tnt_count=0;
            memset(r.projectiles,0,sizeof r.projectiles);
            gm_world_set_block_meta(r.world,12,78,8,46,0);
            CHECK(gm_runtime_set_entity_id_cursor(&r,9701) &&
                  gm_runtime_set_math_random_seed48(&r,math_seed) &&
                  gm_runtime_spawn_arrow_fixture(
                      &r,9700,12.5,78.0,8.5,0.0,0.0,0.0,1,1),
                  "one-tick-fire arrow TNT negative fixture spawns");
            gm_runtime_tick(&r,tnt_idle);
            CHECK(gm_world_block(r.world,12,78,8)==46 &&
                  r.projectiles[0].active &&
                  r.projectiles[0].fire_ticks==0 &&
                  r.primed_tnt_count==0 &&
                  r.next_entity_id==9701 &&
                  r.math_random_seed48==math_seed,
                  "expired arrow fire does not ignite TNT");
            memset(r.projectiles,0,sizeof r.projectiles);
            /* The negative arrow fixture deliberately leaves its TNT block in
             * place. Remove it before the isolated explosion cases: it lies
             * inside this blast radius and would correctly become a chain TNT,
             * contaminating the no-chain crater assertion below. */
            gm_world_set_block_meta(r.world,12,78,8,0,0);
            {
                static const int glass[6][3]={
                    {15,83,8},{17,83,8},{16,83,7},
                    {16,83,9},{16,82,8},{16,84,8}
                };
                const uint64_t world_seed=UINT64_C(0x02468ACE1357);
                memset(r.primed_tnt,0,sizeof r.primed_tnt);
                r.primed_tnt_count=0;
                for(int index=0;index<6;++index)
                    gm_world_set_block_meta(
                        r.world,glass[index][0],glass[index][1],
                        glass[index][2],20,0);
                gm_runtime_set_pose(&r,8.5,78.0,8.5,-180.0f,0.0f);
                CHECK(gm_runtime_set_entity_id_cursor(&r,9801) &&
                      gm_runtime_set_world_random_seed48(&r,world_seed) &&
                      gm_runtime_spawn_primed_tnt_fixture(
                          &r,9800,16.5,83.0,8.5,0.0,0.0,0.0,1),
                      "one-tick primed-TNT save-state fixture spawns");
                gm_runtime_tick(&r,tnt_idle);
                int crater=1;
                for(int index=0;index<6;++index){
                    crater=crater && gm_world_block(
                        r.world,glass[index][0],glass[index][1],
                        glass[index][2])==0;
                    gm_world_set_block_meta(
                        r.world,glass[index][0],glass[index][1],
                        glass[index][2],0,0);
                }
                CHECK(crater && r.primed_tnt_count==0 &&
                      r.next_entity_id==9801 &&
                      r.world_random_seed48==java_lcg_steps(world_seed,1354),
                      "fuse-zero TNT removes the exact six-cell glass crater");
                memset(r.primed_tnt,0,sizeof r.primed_tnt);
                r.primed_tnt_count=0;
                for(int index=0;index<6;++index)
                    gm_world_set_block_meta(
                        r.world,glass[index][0],glass[index][1],
                        glass[index][2],index==0?46:20,0);
                const uint64_t chain_world_seed=UINT64_C(135120319782334);
                CHECK(gm_runtime_set_entity_id_cursor(&r,9901) &&
                      gm_runtime_set_world_random_seed48(
                          &r,chain_world_seed) &&
                      gm_runtime_set_math_random_seed48(&r,math_seed) &&
                      gm_runtime_spawn_primed_tnt_fixture(
                          &r,9900,16.5,83.0,8.5,0.0,0.0,0.0,1),
                      "explosion-chain primed-TNT fixture spawns");
                gm_runtime_tick(&r,tnt_idle);
                crater=1;
                for(int index=0;index<6;++index){
                    crater=crater && gm_world_block(
                        r.world,glass[index][0],glass[index][1],
                        glass[index][2])==0;
                    gm_world_set_block_meta(
                        r.world,glass[index][0],glass[index][1],
                        glass[index][2],0,0);
                }
                CHECK(crater && r.primed_tnt_count==1 &&
                      !r.primed_tnt[0].active &&
                      r.primed_tnt[1].active &&
                      r.primed_tnt[1].eid==9901 &&
                      r.primed_tnt[1].fuse==9 &&
                      r.next_entity_id==9902 &&
                      r.world_random_seed48==
                          java_lcg_steps(chain_world_seed,1355) &&
                      r.math_random_seed48==java_lcg_steps(math_seed,2),
                      "explosion-hit TNT primes with exact short fuse and cursors");
                memset(r.primed_tnt,0,sizeof r.primed_tnt);
                r.primed_tnt_count=0;
                {
                    PvStats vitals_before=r.vitals;
                    int hurt_before=r.mobs.player_hurt_time;
                    int resistant_before=r.mobs.player_hurt_resistant;
                    float last_damage_before=r.mobs.player_last_damage;
                    float absorption_before=r.mobs.player_absorption;
                    const uint64_t blast_seed=
                        UINT64_C(135120319782334);
                    r.vitals.health=20.0f;
                    r.vitals.maxHealth=20.0f;
                    r.vitals.foodLevel=20;
                    r.vitals.saturation=5.0f;
                    r.vitals.exhaustion=0.05f;
                    r.vitals.foodTimer=0;
                    r.mobs.player_hurt_time=0;
                    r.mobs.player_hurt_resistant=0;
                    r.mobs.player_last_damage=0.0f;
                    r.mobs.player_absorption=0.0f;
                    r.player.health=r.server_player.health=20.0f;
                    for(int y=78;y<=84;++y)
                        for(int z=6;z<=10;++z)
                            for(int x=7;x<=15;++x)
                                gm_world_set_block_meta(
                                    r.world,x,y,z,0,0);
                    gm_world_set_block_meta(r.world,8,77,8,1,0);
                    gm_runtime_set_pose_state(
                        &r,8.5,78.0,8.5,-180.0f,0.0f,
                        0.0,-0.0784000015258789,0.0,1,0.0f);
                    CHECK(gm_runtime_set_entity_id_cursor(&r,9951) &&
                          gm_runtime_set_world_random_seed48(&r,blast_seed) &&
                          gm_runtime_spawn_primed_tnt_fixture(
                              &r,9950,14.5,82.0,8.5,
                              0.0,0.0,0.0,1),
                          "open-air player-blast fixture spawns");
                    gm_runtime_tick(&r,tnt_idle);
                    CHECK(r.primed_tnt_count==0 &&
                          r.world_random_seed48==
                              java_lcg_steps(blast_seed,1354),
                          "open-air TNT blast retires with exact world cursor");
                    CHECK(r.vitals.health==17.0f &&
                          r.vitals.foodTimer==1 &&
                          fabsf(r.vitals.exhaustion-0.15f)<1.0e-7f &&
                          r.mobs.player_hurt_time==9,
                          "open-air TNT blast matches player damage lifecycle");
                    CHECK(
                          fabs(r.player.ent.posX-8.409875)<1.0e-15 &&
                          fabs(r.player.ent.posY-78.0)<1.0e-15 &&
                          fabs(r.player.ent.motionX-
                              (-0.0492082557156682))<1.0e-15 &&
                          fabs(r.player.ent.motionY-
                              (-0.0784000015258789))<1.0e-15 &&
                          r.player.ent.onGround,
                          "open-air TNT blast matches client packet response");
                    memset(r.primed_tnt,0,sizeof r.primed_tnt);
                    r.primed_tnt_count=0;
                    r.vitals.health=20.0f;
                    r.vitals.maxHealth=20.0f;
                    r.vitals.foodLevel=20;
                    r.vitals.saturation=5.0f;
                    r.vitals.exhaustion=0.05f;
                    r.vitals.foodTimer=0;
                    r.mobs.player_hurt_time=0;
                    r.mobs.player_hurt_resistant=0;
                    r.mobs.player_last_damage=0.0f;
                    r.mobs.player_absorption=0.0f;
                    r.player.health=r.server_player.health=20.0f;
                    for(int y=78;y<=84;++y)
                        for(int z=6;z<=10;++z)
                            for(int x=7;x<=15;++x)
                                gm_world_set_block_meta(
                                    r.world,x,y,z,0,0);
                    gm_world_set_block_meta(r.world,8,77,8,1,0);
                    gm_world_set_block_meta(r.world,9,78,8,20,0);
                    gm_runtime_set_pose_state(
                        &r,8.5,78.0,8.5,-180.0f,0.0f,
                        0.0,-0.0784000015258789,0.0,1,0.0f);
                    CHECK(gm_runtime_set_entity_id_cursor(&r,9961) &&
                          gm_runtime_set_world_random_seed48(&r,blast_seed) &&
                          gm_runtime_spawn_primed_tnt_fixture(
                              &r,9960,13.5,82.0,8.5,
                              0.0,0.0,0.0,1),
                          "obstructed player-blast fixture spawns");
                    gm_runtime_tick(&r,tnt_idle);
                    CHECK(r.primed_tnt_count==0,
                          "obstructed TNT blast retires its primed entity");
                    CHECK(r.world_random_seed48==
                              java_lcg_steps(blast_seed,1354),
                          "obstructed TNT blast retains exact world cursor");
                    CHECK(gm_world_block(r.world,9,78,8)==20,
                          "obstructed TNT blast retains its glass occluder");
                    CHECK(r.vitals.health==16.0f &&
                          r.vitals.foodTimer==1 &&
                          fabsf(r.vitals.exhaustion-0.15f)<1.0e-7f &&
                          r.mobs.player_hurt_time==9,
                          "obstructed TNT blast matches player damage lifecycle");
                    CHECK(
                          fabs(r.player.ent.posX-8.404875)<1.0e-15 &&
                          fabs(r.player.ent.posY-78.0)<1.0e-15 &&
                          fabs(r.player.ent.motionX-
                              (-0.05193825603276491))<1.0e-15 &&
                          fabs(r.player.ent.motionY-
                              (-0.0784000015258789))<1.0e-15 &&
                          r.player.ent.onGround,
                          "obstructed TNT blast matches 24/45-ray response");
                    r.vitals=vitals_before;
                    r.mobs.player_hurt_time=hurt_before;
                    r.mobs.player_hurt_resistant=resistant_before;
                    r.mobs.player_last_damage=last_damage_before;
                    r.mobs.player_absorption=absorption_before;
                    r.player.health=r.server_player.health=r.vitals.health;
                    r.player.food=r.server_player.food=
                        (float)r.vitals.foodLevel;
                }
            }
            isr_set_stack(&r.player.inv,0,ic_empty());
            r.player.inv.current_item=tnt_hotbar_before;
            isr_set_stack(&r.player.inv,0,tnt_slot0_before);
            gm_runtime_set_pose(
                &r,tnt_player_before.x,tnt_player_before.y,
                tnt_player_before.z,tnt_player_before.yaw,
                tnt_player_before.pitch);
            gm_world_set_block_meta(r.world,12,78,8,0,0);
        }
        for(int y=77;y<=79;++y)
            for(int z=7;z<=9;++z)
                for(int x=11;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        CHECK(gm_runtime_set_block(&r,12,78,8,152,0),
              "redstone block edit enters the notifying set path");
        CHECK(gm_world_block(r.world,12,78,8)==152 &&
              gm_world_block(r.world,13,78,8)==124,
              "WEST/EAST neighbor traversal powers the adjacent lamp");
        long long lamp_due = r.clock.total_time + 4;
        CHECK(gm_runtime_set_block(&r,12,78,8,0,0),
              "redstone-block removal enters the notifying set path");
        GmRuntimeScheduledTick lamp_tick;
        CHECK(gm_world_block(r.world,13,78,8)==124 &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
              lamp_tick.block==124&&lamp_tick.x==13&&lamp_tick.y==78&&
              lamp_tick.z==8&&lamp_tick.time==lamp_due&&
              lamp_tick.priority==0,
              "unpowered lit lamp remains lit and schedules exact +4 update");
        GmAction lamp_idle;memset(&lamp_idle,0,sizeof lamp_idle);
        lamp_idle.hotbar_sel=-1;
        for(int tick=0;tick<3;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,13,78,8)==124 &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "lamp-off update remains pending through total-time +3");
        gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,13,78,8)==123 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "lamp turns off exactly when its +4 update dispatches");
        gm_world_set_block_meta(r.world,12,78,8,152,0);
        gm_world_set_block_meta(r.world,13,78,8,124,0);
        long long powered_due = r.clock.total_time + 3;
        CHECK(gm_runtime_schedule_tick(
                  &r,13,78,8,124,powered_due,0,0),
              "powered lit-lamp saved callback enters exact scheduler slice");
        for(int tick=0;tick<3;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,13,78,8)==124 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "powered lamp drains due callback without turning off");
        gm_world_set_block_meta(r.world,12,78,8,69,5);
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        CHECK(gm_runtime_set_block(&r,12,78,8,69,13) &&
              gm_world_block(r.world,13,78,8)==124,
              "powered lever metadata provides direct weak power to lamp");
        for(int y=78;y<=79;++y)
            for(int x=20;x<=21;++x)
                gm_world_set_block_meta(r.world,x,y,8,0,0);
        gm_world_set_block_meta(r.world,20,78,8,1,0);
        gm_world_set_block_meta(r.world,20,79,8,69,13);
        CHECK(gm_runtime_set_block(&r,21,78,8,123,0) &&
              gm_world_block(r.world,21,78,8)==124,
              "floor lever strongly powers stone into an on-add lamp");
        for(int y=78;y<=79;++y)
            for(int x=23;x<=27;++x)
                gm_world_set_block_meta(r.world,x,y,8,0,0);
        gm_world_set_block_meta(r.world,23,78,8,5,0);
        gm_world_set_block_meta(r.world,23,79,8,69,13);
        CHECK(gm_runtime_set_block(&r,24,78,8,123,0) &&
              gm_world_block(r.world,24,78,8)==124,
              "registry normal-cube planks carry lever strong power");
        long long plank_lamp_due = r.clock.total_time + 4;
        CHECK(gm_runtime_set_block(&r,23,79,8,0,0) &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
              lamp_tick.block==124&&lamp_tick.x==24&&lamp_tick.y==78&&
              lamp_tick.z==8&&lamp_tick.time==plank_lamp_due,
              "plank strong-power loss schedules the exact lamp callback");
        for(int tick=0;tick<4;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,24,78,8)==123 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "registry-backed plank lamp turns off at +4");
        gm_world_set_block_meta(r.world,26,78,8,20,0);
        gm_world_set_block_meta(r.world,26,79,8,69,13);
        CHECK(gm_runtime_set_block(&r,27,78,8,123,0) &&
              gm_world_block(r.world,27,78,8)==123,
              "registry non-normal glass does not carry indirect power");
        gm_world_set_block_meta(r.world,21,78,8,0,0);
        gm_world_set_block_meta(r.world,20,79,8,69,9);
        CHECK(gm_runtime_set_block(&r,21,78,8,123,0) &&
              gm_world_block(r.world,21,78,8)==123,
              "wrong-facing wall lever does not strongly power stone");
        gm_world_set_block_meta(r.world,20,79,8,69,13);
        gm_world_set_block_meta(r.world,21,78,8,0,0);
        CHECK(gm_runtime_set_block(&r,21,78,8,123,0) &&
              gm_world_block(r.world,21,78,8)==124,
              "strongly powered on-add lamp relights from its stone support");
        long long indirect_lamp_due = r.clock.total_time + 4;
        CHECK(gm_runtime_set_block(&r,20,79,8,0,0) &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
              lamp_tick.block==124&&lamp_tick.x==21&&lamp_tick.y==78&&
              lamp_tick.z==8&&lamp_tick.time==indirect_lamp_due,
              "powered-lever removal notifies around its strong-powered support");
        for(int tick=0;tick<4;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,21,78,8)==123 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "indirectly powered lamp turns off at its exact +4 callback");
        memset(&r.entities,0,sizeof r.entities);
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;
        for(int y=77;y<=79;++y)
            for(int z=7;z<=9;++z)
                for(int x=11;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,8,1,0);
        gm_world_set_block_meta(r.world,12,79,8,69,13);
        gm_world_set_block_meta(r.world,13,78,8,124,0);
        gm_world_set_block_meta(r.world,13,79,8,1,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,7400) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,13,79,8,0,0) &&
              gm_world_meta(r.world,12,79,8)==13 &&
              gm_world_block(r.world,13,78,8)==124 &&
              r.entities.n_active==0 &&
              gm_runtime_scheduled_tick_count(&r)==0 &&
              r.world_random_seed48==UINT64_C(0),
              "unrelated attachment removal keeps floor lever and power");
        gm_world_set_block_meta(r.world,13,79,8,1,0);
        long long lever_support_lamp_due=r.clock.total_time+4;
        CHECK(gm_runtime_set_block(&r,12,78,8,0,0) &&
              gm_world_block(r.world,12,79,8)==0 &&
              r.entities.n_active==1 &&
              r.entities.ents[0].eid==7400 &&
              r.entities.ents[0].item==69 &&
              r.entities.ents[0].meta==0 &&
              r.entities.ents[0].age==0 &&
              r.entities.ents[0].pickup_delay==10 &&
              r.next_entity_id==7401 &&
              r.world_random_seed48==UINT64_C(0x2D3873C4CD04) &&
              r.math_random_seed48==UINT64_C(0x33E01D26154D) &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
              lamp_tick.block==124&&lamp_tick.x==13&&lamp_tick.y==78&&
              lamp_tick.z==8&&lamp_tick.time==lever_support_lamp_due,
              "stored floor support loss drops powered lever and queues lamp");
        for(int tick=0;tick<4;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,13,78,8)==123 &&
              r.entities.ents[0].age==4 &&
              r.entities.ents[0].pickup_delay==6 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "lever support-loss item and lamp advance through exact +4");
        memset(&r.entities,0,sizeof r.entities);
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;
        for(int y=77;y<=79;++y)
            for(int z=7;z<=9;++z)
                for(int x=11;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,8,1,0);
        gm_world_set_block_meta(r.world,13,77,8,1,0);
        gm_world_set_block_meta(r.world,13,78,8,77,9);
        gm_world_set_block_meta(r.world,14,78,8,124,0);
        long long stone_button_stale_due=r.clock.total_time+20;
        long long stone_button_lamp_due=r.clock.total_time+4;
        CHECK(gm_runtime_set_entity_id_cursor(&r,7410) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_schedule_tick(
                  &r,13,78,8,77,stone_button_stale_due,0,0) &&
              gm_runtime_set_block(&r,12,78,8,0,0) &&
              gm_world_block(r.world,13,78,8)==0 &&
              r.entities.n_active==1 &&
              r.entities.ents[0].eid==7410 &&
              r.entities.ents[0].item==77 &&
              r.entities.ents[0].meta==0 &&
              gm_runtime_scheduled_tick_count(&r)==2 &&
              gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
              lamp_tick.block==124&&lamp_tick.time==stone_button_lamp_due &&
              gm_runtime_scheduled_tick_get(&r,1,&lamp_tick) &&
              lamp_tick.block==77&&lamp_tick.time==stone_button_stale_due,
              "wall stone-button support loss drops and retains stale pulse");
        for(int tick=0;tick<4;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,14,78,8)==123 &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
              lamp_tick.block==77&&lamp_tick.time==stone_button_stale_due,
              "stone-button stale pulse outlives the lamp +4 callback");
        memset(&r.entities,0,sizeof r.entities);
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;
        for(int y=77;y<=79;++y)
            for(int z=7;z<=9;++z)
                for(int x=11;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,11,78,8,1,0);
        gm_world_set_block_meta(r.world,12,78,8,143,8);
        gm_world_set_block_meta(r.world,12,79,8,1,0);
        gm_world_set_block_meta(r.world,13,78,8,124,0);
        long long wood_button_stale_due=r.clock.total_time+3;
        long long wood_button_lamp_due=r.clock.total_time+4;
        CHECK(gm_runtime_set_entity_id_cursor(&r,7420) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_schedule_tick(
                  &r,12,78,8,143,wood_button_stale_due,0,0) &&
              gm_runtime_set_block(&r,12,79,8,0,0) &&
              gm_world_block(r.world,12,78,8)==0 &&
              r.entities.n_active==1 &&
              r.entities.ents[0].eid==7420 &&
              r.entities.ents[0].item==143 &&
              r.entities.ents[0].meta==0 &&
              gm_runtime_scheduled_tick_count(&r)==2 &&
              gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
              lamp_tick.block==143&&lamp_tick.time==wood_button_stale_due &&
              gm_runtime_scheduled_tick_get(&r,1,&lamp_tick) &&
              lamp_tick.block==124&&lamp_tick.time==wood_button_lamp_due,
              "ceiling wood-button support loss drops with ordered stale work");
        for(int tick=0;tick<3;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
              lamp_tick.block==124&&lamp_tick.time==wood_button_lamp_due,
              "stale wooden-button pulse drains without resurrecting air");
        gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,13,78,8)==123 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "wood-button support-loss lamp turns off at exact +4");
        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX;
        gm_world_set_block_meta(r.world,12,77,8,1,0);
        gm_world_set_block_meta(r.world,12,78,8,77,13);
        CHECK(gm_runtime_set_entity_id_cursor(&r,7430) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,77,8,0,0) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              r.entities.n_active==GM_LIVE_MAX &&
              r.next_entity_id==7430 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "full item pool preserves unsupported button and drop cursors");
        memset(&r.entities,0,sizeof r.entities);
        {
            static const int control_meta[8]={0,1,2,3,4,5,6,7};
            static const int sx[8]={12,11,13,12,12,12,12,12};
            static const int sy[8]={79,78,78,78,78,77,77,79};
            static const int sz[8]={8,8,8,7,9,8,8,8};
            for(int index=0;index<8;++index){
                memset(&r.entities,0,sizeof r.entities);
                for(int y=77;y<=79;++y)
                    for(int z=7;z<=9;++z)
                        for(int x=11;x<=13;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_world_set_block_meta(
                    r.world,sx[index],sy[index],sz[index],1,0);
                gm_world_set_block_meta(
                    r.world,12,78,8,69,control_meta[index]);
                gm_world_set_block_meta(r.world,13,79,8,1,0);
                CHECK(gm_runtime_set_block(&r,13,79,8,0,0) &&
                      gm_world_block(r.world,12,78,8)==69,
                      "all eight lever orientations retain valid stone support");
                CHECK(gm_runtime_set_block(
                          &r,sx[index],sy[index],sz[index],0,0) &&
                      gm_world_block(r.world,12,78,8)==0 &&
                      r.entities.n_active==1 &&
                      r.entities.ents[0].item==69,
                      "all eight lever orientations drop from stored support");
            }
        }
        {
            static const int support_id[5]={44,53,78,154,20};
            static const int support_meta[5]={8,4,7,0,0};
            for(int index=0;index<5;++index){
                memset(&r.entities,0,sizeof r.entities);
                for(int y=77;y<=79;++y)
                    for(int z=7;z<=9;++z)
                        for(int x=11;x<=13;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_world_set_block_meta(
                    r.world,12,77,8,support_id[index],support_meta[index]);
                gm_world_set_block_meta(r.world,12,78,8,77,5);
                gm_world_set_block_meta(r.world,13,78,8,1,0);
                CHECK(gm_runtime_set_block(&r,13,78,8,0,0) &&
                      gm_world_block(r.world,12,78,8)
                          ==(index==4?0:77) &&
                      r.entities.n_active==(index==4?1:0),
                      "button floor support follows Forge side solidity");
            }
        }
        {
            static const int plate_id[4]={70,72,147,148};
            static const int plate_meta[4]={1,1,2,1};
            for(int index=0;index<4;++index){
                memset(&r.entities,0,sizeof r.entities);
                r.scheduled_tick_count=0;
                r.scheduled_tick_next_order=0;
                for(int y=77;y<=79;++y)
                    for(int z=7;z<=9;++z)
                        for(int x=11;x<=14;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_world_set_block_meta(r.world,12,77,8,1,0);
                gm_world_set_block_meta(
                    r.world,12,78,8,plate_id[index],plate_meta[index]);
                gm_world_set_block_meta(r.world,13,78,8,124,0);
                long long plate_stale_due=r.clock.total_time+3;
                long long plate_lamp_due=r.clock.total_time+4;
                CHECK(gm_runtime_set_entity_id_cursor(&r,7440+index) &&
                      gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
                      gm_runtime_set_math_random_seed48(
                          &r,UINT64_C(0x0FEDCBA98765)) &&
                      gm_runtime_schedule_tick(
                          &r,12,78,8,plate_id[index],
                          plate_stale_due,0,0) &&
                      gm_runtime_set_block(&r,12,77,8,0,0) &&
                      gm_world_block(r.world,12,78,8)==0 &&
                      r.entities.n_active==1 &&
                      r.entities.ents[0].eid==7440+index &&
                      r.entities.ents[0].item==plate_id[index] &&
                      r.entities.ents[0].meta==0 &&
                      r.world_random_seed48==UINT64_C(0x2D3873C4CD04) &&
                      r.math_random_seed48==UINT64_C(0x33E01D26154D) &&
                      gm_runtime_scheduled_tick_count(&r)==2 &&
                      gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
                      lamp_tick.block==plate_id[index] &&
                      lamp_tick.time==plate_stale_due &&
                      gm_runtime_scheduled_tick_get(&r,1,&lamp_tick) &&
                      lamp_tick.block==124&&lamp_tick.time==plate_lamp_due,
                      "all pressure-plate support losses drop exactly");
                for(int tick=0;tick<3;++tick)
                    gm_runtime_tick(&r,lamp_idle);
                CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
                      gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
                      lamp_tick.block==124&&lamp_tick.time==plate_lamp_due &&
                      gm_world_block(r.world,13,78,8)==124,
                      "stale pressure-plate callbacks drain from air");
                gm_runtime_tick(&r,lamp_idle);
                CHECK(gm_world_block(r.world,13,78,8)==123 &&
                      r.entities.ents[0].age==4 &&
                      r.entities.ents[0].pickup_delay==6 &&
                      gm_runtime_scheduled_tick_count(&r)==0,
                      "pressure-plate items and lamps advance through +4");
            }
        }
        {
            static const int fence_id[7]={85,113,188,189,190,191,192};
            for(int index=0;index<7;++index){
                memset(&r.entities,0,sizeof r.entities);
                for(int y=77;y<=79;++y)
                    for(int z=7;z<=9;++z)
                        for(int x=11;x<=13;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_world_set_block_meta(
                    r.world,12,77,8,fence_id[index],0);
                gm_world_set_block_meta(r.world,12,78,8,70,0);
                gm_world_set_block_meta(r.world,13,78,8,1,0);
                CHECK(gm_runtime_set_block(&r,13,78,8,0,0) &&
                      gm_world_block(r.world,12,78,8)==70 &&
                      r.entities.n_active==0,
                      "pressure plates retain every vanilla fence support");
            }
        }
        {
            static const int plate_id[4]={70,72,147,148};
            static const int plate_meta[4]={1,1,2,1};
            for(int index=0;index<4;++index){
                memset(&r.entities,0,sizeof r.entities);
                r.scheduled_tick_count=0;
                r.scheduled_tick_next_order=0;
                for(int y=77;y<=79;++y)
                    for(int z=7;z<=9;++z)
                        for(int x=11;x<=14;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_world_set_block_meta(r.world,12,77,8,1,0);
                gm_world_set_block_meta(
                    r.world,12,78,8,plate_id[index],plate_meta[index]);
                gm_world_set_block_meta(r.world,13,77,8,124,0);
                long long direct_lamp_due=r.clock.total_time+4;
                CHECK(gm_runtime_set_world_random_seed48(
                          &r,UINT64_C(0)) &&
                      gm_runtime_set_math_random_seed48(
                          &r,UINT64_C(0x0FEDCBA98765)) &&
                      gm_runtime_set_block(&r,12,78,8,0,0) &&
                      r.entities.n_active==0 &&
                      r.world_random_seed48==UINT64_C(0) &&
                      r.math_random_seed48==UINT64_C(0x0FEDCBA98765) &&
                      gm_runtime_scheduled_tick_count(&r)==1 &&
                      gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
                      lamp_tick.block==124&&lamp_tick.x==13&&
                      lamp_tick.y==77&&lamp_tick.time==direct_lamp_due,
                      "direct powered-plate break notifies support consumer");
                for(int tick=0;tick<3;++tick)
                    gm_runtime_tick(&r,lamp_idle);
                CHECK(gm_world_block(r.world,13,77,8)==124 &&
                      gm_runtime_scheduled_tick_count(&r)==1,
                      "direct powered-plate lamp remains lit through +3");
                gm_runtime_tick(&r,lamp_idle);
                CHECK(gm_world_block(r.world,13,77,8)==123 &&
                      gm_runtime_scheduled_tick_count(&r)==0,
                      "direct powered-plate lamp releases at exact +4");
            }
        }
        memset(&r.entities,0,sizeof r.entities);
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;
        for(int y=77;y<=79;++y)
            for(int z=7;z<=9;++z)
                for(int x=11;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,77,8,1,0);
        gm_world_set_block_meta(r.world,12,78,8,70,0);
        gm_world_set_block_meta(r.world,13,77,8,123,0);
        CHECK(gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_block(&r,12,78,8,0,0) &&
              gm_world_block(r.world,13,77,8)==123 &&
              r.entities.n_active==0 &&
              r.world_random_seed48==UINT64_C(0) &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "direct unpowered-plate break has no support callback");
        {
            static const int support_id[7]={44,53,78,44,78,154,20};
            static const int support_meta[7]={8,4,7,0,6,0,0};
            static const int retained[7]={1,1,1,0,0,1,0};
            for(int index=0;index<7;++index){
                memset(&r.entities,0,sizeof r.entities);
                for(int y=77;y<=79;++y)
                    for(int z=7;z<=9;++z)
                        for(int x=11;x<=13;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_world_set_block_meta(
                    r.world,12,77,8,support_id[index],support_meta[index]);
                gm_world_set_block_meta(r.world,12,78,8,70,0);
                gm_world_set_block_meta(r.world,13,78,8,1,0);
                CHECK(gm_runtime_set_block(&r,13,78,8,0,0) &&
                      gm_world_block(r.world,12,78,8)
                          ==(retained[index]?70:0) &&
                      r.entities.n_active==(retained[index]?0:1),
                      "pressure plates use stateful fully-opaque support");
            }
        }
        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX;
        gm_world_set_block_meta(r.world,12,77,8,1,0);
        gm_world_set_block_meta(r.world,12,78,8,148,1);
        CHECK(gm_runtime_set_entity_id_cursor(&r,7450) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,77,8,0,0) &&
              gm_world_meta(r.world,12,78,8)==1 &&
              r.entities.n_active==GM_LIVE_MAX &&
              r.next_entity_id==7450 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "full item pool preserves unsupported pressure plate");
        {
            static const int diode_id[5]={93,94,149,149,150};
            static const int diode_meta[5]={1,1,1,9,1};
            static const int diode_item[5]={356,356,404,404,404};
            static const int diode_priority[5]={-1,-2,0,0,0};
            static const int diode_output[5]={-1,-1,0,15,15};
            static const int powered[5]={0,1,0,1,1};
            for(int index=0;index<5;++index){
                memset(&r.entities,0,sizeof r.entities);
                r.comparator_count=0;
                r.scheduled_tick_count=0;
                r.scheduled_tick_next_order=0;
                for(int y=77;y<=79;++y)
                    for(int z=7;z<=9;++z)
                        for(int x=11;x<=14;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                if(diode_output[index]>=0)
                    gm_world_set_block_meta(r.world,11,78,8,152,0);
                gm_world_set_block_meta(r.world,12,77,8,1,0);
                if(diode_output[index]>=0){
                    CHECK(gm_runtime_load_block(
                              &r,12,78,8,
                              diode_id[index],diode_meta[index]) &&
                          gm_runtime_comparator_set_output(
                              &r,0,12,78,8,diode_output[index]),
                          "comparator support-loss tile fixture restores");
                }else{
                    gm_world_set_block_meta(
                        r.world,12,78,8,
                        diode_id[index],diode_meta[index]);
                }
                gm_world_set_block_meta(
                    r.world,13,78,8,powered[index]?124:123,0);
                long long diode_stale_due=r.clock.total_time+3;
                long long diode_lamp_due=r.clock.total_time+4;
                CHECK(gm_runtime_set_entity_id_cursor(&r,7460+index) &&
                      gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
                      gm_runtime_set_math_random_seed48(
                          &r,UINT64_C(0x0FEDCBA98765)) &&
                      gm_runtime_schedule_tick(
                          &r,12,78,8,diode_id[index],diode_stale_due,
                          diode_priority[index],0) &&
                      gm_runtime_set_block(&r,12,77,8,0,0) &&
                      gm_world_block(r.world,12,78,8)==0 &&
                      r.entities.n_active==1 &&
                      r.entities.ents[0].eid==7460+index &&
                      r.entities.ents[0].item==diode_item[index] &&
                      r.entities.ents[0].meta==0 &&
                      r.world_random_seed48==UINT64_C(0x2D3873C4CD04) &&
                      r.math_random_seed48==UINT64_C(0x33E01D26154D) &&
                      r.comparator_count==0 &&
                      gm_runtime_scheduled_tick_count(&r)==1+powered[index] &&
                      gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
                      lamp_tick.block==diode_id[index] &&
                      lamp_tick.time==diode_stale_due &&
                      lamp_tick.priority==diode_priority[index],
                      "all diode support losses drop and retire exactly");
                if(powered[index])
                    CHECK(gm_runtime_scheduled_tick_get(&r,1,&lamp_tick) &&
                          lamp_tick.block==124 &&
                          lamp_tick.time==diode_lamp_due &&
                          lamp_tick.priority==0,
                          "powered diode support loss queues lamp +4");
                for(int tick=0;tick<3;++tick)
                    gm_runtime_tick(&r,lamp_idle);
                CHECK(gm_runtime_scheduled_tick_count(&r)==powered[index] &&
                      gm_world_block(r.world,12,78,8)==0,
                      "stale diode callback drains without resurrection");
                gm_runtime_tick(&r,lamp_idle);
                CHECK(gm_world_block(r.world,13,78,8)==123 &&
                      r.entities.ents[0].age==4 &&
                      r.entities.ents[0].pickup_delay==6 &&
                      gm_runtime_scheduled_tick_count(&r)==0,
                      "diode support-loss item and output advance through +4");
            }
        }
        {
            static const int support_id[8]={44,53,78,154,44,78,20,85};
            static const int support_meta[8]={8,4,7,0,0,6,0,0};
            static const int retained[8]={1,1,1,1,0,0,0,0};
            for(int index=0;index<8;++index){
                memset(&r.entities,0,sizeof r.entities);
                r.scheduled_tick_count=0;
                r.scheduled_tick_next_order=0;
                for(int y=77;y<=79;++y)
                    for(int z=7;z<=9;++z)
                        for(int x=11;x<=13;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_world_set_block_meta(
                    r.world,12,77,8,support_id[index],support_meta[index]);
                gm_world_set_block_meta(r.world,12,78,8,93,1);
                gm_world_set_block_meta(r.world,13,78,8,1,0);
                CHECK(gm_runtime_set_block(&r,13,78,8,0,0) &&
                      gm_world_block(r.world,12,78,8)
                          ==(retained[index]?93:0) &&
                      r.entities.n_active==(retained[index]?0:1),
                      "diodes require stateful fully-opaque floor support");
            }
        }
        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX;
        r.comparator_count=0;
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;
        GmRuntimeComparator support_comparator;
        gm_world_set_block_meta(r.world,11,78,8,152,0);
        gm_world_set_block_meta(r.world,12,77,8,1,0);
        CHECK(gm_runtime_load_block(&r,12,78,8,149,9) &&
              gm_runtime_comparator_set_output(&r,0,12,78,8,15),
              "full-pool comparator support-loss fixture restores");
        gm_world_set_block_meta(r.world,13,78,8,124,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,7470) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,77,8,0,0) &&
              gm_world_block(r.world,12,78,8)==149 &&
              gm_world_meta(r.world,12,78,8)==9 &&
              r.comparator_count==1 &&
              gm_runtime_comparator_get(&r,0,&support_comparator) &&
              support_comparator.output_signal==15 &&
              gm_world_block(r.world,13,78,8)==124 &&
              r.entities.n_active==GM_LIVE_MAX &&
              r.next_entity_id==7470 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765) &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "full item pool preserves unsupported comparator atomically");
        {
            static const int support_dx[4]={0,1,0,-1};
            static const int support_dz[4]={-1,0,1,0};
            for(int facing=0;facing<4;++facing){
                int eid=7480+facing;
                memset(&r.entities,0,sizeof r.entities);
                r.scheduled_tick_count=0;
                r.scheduled_tick_next_order=0;
                for(int y=77;y<=79;++y)
                    for(int z=7;z<=9;++z)
                        for(int x=11;x<=13;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_world_set_block_meta(
                    r.world,12+support_dx[facing],78,
                    8+support_dz[facing],1,0);
                gm_world_set_block_meta(r.world,12,78,8,131,facing);
                CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                      gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
                      gm_runtime_set_math_random_seed48(
                          &r,UINT64_C(0x0FEDCBA98765)) &&
                      gm_runtime_set_block(
                          &r,12+support_dx[facing],78,
                          8+support_dz[facing],0,0) &&
                      gm_world_block(r.world,12,78,8)==0 &&
                      r.entities.n_active==1 &&
                      r.entities.ents[0].eid==eid &&
                      r.entities.ents[0].item==131 &&
                      r.entities.ents[0].meta==0 &&
                      r.next_entity_id==eid+1 &&
                      r.world_random_seed48==UINT64_C(0x2D3873C4CD04) &&
                      r.math_random_seed48==UINT64_C(0x33E01D26154D) &&
                      gm_runtime_scheduled_tick_count(&r)==0,
                      "all four hook facings drop from stored support");
            }
        }
        memset(&r.entities,0,sizeof r.entities);
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;
        for(int y=77;y<=79;++y)
            for(int z=7;z<=9;++z)
                for(int x=9;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,9,78,8,1,0);
        gm_world_set_block_meta(r.world,10,78,8,131,15);
        gm_world_set_block_meta(r.world,11,78,8,132,4);
        gm_world_set_block_meta(r.world,12,78,8,132,5);
        gm_world_set_block_meta(r.world,13,78,8,132,4);
        gm_world_set_block_meta(r.world,14,78,8,131,13);
        gm_world_set_block_meta(r.world,15,78,8,1,0);
        gm_world_set_block_meta(r.world,10,78,7,124,0);
        gm_world_set_block_meta(r.world,14,78,7,124,0);
        {
            long long hook_due=r.clock.total_time+10;
            long long lamp_due=r.clock.total_time+4;
            CHECK(gm_runtime_set_entity_id_cursor(&r,7490) &&
                  gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
                  gm_runtime_set_math_random_seed48(
                      &r,UINT64_C(0x0FEDCBA98765)) &&
                  gm_runtime_schedule_tick(
                      &r,10,78,8,131,hook_due,0,0) &&
                  gm_runtime_set_block(&r,9,78,8,0,0) &&
                  gm_world_block(r.world,10,78,8)==0 &&
                  gm_world_meta(r.world,11,78,8)==0 &&
                  gm_world_meta(r.world,12,78,8)==1 &&
                  gm_world_meta(r.world,13,78,8)==0 &&
                  gm_world_meta(r.world,14,78,8)==1 &&
                  r.entities.n_active==1 &&
                  r.entities.ents[0].eid==7490 &&
                  r.entities.ents[0].item==131 &&
                  r.world_random_seed48==UINT64_C(0x2D3873C4CD04) &&
                  r.math_random_seed48==UINT64_C(0x33E01D26154D) &&
                  gm_runtime_scheduled_tick_count(&r)==3 &&
                  gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
                  lamp_tick.block==124&&lamp_tick.x==14&&
                  lamp_tick.z==7&&lamp_tick.time==lamp_due &&
                  gm_runtime_scheduled_tick_get(&r,1,&lamp_tick) &&
                  lamp_tick.block==124&&lamp_tick.x==10&&
                  lamp_tick.z==7&&lamp_tick.time==lamp_due &&
                  gm_runtime_scheduled_tick_get(&r,2,&lamp_tick) &&
                  lamp_tick.block==131&&lamp_tick.x==10&&
                  lamp_tick.z==8&&lamp_tick.time==hook_due,
                  "powered hook support loss detaches line in exact order");
            for(int tick=0;tick<4;++tick)
                gm_runtime_tick(&r,lamp_idle);
            CHECK(gm_world_block(r.world,10,78,7)==123 &&
                  gm_world_block(r.world,14,78,7)==123 &&
                  r.entities.ents[0].age==4 &&
                  r.entities.ents[0].pickup_delay==6 &&
                  gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
                  lamp_tick.block==131&&lamp_tick.time==hook_due,
                  "hook item and both lamps advance through exact +4");
        }
        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX;
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;
        gm_world_set_block_meta(r.world,12,78,7,1,0);
        gm_world_set_block_meta(r.world,12,78,8,131,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,7500) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,7,0,0) &&
              gm_world_block(r.world,12,78,8)==131 &&
              r.entities.n_active==GM_LIVE_MAX &&
              r.next_entity_id==7500 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "full item pool preserves unsupported tripwire hook");
        memset(&r.entities,0,sizeof r.entities);
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;
        for(int y=77;y<=79;++y)
            for(int z=7;z<=9;++z)
                for(int x=9;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,9,78,8,1,0);
        gm_world_set_block_meta(r.world,10,78,8,131,7);
        gm_world_set_block_meta(r.world,11,78,8,132,4);
        gm_world_set_block_meta(r.world,12,78,8,132,4);
        gm_world_set_block_meta(r.world,13,78,8,132,4);
        gm_world_set_block_meta(r.world,14,78,8,131,5);
        gm_world_set_block_meta(r.world,15,78,8,1,0);
        gm_world_set_block_meta(r.world,10,78,7,123,0);
        gm_world_set_block_meta(r.world,14,78,7,123,0);
        {
            long long hook_due=r.clock.total_time+10;
            long long lamps_due=r.clock.total_time+14;
            CHECK(gm_runtime_set_world_random_seed48(
                      &r,UINT64_C(0)) &&
                  gm_runtime_set_math_random_seed48(
                      &r,UINT64_C(0x0FEDCBA98765)) &&
                  gm_runtime_set_block(&r,12,78,8,0,0) &&
                  gm_world_meta(r.world,10,78,8)==15 &&
                  gm_world_meta(r.world,14,78,8)==13 &&
                  gm_world_meta(r.world,11,78,8)==4 &&
                  gm_world_meta(r.world,13,78,8)==4 &&
                  gm_world_block(r.world,10,78,7)==124 &&
                  gm_world_block(r.world,14,78,7)==124 &&
                  r.world_random_seed48==UINT64_C(0) &&
                  r.math_random_seed48==UINT64_C(0x0FEDCBA98765) &&
                  r.entities.n_active==0 &&
                  gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
                  lamp_tick.block==131&&lamp_tick.x==10&&
                  lamp_tick.time==hook_due,
                  "live attached string break starts exact +10 pulse");
            for(int tick=0;tick<9;++tick)
                gm_runtime_tick(&r,lamp_idle);
            CHECK(gm_world_meta(r.world,10,78,8)==15 &&
                  gm_world_meta(r.world,14,78,8)==13 &&
                  gm_runtime_scheduled_tick_count(&r)==1,
                  "string-break pulse remains powered through +9");
            gm_runtime_tick(&r,lamp_idle);
            CHECK(gm_world_meta(r.world,10,78,8)==3 &&
                  gm_world_meta(r.world,11,78,8)==0 &&
                  gm_world_meta(r.world,13,78,8)==0 &&
                  gm_world_meta(r.world,14,78,8)==1 &&
                  gm_runtime_scheduled_tick_count(&r)==2 &&
                  gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
                  lamp_tick.block==124&&lamp_tick.x==14&&
                  lamp_tick.z==7&&lamp_tick.time==lamps_due &&
                  gm_runtime_scheduled_tick_get(&r,1,&lamp_tick) &&
                  lamp_tick.block==124&&lamp_tick.x==10&&
                  lamp_tick.z==7&&lamp_tick.time==lamps_due,
                  "string break releases hooks and orders both lamp callbacks");
            for(int tick=0;tick<4;++tick)
                gm_runtime_tick(&r,lamp_idle);
            CHECK(gm_world_block(r.world,10,78,7)==123 &&
                  gm_world_block(r.world,14,78,7)==123 &&
                  gm_runtime_scheduled_tick_count(&r)==0,
                  "string-break lamps release at exact +14");
        }
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;
        for(int y=77;y<=79;++y)
            for(int z=7;z<=9;++z)
                for(int x=9;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,9,78,8,1,0);
        gm_world_set_block_meta(r.world,10,78,8,131,7);
        gm_world_set_block_meta(r.world,11,78,8,132,4);
        gm_world_set_block_meta(r.world,12,78,8,132,4);
        gm_world_set_block_meta(r.world,13,78,8,132,4);
        gm_world_set_block_meta(r.world,14,78,8,131,5);
        gm_world_set_block_meta(r.world,15,78,8,1,0);
        CHECK(gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,10,78,8,0,0) &&
              gm_world_meta(r.world,11,78,8)==0 &&
              gm_world_meta(r.world,12,78,8)==0 &&
              gm_world_meta(r.world,13,78,8)==0 &&
              gm_world_meta(r.world,14,78,8)==1 &&
              r.world_random_seed48==UINT64_C(277363943098) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765) &&
              r.entities.n_active==0 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "live attached hook break detaches line and consumes two pitches");
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;
        for(int y=77;y<=79;++y)
            for(int z=7;z<=9;++z)
                for(int x=9;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,9,78,8,1,0);
        gm_world_set_block_meta(r.world,10,78,8,131,3);
        gm_world_set_block_meta(r.world,11,78,8,132,0);
        gm_world_set_block_meta(r.world,13,78,8,132,0);
        gm_world_set_block_meta(r.world,14,78,8,131,1);
        gm_world_set_block_meta(r.world,15,78,8,1,0);
        {
            long long on_add_due=r.clock.total_time+10;
            CHECK(gm_runtime_set_world_random_seed48(
                      &r,UINT64_C(0)) &&
                  gm_runtime_set_block(&r,12,78,8,132,0) &&
                  gm_world_meta(r.world,10,78,8)==7 &&
                  gm_world_meta(r.world,11,78,8)==4 &&
                  gm_world_meta(r.world,12,78,8)==4 &&
                  gm_world_meta(r.world,13,78,8)==4 &&
                  gm_world_meta(r.world,14,78,8)==5 &&
                  r.world_random_seed48==UINT64_C(0) &&
                  r.entities.n_active==0 &&
                  gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
                  lamp_tick.block==131&&lamp_tick.x==10&&
                  lamp_tick.time==on_add_due,
                  "placing final tripwire attaches line and queues +10");
            for(int tick=0;tick<9;++tick)
                gm_runtime_tick(&r,lamp_idle);
            CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_world_meta(r.world,12,78,8)==4,
                  "completed tripwire line retains callback through +9");
            gm_runtime_tick(&r,lamp_idle);
            CHECK(gm_runtime_scheduled_tick_count(&r)==0 &&
                  gm_world_meta(r.world,10,78,8)==7 &&
                  gm_world_meta(r.world,12,78,8)==4 &&
                  gm_world_meta(r.world,14,78,8)==5,
                  "completed tripwire callback drains without state change");
        }
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;
        for(int y=77;y<=79;++y)
            for(int z=7;z<=9;++z)
                for(int x=9;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        CHECK(gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_block(&r,12,78,8,132,0) &&
              gm_world_meta(r.world,12,78,8)==0 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.entities.n_active==0 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "isolated tripwire placement stays detached without work");
        memset(&r.entities,0,sizeof r.entities);
        for(int y=77;y<=79;++y)
            for(int z=6;z<=9;++z)
                for(int x=11;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,77,8,1,0);
        gm_world_set_block_meta(r.world,12,78,8,93,1);
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        long long repeater_due=r.clock.total_time+2;
        CHECK(gm_runtime_set_block(&r,11,78,8,152,0) &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
              lamp_tick.block==93&&lamp_tick.time==repeater_due&&
              lamp_tick.priority==-1,
              "delay-1 repeater activation schedules exact +2/priority -1");
        gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,12,78,8)==93 &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "delay-1 repeater remains unpowered through +1");
        gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,12,78,8)==94 &&
              gm_world_meta(r.world,12,78,8)==1 &&
              gm_world_block(r.world,13,78,8)==124 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "delay-1 repeater powers its directional output at +2");
        long long repeater_off_due=r.clock.total_time+2;
        CHECK(gm_runtime_set_block(&r,11,78,8,0,0) &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
              lamp_tick.block==94&&lamp_tick.time==repeater_off_due&&
              lamp_tick.priority==-2,
              "powered repeater falling edge schedules +2/priority -2");
        gm_runtime_tick(&r,lamp_idle);
        gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,12,78,8)==93 &&
              gm_world_block(r.world,13,78,8)==124 &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
              lamp_tick.block==124&&lamp_tick.time==repeater_off_due+4,
              "repeater falling edge hands lamp an independent +4 callback");
        for(int tick=0;tick<4;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,13,78,8)==123 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "repeater-powered lamp drains exactly four ticks later");
        gm_world_set_block_meta(r.world,12,78,8,93,13);
        long long repeater_delay4_due=r.clock.total_time+8;
        CHECK(gm_runtime_set_block(&r,11,78,8,152,0) &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
              lamp_tick.block==93&&lamp_tick.time==repeater_delay4_due,
              "delay metadata 13 derives the exact +8 callback");
        for(int tick=0;tick<7;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,12,78,8)==93 &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "delay-4 repeater remains unpowered through +7");
        gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,12,78,8)==94 &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==124 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "delay-4 repeater powers at exact +8");
        for(int y=77;y<=79;++y)
            for(int z=6;z<=9;++z)
                for(int x=11;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,77,8,1,0);
        gm_world_set_block_meta(r.world,12,78,8,93,1);
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        gm_world_set_block_meta(r.world,12,77,7,1,0);
        gm_world_set_block_meta(r.world,12,78,7,94,2);
        gm_world_set_block_meta(r.world,12,78,6,152,0);
        CHECK(gm_runtime_set_block(&r,11,78,8,152,0) &&
              gm_world_block(r.world,12,78,8)==93 &&
              gm_world_block(r.world,13,78,8)==123 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "perpendicular powered repeater locks the main transition");
        repeater_due=r.clock.total_time+2;
        CHECK(gm_runtime_set_block(&r,12,78,7,0,0) &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
              lamp_tick.block==93&&lamp_tick.time==repeater_due,
              "removing a side repeater notifies and unlocks the main input");
        gm_runtime_tick(&r,lamp_idle);
        gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,12,78,8)==94 &&
              gm_world_block(r.world,13,78,8)==124 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "unlocked repeater powers after its full delay");
        for(int y=77;y<=79;++y)
            for(int z=6;z<=9;++z)
                for(int x=11;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,77,8,1,0);
        gm_world_set_block_meta(r.world,12,78,8,93,1);
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        CHECK(gm_runtime_set_block(&r,11,78,8,152,0),
              "one-observation repeater pulse starts");
        gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_runtime_set_block(&r,11,78,8,0,0),
              "one-observation repeater input is removed before dispatch");
        gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,12,78,8)==94 &&
              gm_world_block(r.world,13,78,8)==124 &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
              lamp_tick.block==94&&
              lamp_tick.time==r.clock.total_time+2&&
              lamp_tick.priority==-1,
              "short input forces the exact minimum powered pulse");
        gm_runtime_tick(&r,lamp_idle);
        gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,12,78,8)==93 &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "minimum-pulse repeater turns off after its second delay");
        for(int tick=0;tick<4;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,13,78,8)==123 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "minimum-pulse lamp returns to its exact initial state");
        for(int y=77;y<=79;++y)
            for(int z=7;z<=9;++z)
                for(int x=11;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,77,8,1,0);
        gm_world_set_block_meta(r.world,13,77,8,1,0);
        gm_world_set_block_meta(r.world,12,78,8,93,1);
        gm_world_set_block_meta(r.world,13,78,8,93,1);
        gm_world_set_block_meta(r.world,14,78,8,123,0);
        CHECK(gm_runtime_set_block(&r,11,78,8,152,0) &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
              lamp_tick.block==93&&lamp_tick.x==12&&
              lamp_tick.priority==-3,
              "repeater chain topology selects exact priority -3");
        for(int tick=0;tick<4;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,12,78,8)==94 &&
              gm_world_block(r.world,13,78,8)==94 &&
              gm_world_block(r.world,14,78,8)==124 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "priority chain powers both repeaters and downstream lamp");
        for(int y=77;y<=79;++y)
            for(int z=7;z<=9;++z)
                for(int x=11;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,11,78,8,152,0);
        gm_world_set_block_meta(r.world,12,77,8,1,0);
        gm_world_set_block_meta(r.world,12,78,8,93,1);
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        long long saved_repeater_due=r.clock.total_time+3;
        CHECK(gm_runtime_schedule_tick(
                  &r,12,78,8,93,saved_repeater_due,-1,0),
              "proof-safe saved repeater callback enters scheduler");
        for(int tick=0;tick<3;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,12,78,8)==94 &&
              gm_world_block(r.world,13,78,8)==124 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "saved repeater callback resumes exact powered outcome");
        for(int y=77;y<=79;++y)
            for(int z=7;z<=9;++z)
                for(int x=11;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,77,8,1,0);
        CHECK(gm_runtime_load_block(&r,12,78,8,149,1) &&
              gm_runtime_comparator_count(&r)==1,
              "comparator snapshot load creates its fixed tile-state entry");
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        long long comparator_due=r.clock.total_time+2;
        CHECK(gm_runtime_set_block(&r,11,78,8,152,0) &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
              lamp_tick.block==149&&lamp_tick.time==comparator_due&&
              lamp_tick.priority==0,
              "compare-mode comparator activation schedules exact +2/priority 0");
        gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,12,78,8)==149 &&
              gm_world_meta(r.world,12,78,8)==1 &&
              gm_world_block(r.world,13,78,8)==123 &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "comparator remains unpowered through +1");
        gm_runtime_tick(&r,lamp_idle);
        GmRuntimeComparator comparator;
        CHECK(gm_runtime_comparator_get(&r,0,&comparator) &&
              comparator.output_signal==15 &&
              gm_world_block(r.world,12,78,8)==149 &&
              gm_world_meta(r.world,12,78,8)==9 &&
              gm_world_block(r.world,13,78,8)==124 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "comparator stores analog output 15 and powers at exact +2");
        CHECK(gm_runtime_load_block(&r,12,78,8,0,0) &&
              gm_runtime_comparator_count(&r)==0,
              "snapshot replacement removes a comparator tile-state entry");
        for(int y=77;y<=79;++y)
            for(int z=7;z<=9;++z)
                for(int x=11;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,11,78,8,118,0);
        gm_world_set_block_meta(r.world,12,77,8,1,0);
        CHECK(gm_runtime_load_block(&r,12,78,8,149,1),
              "cauldron comparator fixture loads");
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        comparator_due=r.clock.total_time+2;
        CHECK(gm_runtime_set_block(&r,11,78,8,118,3) &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
              lamp_tick.block==149&&lamp_tick.time==comparator_due&&
              lamp_tick.priority==0,
              "cauldron level edit schedules comparator +2/priority 0");
        gm_runtime_tick(&r,lamp_idle);
        gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_runtime_comparator_get(&r,0,&comparator) &&
              comparator.output_signal==3 &&
              gm_world_meta(r.world,12,78,8)==9 &&
              gm_world_block(r.world,13,78,8)==124 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "cauldron LEVEL 3 becomes exact comparator output 3");
        CHECK(gm_runtime_load_block(&r,12,78,8,0,0),
              "cauldron comparator fixture clears");
        gm_world_set_block_meta(r.world,11,78,8,0,0);
        CHECK(gm_runtime_load_block(&r,12,78,8,149,1),
              "cake comparator fixture loads");
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        CHECK(gm_runtime_set_block(&r,11,78,8,92,3) &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "cake addition schedules its adjacent comparator");
        gm_runtime_tick(&r,lamp_idle);
        gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_runtime_comparator_get(&r,0,&comparator) &&
              comparator.output_signal==8 &&
              gm_world_meta(r.world,12,78,8)==9 &&
              gm_world_block(r.world,13,78,8)==124,
              "three-bite cake becomes exact comparator output 8");
        CHECK(gm_runtime_load_block(&r,12,78,8,0,0),
              "cake comparator fixture clears");
        gm_world_set_block_meta(r.world,11,78,8,120,1);
        CHECK(gm_runtime_load_block(&r,12,78,8,149,1),
              "End-frame comparator fixture loads");
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        CHECK(gm_runtime_set_block(&r,11,78,8,120,5) &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "End-frame eye edit schedules its adjacent comparator");
        gm_runtime_tick(&r,lamp_idle);
        gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_runtime_comparator_get(&r,0,&comparator) &&
              comparator.output_signal==15 &&
              gm_world_meta(r.world,12,78,8)==9 &&
              gm_world_block(r.world,13,78,8)==124,
              "End-frame eye becomes exact comparator output 15");
        CHECK(gm_runtime_load_block(&r,12,78,8,0,0),
              "End-frame comparator fixture clears");
        gm_world_set_block_meta(r.world,10,77,8,1,0);
        gm_world_set_block_meta(r.world,10,78,8,118,0);
        gm_world_set_block_meta(r.world,11,78,8,1,0);
        CHECK(gm_runtime_load_block(&r,12,78,8,149,1),
              "look-through comparator fixture loads");
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        CHECK(gm_runtime_set_block(&r,10,78,8,118,3) &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "override edit notifies a comparator through one normal cube");
        gm_runtime_tick(&r,lamp_idle);
        gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_runtime_comparator_get(&r,0,&comparator) &&
              comparator.output_signal==3 &&
              gm_world_meta(r.world,12,78,8)==9 &&
              gm_world_block(r.world,13,78,8)==124,
              "one-solid look-through preserves cauldron output 3");
        CHECK(gm_runtime_load_block(&r,12,78,8,0,0),
              "look-through comparator fixture clears");
        gm_world_set_block_meta(r.world,10,78,8,0,0);
        gm_world_set_block_meta(r.world,11,78,8,54,2);
        CHECK(gm_runtime_load_block(&r,12,78,8,149,1),
              "single-chest comparator fixture loads");
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        CHECK(gm_runtime_chest_set_slot(
                  &r,0,11,78,8,0,1,64,0),
              "single-chest slot restore accepts one full stone stack");
        comparator_due=r.clock.total_time+3;
        CHECK(gm_runtime_schedule_tick(
                  &r,12,78,8,149,comparator_due,0,0),
              "saved single-chest comparator callback enters scheduler");
        for(int tick=0;tick<3;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_runtime_comparator_get(&r,0,&comparator) &&
              comparator.output_signal==1 &&
              gm_world_meta(r.world,12,78,8)==9 &&
              gm_world_block(r.world,13,78,8)==124 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "one full chest stack resumes as exact comparator output 1");
        CHECK(gm_runtime_load_block(&r,12,78,8,0,0),
              "single-chest comparator fixture clears");
        gm_world_set_block_meta(r.world,10,77,8,1,0);
        gm_world_set_block_meta(r.world,10,78,8,54,2);
        CHECK(gm_runtime_load_block(&r,12,78,8,149,1),
              "double-chest comparator fixture loads");
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        CHECK(gm_runtime_chest_set_slot(
                  &r,0,10,78,8,0,0,0,0),
              "double-chest empty half materializes");
        for (int slot=0;slot<4;++slot)
            CHECK(gm_runtime_chest_set_slot(
                      &r,0,11,78,8,slot,1,64,0),
                  "double-chest filled half restores a full stack");
        comparator_due=r.clock.total_time+3;
        CHECK(gm_runtime_schedule_tick(
                  &r,12,78,8,149,comparator_due,0,0),
              "saved double-chest comparator callback enters scheduler");
        for(int tick=0;tick<3;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_runtime_comparator_get(&r,0,&comparator) &&
              comparator.output_signal==2 &&
              gm_world_meta(r.world,12,78,8)==9 &&
              gm_world_block(r.world,13,78,8)==124 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "four full double-chest slots resume as comparator output 2");
        for (int slot=0;slot<4;++slot)
            CHECK(gm_runtime_chest_set_slot(
                      &r,0,11,78,8,slot,0,0,0),
                  "double-chest filled half clears");
        CHECK(gm_runtime_chest_set_slot(
                  &r,0,10,78,8,0,0,0,0),
              "double-chest empty half clears");
        CHECK(gm_runtime_load_block(&r,12,78,8,0,0),
              "double-chest comparator fixture clears");
        CHECK(gm_runtime_load_block(&r,10,78,8,0,0),
              "double-chest pair fixture clears");
        CHECK(gm_runtime_load_block(&r,11,78,8,0,0),
              "single-chest tile fixture clears");
        gm_world_set_block_meta(r.world,11,78,8,146,2);
        CHECK(gm_runtime_load_block(&r,12,78,8,149,1),
              "single trapped-chest comparator fixture loads");
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        CHECK(gm_runtime_chest_set_slot(
                  &r,0,11,78,8,0,1,64,0),
              "trapped-chest slot restore accepts one full stone stack");
        comparator_due=r.clock.total_time+3;
        CHECK(gm_runtime_schedule_tick(
                  &r,12,78,8,149,comparator_due,0,0),
              "saved trapped-chest comparator callback enters scheduler");
        for(int tick=0;tick<3;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_runtime_comparator_get(&r,0,&comparator) &&
              comparator.output_signal==1 &&
              gm_world_meta(r.world,12,78,8)==9 &&
              gm_world_block(r.world,13,78,8)==124 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "one full trapped-chest stack resumes as comparator output 1");
        CHECK(gm_runtime_chest_set_slot(
                  &r,0,11,78,8,0,0,0,0),
              "trapped-chest comparator fixture empties");
        CHECK(gm_runtime_load_block(&r,12,78,8,0,0),
              "trapped-chest comparator fixture clears");
        CHECK(gm_runtime_load_block(&r,11,78,8,0,0),
              "single trapped-chest tile fixture clears");
        gm_world_set_block_meta(r.world,10,78,8,146,2);
        gm_world_set_block_meta(r.world,11,78,8,146,2);
        CHECK(gm_runtime_load_block(&r,12,78,8,149,1),
              "double trapped-chest comparator fixture loads");
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        CHECK(gm_runtime_chest_set_slot(
                  &r,0,10,78,8,0,0,0,0),
              "double trapped-chest empty half materializes");
        for (int slot=0;slot<4;++slot)
            CHECK(gm_runtime_chest_set_slot(
                      &r,0,11,78,8,slot,1,64,0),
                  "double trapped-chest filled half restores a full stack");
        comparator_due=r.clock.total_time+3;
        CHECK(gm_runtime_schedule_tick(
                  &r,12,78,8,149,comparator_due,0,0),
              "saved double trapped-chest comparator callback enters scheduler");
        for(int tick=0;tick<3;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_runtime_comparator_get(&r,0,&comparator) &&
              comparator.output_signal==2 &&
              gm_world_meta(r.world,12,78,8)==9 &&
              gm_world_block(r.world,13,78,8)==124 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "four full double trapped-chest slots resume as output 2");
        for (int slot=0;slot<4;++slot)
            CHECK(gm_runtime_chest_set_slot(
                      &r,0,11,78,8,slot,0,0,0),
                  "double trapped-chest filled half clears");
        CHECK(gm_runtime_chest_set_slot(
                  &r,0,10,78,8,0,0,0,0),
              "double trapped-chest empty half clears");
        CHECK(gm_runtime_load_block(&r,12,78,8,0,0),
              "double trapped-chest comparator fixture clears");
        CHECK(gm_runtime_load_block(&r,10,78,8,0,0),
              "double trapped-chest pair fixture clears");
        CHECK(gm_runtime_load_block(&r,11,78,8,0,0),
              "double trapped-chest tile fixture clears");
        gm_world_set_block_meta(r.world,11,77,8,1,0);
        gm_world_set_block_meta(r.world,12,77,8,1,0);
        gm_world_set_block_meta(r.world,11,78,8,61,2);
        CHECK(gm_runtime_load_block(&r,12,78,8,149,1),
              "furnace comparator fixture loads");
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        CHECK(gm_runtime_furnace_set_slot(
                  &r,0,11,78,8,0,1,64,0,0,0,0,200),
              "furnace restore accepts one full stone stack");
        comparator_due=r.clock.total_time+3;
        CHECK(gm_runtime_schedule_tick(
                  &r,12,78,8,149,comparator_due,0,0),
              "saved furnace comparator callback enters scheduler");
        for(int tick=0;tick<3;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_runtime_comparator_get(&r,0,&comparator) &&
              comparator.output_signal==5 &&
              gm_world_meta(r.world,12,78,8)==9 &&
              gm_world_block(r.world,13,78,8)==124 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "one full furnace slot resumes as exact comparator output 5");
        CHECK(gm_runtime_furnace_set_slot(
                  &r,0,11,78,8,0,0,0,0,0,0,0,200),
              "furnace comparator fixture empties");
        CHECK(gm_runtime_load_block(&r,12,78,8,0,0),
              "furnace comparator fixture clears");
        CHECK(gm_runtime_load_block(&r,11,78,8,0,0),
              "furnace tile fixture clears");
        gm_world_set_block_meta(r.world,11,78,8,23,3);
        CHECK(gm_runtime_load_block(&r,12,78,8,149,1),
              "dispenser comparator fixture loads");
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        CHECK(gm_runtime_static_container_set_slot(
                  &r,0,11,78,8,0,1,64,0),
              "dispenser restore accepts one full stone stack");
        comparator_due=r.clock.total_time+3;
        CHECK(gm_runtime_schedule_tick(
                  &r,12,78,8,149,comparator_due,0,0),
              "saved dispenser comparator callback enters scheduler");
        for(int tick=0;tick<3;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_runtime_comparator_get(&r,0,&comparator) &&
              comparator.output_signal==2 &&
              gm_world_meta(r.world,12,78,8)==9 &&
              gm_world_block(r.world,13,78,8)==124 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "one full dispenser slot resumes as exact comparator output 2");
        CHECK(gm_runtime_static_container_set_slot(
                  &r,0,11,78,8,0,0,0,0),
              "dispenser comparator fixture empties");
        CHECK(gm_runtime_load_block(&r,12,78,8,0,0),
              "dispenser comparator fixture clears");
        gm_world_set_block_meta(r.world,11,78,8,0,0);
        gm_world_set_block_meta(r.world,11,78,8,23,3);
        CHECK(gm_runtime_static_container_set_slot(
                  &r,0,11,78,8,0,0,0,0),
              "empty dispenser tile materializes");
        CHECK(gm_runtime_load_block(&r,12,78,8,149,1),
              "empty-dispenser comparator fixture loads");
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        comparator_due=r.clock.total_time+3;
        CHECK(gm_runtime_schedule_tick(
                  &r,12,78,8,149,comparator_due,0,0),
              "saved empty-dispenser callback enters scheduler");
        for(int tick=0;tick<3;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_runtime_comparator_get(&r,0,&comparator) &&
              comparator.output_signal==0 &&
              gm_world_meta(r.world,12,78,8)==1 &&
              gm_world_block(r.world,13,78,8)==123 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "empty dispenser remains exact comparator output zero");
        CHECK(gm_runtime_load_block(&r,12,78,8,0,0),
              "empty-dispenser comparator fixture clears");
        gm_world_set_block_meta(r.world,11,78,8,0,0);
        gm_world_set_block_meta(r.world,11,78,8,158,3);
        CHECK(gm_runtime_load_block(&r,12,78,8,149,1),
              "dropper comparator fixture loads");
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        CHECK(gm_runtime_static_container_set_slot(
                  &r,0,11,78,8,0,403,1,0),
              "dropper restore accepts one non-stackable item");
        comparator_due=r.clock.total_time+3;
        CHECK(gm_runtime_schedule_tick(
                  &r,12,78,8,149,comparator_due,0,0),
              "saved dropper comparator callback enters scheduler");
        for(int tick=0;tick<3;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_runtime_comparator_get(&r,0,&comparator) &&
              comparator.output_signal==2 &&
              gm_world_meta(r.world,12,78,8)==9 &&
              gm_world_block(r.world,13,78,8)==124 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "one non-stackable dropper item uses its item limit for output 2");
        CHECK(gm_runtime_static_container_set_slot(
                  &r,0,11,78,8,0,0,0,0),
              "dropper comparator fixture empties");
        CHECK(gm_runtime_load_block(&r,12,78,8,0,0),
              "dropper comparator fixture clears");
        gm_world_set_block_meta(r.world,11,78,8,0,0);
        gm_world_set_block_meta(r.world,11,78,8,84,1);
        CHECK(gm_runtime_load_block(&r,12,78,8,149,1),
              "jukebox comparator fixture loads");
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        CHECK(gm_runtime_static_container_set_slot(
                  &r,0,11,78,8,0,2256,1,0),
              "jukebox restore accepts record 13");
        comparator_due=r.clock.total_time+3;
        CHECK(gm_runtime_schedule_tick(
                  &r,12,78,8,149,comparator_due,0,0),
              "saved jukebox comparator callback enters scheduler");
        for(int tick=0;tick<3;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_runtime_comparator_get(&r,0,&comparator) &&
              comparator.output_signal==1 &&
              gm_world_meta(r.world,12,78,8)==9 &&
              gm_world_block(r.world,13,78,8)==124 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "jukebox record 13 resumes as exact comparator output 1");
        CHECK(gm_runtime_static_container_set_slot(
                  &r,0,11,78,8,0,2267,1,0),
              "jukebox restore accepts record wait");
        comparator_due=r.clock.total_time+3;
        CHECK(gm_runtime_schedule_tick(
                  &r,12,78,8,149,comparator_due,0,0),
              "saved last-record comparator callback enters scheduler");
        for(int tick=0;tick<3;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_runtime_comparator_get(&r,0,&comparator) &&
              comparator.output_signal==12 &&
              gm_world_meta(r.world,12,78,8)==9 &&
              gm_world_block(r.world,13,78,8)==124,
              "jukebox record wait resumes as exact comparator output 12");
        CHECK(gm_runtime_load_block(&r,12,78,8,0,0),
              "filled-jukebox comparator fixture clears");
        gm_world_set_block_meta(r.world,11,78,8,84,0);
        CHECK(gm_runtime_static_container_set_slot(
                  &r,0,11,78,8,0,0,0,0),
              "empty jukebox tile restores with matching block metadata");
        CHECK(gm_runtime_load_block(&r,12,78,8,149,1),
              "empty-jukebox comparator fixture loads");
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        comparator_due=r.clock.total_time+3;
        CHECK(gm_runtime_schedule_tick(
                  &r,12,78,8,149,comparator_due,0,0),
              "saved empty-jukebox callback enters scheduler");
        for(int tick=0;tick<3;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_runtime_comparator_get(&r,0,&comparator) &&
              comparator.output_signal==0 &&
              gm_world_meta(r.world,12,78,8)==1 &&
              gm_world_block(r.world,13,78,8)==123 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "empty jukebox remains exact comparator output zero");
        CHECK(gm_runtime_load_block(&r,12,78,8,0,0),
              "empty-jukebox comparator fixture clears");
        gm_world_set_block_meta(r.world,11,78,8,0,0);
        {
            static const int command_ids[] = {137, 210, 211};
            for (int command_index = 0; command_index < 3;
                    ++command_index) {
                int command_id = command_ids[command_index];
                gm_world_set_block_meta(
                    r.world,11,78,8,command_id,2);
                CHECK(gm_runtime_load_block(&r,12,78,8,149,1),
                      "command-block comparator fixture loads");
                gm_world_set_block_meta(r.world,13,78,8,123,0);
                CHECK(gm_runtime_command_block_set_success(
                          &r,0,11,78,8,7),
                      "inert command-block success count restores");
                comparator_due=r.clock.total_time+3;
                CHECK(gm_runtime_schedule_tick(
                          &r,12,78,8,149,comparator_due,0,0),
                      "saved command-block comparator callback enters scheduler");
                for(int tick=0;tick<3;++tick)
                    gm_runtime_tick(&r,lamp_idle);
                CHECK(gm_runtime_comparator_get(&r,0,&comparator) &&
                      comparator.output_signal==7 &&
                      gm_world_meta(r.world,12,78,8)==9 &&
                      gm_world_block(r.world,13,78,8)==124 &&
                      gm_runtime_scheduled_tick_count(&r)==0,
                      "all command-block variants resume as output seven");
                CHECK(gm_runtime_load_block(&r,12,78,8,0,0),
                      "command-block comparator fixture clears");
                CHECK(gm_runtime_set_block(&r,11,78,8,0,0),
                      "command-block source fixture clears");
            }
            gm_world_set_block_meta(r.world,11,78,8,137,2);
            CHECK(gm_runtime_command_block_set_success(
                      &r,0,11,78,8,0),
                  "zero-success command block restores");
            CHECK(gm_runtime_load_block(&r,12,78,8,149,1),
                  "zero-success command comparator fixture loads");
            gm_world_set_block_meta(r.world,13,78,8,123,0);
            comparator_due=r.clock.total_time+3;
            CHECK(gm_runtime_schedule_tick(
                      &r,12,78,8,149,comparator_due,0,0),
                  "zero-success command comparator callback enters scheduler");
            for(int tick=0;tick<3;++tick)
                gm_runtime_tick(&r,lamp_idle);
            CHECK(gm_runtime_comparator_get(&r,0,&comparator) &&
                  comparator.output_signal==0 &&
                  gm_world_meta(r.world,12,78,8)==1 &&
                  gm_world_block(r.world,13,78,8)==123,
                  "zero-success command block remains comparator output zero");
            CHECK(gm_runtime_load_block(&r,12,78,8,0,0)
                      && gm_runtime_set_block(&r,11,78,8,0,0),
                  "zero-success command fixture clears");
        }
        {
            GmRuntimeItemFrame frame;
            gm_world_set_block_meta(r.world,10,78,8,0,0);
            gm_world_set_block_meta(r.world,11,77,8,1,0);
            gm_world_set_block_meta(r.world,11,78,8,1,0);
            gm_world_set_block_meta(r.world,12,77,8,1,0);
            CHECK(gm_runtime_load_block(&r,12,78,8,149,1),
                  "item-frame comparator fixture loads");
            gm_world_set_block_meta(r.world,13,78,8,123,0);
            CHECK(gm_runtime_item_frame_set(
                      &r,0,200,10.96875,78.5,8.5,
                      10,78,8,4,1,1,0,6),
                  "rotation-6 WEST item frame restores");
            comparator_due=r.clock.total_time+3;
            CHECK(gm_runtime_schedule_tick(
                      &r,12,78,8,149,comparator_due,0,0),
                  "saved item-frame comparator callback enters scheduler");
            for(int tick=0;tick<3;++tick)
                gm_runtime_tick(&r,lamp_idle);
            CHECK(gm_runtime_comparator_get(&r,0,&comparator) &&
                  comparator.output_signal==7 &&
                  gm_world_meta(r.world,12,78,8)==9 &&
                  gm_world_block(r.world,13,78,8)==124,
                  "rotation-6 item frame resumes as comparator output seven");
            CHECK(gm_runtime_item_frame_set(
                      &r,0,200,10.96875,78.5,8.5,
                      10,78,8,4,1,1,0,7),
                  "rotation-7 item frame state replaces in place");
            comparator_due=r.clock.total_time+3;
            CHECK(gm_runtime_schedule_tick(
                      &r,12,78,8,149,comparator_due,0,0),
                  "rotation-7 frame comparator callback enters scheduler");
            for(int tick=0;tick<3;++tick)
                gm_runtime_tick(&r,lamp_idle);
            CHECK(gm_runtime_comparator_get(&r,0,&comparator) &&
                  comparator.output_signal==8,
                  "rotation-7 item frame produces comparator output eight");
            CHECK(gm_runtime_item_frame_set(
                      &r,0,200,10.96875,78.5,8.5,
                      10,78,8,4,0,0,0,0),
                  "empty exact item frame replaces the displayed stack");
            comparator_due=r.clock.total_time+3;
            CHECK(gm_runtime_schedule_tick(
                      &r,12,78,8,149,comparator_due,0,0),
                  "empty-frame comparator callback enters scheduler");
            for(int tick=0;tick<3;++tick)
                gm_runtime_tick(&r,lamp_idle);
            CHECK(gm_runtime_comparator_get(&r,0,&comparator) &&
                  comparator.output_signal==0 &&
                  gm_world_meta(r.world,12,78,8)==1,
                  "empty item frame produces comparator output zero");
            for(int tick=0;tick<4;++tick)
                gm_runtime_tick(&r,lamp_idle);
            CHECK(gm_world_block(r.world,13,78,8)==123,
                  "empty-frame output hands off the exact lamp-off delay");
            CHECK(gm_runtime_item_frame_get(&r,0,&frame)
                      && frame.item==0 && frame.rotation==0,
                  "empty item-frame source remains represented");
            CHECK(!gm_runtime_item_frame_set(
                      &r,0,200,10.96875,78.5,8.5,
                      10,78,8,4,2,1,0,0),
                  "item-frame comparator subset rejects non-plain items");
            CHECK(gm_runtime_load_block(&r,12,78,8,0,0)
                      && gm_runtime_set_block(&r,11,78,8,0,0)
                      && gm_runtime_item_frame_count(&r)==0,
                  "item-frame comparator fixture clears and retires");
        }
        for(int y=77;y<=79;++y)
            for(int z=7;z<=9;++z)
                for(int x=11;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,11,77,8,1,0);
        gm_world_set_block_meta(r.world,12,77,7,1,0);
        gm_world_set_block_meta(r.world,12,77,8,1,0);
        gm_world_set_block_meta(r.world,11,78,8,147,0);
        gm_world_set_block_meta(r.world,12,78,7,55,8);
        CHECK(gm_runtime_load_block(&r,12,78,8,149,1),
              "compare-mode side-input fixture loads");
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        CHECK(gm_runtime_set_block(&r,11,78,8,147,7) &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "compare mode schedules when analog output changes");
        gm_runtime_tick(&r,lamp_idle);
        gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_runtime_comparator_get(&r,0,&comparator) &&
              comparator.output_signal==7 &&
              gm_world_meta(r.world,12,78,8)==1 &&
              gm_world_block(r.world,13,78,8)==123,
              "higher side input preserves analog 7 but suppresses power");
        CHECK(gm_runtime_load_block(&r,12,78,8,0,0),
              "compare-mode side-input fixture clears");
        gm_world_set_block_meta(r.world,11,78,8,147,0);
        gm_world_set_block_meta(r.world,12,78,7,55,5);
        CHECK(gm_runtime_load_block(&r,12,78,8,149,5),
              "subtract-mode side-input fixture loads");
        CHECK(gm_runtime_set_block(&r,11,78,8,147,7) &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "subtract mode schedules for a changed analog difference");
        gm_runtime_tick(&r,lamp_idle);
        gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_runtime_comparator_get(&r,0,&comparator) &&
              comparator.output_signal==2 &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==124,
              "subtract mode stores 7-5 and powers its exact output");
        CHECK(gm_runtime_load_block(&r,12,78,8,0,0),
              "subtract-mode difference fixture clears");
        gm_world_set_block_meta(r.world,11,78,8,147,0);
        gm_world_set_block_meta(r.world,12,78,7,55,7);
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        CHECK(gm_runtime_load_block(&r,12,78,8,149,5),
              "subtract-equal fixture loads");
        CHECK(gm_runtime_set_block(&r,11,78,8,147,7) &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "subtract equality reproduces vanilla's scheduled callback");
        gm_runtime_tick(&r,lamp_idle);
        gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_runtime_comparator_get(&r,0,&comparator) &&
              comparator.output_signal==0 &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==123,
              "unchanged subtract-zero callback leaves powered state alone");
        CHECK(gm_runtime_load_block(&r,12,78,8,0,0),
              "subtract-equal fixture clears");
        gm_world_set_block_meta(r.world,11,78,8,147,0);
        gm_world_set_block_meta(r.world,12,78,7,55,8);
        CHECK(gm_runtime_load_block(&r,12,78,8,149,5),
              "subtract-higher fixture loads");
        CHECK(gm_runtime_set_block(&r,11,78,8,147,7) &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "unchanged subtract-zero state avoids a vacuous callback");
        CHECK(gm_runtime_load_block(&r,12,78,8,0,0),
              "subtract-higher fixture clears");
        for(int y=77;y<=79;++y)
            for(int z=7;z<=9;++z)
                for(int x=11;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,11,78,8,0,0);
        gm_world_set_block_meta(r.world,12,77,8,1,0);
        gm_world_set_block_meta(r.world,12,78,8,77,13);
        gm_world_set_block_meta(r.world,13,78,8,124,0);
        long long button_due = r.clock.total_time + 3;
        CHECK(gm_runtime_schedule_tick(
                  &r,12,78,8,77,button_due,0,0),
              "powered floor button saved callback enters scheduler");
        for(int tick=0;tick<3;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,12,78,8)==77 &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==124 &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "button release schedules the lamp's independent +4 delay");
        for(int tick=0;tick<4;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,13,78,8)==123 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "lamp turns off four ticks after button release");
        for(int y=77;y<=79;++y)
            for(int z=7;z<=9;++z)
                for(int x=11;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,77,8,1,0);
        gm_world_set_block_meta(r.world,12,78,8,143,5);
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        CHECK(gm_runtime_spawn_arrow_fixture(
                  &r,7001,12.5,77.95,8.5,0.0,0.0,0.0,1,0),
              "stationary wooden-button arrow fixture spawns");
        gm_runtime_tick(&r,lamp_idle);
        long long wood_button_due=r.clock.total_time+30;
        CHECK(gm_world_block(r.world,12,78,8)==143 &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==124 &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
              lamp_tick.block==143&&lamp_tick.x==12&&
              lamp_tick.y==78&&lamp_tick.z==8&&
              lamp_tick.time==wood_button_due,
              "arrow overlap powers wooden button/lamp and schedules +30");
        for(int tick=0;tick<29;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_meta(r.world,12,78,8)==13 &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
              lamp_tick.block==143&&lamp_tick.time==wood_button_due,
              "occupied wooden button holds its first callback through +29");
        gm_runtime_tick(&r,lamp_idle);
        long long wood_button_second_due=wood_button_due+30;
        CHECK(gm_world_meta(r.world,12,78,8)==13 &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
              lamp_tick.block==143&&lamp_tick.time==wood_button_second_due,
              "occupied wooden button replaces due callback with exact +30");
        for(int i=0;i<GM_RUNTIME_PROJECTILES;++i)
            if(r.projectiles[i].active&&r.projectiles[i].eid==7001)
                r.projectiles[i].active=0;
        for(int tick=0;tick<29;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_meta(r.world,12,78,8)==13 &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "arrow-free wooden button stays powered until its due callback");
        gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==124 &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
              lamp_tick.block==124&&
              lamp_tick.time==wood_button_second_due+4,
              "arrow-free callback releases wooden button and schedules lamp +4");
        for(int tick=0;tick<4;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,13,78,8)==123 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "wooden-button arrow release turns lamp off at exact +4");
        gm_world_set_block_meta(r.world,12,78,8,77,5);
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        CHECK(gm_runtime_spawn_arrow_fixture(
                  &r,7002,12.5,77.95,8.5,0.0,0.0,0.0,1,0),
              "stone-button arrow-negative fixture spawns");
        gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==123 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "arrows do not activate stone buttons");
        for(int i=0;i<GM_RUNTIME_PROJECTILES;++i)
            if(r.projectiles[i].active&&r.projectiles[i].eid==7002)
                r.projectiles[i].active=0;
        for(int y=77;y<=79;++y)
            for(int z=7;z<=9;++z)
                for(int x=11;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int z=7;z<=9;++z)
            for(int x=11;x<=14;++x)
                gm_world_set_block_meta(r.world,x,77,z,1,0);
        gm_world_set_block_meta(r.world,12,78,8,70,1);
        gm_world_set_block_meta(r.world,13,78,8,124,0);
        long long plate_due = r.clock.total_time + 3;
        CHECK(gm_runtime_schedule_tick(
                  &r,12,78,8,70,plate_due,0,0),
              "powered stone-pressure-plate callback enters scheduler");
        for(int tick=0;tick<2;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,12,78,8)==70 &&
              gm_world_meta(r.world,12,78,8)==1 &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "unoccupied pressure plate remains powered before +3");
        gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,12,78,8)==70 &&
              gm_world_meta(r.world,12,78,8)==0 &&
              gm_world_block(r.world,13,78,8)==124 &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
              lamp_tick.block==124&&lamp_tick.time==plate_due+4,
              "pressure-plate release hands the lamp an exact +4 callback");
        for(int tick=0;tick<4;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,13,78,8)==123 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "plate-powered lamp turns off four ticks after release");
        gm_world_set_block_meta(r.world,11,77,8,1,0);
        gm_world_set_block_meta(r.world,12,77,8,1,0);
        gm_world_set_block_meta(r.world,13,77,8,1,0);
        gm_world_set_block_meta(r.world,11,78,8,0,0);
        gm_world_set_block_meta(r.world,12,78,8,55,0);
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        CHECK(gm_runtime_set_block(&r,11,78,8,152,0) &&
              gm_world_meta(r.world,12,78,8)==15 &&
              gm_world_block(r.world,13,78,8)==124,
              "flat dust propagates 15 and powers its lamp in the edit tick");
        long long wire_lamp_due = r.clock.total_time + 4;
        CHECK(gm_runtime_set_block(&r,11,78,8,0,0) &&
              gm_world_meta(r.world,12,78,8)==0 &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
              lamp_tick.block==124&&lamp_tick.time==wire_lamp_due,
              "flat dust removal drains to zero and schedules lamp +4");
        for(int tick=0;tick<4;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,13,78,8)==123 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "dust-powered lamp turns off on its exact delayed callback");
        for(int y=77;y<=79;++y)
            for(int z=7;z<=9;++z)
                for(int x=22;x<=28;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,22,77,8,1,0);
        gm_world_set_block_meta(r.world,23,77,8,89,0);
        gm_world_set_block_meta(r.world,23,78,8,55,0);
        gm_world_set_block_meta(r.world,24,77,8,1,0);
        gm_world_set_block_meta(r.world,24,78,8,123,0);
        CHECK(gm_runtime_set_block(&r,22,78,8,152,0) &&
              gm_world_meta(r.world,23,78,8)==15 &&
              gm_world_block(r.world,24,78,8)==124,
              "glowstone-supported dust propagates power to its lamp");
        gm_world_set_block_meta(r.world,26,77,8,1,0);
        gm_world_set_block_meta(r.world,27,77,8,20,0);
        gm_world_set_block_meta(r.world,27,78,8,55,0);
        gm_world_set_block_meta(r.world,28,77,8,1,0);
        gm_world_set_block_meta(r.world,28,78,8,123,0);
        CHECK(gm_runtime_set_block(&r,26,78,8,152,0) &&
              gm_world_meta(r.world,27,78,8)==0 &&
              gm_world_block(r.world,28,78,8)==123,
              "glass support remains outside the bounded dust proof");
        {
            static const int support_id[3]={44,53,78};
            static const int support_meta[3]={8,4,7};
            static const int reject_meta[3]={0,0,6};
            for(int index=0;index<3;++index){
                for(int y=76;y<=79;++y)
                    for(int z=7;z<=9;++z)
                        for(int x=22;x<=24;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_world_set_block_meta(r.world,22,77,8,1,0);
                gm_world_set_block_meta(
                    r.world,23,77,8,support_id[index],support_meta[index]);
                if(support_id[index]==78)
                    gm_world_set_block_meta(r.world,23,76,8,1,0);
                gm_world_set_block_meta(r.world,23,78,8,55,0);
                gm_world_set_block_meta(r.world,24,77,8,1,0);
                gm_world_set_block_meta(r.world,24,78,8,123,0);
                CHECK(gm_runtime_set_block(&r,22,78,8,152,0) &&
                      gm_world_meta(r.world,23,78,8)==15 &&
                      gm_world_block(r.world,24,78,8)==124,
                      "fully-opaque non-normal support propagates dust");
                for(int y=76;y<=79;++y)
                    for(int z=7;z<=9;++z)
                        for(int x=22;x<=24;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_world_set_block_meta(r.world,22,77,8,1,0);
                gm_world_set_block_meta(
                    r.world,23,77,8,support_id[index],reject_meta[index]);
                if(support_id[index]==78)
                    gm_world_set_block_meta(r.world,23,76,8,1,0);
                gm_world_set_block_meta(r.world,23,78,8,55,0);
                gm_world_set_block_meta(r.world,24,77,8,1,0);
                gm_world_set_block_meta(r.world,24,78,8,123,0);
                CHECK(gm_runtime_set_block(&r,22,78,8,152,0) &&
                      gm_world_meta(r.world,23,78,8)==0 &&
                      gm_world_block(r.world,24,78,8)==123,
                      "lower or incomplete support does not propagate dust");
            }
        }
        for(int y=77;y<=79;++y)
            for(int z=7;z<=9;++z)
                for(int x=21;x<=25;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,21,77,8,1,0);
        gm_world_set_block_meta(r.world,22,77,8,1,0);
        gm_world_set_block_meta(r.world,22,78,8,55,0);
        gm_world_set_block_meta(r.world,22,79,8,1,0);
        gm_world_set_block_meta(r.world,23,77,8,1,0);
        gm_world_set_block_meta(r.world,23,78,8,123,0);
        CHECK(gm_runtime_set_block(&r,21,78,8,152,0) &&
              gm_world_meta(r.world,22,78,8)==15 &&
              gm_world_block(r.world,23,78,8)==124,
              "flat dust propagates under a normal-cube ceiling");
        for(int y=77;y<=80;++y)
            for(int z=7;z<=9;++z)
                for(int x=21;x<=25;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,21,77,8,1,0);
        gm_world_set_block_meta(r.world,22,77,8,1,0);
        gm_world_set_block_meta(r.world,22,78,8,55,0);
        gm_world_set_block_meta(r.world,22,79,8,20,0);
        gm_world_set_block_meta(r.world,23,78,8,1,0);
        gm_world_set_block_meta(r.world,23,79,8,55,0);
        gm_world_set_block_meta(r.world,24,78,8,1,0);
        gm_world_set_block_meta(r.world,24,79,8,55,0);
        gm_world_set_block_meta(r.world,25,78,8,1,0);
        gm_world_set_block_meta(r.world,25,79,8,123,0);
        CHECK(gm_runtime_set_block(&r,21,78,8,152,0) &&
              gm_world_meta(r.world,22,78,8)==15 &&
              gm_world_meta(r.world,23,79,8)==14 &&
              gm_world_meta(r.world,24,79,8)==13 &&
              gm_world_block(r.world,25,79,8)==124,
              "non-normal glass headroom permits the exact dust climb");
        for(int y=77;y<=80;++y)
            for(int z=7;z<=9;++z)
                for(int x=21;x<=25;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,21,77,8,1,0);
        gm_world_set_block_meta(r.world,22,77,8,1,0);
        gm_world_set_block_meta(r.world,22,78,8,55,0);
        gm_world_set_block_meta(r.world,22,79,8,1,0);
        gm_world_set_block_meta(r.world,23,78,8,1,0);
        gm_world_set_block_meta(r.world,23,79,8,55,0);
        gm_world_set_block_meta(r.world,24,78,8,1,0);
        gm_world_set_block_meta(r.world,24,79,8,55,0);
        gm_world_set_block_meta(r.world,25,78,8,1,0);
        gm_world_set_block_meta(r.world,25,79,8,123,0);
        CHECK(gm_runtime_set_block(&r,21,78,8,152,0) &&
              gm_world_meta(r.world,22,78,8)==15 &&
              gm_world_meta(r.world,23,79,8)==0 &&
              gm_world_meta(r.world,24,79,8)==0 &&
              gm_world_block(r.world,25,79,8)==123,
              "normal-cube headroom blocks the upward dust edge");
        for(int y=77;y<=80;++y)
            for(int x=16;x<=21;++x)
                gm_world_set_block_meta(r.world,x,y,8,0,0);
        gm_world_set_block_meta(r.world,17,77,8,1,0);
        gm_world_set_block_meta(r.world,17,78,8,55,0);
        gm_world_set_block_meta(r.world,18,78,8,1,0);
        gm_world_set_block_meta(r.world,18,79,8,55,0);
        gm_world_set_block_meta(r.world,19,78,8,1,0);
        gm_world_set_block_meta(r.world,19,79,8,55,0);
        gm_world_set_block_meta(r.world,20,78,8,1,0);
        gm_world_set_block_meta(r.world,20,79,8,123,0);
        CHECK(gm_runtime_set_block(&r,16,78,8,152,0) &&
              gm_world_meta(r.world,17,78,8)==15 &&
              gm_world_meta(r.world,18,79,8)==14 &&
              gm_world_meta(r.world,19,79,8)==13 &&
              gm_world_block(r.world,20,79,8)==124,
              "dust climbs one clear-headed stone step with exact attenuation");
        long long vertical_lamp_due = r.clock.total_time + 4;
        CHECK(gm_runtime_set_block(&r,16,78,8,0,0) &&
              gm_world_meta(r.world,17,78,8)==0 &&
              gm_world_meta(r.world,18,79,8)==0 &&
              gm_world_meta(r.world,19,79,8)==0 &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
              lamp_tick.block==124&&lamp_tick.x==20&&lamp_tick.y==79&&
              lamp_tick.z==8&&lamp_tick.time==vertical_lamp_due,
              "vertical dust drains and hands off the lamp's +4 callback");
        for(int tick=0;tick<4;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,20,79,8)==123 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "vertical-dust endpoint lamp turns off at +4");
        for(int y=77;y<=80;++y)
            for(int z=7;z<=9;++z)
                for(int x=16;x<=20;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,17,77,8,1,0);
        gm_world_set_block_meta(r.world,17,78,8,55,14);
        gm_world_set_block_meta(r.world,18,78,8,1,0);
        gm_world_set_block_meta(r.world,18,79,8,55,15);
        gm_world_set_block_meta(r.world,19,79,8,152,0);
        CHECK(gm_runtime_set_block(&r,17,78,9,123,0) &&
              gm_world_block(r.world,17,78,9)==123 &&
              gm_world_meta(r.world,17,78,8)==14 &&
              gm_world_meta(r.world,18,79,8)==15,
              "climbing dust does not weak-power a perpendicular lamp");
        CHECK(gm_runtime_set_block(&r,16,78,8,123,0) &&
              gm_world_block(r.world,16,78,8)==124,
              "climbing dust weak-powers the face along its connection");
        for(int y=77;y<=80;++y)
            for(int z=7;z<=9;++z)
                for(int x=16;x<=21;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,17,78,8,1,0);
        gm_world_set_block_meta(r.world,17,79,8,55,14);
        gm_world_set_block_meta(r.world,18,77,8,1,0);
        gm_world_set_block_meta(r.world,18,78,8,55,15);
        gm_world_set_block_meta(r.world,19,78,8,152,0);
        CHECK(gm_runtime_set_block(&r,17,79,9,123,0) &&
              gm_world_block(r.world,17,79,9)==123 &&
              gm_world_meta(r.world,17,79,8)==14 &&
              gm_world_meta(r.world,18,78,8)==15,
              "descending dust does not weak-power a perpendicular lamp");
        CHECK(gm_runtime_set_block(&r,16,79,8,123,0) &&
              gm_world_block(r.world,16,79,8)==124,
              "descending dust weak-powers the face along its connection");
        for(int y=77;y<=80;++y)
            for(int z=7;z<=9;++z)
                for(int x=16;x<=21;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        {
            static const int unpowered_diode[2]={93,149};
            static const int powered_diode[2]={94,150};
            for(int index=0;index<2;++index){
                gm_world_set_block_meta(r.world,17,77,8,152,0);
                gm_world_set_block_meta(r.world,17,78,8,55,15);
                gm_world_set_block_meta(r.world,18,77,8,1,0);
                gm_world_set_block_meta(
                    r.world,18,78,8,unpowered_diode[index],0);
                CHECK(gm_runtime_set_block(&r,17,78,9,123,0) &&
                      gm_world_block(r.world,17,78,9)==124,
                      "wrong-axis diode leaves powered dust in dot shape");
                gm_world_set_block_meta(r.world,17,78,9,0,0);
                gm_world_set_block_meta(
                    r.world,18,78,8,powered_diode[index],3);
                CHECK(gm_runtime_set_block(&r,17,78,9,123,0) &&
                      gm_world_block(r.world,17,78,9)==123,
                      "aligned diode suppresses perpendicular dust output");
                for(int y=77;y<=79;++y)
                    for(int z=7;z<=9;++z)
                        for(int x=16;x<=19;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
            }
        }
        memset(&r.entities,0,sizeof r.entities);
        for(int y=77;y<=79;++y)
            for(int z=7;z<=9;++z)
                for(int x=11;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,77,8,1,0);
        gm_world_set_block_meta(r.world,12,78,8,55,0);
        gm_world_set_block_meta(r.world,13,78,8,1,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,7100) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,13,78,8,0,0) &&
              gm_world_block(r.world,12,78,8)==55 &&
              r.entities.n_active==0 &&
              r.next_entity_id==7100 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "adjacent removal keeps supported dust without drop or RNG");
        gm_world_set_block_meta(r.world,13,78,8,1,0);
        CHECK(gm_runtime_set_block(&r,12,77,8,0,0) &&
              gm_world_block(r.world,12,77,8)==0 &&
              gm_world_block(r.world,12,78,8)==0 &&
              r.entities.n_active==1 &&
              r.entities.ents[0].eid==7100 &&
              r.entities.ents[0].item==331 &&
              r.entities.ents[0].count==1 &&
              r.entities.ents[0].meta==0 &&
              r.entities.ents[0].age==0 &&
              r.entities.ents[0].pickup_delay==10 &&
              r.next_entity_id==7101 &&
              r.world_random_seed48==UINT64_C(0x2D3873C4CD04) &&
              r.math_random_seed48==UINT64_C(0x33E01D26154D),
              "support removal drops dust with exact item and spawn cursors");
        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX;
        gm_world_set_block_meta(r.world,12,77,8,1,0);
        gm_world_set_block_meta(r.world,12,78,8,55,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,7110) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,77,8,0,0) &&
              gm_world_block(r.world,12,77,8)==0 &&
              gm_world_block(r.world,12,78,8)==55 &&
              r.entities.n_active==GM_LIVE_MAX &&
              r.next_entity_id==7110 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "full item pool preserves unsupported dust and drop cursors");
        memset(&r.entities,0,sizeof r.entities);
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;
        for(int y=77;y<=79;++y)
            for(int x=11;x<=15;++x)
                gm_world_set_block_meta(r.world,x,y,8,0,0);
        gm_world_set_block_meta(r.world,11,78,8,152,0);
        gm_world_set_block_meta(r.world,12,77,8,1,0);
        gm_world_set_block_meta(r.world,12,78,8,55,15);
        gm_world_set_block_meta(r.world,13,77,8,1,0);
        gm_world_set_block_meta(r.world,13,78,8,55,14);
        gm_world_set_block_meta(r.world,14,78,8,124,0);
        long long support_lamp_due=r.clock.total_time+4;
        CHECK(gm_runtime_set_entity_id_cursor(&r,7200) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,77,8,0,0) &&
              gm_world_block(r.world,12,78,8)==0 &&
              gm_world_meta(r.world,13,78,8)==0 &&
              gm_world_block(r.world,14,78,8)==124 &&
              r.entities.n_active==1 &&
              r.entities.ents[0].eid==7200 &&
              r.entities.ents[0].item==331 &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
              lamp_tick.block==124&&lamp_tick.x==14&&lamp_tick.y==78&&
              lamp_tick.z==8&&lamp_tick.time==support_lamp_due,
              "powered support loss drops wire, drains line, and queues lamp");
        for(int tick=0;tick<3;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,14,78,8)==124 &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "support-loss lamp stays lit through the third delayed tick");
        gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,14,78,8)==123 &&
              gm_runtime_scheduled_tick_count(&r)==0 &&
              r.entities.ents[0].age==4 &&
              r.entities.ents[0].pickup_delay==6,
              "support-loss lamp turns off at +4 while its item advances");
        memset(&r.entities,0,sizeof r.entities);
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;
        for(int y=77;y<=79;++y)
            for(int z=7;z<=9;++z)
                for(int x=11;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,11,78,8,152,0);
        gm_world_set_block_meta(r.world,12,77,8,1,0);
        gm_world_set_block_meta(r.world,12,78,8,55,15);
        gm_world_set_block_meta(r.world,13,77,8,124,0);
        long long direct_wire_due=r.clock.total_time+4;
        CHECK(gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,0,0) &&
              r.entities.n_active==0 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765) &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
              lamp_tick.block==124&&lamp_tick.x==13&&lamp_tick.y==77&&
              lamp_tick.z==8&&lamp_tick.time==direct_wire_due,
              "direct powered-wire break notifies through support");
        for(int tick=0;tick<3;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,13,77,8)==124 &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "direct wire support lamp remains lit through +3");
        gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,13,77,8)==123 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "direct wire support lamp releases at exact +4");
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;
        for(int y=77;y<=79;++y)
            for(int z=7;z<=9;++z)
                for(int x=11;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,77,8,1,0);
        gm_world_set_block_meta(r.world,12,78,8,55,0);
        gm_world_set_block_meta(r.world,13,77,8,123,0);
        CHECK(gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_block(&r,12,78,8,0,0) &&
              gm_world_block(r.world,13,77,8)==123 &&
              r.entities.n_active==0 &&
              r.world_random_seed48==UINT64_C(0) &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "direct unpowered-wire break has no consumer callback");
        for(int powered=0;powered<=1;++powered){
            r.scheduled_tick_count=0;
            r.scheduled_tick_next_order=0;
            for(int y=77;y<=80;++y)
                for(int z=7;z<=9;++z)
                    for(int x=11;x<=14;++x)
                        gm_world_set_block_meta(r.world,x,y,z,0,0);
            gm_world_set_block_meta(r.world,12,78,8,1,0);
            gm_world_set_block_meta(r.world,13,77,8,1,0);
            gm_world_set_block_meta(
                r.world,13,78,8,powered?94:93,1);
            long long wire_add_due=r.clock.total_time+2;
            CHECK(gm_runtime_set_world_random_seed48(
                      &r,UINT64_C(0)) &&
                  gm_runtime_set_math_random_seed48(
                      &r,UINT64_C(0x0FEDCBA98765)) &&
                  gm_runtime_set_block(&r,12,79,8,55,0) &&
                  gm_world_block(r.world,12,79,8)==55 &&
                  gm_world_meta(r.world,12,79,8)==0 &&
                  gm_world_block(r.world,13,78,8)==(powered?94:93) &&
                  r.entities.n_active==0 &&
                  r.world_random_seed48==UINT64_C(0) &&
                  r.math_random_seed48==UINT64_C(0x0FEDCBA98765) &&
                  gm_runtime_scheduled_tick_count(&r)==powered,
                  "direct wire add runs exact vertical notification ring");
            if(powered){
                CHECK(gm_runtime_scheduled_tick_get(
                          &r,0,&lamp_tick) &&
                      lamp_tick.block==94&&lamp_tick.x==13&&
                      lamp_tick.y==78&&lamp_tick.z==8&&
                      lamp_tick.time==wire_add_due&&
                      lamp_tick.priority==-2,
                      "direct wire add wakes diagonal powered repeater +2");
                gm_runtime_tick(&r,lamp_idle);
                gm_runtime_tick(&r,lamp_idle);
                CHECK(gm_world_block(r.world,13,78,8)==93 &&
                      gm_runtime_scheduled_tick_count(&r)==0,
                      "wire on-add repeater release drains at exact +2");
            }else{
                CHECK(gm_world_block(r.world,13,78,8)==93 &&
                      gm_runtime_scheduled_tick_count(&r)==0,
                      "wire on-add unpowered repeater remains queue-free");
            }
        }
        memset(&r.entities,0,sizeof r.entities);
        {
            static const int diode_id[4]={94,93,149,149};
            static const int diode_meta[4]={0,0,8,0};
            static const int diode_output[4]={-1,-1,15,0};
            static const int powered[4]={1,0,1,0};
            for(int index=0;index<4;++index){
                r.comparator_count=0;
                r.scheduled_tick_count=0;
                r.scheduled_tick_next_order=0;
                for(int y=77;y<=79;++y)
                    for(int z=6;z<=9;++z)
                        for(int x=11;x<=14;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_world_set_block_meta(r.world,12,77,8,1,0);
                gm_world_set_block_meta(r.world,12,78,7,1,0);
                if(diode_output[index]>=0){
                    CHECK(gm_runtime_load_block(
                              &r,12,78,8,
                              diode_id[index],diode_meta[index]) &&
                          gm_runtime_comparator_set_output(
                              &r,0,12,78,8,diode_output[index]),
                          "direct comparator-break tile fixture restores");
                }else{
                    gm_world_set_block_meta(
                        r.world,12,78,8,
                        diode_id[index],diode_meta[index]);
                }
                if(diode_id[index]==94)
                    gm_world_set_block_meta(r.world,12,78,9,152,0);
                gm_world_set_block_meta(
                    r.world,13,78,7,powered[index]?124:123,0);
                long long direct_diode_due=r.clock.total_time+4;
                CHECK(gm_runtime_set_world_random_seed48(
                          &r,UINT64_C(0)) &&
                      gm_runtime_set_math_random_seed48(
                          &r,UINT64_C(0x0FEDCBA98765)) &&
                      gm_runtime_set_block(&r,12,78,8,0,0) &&
                      gm_world_block(r.world,12,78,8)==0 &&
                      r.comparator_count==0 &&
                      r.entities.n_active==0 &&
                      r.world_random_seed48==UINT64_C(0) &&
                      r.math_random_seed48==UINT64_C(0x0FEDCBA98765) &&
                      gm_runtime_scheduled_tick_count(&r)==powered[index],
                      "direct diode break retires state without RNG or item");
                if(powered[index]){
                    CHECK(gm_runtime_scheduled_tick_get(
                              &r,0,&lamp_tick) &&
                          lamp_tick.block==124&&lamp_tick.x==13&&
                          lamp_tick.y==78&&lamp_tick.z==7&&
                          lamp_tick.time==direct_diode_due,
                          "direct powered diode queues indirect lamp +4");
                    for(int tick=0;tick<4;++tick)
                        gm_runtime_tick(&r,lamp_idle);
                    CHECK(gm_world_block(r.world,13,78,7)==123 &&
                          gm_runtime_scheduled_tick_count(&r)==0,
                          "direct powered diode releases lamp at exact +4");
                }else{
                    CHECK(gm_world_block(r.world,13,78,7)==123 &&
                          gm_runtime_scheduled_tick_count(&r)==0,
                          "direct unpowered diode has no consumer callback");
                }
            }
        }
        for(int powered=0;powered<=1;++powered){
            r.scheduled_tick_count=0;
            r.scheduled_tick_next_order=0;
            for(int y=77;y<=79;++y)
                for(int z=6;z<=9;++z)
                    for(int x=11;x<=14;++x)
                        gm_world_set_block_meta(r.world,x,y,z,0,0);
            gm_world_set_block_meta(r.world,12,77,8,1,0);
            gm_world_set_block_meta(r.world,12,78,7,1,0);
            gm_world_set_block_meta(r.world,13,78,7,123,0);
            if(powered)
                gm_world_set_block_meta(r.world,12,78,9,152,0);
            CHECK(gm_runtime_set_world_random_seed48(
                      &r,UINT64_C(0)) &&
                  gm_runtime_set_math_random_seed48(
                      &r,UINT64_C(0x0FEDCBA98765)) &&
                  gm_runtime_set_block(
                      &r,12,78,8,powered?94:93,0) &&
                  gm_world_block(r.world,12,78,8)==(powered?94:93) &&
                  gm_world_block(r.world,13,78,7)==(powered?124:123) &&
                  r.entities.n_active==0 &&
                  r.world_random_seed48==UINT64_C(0) &&
                  r.math_random_seed48==UINT64_C(0x0FEDCBA98765) &&
                  gm_runtime_scheduled_tick_count(&r)==0,
                  "direct repeater add runs exact output notification");
        }
        for(int aligned=0;aligned<=1;++aligned){
            GmRuntimeComparator direct_comparator;
            r.comparator_count=0;
            r.scheduled_tick_count=0;
            r.scheduled_tick_next_order=0;
            for(int y=77;y<=79;++y)
                for(int z=7;z<=9;++z)
                    for(int x=11;x<=16;++x)
                        gm_world_set_block_meta(r.world,x,y,z,0,0);
            gm_world_set_block_meta(r.world,12,77,8,1,0);
            gm_world_set_block_meta(r.world,13,78,8,1,0);
            gm_world_set_block_meta(r.world,14,77,8,1,0);
            gm_world_set_block_meta(r.world,15,78,8,124,0);
            gm_world_set_block_meta(r.world,14,78,8,94,1);
            long long comparator_add_due=r.clock.total_time+2;
            CHECK(gm_runtime_set_world_random_seed48(
                      &r,UINT64_C(0)) &&
                  gm_runtime_set_math_random_seed48(
                      &r,UINT64_C(0x0FEDCBA98765)) &&
                  gm_runtime_set_block(
                      &r,12,78,8,149,aligned?1:3) &&
                  r.comparator_count==1 &&
                  gm_runtime_comparator_get(
                      &r,0,&direct_comparator) &&
                  direct_comparator.output_signal==0 &&
                  r.entities.n_active==0 &&
                  r.world_random_seed48==UINT64_C(0) &&
                  r.math_random_seed48==UINT64_C(0x0FEDCBA98765) &&
                  gm_runtime_scheduled_tick_count(&r)==aligned,
                  "direct comparator add creates output-zero tile exactly");
            if(aligned){
                CHECK(gm_runtime_scheduled_tick_get(
                          &r,0,&lamp_tick) &&
                      lamp_tick.block==94&&lamp_tick.x==14&&
                      lamp_tick.y==78&&lamp_tick.z==8&&
                      lamp_tick.time==comparator_add_due&&
                      lamp_tick.priority==-2,
                      "aligned comparator add wakes downstream repeater +2");
                gm_runtime_tick(&r,lamp_idle);
                gm_runtime_tick(&r,lamp_idle);
                CHECK(gm_world_block(r.world,14,78,8)==93 &&
                      gm_runtime_scheduled_tick_count(&r)==1,
                      "comparator add hands downstream lamp its +4 callback");
                for(int tick=0;tick<4;++tick)
                    gm_runtime_tick(&r,lamp_idle);
                CHECK(gm_world_block(r.world,15,78,8)==123 &&
                      gm_runtime_scheduled_tick_count(&r)==0,
                      "comparator add callback chain drains at exact +6");
            }else{
                CHECK(gm_world_block(r.world,14,78,8)==94 &&
                      gm_world_block(r.world,15,78,8)==124 &&
                      gm_runtime_scheduled_tick_count(&r)==0,
                      "wrong-direction comparator add leaves output untouched");
            }
        }
        for(int y=77;y<=79;++y)
            for(int z=7;z<=9;++z)
                for(int x=11;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        r.comparator_count=0;
        gm_world_set_block_meta(r.world,16,79,8,152,0);
        gm_world_set_block_meta(r.world,17,78,8,1,0);
        gm_world_set_block_meta(r.world,17,79,8,55,15);
        CHECK(gm_runtime_set_block(&r,18,78,8,123,0) &&
              gm_world_block(r.world,18,78,8)==124,
              "powered wire strongly powers stone into an on-add lamp");
        long long wire_strong_due = r.clock.total_time + 4;
        CHECK(gm_runtime_set_block(&r,16,79,8,0,0) &&
              gm_world_meta(r.world,17,79,8)==0 &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
              lamp_tick.block==124&&lamp_tick.x==18&&lamp_tick.y==78&&
              lamp_tick.z==8&&lamp_tick.time==wire_strong_due,
              "wire drain notifies around its formerly strong-powered stone");
        for(int tick=0;tick<4;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,18,78,8)==123 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "wire-strong-powered lamp turns off at +4");
        gm_world_set_block_meta(r.world,20,77,8,1,0);
        gm_world_set_block_meta(r.world,20,78,8,76,5);
        gm_world_set_block_meta(r.world,20,79,8,1,0);
        CHECK(gm_runtime_set_block(&r,21,79,8,123,0) &&
              gm_world_block(r.world,21,79,8)==124,
              "lit torch strongly powers the stone immediately above it");
        long long torch_strong_due = r.clock.total_time + 4;
        CHECK(gm_runtime_set_block(&r,20,78,8,0,0) &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
              lamp_tick.block==124&&lamp_tick.x==21&&lamp_tick.y==79&&
              lamp_tick.z==8&&lamp_tick.time==torch_strong_due,
              "lit-torch break notifies around its strong-powered support");
        for(int tick=0;tick<4;++tick)
            gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,21,79,8)==123 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "torch-strong-powered lamp turns off at +4");
        gm_world_set_block_meta(r.world,12,77,8,1,0);
        gm_world_set_block_meta(r.world,12,78,8,76,5);
        gm_world_set_block_meta(r.world,13,78,8,0,0);
        CHECK(gm_runtime_set_block(&r,12,77,8,152,0) &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&lamp_tick) &&
              lamp_tick.block==76 &&
              lamp_tick.time==r.clock.total_time+2,
              "powered floor-torch support schedules its exact +2 callback");
        gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,12,78,8)==76 &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "lit floor torch remains on through the first delayed tick");
        gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,12,78,8)==75 &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "floor torch turns off exactly two ticks after support power");
        CHECK(gm_runtime_set_block(&r,12,77,8,1,0) &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "unpowered floor-torch support schedules relight at +2");
        gm_runtime_tick(&r,lamp_idle);
        gm_runtime_tick(&r,lamp_idle);
        CHECK(gm_world_block(r.world,12,78,8)==76 &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "unburned floor torch relights exactly two ticks later");
        CHECK(r.redstone_torch_toggle_count==1,
              "ordinary floor-torch off callback records one toggle");
        for(int cycle=0;cycle<7;++cycle){
            CHECK(gm_runtime_set_block(&r,12,77,8,152,0),
                  "repeated support power schedules torch-off callback");
            gm_runtime_tick(&r,lamp_idle);
            uint64_t rng_before_burnout=r.world_random_seed48;
            gm_runtime_tick(&r,lamp_idle);
            CHECK(gm_world_block(r.world,12,78,8)==75,
                  "repeated powered callback turns floor torch off");
            if(cycle<6){
                CHECK(gm_runtime_set_block(&r,12,77,8,1,0),
                      "pre-burnout support release schedules relight");
                gm_runtime_tick(&r,lamp_idle);
                gm_runtime_tick(&r,lamp_idle);
                CHECK(gm_world_block(r.world,12,78,8)==76 &&
                      gm_runtime_scheduled_tick_count(&r)==0,
                      "first seven toggles still relight normally");
            }else{
                GmRuntimeScheduledTick recovery;
                long long recovery_due=r.clock.total_time+160;
                CHECK(r.redstone_torch_toggle_count==8 &&
                      gm_runtime_scheduled_tick_count(&r)==1 &&
                      gm_runtime_scheduled_tick_get(&r,0,&recovery) &&
                      recovery.block==75&&recovery.x==12&&
                      recovery.y==78&&recovery.z==8&&
                      recovery.time==recovery_due,
                      "eighth toggle burns out and schedules exact +160 recovery");
                CHECK(r.world_random_seed48==
                      java_lcg_steps(rng_before_burnout,32),
                      "torch burnout consumes two floats and fifteen doubles");
                CHECK(gm_runtime_set_block(&r,12,77,8,1,0) &&
                      gm_runtime_scheduled_tick_count(&r)==1,
                      "support release cannot replace pending burnout recovery");
                for(int tick=0;tick<159;++tick)
                    gm_runtime_tick(&r,lamp_idle);
                CHECK(gm_world_block(r.world,12,78,8)==75 &&
                      gm_runtime_scheduled_tick_count(&r)==1,
                      "burned-out torch remains off through recovery minus one");
                gm_runtime_tick(&r,lamp_idle);
                CHECK(gm_world_block(r.world,12,78,8)==76 &&
                      gm_runtime_scheduled_tick_count(&r)==0 &&
                      r.redstone_torch_toggle_count==0,
                      "burnout history prunes and torch relights at +160");
            }
        }
        {
            static const int support_dx[4]={-1,1,0,0};
            static const int support_dz[4]={0,0,-1,1};
            for(int index=0;index<4;++index){
                int meta=index+1;
                for(int dy=-1;dy<=1;++dy)
                    for(int dz=-1;dz<=1;++dz)
                        for(int dx=-1;dx<=1;++dx)
                            gm_world_set_block_meta(
                                r.world,20+dx,79+dy,8+dz,0,0);
                gm_world_set_block_meta(
                    r.world,20+support_dx[index],79,
                    8+support_dz[index],1,0);
                gm_world_set_block_meta(r.world,20,79,8,76,meta);
                CHECK(gm_runtime_set_block(
                          &r,20+support_dx[index],79,
                          8+support_dz[index],152,0) &&
                      gm_runtime_scheduled_tick_count(&r)==1,
                      "powered wall support schedules oriented torch-off");
                gm_runtime_tick(&r,lamp_idle);
                gm_runtime_tick(&r,lamp_idle);
                CHECK(gm_world_block(r.world,20,79,8)==75 &&
                      gm_world_meta(r.world,20,79,8)==meta &&
                      gm_runtime_scheduled_tick_count(&r)==0,
                      "wall torch turns off with orientation preserved");
                CHECK(gm_runtime_set_block(
                          &r,20+support_dx[index],79,
                          8+support_dz[index],1,0) &&
                      gm_runtime_scheduled_tick_count(&r)==1,
                      "unpowered wall support schedules oriented relight");
                gm_runtime_tick(&r,lamp_idle);
                gm_runtime_tick(&r,lamp_idle);
                CHECK(gm_world_block(r.world,20,79,8)==76 &&
                      gm_world_meta(r.world,20,79,8)==meta &&
                      gm_runtime_scheduled_tick_count(&r)==0,
                      "wall torch relights with orientation preserved");
            }
        }
        memset(&r.entities,0,sizeof r.entities);
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;
        for(int y=77;y<=79;++y)
            for(int z=7;z<=9;++z)
                for(int x=11;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,77,8,1,0);
        gm_world_set_block_meta(r.world,12,78,8,76,5);
        gm_world_set_block_meta(r.world,13,78,8,1,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,7300) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,13,78,8,0,0) &&
              gm_world_block(r.world,12,78,8)==76 &&
              r.entities.n_active==0 && r.next_entity_id==7300 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "unrelated removal keeps a supported floor redstone torch");
        gm_world_set_block_meta(r.world,13,78,8,1,0);
        CHECK(gm_runtime_set_block(&r,12,77,8,0,0) &&
              gm_world_block(r.world,12,77,8)==0 &&
              gm_world_block(r.world,12,78,8)==0 &&
              r.entities.n_active==1 &&
              r.entities.ents[0].eid==7300 &&
              r.entities.ents[0].item==76 &&
              r.entities.ents[0].count==1 &&
              r.entities.ents[0].meta==0 &&
              r.entities.ents[0].age==0 &&
              r.entities.ents[0].pickup_delay==10 &&
              r.next_entity_id==7301 &&
              r.world_random_seed48==UINT64_C(0x2D3873C4CD04) &&
              r.math_random_seed48==UINT64_C(0x33E01D26154D),
              "floor support removal drops lit redstone torch exactly");
        memset(&r.entities,0,sizeof r.entities);
        for(int y=77;y<=79;++y)
            for(int z=7;z<=9;++z)
                for(int x=10;x<=13;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,11,78,8,1,0);
        gm_world_set_block_meta(r.world,12,78,8,75,1);
        CHECK(gm_runtime_set_entity_id_cursor(&r,7310) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,11,78,8,0,0) &&
              gm_world_block(r.world,12,78,8)==0 &&
              r.entities.n_active==1 &&
              r.entities.ents[0].eid==7310 &&
              r.entities.ents[0].item==76 &&
              r.entities.ents[0].meta==0 &&
              r.next_entity_id==7311 &&
              r.world_random_seed48==UINT64_C(0x2D3873C4CD04) &&
              r.math_random_seed48==UINT64_C(0x33E01D26154D),
              "wall support removal maps unlit state to item 76 metadata zero");
        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX;
        gm_world_set_block_meta(r.world,12,77,8,1,0);
        gm_world_set_block_meta(r.world,12,78,8,76,5);
        CHECK(gm_runtime_set_entity_id_cursor(&r,7320) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,77,8,0,0) &&
              gm_world_block(r.world,12,78,8)==76 &&
              r.entities.n_active==GM_LIVE_MAX &&
              r.next_entity_id==7320 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "full item pool preserves unsupported torch and drop cursors");
        {
            static const int floor_id[8]={85,20,95,139,44,53,78,154};
            static const int floor_meta[8]={0,0,0,0,8,4,7,0};
            for(int index=0;index<8;++index){
                memset(&r.entities,0,sizeof r.entities);
                r.scheduled_tick_count=0;
                r.scheduled_tick_next_order=0;
                for(int y=77;y<=79;++y)
                    for(int z=7;z<=9;++z)
                        for(int x=19;x<=21;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_world_set_block_meta(
                    r.world,20,77,8,floor_id[index],floor_meta[index]);
                gm_world_set_block_meta(r.world,20,78,8,76,5);
                gm_world_set_block_meta(r.world,21,78,8,1,0);
                CHECK(gm_runtime_set_block(&r,21,78,8,0,0) &&
                      gm_world_block(r.world,20,78,8)==76 &&
                      r.entities.n_active==0,
                      "Forge torch-top supports retain a floor torch");
            }
        }
        {
            static const int invalid_id[3]={44,53,78};
            static const int invalid_meta[3]={0,0,6};
            for(int index=0;index<3;++index){
                memset(&r.entities,0,sizeof r.entities);
                r.scheduled_tick_count=0;
                r.scheduled_tick_next_order=0;
                for(int y=77;y<=79;++y)
                    for(int z=7;z<=9;++z)
                        for(int x=19;x<=21;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_world_set_block_meta(
                    r.world,20,77,8,invalid_id[index],invalid_meta[index]);
                gm_world_set_block_meta(r.world,20,78,8,76,5);
                gm_world_set_block_meta(r.world,21,78,8,1,0);
                CHECK(gm_runtime_set_block(&r,21,78,8,0,0) &&
                      gm_world_block(r.world,20,78,8)==0 &&
                      r.entities.n_active==1 &&
                      r.entities.ents[0].item==76,
                      "non-solid slab, stair, and snow tops drop floor torch");
            }
        }
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "trapped-chest viewer-power runtime initializes");
    if(r.world){
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
        for(int y=77;y<=81;++y)
            for(int z=4;z<=10;++z)
                for(int x=6;x<=20;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        /* Direct weak-power lamp beside the chest plus a second lamp beside
         * the stone below, which can light only through the chest's UP strong
         * output. */
        gm_world_set_block_meta(r.world,8,78,6,1,0);
        CHECK(gm_runtime_set_block(&r,8,79,6,146,2),
              "live trapped-chest placement creates its empty tile");
        gm_world_set_block_meta(r.world,9,79,6,123,0);
        gm_world_set_block_meta(r.world,9,78,6,123,0);
        {
            GmRuntimeChest chest;
            CHECK(gm_runtime_chest_get(&r,0,&chest) &&
                  chest.wx==8 && chest.wy==79 && chest.wz==6 &&
                  chest.state.loot_filled,
                  "live trapped chest exposes the exact empty tile");
        }
        gm_runtime_set_pose_state(
            &r,8.5,78.0,8.5,-180.0f,0.0f,
            0.0,0.0,0.0,1,0.0f);
        GmAction use=idle;use.use=1;use.do_place=1;
        gm_runtime_tick(&r,use);
        {
            GmRuntimeChest chest;
            CHECK(gm_runtime_chest_get(&r,0,&chest) &&
                  chest.state.te.num_players_using==0 &&
                  gm_world_block(r.world,9,79,6)==123 &&
                  gm_world_block(r.world,9,78,6)==123,
                  "use edge queues without opening on the client-input tick");
        }
        gm_runtime_tick(&r,idle);
        {
            GmRuntimeChest chest;
            CHECK(gm_runtime_chest_get(&r,0,&chest) &&
                  chest.state.te.num_players_using==1 &&
                  chest.state.te.lid_angle==0.1f &&
                  gm_world_block(r.world,9,79,6)==124 &&
                  gm_world_block(r.world,9,78,6)==124,
                  "next server tick opens viewer one and applies weak/up strong power");
        }
        long long trapped_lamp_due=r.clock.total_time+4;
        GmAction close=idle;close.close_container=1;
        gm_runtime_tick(&r,close);
        {
            GmRuntimeChest chest;
            int direct=0,strong=0;
            CHECK(gm_runtime_chest_get(&r,0,&chest) &&
                  chest.state.te.num_players_using==0 &&
                  gm_runtime_scheduled_tick_count(&r)==2,
                  "close decrements viewer and creates two independent lamp callbacks");
            for(int i=0;i<gm_runtime_scheduled_tick_count(&r);++i){
                GmRuntimeScheduledTick pending;
                CHECK(gm_runtime_scheduled_tick_get(&r,i,&pending) &&
                      pending.block==124&&pending.time==trapped_lamp_due,
                      "trapped-chest lamp callback has exact +4 due time");
                if(pending.x==9&&pending.y==79&&pending.z==6)direct=1;
                if(pending.x==9&&pending.y==78&&pending.z==6)strong=1;
            }
            CHECK(direct&&strong,
                  "close schedules both weak and upward-strong consumers");
        }
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(gm_world_block(r.world,9,79,6)==124 &&
              gm_world_block(r.world,9,78,6)==124 &&
              gm_runtime_scheduled_tick_count(&r)==2,
              "both trapped-chest lamps remain lit before +4");
        gm_runtime_tick(&r,idle);
        CHECK(gm_world_block(r.world,9,79,6)==123 &&
              gm_world_block(r.world,9,78,6)==123 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "both trapped-chest lamps turn off on exact +4 callbacks");

        /* Ordinary chest viewer count is real GUI state but never a producer. */
        gm_world_set_block_meta(r.world,12,78,6,1,0);
        gm_world_set_block_meta(r.world,12,79,6,54,2);
        CHECK(gm_runtime_chest_set_slot(
                  &r,0,12,79,6,0,0,0,0) &&
              gm_runtime_use_block(&r,12,79,6),
              "ordinary chest opens for the negative control");
        CHECK(gm_runtime_set_block(&r,13,79,6,123,0) &&
              gm_runtime_set_block(&r,13,78,6,123,0) &&
              gm_world_block(r.world,13,79,6)==123 &&
              gm_world_block(r.world,13,78,6)==123,
              "ordinary chest viewers provide neither weak nor strong power");

        /* Trapped strong power is directional: a chest beside a normal cube
         * must not power through that cube. */
        gm_world_set_block_meta(r.world,17,79,6,1,0);
        gm_world_set_block_meta(r.world,18,79,6,146,2);
        gm_runtime_set_pose_state(
            &r,18.5,78.0,8.5,-180.0f,0.0f,
            0.0,0.0,0.0,1,0.0f);
        CHECK(gm_runtime_chest_set_slot(
                  &r,0,18,79,6,0,0,0,0) &&
              gm_runtime_use_block(&r,18,79,6),
              "side-facing trapped chest opens for strong-power control");
        CHECK(gm_runtime_set_block(&r,16,79,6,123,0) &&
              gm_world_block(r.world,16,79,6)==123,
              "trapped chest does not strongly power a horizontal normal cube");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "packet pressure-plate activation runtime initializes");
    if(r.world){
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
        for(int y=76;y<=83;++y)
            for(int z=5;z<=10;++z)
                for(int x=6;x<=11;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int z=5;z<=10;++z)
            for(int x=6;x<=11;++x)
                gm_world_set_block_meta(r.world,x,77,z,1,0);
        gm_world_set_block_meta(r.world,8,78,8,70,0);
        gm_world_set_block_meta(r.world,9,78,8,123,0);
        gm_runtime_set_pose_state(
            &r,8.5,78.0,6.5,0.0f,0.0f,0.0,0.0,0.0,1,0.0f);
        long long packet_base=r.clock.total_time;
        r.player_move_packet.pending=1;
        r.player_move_packet.moving=1;
        r.player_move_packet.on_ground=1;
        r.player_move_packet.x=8.5-r.ox;
        r.player_move_packet.y=78.0;
        r.player_move_packet.z=8.5-r.oz;
        gm_runtime_tick(&r,idle);
        {
            GmRuntimeScheduledTick pending;
            CHECK(gm_world_meta(r.world,8,78,8)==1 &&
                  gm_world_block(r.world,9,78,8)==124 &&
                  gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                  pending.block==70&&pending.time==packet_base+20,
                  "packet collision activates plate with exact current +20 due");
        }
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "occupied pressure-plate runtime initializes");
    if(r.world){
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
        for(int y=76;y<=83;++y)
            for(int z=6;z<=10;++z)
                for(int x=6;x<=11;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int z=6;z<=10;++z)
            for(int x=6;x<=11;++x)
                gm_world_set_block_meta(r.world,x,77,z,1,0);
        gm_world_set_block_meta(r.world,8,78,8,70,0);
        gm_world_set_block_meta(r.world,9,78,8,123,0);
        gm_runtime_set_pose_state(
            &r,8.5,78.0,8.5,0.0f,0.0f,0.0,0.0,0.0,1,0.0f);
        long long occupied_base=r.clock.total_time;
        gm_runtime_tick(&r,idle);
        {
            GmRuntimeScheduledTick pending;
            CHECK(gm_world_meta(r.world,8,78,8)==1 &&
                  gm_world_block(r.world,9,78,8)==124 &&
                  gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                  pending.block==70&&pending.time==occupied_base+21,
                  "ordinary entity collision maps Java clock to exact +21 due");
        }
        for(int tick=0;tick<19;++tick)
            gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==1,
              "occupied plate callback remains pending through row 19");
        gm_runtime_tick(&r,idle);
        {
            GmRuntimeScheduledTick pending;
            CHECK(gm_world_meta(r.world,8,78,8)==1 &&
                  gm_world_block(r.world,9,78,8)==124 &&
                  gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                  pending.block==70&&pending.time==occupied_base+41,
                  "due occupied plate retains power and reschedules exact +20");
        }
    }
    gm_runtime_destroy(&r);

    {
        GmConfig mob_plate_cfg=cfg;
        mob_plate_cfg.mobs=0;
        CHECK(gm_runtime_init(&r,&mob_plate_cfg,err,sizeof err),
              "mob pressure-plate runtime initializes");
    }
    if(r.world){
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
        for(int y=76;y<=83;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=18;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int z=6;z<=10;++z)
            for(int x=10;x<=18;++x)
                gm_world_set_block_meta(r.world,x,77,z,1,0);
        gm_world_set_block_meta(r.world,12,78,8,70,0);
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        gm_world_set_block_meta(r.world,16,78,8,70,0);
        gm_world_set_block_meta(r.world,17,78,8,123,0);
        gm_runtime_set_pose_state(
            &r,8.5,78.0,8.5,0.0f,0.0f,0.0,0.0,0.0,1,0.0f);
        CHECK(gm_runtime_spawn_mob_fixture(
                  &r,EW_TYPE_PIG,4016,12.5,78.0,8.5,
                  0.0,0.0,0.0,0.0f,5.0f,0,0,0,0),
              "spawn collision-enabled stationary pig fixture");
        CHECK(gm_runtime_spawn_mob_fixture(
                  &r,EW_TYPE_PIG,4017,16.5,78.0,8.5,
                  0.0,0.0,0.0,0.0f,5.0f,1,0,0,0),
              "spawn true NoAI stationary pig fixture");
        long long mob_base=r.clock.total_time;
        gm_runtime_tick(&r,idle);
        {
            GmRuntimeScheduledTick pending;
            CHECK(gm_world_meta(r.world,12,78,8)==1 &&
                  gm_world_block(r.world,13,78,8)==124 &&
                  gm_world_meta(r.world,16,78,8)==0 &&
                  gm_world_block(r.world,17,78,8)==123 &&
                  gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                  pending.block==70&&pending.x==12 &&
                  pending.time==mob_base+21,
                  "ordinary pig collision activates at exact Java +21 while NoAI does not");
        }
        for(int tick=0;tick<19;++tick)
            gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==1,
              "mob-occupied plate callback remains pending through row 19");
        gm_runtime_tick(&r,idle);
        {
            GmRuntimeScheduledTick pending;
            CHECK(gm_world_meta(r.world,12,78,8)==1 &&
                  gm_world_block(r.world,13,78,8)==124 &&
                  gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                  pending.block==70&&pending.time==mob_base+41,
                  "mob-occupied callback retains power and reschedules exact +20");
        }
    }
    gm_runtime_destroy(&r);

    {
        GmConfig item_plate_cfg=cfg;
        item_plate_cfg.mobs=0;
        CHECK(gm_runtime_init(&r,&item_plate_cfg,err,sizeof err),
              "item pressure-plate runtime initializes");
    }
    if(r.world){
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
        for(int y=76;y<=83;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=18;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int z=6;z<=10;++z)
            for(int x=10;x<=18;++x)
                gm_world_set_block_meta(r.world,x,77,z,1,0);
        gm_world_set_block_meta(r.world,12,78,8,72,0);
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        gm_world_set_block_meta(r.world,16,78,8,70,0);
        gm_world_set_block_meta(r.world,17,78,8,123,0);
        gm_runtime_set_pose_state(
            &r,8.5,78.0,8.5,0.0f,0.0f,0.0,0.0,0.0,1,0.0f);
        CHECK(gm_runtime_spawn_item_fixture(
                  &r,4020,12.5,78.0,8.5,0.0,0.0,0.0,
                  1,1,0,0,32767,1),
              "spawn stationary item over wooden plate");
        CHECK(gm_runtime_spawn_item_fixture(
                  &r,4021,16.5,78.0,8.5,0.0,0.0,0.0,
                  1,1,0,0,32767,1),
              "spawn identical stationary item over stone plate");
        {
            McAABB boxes[2];
            CHECK(gm_live_item_boxes(&r.entities,boxes,2)==2 &&
                  boxes[0].minX==12.375&&boxes[0].maxX==12.625 &&
                  boxes[0].minY==78.0&&boxes[0].maxY==78.25 &&
                  boxes[0].minZ==8.375&&boxes[0].maxZ==8.625,
                  "EntityItem enumeration exposes exact 0.25 AABB");
        }
        long long item_base=r.clock.total_time;
        gm_runtime_tick(&r,idle);
        {
            GmRuntimeScheduledTick pending;
            CHECK(gm_world_meta(r.world,12,78,8)==1 &&
                  gm_world_block(r.world,13,78,8)==124 &&
                  gm_world_meta(r.world,16,78,8)==0 &&
                  gm_world_block(r.world,17,78,8)==123 &&
                  r.entities.ents[0].age==1 &&
                  r.entities.ents[0].pickup_delay==32767 &&
                  gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                  pending.block==72&&pending.x==12 &&
                  pending.time==item_base+21,
                  "item activates wooden plate at exact +21 while stone excludes it");
        }
        for(int tick=0;tick<19;++tick)
            gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==1,
              "item-occupied wooden callback remains pending through row 19");
        gm_runtime_tick(&r,idle);
        {
            GmRuntimeScheduledTick pending;
            CHECK(gm_world_meta(r.world,12,78,8)==1 &&
                  gm_world_block(r.world,13,78,8)==124 &&
                  gm_world_meta(r.world,16,78,8)==0 &&
                  gm_world_block(r.world,17,78,8)==123 &&
                  gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                  pending.block==72&&pending.time==item_base+41,
                  "item-occupied wooden callback retains power and reschedules exact +20");
        }
    }
    gm_runtime_destroy(&r);

    {
        GmConfig weighted_plate_cfg=cfg;
        weighted_plate_cfg.mobs=0;
        CHECK(gm_runtime_init(&r,&weighted_plate_cfg,err,sizeof err),
              "weighted pressure-plate runtime initializes");
    }
    if(r.world){
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
        for(int y=76;y<=83;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=25;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int z=6;z<=10;++z)
            for(int x=10;x<=25;++x)
                gm_world_set_block_meta(r.world,x,77,z,1,0);
        /* Gold: player + one item -> strength 2. */
        gm_world_set_block_meta(r.world,12,78,8,147,0);
        gm_world_set_block_meta(r.world,13,78,8,55,0);
        gm_world_set_block_meta(r.world,14,78,8,123,0);
        /* Iron: two item entities -> strength 1. */
        gm_world_set_block_meta(r.world,17,78,8,148,0);
        gm_world_set_block_meta(r.world,18,78,8,55,0);
        gm_world_set_block_meta(r.world,19,78,8,123,0);
        /* A 64-item stack is still one Entity -> gold strength 1. */
        gm_world_set_block_meta(r.world,22,78,8,147,0);
        gm_world_set_block_meta(r.world,23,78,8,55,0);
        gm_world_set_block_meta(r.world,24,78,8,123,0);
        gm_runtime_set_pose_state(
            &r,12.5,78.0,8.5,0.0f,0.0f,0.0,0.0,0.0,1,0.0f);
        CHECK(gm_runtime_spawn_item_fixture(
                  &r,4030,12.5,78.0,8.5,0.0,0.0,0.0,
                  1,1,0,0,32767,1),
              "spawn gold-plate item beside player");
        CHECK(gm_runtime_spawn_item_fixture(
                  &r,4031,17.5,78.0,8.5,0.0,0.0,0.0,
                  1,1,0,0,32767,1) &&
              gm_runtime_spawn_item_fixture(
                  &r,4032,17.5,78.0,8.5,0.0,0.0,0.0,
                  1,1,0,0,32767,1),
              "spawn two iron-plate item entities");
        CHECK(gm_runtime_spawn_item_fixture(
                  &r,4033,22.5,78.0,8.5,0.0,0.0,0.0,
                  1,64,0,0,32767,1),
              "spawn one 64-stack gold-plate entity");
        {
            McAABB iron_trigger=mc_aabb_make(
                17.125,78.0,8.125,17.875,78.25,8.875);
            McAABB stack_trigger=mc_aabb_make(
                22.125,78.0,8.125,22.875,78.25,8.875);
            CHECK(gm_live_items_count_intersects_aabb(
                      &r.entities,&iron_trigger)==2 &&
                  gm_live_items_count_intersects_aabb(
                      &r.entities,&stack_trigger)==1,
                  "weighted count is per EntityItem, not per stack item");
        }
        long long weighted_base=r.clock.total_time;
        gm_runtime_tick(&r,idle);
        CHECK(gm_world_meta(r.world,12,78,8)==2 &&
              gm_world_meta(r.world,13,78,8)==2 &&
              gm_world_block(r.world,14,78,8)==124 &&
              gm_world_meta(r.world,17,78,8)==1 &&
              gm_world_meta(r.world,18,78,8)==1 &&
              gm_world_block(r.world,19,78,8)==124 &&
              gm_world_meta(r.world,22,78,8)==1 &&
              gm_world_meta(r.world,23,78,8)==1 &&
              gm_world_block(r.world,24,78,8)==124,
              "weighted plates emit exact entity-count analog strength");
        CHECK(gm_runtime_scheduled_tick_count(&r)==3,
              "three weighted plates schedule independent callbacks");
        for(int i=0;i<3;++i){
            GmRuntimeScheduledTick pending;
            CHECK(gm_runtime_scheduled_tick_get(&r,i,&pending) &&
                  (pending.block==147||pending.block==148) &&
                  pending.time==weighted_base+11,
                  "weighted activation schedules exact Java +11");
        }
        for(int tick=0;tick<9;++tick)
            gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==3,
              "weighted callbacks remain pending through row 9");
        gm_runtime_tick(&r,idle);
        CHECK(gm_world_meta(r.world,12,78,8)==2 &&
              gm_world_meta(r.world,17,78,8)==1 &&
              gm_world_meta(r.world,22,78,8)==1 &&
              gm_runtime_scheduled_tick_count(&r)==3,
              "occupied weighted callbacks retain analog strength");
        for(int i=0;i<3;++i){
            GmRuntimeScheduledTick pending;
            CHECK(gm_runtime_scheduled_tick_get(&r,i,&pending) &&
                  pending.time==weighted_base+21,
                  "weighted callback recurs at exact +10");
        }
        gm_runtime_set_pose_state(
            &r,8.5,78.0,8.5,0.0f,0.0f,0.0,0.0,0.0,1,0.0f);
        for(int i=0;i<GM_LIVE_MAX;++i)
            r.entities.ents[i].active=0;
        r.entities.n_active=0;
        for(int tick=0;tick<9;++tick)
            gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==3,
              "vacated weighted callbacks remain pending through row 19");
        gm_runtime_tick(&r,idle);
        CHECK(gm_world_meta(r.world,12,78,8)==0 &&
              gm_world_meta(r.world,13,78,8)==0 &&
              gm_world_block(r.world,14,78,8)==124 &&
              gm_world_meta(r.world,17,78,8)==0 &&
              gm_world_meta(r.world,18,78,8)==0 &&
              gm_world_block(r.world,19,78,8)==124 &&
              gm_world_meta(r.world,22,78,8)==0 &&
              gm_world_meta(r.world,23,78,8)==0 &&
              gm_world_block(r.world,24,78,8)==124 &&
              gm_runtime_scheduled_tick_count(&r)==3,
              "vacated weighted callbacks drain plate and dust before lamps");
        for(int i=0;i<3;++i){
            GmRuntimeScheduledTick pending;
            CHECK(gm_runtime_scheduled_tick_get(&r,i,&pending) &&
                  pending.block==124 &&
                  pending.time==weighted_base+25,
                  "weighted release hands lamps exact independent +4");
        }
        for(int tick=0;tick<4;++tick)
            gm_runtime_tick(&r,idle);
        CHECK(gm_world_block(r.world,14,78,8)==123 &&
              gm_world_block(r.world,19,78,8)==123 &&
              gm_world_block(r.world,24,78,8)==123 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "weighted-release lamps drain at exact +4");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "disabled-fire scheduled runtime initializes");
    if(r.world){
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
        for(int y=76;y<=83;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int z=6;z<=10;++z)
            for(int x=10;x<=14;++x)
                gm_world_set_block_meta(r.world,x,77,z,1,0);
        gm_world_set_block_meta(r.world,12,78,8,51,0);
        gm_world_set_block_meta(r.world,13,78,8,5,0);
        gm_runtime_set_total_time(&r,42);
        CHECK(r.do_fire_tick==1 &&
              gm_runtime_set_do_fire_tick(&r,0) &&
              !gm_runtime_set_do_fire_tick(&r,2) &&
              r.do_fire_tick==0,
              "doFireTick defaults true and accepts only boolean state");
        CHECK(gm_runtime_schedule_tick(&r,12,78,8,51,45,0,0),
              "disabled fire still restores its pending callback");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_world_block(r.world,12,78,8)==51 &&
              gm_world_block(r.world,13,78,8)==5,
              "disabled fire remains pending through total time 44");
        CHECK(gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x5DEECE649)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x123456789ABC)) &&
              gm_runtime_set_block_random_seed48(
                  &r,UINT64_C(0x0ABCDEF12345)) &&
              gm_runtime_set_world_update_lcg(&r,1094913777) &&
              gm_runtime_set_entity_id_cursor(&r,7000),
              "disabled-fire due boundary restores every exact cursor");
        gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==0 &&
              gm_world_block(r.world,12,78,8)==51 &&
              gm_world_meta(r.world,12,78,8)==0 &&
              gm_world_block(r.world,13,78,8)==5 &&
              gm_world_meta(r.world,13,78,8)==0,
              "due disabled-fire callback drains without block mutation");
        CHECK(r.world_random_seed48==UINT64_C(0x5DEECE649) &&
              r.math_random_seed48==UINT64_C(0x123456789ABC) &&
              r.block_random_seed48==UINT64_C(0x0ABCDEF12345) &&
              r.world_update_lcg==1094913777 &&
              r.next_entity_id==7000,
              "disabled-fire callback consumes no RNG or entity identity");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "fire scheduled/random-tick runtime initializes");
    if(r.world){
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
        for(int y=76;y<=83;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int z=6;z<=10;++z)
            for(int x=10;x<=14;++x)
                gm_world_set_block_meta(r.world,x,77,z,1,0);
        gm_world_set_block_meta(r.world,12,78,8,51,0);
        gm_world_set_block_meta(r.world,13,78,8,5,0);
        CHECK(gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x5DEECE649)),
              "Random(seed=36) fire cursor restores exactly");
        CHECK(gm_runtime_random_tick_block(&r,12,78,8,51),
              "controlled dry fire callback is represented");
        CHECK(gm_world_block(r.world,12,78,8)==51 &&
              gm_world_meta(r.world,12,78,8)==0 &&
              gm_world_block(r.world,13,78,8)==51 &&
              gm_world_meta(r.world,13,78,8)==0,
              "seed 36 changes only the east plank to age-zero fire");
        CHECK(r.world_random_seed48==UINT64_C(0x8EBD372F3662),
              "fire callback consumes Java's exact eleven-draw sequence");
        {
            GmRuntimeScheduledTick source,east;
            CHECK(gm_runtime_scheduled_tick_count(&r)==2 &&
                  gm_runtime_scheduled_tick_get(&r,0,&source) &&
                  gm_runtime_scheduled_tick_get(&r,1,&east) &&
                  source.block==51&&source.x==12&&source.y==78&&
                  source.z==8&&source.time==35&&source.order==0 &&
                  east.block==51&&east.x==13&&east.y==78&&
                  east.z==8&&east.time==35&&east.order==1,
                  "source and child fire schedules preserve due time/order");
        }
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "wool fire-material runtime initializes");
    if(r.world){
        for(int y=76;y<=83;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int z=6;z<=10;++z)
            for(int x=10;x<=14;++x)
                gm_world_set_block_meta(r.world,x,77,z,1,0);
        gm_world_set_block_meta(r.world,12,78,8,51,0);
        gm_world_set_block_meta(r.world,13,78,8,35,0);
        CHECK(gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x5DEECE649)) &&
              gm_runtime_random_tick_block(&r,12,78,8,51),
              "Random(seed=36) admits wool fire-table callback");
        CHECK(gm_world_meta(r.world,12,78,8)==0 &&
              gm_world_block(r.world,13,78,8)==51 &&
              gm_world_meta(r.world,13,78,8)==0 &&
              r.world_random_seed48==UINT64_C(0x8EBD372F3662),
              "wool flammability 60 produces exact age-zero fire/cursor");
        {
            GmRuntimeScheduledTick source,east;
            CHECK(gm_runtime_scheduled_tick_count(&r)==2 &&
                  gm_runtime_scheduled_tick_get(&r,0,&source) &&
                  gm_runtime_scheduled_tick_get(&r,1,&east) &&
                  source.block==51&&source.x==12&&source.y==78&&
                  source.z==8&&source.time==35&&source.order==0 &&
                  east.block==51&&east.x==13&&east.y==78&&
                  east.z==8&&east.time==35&&east.order==1,
                  "wool target creates exact source/child fire queue");
        }
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "log fire-material runtime initializes");
    if(r.world){
        for(int y=76;y<=83;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int z=6;z<=10;++z)
            for(int x=10;x<=14;++x)
                gm_world_set_block_meta(r.world,x,77,z,1,0);
        gm_world_set_block_meta(r.world,12,78,8,51,0);
        gm_world_set_block_meta(r.world,13,78,8,17,0);
        CHECK(gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x5DEECE654)) &&
              gm_runtime_random_tick_block(&r,12,78,8,51),
              "Random(seed=57) admits log fire-table callback");
        CHECK(gm_world_meta(r.world,12,78,8)==0 &&
              gm_world_block(r.world,13,78,8)==51 &&
              gm_world_meta(r.world,13,78,8)==0 &&
              r.world_random_seed48==UINT64_C(0x27DB2C1FBC09),
              "log flammability 5 produces exact age-zero fire/cursor");
        {
            GmRuntimeScheduledTick source,east;
            CHECK(gm_runtime_scheduled_tick_count(&r)==2 &&
                  gm_runtime_scheduled_tick_get(&r,0,&source) &&
                  gm_runtime_scheduled_tick_get(&r,1,&east) &&
                  source.block==51&&source.x==12&&source.y==78&&
                  source.z==8&&source.time==31&&source.order==0 &&
                  east.block==51&&east.x==13&&east.y==78&&
                  east.z==8&&east.time==38&&east.order==1,
                  "log target creates exact source/child fire queue");
        }
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "tall-grass fire-material runtime initializes");
    if(r.world){
        for(int y=76;y<=83;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int z=6;z<=10;++z)
            for(int x=10;x<=14;++x)
                gm_world_set_block_meta(r.world,x,77,z,1,0);
        gm_world_set_block_meta(r.world,12,78,8,51,0);
        gm_world_set_block_meta(r.world,13,78,8,31,1);
        CHECK(gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x5DEECE669)) &&
              gm_runtime_random_tick_block(&r,12,78,8,51),
              "Random(seed=4) admits tall-grass fire-table callback");
        CHECK(gm_world_meta(r.world,12,78,8)==1 &&
              gm_world_block(r.world,13,78,8)==0 &&
              r.world_random_seed48==UINT64_C(0x1411389CAF08),
              "dry tall grass burns to air after exact nine-draw cursor");
        {
            GmRuntimeScheduledTick source;
            CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&source) &&
                  source.block==51&&source.x==12&&source.y==78&&
                  source.z==8&&source.time==32&&source.order==0,
                  "tall-grass target leaves only exact source fire queue");
        }
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "bookshelf direct-fire runtime initializes");
    if(r.world){
        for(int y=76;y<=83;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int z=6;z<=10;++z)
            for(int x=10;x<=14;++x)
                gm_world_set_block_meta(r.world,x,77,z,1,0);
        gm_world_set_block_meta(r.world,12,78,8,51,0);
        gm_world_set_block_meta(r.world,13,78,8,47,0);
        CHECK(gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x5DEECE649)) &&
              gm_runtime_random_tick_block(&r,12,78,8,51),
              "Random(seed=36) admits direct bookshelf callback");
        CHECK(gm_world_meta(r.world,12,78,8)==0 &&
              gm_world_block(r.world,13,78,8)==51 &&
              gm_world_meta(r.world,13,78,8)==0 &&
              r.world_random_seed48==UINT64_C(0x8EBD372F3662),
              "bookshelf flammability 20 produces exact direct fire/cursor");
        {
            GmRuntimeScheduledTick source,east;
            CHECK(gm_runtime_scheduled_tick_count(&r)==2 &&
                  gm_runtime_scheduled_tick_get(&r,0,&source) &&
                  gm_runtime_scheduled_tick_get(&r,1,&east) &&
                  source.block==51&&source.x==12&&source.y==78&&
                  source.z==8&&source.time==35&&source.order==0 &&
                  east.block==51&&east.x==13&&east.y==78&&
                  east.z==8&&east.time==35&&east.order==1,
                  "bookshelf direct target creates exact fire queue");
        }
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "bookshelf volume-fire runtime initializes");
    if(r.world){
        for(int y=76;y<=83;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,77,8,87,0);
        gm_world_set_block_meta(r.world,12,78,8,51,0);
        gm_world_set_block_meta(r.world,14,79,8,47,0);
        CHECK(gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x5DEECE76A)) &&
              gm_runtime_random_tick_block(&r,12,78,8,51),
              "Random(seed=263) admits bookshelf encouragement callback");
        CHECK(gm_world_block(r.world,12,78,8)==51 &&
              gm_world_meta(r.world,12,78,8)==0 &&
              gm_world_block(r.world,13,79,8)==51 &&
              gm_world_meta(r.world,13,79,8)==0 &&
              gm_world_block(r.world,14,79,8)==47 &&
              r.world_random_seed48==UINT64_C(0x16FECEC71C57),
              "bookshelf encouragement 30 admits exact roll-two volume fire");
        {
            GmRuntimeScheduledTick source,child;
            CHECK(gm_runtime_scheduled_tick_count(&r)==2 &&
                  gm_runtime_scheduled_tick_get(&r,0,&source) &&
                  gm_runtime_scheduled_tick_get(&r,1,&child) &&
                  source.block==51&&source.x==12&&source.y==78&&
                  source.z==8&&source.time==33&&source.order==0 &&
                  child.block==51&&child.x==13&&child.y==79&&
                  child.z==8&&child.time==35&&child.order==1,
                  "bookshelf volume target creates exact source/child queue");
        }
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "hay direct-fire runtime initializes");
    if(r.world){
        for(int y=76;y<=83;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int z=6;z<=10;++z)
            for(int x=10;x<=14;++x)
                gm_world_set_block_meta(r.world,x,77,z,1,0);
        gm_world_set_block_meta(r.world,12,78,8,51,0);
        gm_world_set_block_meta(r.world,13,78,8,170,0);
        CHECK(gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x5DEECE649)) &&
              gm_runtime_random_tick_block(&r,12,78,8,51),
              "Random(seed=36) admits direct hay callback");
        CHECK(gm_world_meta(r.world,12,78,8)==0 &&
              gm_world_block(r.world,13,78,8)==51 &&
              gm_world_meta(r.world,13,78,8)==0 &&
              r.world_random_seed48==UINT64_C(0x8EBD372F3662),
              "hay flammability 20 produces exact direct fire/cursor");
        {
            GmRuntimeScheduledTick source,east;
            CHECK(gm_runtime_scheduled_tick_count(&r)==2 &&
                  gm_runtime_scheduled_tick_get(&r,0,&source) &&
                  gm_runtime_scheduled_tick_get(&r,1,&east) &&
                  source.block==51&&source.x==12&&source.y==78&&
                  source.z==8&&source.time==35&&source.order==0 &&
                  east.block==51&&east.x==13&&east.y==78&&
                  east.z==8&&east.time==35&&east.order==1,
                  "hay direct target creates exact fire queue");
        }
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "hay volume-fire runtime initializes");
    if(r.world){
        for(int y=76;y<=83;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,77,8,87,0);
        gm_world_set_block_meta(r.world,12,78,8,51,0);
        gm_world_set_block_meta(r.world,14,79,8,170,0);
        CHECK(gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x5DEECE7EA)) &&
              gm_runtime_random_tick_block(&r,12,78,8,51),
              "Random(seed=391) admits hay encouragement callback");
        CHECK(gm_world_block(r.world,12,78,8)==51 &&
              gm_world_meta(r.world,12,78,8)==0 &&
              gm_world_block(r.world,13,79,8)==51 &&
              gm_world_meta(r.world,13,79,8)==0 &&
              gm_world_block(r.world,14,79,8)==170 &&
              r.world_random_seed48==UINT64_C(0xF572AB2A46D7),
              "hay encouragement 60 admits exact roll-three volume fire");
        {
            GmRuntimeScheduledTick child,source;
            CHECK(gm_runtime_scheduled_tick_count(&r)==2 &&
                  gm_runtime_scheduled_tick_get(&r,0,&child) &&
                  gm_runtime_scheduled_tick_get(&r,1,&source) &&
                  child.block==51&&child.x==13&&child.y==79&&
                  child.z==8&&child.time==31&&child.order==1 &&
                  source.block==51&&source.x==12&&source.y==78&&
                  source.z==8&&source.time==34&&source.order==0,
                  "hay volume target creates exact due-time-sorted queue");
        }
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "fire-to-TNT callback runtime initializes");
    if(r.world){
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
        for(int y=76;y<=83;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int z=6;z<=10;++z)
            for(int x=10;x<=14;++x)
                gm_world_set_block_meta(r.world,x,77,z,1,0);
        gm_world_set_block_meta(r.world,12,78,8,51,0);
        gm_world_set_block_meta(r.world,13,78,8,46,0);
        CHECK(gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x5DEECE669)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x123456789ABC)) &&
              gm_runtime_set_entity_id_cursor(&r,7100),
              "fire/TNT callback restores World, Math, and entity cursors");
        for(int index=0;index<GM_RUNTIME_PRIMED_TNT;++index)
            r.primed_tnt[index].active=1;
        r.primed_tnt_count=GM_RUNTIME_PRIMED_TNT;
        CHECK(!gm_runtime_random_tick_block(&r,12,78,8,51) &&
              gm_world_meta(r.world,12,78,8)==0 &&
              gm_world_block(r.world,13,78,8)==46 &&
              r.world_random_seed48==UINT64_C(0x5DEECE669) &&
              r.math_random_seed48==UINT64_C(0x123456789ABC) &&
              r.next_entity_id==7100,
              "full fixed TNT pool rejects fire callback atomically");
        memset(r.primed_tnt,0,sizeof r.primed_tnt);
        r.primed_tnt_count=0;
        CHECK(gm_runtime_random_tick_block(&r,12,78,8,51),
              "controlled fire callback admits adjacent TNT");
        CHECK(gm_world_block(r.world,12,78,8)==51 &&
              gm_world_meta(r.world,12,78,8)==1 &&
              gm_world_block(r.world,13,78,8)==0,
              "Random(4) ages source once and burns east TNT to air");
        CHECK(r.world_random_seed48==UINT64_C(0x1411389CAF08) &&
              r.math_random_seed48==UINT64_C(0x689EF830B5D6) &&
              r.next_entity_id==7101,
              "fire/TNT callback consumes exact World/Math/identity cursors");
        CHECK(r.primed_tnt_count==1 && r.primed_tnt[0].active &&
              r.primed_tnt[0].eid==7100 &&
              r.primed_tnt[0].fuse==80 &&
              r.primed_tnt[0].x==13.5 &&
              r.primed_tnt[0].y==78.0 &&
              r.primed_tnt[0].z==8.5 &&
              r.primed_tnt[0].vy==0.20000000298023224,
              "fire invokes TNT EXPLODE destruction after replacement");
        gm_runtime_tick(&r,idle);
        CHECK(r.primed_tnt_count==1 && r.primed_tnt[0].active &&
              r.primed_tnt[0].fuse==79 &&
              r.primed_tnt[0].y==78.1600000038743,
              "new fire-primed TNT receives its same-boundary entity tick");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "normal-humidity chance control initializes");
    if(r.world){
        for(int y=76;y<=83;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int z=6;z<=10;++z)
            for(int x=10;x<=14;++x)
                gm_world_set_block_meta(r.world,x,77,z,1,0);
        gm_world_set_block_meta(r.world,12,78,8,51,0);
        gm_world_set_block_meta(r.world,13,78,8,46,0);
        CHECK(gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x5DEECE66D)) &&
              gm_runtime_random_tick_block(&r,12,78,8,51),
              "normal fire executes Random(0) chance control");
        CHECK(gm_world_meta(r.world,12,78,8)==0 &&
              gm_world_block(r.world,13,78,8)==46 &&
              r.primed_tnt_count==0 &&
              r.world_random_seed48==UINT64_C(0x554DCD2B1A56),
              "normal nextInt(300)=229 misses TNT and consumes volume draws");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "high-humidity fire chance runtime initializes");
    if(r.world){
        for(int y=76;y<=83;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int z=6;z<=10;++z)
            for(int x=10;x<=14;++x)
                gm_world_set_block_meta(r.world,x,77,z,1,0);
        gm_world_set_block_meta(r.world,12,78,8,51,0);
        gm_world_set_block_meta(r.world,13,78,8,46,0);
        CHECK(gm_runtime_set_fire_humidity_context(&r,12,78,8) &&
              gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x5DEECE66D)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x123456789ABC)) &&
              gm_runtime_set_entity_id_cursor(&r,7200) &&
              gm_runtime_random_tick_block(&r,12,78,8,51),
              "humid fire restores context and executes Random(0)");
        CHECK(gm_world_meta(r.world,12,78,8)==0 &&
              gm_world_block(r.world,13,78,8)==0 &&
              r.world_random_seed48==UINT64_C(0x98F8BA3E4B3C) &&
              r.math_random_seed48==UINT64_C(0x689EF830B5D6) &&
              r.next_entity_id==7201 &&
              r.primed_tnt_count==1 && r.primed_tnt[0].active &&
              r.primed_tnt[0].eid==7200 && r.primed_tnt[0].fuse==80,
              "humid nextInt(250)=29 ignites TNT with exact cursors");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "normal humidity spread-threshold runtime initializes");
    if(r.world){
        for(int y=76;y<=83;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int z=6;z<=10;++z)
            for(int x=10;x<=14;++x)
                gm_world_set_block_meta(r.world,x,77,z,1,0);
        gm_world_set_block_meta(r.world,12,78,8,51,0);
        gm_world_set_block_meta(r.world,13,78,8,46,0);
        CHECK(gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x5DEECE565)) &&
              gm_runtime_random_tick_block(&r,12,78,8,51),
              "normal fire executes Random(776) spread threshold");
        CHECK(gm_world_meta(r.world,12,78,8)==0 &&
              gm_world_block(r.world,13,78,8)==46 &&
              gm_world_block(r.world,13,78,7)==51 &&
              gm_world_meta(r.world,13,78,7)==1 &&
              r.primed_tnt_count==0 &&
              r.world_random_seed48==UINT64_C(0x1D022632DC18),
              "normal threshold two admits roll two at first candidate");
        {
            GmRuntimeScheduledTick source,child;
            CHECK(gm_runtime_scheduled_tick_count(&r)==2 &&
                  gm_runtime_scheduled_tick_get(&r,0,&source) &&
                  gm_runtime_scheduled_tick_get(&r,1,&child) &&
                  source.block==51&&source.x==12&&source.y==78&&
                  source.z==8&&source.time==36&&source.order==0 &&
                  child.block==51&&child.x==13&&child.y==78&&
                  child.z==7&&child.time==39&&child.order==1,
                  "normal spread creates the exact source/child queue");
        }
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "humid spread-threshold runtime initializes");
    if(r.world){
        for(int y=76;y<=83;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int z=6;z<=10;++z)
            for(int x=10;x<=14;++x)
                gm_world_set_block_meta(r.world,x,77,z,1,0);
        gm_world_set_block_meta(r.world,12,78,8,51,0);
        gm_world_set_block_meta(r.world,13,78,8,46,0);
        CHECK(gm_runtime_set_fire_humidity_context(&r,12,78,8) &&
              gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x5DEECE565)) &&
              gm_runtime_random_tick_block(&r,12,78,8,51),
              "humid fire executes Random(776) spread threshold");
        CHECK(gm_world_meta(r.world,12,78,8)==0 &&
              gm_world_block(r.world,13,78,8)==46 &&
              gm_world_block(r.world,13,78,7)==0 &&
              r.primed_tnt_count==0 &&
              r.world_random_seed48==UINT64_C(0xFA7ED69E92AE),
              "humid threshold one rejects roll two at first candidate");
        {
            GmRuntimeScheduledTick source;
            CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&source) &&
                  source.block==51&&source.x==12&&source.y==78&&
                  source.z==8&&source.time==36&&source.order==0,
                  "humid spread retains only the source schedule");
        }
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "fire delayed-dispatch runtime initializes");
    if(r.world){
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
        for(int y=76;y<=83;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int z=6;z<=10;++z)
            for(int x=10;x<=14;++x)
                gm_world_set_block_meta(r.world,x,77,z,1,0);
        gm_world_set_block_meta(r.world,12,78,8,51,0);
        gm_world_set_block_meta(r.world,13,78,8,5,0);
        gm_runtime_set_total_time(&r,42);
        CHECK(gm_runtime_schedule_tick(&r,12,78,8,51,45,0,0),
              "proof-safe fire enters the pending queue");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_world_block(r.world,13,78,8)==5,
              "fire remains pending through total time 44");
        CHECK(gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x5DEECE649)),
              "scheduled callback RNG resets at its due boundary");
        gm_runtime_tick(&r,idle);
        CHECK(gm_world_block(r.world,13,78,8)==51 &&
              gm_world_meta(r.world,13,78,8)==0,
              "due scheduled fire burns the exact east plank");
        {
            GmRuntimeScheduledTick source,east;
            CHECK(gm_runtime_scheduled_tick_count(&r)==2 &&
                  gm_runtime_scheduled_tick_get(&r,0,&source) &&
                  gm_runtime_scheduled_tick_get(&r,1,&east) &&
                  source.time==80&&source.order==1 &&
                  east.time==80&&east.order==2,
                  "scheduled fire dispatch replaces itself with ordered +35 work");
        }
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "netherrack fire-source runtime initializes");
    if(r.world){
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
        for(int y=76;y<=83;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int z=6;z<=10;++z)
            for(int x=10;x<=14;++x)
                gm_world_set_block_meta(r.world,x,77,z,1,0);
        gm_world_set_block_meta(r.world,12,77,8,87,0);
        gm_world_set_block_meta(r.world,12,78,8,51,15);
        gm_runtime_set_total_time(&r,42);
        CHECK(gm_runtime_schedule_tick(&r,12,78,8,51,45,0,0),
              "age-15 fire on netherrack restores its pending callback");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_world_block(r.world,12,78,8)==51 &&
              gm_world_meta(r.world,12,78,8)==15,
              "netherrack source fire remains pending through total time 44");
        CHECK(gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x5DEECE649)),
              "netherrack fire resets Random(seed=36) at the due boundary");
        gm_runtime_tick(&r,idle);
        {
            GmRuntimeScheduledTick source;
            CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&source) &&
                  source.block==51&&source.x==12&&source.y==78&&
                  source.z==8&&source.time==84&&source.order==1 &&
                  gm_world_block(r.world,12,77,8)==87 &&
                  gm_world_block(r.world,12,78,8)==51 &&
                  gm_world_meta(r.world,12,78,8)==15,
                  "netherrack keeps age-15 fire and its exact +39 successor");
        }
        CHECK(r.world_random_seed48==UINT64_C(0xB6679B27AF7E),
              "netherrack source callback consumes exactly seven RNG draws");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err) &&
          gm_runtime_set_dimension(&r,-1),
          "Nether netherrack fire-source runtime initializes");
    if(r.world){
        for(int y=76;y<=83;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int z=6;z<=10;++z)
            for(int x=10;x<=14;++x)
                gm_world_set_block_meta(r.world,x,77,z,1,0);
        gm_world_set_block_meta(r.world,12,77,8,87,0);
        gm_world_set_block_meta(r.world,12,78,8,51,15);
        CHECK(gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x5DEECE649)) &&
              gm_runtime_random_tick_block(&r,12,78,8,51),
              "Nether netherrack admits the controlled fire callback");
        {
            GmRuntimeScheduledTick source;
            CHECK(r.dimension==-1 &&
                  gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&source) &&
                  source.block==51&&source.x==12&&source.y==78&&
                  source.z==8&&source.time==39&&source.order==0 &&
                  gm_world_block(r.world,12,77,8)==87 &&
                  gm_world_block(r.world,12,78,8)==51 &&
                  gm_world_meta(r.world,12,78,8)==15,
                  "Nether netherrack keeps fire and its exact successor");
        }
        CHECK(r.world_random_seed48==UINT64_C(0xB6679B27AF7E),
              "Nether source callback consumes exactly seven RNG draws");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err) &&
          gm_runtime_set_dimension(&r,1),
          "End bedrock fire-source runtime initializes");
    if(r.world){
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
        for(int y=76;y<=83;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int z=6;z<=10;++z)
            for(int x=10;x<=14;++x)
                gm_world_set_block_meta(r.world,x,77,z,1,0);
        gm_world_set_block_meta(r.world,12,77,8,7,0);
        gm_world_set_block_meta(r.world,12,78,8,51,15);
        gm_runtime_set_total_time(&r,42);
        CHECK(gm_runtime_schedule_tick(&r,12,78,8,51,45,0,0),
              "age-15 fire on End bedrock restores its pending callback");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_world_block(r.world,12,78,8)==51 &&
              gm_world_meta(r.world,12,78,8)==15,
              "End bedrock source fire remains pending through total time 44");
        CHECK(gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x5DEECE649)),
              "End bedrock fire resets Random(seed=36) at the due boundary");
        gm_runtime_tick(&r,idle);
        {
            GmRuntimeScheduledTick source;
            CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&source) &&
                  source.block==51&&source.x==12&&source.y==78&&
                  source.z==8&&source.time==84&&source.order==1 &&
                  gm_world_block(r.world,12,77,8)==7 &&
                  gm_world_block(r.world,12,78,8)==51 &&
                  gm_world_meta(r.world,12,78,8)==15,
                  "End bedrock keeps age-15 fire and its exact +39 successor");
        }
        CHECK(r.world_random_seed48==UINT64_C(0xB6679B27AF7E),
              "End bedrock source callback consumes exactly seven RNG draws");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "rain-extinguish fire runtime initializes");
    if(r.world){
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
        for(int y=76;y<=83;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int z=6;z<=10;++z)
            for(int x=10;x<=14;++x)
                gm_world_set_block_meta(r.world,x,77,z,1,0);
        gm_world_set_block_meta(r.world,12,78,8,51,15);
        gm_runtime_set_total_time(&r,42);
        gm_runtime_set_weather(&r,1,0,1000000,1000000);
        CHECK(!gm_runtime_schedule_tick(&r,12,78,8,51,45,0,0),
              "rain fire rejects a queue lacking captured canDie context");
        CHECK(gm_runtime_set_fire_rain_context(&r,12,78,8,1,1,1) &&
              gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x5DEECE26D)) &&
              gm_runtime_schedule_tick(&r,12,78,8,51,45,0,0),
              "exposed age-15 rain fire restores context, RNG, and queue");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_world_block(r.world,12,78,8)==51 &&
              gm_world_meta(r.world,12,78,8)==15 &&
              r.world_random_seed48==UINT64_C(0x5DEECE26D) &&
              r.clock.raining==1 && r.clock.thundering==0 &&
              r.clock.rain_time==999998,
              "rain fire remains pending without consuming its callback RNG");
        gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==0 &&
              gm_world_block(r.world,12,78,8)==0 &&
              r.world_random_seed48==UINT64_C(0xA3A500C65674) &&
              r.clock.raining==1 && r.clock.thundering==0 &&
              r.clock.rain_time==999997,
              "one exact rain float extinguishes fire before rescheduling");
        gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==0 &&
              gm_world_block(r.world,12,78,8)==0 &&
              r.world_random_seed48==UINT64_C(0xA3A500C65674),
              "rain-extinguished fire leaves no stale successor callback");
        gm_world_set_block_meta(r.world,12,78,8,51,15);
        gm_runtime_set_weather(&r,1,1,1000000,1000000);
        CHECK(gm_runtime_set_fire_rain_context(&r,12,78,8,1,1,1) &&
              gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x5DEECE26D)) &&
              gm_runtime_random_tick_block(&r,12,78,8,51) &&
              gm_world_block(r.world,12,78,8)==0 &&
              gm_runtime_scheduled_tick_count(&r)==0 &&
              r.world_random_seed48==UINT64_C(0xA3A500C65674) &&
              r.clock.raining==1 && r.clock.thundering==1,
              "thunder uses the same exact one-float fire extinguish path");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "failed-rain-roll fire runtime initializes");
    if(r.world){
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
        for(int y=76;y<=83;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int z=6;z<=10;++z)
            for(int x=10;x<=14;++x)
                gm_world_set_block_meta(r.world,x,77,z,1,0);
        gm_world_set_block_meta(r.world,12,78,8,51,15);
        gm_runtime_set_total_time(&r,42);
        gm_runtime_set_weather(&r,1,0,1000000,1000000);
        CHECK(gm_runtime_set_fire_rain_context(&r,12,78,8,1,1,1) &&
              gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x5DEECE66D)) &&
              gm_runtime_schedule_tick(&r,12,78,8,51,45,0,0),
              "failed rain roll restores Random(seed=0) and pending fire");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        {
            GmRuntimeScheduledTick stale;
            CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&stale) &&
                  stale.block==51&&stale.x==12&&stale.y==78&&
                  stale.z==8&&stale.time==83&&stale.order==1 &&
                  gm_world_block(r.world,12,78,8)==0,
                  "failed rain roll reschedules before isolated burnout");
        }
        CHECK(r.world_random_seed48==UINT64_C(0xD4D95138AB6F),
              "failed rain roll consumes one float and one schedule draw");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "rain-exposed direct fire target runtime initializes");
    if(r.world){
        for(int y=76;y<=83;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int z=6;z<=10;++z)
            for(int x=10;x<=14;++x)
                gm_world_set_block_meta(r.world,x,77,z,1,0);
        gm_world_set_block_meta(r.world,12,77,8,87,0);
        gm_world_set_block_meta(r.world,12,78,8,51,0);
        gm_world_set_block_meta(r.world,13,78,8,31,1);
        gm_runtime_set_weather(&r,1,0,1000000,1000000);
        CHECK(gm_runtime_set_fire_rain_context(&r,12,78,8,1,1,1) &&
              gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x5DEECE668)) &&
              gm_runtime_random_tick_block(&r,12,78,8,51),
              "exposed tall-grass fire target executes Random(5)");
        CHECK(gm_world_block(r.world,12,78,8)==51 &&
              gm_world_meta(r.world,12,78,8)==1 &&
              gm_world_block(r.world,13,78,8)==0 &&
              r.world_random_seed48==UINT64_C(0x72D7583447FB),
              "rain-exposed target burns to air without age draws");
        {
            GmRuntimeScheduledTick source;
            CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&source) &&
                  source.block==51&&source.x==12&&source.y==78&&
                  source.z==8&&source.time==32&&source.order==0,
                  "wet direct target leaves only the source schedule");
        }
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "rain-covered direct fire target runtime initializes");
    if(r.world){
        for(int y=76;y<=83;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int z=6;z<=10;++z)
            for(int x=10;x<=14;++x)
                gm_world_set_block_meta(r.world,x,77,z,1,0);
        gm_world_set_block_meta(r.world,12,77,8,87,0);
        gm_world_set_block_meta(r.world,12,78,8,51,0);
        gm_world_set_block_meta(r.world,13,78,8,31,1);
        gm_world_set_block_meta(r.world,13,80,8,1,0);
        gm_runtime_set_weather(&r,1,0,1000000,1000000);
        CHECK(gm_runtime_set_fire_rain_context(&r,12,78,8,1,0,1) &&
              gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x5DEECE668)) &&
              gm_runtime_random_tick_block(&r,12,78,8,51),
              "covered tall-grass fire target executes Random(5)");
        CHECK(gm_world_block(r.world,12,78,8)==51 &&
              gm_world_meta(r.world,12,78,8)==1 &&
              gm_world_block(r.world,13,78,8)==51 &&
              gm_world_meta(r.world,13,78,8)==0 &&
              r.world_random_seed48==UINT64_C(0xB29D468F3AAD),
              "rain-covered target becomes age-zero fire");
        {
            GmRuntimeScheduledTick source,child;
            CHECK(gm_runtime_scheduled_tick_count(&r)==2 &&
                  gm_runtime_scheduled_tick_get(&r,0,&source) &&
                  gm_runtime_scheduled_tick_get(&r,1,&child) &&
                  source.block==51&&source.x==12&&source.y==78&&
                  source.z==8&&source.time==32&&source.order==0 &&
                  child.block==51&&child.x==13&&child.y==78&&
                  child.z==8&&child.time==35&&child.order==1,
                  "covered direct target creates exact source/child queue");
        }
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "rain-exposed volumetric fire runtime initializes");
    if(r.world){
        for(int y=76;y<=83;++y)
            for(int z=6;z<=10;++z)
                for(int x=9;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int z=6;z<=10;++z)
            for(int x=9;x<=14;++x)
                gm_world_set_block_meta(r.world,x,77,z,1,0);
        gm_world_set_block_meta(r.world,12,77,8,87,0);
        gm_world_set_block_meta(r.world,12,78,8,51,0);
        gm_world_set_block_meta(r.world,10,78,8,171,0);
        gm_runtime_set_weather(&r,1,0,1000000,1000000);
        CHECK(gm_runtime_set_fire_rain_context(&r,12,78,8,1,1,1) &&
              gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x5DEECE610)) &&
              gm_runtime_random_tick_block(&r,12,78,8,51),
              "exposed west volume candidate executes Random(125)");
        CHECK(gm_world_block(r.world,12,78,8)==51 &&
              gm_world_meta(r.world,12,78,8)==0 &&
              gm_world_block(r.world,10,78,8)==171 &&
              gm_world_block(r.world,11,78,8)==0 &&
              r.world_random_seed48==UINT64_C(0x06F23450DB83),
              "rain suppresses successful west volume candidate roll");
        {
            GmRuntimeScheduledTick source;
            CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&source) &&
                  source.block==51&&source.x==12&&source.y==78&&
                  source.z==8&&source.time==30&&source.order==0,
                  "wet west candidate leaves only the source schedule");
        }
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "rain-covered volumetric fire runtime initializes");
    if(r.world){
        static const int roof_dx[5]={-1,-2,0,-1,-1};
        static const int roof_dz[5]={0,0,0,-1,1};
        for(int y=76;y<=83;++y)
            for(int z=6;z<=10;++z)
                for(int x=9;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int z=6;z<=10;++z)
            for(int x=9;x<=14;++x)
                gm_world_set_block_meta(r.world,x,77,z,1,0);
        gm_world_set_block_meta(r.world,12,77,8,87,0);
        gm_world_set_block_meta(r.world,12,78,8,51,0);
        gm_world_set_block_meta(r.world,10,78,8,171,0);
        for(int index=0;index<5;++index)
            gm_world_set_block_meta(
                r.world,12+roof_dx[index],80,8+roof_dz[index],1,0);
        gm_runtime_set_weather(&r,1,0,1000000,1000000);
        CHECK(gm_runtime_set_fire_rain_context(&r,12,78,8,1,1,0) &&
              gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x5DEECE610)) &&
              gm_runtime_random_tick_block(&r,12,78,8,51),
              "covered west volume candidate executes Random(125)");
        CHECK(gm_world_block(r.world,12,78,8)==51 &&
              gm_world_meta(r.world,12,78,8)==0 &&
              gm_world_block(r.world,10,78,8)==171 &&
              gm_world_block(r.world,11,78,8)==51 &&
              gm_world_meta(r.world,11,78,8)==0 &&
              r.world_random_seed48==UINT64_C(0xE9AD9F0B0D75),
              "covered west volume candidate becomes age-zero fire");
        {
            GmRuntimeScheduledTick source,child;
            CHECK(gm_runtime_scheduled_tick_count(&r)==2 &&
                  gm_runtime_scheduled_tick_get(&r,0,&source) &&
                  gm_runtime_scheduled_tick_get(&r,1,&child) &&
                  source.block==51&&source.x==12&&source.y==78&&
                  source.z==8&&source.time==30&&source.order==0 &&
                  child.block==51&&child.x==11&&child.y==78&&
                  child.z==8&&child.time==35&&child.order==1,
                  "covered west candidate creates exact source/child queue");
        }
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "crop random-tick runtime initializes");
    if(r.world){
        for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx){
            gm_world_set_block_meta(r.world,12+dx,77,8+dz,1,0);
            gm_world_set_block_meta(r.world,12+dx,78,8+dz,0,0);
            gm_world_set_block_meta(r.world,12+dx,79,8+dz,0,0);
        }
        gm_world_set_block_meta(r.world,12,77,8,60,7);
        gm_world_set_block_meta(r.world,12,78,8,59,0);
        CHECK(gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x5DEECE664)),
              "Java Random internal seed restores exactly");
        CHECK(gm_runtime_random_tick_block(&r,12,78,8,59),
              "real wheat random-tick callback is represented");
        CHECK(gm_world_block(r.world,12,78,8)==59 &&
              gm_world_meta(r.world,12,78,8)==1,
              "Random(seed=9) grows isolated hydrated wheat from age 0 to 1");
        CHECK(r.world_random_seed48==UINT64_C(0xBAEBDE0BF09F),
              "wheat callback advances java.util.Random by one exact nextInt");
        {
            uint64_t cursor=r.world_random_seed48;
            gm_world_set_block_meta(r.world,12,78,8,59,7);
            CHECK(gm_runtime_random_tick_block(&r,12,78,8,59) &&
                  r.world_random_seed48==cursor,
                  "mature wheat exits before consuming world RNG");
        }
        {
            GmAction idle;
            memset(&idle,0,sizeof idle);
            idle.hotbar_sel=-1;
            for(int y=73;y<=83;++y)
                for(int z=3;z<=13;++z)
                    for(int x=7;x<=17;++x)
                        gm_world_set_block_meta(r.world,x,y,z,0,0);
            gm_world_set_block_meta(r.world,12,78,8,18,8);
            gm_world_set_block_meta(r.world,13,78,8,18,8);
            gm_world_set_block_meta(r.world,14,78,8,161,8);
            gm_world_set_block_meta(r.world,15,78,8,18,8);
            gm_world_set_block_meta(r.world,16,78,8,17,0);
            CHECK(gm_runtime_set_world_random_seed48(
                      &r,UINT64_C(0x0005DEECE66C)) &&
                  gm_runtime_random_tick_block(&r,12,78,8,18) &&
                  gm_world_block(r.world,12,78,8)==18 &&
                  gm_world_meta(r.world,12,78,8)==0 &&
                  gm_world_meta(r.world,13,78,8)==8 &&
                  gm_world_meta(r.world,14,78,8)==8 &&
                  gm_world_meta(r.world,15,78,8)==8 &&
                  r.world_random_seed48==UINT64_C(0x0005DEECE66C),
                  "four face-connected old/new leaf steps reach a log, clear "
                  "only the target check flag, and consume no RNG");

            for(int y=73;y<=83;++y)
                for(int z=3;z<=13;++z)
                    for(int x=7;x<=17;++x)
                        gm_world_set_block_meta(r.world,x,y,z,0,0);
            for(int z=3;z<=13;++z)
                for(int x=7;x<=17;++x)
                    gm_world_set_block_meta(r.world,x,77,z,1,0);
            memset(&r.entities,0,sizeof r.entities);
            gm_world_set_block_meta(r.world,12,78,8,18,8);
            gm_world_set_block_meta(r.world,12,79,8,18,4);
            gm_world_set_block_meta(r.world,13,78,9,161,4);
            CHECK(gm_runtime_set_entity_id_cursor(&r,5000) &&
                  gm_runtime_set_world_random_seed48(
                      &r,UINT64_C(0x0005DEECF39C)) &&
                  gm_runtime_set_math_random_seed48(
                      &r,UINT64_C(0x0FEDCBA98765)) &&
                  gm_runtime_set_block_random_seed48(
                      &r,UINT64_C(0x123456789ABC)) &&
                  gm_runtime_random_tick_block(&r,12,78,8,18) &&
                  gm_world_block(r.world,12,78,8)==0 &&
                  gm_world_meta(r.world,12,79,8)==12 &&
                  gm_world_meta(r.world,13,78,9)==12 &&
                  r.entities.n_active==2 &&
                  r.entities.ents[0].eid==5000 &&
                  r.entities.ents[0].item==6 &&
                  r.entities.ents[0].meta==0 &&
                  r.entities.ents[1].eid==5001 &&
                  r.entities.ents[1].item==260 &&
                  r.entities.ents[1].meta==0 &&
                  r.next_entity_id==5002 &&
                  r.world_random_seed48==UINT64_C(0xB6F421B010BE) &&
                  r.math_random_seed48==UINT64_C(0x6B1C94DF7835) &&
                  r.block_random_seed48==UINT64_C(0x123456789ABC),
                  "unsupported oak leaf decay selects sapling then apple, "
                  "destroys itself, and marks adjacent old/new leaves");
            gm_runtime_tick(&r,idle);
            CHECK(r.entities.ents[0].age==1 &&
                  r.entities.ents[0].pickup_delay==9 &&
                  r.entities.ents[0].y==78.75 &&
                  r.entities.ents[0].my==0.0,
                  "rising decay drop clips against a registry full-cube leaf "
                  "ceiling at the exact EntityItem top boundary");

            for(int y=73;y<=83;++y)
                for(int z=3;z<=13;++z)
                    for(int x=7;x<=17;++x)
                        gm_world_set_block_meta(r.world,x,y,z,0,0);
            memset(&r.entities,0,sizeof r.entities);
            for(int i=0;i<GM_LIVE_MAX-1;++i)
                r.entities.ents[i].active=1;
            r.entities.n_active=GM_LIVE_MAX-1;
            gm_world_set_block_meta(r.world,12,78,8,18,8);
            gm_world_set_block_meta(r.world,12,79,8,18,4);
            gm_world_set_block_meta(r.world,13,78,9,161,4);
            CHECK(gm_runtime_set_entity_id_cursor(&r,5010) &&
                  gm_runtime_set_world_random_seed48(
                      &r,UINT64_C(0x0005DEECF39C)) &&
                  gm_runtime_set_math_random_seed48(
                      &r,UINT64_C(0x0FEDCBA98765)) &&
                  !gm_runtime_random_tick_block(&r,12,78,8,18) &&
                  gm_world_meta(r.world,12,78,8)==8 &&
                  gm_world_meta(r.world,12,79,8)==4 &&
                  gm_world_meta(r.world,13,78,9)==4 &&
                  r.entities.n_active==GM_LIVE_MAX-1 &&
                  r.next_entity_id==5010 &&
                  r.world_random_seed48==UINT64_C(0x0005DEECF39C) &&
                  r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
                  "two-drop decay rejects one free entity slot without "
                  "partial RNG, items, decay marking, or block mutation");

            for(int y=73;y<=83;++y)
                for(int z=3;z<=13;++z)
                    for(int x=7;x<=17;++x)
                        gm_world_set_block_meta(r.world,x,y,z,0,0);
            memset(&r.entities,0,sizeof r.entities);
            gm_world_set_block_meta(r.world,12,78,8,161,13);
            CHECK(gm_runtime_set_world_random_seed48(
                      &r,UINT64_C(0x0005DEECE66C)) &&
                  gm_runtime_random_tick_block(&r,12,78,8,161) &&
                  gm_world_meta(r.world,12,78,8)==13 &&
                  r.world_random_seed48==UINT64_C(0x0005DEECE66C),
                  "nondecayable checked leaves2 exit before scan and RNG");
            gm_world_set_block_meta(r.world,12,78,8,161,2);
            CHECK(!gm_runtime_random_tick_block(&r,12,78,8,161) &&
                  gm_world_meta(r.world,12,78,8)==2 &&
                  r.world_random_seed48==UINT64_C(0x0005DEECE66C),
                  "noncanonical leaves2 variant is rejected atomically");
        }
        CHECK(!gm_runtime_random_tick_block(&r,12,78,8,2),
              "stale random-tick block identity is rejected");
        gm_world_set_block_meta(r.world,12,78,8,2,0);
        gm_world_set_block_meta(r.world,12,79,8,1,0);
        CHECK(gm_runtime_set_world_update_lcg(&r,1094913777),
              "World.updateLCG cursor restores exactly");
        CHECK(gm_runtime_random_tick_selection(&r,12,78,8,2,0),
              "isolated natural random-tick selector dispatches");
        CHECK(r.world_update_lcg==0x00382032 &&
              gm_world_block(r.world,12,78,8)==3,
              "updateLCG selects capped grass and its callback decays to dirt");
        gm_world_set_block_meta(r.world,12,77,8,12,0);
        gm_world_set_block_meta(r.world,12,78,8,81,0);
        gm_world_set_block_meta(r.world,12,79,8,0,0);
        CHECK(gm_runtime_set_world_update_lcg(&r,1094913777) &&
              gm_runtime_random_tick_selection(&r,12,78,8,81,0),
              "isolated selector dispatches light-independent cactus callback");
        CHECK(r.world_update_lcg==0x00382032 &&
              gm_world_block(r.world,12,78,8)==81 &&
              gm_world_meta(r.world,12,78,8)==1,
              "selected age-zero cactus advances to age one");
        gm_world_set_block_meta(r.world,12,78,8,2,0);
        CHECK(gm_runtime_set_world_update_lcg(&r,0) &&
              !gm_runtime_random_tick_selection(&r,12,78,8,2,0),
              "selector rejects an updateLCG coordinate miss");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "observer pulse runtime initializes");
    if(r.world){
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
        GmRuntimeScheduledTick pending;
        for(int y=77;y<=79;++y)
            for(int z=7;z<=9;++z)
                for(int x=10;x<=14;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,8,218,4);
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        gm_runtime_set_total_time(&r,100);
        CHECK(gm_runtime_set_block(&r,11,78,8,1,0) &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&pending) &&
              pending.block==218&&pending.x==12&&pending.y==78&&
              pending.z==8&&pending.time==102&&pending.priority==0,
              "watched-face edit schedules the observer at exact +2");
        CHECK(gm_runtime_set_block(&r,11,78,8,5,0) &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "second watched edit is suppressed while activation is pending");
        gm_runtime_tick(&r,idle);
        CHECK(gm_world_meta(r.world,12,78,8)==4 &&
              gm_world_block(r.world,13,78,8)==123,
              "observer remains unpowered through the first tick");
        gm_runtime_tick(&r,idle);
        CHECK(gm_world_meta(r.world,12,78,8)==12 &&
              gm_world_block(r.world,13,78,8)==124 &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&pending) &&
              pending.block==218&&pending.time==104,
              "due observer powers directionally and schedules +2 release");
        CHECK(gm_runtime_set_block(&r,11,78,8,0,0) &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "watched edit is suppressed while observer is powered");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(gm_world_meta(r.world,12,78,8)==4 &&
              gm_world_block(r.world,13,78,8)==124 &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&pending) &&
              pending.block==124&&pending.time==108,
              "observer release hands the lamp its independent +4 callback");
        for(int tick=0;tick<4;++tick)
            gm_runtime_tick(&r,idle);
        CHECK(gm_world_block(r.world,13,78,8)==123 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "observer-powered lamp turns off at the exact delayed boundary");
        CHECK(gm_runtime_set_block(&r,12,78,7,1,0) &&
              gm_runtime_scheduled_tick_count(&r)==0 &&
              gm_world_meta(r.world,12,78,8)==4,
              "non-watched neighbor edit does not start an observer pulse");
        gm_world_set_block_meta(r.world,12,78,8,218,12);
        gm_world_set_block_meta(r.world,13,78,8,1,0);
        gm_world_set_block_meta(r.world,14,78,8,124,0);
        CHECK(gm_runtime_schedule_tick(
                  &r,12,78,8,218,r.clock.total_time+2,0,0) &&
              gm_runtime_set_block(&r,12,78,8,0,0) &&
              gm_runtime_scheduled_tick_count(&r)==2 &&
              gm_runtime_scheduled_tick_get(&r,1,&pending) &&
              pending.block==124&&pending.x==14&&pending.time==
                  r.clock.total_time+4,
              "breaking powered pending observer notifies indirect lamp +4");
        for(int tick=0;tick<4;++tick)
            gm_runtime_tick(&r,idle);
        CHECK(gm_world_block(r.world,14,78,8)==123 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "powered-observer break drains its indirect lamp exactly");
        gm_world_set_block_meta(r.world,13,78,8,123,0);
        gm_world_set_block_meta(r.world,14,78,8,0,0);
        CHECK(gm_runtime_load_block(&r,12,78,8,0,0) &&
              gm_runtime_set_block(&r,12,78,8,218,4) &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&pending) &&
              pending.block==218&&pending.time==r.clock.total_time+2,
              "live observer placement creates its vanilla startup callback");
        gm_world_set_block_meta(r.world,12,78,8,218,6);
        CHECK(!gm_runtime_schedule_tick(
                  &r,12,78,8,218,r.clock.total_time+2,0,0),
              "invalid observer facing is rejected at saved callback boundary");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "empty piston extension runtime initializes");
    if(r.world){
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
        CHECK(gm_runtime_set_block_random_seed48(
                  &r,(UINT64_C(1)<<48)-UINT64_C(1)) &&
              r.block_random_seed48
                  ==(UINT64_C(1)<<48)-UINT64_C(1) &&
              !gm_runtime_set_block_random_seed48(
                  &r,UINT64_C(1)<<48) &&
              r.block_random_seed48
                  ==(UINT64_C(1)<<48)-UINT64_C(1),
              "Block.RANDOM internal cursor setter enforces 48-bit range");
        for(int y=77;y<=79;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,8,33,5);
        CHECK(gm_runtime_set_block(&r,12,78,9,152,0) &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              gm_world_meta(r.world,13,78,8)==5 &&
              r.piston_count==1,
              "side redstone block starts east empty extension immediately");
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==1 &&
              r.pistons[0].last_progress==0.0f &&
              r.pistons[0].progress==0.5f &&
              gm_world_block(r.world,13,78,8)==36,
              "first piston tile tick advances moving head to progress 0.5");
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==1 &&
              r.pistons[0].last_progress==0.5f &&
              r.pistons[0].progress==1.0f &&
              gm_world_block(r.world,13,78,8)==36,
              "second piston tile tick retains moving block at progress 1");
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,13,78,8)==34 &&
              gm_world_meta(r.world,13,78,8)==5,
              "third piston tile tick settles exact east piston head");

        CHECK(gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,9,0,0) &&
              gm_world_block(r.world,12,78,8)==36 &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==0 &&
              r.piston_count==1 &&
              r.pistons[0].active &&
              r.pistons[0].x==12 &&
              r.pistons[0].y==78 &&
              r.pistons[0].z==8 &&
              r.pistons[0].moved_block==33 &&
              r.pistons[0].moved_meta==5 &&
              r.pistons[0].facing==5 &&
              !r.pistons[0].extending &&
              r.pistons[0].source &&
              r.pistons[0].progress==0.0f &&
              r.world_random_seed48==UINT64_C(0xB) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765) &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "power loss starts exact normal-piston retraction and consumes "
              "one contraction-pitch float");
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==1 &&
              r.pistons[0].last_progress==0.0f &&
              r.pistons[0].progress==0.5f &&
              gm_world_block(r.world,12,78,8)==36 &&
              gm_world_block(r.world,13,78,8)==0,
              "first retraction tile tick advances the moving base to 0.5");
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==1 &&
              r.pistons[0].last_progress==0.5f &&
              r.pistons[0].progress==1.0f &&
              gm_world_block(r.world,12,78,8)==36 &&
              gm_world_block(r.world,13,78,8)==0,
              "second retraction tile tick retains moving base at progress 1");
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==0 &&
              r.world_random_seed48==UINT64_C(0xB),
              "third retraction tile tick settles the unextended base");

        for(int y=77;y<=79;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,8,29,5);
        gm_world_set_block_meta(r.world,13,78,8,1,0);
        CHECK(gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,9,152,0) &&
              gm_world_block(r.world,12,78,8)==29 &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              gm_world_meta(r.world,13,78,8)==13 &&
              gm_world_block(r.world,14,78,8)==36 &&
              gm_world_meta(r.world,14,78,8)==5 &&
              r.piston_count==2 &&
              r.pistons[0].moved_block==1 &&
              r.pistons[0].moved_meta==0 &&
              r.pistons[0].x==14 &&
              r.pistons[0].extending &&
              !r.pistons[0].source &&
              r.pistons[1].moved_block==34 &&
              r.pistons[1].moved_meta==13 &&
              r.pistons[1].x==13 &&
              r.pistons[1].extending &&
              r.pistons[1].source &&
              r.world_random_seed48==UINT64_C(0xB) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "sticky piston extension creates distinct sticky-head and "
              "stone moving tiles with exact sound RNG");
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==2 &&
              r.pistons[0].progress==0.5f &&
              r.pistons[1].progress==0.5f,
              "both sticky extension tiles advance together to 0.5");
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==2 &&
              r.pistons[0].progress==1.0f &&
              r.pistons[1].progress==1.0f,
              "both sticky extension tiles retain progress 1");
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,12,78,8)==29 &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==34 &&
              gm_world_meta(r.world,13,78,8)==13 &&
              gm_world_block(r.world,14,78,8)==1 &&
              gm_world_meta(r.world,14,78,8)==0,
              "sticky extension settles typed head and pushed stone");

        CHECK(gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,9,0,0) &&
              gm_world_block(r.world,12,78,8)==36 &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              gm_world_meta(r.world,13,78,8)==5 &&
              gm_world_block(r.world,14,78,8)==0 &&
              r.piston_count==2 &&
              r.pistons[0].moved_block==29 &&
              r.pistons[0].moved_meta==5 &&
              r.pistons[0].x==12 &&
              !r.pistons[0].extending &&
              r.pistons[0].source &&
              r.pistons[1].moved_block==1 &&
              r.pistons[1].moved_meta==0 &&
              r.pistons[1].x==13 &&
              !r.pistons[1].extending &&
              !r.pistons[1].source &&
              r.world_random_seed48==UINT64_C(0xB) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "sticky retraction creates base and pulled-stone moving tiles "
              "in Java insertion order");
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==2 &&
              r.pistons[0].progress==0.5f &&
              r.pistons[1].progress==0.5f,
              "both sticky retraction tiles advance together to 0.5");
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==2 &&
              r.pistons[0].progress==1.0f &&
              r.pistons[1].progress==1.0f,
              "both sticky retraction tiles retain progress 1");
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,12,78,8)==29 &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==1 &&
              gm_world_meta(r.world,13,78,8)==0 &&
              gm_world_block(r.world,14,78,8)==0 &&
              r.world_random_seed48==UINT64_C(0xB),
              "sticky retraction settles the base and pulls stone one cell");

        {
            static const int piston_dx[6]={0,0,0,0,-1,1};
            static const int piston_dy[6]={-1,1,0,0,0,0};
            static const int piston_dz[6]={0,0,-1,1,0,0};
            for(int facing=0;facing<6;++facing){
                int bx=12,by=facing==0?80:79,bz=8;
                int hx=bx+piston_dx[facing];
                int hy=by+piston_dy[facing];
                int hz=bz+piston_dz[facing];
                int sx=12,sy=by,sz=facing==3?7:9;
                r.piston_count=0;
                memset(r.pistons,0,sizeof r.pistons);
                for(int y=76;y<=83;++y)
                    for(int z=5;z<=11;++z)
                        for(int x=9;x<=15;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_world_set_block_meta(
                    r.world,bx,by,bz,29,facing|8);
                gm_world_set_block_meta(
                    r.world,hx,hy,hz,34,facing|8);
                gm_world_set_block_meta(r.world,sx,sy,sz,152,0);
                CHECK(gm_runtime_set_world_random_seed48(
                          &r,UINT64_C(0)) &&
                      gm_runtime_set_block(&r,sx,sy,sz,0,0) &&
                      gm_world_block(r.world,bx,by,bz)==36 &&
                      gm_world_meta(r.world,bx,by,bz)==(facing|8) &&
                      gm_world_block(r.world,hx,hy,hz)==0 &&
                      gm_world_block(
                          r.world,
                          bx+2*piston_dx[facing],
                          by+2*piston_dy[facing],
                          bz+2*piston_dz[facing])==0 &&
                      r.piston_count==1 &&
                      r.pistons[0].x==bx &&
                      r.pistons[0].y==by &&
                      r.pistons[0].z==bz &&
                      r.pistons[0].moved_block==29 &&
                      r.pistons[0].moved_meta==facing &&
                      !r.pistons[0].extending &&
                      r.pistons[0].source &&
                      r.world_random_seed48==UINT64_C(0xB),
                      "empty sticky retraction starts base-only motion in "
                      "all six facings");
                gm_runtime_tick(&r,idle);
                gm_runtime_tick(&r,idle);
                gm_runtime_tick(&r,idle);
                CHECK(r.piston_count==0 &&
                      gm_world_block(r.world,bx,by,bz)==29 &&
                      gm_world_meta(r.world,bx,by,bz)==facing &&
                      gm_world_block(r.world,hx,hy,hz)==0 &&
                      r.world_random_seed48==UINT64_C(0xB),
                      "six-facing empty sticky retraction settles exactly");
            }
            for(int facing=0;facing<6;++facing){
                int bx=12,by=facing==0?80:79,bz=8;
                int hx=bx+piston_dx[facing];
                int hy=by+piston_dy[facing];
                int hz=bz+piston_dz[facing];
                int tx=bx+2*piston_dx[facing];
                int ty=by+2*piston_dy[facing];
                int tz=bz+2*piston_dz[facing];
                int sx=12,sy=by,sz=facing==3?7:9;
                r.piston_count=0;
                memset(r.pistons,0,sizeof r.pistons);
                for(int y=76;y<=83;++y)
                    for(int z=5;z<=11;++z)
                        for(int x=9;x<=15;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_world_set_block_meta(r.world,bx,by,bz,29,facing);
                gm_world_set_block_meta(r.world,hx,hy,hz,1,0);
                CHECK(gm_runtime_set_world_random_seed48(
                          &r,UINT64_C(0)) &&
                      gm_runtime_set_block(&r,sx,sy,sz,152,0) &&
                      gm_world_block(r.world,bx,by,bz)==29 &&
                      gm_world_meta(r.world,bx,by,bz)==(facing|8) &&
                      gm_world_block(r.world,hx,hy,hz)==36 &&
                      gm_world_meta(r.world,hx,hy,hz)==(facing|8) &&
                      gm_world_block(r.world,tx,ty,tz)==36 &&
                      gm_world_meta(r.world,tx,ty,tz)==facing &&
                      r.piston_count==2 &&
                      r.world_random_seed48==UINT64_C(0xB),
                      "one-stone sticky extension starts exact moving states "
                      "in all six facings");
                gm_runtime_tick(&r,idle);
                gm_runtime_tick(&r,idle);
                gm_runtime_tick(&r,idle);
                CHECK(r.piston_count==0 &&
                      gm_world_block(r.world,bx,by,bz)==29 &&
                      gm_world_meta(r.world,bx,by,bz)==(facing|8) &&
                      gm_world_block(r.world,hx,hy,hz)==34 &&
                      gm_world_meta(r.world,hx,hy,hz)==(facing|8) &&
                      gm_world_block(r.world,tx,ty,tz)==1,
                      "six-facing one-stone sticky extension settles exactly");
                CHECK(gm_runtime_set_world_random_seed48(
                          &r,UINT64_C(0)) &&
                      gm_runtime_set_block(&r,sx,sy,sz,0,0) &&
                      gm_world_block(r.world,bx,by,bz)==36 &&
                      gm_world_meta(r.world,bx,by,bz)==(facing|8) &&
                      gm_world_block(r.world,hx,hy,hz)==36 &&
                      gm_world_meta(r.world,hx,hy,hz)==facing &&
                      gm_world_block(r.world,tx,ty,tz)==0 &&
                      r.piston_count==2 &&
                      r.world_random_seed48==UINT64_C(0xB),
                      "one-stone sticky pull starts exact moving states in "
                      "all six facings");
                gm_runtime_tick(&r,idle);
                gm_runtime_tick(&r,idle);
                gm_runtime_tick(&r,idle);
                CHECK(r.piston_count==0 &&
                      gm_world_block(r.world,bx,by,bz)==29 &&
                      gm_world_meta(r.world,bx,by,bz)==facing &&
                      gm_world_block(r.world,hx,hy,hz)==1 &&
                      gm_world_block(r.world,tx,ty,tz)==0 &&
                      r.world_random_seed48==UINT64_C(0xB),
                      "six-facing one-stone sticky pull settles exactly");
            }
            {
                static const int target_block[5]={5,217,54,33,33};
                static const int target_meta[5]={2,0,2,13,5};
                static const int target_pulled[5]={1,0,0,0,1};
                for(int facing=0;facing<5;++facing){
                    int bx=12,by=facing==0?80:79,bz=8;
                    int hx=bx+piston_dx[facing];
                    int hy=by+piston_dy[facing];
                    int hz=bz+piston_dz[facing];
                    int tx=bx+2*piston_dx[facing];
                    int ty=by+2*piston_dy[facing];
                    int tz=bz+2*piston_dz[facing];
                    int sx=12,sy=by,sz=facing==3?7:9;
                    r.piston_count=0;
                    memset(r.pistons,0,sizeof r.pistons);
                    for(int y=76;y<=83;++y)
                        for(int z=5;z<=11;++z)
                            for(int x=9;x<=15;++x)
                                gm_world_set_block_meta(r.world,x,y,z,0,0);
                    gm_world_set_block_meta(
                        r.world,bx,by,bz,29,facing|8);
                    gm_world_set_block_meta(
                        r.world,hx,hy,hz,34,facing|8);
                    gm_world_set_block_meta(
                        r.world,tx,ty,tz,
                        target_block[facing],target_meta[facing]);
                    if(facing==3){
                        gm_world_set_block_meta(r.world,tx+1,ty,tz,34,5);
                        gm_world_set_block_meta(r.world,tx,ty,tz+1,152,0);
                    }
                    gm_world_set_block_meta(r.world,sx,sy,sz,152,0);
                    CHECK(gm_runtime_set_world_random_seed48(
                              &r,UINT64_C(0)) &&
                          gm_runtime_set_block(&r,sx,sy,sz,0,0) &&
                          gm_world_block(r.world,bx,by,bz)==36 &&
                          gm_world_meta(r.world,bx,by,bz)==(facing|8) &&
                          gm_world_block(r.world,hx,hy,hz)==
                              (target_pulled[facing]?36:0) &&
                          gm_world_block(r.world,tx,ty,tz)==
                              (target_pulled[facing]
                               ?0:target_block[facing]) &&
                          r.piston_count==(target_pulled[facing]?2:1) &&
                          r.world_random_seed48==UINT64_C(0xB),
                          "sticky target reactions start exact movable or "
                          "base-only motion across five facings");
                    gm_runtime_tick(&r,idle);
                    gm_runtime_tick(&r,idle);
                    gm_runtime_tick(&r,idle);
                    CHECK(r.piston_count==0 &&
                          gm_world_block(r.world,bx,by,bz)==29 &&
                          gm_world_meta(r.world,bx,by,bz)==facing &&
                          gm_world_block(r.world,hx,hy,hz)==
                              (target_pulled[facing]
                               ?target_block[facing]:0) &&
                          gm_world_block(r.world,tx,ty,tz)==
                              (target_pulled[facing]
                               ?0:target_block[facing]) &&
                          gm_world_meta(
                              r.world,
                              target_pulled[facing]?hx:tx,
                              target_pulled[facing]?hy:ty,
                              target_pulled[facing]?hz:tz)==
                              target_meta[facing],
                          "sticky target reactions settle exact target state "
                          "across five facings");
                }
            }
        }

        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=76;y<=83;++y)
            for(int z=5;z<=11;++z)
                for(int x=9;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,29,13);
        gm_world_set_block_meta(r.world,13,79,8,34,13);
        gm_world_set_block_meta(r.world,14,79,8,49,0);
        gm_world_set_block_meta(r.world,12,79,9,152,0);
        CHECK(gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_block(&r,12,79,9,0,0) &&
              gm_world_block(r.world,12,79,8)==36 &&
              gm_world_meta(r.world,12,79,8)==13 &&
              gm_world_block(r.world,13,79,8)==0 &&
              gm_world_block(r.world,14,79,8)==49 &&
              r.piston_count==1 &&
              r.world_random_seed48==UINT64_C(0xB),
              "sticky retraction leaves an immovable target and starts only "
              "the source tile");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,12,79,8)==29 &&
              gm_world_meta(r.world,12,79,8)==5 &&
              gm_world_block(r.world,13,79,8)==0 &&
              gm_world_block(r.world,14,79,8)==49,
              "immovable-target sticky retraction settles base-only");

        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=76;y<=83;++y)
            for(int z=5;z<=11;++z)
                for(int x=9;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,29,13);
        gm_world_set_block_meta(r.world,13,79,8,34,13);
        gm_world_set_block_meta(r.world,12,79,9,152,0);
        CHECK(gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_block(&r,12,79,9,0,0) &&
              r.piston_count==1 &&
              r.world_random_seed48==UINT64_C(0xB),
              "sticky repower regression begins one exact retraction");
        gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_set_block(&r,12,79,7,152,0) &&
              r.piston_count==1 &&
              r.world_random_seed48==UINT64_C(0xB),
              "power restored beside moving base does not start early");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 && r.piston_recheck_count==1 &&
              gm_world_block(r.world,12,79,8)==29 &&
              gm_world_meta(r.world,12,79,8)==5 &&
              gm_world_block(r.world,13,79,8)==0 &&
              r.world_random_seed48==UINT64_C(0xB),
              "settled repowered base defers its queued extension event");
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==1 && r.piston_recheck_count==0 &&
              gm_world_block(r.world,12,79,8)==29 &&
              gm_world_meta(r.world,12,79,8)==13 &&
              gm_world_block(r.world,13,79,8)==36 &&
              gm_world_meta(r.world,13,79,8)==13 &&
              r.pistons[0].moved_block==34 &&
              r.pistons[0].moved_meta==13 &&
              r.pistons[0].extending && r.pistons[0].source &&
              r.pistons[0].progress==0.5f &&
              r.pistons[0].last_progress==0.0f &&
              r.world_random_seed48==UINT64_C(277363943098),
              "next tick starts and advances exact queued re-extension");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,12,79,8)==29 &&
              gm_world_meta(r.world,12,79,8)==13 &&
              gm_world_block(r.world,13,79,8)==34 &&
              gm_world_meta(r.world,13,79,8)==13 &&
              gm_world_block(r.world,12,79,7)==152 &&
              gm_world_block(r.world,12,79,9)==0,
              "repowered sticky piston returns to exact extended state");

        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=76;y<=83;++y)
            for(int z=5;z<=11;++z)
                for(int x=9;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,29,13);
        gm_world_set_block_meta(r.world,12,79,9,152,0);
        CHECK(gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_block(&r,12,79,9,0,0) &&
              gm_world_block(r.world,12,79,8)==36 &&
              gm_world_meta(r.world,12,79,8)==13 &&
              gm_world_block(r.world,13,79,8)==0 &&
              r.piston_count==1 &&
              r.pistons[0].moved_block==29 &&
              r.pistons[0].moved_meta==5 &&
              r.world_random_seed48==UINT64_C(0xB),
              "headless extended sticky base still accepts its retraction "
              "event");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,12,79,8)==29 &&
              gm_world_meta(r.world,12,79,8)==5 &&
              gm_world_block(r.world,13,79,8)==0,
              "headless sticky retraction settles the unextended base");

        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=76;y<=83;++y)
            for(int z=5;z<=11;++z)
                for(int x=9;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,33,13);
        gm_world_set_block_meta(r.world,12,79,9,152,0);
        CHECK(gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_block(&r,12,79,9,0,0) &&
              gm_world_block(r.world,12,79,8)==36 &&
              gm_world_meta(r.world,12,79,8)==5 &&
              gm_world_block(r.world,13,79,8)==0 &&
              r.piston_count==1 &&
              r.pistons[0].moved_block==33 &&
              r.pistons[0].moved_meta==5 &&
              r.world_random_seed48==UINT64_C(0xB),
              "headless extended normal base still accepts its retraction "
              "event");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,12,79,8)==33 &&
              gm_world_meta(r.world,12,79,8)==5 &&
              gm_world_block(r.world,13,79,8)==0,
              "headless normal retraction settles the unextended base");

        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=76;y<=83;++y)
            for(int z=5;z<=11;++z)
                for(int x=9;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,29,13);
        gm_world_set_block_meta(r.world,13,79,8,1,0);
        gm_world_set_block_meta(r.world,14,79,8,49,0);
        gm_world_set_block_meta(r.world,12,79,9,152,0);
        CHECK(gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_block(&r,12,79,9,0,0) &&
              gm_world_block(r.world,12,79,8)==36 &&
              gm_world_meta(r.world,12,79,8)==13 &&
              gm_world_block(r.world,13,79,8)==1 &&
              gm_world_block(r.world,14,79,8)==49 &&
              r.piston_count==1 &&
              r.world_random_seed48==UINT64_C(0xB),
              "base-only sticky retraction preserves an unrelated front "
              "block and immovable pull target");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,12,79,8)==29 &&
              gm_world_meta(r.world,12,79,8)==5 &&
              gm_world_block(r.world,13,79,8)==1 &&
              gm_world_block(r.world,14,79,8)==49,
              "obstructed-front sticky retraction settles without erasure");

        for(int y=77;y<=79;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,8,29,5);
        gm_world_set_block_meta(r.world,13,78,8,1,0);
        CHECK(gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,9,152,0) &&
              r.piston_count==2 &&
              r.world_random_seed48==UINT64_C(0xB),
              "minimum-pulse fixture starts sticky one-stone extension");
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==2 &&
              r.pistons[0].progress==0.5f &&
              r.pistons[1].progress==0.5f,
              "minimum-pulse fixture reaches in-flight extension state");
        CHECK(gm_runtime_set_block(&r,12,78,9,0,0) &&
              gm_world_block(r.world,12,78,8)==36 &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==0 &&
              gm_world_block(r.world,14,78,8)==1 &&
              gm_world_meta(r.world,14,78,8)==0 &&
              r.piston_count==1 &&
              r.pistons[0].moved_block==29 &&
              r.pistons[0].moved_meta==5 &&
              r.pistons[0].x==12 &&
              !r.pistons[0].extending &&
              r.pistons[0].source &&
              r.pistons[0].progress==0.0f &&
              r.world_random_seed48==UINT64_C(0x40942DE6BA) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "minimum pulse force-settles extending tiles, suppresses the "
              "sticky pull, and starts exact retraction");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,12,78,8)==29 &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==0 &&
              gm_world_block(r.world,14,78,8)==1 &&
              r.world_random_seed48==UINT64_C(0x40942DE6BA),
              "minimum-pulse retraction settles without pulling the stone");

        for(int y=77;y<=79;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,8,33,5);
        gm_world_set_block_meta(r.world,13,78,8,1,0);
        CHECK(gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_block(&r,12,78,9,152,0) &&
              r.piston_count==2 &&
              r.world_random_seed48==UINT64_C(0xB),
              "normal minimum-pulse fixture starts one-stone extension");
        gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_set_block(&r,12,78,9,0,0) &&
              gm_world_block(r.world,12,78,8)==36 &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==0 &&
              gm_world_block(r.world,14,78,8)==36 &&
              r.piston_count==2 &&
              r.pistons[0].moved_block==1 &&
              r.pistons[0].extending &&
              r.pistons[1].moved_block==33 &&
              !r.pistons[1].extending &&
              r.pistons[1].source &&
              r.world_random_seed48==UINT64_C(0x40942DE6BA),
              "normal minimum pulse clears the head while its moved stone "
              "continues and the base reverses");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==0 &&
              gm_world_block(r.world,14,78,8)==1 &&
              gm_world_meta(r.world,14,78,8)==0,
              "normal minimum pulse settles its destination and base");

        for(int y=77;y<=82;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,33,5);
        gm_world_set_block_meta(r.world,13,79,8,165,0);
        gm_world_set_block_meta(r.world,13,80,8,1,0);
        CHECK(gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_block(&r,12,79,9,152,0) &&
              gm_world_block(r.world,12,79,8)==33 &&
              gm_world_meta(r.world,12,79,8)==13 &&
              gm_world_block(r.world,13,79,8)==36 &&
              gm_world_block(r.world,14,79,8)==36 &&
              gm_world_block(r.world,13,80,8)==0 &&
              gm_world_block(r.world,14,80,8)==36 &&
              r.piston_count==3 &&
              r.pistons[0].x==14 && r.pistons[0].y==80 &&
              r.pistons[0].moved_block==1 &&
              r.pistons[1].x==14 && r.pistons[1].y==79 &&
              r.pistons[1].moved_block==165 &&
              r.pistons[2].x==13 && r.pistons[2].y==79 &&
              r.pistons[2].moved_block==34 &&
              r.pistons[2].source &&
              r.world_random_seed48==UINT64_C(0xB),
              "slime branch moves its UP stone before slime and head");
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==3 &&
              r.pistons[0].progress==0.5f &&
              r.pistons[1].progress==0.5f &&
              r.pistons[2].progress==0.5f,
              "slime branch moving tiles share the first progress boundary");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,13,79,8)==34 &&
              gm_world_meta(r.world,13,79,8)==5 &&
              gm_world_block(r.world,14,79,8)==165 &&
              gm_world_block(r.world,13,80,8)==0 &&
              gm_world_block(r.world,14,80,8)==1,
              "slime branch settles attached stone at its destination");

        for(int y=78;y<=82;++y)
            for(int z=7;z<=10;++z)
                for(int x=0;x<=24;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int x=0;x<=24;++x)
            gm_world_set_block_meta(r.world,x,77,8,1,0);
        gm_world_set_block_meta(r.world,12,78,8,33,5);
        gm_world_set_block_meta(r.world,13,78,8,165,0);
        CHECK(gm_runtime_set_block(&r,12,78,9,152,0) &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==165 &&
              r.piston_count==0,
              "slime attachment rejects a floor line beyond the 12-block "
              "structure limit");

        for(int y=78;y<=82;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,33,5);
        gm_world_set_block_meta(r.world,13,79,8,165,0);
        gm_world_set_block_meta(r.world,13,80,8,49,0);
        CHECK(gm_runtime_set_block(&r,12,79,9,152,0) &&
              gm_world_meta(r.world,12,79,8)==13 &&
              gm_world_block(r.world,13,80,8)==49 &&
              gm_world_block(r.world,14,80,8)==0 &&
              r.piston_count==2,
              "slime branch ignores an immovable side attachment");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,14,79,8)==165 &&
              gm_world_block(r.world,13,80,8)==49,
              "slime moves while side obsidian remains fixed");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=82;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,33,5);
        gm_world_set_block_meta(r.world,13,79,8,165,0);
        gm_world_set_block_meta(r.world,13,80,8,1,0);
        gm_world_set_block_meta(r.world,14,80,8,30,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5300) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,79,9,152,0) &&
              gm_world_meta(r.world,12,79,8)==13 &&
              gm_world_block(r.world,13,79,8)==36 &&
              gm_world_block(r.world,14,79,8)==36 &&
              gm_world_block(r.world,13,80,8)==0 &&
              gm_world_block(r.world,14,80,8)==36 &&
              r.piston_count==3 &&
              r.pistons[0].x==14 && r.pistons[0].y==80 &&
              r.pistons[0].moved_block==1 &&
              r.pistons[1].x==14 && r.pistons[1].y==79 &&
              r.pistons[1].moved_block==165 &&
              r.pistons[2].x==13 && r.pistons[2].y==79 &&
              r.pistons[2].moved_block==34 &&
              r.entities.n_active==1 &&
              r.entities.ents[0].eid==5300 &&
              r.entities.ents[0].item==287 &&
              r.entities.ents[0].count==1 &&
              r.entities.ents[0].meta==0 &&
              r.entities.ents[0].age==0 &&
              r.entities.ents[0].pickup_delay==10 &&
              r.next_entity_id==5301 &&
              r.world_random_seed48==UINT64_C(0x5D5692ACE2BF) &&
              r.math_random_seed48==UINT64_C(0x33E01D26154D),
              "slime side line destroys its cobweb before moving the "
              "attached stone");
        gm_runtime_tick(&r,idle);
        CHECK(r.entities.ents[0].active &&
              r.entities.ents[0].age==1 &&
              r.entities.ents[0].pickup_delay==9 &&
              fabs(r.entities.ents[0].x-14.635)<1.0e-12,
              "slime terminal string drop collides with the moving side "
              "stone on its first entity tick");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,13,79,8)==34 &&
              gm_world_block(r.world,14,79,8)==165 &&
              gm_world_block(r.world,13,80,8)==0 &&
              gm_world_block(r.world,14,80,8)==1,
              "slime cobweb side line settles both moved blocks");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=82;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,33,5);
        gm_world_set_block_meta(r.world,13,79,8,165,0);
        gm_world_set_block_meta(r.world,13,80,8,1,0);
        gm_world_set_block_meta(r.world,14,80,8,26,3);
        gm_world_set_block_meta(r.world,15,80,8,26,11);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5340) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,79,9,152,0) &&
              gm_world_meta(r.world,12,79,8)==13 &&
              gm_world_block(r.world,13,79,8)==36 &&
              gm_world_block(r.world,14,79,8)==36 &&
              gm_world_block(r.world,13,80,8)==0 &&
              gm_world_block(r.world,14,80,8)==36 &&
              gm_world_block(r.world,15,80,8)==0 &&
              r.piston_count==3 &&
              r.entities.n_active==1 &&
              r.entities.ents[0].eid==5340 &&
              r.entities.ents[0].item==355 &&
              r.entities.ents[0].meta==0 &&
              r.next_entity_id==5341 &&
              r.world_random_seed48==UINT64_C(0x5D5692ACE2BF) &&
              r.math_random_seed48==UINT64_C(0x33E01D26154D),
              "slime side line destroys a bed foot and its ordered "
              "notification removes the paired head");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,13,79,8)==34 &&
              gm_world_block(r.world,14,79,8)==165 &&
              gm_world_block(r.world,14,80,8)==1 &&
              gm_world_block(r.world,15,80,8)==0 &&
              r.entities.ents[0].active &&
              r.entities.ents[0].age==3,
              "slime bed-foot extension settles with one retained bed item");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=82;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,33,5);
        gm_world_set_block_meta(r.world,13,79,8,165,0);
        gm_world_set_block_meta(r.world,13,80,8,1,0);
        gm_world_set_block_meta(r.world,14,80,7,26,0);
        gm_world_set_block_meta(r.world,14,80,8,26,8);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5350) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,79,9,152,0) &&
              gm_world_meta(r.world,12,79,8)==13 &&
              gm_world_block(r.world,14,80,7)==0 &&
              gm_world_block(r.world,14,80,8)==36 &&
              r.piston_count==3 &&
              r.entities.n_active==1 &&
              r.entities.ents[0].eid==5350 &&
              r.entities.ents[0].item==355 &&
              r.next_entity_id==5351 &&
              r.world_random_seed48==UINT64_C(0x5D5692ACE2BF) &&
              r.math_random_seed48==UINT64_C(0x33E01D26154D),
              "slime side line destroys a bed head before its foot owns "
              "the deferred item drop");

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=82;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,33,5);
        gm_world_set_block_meta(r.world,13,79,8,165,0);
        gm_world_set_block_meta(r.world,13,80,8,1,0);
        gm_world_set_block_meta(r.world,14,80,7,26,0);
        gm_world_set_block_meta(r.world,14,80,8,26,8);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5360) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,79,9,152,0) &&
              gm_world_meta(r.world,12,79,8)==5 &&
              gm_world_block(r.world,13,79,8)==165 &&
              gm_world_block(r.world,13,80,8)==1 &&
              gm_world_meta(r.world,14,80,7)==0 &&
              gm_world_meta(r.world,14,80,8)==8 &&
              r.piston_count==0 &&
              r.entities.n_active==GM_LIVE_MAX &&
              r.next_entity_id==5360 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "slime bed-head preflight reserves its deferred foot drop "
              "and rejects a full item pool atomically");

        {
            static const int door_blocks[7]={64,71,193,194,195,196,197};
            static const int door_items[7]={324,330,427,428,429,430,431};
            int lower_states=0;
            int upper_states=0;
            for(int door_i=0;door_i<7;++door_i){
                int door=door_blocks[door_i];
                int item=door_items[door_i];
                for(int lower_meta=0;lower_meta<8;++lower_meta){
                    int upper_meta=8+(lower_meta&3);
                    int eid=6000+door_i*16+lower_meta;
                    memset(&r.entities,0,sizeof r.entities);
                    r.piston_count=0;
                    memset(r.pistons,0,sizeof r.pistons);
                    for(int y=77;y<=82;++y)
                        for(int z=6;z<=10;++z)
                            for(int x=10;x<=16;++x)
                                gm_world_set_block_meta(
                                    r.world,x,y,z,0,0);
                    gm_world_set_block_meta(r.world,12,79,8,33,5);
                    gm_world_set_block_meta(r.world,13,79,8,165,0);
                    gm_world_set_block_meta(r.world,13,79,7,1,0);
                    gm_world_set_block_meta(r.world,14,78,7,1,0);
                    gm_world_set_block_meta(
                        r.world,14,79,7,door,lower_meta);
                    gm_world_set_block_meta(
                        r.world,14,80,7,door,upper_meta);
                    CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                          gm_runtime_set_world_random_seed48(
                              &r,UINT64_C(0)) &&
                          gm_runtime_set_math_random_seed48(
                              &r,UINT64_C(0x0FEDCBA98765)) &&
                          gm_runtime_set_block(&r,12,79,9,152,0) &&
                          gm_world_meta(r.world,12,79,8)==13 &&
                          gm_world_block(r.world,14,79,7)==36 &&
                          gm_world_block(r.world,14,80,7)==0 &&
                          r.piston_count==3 &&
                          r.entities.n_active==1 &&
                          r.entities.ents[0].eid==eid &&
                          r.entities.ents[0].item==item &&
                          r.entities.ents[0].meta==0 &&
                          r.next_entity_id==eid+1 &&
                          r.world_random_seed48
                              ==UINT64_C(0x5D5692ACE2BF) &&
                          r.math_random_seed48
                              ==UINT64_C(0x33E01D26154D),
                          "all door lower states are destroyed by a slime "
                          "side line with their exact registered item");
                    ++lower_states;
                }
                for(int upper_meta=8;upper_meta<=11;++upper_meta){
                    int lower_meta=upper_meta&3;
                    int eid=6120+door_i*8+upper_meta;
                    memset(&r.entities,0,sizeof r.entities);
                    r.piston_count=0;
                    memset(r.pistons,0,sizeof r.pistons);
                    for(int y=77;y<=82;++y)
                        for(int z=6;z<=10;++z)
                            for(int x=10;x<=16;++x)
                                gm_world_set_block_meta(
                                    r.world,x,y,z,0,0);
                    gm_world_set_block_meta(r.world,12,79,8,33,5);
                    gm_world_set_block_meta(r.world,13,79,8,165,0);
                    gm_world_set_block_meta(r.world,13,79,7,1,0);
                    gm_world_set_block_meta(r.world,14,77,7,1,0);
                    gm_world_set_block_meta(
                        r.world,14,78,7,door,lower_meta);
                    gm_world_set_block_meta(
                        r.world,14,79,7,door,upper_meta);
                    CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                          gm_runtime_set_world_random_seed48(
                              &r,UINT64_C(0)) &&
                          gm_runtime_set_math_random_seed48(
                              &r,UINT64_C(0x0FEDCBA98765)) &&
                          gm_runtime_set_block(&r,12,79,9,152,0) &&
                          gm_world_meta(r.world,12,79,8)==13 &&
                          gm_world_block(r.world,14,78,7)==0 &&
                          gm_world_block(r.world,14,79,7)==36 &&
                          r.piston_count==3 &&
                          r.entities.n_active==1 &&
                          r.entities.ents[0].eid==eid &&
                          r.entities.ents[0].item==item &&
                          r.entities.ents[0].meta==0 &&
                          r.next_entity_id==eid+1 &&
                          r.world_random_seed48
                              ==UINT64_C(0x5D5692ACE2BF) &&
                          r.math_random_seed48
                              ==UINT64_C(0x33E01D26154D),
                          "all door upper states defer the exact item to "
                          "their notified lower half under slime movement");
                    ++upper_states;
                }
            }
            CHECK(lower_states==56 && upper_states==28,
                  "slime door tests cover every canonical paired state");
        }

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=82;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,33,5);
        gm_world_set_block_meta(r.world,13,79,8,165,0);
        gm_world_set_block_meta(r.world,13,79,7,1,0);
        gm_world_set_block_meta(r.world,14,77,7,1,0);
        gm_world_set_block_meta(r.world,14,78,7,64,3);
        gm_world_set_block_meta(r.world,14,79,7,64,11);
        CHECK(gm_runtime_set_entity_id_cursor(&r,6200) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,79,9,152,0) &&
              gm_world_meta(r.world,12,79,8)==5 &&
              gm_world_block(r.world,13,79,8)==165 &&
              gm_world_block(r.world,13,79,7)==1 &&
              gm_world_meta(r.world,14,78,7)==3 &&
              gm_world_meta(r.world,14,79,7)==11 &&
              r.piston_count==0 &&
              r.entities.n_active==GM_LIVE_MAX &&
              r.next_entity_id==6200 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "slime door-upper preflight reserves the lower item and "
              "rejects a full pool atomically");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=82;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,29,13);
        gm_world_set_block_meta(r.world,13,79,8,34,13);
        gm_world_set_block_meta(r.world,14,79,8,165,0);
        gm_world_set_block_meta(r.world,14,79,7,1,0);
        gm_world_set_block_meta(r.world,13,78,7,1,0);
        gm_world_set_block_meta(r.world,13,79,7,64,3);
        gm_world_set_block_meta(r.world,13,80,7,64,11);
        gm_world_set_block_meta(r.world,12,79,9,152,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,6210) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,79,9,0,0) &&
              gm_world_block(r.world,12,79,8)==36 &&
              gm_world_block(r.world,13,79,8)==36 &&
              gm_world_block(r.world,14,79,8)==0 &&
              gm_world_block(r.world,13,79,7)==36 &&
              gm_world_block(r.world,14,79,7)==0 &&
              gm_world_block(r.world,13,80,7)==0 &&
              r.piston_count==3 &&
              r.entities.n_active==1 &&
              r.entities.ents[0].eid==6210 &&
              r.entities.ents[0].item==324 &&
              r.next_entity_id==6211 &&
              r.world_random_seed48==UINT64_C(0x5D5692ACE2BF) &&
              r.math_random_seed48==UINT64_C(0x33E01D26154D),
              "sticky slime pull destroys a lower door and removes its "
              "upper before moving the north side stone west");

        {
            static const int lower_metas[5]={0,1,3,4,5};
            int lower_states=0;
            int upper_states=0;
            for(int variant_i=0;variant_i<5;++variant_i){
                int lower_meta=lower_metas[variant_i];
                int has_item=lower_meta!=3;
                for(int upper_meta=8;upper_meta<=11;++upper_meta){
                    int eid=6300+variant_i*8+upper_meta;
                    memset(&r.entities,0,sizeof r.entities);
                    r.piston_count=0;
                    memset(r.pistons,0,sizeof r.pistons);
                    for(int y=77;y<=82;++y)
                        for(int z=6;z<=10;++z)
                            for(int x=10;x<=16;++x)
                                gm_world_set_block_meta(
                                    r.world,x,y,z,0,0);
                    gm_world_set_block_meta(r.world,12,79,8,33,5);
                    gm_world_set_block_meta(r.world,13,79,8,165,0);
                    gm_world_set_block_meta(r.world,13,79,7,1,0);
                    gm_world_set_block_meta(r.world,14,78,7,3,0);
                    gm_world_set_block_meta(
                        r.world,14,79,7,175,lower_meta);
                    gm_world_set_block_meta(
                        r.world,14,80,7,175,upper_meta);
                    CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                          gm_runtime_set_world_random_seed48(
                              &r,UINT64_C(0)) &&
                          gm_runtime_set_math_random_seed48(
                              &r,UINT64_C(0x0FEDCBA98765)) &&
                          gm_runtime_set_block(&r,12,79,9,152,0) &&
                          gm_world_meta(r.world,12,79,8)==13 &&
                          gm_world_block(r.world,14,79,7)==36 &&
                          gm_world_block(r.world,14,80,7)==0 &&
                          r.piston_count==3 &&
                          r.entities.n_active==has_item &&
                          (!has_item ||
                              (r.entities.ents[0].eid==eid &&
                               r.entities.ents[0].item==175 &&
                               r.entities.ents[0].meta==lower_meta)) &&
                          r.next_entity_id==eid+has_item &&
                          r.world_random_seed48==(has_item
                              ? UINT64_C(0x5D5692ACE2BF)
                              : UINT64_C(0xB)) &&
                          r.math_random_seed48==(has_item
                              ? UINT64_C(0x33E01D26154D)
                              : UINT64_C(0x0FEDCBA98765)),
                          "all deterministic double-plant lower states "
                          "destroy with their exact registered payload");
                    ++lower_states;

                    memset(&r.entities,0,sizeof r.entities);
                    r.piston_count=0;
                    memset(r.pistons,0,sizeof r.pistons);
                    for(int y=77;y<=82;++y)
                        for(int z=6;z<=10;++z)
                            for(int x=10;x<=16;++x)
                                gm_world_set_block_meta(
                                    r.world,x,y,z,0,0);
                    gm_world_set_block_meta(r.world,12,79,8,33,5);
                    gm_world_set_block_meta(r.world,13,79,8,165,0);
                    gm_world_set_block_meta(r.world,13,79,7,1,0);
                    gm_world_set_block_meta(r.world,14,77,7,3,0);
                    gm_world_set_block_meta(
                        r.world,14,78,7,175,lower_meta);
                    gm_world_set_block_meta(
                        r.world,14,79,7,175,upper_meta);
                    CHECK(gm_runtime_set_entity_id_cursor(&r,eid+100) &&
                          gm_runtime_set_world_random_seed48(
                              &r,UINT64_C(0)) &&
                          gm_runtime_set_math_random_seed48(
                              &r,UINT64_C(0x0FEDCBA98765)) &&
                          gm_runtime_set_block(&r,12,79,9,152,0) &&
                          gm_world_meta(r.world,12,79,8)==13 &&
                          gm_world_block(r.world,14,78,7)==0 &&
                          gm_world_block(r.world,14,79,7)==36 &&
                          r.piston_count==3 &&
                          r.entities.n_active==has_item &&
                          (!has_item ||
                              (r.entities.ents[0].eid==eid+100 &&
                               r.entities.ents[0].item==175 &&
                               r.entities.ents[0].meta==lower_meta)) &&
                          r.next_entity_id==eid+100+has_item &&
                          r.world_random_seed48==(has_item
                              ? UINT64_C(0x5D5692ACE2BF)
                              : UINT64_C(0xB)) &&
                          r.math_random_seed48==(has_item
                              ? UINT64_C(0x33E01D26154D)
                              : UINT64_C(0x0FEDCBA98765)),
                          "all deterministic double-plant upper states "
                          "defer their exact lower-half payload");
                    ++upper_states;
                }
            }
            CHECK(lower_states==20 && upper_states==20,
                  "slime double-plant tests cover every deterministic "
                  "canonical paired state");
        }

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=82;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,33,5);
        gm_world_set_block_meta(r.world,13,79,8,165,0);
        gm_world_set_block_meta(r.world,13,79,7,1,0);
        gm_world_set_block_meta(r.world,14,77,7,3,0);
        gm_world_set_block_meta(r.world,14,78,7,175,4);
        gm_world_set_block_meta(r.world,14,79,7,175,10);
        CHECK(gm_runtime_set_entity_id_cursor(&r,6500) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,79,9,152,0) &&
              gm_world_meta(r.world,12,79,8)==5 &&
              gm_world_block(r.world,13,79,8)==165 &&
              gm_world_block(r.world,13,79,7)==1 &&
              gm_world_meta(r.world,14,78,7)==4 &&
              gm_world_meta(r.world,14,79,7)==10 &&
              r.piston_count==0 && r.entities.n_active==GM_LIVE_MAX &&
              r.next_entity_id==6500 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "slime double-plant upper preflight reserves the lower item "
              "and rejects a full pool atomically");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        gm_world_set_block_meta(r.world,12,79,9,0,0);
        gm_world_set_block_meta(r.world,14,78,7,175,2);
        gm_world_set_block_meta(r.world,14,79,7,175,10);
        CHECK(gm_runtime_set_entity_id_cursor(&r,6501) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,79,9,152,0) &&
              gm_world_meta(r.world,12,79,8)==13 &&
              gm_world_block(r.world,14,78,7)==0 &&
              gm_world_block(r.world,14,79,7)==36 &&
              r.piston_count==3 && r.entities.n_active==1 &&
              r.entities.ents[0].eid==6501 &&
              r.entities.ents[0].item==295 &&
              r.entities.ents[0].meta==0 &&
              r.next_entity_id==6502 &&
              r.world_random_seed48==UINT64_C(0x17617168255E) &&
              r.math_random_seed48==UINT64_C(0x33E01D26154D),
              "random double-grass upper defers its seed roll and drop to "
              "the lower-half notification");

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=82;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,33,5);
        gm_world_set_block_meta(r.world,13,79,8,165,0);
        gm_world_set_block_meta(r.world,13,79,7,1,0);
        gm_world_set_block_meta(r.world,14,77,7,3,0);
        gm_world_set_block_meta(r.world,14,78,7,175,2);
        gm_world_set_block_meta(r.world,14,79,7,175,10);
        CHECK(gm_runtime_set_entity_id_cursor(&r,6503) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(1396)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,79,9,152,0) &&
              gm_world_meta(r.world,12,79,8)==13 &&
              gm_world_block(r.world,14,78,7)==0 &&
              gm_world_block(r.world,14,79,7)==36 &&
              r.piston_count==3 && r.entities.n_active==GM_LIVE_MAX &&
              r.next_entity_id==6503 &&
              r.world_random_seed48==UINT64_C(0x6E982FE6AB4E) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "random double-grass no-drop upper remains admissible with a "
              "full item pool and consumes its exact roll before sound");

        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=82;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,33,5);
        gm_world_set_block_meta(r.world,13,79,8,165,0);
        gm_world_set_block_meta(r.world,13,79,7,1,0);
        gm_world_set_block_meta(r.world,14,77,7,3,0);
        gm_world_set_block_meta(r.world,14,78,7,175,2);
        gm_world_set_block_meta(r.world,14,79,7,175,10);
        CHECK(gm_runtime_set_entity_id_cursor(&r,6504) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,79,9,152,0) &&
              gm_world_meta(r.world,12,79,8)==5 &&
              gm_world_block(r.world,13,79,8)==165 &&
              gm_world_meta(r.world,14,78,7)==2 &&
              gm_world_meta(r.world,14,79,7)==10 &&
              r.piston_count==0 && r.entities.n_active==GM_LIVE_MAX &&
              r.next_entity_id==6504 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "random double-grass dropping upper rejects a full pool "
              "atomically before consuming its roll");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=82;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,33,5);
        gm_world_set_block_meta(r.world,13,79,8,165,0);
        gm_world_set_block_meta(r.world,13,79,7,1,0);
        gm_world_set_block_meta(r.world,13,79,9,1,0);
        gm_world_set_block_meta(r.world,14,78,7,3,0);
        gm_world_set_block_meta(r.world,14,79,7,175,2);
        gm_world_set_block_meta(r.world,14,80,7,175,10);
        gm_world_set_block_meta(r.world,14,77,9,3,0);
        gm_world_set_block_meta(r.world,14,78,9,175,2);
        gm_world_set_block_meta(r.world,14,79,9,175,10);
        CHECK(gm_runtime_set_entity_id_cursor(&r,6520) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,79,9,152,0) &&
              gm_world_meta(r.world,12,79,8)==13 &&
              gm_world_block(r.world,13,79,8)==36 &&
              gm_world_block(r.world,14,79,8)==36 &&
              gm_world_block(r.world,13,79,7)==0 &&
              gm_world_block(r.world,14,79,7)==36 &&
              gm_world_block(r.world,14,80,7)==0 &&
              gm_world_block(r.world,13,79,9)==0 &&
              gm_world_block(r.world,14,78,9)==0 &&
              gm_world_block(r.world,14,79,9)==36 &&
              r.piston_count==4 && r.entities.n_active==2 &&
              r.entities.ents[0].eid==6520 &&
              r.entities.ents[0].item==295 &&
              r.entities.ents[1].eid==6521 &&
              r.entities.ents[1].item==295 &&
              r.next_entity_id==6522 &&
              r.world_random_seed48==UINT64_C(0x3BB194F24A25) &&
              r.math_random_seed48==UINT64_C(0x6B1C94DF7835),
              "mixed double-grass structure rolls the direct NORTH lower "
              "before the deferred SOUTH lower and preserves both cursors");

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX-1;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX-1;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=82;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,33,5);
        gm_world_set_block_meta(r.world,13,79,8,165,0);
        gm_world_set_block_meta(r.world,13,79,7,1,0);
        gm_world_set_block_meta(r.world,13,79,9,1,0);
        gm_world_set_block_meta(r.world,14,78,7,3,0);
        gm_world_set_block_meta(r.world,14,79,7,175,2);
        gm_world_set_block_meta(r.world,14,80,7,175,10);
        gm_world_set_block_meta(r.world,14,77,9,3,0);
        gm_world_set_block_meta(r.world,14,78,9,175,2);
        gm_world_set_block_meta(r.world,14,79,9,175,10);
        CHECK(gm_runtime_set_entity_id_cursor(&r,6530) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,79,9,152,0) &&
              gm_world_meta(r.world,12,79,8)==5 &&
              gm_world_block(r.world,13,79,8)==165 &&
              gm_world_block(r.world,13,79,7)==1 &&
              gm_world_meta(r.world,14,79,7)==2 &&
              gm_world_meta(r.world,14,79,9)==10 &&
              r.piston_count==0 &&
              r.entities.n_active==GM_LIVE_MAX-1 &&
              r.next_entity_id==6530 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "mixed double-grass structure reserves both selected drops "
              "and rejects one free slot atomically");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=82;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,29,13);
        gm_world_set_block_meta(r.world,13,79,8,34,13);
        gm_world_set_block_meta(r.world,14,79,8,165,0);
        gm_world_set_block_meta(r.world,14,79,7,1,0);
        gm_world_set_block_meta(r.world,13,78,7,3,0);
        gm_world_set_block_meta(r.world,13,79,7,175,4);
        gm_world_set_block_meta(r.world,13,80,7,175,10);
        gm_world_set_block_meta(r.world,12,79,9,152,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,6510) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,79,9,0,0) &&
              gm_world_block(r.world,12,79,8)==36 &&
              gm_world_block(r.world,13,79,8)==36 &&
              gm_world_block(r.world,14,79,8)==0 &&
              gm_world_block(r.world,13,79,7)==36 &&
              gm_world_block(r.world,14,79,7)==0 &&
              gm_world_block(r.world,13,80,7)==0 &&
              r.piston_count==3 && r.entities.n_active==1 &&
              r.entities.ents[0].eid==6510 &&
              r.entities.ents[0].item==175 &&
              r.entities.ents[0].meta==4 &&
              r.next_entity_id==6511 &&
              r.world_random_seed48==UINT64_C(0x5D5692ACE2BF) &&
              r.math_random_seed48==UINT64_C(0x33E01D26154D),
              "sticky slime pull destroys a double-plant lower and removes "
              "its upper before moving the north side stone west");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=82;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,33,5);
        gm_world_set_block_meta(r.world,13,79,8,165,0);
        gm_world_set_block_meta(r.world,13,80,8,1,0);
        gm_world_set_block_meta(r.world,14,80,8,30,0);
        gm_world_set_block_meta(r.world,13,79,7,1,0);
        gm_world_set_block_meta(r.world,14,79,7,37,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5310) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,79,9,152,0) &&
              r.piston_count==4 &&
              r.pistons[0].x==14 && r.pistons[0].y==79 &&
              r.pistons[0].z==7 && r.pistons[0].moved_block==1 &&
              r.pistons[1].x==14 && r.pistons[1].y==80 &&
              r.pistons[1].z==8 && r.pistons[1].moved_block==1 &&
              r.pistons[2].x==14 && r.pistons[2].y==79 &&
              r.pistons[2].z==8 && r.pistons[2].moved_block==165 &&
              r.pistons[3].x==13 && r.pistons[3].y==79 &&
              r.pistons[3].z==8 && r.pistons[3].moved_block==34 &&
              r.entities.n_active==2 &&
              r.entities.ents[0].eid==5310 &&
              r.entities.ents[0].item==37 &&
              r.entities.ents[1].eid==5311 &&
              r.entities.ents[1].item==287 &&
              r.next_entity_id==5312 &&
              r.world_random_seed48==UINT64_C(0x86D91B38BCB3) &&
              r.math_random_seed48==UINT64_C(0x6B1C94DF7835),
              "multiple slime terminal destroys and moves follow Java "
              "reverse insertion order");

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX-1;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX-1;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=82;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,33,5);
        gm_world_set_block_meta(r.world,13,79,8,165,0);
        gm_world_set_block_meta(r.world,13,80,8,1,0);
        gm_world_set_block_meta(r.world,14,80,8,30,0);
        gm_world_set_block_meta(r.world,13,79,7,1,0);
        gm_world_set_block_meta(r.world,14,79,7,37,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5320) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,79,9,152,0) &&
              gm_world_meta(r.world,12,79,8)==5 &&
              gm_world_block(r.world,13,79,8)==165 &&
              gm_world_block(r.world,13,80,8)==1 &&
              gm_world_block(r.world,14,80,8)==30 &&
              gm_world_block(r.world,13,79,7)==1 &&
              gm_world_block(r.world,14,79,7)==37 &&
              r.piston_count==0 &&
              r.entities.n_active==GM_LIVE_MAX-1 &&
              r.next_entity_id==5320 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "multi-terminal slime preflight rejects one free item slot "
              "without partial piston, drop, or RNG state");
        memset(&r.entities,0,sizeof r.entities);

        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=76;y<=82;++y)
            for(int z=5;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,33,5);
        gm_world_set_block_meta(r.world,13,79,8,165,0);
        gm_world_set_block_meta(r.world,13,79,7,1,0);
        gm_world_set_block_meta(r.world,14,78,7,3,0);
        gm_world_set_block_meta(r.world,14,78,6,9,0);
        gm_world_set_block_meta(r.world,14,79,7,83,3);
        gm_world_set_block_meta(r.world,14,80,7,83,7);
        gm_world_set_block_meta(r.world,14,81,7,83,12);
        CHECK(gm_runtime_set_entity_id_cursor(&r,6540) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,79,9,152,0) &&
              gm_world_meta(r.world,12,79,8)==13 &&
              gm_world_block(r.world,14,79,7)==36 &&
              gm_world_block(r.world,14,80,7)==0 &&
              gm_world_block(r.world,14,81,7)==0 &&
              r.piston_count==3 && r.entities.n_active==3 &&
              r.entities.ents[0].eid==6540 &&
              r.entities.ents[1].eid==6541 &&
              r.entities.ents[2].eid==6542 &&
              r.entities.ents[0].item==338 &&
              r.entities.ents[1].item==338 &&
              r.entities.ents[2].item==338 &&
              r.next_entity_id==6543 &&
              r.world_random_seed48==UINT64_C(0xEE85F453C1E7) &&
              r.math_random_seed48==UINT64_C(0x63BD8BBB501D),
              "slime lower-reed terminal drops its three-high column in "
              "bottom-up callback order with exact cursors");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=76;y<=82;++y)
            for(int z=5;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,29,13);
        gm_world_set_block_meta(r.world,13,79,8,34,13);
        gm_world_set_block_meta(r.world,14,79,8,165,0);
        gm_world_set_block_meta(r.world,14,79,7,1,0);
        gm_world_set_block_meta(r.world,13,77,7,3,0);
        gm_world_set_block_meta(r.world,13,77,6,9,0);
        gm_world_set_block_meta(r.world,13,78,7,83,3);
        gm_world_set_block_meta(r.world,13,79,7,83,7);
        gm_world_set_block_meta(r.world,13,80,7,83,12);
        gm_world_set_block_meta(r.world,12,79,9,152,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,6550) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,79,9,0,0) &&
              gm_world_block(r.world,13,78,7)==83 &&
              gm_world_block(r.world,13,79,7)==36 &&
              gm_world_block(r.world,13,80,7)==0 &&
              r.piston_count==3 && r.entities.n_active==2 &&
              r.entities.ents[0].eid==6550 &&
              r.entities.ents[1].eid==6551 &&
              r.entities.ents[0].item==338 &&
              r.entities.ents[1].item==338 &&
              r.next_entity_id==6552 &&
              r.world_random_seed48==UINT64_C(0x86D91B38BCB3) &&
              r.math_random_seed48==UINT64_C(0x6B1C94DF7835),
              "sticky slime middle-reed pull leaves the supported lower "
              "cell and drops the middle then top");

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX-2;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX-2;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=76;y<=82;++y)
            for(int z=5;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,33,5);
        gm_world_set_block_meta(r.world,13,79,8,165,0);
        gm_world_set_block_meta(r.world,13,79,7,1,0);
        gm_world_set_block_meta(r.world,14,78,7,3,0);
        gm_world_set_block_meta(r.world,14,78,6,9,0);
        gm_world_set_block_meta(r.world,14,79,7,83,3);
        gm_world_set_block_meta(r.world,14,80,7,83,7);
        gm_world_set_block_meta(r.world,14,81,7,83,12);
        CHECK(gm_runtime_set_entity_id_cursor(&r,6560) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,79,9,152,0) &&
              gm_world_meta(r.world,12,79,8)==5 &&
              gm_world_block(r.world,13,79,8)==165 &&
              gm_world_block(r.world,13,79,7)==1 &&
              gm_world_meta(r.world,14,79,7)==3 &&
              gm_world_meta(r.world,14,80,7)==7 &&
              gm_world_meta(r.world,14,81,7)==12 &&
              r.piston_count==0 &&
              r.entities.n_active==GM_LIVE_MAX-2 &&
              r.next_entity_id==6560 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "three-reed slime cascade rejects two free slots without "
              "partial world, piston, entity, or RNG state");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=76;y<=82;++y)
            for(int z=5;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,33,5);
        gm_world_set_block_meta(r.world,13,79,8,165,0);
        gm_world_set_block_meta(r.world,13,79,7,1,0);
        gm_world_set_block_meta(r.world,14,77,6,1,0);
        gm_world_set_block_meta(r.world,14,78,6,12,0);
        gm_world_set_block_meta(r.world,14,79,6,81,3);
        gm_world_set_block_meta(r.world,14,80,6,81,7);
        gm_world_set_block_meta(r.world,14,81,6,81,12);
        CHECK(gm_runtime_set_entity_id_cursor(&r,6570) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,79,9,152,0) &&
              r.piston_count==3 && r.entities.n_active==0 &&
              r.world_random_seed48==UINT64_C(0xB),
              "slime branch starts beside a valid cactus column without "
              "premature destruction");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,14,79,7)==1 &&
              gm_world_block(r.world,14,79,6)==0 &&
              gm_world_block(r.world,14,80,6)==0 &&
              gm_world_block(r.world,14,81,6)==0 &&
              r.entities.n_active==3 &&
              r.entities.ents[0].item==81 &&
              r.entities.ents[1].item==81 &&
              r.entities.ents[2].item==81 &&
              r.entities.ents[0].age==0 &&
              r.entities.ents[1].age==0 &&
              r.entities.ents[2].age==0 &&
              r.next_entity_id==6573 &&
              r.world_random_seed48==UINT64_C(0xEE85F453C1E7) &&
              r.math_random_seed48==UINT64_C(0x63BD8BBB501D),
              "settled slime side stone collapses the three-high cactus "
              "column in the settlement boundary");

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX-2;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX-2;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=76;y<=82;++y)
            for(int z=5;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,33,5);
        gm_world_set_block_meta(r.world,13,79,8,165,0);
        gm_world_set_block_meta(r.world,13,79,7,1,0);
        gm_world_set_block_meta(r.world,14,77,6,1,0);
        gm_world_set_block_meta(r.world,14,78,6,12,0);
        gm_world_set_block_meta(r.world,14,79,6,81,3);
        gm_world_set_block_meta(r.world,14,80,6,81,7);
        gm_world_set_block_meta(r.world,14,81,6,81,12);
        CHECK(gm_runtime_set_entity_id_cursor(&r,6580) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,79,9,152,0) &&
              gm_world_meta(r.world,12,79,8)==5 &&
              gm_world_block(r.world,13,79,8)==165 &&
              gm_world_block(r.world,13,79,7)==1 &&
              gm_world_meta(r.world,14,79,6)==3 &&
              gm_world_meta(r.world,14,80,6)==7 &&
              gm_world_meta(r.world,14,81,6)==12 &&
              r.piston_count==0 &&
              r.entities.n_active==GM_LIVE_MAX-2 &&
              r.next_entity_id==6580 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "slime cactus settlement reserves the complete column and "
              "rejects two free slots atomically");

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX-4;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX-4;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=76;y<=82;++y)
            for(int z=5;z<=11;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,33,5);
        gm_world_set_block_meta(r.world,13,79,8,165,0);
        gm_world_set_block_meta(r.world,13,79,7,1,0);
        gm_world_set_block_meta(r.world,14,78,7,3,0);
        gm_world_set_block_meta(r.world,14,78,6,9,0);
        gm_world_set_block_meta(r.world,14,79,7,83,3);
        gm_world_set_block_meta(r.world,14,80,7,83,7);
        gm_world_set_block_meta(r.world,14,81,7,83,12);
        gm_world_set_block_meta(r.world,13,79,9,1,0);
        gm_world_set_block_meta(r.world,14,77,10,1,0);
        gm_world_set_block_meta(r.world,14,78,10,12,0);
        gm_world_set_block_meta(r.world,14,79,10,81,3);
        gm_world_set_block_meta(r.world,14,80,10,81,7);
        gm_world_set_block_meta(r.world,14,81,10,81,12);
        CHECK(gm_runtime_set_entity_id_cursor(&r,6585) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,79,9,152,0) &&
              gm_world_meta(r.world,12,79,8)==5 &&
              gm_world_block(r.world,13,79,8)==165 &&
              gm_world_block(r.world,13,79,7)==1 &&
              gm_world_block(r.world,13,79,9)==1 &&
              gm_world_meta(r.world,14,79,7)==3 &&
              gm_world_meta(r.world,14,81,7)==12 &&
              gm_world_meta(r.world,14,79,10)==3 &&
              gm_world_meta(r.world,14,81,10)==12 &&
              r.piston_count==0 &&
              r.entities.n_active==GM_LIVE_MAX-4 &&
              r.next_entity_id==6585 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "slime preflight combines three direct reed and three delayed "
              "cactus drops before accepting four free slots");

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX-2;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX-2;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=76;y<=82;++y)
            for(int z=5;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,29,13);
        gm_world_set_block_meta(r.world,13,79,8,34,13);
        gm_world_set_block_meta(r.world,14,79,8,165,0);
        gm_world_set_block_meta(r.world,14,79,7,1,0);
        gm_world_set_block_meta(r.world,13,77,6,1,0);
        gm_world_set_block_meta(r.world,13,78,6,12,0);
        gm_world_set_block_meta(r.world,13,79,6,81,3);
        gm_world_set_block_meta(r.world,13,80,6,81,7);
        gm_world_set_block_meta(r.world,13,81,6,81,12);
        gm_world_set_block_meta(r.world,12,79,9,152,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,6590) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,79,9,0,0) &&
              gm_world_block(r.world,12,79,8)==36 &&
              gm_world_block(r.world,13,79,8)==0 &&
              gm_world_block(r.world,14,79,8)==165 &&
              gm_world_block(r.world,14,79,7)==1 &&
              gm_world_meta(r.world,13,79,6)==3 &&
              gm_world_meta(r.world,13,80,6)==7 &&
              gm_world_meta(r.world,13,81,6)==12 &&
              r.piston_count==1 &&
              r.entities.n_active==GM_LIVE_MAX-2 &&
              r.next_entity_id==6590 &&
              r.world_random_seed48==UINT64_C(0xB) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "capacity-limited sticky retraction settles only its base and "
              "leaves the slime branch and cactus column intact");
        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);

        for(int y=77;y<=82;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,29,13);
        gm_world_set_block_meta(r.world,13,79,8,34,13);
        gm_world_set_block_meta(r.world,14,79,8,165,0);
        gm_world_set_block_meta(r.world,14,80,8,1,0);
        gm_world_set_block_meta(r.world,12,79,9,152,0);
        CHECK(gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,79,9,0,0) &&
              gm_world_block(r.world,12,79,8)==36 &&
              gm_world_meta(r.world,12,79,8)==13 &&
              gm_world_block(r.world,13,79,8)==36 &&
              gm_world_meta(r.world,13,79,8)==5 &&
              gm_world_block(r.world,14,79,8)==0 &&
              gm_world_block(r.world,13,80,8)==36 &&
              gm_world_meta(r.world,13,80,8)==5 &&
              gm_world_block(r.world,14,80,8)==0 &&
              r.piston_count==3 &&
              r.pistons[0].x==12 && r.pistons[0].y==79 &&
              r.pistons[0].moved_block==29 &&
              r.pistons[0].moved_meta==5 &&
              !r.pistons[0].extending && r.pistons[0].source &&
              r.pistons[1].x==13 && r.pistons[1].y==80 &&
              r.pistons[1].moved_block==1 &&
              !r.pistons[1].extending && !r.pistons[1].source &&
              r.pistons[2].x==13 && r.pistons[2].y==79 &&
              r.pistons[2].moved_block==165 &&
              !r.pistons[2].extending && !r.pistons[2].source &&
              r.world_random_seed48==UINT64_C(0xB) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "sticky retraction pulls a slime branch in exact reverse "
              "structure order");
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==3 &&
              r.pistons[0].progress==0.5f &&
              r.pistons[1].progress==0.5f &&
              r.pistons[2].progress==0.5f,
              "sticky slime pull tiles share the first progress boundary");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,12,79,8)==29 &&
              gm_world_meta(r.world,12,79,8)==5 &&
              gm_world_block(r.world,13,79,8)==165 &&
              gm_world_block(r.world,14,79,8)==0 &&
              gm_world_block(r.world,13,80,8)==1 &&
              gm_world_block(r.world,14,80,8)==0,
              "sticky slime pull settles its attached stone and slime");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=82;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,29,13);
        gm_world_set_block_meta(r.world,13,79,8,34,13);
        gm_world_set_block_meta(r.world,14,79,8,165,0);
        gm_world_set_block_meta(r.world,14,80,8,1,0);
        gm_world_set_block_meta(r.world,13,80,8,30,0);
        gm_world_set_block_meta(r.world,12,79,9,152,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5330) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,79,9,0,0) &&
              gm_world_block(r.world,12,79,8)==36 &&
              gm_world_meta(r.world,12,79,8)==13 &&
              gm_world_block(r.world,13,79,8)==36 &&
              gm_world_meta(r.world,13,79,8)==5 &&
              gm_world_block(r.world,14,79,8)==0 &&
              gm_world_block(r.world,13,80,8)==36 &&
              gm_world_meta(r.world,13,80,8)==5 &&
              gm_world_block(r.world,14,80,8)==0 &&
              r.piston_count==3 &&
              r.pistons[0].moved_block==29 &&
              r.pistons[1].x==13 && r.pistons[1].y==80 &&
              r.pistons[1].moved_block==1 &&
              r.pistons[2].x==13 && r.pistons[2].y==79 &&
              r.pistons[2].moved_block==165 &&
              r.entities.n_active==1 &&
              r.entities.ents[0].eid==5330 &&
              r.entities.ents[0].item==287 &&
              r.next_entity_id==5331 &&
              r.world_random_seed48==UINT64_C(0x5D5692ACE2BF) &&
              r.math_random_seed48==UINT64_C(0x33E01D26154D),
              "sticky slime pull destroys a simple terminal before moving "
              "its side line west");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,12,79,8)==29 &&
              gm_world_meta(r.world,12,79,8)==5 &&
              gm_world_block(r.world,13,79,8)==165 &&
              gm_world_block(r.world,14,79,8)==0 &&
              gm_world_block(r.world,13,80,8)==1 &&
              gm_world_block(r.world,14,80,8)==0 &&
              r.entities.ents[0].active &&
              r.entities.ents[0].age==3,
              "sticky slime terminal destroy settles blocks and retains "
              "the exact item lifetime");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=82;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,8,29,13);
        gm_world_set_block_meta(r.world,13,79,8,34,13);
        gm_world_set_block_meta(r.world,14,79,8,165,0);
        gm_world_set_block_meta(r.world,12,80,8,26,9);
        gm_world_set_block_meta(r.world,13,80,8,26,1);
        gm_world_set_block_meta(r.world,14,80,8,1,0);
        gm_world_set_block_meta(r.world,12,79,9,152,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5370) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,79,9,0,0) &&
              gm_world_block(r.world,12,79,8)==36 &&
              gm_world_block(r.world,13,79,8)==36 &&
              gm_world_block(r.world,14,79,8)==0 &&
              gm_world_block(r.world,12,80,8)==0 &&
              gm_world_block(r.world,13,80,8)==36 &&
              gm_world_block(r.world,14,80,8)==0 &&
              r.piston_count==3 &&
              r.entities.n_active==1 &&
              r.entities.ents[0].eid==5370 &&
              r.entities.ents[0].item==355 &&
              r.next_entity_id==5371 &&
              r.world_random_seed48==UINT64_C(0x5D5692ACE2BF) &&
              r.math_random_seed48==UINT64_C(0x33E01D26154D),
              "sticky slime pull destroys a bed foot and removes its head "
              "before moving the side stone west");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,12,79,8)==29 &&
              gm_world_meta(r.world,12,79,8)==5 &&
              gm_world_block(r.world,13,79,8)==165 &&
              gm_world_block(r.world,13,80,8)==1 &&
              gm_world_block(r.world,12,80,8)==0 &&
              r.entities.ents[0].active &&
              r.entities.ents[0].age==3,
              "sticky slime bed-foot pull settles with one bed item");

        for(int y=77;y<=82;++y)
            for(int z=7;z<=10;++z)
                for(int x=0;x<=24;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        for(int x=0;x<=24;++x)
            gm_world_set_block_meta(r.world,x,78,8,1,0);
        gm_world_set_block_meta(r.world,12,79,8,29,13);
        gm_world_set_block_meta(r.world,13,79,8,34,13);
        gm_world_set_block_meta(r.world,14,79,8,165,0);
        gm_world_set_block_meta(r.world,12,79,9,152,0);
        CHECK(gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_block(&r,12,79,9,0,0) &&
              gm_world_block(r.world,12,79,8)==36 &&
              gm_world_meta(r.world,12,79,8)==13 &&
              gm_world_block(r.world,13,79,8)==0 &&
              gm_world_block(r.world,14,79,8)==165 &&
              gm_world_block(r.world,14,78,8)==1 &&
              r.piston_count==1 &&
              r.pistons[0].moved_block==29 &&
              r.pistons[0].source &&
              r.world_random_seed48==UINT64_C(0xB),
              "oversized sticky slime structure is left in place while "
              "the piston still retracts");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,12,79,8)==29 &&
              gm_world_meta(r.world,12,79,8)==5 &&
              gm_world_block(r.world,13,79,8)==0 &&
              gm_world_block(r.world,14,79,8)==165 &&
              gm_world_block(r.world,14,78,8)==1,
              "oversized sticky slime rejection settles only the base");

        for(int y=77;y<=79;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,77,9,1,0);
        gm_world_set_block_meta(r.world,12,78,8,33,5);
        gm_world_set_block_meta(r.world,12,78,9,69,5);
        CHECK(gm_runtime_set_block(&r,12,78,9,69,13) &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              gm_world_meta(r.world,13,78,8)==5 &&
              r.piston_count==1,
              "powered side lever starts east empty extension immediately");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,13,78,8)==34 &&
              gm_world_meta(r.world,13,78,8)==5,
              "lever-powered empty piston settles through the same lifecycle");

        for(int y=77;y<=79;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,77,9,1,0);
        gm_world_set_block_meta(r.world,12,78,8,33,5);
        gm_world_set_block_meta(r.world,12,78,9,69,5);
        CHECK(gm_runtime_set_block(&r,12,78,9,69,5) &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==0 &&
              r.piston_count==0,
              "unpowered side lever does not start piston extension");

        for(int y=77;y<=79;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,77,9,1,0);
        gm_world_set_block_meta(r.world,12,78,8,33,5);
        gm_world_set_block_meta(r.world,12,78,9,77,5);
        CHECK(gm_runtime_set_block(&r,12,78,9,77,13) &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              gm_world_meta(r.world,13,78,8)==5 &&
              r.piston_count==1,
              "powered side stone button starts east empty extension");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,13,78,8)==34 &&
              gm_world_meta(r.world,13,78,8)==5,
              "button-powered empty piston settles through same lifecycle");

        for(int y=77;y<=79;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,77,9,1,0);
        gm_world_set_block_meta(r.world,12,78,8,33,5);
        gm_world_set_block_meta(r.world,12,78,9,77,5);
        CHECK(gm_runtime_set_block(&r,12,78,9,77,5) &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==0 &&
              r.piston_count==0,
              "unpowered side stone button does not start piston extension");

        for(int y=77;y<=79;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,77,9,1,0);
        gm_world_set_block_meta(r.world,12,78,8,33,5);
        gm_world_set_block_meta(r.world,12,78,9,143,5);
        CHECK(gm_runtime_set_block(&r,12,78,9,143,13) &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              gm_world_meta(r.world,13,78,8)==5 &&
              r.piston_count==1,
              "powered side wooden button starts east empty extension");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,13,78,8)==34 &&
              gm_world_meta(r.world,13,78,8)==5,
              "wood-button piston settles through same empty lifecycle");

        for(int y=77;y<=79;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,77,9,1,0);
        gm_world_set_block_meta(r.world,12,78,8,33,5);
        gm_world_set_block_meta(r.world,12,78,9,143,5);
        CHECK(gm_runtime_set_block(&r,12,78,9,143,5) &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==0 &&
              r.piston_count==0,
              "unpowered side wooden button does not start piston extension");

        {
            static const int plate_id[4]={70,72,147,148};
            static const int plate_power[4]={1,1,7,1};
            for(int plate=0;plate<4;++plate){
                for(int y=77;y<=79;++y)
                    for(int z=7;z<=10;++z)
                        for(int x=10;x<=15;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_world_set_block_meta(r.world,12,77,9,1,0);
                gm_world_set_block_meta(r.world,12,78,8,33,5);
                gm_world_set_block_meta(
                    r.world,12,78,9,plate_id[plate],0);
                CHECK(gm_runtime_set_block(
                          &r,12,78,9,plate_id[plate],plate_power[plate]) &&
                      gm_world_block(r.world,12,78,8)==33 &&
                      gm_world_meta(r.world,12,78,8)==13 &&
                      gm_world_block(r.world,13,78,8)==36 &&
                      gm_world_meta(r.world,13,78,8)==5 &&
                      r.piston_count==1,
                      "all four powered plate IDs start empty extension");
                gm_runtime_tick(&r,idle);
                gm_runtime_tick(&r,idle);
                gm_runtime_tick(&r,idle);
                CHECK(r.piston_count==0 &&
                      gm_world_block(r.world,13,78,8)==34 &&
                      gm_world_meta(r.world,13,78,8)==5,
                      "all four plate-powered pistons settle exact heads");

                for(int y=77;y<=79;++y)
                    for(int z=7;z<=10;++z)
                        for(int x=10;x<=15;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_world_set_block_meta(r.world,12,77,9,1,0);
                gm_world_set_block_meta(r.world,12,78,8,33,5);
                gm_world_set_block_meta(
                    r.world,12,78,9,plate_id[plate],0);
                CHECK(gm_runtime_set_block(
                          &r,12,78,9,plate_id[plate],0) &&
                      gm_world_block(r.world,12,78,8)==33 &&
                      gm_world_meta(r.world,12,78,8)==5 &&
                      gm_world_block(r.world,13,78,8)==0 &&
                      r.piston_count==0,
                      "zero-strength plates do not start piston extension");
            }
        }

        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,77,9,1,0);
        gm_world_set_block_meta(r.world,12,78,8,33,5);
        CHECK(gm_runtime_set_block(&r,12,78,9,76,5) &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              gm_world_meta(r.world,13,78,8)==5 &&
              r.piston_count==1,
              "directional side torch starts east empty extension");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,13,78,8)==34 &&
              gm_world_meta(r.world,13,78,8)==5,
              "torch-powered piston settles through same empty lifecycle");

        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,8,33,5);
        CHECK(gm_runtime_set_block(&r,12,79,8,76,5) &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block_light(r.world,12,78,8)==6 &&
              r.piston_count==0,
              "piston opacity zero admits exact wrong-face torch light 6");
        gm_world_set_block_meta(r.world,15,78,8,150,0);
        CHECK(gm_world_block_light(r.world,15,78,8)==9,
              "powered comparator emits exact registered block light 9");

        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,77,9,1,0);
        gm_world_set_block_meta(r.world,12,78,8,33,5);
        gm_world_set_block_meta(r.world,12,78,10,152,0);
        CHECK(gm_runtime_set_block(&r,12,78,9,94,0) &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              gm_world_meta(r.world,13,78,8)==5 &&
              r.piston_count==1,
              "directional powered repeater starts east empty extension");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,13,78,8)==34 &&
              gm_world_meta(r.world,13,78,8)==5,
              "repeater-powered piston settles through empty lifecycle");

        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,77,9,1,0);
        gm_world_set_block_meta(r.world,12,78,8,33,5);
        gm_world_set_block_meta(r.world,11,78,9,152,0);
        CHECK(gm_runtime_set_block(&r,12,78,9,94,1) &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==0 &&
              r.piston_count==0,
              "wrong-direction powered repeater leaves piston unextended");

        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,77,9,1,0);
        gm_world_set_block_meta(r.world,12,78,10,152,0);
        CHECK(gm_runtime_set_block(&r,12,78,9,150,0) &&
              gm_runtime_comparator_set_output(
                  &r,0,12,78,9,15) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              gm_world_meta(r.world,13,78,8)==5 &&
              r.piston_count==1,
              "directional comparator output starts east empty extension");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,13,78,8)==34 &&
              gm_world_meta(r.world,13,78,8)==5,
              "comparator-powered piston settles through empty lifecycle");

        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,77,9,1,0);
        gm_world_set_block_meta(r.world,11,78,9,152,0);
        CHECK(gm_runtime_set_block(&r,12,78,9,150,1) &&
              gm_runtime_comparator_set_output(
                  &r,0,12,78,9,15) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==0 &&
              r.piston_count==0,
              "wrong-direction comparator leaves piston unextended");

        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,77,9,1,0);
        gm_world_set_block_meta(r.world,12,78,10,152,0);
        CHECK(gm_runtime_set_block(&r,12,78,9,150,0) &&
              gm_runtime_comparator_set_output(
                  &r,0,12,78,9,0) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==0 &&
              r.piston_count==0 &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "zero-output powered comparator leaves piston unextended and "
              "schedules its input-change callback");
        {
            GmRuntimeScheduledTick pending;
            CHECK(gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                  pending.x==12&&pending.y==78&&pending.z==9 &&
                  pending.block==150 &&
                  pending.time==r.clock.total_time+2,
                  "piston-neighbor comparator callback retains exact due");
        }
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;

        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,8,33,5);
        gm_world_set_block_meta(r.world,12,78,9,218,3);
        CHECK(gm_runtime_set_block(&r,12,78,10,1,0) &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "south-watching observer schedules activation beside piston");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(gm_world_block(r.world,12,78,9)==218 &&
              gm_world_meta(r.world,12,78,9)==11 &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              gm_world_meta(r.world,13,78,8)==5 &&
              r.piston_count==1,
              "powered observer north output starts east piston extension");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==1 &&
              !r.pistons[0].extending &&
              r.pistons[0].source &&
              r.pistons[0].progress==1.0f &&
              gm_world_block(r.world,12,78,8)==36 &&
              gm_world_block(r.world,13,78,8)==0,
              "observer minimum pulse reverses the in-flight empty piston");
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==0,
              "observer-pulse retraction settles the unextended base");

        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,8,33,5);
        gm_world_set_block_meta(r.world,12,78,9,218,5);
        CHECK(gm_runtime_set_block(&r,13,78,9,1,0) &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "east-watching observer schedules rotated activation");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(gm_world_block(r.world,12,78,9)==218 &&
              gm_world_meta(r.world,12,78,9)==13 &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==0 &&
              r.piston_count==0,
              "rotated powered observer leaves piston unextended");

        for(int y=77;y<=80;++y)
            for(int z=7;z<=11;++z)
                for(int x=10;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,77,9,1,0);
        gm_world_set_block_meta(r.world,12,78,9,55,15);
        gm_world_set_block_meta(r.world,12,78,10,152,0);
        CHECK(gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              gm_world_meta(r.world,13,78,8)==5 &&
              r.piston_count==1,
              "directional powered wire starts east piston extension");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,13,78,8)==34 &&
              gm_world_meta(r.world,13,78,8)==5,
              "wire-powered piston settles through empty lifecycle");

        for(int y=77;y<=80;++y)
            for(int z=7;z<=11;++z)
                for(int x=10;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,77,9,1,0);
        gm_world_set_block_meta(r.world,13,77,9,1,0);
        gm_world_set_block_meta(r.world,12,78,9,55,14);
        gm_world_set_block_meta(r.world,13,78,9,55,15);
        gm_world_set_block_meta(r.world,14,78,9,152,0);
        CHECK(gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==0 &&
              r.piston_count==0,
              "wrong-direction powered wire leaves piston unextended");

        for(int y=77;y<=80;++y)
            for(int z=7;z<=11;++z)
                for(int x=10;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,1,0);
        gm_world_set_block_meta(r.world,12,79,9,55,15);
        gm_world_set_block_meta(r.world,11,79,9,152,0);
        CHECK(gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              gm_world_meta(r.world,13,78,8)==5 &&
              r.piston_count==1,
              "dust-strong-powered normal cube relays into piston");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,13,78,8)==34 &&
              gm_world_meta(r.world,13,78,8)==5,
              "indirectly powered piston settles through empty lifecycle");

        for(int y=77;y<=80;++y)
            for(int z=7;z<=11;++z)
                for(int x=10;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,1,0);
        gm_world_set_block_meta(r.world,12,77,10,1,0);
        gm_world_set_block_meta(r.world,13,77,10,1,0);
        gm_world_set_block_meta(r.world,12,78,10,55,14);
        gm_world_set_block_meta(r.world,13,78,10,55,15);
        gm_world_set_block_meta(r.world,14,78,10,152,0);
        CHECK(gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==0 &&
              r.piston_count==0,
              "wire not strongly facing normal cube leaves piston unextended");

        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,13,78,8,152,0);
        CHECK(gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==152 &&
              r.piston_count==0,
              "piston output-face power is excluded");

        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,79,9,152,0);
        CHECK(gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              gm_world_meta(r.world,13,78,8)==5 &&
              r.piston_count==1,
              "pos-up quasi-connectivity starts east piston extension");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,13,78,8)==34 &&
              gm_world_meta(r.world,13,78,8)==5,
              "quasi-connected piston settles through empty lifecycle");

        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,77,9,152,0);
        CHECK(gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==0 &&
              r.piston_count==0,
              "below-diagonal source is outside quasi-connectivity");

        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,78,8,1,0);
        CHECK(gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              gm_world_meta(r.world,13,78,8)==5 &&
              gm_world_block(r.world,14,78,8)==36 &&
              gm_world_meta(r.world,14,78,8)==5 &&
              r.piston_count==2,
              "east piston starts exact single-stone moving pair");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(gm_world_block(r.world,13,78,8)==36 &&
              gm_world_block(r.world,14,78,8)==36 &&
              r.piston_count==2,
              "single-stone push retains both moving blocks through progress");
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,13,78,8)==34 &&
              gm_world_meta(r.world,13,78,8)==5 &&
              gm_world_block(r.world,14,78,8)==1 &&
              gm_world_meta(r.world,14,78,8)==0,
              "single-stone push settles head and moved block exactly");

        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,78,8,5,2);
        CHECK(gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              gm_world_block(r.world,14,78,8)==36 &&
              r.piston_count==2,
              "normal-reaction birch planks start an exact moving pair");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,13,78,8)==34 &&
              gm_world_meta(r.world,13,78,8)==5 &&
              gm_world_block(r.world,14,78,8)==5 &&
              gm_world_meta(r.world,14,78,8)==2,
              "normal-reaction push preserves non-stone block metadata");

        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=17;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,78,8,1,0);
        gm_world_set_block_meta(r.world,14,78,8,1,0);
        CHECK(gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              gm_world_block(r.world,14,78,8)==36 &&
              gm_world_block(r.world,15,78,8)==36 &&
              r.piston_count==3,
              "two-stone line starts three exact moving tiles");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,13,78,8)==34 &&
              gm_world_meta(r.world,13,78,8)==5 &&
              gm_world_block(r.world,14,78,8)==1 &&
              gm_world_block(r.world,15,78,8)==1,
              "two-stone line settles head and both stones");

        for(int y=79;y<=81;++y)
            for(int z=13;z<=15;++z)
                for(int x=18;x<=35;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,20,80,15,152,0);
        for(int x=21;x<=32;++x)
            gm_world_set_block_meta(r.world,x,80,14,1,0);
        CHECK(gm_runtime_set_block(&r,20,80,14,33,5) &&
              gm_world_meta(r.world,20,80,14)==13 &&
              r.piston_count==13,
              "twelve-stone maximum starts head plus twelve moving tiles");
        for(int x=21;x<=33;++x)
            CHECK(gm_world_block(r.world,x,80,14)==36 &&
                  gm_world_meta(r.world,x,80,14)==5,
                  "maximum stone line exposes exact moving metadata");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,21,80,14)==34 &&
              gm_world_meta(r.world,21,80,14)==5,
              "maximum stone line settles its piston head");
        for(int x=22;x<=33;++x)
            CHECK(gm_world_block(r.world,x,80,14)==1 &&
                  gm_world_meta(r.world,x,80,14)==0,
                  "maximum stone line settles all twelve stones");

        for(int y=79;y<=81;++y)
            for(int z=13;z<=15;++z)
                for(int x=18;x<=35;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,20,80,15,152,0);
        for(int x=21;x<=33;++x)
            gm_world_set_block_meta(r.world,x,80,14,1,0);
        CHECK(gm_runtime_set_block(&r,20,80,14,33,5) &&
              gm_world_meta(r.world,20,80,14)==5 &&
              r.piston_count==0,
              "thirteen-stone line is rejected at vanilla push limit");
        for(int x=21;x<=33;++x)
            CHECK(gm_world_block(r.world,x,80,14)==1,
                  "rejected thirteen-stone line remains intact");

        memset(&r.entities,0,sizeof r.entities);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,1,0);
        gm_world_set_block_meta(r.world,13,78,8,37,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4242) &&
              gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x123456789ABC)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              r.piston_count==1 &&
              r.entities.n_active==1 &&
              r.entities.ents[0].eid==4242 &&
              r.entities.ents[0].item==37 &&
              r.entities.ents[0].count==1 &&
              r.entities.ents[0].meta==0 &&
              r.entities.ents[0].age==0 &&
              r.entities.ents[0].pickup_delay==10 &&
              r.next_entity_id==4243,
              "front dandelion is destroyed into an exact moving head and drop");
        gm_runtime_tick(&r,idle);
        CHECK(r.entities.ents[0].active &&
              r.entities.ents[0].age==1 &&
              r.entities.ents[0].pickup_delay==9 &&
              fabs(r.entities.ents[0].x-13.635)<1.0e-12,
              "piston-destroyed item ticks before the moving head sweeps it");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);

        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,1,0);
        gm_world_set_block_meta(r.world,13,78,8,38,2);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4343) &&
              gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x123456789ABC)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              r.piston_count==1 &&
              r.entities.n_active==1 &&
              r.entities.ents[0].eid==4343 &&
              r.entities.ents[0].item==38 &&
              r.entities.ents[0].count==1 &&
              r.entities.ents[0].meta==2 &&
              r.entities.ents[0].age==0 &&
              r.entities.ents[0].pickup_delay==10,
              "front allium preserves flower ID and metadata in piston drop");
        gm_runtime_tick(&r,idle);
        CHECK(r.entities.ents[0].active &&
              r.entities.ents[0].age==1 &&
              r.entities.ents[0].pickup_delay==9 &&
              fabs(r.entities.ents[0].x-13.635)<1.0e-12,
              "allium drop follows the exact moving-head sweep");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);

        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,1,0);
        gm_world_set_block_meta(r.world,13,78,8,50,5);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4444) &&
              gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x123456789ABC)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              r.piston_count==1 &&
              r.entities.n_active==1 &&
              r.entities.ents[0].eid==4444 &&
              r.entities.ents[0].item==50 &&
              r.entities.ents[0].count==1 &&
              r.entities.ents[0].meta==0 &&
              r.entities.ents[0].age==0 &&
              r.entities.ents[0].pickup_delay==10,
              "floor-torch piston drop strips block orientation metadata");
        gm_runtime_tick(&r,idle);
        CHECK(r.entities.ents[0].active &&
              r.entities.ents[0].age==1 &&
              r.entities.ents[0].pickup_delay==9 &&
              fabs(r.entities.ents[0].x-13.635)<1.0e-12,
              "floor-torch drop follows the exact moving-head sweep");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);

        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,1,0);
        gm_world_set_block_meta(r.world,13,78,8,55,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4545) &&
              gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x123456789ABC)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              r.piston_count==1 &&
              r.entities.n_active==1 &&
              r.entities.ents[0].eid==4545 &&
              r.entities.ents[0].item==331 &&
              r.entities.ents[0].count==1 &&
              r.entities.ents[0].meta==0 &&
              r.entities.ents[0].age==0 &&
              r.entities.ents[0].pickup_delay==10,
              "wire block piston drop maps to redstone item ID 331");
        gm_runtime_tick(&r,idle);
        CHECK(r.entities.ents[0].active &&
              r.entities.ents[0].age==1 &&
              r.entities.ents[0].pickup_delay==9 &&
              fabs(r.entities.ents[0].x-13.635)<1.0e-12,
              "redstone-item drop follows the exact moving-head sweep");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,8,33,5);
        gm_world_set_block_meta(r.world,13,78,8,1,0);
        gm_world_set_block_meta(r.world,14,77,8,1,0);
        gm_world_set_block_meta(r.world,14,78,8,51,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4646) &&
              gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x123456789ABC)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,9,152,0) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              gm_world_block(r.world,14,78,8)==36 &&
              r.piston_count==2 &&
              r.entities.n_active==0 &&
              r.next_entity_id==4646 &&
              r.world_random_seed48==UINT64_C(0x1902D9AECA17) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "terminal fire consumes no drop RNG and one piston-pitch draw");
        gm_runtime_tick(&r,idle);
        CHECK(r.entities.n_active==0 &&
              r.next_entity_id==4646,
              "zero-drop fire remains entity-free during the moving sweep");

        for(int fluid=8;fluid<=11;++fluid){
            for(int meta=0;meta<16;++meta){
                memset(&r.entities,0,sizeof r.entities);
                r.piston_count=0;
                memset(r.pistons,0,sizeof r.pistons);
                for(int y=77;y<=80;++y)
                    for(int z=7;z<=10;++z)
                        for(int x=10;x<=16;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_world_set_block_meta(r.world,12,78,9,152,0);
                gm_world_set_block_meta(r.world,13,78,8,fluid,meta);
                CHECK(gm_runtime_set_entity_id_cursor(&r,4650) &&
                      gm_runtime_set_world_random_seed48(
                          &r,UINT64_C(0x123456789ABC)) &&
                      gm_runtime_set_math_random_seed48(
                          &r,UINT64_C(0x0FEDCBA98765)) &&
                      gm_runtime_set_block(&r,12,78,8,33,5) &&
                      gm_world_meta(r.world,12,78,8)==13 &&
                      gm_world_block(r.world,13,78,8)==36 &&
                      r.piston_count==1 &&
                      r.entities.n_active==0 &&
                      r.next_entity_id==4650 &&
                      r.world_random_seed48
                          ==UINT64_C(0x1902D9AECA17) &&
                      r.math_random_seed48
                          ==UINT64_C(0x0FEDCBA98765),
                      "all water/lava metadata states are zero-drop "
                      "piston DESTROY states");
            }
        }

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,78,8,9,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4651) &&
              gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x123456789ABC)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              r.piston_count==1 &&
              r.entities.n_active==GM_LIVE_MAX &&
              r.next_entity_id==4651,
              "zero-drop fluid destruction does not require entity capacity");

        for(int meta=0;meta<=6;++meta){
            memset(&r.entities,0,sizeof r.entities);
            r.piston_count=0;
            memset(r.pistons,0,sizeof r.pistons);
            for(int y=77;y<=80;++y)
                for(int z=7;z<=10;++z)
                    for(int x=10;x<=16;++x)
                        gm_world_set_block_meta(r.world,x,y,z,0,0);
            gm_world_set_block_meta(r.world,12,78,9,152,0);
            gm_world_set_block_meta(r.world,13,78,8,92,meta);
            CHECK(gm_runtime_set_entity_id_cursor(&r,4652) &&
                  gm_runtime_set_world_random_seed48(
                      &r,UINT64_C(0x123456789ABC)) &&
                  gm_runtime_set_math_random_seed48(
                      &r,UINT64_C(0x0FEDCBA98765)) &&
                  gm_runtime_set_block(&r,12,78,8,33,5) &&
                  gm_world_meta(r.world,12,78,8)==13 &&
                  gm_world_block(r.world,13,78,8)==36 &&
                  r.piston_count==1 &&
                  r.entities.n_active==0 &&
                  r.next_entity_id==4652 &&
                  r.world_random_seed48==UINT64_C(0x1902D9AECA17) &&
                  r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
                  "all seven cake bite states are zero-drop piston "
                  "DESTROY states");
        }

        for(int meta=7;meta<16;++meta){
            memset(&r.entities,0,sizeof r.entities);
            r.piston_count=0;
            memset(r.pistons,0,sizeof r.pistons);
            for(int y=77;y<=80;++y)
                for(int z=7;z<=10;++z)
                    for(int x=10;x<=16;++x)
                        gm_world_set_block_meta(r.world,x,y,z,0,0);
            gm_world_set_block_meta(r.world,12,78,9,152,0);
            gm_world_set_block_meta(r.world,13,78,8,92,meta);
            CHECK(gm_runtime_set_entity_id_cursor(&r,4653) &&
                  gm_runtime_set_world_random_seed48(
                      &r,UINT64_C(0x123456789ABC)) &&
                  gm_runtime_set_math_random_seed48(
                      &r,UINT64_C(0x0FEDCBA98765)) &&
                  gm_runtime_set_block(&r,12,78,8,33,5) &&
                  gm_world_meta(r.world,12,78,8)==5 &&
                  gm_world_meta(r.world,13,78,8)==meta &&
                  r.piston_count==0 &&
                  r.entities.n_active==0 &&
                  r.next_entity_id==4653 &&
                  r.world_random_seed48==UINT64_C(0x123456789ABC) &&
                  r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
                  "noncanonical cake metadata remains rejected atomically");
        }

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,78,8,92,6);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4654) &&
              gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x123456789ABC)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              r.piston_count==1 &&
              r.entities.n_active==GM_LIVE_MAX &&
              r.next_entity_id==4654,
              "zero-drop cake destruction does not require entity capacity");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=8;x<=13;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,10,77,8,1,0);
        gm_world_set_block_meta(r.world,11,77,8,1,0);
        gm_world_set_block_meta(r.world,9,78,8,33,5);
        gm_world_set_block_meta(r.world,10,78,8,92,3);
        gm_world_set_block_meta(r.world,12,78,8,124,0);
        CHECK(gm_runtime_load_block(&r,11,78,8,149,9) &&
              gm_runtime_comparator_set_output(&r,0,11,78,8,8),
              "three-bite cake initializes a powered west comparator");
        long long cake_comparator_due=r.clock.total_time+2;
        CHECK(gm_runtime_set_entity_id_cursor(&r,4655) &&
              gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x123456789ABC)) &&
              gm_runtime_set_block(&r,9,78,9,152,0) &&
              gm_world_meta(r.world,9,78,8)==13 &&
              gm_world_block(r.world,10,78,8)==36 &&
              gm_world_meta(r.world,10,78,8)==5 &&
              r.piston_count==1 &&
              r.entities.n_active==0 &&
              r.next_entity_id==4655 &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "cake destruction queues its comparator while the head moves");
        GmRuntimeScheduledTick cake_tick;
        CHECK(gm_runtime_scheduled_tick_get(&r,0,&cake_tick) &&
              cake_tick.block==149 && cake_tick.x==11 &&
              cake_tick.y==78 && cake_tick.z==8 &&
              cake_tick.time==cake_comparator_due &&
              cake_tick.priority==0 &&
              gm_world_meta(r.world,11,78,8)==9,
              "cake comparator queues its exact +2 callback");
        gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_world_block(r.world,12,78,8)==124,
              "cake comparator callback remains pending for one tick");
        gm_runtime_tick(&r,idle);
        GmRuntimeComparator cake_comparator;
        int cake_comparator_found=0;
        for(int i=0;i<gm_runtime_comparator_count(&r);++i){
            GmRuntimeComparator candidate;
            if(gm_runtime_comparator_get(&r,i,&candidate) &&
                    candidate.x==11 && candidate.y==78 &&
                    candidate.z==8){
                cake_comparator=candidate;
                cake_comparator_found=1;
            }
        }
        CHECK(cake_comparator_found &&
              cake_comparator.output_signal==0 &&
              gm_world_block(r.world,11,78,8)==149 &&
              gm_world_meta(r.world,11,78,8)==1 &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&cake_tick) &&
              cake_tick.block==124 && cake_tick.x==12 &&
              cake_tick.y==78 && cake_tick.z==8 &&
              cake_tick.time==cake_comparator_due+4 &&
              cake_tick.priority==0,
              "cake analog 8 clears at +2 and queues lamp-off at +4");
        for(int tick=0;tick<4;++tick)
            gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,10,78,8)==34 &&
              gm_world_meta(r.world,10,78,8)==5 &&
              gm_world_block(r.world,12,78,8)==123 &&
              gm_runtime_scheduled_tick_count(&r)==0 &&
              r.entities.n_active==0 &&
              r.next_entity_id==4655,
              "cake piston settles and its lamp turns off without a drop");

        gm_world_set_block_meta(r.world,11,78,9,0,0);
        gm_world_set_block_meta(r.world,12,78,8,123,0);
        CHECK(gm_runtime_load_block(&r,11,78,8,149,9) &&
              gm_runtime_comparator_set_output(&r,0,11,78,8,8),
              "settled piston-head comparator initializes saved analog 8");
        long long head_comparator_due=r.clock.total_time+2;
        CHECK(gm_runtime_set_block(&r,11,78,9,152,0) &&
              gm_runtime_scheduled_tick_count(&r)==1 &&
              gm_runtime_scheduled_tick_get(&r,0,&cake_tick) &&
              cake_tick.block==149 && cake_tick.x==11 &&
              cake_tick.y==78 && cake_tick.z==8 &&
              cake_tick.time==head_comparator_due &&
              cake_tick.priority==0,
              "settled piston head admits the comparator +2 callback");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        cake_comparator_found=0;
        for(int i=0;i<gm_runtime_comparator_count(&r);++i){
            GmRuntimeComparator candidate;
            if(gm_runtime_comparator_get(&r,i,&candidate) &&
                    candidate.x==11 && candidate.y==78 &&
                    candidate.z==8){
                cake_comparator=candidate;
                cake_comparator_found=1;
            }
        }
        CHECK(cake_comparator_found &&
              cake_comparator.output_signal==0 &&
              gm_world_meta(r.world,11,78,8)==1 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "settled-head comparator clears at its exact +2 callback");
        CHECK(gm_world_block(r.world,10,78,8)==34 &&
              gm_world_meta(r.world,10,78,8)==5 &&
              gm_world_block(r.world,12,78,8)==123 &&
              gm_runtime_scheduled_tick_count(&r)==0 &&
              gm_runtime_load_block(&r,11,78,8,0,0),
              "settled-head circuit drains and removes its comparator tile");

        for(int meta=0;meta<16;++meta){
            int eid=4700+meta*8;
            memset(&r.entities,0,sizeof r.entities);
            r.piston_count=0;
            memset(r.pistons,0,sizeof r.pistons);
            for(int y=77;y<=80;++y)
                for(int z=7;z<=10;++z)
                    for(int x=10;x<=16;++x)
                        gm_world_set_block_meta(r.world,x,y,z,0,0);
            gm_world_set_block_meta(r.world,12,78,9,152,0);
            gm_world_set_block_meta(r.world,13,78,8,103,meta);
            CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                  gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
                  gm_runtime_set_math_random_seed48(
                      &r,UINT64_C(0x1D63F477164C)) &&
                  gm_runtime_set_block(&r,12,78,8,33,5) &&
                  gm_world_meta(r.world,12,78,8)==13 &&
                  gm_world_block(r.world,13,78,8)==36 &&
                  r.piston_count==1 && r.entities.n_active==3 &&
                  r.next_entity_id==eid+3 &&
                  r.world_random_seed48==UINT64_C(0x8E5C32BB7479) &&
                  r.math_random_seed48==UINT64_C(0x4F3CD54DF4A4),
                  "all sixteen melon metadata states consume the exact "
                  "three-drop piston RNG transition");
            for(int i=0;i<3;++i)
                CHECK(r.entities.ents[i].active &&
                      r.entities.ents[i].eid==eid+i &&
                      r.entities.ents[i].item==360 &&
                      r.entities.ents[i].count==1 &&
                      r.entities.ents[i].meta==0 &&
                      r.entities.ents[i].age==0 &&
                      r.entities.ents[i].pickup_delay==10,
                      "melon emits separate sequential item-360 stacks");
        }

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,78,8,103,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4900) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(1)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x71C1D9EFC4FE)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              r.piston_count==1 && r.entities.n_active==7 &&
              r.next_entity_id==4907 &&
              r.world_random_seed48==UINT64_C(0x2DC896B07C2E) &&
              r.math_random_seed48==UINT64_C(0xC99E60AE8A36),
              "melon seed-one boundary emits seven exact piston drops");
        for(int i=0;i<7;++i)
            CHECK(r.entities.ents[i].active &&
                  r.entities.ents[i].eid==4900+i &&
                  r.entities.ents[i].item==360 &&
                  r.entities.ents[i].count==1 &&
                  r.entities.ents[i].meta==0,
                  "seven-drop melon boundary retains sequential stack IDs");

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX-6;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX-6;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,78,8,103,15);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4910) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(1)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x71C1D9EFC4FE)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_meta(r.world,13,78,8)==15 &&
              r.piston_count==0 &&
              r.entities.n_active==GM_LIVE_MAX-6 &&
              r.next_entity_id==4910 &&
              r.world_random_seed48==UINT64_C(1) &&
              r.math_random_seed48==UINT64_C(0x71C1D9EFC4FE),
              "seven-drop melon rejects six free entity slots atomically");

        for(int stem=104;stem<=105;++stem){
            for(int age=0;age<=7;++age){
                int expected=age>=6?3:(age>=4?2:0);
                uint64_t expected_world=expected==3
                    ?UINT64_C(0xEE85F453C1E7)
                    :(expected==2?UINT64_C(0x86D91B38BCB3):UINT64_C(11));
                uint64_t expected_math=expected==3
                    ?UINT64_C(0x63BD8BBB501D)
                    :(expected==2?UINT64_C(0x6B1C94DF7835)
                                 :UINT64_C(0x0FEDCBA98765));
                int eid=5000+(stem-104)*64+age*4;
                memset(&r.entities,0,sizeof r.entities);
                r.piston_count=0;
                memset(r.pistons,0,sizeof r.pistons);
                for(int y=77;y<=80;++y)
                    for(int z=7;z<=10;++z)
                        for(int x=10;x<=16;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_world_set_block_meta(r.world,12,78,9,152,0);
                gm_world_set_block_meta(r.world,13,77,8,60,0);
                gm_world_set_block_meta(r.world,13,78,8,stem,age);
                CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                      gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
                      gm_runtime_set_math_random_seed48(
                          &r,UINT64_C(0x0FEDCBA98765)) &&
                      gm_runtime_set_block_random_seed48(&r,UINT64_C(15)) &&
                      gm_runtime_set_block(&r,12,78,8,33,5) &&
                      gm_world_meta(r.world,12,78,8)==13 &&
                      gm_world_block(r.world,13,77,8)==3 &&
                      gm_world_block(r.world,13,78,8)==36 &&
                      r.piston_count==1 &&
                      r.entities.n_active==expected &&
                      r.next_entity_id==eid+expected &&
                      r.block_random_seed48==UINT64_C(0x7F9B67EFDFD8) &&
                      r.world_random_seed48==expected_world &&
                      r.math_random_seed48==expected_math,
                      "both stem types consume three exact age-gated block "
                      "RNG trials and notify supporting farmland");
                for(int i=0;i<expected;++i)
                    CHECK(r.entities.ents[i].active &&
                          r.entities.ents[i].eid==eid+i &&
                          r.entities.ents[i].item==(stem==104?361:362) &&
                          r.entities.ents[i].count==1 &&
                          r.entities.ents[i].meta==0 &&
                          r.entities.ents[i].age==0 &&
                          r.entities.ents[i].pickup_delay==10,
                          "stem success trials emit separate exact seed stacks");
            }
        }

        for(int stem=104;stem<=105;++stem){
            int eid=5200+stem;
            memset(&r.entities,0,sizeof r.entities);
            r.piston_count=0;
            memset(r.pistons,0,sizeof r.pistons);
            for(int y=77;y<=80;++y)
                for(int z=7;z<=10;++z)
                    for(int x=10;x<=16;++x)
                        gm_world_set_block_meta(r.world,x,y,z,0,0);
            gm_world_set_block_meta(r.world,12,78,9,152,0);
            gm_world_set_block_meta(r.world,13,77,8,60,0);
            gm_world_set_block_meta(r.world,13,78,8,stem,0);
            CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                  gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
                  gm_runtime_set_math_random_seed48(
                      &r,UINT64_C(0x0FEDCBA98765)) &&
                  gm_runtime_set_block_random_seed48(&r,UINT64_C(0)) &&
                  gm_runtime_set_block(&r,12,78,8,33,5) &&
                  r.entities.n_active==1 &&
                  r.entities.ents[0].eid==eid &&
                  r.entities.ents[0].item==(stem==104?361:362) &&
                  r.next_entity_id==eid+1 &&
                  r.block_random_seed48==UINT64_C(0x0AA8544E593D) &&
                  r.world_random_seed48==UINT64_C(0x5D5692ACE2BF) &&
                  r.math_random_seed48==UINT64_C(0x33E01D26154D),
                  "age-zero stems retain the exact one-success boundary");
        }

        for(int stem=104;stem<=105;++stem){
            for(int meta=8;meta<16;++meta){
                memset(&r.entities,0,sizeof r.entities);
                r.piston_count=0;
                memset(r.pistons,0,sizeof r.pistons);
                for(int y=77;y<=80;++y)
                    for(int z=7;z<=10;++z)
                        for(int x=10;x<=16;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_world_set_block_meta(r.world,12,78,9,152,0);
                gm_world_set_block_meta(r.world,13,77,8,60,0);
                gm_world_set_block_meta(r.world,13,78,8,stem,meta);
                CHECK(gm_runtime_set_entity_id_cursor(&r,5300) &&
                      gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
                      gm_runtime_set_math_random_seed48(
                          &r,UINT64_C(0x0FEDCBA98765)) &&
                      gm_runtime_set_block_random_seed48(&r,UINT64_C(15)) &&
                      gm_runtime_set_block(&r,12,78,8,33,5) &&
                      gm_world_meta(r.world,12,78,8)==5 &&
                      gm_world_block(r.world,13,77,8)==60 &&
                      gm_world_block(r.world,13,78,8)==stem &&
                      gm_world_meta(r.world,13,78,8)==meta &&
                      r.piston_count==0 && r.entities.n_active==0 &&
                      r.next_entity_id==5300 &&
                      r.block_random_seed48==UINT64_C(15) &&
                      r.world_random_seed48==UINT64_C(0) &&
                      r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
                      "noncanonical stem metadata rejects without partial state");
            }
        }

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX-2;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX-2;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,60,0);
        gm_world_set_block_meta(r.world,13,78,8,105,7);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5400) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block_random_seed48(&r,UINT64_C(15)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,77,8)==60 &&
              gm_world_block(r.world,13,78,8)==105 &&
              gm_world_meta(r.world,13,78,8)==7 &&
              r.piston_count==0 &&
              r.entities.n_active==GM_LIVE_MAX-2 &&
              r.next_entity_id==5400 &&
              r.block_random_seed48==UINT64_C(15) &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "three-drop stem rejects two free entity slots atomically");

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,60,0);
        gm_world_set_block_meta(r.world,13,78,8,104,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5401) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block_random_seed48(&r,UINT64_C(1)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,77,8)==3 &&
              gm_world_block(r.world,13,78,8)==36 &&
              r.piston_count==1 && r.entities.n_active==GM_LIVE_MAX &&
              r.next_entity_id==5401 &&
              r.block_random_seed48==UINT64_C(0xDF4111591DF2) &&
              r.world_random_seed48==UINT64_C(11) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "zero-drop stem succeeds with a completely full entity pool");

        for(int meta=0;meta<16;++meta){
            memset(&r.entities,0,sizeof r.entities);
            r.piston_count=0;
            memset(r.pistons,0,sizeof r.pistons);
            for(int y=77;y<=80;++y)
                for(int z=7;z<=10;++z)
                    for(int x=10;x<=16;++x)
                        gm_world_set_block_meta(r.world,x,y,z,0,0);
            gm_world_set_block_meta(r.world,12,78,9,152,0);
            gm_world_set_block_meta(r.world,13,78,9,1,0);
            gm_world_set_block_meta(r.world,13,78,8,106,meta);
            CHECK(gm_runtime_set_entity_id_cursor(&r,5500+meta) &&
                  gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
                  gm_runtime_set_math_random_seed48(
                      &r,UINT64_C(0x0FEDCBA98765)) &&
                  gm_runtime_set_block(&r,12,78,8,33,5) &&
                  gm_world_meta(r.world,12,78,8)==13 &&
                  gm_world_block(r.world,13,78,8)==36 &&
                  gm_world_block(r.world,13,78,9)==1 &&
                  r.piston_count==1 && r.entities.n_active==0 &&
                  r.next_entity_id==5500+meta &&
                  r.world_random_seed48==UINT64_C(11) &&
                  r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
                  "all sixteen vine attachment masks are exact zero-drop "
                  "piston states");
        }

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,78,9,1,0);
        gm_world_set_block_meta(r.world,13,78,8,106,15);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5520) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              r.piston_count==1 && r.entities.n_active==GM_LIVE_MAX &&
              r.next_entity_id==5520 &&
              r.world_random_seed48==UINT64_C(11) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "zero-drop vine succeeds with a completely full entity pool");

        for(int meta=0;meta<16;++meta){
            int eid=5530+meta;
            memset(&r.entities,0,sizeof r.entities);
            r.piston_count=0;
            memset(r.pistons,0,sizeof r.pistons);
            r.scheduled_tick_count=0;
            r.scheduled_tick_next_order=0;
            for(int y=77;y<=80;++y)
                for(int z=7;z<=10;++z)
                    for(int x=10;x<=16;++x)
                        gm_world_set_block_meta(r.world,x,y,z,0,0);
            gm_world_set_block_meta(r.world,12,78,9,152,0);
            gm_world_set_block_meta(r.world,13,77,8,9,0);
            gm_world_set_block_meta(r.world,13,78,8,111,meta);
            CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                  gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
                  gm_runtime_set_math_random_seed48(
                      &r,UINT64_C(0x0FEDCBA98765)) &&
                  gm_runtime_set_block(&r,12,78,8,33,5) &&
                  gm_world_meta(r.world,12,78,8)==13 &&
                  gm_world_block(r.world,13,77,8)==9 &&
                  gm_world_meta(r.world,13,77,8)==0 &&
                  gm_world_block(r.world,13,78,8)==36 &&
                  r.piston_count==1 && r.entities.n_active==1 &&
                  r.entities.ents[0].eid==eid &&
                  r.entities.ents[0].item==111 &&
                  r.entities.ents[0].count==1 &&
                  r.entities.ents[0].meta==0 &&
                  r.entities.ents[0].age==0 &&
                  r.entities.ents[0].pickup_delay==10 &&
                  r.next_entity_id==eid+1 &&
                  r.world_random_seed48==UINT64_C(0x5D5692ACE2BF) &&
                  r.math_random_seed48==UINT64_C(0x33E01D26154D) &&
                  gm_runtime_scheduled_tick_count(&r)==0,
                  "all waterlily metadata states emit one exact item over "
                  "unchanged source water");
        }

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,9,0);
        gm_world_set_block_meta(r.world,13,78,8,111,15);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5560) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,77,8)==9 &&
              gm_world_block(r.world,13,78,8)==111 &&
              gm_world_meta(r.world,13,78,8)==15 &&
              r.piston_count==0 && r.entities.n_active==GM_LIVE_MAX &&
              r.next_entity_id==5560 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "waterlily rejects a full entity pool without partial state");

        for(int age=0;age<=3;++age){
            int expected=age==3?2:1;
            int eid=5580+age*4;
            memset(&r.entities,0,sizeof r.entities);
            r.piston_count=0;
            memset(r.pistons,0,sizeof r.pistons);
            for(int y=77;y<=80;++y)
                for(int z=7;z<=10;++z)
                    for(int x=10;x<=16;++x)
                        gm_world_set_block_meta(r.world,x,y,z,0,0);
            gm_world_set_block_meta(r.world,12,78,9,152,0);
            gm_world_set_block_meta(r.world,13,77,8,88,0);
            gm_world_set_block_meta(r.world,13,78,8,115,age);
            CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                  gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
                  gm_runtime_set_math_random_seed48(
                      &r,UINT64_C(0x0FEDCBA98765)) &&
                  gm_runtime_set_block(&r,12,78,8,33,5) &&
                  gm_world_meta(r.world,12,78,8)==13 &&
                  gm_world_block(r.world,13,77,8)==88 &&
                  gm_world_block(r.world,13,78,8)==36 &&
                  r.piston_count==1 &&
                  r.entities.n_active==expected &&
                  r.next_entity_id==eid+expected &&
                  r.world_random_seed48==(expected==1
                      ?UINT64_C(0x5D5692ACE2BF)
                      :UINT64_C(0x7455BFB52A42)) &&
                  r.math_random_seed48==(expected==1
                      ?UINT64_C(0x33E01D26154D)
                      :UINT64_C(0x6B1C94DF7835)),
                  "all four nether-wart ages consume exact count and drop RNG");
            for(int i=0;i<expected;++i)
                CHECK(r.entities.ents[i].active &&
                      r.entities.ents[i].eid==eid+i &&
                      r.entities.ents[i].item==372 &&
                      r.entities.ents[i].count==1 &&
                      r.entities.ents[i].meta==0 &&
                      r.entities.ents[i].age==0 &&
                      r.entities.ents[i].pickup_delay==10,
                      "nether wart emits separate exact item-372 stacks");
        }

        for(int meta=4;meta<16;++meta){
            memset(&r.entities,0,sizeof r.entities);
            r.piston_count=0;
            memset(r.pistons,0,sizeof r.pistons);
            for(int y=77;y<=80;++y)
                for(int z=7;z<=10;++z)
                    for(int x=10;x<=16;++x)
                        gm_world_set_block_meta(r.world,x,y,z,0,0);
            gm_world_set_block_meta(r.world,12,78,9,152,0);
            gm_world_set_block_meta(r.world,13,77,8,88,0);
            gm_world_set_block_meta(r.world,13,78,8,115,meta);
            CHECK(gm_runtime_set_entity_id_cursor(&r,5600) &&
                  gm_runtime_set_world_random_seed48(&r,UINT64_C(1)) &&
                  gm_runtime_set_math_random_seed48(
                      &r,UINT64_C(0x0FEDCBA98765)) &&
                  gm_runtime_set_block(&r,12,78,8,33,5) &&
                  gm_world_meta(r.world,12,78,8)==5 &&
                  gm_world_block(r.world,13,77,8)==88 &&
                  gm_world_block(r.world,13,78,8)==115 &&
                  gm_world_meta(r.world,13,78,8)==meta &&
                  r.piston_count==0 && r.entities.n_active==0 &&
                  r.next_entity_id==5600 &&
                  r.world_random_seed48==UINT64_C(1) &&
                  r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
                  "noncanonical nether-wart metadata rejects atomically");
        }

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,88,0);
        gm_world_set_block_meta(r.world,13,78,8,115,3);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5620) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(1)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              r.piston_count==1 && r.entities.n_active==4 &&
              r.next_entity_id==5624 &&
              r.world_random_seed48==UINT64_C(0x218C3DC9CD73) &&
              r.math_random_seed48==UINT64_C(0x63EF35B33D05),
              "mature nether wart seed-one boundary emits four exact drops");

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX-3;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX-3;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,88,0);
        gm_world_set_block_meta(r.world,13,78,8,115,3);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5630) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(1)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,77,8)==88 &&
              gm_world_block(r.world,13,78,8)==115 &&
              gm_world_meta(r.world,13,78,8)==3 &&
              r.piston_count==0 &&
              r.entities.n_active==GM_LIVE_MAX-3 &&
              r.next_entity_id==5630 &&
              r.world_random_seed48==UINT64_C(1) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "four-drop nether wart rejects three free slots atomically");

        for(int meta=0;meta<16;++meta){
            int eid=5650+meta;
            memset(&r.entities,0,sizeof r.entities);
            r.piston_count=0;
            memset(r.pistons,0,sizeof r.pistons);
            r.scheduled_tick_count=0;
            r.scheduled_tick_next_order=0;
            for(int y=77;y<=80;++y)
                for(int z=7;z<=10;++z)
                    for(int x=10;x<=16;++x)
                        gm_world_set_block_meta(r.world,x,y,z,0,0);
            gm_world_set_block_meta(r.world,12,78,9,152,0);
            gm_world_set_block_meta(r.world,13,77,8,1,0);
            gm_world_set_block_meta(r.world,13,78,8,122,meta);
            CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                  gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
                  gm_runtime_set_math_random_seed48(
                      &r,UINT64_C(0x0FEDCBA98765)) &&
                  gm_runtime_set_block(&r,12,78,8,33,5) &&
                  gm_world_meta(r.world,12,78,8)==13 &&
                  gm_world_block(r.world,13,77,8)==1 &&
                  gm_world_block(r.world,13,78,8)==36 &&
                  r.piston_count==1 && r.entities.n_active==1 &&
                  r.entities.ents[0].eid==eid &&
                  r.entities.ents[0].item==122 &&
                  r.entities.ents[0].count==1 &&
                  r.entities.ents[0].meta==0 &&
                  r.entities.ents[0].age==0 &&
                  r.entities.ents[0].pickup_delay==10 &&
                  r.next_entity_id==eid+1 &&
                  r.world_random_seed48==UINT64_C(0x5D5692ACE2BF) &&
                  r.math_random_seed48==UINT64_C(0x33E01D26154D) &&
                  gm_runtime_scheduled_tick_count(&r)==0,
                  "all dragon-egg raw metadata states normalize to one exact "
                  "item 122:0");
        }

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,1,0);
        gm_world_set_block_meta(r.world,13,78,8,122,15);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5680) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,77,8)==1 &&
              gm_world_block(r.world,13,78,8)==122 &&
              gm_world_meta(r.world,13,78,8)==15 &&
              r.piston_count==0 && r.entities.n_active==GM_LIVE_MAX &&
              r.next_entity_id==5680 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "dragon egg rejects a full entity pool without partial state");

        for(int meta=0;meta<12;++meta){
            int expected=meta>=8?3:1;
            int eid=5700+meta*4;
            memset(&r.entities,0,sizeof r.entities);
            r.piston_count=0;
            memset(r.pistons,0,sizeof r.pistons);
            r.scheduled_tick_count=0;
            r.scheduled_tick_next_order=0;
            for(int y=77;y<=80;++y)
                for(int z=7;z<=10;++z)
                    for(int x=10;x<=16;++x)
                        gm_world_set_block_meta(r.world,x,y,z,0,0);
            gm_world_set_block_meta(r.world,12,78,9,152,0);
            gm_world_set_block_meta(r.world,13,78,9,17,3);
            gm_world_set_block_meta(r.world,13,78,8,127,meta);
            CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                  gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
                  gm_runtime_set_math_random_seed48(
                      &r,UINT64_C(0x0FEDCBA98765)) &&
                  gm_runtime_set_block(&r,12,78,8,33,5) &&
                  gm_world_meta(r.world,12,78,8)==13 &&
                  gm_world_block(r.world,13,78,9)==17 &&
                  gm_world_meta(r.world,13,78,9)==3 &&
                  gm_world_block(r.world,13,78,8)==36 &&
                  r.piston_count==1 &&
                  r.entities.n_active==expected &&
                  r.next_entity_id==eid+expected &&
                  r.world_random_seed48==(expected==1
                      ?UINT64_C(0x5D5692ACE2BF)
                      :UINT64_C(0xEE85F453C1E7)) &&
                  r.math_random_seed48==(expected==1
                      ?UINT64_C(0x33E01D26154D)
                      :UINT64_C(0x63BD8BBB501D)) &&
                  gm_runtime_scheduled_tick_count(&r)==0,
                  "all twelve cocoa facing/age states consume exact fixed "
                  "drop RNG");
            for(int i=0;i<expected;++i)
                CHECK(r.entities.ents[i].active &&
                      r.entities.ents[i].eid==eid+i &&
                      r.entities.ents[i].item==351 &&
                      r.entities.ents[i].count==1 &&
                      r.entities.ents[i].meta==3 &&
                      r.entities.ents[i].age==0 &&
                      r.entities.ents[i].pickup_delay==10,
                      "cocoa emits separate exact brown-dye item stacks");
        }

        for(int meta=12;meta<16;++meta){
            memset(&r.entities,0,sizeof r.entities);
            r.piston_count=0;
            memset(r.pistons,0,sizeof r.pistons);
            for(int y=77;y<=80;++y)
                for(int z=7;z<=10;++z)
                    for(int x=10;x<=16;++x)
                        gm_world_set_block_meta(r.world,x,y,z,0,0);
            gm_world_set_block_meta(r.world,12,78,9,152,0);
            gm_world_set_block_meta(r.world,13,78,9,17,3);
            gm_world_set_block_meta(r.world,13,78,8,127,meta);
            CHECK(gm_runtime_set_entity_id_cursor(&r,5750) &&
                  gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
                  gm_runtime_set_math_random_seed48(
                      &r,UINT64_C(0x0FEDCBA98765)) &&
                  gm_runtime_set_block(&r,12,78,8,33,5) &&
                  gm_world_meta(r.world,12,78,8)==5 &&
                  gm_world_block(r.world,13,78,9)==17 &&
                  gm_world_block(r.world,13,78,8)==127 &&
                  gm_world_meta(r.world,13,78,8)==meta &&
                  r.piston_count==0 && r.entities.n_active==0 &&
                  r.next_entity_id==5750 &&
                  r.world_random_seed48==UINT64_C(0) &&
                  r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
                  "noncanonical cocoa age-three metadata rejects atomically");
        }

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX-2;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX-2;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,78,9,17,3);
        gm_world_set_block_meta(r.world,13,78,8,127,8);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5760) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,9)==17 &&
              gm_world_block(r.world,13,78,8)==127 &&
              gm_world_meta(r.world,13,78,8)==8 &&
              r.piston_count==0 &&
              r.entities.n_active==GM_LIVE_MAX-2 &&
              r.next_entity_id==5760 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "three-drop cocoa rejects two free slots without partial state");

        {
            static const int support_dx[4]={0,1,0,-1};
            static const int support_dz[4]={-1,0,1,0};
            for(int meta=0;meta<16;++meta){
                int eid=5780+meta;
                int facing=meta&3;
                int detach=(meta&4)!=0&&(meta&8)==0;
                memset(&r.entities,0,sizeof r.entities);
                r.piston_count=0;
                memset(r.pistons,0,sizeof r.pistons);
                r.scheduled_tick_count=0;
                r.scheduled_tick_next_order=0;
                for(int y=77;y<=80;++y)
                    for(int z=6;z<=10;++z)
                        for(int x=10;x<=16;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_world_set_block_meta(r.world,12,78,9,152,0);
                if(facing!=3)
                    gm_world_set_block_meta(
                        r.world,13+support_dx[facing],78,
                        8+support_dz[facing],1,0);
                gm_world_set_block_meta(r.world,13,78,8,131,meta);
                CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                      gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
                      gm_runtime_set_math_random_seed48(
                          &r,UINT64_C(0x0FEDCBA98765)) &&
                      gm_runtime_set_block(&r,12,78,8,33,5) &&
                      gm_world_meta(r.world,12,78,8)==13 &&
                      gm_world_block(r.world,13,78,8)==36 &&
                      r.piston_count==1 && r.entities.n_active==1 &&
                      r.entities.ents[0].eid==eid &&
                      r.entities.ents[0].item==131 &&
                      r.entities.ents[0].count==1 &&
                      r.entities.ents[0].meta==0 &&
                      r.entities.ents[0].age==0 &&
                      r.entities.ents[0].pickup_delay==10 &&
                      r.next_entity_id==eid+1 &&
                      r.world_random_seed48==java_lcg_steps(0,detach?6:5) &&
                      r.math_random_seed48==UINT64_C(0x33E01D26154D) &&
                      gm_runtime_scheduled_tick_count(&r)==0,
                      "all tripwire-hook states emit item 131:0 with exact "
                      "isolated detach RNG");
            }
        }

        {
            static const int canonical_meta[8]={0,1,4,5,8,9,12,13};
            for(int index=0;index<8;++index){
                int meta=canonical_meta[index];
                int eid=5800+index;
                memset(&r.entities,0,sizeof r.entities);
                r.piston_count=0;
                memset(r.pistons,0,sizeof r.pistons);
                r.scheduled_tick_count=0;
                r.scheduled_tick_next_order=0;
                for(int y=77;y<=80;++y)
                    for(int z=7;z<=10;++z)
                        for(int x=10;x<=16;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_world_set_block_meta(r.world,12,78,9,152,0);
                gm_world_set_block_meta(r.world,13,78,8,132,meta);
                CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                      gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
                      gm_runtime_set_math_random_seed48(
                          &r,UINT64_C(0x0FEDCBA98765)) &&
                      gm_runtime_set_block(&r,12,78,8,33,5) &&
                      gm_world_meta(r.world,12,78,8)==13 &&
                      gm_world_block(r.world,13,78,8)==36 &&
                      r.piston_count==1 && r.entities.n_active==1 &&
                      r.entities.ents[0].eid==eid &&
                      r.entities.ents[0].item==287 &&
                      r.entities.ents[0].count==1 &&
                      r.entities.ents[0].meta==0 &&
                      r.entities.ents[0].age==0 &&
                      r.entities.ents[0].pickup_delay==10 &&
                      r.next_entity_id==eid+1 &&
                      r.world_random_seed48==UINT64_C(0x5D5692ACE2BF) &&
                      r.math_random_seed48==UINT64_C(0x33E01D26154D) &&
                      gm_runtime_scheduled_tick_count(&r)==0,
                      "all canonical tripwire states emit one exact string");
            }
            for(int meta=2;meta<16;meta+=4)
                for(int extra=0;extra<2;++extra){
                    int invalid=meta+extra;
                    memset(&r.entities,0,sizeof r.entities);
                    r.piston_count=0;
                    memset(r.pistons,0,sizeof r.pistons);
                    for(int y=77;y<=80;++y)
                        for(int z=7;z<=10;++z)
                            for(int x=10;x<=16;++x)
                                gm_world_set_block_meta(r.world,x,y,z,0,0);
                    gm_world_set_block_meta(r.world,12,78,9,152,0);
                    gm_world_set_block_meta(r.world,13,78,8,132,invalid);
                    CHECK(gm_runtime_set_entity_id_cursor(&r,5820) &&
                          gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
                          gm_runtime_set_math_random_seed48(
                              &r,UINT64_C(0x0FEDCBA98765)) &&
                          gm_runtime_set_block(&r,12,78,8,33,5) &&
                          gm_world_meta(r.world,12,78,8)==5 &&
                          gm_world_block(r.world,13,78,8)==132 &&
                          gm_world_meta(r.world,13,78,8)==invalid &&
                          r.piston_count==0 && r.entities.n_active==0 &&
                          r.next_entity_id==5820 &&
                          r.world_random_seed48==UINT64_C(0) &&
                          r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
                          "noncanonical tripwire raw metadata rejects atomically");
                }
        }

        for(int block=131;block<=132;++block){
            memset(&r.entities,0,sizeof r.entities);
            for(int i=0;i<GM_LIVE_MAX;++i)
                r.entities.ents[i].active=1;
            r.entities.n_active=GM_LIVE_MAX;
            r.piston_count=0;
            memset(r.pistons,0,sizeof r.pistons);
            for(int y=77;y<=80;++y)
                for(int z=7;z<=10;++z)
                    for(int x=10;x<=16;++x)
                        gm_world_set_block_meta(r.world,x,y,z,0,0);
            gm_world_set_block_meta(r.world,12,78,9,152,0);
            gm_world_set_block_meta(r.world,13,78,8,block,block==131?3:0);
            CHECK(gm_runtime_set_entity_id_cursor(&r,5830) &&
                  gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
                  gm_runtime_set_math_random_seed48(
                      &r,UINT64_C(0x0FEDCBA98765)) &&
                  gm_runtime_set_block(&r,12,78,8,33,5) &&
                  gm_world_meta(r.world,12,78,8)==5 &&
                  gm_world_block(r.world,13,78,8)==block &&
                  r.piston_count==0 && r.entities.n_active==GM_LIVE_MAX &&
                  r.next_entity_id==5830 &&
                  r.world_random_seed48==UINT64_C(0) &&
                  r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
                  "tripwire payload rejects a full item pool atomically");
        }

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;
        for(int y=77;y<=80;++y)
            for(int z=6;z<=10;++z)
                for(int x=8;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,9,78,7,152,0);
        gm_world_set_block_meta(r.world,9,78,8,1,0);
        gm_world_set_block_meta(r.world,10,78,8,131,7);
        gm_world_set_block_meta(r.world,11,78,8,132,4);
        gm_world_set_block_meta(r.world,12,78,8,132,4);
        gm_world_set_block_meta(r.world,13,78,8,132,4);
        gm_world_set_block_meta(r.world,14,78,8,131,5);
        gm_world_set_block_meta(r.world,15,78,8,1,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5840) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,10,78,7,33,3),
              "attached hook break operation succeeds");
        CHECK(gm_world_meta(r.world,10,78,7)==11 &&
              gm_world_block(r.world,10,78,8)==36 &&
              gm_world_meta(r.world,10,78,8)==3 &&
              gm_world_meta(r.world,11,78,8)==0 &&
              gm_world_meta(r.world,12,78,8)==0 &&
              gm_world_meta(r.world,13,78,8)==0 &&
              gm_world_block(r.world,14,78,8)==131 &&
              gm_world_meta(r.world,14,78,8)==1 &&
              gm_world_block(r.world,15,78,8)==1 &&
              r.piston_count==1,
              "attached hook break preserves exact line and moving state");
        CHECK(r.entities.n_active==1 &&
              r.entities.ents[0].eid==5840 &&
              r.entities.ents[0].item==131 &&
              r.entities.ents[0].meta==0 &&
              r.next_entity_id==5841,
              "attached hook break emits exact hook item");
        CHECK(r.world_random_seed48==java_lcg_steps(0,7),
              "attached hook break consumes exact World RNG");
        CHECK(r.math_random_seed48==UINT64_C(0x33E01D26154D),
              "attached hook break consumes exact Math RNG");
        CHECK(gm_runtime_scheduled_tick_count(&r)==0,
              "attached hook break leaves no callback");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;
        for(int y=77;y<=80;++y)
            for(int z=6;z<=10;++z)
                for(int x=8;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,11,78,7,152,0);
        gm_world_set_block_meta(r.world,9,78,8,1,0);
        gm_world_set_block_meta(r.world,10,78,8,131,7);
        gm_world_set_block_meta(r.world,11,78,8,132,4);
        gm_world_set_block_meta(r.world,12,78,8,132,4);
        gm_world_set_block_meta(r.world,13,78,8,132,4);
        gm_world_set_block_meta(r.world,14,78,8,131,5);
        gm_world_set_block_meta(r.world,15,78,8,1,0);
        {
            long long due=r.clock.total_time+10;
            GmRuntimeScheduledTick pending;
            CHECK(gm_runtime_set_entity_id_cursor(&r,5850) &&
                  gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
                  gm_runtime_set_math_random_seed48(
                      &r,UINT64_C(0x0FEDCBA98765)) &&
                  gm_runtime_set_block(&r,12,78,7,33,3) &&
                  gm_world_meta(r.world,12,78,7)==11 &&
                  gm_world_block(r.world,12,78,8)==36 &&
                  gm_world_meta(r.world,12,78,8)==3 &&
                  gm_world_meta(r.world,10,78,8)==15 &&
                  gm_world_meta(r.world,11,78,8)==4 &&
                  gm_world_meta(r.world,13,78,8)==4 &&
                  gm_world_meta(r.world,14,78,8)==13 &&
                  r.piston_count==1 && r.entities.n_active==1 &&
                  r.entities.ents[0].eid==5850 &&
                  r.entities.ents[0].item==287 &&
                  r.entities.ents[0].meta==0 &&
                  r.next_entity_id==5851 &&
                  r.world_random_seed48==java_lcg_steps(0,5) &&
                  r.math_random_seed48==UINT64_C(0x33E01D26154D) &&
                  gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                  pending.x==10 && pending.y==78 && pending.z==8 &&
                  pending.block==131 && pending.time==due &&
                  pending.priority==0 && pending.order==0,
                  "armed tripwire break powers both hooks and schedules "
                  "the exact west-hook recheck");
            for(int tick=0;tick<9;++tick)
                gm_runtime_tick(&r,idle);
            CHECK(gm_world_meta(r.world,10,78,8)==15 &&
                  gm_world_meta(r.world,14,78,8)==13 &&
                  gm_runtime_scheduled_tick_count(&r)==1,
                  "broken tripwire keeps both hooks powered through +9");
            gm_runtime_tick(&r,idle);
            CHECK(gm_world_block(r.world,12,78,8)==34 &&
                  gm_world_meta(r.world,12,78,8)==3 &&
                  gm_world_meta(r.world,10,78,8)==3 &&
                  gm_world_meta(r.world,11,78,8)==0 &&
                  gm_world_meta(r.world,13,78,8)==0 &&
                  gm_world_meta(r.world,14,78,8)==1 &&
                  gm_runtime_scheduled_tick_count(&r)==0 &&
                  r.world_random_seed48==java_lcg_steps(0,5),
                  "west hook rechecks the broken line at +10 and detaches "
                  "both remaining segments plus the far hook");
        }

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;
        for(int y=77;y<=80;++y)
            for(int z=6;z<=10;++z)
                for(int x=8;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,9,78,8,1,0);
        gm_world_set_block_meta(r.world,10,78,8,131,7);
        gm_world_set_block_meta(r.world,11,78,8,132,4);
        gm_world_set_block_meta(r.world,12,78,8,132,4);
        gm_world_set_block_meta(r.world,13,78,8,132,4);
        gm_world_set_block_meta(r.world,14,78,8,131,5);
        gm_world_set_block_meta(r.world,15,78,8,1,0);
        gm_world_set_block_meta(r.world,10,78,7,123,0);
        gm_world_set_block_meta(r.world,14,78,7,123,0);
        isr_init(&r.player.inv);
        r.player.inv.current_item=0;
        isr_set_stack(&r.player.inv,0,ic_mk(359,1,0));
        {
            long long due=r.clock.total_time+10;
            float exhaustion_before=r.vitals.exhaustion;
            GmRuntimeScheduledTick pending;
            CHECK(gm_runtime_set_entity_id_cursor(&r,5860) &&
                  gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
                  gm_runtime_set_math_random_seed48(
                      &r,UINT64_C(0x0FEDCBA98765)) &&
                  gm_runtime_harvest_block(&r,12,78,8) &&
                  gm_world_meta(r.world,10,78,8)==3 &&
                  gm_world_meta(r.world,11,78,8)==0 &&
                  gm_world_block(r.world,12,78,8)==0 &&
                  gm_world_meta(r.world,13,78,8)==0 &&
                  gm_world_meta(r.world,14,78,8)==1 &&
                  gm_world_block(r.world,10,78,7)==123 &&
                  gm_world_block(r.world,14,78,7)==123 &&
                  gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                  pending.x==10 && pending.y==78 && pending.z==8 &&
                  pending.block==131 && pending.time==due &&
                  pending.priority==0 && pending.order==0,
                  "shears disarm attached tripwire without an alarm pulse");
            ICStack shears=isr_get_stack(&r.player.inv,0);
            CHECK(shears.item==359 && shears.count==1 && shears.meta==1 &&
                  fabsf(r.vitals.exhaustion-
                        (exhaustion_before+0.005f))<1e-7f,
                  "tripwire harvest damages shears and charges exhaustion");
            CHECK(r.entities.n_active==1 &&
                  r.entities.ents[0].eid==5860 &&
                  r.entities.ents[0].item==287 &&
                  r.entities.ents[0].count==1 &&
                  r.entities.ents[0].meta==0 &&
                  r.entities.ents[0].age==0 &&
                  r.entities.ents[0].pickup_delay==10 &&
                  r.next_entity_id==5861 &&
                  r.world_random_seed48==java_lcg_steps(0,6) &&
                  r.math_random_seed48==UINT64_C(0x33E01D26154D),
                  "tripwire harvest emits exact string entity and RNG cursors");
        }
        isr_init(&r.player.inv);

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;
        for(int y=77;y<=80;++y)
            for(int z=6;z<=10;++z)
                for(int x=8;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,8,77,8,1,0);
        gm_world_set_block_meta(r.world,9,78,8,1,0);
        gm_world_set_block_meta(r.world,10,78,8,131,3);
        gm_world_set_block_meta(r.world,11,78,8,132,0);
        gm_world_set_block_meta(r.world,12,78,8,132,0);
        gm_world_set_block_meta(r.world,13,78,8,132,0);
        gm_world_set_block_meta(r.world,14,78,8,131,1);
        gm_world_set_block_meta(r.world,15,78,8,1,0);
        gm_world_set_block_meta(r.world,10,78,7,123,0);
        gm_world_set_block_meta(r.world,14,78,7,123,0);
        gm_runtime_set_pose_state(
            &r,8.5,78.0,8.5,0.0f,0.0f,0.0,0.0,0.0,1,0.0f);
        {
            long long base=r.clock.total_time;
            GmRuntimeScheduledTick hook;
            GmRuntimeScheduledTick wire;
            CHECK(gm_runtime_schedule_tick(
                      &r,10,78,8,131,base+10,0,0) &&
                  gm_runtime_spawn_item_fixture(
                      &r,5860,12.5,78.0,8.5,0.0,0.0,0.0,
                      1,1,0,0,32767,1),
                  "attached tripwire restores hook callback and item");
            gm_runtime_tick(&r,idle);
            CHECK(gm_world_meta(r.world,10,78,8)==15 &&
                  gm_world_meta(r.world,11,78,8)==4 &&
                  gm_world_meta(r.world,12,78,8)==5 &&
                  gm_world_meta(r.world,13,78,8)==4 &&
                  gm_world_meta(r.world,14,78,8)==13 &&
                  gm_world_block(r.world,10,78,7)==124 &&
                  gm_world_block(r.world,14,78,7)==124 &&
                  gm_world_block_light(r.world,10,78,7)==15 &&
                  gm_world_block_light(r.world,10,78,8)==14 &&
                  gm_world_block_light(r.world,11,78,8)==13 &&
                  gm_world_block_light(r.world,12,78,8)==12 &&
                  gm_world_block_light(r.world,13,78,8)==13 &&
                  gm_world_block_light(r.world,14,78,8)==14 &&
                  gm_world_block_light(r.world,14,78,7)==15 &&
                  r.entities.ents[0].age==1 &&
                  gm_runtime_scheduled_tick_count(&r)==2 &&
                  gm_runtime_scheduled_tick_get(&r,0,&hook) &&
                  gm_runtime_scheduled_tick_get(&r,1,&wire) &&
                  hook.block==131 && hook.time==base+10 &&
                  hook.order==0 && wire.block==132 &&
                  wire.time==base+11 && wire.order==1,
                  "item collision powers exact wire, hooks, lamps, and "
                  "adds the +10 wire callback after staged hook work");
            for(int tick=0;tick<8;++tick)
                gm_runtime_tick(&r,idle);
            CHECK(gm_runtime_scheduled_tick_count(&r)==2,
                  "occupied tripwire retains hook and wire through +9");
            gm_runtime_tick(&r,idle);
            CHECK(gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&wire) &&
                  wire.block==132 && wire.time==base+11,
                  "staged hook callback drains first at exact +10");
            gm_runtime_tick(&r,idle);
            CHECK(gm_world_meta(r.world,12,78,8)==5 &&
                  gm_runtime_scheduled_tick_count(&r)==1 &&
                  gm_runtime_scheduled_tick_get(&r,0,&wire) &&
                  wire.block==132 && wire.time==base+21 && wire.order==2,
                  "occupied wire callback retains power and reschedules +10");
            memset(&r.entities,0,sizeof r.entities);
            for(int tick=0;tick<9;++tick)
                gm_runtime_tick(&r,idle);
            CHECK(gm_world_meta(r.world,12,78,8)==5 &&
                  gm_runtime_scheduled_tick_count(&r)==1,
                  "cleared tripwire remains powered through its delayed poll");
            gm_runtime_tick(&r,idle);
            CHECK(gm_world_meta(r.world,10,78,8)==7 &&
                  gm_world_meta(r.world,12,78,8)==4 &&
                  gm_world_meta(r.world,14,78,8)==5 &&
                  gm_world_block(r.world,10,78,7)==124 &&
                  gm_world_block(r.world,14,78,7)==124 &&
                  gm_runtime_scheduled_tick_count(&r)==3,
                  "vacated wire poll releases wire/hooks and schedules lamps/hook");
            for(int tick=0;tick<10;++tick)
                gm_runtime_tick(&r,idle);
            CHECK(gm_world_meta(r.world,10,78,8)==7,
                  "final hook recheck preserves near hook attachment");
            CHECK(gm_world_meta(r.world,11,78,8)==4,
                  "final hook recheck preserves first wire attachment");
            CHECK(gm_world_meta(r.world,12,78,8)==4,
                  "final hook recheck preserves released wire attachment");
            CHECK(gm_world_meta(r.world,13,78,8)==4,
                  "final hook recheck preserves last wire attachment");
            CHECK(gm_world_meta(r.world,14,78,8)==5,
                  "final hook recheck preserves far hook attachment");
            CHECK(gm_world_block(r.world,10,78,7)==123 &&
                  gm_world_block(r.world,14,78,7)==123,
                  "final hook recheck leaves both lamps unpowered");
            CHECK(gm_runtime_scheduled_tick_count(&r)==0,
                  "final hook recheck drains all callbacks");
        }

        memset(r.projectiles,0,sizeof r.projectiles);
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;
        for(int y=77;y<=80;++y)
            for(int z=6;z<=10;++z)
                for(int x=8;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,8,77,8,1,0);
        gm_world_set_block_meta(r.world,9,78,8,1,0);
        gm_world_set_block_meta(r.world,10,78,8,131,3);
        gm_world_set_block_meta(r.world,11,78,8,132,0);
        gm_world_set_block_meta(r.world,12,78,8,132,0);
        gm_world_set_block_meta(r.world,13,78,8,132,0);
        gm_world_set_block_meta(r.world,14,78,8,131,1);
        gm_world_set_block_meta(r.world,15,78,8,1,0);
        gm_world_set_block_meta(r.world,10,78,7,123,0);
        gm_world_set_block_meta(r.world,14,78,7,123,0);
        gm_runtime_set_pose_state(
            &r,8.5,78.0,8.5,0.0f,0.0f,0.0,0.0,0.0,1,0.0f);
        {
            long long base=r.clock.total_time;
            GmRuntimeScheduledTick hook;
            GmRuntimeScheduledTick wire;
            CHECK(gm_runtime_schedule_tick(
                      &r,10,78,8,131,base+10,0,0) &&
                  gm_runtime_spawn_arrow_fixture(
                      &r,5861,12.5,78.0,8.5,0.0,0.0,0.0,1,0),
                  "attached tripwire restores hook callback and arrow");
            gm_runtime_tick(&r,idle);
            CHECK(gm_world_meta(r.world,10,78,8)==15 &&
                  gm_world_meta(r.world,11,78,8)==4 &&
                  gm_world_meta(r.world,12,78,8)==5 &&
                  gm_world_meta(r.world,13,78,8)==4 &&
                  gm_world_meta(r.world,14,78,8)==13 &&
                  gm_world_block(r.world,10,78,7)==124 &&
                  gm_world_block(r.world,14,78,7)==124 &&
                  r.projectiles[0].age==1 &&
                  gm_runtime_scheduled_tick_count(&r)==2 &&
                  gm_runtime_scheduled_tick_get(&r,0,&hook) &&
                  gm_runtime_scheduled_tick_get(&r,1,&wire) &&
                  hook.block==131 && hook.time==base+10 &&
                  wire.block==132 && wire.time==base+11,
                  "arrow collision powers exact wire, hooks, and lamps");
        }

        memset(r.projectiles,0,sizeof r.projectiles);
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;
        for(int y=77;y<=80;++y)
            for(int z=6;z<=10;++z)
                for(int x=10;x<=22;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,77,8,1,0);
        gm_world_set_block_meta(r.world,16,77,8,1,0);
        gm_world_set_block_meta(r.world,20,77,8,1,0);
        gm_world_set_block_meta(r.world,12,78,8,70,0);
        gm_world_set_block_meta(r.world,16,78,8,72,0);
        gm_world_set_block_meta(r.world,20,78,8,147,0);
        CHECK(gm_runtime_spawn_arrow_fixture(
                  &r,5862,12.5,78.0,8.5,0.0,0.0,0.0,1,0) &&
              gm_runtime_spawn_arrow_fixture(
                  &r,5863,16.5,78.0,8.5,0.0,0.0,0.0,1,0) &&
              gm_runtime_spawn_arrow_fixture(
                  &r,5864,20.5,78.0,8.5,0.0,0.0,0.0,1,0),
              "spawn stone, wood, and weighted arrow controls");
        gm_runtime_tick(&r,idle);
        CHECK(gm_world_meta(r.world,12,78,8)==0,
              "arrow does not activate stone MOBS pressure plate");
        CHECK(gm_world_meta(r.world,16,78,8)==1,
              "arrow activates wooden EVERYTHING pressure plate");
        CHECK(gm_world_meta(r.world,20,78,8)==1,
              "one arrow gives gold weighted plate strength one");
        memset(r.projectiles,0,sizeof r.projectiles);
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,78,8,217,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5353) &&
              gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x123456789ABC)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              r.piston_count==1 &&
              r.entities.n_active==0 &&
              r.next_entity_id==5353 &&
              r.world_random_seed48==UINT64_C(0x1902D9AECA17) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "structure void consumes only the successful piston-pitch draw");
        gm_runtime_tick(&r,idle);
        CHECK(r.entities.n_active==0 &&
              r.next_entity_id==5353,
              "destroyed structure void remains entity-free during movement");

        {
            static const struct {
                int block;
                unsigned valid_meta_mask;
                int item;
            } controls[]={
                {69,UINT32_C(0xFFFF),69},
                {70,UINT32_C(0x0003),70},
                {72,UINT32_C(0x0003),72},
                {75,UINT32_C(0x003E),76},
                {76,UINT32_C(0x003E),76},
                {77,UINT32_C(0x3F3F),77},
                {93,UINT32_C(0xFFFF),356},
                {94,UINT32_C(0xFFFF),356},
                {143,UINT32_C(0x3F3F),143},
                {147,UINT32_C(0xFFFF),147},
                {148,UINT32_C(0xFFFF),148},
            };
            int sequence=0;
            for(size_t control_i=0;
                control_i<sizeof controls/sizeof controls[0];
                ++control_i){
                for(int meta=0;meta<16;++meta){
                    int eid;
                    if((controls[control_i].valid_meta_mask
                            & (UINT32_C(1)<<meta))==0)
                        continue;
                    eid=5400+sequence++;
                    memset(&r.entities,0,sizeof r.entities);
                    r.piston_count=0;
                    memset(r.pistons,0,sizeof r.pistons);
                    r.comparator_count=0;
                    memset(r.comparators,0,sizeof r.comparators);
                    r.scheduled_tick_count=0;
                    r.scheduled_tick_next_order=0;
                    r.redstone_torch_toggle_count=0;
                    for(int y=77;y<=80;++y)
                        for(int z=7;z<=10;++z)
                            for(int x=10;x<=16;++x)
                                gm_world_set_block_meta(
                                    r.world,x,y,z,0,0);
                    gm_world_set_block_meta(r.world,12,78,9,152,0);
                    gm_world_set_block_meta(r.world,13,77,8,1,0);
                    gm_world_set_block_meta(
                        r.world,13,78,8,
                        controls[control_i].block,meta);
                    CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                          gm_runtime_set_world_random_seed48(
                              &r,UINT64_C(0x123456789ABC)) &&
                          gm_runtime_set_math_random_seed48(
                              &r,UINT64_C(0x0FEDCBA98765)) &&
                          gm_runtime_set_block(&r,12,78,8,33,5) &&
                          gm_world_meta(r.world,12,78,8)==13 &&
                          gm_world_block(r.world,13,78,8)==36 &&
                          r.piston_count==1 &&
                          r.entities.n_active==1 &&
                          r.entities.ents[0].eid==eid &&
                          r.entities.ents[0].item
                              ==controls[control_i].item &&
                          r.entities.ents[0].count==1 &&
                          r.entities.ents[0].meta==0 &&
                          r.entities.ents[0].age==0 &&
                          r.entities.ents[0].pickup_delay==10,
                          "all canonical control/plate/torch/repeater "
                          "metadata maps to its exact piston item");
                }
            }
            CHECK(sequence==118,
                  "control destroy payload test covers 118 canonical states");
            memset(&r.entities,0,sizeof r.entities);
            r.piston_count=1;
            memset(r.pistons,0,sizeof r.pistons);
            for(int y=77;y<=80;++y)
                for(int z=7;z<=10;++z)
                    for(int x=10;x<=16;++x)
                        gm_world_set_block_meta(r.world,x,y,z,0,0);
            gm_world_set_block_meta(r.world,13,78,8,36,5);
            r.pistons[0]=(GmRuntimePiston){
                .active=1,.dimension=r.dimension,
                .x=13,.y=78,.z=8,
                .moved_block=34,.moved_meta=5,
                .facing=5,.extending=1,.source=1,
                .progress=0.5f,.last_progress=0.0f
            };
            CHECK(gm_live_spawn_item_exact(
                      &r.entities,5599,
                      13.635,78.5827826038003,8.36773057281971,
                      -0.01706784315098364,
                      0.15680000684857376,
                      -0.08294180942076679,
                      0.0f,356,1,0,1,9,0),
                  "half-extended-head item fixture spawns exact tick-0 row");
            gm_runtime_tick(&r,idle);
            CHECK(r.entities.n_active==1 &&
                  fabs(r.entities.ents[0].x-14.135)<1.0e-12 &&
                  fabs(r.entities.ents[0].y
                      -78.69958261154294)<1.0e-12 &&
                  fabs(r.entities.ents[0].z
                      -8.284788763398943)<1.0e-12 &&
                  r.entities.ents[0].mx==0.0 &&
                  fabs(r.entities.ents[0].my
                      -0.1144640098155739)<1.0e-12 &&
                  fabs(r.entities.ents[0].mz
                      +0.08128297481434092)<1.0e-12,
                  "item SELF motion collides with the half-extended head "
                  "before the second piston sweep");

            memset(&r.entities,0,sizeof r.entities);
            r.piston_count=0;
            memset(r.pistons,0,sizeof r.pistons);
            for(int y=77;y<=80;++y)
                for(int z=7;z<=10;++z)
                    for(int x=10;x<=16;++x)
                        gm_world_set_block_meta(r.world,x,y,z,0,0);
            gm_world_set_block_meta(r.world,13,78,8,34,5);
            CHECK(gm_live_spawn_item_exact(
                      &r.entities,5600,
                      14.1283371996104,
                      78.77404662225258,
                      8.219621456019615,
                      -0.006529544508891597,
                      0.072974731915739,
                      -0.0744988561627548,
                      0.0f,69,1,0,3,7,0),
                  "settled-head item fixture spawns exact pre-collision row");
            gm_runtime_tick(&r,idle);
            CHECK(r.entities.n_active==1 &&
                  fabs(r.entities.ents[0].x-14.125)<1.0e-12 &&
                  fabs(r.entities.ents[0].y
                      -78.80702135506239)<1.0e-12 &&
                  fabs(r.entities.ents[0].z
                      -8.14512259985686)<1.0e-12 &&
                  r.entities.ents[0].mx==0.0 &&
                  fabs(r.entities.ents[0].my
                      -0.032315238782555614)<1.0e-12 &&
                  fabs(r.entities.ents[0].mz
                      +0.07300888046045262)<1.0e-12 &&
                  r.entities.ents[0].age==4 &&
                  r.entities.ents[0].pickup_delay==6,
                  "item SELF motion collides with the settled piston-head "
                  "plate at the earliest oracle divergence");

            {
                static const struct {
                    int block,meta;
                } invalid[]={
                    {70,2},{72,2},{75,0},{75,6},{76,0},{76,6},
                    {77,6},{77,14},{143,7},{143,15},
                };
                for(size_t invalid_i=0;
                    invalid_i<sizeof invalid/sizeof invalid[0];
                    ++invalid_i){
                    memset(&r.entities,0,sizeof r.entities);
                    r.piston_count=0;
                    memset(r.pistons,0,sizeof r.pistons);
                    for(int y=77;y<=80;++y)
                        for(int z=7;z<=10;++z)
                            for(int x=10;x<=16;++x)
                                gm_world_set_block_meta(
                                    r.world,x,y,z,0,0);
                    gm_world_set_block_meta(r.world,12,78,9,152,0);
                    gm_world_set_block_meta(
                        r.world,13,78,8,
                        invalid[invalid_i].block,
                        invalid[invalid_i].meta);
                    CHECK(gm_runtime_set_block(&r,12,78,8,33,5) &&
                          gm_world_meta(r.world,12,78,8)==5 &&
                          gm_world_block(r.world,13,78,8)
                              ==invalid[invalid_i].block &&
                          gm_world_meta(r.world,13,78,8)
                              ==invalid[invalid_i].meta &&
                          r.piston_count==0 &&
                          r.entities.n_active==0,
                          "noncanonical control metadata stays visible "
                          "instead of being silently destroyed");
                }
            }
        }

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,1,0);
        gm_world_set_block_meta(r.world,13,78,8,78,3);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4747) &&
              gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x123456789ABC)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              r.piston_count==1 &&
              r.entities.n_active==0 &&
              r.next_entity_id==4747 &&
              r.world_random_seed48==UINT64_C(0x2238BD434A3A) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "four-layer snow consumes five rejected drop draws then pitch");
        gm_runtime_tick(&r,idle);
        CHECK(r.entities.n_active==0 &&
              r.next_entity_id==4747,
              "piston-broken snow remains entity-free during the moving sweep");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,12,0);
        gm_world_set_block_meta(r.world,13,78,8,32,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4770) &&
              gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x123456789ABC)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              r.piston_count==1 &&
              r.entities.n_active==2 &&
              r.entities.ents[0].eid==4770 &&
              r.entities.ents[1].eid==4771 &&
              r.entities.ents[0].item==280 &&
              r.entities.ents[1].item==280 &&
              r.entities.ents[0].count==1 &&
              r.entities.ents[1].count==1 &&
              r.entities.ents[0].meta==0 &&
              r.entities.ents[1].meta==0 &&
              r.next_entity_id==4772 &&
              r.world_random_seed48==UINT64_C(0xC788B5C040DE) &&
              r.math_random_seed48==UINT64_C(0x6B1C94DF7835),
              "dead bush nextInt(3) creates two ordered one-stick entities "
              "then consumes the successful piston-pitch draw");
        gm_runtime_tick(&r,idle);
        CHECK(r.entities.n_active==2 &&
              r.entities.ents[0].age==1 &&
              r.entities.ents[1].age==1 &&
              r.entities.ents[0].pickup_delay==9 &&
              r.entities.ents[1].pickup_delay==9,
              "both randomized dead-bush stacks tick exactly once before "
              "the moving-head sweep");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,12,0);
        gm_world_set_block_meta(r.world,13,78,8,32,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4780) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              r.entities.n_active==0 &&
              r.next_entity_id==4780 &&
              r.world_random_seed48==UINT64_C(0x0040942DE6BA) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "dead bush zero-count branch consumes nextInt(3) then pitch");

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX-1;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX-1;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,12,0);
        gm_world_set_block_meta(r.world,13,78,8,32,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4790) &&
              gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x123456789ABC)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==32 &&
              r.piston_count==0 &&
              r.entities.n_active==GM_LIVE_MAX-1 &&
              r.next_entity_id==4790 &&
              r.world_random_seed48==UINT64_C(0x123456789ABC) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "dead bush two-item branch rejects insufficient capacity "
              "without partial RNG, entity, or world mutation");

        {
            int tall_grass_states=0;
            for(int meta=0;meta<=3;++meta){
                int canonical=meta<=2;
                int eid=4795+meta;
                memset(&r.entities,0,sizeof r.entities);
                r.piston_count=0;
                memset(r.pistons,0,sizeof r.pistons);
                for(int y=77;y<=80;++y)
                    for(int z=7;z<=10;++z)
                        for(int x=10;x<=16;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_world_set_block_meta(r.world,12,78,9,152,0);
                gm_world_set_block_meta(r.world,13,77,8,3,0);
                gm_world_set_block_meta(r.world,13,78,8,31,meta);
                CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                      gm_runtime_set_world_random_seed48(
                          &r,UINT64_C(0x123456789ABC)) &&
                      gm_runtime_set_math_random_seed48(
                          &r,UINT64_C(0x0FEDCBA98765)) &&
                      gm_runtime_set_block_random_seed48(&r,UINT64_C(0)) &&
                      gm_runtime_set_block(&r,12,78,8,33,5),
                      "tall-grass piston fixture applies its tick-zero edit");
                if(canonical){
                    ++tall_grass_states;
                    CHECK(gm_world_meta(r.world,12,78,8)==13 &&
                          gm_world_block(r.world,13,78,8)==36 &&
                          r.piston_count==1 &&
                          r.entities.n_active==1 &&
                          r.entities.ents[0].eid==eid &&
                          r.entities.ents[0].item==295 &&
                          r.entities.ents[0].count==1 &&
                          r.entities.ents[0].meta==0 &&
                          r.next_entity_id==eid+1 &&
                          r.block_random_seed48
                              ==UINT64_C(0x0AA8544E593D) &&
                          r.world_random_seed48
                              ==UINT64_C(0x90493252C18B) &&
                          r.math_random_seed48
                              ==UINT64_C(0x33E01D26154D),
                          "all canonical tall-grass states consume three "
                          "Block.RANDOM draws and spawn one wheat seed");
                }else{
                    CHECK(gm_world_meta(r.world,12,78,8)==5 &&
                          gm_world_block(r.world,13,78,8)==31 &&
                          gm_world_meta(r.world,13,78,8)==meta &&
                          r.piston_count==0 &&
                          r.entities.n_active==0 &&
                          r.next_entity_id==eid &&
                          r.block_random_seed48==UINT64_C(0) &&
                          r.world_random_seed48
                              ==UINT64_C(0x123456789ABC) &&
                          r.math_random_seed48
                              ==UINT64_C(0x0FEDCBA98765),
                          "noncanonical tall-grass metadata remains visible "
                          "without partial piston state");
                }
            }
            CHECK(tall_grass_states==3,
                  "tall-grass payload covers its three canonical states");
        }

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,3,0);
        gm_world_set_block_meta(r.world,13,78,8,31,1);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4799) &&
              gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x123456789ABC)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block_random_seed48(&r,UINT64_C(1396)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              r.piston_count==1 &&
              r.entities.n_active==0 &&
              r.next_entity_id==4799 &&
              r.block_random_seed48==UINT64_C(0x2003A3D88A6F) &&
              r.world_random_seed48==UINT64_C(0x1902D9AECA17) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "tall-grass seven-eighths branch consumes one block draw, "
              "spawns nothing, and still consumes piston pitch");

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,3,0);
        gm_world_set_block_meta(r.world,13,78,8,31,1);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4800) &&
              gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x123456789ABC)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==31 &&
              gm_world_meta(r.world,13,78,8)==1 &&
              r.piston_count==0 &&
              r.entities.n_active==GM_LIVE_MAX &&
              r.next_entity_id==4800 &&
              r.block_random_seed48==UINT64_C(0) &&
              r.world_random_seed48==UINT64_C(0x123456789ABC) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "successful tall-grass drop rejects full entity capacity "
              "without partial RNG, entity, or world mutation");

        {
            int wheat_states=0;
            for(int meta=0;meta<=8;++meta){
                int canonical=meta<=7;
                int eid=4816+meta*4;
                memset(&r.entities,0,sizeof r.entities);
                r.piston_count=0;
                memset(r.pistons,0,sizeof r.pistons);
                for(int y=77;y<=80;++y)
                    for(int z=7;z<=10;++z)
                        for(int x=10;x<=16;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_world_set_block_meta(r.world,12,78,9,152,0);
                gm_world_set_block_meta(r.world,13,77,8,60,0);
                gm_world_set_block_meta(r.world,13,78,8,59,meta);
                CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                      gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
                      gm_runtime_set_math_random_seed48(
                          &r,UINT64_C(0x0FEDCBA98765)) &&
                      gm_runtime_set_block_random_seed48(
                          &r,UINT64_C(0x123456789ABC)) &&
                      gm_runtime_set_block(&r,12,78,8,33,5),
                      "wheat piston fixture applies its tick-zero edit");
                if(!canonical){
                    CHECK(gm_world_meta(r.world,12,78,8)==5 &&
                          gm_world_block(r.world,13,78,8)==59 &&
                          gm_world_meta(r.world,13,78,8)==8 &&
                          r.piston_count==0 &&
                          r.entities.n_active==0 &&
                          r.next_entity_id==eid &&
                          r.world_random_seed48==UINT64_C(0) &&
                          r.math_random_seed48
                              ==UINT64_C(0x0FEDCBA98765) &&
                          r.block_random_seed48
                              ==UINT64_C(0x123456789ABC),
                          "noncanonical wheat metadata remains visible "
                          "without partial piston or RNG state");
                    continue;
                }
                ++wheat_states;
                CHECK(gm_world_meta(r.world,12,78,8)==13 &&
                      gm_world_block(r.world,13,78,8)==36 &&
                      gm_world_block(r.world,13,77,8)==3 &&
                      gm_world_meta(r.world,13,77,8)==0 &&
                      r.piston_count==1,
                      "all canonical wheat ages are piston DESTROY states "
                      "and their solid moving head turns farmland to dirt");
                if(meta<7){
                    CHECK(r.entities.n_active==1 &&
                          r.entities.ents[0].eid==eid &&
                          r.entities.ents[0].item==295 &&
                          r.entities.ents[0].count==1 &&
                          r.entities.ents[0].meta==0 &&
                          r.next_entity_id==eid+1 &&
                          r.world_random_seed48
                              ==UINT64_C(0x5D5692ACE2BF) &&
                          r.math_random_seed48
                              ==UINT64_C(0x33E01D26154D),
                          "immature wheat emits one seed without mature "
                          "count trials, then consumes exact spawn/pitch RNG");
                }else{
                    CHECK(r.entities.n_active==3 &&
                          r.entities.ents[0].eid==eid &&
                          r.entities.ents[0].item==296 &&
                          r.entities.ents[1].eid==eid+1 &&
                          r.entities.ents[1].item==295 &&
                          r.entities.ents[2].eid==eid+2 &&
                          r.entities.ents[2].item==295 &&
                          r.entities.ents[0].count==1 &&
                          r.entities.ents[1].count==1 &&
                          r.entities.ents[2].count==1 &&
                          r.next_entity_id==eid+3 &&
                          r.world_random_seed48
                              ==UINT64_C(0x0D0352014D90) &&
                          r.math_random_seed48
                              ==UINT64_C(0x63BD8BBB501D),
                          "mature wheat seed zero emits wheat plus two seed "
                          "stacks with exact count/spawn/pitch RNG cursors");
                }
                CHECK(r.block_random_seed48
                          ==UINT64_C(0x123456789ABC),
                      "wheat drops do not consume process-global Block.RANDOM");
            }
            CHECK(wheat_states==8,
                  "wheat payload covers all eight canonical ages");
        }

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX-2;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX-2;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,60,0);
        gm_world_set_block_meta(r.world,13,78,8,59,7);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4855) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==59 &&
              gm_world_meta(r.world,13,78,8)==7 &&
              r.piston_count==0 &&
              r.entities.n_active==GM_LIVE_MAX-2 &&
              r.next_entity_id==4855 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "mature wheat three-stack branch rejects two free slots "
              "without partial RNG, entity, piston, or world mutation");

        {
            int crop_states=0;
            for(int crop_index=0;crop_index<2;++crop_index){
                int crop_id=crop_index==0?141:142;
                int crop_item=crop_index==0?391:392;
                for(int meta=0;meta<=8;++meta){
                    int canonical=meta<=7;
                    int eid=4860+crop_index*40+meta*4;
                    memset(&r.entities,0,sizeof r.entities);
                    r.piston_count=0;
                    memset(r.pistons,0,sizeof r.pistons);
                    for(int y=77;y<=80;++y)
                        for(int z=7;z<=10;++z)
                            for(int x=10;x<=16;++x)
                                gm_world_set_block_meta(r.world,x,y,z,0,0);
                    gm_world_set_block_meta(r.world,12,78,9,152,0);
                    gm_world_set_block_meta(r.world,13,77,8,60,0);
                    gm_world_set_block_meta(
                        r.world,13,78,8,crop_id,meta);
                    CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                          gm_runtime_set_world_random_seed48(
                              &r,UINT64_C(0)) &&
                          gm_runtime_set_math_random_seed48(
                              &r,UINT64_C(0x0FEDCBA98765)) &&
                          gm_runtime_set_block_random_seed48(
                              &r,UINT64_C(1)) &&
                          gm_runtime_set_block(&r,12,78,8,33,5),
                          "carrot/potato piston fixture applies tick-zero");
                    if(!canonical){
                        CHECK(gm_world_meta(r.world,12,78,8)==5 &&
                              gm_world_block(r.world,13,78,8)==crop_id &&
                              gm_world_meta(r.world,13,78,8)==8 &&
                              r.piston_count==0 &&
                              r.entities.n_active==0 &&
                              r.next_entity_id==eid &&
                              r.world_random_seed48==UINT64_C(0) &&
                              r.math_random_seed48
                                  ==UINT64_C(0x0FEDCBA98765) &&
                              r.block_random_seed48==UINT64_C(1),
                              "noncanonical carrot/potato metadata is "
                              "rejected without partial state");
                        continue;
                    }
                    ++crop_states;
                    CHECK(gm_world_meta(r.world,12,78,8)==13 &&
                          gm_world_block(r.world,13,78,8)==36 &&
                          gm_world_block(r.world,13,77,8)==3 &&
                          gm_world_meta(r.world,13,77,8)==0 &&
                          r.piston_count==1,
                          "all canonical carrot/potato ages are DESTROY "
                          "and the moving head turns farmland to dirt");
                    if(meta<7){
                        CHECK(r.entities.n_active==1 &&
                              r.entities.ents[0].eid==eid &&
                              r.entities.ents[0].item==crop_item &&
                              r.entities.ents[0].count==1 &&
                              r.entities.ents[0].meta==0 &&
                              r.next_entity_id==eid+1 &&
                              r.world_random_seed48
                                  ==UINT64_C(0x5D5692ACE2BF) &&
                              r.math_random_seed48
                                  ==UINT64_C(0x33E01D26154D) &&
                              r.block_random_seed48==UINT64_C(1),
                              "immature carrot/potato emits one crop item "
                              "without mature RNG trials");
                    }else{
                        CHECK(r.entities.n_active==3 &&
                              r.entities.ents[0].eid==eid &&
                              r.entities.ents[1].eid==eid+1 &&
                              r.entities.ents[2].eid==eid+2 &&
                              r.entities.ents[0].item==crop_item &&
                              r.entities.ents[1].item==crop_item &&
                              r.entities.ents[2].item==crop_item &&
                              r.entities.ents[0].count==1 &&
                              r.entities.ents[1].count==1 &&
                              r.entities.ents[2].count==1 &&
                              r.next_entity_id==eid+3 &&
                              r.world_random_seed48
                                  ==UINT64_C(0x0D0352014D90) &&
                              r.math_random_seed48
                                  ==UINT64_C(0x63BD8BBB501D) &&
                              r.block_random_seed48
                                  ==(crop_id==142
                                      ?UINT64_C(0x5DEECE678)
                                      :UINT64_C(1)),
                              "mature carrot/potato seed zero emits three "
                              "ordered crop stacks; potato also consumes "
                              "its failed poison trial");
                    }
                }
            }
            CHECK(crop_states==16,
                  "carrot/potato payload covers all 16 canonical ages");
        }

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,60,0);
        gm_world_set_block_meta(r.world,13,78,8,142,7);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4940) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              gm_world_block(r.world,13,77,8)==3 &&
              r.piston_count==1 &&
              r.entities.n_active==4 &&
              r.entities.ents[0].eid==4940 &&
              r.entities.ents[1].eid==4941 &&
              r.entities.ents[2].eid==4942 &&
              r.entities.ents[3].eid==4943 &&
              r.entities.ents[0].item==392 &&
              r.entities.ents[1].item==392 &&
              r.entities.ents[2].item==392 &&
              r.entities.ents[3].item==394 &&
              r.next_entity_id==4944 &&
              r.world_random_seed48==UINT64_C(0x4C56A6636394) &&
              r.math_random_seed48==UINT64_C(0x63EF35B33D05) &&
              r.block_random_seed48==UINT64_C(0xB),
              "mature potato appends poisonous potato after three ordinary "
              "stacks with exact World/Block/Math RNG order");

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX-3;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX-3;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,60,0);
        gm_world_set_block_meta(r.world,13,78,8,142,7);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4950) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==142 &&
              gm_world_meta(r.world,13,78,8)==7 &&
              gm_world_block(r.world,13,77,8)==60 &&
              r.piston_count==0 &&
              r.entities.n_active==GM_LIVE_MAX-3 &&
              r.next_entity_id==4950 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765) &&
              r.block_random_seed48==UINT64_C(0),
              "four-stack potato branch rejects three free slots without "
              "partial RNG, entity, piston, farmland, or world mutation");

        {
            int beetroot_states=0;
            for(int meta=0;meta<=4;++meta){
                int canonical=meta<=3;
                int eid=4955+meta*4;
                memset(&r.entities,0,sizeof r.entities);
                r.piston_count=0;
                memset(r.pistons,0,sizeof r.pistons);
                for(int y=77;y<=80;++y)
                    for(int z=7;z<=10;++z)
                        for(int x=10;x<=16;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_world_set_block_meta(r.world,12,78,9,152,0);
                gm_world_set_block_meta(r.world,13,77,8,60,0);
                gm_world_set_block_meta(r.world,13,78,8,207,meta);
                CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                      gm_runtime_set_world_random_seed48(
                          &r,UINT64_C(0)) &&
                      gm_runtime_set_math_random_seed48(
                          &r,UINT64_C(0x0FEDCBA98765)) &&
                      gm_runtime_set_block_random_seed48(
                          &r,UINT64_C(0x123456789ABC)) &&
                      gm_runtime_set_block(&r,12,78,8,33,5),
                      "beetroot piston fixture applies tick-zero");
                if(!canonical){
                    CHECK(gm_world_meta(r.world,12,78,8)==5 &&
                          gm_world_block(r.world,13,78,8)==207 &&
                          gm_world_meta(r.world,13,78,8)==4 &&
                          gm_world_block(r.world,13,77,8)==60 &&
                          r.piston_count==0 &&
                          r.entities.n_active==0 &&
                          r.next_entity_id==eid &&
                          r.world_random_seed48==UINT64_C(0) &&
                          r.math_random_seed48
                              ==UINT64_C(0x0FEDCBA98765) &&
                          r.block_random_seed48
                              ==UINT64_C(0x123456789ABC),
                          "noncanonical beetroot metadata is rejected "
                          "without partial state");
                    continue;
                }
                ++beetroot_states;
                CHECK(gm_world_meta(r.world,12,78,8)==13 &&
                      gm_world_block(r.world,13,78,8)==36 &&
                      gm_world_block(r.world,13,77,8)==3 &&
                      gm_world_meta(r.world,13,77,8)==0 &&
                      r.piston_count==1,
                      "all canonical beetroot ages are DESTROY and the "
                      "moving head turns farmland to dirt");
                if(meta<3){
                    CHECK(r.entities.n_active==1 &&
                          r.entities.ents[0].eid==eid &&
                          r.entities.ents[0].item==435 &&
                          r.entities.ents[0].count==1 &&
                          r.entities.ents[0].meta==0 &&
                          r.next_entity_id==eid+1 &&
                          r.world_random_seed48
                              ==UINT64_C(0x5D5692ACE2BF) &&
                          r.math_random_seed48
                              ==UINT64_C(0x33E01D26154D) &&
                          r.block_random_seed48
                              ==UINT64_C(0x123456789ABC),
                          "immature beetroot emits one seed without "
                          "mature RNG trials");
                }else{
                    CHECK(r.entities.n_active==3 &&
                          r.entities.ents[0].eid==eid &&
                          r.entities.ents[1].eid==eid+1 &&
                          r.entities.ents[2].eid==eid+2 &&
                          r.entities.ents[0].item==434 &&
                          r.entities.ents[1].item==435 &&
                          r.entities.ents[2].item==435 &&
                          r.entities.ents[0].count==1 &&
                          r.entities.ents[1].count==1 &&
                          r.entities.ents[2].count==1 &&
                          r.next_entity_id==eid+3 &&
                          r.world_random_seed48
                              ==UINT64_C(0x0D0352014D90) &&
                          r.math_random_seed48
                              ==UINT64_C(0x63BD8BBB501D) &&
                          r.block_random_seed48
                              ==UINT64_C(0x123456789ABC),
                          "mature beetroot seed zero emits one beetroot "
                          "then two ordered seed stacks");
                }
            }
            CHECK(beetroot_states==4,
                  "beetroot payload covers all four canonical ages");
        }

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX-2;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX-2;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,60,0);
        gm_world_set_block_meta(r.world,13,78,8,207,3);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4975) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block_random_seed48(
                  &r,UINT64_C(0x123456789ABC)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==207 &&
              gm_world_meta(r.world,13,78,8)==3 &&
              gm_world_block(r.world,13,77,8)==60 &&
              r.piston_count==0 &&
              r.entities.n_active==GM_LIVE_MAX-2 &&
              r.next_entity_id==4975 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765) &&
              r.block_random_seed48==UINT64_C(0x123456789ABC),
              "mature beetroot three-stack branch rejects two free slots "
              "without partial RNG, entity, piston, or world mutation");

        {
            int comparator_states=0;
            for(int block_index=0;block_index<2;++block_index){
                int comparator_block=block_index==0?149:150;
                for(int meta=0;meta<16;++meta){
                    int eid=4960+block_index*16+meta;
                    int expects_pending=comparator_block==150||(meta&8)!=0;
                    GmRuntimeScheduledTick pending;
                    memset(&r.entities,0,sizeof r.entities);
                    r.piston_count=0;
                    memset(r.pistons,0,sizeof r.pistons);
                    r.scheduled_tick_count=0;
                    r.scheduled_tick_next_order=0;
                    for(int y=77;y<=80;++y)
                        for(int z=7;z<=10;++z)
                            for(int x=10;x<=16;++x)
                                gm_world_set_block_meta(r.world,x,y,z,0,0);
                    gm_world_set_block_meta(r.world,12,78,9,152,0);
                    gm_world_set_block_meta(r.world,13,77,8,1,0);
                    CHECK(gm_runtime_load_block(
                              &r,13,78,8,comparator_block,meta) &&
                          gm_runtime_comparator_set_output(
                              &r,0,13,78,8,0) &&
                          gm_runtime_set_entity_id_cursor(&r,eid) &&
                          gm_runtime_set_world_random_seed48(
                              &r,UINT64_C(0)) &&
                          gm_runtime_set_math_random_seed48(
                              &r,UINT64_C(0x0FEDCBA98765)) &&
                          gm_runtime_set_block_random_seed48(
                              &r,UINT64_C(0x123456789ABC)) &&
                          gm_runtime_set_block(&r,12,78,8,33,5),
                          "comparator piston fixture applies tick-zero");
                    ++comparator_states;
                    CHECK(gm_world_meta(r.world,12,78,8)==13 &&
                          gm_world_block(r.world,13,78,8)==36 &&
                          gm_world_meta(r.world,13,78,8)==5 &&
                          gm_world_block(r.world,13,77,8)==1 &&
                          r.piston_count==1 &&
                          gm_runtime_comparator_count(&r)==0 &&
                          r.entities.n_active==1 &&
                          r.entities.ents[0].eid==eid &&
                          r.entities.ents[0].item==404 &&
                          r.entities.ents[0].count==1 &&
                          r.entities.ents[0].meta==0 &&
                          r.next_entity_id==eid+1 &&
                          r.world_random_seed48
                              ==UINT64_C(0x5D5692ACE2BF) &&
                          r.math_random_seed48
                              ==UINT64_C(0x33E01D26154D) &&
                          r.block_random_seed48
                              ==UINT64_C(0x123456789ABC),
                          "all comparator states emit item 404, retire the "
                          "tile, and retain exact piston/item cursors");
                    CHECK(r.scheduled_tick_count==expects_pending,
                          "powered comparator state preserves exactly one "
                          "pre-destroy self-correction callback");
                    if(expects_pending){
                        CHECK(gm_runtime_scheduled_tick_get(
                                  &r,0,&pending) &&
                              pending.x==13&&pending.y==78&&pending.z==8 &&
                              pending.block==comparator_block &&
                              pending.time==r.clock.total_time+2 &&
                              pending.priority==0&&pending.order==0,
                              "stale comparator callback retains exact "
                              "block, due time, priority, and order");
                    }
                }
            }
            CHECK(comparator_states==32,
                  "comparator payload covers all 32 canonical block states");
        }

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        r.comparator_count=0;
        memset(r.comparators,0,sizeof r.comparators);
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,1,0);
        gm_world_set_block_meta(r.world,13,78,9,152,0);
        gm_world_set_block_meta(r.world,13,78,7,124,0);
        CHECK(gm_runtime_load_block(&r,13,78,8,149,8) &&
              gm_runtime_comparator_set_output(&r,0,13,78,8,15) &&
              gm_runtime_set_entity_id_cursor(&r,5000) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_runtime_comparator_count(&r)==0 &&
              r.entities.n_active==1 &&
              r.entities.ents[0].item==404 &&
              r.scheduled_tick_count==1,
              "powered comparator destruction retires its tile, drops the "
              "item, and schedules output lamp release");
        {
            GmRuntimeScheduledTick pending;
            CHECK(gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                  pending.x==13&&pending.y==78&&pending.z==7 &&
                  pending.block==124 &&
                  pending.time==r.clock.total_time+4,
                  "comparator output notification creates exact +4 lamp "
                  "callback");
        }
        for(int i=0;i<4;++i)
            gm_runtime_tick(&r,idle);
        CHECK(gm_world_block(r.world,13,78,7)==123 &&
              r.scheduled_tick_count==0,
              "powered comparator output lamp turns off at +4 exactly");

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        r.comparator_count=0;
        memset(r.comparators,0,sizeof r.comparators);
        r.scheduled_tick_count=0;
        r.scheduled_tick_next_order=0;
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,1,0);
        CHECK(gm_runtime_load_block(&r,13,78,8,149,0) &&
              gm_runtime_comparator_set_output(&r,0,13,78,8,7) &&
              gm_runtime_set_entity_id_cursor(&r,5010) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==149 &&
              gm_runtime_comparator_count(&r)==1 &&
              r.piston_count==0 &&
              r.entities.n_active==GM_LIVE_MAX &&
              r.next_entity_id==5010 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765) &&
              r.scheduled_tick_count==1,
              "comparator drop rejects full entity capacity without "
              "partial item, tile, piston, RNG, or target mutation");
        {
            GmRuntimeScheduledTick pending;
            CHECK(gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                  pending.x==13&&pending.y==78&&pending.z==8 &&
                  pending.block==149 &&
                  pending.time==r.clock.total_time+2 &&
                  pending.priority==0&&pending.order==0,
                  "rejected comparator extension retains the exact +2 "
                  "callback caused by piston placement");
        }
        CHECK(gm_runtime_load_block(&r,13,78,8,0,0) &&
              gm_runtime_comparator_count(&r)==0,
              "comparator capacity fixture cleanup retires its saved tile");
        memset(&r.entities,0,sizeof r.entities);

        {
            int leaf_states=0;
            int rejected_states=0;
            for(int leaf_id_index=0;leaf_id_index<2;++leaf_id_index){
                int leaf_id=leaf_id_index==0?18:161;
                for(int meta=0;meta<16;++meta){
                    int canonical=leaf_id==18||(meta&2)==0;
                    int apple_variant=(leaf_id==18&&(meta&3)==0)
                        ||(leaf_id==161&&(meta&3)==1);
                    int eid=4860+leaf_id_index*16+meta;
                    memset(&r.entities,0,sizeof r.entities);
                    r.piston_count=0;
                    memset(r.pistons,0,sizeof r.pistons);
                    for(int y=77;y<=80;++y)
                        for(int z=7;z<=10;++z)
                            for(int x=10;x<=16;++x)
                                gm_world_set_block_meta(r.world,x,y,z,0,0);
                    gm_world_set_block_meta(r.world,12,78,9,152,0);
                    gm_world_set_block_meta(
                        r.world,13,78,8,leaf_id,meta);
                    CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                          gm_runtime_set_world_random_seed48(&r,UINT64_C(1)) &&
                          gm_runtime_set_math_random_seed48(
                              &r,UINT64_C(0x0FEDCBA98765)) &&
                          gm_runtime_set_block_random_seed48(
                              &r,UINT64_C(0x123456789ABC)) &&
                          gm_runtime_set_block(&r,12,78,8,33,5),
                          "leaf piston fixture applies its tick-zero edit");
                    if(canonical){
                        ++leaf_states;
                        CHECK(gm_world_meta(r.world,12,78,8)==13 &&
                              gm_world_block(r.world,13,78,8)==36 &&
                              r.piston_count==1 &&
                              r.entities.n_active==0 &&
                              r.next_entity_id==eid &&
                              r.world_random_seed48
                                  ==(apple_variant
                                      ?UINT64_C(0xDF4111591DF2)
                                      :UINT64_C(0xBB61488DF123)) &&
                              r.math_random_seed48
                                  ==UINT64_C(0x0FEDCBA98765) &&
                              r.block_random_seed48
                                  ==UINT64_C(0x123456789ABC),
                              "all canonical old/new leaf flag states take "
                              "their exact no-drop selection and pitch path");
                    }else{
                        ++rejected_states;
                        CHECK(gm_world_meta(r.world,12,78,8)==5 &&
                              gm_world_block(r.world,13,78,8)==161 &&
                              gm_world_meta(r.world,13,78,8)==meta &&
                              r.piston_count==0 &&
                              r.entities.n_active==0 &&
                              r.next_entity_id==eid &&
                              r.world_random_seed48==UINT64_C(1) &&
                              r.math_random_seed48
                                  ==UINT64_C(0x0FEDCBA98765) &&
                              r.block_random_seed48
                                  ==UINT64_C(0x123456789ABC),
                              "noncanonical leaves2 variants remain visible "
                              "without partial piston or cursor state");
                    }
                }
            }
            CHECK(leaf_states==24&&rejected_states==8,
                  "leaf payload covers all 24 canonical states and rejects "
                  "all eight leaves2 variant aliases");
        }

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,78,8,18,0);
        gm_world_set_block_meta(r.world,13,79,8,18,4);
        gm_world_set_block_meta(r.world,14,78,9,161,4);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4900) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(90)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block_random_seed48(
                  &r,UINT64_C(0x123456789ABC)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              gm_world_meta(r.world,13,79,8)==12 &&
              gm_world_meta(r.world,14,78,9)==12 &&
              r.piston_count==1 &&
              r.entities.n_active==2 &&
              r.entities.ents[0].eid==4900 &&
              r.entities.ents[0].item==6 &&
              r.entities.ents[0].meta==0 &&
              r.entities.ents[1].eid==4901 &&
              r.entities.ents[1].item==260 &&
              r.entities.ents[1].meta==0 &&
              r.next_entity_id==4902 &&
              r.world_random_seed48==UINT64_C(0xAC1B0BE80407) &&
              r.math_random_seed48==UINT64_C(0x6B1C94DF7835) &&
              r.block_random_seed48==UINT64_C(0x123456789ABC),
              "oak leaves select sapling then apple before both spawn paths "
              "and mark adjacent old/new leaves for decay");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,78,8,18,3);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4910) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(18)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              r.entities.n_active==0 &&
              r.next_entity_id==4910 &&
              r.world_random_seed48==UINT64_C(0x288D42EEA21C) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "jungle leaf seed 18 rejects result 20 with nextInt(40), "
              "spawns nothing, and then consumes piston pitch");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,78,8,161,1);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4920) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(55)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              r.entities.n_active==1 &&
              r.entities.ents[0].eid==4920 &&
              r.entities.ents[0].item==260 &&
              r.entities.ents[0].meta==0 &&
              r.next_entity_id==4921 &&
              r.world_random_seed48==UINT64_C(0x112010BD0B24) &&
              r.math_random_seed48==UINT64_C(0x33E01D26154D),
              "dark-oak leaves can fail sapling selection then emit one "
              "apple with the exact leaves2 cursor order");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,78,8,161,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4930) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              r.entities.n_active==1 &&
              r.entities.ents[0].eid==4930 &&
              r.entities.ents[0].item==6 &&
              r.entities.ents[0].meta==4 &&
              r.next_entity_id==4931 &&
              r.world_random_seed48==UINT64_C(0x17617168255E) &&
              r.math_random_seed48==UINT64_C(0x33E01D26154D),
              "acacia leaves emit sapling metadata four with exact selection, "
              "spawn, and pitch cursors");

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX-1;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX-1;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,78,8,18,0);
        gm_world_set_block_meta(r.world,13,79,8,18,4);
        gm_world_set_block_meta(r.world,14,78,9,161,4);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4940) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(90)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==18 &&
              gm_world_meta(r.world,13,78,8)==0 &&
              gm_world_meta(r.world,13,79,8)==4 &&
              gm_world_meta(r.world,14,78,9)==4 &&
              r.piston_count==0 &&
              r.entities.n_active==GM_LIVE_MAX-1 &&
              r.next_entity_id==4940 &&
              r.world_random_seed48==UINT64_C(90) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "two-drop leaf branch rejects one free entity slot without "
              "partial RNG, decay marking, piston, or world mutation");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,12,0);
        gm_world_set_block_meta(r.world,13,77,9,9,0);
        gm_world_set_block_meta(r.world,13,78,8,83,7);
        gm_world_set_block_meta(r.world,13,79,8,83,11);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4950) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block_random_seed48(
                  &r,UINT64_C(0x123456789ABC)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              gm_world_block(r.world,13,79,8)==0 &&
              r.piston_count==1 &&
              r.entities.n_active==2 &&
              r.entities.ents[0].eid==4950 &&
              r.entities.ents[1].eid==4951 &&
              r.entities.ents[0].item==338 &&
              r.entities.ents[1].item==338 &&
              r.entities.ents[0].meta==0 &&
              r.entities.ents[1].meta==0 &&
              r.next_entity_id==4952 &&
              r.world_random_seed48==UINT64_C(0x86D91B38BCB3) &&
              r.math_random_seed48==UINT64_C(0x6B1C94DF7835) &&
              r.block_random_seed48==UINT64_C(0x123456789ABC),
              "piston destroys the lower reed, then its notification "
              "recursively drops the upper reed with exact cursors");
        gm_runtime_tick(&r,idle);
        CHECK(r.entities.ents[0].active &&
              r.entities.ents[1].active &&
              r.entities.ents[0].age==1 &&
              r.entities.ents[1].age==1 &&
              r.entities.ents[0].pickup_delay==9 &&
              r.entities.ents[1].pickup_delay==9,
              "both same-boundary reed drops enter the ordinary item tick");

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX-1;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX-1;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,12,0);
        gm_world_set_block_meta(r.world,13,77,9,9,0);
        gm_world_set_block_meta(r.world,13,78,8,83,7);
        gm_world_set_block_meta(r.world,13,79,8,83,11);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4960) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_meta(r.world,13,78,8)==7 &&
              gm_world_meta(r.world,13,79,8)==11 &&
              r.piston_count==0 &&
              r.entities.n_active==GM_LIVE_MAX-1 &&
              r.next_entity_id==4960 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "two-reed cascade rejects one free slot without partial "
              "RNG, entity, piston, or world mutation");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=76;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,15,76,8,1,0);
        gm_world_set_block_meta(r.world,15,77,8,12,0);
        gm_world_set_block_meta(r.world,15,78,8,81,9);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4970) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,14,78,8,1,0) &&
              gm_world_block(r.world,14,78,8)==1 &&
              gm_world_block(r.world,15,78,8)==0 &&
              r.entities.n_active==1 &&
              r.entities.ents[0].eid==4970 &&
              r.entities.ents[0].item==81 &&
              r.entities.ents[0].meta==0 &&
              r.entities.ents[0].age==0 &&
              r.entities.ents[0].pickup_delay==10 &&
              r.next_entity_id==4971 &&
              r.world_random_seed48==UINT64_C(0x2D3873C4CD04) &&
              r.math_random_seed48==UINT64_C(0x33E01D26154D),
              "solid horizontal neighbor destroys cactus with exact item, "
              "metadata, and spawn cursors");
        gm_runtime_tick(&r,idle);
        CHECK(r.entities.ents[0].active &&
              r.entities.ents[0].age==1 &&
              r.entities.ents[0].pickup_delay==9,
              "same-boundary cactus drop enters the ordinary item tick");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=76;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,78,8,1,0);
        gm_world_set_block_meta(r.world,15,76,8,1,0);
        gm_world_set_block_meta(r.world,15,77,8,12,0);
        gm_world_set_block_meta(r.world,15,78,8,81,9);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4980) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              gm_world_block(r.world,14,78,8)==36 &&
              gm_world_meta(r.world,15,78,8)==9 &&
              r.piston_count==2 &&
              r.entities.n_active==0,
              "cactus remains valid while the adjacent stone is block 36");
        gm_runtime_tick(&r,idle);
        gm_runtime_tick(&r,idle);
        CHECK(gm_world_block(r.world,14,78,8)==36 &&
              gm_world_meta(r.world,15,78,8)==9 &&
              r.entities.n_active==0,
              "cactus remains through both moving-piston progress ticks");
        gm_runtime_tick(&r,idle);
        CHECK(r.piston_count==0 &&
              gm_world_block(r.world,13,78,8)==34 &&
              gm_world_block(r.world,14,78,8)==1 &&
              gm_world_block(r.world,15,78,8)==0 &&
              r.entities.n_active==1 &&
              r.entities.ents[0].eid==4980 &&
              r.entities.ents[0].item==81 &&
              r.entities.ents[0].meta==0 &&
              r.entities.ents[0].age==0 &&
              r.entities.ents[0].pickup_delay==10 &&
              r.next_entity_id==4981 &&
              r.world_random_seed48==UINT64_C(0x5D5692ACE2BF) &&
              r.math_random_seed48==UINT64_C(0x33E01D26154D),
              "settled stone notifies cactus after the entity pass with "
              "exact drop and cursor timing");

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX-1;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX-1;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=76;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,78,8,1,0);
        gm_world_set_block_meta(r.world,15,76,8,1,0);
        gm_world_set_block_meta(r.world,15,77,8,12,0);
        gm_world_set_block_meta(r.world,15,78,8,81,3);
        gm_world_set_block_meta(r.world,15,79,8,81,14);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4990) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==1 &&
              gm_world_block(r.world,14,78,8)==0 &&
              gm_world_meta(r.world,15,78,8)==3 &&
              gm_world_meta(r.world,15,79,8)==14 &&
              r.piston_count==0 &&
              r.entities.n_active==GM_LIVE_MAX-1 &&
              r.next_entity_id==4990 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "two-cactus settlement cascade rejects one free slot before "
              "partial RNG, piston, entity, or world mutation");

        memset(&r.entities,0,sizeof r.entities);
        for(int y=76;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=17;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,15,77,8,121,0);
        gm_world_set_block_meta(r.world,15,78,8,200,4);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4995) &&
              gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x123456789ABC)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,15,77,8,0,0) &&
              gm_world_block(r.world,15,78,8)==200 &&
              gm_world_meta(r.world,15,78,8)==4 &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "unsupported chorus flower remains until its +1 callback");
        {
            GmRuntimeScheduledTick pending;
            CHECK(gm_runtime_scheduled_tick_get(&r,0,&pending) &&
                  pending.block==200 && pending.x==15 &&
                  pending.y==78 && pending.z==8 &&
                  pending.time==r.clock.total_time+1 &&
                  pending.priority==0,
                  "chorus flower support loss schedules exact block callback");
        }
        gm_runtime_tick(&r,idle);
        CHECK(gm_world_block(r.world,15,78,8)==0 &&
              gm_runtime_scheduled_tick_count(&r)==0 &&
              r.entities.n_active==0 && r.next_entity_id==4995 &&
              r.world_random_seed48==UINT64_C(0x123456789ABC) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "invalid chorus flower destroys at +1 without item or RNG");

        gm_world_set_block_meta(r.world,15,78,8,200,2);
        gm_world_set_block_meta(r.world,14,77,8,121,0);
        gm_world_set_block_meta(r.world,16,77,8,121,0);
        gm_world_set_block_meta(r.world,14,78,8,199,0);
        CHECK(gm_runtime_set_block(&r,15,77,8,0,0) &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "one horizontal chorus plant sustains flower over air");
        CHECK(gm_runtime_set_block(&r,16,78,8,199,0) &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "second horizontal chorus plant invalidates side branch");
        gm_runtime_tick(&r,idle);
        CHECK(gm_world_block(r.world,15,78,8)==0 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "invalid two-branch chorus flower is removed at +1");

        for(int y=76;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=17;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,15,77,8,121,0);
        gm_world_set_block_meta(r.world,15,78,8,199,0);
        CHECK(gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_block(&r,15,77,8,0,0) &&
              gm_world_block(r.world,15,78,8)==199 &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "unsupported chorus plant remains until its +1 callback");
        gm_runtime_tick(&r,idle);
        CHECK(gm_world_block(r.world,15,78,8)==0 &&
              gm_runtime_scheduled_tick_count(&r)==0 &&
              r.entities.n_active==0 &&
              r.world_random_seed48==UINT64_C(11),
              "zero-fruit chorus plant branch consumes one nextInt(2) and "
              "destroys at +1");

        gm_world_set_block_meta(r.world,14,77,8,121,0);
        gm_world_set_block_meta(r.world,14,78,8,199,0);
        gm_world_set_block_meta(r.world,15,78,8,199,0);
        CHECK(gm_runtime_set_block(&r,15,77,8,0,0) &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "supported horizontal chorus branch survives over air");
        CHECK(gm_runtime_set_block(&r,14,77,8,0,0) &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "removing branch-root support schedules dependent plant");
        gm_runtime_tick(&r,idle);
        CHECK(gm_world_block(r.world,14,78,8)==0 &&
              gm_world_block(r.world,15,78,8)==199 &&
              gm_runtime_scheduled_tick_count(&r)==1,
              "chorus support chain advances one scheduled layer per tick");
        gm_runtime_tick(&r,idle);
        CHECK(gm_world_block(r.world,15,78,8)==0 &&
              gm_runtime_scheduled_tick_count(&r)==0,
              "dependent chorus plant collapses on the following tick");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,121,0);
        gm_world_set_block_meta(r.world,13,78,8,200,4);
        CHECK(gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              r.piston_count==1 && r.entities.n_active==0 &&
              r.world_random_seed48==UINT64_C(11),
              "piston destroys chorus flower without drop RNG or item");

        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        gm_world_set_block_meta(r.world,12,78,8,0,0);
        gm_world_set_block_meta(r.world,13,78,8,199,0);
        CHECK(gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              r.piston_count==1 && r.entities.n_active==0 &&
              r.world_random_seed48==UINT64_C(0x0040942DE6BA),
              "piston zero-fruit chorus branch consumes count then pitch");

        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        gm_world_set_block_meta(r.world,12,78,8,0,0);
        gm_world_set_block_meta(r.world,13,78,8,199,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4996) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(5582)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              r.piston_count==1 && r.entities.n_active==1 &&
              r.entities.ents[0].eid==4996 &&
              r.entities.ents[0].item==432 &&
              r.entities.ents[0].meta==0 &&
              r.entities.ents[0].age==0 &&
              r.entities.ents[0].pickup_delay==10 &&
              r.next_entity_id==4997 &&
              r.world_random_seed48==UINT64_C(0xF218F974BCBC) &&
              r.math_random_seed48==UINT64_C(0x33E01D26154D),
              "piston one-fruit chorus branch has exact item and cursors");

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        gm_world_set_block_meta(r.world,12,78,8,0,0);
        gm_world_set_block_meta(r.world,13,78,8,199,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4998) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(5582)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==199 &&
              r.piston_count==0 && r.entities.n_active==GM_LIVE_MAX &&
              r.next_entity_id==4998 &&
              r.world_random_seed48==UINT64_C(5582) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "chorus fruit piston branch rejects full pool atomically");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,13,77,8,3,0);
        gm_world_set_block_meta(r.world,13,78,8,175,4);
        gm_world_set_block_meta(r.world,13,79,8,175,10);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4999) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,13,77,8,0,0) &&
              gm_world_block(r.world,13,78,8)==0 &&
              gm_world_block(r.world,13,79,8)==0 &&
              r.entities.n_active==1 &&
              r.entities.ents[0].eid==4999 &&
              r.entities.ents[0].item==175 &&
              r.entities.ents[0].meta==4 &&
              r.next_entity_id==5000 &&
              r.world_random_seed48==UINT64_C(0x2D3873C4CD04) &&
              r.math_random_seed48==UINT64_C(0x33E01D26154D),
              "double-rose support loss drops lower variant and removes "
              "both halves in one neighbor boundary");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,3,0);
        gm_world_set_block_meta(r.world,13,78,8,175,4);
        gm_world_set_block_meta(r.world,13,79,8,175,10);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5000) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              gm_world_block(r.world,13,79,8)==0 &&
              r.piston_count==1 && r.entities.n_active==1 &&
              r.entities.ents[0].eid==5000 &&
              r.entities.ents[0].item==175 &&
              r.entities.ents[0].meta==4 &&
              r.next_entity_id==5001 &&
              r.world_random_seed48==UINT64_C(0x5D5692ACE2BF) &&
              r.math_random_seed48==UINT64_C(0x33E01D26154D),
              "piston destroys double-rose lower, drops its variant, and "
              "removes the upper half exactly");

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        gm_world_set_block_meta(r.world,12,78,8,0,0);
        gm_world_set_block_meta(r.world,13,78,8,175,4);
        gm_world_set_block_meta(r.world,13,79,8,175,10);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5002) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_meta(r.world,13,78,8)==4 &&
              gm_world_meta(r.world,13,79,8)==10 &&
              r.piston_count==0 && r.entities.n_active==GM_LIVE_MAX &&
              r.next_entity_id==5002 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "double-rose piston drop rejects full pool without partial "
              "RNG, entity, piston, or paired-block mutation");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        gm_world_set_block_meta(r.world,12,78,8,0,0);
        gm_world_set_block_meta(r.world,13,78,8,175,2);
        gm_world_set_block_meta(r.world,13,79,8,175,10);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5003) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              gm_world_block(r.world,13,79,8)==0 &&
              r.piston_count==1 && r.entities.n_active==1 &&
              r.entities.ents[0].eid==5003 &&
              r.entities.ents[0].item==295 &&
              r.entities.ents[0].meta==0 &&
              r.next_entity_id==5004 &&
              r.world_random_seed48==UINT64_C(0x17617168255E) &&
              r.math_random_seed48==UINT64_C(0x33E01D26154D),
              "double-grass seed branch uses World.rand then removes both "
              "halves exactly");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        gm_world_set_block_meta(r.world,12,78,8,0,0);
        gm_world_set_block_meta(r.world,13,78,8,175,2);
        gm_world_set_block_meta(r.world,13,79,8,175,10);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5005) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(1396)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              gm_world_block(r.world,13,79,8)==0 &&
              r.piston_count==1 && r.entities.n_active==0 &&
              r.next_entity_id==5005 &&
              r.world_random_seed48==UINT64_C(0x6E982FE6AB4E) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "double-grass no-drop branch consumes nextInt(8) before "
              "piston pitch and removes both halves");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        gm_world_set_block_meta(r.world,12,79,8,0,0);
        gm_world_set_block_meta(r.world,13,78,8,175,4);
        gm_world_set_block_meta(r.world,13,79,8,175,10);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5006) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,79,9,152,0) &&
              gm_runtime_set_block(&r,12,79,8,33,5) &&
              gm_world_meta(r.world,12,79,8)==13 &&
              gm_world_block(r.world,13,79,8)==36 &&
              gm_world_block(r.world,13,78,8)==0 &&
              r.piston_count==1 && r.entities.n_active==1 &&
              r.entities.ents[0].eid==5006 &&
              r.entities.ents[0].item==175 &&
              r.entities.ents[0].meta==4 &&
              r.next_entity_id==5007 &&
              r.world_random_seed48==UINT64_C(0x5D5692ACE2BF) &&
              r.math_random_seed48==UINT64_C(0x33E01D26154D),
              "piston destroys upper double-rose half then drops and removes "
              "the lower half from its ordered callback");

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        gm_world_set_block_meta(r.world,12,79,8,0,0);
        gm_world_set_block_meta(r.world,13,78,8,175,4);
        gm_world_set_block_meta(r.world,13,79,8,175,10);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5008) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,79,8,33,5) &&
              gm_world_meta(r.world,12,79,8)==5 &&
              gm_world_meta(r.world,13,79,8)==10 &&
              gm_world_meta(r.world,13,78,8)==4 &&
              r.piston_count==0 && r.entities.n_active==GM_LIVE_MAX &&
              r.next_entity_id==5008 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "upper-half piston predicts lower rose capacity before any "
              "paired-block, RNG, entity, or piston mutation");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=17;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,78,8,26,3);
        gm_world_set_block_meta(r.world,14,78,8,26,11);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5010) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              gm_world_block(r.world,14,78,8)==0 &&
              r.piston_count==1 && r.entities.n_active==1 &&
              r.entities.ents[0].eid==5010 &&
              r.entities.ents[0].item==355 &&
              r.entities.ents[0].meta==0 &&
              r.next_entity_id==5011 &&
              r.world_random_seed48==UINT64_C(0x5D5692ACE2BF) &&
              r.math_random_seed48==UINT64_C(0x33E01D26154D),
              "piston destroys bed foot, emits one bed item, and removes "
              "the head in the same neighbor boundary");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=17;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,15,78,9,152,0);
        gm_world_set_block_meta(r.world,13,78,8,26,3);
        gm_world_set_block_meta(r.world,14,78,8,26,11);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5012) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,15,78,8,33,4) &&
              gm_world_meta(r.world,15,78,8)==12 &&
              gm_world_block(r.world,14,78,8)==36 &&
              gm_world_block(r.world,13,78,8)==0 &&
              r.piston_count==1 && r.entities.n_active==1 &&
              r.entities.ents[0].eid==5012 &&
              r.entities.ents[0].item==355 &&
              r.entities.ents[0].meta==0 &&
              r.next_entity_id==5013 &&
              r.world_random_seed48==UINT64_C(0x5D5692ACE2BF) &&
              r.math_random_seed48==UINT64_C(0x33E01D26154D),
              "piston destroys bed head, then the foot callback emits one "
              "bed item at the paired position");

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        gm_world_set_block_meta(r.world,15,78,8,0,0);
        gm_world_set_block_meta(r.world,13,78,8,26,3);
        gm_world_set_block_meta(r.world,14,78,8,26,11);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5014) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,15,78,8,33,4) &&
              gm_world_meta(r.world,15,78,8)==4 &&
              gm_world_meta(r.world,14,78,8)==11 &&
              gm_world_meta(r.world,13,78,8)==3 &&
              r.piston_count==0 && r.entities.n_active==GM_LIVE_MAX &&
              r.next_entity_id==5014 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "bed-head piston predicts paired foot capacity before any "
              "block, RNG, entity, or piston mutation");

        {
            static const int door_blocks[7]={64,71,193,194,195,196,197};
            static const int door_items[7]={324,330,427,428,429,430,431};
            int door_lower_states=0;
            int door_upper_states=0;
            for(int door_i=0;door_i<7;++door_i){
                int door=door_blocks[door_i];
                int item=door_items[door_i];
                for(int lower_meta=0;lower_meta<8;++lower_meta){
                    int upper_meta=8+(lower_meta&3);
                    int eid=5100+door_i*16+lower_meta;
                    memset(&r.entities,0,sizeof r.entities);
                    r.piston_count=0;
                    memset(r.pistons,0,sizeof r.pistons);
                    for(int y=77;y<=80;++y)
                        for(int z=7;z<=10;++z)
                            for(int x=10;x<=16;++x)
                                gm_world_set_block_meta(
                                    r.world,x,y,z,0,0);
                    gm_world_set_block_meta(r.world,12,78,9,152,0);
                    gm_world_set_block_meta(r.world,13,77,8,1,0);
                    gm_world_set_block_meta(
                        r.world,13,78,8,door,lower_meta);
                    gm_world_set_block_meta(
                        r.world,13,79,8,door,upper_meta);
                    CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                          gm_runtime_set_world_random_seed48(
                              &r,UINT64_C(0)) &&
                          gm_runtime_set_math_random_seed48(
                              &r,UINT64_C(0x0FEDCBA98765)) &&
                          gm_runtime_set_block(&r,12,78,8,33,5) &&
                          gm_world_meta(r.world,12,78,8)==13 &&
                          gm_world_block(r.world,13,78,8)==36 &&
                          gm_world_block(r.world,13,79,8)==0 &&
                          r.piston_count==1 && r.entities.n_active==1 &&
                          r.entities.ents[0].eid==eid &&
                          r.entities.ents[0].item==item &&
                          r.entities.ents[0].count==1 &&
                          r.entities.ents[0].meta==0 &&
                          r.next_entity_id==eid+1 &&
                          r.world_random_seed48
                              ==UINT64_C(0x5D5692ACE2BF) &&
                          r.math_random_seed48
                              ==UINT64_C(0x33E01D26154D),
                          "all seven door types and eight lower states drop "
                          "one registered ItemDoor and remove the upper");
                    ++door_lower_states;
                }
                for(int upper_meta=8;upper_meta<=11;++upper_meta){
                    int lower_meta=upper_meta&3;
                    int eid=5300+door_i*8+upper_meta;
                    memset(&r.entities,0,sizeof r.entities);
                    r.piston_count=0;
                    memset(r.pistons,0,sizeof r.pistons);
                    for(int y=77;y<=80;++y)
                        for(int z=7;z<=10;++z)
                            for(int x=10;x<=16;++x)
                                gm_world_set_block_meta(
                                    r.world,x,y,z,0,0);
                    gm_world_set_block_meta(r.world,12,79,9,152,0);
                    gm_world_set_block_meta(r.world,13,77,8,1,0);
                    gm_world_set_block_meta(
                        r.world,13,78,8,door,lower_meta);
                    gm_world_set_block_meta(
                        r.world,13,79,8,door,upper_meta);
                    CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                          gm_runtime_set_world_random_seed48(
                              &r,UINT64_C(0)) &&
                          gm_runtime_set_math_random_seed48(
                              &r,UINT64_C(0x0FEDCBA98765)) &&
                          gm_runtime_set_block(&r,12,79,8,33,5) &&
                          gm_world_meta(r.world,12,79,8)==13 &&
                          gm_world_block(r.world,13,79,8)==36 &&
                          gm_world_block(r.world,13,78,8)==0 &&
                          r.piston_count==1 && r.entities.n_active==1 &&
                          r.entities.ents[0].eid==eid &&
                          r.entities.ents[0].item==item &&
                          r.entities.ents[0].count==1 &&
                          r.entities.ents[0].meta==0 &&
                          r.next_entity_id==eid+1 &&
                          r.world_random_seed48
                              ==UINT64_C(0x5D5692ACE2BF) &&
                          r.math_random_seed48
                              ==UINT64_C(0x33E01D26154D),
                          "all seven door types and four upper states defer "
                          "their one ItemDoor to the removed lower half");
                    ++door_upper_states;
                }
            }
            CHECK(door_lower_states==56 && door_upper_states==28,
                  "door piston tests cover every canonical paired state");
        }

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,1,0);
        gm_world_set_block_meta(r.world,13,78,8,140,0);
        CHECK(gm_runtime_flower_pot_set(&r,0,13,78,8,38,2) &&
              gm_runtime_set_entity_id_cursor(&r,5390) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              r.piston_count==1 && r.entities.n_active==2 &&
              r.entities.ents[0].eid==5390 &&
              r.entities.ents[0].item==390 &&
              r.entities.ents[0].count==1 &&
              r.entities.ents[0].meta==0 &&
              r.entities.ents[1].eid==5391 &&
              r.entities.ents[1].item==38 &&
              r.entities.ents[1].count==1 &&
              r.entities.ents[1].meta==2 &&
              gm_runtime_flower_pot_count(&r)==0 &&
              r.next_entity_id==5392 &&
              r.world_random_seed48==UINT64_C(0x86D91B38BCB3) &&
              r.math_random_seed48==UINT64_C(0x6B1C94DF7835),
              "piston destroys an occupied flower pot, drops pot then flower, "
              "and retires its tile state");

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX-1;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX-1;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        gm_world_set_block_meta(r.world,12,78,8,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,1,0);
        gm_world_set_block_meta(r.world,13,78,8,140,0);
        CHECK(gm_runtime_flower_pot_set(&r,0,13,78,8,38,2) &&
              gm_runtime_set_entity_id_cursor(&r,5391) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==140 &&
              gm_runtime_flower_pot_count(&r)==1 &&
              r.piston_count==0 &&
              r.entities.n_active==GM_LIVE_MAX-1 &&
              r.next_entity_id==5391 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "occupied flower-pot piston preflights both drops before "
              "using the final entity slot");

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        gm_world_set_block_meta(r.world,12,78,8,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,1,0);
        gm_world_set_block_meta(r.world,13,78,8,140,0);
        CHECK(gm_runtime_flower_pot_set(&r,0,13,78,8,38,2) &&
              gm_runtime_set_entity_id_cursor(&r,5391) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==140 &&
              gm_runtime_flower_pot_count(&r)==1 &&
              r.piston_count==0 && r.entities.n_active==GM_LIVE_MAX &&
              r.next_entity_id==5391 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "flower-pot piston rejects a full item pool without partial "
              "tile, block, RNG, entity, or piston mutation");

        {
            int skull_states=0;
            for(int skull_type=0;skull_type<=5;++skull_type)
                for(int rotation=0;rotation<=15;++rotation){
                    int eid=5420+skull_states;
                    memset(&r.entities,0,sizeof r.entities);
                    r.piston_count=0;
                    memset(r.pistons,0,sizeof r.pistons);
                    gm_world_set_block_meta(r.world,12,78,8,0,0);
                    gm_world_set_block_meta(r.world,12,78,9,152,0);
                    gm_world_set_block_meta(r.world,13,77,8,1,0);
                    gm_world_set_block_meta(r.world,13,78,8,144,1);
                    CHECK(gm_runtime_skull_set(
                              &r,0,13,78,8,skull_type,rotation) &&
                          gm_runtime_set_entity_id_cursor(&r,eid) &&
                          gm_runtime_set_world_random_seed48(
                              &r,UINT64_C(0)) &&
                          gm_runtime_set_math_random_seed48(
                              &r,UINT64_C(0x0FEDCBA98765)) &&
                          gm_runtime_set_block(&r,12,78,8,33,5) &&
                          gm_world_meta(r.world,12,78,8)==13 &&
                          gm_world_block(r.world,13,78,8)==36 &&
                          r.piston_count==1 && r.entities.n_active==1 &&
                          r.entities.ents[0].eid==eid &&
                          r.entities.ents[0].item==397 &&
                          r.entities.ents[0].count==1 &&
                          r.entities.ents[0].meta==skull_type &&
                          gm_runtime_skull_count(&r)==0 &&
                          r.next_entity_id==eid+1 &&
                          r.world_random_seed48
                              ==UINT64_C(0x5D5692ACE2BF) &&
                          r.math_random_seed48
                              ==UINT64_C(0x33E01D26154D),
                          "all ownerless skull types and rotations drop the "
                          "tile-owned ItemSkull metadata");
                    ++skull_states;
                }
            CHECK(skull_states==96,
                  "skull piston tests cover all ownerless type/rotation "
                  "states");
        }

        {
            static const unsigned char profile_nbt[] = {
                10,0,0,8,0,4,'N','a','m','e',
                0,10,'P','a','r','i','t','y','H','e','a','d',0,
            };
            static const unsigned char item_tag_nbt[] = {
                10,0,0,10,0,10,'S','k','u','l','l','O','w','n','e','r',
                8,0,4,'N','a','m','e',
                0,10,'P','a','r','i','t','y','H','e','a','d',0,0,
            };
            GmRuntimeTaggedItem tagged;
            memset(&r.entities,0,sizeof r.entities);
            r.piston_count=0;
            memset(r.pistons,0,sizeof r.pistons);
            gm_world_set_block_meta(r.world,12,78,8,0,0);
            gm_world_set_block_meta(r.world,12,78,9,152,0);
            gm_world_set_block_meta(r.world,13,77,8,1,0);
            gm_world_set_block_meta(r.world,13,78,8,144,1);
            CHECK(gm_runtime_skull_set_profile_nbt(
                      &r,0,13,78,8,3,7,
                      profile_nbt,sizeof profile_nbt) &&
                  gm_runtime_set_entity_id_cursor(&r,5510) &&
                  gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
                  gm_runtime_set_math_random_seed48(
                      &r,UINT64_C(0x0FEDCBA98765)) &&
                  gm_runtime_set_block(&r,12,78,8,33,5) &&
                  gm_world_block(r.world,13,78,8)==36 &&
                  r.entities.n_active==1 &&
                  r.entities.ents[0].eid==5510 &&
                  r.entities.ents[0].item==397 &&
                  r.entities.ents[0].meta==3 &&
                  gm_runtime_tagged_item_get_by_eid(&r,5510,&tagged) &&
                  tagged.tag.len==sizeof item_tag_nbt &&
                  !memcmp(tagged.tag.data,item_tag_nbt,sizeof item_tag_nbt) &&
                  gm_runtime_skull_count(&r)==0 &&
                  r.next_entity_id==5511 &&
                  r.world_random_seed48==UINT64_C(0x5D5692ACE2BF) &&
                  r.math_random_seed48==UINT64_C(0x33E01D26154D),
                  "player skull piston drop wraps the exact profile under "
                  "the ItemStack SkullOwner compound");
        }

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        gm_world_set_block_meta(r.world,12,78,8,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,1,0);
        gm_world_set_block_meta(r.world,13,78,8,144,1);
        CHECK(gm_runtime_skull_set(&r,0,13,78,8,5,11) &&
              gm_runtime_set_entity_id_cursor(&r,5520) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==144 &&
              gm_runtime_skull_count(&r)==1 && r.piston_count==0 &&
              r.entities.n_active==GM_LIVE_MAX &&
              r.next_entity_id==5520 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "skull piston rejects a full item pool without partial tile, "
              "block, RNG, entity, or piston mutation");

        {
            int shulker_states=0;
            for(int block=219;block<=234;++block)
                for(int facing=0;facing<=5;++facing){
                    int eid=5530+shulker_states;
                    GmRuntimeTaggedItem tagged;
                    memset(&r.entities,0,sizeof r.entities);
                    r.piston_count=0;
                    memset(r.pistons,0,sizeof r.pistons);
                    gm_world_set_block_meta(r.world,12,78,8,0,0);
                    gm_world_set_block_meta(r.world,12,78,9,152,0);
                    gm_world_set_block_meta(r.world,13,77,8,1,0);
                    gm_world_set_block_meta(
                        r.world,13,78,8,block,facing);
                    CHECK(gm_runtime_static_container_set_slot(
                              &r,0,13,78,8,0,1,64,0) &&
                          gm_runtime_set_entity_id_cursor(&r,eid) &&
                          gm_runtime_set_world_random_seed48(
                              &r,UINT64_C(0)) &&
                          gm_runtime_set_math_random_seed48(
                              &r,UINT64_C(0x0FEDCBA98765)) &&
                          gm_runtime_set_block(&r,12,78,8,33,5) &&
                          gm_world_meta(r.world,12,78,8)==13 &&
                          gm_world_block(r.world,13,78,8)==36 &&
                          r.piston_count==1 && r.entities.n_active==1 &&
                          r.entities.ents[0].eid==eid &&
                          r.entities.ents[0].item==block &&
                          r.entities.ents[0].count==1 &&
                          r.entities.ents[0].meta==0 &&
                          gm_runtime_static_container_count(&r)==0 &&
                          gm_runtime_tagged_item_get_by_eid(
                              &r,eid,&tagged) &&
                          tagged.item==block && tagged.size==27 &&
                          tagged.slots[0].item==1 &&
                          tagged.slots[0].count==64 &&
                          tagged.slots[0].meta==0 &&
                          r.next_entity_id==eid+1 &&
                          r.world_random_seed48
                              ==UINT64_C(0x2D3873C4CD04) &&
                          r.math_random_seed48
                              ==UINT64_C(0x33E01D26154D),
                          "all shulker colors and facings drop one tagged "
                          "box item retaining its plain inventory");
                    ++shulker_states;
                }
            CHECK(shulker_states==96,
                  "shulker piston tests cover all color/facing states");
        }

        {
            GmRuntimeTaggedItem tagged;
            int payload_empty=1;
            memset(&tagged,0,sizeof tagged);
            memset(&r.entities,0,sizeof r.entities);
            r.piston_count=0;
            memset(r.pistons,0,sizeof r.pistons);
            gm_world_set_block_meta(r.world,12,78,8,0,0);
            gm_world_set_block_meta(r.world,12,78,9,152,0);
            gm_world_set_block_meta(r.world,13,77,8,1,0);
            gm_world_set_block_meta(r.world,13,78,8,229,5);
            CHECK(gm_runtime_static_container_set_slot(
                      &r,0,13,78,8,0,0,0,0) &&
                  gm_runtime_shulker_set_item_tag_nbt(
                      &r,0,13,78,8,
                      empty_shulker_item_tag_nbt,
                      sizeof empty_shulker_item_tag_nbt) &&
                  gm_runtime_set_entity_id_cursor(&r,5626) &&
                  gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
                  gm_runtime_set_math_random_seed48(
                      &r,UINT64_C(0x0FEDCBA98765)) &&
                  gm_runtime_set_block(&r,12,78,8,33,5) &&
                  gm_runtime_tagged_item_get_by_eid(&r,5626,&tagged) &&
                  tagged.item==229 && tagged.size==27 &&
                  tagged.tag.len==sizeof empty_shulker_item_tag_nbt &&
                  !memcmp(tagged.tag.data,empty_shulker_item_tag_nbt,
                      sizeof empty_shulker_item_tag_nbt) &&
                  gm_runtime_static_container_count(&r)==0 &&
                  r.world_random_seed48==UINT64_C(0x2D3873C4CD04) &&
                  r.math_random_seed48==UINT64_C(0x33E01D26154D),
                  "empty shulker retains an exact empty BlockEntityTag");
            for(int slot=0;slot<GM_RUNTIME_STATIC_CONTAINER_SLOTS;++slot)
                if(!isr_is_empty(&tagged.slots[slot]))
                    payload_empty=0;
            CHECK(payload_empty,
                  "empty shulker tagged item has twenty-seven empty slots");
        }

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        gm_world_set_block_meta(r.world,12,78,8,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,1,0);
        gm_world_set_block_meta(r.world,13,78,8,229,5);
        CHECK(gm_runtime_static_container_set_slot(
                  &r,0,13,78,8,0,1,64,0) &&
              gm_runtime_set_entity_id_cursor(&r,5630) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==229 &&
              gm_runtime_static_container_count(&r)==1 &&
              r.piston_count==0 && r.entities.n_active==GM_LIVE_MAX &&
              r.next_entity_id==5630 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "shulker piston rejects a full item pool without partial tile, "
              "block, RNG, entity, or piston mutation");

        memset(&r.entities,0,sizeof r.entities);
        for(int i=0;i<GM_LIVE_MAX;++i)
            r.entities.ents[i].active=1;
        r.entities.n_active=GM_LIVE_MAX;
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,77,8,1,0);
        gm_world_set_block_meta(r.world,13,78,8,64,3);
        /* Keep the saved pair internally unpowered. Metadata 11 carries the
         * upper POWERED bit without a source, so the newly modeled door
         * neighbor callback correctly normalizes it during piston placement. */
        gm_world_set_block_meta(r.world,13,79,8,64,9);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5400) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_meta(r.world,13,78,8)==3 &&
              gm_world_meta(r.world,13,79,8)==9 &&
              r.piston_count==0 && r.entities.n_active==GM_LIVE_MAX &&
              r.next_entity_id==5400 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "lower-door piston rejects a full pool without partial state");

        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        gm_world_set_block_meta(r.world,12,78,8,0,0);
        gm_world_set_block_meta(r.world,12,78,9,0,0);
        gm_world_set_block_meta(r.world,12,79,9,152,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5401) &&
              gm_runtime_set_world_random_seed48(&r,UINT64_C(0)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,79,8,33,5) &&
              gm_world_meta(r.world,12,79,8)==5 &&
              gm_world_meta(r.world,13,78,8)==3 &&
              gm_world_meta(r.world,13,79,8)==9 &&
              r.piston_count==0 && r.entities.n_active==GM_LIVE_MAX &&
              r.next_entity_id==5401 &&
              r.world_random_seed48==UINT64_C(0) &&
              r.math_random_seed48==UINT64_C(0x0FEDCBA98765),
              "upper-door piston predicts lower item capacity atomically");

        {
            int sapling_states=0;
            for(int meta=0;meta<16;++meta){
                int canonical=(meta<=5)||(meta>=8&&meta<=13);
                int eid=4800+meta;
                memset(&r.entities,0,sizeof r.entities);
                r.piston_count=0;
                memset(r.pistons,0,sizeof r.pistons);
                for(int y=77;y<=80;++y)
                    for(int z=7;z<=10;++z)
                        for(int x=10;x<=16;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_world_set_block_meta(r.world,12,78,9,152,0);
                gm_world_set_block_meta(r.world,13,77,8,3,0);
                gm_world_set_block_meta(r.world,13,78,8,6,meta);
                CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                      gm_runtime_set_world_random_seed48(
                          &r,UINT64_C(0x123456789ABC)) &&
                      gm_runtime_set_math_random_seed48(
                          &r,UINT64_C(0x0FEDCBA98765)) &&
                      gm_runtime_set_block(&r,12,78,8,33,5),
                      "sapling piston fixture applies its tick-zero edit");
                if(canonical){
                    ++sapling_states;
                    CHECK(gm_world_meta(r.world,12,78,8)==13 &&
                          gm_world_block(r.world,13,78,8)==36 &&
                          r.piston_count==1 &&
                          r.entities.n_active==1 &&
                          r.entities.ents[0].eid==eid &&
                          r.entities.ents[0].item==6 &&
                          r.entities.ents[0].count==1 &&
                          r.entities.ents[0].meta==(meta&7),
                          "all 12 sapling type/stage states preserve only "
                          "wood type in exact piston item metadata");
                }else{
                    CHECK(gm_world_meta(r.world,12,78,8)==5 &&
                          gm_world_block(r.world,13,78,8)==6 &&
                          gm_world_meta(r.world,13,78,8)==meta &&
                          r.piston_count==0 &&
                          r.entities.n_active==0 &&
                          r.next_entity_id==eid &&
                          r.world_random_seed48
                              ==UINT64_C(0x123456789ABC) &&
                          r.math_random_seed48
                              ==UINT64_C(0x0FEDCBA98765),
                          "noncanonical sapling raw metadata remains "
                          "visible without partial piston state");
                }
            }
            CHECK(sapling_states==12,
                  "sapling payload test covers 12 canonical states");
        }

        /* Two successful extensions in one neighbor-notification pass expose
         * the post-drop piston sound draw.  WEST is notified before EAST, so
         * the second entity's offsets depend on exactly one intervening
         * World.rand nextFloat with no client work between the two events. */
        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=9;++z)
                for(int x=9;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,10,77,8,3,0);
        gm_world_set_block_meta(r.world,10,78,8,6,0);
        gm_world_set_block_meta(r.world,11,78,8,33,4);
        gm_world_set_block_meta(r.world,13,78,8,33,5);
        gm_world_set_block_meta(r.world,14,77,8,3,0);
        gm_world_set_block_meta(r.world,14,78,8,6,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4326) &&
              gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(135120319782334)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(252261458320234)) &&
              gm_runtime_set_block(&r,12,78,8,152,0) &&
              r.world_random_seed48==UINT64_C(94745312360688) &&
              r.math_random_seed48==UINT64_C(259689059456890) &&
              r.entities.n_active==2 &&
              r.entities.ents[0].eid==4326 &&
              r.entities.ents[1].eid==4327 &&
              r.entities.ents[0].item==6 &&
              r.entities.ents[1].item==6 &&
              gm_world_meta(r.world,11,78,8)==12 &&
              gm_world_meta(r.world,13,78,8)==13,
              "dual sapling pistons retain exact pitch RNG and entity cursors");
        gm_runtime_tick(&r,idle);
        CHECK(r.entities.ents[0].active &&
              r.entities.ents[1].active &&
              r.entities.ents[0].age==1 &&
              r.entities.ents[1].age==1 &&
              r.entities.ents[0].pickup_delay==9 &&
              r.entities.ents[1].pickup_delay==9 &&
              r.next_entity_id==4328,
              "both dual-piston item drops tick once at the shared boundary");

        for(int mushroom=39;mushroom<=40;++mushroom){
            int eid=4800+mushroom;
            memset(&r.entities,0,sizeof r.entities);
            r.piston_count=0;
            memset(r.pistons,0,sizeof r.pistons);
            for(int y=77;y<=80;++y)
                for(int z=7;z<=10;++z)
                    for(int x=10;x<=16;++x)
                        gm_world_set_block_meta(r.world,x,y,z,0,0);
            gm_world_set_block_meta(r.world,12,78,9,152,0);
            gm_world_set_block_meta(r.world,13,77,8,110,0);
            gm_world_set_block_meta(r.world,13,78,8,mushroom,0);
            CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                  gm_runtime_set_world_random_seed48(
                      &r,UINT64_C(0x123456789ABC)) &&
                  gm_runtime_set_math_random_seed48(
                      &r,UINT64_C(0x0FEDCBA98765)) &&
                  gm_runtime_set_block(&r,12,78,8,33,5) &&
                  gm_world_meta(r.world,12,78,8)==13 &&
                  gm_world_block(r.world,13,78,8)==36 &&
                  r.piston_count==1 &&
                  r.entities.n_active==1 &&
                  r.entities.ents[0].eid==eid &&
                  r.entities.ents[0].item==mushroom &&
                  r.entities.ents[0].count==1 &&
                  r.entities.ents[0].meta==0 &&
                  r.entities.ents[0].age==0 &&
                  r.entities.ents[0].pickup_delay==10,
                  "brown/red mushroom maps to its exact one-item piston drop");
            gm_runtime_tick(&r,idle);
            CHECK(r.entities.ents[0].active &&
                  r.entities.ents[0].age==1 &&
                  r.entities.ents[0].pickup_delay==9 &&
                  fabs(r.entities.ents[0].x-13.635)<1.0e-12,
                  "mushroom drop follows the exact moving-head sweep");
        }

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,8,33,5);
        gm_world_set_block_meta(r.world,13,78,8,65,5);
        CHECK(gm_runtime_set_entity_id_cursor(&r,4949) &&
              gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x123456789ABC)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,9,152,0) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              r.piston_count==1 &&
              r.entities.n_active==1 &&
              r.entities.ents[0].eid==4949 &&
              r.entities.ents[0].item==65 &&
              r.entities.ents[0].count==1 &&
              r.entities.ents[0].meta==0 &&
              r.entities.ents[0].age==0 &&
              r.entities.ents[0].pickup_delay==10,
              "attached ladder strips orientation in its exact piston drop");
        gm_runtime_tick(&r,idle);
        CHECK(r.entities.ents[0].active &&
              r.entities.ents[0].age==1 &&
              r.entities.ents[0].pickup_delay==9 &&
              fabs(r.entities.ents[0].x-13.635)<1.0e-12,
              "attached ladder drop follows the exact moving-head sweep");

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,78,8,30,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5050) &&
              gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x123456789ABC)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              r.piston_count==1 &&
              r.entities.n_active==1 &&
              r.entities.ents[0].eid==5050 &&
              r.entities.ents[0].item==287 &&
              r.entities.ents[0].count==1 &&
              r.entities.ents[0].meta==0 &&
              r.entities.ents[0].age==0 &&
              r.entities.ents[0].pickup_delay==10,
              "cobweb piston drop maps block 30 to string item 287");
        gm_runtime_tick(&r,idle);
        CHECK(r.entities.ents[0].active &&
              r.entities.ents[0].age==1 &&
              r.entities.ents[0].pickup_delay==9 &&
              fabs(r.entities.ents[0].x-13.635)<1.0e-12,
              "string drop follows the exact moving-head sweep");

        {
            const int pumpkins[2]={86,91};
            for(int pumpkin_i=0;pumpkin_i<2;++pumpkin_i){
                int pumpkin=pumpkins[pumpkin_i];
                int eid=5100+pumpkin;
                memset(&r.entities,0,sizeof r.entities);
                r.piston_count=0;
                memset(r.pistons,0,sizeof r.pistons);
                for(int y=77;y<=80;++y)
                    for(int z=7;z<=10;++z)
                        for(int x=10;x<=16;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_world_set_block_meta(r.world,12,78,9,152,0);
                gm_world_set_block_meta(r.world,13,78,8,pumpkin,3);
                CHECK(gm_runtime_set_entity_id_cursor(&r,eid) &&
                      gm_runtime_set_world_random_seed48(
                          &r,UINT64_C(0x123456789ABC)) &&
                      gm_runtime_set_math_random_seed48(
                          &r,UINT64_C(0x0FEDCBA98765)) &&
                      gm_runtime_set_block(&r,12,78,8,33,5) &&
                      gm_world_meta(r.world,12,78,8)==13 &&
                      gm_world_block(r.world,13,78,8)==36 &&
                      r.piston_count==1 &&
                      r.entities.n_active==1 &&
                      r.entities.ents[0].eid==eid &&
                      r.entities.ents[0].item==pumpkin &&
                      r.entities.ents[0].count==1 &&
                      r.entities.ents[0].meta==0 &&
                      r.entities.ents[0].age==0 &&
                      r.entities.ents[0].pickup_delay==10,
                      "ordinary/lit pumpkin strips facing in its piston drop");
                gm_runtime_tick(&r,idle);
                CHECK(r.entities.ents[0].active &&
                      r.entities.ents[0].age==1 &&
                      r.entities.ents[0].pickup_delay==9 &&
                      fabs(r.entities.ents[0].x-13.635)<1.0e-12,
                      "pumpkin drop follows the exact moving-head sweep");
            }
        }

        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);
        for(int y=77;y<=80;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=16;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,9,152,0);
        gm_world_set_block_meta(r.world,13,78,8,1,0);
        gm_world_set_block_meta(r.world,14,77,8,1,0);
        gm_world_set_block_meta(r.world,14,78,8,37,0);
        CHECK(gm_runtime_set_entity_id_cursor(&r,5252) &&
              gm_runtime_set_world_random_seed48(
                  &r,UINT64_C(0x123456789ABC)) &&
              gm_runtime_set_math_random_seed48(
                  &r,UINT64_C(0x0FEDCBA98765)) &&
              gm_runtime_set_block(&r,12,78,8,33,5) &&
              gm_world_meta(r.world,12,78,8)==13 &&
              gm_world_block(r.world,13,78,8)==36 &&
              gm_world_block(r.world,14,78,8)==36 &&
              r.piston_count==2 &&
              r.entities.n_active==1 &&
              r.entities.ents[0].eid==5252 &&
              r.entities.ents[0].item==37,
              "terminal dandelion is destroyed before one ordinary block moves");
        gm_runtime_tick(&r,idle);
        CHECK(r.entities.ents[0].active &&
              r.entities.ents[0].age==1 &&
              r.entities.ents[0].pickup_delay==9 &&
              fabs(r.entities.ents[0].x-14.635)<1.0e-12,
              "moved ordinary block sweeps its terminal destroy-reaction drop");
        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);

        {
            for(int facing=0;facing<6;++facing){
                double expected_x=20.5;
                double expected_y=80.5;
                double expected_z=20.5;
                double item_x=20.5;
                double item_y=80.5;
                double item_z=20.5;
                int sign=(facing&1)?1:-1;
                int axis=facing<2?1:(facing<4?2:0);
                memset(&r.entities,0,sizeof r.entities);
                r.piston_count=1;
                memset(r.pistons,0,sizeof r.pistons);
                r.pistons[0]=(GmRuntimePiston){
                    .active=1,.dimension=r.dimension,
                    .x=20,.y=80,.z=20,
                    .moved_block=1,.moved_meta=0,
                    .facing=facing,.extending=1,
                    .progress=0.0f,.last_progress=0.0f
                };
                if(axis==0){
                    item_x=20.5-0.25*(double)sign;
                    expected_x=20.5+0.135*(double)sign;
                }else if(axis==1){
                    item_y=80.5-0.25*(double)sign;
                    expected_y=sign>0?80.51:80.24;
                }else{
                    item_z=20.5-0.25*(double)sign;
                    expected_z=20.5+0.135*(double)sign;
                }
                gm_world_set_block_meta(
                    r.world,20,80,20,36,facing);
                CHECK(gm_live_spawn_item_exact(
                          &r.entities,6000+facing,
                          item_x,item_y,item_z,
                          0.0,0.0,0.0,0.0f,
                          1,1,0,0,10,1),
                      "six-face piston sweep fixture spawns controlled item");
                gm_runtime_tick(&r,idle);
                CHECK(fabs(r.entities.ents[0].x-expected_x)<1.0e-12 &&
                      fabs(r.entities.ents[0].y-expected_y)<1.0e-12 &&
                      fabs(r.entities.ents[0].z-expected_z)<1.0e-12,
                      "moving full block sweeps item in all six directions");
                gm_world_set_block_meta(r.world,20,80,20,0,0);
            }
            {
                const EwStore *mobs;
                for(int y=78;y<=82;++y)
                    for(int z=18;z<=22;++z)
                        for(int x=18;x<=22;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_mobs_init(&r.mobs,0);
                r.mobs_enabled=0;
                r.piston_count=1;
                memset(r.pistons,0,sizeof r.pistons);
                r.pistons[0]=(GmRuntimePiston){
                    .active=1,.dimension=r.dimension,
                    .x=20,.y=80,.z=20,
                    .moved_block=34,.moved_meta=5,
                    .facing=5,.extending=1,.source=1,
                    .progress=0.0f,.last_progress=0.0f
                };
                gm_world_set_block_meta(r.world,20,80,20,36,5);
                gm_world_set_block_meta(r.world,21,80,20,1,0);
                CHECK(gm_runtime_spawn_mob_fixture(
                          &r,GM_MOB_PIG,7200,20.5,80.0,20.5,
                          0.0,0.0,0.0,0.0f,10.0f,1,0,0,0),
                      "wall-clipped piston pig fixture spawns");
                gm_runtime_tick(&r,idle);
                mobs=r.mobs.current?&r.mobs.b:&r.mobs.a;
                CHECK(fabs(mobs->x[1]-20.55000001192093)<1.0e-9 &&
                      gm_world_block(r.world,21,80,20)==1,
                      "piston push clips living mob at a static full cube");
                gm_runtime_tick(&r,idle);
                mobs=r.mobs.current?&r.mobs.b:&r.mobs.a;
                CHECK(fabs(mobs->x[1]-20.55000001192093)<1.0e-9,
                      "wall blocks the remaining piston living displacement");
            }
            {
                const EwStore *mobs;
                for(int y=78;y<=82;++y)
                    for(int z=18;z<=22;++z)
                        for(int x=18;x<=22;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_mobs_init(&r.mobs,0);
                r.mobs_enabled=0;
                r.piston_count=1;
                memset(r.pistons,0,sizeof r.pistons);
                r.pistons[0]=(GmRuntimePiston){
                    .active=1,.dimension=r.dimension,
                    .x=20,.y=80,.z=20,
                    .moved_block=34,.moved_meta=0,
                    .facing=0,.extending=1,.source=1,
                    .progress=0.0f,.last_progress=0.0f
                };
                gm_world_set_block_meta(r.world,20,80,20,36,0);
                gm_world_set_block_meta(r.world,20,79,20,88,0);
                CHECK(gm_runtime_spawn_mob_fixture(
                          &r,GM_MOB_PIG,7201,20.5,80.0,20.5,
                          0.0,0.0,0.0,0.0f,10.0f,1,0,0,0),
                      "soul-sand piston pig fixture spawns");
                gm_runtime_tick(&r,idle);
                mobs=r.mobs.current?&r.mobs.b:&r.mobs.a;
                CHECK(fabs(mobs->y[1]-79.875)<1.0e-12,
                      "down piston push uses soul sand's 7/8 top face");
                gm_runtime_tick(&r,idle);
                mobs=r.mobs.current?&r.mobs.b:&r.mobs.a;
                CHECK(fabs(mobs->y[1]-79.875)<1.0e-12,
                      "soul sand blocks the remaining downward displacement");
            }
            {
                const int slab_id[4]={44,126,182,205};
                const int slab_meta[4]={0,8,0,8};
                const double first_y[4]={
                    79.59000002384185,80.0,79.59000002384185,80.0};
                const double second_y[4]={79.5,80.0,79.5,80.0};
                for(int slab=0;slab<4;++slab){
                    const EwStore *mobs;
                    for(int y=78;y<=82;++y)
                        for(int z=18;z<=22;++z)
                            for(int x=18;x<=22;++x)
                                gm_world_set_block_meta(r.world,x,y,z,0,0);
                    gm_mobs_init(&r.mobs,0);
                    r.mobs_enabled=0;
                    r.piston_count=1;
                    memset(r.pistons,0,sizeof r.pistons);
                    r.pistons[0]=(GmRuntimePiston){
                        .active=1,.dimension=r.dimension,
                        .x=20,.y=80,.z=20,
                        .moved_block=34,.moved_meta=0,
                        .facing=0,.extending=1,.source=1,
                        .progress=0.0f,.last_progress=0.0f
                    };
                    gm_world_set_block_meta(r.world,20,80,20,36,0);
                    gm_world_set_block_meta(
                        r.world,20,79,20,slab_id[slab],slab_meta[slab]);
                    CHECK(gm_runtime_spawn_mob_fixture(
                              &r,GM_MOB_PIG,7202+slab,20.5,80.0,20.5,
                              0.0,0.0,0.0,0.0f,10.0f,1,0,0,0),
                          "single-slab piston pig fixture spawns");
                    gm_runtime_tick(&r,idle);
                    mobs=r.mobs.current?&r.mobs.b:&r.mobs.a;
                    CHECK(fabs(mobs->y[1]-first_y[slab])<1.0e-12,
                          "down piston half-step clips at exact slab face");
                    gm_runtime_tick(&r,idle);
                    mobs=r.mobs.current?&r.mobs.b:&r.mobs.a;
                    CHECK(fabs(mobs->y[1]-second_y[slab])<1.0e-12,
                          "single slab blocks settled downward displacement");
                }
            }
            {
                static const int stair_id[14]={
                    53,67,108,109,114,128,134,135,136,156,163,164,180,203};
                for(int stair=0;stair<14;++stair)
                    for(int top=0;top<2;++top)
                        for(int meta=0;meta<4;++meta)
                            for(int upper=0;upper<2;++upper){
                                const EwStore *mobs;
                                int facing=5-meta;
                                double x=20.5,z=20.5;
                                double first_y=top||upper
                                    ?80.0:79.59000002384185;
                                double second_y=top||upper?80.0:79.5;
                                for(int y=78;y<=82;++y)
                                    for(int zz=18;zz<=22;++zz)
                                        for(int xx=18;xx<=22;++xx)
                                            gm_world_set_block_meta(
                                                r.world,xx,y,zz,0,0);
                                if(facing==5) x=upper?21.0:20.0;
                                else if(facing==4) x=upper?20.0:21.0;
                                else if(facing==3) z=upper?21.0:20.0;
                                else z=upper?20.0:21.0;
                                gm_mobs_init(&r.mobs,0);
                                r.mobs_enabled=0;
                                r.piston_count=1;
                                memset(r.pistons,0,sizeof r.pistons);
                                r.pistons[0]=(GmRuntimePiston){
                                    .active=1,.dimension=r.dimension,
                                    .x=20,.y=80,.z=20,
                                    .moved_block=34,.moved_meta=0,
                                    .facing=0,.extending=1,.source=1,
                                    .progress=0.0f,.last_progress=0.0f
                                };
                                gm_world_set_block_meta(
                                    r.world,20,80,20,36,0);
                                gm_world_set_block_meta(
                                    r.world,20,79,20,stair_id[stair],
                                    meta|(top?4:0));
                                CHECK(gm_runtime_spawn_mob_fixture(
                                          &r,GM_MOB_PIG,
                                          7300+stair*16+top*8+meta*2+upper,
                                          x,80.0,z,0.0,0.0,0.0,0.0f,
                                          10.0f,1,0,0,0),
                                      "stair piston pig fixture spawns");
                                gm_runtime_tick(&r,idle);
                                mobs=r.mobs.current?&r.mobs.b:&r.mobs.a;
                                CHECK(fabs(mobs->y[1]-first_y)<1.0e-12,
                                      "stair facing and half clip at exact face");
                                gm_runtime_tick(&r,idle);
                                mobs=r.mobs.current?&r.mobs.b:&r.mobs.a;
                                CHECK(fabs(mobs->y[1]-second_y)<1.0e-12,
                                      "all stair registry IDs settle exactly");
                            }
            }
            {
                static const double x[3]={20.5,20.5,20.0};
                static const double z[3]={20.0,21.0,21.0};
                static const double first_y[3]={
                    80.0,79.59000002384185,79.59000002384185};
                static const double second_y[3]={80.0,79.5,79.5};
                for(int shape=0;shape<3;++shape){
                    const EwStore *mobs;
                    for(int y=78;y<=82;++y)
                        for(int zz=18;zz<=22;++zz)
                            for(int xx=18;xx<=22;++xx)
                                gm_world_set_block_meta(r.world,xx,y,zz,0,0);
                    gm_mobs_init(&r.mobs,0);
                    r.mobs_enabled=0;
                    r.piston_count=1;
                    memset(r.pistons,0,sizeof r.pistons);
                    r.pistons[0]=(GmRuntimePiston){
                        .active=1,.dimension=r.dimension,
                        .x=20,.y=80,.z=20,
                        .moved_block=34,.moved_meta=0,
                        .facing=0,.extending=1,.source=1,
                        .progress=0.0f,.last_progress=0.0f
                    };
                    gm_world_set_block_meta(r.world,20,80,20,36,0);
                    gm_world_set_block_meta(r.world,20,79,20,53,0);
                    if(shape<2)
                        gm_world_set_block_meta(r.world,21,79,20,53,3);
                    else
                        gm_world_set_block_meta(r.world,19,79,20,53,3);
                    CHECK(gm_runtime_spawn_mob_fixture(
                              &r,GM_MOB_PIG,7600+shape,x[shape],80.0,z[shape],
                              0.0,0.0,0.0,0.0f,10.0f,1,0,0,0),
                          "connected-stair piston pig fixture spawns");
                    gm_runtime_tick(&r,idle);
                    mobs=r.mobs.current?&r.mobs.b:&r.mobs.a;
                    CHECK(fabs(mobs->y[1]-first_y[shape])<1.0e-12,
                          "outer and inner stair first-step shapes match");
                    gm_runtime_tick(&r,idle);
                    mobs=r.mobs.current?&r.mobs.b:&r.mobs.a;
                    CHECK(fabs(mobs->y[1]-second_y[shape])<1.0e-12,
                          "outer and inner stair settled shapes match");
                }
            }
            {
                static const int fence_id[7]={85,113,188,189,190,191,192};
                double y[2];
                for(int fence=0;fence<7;++fence)
                    CHECK(down_piston_pig_shape_probe(
                              &r,idle,fence_id[fence],0,0,0,0,
                              20.5,20.5,7700+fence,y) &&
                          fabs(y[0]-80.5)<1.0e-12 &&
                          fabs(y[1]-80.5)<1.0e-12,
                          "all fence registry posts clip at 1.5 blocks");
                for(int direction=0;direction<4;++direction){
                    static const double x[4]={20.0,21.1,20.0,19.9};
                    static const double z[4]={19.9,20.0,21.1,20.0};
                    CHECK(down_piston_pig_shape_probe(
                              &r,idle,85,0,1<<direction,107,4,
                              x[direction],z[direction],
                              7710+direction,y) &&
                          fabs(y[0]-80.5)<1.0e-12 &&
                          fabs(y[1]-80.5)<1.0e-12,
                          "fence collision arms follow all four connections");
                }
                CHECK(down_piston_pig_shape_probe(
                          &r,idle,85,0,(1<<0)|(1<<1),107,4,
                          21.1,19.9,7714,y) &&
                      fabs(y[0]-79.99)<1.0e-12 &&
                      fabs(y[1]-79.47999999999999)<1.0e-12,
                      "fence L-shape keeps its outer corner empty");
                for(int exception=0;exception<2;++exception){
                    int neighbor=exception?218:152;
                    CHECK(down_piston_pig_shape_probe(
                              &r,idle,85,0,1<<0,neighbor,0,
                              20.0,19.9,7715+exception,y) &&
                          fabs(y[0]-80.5)<1.0e-12 &&
                          fabs(y[1]-80.5)<1.0e-12,
                          "opaque full non-normal cubes connect to fences");
                }
                CHECK(down_piston_pig_shape_probe(
                          &r,idle,85,0,1<<0,86,0,
                          20.0,19.9,7717,y) &&
                      fabs(y[0]-80.0)<1.0e-12 &&
                      fabs(y[1]-80.0)<1.0e-12,
                      "full-cube gourd does not create a fence arm");
            }
            {
                static const int gate_id[6]={107,183,184,185,186,187};
                double y[2];
                for(int gate=0;gate<6;++gate)
                    for(int meta=0;meta<8;++meta){
                        double expected0=(meta&4)?79.99:80.5;
                        double expected1=(meta&4)?79.47999999999999:80.5;
                        CHECK(down_piston_pig_shape_probe(
                                  &r,idle,gate_id[gate],meta,0,0,0,
                                  20.5,20.5,7720+gate*8+meta,y) &&
                              fabs(y[0]-expected0)<1.0e-12 &&
                              fabs(y[1]-expected1)<1.0e-12,
                              "all gate IDs, axes, and open states clip exactly");
                    }
            }
            {
                static const double arm_x[4]={20.0,21.21,20.0,19.79};
                static const double arm_z[4]={19.79,20.0,21.21,20.0};
                double y[2];
                for(int variant=0;variant<2;++variant)
                    CHECK(down_piston_pig_shape_probe(
                              &r,idle,139,variant,0,0,0,
                              20.5,20.5,7800+variant,y) &&
                          fabs(y[0]-80.5)<1.0e-12 &&
                          fabs(y[1]-80.5)<1.0e-12,
                          "both wall variants use the 1.5-block collision post");
                for(int direction=0;direction<4;++direction)
                    CHECK(down_piston_pig_shape_probe(
                              &r,idle,139,0,1<<direction,107,4,
                              arm_x[direction],arm_z[direction],
                              7810+direction,y) &&
                          fabs(y[0]-80.5)<1.0e-12 &&
                          fabs(y[1]-80.5)<1.0e-12,
                          "wall collision box follows all four connections");
                CHECK(down_piston_pig_shape_probe(
                          &r,idle,139,0,(1<<0)|(1<<2),107,4,
                          19.86,20.5,7814,y) &&
                      fabs(y[0]-79.99)<1.0e-12 &&
                      fabs(y[1]-79.47999999999999)<1.0e-12,
                      "north-south wall narrows to a 3/8 collision strip");
                CHECK(down_piston_pig_shape_probe(
                          &r,idle,139,0,(1<<1)|(1<<3),107,4,
                          20.5,19.86,7815,y) &&
                      fabs(y[0]-79.99)<1.0e-12 &&
                      fabs(y[1]-79.47999999999999)<1.0e-12,
                      "east-west wall narrows to a 3/8 collision strip");
            }
            {
                double y[2];
                for(int meta=0;meta<16;++meta){
                    double surface=79.0+(double)(meta&7)*0.125;
                    double expected0=surface>79.59000002384185
                        ?surface:79.59000002384185;
                    double expected1=surface>79.09000002384185
                        ?surface:79.09000002384185;
                    CHECK(down_piston_pig_shape_probe_at_y(
                              &r,idle,78,meta,0,0,0,
                              80.0,20.5,20.5,7900+meta,y) &&
                          fabs(y[0]-expected0)<1.0e-12 &&
                          fabs(y[1]-expected1)<1.0e-12,
                          "all snow metadata states collide one layer low");
                }
                for(int meta=0;meta<16;++meta){
                    double x[2];
                    CHECK(east_piston_pig_shape_probe(
                              &r,idle,171,meta,80.0,7920+meta,x) &&
                          fabs(x[0]-20.55000001192093)<1.0e-9 &&
                          fabs(x[1]-20.55000001192093)<1.0e-9,
                          "all carpet colors block a low horizontal sweep");
                }
                for(int meta=0;meta<16;++meta)
                    CHECK(down_piston_pig_shape_probe_at_y(
                              &r,idle,26,meta,0,0,0,
                              80.0,20.5,20.5,7930+meta,y) &&
                          fabs(y[0]-79.59000002384185)<1.0e-12 &&
                          fabs(y[1]-79.5625)<1.0e-12,
                          "both bed parts and all facing bits collide at 9/16");
                for(int bites=0;bites<=6;++bites)
                    CHECK(down_piston_pig_shape_probe_at_y(
                              &r,idle,92,bites,0,0,0,
                              80.0,20.5,20.5,7950+bites,y) &&
                          fabs(y[0]-79.59000002384185)<1.0e-12 &&
                          fabs(y[1]-79.5)<1.0e-12,
                          "all cake bite states retain their 1/2-high slice");
                CHECK(down_piston_pig_shape_probe_at_y(
                          &r,idle,92,6,0,0,0,
                          80.0,19.7,20.5,7957,y) &&
                      fabs(y[0]-79.59000002384185)<1.0e-12 &&
                      fabs(y[1]-79.09000002384185)<1.0e-12,
                      "six-bite cake keeps its eaten west edge empty");
                CHECK(down_piston_pig_shape_probe_at_y(
                          &r,idle,116,0,0,0,0,
                          80.0,20.5,20.5,7960,y) &&
                      fabs(y[0]-79.75)<1.0e-12 &&
                      fabs(y[1]-79.75)<1.0e-12,
                      "enchanting table clips at 3/4 height");
                for(int inverted=0;inverted<2;++inverted)
                    for(int meta=0;meta<16;++meta)
                        CHECK(down_piston_pig_shape_probe_at_y(
                                  &r,idle,inverted?178:151,meta,0,0,0,
                                  80.0,20.5,20.5,
                                  7970+inverted*16+meta,y) &&
                              fabs(y[0]-79.59000002384185)<1.0e-12 &&
                              fabs(y[1]-79.375)<1.0e-12,
                              "both daylight detector registries clip at 3/8");
                {
                    static const int diode_id[4]={93,94,149,150};
                    for(int block=0;block<4;++block)
                        for(int meta=0;meta<16;++meta)
                            CHECK(down_piston_pig_shape_probe_at_y(
                                      &r,idle,diode_id[block],meta,0,0,0,
                                      80.0,20.5,20.5,
                                      8900+block*16+meta,y) &&
                                  fabs(y[0]-79.59000002384185)<1.0e-12 &&
                                  fabs(y[1]-79.125)<1.0e-12,
                                  "all repeater/comparator states clip at 1/8");
                }
                for(int meta=0;meta<8;++meta){
                    CHECK(down_piston_pig_shape_probe_at_y(
                              &r,idle,117,meta,0,0,0,
                              80.0,20.5,20.5,9000+meta,y) &&
                          fabs(y[0]-79.875)<1.0e-12 &&
                          fabs(y[1]-79.875)<1.0e-12,
                          "all brewing-stand bottle states retain the center stem");
                    CHECK(down_piston_pig_shape_probe_at_y(
                              &r,idle,117,meta,0,0,0,
                              80.0,19.9,20.5,9020+meta,y) &&
                          fabs(y[0]-79.59000002384185)<1.0e-12 &&
                          fabs(y[1]-79.125)<1.0e-12,
                          "all brewing-stand bottle states retain the side base");
                }
                {
                    static const int piston_id[2]={29,33};
                    static const double clear_x[6]={20.5,20.5,20.5,20.5,19.75,21.25};
                    static const double clear_z[6]={20.5,20.5,19.75,21.25,20.5,20.5};
                    for(int block=0;block<2;++block)
                        for(int facing=0;facing<6;++facing){
                            double occupied=facing==1?79.75:80.0;
                            CHECK(down_piston_pig_shape_probe_at_y(
                                      &r,idle,piston_id[block],facing,0,0,0,
                                      80.0,20.5,20.5,
                                      9040+block*40+facing*3,y) &&
                                  fabs(y[0]-80.0)<1.0e-12 &&
                                  fabs(y[1]-80.0)<1.0e-12,
                                  "all retracted normal/sticky piston bases are full cubes");
                            CHECK(down_piston_pig_shape_probe_at_y(
                                      &r,idle,piston_id[block],facing|8,0,0,0,
                                      80.0,20.5,20.5,
                                      9041+block*40+facing*3,y) &&
                                  fabs(y[0]-occupied)<1.0e-12 &&
                                  fabs(y[1]-occupied)<1.0e-12,
                                  "all extended normal/sticky piston bases retain the facing 3/4 body");
                            if(facing>=2)
                                CHECK(down_piston_pig_shape_probe_at_y(
                                          &r,idle,piston_id[block],facing|8,0,0,0,
                                          80.0,clear_x[facing],clear_z[facing],
                                          9042+block*40+facing*3,y) &&
                                      fabs(y[0]-79.59000002384185)<1.0e-12 &&
                                      fabs(y[1]-79.09000002384185)<1.0e-12,
                                      "horizontal extended piston bases leave only the facing quarter empty");
                        }
                }
                for(int id=219;id<=234;++id)
                    CHECK(down_piston_pig_shape_probe_at_y(
                              &r,idle,id,1,0,0,0,
                              80.0,20.5,20.5,9120+id-219,y) &&
                          fabs(y[0]-80.0)<1.0e-12 &&
                          fabs(y[1]-80.0)<1.0e-12,
                          "all sixteen closed shulker colors collide as full cubes");
                for(int facing=0;facing<6;++facing)
                    CHECK(down_piston_pig_shape_probe_at_y(
                              &r,idle,229,facing,0,0,0,
                              80.0,20.5,20.5,9140+facing,y) &&
                          fabs(y[0]-80.0)<1.0e-12 &&
                          fabs(y[1]-80.0)<1.0e-12,
                          "all six closed shulker facings collide as full cubes");
                for(int meta=0;meta<16;++meta){
                    double expected=(meta&4)?80.0:79.8125;
                    CHECK(down_piston_pig_shape_probe_at_y(
                              &r,idle,120,meta,0,0,0,
                              80.0,20.5,20.5,8010+meta,y) &&
                          fabs(y[0]-expected)<1.0e-12 &&
                          fabs(y[1]-expected)<1.0e-12,
                          "end-frame base and eye boxes follow metadata");
                }
                CHECK(down_piston_pig_shape_probe_at_y(
                          &r,idle,120,4,0,0,0,
                          80.0,19.8,20.5,8026,y) &&
                      fabs(y[0]-79.8125)<1.0e-12 &&
                      fabs(y[1]-79.8125)<1.0e-12,
                      "end-frame eye leaves its side lane at base height");
            }
            {
                static const int pane_id[3]={101,102,160};
                static const double arm_x[4]={20.5,20.75,20.5,20.25};
                static const double arm_z[4]={20.25,20.5,20.75,20.5};
                double y[2];
                for(int pane=0;pane<3;++pane)
                    CHECK(down_piston_item_shape_probe(
                              &r,idle,pane_id[pane],0,0,0,0,
                              20.5,20.5,8040+pane,y) &&
                          fabs(y[0]-80.24)<1.0e-12 &&
                          fabs(y[1]-80.0)<1.0e-12,
                          "all pane registry IDs retain a central post");
                CHECK(down_piston_item_shape_probe(
                          &r,idle,102,0,0,0,0,
                          20.5,20.25,8043,y) &&
                      fabs(y[0]-80.24)<1.0e-12 &&
                      fabs(y[1]-79.74)<1.0e-12,
                      "isolated pane leaves its north lane empty");
                for(int direction=0;direction<4;++direction)
                    CHECK(down_piston_item_shape_probe(
                              &r,idle,102,0,1<<direction,101,0,
                              arm_x[direction],arm_z[direction],
                              8050+direction,y) &&
                          fabs(y[0]-80.24)<1.0e-12 &&
                          fabs(y[1]-80.0)<1.0e-12,
                          "pane arms connect in all four directions");
                CHECK(down_piston_item_shape_probe(
                          &r,idle,160,0,(1<<0)|(1<<1),101,0,
                          20.75,20.25,8054,y) &&
                      fabs(y[0]-80.24)<1.0e-12 &&
                      fabs(y[1]-79.74)<1.0e-12,
                      "pane L-shape keeps its outer corner empty");
                CHECK(down_piston_item_shape_probe(
                          &r,idle,102,0,1<<0,60,7,
                          20.5,20.25,8055,y) &&
                      fabs(y[0]-80.24)<1.0e-12 &&
                      fabs(y[1]-80.0)<1.0e-12,
                      "farmland solid side creates a pane arm");
                CHECK(down_piston_item_shape_probe(
                          &r,idle,102,0,1<<0,44,0,
                          20.5,20.25,8056,y) &&
                      fabs(y[0]-80.24)<1.0e-12 &&
                      fabs(y[1]-79.74)<1.0e-12,
                      "bottom slab horizontal side does not create a pane arm");
                CHECK(down_piston_item_shape_probe(
                          &r,idle,102,0,1<<0,53,2,
                          20.5,20.25,8057,y) &&
                      fabs(y[0]-80.24)<1.0e-12 &&
                      fabs(y[1]-80.0)<1.0e-12,
                      "stair facing pane exposes a solid connecting side");
                CHECK(down_piston_item_shape_probe(
                          &r,idle,102,0,1<<0,53,3,
                          20.5,20.25,8058,y) &&
                      fabs(y[0]-80.24)<1.0e-12 &&
                      fabs(y[1]-79.74)<1.0e-12,
                      "stair facing away leaves pane side disconnected");
                CHECK(down_piston_item_shape_probe(
                          &r,idle,102,0,1<<0,78,7,
                          20.5,20.25,8059,y) &&
                      fabs(y[0]-80.24)<1.0e-12 &&
                      fabs(y[1]-80.0)<1.0e-12,
                      "eight-layer snow solid side creates a pane arm");
                CHECK(down_piston_item_shape_probe(
                          &r,idle,102,0,1<<0,78,6,
                          20.5,20.25,8060,y) &&
                      fabs(y[0]-80.24)<1.0e-12 &&
                      fabs(y[1]-79.74)<1.0e-12,
                      "seven-layer snow leaves pane side disconnected");
                CHECK(down_piston_item_shape_probe(
                          &r,idle,102,0,1<<0,152,0,
                          20.5,20.25,8061,y) &&
                      fabs(y[0]-80.24)<1.0e-12 &&
                      fabs(y[1]-80.0)<1.0e-12,
                      "redstone block Forge override creates a pane arm");
            }
            {
                static const int trapdoor_id[2]={96,167};
                static const double open_x[4]={20.5,20.5,20.25,20.75};
                static const double open_z[4]={20.25,20.75,20.5,20.5};
                double y[2];
                for(int trapdoor=0;trapdoor<2;++trapdoor)
                    for(int meta=0;meta<16;++meta){
                        double expected0;
                        double expected1;
                        if(meta&4){
                            expected0=80.0;
                            expected1=80.0;
                        }else if(meta&8){
                            expected0=80.0;
                            expected1=80.0;
                        }else{
                            expected0=79.59000002384185;
                            expected1=79.1875;
                        }
                        CHECK(down_piston_pig_shape_probe_at_y(
                                  &r,idle,trapdoor_id[trapdoor],meta,0,0,0,
                                  80.0,20.5,20.5,
                                  8070+trapdoor*16+meta,y) &&
                              fabs(y[0]-expected0)<1.0e-12 &&
                              fabs(y[1]-expected1)<1.0e-12,
                              "both trapdoor IDs and all metadata clip exactly");
                        if(meta&4){
                            int facing=meta&3;
                            CHECK(down_piston_pig_shape_probe_at_y(
                                      &r,idle,trapdoor_id[trapdoor],meta,
                                      0,0,0,80.0,
                                      open_x[facing],open_z[facing],
                                      8110+trapdoor*16+meta,y) &&
                                  fabs(y[0]-79.59000002384185)<1.0e-12 &&
                                  fabs(y[1]-79.09000002384185)<1.0e-12,
                                  "open trapdoor leaves opposite lane empty");
                        }
                    }
            }
            {
                static const int block_id[2]={118,154};
                static const double second_y[2]={79.74,79.74};
                static const double start_y[2]={80.0,80.0};
                static const double first_y[2]={80.0,80.0};
                static const double wall_x[4]={20.0,21.0,20.5,20.5};
                static const double wall_z[4]={20.5,20.5,20.0,21.0};
                double y[2];
                for(int shape=0;shape<2;++shape)
                    for(int meta=0;meta<16;++meta){
                        CHECK(down_piston_item_shape_probe_at_y(
                                  &r,idle,block_id[shape],meta,0,0,0,
                                  start_y[shape],20.5,20.5,
                                  8150+shape*80+meta,y) &&
                              fabs(y[0]-first_y[shape])<1.0e-12 &&
                              fabs(y[1]-second_y[shape])<1.0e-12,
                              "cauldron and hopper centers distinguish hollow from rim");
                        for(int wall=0;wall<4;++wall)
                            CHECK(down_piston_item_shape_probe_at_y(
                                      &r,idle,block_id[shape],meta,0,0,0,
                                      80.0,wall_x[wall],wall_z[wall],
                                      8170+shape*80+meta*4+wall,y) &&
                                  fabs(y[0]-80.0)<1.0e-12 &&
                                  fabs(y[1]-80.0)<1.0e-12,
                                  "cauldron and hopper retain all four rim walls");
                    }
            }
            {
                static const int anvil_meta[6]={0,1,4,5,8,9};
                double y[2];
                for(int state=0;state<6;++state){
                    int meta=anvil_meta[state];
                    double lane_x=(meta&1)?20.5:19.99;
                    double lane_z=(meta&1)?19.99:20.5;
                    CHECK(down_piston_item_shape_probe(
                              &r,idle,145,meta,0,0,0,
                              20.5,20.5,8350+state*2,y) &&
                          fabs(y[0]-80.24)<1.0e-12 &&
                          fabs(y[1]-80.0)<1.0e-12,
                          "all anvil facings and damage states retain the occupied axis");
                    CHECK(down_piston_item_shape_probe(
                              &r,idle,145,meta,0,0,0,
                              lane_x,lane_z,8351+state*2,y) &&
                          fabs(y[0]-80.24)<1.0e-12 &&
                          fabs(y[1]-79.74)<1.0e-12,
                          "all anvil facings leave the perpendicular inset empty");
                }
                for(int meta=0;meta<6;++meta){
                    int axis=meta<2?1:meta<4?2:0;
                    double lane_x=axis==0?20.5:20.24;
                    double lane_z=axis==0?20.24:20.5;
                    double expected_down=axis==1?80.0:79.74;
                    double expected_east=axis==0
                        ?20.55000001192093:20.92500001192093;
                    double clear_y=axis==2?80.7:80.0;
                    double clear_z=axis==2?20.5:19.8;
                    CHECK(down_piston_item_shape_probe(
                              &r,idle,198,meta,0,0,0,
                              20.5,20.5,8380+meta*2,y) &&
                          fabs(y[0]-80.24)<1.0e-12 &&
                          fabs(y[1]-expected_down)<1.0e-12,
                          "downward items distinguish vertical and horizontal end rods");
                    CHECK(down_piston_item_shape_probe(
                              &r,idle,198,meta,0,0,0,
                              lane_x,lane_z,8381+meta*2,y) &&
                          fabs(y[0]-80.24)<1.0e-12 &&
                          fabs(y[1]-79.74)<1.0e-12,
                          "all three end-rod axes leave a perpendicular lane empty");
                    {
                        double x[2];
                        CHECK(east_piston_pig_shape_probe_at_z(
                                  &r,idle,198,meta,80.0,20.5,
                                  8420+meta*2,x) &&
                              fabs(x[0]-expected_east)<1.0e-9 &&
                              fabs(x[1]-expected_east)<1.0e-9,
                              "horizontal piston clips all six end-rod facings");
                        CHECK(east_piston_pig_shape_probe_at_z(
                                  &r,idle,198,meta,clear_y,clear_z,
                                  8421+meta*2,x) &&
                              fabs(x[0]-20.95999998807907)<1.0e-9 &&
                              fabs(x[1]-21.45999998807907)<1.0e-9,
                              "each end-rod axis retains a measured clear lane");
                    }
                }
                CHECK(down_piston_item_shape_probe(
                          &r,idle,122,0,0,0,0,
                          20.5,20.5,8400,y) &&
                      fabs(y[0]-80.24)<1.0e-12 &&
                      fabs(y[1]-80.0)<1.0e-12,
                      "dragon egg retains the inset full-height body");
                CHECK(down_piston_item_shape_probe(
                          &r,idle,122,0,0,0,0,
                          19.9,20.5,8401,y) &&
                      fabs(y[0]-80.24)<1.0e-12 &&
                      fabs(y[1]-79.74)<1.0e-12,
                      "dragon egg leaves the outside footprint empty");
                for(int chest=0;chest<2;++chest){
                    static const int chest_id[2]={54,146};
                    static const double seam_x[4]={20.5,21.07,20.5,19.93};
                    static const double seam_z[4]={19.93,20.5,21.07,20.5};
                    int id=chest_id[chest];
                    CHECK(down_piston_item_shape_probe(
                              &r,idle,id,2,0,0,0,
                              20.5,20.5,8450+chest*10,y) &&
                          fabs(y[0]-80.24)<1.0e-12 &&
                          fabs(y[1]-79.875)<1.0e-12,
                          "ordinary and trapped chest tops collide at 7/8");
                    CHECK(down_piston_item_shape_probe(
                              &r,idle,id,2,0,0,0,
                              20.5,19.93,8451+chest*10,y) &&
                          fabs(y[0]-80.24)<1.0e-12 &&
                          fabs(y[1]-79.74)<1.0e-12,
                          "isolated chests leave their north inset empty");
                    for(int direction=0;direction<4;++direction)
                        CHECK(down_piston_item_shape_probe(
                                  &r,idle,id,2,1<<direction,id,2,
                                  seam_x[direction],seam_z[direction],
                                  8452+chest*10+direction,y) &&
                              fabs(y[0]-80.24)<1.0e-12 &&
                              fabs(y[1]-79.875)<1.0e-12,
                              "same-registry chests join in all four directions");
                }
                {
                    static const int door_id[7]={64,71,193,194,195,196,197};
                    static const double closed_x[4]={20.1,20.5,20.9,20.5};
                    static const double closed_z[4]={20.5,20.1,20.5,20.9};
                    static const double open_x[4][2]={
                        {20.5,20.5},{20.9,20.1},
                        {20.5,20.5},{20.1,20.9}
                    };
                    static const double open_z[4][2]={
                        {20.1,20.9},{20.5,20.5},
                        {20.9,20.1},{20.5,20.5}
                    };
                    for(int facing=0;facing<4;++facing)
                        CHECK(down_piston_item_door_shape_probe(
                                  &r,idle,64,facing,8,
                                  closed_x[facing],closed_z[facing],
                                  8480+facing,y) &&
                              fabs(y[0]-80.24)<1.0e-12 &&
                              fabs(y[1]-80.0)<1.0e-12,
                              "closed oak doors retain all four facing panels");
                    for(int facing=0;facing<4;++facing)
                        for(int hinge=0;hinge<2;++hinge)
                            CHECK(down_piston_item_door_shape_probe(
                                      &r,idle,64,4+facing,8+hinge,
                                      open_x[facing][hinge],
                                      open_z[facing][hinge],
                                      8490+facing*2+hinge,y) &&
                                  fabs(y[0]-80.24)<1.0e-12 &&
                                  fabs(y[1]-80.0)<1.0e-12,
                                  "open oak door panels follow facing and hinge");
                    CHECK(down_piston_item_door_shape_probe(
                              &r,idle,64,0,8,20.5,20.5,8500,y) &&
                          fabs(y[0]-80.24)<1.0e-12 &&
                          fabs(y[1]-79.74)<1.0e-12,
                          "closed door leaves its center lane empty");
                    CHECK(down_piston_item_door_shape_probe(
                              &r,idle,64,4,8,20.1,20.5,8501,y) &&
                          fabs(y[0]-80.24)<1.0e-12 &&
                          fabs(y[1]-79.74)<1.0e-12,
                          "opening an east door clears its former panel lane");
                    for(int door=1;door<7;++door){
                        CHECK(down_piston_item_door_shape_probe(
                                  &r,idle,door_id[door],0,8,
                                  20.1,20.5,8510+door*2,y) &&
                              fabs(y[0]-80.24)<1.0e-12 &&
                              fabs(y[1]-80.0)<1.0e-12,
                              "all door registries share closed geometry");
                        CHECK(down_piston_item_door_shape_probe(
                                  &r,idle,door_id[door],4,9,
                                  20.5,20.9,8511+door*2,y) &&
                              fabs(y[0]-80.24)<1.0e-12 &&
                              fabs(y[1]-80.0)<1.0e-12,
                              "all door registries share open hinge geometry");
                    }
                }
                CHECK(down_piston_item_shape_probe(
                          &r,idle,81,0,0,0,0,
                          20.5,20.5,8530,y) &&
                      fabs(y[0]-80.24)<1.0e-12 &&
                      fabs(y[1]-79.9375)<1.0e-12,
                      "cactus retains its inset 15/16 collision body");
                CHECK(down_piston_item_shape_probe(
                          &r,idle,81,0,0,0,0,
                          19.9,20.5,8531,y) &&
                      fabs(y[0]-80.24)<1.0e-12 &&
                      fabs(y[1]-79.74)<1.0e-12,
                      "cactus leaves its outside footprint empty");
                CHECK(down_piston_item_shape_probe(
                          &r,idle,130,2,0,0,0,
                          20.5,20.5,8532,y) &&
                      fabs(y[0]-80.24)<1.0e-12 &&
                      fabs(y[1]-79.875)<1.0e-12,
                      "ender chest retains its isolated 7/8 collision top");
                CHECK(down_piston_item_shape_probe(
                          &r,idle,130,2,0,0,0,
                          19.9,20.5,8533,y) &&
                      fabs(y[0]-80.24)<1.0e-12 &&
                      fabs(y[1]-79.74)<1.0e-12,
                      "ender chest leaves its outside footprint empty");
                {
                    double x[2];
                    CHECK(east_piston_item_shape_probe(
                              &r,idle,111,0,80.0,20.5,8534,x) &&
                          fabs(x[0]-20.635)<1.0e-12 &&
                          fabs(x[1]-20.9375)<1.0e-12,
                          "lily pad clips non-boat items at 3/32 height");
                    CHECK(east_piston_item_shape_probe(
                              &r,idle,111,0,80.1,20.5,8535,x) &&
                          fabs(x[0]-20.635)<1.0e-12 &&
                          fabs(x[1]-21.135)<1.0e-12,
                          "lily pad leaves the exact above-pad lane empty");
                    for(int block_index=0;block_index<2;++block_index){
                        int block=block_index?208:60;
                        int meta_count=block==60?8:1;
                        for(int meta=0;meta<meta_count;++meta){
                            int eid=8600+block_index*20+meta*2;
                            CHECK(east_piston_item_shape_probe(
                                      &r,idle,block,meta,
                                      80.8,20.5,eid,x) &&
                                  fabs(x[0]-20.635)<1.0e-12 &&
                                  fabs(x[1]-20.875)<1.0e-12,
                                  "farmland and grass path clip at 15/16 height");
                            CHECK(east_piston_item_shape_probe(
                                      &r,idle,block,meta,
                                      80.94,20.5,eid+1,x) &&
                                  fabs(x[0]-20.635)<1.0e-12 &&
                                  fabs(x[1]-21.135)<1.0e-12,
                                  "farmland and grass path leave the lane above 15/16 empty");
                        }
                    }
                    CHECK(east_piston_item_shape_probe(
                              &r,idle,144,1,80.1,20.5,8536,x) &&
                          fabs(x[0]-20.635)<1.0e-12 &&
                          fabs(x[1]-21.125)<1.0e-12,
                          "floor skull clips across its centered half block");
                    CHECK(east_piston_item_shape_probe(
                              &r,idle,144,1,80.5,20.5,8537,x) &&
                          fabs(x[0]-20.635)<1.0e-12 &&
                          fabs(x[1]-21.135)<1.0e-12,
                          "floor skull leaves the exact above-skull lane empty");
                    CHECK(east_piston_pig_shape_probe_at_z(
                              &r,idle,140,0,80.1,20.5,8538,x) &&
                          fabs(x[0]-20.862500011920929)<1.0e-9 &&
                          fabs(x[1]-20.862500011920929)<1.0e-9,
                          "flower pot clips a living entity at 3/8 height");
                    CHECK(east_piston_pig_shape_probe_at_z(
                              &r,idle,140,0,80.4,20.5,8539,x) &&
                          fabs(x[0]-20.95999998807907)<1.0e-9 &&
                          fabs(x[1]-21.45999998807907)<1.0e-9,
                          "flower pot leaves the exact above-pot lane empty");
                }
                {
                    static const double skull_x[4]={20.5,20.5,20.75,20.25};
                    static const double skull_z[4]={20.75,20.25,20.5,20.5};
                    for(int facing=0;facing<4;++facing)
                        for(int nodrop=0;nodrop<2;++nodrop)
                            CHECK(down_piston_item_shape_probe(
                                      &r,idle,144,2+facing+nodrop*8,
                                      0,0,0,skull_x[facing],skull_z[facing],
                                      8540+facing+nodrop*4,y) &&
                                  fabs(y[0]-80.24)<1.0e-12 &&
                                  fabs(y[1]-79.75)<1.0e-12,
                                  "wall skull facings and nodrop bit clip exactly");
                }
                {
                    static const double ladder_x[6]={20.5,20.5,20.5,20.5,20.9,20.1};
                    static const double ladder_z[6]={20.9,20.9,20.9,20.1,20.5,20.5};
                    for(int meta=0;meta<16;++meta){
                        int facing=meta%6;
                        CHECK(down_piston_item_shape_probe(
                                  &r,idle,65,meta,0,0,0,
                                  ladder_x[facing],ladder_z[facing],
                                  8550+meta,y) &&
                              fabs(y[0]-80.24)<1.0e-12 &&
                              fabs(y[1]-80.0)<1.0e-12,
                              "ladder metadata follows D-U-N-S-W-E panels");
                    }
                    CHECK(down_piston_item_shape_probe(
                              &r,idle,65,2,0,0,0,
                              20.5,20.5,8566,y) &&
                          fabs(y[0]-80.24)<1.0e-12 &&
                          fabs(y[1]-79.74)<1.0e-12,
                          "north ladder leaves its interior lane empty");
                }
                {
                    static const double cocoa_x[4]={20.5,20.2,20.5,20.8};
                    static const double cocoa_z[4]={20.8,20.5,20.2,20.5};
                    for(int age=0;age<3;++age)
                        for(int facing=0;facing<4;++facing){
                            int meta=facing+age*4;
                            CHECK(down_piston_item_shape_probe(
                                      &r,idle,127,meta,0,0,0,
                                      cocoa_x[facing],cocoa_z[facing],
                                      8570+meta,y) &&
                                  fabs(y[0]-80.24)<1.0e-12 &&
                                  fabs(y[1]-79.75)<1.0e-12,
                                  "all cocoa facings and ages clip at 3/4 top");
                        }
                    CHECK(down_piston_item_shape_probe(
                              &r,idle,127,10,0,0,0,
                              20.5,20.9,8582,y) &&
                          fabs(y[0]-80.24)<1.0e-12 &&
                          fabs(y[1]-79.74)<1.0e-12,
                          "mature north cocoa leaves its opposite lane empty");
                }
                CHECK(down_piston_item_shape_probe_with_below_at_y(
                          &r,idle,199,0,121,0,0,0,0,
                          80.5,20.5,20.5,8583,y) &&
                      fabs(y[0]-80.24)<1.0e-12 &&
                      fabs(y[1]-79.8125)<1.0e-12,
                      "isolated chorus plant retains its centered 5/8 body");
                {
                    static const double chorus_x[4]={20.5,20.96,20.5,20.04};
                    static const double chorus_z[4]={20.04,20.5,20.96,20.5};
                    for(int direction=0;direction<4;++direction){
                        CHECK(down_piston_item_shape_probe_with_below_at_y(
                                  &r,idle,199,0,121,0,
                                  0,0,0,80.5,
                                  chorus_x[direction],chorus_z[direction],
                                  8584+direction*2,y) &&
                              fabs(y[0]-80.24)<1.0e-12 &&
                              fabs(y[1]-79.74)<1.0e-12,
                              "isolated chorus plant leaves side lanes empty");
                        CHECK(down_piston_item_shape_probe_with_below_at_y(
                                  &r,idle,199,0,121,0,
                                  1<<direction,199,0,80.5,
                                  chorus_x[direction],chorus_z[direction],
                                  8585+direction*2,y) &&
                              fabs(y[0]-80.24)<1.0e-12 &&
                              fabs(y[1]-79.8125)<1.0e-12,
                              "chorus plant horizontal arms follow neighbors");
                    }
                }
                {
                    double x[2];
                    CHECK(east_piston_item_shape_probe(
                              &r,idle,199,0,80.96,20.5,8592,x) &&
                          fabs(x[0]-20.635)<1.0e-12 &&
                          fabs(x[1]-21.135)<1.0e-12,
                          "isolated chorus plant leaves its upper lane empty");
                    CHECK(east_piston_item_shape_probe_with_neighbor(
                              &r,idle,199,0,0,1,0,199,0,
                              80.96,20.5,8593,x) &&
                          fabs(x[0]-20.635)<1.0e-12 &&
                          fabs(x[1]-21.0625)<1.0e-12,
                          "chorus plant adds its exact upward arm");
                    CHECK(east_piston_item_shape_probe(
                              &r,idle,199,0,79.9374,20.5,8594,x) &&
                          fabs(x[0]-20.635)<1.0e-12 &&
                          fabs(x[1]-21.135)<1.0e-12,
                          "isolated chorus plant leaves its lower lane empty");
                    CHECK(east_piston_item_shape_probe_with_neighbor(
                              &r,idle,199,0,0,-1,0,199,0,
                              79.9374,20.5,8595,x) &&
                          fabs(x[0]-20.635)<1.0e-12 &&
                          fabs(x[1]-21.0625)<1.0e-12,
                          "chorus plant adds its exact downward arm");
                }
            }
        }
        memset(&r.entities,0,sizeof r.entities);
        r.piston_count=0;
        memset(r.pistons,0,sizeof r.pistons);

        for(int y=77;y<=79;++y)
            for(int z=7;z<=10;++z)
                for(int x=10;x<=15;++x)
                    gm_world_set_block_meta(r.world,x,y,z,0,0);
        gm_world_set_block_meta(r.world,12,78,8,33,5);
        gm_world_set_block_meta(r.world,13,78,8,49,0);
        CHECK(gm_runtime_set_block(&r,12,78,9,152,0) &&
              gm_world_block(r.world,12,78,8)==33 &&
              gm_world_meta(r.world,12,78,8)==5 &&
              gm_world_block(r.world,13,78,8)==49 &&
              r.piston_count==0,
              "obsidian blocks a powered normal-piston extension");
        {
            static const int dx[6]={0,0,0,0,-1,1};
            static const int dy[6]={-1,1,0,0,0,0};
            static const int dz[6]={0,0,-1,1,0,0};
            for(int facing=0;facing<6;++facing){
                int source_face=facing==3?2:3;
                for(int y=78;y<=82;++y)
                    for(int z=6;z<=10;++z)
                        for(int x=18;x<=22;++x)
                            gm_world_set_block_meta(
                                r.world,x,y,z,0,0);
                gm_world_set_block_meta(r.world,20,80,8,33,facing);
                CHECK(gm_runtime_set_block(
                          &r,20+dx[source_face],80+dy[source_face],
                          8+dz[source_face],152,0) &&
                      gm_world_block(r.world,20,80,8)==33 &&
                      gm_world_meta(r.world,20,80,8)==(facing|8) &&
                      gm_world_block(
                          r.world,20+dx[facing],80+dy[facing],
                          8+dz[facing])==36 &&
                      gm_world_meta(
                          r.world,20+dx[facing],80+dy[facing],
                          8+dz[facing])==facing &&
                      r.piston_count==1 &&
                      r.pistons[0].facing==facing,
                      "all six normal-piston facings start exact empty motion");
                gm_runtime_tick(&r,idle);
                gm_runtime_tick(&r,idle);
                gm_runtime_tick(&r,idle);
                CHECK(r.piston_count==0 &&
                      gm_world_block(
                          r.world,20+dx[facing],80+dy[facing],
                          8+dz[facing])==34 &&
                      gm_world_meta(
                          r.world,20+dx[facing],80+dy[facing],
                          8+dz[facing])==facing,
                      "all six normal-piston facings settle exact head metadata");
            }
        }
        {
            static const double start[6][3]={
                {20.5,80.0,20.5},{20.5,80.0,20.5},
                {20.5,80.0,20.5},{20.5,80.0,20.5},
                {20.5,80.0,20.5},{20.5,80.0,20.5}
            };
            static const double half[6][3]={
                {20.5,79.59000002384185,20.5},{20.5,80.51,20.5},
                {20.5,80.0,20.04000001192093},
                {20.5,80.0,20.95999998807907},
                {20.04000001192093,80.0,20.5},
                {20.95999998807907,80.0,20.5}
            };
            static const double full[6][3]={
                {20.5,79.09000002384185,20.5},{20.5,81.01,20.5},
                {20.5,80.0,19.54000001192093},
                {20.5,80.0,21.45999998807907},
                {19.54000001192093,80.0,20.5},
                {21.45999998807907,80.0,20.5}
            };
            for(int facing=0;facing<6;++facing){
                const EwStore *mobs;
                for(int y=78;y<=82;++y)
                    for(int z=18;z<=22;++z)
                        for(int x=18;x<=22;++x)
                            gm_world_set_block_meta(r.world,x,y,z,0,0);
                gm_mobs_init(&r.mobs,0);
                r.mobs_enabled=0;
                r.piston_count=1;
                memset(r.pistons,0,sizeof r.pistons);
                r.pistons[0]=(GmRuntimePiston){
                    .active=1,.dimension=r.dimension,
                    .x=20,.y=80,.z=20,
                    .moved_block=34,.moved_meta=facing,
                    .facing=facing,.extending=1,.source=1,
                    .progress=0.0f,.last_progress=0.0f
                };
                gm_world_set_block_meta(r.world,20,80,20,36,facing);
                CHECK(gm_runtime_spawn_mob_fixture(
                          &r,GM_MOB_PIG,7000+facing,
                          start[facing][0],start[facing][1],start[facing][2],
                          0.0,0.0,0.0,0.0f,10.0f,1,0,0,0) &&
                      gm_runtime_spawn_mob_fixture(
                          &r,GM_MOB_PIG,7100+facing,
                          start[facing][0]+2.0,
                          start[facing][1],start[facing][2]+2.0,
                          0.0,0.0,0.0,0.0f,10.0f,1,0,0,0),
                      "six-face piston living sweep fixtures spawn pigs");
                gm_runtime_tick(&r,idle);
                mobs=r.mobs.current?&r.mobs.b:&r.mobs.a;
                CHECK(fabs(mobs->x[1]-half[facing][0])<1.0e-9 &&
                      fabs(mobs->y[1]-half[facing][1])<1.0e-9 &&
                      fabs(mobs->z[1]-half[facing][2])<1.0e-9 &&
                      fabs(mobs->x[2]-(start[facing][0]+2.0))<1.0e-12 &&
                      fabs(mobs->y[2]-start[facing][1])<1.0e-12 &&
                      fabs(mobs->z[2]-(start[facing][2]+2.0))<1.0e-12,
                      "moving head half-step pushes only intersecting pig");
                gm_runtime_tick(&r,idle);
                mobs=r.mobs.current?&r.mobs.b:&r.mobs.a;
                CHECK(fabs(mobs->x[1]-full[facing][0])<1.0e-9 &&
                      fabs(mobs->y[1]-full[facing][1])<1.0e-9 &&
                      fabs(mobs->z[1]-full[facing][2])<1.0e-9,
                      "moving head completes living push in all six faces");
                gm_world_set_block_meta(r.world,20,80,20,0,0);
            }
        }
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),"hostile behavior runtime initializes");
    if(r.world){
        gm_runtime_set_pose(&r,8.5,4.0,8.5,0,0);
        CHECK(gm_mobs_spawn(&r.mobs,EW_TYPE_SKELETON,8.5,4.0,16.5)>0,
              "skeleton component target spawns");
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
        float hp=r.vitals.health;int saw_hostile_arrow=0;
        for(int t=0;t<30&&r.vitals.health==hp;++t){
            gm_runtime_tick(&r,idle);
            for(int i=0;i<GM_RUNTIME_PROJECTILES;++i)
                if(r.projectiles[i].active&&r.projectiles[i].type==2)saw_hostile_arrow=1;
        }
        CHECK(saw_hostile_arrow,"skeleton creates a visible ballistic projectile");
        CHECK(r.vitals.health<hp,"skeleton projectile damages the player");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),"creeper runtime initializes");
    if(r.world){
        gm_runtime_set_pose(&r,8.5,4.0,8.5,0,0);
        CHECK(gm_mobs_spawn(&r.mobs,EW_TYPE_CREEPER,8.5,4.0,10.8)>0,
              "creeper component target spawns");
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;float hp=r.vitals.health;
        for(int t=0;t<30;++t)gm_runtime_tick(&r,idle);
        CHECK(gm_mobs_alive(&r.mobs)==0,"creeper is consumed after its 30-tick fuse");
        CHECK(r.vitals.health<hp,"creeper explosion applies verified explosion damage");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),"bow runtime initializes");
    if(r.world){
        isr_set_stack(&r.player.inv,0,ic_mk(261,1,0));
        isr_set_stack(&r.player.inv,1,ic_mk(262,2,0));
        CHECK(gm_mobs_spawn(&r.mobs,GM_MOB_SHEEP,8.5,4.0,14.5)>0,
              "stationary projectile target spawns");
        gm_runtime_set_pose(&r,8.5,4.0,8.5,0.0f,6.8f);
        GmAction draw;memset(&draw,0,sizeof draw);draw.use=1;draw.hotbar_sel=0;
        for(int t=0;t<20;++t)gm_runtime_tick(&r,draw);
        GmAction release;memset(&release,0,sizeof release);release.hotbar_sel=0;
        gm_runtime_tick(&r,release);
        CHECK(isr_get_stack(&r.player.inv,1).count==1,
              "bow release consumes exactly one survival arrow");
        int visible=0;
        for(int t=0;t<8;++t){
            GmEntityView arrows[GM_RUNTIME_PROJECTILES];
            if(gm_runtime_projectile_views(&r,arrows,GM_RUNTIME_PROJECTILES)>0)visible=1;
            gm_runtime_tick(&r,release);
        }
        GmEntityView target[EW_MAX_ENTITIES];
        int nt=gm_mobs_fill_views(&r.mobs,target,EW_MAX_ENTITIES);
        CHECK(visible,"flying arrow is exposed as a runtime entity view");
        CHECK(nt==1&&target[0].health<20.0f,
              "swept arrow flight damages an entity without endpoint tunneling");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "moving-piston checkpoint runtime initializes");
    if(r.world){
        GmRuntimePiston moving;
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
        CHECK(gm_runtime_load_block_dim(&r,0,12,78,8,36,5),
              "moving-piston checkpoint loads its extension block");
        CHECK(gm_runtime_moving_piston_load(
                  &r,0,12,78,8,1,0,5,1,0,0.5f,0.0f),
              "moving-piston checkpoint restores exact progress state");
        CHECK(!gm_runtime_moving_piston_load(
                  &r,0,12,78,8,1,0,5,1,0,0.5f,0.0f),
              "moving-piston checkpoint rejects a duplicate tile position");
        CHECK(gm_runtime_moving_piston_count(&r)==1 &&
              gm_runtime_moving_piston_get(&r,0,&moving) &&
              moving.progress==0.5f && moving.last_progress==0.0f,
              "moving-piston checkpoint exposes the restored tile state");
        gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_moving_piston_count(&r)==1 &&
              gm_runtime_moving_piston_get(&r,0,&moving) &&
              moving.progress==1.0f && moving.last_progress==0.5f &&
              gm_world_block(r.world,12,78,8)==36,
              "restored moving tile advances one Java half-step");
        gm_runtime_tick(&r,idle);
        CHECK(gm_runtime_moving_piston_count(&r)==0 &&
              gm_world_block(r.world,12,78,8)==1,
              "restored moving tile settles to its moved block");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "saved-skylight capsule runtime initializes");
    if(r.world){
        CHECK(gm_runtime_load_block_dim(&r,0,8,100,8,1,0),
              "cold block snapshot accepts a roof cell");
        CHECK(gm_runtime_finalize_block_snapshot_dim(&r,0,0,0,1),
              "cold block snapshot resolves its derived lighting once");
        CHECK(gm_runtime_load_sky_light_dim(&r,0,8,100,8,7),
              "saved skylight nibble overlays the derived value");
        CHECK(!gm_runtime_load_sky_light_dim(&r,0,8,100,8,16),
              "saved skylight restore rejects values outside a nibble");
        CHECK(gm_runtime_finalize_sky_light_snapshot_dim(&r,0),
              "saved skylight snapshot finalizes");
        gm_world_ensure(r.world,0,0,1);
        CHECK(gm_world_sky_light(r.world,8,100,8)==7,
              "ordinary ensure preserves the exact saved skylight boundary");
        CHECK(gm_runtime_set_block(&r,8,100,8,0,0),
              "live block mutation resumes normal lighting after restore");
        CHECK(gm_world_sky_light(r.world,8,100,8)==15,
              "live roof removal replaces the restored nibble with direct sky");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),"food runtime initializes");
    if(r.world){
        r.vitals.foodLevel=10;r.vitals.saturation=0;r.player.food=10;
        isr_set_stack(&r.player.inv,0,ic_mk(364,1,0));
        GmAction eat;memset(&eat,0,sizeof eat);eat.use=1;eat.hotbar_sel=0;
        for(int t=0;t<32;++t)gm_runtime_tick(&r,eat);
        CHECK(r.vitals.foodLevel==18&&isr_get_stack(&r.player.inv,0).count==0,
              "holding use for 32 ticks consumes cooked food and restores hunger");
    }
    gm_runtime_destroy(&r);

    {
        float regen_off_health=0.0f,regen_off_saturation=0.0f;
        cfg.mobs=0;
        CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
              "gamerule-off runtime initializes");
        if(r.world){
            McGameRules gr=mc_gamerules_default();
            gr.naturalRegeneration=0;
            gr.doDaylightCycle=0;
            gr.doWeatherCycle=0;
            gm_runtime_set_time(&r,6000);
            gm_runtime_set_total_time(&r,1000);
            gm_runtime_set_weather(&r,1,1,100,200);
            gm_runtime_set_gamerules(&r,&gr);
            gm_runtime_set_vitals(&r,10.0f,20);
            GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
            for(int t=0;t<11;++t)gm_runtime_tick(&r,idle);
            regen_off_health=r.vitals.health;
            regen_off_saturation=r.vitals.saturation;
            CHECK(fabsf(regen_off_health-10.0f)<1e-6f&&
                  fabsf(regen_off_saturation-5.0f)<1e-6f,
                  "naturalRegeneration false suppresses saturated healing");
            CHECK(r.clock.world_time==6000&&r.clock.total_time==1011,
                  "doDaylightCycle false freezes world time only");
            CHECK(r.clock.rain_time==100&&r.clock.thunder_time==200&&
                  r.clock.raining&&r.clock.thundering,
                  "doWeatherCycle false freezes weather state and timers");
        }
        gm_runtime_destroy(&r);

        CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
              "gamerule-on runtime initializes");
        if(r.world){
            McGameRules gr=mc_gamerules_default();
            gm_runtime_set_time(&r,6000);
            gm_runtime_set_total_time(&r,1000);
            gm_runtime_set_weather(&r,1,1,100,200);
            gm_runtime_set_gamerules(&r,&gr);
            gm_runtime_set_vitals(&r,10.0f,20);
            GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
            for(int t=0;t<11;++t)gm_runtime_tick(&r,idle);
            CHECK(r.vitals.health>regen_off_health+0.1f&&
                  r.vitals.saturation<regen_off_saturation-0.1f,
                  "naturalRegeneration true heals and consumes saturation");
            CHECK(r.clock.world_time==6011&&r.clock.total_time==1011,
                  "doDaylightCycle true advances world time");
            CHECK(r.clock.rain_time!=100||r.clock.thunder_time!=200,
                  "doWeatherCycle true advances weather timers");
        }
        gm_runtime_destroy(&r);
        cfg.mobs=1;
    }

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),"bed runtime initializes");
    if(r.world){
        isr_set_stack(&r.player.inv,0,ic_mk(355,1,0));gm_runtime_set_pose(&r,8.5,5,8.5,0,60);
        GmAction place;memset(&place,0,sizeof place);place.do_place=1;place.hotbar_sel=0;
        gm_runtime_tick(&r,place);int bx=0,by=0,bz=0,bedparts=0;
        for(int x=6;x<=10;++x)for(int y=4;y<=6;++y)for(int z=7;z<=12;++z)
            if(gm_world_block(r.world,x,y,z)==26){bx=x;by=y;bz=z;++bedparts;}
        CHECK(bedparts==2,"bed item places linked foot and head blocks");
        r.dimension=-1;float hp=r.vitals.health;
        CHECK(gm_runtime_use_block(&r,bx,by,bz),"using bed outside Overworld triggers explosion");
        CHECK(gm_world_block(r.world,bx,by,bz)==0&&r.vitals.health<hp,
              "bed explosion removes bed and applies verified explosion damage");
    }
    gm_runtime_destroy(&r);
    if (fail) return 1;
    fprintf(stderr, "runtime: PASS\n");
    return 0;
}
