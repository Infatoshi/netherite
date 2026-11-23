#ifndef NETHERITE_RASTER_TILEENT_H
#define NETHERITE_RASTER_TILEENT_H

/* TileEntityRendererDispatcher and the tile-entity renderers the game reaches:
 * TileEntityChestRenderer (single, double, trapped; the lid angle),
 * TileEntityEnderChestRenderer, TileEntitySignRenderer (the board and its four
 * lines), TileEntitySkullRenderer (every type, floor and wall),
 * TileEntityMobSpawnerRenderer (the spinning entity, through raster_mobs) and
 * RenderEndPortal (the sixteen texgen layers). RenderGlobal.renderEntities
 * draws them after the entities, in RenderGlobal.tileEntities order, under
 * the standard item lighting and the lightmap; the GL state one renderer
 * leaves (the skull's culling, GL_RESCALE_NORMAL) carries to the next, so the
 * list is drawn in order with that state threaded through. */

#include "raster_entity_quad.h"

enum te_render_kind
{
    TE_RENDER_NONE,
    TE_RENDER_CHEST,
    TE_RENDER_ENDER_CHEST,
    TE_RENDER_SIGN,
    TE_RENDER_SKULL,
    TE_RENDER_SPAWNER,
    TE_RENDER_END_PORTAL,
    TE_RENDER_ENCHANT_TABLE
};

#define TE_SIGN_LINE 64 /* bytes of UTF-8 per sign line (15 characters in the game) */

/* One entry of RenderGlobal.tileEntities, in the renderers' own terms. */
struct te_render_item
{
    enum te_render_kind kind;
    int x, y, z;
    int block, meta;
    int brf;                  /* World.getLightBrightnessForSkyBlocks(x, y, z, 0) */
    float lid, prev_lid;      /* chests: lidAngle and prevLidAngle */
    unsigned char adj[4];     /* TileEntityChest's adjacent chests: z-, x+, x-, z+ */
    char text[4][TE_SIGN_LINE];
    int edit;                 /* TileEntitySign.lineBeingEdited */
    int skull_type, skull_rot;
    double rot, prev_rot;     /* MobSpawnerBaseLogic field_98287_c and field_98284_d */
    int mob;                  /* the spawner's entity as an enum mob_model_kind, -1 for none */
    float mob_body_yaw, mob_head_yaw, mob_pitch; /* its interpolated rotations */
    int mob_age;              /* its ticksExisted */
    /* TileEntityEnchantmentTable (the client's own): field_145926_a (the tick
     * count), field_145933_i and field_145931_j (the page flip and its last),
     * field_145930_m and field_145927_n (the book's spread and its last),
     * field_145928_o and field_145925_p (its turn and its last) */
    int book_ticks;
    float book_flip, book_prev_flip, book_spread, book_prev_spread, book_rot, book_prev_rot;
};

/* The frame-wide inputs the renderers read. */
struct te_render_frame
{
    double view[3];           /* the viewer: staticPlayerX/Y/Z and field_147560_j/k/l */
    float object[3];          /* ActiveRenderInfo.objectX/Y/Z */
    float pt;
    long long ms;             /* Minecraft.getSystemTime() */
    int fancy;
    int normalize;            /* GL_NORMALIZE on (RenderSlime left it on) */
};

/* Draws LIST in order into the target's frame (its mv is the camera
 * modelview). ASSETS is a directory holding the textures as state/te_*.rgba
 * and state/<mob>.rgba (a recorded scene), else out/assets is read.
 * Returns the number drawn. */
int raster_tileents_draw(const char *assets, const struct entity_raster_target *t,
                         const struct te_render_frame *f, const struct te_render_item *list, int n);

/* The recorded frame's "tes" block (TileEntityRenderProbe), drawn as above;
 * a frame without one draws nothing. */
void raster_tileents_scene(const char *scene, const struct entity_raster_target *t);

/* The client half of the state the renderers read, one tick of each:
 *
 * TileEntityChest.updateEntity's lid (and TileEntityEnderChest's): prevLidAngle
 * takes lidAngle, which then opens or closes 0.1 toward whether any player
 * uses it (numPlayersUsing, which S24 block event 1 sets on the client).
 * LEAD is whether the chest plays the sounds (a chest with no chest of its
 * kind at z-1 or x-1; always for the ender chest); the return value is the
 * number of World.rand.nextFloat draws the sound pitches take (0 or 1).
 *
 * MobSpawnerBaseLogic.updateSpawner's client branch past canRun (a player
 * within 16 blocks): the delay counts down to 0, then field_98284_d takes
 * field_98287_c, which turns 1000 / (delay + 200) degrees; the three
 * World.rand.nextFloat draws of its particle position come first (not
 * taken here). */
int raster_tileents_lid_tick(float *lid, float *prev_lid, int players_using, int lead);
void raster_tileents_spawner_tick(double *rot, double *prev_rot, int *delay);

/* The enum mob_model_kind of an EntityList name ("Zombie", "CaveSpider"),
 * -1 when the mob renderer has no model for it. */
int raster_tileents_mob_kind(const char *name);

/* one recorded end portal triangle (raster_obs.h) drawn into T's frame */
struct raster_obs_portal;
void raster_tileent_portal_triangle(const struct entity_raster_target *t, const struct raster_obs_portal *p,
                                    const unsigned char *tex, int tex_w, int tex_h);

#endif
