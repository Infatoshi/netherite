/* One live frame's terrain passes as data: what raster_live_frame drew (the
 * sky, the opaque sections) and raster_live_translucent draws over it (the
 * translucent sections), with every input those passes read. The device
 * renderer (csrc/cuda/render) draws the same frame from it, bit for bit;
 * raster_obs_write and raster_obs_read keep one on disk, beside the C
 * renderer's own frame of it, for the device's gate.
 *
 * The scalars are the numbers the C passes use, not their sources: the
 * celestial rotation's cosf and sinf, the sunrise fan's sine table, the
 * clouds' origin are computed here once by the same C expressions the
 * passes run (raster.c, raster_sky2.c), so a device port never calls a
 * libm function but expf (csrc/cuda/render: glibc's, reproduced). */
#ifndef NETHERITE_RASTER_OBS_H
#define NETHERITE_RASTER_OBS_H

#include <stddef.h>
#include <stdint.h>

#include "raster_entity_quad.h"

struct raster_live;
struct meshfeed_out;
struct rb_mesher;

enum { RASTER_OBS_ANIM = 16 };

/* The RL observation (Elliot, 2026-09-28, SPEC.md): 128x128 (square,
 * vertical fov 70), vanilla's F1 (no HUD, hand or outline in the image; the
 * HUD goes as numbers), render distance 4. play --obs draws it; the
 * device renderer's gate runs at it. */
enum { RASTER_OBS_W = 128, RASTER_OBS_H = 128, RASTER_OBS_HIDE_GUI = 1, RASTER_OBS_RD = 4 };

/* One section's quads (8 int32 per vertex, 4 vertices per quad, the
 * Tessellator's raw layout) as a draw: the translucent ones carry the order
 * their quads are drawn in (vertex offsets, back to front). key names the
 * section in the renderer's square (for a cache of uploaded meshes) and
 * version its mesh, unique in the process (a new mesh, a new version). */
struct raster_obs_draw {
    int cx, s, cz, flags, count;       /* count: vertices */
    uint32_t key, version;
    const int32_t *raw;
    const int32_t *order;              /* count / 4 vertex offsets, or NULL */
    /* 1: the mesh is the device's (raster_live_device_mesh), the one a
     * request of this frame's feed or an earlier one made at this version;
     * count is -1 until the host knows it, raw and order may be NULL (the
     * device orders a translucent section's quads itself) */
    int device;
};

/* A section pass the frame's feed asked the device to mesh, as the host
 * meshed it (raster_live_device_mesh 2), for the device's to be checked
 * against: vertices, the Tessellator's flags, 8 int32 per vertex. */
struct raster_obs_hostmesh { int count, flags; const int32_t *raw; };

struct raster_obs_sky {
    float sky[3], sky_bottom[3], fog[3], cloud_color[3];
    float sky_far, sky_floor, cloud_height, star;
    float sunrise[4];                  /* rgba; alpha 0: no fan */
    float rise_dir, rise_center[4];    /* the fan's side and its centre colour */
    float rise_sc[17][2];              /* mh_sin, mh_cos of i * 2 pi / 16 */
    float cel_cs, cel_sn;              /* the sun, moon and stars' rotation */
    float moon_uv[4];                  /* u0 v0 u1 v1 of the moon phase */
    float cloud_ix, cloud_iz, cloud_fx, cloud_fz;
    int dimension, clouds, stars;      /* stars: the star pass runs */
    /* what the C passes take them from (raster_obs_render) */
    float celestial_angle;
    int moon_phase, cloud_tick;
};

/* The world passes after the opaque terrain, as data (raster_rec): the
 * commands in draw order, each a run of recorded primitives or a marker. */
enum {
    RASTER_CMD_QUAD,          /* raster_entity_quad's quads [first, first + count) */
    RASTER_CMD_CRACK,         /* raster_overlay's crack triangles */
    RASTER_CMD_WATER,         /* raster_live_translucent: the translucent sections here */
    RASTER_CMD_CLEAR_DEPTH,   /* raster_live_overlays' depth clear */
    RASTER_CMD_PORTAL,        /* raster_tileent's end portal triangles */
    RASTER_CMD_OTHER,         /* pixels a pass wrote that no command carries (a frame the device cannot draw) */
    RASTER_CMD_LINE,          /* raster_overlay's block outline lines */
    RASTER_CMD_DEPTH_SNAP,    /* the depth buffer here is the one a renderer that returns depth returns
                                 (the frame judge's drops check: the world's, before the hand's) */
};
struct raster_obs_cmd { int kind, first, count; };

/* the entity_raster_target fields raster_entity_quad's triangles read */
struct raster_obs_estate {
    float fog[3], fogs, foge, fogd;
    int fogm, lm;                      /* lm: the index of its lightmap */
    int overlay, overlay_lightmap, overlay_has_rgb, blend, unlit, clamp_texture, linear_depth;
    int cull_front, depth_equal, vertex_color, two_sided, no_light, no_alpha, alpha_cut;
    int vertex_alpha, depth_lequal;
    float overlay_brightness, overlay_alpha, overlay_rgb[3], material[3], alpha, alpha_ref;
};
struct raster_obs_equad { int state, tex; struct entity_clip_vertex v[4]; };
/* A model box (raster_mobs.c emit_box_faces: ModelBox.render's six faces)
 * as the recorder keeps it: its quads are equad[first .. first + 6), each
 * of state and tex, whose vertices raster_ebox_quads makes from this (the
 * recorder leaves them unwritten: a consumer of equad makes them, the
 * device renderer on the device). xf is the frame's ebox_xf entry of its
 * modelview and projection. */
struct raster_obs_ebox {
    int first, state, tex, xf;
    float v[8][3];             /* the corners, transformed (ModelBox's vertex order) */
    float base[3];             /* (float)(entity - camera), added to them unless outer */
    int outer;
    float light[2];            /* the brightness's two halves */
    int u, vv, dx, dy, dz, mirror, flip_normal;
    int tex_w, tex_h;          /* the model texture's size (the texture coordinates' divisor) */
    float tex_u, tex_v;
    int tex_matrix;
    float tm[6];
};
/* its six quads: the face order and the vertices emit_box_faces draws */
void raster_ebox_quads(const struct raster_obs_ebox *b, const float mv[16], const float proj[16],
                       struct entity_clip_vertex q[6][4]);
/* the lighting of a face of unit normal (nx, ny, nz) (RenderHelper's two lights) */
float raster_ebox_diffuse(float nx, float ny, float nz);
/* An item drawn flat (raster_glx.c glx_item_in_2d: ItemRenderer.
 * renderItemIn2D) as the recorder keeps it: its raster_eitem_quads(it)
 * quads are equad[first ..], each of state and tex, made by
 * raster_eitem_quad; xf the frame's ebox_xf entry of the item's modelview
 * and the projection. */
struct raster_obs_eitem {
    int first, state, tex, xf;
    float p1, p2, p3, p4, thick;
    int w, h;
    float diffuse[6];          /* the lit factor of the front, the back, the -x, +x, +y and -y strips */
    float light[2], alpha;     /* the lightmap coordinates, glColor's alpha */
    int tex_matrix;
    float tm[6];               /* u' = tm[0] u + tm[1] v + tm[2], v' = tm[3] u + tm[4] v + tm[5] */
};
int raster_eitem_quads(const struct raster_obs_eitem *it);
/* quad K of IT (glx_item_in_2d's order: the front, the back, the -x, +x,
 * +y and -y strips) */
void raster_eitem_quad(const struct raster_obs_eitem *it, const float mv[16], const float proj[16], int k,
                       struct entity_clip_vertex q[4]);
/* key: raster_obs_tex_key of the bytes (a device renderer's cache key) */
struct raster_obs_tex { int w, h; const unsigned char *rgba; uint64_t key; };
uint64_t raster_obs_tex_key(const unsigned char *rgba, int w, int h);
/* raster_overlay's crack triangle: three projected points (x y z w u v f)
 * and the pass's fog (fog_on 0: renderWorld's fog array was NULL) */
struct raster_obs_crack {
    float p[3][7];
    int fog_on, fogm;
    float fog[3], fogs, foge, fogd;
};

/* raster_tileent's end portal triangle: its three screen vertices as
 * portal_triangle takes them (clip x y z w, x y sx sy w depth, s t q, the
 * eye distance) and its layer's state */
struct raster_obs_portal {
    float v[3][14];
    float color[4], light[3], fog[3], fogs, foge, fogd;
    int fogm, additive, cull, tex;
};

/* raster_overlay's outline line: its two projected ends (x y z w f) and
 * the pass's fog, as the crack's */
struct raster_obs_line {
    float a[5], b[5];
    int fog_on, fogm;
    float fog[3], fogs, foge, fogd;
};

struct raster_obs {
    int w, h;
    int prec;                          /* raster_prec.h: the stages drawn fast (0: exact) */
    float proj[16], mv[16];
    double cam[3];
    float fog[3], fogs, foge, fogd;
    int fogm;
    uint32_t lm[256];
    struct raster_obs_sky sky;
    int nopaque, nwater;
    const struct raster_obs_draw *opaque, *water;
    /* the block atlas (level 0, animated to this frame) and the sky's sheets */
    const unsigned char *atlas;
    int atlas_w, atlas_h;
    const unsigned char *sun, *moon, *clouds, *end_sky;   /* 32x32, 128x64, 256x256 RGBA; 128x128 RGB */
    const float *stars;                /* nstars quads of 4 xyz vertices */
    int nstars;
    /* the atlas rectangles (x y w h) the animation patches between frames:
     * the only texels a kept copy of the atlas needs again */
    int nanim;
    int32_t anim[RASTER_OBS_ANIM][4];
    /* the passes after the opaque terrain (raster_rec_attach; none: the
     * translucent sections right after the opaque ones) */
    int ncmd, nequad, nestate, ntex, nlm, ncrack, nportal, nline;
    /* 1: every equad's state and tex index in range, as the recorder makes
     * them (raster_rec_attach): a consumer need not check them again (the
     * device renderer's render_set read each quad's line for it) */
    int equads_in_range;
    const struct raster_obs_cmd *cmd;
    const struct raster_obs_equad *equad;
    const struct raster_obs_estate *estate;
    const struct raster_obs_tex *tex;
    const uint32_t (*lm_table)[256];
    const struct raster_obs_crack *crack;
    const struct raster_obs_portal *portal;
    const struct raster_obs_line *line;
    /* the model boxes and flat items among the quads (each in first's
     * order) and their matrices (mv, then proj); a file holds none
     * (raster_obs_write writes their quads) */
    int nebox, neitem, nebox_xf;
    const struct raster_obs_ebox *ebox;
    const struct raster_obs_eitem *eitem;
    const float (*ebox_xf)[32];
    /* the device mesher's work (raster_live_device_mesh; NULL: none): the
     * feed, the host's meshes of its requests (NULL unless both mesh), the
     * mesher the host meshes with (its tables are the device's) and the
     * scene its tables came from */
    const struct meshfeed_out *mesh;
    const struct raster_obs_hostmesh *mesh_host;
    const struct rb_mesher *mesher;
    const char *assets;
    /* raster_obs_read's buffers (the file's copy of all of the above) */
    void *owned;
};

/* The frame raster_live_frame last drew (its lists, the translucent orders
 * raster_live_translucent draws) into O; O points into L until L's next
 * frame. 0 on success; -1 when the texture is one the device does not draw
 * (mipmapped). */
int raster_live_obs(struct raster_live *L, struct raster_obs *o);

/* O and the C renderer's RGB frame of it (w * h * 3; NULL for none) to
 * PATH, zlib-compressed; 0 on success. DEPTH (w * h floats; NULL for none)
 * is the C renderer's depth buffer after the frame's last pass. */
int raster_obs_write(const char *path, const struct raster_obs *o, const unsigned char *rgb);
int raster_obs_write_depth(const char *path, const struct raster_obs *o, const unsigned char *rgb, const float *depth);
/* The file back: O owns its buffers (raster_obs_free); *RGB, when RGB is
 * not NULL, is a malloc'd copy of the frame (NULL if the file has none),
 * and so is *DEPTH of its depth buffer. */
int raster_obs_read(const char *path, struct raster_obs *o, unsigned char **rgb);
int raster_obs_read_depth(const char *path, struct raster_obs *o, unsigned char **rgb, float **depth);
void raster_obs_free(struct raster_obs *o);

/* The recorder: between raster_rec_begin and raster_rec_end every
 * raster_entity_quad, crack triangle, translucent pass and depth clear of
 * this process is also appended to R (the passes still draw), in order.
 * raster_rec_attach points O's command fields at R's arrays (valid until
 * R's next begin). The hooks are the passes' own (raster_entities.c,
 * raster_overlay.c, raster.c); a pass that writes pixels another way
 * records RASTER_CMD_OTHER. Not thread safe: one recorder at a time, and
 * the recorded passes run on the recording thread. */
struct raster_rec;
struct raster_rec *raster_rec_new(void);
void raster_rec_free(struct raster_rec *r);
void raster_rec_begin(struct raster_rec *r);
void raster_rec_end(void);
void raster_rec_attach(struct raster_rec *r, struct raster_obs *o);
/* the hooks */
/* a quad of raster_entities.c's rasterizer, ST its state (ST's lm is set
 * here from LM, NULL when unlit) */
void raster_rec_equad(const struct raster_obs_estate *st, const uint32_t *lm, const unsigned char *tex,
                      int tex_w, int tex_h, const struct entity_clip_vertex v[4]);
void raster_rec_crack(const float p[3][7], const float *fog, float fogs, float foge, float fogd, int fogm);
/* a portal triangle; P's tex is set here from the texture */
void raster_rec_portal(const struct raster_obs_portal *p, const unsigned char *tex, int tex_w, int tex_h);
void raster_rec_line(const struct raster_obs_line *l);
/* a model box's six quads as one record (B's first, state, tex and xf set
 * here; ST and LM as raster_rec_equad's, TEX what its quads sample, MV and
 * PROJ what raster_ebox_quads projects with) */
void raster_rec_ebox(const struct raster_obs_estate *st, const uint32_t *lm, const unsigned char *tex, int tex_w,
                     int tex_h, const struct raster_obs_ebox *b, const float *mv, const float *proj);
/* a flat item's quads as one record (as raster_rec_ebox) */
void raster_rec_eitem(const struct raster_obs_estate *st, const uint32_t *lm, const unsigned char *tex, int tex_w,
                      int tex_h, const struct raster_obs_eitem *it, const float *mv, const float *proj);
/* the state a quad raster_entity_quad draws for T records (*LM its
 * lightmap, NULL when unlit), for raster_rec_ebox (raster_entities.c) */
void raster_entity_estate(const struct entity_raster_target *t, struct raster_obs_estate *st, const uint32_t **lm);
/* the recording held off (the quads a box record stands for, drawn by the C
 * passes) and back on */
struct raster_rec *raster_rec_hold(void);
void raster_rec_restore(struct raster_rec *r);
void raster_rec_marker(int kind);
int raster_rec_on(void);
/* Record only (csrc/runtime's frames, which the device draws): while on,
 * the passes build and record what they draw but rasterize nothing
 * (raster_live_frame's sky and sections, raster_live_translucent, every
 * recorded quad, crack and portal triangle); the frame's pixels and depth
 * are left as they were. raster_live_obs and the recorder's commands are
 * the same bytes either way. Process-wide, as the recorder is. */
void raster_rec_set_only(int on);
int raster_rec_only(void);
/* RGBA's bytes change (an animation's patch, a buffer about to be reused):
 * a later quad sampling it takes a new copy. The recorder copies each
 * texture when a quad first samples it. Every write to a texture a
 * recording may have sampled, outside a free and a new allocation, must be
 * named here or to raster_rec_texture_patched, in a recording or between
 * them. */
void raster_rec_texture_changed(const unsigned char *rgba);
/* The same for a write to RGBA (AW x AH texels) that stays in the W x H
 * texels at (X, Y): a copy kept from an earlier frame is compared there
 * alone. */
void raster_rec_texture_patched(const unsigned char *rgba, int aw, int ah, int x, int y, int w, int h);
/* COUNT(P, BYTES): the frees so far that could have put other BYTES at P
 * (image.h image_free_count_at): a texture's copy, in its frame or kept
 * from an earlier one, is compared with its source again only after such a
 * free or a write named above. NULL, the default: a small texture's bytes
 * at every reuse, a big one's once a frame. */
void raster_rec_free_count(uint64_t (*count)(const void *p, size_t bytes));
/* ON: every copy the rules above take back unchecked is compared with its
 * source anyway (pipe_gate --mesh-check, play --mesh-check);
 * raster_rec_verify_stats: the copies so compared since the start, and
 * those that differed (each one named on stderr). */
void raster_rec_verify(int on);
void raster_rec_verify_stats(uint64_t *checked, uint64_t *bad);

/* The C renderer over O on this thread: the passes raster_live_frame and
 * raster_live_translucent run, from O alone, into RGB (w * h * 3); DEPTH
 * is w * h floats of scratch. The same bytes the live frame drew. */
void raster_obs_render(const struct raster_obs *o, unsigned char *rgb, float *depth);

#endif
