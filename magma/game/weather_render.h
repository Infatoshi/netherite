/* Exact 1.11.2 EntityRenderer.renderRainSnow column geometry. Header-only so
 * both interactive and capture render paths use one implementation. */
#ifndef MAGMA_GAME_WEATHER_RENDER_H
#define MAGMA_GAME_WEATHER_RENDER_H

#include "game/game.h"
#include "mc_rng.h"
#include <math.h>

#define GM_WEATHER_RADIUS 10
#define GM_WEATHER_MAX_VERTS_PER_KIND \
    ((GM_WEATHER_RADIUS * 2 + 1) * (GM_WEATHER_RADIUS * 2 + 1) * 12)
#define GM_LIGHTNING_MAX_VERTS (4 * 14 * 8 * 3)

typedef struct { int rain_verts, snow_verts; } GmWeatherGeom;

static inline CrVertex gm_weather_vertex(
        float x, float y, float z, float u, float v,
        int sky, int block, float alpha) {
    CrVertex q = {0};
    q.pos = (CrVec3){x,y,z}; q.uv = (CrVec2){u,v};
    q.light = (float)sky; q.blk = (float)block;
    q.tint = (CrRgba){255,255,255,
        (u8)(alpha <= 0.0f ? 0 : alpha >= 1.0f ? 255 : alpha * 255.0f + 0.5f)};
    q.ao = 1.0f;
    return q;
}

static inline int gm_weather_quad(
        CrVertex *out, int n, int cap, const CrVertex q[4]) {
    if (n + 12 > cap) return n;
    out[n++]=q[0]; out[n++]=q[1]; out[n++]=q[2];
    out[n++]=q[0]; out[n++]=q[2]; out[n++]=q[3];
    /* GlStateManager.disableCull: supply the reverse windings too. */
    out[n++]=q[2]; out[n++]=q[1]; out[n++]=q[0];
    out[n++]=q[3]; out[n++]=q[2]; out[n++]=q[0];
    return n;
}

static inline i32 gm_weather_seed32(i32 x, i32 z) {
    u32 a = (u32)x * (u32)x * 3121u + (u32)x * 45238971u;
    u32 b = (u32)z * (u32)z * 418711u + (u32)z * 13761u;
    return (i32)(a ^ b);
}

static inline GmWeatherGeom gm_weather_emit(
        const GmWorld *world, float ex, float ey, float ez,
        int renderer_tick, float partial_ticks, float strength,
        CrVertex *rain, int rain_cap, CrVertex *snow, int snow_cap) {
    GmWeatherGeom g = {0,0};
    if (!world || strength <= 0.0f) return g;
    int ix=(int)floorf(ex), iy=(int)floorf(ey), iz=(int)floorf(ez);
    float frame=(float)renderer_tick + partial_ticks;
    for (int z=iz-GM_WEATHER_RADIUS; z<=iz+GM_WEATHER_RADIUS; ++z) {
        for (int x=ix-GM_WEATHER_RADIUS; x<=ix+GM_WEATHER_RADIUS; ++x) {
            float dx=(float)(x-ix), dz=(float)(z-iz);
            float len=sqrtf(dx*dx+dz*dz);
            if (len == 0.0f) continue; /* Java's centre quad contains NaNs. */
            double rx=(double)(-dz/len)*0.5;
            double rz=(double)( dx/len)*0.5;
            int precip=gm_world_precipitation_y(world,x,z);
            int lo=iy-GM_WEATHER_RADIUS, hi=iy+GM_WEATHER_RADIUS;
            if(lo<precip)lo=precip;
            if(hi<precip)hi=precip;
            int light_y=precip<iy?iy:precip;
            if(lo==hi)continue;
            int kind=gm_world_precipitation_kind(world,x,lo,z);
            if(!kind)continue;
            int sky=gm_world_sky_light(world,x,light_y,z);
            int block=gm_world_block_light(world,x,light_y,z);
            i32 seed=gm_weather_seed32((i32)x,(i32)z);
            JavaGaussianRandom random;jrand_gaussian_set(&random,(i64)seed);
            float dist=sqrtf(((float)x+0.5f-ex)*((float)x+0.5f-ex)
                           +((float)z+0.5f-ez)*((float)z+0.5f-ez))
                       /(float)GM_WEATHER_RADIUS;
            CrVertex q[4];
            if(kind==1){
                u32 phase=(u32)renderer_tick
                    +(u32)x*(u32)x*3121u+(u32)x*45238971u
                    +(u32)z*(u32)z*418711u+(u32)z*13761u;
                double scroll=-((double)(phase&31u)+(double)partial_ticks)/32.0
                    *(3.0+jrand_double(&random.random));
                float alpha=((1.0f-dist*dist)*0.5f+0.5f)*strength;
                q[0]=gm_weather_vertex((float)((double)x-rx+0.5), (float)hi,
                    (float)((double)z-rz+0.5),0.0f,(float)((double)lo*0.25+scroll),sky,block,alpha);
                q[1]=gm_weather_vertex((float)((double)x+rx+0.5), (float)hi,
                    (float)((double)z+rz+0.5),1.0f,(float)((double)lo*0.25+scroll),sky,block,alpha);
                q[2]=gm_weather_vertex((float)((double)x+rx+0.5), (float)lo,
                    (float)((double)z+rz+0.5),1.0f,(float)((double)hi*0.25+scroll),sky,block,alpha);
                q[3]=gm_weather_vertex((float)((double)x-rx+0.5), (float)lo,
                    (float)((double)z-rz+0.5),0.0f,(float)((double)hi*0.25+scroll),sky,block,alpha);
                g.rain_verts=gm_weather_quad(rain,g.rain_verts,rain_cap,q);
            }else{
                double sv=-((double)(renderer_tick&511)+partial_ticks)/512.0;
                double su=jrand_double(&random.random)+(double)frame*0.01
                    *(double)(float)jrand_gaussian_next(&random);
                double jitter=jrand_double(&random.random)
                    +(double)(frame*(float)jrand_gaussian_next(&random))*0.001;
                float alpha=((1.0f-dist*dist)*0.3f+0.5f)*strength;
                sky=(sky*3+15)/4; block=(block*3+15)/4;
                q[0]=gm_weather_vertex((float)((double)x-rx+0.5), (float)hi,
                    (float)((double)z-rz+0.5),(float)su,(float)((double)lo*0.25+sv+jitter),sky,block,alpha);
                q[1]=gm_weather_vertex((float)((double)x+rx+0.5), (float)hi,
                    (float)((double)z+rz+0.5),(float)(1.0+su),(float)((double)lo*0.25+sv+jitter),sky,block,alpha);
                q[2]=gm_weather_vertex((float)((double)x+rx+0.5), (float)lo,
                    (float)((double)z+rz+0.5),(float)(1.0+su),(float)((double)hi*0.25+sv+jitter),sky,block,alpha);
                q[3]=gm_weather_vertex((float)((double)x-rx+0.5), (float)lo,
                    (float)((double)z-rz+0.5),(float)su,(float)((double)hi*0.25+sv+jitter),sky,block,alpha);
                g.snow_verts=gm_weather_quad(snow,g.snow_verts,snow_cap,q);
            }
        }
    }
    return g;
}


static inline CrVertex gm_lightning_vertex(double x, double y, double z) {
    CrVertex q = {0};
    q.pos = (CrVec3){(float)x, (float)y, (float)z};
    q.light = 15.0f;
    q.blk = 15.0f;
    q.tint = (CrRgba){115, 115, 128, 77};
    q.ao = 1.0f;
    return q;
}

static inline int gm_lightning_emit(
        const GmLightningView *bolts, int count,
        CrVertex *out, int cap) {
    int n = 0;
    if (!bolts || !out || count <= 0 || cap <= 0) return 0;
    for (int bolt_index = 0; bolt_index < count; ++bolt_index) {
        const GmLightningView *bolt = &bolts[bolt_index];
        double path_x[8], path_z[8];
        double carry_x = 0.0, carry_z = 0.0;
        JavaRandom path_random;
        jrand_set(&path_random, bolt->bolt_vertex);
        for (int i = 7; i >= 0; --i) {
            path_x[i] = carry_x;
            path_z[i] = carry_z;
            carry_x += (double)(jrand_int_bound(&path_random, 11) - 5);
            carry_z += (double)(jrand_int_bound(&path_random, 11) - 5);
        }
        for (int layer = 0; layer < 4; ++layer) {
            JavaRandom branch_random;
            jrand_set(&branch_random, bolt->bolt_vertex);
            for (int branch = 0; branch < 3; ++branch) {
                int top = branch > 0 ? 7 - branch : 7;
                int bottom = branch > 0 ? top - 2 : 0;
                double next_x = path_x[top] - carry_x;
                double next_z = path_z[top] - carry_z;
                for (int segment = top; segment >= bottom; --segment) {
                    CrVertex strip[10];
                    double prior_x = next_x, prior_z = next_z;
                    int bound = branch == 0 ? 11 : 31;
                    int bias = branch == 0 ? 5 : 15;
                    next_x += (double)(
                        jrand_int_bound(&branch_random, bound) - bias);
                    next_z += (double)(
                        jrand_int_bound(&branch_random, bound) - bias);
                    double upper_radius = 0.1 + (double)layer * 0.2;
                    double lower_radius = 0.1 + (double)layer * 0.2;
                    if (branch == 0) {
                        upper_radius *= (double)segment * 0.1 + 1.0;
                        lower_radius *= (double)(segment - 1) * 0.1 + 1.0;
                    }
                    for (int corner = 0; corner < 5; ++corner) {
                        double upper_x = (double)bolt->x + 0.5 - upper_radius;
                        double upper_z = (double)bolt->z + 0.5 - upper_radius;
                        double lower_x = (double)bolt->x + 0.5 - lower_radius;
                        double lower_z = (double)bolt->z + 0.5 - lower_radius;
                        if (corner == 1 || corner == 2) {
                            upper_x += upper_radius * 2.0;
                            lower_x += lower_radius * 2.0;
                        }
                        if (corner == 2 || corner == 3) {
                            upper_z += upper_radius * 2.0;
                            lower_z += lower_radius * 2.0;
                        }
                        strip[corner * 2] = gm_lightning_vertex(
                            lower_x + next_x,
                            (double)bolt->y + (double)(segment * 16),
                            lower_z + next_z);
                        strip[corner * 2 + 1] = gm_lightning_vertex(
                            upper_x + prior_x,
                            (double)bolt->y + (double)((segment + 1) * 16),
                            upper_z + prior_z);
                    }
                    if (n + 24 > cap) return n;
                    for (int tri = 0; tri < 8; ++tri) {
                        if ((tri & 1) == 0) {
                            out[n++] = strip[tri];
                            out[n++] = strip[tri + 1];
                        } else {
                            out[n++] = strip[tri + 1];
                            out[n++] = strip[tri];
                        }
                        out[n++] = strip[tri + 2];
                    }
                }
            }
        }
    }
    return n;
}
#endif
