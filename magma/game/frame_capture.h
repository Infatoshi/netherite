#ifndef MAGMA_GAME_FRAME_CAPTURE_H
#define MAGMA_GAME_FRAME_CAPTURE_H

#include "game/runtime.h"

typedef struct GmFrameCapture GmFrameCapture;

GmFrameCapture *gm_frame_capture_open(const GmConfig *cfg, char *err, int err_cap);
/* Call once per tick. Always advances hand-animation state; renders and
 * writes the tick-numbered PPM only when render is non-zero. */
int gm_frame_capture_write(GmFrameCapture *capture, GmRuntime *runtime,
                           const GmAction *action, int render,
                           char *err, int err_cap);
void gm_frame_capture_close(GmFrameCapture *capture);

/* Fill the 16x16 EntityRenderer.updateLightmap LUT for the given world time
 * (overworld texels; callers gate on lightmap mode + dimension 0). Shared by
 * the capture path and the interactive window loop. */
void gm_frame_lightmap_fill(const McSinTable *st, long long world_time,
                            CrRgba lut[256]);

#endif
