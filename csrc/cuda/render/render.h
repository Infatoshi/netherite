/* The device renderer: the terrain passes of the C renderer's live frame (the
 * sky and the clouds, the opaque sections, the translucent sections over
 * them; fog and the lightmap), drawn for many environments in one pass
 * sequence into one device buffer, bit for bit the bytes raster.c draws for
 * each (raster_obs.h is the frame as data; raster_obs_render the C side).
 *
 * The pipeline per render, over every environment at once:
 *   units    one thread per sky slot or section quad: its triangles built
 *            (make_vertex, sky_vertex and the sky passes' generators),
 *            clipped (clip_triangle) and culled (live_emit), counted; a scan
 *            gives each its place in draw order, and a second pass writes
 *            them (the C engine's operation order, no FMA contraction,
 *            correctly rounded division and square root, glibc's expf);
 *   bins     each triangle's 16x16 screen tiles as (environment, tile) keys
 *            in triangle order, stably sorted, so every tile's list keeps
 *            the draw order;
 *   raster   one block per tile, one thread per pixel, walking its tile's
 *            triangles in order through triangle()'s per-pixel test, depth,
 *            texture, light, fog and blend; the pixel's bytes go to
 *            out[env][y][x][3].
 * The arena grows to hold a render's triangles and pairs while the device
 * has the memory; past that a render runs its environments in groups. Meshes are uploaded once per section version and kept on the
 * device; the atlas per environment is patched by the 16x16 tiles that
 * changed. Linux and CUDA only: never part of native's own build. */
#ifndef NETHERITE_CUDA_RENDER_H
#define NETHERITE_CUDA_RENDER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct raster_obs;
struct render;

struct render_stats {
    double upload_ms, units_ms, bin_ms, raster_ms, total_ms;  /* device time of the last render */
    size_t upload_bytes, triangles, pairs;
    size_t mesh_bytes, atlas_bytes, tex_bytes;    /* what the upload held: fresh meshes, atlas tiles, new textures */
    size_t quad_bytes;                             /* the recorded entity quads */
    size_t fresh_meshes, mesh_resets;              /* sections uploaded, the mesh arena started over */
    size_t trips, trip_overs;                      /* drawn in one trip (render_one_trip); begun so and run again */
    size_t units, sky_units, clip_units;          /* all, the sky's drawn slots, the quads the clipper took */
    int groups;
    /* the device mesher (cuda/meshing): section passes meshed, the feeds'
     * bytes, meshes compared with the host's and those that differed */
    size_t dmesh_requests, dmesh_upload_bytes, dmesh_checked, dmesh_diffs;
    /* device time of its count and write passes (on the mesher's own
     * stream: the write pass's once the next work has begun) */
    double dmesh_count_ms, dmesh_write_ms;
    /* the feeds' upload by kind (meshing/meshing.h meshing_stats): band images, chunk
     * records, flower pot and libm lists, requests, copy lists and launches */
    size_t dmesh_band_bytes, dmesh_chunk_bytes, dmesh_list_bytes, dmesh_req_bytes, dmesh_ctl_bytes;
    size_t dmesh_band_run_bytes;                   /* of the band images, sent as changed runs */
};

/* MAX_ENVS environments of frames W x H; ARENA_MB is the triangle and bin
 * buffers' first size (0: 512), grown by a render that needs more while
 * the device has 1 GB to spare. NULL when the device cannot hold it. */
struct render *render_new(int max_envs, int w, int h, int arena_mb);
void render_free(struct render *r);

/* Environment ENV's next frame; O must stay valid until render_render
 * returns. 0 on success, -1 for a frame this renderer cannot draw (another
 * size, other sky sheets than the first frame's). */
int render_set(struct render *r, int env, const struct raster_obs *o);

/* Draws environments 0..N-1 (each set since the last render); returns the
 * device buffer uint8 [N][h][w][3] (valid until the next render). */
const unsigned char *render_render(struct render *r, int n);

/* Draws the frames set for SLOTS[0..N) (each set since the last render),
 * frame i into OUTS[i] (a device address of h * w * 3 bytes, anywhere),
 * on this renderer's stream; returns when they are written. A slot is an
 * environment's resident state here (its meshes and atlas). 0 on success. */
int render_render_list(struct render *r, int n, const int *slots, unsigned char *const *outs);

/* The device mesher's work for the frames set on SLOTS[0..N) ahead of their
 * render, on the mesher's own stream: _start applies their feeds and
 * launches the count of their section passes; _finish waits for the counts
 * and launches the write of their meshes (neither waits for the device
 * otherwise). Between the two the caller may render other slots' frames
 * (the device counts while it draws); a render of these frames then draws
 * with the meshes written (its stream waits for them), and a frame whose
 * meshes were lost fails there (render_failed). One work at a time: a
 * _start or a render that must mesh finishes the work begun. */
int render_mesh_start(struct render *r, int n, const int *slots);
int render_mesh_finish(struct render *r);

/* Frame I of the last render (its place in the call's list) could not be
 * drawn: a frame whose device meshes the renderer lost or whose mesh feed
 * did not follow its slot's (raster_obs mesh); its view was told to mesh
 * everything again (the feed's resync flag). */
int render_failed(const struct render *r, int i);

/* One environment's frame to the host. */
int render_download(struct render *r, int env, unsigned char *rgb);
/* Its depth buffer after the last pass (w * h floats), as the C
 * renderer's is left. */
int render_download_depth(struct render *r, int env, float *depth);

/* ON: a render waits for the device once (the steps past the count take
 * their sizes on the device, every environment in one group, the sort run
 * to a bound; a render past the arena runs again the usual way). Each wait
 * is a turn of a shared device's time slices. Off by default. */
void render_one_trip(struct render *r, int on);
/* ON: the device mesher meshes in its table mode (cuda/meshing table.h:
 * the same meshes). Off by default. */
void render_mesh_tab(struct render *r, int on);
/* Each render copies its frames to RGB and its depth buffers to DEPTH
 * (pinned host memory, every frame's in order) before it returns (NULL:
 * none; cuda/render/framedev.cu). */
void render_frames_to(struct render *r, unsigned char *rgb, float *depth);
/* No device mesher (cuda/render/framedev.cu's plugin, which links
 * cuda/render/nomesher.c in its place): a frame with a mesh feed fails. */
void render_no_mesher(struct render *r);

/* Forget every uploaded mesh (the next render uploads them again). */
void render_reset_meshes(struct render *r);
/* ENV now holds another world: its meshes (named by section key and
 * version, which only one renderer's frames keep apart) and its atlas are
 * sent again at its next render. */
void render_forget(struct render *r, int env);

const struct render_stats *render_stats(const struct render *r);
/* the device memory this renderer holds (bytes): its arenas, meshes, atlases, textures, frames and depth buffers */
size_t render_device_bytes(const struct render *r);

/* The device, for a caller that keeps no CUDA header (csrc/runtime):
 * select it on this thread, device memory, a copy back, its free memory.
 * 0 (or non-NULL) on success. */
int render_device(int dev);
void *render_dev_alloc(size_t bytes);
void render_dev_free(void *p);
int render_dev_download(void *host, const void *dev, size_t bytes);
int render_dev_mem(size_t *free_bytes, size_t *total_bytes);
/* A named range on the calling thread for a profiler's timeline (NVTX):
 * PUSH 1 opens NAME, PUSH 0 closes the innermost (NAME unused). */
void render_range(const char *name, int push);

#ifdef __cplusplus
}
#endif

#endif
