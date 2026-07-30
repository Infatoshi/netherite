#ifndef MAGMA_RASTER_METAL_SOURCE_H
#define MAGMA_RASTER_METAL_SOURCE_H

static const char *const CR_METAL_SHADER_SOURCE = R"CRMSL(
#include <metal_stdlib>
using namespace metal;
#pragma clang fp contract(off)

constant uint CR_LEVELS = 16;
constant uint CR_TILE = 16;
constant uint CR_BATCH = 256;

struct ScreenVert {
    float sx, sy, sz, invw;
    float uvx, uvy, light, ao;
    float eye_dist;
    float tint_r, tint_g, tint_b, tint_a;
    float blk;
};
struct ScreenTri { ScreenVert v[3]; float lod; };
struct TriBox { int minx, miny, maxx, maxy; };
struct RasterParams { uint width, height, ntris, pad; };
struct TextureDesc {
    uint level_count;
    uint level_offset[16];
    uint level_width[16];
    uint level_height[16];
};
struct ShadeDesc {
    uint fog_rgba;
    float fog_start, fog_end;
    int alpha_test;
    float alpha_ref;
    int enable_fog, layer, blend, use_mips;
    float mip_bias;
    int has_lightmap, depth_lequal;
    float fog_exp_density;
    int alpha_mask;
    float mask_u_off, mask_v_off;
    int untextured, color_trunc;
    float cover_eps;
    int sample_mode;
};
struct SkyDesc {
    float sky_top_x, sky_top_y, sky_top_z;
    float fog_x, fog_y, fog_z;
    int sunset_active;
    float sunset[4];
    float sun_h_x, sun_h_y, sun_h_z;
    float star_b, cos_angle, sin_angle;
    int underwater;
    float underwater_fog_x, underwater_fog_y, underwater_fog_z;
    float underwater_density;
    float plane_y;
};
struct SkyParams { float basis[11]; uint width, height; };

inline float edge(float ax, float ay, float bx, float by, float px, float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}
inline bool top_left(float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay;
    return (dy > 0.0f) || (dy == 0.0f && dx < 0.0f);
}
inline float log2_det(float x) {
    uint bits = as_type<uint>(x);
    int e = int((bits >> 23) & 0xffu) - 127;
    bits = (bits & 0x007fffffu) | 0x3f800000u;
    float m = as_type<float>(bits);
    float p = -1.7417939f + (2.8212026f + (-1.4699568f +
              (0.4479489f - 0.0563525f * m) * m) * m) * m;
    return float(e) + p;
}
inline float tri_lod(thread const ScreenVert &v0,
                     thread const ScreenVert &v1,
                     thread const ScreenVert &v2,
                     constant TextureDesc &tex, float area2) {
    if (tex.level_count == 0 || tex.level_width[0] == 0 || tex.level_height[0] == 0)
        return 0.0f;
    float iw0 = 1.0f / v0.invw, iw1 = 1.0f / v1.invw, iw2 = 1.0f / v2.invw;
    float u0 = v0.uvx * iw0, vv0 = v0.uvy * iw0;
    float u1 = v1.uvx * iw1, vv1 = v1.uvy * iw1;
    float u2 = v2.uvx * iw2, vv2 = v2.uvy * iw2;
    float tw = float(tex.level_width[0]), th = float(tex.level_height[0]);
    float ex1 = (u1 - u0) * tw, ey1 = (vv1 - vv0) * th;
    float ex2 = (u2 - u0) * tw, ey2 = (vv2 - vv0) * th;
    float tex_area2 = fabs(ex1 * ey2 - ex2 * ey1);
    float pix_area2 = fabs(area2);
    if (tex_area2 <= 0.0f || pix_area2 <= 0.0f) return 0.0f;
    return 0.5f * log2_det(tex_area2 / pix_area2);
}

inline uchar4 sample_level(device const uchar4 *pixels,
                           constant TextureDesc &tex, uint level,
                           float u, float v, int mode) {
    if (tex.level_count == 0) return uchar4(0, 0, 0, 255);
    level = min(level, tex.level_count - 1);
    int w = int(tex.level_width[level]), h = int(tex.level_height[level]);
    if (w <= 0 || h <= 0) return uchar4(0, 0, 0, 255);
    float fu = u * float(w), fv = v * float(h);
    int ix, iy;
    if (level != 0) {
        ix = int(floor(fu)); iy = int(floor(fv));
    } else if (mode == 1) {
        ix = int(floor(fu)); iy = int(floor(fv));
    } else if (mode == 2) {
        ix = int(floor(fu + 0.5f)); iy = int(floor(fv + 0.5f));
    } else if (mode == 3) {
        ix = int(floor(fu + 1.0e-4f)); iy = int(floor(fv + 1.0e-4f));
    } else {
        ix = int(floor(fu - 1.0e-4f)); iy = int(floor(fv - 1.0e-4f));
    }
    ix = clamp(ix, 0, w - 1); iy = clamp(iy, 0, h - 1);
    return pixels[tex.level_offset[level] + uint(iy * w + ix)];
}

inline float exp_neg(float x) {
    if (x <= 0.0f) return 1.0f;
    float t = x * 1.4426950408889634f;
    if (t >= 127.0f) return 0.0f;
    float fi = floor(t), f = t - fi;
    float u = f * -0.6931471805599453f;
    float p = 1.0f + u * (1.0f + u * (0.5f + u * (0.16666666666666666f
              + u * (0.041666666666666664f + u * 0.008333333333333333f))));
    int n = int(fi); float s = 1.0f;
    while (n >= 8) { s = s * 0.00390625f; n -= 8; }
    while (n > 0) { s = s * 0.5f; --n; }
    return p * s;
}

struct Fragment {
    float u, v, light, ao;
    uchar4 tint;
    float eye_dist, lod, blk;
};

inline uchar4 shade(constant ShadeDesc &sh, constant TextureDesc &tex,
                    device const uchar4 *pixels,
                    device const uchar4 *lightmap,
                    thread const Fragment &frag) {
    const float inv255 = 1.0f / 255.0f;
    if (sh.alpha_mask != 0 && frag.light < 0.0f) {
        uchar4 mask = sample_level(pixels, tex, 0, frag.u + sh.mask_u_off,
                                   frag.v + sh.mask_v_off, sh.sample_mode);
        if (float(mask.a) * inv255 <= frag.ao) return uchar4(0);
    }
    uchar4 texel = sh.untextured ? uchar4(255) :
        sample_level(pixels, tex,
            sh.use_mips ? uint(clamp(int(floor(frag.lod + sh.mip_bias + 0.5f)),
                                     0, int(tex.level_count) - 1)) : 0,
            frag.u, frag.v, sh.sample_mode);
    int at = sh.alpha_test || sh.layer == 1 || sh.layer == 2;
    if (sh.alpha_mask != 0 && frag.light < 0.0f) at = 0;
    if (at) {
        float ref = sh.alpha_ref > 0.0f ? sh.alpha_ref : 0.5f;
        int threshold = int(ref * 255.0f + 1.0e-5f);
        if (int(texel.a) <= threshold) return uchar4(0);
    }
    float lmr = 1.0f, lmg = 1.0f, lmb = 1.0f;
    float lscalar = frag.light < 0.0f ? 1.0f : frag.light;
    float ao_mul = (sh.alpha_mask != 0 && frag.light < 0.0f) ? 1.0f : frag.ao;
    if (sh.has_lightmap != 0 && frag.light >= 0.0f) {
        float s = clamp(frag.light, 0.0f, 15.0f);
        float b = clamp(frag.blk, 0.0f, 15.0f);
        int s0 = int(floor(s)), b0 = int(floor(b));
        int s1 = min(s0 + 1, 15), b1 = min(b0 + 1, 15);
        float fs = s - float(s0), fb = b - float(b0);
        uchar4 t00 = lightmap[s0 * 16 + b0], t01 = lightmap[s0 * 16 + b1];
        uchar4 t10 = lightmap[s1 * 16 + b0], t11 = lightmap[s1 * 16 + b1];
        float w00 = (1.0f - fs) * (1.0f - fb), w01 = (1.0f - fs) * fb;
        float w10 = fs * (1.0f - fb), w11 = fs * fb;
        lmr = (float(t00.r) * w00 + float(t01.r) * w01 +
               float(t10.r) * w10 + float(t11.r) * w11) * inv255;
        lmg = (float(t00.g) * w00 + float(t01.g) * w01 +
               float(t10.g) * w10 + float(t11.g) * w11) * inv255;
        lmb = (float(t00.b) * w00 + float(t01.b) * w01 +
               float(t10.b) * w10 + float(t11.b) * w11) * inv255;
        lscalar = 1.0f;
    }
    float la = lscalar * ao_mul;
    float tr = float(frag.tint.r) * inv255;
    float tg = float(frag.tint.g) * inv255;
    float tb = float(frag.tint.b) * inv255;
    float cr = (float(texel.r) * inv255) * tr * la * lmr;
    float cg = (float(texel.g) * inv255) * tg * la * lmg;
    float cb = (float(texel.b) * inv255) * tb * la * lmb;
    if (sh.enable_fog != 0) {
        float ft = sh.fog_exp_density > 0.0f
            ? 1.0f - exp_neg(sh.fog_exp_density * frag.eye_dist)
            : (frag.eye_dist - sh.fog_start) / (sh.fog_end - sh.fog_start);
        ft = clamp(ft, 0.0f, 1.0f);
        float fr = float(sh.fog_rgba & 255u) * inv255;
        float fg = float((sh.fog_rgba >> 8) & 255u) * inv255;
        float fb = float((sh.fog_rgba >> 16) & 255u) * inv255;
        cr = cr + (fr - cr) * ft;
        cg = cg + (fg - cg) * ft;
        cb = cb + (fb - cb) * ft;
    }
    float add = sh.color_trunc ? 0.0f : 0.5f;
    uchar4 out;
    out.r = uchar(clamp(cr, 0.0f, 1.0f) * 255.0f + add);
    out.g = uchar(clamp(cg, 0.0f, 1.0f) * 255.0f + add);
    out.b = uchar(clamp(cb, 0.0f, 1.0f) * 255.0f + add);
    if (sh.layer == 0) out.a = 255;
    else {
        float ca = (float(texel.a) * inv255) * (float(frag.tint.a) * inv255);
        uchar a = uchar(clamp(ca, 0.0f, 1.0f) * 255.0f + add);
        out.a = a == 0 ? 1 : a;
    }
    return out;
}

inline float sky_clamp01(float x) { return clamp(x, 0.0f, 1.0f); }
inline float sky_smoothstep(float a, float b, float x) {
    if (a == b) return x < a ? 0.0f : 1.0f;
    float t = sky_clamp01((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}
inline float3 sky_normalize(float3 v) {
    float length = sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
    if (length <= 1.0e-12f) return float3(0.0f, 1.0f, 0.0f);
    float inverse = 1.0f / length;
    return float3(v.x*inverse, v.y*inverse, v.z*inverse);
}
inline float3 sky_mix(float3 a, float3 b, float t) {
    return float3(a.x + (b.x-a.x)*t,
                  a.y + (b.y-a.y)*t,
                  a.z + (b.z-a.z)*t);
}
inline float sky_hash21(float x, float y) {
    float s = sin(x * 127.1f + y * 311.7f) * 43758.5453f;
    return s - floor(s);
}
inline float3 sky_corner_fog(float3 vertex_color, float3 fog_color,
                             float cx, float plane_y, float cz) {
    float distance = sqrt(cx*cx + plane_y*plane_y + cz*cz);
    float factor = sky_clamp01((128.0f - distance) / 128.0f);
    return sky_mix(fog_color, vertex_color, factor);
}
inline float3 sky_plane_fog(float3 vertex_color, float3 fog_color,
                            float3 direction, float plane_y) {
    float dir_y = direction.y;
    if ((plane_y > 0.0f && dir_y <= 0.0f) ||
        (plane_y < 0.0f && dir_y >= 0.0f)) return fog_color;
    float t = plane_y / dir_y;
    if (t <= 0.0f) return fog_color;
    float px = direction.x * t, pz = direction.z * t;
    float tx0 = floor(px / 64.0f) * 64.0f;
    float tz0 = floor(pz / 64.0f) * 64.0f;
    float fx = (px - tx0) / 64.0f, fz = (pz - tz0) / 64.0f;
    float3 c00 = sky_corner_fog(vertex_color, fog_color, tx0, plane_y, tz0);
    float3 c10 = sky_corner_fog(vertex_color, fog_color, tx0+64.0f, plane_y, tz0);
    float3 c01 = sky_corner_fog(vertex_color, fog_color, tx0, plane_y, tz0+64.0f);
    float3 c11 = sky_corner_fog(vertex_color, fog_color, tx0+64.0f, plane_y, tz0+64.0f);
    return sky_mix(sky_mix(c00,c10,fx), sky_mix(c01,c11,fx), fz);
}
inline float4 sky_texture(device const uchar4 *pixels, float u, float v) {
    int x = clamp(int(u * 32.0f), 0, 31);
    int y = clamp(int(v * 32.0f), 0, 31);
    return float4(pixels[y*32+x]) * (1.0f/255.0f);
}
inline uchar4 sky_ray(device const SkyDesc &sc, float3 input_direction,
                      device const uchar4 *sun_pixels,
                      device const uchar4 *moon_pixels) {
    float3 direction = sky_normalize(input_direction);
    float eye_y = direction.y;
    float3 sky_top = float3(sc.sky_top_x, sc.sky_top_y, sc.sky_top_z);
    float3 fog = float3(sc.fog_x, sc.fog_y, sc.fog_z);
    if (sc.underwater != 0) {
        float3 underwater_fog = float3(sc.underwater_fog_x,
                                       sc.underwater_fog_y,
                                       sc.underwater_fog_z);
        float3 color = underwater_fog;
        if (eye_y > 1.0e-4f) {
            float t = sc.plane_y / eye_y;
            float factor = exp(-sc.underwater_density * t);
            color = sky_mix(underwater_fog, sky_top, factor);
        }
        return uchar4(uchar(sky_clamp01(color.x)*255.0f+0.5f),
                      uchar(sky_clamp01(color.y)*255.0f+0.5f),
                      uchar(sky_clamp01(color.z)*255.0f+0.5f), 255);
    }

    float3 color = eye_y >= 0.0f
        ? sky_plane_fog(sky_top, fog, direction, sc.plane_y) : fog;
    if (sc.sunset_active != 0) {
        float3 sun_h = float3(sc.sun_h_x, sc.sun_h_y, sc.sun_h_z);
        float azimuth = sky_clamp01(dot(float3(direction.x,0.0f,direction.z),sun_h));
        float abs_eye_y = eye_y < 0.0f ? -eye_y : eye_y;
        float low = 1.0f - sky_smoothstep(0.0f,0.35f,abs_eye_y);
        float weight = sc.sunset[3]*low*azimuth*azimuth*(eye_y > -0.15f ? 1.0f : 0.0f);
        color = sky_mix(color,float3(sc.sunset[0],sc.sunset[1],sc.sunset[2]),
                        sky_clamp01(weight));
    }
    if (sc.star_b > 0.001f && eye_y > 0.02f) {
        float u = (direction.x/(eye_y+0.25f))*26.0f;
        float v = (direction.z/(eye_y+0.25f))*26.0f;
        float gx=floor(u), gy=floor(v), h=sky_hash21(gx,gy);
        if (h > 0.985f) {
            float px=sky_hash21(gx+1.3f,gy), py=sky_hash21(gx,gy+2.7f);
            float dx=(u-gx)-px, dy=(v-gy)-py;
            float point=1.0f-sky_smoothstep(0.0f,0.020f,dx*dx+dy*dy);
            float twinkle=0.5f+0.5f*sky_hash21(gy,gx);
            float star=sc.star_b*twinkle*point*sky_smoothstep(0.02f,0.15f,eye_y);
            color=sky_mix(color,float3(1.0f),sky_clamp01(star));
        }
    }
    {
        float3 center=float3(-sc.sin_angle,sc.cos_angle,0.0f);
        float denominator=dot(direction,center);
        if(denominator>1.0e-4f){
            float t=100.0f/denominator;
            float3 point=direction*t;
            float lx=point.z;
            float lz=-sc.cos_angle*point.x-sc.sin_angle*point.y;
            if(lx>=-30.0f&&lx<=30.0f&&lz>=-30.0f&&lz<=30.0f){
                float4 sample=sky_texture(sun_pixels,(lx+30.0f)/60.0f,
                                          (lz+30.0f)/60.0f);
                color=float3(color.x+sample.x,color.y+sample.y,color.z+sample.z);
            }
        }
    }
    {
        float3 center=float3(sc.sin_angle,-sc.cos_angle,0.0f);
        float denominator=dot(direction,center);
        if(denominator>1.0e-4f){
            float t=100.0f/denominator;
            float3 point=direction*t;
            float lx=point.z;
            float lz=-sc.cos_angle*point.x-sc.sin_angle*point.y;
            if(lx>=-20.0f&&lx<=20.0f&&lz>=-20.0f&&lz<=20.0f){
                float4 sample=sky_texture(moon_pixels,(20.0f-lx)/40.0f,
                                           (lz+20.0f)/40.0f);
                color=float3(color.x+sample.x,color.y+sample.y,color.z+sample.z);
            }
        }
    }
    return uchar4(uchar(sky_clamp01(color.x)*255.0f+0.5f),
                  uchar(sky_clamp01(color.y)*255.0f+0.5f),
                  uchar(sky_clamp01(color.z)*255.0f+0.5f),255);
}

kernel void cr_metal_sky(device uchar4 *color [[buffer(0)]],
                         device const SkyDesc &sc [[buffer(1)]],
                         device const SkyParams &p [[buffer(2)]],
                         device const uchar4 *sun_pixels [[buffer(3)]],
                         device const uchar4 *moon_pixels [[buffer(4)]],
                         uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= p.width || gid.y >= p.height) return;
    float ndc_y=1.0f-2.0f*(float(gid.y)+0.5f)/float(p.height);
    float sv=ndc_y*p.basis[9];
    float ndc_x=2.0f*(float(gid.x)+0.5f)/float(p.width)-1.0f;
    float su=ndc_x*p.basis[9]*p.basis[10];
    float3 direction=float3(su*p.basis[3]+sv*p.basis[6]+p.basis[0],
                            su*p.basis[4]+sv*p.basis[7]+p.basis[1],
                            su*p.basis[5]+sv*p.basis[8]+p.basis[2]);
    color[gid.y*p.width+gid.x]=sky_ray(sc,direction,sun_pixels,moon_pixels);
}

kernel void cr_metal_bbox(device const ScreenTri *tris [[buffer(0)]],
                          device TriBox *boxes [[buffer(1)]],
                          constant RasterParams &p [[buffer(2)]],
                          uint t [[thread_position_in_grid]]) {
    if (t >= p.ntris) return;
    device const ScreenVert &v0 = tris[t].v[0];
    device const ScreenVert &v1 = tris[t].v[1];
    device const ScreenVert &v2 = tris[t].v[2];
    float area = edge(v0.sx, v0.sy, v1.sx, v1.sy, v2.sx, v2.sy);
    if (area * -1.0f <= 0.0f) { boxes[t] = TriBox{1, 1, 0, 0}; return; }
    int minx = max(int(floor(min(v0.sx, min(v1.sx, v2.sx)))), 0);
    int maxx = min(int(ceil(max(v0.sx, max(v1.sx, v2.sx)))), int(p.width));
    int miny = max(int(floor(min(v0.sy, min(v1.sy, v2.sy)))), 0);
    int maxy = min(int(ceil(max(v0.sy, max(v1.sy, v2.sy)))), int(p.height));
    boxes[t] = TriBox{minx, miny, maxx, maxy};
}

kernel void cr_metal_raster(device uchar4 *color [[buffer(0)]],
                            device float *depth [[buffer(1)]],
                            device const ScreenTri *tris [[buffer(2)]],
                            device const TriBox *boxes [[buffer(3)]],
                            device const uchar4 *pixels [[buffer(4)]],
                            device const uchar4 *lightmap [[buffer(5)]],
                            constant TextureDesc &tex [[buffer(6)]],
                            constant ShadeDesc &sh [[buffer(7)]],
                            constant RasterParams &p [[buffer(8)]],
                            uint2 gid [[thread_position_in_grid]],
                            uint2 lid2 [[thread_position_in_threadgroup]],
                            uint2 tgp [[threadgroup_position_in_grid]]) {
    uint lane = lid2.y * CR_TILE + lid2.x;
    bool valid = gid.x < p.width && gid.y < p.height;
    uint index = valid ? gid.y * p.width + gid.x : 0;
    float fx = float(gid.x) + 0.5f, fy = float(gid.y) + 0.5f;
    uchar4 cur = valid ? color[index] : uchar4(0);
    float curz = valid ? depth[index] : 0.0f;
    threadgroup int flags[256];
    threadgroup uint list[256];
    int tile_minx = int(tgp.x * CR_TILE), tile_miny = int(tgp.y * CR_TILE);
    int tile_maxx = tile_minx + int(CR_TILE), tile_maxy = tile_miny + int(CR_TILE);

    for (uint base = 0; base < p.ntris; base += CR_BATCH) {
        uint t0 = base + lane;
        int pass = 0;
        if (t0 < p.ntris) {
            TriBox b = boxes[t0];
            pass = b.minx < tile_maxx && b.maxx > tile_minx &&
                   b.miny < tile_maxy && b.maxy > tile_miny;
        }
        flags[lane] = pass;
        threadgroup_barrier(mem_flags::mem_threadgroup);
        for (uint off = 1; off < CR_BATCH; off <<= 1) {
            int add = lane >= off ? flags[lane - off] : 0;
            threadgroup_barrier(mem_flags::mem_threadgroup);
            flags[lane] += add;
            threadgroup_barrier(mem_flags::mem_threadgroup);
        }
        int total = flags[CR_BATCH - 1];
        int slot = flags[lane] - pass;
        if (pass) list[uint(slot)] = t0;
        threadgroup_barrier(mem_flags::mem_threadgroup);

        if (valid) for (int k = 0; k < total; ++k) {
            uint t = list[uint(k)];
            device const ScreenVert &v0d = tris[t].v[0];
            device const ScreenVert &v1d = tris[t].v[1];
            device const ScreenVert &v2d = tris[t].v[2];
            ScreenVert v0 = v0d, v1 = v1d, v2 = v2d;
            float area = edge(v0.sx, v0.sy, v1.sx, v1.sy, v2.sx, v2.sy);
            if (area * -1.0f <= 0.0f) continue;
            float lod = sh.use_mips ? tris[t].lod : 0.0f;
            bool tl0 = top_left(v1.sx, v1.sy, v2.sx, v2.sy);
            bool tl1 = top_left(v2.sx, v2.sy, v0.sx, v0.sy);
            bool tl2 = top_left(v0.sx, v0.sy, v1.sx, v1.sy);
            float w0 = edge(v1.sx, v1.sy, v2.sx, v2.sy, fx, fy);
            float w1 = edge(v2.sx, v2.sy, v0.sx, v0.sy, fx, fy);
            float w2 = edge(v0.sx, v0.sy, v1.sx, v1.sy, fx, fy);
            float b0 = w0 / area, b1 = w1 / area, b2 = w2 / area;
            bool s0 = b0 > 0.0f || (b0 == 0.0f && tl0);
            bool s1 = b1 > 0.0f || (b1 == 0.0f && tl1);
            bool s2 = b2 > 0.0f || (b2 == 0.0f && tl2);
            bool strict_in = s0 && s1 && s2;
            bool in0 = s0, in1 = s1, in2 = s2;
            if (!strict_in && sh.cover_eps > 0.0f) {
                float el0 = sqrt((v2.sx-v1.sx)*(v2.sx-v1.sx) + (v2.sy-v1.sy)*(v2.sy-v1.sy));
                float el1 = sqrt((v0.sx-v2.sx)*(v0.sx-v2.sx) + (v0.sy-v2.sy)*(v0.sy-v2.sy));
                float el2 = sqrt((v1.sx-v0.sx)*(v1.sx-v0.sx) + (v1.sy-v0.sy)*(v1.sy-v0.sy));
                if (!in0 && el0 > 1.0e-12f && w0*area < 0.0f && fabs(w0)/el0 <= sh.cover_eps) in0=true;
                if (!in1 && el1 > 1.0e-12f && w1*area < 0.0f && fabs(w1)/el1 <= sh.cover_eps) in1=true;
                if (!in2 && el2 > 1.0e-12f && w2*area < 0.0f && fabs(w2)/el2 <= sh.cover_eps) in2=true;
            }
            if (!(in0 && in1 && in2)) continue;
            bool slack = !strict_in;
            float invw = b0*v0.invw + b1*v1.invw + b2*v2.invw;
            float z = b0*v0.sz + b1*v1.sz + b2*v2.sz;
            bool depth_ok = slack ? (z + 1.0e-5f < curz)
                : (z < curz || (sh.depth_lequal != 0 && z == curz));
            if (!depth_ok) continue;
            float iw = 1.0f / invw;
            Fragment f;
            f.u = (b0*v0.uvx+b1*v1.uvx+b2*v2.uvx)*iw;
            f.v = (b0*v0.uvy+b1*v1.uvy+b2*v2.uvy)*iw;
            f.light = (b0*v0.light+b1*v1.light+b2*v2.light)*iw;
            f.ao = (b0*v0.ao+b1*v1.ao+b2*v2.ao)*iw;
            f.blk = (b0*v0.blk+b1*v1.blk+b2*v2.blk)*iw;
            f.tint.r = uchar(clamp((b0*v0.tint_r+b1*v1.tint_r+b2*v2.tint_r)*iw,0.0f,255.0f)+0.5f);
            f.tint.g = uchar(clamp((b0*v0.tint_g+b1*v1.tint_g+b2*v2.tint_g)*iw,0.0f,255.0f)+0.5f);
            f.tint.b = uchar(clamp((b0*v0.tint_b+b1*v1.tint_b+b2*v2.tint_b)*iw,0.0f,255.0f)+0.5f);
            f.tint.a = uchar(clamp((b0*v0.tint_a+b1*v1.tint_a+b2*v2.tint_a)*iw,0.0f,255.0f)+0.5f);
            f.eye_dist = (b0*v0.eye_dist+b1*v1.eye_dist+b2*v2.eye_dist)*iw;
            f.lod = lod;
            uchar4 c = shade(sh, tex, pixels, lightmap, f);
            if (c.a == 0) continue;
            if (sh.blend == 1 || sh.blend == 4) {
                uchar4 d = cur; float a = float(c.a)*(1.0f/255.0f), ia=1.0f-a;
                cur.r=uchar(clamp(float(c.r)*a+float(d.r)*ia,0.0f,255.0f)+0.5f);
                cur.g=uchar(clamp(float(c.g)*a+float(d.g)*ia,0.0f,255.0f)+0.5f);
                cur.b=uchar(clamp(float(c.b)*a+float(d.b)*ia,0.0f,255.0f)+0.5f);
                cur.a=255; if(sh.blend==4)curz=z;
            } else if (sh.blend == 2) {
                uchar4 d=cur;
                cur.r=uchar(min(255.0f,(2.0f*float(c.r)*float(d.r))*(1.0f/255.0f))+0.5f);
                cur.g=uchar(min(255.0f,(2.0f*float(c.g)*float(d.g))*(1.0f/255.0f))+0.5f);
                cur.b=uchar(min(255.0f,(2.0f*float(c.b)*float(d.b))*(1.0f/255.0f))+0.5f);cur.a=255;
            } else if (sh.blend == 3) {
                uchar4 d=cur;float a=float(c.a)*(1.0f/255.0f);
                cur.r=uchar(min(255.0f,float(c.r)*a+float(d.r))+0.5f);
                cur.g=uchar(min(255.0f,float(c.g)*a+float(d.g))+0.5f);
                cur.b=uchar(min(255.0f,float(c.b)*a+float(d.b))+0.5f);cur.a=255;
            } else { cur=c;curz=z; }
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    if (valid) { color[index]=cur;depth[index]=curz; }
}
)CRMSL";

#endif
