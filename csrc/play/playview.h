/* The play client's view as a library: play.c built with -DPLAY_VIEW
 * (csrc/runtime/pipeline.mk, out/runtime/playview.so) draws the frame play
 * draws, the observation, for a pool environment's session instead of its
 * own. The view's state is play's (its statics and the renderer files'), so
 * one loaded copy is one view: csrc/runtime/view.c loads a private copy of
 * the library per environment and reloads it at each reset.
 *
 * The copy exports one symbol, pv_api. Every call runs on the thread that
 * has the environment current (nw_env), its image heap on, as the pool's
 * step does; one environment's calls never overlap. */
#ifndef NETHERITE_PLAYVIEW_H
#define NETHERITE_PLAYVIEW_H

#include <stddef.h>
#include <stdint.h>

struct session;
struct act;
struct raster_obs;
struct jval;

struct pv_config {
    const char *assets;       /* the scene play draws with (play's --assets; NULL: its default) */
    int w, h;                 /* the frame (play's --size) */
    int rd;                   /* the renderer's distance (play's --render-distance; 0: the tape's) */
    int hide_gui;             /* F1 held (play's --hide-gui) */
    /* a process-wide store of parsed read-only tables (raster.h
     * raster_hud_shared_json): PATH parsed once for every copy */
    const struct jval *(*shared_json)(const char *path);
    /* where the sections are meshed (raster.h raster_live_device_mesh): 0
     * here, 1 the device, 2 both (the device's checked against these) */
    int device_mesh;
    /* every section pass the mesh key spares meshed anyway and compared
     * (raster.h raster_live_mesh_check; the gate) */
    int mesh_check;
    /* the renderer's precision (engine/raster_prec.h: 0 exact, else the mask
     * of the stages drawn fast; play's --render-prec) */
    int render_prec;
    float gamma;              /* options.txt's gamma (play's --options): the frames' light (config.yaml client.gamma) */
};

/* the last frame's section meshing (raster.h raster_live_stats): passes
 * meshed (or sent to the device), passes that kept their mesh by key,
 * passes given the empty mesh (no cell drawn in them), sections drawn;
 * with mesh_check, the kept and empty meshes compared and those that
 * differed */
/* tex_checked, tex_bad: raster_rec_verify_stats' (since the view opened);
 * band_checked, band_bad: raster_live_key_stats' */
struct pv_frame_stats { int meshed, reused, empty, draws, checked, check_bad; uint64_t tex_checked, tex_bad, band_checked, band_bad; double feed_ms; };

struct pv_api {
    int version;
    /* the view of SS from its current state, as play's start sets it up (the
     * session's hooks and the client player's renderer hook are the view's
     * from here); 0 with ERR on failure */
    int (*open)(struct session *ss, const struct pv_config *c, char *err, size_t n);
    /* around each session_tick of the environment: play_tick's halves, then
     * (tick_end) the render world's follow-up of the tick */
    void (*tick_begin)(int64_t t, const struct act *a);
    void (*tick_end)(int64_t t, const struct act *a);
    /* the frame after the last tick at partial tick 1.0, as play's shot
     * (judge_shot_draw) draws it before the HUD: its terrain lists and
     * recorded passes into O (valid until the next frame; raster_obs.h),
     * with RECORD_ONLY nothing rasterized, else the C frame into RGB
     * (w * h * 3). 0 on success; -1 for a frame raster_live_obs refuses. */
    int (*frame)(struct raster_obs *o, int record_only, unsigned char *rgb);
    /* the C renderer over O into RGB (raster_obs_render, this copy's) */
    void (*render)(const struct raster_obs *o, unsigned char *rgb);
    void (*frame_stats)(struct pv_frame_stats *st);
    /* the device lost the view's meshes (its renderer was made again):
     * every section is meshed again, from a new feed epoch */
    void (*mesh_resync)(void);
};

#define PV_API_VERSION 5

#endif
