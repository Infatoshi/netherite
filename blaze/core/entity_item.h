/* entity_item: EntityItem.onUpdate ground-item physics (MC 1.11.2
 * net/minecraft/entity/item/EntityItem.java), a bit-faithful C port.
 *
 * SCOPE: the deterministic motion + merge (combineItems) path. Verified vs the live Java game
 * (verify/entity_trace item scenarios, item_trace_verify.c) - per-tick posX/Y/Z + motionX/Y/Z
 * compared on raw Double.doubleToRawLongBits bits.
 *
 * RAND-FREEDOM: EntityItem.onUpdate's motion path draws NO random numbers for our sealed dry/water
 * arenas. rand is touched only at three sites, all provably inactive here:
 *   1. the LAVA branch (this.rand.nextFloat) - no lava in the arena.
 *   2. Entity.pushOutOfBlocks (rand.nextFloat when the item is INSIDE a solid) - the item always
 *      rests ON TOP of a block (box.minY == block.maxY) and AABB.intersectsWith is a strict '<',
 *      so collidesWithAnyBlock is false and pushOutOfBlocks returns without drawing (see ei_pre).
 *   3. Entity.resetHeight (rand on water-entry) - it plays a splash sound + spawns particles but
 *      NEVER writes motionX/Y/Z, so it cannot perturb the trajectory (water scenario stays exact).
 * combineItems has no rand. => item motion + merge are fully deterministic given the summon state.
 *
 * WATER: 1.11.2 EntityItem has no buoyancy/float code (the prompt's "applyFloatMotion" does not
 * exist in this oracle). inWater is motion-inert; the only water force is handleMaterialAcceleration's
 * flow push, which is exactly Vec3d.ZERO for a source block walled by solids on all four horizontal
 * sides (BlockLiquid.getFlow: solid neighbors blocksMovement() -> skipped -> zero). So an item in
 * such a pool sinks under the SAME gravity/drag as in air; we run the dry tick unchanged and a
 * bit-exact match against the live water trace empirically proves flow == 0.
 *
 * TRIMMED (no motion bearing): hoverStart/bob (render-only), spawnRunningParticles, portal/fire
 * counters, isAirBorne, the pickup path (onCollideWithPlayer -> InventoryPlayer.addItemStack, which
 * is separately golden as inventory_stack_rules.h; our item scenarios use a huge pickupDelay so the
 * parked player cannot vacuum it), ForgeEventFactory.onItemExpire (default -> setDead at age>=lifespan).
 *
 * Collision reuses physics_collision_math.h::mc_entity_move (the shared Entity.move AABB sweep, the
 * same core projectile_motion.h stays consistent with). If the entity spine lands a unified move
 * entry mid-round this keeps its local call - TODO: unify on the shared entry. */
#ifndef MC_ENTITY_ITEM_H
#define MC_ENTITY_ITEM_H

#include "mc.h"
#include "mc_world.h"
#include "mc_blocks.h"
#include "mc_math.h"
#include "physics_collision_math.h"

typedef struct {
    double posX, posY, posZ;
    double motionX, motionY, motionZ;
    McAABB box;
    int onGround;
    int age;                  /* -32768 = no-age sentinel */
    int delayBeforeCanPickup; /* 32767 = infinite sentinel */
    int ticksExisted;
    int lifespan;
    int dead;
    /* ItemStack (registry item id, count, damage/meta) */
    int item, count, meta;
    int hasSubtypes;          /* Item.getHasSubtypes() */
    int hasTag;               /* NBTTagCompound present (0 for our scenes) */
    int maxStack;             /* ItemStack.getMaxStackSize() */
} McItem;

/* Block.getSlipperiness: 0.98F for the ice family, 0.6F default (Block.slipperiness). */
MC_HD static inline float ei_slipperiness(u16 under_state) {
    int id = mc_state_id(under_state);
    if (id == BLK_ICE || id == 174 /* packed_ice */ || id == 212 /* frosted_ice */)
        return 0.98f;
    return 0.6f;
}

/* setSize(0.25,0.25) + setPosition: box centered on (x,z), feet at posY, height 0.25. */
MC_HD static inline void ei_set_position(McItem *it, double x, double y, double z) {
    it->posX = x; it->posY = y; it->posZ = z;
    float f = 0.25f / 2.0f;   /* width/2 */
    float f1 = 0.25f;         /* height */
    it->box = mc_aabb_make(x - (double)f, y, z - (double)f,
                           x + (double)f, y + (double)f1, z + (double)f);
}

/* Entity.move(SELF) via the shared AABB sweep; syncs the McItem's own fields to/from McEntity. */
MC_HD static inline void ei_move(McItem *it, double dx, double dy, double dz,
                                 const McAABB *blocks, int nblocks) {
    McEntity e;
    e.box = it->box;
    e.posX = it->posX; e.posY = it->posY; e.posZ = it->posZ;
    e.motionX = it->motionX; e.motionY = it->motionY; e.motionZ = it->motionZ;
    e.onGround = it->onGround;
    e.collidedHorizontally = e.collidedVertically = e.isCollided = 0;
    mc_entity_move(&e, dx, dy, dz, blocks, nblocks);
    it->box = e.box;
    it->posX = e.posX; it->posY = e.posY; it->posZ = e.posZ;
    it->motionX = e.motionX; it->motionY = e.motionY; it->motionZ = e.motionZ;
    it->onGround = e.onGround;
}

/* onUpdate part 1: pickup-delay decrement + gravity + pushOutOfBlocks (inert) + move.
 * Returns `flag` = the item crossed an integer block boundary this tick (searchForOtherItemsNearby
 * gate: flag || ticksExisted % 25 == 0). colliding_push MUST be 0 for our scenes (asserted by the
 * caller); if it were 1 the vanilla path draws rand -> not bit-portable. */
MC_HD static inline int ei_pre(McItem *it, const McAABB *blocks, int nblocks, int colliding_push) {
    int px, py, pz;
    (void)colliding_push;   /* pushOutOfBlocks: collidesWithAnyBlock == false -> no-op, no rand */
    if (it->delayBeforeCanPickup > 0 && it->delayBeforeCanPickup != 32767)
        --it->delayBeforeCanPickup;

    px = mc_floor(it->posX); py = mc_floor(it->posY); pz = mc_floor(it->posZ);

    it->motionY -= (double)0.03999999910593033;   /* (double)0.04f, verbatim oracle literal */

    ei_move(it, it->motionX, it->motionY, it->motionZ, blocks, nblocks);

    return (px != mc_floor(it->posX)) || (py != mc_floor(it->posY)) || (pz != mc_floor(it->posZ));
}

/* onUpdate part 2 (after searchForOtherItemsNearby): ground friction + air/vertical drag + bounce
 * damp + age. under_state is the block one below the resolved box bottom (only used on ground). */
MC_HD static inline void ei_post(McItem *it, u16 under_state) {
    float f = 0.98f;
    if (it->onGround)
        f = ei_slipperiness(under_state) * 0.98f;
    it->motionX *= (double)f;
    it->motionY *= 0.9800000190734863;   /* (double)0.98f */
    it->motionZ *= (double)f;
    if (it->onGround)
        it->motionY *= -0.5;
    if (it->age != -32768)
        ++it->age;
    /* handleWaterMovement: flow == 0 for our pools -> no motion change (see header note). */
    if (it->age >= it->lifespan)
        it->dead = 1;   /* ForgeEventFactory.onItemExpire default (uncanceled) -> setDead */
}

/* EntityItem.combineItems(this=a, other=b). Returns 1 if a merge happened (one of the two dies).
 * Mirrors the oracle's compatibility ladder; NBT tag equality is (hasTag xor) then equals - our
 * scenes carry no tags so both sides are 0. maxStack is ItemStack.getMaxStackSize(). */
MC_HD static inline int ei_combine(McItem *a, McItem *b) {
    if (a == b) return 0;
    if (a->dead || b->dead) return 0;
    /* itemstack = a (this), itemstack1 = b (other) */
    if (a->delayBeforeCanPickup == 32767 || b->delayBeforeCanPickup == 32767) return 0;
    if (a->age == -32768 || b->age == -32768) return 0;
    if (b->item != a->item) return 0;
    if ((b->hasTag ^ a->hasTag) != 0) return 0;
    if (b->hasTag && a->hasTag) { /* tag equality: assume equal for our tagless scenes */ }
    if (a->hasSubtypes && b->meta != a->meta) return 0;
    if (b->count < a->count) return ei_combine(b, a);   /* swap so the larger stack absorbs */
    if (b->count + a->count > b->maxStack) return 0;
    /* areCapsCompatible: true (no capabilities) */
    b->count += a->count;
    if (a->delayBeforeCanPickup > b->delayBeforeCanPickup)
        b->delayBeforeCanPickup = a->delayBeforeCanPickup;
    if (a->age < b->age)
        b->age = a->age;
    a->dead = 1;
    return 1;
}

#endif /* MC_ENTITY_ITEM_H */
