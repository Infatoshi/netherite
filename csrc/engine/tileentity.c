/* The tile entity store: Chunk's chunkTileEntityMap (a per-chunk array of
 * pointers, in creation order), the demand creation Chunk.func_150806_e runs,
 * and the canonical NBT each class's writeToNBT / readFromNBT round-trips.
 * See tileentity.h. */
#include "tileentity.h"
#include "env.h"
#include "itemtag.h"
#include "tileticks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The MobSpawnerBaseLogic defaults a fresh spawner starts with. */
#define SPAWNER_DEFAULT_MOB "Pig"
#define SPAWNER_DELAY 20
#define SPAWNER_MIN_DELAY 200
#define SPAWNER_MAX_DELAY 800
#define SPAWNER_COUNT 4
#define SPAWNER_MAX_NEARBY 6
#define SPAWNER_PLAYER_RANGE 16
#define SPAWNER_RANGE 4

/* TileEntityChest / TileEntityMobSpawner / TileEntityFurnace /
 * TileEntityDispenser / TileEntityFlowerPot as their registry names. */
#define CHEST_ID "Chest"
#define SPAWNER_ID "MobSpawner"
#define FURNACE_ID "Furnace"
#define DISPENSER_ID "Trap"
#define FLOWER_POT_ID "FlowerPot"

static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

/* A canonical "str:" value into an owned malloc'd string, without the prefix. */
static char *dup_nbt_str(const nbt *v)
{
    if (v == NULL || nbt_kind(v) != NBT_STRING) return NULL;
    return dup_str(nbt_string_value(v));
}

/* ------------------------------------------------------------ the stacks */

static void stack_clear(struct te_stack *s)
{
    s->item = -1;
    s->count = 0;
    s->damage = 0;
    s->tag = 0;
}

static void stack_load(struct te_stack *s, const nbt *tag)
{
    stack_clear(s);

    const nbt *v = nbt_get(tag, "id");
    if (v == NULL || nbt_kind(v) != NBT_SHORT) return;

    char *text = NULL;
    /* the id's scalar text, without the "s:" prefix */
    text = nbt_render(v);
    s->item = atoi(text + 3);
    free(text);

    v = nbt_get(tag, "Count");
    if (v && nbt_kind(v) == NBT_BYTE)
    {
        text = nbt_render(v);
        s->count = atoi(text + 3);
        free(text);
    }

    v = nbt_get(tag, "Damage");
    if (v && nbt_kind(v) == NBT_SHORT)
    {
        text = nbt_render(v);
        s->damage = atoi(text + 3);
        free(text);
    }

    /* ItemStack.readFromNBT's hasKey("tag", 10) */
    s->tag = itag_from_item(tag);
}

/* One ItemStack as ItemStack.writeToNBT writes it, plus the Slot the
 * inventory stores it under. */
static void put_stack(nbt *list, const struct te_stack *s, int slot)
{
    nbt *e = nbt_new_compound();

    nbt_put(e, "Slot", nbt_new_byte(slot));
    nbt_put(e, "id", nbt_new_short(s->item));
    nbt_put(e, "Count", nbt_new_byte(s->count));
    nbt_put(e, "Damage", nbt_new_short(s->damage));

    itag_put(e, s->tag);

    nbt_list_add(list, e);
}

/* An inventory's Items list off the parsed NBT, into slots (n of them):
 * the Slot byte picks the slot, as readFromNBT does. */
static void load_items(struct te_stack *slots, int n, const nbt *comp)
{
    const nbt *list = nbt_get(comp, "Items");

    if (list == NULL || nbt_kind(list) != NBT_LIST) return;

    int count = nbt_list_size(list);

    for (int i = 0; i < count; ++i)
    {
        const nbt *e = nbt_list_get(list, i);
        const nbt *v = nbt_get(e, "Slot");

        if (v == NULL || nbt_kind(v) != NBT_BYTE) continue;

        char *text = nbt_render(v);
        int slot = atoi(text + 3);
        free(text);

        if (slot < 0 || slot >= n) continue;

        stack_load(&slots[slot], e);
    }
}

/* The Items list into out, in slot order (TileEntityChest.writeToNBT and
 * TileEntityDispenser.writeToNBT: the same list over the inventory's own slot
 * count, always written even when empty). */
static void render_items(nbt *root, const struct te_stack *slots, int n, const char *key)
{
    nbt *items = nbt_new_list();

    for (int i = 0; i < n; ++i)
        if (slots[i].item >= 0) put_stack(items, &slots[i], i);

    nbt_put(root, key, items);
}

/* ------------------------------------------------------------ the fields */

static int nbt_int(const nbt *comp, const char *key, int def)
{
    const nbt *v = nbt_get(comp, key);

    if (v == NULL) return def;

    char *text = nbt_render(v);
    int r = def;

    if (nbt_kind(v) == NBT_BYTE) r = (signed char)atoi(text + 3);
    else if (nbt_kind(v) == NBT_SHORT) r = (short)atoi(text + 3);
    else if (nbt_kind(v) == NBT_INT) r = (int)strtol(text + 3, NULL, 10);
    else if (nbt_kind(v) == NBT_LONG) r = (int)strtol(text + 3, NULL, 10);

    free(text);
    return r;
}

static int nbt_bool(const nbt *comp, const char *key)
{
    return nbt_int(comp, key, 0) != 0;
}

static float nbt_float(const nbt *comp, const char *key)
{
    const nbt *v = nbt_get(comp, key);

    if (v == NULL || nbt_kind(v) != NBT_FLOAT) return 0.0F;

    char *text = nbt_render(v);
    uint32_t bits = (uint32_t)strtoul(text + 3, NULL, 16);
    float f;
    memcpy(&f, &bits, sizeof f);
    free(text);
    return f;
}

/* ------------------------------------------------- creation and the store */

int te_kind_of_block(int id)
{
    switch (id & 4095)
    {
    case 61: /* furnace */
    case 62: /* lit_furnace */
        return TE_FURNACE;
    case 54: /* chest */
    case 146: /* trapped_chest */
        return TE_CHEST;
    case 130: /* ender_chest */
        return TE_ENDER_CHEST;
    case 52: /* mob_spawner */
        return TE_MOB_SPAWNER;
    case 25: /* noteblock */
        return TE_NOTE;
    case 151: /* daylight_detector */
        return TE_DAYLIGHT_DETECTOR;
    case 154: /* hopper */
        return TE_HOPPER;
    case 138: /* beacon */
        return TE_BEACON;
    case 117: /* brewing_stand */
        return TE_BREWING_STAND;
    case 116: /* enchanting_table */
        return TE_ENCHANT_TABLE;
    case 23: /* dispenser */
    case 158: /* dropper: TileEntityDropper extends TileEntityDispenser, so
               * the demand kind is the dispenser's; the NBT id comes from
               * the block */
        return TE_DISPENSER;
    case 140: /* flower_pot */
        return TE_FLOWER_POT;
    case 63: /* standing_sign */
    case 68: /* wall_sign */
        return TE_SIGN;
    case 144: /* skull */
        return TE_SKULL;
    case 149: /* unpowered_comparator */
    case 150: /* powered_comparator */
        return TE_COMPARATOR;
    case 137: /* command_block */
        return TE_COMMAND_BLOCK;
    case 84: /* jukebox */
        return TE_JUKEBOX;
    case 119: /* end_portal */
        return TE_END_PORTAL;
    default:
        return 0; /* BlockPistonMoving (36) and the rest create none here */
    }
}

struct tile_entity *te_new(int kind)
{
    struct tile_entity *te = calloc(1, sizeof *te);

    te->kind = kind;
    te->block_metadata = -1;

    if (kind == TE_FURNACE)
    {
        for (int i = 0; i < 3; ++i) te->u.furnace.slots[i].item = -1;
    }
    else if (kind == TE_CHEST || kind == TE_ENDER_CHEST)
    {
        int n = kind == TE_CHEST ? CHEST_SLOTS : 0;

        for (int i = 0; i < n; ++i) te->u.chest.slots[i].item = -1;
    }
    else if (kind == TE_DISPENSER)
    {
        for (int i = 0; i < DISPENSER_SLOTS; ++i) te->u.dispenser.slots[i].item = -1;

        if (te_dispenser_random != NULL) te_dispenser_random(te_dispenser_random_ctx);
    }
    else if (kind == TE_HOPPER)
    {
        for (int i = 0; i < 5; ++i) te->u.hopper.slots[i].item = -1;

        te->u.hopper.cooldown = -1;
    }
    else if (kind == TE_MOB_SPAWNER)
    {
        te->u.spawner.mob_id = dup_str(SPAWNER_DEFAULT_MOB);
        te->u.spawner.spawn_delay = SPAWNER_DELAY;
        te->u.spawner.desc_delay = SPAWNER_DELAY;
        te->u.spawner.min_spawn_delay = SPAWNER_MIN_DELAY;
        te->u.spawner.max_spawn_delay = SPAWNER_MAX_DELAY;
        te->u.spawner.spawn_count = SPAWNER_COUNT;
        te->u.spawner.max_nearby_entities = SPAWNER_MAX_NEARBY;
        te->u.spawner.required_player_range = SPAWNER_PLAYER_RANGE;
        te->u.spawner.spawn_range = SPAWNER_RANGE;
    }
    else if (kind == TE_BREWING_STAND)
    {
        for (int i = 0; i < 4; ++i) te->u.brewing.slots[i].item = -1;
    }
    else if (kind == TE_JUKEBOX)
    {
        te->u.jukebox.item = -1;   /* Java's field_145858_a is null */
    }
    else if (kind == TE_ENCHANT_TABLE)
    {
        /* the float fields start at 0, the constructor leaves them */
    }
    else if (kind == TE_BEACON)
    {
        /* TileEntityBeacon's fresh field_152885_a: the levels start at -1 */
        te->u.beacon.levels = -1;
    }

    return te;
}

void te_free(struct tile_entity *te)
{
    if (te == NULL) return;

    int slots = 0;
    struct te_stack *arr = NULL;

    if (te->kind == TE_FURNACE) { arr = te->u.furnace.slots; slots = 3; }
    else if (te->kind == TE_CHEST) { arr = te->u.chest.slots; slots = CHEST_SLOTS; }
    else if (te->kind == TE_HOPPER) { arr = te->u.hopper.slots; slots = 5; }
    else if (te->kind == TE_BREWING_STAND) { arr = te->u.brewing.slots; slots = 4; }
    else if (te->kind == TE_DISPENSER) { arr = te->u.dispenser.slots; slots = DISPENSER_SLOTS; }

    for (int i = 0; i < slots; ++i) stack_clear(&arr[i]);

    if (te->kind == TE_MOB_SPAWNER)
    {
        free(te->u.spawner.mob_id);
        te->u.spawner.mob_id = NULL;
    }

    free(te);
}

void te_store_free(struct te_store *s)
{
    /* the entities are the world's: only the array goes here */
    free(s->v);
    s->v = NULL;
    s->n = 0;
    s->cap = 0;
}

struct tile_entity *te_find(struct te_store *s, int x, int y, int z)
{
    for (int i = 0; i < s->n; ++i)
        if (s->v[i]->x == x && s->v[i]->y == y && s->v[i]->z == z) return s->v[i];

    return NULL;
}

/* te_put stores `te` under the position; whatever sat there leaves the store
 * WITHOUT being freed (the caller owns it now: the world path graves it or
 * puts it back). A fresh one comes from te_new. */
struct tile_entity *te_put(struct te_store *s, int x, int y, int z, int kind)
{
    struct tile_entity *old = te_find(s, x, y, z);
    int had = old != NULL;

    if (had)
    {
        for (int i = 0; i < s->n; ++i)
        {
            if (s->v[i] == old)
            {
                for (int j = i + 1; j < s->n; ++j) s->v[j - 1] = s->v[j];

                --s->n;
                break;
            }
        }
    }

    if (s->n == s->cap)
    {
        s->cap = s->cap ? s->cap * 2 : 4;
        s->v = realloc(s->v, (size_t)s->cap * sizeof *s->v);
    }

    struct tile_entity *te = te_new(kind);
    te->x = x;
    te->y = y;
    te->z = z;
    s->v[s->n++] = te;
    return te;
}

/* The entity leaves the store; the caller owns it (the world path graves it
 * or re-puts it). */
struct tile_entity *te_remove(struct te_store *s, int x, int y, int z)
{
    for (int i = 0; i < s->n; ++i)
    {
        if (s->v[i]->x != x || s->v[i]->y != y || s->v[i]->z != z) continue;

        struct tile_entity *te = s->v[i];

        for (int j = i + 1; j < s->n; ++j) s->v[j - 1] = s->v[j];

        --s->n;
        return te;
    }

    return NULL;
}

/* ---------------------------------------------------------------- the NBT */

void te_load(struct tile_entity *te, const nbt *tag)
{
    te->x = nbt_int(tag, "x", te->x);
    te->y = nbt_int(tag, "y", te->y);
    te->z = nbt_int(tag, "z", te->z);

    switch (te->kind)
    {
    case TE_FURNACE:
        load_items(te->u.furnace.slots, 3, tag);
        te->u.furnace.burn_time = nbt_int(tag, "BurnTime", 0);
        te->u.furnace.cook_time = nbt_int(tag, "CookTime", 0);
        /* readFromNBT: currentItemBurnTime is not saved but recomputed
         * from the fuel slot */
        te->u.furnace.fuel_total = te->u.furnace.slots[1].count > 0 ? furnace_fuel_value(te->u.furnace.slots[1].item) : 0;
        break;

    case TE_CHEST:
        load_items(te->u.chest.slots, CHEST_SLOTS, tag);
        break;

    case TE_DISPENSER:
        load_items(te->u.dispenser.slots, DISPENSER_SLOTS, tag);
        break;

    case TE_FLOWER_POT:
        te->u.pot.item = nbt_int(tag, "Item", -1);
        if (te->u.pot.item == 0) te->u.pot.item = -1;   /* getIdFromItem(null) */
        te->u.pot.data = nbt_int(tag, "Data", 0);
        break;

    case TE_ENDER_CHEST:
        break;

    case TE_SIGN:
        /* TileEntitySign.readFromNBT: Text1..Text4, each cut to its first
         * 15 chars (getString answers "" for a missing key) */
        for (int i = 0; i < 4; ++i)
        {
            char key[8];
            snprintf(key, sizeof key, "Text%d", i + 1);
            const nbt *v = nbt_get(tag, key);
            const char *s = v != NULL && nbt_kind(v) == NBT_STRING ? nbt_string_value(v) : "";
            size_t n = 0;
            int chars = 0;

            /* String.substring(0, 15) over UTF-16 units: one per code point
             * below U+10000, two above */
            while (s[n] != 0)
            {
                unsigned char c = (unsigned char)s[n];
                size_t len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
                int units = len == 4 ? 2 : 1;
                if (chars + units > 15 || n + len >= sizeof te->sign_text[i]) break;
                chars += units;
                n += len;
            }

            memcpy(te->sign_text[i], s, n);
            te->sign_text[i][n] = 0;
        }
        break;

    case TE_SKULL:
        /* TileEntitySkull.readFromNBT: getByte for the type and rotation (a
         * player head's Owner or ExtraType profile is not modelled) */
        te->skull_type = nbt_int(tag, "SkullType", 0);
        te->skull_rot = nbt_int(tag, "Rot", 0);
        break;

    case TE_HOPPER:
        load_items(te->u.hopper.slots, 5, tag);
        te->u.hopper.cooldown = nbt_int(tag, "TransferCooldown", 0);
        break;

    case TE_MOB_SPAWNER:
    {
        struct te_spawner *sp = &te->u.spawner;
        free(sp->mob_id);
        sp->mob_id = dup_nbt_str(nbt_get(tag, "EntityId"));
        if (sp->mob_id == NULL) sp->mob_id = dup_str(SPAWNER_DEFAULT_MOB);
        sp->spawn_delay = nbt_int(tag, "Delay", SPAWNER_DELAY);
        sp->desc_delay = sp->spawn_delay;
        sp->min_spawn_delay = nbt_int(tag, "MinSpawnDelay", SPAWNER_MIN_DELAY);
        sp->max_spawn_delay = nbt_int(tag, "MaxSpawnDelay", SPAWNER_MAX_DELAY);
        sp->spawn_count = nbt_int(tag, "SpawnCount", SPAWNER_COUNT);
        sp->max_nearby_entities = nbt_int(tag, "MaxNearbyEntities", SPAWNER_MAX_NEARBY);
        sp->required_player_range = nbt_int(tag, "RequiredPlayerRange", SPAWNER_PLAYER_RANGE);
        sp->spawn_range = nbt_int(tag, "SpawnRange", SPAWNER_RANGE);
        break;
    }

    case TE_NOTE:
        te->u.note.note = nbt_int(tag, "note", 0);
        break;

    case TE_BEACON:
        te->u.beacon.primary = nbt_int(tag, "Primary", 0);
        te->u.beacon.secondary = nbt_int(tag, "Secondary", 0);
        te->u.beacon.levels = nbt_int(tag, "Levels", 0);
        break;

    case TE_BREWING_STAND:
        load_items(te->u.brewing.slots, 4, tag);
        te->u.brewing.brew_time = nbt_int(tag, "BrewTime", 0);
        break;

    case TE_ENCHANT_TABLE:
        break;

    case TE_PISTON:
        te->u.piston.block_id = nbt_int(tag, "blockId", 0);
        te->u.piston.block_meta = nbt_int(tag, "blockData", 0);
        te->u.piston.facing = nbt_int(tag, "facing", 0);
        te->u.piston.progress = nbt_float(tag, "progress");
        te->u.piston.last_progress = te->u.piston.progress;
        te->u.piston.extending = nbt_bool(tag, "extending");
        break;

    case TE_JUKEBOX:
    {
        const nbt *rec = nbt_get(tag, "RecordItem");

        if (rec != NULL && nbt_kind(rec) == NBT_COMPOUND)
        {
            struct te_stack s;

            stack_load(&s, rec);
            te->u.jukebox.item = s.item;
            te->u.jukebox.damage = s.damage;
            te->u.jukebox.count = s.count;
            stack_clear(&s);
        }
        else
        {
            int record = nbt_int(tag, "Record", 0);

            if (record > 0)
            {
                te->u.jukebox.item = record;
                te->u.jukebox.count = 1;
                te->u.jukebox.damage = 0;
            }
            else
            {
                te->u.jukebox.item = -1;
                te->u.jukebox.count = 0;
                te->u.jukebox.damage = 0;
            }
        }

        break;
    }

    case TE_COMPARATOR:
        te->u.comparator.out_signal = nbt_int(tag, "OutputSignal", 0);
        break;
    }
}

static char *render_furnace(const struct tile_entity *te)
{
    nbt *root = nbt_new_compound();

    nbt_put(root, "BurnTime", nbt_new_short(te->u.furnace.burn_time));
    nbt_put(root, "CookTime", nbt_new_short(te->u.furnace.cook_time));
    render_items(root, te->u.furnace.slots, 3, "Items");
    nbt_put(root, "id", nbt_new_string("Furnace"));
    nbt_put(root, "x", nbt_new_int(te->x));
    nbt_put(root, "y", nbt_new_int(te->y));
    nbt_put(root, "z", nbt_new_int(te->z));

    char *out = nbt_render(root);
    nbt_free(root);
    return out;
}

static char *render_chest_like(const struct tile_entity *te, const char *id)
{
    nbt *root = nbt_new_compound();

    render_items(root, te->u.chest.slots, CHEST_SLOTS, "Items");
    nbt_put(root, "id", nbt_new_string(id));
    nbt_put(root, "x", nbt_new_int(te->x));
    nbt_put(root, "y", nbt_new_int(te->y));
    nbt_put(root, "z", nbt_new_int(te->z));

    char *out = nbt_render(root);
    nbt_free(root);
    return out;
}

static char *render_hopper(const struct tile_entity *te)
{
    nbt *root = nbt_new_compound();

    render_items(root, te->u.hopper.slots, 5, "Items");
    nbt_put(root, "TransferCooldown", nbt_new_int(te->u.hopper.cooldown));
    nbt_put(root, "id", nbt_new_string("Hopper"));
    nbt_put(root, "x", nbt_new_int(te->x));
    nbt_put(root, "y", nbt_new_int(te->y));
    nbt_put(root, "z", nbt_new_int(te->z));

    char *out = nbt_render(root);
    nbt_free(root);
    return out;
}

static char *render_spawner(const struct tile_entity *te)
{
    const struct te_spawner *sp = &te->u.spawner;
    nbt *root = nbt_new_compound();

    nbt_put(root, "Delay", nbt_new_short(sp->spawn_delay));
    nbt_put(root, "EntityId", nbt_new_string(sp->mob_id ? sp->mob_id : SPAWNER_DEFAULT_MOB));
    nbt_put(root, "MaxNearbyEntities", nbt_new_short(sp->max_nearby_entities));
    nbt_put(root, "MaxSpawnDelay", nbt_new_short(sp->max_spawn_delay));
    nbt_put(root, "MinSpawnDelay", nbt_new_short(sp->min_spawn_delay));
    nbt_put(root, "RequiredPlayerRange", nbt_new_short(sp->required_player_range));
    nbt_put(root, "SpawnCount", nbt_new_short(sp->spawn_count));
    nbt_put(root, "SpawnRange", nbt_new_short(sp->spawn_range));
    nbt_put(root, "id", nbt_new_string("MobSpawner"));
    nbt_put(root, "x", nbt_new_int(te->x));
    nbt_put(root, "y", nbt_new_int(te->y));
    nbt_put(root, "z", nbt_new_int(te->z));

    char *out = nbt_render(root);
    nbt_free(root);
    return out;
}

static char *render_note(const struct tile_entity *te)
{
    nbt *root = nbt_new_compound();

    nbt_put(root, "note", nbt_new_byte(te->u.note.note));
    nbt_put(root, "id", nbt_new_string("Music"));
    nbt_put(root, "x", nbt_new_int(te->x));
    nbt_put(root, "y", nbt_new_int(te->y));
    nbt_put(root, "z", nbt_new_int(te->z));

    char *out = nbt_render(root);
    nbt_free(root);
    return out;
}

static char *render_beacon(const struct tile_entity *te)
{
    nbt *root = nbt_new_compound();

    nbt_put(root, "Levels", nbt_new_int(te->u.beacon.levels));
    nbt_put(root, "Primary", nbt_new_int(te->u.beacon.primary));
    nbt_put(root, "Secondary", nbt_new_int(te->u.beacon.secondary));
    nbt_put(root, "id", nbt_new_string("Beacon"));
    nbt_put(root, "x", nbt_new_int(te->x));
    nbt_put(root, "y", nbt_new_int(te->y));
    nbt_put(root, "z", nbt_new_int(te->z));

    char *out = nbt_render(root);
    nbt_free(root);
    return out;
}

static char *render_brewing(const struct tile_entity *te)
{
    nbt *root = nbt_new_compound();

    nbt_put(root, "BrewTime", nbt_new_short(te->u.brewing.brew_time));
    render_items(root, te->u.brewing.slots, 4, "Items");
    nbt_put(root, "id", nbt_new_string("Cauldron"));
    nbt_put(root, "x", nbt_new_int(te->x));
    nbt_put(root, "y", nbt_new_int(te->y));
    nbt_put(root, "z", nbt_new_int(te->z));

    char *out = nbt_render(root);
    nbt_free(root);
    return out;
}

static char *render_plain(const struct tile_entity *te, const char *id)
{
    nbt *root = nbt_new_compound();

    nbt_put(root, "id", nbt_new_string(id));
    nbt_put(root, "x", nbt_new_int(te->x));
    nbt_put(root, "y", nbt_new_int(te->y));
    nbt_put(root, "z", nbt_new_int(te->z));

    char *out = nbt_render(root);
    nbt_free(root);
    return out;
}

/* BlockJukebox.TileEntityJukebox.writeToNBT: the RecordItem stack, then the
 * Record id it came from; an empty jukebox writes neither. */
static char *render_jukebox(const struct tile_entity *te)
{
    nbt *root = nbt_new_compound();

    if (te->u.jukebox.item > 0)
    {
        nbt *rec = nbt_new_compound();

        nbt_put(rec, "id", nbt_new_short(te->u.jukebox.item));
        nbt_put(rec, "Count", nbt_new_byte(te->u.jukebox.count));
        nbt_put(rec, "Damage", nbt_new_short(te->u.jukebox.damage));
        nbt_put(root, "RecordItem", rec);
        nbt_put(root, "Record", nbt_new_int(te->u.jukebox.item));
    }

    nbt_put(root, "id", nbt_new_string("RecordPlayer"));
    nbt_put(root, "x", nbt_new_int(te->x));
    nbt_put(root, "y", nbt_new_int(te->y));
    nbt_put(root, "z", nbt_new_int(te->z));

    char *out = nbt_render(root);
    nbt_free(root);
    return out;
}

/* TileEntityComparator.writeToNBT. */
static char *render_comparator(const struct tile_entity *te)
{
    nbt *root = nbt_new_compound();

    nbt_put(root, "OutputSignal", nbt_new_int(te->u.comparator.out_signal));
    nbt_put(root, "id", nbt_new_string("Comparator"));
    nbt_put(root, "x", nbt_new_int(te->x));
    nbt_put(root, "y", nbt_new_int(te->y));
    nbt_put(root, "z", nbt_new_int(te->z));

    char *out = nbt_render(root);
    nbt_free(root);
    return out;
}

static char *render_piston(const struct tile_entity *te)
{
    nbt *root = nbt_new_compound();

    nbt_put(root, "blockId", nbt_new_int(te->u.piston.block_id));
    nbt_put(root, "blockData", nbt_new_int(te->u.piston.block_meta));
    nbt_put(root, "extending", nbt_new_byte(te->u.piston.extending ? 1 : 0));
    nbt_put(root, "facing", nbt_new_int(te->u.piston.facing));
    nbt_put(root, "progress", nbt_new_float(te->u.piston.last_progress)); /* field_145870_n */
    nbt_put(root, "id", nbt_new_string("Piston"));
    nbt_put(root, "x", nbt_new_int(te->x));
    nbt_put(root, "y", nbt_new_int(te->y));
    nbt_put(root, "z", nbt_new_int(te->z));

    char *out = nbt_render(root);
    nbt_free(root);
    return out;
}

/* TileEntityChest.writeToNBT with a dispenser's id: the same Items list over
 * its own nine slots. */
/* The placement kinds' fresh writers: the fields the Java classes' writeToNBT
 * stores on top of TileEntity's own. */
static char *render_sign(const struct tile_entity *te)
{
    nbt *root = nbt_new_compound();

    nbt_put(root, "Text1", nbt_new_string(te->sign_text[0]));
    nbt_put(root, "Text2", nbt_new_string(te->sign_text[1]));
    nbt_put(root, "Text3", nbt_new_string(te->sign_text[2]));
    nbt_put(root, "Text4", nbt_new_string(te->sign_text[3]));
    nbt_put(root, "id", nbt_new_string("Sign"));
    nbt_put(root, "x", nbt_new_int(te->x));
    nbt_put(root, "y", nbt_new_int(te->y));
    nbt_put(root, "z", nbt_new_int(te->z));

    char *out = nbt_render(root);
    nbt_free(root);
    return out;
}

static char *render_skull(const struct tile_entity *te)
{
    nbt *root = nbt_new_compound();

    nbt_put(root, "SkullType", nbt_new_byte(te->skull_type & 255));
    nbt_put(root, "Rot", nbt_new_byte(te->skull_rot & 255));
    nbt_put(root, "id", nbt_new_string("Skull"));
    nbt_put(root, "x", nbt_new_int(te->x));
    nbt_put(root, "y", nbt_new_int(te->y));
    nbt_put(root, "z", nbt_new_int(te->z));

    char *out = nbt_render(root);
    nbt_free(root);
    return out;
}

static char *render_command_block(const struct tile_entity *te)
{
    nbt *root = nbt_new_compound();

    nbt_put(root, "Command", nbt_new_string(""));
    nbt_put(root, "CustomName", nbt_new_string("@"));
    nbt_put(root, "SuccessCount", nbt_new_int(0));
    nbt_put(root, "TrackOutput", nbt_new_byte(1));
    nbt_put(root, "id", nbt_new_string("Control"));
    nbt_put(root, "x", nbt_new_int(te->x));
    nbt_put(root, "y", nbt_new_int(te->y));
    nbt_put(root, "z", nbt_new_int(te->z));

    char *out = nbt_render(root);
    nbt_free(root);
    return out;
}

static char *render_dispenser(const struct tile_entity *te)
{
    nbt *root = nbt_new_compound();

    render_items(root, te->u.dispenser.slots, DISPENSER_SLOTS, "Items");
    nbt_put(root, "id", nbt_new_string(te->block == 158 ? "Dropper" : DISPENSER_ID));
    nbt_put(root, "x", nbt_new_int(te->x));
    nbt_put(root, "y", nbt_new_int(te->y));
    nbt_put(root, "z", nbt_new_int(te->z));

    char *out = nbt_render(root);

    nbt_free(root);
    return out;
}

/* TileEntityFlowerPot.writeToNBT: the Item and Data its createNewTileEntity
 * picked from the block's metadata. The internal -1 is vanilla's null field,
 * which getIdFromItem renders as 0. */
static char *render_flower_pot(const struct tile_entity *te)
{
    nbt *root = nbt_new_compound();

    nbt_put(root, "Data", nbt_new_int(te->u.pot.data));
    nbt_put(root, "Item", nbt_new_int(te->u.pot.item < 0 ? 0 : te->u.pot.item));
    nbt_put(root, "id", nbt_new_string(FLOWER_POT_ID));
    nbt_put(root, "x", nbt_new_int(te->x));
    nbt_put(root, "y", nbt_new_int(te->y));
    nbt_put(root, "z", nbt_new_int(te->z));

    char *out = nbt_render(root);

    nbt_free(root);
    return out;
}

/* A replayed structure entry: the oracle's canonical NBT, stored verbatim. */
void te_set_raw(struct tile_entity *te, int kind, const char *name, const char *text)
{
    (void)name;   /* the class only labels the record; the NBT carries it */
    te->kind = kind;
    free(te->raw);
    te->raw = dup_str(text);
}

char *te_render(const struct tile_entity *te)
{
    if (te->raw != NULL) return dup_str(te->raw);

    switch (te->kind)
    {
    case TE_FURNACE: return render_furnace(te);
    case TE_CHEST: return render_chest_like(te, "Chest");
    case TE_ENDER_CHEST: return render_plain(te, "EnderChest");
    case TE_HOPPER: return render_hopper(te);
    case TE_MOB_SPAWNER: return render_spawner(te);
    case TE_NOTE: return render_note(te);
    case TE_DAYLIGHT_DETECTOR: return render_plain(te, "DLDetector");
    case TE_BEACON: return render_beacon(te);
    case TE_BREWING_STAND: return render_brewing(te);
    case TE_ENCHANT_TABLE: return render_plain(te, "EnchantTable");
    case TE_PISTON: return render_piston(te);
    case TE_DISPENSER: return render_dispenser(te);
    case TE_FLOWER_POT: return render_flower_pot(te);
    case TE_SIGN: return render_sign(te);
    case TE_SKULL: return render_skull(te);
    case TE_COMPARATOR: return render_comparator(te);
    case TE_COMMAND_BLOCK: return render_command_block(te);
    case TE_JUKEBOX: return render_jukebox(te);
    case TE_END_PORTAL: return render_plain(te, "Airportal");
    default: return render_plain(te, "?");
    }
}
