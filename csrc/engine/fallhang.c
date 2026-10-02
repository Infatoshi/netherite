/* Falling blocks and hanging entities, see fallhang.h.
 *
 * Java exactness notes carried from the sources:
 *
 * - EntityFallingBlock.onUpdate: gravity 0.03999999910593033, drag
 *   0.9800000190734863 on every axis, the Time 1 block check, the landing
 *   branch (canPlaceEntityOnSide with force true, so no entity collision
 *   scan, only the replaced material's replaceability, with the anvil's
 *   circuits escape), the func_149828_a sound the anvil adds, the drop with
 *   damageDropped, the time-out (Time over 100 with the position out of
 *   range, or over 600). A block whose item does not exist (the dragon egg)
 *   drops nothing: ItemStack's constructor takes a null item and
 *   entityDropItem refuses it.
 * - EntityFallingBlock.fall, through Entity.moveEntity's updateFallState
 *   when HurtEntities is set: every entity in the landing box is attacked
 *   (the base Entity body only marks velocityChanged, the hanging one
 *   breaks and drops), then the anvil's damage state rolls against the
 *   entity's own Random.
 * - EntityHanging.onUpdate: the validity check every 100th tick of life,
 *   the break drops through onBroken.
 * - EntityHanging.setDirection: float arithmetic throughout; the position
 *   and the bounding box are widened floats.
 * - The NBT load path (the bare World constructor, then
 *   Entity.readFromNBT): the entity keeps the Entity-constructor size
 *   0.6x1.8 with yOffset 0, and preventEntitySpawning stays false, unlike a
 *   constructed one.
 */
#include "nbtw.h"
#include "fallhang.h"
#include "jmath.h"
#include "env.h"
#include "itemtag.h"

#include "entity_nbt.h"

#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include "blocks.h"
#include "ticks.h"

/* BlockFalling.field_149832_M. */
#define fall_instantly (nw_env->fallhang.fall_instantly)

void fh_set_fall_instantly(int on)
{
    fall_instantly = on;
    ticks_set_fall_instantly(on);
}

/* MathHelper.ceiling_float_int. */
static int ceiling_float_int(float v)
{
    int i = (int)v;
    return v > (float)i ? i + 1 : i;
}

/* The block ids the port names. */
enum {
    ID_AIR = 0,
    ID_SAND = 12,
    ID_GRAVEL = 13,
    ID_FIRE = 51,
    ID_PISTON_EXTENSION = 36,
    ID_ANVIL = 145,
    ID_DRAGON_EGG = 122,
    ITEM_PAINTING = 321,
    ITEM_FRAME = 389,
};

/* BlockFalling.func_149831_e: air, fire, water and lava let a block fall. */
static int fall_through(struct world *w, int x, int y, int z)
{
    int id = world_get_block(w, x, y, z) & 4095;

    if (id == ID_AIR) return 1;
    if (id == ID_FIRE) return 1;

    int m = BLOCKS[id].material;

    return m == 6 /* Material.water */ || m == 7 /* Material.lava */;
}

/* Block.damageDropped on the falling blocks: the base returns the metadata,
 * the anvil the damage state. */
static int damage_dropped(int id, int meta)
{
    if (id == ID_ANVIL) return meta >> 2;
    return meta;
}

/* Item.getItemFromBlock: the block items share the block id; the dragon egg
 * has no item, 0 stands for null. */
static int item_of_block(int id)
{
    if (id == ID_DRAGON_EGG) return 0;
    return id;
}

/* The painting arts, in EnumArt declaration order. */
static const struct art { const char *title; int w, h; } ARTS[26] = {
    {"Kebab", 16, 16}, {"Aztec", 16, 16}, {"Alban", 16, 16}, {"Aztec2", 16, 16},
    {"Bomb", 16, 16}, {"Plant", 16, 16}, {"Wasteland", 16, 16},
    {"Pool", 32, 16}, {"Courbet", 32, 16}, {"Sea", 32, 16}, {"Sunset", 32, 16},
    {"Creebet", 32, 16}, {"Wanderer", 16, 32}, {"Graham", 16, 32},
    {"Match", 32, 32}, {"Bust", 32, 32}, {"Stage", 32, 32}, {"Void", 32, 32},
    {"SkullAndRoses", 32, 32}, {"Wither", 32, 32}, {"Fighters", 64, 32},
    {"Pointer", 64, 64}, {"Pigscene", 64, 64}, {"BurningSkull", 64, 64},
    {"Skeleton", 64, 48}, {"DonkeyKong", 64, 48},
};

/* Direction.rotateOpposite. */
static const int ROTATE_OPPOSITE[4] = {2, 3, 0, 1};

/* -------------------------------------------------------------- the lists */

void fh_init(fh_world *fw, struct world *w, det_state *det)
{
    memset(fw, 0, sizeof *fw);
    fw->w = w;
    fw->det = det;
    fw->role = DET_OTHER;
}

int fh_chunk_drop_empty(fh_world *fw, int cx, int cz)
{
    for (int i = 0; i < fw->nchunks; ++i)
    {
        fh_chunk *c = &fw->chunks[i];
        if (!c->used || c->cx != cx || c->cz != cz) continue;
        for (int s = 0; s < FH_SECTIONS; ++s)
            if (c->sec[s].n) return 0;
        for (int s = 0; s < FH_SECTIONS; ++s) sec_release(&c->sec[s]);
        if (i != fw->nchunks - 1) *c = fw->chunks[fw->nchunks - 1];
        --fw->nchunks;
        return 1;
    }
    return 0;
}

void fh_free(fh_world *fw)
{
    for (int i = 0; i < fw->n; ++i)
    {
        nbt_free(fh_ent_at(fw->slot[i])->tile_entity_data);
        fh_ent_release(fh_ent_at(fw->slot[i]));
    }

    fw->n = 0;
    for (int i = 0; i < fw->nchunks; ++i)
        for (int s = 0; s < FH_SECTIONS; ++s) sec_release(&fw->chunks[i].sec[s]);
    slab_table_free(fw->chunks, fw->capchunks, sizeof *fw->chunks);
    fw->chunks = NULL;
    fw->nchunks = fw->capchunks = 0;
}

static fh_chunk *chunk_find(fh_world *fw, int cx, int cz)
{
    for (int i = 0; i < fw->nchunks; ++i)
    {
        if (fw->chunks[i].used && fw->chunks[i].cx == cx && fw->chunks[i].cz == cz) return &fw->chunks[i];
    }

    return NULL;
}

/* Chunk.addEntity: the y section list takes the entity in insertion order. */
static void chunk_add(fh_world *fw, fh_ent *en, int cx, int cy, int cz)
{
    fw->chunks = slab_table_room(fw->chunks, fw->nchunks, &fw->capchunks, sizeof *fw->chunks);

    fh_chunk *c = chunk_find(fw, cx, cz);

    if (!c)
    {
        c = &fw->chunks[fw->nchunks++];
        /* the section lists are made as they fill (sec_push) */
        memset(c, 0, sizeof *c);
        c->cx = cx;
        c->cz = cz;
        c->used = 1;
    }

    if (cy < 0) cy = 0;
    if (cy >= FH_SECTIONS) cy = FH_SECTIONS - 1;

    en->e.chunk_stamp = ++entity_chunk_stamp;
    en->added_to_chunk = 1;
    en->chunk_x = cx;
    en->chunk_y = cy;
    en->chunk_z = cz;

    sec_push(&c->sec[cy], fh_ent_index(en), FH_MAX_ENTITIES);
}

/* Chunk.removeEntityAtIndex: first occurrence out of the y section. */
static void chunk_remove_at(fh_world *fw, fh_ent *en, int cy)
{
    if (cy < 0) cy = 0;
    if (cy >= FH_SECTIONS) cy = FH_SECTIONS - 1;

    fh_chunk *c = chunk_find(fw, en->chunk_x, en->chunk_z);

    if (!c) return;

    sec_remove_first(&c->sec[cy], fh_ent_index(en));
}

/* ------------------------------------------------------------- the entities */

/* EntityFallingBlock.fall, through Entity.moveEntity's updateFallState. */
static void falling_fall(void *self, float dist);

/* The Entity constructor: the per-role ID, the per-entity Random from
 * Det.newRandom (one seeder draw) and the UUID (two more). */
static void entity_common(fh_world *fw, fh_ent *en)
{
    entity_init(&en->e, fw->w);
    en->e.self = en;
    en->e.fall = NULL;
    en->entity_id = det_next_entity_id_role(fw->det, fw->role);
    en->rand = det_new_random_role(fw->det, fw->role);
    det_uuid_role(fw->det, fw->role, &en->uuid_msb, &en->uuid_lsb);
    en->fw = fw;
    en->spawn_index = -1; /* assigned when the run's flush reaches it */
}

/* The EntityFallingBlock(World, x, y, z, block, meta) constructor. */
fh_ent *fh_spawn_falling(fh_world *fw, double x, double y, double z, int block, int meta)
{
    fh_ent *en = fh_ent_alloc();
    entity_common(fw, en);
    en->kind = FH_FALLING;
    en->block = block;
    en->meta = meta;
    en->drop_item = 1;      /* field_145813_c */
    en->hurt_amount = 2.0F; /* field_145816_i */
    en->hurt_max = 40;      /* field_145815_h */
    en->e.can_trigger_walking = 0;
    en->e.prevent_entity_spawning = 1;
    en->e.fall = falling_fall;
    entity_set_size(&en->e, 0.98F, 0.98F);
    en->e.y_offset = en->e.height / 2.0F;
    entity_set_position(&en->e, x, y, z);
    en->e.motion_x = 0.0;
    en->e.motion_y = 0.0;
    en->e.motion_z = 0.0;

    return en;
}

/* The tape replay's version: the same constructor with the id, the Random
 * state and the UUID the tick's recorder already spent. */
fh_ent *fh_adopt_falling(fh_world *fw, int entity_id, int64_t uuid_msb, int64_t uuid_lsb,
                         uint64_t rand_state, double x, double y, double z, int block, int meta)
{
    fh_ent *en = fh_ent_alloc();
    entity_init(&en->e, fw->w);
    en->e.self = en;
    en->entity_id = entity_id;
    en->rand.r.seed = rand_state;
    en->uuid_msb = uuid_msb;
    en->uuid_lsb = uuid_lsb;
    en->fw = fw;
    en->spawn_index = -1;
    en->kind = FH_FALLING;
    en->block = block;
    en->meta = meta;
    en->drop_item = 1;
    en->hurt_amount = 2.0F;
    en->hurt_max = 40;
    /* the tick's func_149830_m then BlockAnvil.func_149829_a */
    if (block == ID_ANVIL) en->hurt_entities = 1;
    en->e.can_trigger_walking = 0;
    en->e.prevent_entity_spawning = 1;
    en->e.fall = falling_fall;
    entity_set_size(&en->e, 0.98F, 0.98F);
    en->e.y_offset = en->e.height / 2.0F;
    entity_set_position(&en->e, x, y, z);

    return en;
}

/* The scalars out of a parsed canonical tree: nbt_render prints the typed
 * prefix ("d:<16hex>", "f:<8hex>", "i:-3", ...), which is the one accessor
 * that works on every scalar kind. */
static double nbt_scalar_double(const nbt *v)
{
    char *text = nbt_render(v);
    uint64_t bits = strtoull(strchr(text, ':') + 1, NULL, 16);
    double d;
    memcpy(&d, &bits, 8);
    free(text);
    return d;
}

static float nbt_scalar_float(const nbt *v)
{
    char *text = nbt_render(v);
    uint32_t bits = (uint32_t)strtoul(strchr(text, ':') + 1, NULL, 16);
    float f;
    memcpy(&f, &bits, 4);
    free(text);
    return f;
}

static long nbt_scalar_int(const nbt *v)
{
    char *text = nbt_render(v);
    long out = strtol(strchr(text, ':') + 1, NULL, 10);
    free(text);
    return out;
}

fh_ent *fh_spawn_falling_nbt(fh_world *fw, const nbt *tag)
{
    fh_ent *en = fh_ent_alloc();
    entity_common(fw, en);
    en->kind = FH_FALLING;
    en->drop_item = 1;
    en->hurt_amount = 2.0F;
    en->hurt_max = 40;
    en->e.can_trigger_walking = 0;
    en->e.fall = falling_fall;

    const nbt *l = nbt_get(tag, "Motion");

    if (l && nbt_kind(l) == NBT_LIST && nbt_list_size(l) == 3)
    {
        double mx = nbt_scalar_double(nbt_list_get(l, 0));
        double my = nbt_scalar_double(nbt_list_get(l, 1));
        double mz = nbt_scalar_double(nbt_list_get(l, 2));

        if (mx > 10.0 || mx < -10.0) mx = 0.0;
        if (my > 10.0 || my < -10.0) my = 0.0;
        if (mz > 10.0 || mz < -10.0) mz = 0.0;

        en->e.motion_x = mx;
        en->e.motion_y = my;
        en->e.motion_z = mz;
    }

    l = nbt_get(tag, "Pos");

    if (l && nbt_kind(l) == NBT_LIST && nbt_list_size(l) == 3)
    {
        double px = nbt_scalar_double(nbt_list_get(l, 0));
        double py = nbt_scalar_double(nbt_list_get(l, 1));
        double pz = nbt_scalar_double(nbt_list_get(l, 2));

        en->e.pos_x = px;
        en->e.pos_y = py;
        en->e.pos_z = pz;
    }

    l = nbt_get(tag, "Rotation");

    if (l && nbt_kind(l) == NBT_LIST && nbt_list_size(l) == 2)
    {
        en->rotation_yaw = nbt_scalar_float(nbt_list_get(l, 0));
        en->rotation_pitch = nbt_scalar_float(nbt_list_get(l, 1));
    }

    const nbt *v = nbt_get(tag, "FallDistance");

    if (v && nbt_kind(v) == NBT_FLOAT)
    {
        en->e.fall_distance = nbt_scalar_float(v);
    }

    v = nbt_get(tag, "Fire");

    if (v && nbt_kind(v) == NBT_SHORT) en->e.fire = (int16_t)nbt_scalar_int(v);
    v = nbt_get(tag, "Air");

    if (v && nbt_kind(v) == NBT_SHORT) { /* getAir/setAir, nothing the tick reads */ }

    v = nbt_get(tag, "OnGround");

    if (v && nbt_kind(v) == NBT_BYTE) en->e.on_ground = nbt_scalar_int(v) != 0;

    /* setPosition(pos), then the subclass read, then setPosition again
     * (shouldSetPosAfterLoading is true): the box follows the position */
    entity_set_position(&en->e, en->e.pos_x, en->e.pos_y, en->e.pos_z);
    en->e.bounding_box = aabb_offset(en->e.bounding_box, 0, 0, 0);

    /* readEntityFromNBT */
    v = nbt_get(tag, "TileID");

    if (v && nbt_kind(v) == NBT_INT)
    {
        en->block = nbt_scalar_int(v);
    }
    else
    {
        v = nbt_get(tag, "Tile");
        en->block = v && nbt_kind(v) == NBT_BYTE ? (nbt_scalar_int(v) & 255) : 0;
    }

    v = nbt_get(tag, "Data");

    if (v && nbt_kind(v) == NBT_BYTE) en->meta = nbt_scalar_int(v) & 255;
    v = nbt_get(tag, "Time");

    if (v && nbt_kind(v) == NBT_BYTE) en->time = nbt_scalar_int(v) & 255;

    v = nbt_get(tag, "HurtEntities");

    if (v && nbt_kind(v) == NBT_BYTE)
    {
        en->hurt_entities = nbt_scalar_int(v) != 0;
        v = nbt_get(tag, "FallHurtAmount");

        if (v && nbt_kind(v) == NBT_FLOAT) en->hurt_amount = nbt_scalar_float(v);

        v = nbt_get(tag, "FallHurtMax");

        if (v && nbt_kind(v) == NBT_INT) en->hurt_max = nbt_scalar_int(v);
    }
    else if (en->block == ID_ANVIL)
    {
        en->hurt_entities = 1;
    }

    v = nbt_get(tag, "DropItem");

    if (v && nbt_kind(v) == NBT_BYTE) en->drop_item = nbt_scalar_int(v) != 0;

    v = nbt_get(tag, "TileEntityData");

    if (v && nbt_kind(v) == NBT_COMPOUND)
    {
        char *text = nbt_render(v);
        en->tile_entity_data = nbt_parse(text);
        free(text);
    }

    if (BLOCKS[en->block].material == 0 /* air */) en->block = ID_SAND;

    entity_set_position(&en->e, en->e.pos_x, en->e.pos_y, en->e.pos_z);

    return en;
}

/* EntityHanging's constructor: yOffset 0, size 0.5, then setDirection. */
static fh_ent *spawn_hanging(fh_world *fw, int kind, int tile_x, int tile_y, int tile_z, int dir)
{
    fh_ent *en = fh_ent_alloc();
    entity_common(fw, en);
    en->kind = kind;
    en->tile_x = tile_x;
    en->tile_y = tile_y;
    en->tile_z = tile_z;
    en->e.can_trigger_walking = 0;
    en->e.y_offset = 0.0F;
    entity_set_size(&en->e, 0.5F, 0.5F);
    fh_set_direction(en, dir);
    return en;
}

fh_ent *fh_spawn_painting(fh_world *fw, int tile_x, int tile_y, int tile_z, int dir)
{
    fh_ent *en = spawn_hanging(fw, FH_PAINTING, tile_x, tile_y, tile_z, dir);

    /* the art loop: every art in declaration order, the ones whose box
     * holds on this wall are candidates, the entity's Random picks */
    int candidates[26];
    int n = 0;

    for (int i = 0; i < 26; ++i)
    {
        en->art = i;
        fh_set_direction(en, dir);

        if (fh_valid_surface(fw, en)) candidates[n++] = i;
    }

    if (n > 0) en->art = candidates[det_rng_int_n(&en->rand, n)];

    fh_set_direction(en, dir);

    return en;
}

fh_ent *fh_spawn_knot(fh_world *fw, int tile_x, int tile_y, int tile_z)
{
    fh_ent *en = fh_ent_alloc();
    entity_common(fw, en);
    en->kind = FH_KNOT;
    en->tile_x = tile_x;
    en->tile_y = tile_y;
    en->tile_z = tile_z;
    en->e.can_trigger_walking = 0;
    en->e.y_offset = 0.0F;
    entity_set_size(&en->e, 0.5F, 0.5F);
    entity_set_position(&en->e, (double)tile_x + 0.5, (double)tile_y + 0.5, (double)tile_z + 0.5);
    return en;
}

fh_ent *fh_spawn_hanging_nbt(fh_world *fw, int kind, const nbt *tag)
{
    /* EntityHanging.readEntityFromNBT: Direction, or the old Dir mapped */
    int dir;
    if (nbt_get(tag, "Direction") != NULL) dir = (int)nbt_int_value(nbt_get(tag, "Direction"));
    else
    {
        static const int from_dir[4] = {2, 1, 0, 3};
        dir = from_dir[(int)nbt_int_value(nbt_get(tag, "Dir")) & 3];
    }

    fh_ent *en = spawn_hanging(fw, kind, (int)nbt_int_value(nbt_get(tag, "TileX")),
                               (int)nbt_int_value(nbt_get(tag, "TileY")),
                               (int)nbt_int_value(nbt_get(tag, "TileZ")), dir);

    if (kind == FH_PAINTING)
    {
        /* EntityPainting: the art by title, Kebab when none matches */
        const char *title = nbt_string_value(nbt_get(tag, "Motive"));
        en->art = 0;
        for (int i = 0; i < 26 && title; ++i)
            if (strcmp(ARTS[i].title, title) == 0) en->art = i;
    }
    else
    {
        /* EntityItemFrame: the displayed item and its rotation */
        const nbt *item = nbt_get(tag, "Item");
        if (item != NULL && nbt_get(item, "id") != NULL)
        {
            en->item = (int)nbt_int_value(nbt_get(item, "id"));
            en->item_damage = (int)nbt_int_value(nbt_get(item, "Damage"));
            en->item_tag = itag_from_item(item);
            en->rot = (int)nbt_int_value(nbt_get(tag, "ItemRotation"));
        }
    }

    fh_set_direction(en, dir);
    return en;
}

fh_ent *fh_spawn_frame(fh_world *fw, int tile_x, int tile_y, int tile_z, int dir)
{
    return spawn_hanging(fw, FH_FRAME, tile_x, tile_y, tile_z, dir);
}

/* EntityHanging.setDirection, in float arithmetic. */
void fh_set_direction(fh_ent *en, int dir)
{
    en->dir = dir;
    en->prev_rotation_yaw = en->rotation_yaw = (float)(dir * 90);

    const struct art *a = en->kind == FH_PAINTING ? &ARTS[en->art] : NULL;
    int width_px = en->kind == FH_PAINTING ? a->w : 9;
    int height_px = en->kind == FH_PAINTING ? a->h : 9;

    float var2 = (float)width_px;
    float var3 = (float)height_px;
    float var4 = (float)width_px;

    if (dir != 2 && dir != 0)
    {
        var2 = 0.5F;
    }
    else
    {
        var4 = 0.5F;
        en->rotation_yaw = en->prev_rotation_yaw = (float)(ROTATE_OPPOSITE[dir] * 90);
    }

    var2 /= 32.0F;
    var3 /= 32.0F;
    var4 /= 32.0F;
    float var5 = (float)en->tile_x + 0.5F;
    float var6 = (float)en->tile_y + 0.5F;
    float var7 = (float)en->tile_z + 0.5F;
    float var8 = 0.5625F;

    /* func_70517_b: the half-block shift for the 32 and 64 pixel arts */
    float center_w = (width_px == 32 || width_px == 64) ? 0.5F : 0.0F;
    float center_h = (height_px == 32 || height_px == 64) ? 0.5F : 0.0F;

    if (dir == 2) var7 -= var8;
    if (dir == 1) var5 -= var8;
    if (dir == 0) var7 += var8;
    if (dir == 3) var5 += var8;

    if (dir == 2) var5 -= center_w;
    if (dir == 1) var7 += center_w;
    if (dir == 0) var5 += center_w;
    if (dir == 3) var7 -= center_w;

    var6 += center_h;

    en->e.pos_x = (double)var5;
    en->e.pos_y = (double)var6;
    en->e.pos_z = (double)var7;

    float var9 = -0.03125F;
    en->e.bounding_box = aabb_make((double)(var5 - var2 - var9), (double)(var6 - var3 - var9),
                                   (double)(var7 - var4 - var9), (double)(var5 + var2 + var9),
                                   (double)(var6 + var3 + var9), (double)(var7 + var4 + var9));
}

/* World.getEntitiesWithinAABBExcludingEntity's chunk walk: the box grown by
 * 2 on x and z picks the chunks, cx outer and cz inner, each chunk's y
 * sections bottom up, each section's entities in insertion order. */
static int entities_in_box(fh_world *fw, struct aabb box, fh_ent *exclude, fh_ent **out, int max_out)
{
    int n = 0;
    int cx0 = mh_floor((box.min_x - 2.0) / 16.0);
    int cx1 = mh_floor((box.max_x + 2.0) / 16.0);
    int cz0 = mh_floor((box.min_z - 2.0) / 16.0);
    int cz1 = mh_floor((box.max_z + 2.0) / 16.0);

    for (int cx = cx0; cx <= cx1; ++cx)
    {
        for (int cz = cz0; cz <= cz1; ++cz)
        {
            if (!world_chunk_loaded(fw->w, cx, cz)) continue;

            fh_chunk *c = chunk_find(fw, cx, cz);

            if (!c) continue;

            int s0 = mh_floor((box.min_y - 2.0) / 16.0);
            int s1 = mh_floor((box.max_y + 2.0) / 16.0);

            if (s0 < 0) s0 = 0;
            if (s0 >= FH_SECTIONS) s0 = FH_SECTIONS - 1;
            if (s1 < 0) s1 = 0;
            if (s1 >= FH_SECTIONS) s1 = FH_SECTIONS - 1;

            for (int s = s0; s <= s1; ++s)
            {
                for (int i = 0; i < c->sec[s].n; ++i)
                {
                    fh_ent *en = fh_ent_at(sec_items(&c->sec[s])[i]);

                    if (en != exclude && aabb_intersects(&en->e.bounding_box, &box))
                    {
                        if (n >= max_out) list_full("falling and hanging entities in one query", max_out);
                        out[n++] = en;
                    }
                }
            }
        }
    }

    return n;
}

int fh_entities_in_box(fh_world *fw, struct aabb box, fh_ent **out, int max_out)
{
    return entities_in_box(fw, box, NULL, out, max_out);
}

/* EntityHanging.onValidSurface. */
int fh_valid_surface(fh_world *fw, fh_ent *en)
{
    /* EntityLeashKnot.onValidSurface: a fence (render type 11) at the tile */
    if (en->kind == FH_KNOT)
        return BLOCKS[world_get_block(fw->w, en->tile_x, en->tile_y, en->tile_z) & 4095].render_type == 11;

    struct collide_list *boxes COLLIDE_SCRATCH = collide_scratch_begin();
    collide_list_clear(boxes);
    world_get_colliding_bounding_boxes(fw->w, en->e.bounding_box, boxes);

    if (boxes->n != 0) return 0;

    const struct art *a = en->kind == FH_PAINTING ? &ARTS[en->art] : NULL;
    int width_px = en->kind == FH_PAINTING ? a->w : 9;
    int height_px = en->kind == FH_PAINTING ? a->h : 9;

    int var1 = width_px / 16;
    if (var1 < 1) var1 = 1;
    int var2 = height_px / 16;
    if (var2 < 1) var2 = 1;

    int var3 = en->tile_x;
    int var5 = en->tile_z;

    if (en->dir == 2 || en->dir == 0)
        var3 = mh_floor(en->e.pos_x - (double)((float)width_px / 32.0F));
    else
        var5 = mh_floor(en->e.pos_z - (double)((float)width_px / 32.0F));

    int var4 = mh_floor(en->e.pos_y - (double)((float)height_px / 32.0F));

    for (int i = 0; i < var1; ++i)
    {
        for (int j = 0; j < var2; ++j)
        {
            int id;

            if (en->dir != 2 && en->dir != 0)
                id = world_get_block(fw->w, en->tile_x, var4 + j, var5 + i) & 4095;
            else
                id = world_get_block(fw->w, var3 + i, var4 + j, en->tile_z) & 4095;

            if (!MATERIALS[BLOCKS[id].material].is_solid) return 0;
        }
    }

    /* the entity query runs over the raw box, not the expanded one the
     * colliding-box scan uses */
    FH_QUERY_LIST(out);
    int n = entities_in_box(fw, en->e.bounding_box, en, out, FH_MAX_ENTITIES);

    for (int i = 0; i < n; ++i)
    {
        if (out[i]->kind == FH_PAINTING || out[i]->kind == FH_FRAME || out[i]->kind == FH_KNOT) return 0;
    }

    if (fw->other_hanging != NULL && fw->other_hanging(fw->other_ctx, &en->e.bounding_box)) return 0;

    return 1;
}

/* Chunk.addEntity from a chunk reload, whether or not the chunk is in the
 * world yet (AnvilChunkLoader.readChunkFromNBT adds before the provider
 * inserts the chunk). */
void fh_add_to_chunk(fh_world *fw, fh_ent *en)
{
    if (en->added_to_chunk) return;
    chunk_add(fw, en, mh_floor(en->e.pos_x / 16.0), mh_floor(en->e.pos_y / 16.0),
              mh_floor(en->e.pos_z / 16.0));
}

void fh_chunk_leave(fh_world *fw, fh_ent *en)
{
    if (en->added_to_chunk && world_chunk_loaded(fw->w, en->chunk_x, en->chunk_z)) chunk_remove_at(fw, en, en->chunk_y);
    en->added_to_chunk = 0;
}

void fh_added_to_world(fh_world *fw, fh_ent *en)
{
    int cx = mh_floor(en->e.pos_x / 16.0);
    int cz = mh_floor(en->e.pos_z / 16.0);

    if (!en->added_to_chunk && world_chunk_loaded(fw->w, cx, cz))
    {
        chunk_add(fw, en, cx, mh_floor(en->e.pos_y / 16.0), cz);
    }

    fh_list_push(fw, en);
}

fh_ent *fh_place_hanging(fh_world *fw, int kind, int tile_x, int tile_y, int tile_z,
                         int dir, int item, int damage, int rot)
{
    fh_ent *en = kind == FH_PAINTING
        ? fh_spawn_painting(fw, tile_x, tile_y, tile_z, dir)
        : fh_spawn_frame(fw, tile_x, tile_y, tile_z, dir);

    /* ItemHangingEntity.onItemUse: the entity was constructed either way, so
     * the Det draws are spent even when the surface refuses it */
    if (fh_valid_surface(fw, en))
    {
        fh_added_to_world(fw, en);

        if (kind == FH_FRAME && item != 0)
        {
            en->item = item;
            en->item_damage = damage;
            en->rot = rot % 4;
        }

        return en;
    }

    nbt_free(en->tile_entity_data);
    fh_ent_release(en);
    return NULL;
}

/* ---------------------------------------------------------------- the ticks */

/* EntityHanging.onBroken, the Entity parameter always null on these paths. */
static void on_broken(fh_world *fw, fh_ent *en);

/* The item drop, Entity.entityDropItem over the EntityItem constructor: the
 * four Det.mathRandom draws, delay 10. The probe takes the entity back out
 * at once, so only the record is built here. */
static void drop_item(fh_world *fw, const fh_ent *from, int item, int damage, int tag)
{
    /* the dragon egg has no item: the ItemStack is never made */
    if (item == 0) return;

    fh_drop d;
    memset(&d, 0, sizeof d);
    d.entity_id = det_next_entity_id_role(fw->det, fw->role);
    det_rng rand = det_new_random_role(fw->det, fw->role);
    d.rand_state = det_rng_state(&rand);
    det_uuid_role(fw->det, fw->role, &d.uuid_msb, &d.uuid_lsb);

    d.hover = (float)(det_math_random_role(fw->det, fw->role) * 3.141592653589793 * 2.0);
    d.yaw = (float)(det_math_random_role(fw->det, fw->role) * 360.0);
    double m = det_math_random_role(fw->det, fw->role);
    d.motion_x = (double)(float)(m * 0.20000000298023224 - 0.10000000149011612);
    d.motion_y = 0.20000000298023224;
    m = det_math_random_role(fw->det, fw->role);
    d.motion_z = (double)(float)(m * 0.20000000298023224 - 0.10000000149011612);

    d.x = from->e.pos_x;
    d.y = from->e.pos_y;
    d.z = from->e.pos_z;
    d.item = item;
    d.damage = damage;
    d.count = 1;
    d.tag = tag;
    d.delay = 10;
    d.age = 0;

    if (nw_env->fallhang.env.drop) nw_env->fallhang.env.drop(nw_env->fallhang.env.ctx, &d);
}

/* EntityHanging.attackEntityFrom: the anvil's fall damage reaches it.
 * EntityItemFrame's override comes first: a frame that shows an item only
 * lets the item go (func_146065_b(source, false): no frame, the drop-chance
 * draw, the copy at offset 0) and empties (setDisplayedItem(null) keeps the
 * rotation byte); there is no isDead test on that branch. */
static void hanging_attacked(fh_world *fw, fh_ent *en)
{
    if (en->kind == FH_FRAME && en->item != 0)
    {
        (void)det_rng_float(&en->rand); /* itemDropChance is 1.0F */
        drop_item(fw, en, en->item, en->item_damage, en->item_tag);
        en->item = 0;
        en->item_damage = 0;
        en->item_tag = 0;
        return;
    }

    if (!en->is_dead)
    {
        en->is_dead = 1;
        en->e.velocity_changed = 1; /* setBeenAttacked */
        on_broken(fw, en);
    }
}

void fh_attacked(fh_world *fw, fh_ent *en)
{
    if (en->kind == FH_FALLING) en->e.velocity_changed = 1;
    else hanging_attacked(fw, en);
}

void fallhang_player_break(fh_ent *en)
{
    hanging_attacked(en->fw, en);
}

/* EntityItemFrame.interactFirst's server half: an empty frame takes a copy
 * of the held stack with size 1 (setDisplayedItem; the rotation byte stays)
 * and the held stack shrinks; a full frame turns a quarter
 * (setItemRotation(getRotation() + 1), mod 4). */
void fh_frame_interact(fh_ent *en, int item, int damage, int tag, int *count)
{
    if (en->item == 0)
    {
        if (*count > 0)
        {
            en->item = item;
            en->item_damage = damage;
            en->item_tag = tag;
            --*count;
        }
    }
    else en->rot = (en->rot + 1) % 4;
}

/* EntityFallingBlock.fall, through Entity.moveEntity's updateFallState. */
static void falling_fall(void *self, float dist)
{
    fh_ent *en = self;

    if (!en->hurt_entities) return;

    int var2 = ceiling_float_int(dist - 1.0F);

    if (var2 <= 0) return;

    fh_world *fw = en->fw;
    int anvil = en->block == ID_ANVIL;
    int dmg = mh_floor_float((float)var2 * en->hurt_amount);

    if (dmg > en->hurt_max) dmg = en->hurt_max;

    if (nw_env->fallhang.env.hurt != NULL) nw_env->fallhang.env.hurt(nw_env->fallhang.env.ctx, en, (float)dmg);
    else
    {
        FH_QUERY_LIST(out);
        int n = entities_in_box(fw, en->e.bounding_box, en, out, FH_MAX_ENTITIES);

        for (int i = 0; i < n; ++i) fh_attacked(fw, out[i]);
    }

    if (anvil && (double)det_rng_float(&en->rand) < 0.05000000074505806 + (double)var2 * 0.05)
    {
        int var8 = (en->meta >> 2) + 1;
        int var9 = en->meta & 3;

        if (var8 > 2) en->broke = 1;
        else en->meta = var9 | var8 << 2;
    }
}

/* EntityHanging.onBroken, the Entity parameter always null on these paths. */
static void on_broken(fh_world *fw, fh_ent *en)
{
    if (en->kind == FH_PAINTING)
    {
        drop_item(fw, en, ITEM_PAINTING, 0, 0);
    }
    else if (en->kind == FH_FRAME)
    {
        /* func_146065_b(null, true): the frame, then the displayed item
         * against the drop chance, whose draw is spent either way */
        drop_item(fw, en, ITEM_FRAME, 0, 0);

        if (en->item != 0)
        {
            (void)det_rng_float(&en->rand); /* itemDropChance is 1.0F */
            drop_item(fw, en, en->item, en->item_damage, en->item_tag);
        }
    }
}

/* The landing check, World.canPlaceEntityOnSide with force true: no entity
 * collision scan runs, the answer is the replaced material's
 * replaceability, with the anvil's circuits escape. */
static int can_place_fall(struct world *w, int block, int x, int y, int z)
{
    int here = world_get_block(w, x, y, z) & 4095;
    int mat = BLOCKS[here].material;

    /* the anvil replaces a circuits block (a torch under the landing cell) */
    if (MATERIALS[mat].name != NULL && strcmp(MATERIALS[mat].name, "circuits") == 0 && block == ID_ANVIL)
        return 1;

    return MATERIALS[mat].replaceable;
}

/* BlockEndPortal.onEntityCollidedWithBlock: travelToDimension(1) at once,
 * inside the move's block walk (the falling block neither rides nor is
 * ridden). */
static void fh_end_portal(void *self)
{
    fh_ent *en = self;
    if (en->is_dead || en->kind != FH_FALLING || en->fw == NULL || en->fw->portal_travel == NULL ||
        en->e.world->is_remote)
        return;
    en->fw->portal_travel(en->fw, en, 1);
}

/* EntityFallingBlock.onUpdate. */
static void falling_update(fh_world *fw, fh_ent *en)
{
    struct entity *e = &en->e;
    struct world *w = fw->w;

    if (BLOCKS[en->block].material == 0 /* air */)
    {
        en->is_dead = 1;
        return;
    }

    ++en->time;
    e->motion_y -= 0.03999999910593033;
    e->end_portal = fh_end_portal;
    entity_move(e, e->motion_x, e->motion_y, e->motion_z);
    /* a trip in the move: the rest runs in the destination (worldObj) */
    fw = en->fw;
    w = e->world;
    e->motion_x *= 0.9800000190734863;
    e->motion_y *= 0.9800000190734863;
    e->motion_z *= 0.9800000190734863;

    int x = mh_floor(e->pos_x);
    int y = mh_floor(e->pos_y);
    int z = mh_floor(e->pos_z);

    if (en->time == 1)
    {
        if ((world_get_block(w, x, y, z) & 4095) != en->block)
        {
            en->is_dead = 1;
            return;
        }

        world_set_block(w, x, y, z, 0, 0, 3); /* setBlockToAir */
    }

    if (e->on_ground)
    {
        e->motion_x *= 0.699999988079071;
        e->motion_z *= 0.699999988079071;
        e->motion_y *= -0.5;

        if ((world_get_block(w, x, y, z) & 4095) != ID_PISTON_EXTENSION)
        {
            en->is_dead = 1;

            if (!en->broke && can_place_fall(w, en->block, x, y, z)
                && !fall_through(w, x, y - 1, z)
                && world_set_block(w, x, y, z, en->block, en->meta, 3))
            {
                /* BlockFalling.func_149828_a: only the anvil adds the sound */
                if (en->block == ID_ANVIL && nw_env->fallhang.env.aux_sfx)
                    nw_env->fallhang.env.aux_sfx(nw_env->fallhang.env.ctx, 1022, x, y, z, 0);
                else if (en->block == ID_ANVIL) env_aux_sfx(w, 1022, x, y, z, 0);

                /* the TileEntityData path needs an ITileEntityProvider
                 * falling block; none exists in vanilla 1.7.10 */
            }
            else if (en->drop_item && !en->broke)
            {
                drop_item(fw, en, item_of_block(en->block), damage_dropped(en->block, en->meta), 0);
            }
        }
    }
    else if ((en->time > 100 && (y < 1 || y > 256)) || en->time > 600)
    {
        if (en->drop_item) drop_item(fw, en, item_of_block(en->block), damage_dropped(en->block, en->meta), 0);

        en->is_dead = 1;
    }
}

/* EntityHanging.onUpdate. */
static void hanging_update(fh_world *fw, fh_ent *en)
{
    int old = en->tick_counter1++;

    if (old == 100)
    {
        en->tick_counter1 = 0;

        if (!en->is_dead && !fh_valid_surface(fw, en))
        {
            en->is_dead = 1;
            on_broken(fw, en);
        }
    }
}

/* ------------------------------------------------- updateEntityWithOptionalForce */

/* World.updateEntityWithOptionalForce(e, true): the bookkeeping, the update
 * when the entity sits in a chunk, then the chunk membership. */
static void update_entity(fh_world *fw, fh_ent *en)
{
    struct entity *e = &en->e;
    struct world *w = fw->w;

    en->last_tick_x = e->pos_x;
    en->last_tick_y = e->pos_y;
    en->last_tick_z = e->pos_z;
    en->prev_rotation_yaw = en->rotation_yaw;

    if (en->added_to_chunk)
    {
        ++en->ticks_existed;

        if (en->kind == FH_FALLING) falling_update(fw, en);
        else hanging_update(fw, en);
    }

    if (e->pos_x != e->pos_x || e->pos_x == 1.0 / 0.0 || e->pos_x == -1.0 / 0.0) e->pos_x = en->last_tick_x;
    if (e->pos_y != e->pos_y || e->pos_y == 1.0 / 0.0 || e->pos_y == -1.0 / 0.0) e->pos_y = en->last_tick_y;
    if (e->pos_z != e->pos_z || e->pos_z == 1.0 / 0.0 || e->pos_z == -1.0 / 0.0) e->pos_z = en->last_tick_z;

    int var6 = mh_floor(e->pos_x / 16.0);
    int var7 = mh_floor(e->pos_y / 16.0);
    int var8 = mh_floor(e->pos_z / 16.0);

    if (!en->added_to_chunk || en->chunk_x != var6 || en->chunk_y != var7 || en->chunk_z != var8)
    {
        if (en->added_to_chunk && world_chunk_loaded(w, en->chunk_x, en->chunk_z))
        {
            chunk_remove_at(fw, en, en->chunk_y);
        }

        if (world_chunk_loaded(w, var6, var8)) chunk_add(fw, en, var6, var7, var8);
        else en->added_to_chunk = 0;
    }
}

/* Chunk.onChunkUnload over this pool, the same shape as ie_unload_chunk. */
int fh_unload_chunk(fh_world *fw, int cx, int cz, fh_ent **out, int cap)
{
    int n = 0;

    for (int i = 0; i < fw->n; ++i)
    {
        fh_ent *en = fh_ent_at(fw->slot[i]);

        if (!en->added_to_chunk || en->chunk_x != cx || en->chunk_z != cz) continue;

        chunk_remove_at(fw, en, en->chunk_y);
        en->added_to_chunk = 0;

        if (n < cap) out[n] = en;
        ++n;

        memmove(&fw->slot[i], &fw->slot[i + 1], (size_t)(fw->n - i - 1) * sizeof *fw->slot);
        --fw->n;
        --i;
    }

    return n;
}

/* World.updateEntities' entity pass for one entity of this pool. */
int fh_tick_one(fh_world *fw, fh_ent *en, int tick, fh_removal *out, int max_out, int *n_out)
{
    if (n_out) *n_out = 0;

    /* World.updateEntities: an entity killed outside its own update (a
     * player's attack, a projectile) is not updated, only removed */
    if (!en->is_dead)
    {
        update_entity(fw, en);
        if (fw->post_update != NULL) fw->post_update(fw);
    }

    if (!en->is_dead) return 0;

    if (en->added_to_chunk && world_chunk_loaded(fw->w, en->chunk_x, en->chunk_z))
    {
        chunk_remove_at(fw, en, en->chunk_y);
    }

    int reason;

    if (en->kind == FH_FALLING)
    {
        int y = mh_floor(en->e.pos_y);

        if (en->time > 100 && (y < 1 || y > 256)) reason = FH_REMOVAL_TIMEOUT;
        else if (en->time > 600) reason = FH_REMOVAL_TIMEOUT;
        else reason = FH_REMOVAL_LANDED;
    }
    else
    {
        reason = FH_REMOVAL_BROKEN;
    }

    if (out && *n_out < max_out)
    {
        fh_removal *r = &out[(*n_out)++];
        r->tick = tick;
        r->spawn_index = en->spawn_index;
        r->entity_id = en->entity_id;
        r->reason = reason;
        r->extra = en->kind == FH_FALLING ? en->time : en->tick_counter1;
        r->pos_y = en->e.pos_y;
    }

    int at = -1;

    for (int i = 0; i < fw->n; ++i)
        if (fh_ent_at(fw->slot[i]) == en) { at = i; break; }

    if (at >= 0)
    {
        memmove(&fw->slot[at], &fw->slot[at + 1], (size_t)(fw->n - at - 1) * sizeof *fw->slot);
        --fw->n;
    }

    nbt_free(en->tile_entity_data);
    fh_ent_release(en);
    return 1;
}

/* World.updateEntities' entity pass for one tick. */
void fh_tick(fh_world *fw, int tick, fh_removal *out, int max_out, int *n_out)
{
    if (n_out) *n_out = 0;

    for (int i = 0; i < fw->n; ++i)
    {
        fh_ent *en = fh_ent_at(fw->slot[i]);

        if (en->is_dead) continue;

        update_entity(fw, en);

        if (en->is_dead)
        {
            if (en->added_to_chunk && world_chunk_loaded(fw->w, en->chunk_x, en->chunk_z))
            {
                chunk_remove_at(fw, en, en->chunk_y);
            }

            int reason;

            if (en->kind == FH_FALLING)
            {
                int y = mh_floor(en->e.pos_y);

                if (en->time > 100 && (y < 1 || y > 256)) reason = FH_REMOVAL_TIMEOUT;
                else if (en->time > 600) reason = FH_REMOVAL_TIMEOUT;
                else reason = FH_REMOVAL_LANDED;
            }
            else
            {
                reason = FH_REMOVAL_BROKEN;
            }

            if (out && *n_out < max_out)
            {
                fh_removal *r = &out[(*n_out)++];
                r->tick = tick;
                r->spawn_index = en->spawn_index;
                r->entity_id = en->entity_id;
                r->reason = reason;
                r->extra = en->kind == FH_FALLING ? en->time : en->tick_counter1;
                r->pos_y = en->e.pos_y;
            }

            nbt_free(en->tile_entity_data);
            fh_ent_release(en);
            memmove(&fw->slot[i], &fw->slot[i + 1], (size_t)(fw->n - i - 1) * sizeof *fw->slot);
            --fw->n;
            --i;
        }
    }
}

/* BlockFalling.func_149830_m and BlockDragonEgg.func_150018_e, the
 * updateTick bodies ticks_tick_updates dispatches here. */
void fallhang_update_tick(fh_world *fw, int id, int x, int y, int z)
{
    struct world *w = fw->w;
    int egg = id == ID_DRAGON_EGG;

    if (!fall_through(w, x, y - 1, z) || y < 0) return;

    if (!fall_instantly && world_check_chunks_exist(w, x - 32, y - 32, z - 32, x + 32, y + 32, z + 32))
    {
        fh_ent *en = fh_spawn_falling(fw, (double)((float)x + 0.5F), (double)((float)y + 0.5F),
                                      (double)((float)z + 0.5F), id, egg ? 0 : world_get_meta(w, x, y, z));

        if (id == ID_ANVIL) en->hurt_entities = 1; /* BlockAnvil.func_149829_a */

        fh_added_to_world(fw, en);
    }
    else
    {
        world_set_block(w, x, y, z, 0, 0, 3);

        while (fall_through(w, x, y - 1, z) && y > 0) --y;

        if (y > 0)
        {
            if (egg) world_set_block(w, x, y, z, id, 0, 2);
            else world_set_block(w, x, y, z, id, 0, 3); /* setBlock(x, y, z, this) */
        }
    }
}

/* --------------------------------------------------------------- the NBT */

/* A deep copy of an NBT subtree through the canonical round trip. */
static nbt *nbt_copy(const nbt *v)
{
    char *text = nbt_render(v);
    nbt *out = nbt_parse(text);
    free(text);
    return out;
}

/* Entity.writeToNBT, the canonical NBT the probe records: the base fields come
 * from entity_nbt.c in Java's insertion order, which the binary writer that
 * hashes an entity needs. */
void fh_write_w(struct nbtw *w, const fh_ent *en)
{
    ent_w_base(w, &en->e, en->e.world ? en->e.world->dim : 0, 300, 0, en->uuid_msb, en->uuid_lsb,
               en->rotation_yaw, 0.0F);

    if (en->kind == FH_FALLING)
    {
        nbtw_byte(w, "Tile", (signed char)en->block);
        nbtw_int(w, "TileID", en->block);
        nbtw_byte(w, "Data", (signed char)en->meta);
        nbtw_byte(w, "Time", (signed char)en->time);
        nbtw_byte(w, "DropItem", en->drop_item ? 1 : 0);
        nbtw_byte(w, "HurtEntities", en->hurt_entities ? 1 : 0);
        nbtw_float(w, "FallHurtAmount", en->hurt_amount);
        nbtw_int(w, "FallHurtMax", en->hurt_max);

        if (en->tile_entity_data)
        {
            /* the copy is what goes in the tree, and the canonical round
             * trip sorts its keys: the binary output writes a copy too */
            if (nbtw_is_bin(w))
            {
                /* in the scratch region, as itag_w's (itemtag.c) */
                nbt_scratch_begin();
                nbt *c = nbt_copy(en->tile_entity_data);
                nbtw_put_tree(w, "TileEntityData", c, nbt_copy);
                nbt_scratch_end();
            }
            else nbtw_put_tree(w, "TileEntityData", en->tile_entity_data, nbt_copy);
        }
    }
    else if (en->kind == FH_KNOT)
    {
        /* EntityLeashKnot.writeEntityToNBT is empty */
    }
    else
    {
        nbtw_byte(w, "Direction", en->dir & 255);
        nbtw_int(w, "TileX", en->tile_x);
        nbtw_int(w, "TileY", en->tile_y);
        nbtw_int(w, "TileZ", en->tile_z);

        static const int DIR_BACK[4] = {2, 1, 0, 3};
        nbtw_byte(w, "Dir", DIR_BACK[en->dir] & 255);

        if (en->kind == FH_PAINTING)
        {
            nbtw_string(w, "Motive", ARTS[en->art].title);
        }
        else if (en->item != 0)
        {
            nbtw_comp(w, "Item");
            /* ItemStack.writeToNBT's order */
            nbtw_short(w, "id", en->item);
            nbtw_byte(w, "Count", 1);
            nbtw_short(w, "Damage", en->item_damage);
            itag_w(w, en->item_tag);
            nbtw_end(w);
            nbtw_byte(w, "ItemRotation", en->rot & 255);
            nbtw_float(w, "ItemDropChance", 1.0F);
        }
    }
}

nbt *fh_write_nbt(const fh_ent *en)
{
    nbt *t = nbt_new_compound();
    struct nbtw w;
    nbtw_tree(&w, t);
    fh_write_w(&w, en);
    return t;
}
