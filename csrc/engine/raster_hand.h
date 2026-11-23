#ifndef NETHERITE_RASTER_HAND_H
#define NETHERITE_RASTER_HAND_H

#include <stddef.h>
#include <stdint.h>

/* The first-person hand and held item: EntityRenderer.renderHand and
 * ItemRenderer.renderItemInFirstPerson over a finished frame, after the 3D
 * overlays and the weather, before renderOverlays' screen overlays
 * (raster_worldfx) and the HUD. Recorded scenes read their numbers from the
 * frame's "hand" block (RenderStateProbe.hand); the playable client fills
 * raster_hand_in from its own client state. */

struct rb_world;
struct rb_table;

/* EnumAction's ordinals, as getItemUseAction returns them. */
enum { HAND_USE_NONE, HAND_USE_EAT, HAND_USE_DRINK, HAND_USE_BLOCK, HAND_USE_BOW };

/* ItemRenderer.itemToRender, reduced to what the item paths branch on. */
struct raster_hand_item
{
    int id, meta;
    int block3d;          /* ItemBlock with a sprite-0 renderItemIn3d render type */
    int sprite;           /* getItemSpriteNumber: 0 the blocks atlas, 1 the items atlas */
    int npass;            /* requiresMultipleRenderPasses: 2 */
    struct
    {
        float min_u, max_u, min_v, max_v;
        int icon_w, icon_h;
        int tint;         /* getColorFromItemStack(stack, pass) */
        int has_icon;
    } pass[2];
    int action, max_use;  /* getItemUseAction, getMaxItemUseDuration */
    int rotate;           /* shouldRotateAroundWhenRendering */
    int render_color;     /* Block.getRenderColor(meta), the held block's colour */
    int grass;            /* Blocks.grass: white sides, render_color on top */
    int eff;              /* ItemStack.hasEffect: the glint over pass 0 */
};

struct raster_hand_in
{
    float pt;
    /* the client player's rotation and EntityPlayerSP's lagging arm angles */
    float pitch, prev_pitch, yaw, prev_yaw;
    float arm_pitch, prev_arm_pitch, arm_yaw, prev_arm_yaw;
    float equipped, prev_equipped;     /* ItemRenderer.equippedProgress */
    float swing, prev_swing;           /* EntityLivingBase.swingProgress */
    int use_count;                     /* getItemInUseCount */
    int light;                         /* getLightBrightnessForSkyBlocks at the player */
    int invisible;
    int has_item;
    struct raster_hand_item item;
    const float *proj, *mv;            /* renderstate_hand_camera */
    const uint32_t *lm;                /* the frame's lightmap */
    const char *scene;                 /* table.bin and atlas.json for held blocks */
    const unsigned char *skin;         /* 64x32 RGBA */
    const unsigned char *items;        /* the items atlas */
    int items_w, items_h;
    const unsigned char *blocks;       /* the blocks atlas, mip level 0 */
    int blocks_w, blocks_h;
    const unsigned char *chest;        /* 64x64, the chest item's model texture */
    int no_hand;                       /* renderItemInFirstPerson skipped (hideGUI) */
    const unsigned char *glint;        /* 64x64, the enchant glint; NULL draws none */
    int64_t now;                       /* Minecraft.getSystemTime, the glint's scroll */
};

/* Draws the hand pass into rgb (w * h * 3) over its own cleared depth. */
void raster_hand_draw(const struct raster_hand_in *in, unsigned char *rgb, int w, int h);

/* A recorded scene's hand: its frame's hand block, or for recordings made
 * before RenderStateProbe wrote one, the settled state of an idle player
 * holding the HUD's selected stack (the light from WORLD at the player). */
void raster_hand_scene(const char *scene, unsigned char *rgb, int w, int h,
                       const uint32_t lm[256], const struct rb_world *world,
                       const struct rb_table *table);

/* One item's hand record from a recording's state/item_table.json, for the
 * playable client. Returns 0 when the item is unknown. */
int raster_hand_item_lookup(const char *scene, int id, int meta, struct raster_hand_item *out);

/* A whole RGBA file of N bytes: SCENE/NAME, else out/assets/FALLBACK or
 * assets/FALLBACK. NULL when none reads. */
unsigned char *raster_hand_read_rgba(const char *scene, const char *name, const char *fallback,
                                     size_t n);

#endif
