#ifndef NETHERITE_RASTER_MOBS_H
#define NETHERITE_RASTER_MOBS_H

#include "raster_entity_quad.h"
#include "raster_things.h"

struct rb_world;
struct rb_table;

enum mob_model_kind
{
    MOB_PIG,
    MOB_COW,
    MOB_SHEEP,
    MOB_CHICKEN,
    MOB_ZOMBIE,
    MOB_SKELETON,
    MOB_WITHER_SKELETON,
    MOB_CREEPER,
    MOB_SPIDER,
    MOB_CAVE_SPIDER,
    MOB_ENDERMAN,
    MOB_WITCH,
    MOB_SLIME,
    MOB_SILVERFISH,
MOB_PIGMAN, MOB_GHAST, MOB_BLAZE, MOB_MAGMA_CUBE,
    MOB_VILLAGER, MOB_IRON_GOLEM, MOB_SQUID, MOB_BAT, MOB_MOOSHROOM,
    MOB_DRAGON,
    MOB_ENDER_CRYSTAL,
    MOB_LEASH_KNOT,
    MOB_PLAYER
};

struct mob_render_input
{
    enum mob_model_kind kind;
    double x, y, z;
    /* RenderLiving.func_110827_b's lead: the strip's start relative to x, y,
     * z (the body's side, 1.6 - height lower) and its run to the holder */
    int leash;
    float leash_start[3], leash_run[3];
    float body_yaw, head_yaw, pitch;
    float limb, limb_amount, swing, wing, eat_head_y, eat_head_x;
    float partial_tick;
    float brightness_scalar;
    float height;
    float width;
    float squish, tentacle, squid_pitch, squid_yaw, attack_progress;
    int age, hurt, death, child, sheared, color, saddle, brightness, invisible, burning;
    int id, charged, carried_id, carried_meta, screaming, slime_size;
int profession, ghast_shooting, hanging, attack_timer, rose_timer;
    float flash;
    /* EntityDragon: ModelDragon.render's animTime and the 24 movement offsets
     * the neck, head and tail chains read, plus RenderDragon's death ticks and
     * RenderDragon.doRender's healing beam inputs. */
    float anim;
    double off[24][3];
    int death_ticks;
    int has_beam;
    float beam_rot, beam_dx, beam_dy, beam_dz, beam_len;
    int beam_ticks;
    /* EntityEnderCrystal: RenderEnderCrystal's var10 (innerRotation + pt). */
    float crystal_rot;
    /* lane/entrender: Render.renderEntityOnFire's posY - boundingBox.minY and
     * the view yaw, RenderBiped's equipment (0 the hand, 1..4 boots to
     * helmet), the zombie villager and its conversion shake, the witch's
     * held-item pose and Minecraft.getSystemTime for the item glint. */
    float fire_dy, view_yaw;
    int zombie_villager, converting;
    struct thing_item equip[5];
    int64_t now;
    /* RenderPlayer: ModelBiped's heldItemRight (1 an item, 3 a sword
     * blocking), aimedBow, isSneak and isRiding; the sleeping player's
     * rotateCorpse (getBedOrientationInDegrees) and renderLivingAt offset;
     * Entity.yOffset (the shadow reads posY, which it lifts) */
    int held_right, aimed_bow, sneak, riding, sleeping;
    /* RendererLivingEntity.renderArrowsStuckInEntity (RenderPlayer's): the
     * arrow count (DataWatcher 9) and the entity id that seeds the places */
    int arrows, entity_id;
    float bed_deg, sleep_dx, sleep_dy, sleep_dz, y_offset;
    /* IBossDisplayData: the health RenderDragon.doRender hands BossStatus */
    float health, max_health;
    /* TileEntityMobSpawnerRenderer: the entity drawn at the origin of a
     * modelview of its own (column-major, camera-relative, the camera left
     * out), turned and scaled inside the cage; x, y, z and the range test
     * are unused */
    int outer_set;
    float outer[16];
    /* Render.renderEntityOnFire under an outer modelview: the frame's own
     * lift (the preview's yOffset, which the model's y - yOffset takes back)
     * and the lightmap coordinates the GL state holds then (set: the
     * first-person hand's, RendererLivingEntity having re-enabled the unit) */
    float fire_up;
    int fire_brightness_set, fire_brightness;
};

/* GuiInventory.func_147046_a's player: what the preview draws with. The
 * rotations come from the mouse offsets (dx, dy: the screen point minus the
 * mouse); prev_body_yaw and prev_pitch are the entity's own previous values,
 * which RendererLivingEntity interpolates from at partial tick 1. */
struct player_preview
{
    float dx, dy;
    float prev_body_yaw, prev_pitch;
    float limb, limb_amount, prev_limb_amount, swing, y_offset;
    int age, sneak, riding, held;
    int prev_body_own;   /* prev_body_yaw unknown: start from the preview's own yaw */
};

/* The preview drawn over RGB (w by h device pixels, the overlay sw_d by sh_d
 * GUI units) at GUI point (x, y), scale SIZE: ModelBiped with the 64x32 SKIN,
 * RenderPlayer's 0.9375, the standard item lighting set under the preview's
 * 135 degree turn, no lightmap, its own depth test over the screen. */
void raster_mobs_player_preview(unsigned char *rgb, int w, int h, double sw_d, double sh_d,
                                int x, int y, int size, const unsigned char *skin,
                                const struct player_preview *p);

/* The same preview through RenderPlayer whole (the model, the armour, the
 * held item, the arrows, the hurt tint, the fire): BASE is the client
 * player's render input at partial tick 1, whose body, head and pitch the
 * preview's rotations replace; the textures come from ASSETS. */
void raster_mobs_player_preview_full(const char *assets, unsigned char *rgb, int w, int h, double sw_d,
                                     double sh_d, int x, int y, int size, const struct player_preview *p,
                                     const struct mob_render_input *base, const uint32_t *lm);

int raster_mobs_draw(const char *assets, const struct entity_raster_target *target,
                     const struct mob_render_input *mob, int count, const struct rb_world *world,
                     const struct rb_table *table, int fancy, const float light_brightness[16]);
void raster_mobs_scene(const char *scene, const struct entity_raster_target *target);

#endif
