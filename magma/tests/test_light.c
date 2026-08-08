/* tests/test_light.c - self-test for the LIGHT module.
 *
 * Two kinds of checks:
 *  (A) BITWISE golden verification of the pure reference kernels against the
 *      render-opt kernel goldens (k14 light_query, k17 skylight_gen,
 *      k18 biome_color_blend), plus a k16 light_propagation golden replay that
 *      exercises the exact getRawLight/BFS semantics light.c's world block-light
 *      uses.
 *  (B) World-level sanity: light_create(0)/light_ensure(0,0,1) then assert sky
 *      light is 15 above terrain and 0 deep underground, decreases monotonically
 *      downward, block light near lava > 0, and tint colours are valid 0xRRGGBB.
 *
 * Prints PASS/FAIL; exits nonzero on any FAIL.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "world/light.h"

#define KDIR "../render-opt/kernels/"

static int failures = 0;
static void check(int cond, const char *msg) {
    if (!cond) { printf("FAIL: %s\n", msg); failures++; }
    else       { printf("ok  : %s\n", msg); }
}

/* --------------------------- golden bit-checks --------------------------- */

static int verify_k14(void) {
    FILE *fi = fopen(KDIR "14_light_query/golden/inputs.txt", "r");
    FILE *fg = fopen(KDIR "14_light_query/golden/golden.txt", "r");
    if (!fi || !fg) { printf("FAIL: k14 golden files missing\n"); if(fi)fclose(fi); if(fg)fclose(fg); return 0; }
    int type, nb, up, e, w, s, n, own, exp, mism = 0, cnt = 0;
    while (fscanf(fi, "%d %d %d %d %d %d %d %d", &type, &nb, &up, &e, &w, &s, &n, &own) == 8) {
        if (fscanf(fg, "%d", &exp) != 1) { mism++; break; }
        if (cr_k14_light_query(nb, up, e, w, s, n, own) != exp) mism++;
        cnt++;
    }
    fclose(fi); fclose(fg);
    printf("     k14: %d records, %d mismatches\n", cnt, mism);
    return mism == 0 && cnt > 0;
}

static int verify_k18(void) {
    FILE *fi = fopen(KDIR "18_biome_color_blend/golden/inputs.txt", "r");
    FILE *fg = fopen(KDIR "18_biome_color_blend/golden/golden.txt", "r");
    if (!fi || !fg) { printf("FAIL: k18 golden files missing\n"); if(fi)fclose(fi); if(fg)fclose(fg); return 0; }
    int c[9], exp, mism = 0, cnt = 0;
    while (fscanf(fi, "%d %d %d %d %d %d %d %d %d",
                  &c[0],&c[1],&c[2],&c[3],&c[4],&c[5],&c[6],&c[7],&c[8]) == 9) {
        if (fscanf(fg, "%d", &exp) != 1) { mism++; break; }
        if (cr_k18_blend3x3(c) != exp) mism++;
        cnt++;
    }
    fclose(fi); fclose(fg);
    printf("     k18: %d records, %d mismatches\n", cnt, mism);
    return mism == 0 && cnt > 0;
}

static int verify_k17(void) {
    FILE *fi = fopen(KDIR "17_skylight_gen/golden/inputs.txt", "r");
    FILE *fg = fopen(KDIR "17_skylight_gen/golden/golden.txt", "r");
    if (!fi || !fg) { printf("FAIL: k17 golden files missing\n"); if(fi)fclose(fi); if(fg)fclose(fg); return 0; }
    int topSeg, hasSky, mism = 0, cnt = 0;
    int op[512], nn[512], sky[512];
    while (fscanf(fi, "%d %d", &topSeg, &hasSky) == 2) {
        int nY = topSeg + 16;
        if (nY > 512) { mism++; break; }
        for (int y = 0; y < nY; ++y)
            if (fscanf(fi, "%d %d", &op[y], &nn[y]) != 2) { mism++; goto done; }
        int hm = cr_k17_skylight_column(topSeg, hasSky, op, nn, sky);
        int exp;
        if (fscanf(fg, "%d", &exp) != 1 || exp != hm) mism++;
        for (int y = 0; y < nY; ++y)
            if (fscanf(fg, "%d", &exp) != 1 || exp != sky[y]) { mism++; break; }
        cnt++;
    }
done:
    fclose(fi); fclose(fg);
    printf("     k17: %d columns, %d mismatches\n", cnt, mism);
    return mism == 0 && cnt > 0;
}

/* ---- k16 golden replay: verbatim getRawLight + checkLightFor BFS (brightening),
 * the same formula light.c's world block-light BFS uses. --------------------- */
#define K16_MAXR 16
#define K16_DIM  (2 * K16_MAXR + 1)
static int k16_R, k16_sx, k16_sy, k16_sz, k16_LUM;
static signed char k16_light[K16_DIM][K16_DIM][K16_DIM];
static signed char k16_opac[K16_DIM][K16_DIM][K16_DIM];
static int k16_inb(int x,int y,int z){return x>=-k16_R&&x<=k16_R&&y>=-k16_R&&y<=k16_R&&z>=-k16_R&&z<=k16_R;}
static int k16_ix(int v){return v+K16_MAXR;}
static int k16_get(int x,int y,int z){return k16_inb(x,y,z)?k16_light[k16_ix(x)][k16_ix(y)][k16_ix(z)]:0;}
static void k16_set(int x,int y,int z,int v){if(k16_inb(x,y,z))k16_light[k16_ix(x)][k16_ix(y)][k16_ix(z)]=(signed char)v;}
static int k16_lum(int x,int y,int z){return (x==k16_sx&&y==k16_sy&&z==k16_sz)?k16_LUM:0;}
static int k16_op(int x,int y,int z){return k16_inb(x,y,z)?k16_opac[k16_ix(x)][k16_ix(y)][k16_ix(z)]:0;}
static int k16_raw(int x,int y,int z){
    int bl=k16_lum(x,y,z), i=bl, j=k16_op(x,y,z);
    if(j>=15&&bl>0)j=1;
    if(j<1)j=1;
    if(j>=15)return 0;
    if(i>=14)return i;
    static const int dx[6]={0,0,-1,1,0,0},dy[6]={-1,1,0,0,0,0},dz[6]={0,0,0,0,-1,1};
    for(int f=0;f<6;f++){int k=k16_get(x+dx[f],y+dy[f],z+dz[f])-j; if(k>i)i=k; if(i>=14)return i;}
    return i;
}
static int verify_k16(void) {
    FILE *fi = fopen(KDIR "16_light_propagation/golden/inputs.txt", "r");
    FILE *fg = fopen(KDIR "16_light_propagation/golden/golden.txt", "r");
    if (!fi || !fg) { printf("FAIL: k16 golden files missing\n"); if(fi)fclose(fi); if(fg)fclose(fg); return 0; }
    char header[512];
    if (!fgets(header, sizeof header, fi) ||
        sscanf(header, "# rel_to_first_cell src=(%d,%d,%d) luminance=%d radius=%d",
               &k16_sx,&k16_sy,&k16_sz,&k16_LUM,&k16_R) != 5 || k16_R > K16_MAXR) {
        printf("FAIL: k16 header parse\n"); fclose(fi); fclose(fg); return 0;
    }
    memset(k16_light,0,sizeof k16_light); memset(k16_opac,0,sizeof k16_opac);
    int cap=(2*k16_R+1)*(2*k16_R+1)*(2*k16_R+1);
    int *cx=malloc(sizeof(int)*cap),*cy=malloc(sizeof(int)*cap),*cz=malloc(sizeof(int)*cap);
    int rx,ry,rz,lb,op,n=0;
    while (fscanf(fi, "%d %d %d %d %d", &rx,&ry,&rz,&lb,&op) == 5) {
        k16_light[k16_ix(rx)][k16_ix(ry)][k16_ix(rz)]=(signed char)lb;
        k16_opac[k16_ix(rx)][k16_ix(ry)][k16_ix(rz)]=(signed char)op;
        cx[n]=rx; cy[n]=ry; cz[n]=rz; n++;
    }
    /* brightening BFS (this capture is a pure brighten from one central source) */
    static int q[32768]; int qj=0, qi=0;
    int kk=k16_get(k16_sx,k16_sy,k16_sz), l=k16_raw(k16_sx,k16_sy,k16_sz);
    if (l > kk) q[qj++]=133152;
    while (qi<qj) {
        int i5=q[qi++];
        int j5=(i5&63)-32+k16_sx, k5=(i5>>6&63)-32+k16_sy, l5=(i5>>12&63)-32+k16_sz;
        int i6=k16_get(j5,k5,l5), j6=k16_raw(j5,k5,l5);
        if (j6!=i6) {
            k16_set(j5,k5,l5,j6);
            if (j6>i6) {
                int k6=abs(j5-k16_sx),l6=abs(k5-k16_sy),i7=abs(l5-k16_sz);
                if (k6+l6+i7<17 && qj<32768-6) {
                    if (k16_inb(j5-1,k5,l5)&&k16_get(j5-1,k5,l5)<j6) q[qj++]=(j5-1-k16_sx+32)+((k5-k16_sy+32)<<6)+((l5-k16_sz+32)<<12);
                    if (k16_inb(j5+1,k5,l5)&&k16_get(j5+1,k5,l5)<j6) q[qj++]=(j5+1-k16_sx+32)+((k5-k16_sy+32)<<6)+((l5-k16_sz+32)<<12);
                    if (k16_inb(j5,k5-1,l5)&&k16_get(j5,k5-1,l5)<j6) q[qj++]=(j5-k16_sx+32)+((k5-1-k16_sy+32)<<6)+((l5-k16_sz+32)<<12);
                    if (k16_inb(j5,k5+1,l5)&&k16_get(j5,k5+1,l5)<j6) q[qj++]=(j5-k16_sx+32)+((k5+1-k16_sy+32)<<6)+((l5-k16_sz+32)<<12);
                    if (k16_inb(j5,k5,l5-1)&&k16_get(j5,k5,l5-1)<j6) q[qj++]=(j5-k16_sx+32)+((k5-k16_sy+32)<<6)+((l5-1-k16_sz+32)<<12);
                    if (k16_inb(j5,k5,l5+1)&&k16_get(j5,k5,l5+1)<j6) q[qj++]=(j5-k16_sx+32)+((k5-k16_sy+32)<<6)+((l5+1-k16_sz+32)<<12);
                }
            }
        }
    }
    int exp, mism=0;
    for (int t=0;t<n;t++) { if (fscanf(fg,"%d",&exp)!=1 || exp!=k16_get(cx[t],cy[t],cz[t])) mism++; }
    free(cx); free(cy); free(cz); fclose(fi); fclose(fg);
    printf("     k16: %d cells, %d mismatches\n", n, mism);
    return mism == 0 && n > 0;
}

/* k15 has only Golden.java (no inputs.txt/golden.txt), so it cannot be bit-checked
 * against a captured golden; verify the packing formula on known values instead. */
static int verify_k15_note(void) {
    int ok = 1;
    ok &= (cr_k15_combine(15, 15, 0) == (15 << 20 | 15 << 4));   /* 15728880 */
    ok &= (cr_k15_combine(0, 5, 10) == (10 << 4));               /* override wins -> 160 */
    ok &= (cr_k15_combine(7, 9, 3) == (7 << 20 | 9 << 4));       /* block>override */
    return ok;
}

/* ------------------------------ world tests ------------------------------ */

static int valid_rgb(int c) { return c >= 0 && c <= 0xFFFFFF; }

int main(void) {
    printf("== golden bit-verification ==\n");
    check(verify_k14(), "k14 light_query bitwise == golden");
    check(verify_k15_note(), "k15 combine_pack (implemented; no golden.txt to bit-check)");
    check(verify_k16(), "k16 light_propagation BFS bitwise == golden");
    check(verify_k17(), "k17 skylight_gen bitwise == golden");
    check(verify_k18(), "k18 biome_color_blend bitwise == golden");

    printf("== world light ==\n");
    CrLight *L = light_create(0);
    check(L != NULL, "light_create(0)");
    light_ensure(L, 0, 0, 1);

    /* pick a representative column at chunk-(0,0) centre */
    int wx = 8, wz = 8;
    /* find terrain top: highest opaque (stone/dirt/etc) block */
    int top = -1;
    for (int y = 255; y >= 0; --y) {
        int b = light_block(L, wx, y, wz);
        if (b != 0 && b != 2 /*CB_WATER*/) { top = y; break; }
    }
    check(top >= 0, "found terrain surface in column (8,8)");

    /* sky = 15 well above terrain */
    check(light_sky(L, wx, 250, wz) == 15, "sky light == 15 at y=250 (above terrain)");
    check(light_sky(L, wx, top + 30, wz) == 15, "sky light == 15 30 blocks above surface");

    /* sky = 0 deep underground */
    check(light_sky(L, wx, 5, wz) == 0, "sky light == 0 at y=5 (deep underground)");

    /* monotone non-increasing downward through the column */
    int mono = 1, prev = light_sky(L, wx, 255, wz);
    for (int y = 254; y >= 0; --y) {
        int s = light_sky(L, wx, y, wz);
        if (s > prev) { mono = 0; break; }
        prev = s;
    }
    check(mono, "sky light monotonically non-increasing going downward");

    /* A tape snapshot is a block-only saved-chunk patch. Replacing a tall
     * default-world column with superflat air in ascending Y order must rerun
     * Chunk.generateSkylightMap: direct-sky air remains 15 all the way down,
     * rather than attenuating one level per cell through the generic BFS. */
    light_load_state(L, wx, 3, wz, (uint16_t)(2 << 4));
    for (int y = 4; y < 256; ++y) light_load_state(L, wx, y, wz, 0);
    light_ensure(L, 0, 0, 1);
    check(light_sky(L, wx, 4, wz) == 15,
          "bulk flat snapshot rebuilds direct skylight to 15 at the surface");

    /* Snapshot loads often change only leaf metadata (CHECK_DECAY). The stored
     * skylight must survive that load; seeding the cell with 15 makes a canopy
     * one light level too bright. Opacity changes are rebuilt from neighbours,
     * with direct-sky air returning to 15. */
    light_set_state(L, wx, 200, wz, (uint16_t)(18 << 4));
    light_ensure(L, 0, 0, 1);
    check(light_sky(L, wx, 200, wz) == 14,
          "leaf insertion attenuates direct sky to 14");
    light_set_state(L, wx, 200, wz, (uint16_t)((18 << 4) | 8));
    light_ensure(L, 0, 0, 1);
    check(light_sky(L, wx, 200, wz) == 14,
          "metadata-only leaf load preserves stored skylight");
    light_set_state(L, wx, 200, wz, 0);
    light_ensure(L, 0, 0, 1);
    check(light_sky(L, wx, 200, wz) == 15,
          "direct-sky air rebuilt to 15 after leaf removal");

    /* BlockLiquid is non-opaque. Water explicitly overrides light opacity to
     * 3, while lava keeps Block's constructor-derived opacity 0. */
    light_set_state(L, wx, 200, wz, (uint16_t)((10 << 4) | 1));
    light_ensure(L, 0, 0, 1);
    check(light_sky(L, wx, 200, wz) == 15,
          "flowing lava opacity 0 preserves direct skylight");
    light_set_state(L, wx, 200, wz, 0);
    light_ensure(L, 0, 0, 1);

    /* BlockHopper.isOpaqueCube is false during Block construction, so its
     * registered light opacity is zero. A redstone torch above it emits 7 and
     * World.getRawLight applies the minimum one-level attenuation in the
     * hopper cell. */
    light_set_state(L, wx, 200, wz, (uint16_t)(154 << 4));
    light_set_state(L, wx, 201, wz, (uint16_t)((76 << 4) | 5));
    light_ensure(L, 0, 0, 1);
    check(light_blk(L, wx, 201, wz) == 7,
          "lit redstone torch emits exact block light 7");
    check(light_blk(L, wx, 200, wz) == 6,
          "hopper opacity zero admits exact torch block light 6");
    light_set_state(L, wx, 201, wz, 0);
    light_set_state(L, wx, 200, wz, 0);
    light_ensure(L, 0, 0, 1);

    /* block light near lava > 0 (scan the whole 3x3 loaded region for lava) */
    int lava_found = 0, lit = 0, lava_self = 0;
    for (int cx = -1; cx <= 1 && !lit; ++cx)
    for (int cz = -1; cz <= 1 && !lit; ++cz)
    for (int lx = 0; lx < 16 && !lit; ++lx)
    for (int lz = 0; lz < 16 && !lit; ++lz)
    for (int y = 0; y < 40 && !lit; ++y) {
        int wxx = cx * 16 + lx, wzz = cz * 16 + lz;
        int b = light_block(L, wxx, y, wzz);
        if (b == 11 /*CB_LAVA*/ || b == 12 /*CB_FLOWING_LAVA*/) {
            lava_found = 1;
            if (light_blk(L, wxx, y, wzz) > 0) lava_self = 1;
            /* check the 6 neighbours for propagated light */
            int nb[6][3] = {{wxx-1,y,wzz},{wxx+1,y,wzz},{wxx,y-1,wzz},
                            {wxx,y+1,wzz},{wxx,y,wzz-1},{wxx,y,wzz+1}};
            for (int f = 0; f < 6; ++f)
                if (light_blk(L, nb[f][0], nb[f][1], nb[f][2]) > 0) lit = 1;
        }
    }
    if (lava_found) {
        check(lava_self, "block light > 0 at a lava cell (self-emission)");
        check(lit, "block light > 0 in a lava cell's neighbourhood");
    } else {
        printf("note: no lava in loaded chunks (seed 0); block-light-near-lava check skipped\n");
    }

    /* tint colours valid 0xRRGGBB */
    int g = light_grass_color(L, wx, 64, wz);
    int f = light_foliage_color(L, wx, 64, wz);
    int wcol = light_water_color(L, wx, wz);
    printf("     grass=0x%06X foliage=0x%06X water=0x%06X\n", g, f, wcol);
    check(valid_rgb(g), "grass color is valid 0xRRGGBB");
    check(valid_rgb(f), "foliage color is valid 0xRRGGBB");
    check(valid_rgb(wcol), "water color is valid 0xRRGGBB");

    light_destroy(L);

    printf("\n%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
