/* game/entity_render.h - owner: ENTITY-RENDER agent.
 *
 * Emit vanilla-faithful multi-box mob models (world-space CrVertex triangle
 * lists) for the visible entities, plus the mob-skin atlas for that raster pass.
 * The prototypes also live in game/game.h (the seam contract); this header lets
 * the module be built/tested standalone without pulling the whole game seam. */
#ifndef MAGMA_ENTITY_RENDER_H
#define MAGMA_ENTITY_RENDER_H

#include "core/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GM_VIEW_EXPLOSION_LARGE
#define GM_VIEW_EXPLOSION_LARGE 50
#endif

/* GmEntityView matches game/game.h exactly (POD). Redeclared here (guarded) so
 * this module compiles without game.h; game.h defines the same layout. */
#ifndef MAGMA_GAME_H
typedef struct {
    int   type;     /* EW_TYPE_* (0 none, 1 player, 2 zombie, ...) */
    float x, y, z;  /* FEET position, world coords */
    float yaw;      /* body yaw, degrees */
    float health;   /* current */
    int   item_id, item_meta, age;
    float limb_swing, limb_swing_amount;
    int   hurt_time, ent_id;
    int   tape_pose;
    float head_yaw, pitch, swing_progress;
    int   death_time, flags, sheared, fleece_color;
    float graze_y, graze_x;
    int   item_count;
    float hover_start;
    int   has_hover_start;
    float crystal_rot;
    int   show_bottom;
    int   beam_x, beam_y, beam_z;
    int   has_beam;
    float anim_time;
    int   death_ticks;
    int   phase_id;
    int   stationary;
    int   has_heal_beam;
    float heal_x, heal_y, heal_z;
    int   heal_crystal_ticks;
    int   skin;
    int   lm_lit;
    float lm_light, lm_blk;
    float lm_mul_r, lm_mul_g, lm_mul_b;
    /* EntitySlime/EntityMagmaCube.squishFactor (render partial=1). */
    float squish;
    int   creeper_fuse;
    int   ticks_existed;
    int   armor_feet, armor_legs, armor_chest, armor_head;
    int   stand_flags;
    float boat_paddle[2];
} GmEntityView;
#endif

/* Emit textured multi-box mob models for `n` entities into `out` (flat triangle
 * list). Returns vertex count written (<= max); only whole models that fit are
 * emitted, so `out` is never overrun. Each modeled entity contributes 36 verts
 * per model box (biped 216, spider 396, blaze 468, ... - vanilla part counts);
 * unmodeled types keep the legacy single 36-vert marker box. NONE/PLAYER and
 * XP orbs (type 21, drawn by gm_xp_orbs_emit) are skipped. */
int       gm_entities_emit(const GmEntityView *ents, int n, CrVertex *out, int max);

/* Minecart display tiles are emitted in the terrain-atlas pass. */
int       gm_minecart_contents_emit(const GmEntityView *ents, int n,
                                    CrVertex *out, int max);
/* RenderXPOrb.doRender: camera-facing experience_orb.png billboard. 6 verts per
 * orb. Uses view_yaw/view_pitch (playerViewY/X). item_id=xpValue, item_meta=
 * xpColor, age=xpOrbAge. Binds the mob atlas (CR_MOB_EXPERIENCE_ORB). */
int       gm_xp_orbs_emit(const GmEntityView *ents, int n, float view_yaw,
                          float view_pitch, CrVertex *out, int max);

/* Advance the dragon trail ring for a tick whose frame is NOT rendered
 * (--frame-every sparse capture). Rendered ticks push inside the dragon
 * emit; call exactly one of the two per tick or the trail desyncs. */
void      gm_dragon_pose_tick(int ent_id, float yaw, float y, float health);

/* Geometry oracle: when MAGMA_GEOM_DUMP names a file, each emit logs its
 * model-part poses ("D <tick> <label> rpx rpy rpz rx ry rz", vanilla
 * setRotationAngles units) there; this stamps the tick for those lines. */
void      gm_entity_geom_tick(long tick);

/* Tape type string (EntityList simple class name, e.g. "EntitySheep") ->
 * EW_TYPE_* / render-only id with a model or billboard, or -1 when no model
 * exists (caller skips). */
int       gm_entity_type_for_name(const char *name);

/* Render-only billboard type -> packed item-atlas sprite id. */
int       gm_entity_billboard_item(const char *name);

/* Tape type string -> skin-variant sprite override (CR_MOB_*+1) for types that
 * reuse a base mob's model with a different skin (EntityPigZombie, EntityHusk,
 * EntityStray, EntityCaveSpider, EntityMooshroom). 0 = no override. */
int       gm_entity_skin_for_name(const char *name);

/* Vanilla getEyeHeight for a rendered type - where to sample world light. */
float     gm_entity_eye_y(int type);

/* 1 when the type overrides getBrightnessForRender to lightmap max, so world
 * light must not dim it. EntityBlaze returns 15728880 (sky 15 / block 15). */
int       gm_entity_fullbright(int type);

/* The mob-texture atlas to bind (CrShadeCtx.atlas) for the entity pass. */
CrTexture gm_entity_atlas(void);
CrTexture gm_crystal_beam_texture(void);

/* Live projectile views collapse ghast large and blaze small fireballs to the
 * same BILLBOARD+385. Call after gm_runtime_projectile_views with the dense
 * list of active projectile types (3=small, 5=large) in emission order, or
 * pass nproj=0 to honour item_meta>=2 already set on views. Morphs large shots
 * to type GM_VIEW_DRAGON_FIREBALL with item_id 385 so RenderFireball scale is
 * 2.0 while keeping the fire_charge sprite. */
void gm_entity_patch_large_fireballs(const int *proj_types, int nproj,
                                     GmEntityView *views, int nviews);
/* Temporarily retype large fireballs as BILLBOARD so the fire-layer emit runs;
 * pair with gm_entity_restore_large_fireball_types after. */
void gm_entity_prep_large_fireball_fire(GmEntityView *views, int nviews);
void gm_entity_restore_large_fireball_types(GmEntityView *views, int nviews);

/* LayerEnderDragonDeath light rays (death_ticks > 0). Untextured additive
 * triangle fans (9 verts/ray = 3 tris). Rotations accumulate; verts after
 * applyRotations+prepareScale+Layer(0,-1,-2). Draw with blend=3, untextured=1. */
int gm_dragon_death_rays_emit(const GmEntityView *ents, int n, CrVertex *out,
                              int max);

/* RenderDragon.renderCrystalBeams: the dragon's healing beam to its
 * healingEnderCrystal, plus any crystal's own getBeamTarget() beam. Textured
 * with CR_MOB_ENDERCRYSTAL_BEAM, vertex-coloured black (origin) to white (far
 * end), lightmap folded from the drawn entity. Vanilla disables culling, so
 * both windings are emitted. Draw on the mob atlas with alpha_test,
 * alpha_ref=0.1, CR_LAYER_CUTOUT and the same fog/lightmap as the entity pass.
 * Up to 16 tris * 2 windings * (1 + V bands) * 3 verts per beam. */
int gm_crystal_beams_emit(const GmEntityView *ents, int n, CrVertex *out,
                          int max);

/* Portal (particles.png) + dragon death EXPLOSION_LARGE (explosion.png FXLayer 3).
 * EntityDragon: LARGE each dead tick only when health<=0; HUGE in [180,200].
 * Oracle deathTicks pins keep health full so particles do not fire. */
int gm_particles_emit(const GmEntityView *ents, int n, float view_yaw,
                      float view_pitch, CrVertex *out, int max);
int gm_particles_emit_filtered(const GmEntityView *ents, int n, float view_yaw,
                               float view_pitch, int suppress_explosion,
                               CrVertex *out, int max);

/* Vanilla removes EntityDragon at deathTicks 200 while its ParticleManager
 * cloud lives ~17 more ticks. Call once per simulated tick with that tick's
 * views, before gm_particles_emit, so the burst does not pop off with the
 * entity. Idempotent within a tick. */
void gm_particles_dragon_latch(long long tick, const GmEntityView *ents, int n);

/* Dig hit-effect billboards (stage 1..10 progress proxy). ParticleDigging UVs
 * from bm_particle_sprite (model particle icon), not a cube face. face is
 * EnumFacing D-U-N-S-W-E or -1 when unknown (spawn without face offset).
 * particle_count: when >0, number of billboards (entity_pin dig_hit count);
 * when <=0, fall back to stage (live progress proxy). Draw with terrain atlas.
 * lm_r/g/b: EntityRenderer.updateLightmap RGB at the particle (0..1). Vanilla
 * ParticleDigging multiplies its 0.6 gray by the lightmap (VertexBuffer.lightmap
 * + color); without that fold End dig dust stays fullbright gray on unlit stone.
 * Input limit: dig_state has no per-tick particle age list or continuous rand
 * stream — recon only. */
int gm_block_break_particles_emit(int wx, int wy, int wz, int block_id,
                                  int stage, int face, int particle_count,
                                  float view_yaw, float view_pitch,
                                  float lm_r, float lm_g, float lm_b,
                                  CrVertex *out, int max);

/* LayerSlimeGel outer shell. Draw with blend=4 (src-over + depth write),
 * alpha_test + alpha_ref=0.1 (living GL_GREATER 0.1), tint.a=255. */
int gm_slime_gel_emit(const GmEntityView *ents, int n, CrVertex *out, int max);

/* RenderLivingBase.applyRotations death keel for this view, radians about the
 * entity-local Z (0 while alive). f = min(1, sqrt(deathTime/20*1.6)) * 90deg
 * with the file-wide capture partialTicks of 1.0; driven only by the tape's
 * recorded deathTime. */
float er_death_roll(const GmEntityView *v);

/* ---- TileEntityMobSpawnerRenderer miniature -------------------------------
 * One mob-spawner tile entity's render input: the block it sits in, the
 * EW_TYPE_* of MobSpawnerBaseLogic.getCachedEntity() (i.e. the spawner's
 * SpawnData/SpawnPotentials entity id), and MobSpawnerBaseLogic.mobRotation
 * in its pre-x10 units. type < 0 (no cached entity) emits nothing, exactly
 * like the oracle's `if (entity != null)` guard.
 *
 * NOTE: magma has no tile-entity store fed from world data, so NOTHING
 * currently constructs a GmSpawnerView from a real spawner - see
 * magma/OPEN_DIVERGENCES.md "Spawner-cage miniature" for the exact missing
 * plumbing. This renderer is verified against the oracle transform by
 * game/test_entity_render.c and is correct the moment the type flows. */
typedef struct {
    int   wx, wy, wz;
    int   type;
    float mob_rotation;
} GmSpawnerView;

/* Vanilla Entity setSize() width/height, for the miniature's shrink-to-fit. */
void  gm_entity_size(int type, float *w, float *h);

/* TileEntityMobSpawnerRenderer.renderMob: f = 0.53125, and f /= max(w,h)
 * when max(w,h) > 1. Blaze (0.6 x 1.8) -> 0.53125/1.8. */
float gm_spawner_mini_scale(int type);

/* Emit the miniature entities for `n` spawners. nparts*36 verts each; binds
 * the same mob atlas as gm_entities_emit. */
int   gm_spawner_miniatures_emit(const GmSpawnerView *sp, int n,
                                 CrVertex *out, int max);

/* RH Rodrigues unit checks (Rx+90 maps +Y→+Z; composed axes stay unit). */
int gm_entity_rot_rx90_maps_y_to_z(void);
int gm_entity_rot_axes_are_unit(void);

/* UV offset dragon -> dragon_exploding for CrShadeCtx.alpha_mask dissolve. */
void gm_entity_dissolve_mask(float *u_off, float *v_off);

#ifdef __cplusplus
}
#endif
#endif /* MAGMA_ENTITY_RENDER_H */
