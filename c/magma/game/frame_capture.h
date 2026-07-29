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

#endif
