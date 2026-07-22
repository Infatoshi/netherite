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
    float anim_time;
    int   death_ticks;
    int   phase_id;
    int   stationary;
    int   skin;
    int   lm_lit;
    float lm_light, lm_blk;
    float lm_mul_r, lm_mul_g, lm_mul_b;
} GmEntityView;
#endif

/* Emit textured multi-box mob models for `n` entities into `out` (flat triangle
 * list). Returns vertex count written (<= max); only whole models that fit are
 * emitted, so `out` is never overrun. Each modeled entity contributes 36 verts
 * per model box (biped 216, spider 396, blaze 468, ... - vanilla part counts);
 * unmodeled types keep the legacy single 36-vert marker box. NONE/PLAYER are
 * skipped. */
int       gm_entities_emit(const GmEntityView *ents, int n, CrVertex *out, int max);

/* Advance the dragon trail ring for a tick whose frame is NOT rendered
 * (--frame-every sparse capture). Rendered ticks push inside the dragon
 * emit; call exactly one of the two per tick or the trail desyncs. */
void      gm_dragon_pose_tick(int ent_id, float yaw, float y);

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

/* The mob-texture atlas to bind (CrShadeCtx.atlas) for the entity pass. */
CrTexture gm_entity_atlas(void);

#ifdef __cplusplus
}
#endif
#endif /* MAGMA_ENTITY_RENDER_H */
