#ifndef NETHERITE_RASTER_H
#define NETHERITE_RASTER_H

#include <stddef.h>
#include <stdint.h>

#include "gui_chat.h"

/* Render opaque and translucent chunk quads from a MeshProbe world dump and
 * the matching RenderStateProbe frame. The output is RGB PNG; uncovered pixels
 * show the clear colour or the world sky. */
int raster_render(const char *scene, const char *png, const char *coverage,
                  const char *sky_png);

/* An RGB buffer as a PNG file; 0 on success. */
int raster_png(const char *path, int w, int h, const unsigned char *rgb);

/* The 2D overlay pass (raster_hud.c) over a finished RGB frame: GuiIngame's
 * HUD and the achievement toast, from the scene's recorded HUD state. */
int raster_hud(unsigned char *rgb, int w, int h, const char *scene);
struct jval;
int raster_hud_draw(unsigned char *rgb, int w, int h, const char *scene, const struct jval *hud);

/* BossStatus.setBossStatus, as a boss's renderer calls it when it draws: the
 * health fraction and the formatted name; the status bar shows for the next
 * 100 frames the HUD draws. */
void raster_hud_boss_set(float health_scale, const char *name);

struct chat;
/* pop: a hotbar stack's animationsToGo less the partial tick (the item is
 * squashed while it is over 0) */
struct hud_live_item { int id, meta, count, tag; float pop; };
struct hud_live_state {
    int width, height, scale, fancy, hide, screen;
    int chat_open;          /* GuiNewChat.getChatOpen: a GuiChat (GuiSleepMP) is up */
    int chat_scroll, chat_scrolled;   /* its field_146250_j and field_146251_k */
    int update_counter, current_slot;
    int sleep_timer;        /* EntityPlayer.getSleepTimer: GuiIngame's sleep fade */
    long long clock_ms;
    float vignette, brightness;
    float health, previous_health, max_health, absorption, saturation, xp;
    int hurt_resistant, armor_value, food, previous_food, air, in_water;
    int hardcore, level, xp_cap, show_hud, survival, poison, wither, regen, hunger;
    /* the ridden living's hearts in place of the food (its health, max) */
    int mount;
    float mount_health, mount_max;
    int tooltip_enabled, highlight_ticks;
    /* the two full-screen overlays GuiIngame.renderGameOverlay draws */
    int pumpkin_overlay, third_person, confusion;
    float portal;
    struct hud_live_item highlighted;
    const char *highlight_name;
    struct hud_live_item inventory[9], armor[4];
    /* the GuiAchievement toast, the renderstate probe's json fields */
    int toast_on, toast_desc;
    long long toast_l;
    const char *toast_title, *toast_sub;
    struct hud_live_item toast_item;
    int toast_has_item;
    /* the open GuiScreen's probe subtree, passed through verbatim (null when
     * no screen is open: the emitter writes null) */
    const struct jval *gui_screen;
    /* GuiNewChat's messages, newest first (NULL: none), at the default chat
     * settings */
    const struct chat *chat;
};
char *raster_hud_live_json(const struct hud_live_state *state, const char *scene);
char *raster_hud_toast_json(const struct hud_live_state *state, const char *scene);
const char *raster_hud_item_name(const char *scene, int id, int meta);
const struct jval *raster_hud_item_entry(const char *scene, int id, int meta);
/* A process-wide store for the item tables above (read-only once parsed),
 * for a program that keeps a copy of the renderer's files per environment
 * (csrc/runtime's views, playview.h pv_config.shared_json): the copies
 * then share one parse. NULL (the default): each keeps its own. */
extern const struct jval *(*raster_hud_shared_json)(const char *path);
/* An item atlas sprite's minU, maxU, minV, maxV by icon name (the recorder's
 * gui.json dump); 0 when the scene has none. */
int raster_hud_item_sprite(const char *scene, const char *name, float uv[4]);
int raster_hud_live(unsigned char *rgb, int w, int h, const char *scene,
                    const struct hud_live_state *state);

struct container;

/* One active potion effect as InventoryEffectRenderer lists it. */
struct gui_effect { int id, amplifier, duration, max; };

/* GuiInventory.func_147046_a's player: what RenderPlayer reads besides the
 * rotations the preview sets itself. */
struct mob_render_input;
struct gui_preview {
    float limb, limb_amount, prev_limb_amount, swing, y_offset, brightness;
    float prev_body_yaw, prev_pitch;
    float mouse_x, mouse_y;    /* GuiInventory's stored mouse (the last drawScreen's) */
    int age, hurt, death, sneak, invisible, riding, held;
    int prev_body_own;         /* no previous body yaw: interpolate from the preview's own */
};

/* What a screen draws beyond its container: GuiMerchant's title (NULL or
 * empty: "Villager"), the player's effects in getActivePotionEffects' order
 * (InventoryEffectRenderer's column and left shift), the player preview, and
 * GuiWinGame (credits: its clock, scroll speed and the session name). */
enum { GUI_DRAG_SLOTS = 96, GUI_MARK_SHARE = 1, GUI_MARK_HIDE = 2, GUI_MARK_YELLOW = 4 };

struct gui_screen_extra {
    const char *merchant_name;
    int neffects;
    struct gui_effect effects[32];
    int has_preview;
    struct gui_preview preview;
    /* the preview through RenderPlayer whole: the client player's render
     * input and the scene its textures come from (NULL: the bare model) */
    const struct mob_render_input *preview_mob;
    const char *preview_assets;
    const uint32_t *preview_lm;       /* the frame's lightmap, for the fire */
    int credits, credits_time;
    float credits_speed, partial_tick;
    const char *user;
    long long clock_ms;        /* Minecraft.getSystemTime: the enchant glint's phase */
    /* GuiContainer's drag while it holds slots (func_146977_a, drawScreen):
     * per slot GUI_MARK_* bits, and (drag_on) the cursor's remainder */
    unsigned char slot_mark[GUI_DRAG_SLOTS];
    int drag_on, drag_remainder;
    /* GuiChat and GuiSleepMP (a GuiChat with the Leave Bed button): the
     * input strip and the text field (gui_chat.h) */
    int chat, sleep;
    struct gui_chat_draw chat_draw;
    /* GuiChat.drawScreen's hovering text (func_146283_a) at the mouse */
    int ntip, tip_x, tip_y;
    char tip[8][192];
};

/* GuiWinGame.initGui's line list for USER, with SCENE's font widths
 * (malloc'd lines, *n of them). */
char **raster_hud_credits_lines(const char *scene, const char *user, int *n);

/* Draw the current 1.7.10 GuiScreen after the live HUD. Mouse coordinates
 * are scaled GUI pixels. death and pause are mutually exclusive; extra may be
 * NULL. */
int raster_gui_live(unsigned char *rgb, int w, int h, const char *hud_scene,
                    const struct container *container, int inventory_open,
                    int death, int pause, int gui_scale, int mouse_x, int mouse_y,
                    int score, int death_ticks, int burn, int burn_total, int cook,
                    const struct gui_screen_extra *extra);

/* Live frames for the playable client: the same sky and chunk passes over an
 * rb_world the caller keeps current, with the frame's numbers from
 * renderstate_compute. The textures, table.bin and atlas.json come from ASSETS,
 * any render scene directory. Each section's quads are cached until
 * raster_live_stale marks the section; threads split the screen into bands. */
struct rs_out;
struct rb_world;
struct rb_mesher;
struct raster_live;

struct raster_live_in {
    const struct rs_out *rs;
    int dimension;
    int rd;             /* renderDistanceChunks: the square of sections drawn */
    int cloud_tick;     /* RenderGlobal.cloudTickCounter */
    int64_t wt;         /* the world time, for the moon phase */
    double view_y;      /* the render view entity's interpolated posY */
    int clouds;          /* GameSettings.shouldRenderClouds */
};

struct raster_live *raster_live_new(const char *assets, const struct rb_world *world,
                                    int w, int h, int threads);
/* The mesher over the live world, for rb_biome_temperature. */
struct rb_mesher *raster_live_mesher(struct raster_live *L);
/* the particle pass's layer 0 and layer 3 sheets (128x128 RGBA) */
const unsigned char *raster_live_particle_tex(struct raster_live *L);
const unsigned char *raster_live_explosion_tex(struct raster_live *L);
/* the layer 1 sheet: the terrain atlas and its dims */
const unsigned char *raster_live_block_atlas(struct raster_live *L, int *w, int *h);
/* The last frame's depth buffer (w * h, the opaque pass's window z). */
const float *raster_live_depth(const struct raster_live *L);
float *raster_live_depth_mut(struct raster_live *L);
void raster_live_stale(struct raster_live *L, int cx, int s, int cz);
/* While ON, raster_live_frame drops no section's mesh outside its square:
 * the frames a caller keeps as data (raster_live_obs) point into them
 * until it draws them. */
void raster_live_keep(struct raster_live *L, int on);
/* While ON, every stale section pass the key would spare is meshed anyway
 * and compared with the mesh the key kept (or the empty mesh), which it
 * keeps when equal: raster_live_stats' checked and check_bad (a line on
 * stderr names each section that differs). Not with raster_live_device_mesh 1,
 * where the host holds no meshes. */
void raster_live_mesh_check(struct raster_live *L, int on);
/* The rb_world the renderer was made with moved its window (a new origin or
 * margin, the records already copied): the cached section meshes follow their
 * chunks, the ones that left the window are dropped. The old geometry is
 * what the window was before the move. */
void raster_live_rebase(struct raster_live *L, int old_origin_cx, int old_origin_cz, int old_margin, int old_rows);
void raster_live_stale_all(struct raster_live *L);
/* The precision the frames draw at (raster_prec.h: the mask of fast stages,
 * 0 exact, the default), recorded in raster_live_obs. The entity
 * rasterizer's is the process's own (raster_entity_set_prec). */
void raster_live_set_prec(struct raster_live *L, int prec);
/* B went into a window record that did not hold it (raster_meshkey.h
 * meshkey_cache_taken) */
struct chunk_sec;
void raster_live_band_taken(struct raster_live *L, const struct chunk_sec *b);
/* the same for B taken in place of FROM with FROM's bytes (meshkey_cache_same) */
void raster_live_band_same(struct raster_live *L, const struct chunk_sec *from, const struct chunk_sec *b);
/* raster_live_mesh_check: the kept band hashes of the keys made again and
 * compared since the renderer was made, and those that differed */
void raster_live_key_stats(const struct raster_live *L, uint64_t *checked, uint64_t *bad);
/* Where the sections are meshed (raster_obs.h, engine/meshfeed.h): 0 here
 * (the default), 1 on the device (each frame's stale sections become
 * requests in the frame's mesh feed, with the world bytes the device
 * lacks; raster_live_obs exports them, the draws without meshes), 2 both
 * (the host's meshes too, which the frame carries beside the feed for the
 * device's to be checked against, and which the C renderer draws). */
void raster_live_device_mesh(struct raster_live *L, int mode);
/* the device lost what the feed sent: the next frame starts a new epoch
 * with every section a request (a device renderer made again) */
void raster_live_mesh_resync(struct raster_live *L);
/* Where the last frame's time went: meshing stale sections (one thread), the
 * draw lists (culling, the translucent sorts), recording the sky, stage one
 * (transform and clip, all threads) and stage two (rasterize, all threads). */
struct raster_live_stats {
    double mesh_ms, feed_ms, lists_ms, sky_ms, record_ms, raster_ms;
    int meshed, draws;
    /* the stale section passes that kept their mesh (their key had not
     * moved: raster_meshkey.h) and the ones given the empty mesh (no cell
     * of the section is drawn in the pass), neither meshed; with
     * raster_live_mesh_check, the kept and empty
     * meshes compared so far with the same meshes made again, and those
     * that differed */
    int reused, empty, checked, check_bad;
    long triangles;
};
const struct raster_live_stats *raster_live_stats(const struct raster_live *L);
/* the section mesh cache's bytes (its table and the vertices it holds) */
size_t raster_live_mesh_bytes(const struct raster_live *L);

/* One frame into RGB (w * h * 3): the sky and the opaque terrain (pass 0);
 * returns how many section passes it meshed. */
int raster_live_frame(struct raster_live *L, const struct raster_live_in *in, unsigned char *rgb);
/* sortAndRender(pass 1): the translucent terrain the last raster_live_frame
 * sorted, into RGB over what is there (the entities, the tile entities, the
 * block overlays and the particles EntityRenderer.renderWorld draws first),
 * against the depth they left; it writes no depth. */
void raster_live_translucent(struct raster_live *L, unsigned char *rgb);
/* The two 3D presentation passes after entities and before the HUD, under
 * the frame's fog. BOX (min x, y, z, max x, y, z in the block's own
 * coordinates, what setBlockBoundsBasedOnState left) is the selection's
 * bounds; NULL takes the block table's for the id and metadata. */
void raster_live_overlay(struct raster_live *L, const struct rs_out *rs,
                         unsigned char *rgb, int selected, int sx, int sy, int sz,
                         const double *box, int stage, int dx, int dy, int dz);
/* The weather and the first-person screen overlays over the frame
 * raster_live_frame just drew, the way raster_render's recorded path draws them:
 * the weather, then the caller's hand, then the depth clear and the overlays.
 * The world, the mesher and the textures come from L; every other field is the
 * caller's. */
struct worldfx_in;
void raster_live_weather(struct raster_live *L, const struct worldfx_in *in);
void raster_live_overlays(struct raster_live *L, const struct worldfx_in *in);
/* TextureMap.updateAnimations' uploads into the live block atlases (the
 * terrain's and the one the particles and the fire overlay read). */
struct texanim;
struct texanim_state;
void raster_live_animate(struct raster_live *L, const struct texanim *a, const struct texanim_state *st);
void raster_live_free(struct raster_live *L);

#endif
