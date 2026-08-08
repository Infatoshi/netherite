#include <stdio.h>
#include "game/weather_render.h"

int gm_world_precipitation_y(const GmWorld *w,int x,int z){(void)w;(void)x;(void)z;return 65;}
int gm_world_precipitation_kind(const GmWorld *w,int x,int y,int z){
    (void)w;(void)y;return z==2&&x==1?1:z==2&&x==-1?2:0;
}
int gm_world_sky_light(const GmWorld *w,int x,int y,int z){(void)w;(void)x;(void)y;(void)z;return 12;}
int gm_world_block_light(const GmWorld *w,int x,int y,int z){(void)w;(void)x;(void)y;(void)z;return 3;}

static unsigned bits(float f){union{float f;unsigned u;}v;v.f=f;return v.u;}
static void dump(const CrVertex *v,int n){
    for(int i=0;i<n;i++)printf("%08x %08x %08x %08x %08x %08x %02x\n",
        bits(v[i].pos.x),bits(v[i].pos.y),bits(v[i].pos.z),
        bits(v[i].uv.x),bits(v[i].uv.y),bits(v[i].light),v[i].tint.a);
}
int main(void){
    CrVertex rain[12],snow[12],lightning[GM_LIGHTNING_MAX_VERTS];
    GmWeatherGeom g=gm_weather_emit((const GmWorld *)1,0.5f,72.0f,2.5f,
        37,0.5f,0.8f,rain,12,snow,12);
    printf("%d %d\n",g.rain_verts,g.snow_verts);dump(rain,g.rain_verts);dump(snow,g.snow_verts);
    GmLightningView bolt={7,INT64_C(0x123456789abcdef),4.0f,5.0f,6.0f};
    int ln=gm_lightning_emit(&bolt,1,lightning,GM_LIGHTNING_MAX_VERTS);
    printf("L %d\n",ln);dump(lightning,ln);
    return 0;
}
