/* The container blocks' breakBlock inventory spill (spill.h). */
#include "spill.h"
#include "env.h"

#include "tileentity.h"
#include "world.h"

/* The born states: Det.newRandom at bootstrap, from a snapshot taken before
 * any spill (det.nbt's furnace, chest, dispenser, hopper and brewing keys). */
static const uint64_t born[BR_N] = {
    215304245116296ULL, 60501207436205ULL,
    244685491946398ULL, 17744615949726ULL,
    200501147812931ULL, 41359080989754ULL,
    6681554875303ULL,
    32956670647041ULL,
};

const struct block_rand_group block_rand_groups[5] = {
    {"furnace", BR_FURNACE, 2},
    {"chest", BR_CHEST, 2},
    {"dispenser", BR_DISPENSER, 2},
    {"hopper", BR_HOPPER, 1},
    {"brewing", BR_BREWING_STAND, 1},
};

void block_rand_born(det_rng *r)
{
    for (int i = 0; i < BR_N; ++i)
    {
        r[i].r.seed = born[i];
        r[i].have_next_next_gaussian = 0;
        r[i].next_next_gaussian = 0.0;
    }
}

void block_rand_reset(void)
{
    block_rand_born(block_rand);
}

int block_rand_index(int block)
{
    switch (block & 4095)
    {
        case BLK_FURNACE: return BR_FURNACE;
        case BLK_LIT_FURNACE: return BR_LIT_FURNACE;
        case BLK_CHEST: return BR_CHEST;
        case BLK_TRAPPED_CHEST: return BR_TRAPPED_CHEST;
        case BLK_DISPENSER: return BR_DISPENSER;
        case 158: return BR_DROPPER;
        case BLK_HOPPER: return BR_HOPPER;
        case BLK_BREWING_STAND: return BR_BREWING_STAND;
        default: return -1;
    }
}

/* The tile entity's slots, when it is the kind the block's breakBlock casts
 * to (BlockBrewingStand's instanceof, the others' cast; the dropper's is a
 * TileEntityDispenser). */
static struct te_stack *slots_of(struct tile_entity *te, int br, int *n)
{
    if (te == NULL) return NULL;

    switch (br)
    {
        case BR_CHEST: case BR_TRAPPED_CHEST:
            if (te->kind != TE_CHEST) return NULL;
            *n = CHEST_SLOTS;
            return te->u.chest.slots;
        case BR_FURNACE: case BR_LIT_FURNACE:
            if (te->kind != TE_FURNACE) return NULL;
            *n = 3;
            return te->u.furnace.slots;
        case BR_DISPENSER: case BR_DROPPER:
            if (te->kind != TE_DISPENSER) return NULL;
            *n = DISPENSER_SLOTS;
            return te->u.dispenser.slots;
        case BR_HOPPER:
            if (te->kind != TE_HOPPER) return NULL;
            *n = 5;
            return te->u.hopper.slots;
        case BR_BREWING_STAND:
            if (te->kind != TE_BREWING_STAND) return NULL;
            *n = 4;
            return te->u.brewing.slots;
    }

    return NULL;
}

int container_spill(struct world *w, int x, int y, int z, int block, det_rng *r, det_state *det, int role,
                    spill_sink sink, void *ctx)
{
    int br = block_rand_index(block);

    if (br < 0) return 0;

    int n = 0;
    struct te_stack *slots = slots_of(world_tile_entity(w, x, y, z), br, &n);

    if (slots == NULL) return 0;

    if (r == NULL) r = &block_rand[br];

    int spilled = 0;

    for (int s = 0; s < n; ++s)
    {
        struct te_stack *st = &slots[s];

        if (st->item < 0 || st->count <= 0) continue;

        float f1 = (float)jr_float(&r->r) * 0.8f + 0.1f;
        float f2 = (float)jr_float(&r->r) * 0.8f + 0.1f;
        float f3 = (float)jr_float(&r->r) * 0.8f + 0.1f;

        while (st->count > 0)
        {
            int take = jr_int_n(&r->r, 21) + 10;

            if (take > st->count) take = st->count;

            st->count -= take;

            struct spill_item e;

            e.item = st->item;
            e.damage = st->damage;
            e.count = take;
            /* breakBlock: if (var9.hasTagCompound()) the entity's stack
             * takes a copy of the slot's tag */
            e.tag = st->tag;
            e.x = (double)((float)x + f1);
            e.y = (double)((float)y + f2);
            e.z = (double)((float)z + f3);

            /* new EntityItem: the Entity boilerplate (id, Random, UUID), then
             * hoverStart, rotationYaw and the two motions the spill
             * overwrites, all Math.random */
            e.entity_id = det_next_entity_id_role(det, role);
            det_rng entity_rand = det_new_random_role(det, role);
            e.rand_state = entity_rand.r.seed;
            det_uuid_role(det, role, &e.uuid_msb, &e.uuid_lsb);
            e.hover = (float)(det_math_random_role(det, role) * 3.141592653589793 * 2.0);
            e.yaw = (float)(det_math_random_role(det, role) * 360.0);
            (void)(float)(det_math_random_role(det, role) * 0.20000000298023224 - 0.10000000149011612);
            (void)(float)(det_math_random_role(det, role) * 0.20000000298023224 - 0.10000000149011612);

            float g = 0.05f;
            e.mx = (double)((float)det_rng_gaussian(r) * g);
            e.my = (double)((float)det_rng_gaussian(r) * g + 0.2f);
            e.mz = (double)((float)det_rng_gaussian(r) * g);

            if (sink != NULL) sink(ctx, &e);
            ++spilled;
        }
    }

    return spilled;
}
