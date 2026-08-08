#include "game/runtime.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static uint64_t double_bits(double value) {
    union { double d; uint64_t u; } bits;
    bits.d=value;return bits.u;
}
static uint32_t float_bits(float value) {
    union { float f; uint32_t u; } bits;
    bits.f=value;return bits.u;
}

int main(void) {
    static const int cases[][3] = {
        {260,0,123456},{282,0,123456},{297,0,123456},
        {319,0,123456},{320,0,123456},{322,0,123456},{322,1,123456},
        {349,0,123456},{349,1,123456},{349,2,123456},{349,3,123456},
        {350,0,123456},{350,1,123456},{357,0,123456},{360,0,123456},
        {363,0,123456},{364,0,123456},{365,0,123456},{366,0,123456},
        {367,0,123456},{375,0,123456},{391,0,123456},{392,0,123456},
        {393,0,123456},{394,0,123456},{396,0,123456},{400,0,123456},
        {411,0,123456},{412,0,123456},{413,0,123456},{423,0,123456},
        {424,0,123456},{432,0,123456},{434,0,123456},{436,0,123456},
        {365,0,1},{367,0,1},{394,0,1}
    };
    GmConfig cfg;GmRuntime runtime;char error[256];
    gm_config_defaults(&cfg);cfg.mobs=0;cfg.weather=0;
    if(!gm_runtime_init(&runtime,&cfg,error,sizeof error)) {
        fprintf(stderr,"runtime init: %s\n",error);return 1;
    }
    for(int x=-12;x<=12;++x)for(int z=-12;z<=12;++z) {
        gm_world_set_block_meta(runtime.world,x,64,z,1,0);
        for(int y=65;y<=85;++y)
            gm_world_set_block_meta(runtime.world,x,y,z,0,0);
    }
    gm_world_fill_window(runtime.world,runtime.ccx,runtime.ccz,
        (struct Chunk *)runtime.window);
    for (int ci=0;ci<(int)(sizeof cases/sizeof cases[0]);++ci) {
        gm_runtime_potions_clear(&runtime);
        runtime.chorus_fruit_cooldown=0;
        runtime.particle_event_count=0;
        runtime.sound_event_head=0;
        runtime.sound_event_count=0;
        gm_runtime_set_pose(&runtime,0.5,70.0,0.5,0.0F,0.0F);
        gm_runtime_set_world_random_seed48(
            &runtime,((uint64_t)cases[ci][2]^UINT64_C(0x5DEECE66D))
                &((UINT64_C(1)<<48)-1));
        gm_runtime_set_player_random_seed48(
            &runtime,(UINT64_C(654321)^UINT64_C(0x5DEECE66D))
                &((UINT64_C(1)<<48)-1));
        ICStack food=ic_mk(cases[ci][0],1,cases[ci][1]);
        gm_runtime_apply_finished_food(&runtime,&food);
        printf("E %d %d %d ",
            cases[ci][0],cases[ci][1],cases[ci][2]);
        int first=1;
        for(int id=1;id<=255;++id)
            for(int i=0;i<runtime.potion_count;++i)
                if(runtime.potions[i].id==id) {
                    if(!first)putchar(',');
                    first=0;
                    printf("%d:%d:%d",id,runtime.potions[i].amplifier,
                        runtime.potions[i].duration);
                }
        if(first)putchar('-');
        printf(" %016llx %016llx %d %012llx %d",
            (unsigned long long)double_bits(runtime.player.ent.posX),
            (unsigned long long)double_bits(runtime.player.ent.posZ),
            runtime.chorus_fruit_cooldown>0?1:0,
            (unsigned long long)runtime.mobs.player_random.seed,
            runtime.particle_event_count);
        double last[6]={0};
        if(runtime.particle_event_count>0) {
            GmRuntimeParticleEvent *event=
                &runtime.particle_events[runtime.particle_event_count-1];
            last[0]=event->x;last[1]=event->y;last[2]=event->z;
            last[3]=event->motion_x;last[4]=event->motion_y;
            last[5]=event->motion_z;
        }
        for(int i=0;i<6;++i)
            printf(" %016llx",(unsigned long long)double_bits(last[i]));
        printf(" %d ",runtime.sound_event_count);
        for(int i=0;i<runtime.sound_event_count;++i) {
            GmRuntimeSoundEvent *event=&runtime.sound_events[i];
            const char *name=event->sound==GM_SOUND_PLAYER_BURP
                ? "minecraft:entity.player.burp"
                : "minecraft:item.chorus_fruit.teleport";
            if(i>0)putchar('/');
            printf("%s@%llx:%llx:%llx:%x:%x",name,
                (unsigned long long)double_bits(event->x),
                (unsigned long long)double_bits(event->y),
                (unsigned long long)double_bits(event->z),
                float_bits(event->volume),float_bits(event->pitch));
        }
        putchar('\n');
    }
    gm_runtime_destroy(&runtime);
    return 0;
}
