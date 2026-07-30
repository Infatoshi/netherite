/* present/present.c - SDL2 window, blit, and input for magma.
 *
 * PRESENT module. Owns: cr_window_open, cr_window_present, cr_window_poll,
 * cr_window_close (declared in core/types.h). SDL2 is used ONLY to open a
 * window, upload a finished RGBA buffer to a streaming texture and present it
 * (a memcpy + present, NOT rendering), and to read keyboard/mouse. No OpenGL.
 *
 * Pixel format
 * ------------
 * CrRgba stores bytes in memory order R, G, B, A (see core/types.h). We create
 * the SDL streaming texture with SDL_PIXELFORMAT_ABGR8888. That enum is a packed
 * 32-bit value 0xAABBGGRR; on a little-endian host the bytes land in memory as
 * R, G, B, A - exactly the CrRgba layout - so the upload is a straight memcpy
 * with no per-pixel swizzle. (anvil is x86-64, little-endian.)
 *
 * Headless / CI
 * -------------
 * If DISPLAY is unset and SDL video init fails, we fall back to the "dummy"
 * video driver (set via SDL_VIDEODRIVER before SDL_Init) so the app loop still
 * runs in CI; present then becomes an effective no-op. cr_window_open never
 * crashes headless - it returns a usable (if non-visible) window handle.
 */
#include "core/types.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CrWindow {
    SDL_Window   *win;
    SDL_Renderer *ren;
    SDL_Texture  *tex;     /* streaming ABGR8888, sized w x h */
    int           w, h;
    int           dummy;   /* 1 if running under the dummy video driver */
    int           captured;/* relative-mouse capture state */
    int           capture_off; /* 1 = a GUI screen owns the cursor: no auto-recapture */
    int           quit;    /* sticky quit request (window close / ESC) */
};

/* Initialize the SDL video subsystem, falling back to the dummy driver when
 * there is no display. Returns 0 on success, and sets *is_dummy. */
static int present_video_init(int *is_dummy)
{
    *is_dummy = 0;

    /* If the user already forced a driver, honour it. */
    const char *forced = SDL_getenv("SDL_VIDEODRIVER");
    if (forced && forced[0]) {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) == 0) {
            *is_dummy = (strcmp(forced, "dummy") == 0);
            return 0;
        }
    }

    /* No display at all: go straight to dummy so we never crash headless.
     * DISPLAY/WAYLAND_DISPLAY are X11/Wayland concepts - on macOS the cocoa
     * driver needs no env var, so this heuristic must not run there. */
#ifndef __APPLE__
    if (!getenv("DISPLAY") && !getenv("WAYLAND_DISPLAY")) {
        fprintf(stderr, "cr_window: no DISPLAY/WAYLAND_DISPLAY; using dummy video driver\n");
        SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) == 0) {
            *is_dummy = 1;
            return 0;
        }
    }
#endif

    /* Try the real driver first. */
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) == 0)
        return 0;

    /* Real init failed - fall back to dummy so CI can still loop. */
    fprintf(stderr, "cr_window: SDL video init failed (%s); using dummy driver\n",
            SDL_GetError());
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) == 0) {
        *is_dummy = 1;
        return 0;
    }
    return -1;
}

CrWindow *cr_window_open(int w, int h, const char *title)
{
    if (w <= 0 || h <= 0)
        return NULL;

    CrWindow *cw = (CrWindow *)calloc(1, sizeof(*cw));
    if (!cw)
        return NULL;
    cw->w = w;
    cw->h = h;

    if (present_video_init(&cw->dummy) != 0) {
        fprintf(stderr, "cr_window_open: no usable SDL video driver: %s\n",
                SDL_GetError());
        free(cw);
        return NULL;
    }

    cw->win = SDL_CreateWindow(title ? title : "magma",
                               SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               w, h, SDL_WINDOW_SHOWN);
    if (!cw->win) {
        fprintf(stderr, "cr_window_open: SDL_CreateWindow failed: %s\n",
                SDL_GetError());
        cr_window_close(cw);
        return NULL;
    }

    cw->ren = SDL_CreateRenderer(cw->win, -1, SDL_RENDERER_ACCELERATED);
    if (!cw->ren)
        cw->ren = SDL_CreateRenderer(cw->win, -1, SDL_RENDERER_SOFTWARE);
    if (!cw->ren) {
        fprintf(stderr, "cr_window_open: SDL_CreateRenderer failed: %s\n",
                SDL_GetError());
        cr_window_close(cw);
        return NULL;
    }

    cw->tex = SDL_CreateTexture(cw->ren, SDL_PIXELFORMAT_ABGR8888,
                                SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!cw->tex) {
        fprintf(stderr, "cr_window_open: SDL_CreateTexture failed: %s\n",
                SDL_GetError());
        cr_window_close(cw);
        return NULL;
    }

    return cw;
}

int cr_window_present(CrWindow *win, const CrFramebuffer *fb)
{
    if (!win || !fb || !fb->color)
        return -1;

    /* Only upload the region both buffers share; guards against a resized fb. */
    int uw = fb->w < win->w ? fb->w : win->w;
    int uh = fb->h < win->h ? fb->h : win->h;
    if (uw <= 0 || uh <= 0)
        return -1;

    void *pixels = NULL;
    int   pitch  = 0;
    if (SDL_LockTexture(win->tex, NULL, &pixels, &pitch) != 0) {
        fprintf(stderr, "cr_window_present: SDL_LockTexture failed: %s\n",
                SDL_GetError());
        return -1;
    }

    const size_t src_stride = (size_t)fb->w * sizeof(CrRgba);
    const size_t row_bytes  = (size_t)uw * sizeof(CrRgba);
    const u8 *src = (const u8 *)fb->color;
    u8       *dst = (u8 *)pixels;
    for (int y = 0; y < uh; ++y)
        memcpy(dst + (size_t)y * pitch, src + (size_t)y * src_stride, row_bytes);

    SDL_UnlockTexture(win->tex);

    SDL_RenderClear(win->ren);
    SDL_RenderCopy(win->ren, win->tex, NULL, NULL);
    SDL_RenderPresent(win->ren);
    return 0;
}

/* Toggle SDL relative-mouse (FPS) capture and remember the state. */
static void present_set_capture(CrWindow *win, int on)
{
    on = on ? 1 : 0;
    if (on == win->captured)
        return;
    if (SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE) == 0)
        win->captured = on;
}

void cr_window_capture_enable(CrWindow *win, int on)
{
    if (!win)
        return;
    win->capture_off = on ? 0 : 1;
    if (!on)
        present_set_capture(win, 0);
}

void cr_window_poll(CrWindow *win, CrInput *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (!win) {
        out->quit = 1;
        return;
    }

    /* Accumulate relative motion across all pending motion events this poll. */
    int dx = 0, dy = 0;
    /* Accumulate wheel delta and the last number key pressed this poll. */
    int wheel = 0;
    int key_num = 0;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            win->quit = 1;
            break;
        case SDL_KEYDOWN:
            if (e.key.keysym.sym == SDLK_ESCAPE) {
                /* ESC: with a GUI screen owning the cursor it is reported to the
                 * app (closes the screen); else it releases FPS capture if held,
                 * else requests quit. */
                if (win->capture_off)
                    out->key_esc = 1;
                else if (win->captured)
                    present_set_capture(win, 0);
                else
                    win->quit = 1;
            }
            /* Number keys 1-9 -> hotbar slot select (edge, on keydown). */
            if (e.key.keysym.sym >= SDLK_1 && e.key.keysym.sym <= SDLK_9)
                key_num = (int)(e.key.keysym.sym - SDLK_1) + 1;
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                if (!win->capture_off)
                    present_set_capture(win, 1);
                out->click_left = 1;
            }
            if (e.button.button == SDL_BUTTON_RIGHT)
                out->click_right = 1;
            break;
        case SDL_MOUSEMOTION:
            dx += e.motion.xrel;
            dy += e.motion.yrel;
            break;
        case SDL_MOUSEWHEEL: {
            int wy = e.wheel.y;
            if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
                wy = -wy;
            wheel += wy;
            break;
        }
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_CLOSE)
                win->quit = 1;
            break;
        default:
            break;
        }
    }

    const u8 *ks = SDL_GetKeyboardState(NULL);
    if (ks) {
        out->key_w     = ks[SDL_SCANCODE_W];
        out->key_a     = ks[SDL_SCANCODE_A];
        out->key_s     = ks[SDL_SCANCODE_S];
        out->key_d     = ks[SDL_SCANCODE_D];
        out->key_space = ks[SDL_SCANCODE_SPACE];
        out->key_shift = ks[SDL_SCANCODE_LSHIFT] || ks[SDL_SCANCODE_RSHIFT];
        out->key_ctrl  = ks[SDL_SCANCODE_LCTRL]  || ks[SDL_SCANCODE_RCTRL];
        out->key_e     = ks[SDL_SCANCODE_E];
        out->key_q     = ks[SDL_SCANCODE_Q];
        out->key_tab   = ks[SDL_SCANCODE_TAB];
        out->key_up    = ks[SDL_SCANCODE_UP];
        out->key_down  = ks[SDL_SCANCODE_DOWN];
        out->key_left  = ks[SDL_SCANCODE_LEFT];
        out->key_right = ks[SDL_SCANCODE_RIGHT];
    }

    /* Current mouse button held-state (bitmask) + absolute cursor position,
     * independent of the event queue. */
    int mx = 0, my = 0;
    Uint32 mb = SDL_GetMouseState(&mx, &my);
    out->mouse_left  = (mb & SDL_BUTTON(SDL_BUTTON_LEFT))  ? 1 : 0;
    out->mouse_right = (mb & SDL_BUTTON(SDL_BUTTON_RIGHT)) ? 1 : 0;
    out->mouse_x = mx;
    out->mouse_y = my;

    out->key_num = key_num;
    out->wheel   = wheel;

    /* Only report mouse deltas while captured, matching FPS-look expectations. */
    out->mouse_dx       = win->captured ? dx : 0;
    out->mouse_dy       = win->captured ? dy : 0;
    out->mouse_captured = win->captured;
    out->quit           = win->quit;
}

void cr_window_close(CrWindow *win)
{
    if (!win)
        return;
    if (win->captured)
        SDL_SetRelativeMouseMode(SDL_FALSE);
    if (win->tex)
        SDL_DestroyTexture(win->tex);
    if (win->ren)
        SDL_DestroyRenderer(win->ren);
    if (win->win)
        SDL_DestroyWindow(win->win);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    free(win);
}
