/* The 1.7.10 client's per-frame render state, exact against
 * netherite.oracle.RenderStateProbe's dump. One call turns the inputs of a
 * frame (options, the entity renderer's carried state, the client player, the
 * world's time and weather, the viewpoint block) into every number that
 * decides pixels before anything draws: EntityRenderer.updateLightmap,
 * updateFogColor, setupFog, setupCameraTransform with orientCamera,
 * setupViewBobbing and hurtCameraEffect, and World's getSkyColor, getFogColor,
 * getCloudColour, getStarBrightness, getCelestialAngle, getSunBrightness and
 * WorldProvider.calcSunriseSunsetColors. */
#ifndef NETHERITE_RENDERSTATE_H
#define NETHERITE_RENDERSTATE_H

#include <stdint.h>

#include "det.h"

struct rs_in
{
    /* opt: GameSettings fields the frame's numbers read */
    int rd, bob, clouds, tpv, ana, dbgcam;
    float fov, gamma;
    int dw, dh; /* display, the projection's aspect */

    /* er: EntityRenderer state as updateRenderer left it */
    float tfx, tfy;      /* torchFlickerX/Y */
    float fc1, fc2;      /* fogColor1/2, the smoothing pair */
    float fmh, fmhp;     /* fovModifierHand/Prev */
    int ruc;             /* rendererUpdateCount */
    float roll, proll;   /* camRoll/prevCamRoll */
    double zoom;         /* cameraZoom */
    int dvd;             /* debugViewDirection */

    /* pl: the render view entity, interpolated by pt */
    double ppx, ppy, ppz; /* prevPos */
    double px, py, pz;
    float yaw, pyaw, pit, ppit;
    float yoff; /* Entity.yOffset */
    float dwm, pdwm;
    float ecyaw, pecyaw, ecpit, pepit; /* cameraYaw/Pitch + prev */
    int hurt, mhurt, death;
    float aaty, hp;
    float portal, pportal; /* timeInPortal/prevTimeInPortal */
    int sleep, crea, brf, resp, nv, nvd, blind, bldur, wb, conf;

    /* wo: the world's time and weather */
    int64_t wt;
    float rain, prain, thu, pthu;
    int lbolt;          /* lastLightningBolt */
    long long cloud;    /* World.cloudColour */
    int dim, nosky, voidp, xzfog;
    double voidf;
    float temp;    /* biome temperature at the player */
    int skytemp;   /* biome.getSkyColorByTemp(temp) */
    float lbt[16]; /* provider.lightBrightnessTable */

    float pt;

    /* vp: the block at the entity viewpoint, as ActiveRenderInfo sees it */
    int mat; /* 0 none, 1 water, 2 lava */

    /* orientCamera's world reads, made by the caller: the third-person
     * distance after its eight rayTraceBlocks (0: the unclipped 4.0), and the
     * bed's metadata & 3 under a sleeping player (-1: no bed block there) */
    double tpdist;
    int bedrot;
};

/* orientCamera's third-person distance: thirdPersonDistance (4.0) shortened
 * to the nearest of eight rays from the eye, 0.1 around it, backward along
 * the look (tpv 2 the front view). RAY answers World.rayTraceBlocks: 1 with
 * the hit vector when a block stops the ray from (sx, sy, sz) to (ex, ey,
 * ez). */
double renderstate_third_person_distance(double x, double y, double z, float yaw, float pitch, int tpv,
                                         int (*ray)(void *ctx, const double s[3], const double e[3], double hit[3]),
                                         void *ctx);

struct rs_out
{
    /* World getters */
    float ang, angr, sun, star;
    float rain;       /* World.getRainStrength(pt) */
    double sky[3], fog[3], cloud[3];
    int rise_ok;
    float rise[4];

    /* updateFogColor's fields */
    float fcr, fcg, fcb, far;
    int lm[256];

    /* setupCameraTransform */
    float proj[16], mv[16];
    double camx, camy, camz;
    int cloudfog;

    /* the GL fog state after the prepareterrain setupFog(0) */
    float gfogc[4], gfogs, gfoge, gfogd;
    int gfogm;
};

void renderstate_compute(const struct rs_in *in, struct rs_out *o);

/* A RenderStateProbe frame row as the inputs above (renderstate_row.c);
 * returns 0 when a field is missing. */
struct jval;
int renderstate_in_from_row(const struct jval *row, struct rs_in *in);

/* EntityRenderer.renderHand's own camera (see renderstate.c). Reads rd, dw,
 * dh, pt, hp, death, mat, the hurt fields and, with bob set, the bobbing
 * fields. */
void renderstate_hand_camera(const struct rs_in *in, float proj[16], float mv[16]);

/* Mesa's fixed-function matrix operations on a column-major float[16], for
 * the passes that build their own modelview the way GL11 calls do. */
void rs_gl_identity(float *m);
void rs_gl_mult(float *m, const float *b);
void rs_gl_rotate(float *m, float angle, float x, float y, float z);
void rs_gl_translate(float *m, float x, float y, float z);
void rs_gl_scale(float *m, float x, float y, float z);

/* EntityRenderer's torch flicker: torchFlickerDX, DY, X and Y. */
struct rs_flicker { float dx, dy, x, y; };

/* EntityRenderer.updateTorchFlicker, once per client tick in updateRenderer:
 * eight Math.random draws on the client stream of DET. The lightmap reads x
 * (rs_in.tfx); y is kept and never read. */
void renderstate_torch_flicker(struct rs_flicker *f, det_state *det);
/* The same step over the eight Math.random values it draws, in order (Det's
 * consumer pin gives them from the flicker's own stream). */
void renderstate_torch_flicker_step(struct rs_flicker *f, const double v[8]);

/* World.getCelestialAngle(pt) in dimension DIM at world time WT. */
float renderstate_celestial(int dim, int64_t wt, float pt);

#endif
