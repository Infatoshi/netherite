/* game/game.h - THE SEAM CONTRACT: wire the verified mc-sim tick to the magma
 * software rasterizer into one playable binary (`magma_game`).
 *
 * This is the game-layer analogue of core/types.h. Every game/ module implements
 * the prototypes declared here against these exact POD types. ONE OWNER PER FILE
 * (named in the section comments); do NOT edit another module's file. Add internal
 * helpers in your own .c freely. Do not change a signature here without telling the
 * orchestrator (app/main.c depends on all of them).
 *
 * ARCHITECTURE (why these pieces exist):
 *   - The RENDER world is a real view-distance mc-sim worldgen landscape meshed by
 *     the MC-faithful mesher (world/mesh_mc.c) + frustum cull (core/frustum.h). This
 *     path already pixel-matches real MC to ~39/ch (rung-4). game/world_live.c makes
 *     it LIVE: stream chunks around the camera, re-mesh only dirty chunks, edit blocks.
 *   - The PLAYER is driven by the VERIFIED player_survival.h kernels (physics /
 *     collision / raycast / break / place / vitals). Those kernels address an
 *     ORIGIN-CENTERED PSV_DIM x PSV_DIM Chunk window (psv_chunk_index), so the player
 *     runs in a FLOATING-ORIGIN local frame: app/main.c keeps a block offset = the
 *     player's current chunk origin, fills a region-centered window from the live
 *     world (gm_world_fill_window), ticks the player in local coords (game/player_ctl.c),
 *     writes edits back (gm_world_set_block), and recenters on each chunk crossing.
 *   - INPUT (present.c CrInput) -> GmAction (game/input_map.c). HUD overlay
 *     (game/hud.c). Mob models (game/entity_render.c). Pose pixel-diff vs real MC
 *     (raster/verify/mc_capture/game_verify.*).
 *
 * Runtime block ids are canonical vanilla 1.11.2 numeric ids with legacy metadata.
 * Compact CB_ / PB_ values exist only behind the worldgen/renderer model-key boundary.
 */
#ifndef MAGMA_GAME_H
#define MAGMA_GAME_H

#include "core/types.h"

/* The physics-touching prototypes below pass three mc-sim types (Chunk, McSinTable,
 * PsvPlayer) by pointer. mc-sim defines them as ANONYMOUS-tag typedefs (typedef struct
 * {...} Chunk;), so the `struct Chunk` used here is a DISTINCT, incomplete type -- an
 * OPAQUE HANDLE. That is intentional: it keeps this header mc-sim-free (hud/input/entity
 * modules need no mc-sim include). The implementing files (world_live.c, player_ctl.c)
 * and app/game_main.c include player_survival.h and CAST these opaque pointers to the
 * real mc-sim types at the boundary (a pointer cast is legal; only dereferencing the
 * incomplete tag would not be). Callers holding a real Chunk / McSinTable / PsvPlayer
 * pointer pass it through a cast to silence the incompatible-pointer warning. */
struct Chunk;
struct McSinTable;
struct PsvPlayer;

#ifdef __cplusplus
extern "C" {
#endif

/* ============================ shared POD types ============================ */

#define GM_MAX_POTION_EFFECTS 8

typedef struct {
    int id;        /* vanilla Potion registry id */
    int amplifier;
    int duration;  /* remaining client ticks */
    /* !PotionEffect.doesShowParticles(). GuiIngame.renderPotionEffects skips
     * the top-right status icon entirely when showParticles is false
     * (`/effect <ent> <id> <sec> <amp> true`), so it is NOT cosmetic - it is
     * the whole gate on the icon. Stored INVERTED so the zero value of every
     * existing producer (live effects, HUD layout tests, tape rows predating
     * the flag) keeps vanilla's shown-by-default behaviour. */
    int hide_particles;
} GmPotionEffectView;

/* One tick of intent, produced from CrInput by game/input_map.c and consumed by
 * game/player_ctl.c. forward/strafe in [-1,1] (MC WASD); dyaw/dpitch are DELTA
 * degrees this tick from the mouse; the rest are edge/held flags. */
typedef struct {
    float forward;    /* +1 = W (toward look), -1 = S            */
    float strafe;     /* +1 = D (right),        -1 = A            */
    float dyaw;       /* degrees to ADD to player yaw this tick  */
    float dpitch;     /* degrees to ADD to player pitch this tick */
    int   jump;       /* space held                              */
    int   sneak;      /* shift held                              */
    int   sprint;     /* ctrl held                               */
    int   attack;     /* left mouse held (mine / break)          */
    int   use;        /* right mouse held (place / use)          */
    int   do_break;   /* attack edge this tick -> break target   */
    int   do_place;   /* use edge this tick    -> place target   */
    int   hotbar_sel; /* 0..8 selected slot, or -1 = unchanged   */
    /* Container.slotClick applied this tick by gm_runtime_tick -> gm_container_click
     * (game/container_live.h): full inventory + open-container slot ids. */
    int   inv_click;
    int   inv_slot;    /* 0..35 inv, 36..44 grid, 45 result, 46..48 furnace,
                        * 49..52 armor, 53..79 chest, -999 */
    int   inv_button;  /* 0 left / 1 right */
    int   inv_type;    /* CC_CLICK_PICKUP / QUICK_MOVE / THROW */
    /* GuiGameOver click this tick (only consumed while dead).
     * death_click=1 and death_button 0=Respawn / 1=Title Screen. */
    int   death_click;
    int   death_button;
} GmAction;

/* Player state the camera + HUD read. Positions in WORLD coords (doubles collapsed
 * to float for the camera is fine; HUD uses the scalar vitals only). */
typedef struct {
    float x, y, z;        /* FEET position, world coords            */
    float eye_height;     /* add to y for the eye/camera            */
    float yaw, pitch;     /* degrees, MC convention                 */
    int   on_ground;
    float health, max_health;   /* 0..20 -> HUD hearts             */
    float food,   max_food;     /* 0..20 -> HUD haunches           */
    int   xp_level;             /* green number                    */
    float xp_frac;              /* 0..1 XP bar fill                */
    int   air;                  /* 0..300 (bubbles), -1 = hidden   */
    int   hotbar_ids[9];        /* block/item id per slot (0 empty)*/
    int   hotbar_counts[9];     /* stack size (0 = empty slot)     */
    int   hotbar_sel;           /* 0..8 highlighted slot           */
    /* --- terminal death observation (appended for back-compat). The shipped game
     * ends the episode at health 0; component callers default these fields to 0. --- */
    int   dead;                 /* 1 when the terminal death state was reached */
    int   deaths;               /* running death count             */
    int   score;                /* EntityPlayer.getScore for GuiGameOver line */
    int   death_ticks;          /* GuiGameOver.enableButtonsTimer (ticks open) */
    float portal;               /* 0..1 client portal overlay/distortion ramp */
    int   portal_frame;         /* physical portal TextureAtlasSprite frame */
    int   portal_phase;         /* EntityRenderer.rendererUpdateCount */
    int   loading;              /* 1=GuiDownloadTerrain, 2=post-close blank frame */
    float fov_mult;             /* EntityRenderer.fovModifierHand (sprint FOV ease;
                                 * 0 from zeroed legacy callers = treat as 1.0) */
    int   bow_pull;             /* ticks the bow has been drawn; <=0 = idle
                                 * (drawn-bow viewmodel + pulling sprite) */
    int   hotbar_meta[9];       /* item damage / block metadata          */
    int   texture_animations_pinned; /* QRL physical-frame-zero atlas pin */
    int   fire;                 /* Entity.isBurning(), after creative suppression */
    int   creative;             /* capabilities.disableDamage           */
    int   hurt_time;            /* EntityLivingBase.hurtTime countdown  */
    int   max_hurt_time;        /* EntityLivingBase.maxHurtTime          */
    float hurt_yaw;             /* EntityLivingBase.attackedAtYaw        */
    float attack_cooldown;      /* getCooledAttackStrength(1), 0..1      */
    int   potion_count;
    GmPotionEffectView potions[GM_MAX_POTION_EFFECTS];
    /* GuiIngame.renderPlayerStats state, filled by gm_hud_state_step. */
    int   hud_health, hud_last_health;
    int   hud_flash, hud_state_valid;
    int   hud_transition_lead; /* post-tick tape row trails GuiIngame by 1 */
    /* Live armor + active hand use (filled by gm_player_view / gm_runtime_view).
     * armor_points: ForgeHooks.getTotalArmorValue / ita_armor_set_points (0..20).
     * use_action: 0 none, 1 EAT/DRINK, 2 BLOCK (shield item 442 only in 1.11.2;
     * swords are NONE). BOW uses bow_pull instead.
     * use_remaining / use_max: getItemInUseCount / getMaxItemUseDuration.
     * absorption: EntityLivingBase.getAbsorptionAmount (health points); drives
     * GuiIngame heart-row count and armor-row y. Live path leaves 0 when no
     * vitals absorption field exists (do not invent).
     * NOTE armor_points derived from item ids alone is a GUESS: vanilla reads
     * the generic.armor attribute, and an ItemStack carrying an
     * AttributeModifiers tag REPLACES the item's default modifiers
     * (ItemStack.getAttributeModifiers), so e.g. a knockback-resistance
     * leather chestplate is worth 0 armor. A tape that records the player's
     * real total overrides the guess (gm_runtime_tape_armor). */
    int   armor_points;
    int   use_action;
    int   use_remaining;
    int   use_max;
    float absorption;
} GmPlayerView;

/* A mob/entity to draw this frame (from the entity store). */
#define GM_VIEW_ITEM 22   /* dropped-item entity view type (not an EW_TYPE) */
#define GM_VIEW_BILLBOARD 30 /* thrown pearl/eye: camera-facing item sprite */
#define GM_VIEW_DRAGON_FIREBALL 33 /* RenderDragonFireball direct 2x quad */
#define GM_VIEW_FALLING_BLOCK 38 /* EntityFallingBlock full-size block model */
typedef struct {
    int   type;       /* EW_TYPE_* (zombie/skeleton/...) or GM_VIEW_ITEM */
    float x, y, z;    /* FEET position, world coords */
    float yaw;        /* body yaw, degrees */
    float health;     /* current, for a tiny overhead bar if desired */
    /* --- appended (zeroed by callers that predate them) --- */
    int   item_id;    /* item/block id; billboard sprite id for view projectiles */
    int   item_meta;  /* GM_VIEW_ITEM: meta of the stack */
    int   age;        /* entity age in ticks (item bob/spin phase) */
    /* render-pose animation (tape ghost path; live mobs leave 0) */
    float limb_swing;        /* EntityLivingBase.limbSwing */
    float limb_swing_amount; /* EntityLivingBase.limbSwingAmount */
    int   hurt_time;         /* EntityLivingBase.hurtTime countdown */
    int   ent_id;            /* tape entity id (-1 if unknown) */
    /* Exact render state from post-2026-07-12 oracle tapes. Zeroed legacy/live
     * callers keep the old inferred-pose path. */
    int   tape_pose;         /* 1 when the fields below came from the oracle */
    float head_yaw;          /* EntityLivingBase.rotationYawHead, degrees */
    float pitch;             /* Entity.rotationPitch, degrees */
    float swing_progress;    /* EntityLivingBase.swingProgress */
    int   death_time;        /* EntityLivingBase.deathTime */
    int   flags;             /* 1 burn, 2 sneak, 4 invisible, 8 child */
    int   sheared;           /* sheep wool layer hidden */
    int   fleece_color;      /* EnumDyeColor metadata 0..15 */
    float graze_y;           /* EntitySheep.getHeadRotationPointY */
    float graze_x;           /* EntitySheep.getHeadRotationAngleX */
    int   item_count;        /* GM_VIEW_ITEM stack count */
    float hover_start;       /* EntityItem random bob/spin phase */
    int   has_hover_start;   /* hover_start is recorded, including 0 */
    /* EntityEnderCrystal render state (RenderEnderCrystal) */
    float crystal_rot;       /* innerRotation (random-init spin/bob phase) */
    int   show_bottom;       /* bedrock base plate rendered */
    int   beam_x, beam_y, beam_z; /* heal-beam target block, -1 = none */
    /* EntityDragon render state */
    float anim_time;         /* wing-flap phase */
    int   death_ticks;       /* 0..200 death collapse/beam animation */
    int   phase_id;          /* PhaseList id (3 LANDING, 4 TAKEOFF, 9 DYING) */
    int   stationary;        /* IPhase.getIsStationary (sitting phases) */
    /* Skin-variant override: 0 = model default, else CR_MOB_*+1 (pigman,
     * husk, stray, cave spider, mooshroom share their base mob's layout). */
    int   skin;
    /* World lighting at the entity (set by the render caller; zero = legacy
     * fullbright). lm_lit=1: light/blk are MC 0..15 levels for the lightmap
     * LUT; lm_lit=2: no LUT (Nether/End) - mul_* is the folded lightmap RGB
     * multiplier and the light scalar stays 1. */
    int   lm_lit;
    float lm_light, lm_blk;
    float lm_mul_r, lm_mul_g, lm_mul_b;
    /* EntitySlime/EntityMagmaCube.squishFactor (render partialTicks=1). */
    float squish;
    /* Entity.ticksExisted on the CLIENT (RenderDragon.renderCrystalBeams uses
     * the dragon's for the beam texture scroll and the crystal's for the beam
     * origin pulse). Recorded per entity; 0 when the tape predates the field. */
    int   ticks_existed;
} GmEntityView;

/* A block edit produced by the player tick, applied to the live world by app/main.c. */
typedef struct {
    int wx, wy, wz;   /* WORLD coords */
    int id;           /* new block id (0 = air = a break) */
    int meta;         /* legacy meta 0..15 (facing/open state) */
    /* Natural harvest result. A break emits this separately so the world loop
     * can create an EntityItem; it must never teleport directly into inventory. */
    int drop_id;      /* item id, or 0 when the block yields nothing */
    int drop_count;
    int drop_meta;
} GmBlockEdit;

/* ============================ game/input_map.c (owner: INPUT agent) ============================
 * Map a present-layer CrInput snapshot to a GmAction. Stateful across calls for
 * edge detection (attack/use/hotbar) and mouse-look scaling; call gm_input_reset()
 * once at startup. mouse_sens is degrees per mouse count (e.g. 0.15). The player's
 * ABSOLUTE yaw/pitch is integrated by the caller from dyaw/dpitch (clamped pitch). */
void     gm_input_reset(void);
GmAction gm_input_map(const CrInput *in, float mouse_sens);

/* ============================ game/world_live.c (owner: WORLD-LIVE agent) ============================
 * Live streaming view-distance world over the MC-faithful mesher (world/mesh_mc.c)
 * + frustum cull (core/frustum.h). Authoritative block store; both the mesh feed and
 * the physics window read from it. */
typedef struct GmWorld GmWorld;

GmWorld  *gm_world_create(long long seed);
/* world_type: 0 default completion world, 1 vanilla-default superflat RL arena. */
GmWorld  *gm_world_create_type(long long seed, int world_type);
void      gm_world_destroy(GmWorld *w);

/* Ensure every chunk within Chebyshev `radius` of chunk (ccx,ccz) is generated+lit. */
void      gm_world_ensure(GmWorld *w, int ccx, int ccz, int radius);

/* Canonical vanilla block id at WORLD coords (0/air outside loaded/[0,255]). */
int       gm_world_block(const GmWorld *w, int wx, int wy, int wz);
/* Legacy meta nibble 0..15 (doors/facing). 0 if unloaded. */
int       gm_world_meta(const GmWorld *w, int wx, int wy, int wz);
int       gm_world_sky_light(const GmWorld *w, int wx, int wy, int wz);
int       gm_world_block_light(const GmWorld *w, int wx, int wy, int wz);
/* Voronoi biome id at a column (-1 unloaded) and the blended 0xRRGGBB grass
 * tint the mesher would use — exposed for the script.c oracle-diff probes. */
int       gm_world_biome(const GmWorld *w, int wx, int wz);
int       gm_world_grass_color(const GmWorld *w, int wx, int wy, int wz);

/* Edit a block: updates the store, re-lights locally, marks the touched chunk (and
 * any neighbour across a chunk border) dirty so the next mesh_view rebuilds it.
 * id is a vanilla block id; meta is 0..15 (facing/open). */
void      gm_world_set_block(GmWorld *w, int wx, int wy, int wz, int id);
void      gm_world_set_block_meta(GmWorld *w, int wx, int wy, int wz, int id, int meta);
/* Snapshot bulk-load primitive: target chunk must already be ensured. Updates
 * canonical state + dirty mesh/light flags without recomputing light per cell. */
void      gm_world_load_block_meta(GmWorld *w, int wx, int wy, int wz, int id, int meta);
/* Monotonic block-mutation counter; bumps on every set_block(_meta). Callers
 * holding a copied window (runtime physics window) refill when it changes. */
long long gm_world_block_gen(const GmWorld *w);

/* World-time / weather counters for the live tick composition (advanced by
 * gm_world_tick). */
typedef struct {
    i64 total_time;
    i64 world_time;
    int rain_time, thunder_time;
    int raining, thundering;
    /* doDaylightCycle off: world_time stays put while total_time (and the
     * weather sim) keep ticking. 0 default = vanilla advance (back-compat). */
    int freeze_daylight;
} GmWorldClock;
void      gm_world_clock_init(GmWorldClock *c, i64 seed);
/* Advance one WorldServer-style weather+time tick (uses world_weather.h). */
void      gm_world_tick(GmWorldClock *c);
/* Advance time with the optional weather bundle disabled: permanently clear,
 * no weather RNG/timer work. */
void      gm_world_tick_clear(GmWorldClock *c);
void      gm_world_clock_set_total_time(GmWorldClock *c, long long total_time);
/* Harness-only weather state injection. Keeps the private verified weather
 * kernel synchronized so the following tick continues from the injected state. */
void      gm_world_clock_set_weather(GmWorldClock *c, int raining, int thundering,
                                     int rain_time, int thunder_time);

/* Stand-on y (highest non-air + 1) at column (wx,wz); sensible default if ungenerated. */
int       gm_world_surface_y(const GmWorld *w, int wx, int wz);

/* Atlas to bind as CrShadeCtx.atlas for the terrain passes (== bm_atlas()). */
CrTexture gm_world_atlas(const GmWorld *w);

/* Fill a region-centered raw-Chunk window for the player physics kernels. `win`
 * points at PSV_NCHUNKS (=9) contiguous mc-sim Chunk structs; window index for the
 * chunk at (ccx+dx, ccz+dz), dx,dz in [-PSV_R,PSV_R], is (dz+PSV_R)*PSV_DIM+(dx+PSV_R)
 * -- exactly what psv_chunk_index expects for an origin-centered region. So a player
 * whose LOCAL position is (worldpos - (ccx*16, 0, ccz*16)) collides correctly. Ensures
 * the region first. */
void      gm_world_fill_window(GmWorld *w, int ccx, int ccz, struct Chunk *win);

/* Re-mesh dirty/newly-visible chunks in the frustum-culled view radius around `cam`
 * and return the concatenated per-CrRenderLayer vertex buffers (world owns them; valid
 * until the next call; do NOT free). Cached: unchanged chunks are not re-meshed. */
typedef struct {
    CrVertex *verts[4];   /* per CrRenderLayer, concatenated over kept chunks */
    int       nverts[4];
    int       n_kept, n_culled;
} GmMeshView;
void      gm_world_mesh_view(GmWorld *w, const CrCamera *cam, int fb_w, int fb_h,
                             GmMeshView *out);

/* Device-resident-mesh variant of mesh_view: same ensure + frustum + remesh
 * walk in the same chunk order, but instead of memcpy-concatenating every
 * visible chunk's verts into the per-layer draw buffers (~2ms/frame of pure
 * memmove), it returns one entry per kept chunk pointing INTO the chunk's
 * packed mesh slab. The CUDA backend mirrors the slabs on the device (upload
 * keyed on `builds`) and concatenates on-GPU in entry order - byte-identical
 * draw buffers, no host copy. off[l]/n[l] are vert offsets into `slab`
 * (packed layer0|1|2|3, used verts = off[3]+n[3]). */
typedef struct {
    int             slot;      /* toroidal mesh-pool slot index (stable id) */
    int             builds;    /* slot rebuild counter (device re-upload key) */
    const CrVertex *slab;      /* host packed slab base */
    int             off[4], n[4];
} GmChunkDraw;
/* Returns kept-chunk count (<= caps.mesh_slots; max_out must cover it) and
 * the per-layer vert totals in nverts[4]. */
int gm_world_mesh_chunks(GmWorld *w, const CrCamera *cam, int fb_w, int fb_h,
                         GmChunkDraw *out, int max_out, int nverts[4]);

/* ============================ game/player_ctl.c (owner: PLAYER-CTL agent) ============================
 * One player tick over a region-centered raw-Chunk window using the VERIFIED
 * player_survival.h kernels (psv_physics_tick / psv_raycast / break / place / vitals).
 * PURE over the supplied window: the player's pos in `pl->ent` is in the window's LOCAL
 * frame (chunk 0 == region center). Emits up to *nedits GmBlockEdit in WORLD coords by
 * adding the caller-supplied (ox,oy,oz) block offset (= region-center chunk origin) to
 * each local edit. Does NOT mutate the window's blocks for edits it reports (the caller
 * applies them via gm_world_set_block); it MAY write them into the window too for its own
 * next-tick reads if convenient -- document which. Returns nothing; fills *nedits.
 *
 * `vitals` (mc-sim PvStats from player_vitals.h, opaque here) is the VERIFIED vanilla
 * vitals state (FoodStats/regen/fall, java==cpu==cuda). gm_player_tick feeds it movement
 * exhaustion + landing fall damage, runs pv_on_update, and MIRRORS pv health/food back
 * into pl->health/pl->food so gm_player_view (which reads pl) reports vanilla values.
 * pv is authoritative; init it with pv_init before the first tick. */
struct PsvPlayer;  /* from player_survival.h */
struct PvStats;    /* from player_vitals.h */
void gm_player_tick(struct Chunk *window, const struct McSinTable *st,
                    struct PsvPlayer *pl, struct PvStats *vitals, GmAction act,
                    int ox, int oy, int oz,
                    GmBlockEdit *edits, int *nedits, int max_edits);

/* Fill a GmPlayerView (world coords) from a PsvPlayer whose pos is in the LOCAL frame,
 * given the block offset (ox,oz) to convert local->world. Convenience for app/main.c. */
void gm_player_view(const struct PsvPlayer *pl, int ox, int oz, GmPlayerView *out);

/* ============================ game/hud.c (owner: HUD agent) ============================
 * 2D overlay composited onto the FINISHED framebuffer (hotbar, selection, hearts,
 * hunger, XP bar+level, crosshair). Uses real MC gui sprites (widgets.png/icons.png)
 * extracted into an assets header. gm_hud_init() loads them once (returns 0 ok). */
int  gm_hud_init(void);
void gm_hud_draw(CrFramebuffer *fb, const GmPlayerView *pv);

/* ============================ game/entity_render.c (owner: ENTITY-RENDER agent) ============================
 * Emit textured-box mob models (world-space CrVertex triangle list) for the visible
 * entities. Bound with gm_entity_atlas() as a SEPARATE raster pass (mob textures live in
 * their own atlas, not the block atlas). Returns vertex count written (<= max). */
int       gm_entities_emit(const GmEntityView *ents, int n, CrVertex *out, int max);
int       gm_xp_orbs_emit(const GmEntityView *ents, int n, float view_yaw,
                          float view_pitch, CrVertex *out, int max);
CrTexture gm_entity_atlas(void);
/* Advance the dragon trail ring on a tick whose frame is not rendered
 * (sparse --frame-every capture); rendered ticks push inside the emit. */
void      gm_dragon_pose_tick(int ent_id, float yaw, float y, float health);
/* Stamp the tick written on MAGMA_GEOM_DUMP part-pose lines (geometry oracle). */
void      gm_entity_geom_tick(long tick);
/* Tape type string ("EntitySheep"...) -> EW_TYPE_* id with a model, or -1. */
int       gm_entity_type_for_name(const char *name);
int       gm_entity_billboard_item(const char *name);
int       gm_entity_skin_for_name(const char *name);
float     gm_entity_eye_y(int type);

#ifdef __cplusplus
}
#endif
#endif /* MAGMA_GAME_H */
