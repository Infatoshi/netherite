#ifndef MAGMA_GAME_WINDOW_COMPOSE_H
#define MAGMA_GAME_WINDOW_COMPOSE_H

#include "game/config.h"
#include "game/game.h"
#include "game/particles_live.h"
#include "game/runtime.h"

typedef struct GmWindowCompose GmWindowCompose;

typedef void (*GmWindowComposeStampFn)(int slot);

typedef struct {
    const GmPlayerView *view;
    const GmPlayerView *camera_view;
    float partial_ticks;
    int interactive;
    int screen_open;
    int mouse_x;
    int mouse_y;
    GmWindowComposeStampFn stamp;
} GmWindowComposeFrame;

typedef struct {
    int ntris;
    int mesh_kept;
    int mesh_culled;
    int mesh_nverts[4];
} GmWindowComposeStats;

GmWindowCompose *gm_window_compose_open(const GmConfig *cfg,
                                         char *err, int err_cap);
void gm_window_compose_bind(GmWindowCompose *c, GmRuntime *runtime,
                            GmParticlesLive *particles);
void gm_window_compose_advance(GmWindowCompose *c, GmPlayerView *view,
                               const GmAction *action, int nticks);
int gm_window_compose_draw(GmWindowCompose *c,
                           const GmWindowComposeFrame *frame,
                           GmWindowComposeStats *stats,
                           char *err, int err_cap);
int gm_window_compose_emit_frame(GmWindowCompose *c, int tick,
                                 char *err, int err_cap);
CrFramebuffer *gm_window_compose_framebuffer(GmWindowCompose *c);
void gm_window_compose_close(GmWindowCompose *c);

#endif
