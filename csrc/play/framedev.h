/* The judges' device frames: the ABI between play (csrc/play/play.c) and
 * the device renderer's plugin, out/native/cuda/libframedev.so
 * (csrc/cuda/render, built on a machine with nvcc: the dev host's 3090). play
 * loads it at run time on Linux when it is there (dlopen) and draws the
 * world frame of every judged shot (--shots) with it; without it (the Mac,
 * no device, no free device slot) the C renderer draws them, as before.
 *
 * The plugin draws what raster_obs.h records (raster_rec_begin_only): the
 * sky, the terrain and every world pass up to the translucent sections,
 * bit for bit the C renderer's frame and depth buffer; play draws the rest
 * of the frame in C over them (the weather, the hand, the overlays, the
 * HUD, the screens, the toast). */
#ifndef NETHERITE_FRAMEDEV_H
#define NETHERITE_FRAMEDEV_H

#include <stddef.h>

struct raster_obs;

/* bumped with any change below or to raster_obs.h's structs; sizes lists
 * those structs' sizes as play was built, which the plugin checks */
enum { FRAMEDEV_ABI = 3 };

struct framedev_sizes {
    size_t obs, draw, sky, cmd, estate, equad, tex, crack, portal, line;
};

struct framedev_api {
    int abi;
    /* a renderer of up to MAX_ENVS frames of W x H a render; NULL when
     * there is none (WHY says why: no device, no free device slot, too
     * little free device memory, another build, or a judge found the
     * device's frames slower than the C renderer's a moment ago, which
     * FORCE ignores) */
    void *(*open)(int max_envs, int w, int h, const struct framedev_sizes *sizes, int force, char *why, size_t why_n);
    /* environment ENV's frame for the next render (O stays valid until it) */
    int (*set)(void *r, int env, const struct raster_obs *o);
    /* draws environments 0..N-1; 0 on success, *MS its device time */
    int (*render)(void *r, int n, double *ms);
    /* the last render's N frames (w * h * 3 each) and depth buffers (w * h
     * floats each), which came back with it */
    int (*download)(void *r, int n, unsigned char *const *rgb, float *const *depth);
    void (*close)(void *r);
    /* the device's frames cost this client more than the C renderer's
     * (WHY): the judges that open in the next minute or two decline */
    void (*slow)(void *r, const char *why);
};

#ifdef __cplusplus
extern "C" {
#endif
/* the plugin's one export */
const struct framedev_api *framedev_api(void);
#ifdef __cplusplus
}
#endif

#endif
