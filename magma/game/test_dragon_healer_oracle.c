#include "ender_dragon.h"

#include <stdio.h>

static void setup(EdArena *a, long long seed, int ticks, int healing)
{
    ed_init(a,(u64)seed);
    a->dragon.x=0.0;a->dragon.y=100.0;a->dragon.z=0.0;
    a->dragon.health=100.0f;a->dragon.ticks_existed=ticks;
    a->dragon.heal_crystal_idx=healing;
    for(int i=0;i<ED_NUM_CRYSTALS;++i)a->crystals[i].alive=0;
}

static void step(const char *name,int n,EdArena *a)
{
    ++a->dragon.ticks_existed;ed_update_healing_crystal(a);
    printf("%s %d %d %.1f %d\n",name,n,a->dragon.ticks_existed,
           a->dragon.health,a->dragon.heal_crystal_idx);
}

int main(void)
{
    EdArena a;
    setup(&a,1,8,0);a.crystals[0]=(EdCrystal){0,100,10,1};
    for(int i=0;i<4;++i)step("persist_heal",i,&a);

    setup(&a,3,0,-1);a.crystals[0]=(EdCrystal){20,100,0,1};
    a.crystals[1]=(EdCrystal){5,100,0,1};
    for(int i=0;i<2;++i)step("select_gate",i,&a);

    setup(&a,1,0,0);a.crystals[0]=(EdCrystal){0,100,10,0};
    step("dead_clear",0,&a);

    setup(&a,0,9,0);a.crystals[0]=(EdCrystal){50,100,0,1};
    a.crystals[1]=(EdCrystal){5,100,0,1};
    step("heal_then_select",0,&a);

    setup(&a,0,0,-1);a.crystals[0]=(EdCrystal){40,100,0,1};
    step("expanded_box",0,&a);
    return 0;
}
