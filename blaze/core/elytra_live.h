/* elytra_live.h - EntityPlayerSP START_FALL_FLYING + updateElytra damage.
 *
 * Magma magma/game/player_ctl.c and blaze-CPU/CUDA compile this one source.
 * Travel math stays in player_survival.h psv_elytra_travel.
 *
 * Java 1.11.2 (java/oracle-src):
 *   EntityPlayerSP.onLivingUpdate START_FALL_FLYING
 *     EntityPlayerSP.java:1030-1036  fresh jump, airborne, descending
 *   NetHandlerPlayServer.setElytraFlying
 *     NetHandlerPlayServer.java:1019-1027
 *     !onGround && motionY < 0 && usable chest elytra
 *   EntityLivingBase.updateElytra
 *     EntityLivingBase.java: onUpdate ticksElytraFlying, damage every 20
 *     ticks when flag 7 is set (ItemElytra, chest slot)
 *   EntityLivingBase.travel isElytraFlying branch
 *     EntityLivingBase.java:1866-1944  (psv_elytra_travel)
 *   10-tick landing: jumpTicks is unrelated; wall damage is
 *     (speed_h - speed_after)*10 - 3  EntityLivingBase.java:1935-1941
 *
 * Magma extras (M1 is magma semantics):
 *   elytra_flying_pending is the 1-tick metadata round trip
 *     (player_ctl.c:348-351, :810-842)
 *   empty chest preserves gm_runtime_set_elytra; occupied non-elytra clears
 *   no creative isFlying, no riding
 */
#ifndef MC_ELYTRA_LIVE_H
#define MC_ELYTRA_LIVE_H

#include "player_survival.h"
#include "inventory_stack_rules.h"
#include "items_tools_armor.h"

MC_HD static inline void el_consume_pending(PsvPlayer *pl) {
    if (!pl->elytra_flag7_recorded && pl->elytra_flying_pending) {
        pl->elytra_flying = 1;
        pl->elytra_flying_pending = 0;
    }
}

/* EntityPlayerSP / player_ctl.c:824-833. */
MC_HD static inline void el_derive_equipped(PsvPlayer *pl) {
    ICStack chest = isr_get_stack(&pl->inv, ISR_ARMOR_CHEST);
    if (chest.item == ISR_ELYTRA_ITEM)
        pl->elytra_equipped = isr_elytra_usable(&chest);
    else if (!isr_is_empty(&chest))
        pl->elytra_equipped = 0;
}

MC_HD static inline void el_damage_chest(PsvPlayer *pl) {
    ICStack chest = isr_get_stack(&pl->inv, ISR_ARMOR_CHEST);
    ITAStack e;
    if (chest.item != ISR_ELYTRA_ITEM || chest.count <= 0) return;
    e = ita_mk(chest.item, chest.meta);
    if (ita_attempt_damage(&e, 1, NULL)) {
        isr_set_stack(&pl->inv, ISR_ARMOR_CHEST, ic_empty());
        pl->elytra_equipped = 0;
        pl->elytra_flying = 0;
    } else {
        chest.meta = e.damage;
        isr_set_stack(&pl->inv, ISR_ARMOR_CHEST, chest);
        pl->elytra_equipped = isr_elytra_usable(&chest);
        if (!pl->elytra_equipped) pl->elytra_flying = 0;
    }
}

/* player_ctl.c:838-871 after psv_physics_tick. flying_was is flag 7
 * before travel. can_start is !onGround && motionY<0 sampled PRE-travel. */
MC_HD static inline void el_post_travel(PsvPlayer *pl, int jump, int water_pre,
                                        int flying_was, int can_start,
                                        const Chunk *now, McAABB *blocks) {
    int press = jump && !pl->prev_jump;
    if (!pl->elytra_flag7_recorded && press && !flying_was &&
        pl->elytra_equipped && !water_pre && can_start)
        pl->elytra_flying_pending = 1;
    pl->prev_jump = jump;
    if (flying_was && pl->elytra_flying) {
        ++pl->ticks_elytra_flying;
        if ((pl->ticks_elytra_flying % 20) == 0)
            el_damage_chest(pl);
    } else if (!pl->elytra_flying) {
        pl->ticks_elytra_flying = 0;
    }
    psv_update_elytra_size(now, pl, blocks);
}

#endif /* MC_ELYTRA_LIVE_H */
