/* player_death: vanilla-faithful DEATH + natural RESPAWN wrapped around the VERIFIED vitals model
 * (core/player_vitals.h). One MC_HD source, CPU==CUDA, and a verbatim-Java golden
 * (oracle/goldens/player_death/Golden.java) driven by the SAME deterministic tape.
 *
 * This EXTENDS player_vitals without altering any of its numbers: PvStats and every pv_* helper are
 * reused verbatim; the only new state is a tiny death state machine {dead, deaths, death_time}.
 *
 * Sources (java/oracle-src/net/minecraft):
 *   entity/EntityLivingBase.java  onUpdate death gate `if (getHealth() <= 0.0F) onDeathUpdate()`
 *                                   (358-360); onDeathUpdate `++deathTime; if (deathTime == 20)`
 *                                   (419-423); onDeath sets `this.dead = true` (1224-1242);
 *                                   ctor `setHealth(getMaxHealth())` (200); setHealth clamp (927-929);
 *                                   isEntityAlive = !dead && getHealth()>0 (1384-1387).
 *   util/FoodStats.java           new FoodStats defaults: foodLevel=20 (14), foodSaturationLevel=5.0
 *                                   (16), foodExhaustionLevel=0 (18 default), foodTimer=0 (20 default);
 *                                   NORMAL starve floor 1.0 (90-92).
 *   server/management/PlayerList.java  recreatePlayerEntity builds a NEW EntityPlayerMP on natural
 *                                   respawn (552) -> ctor health=maxHealth + fresh FoodStats.
 *
 * FIXED DIFFICULTY = NORMAL, natural respawn (no bed); keepInventory is now gamerule-driven
 * (pd_check_death_gr), defaulting to FALSE (the reference driver's emitted state is unchanged). On NORMAL the verified
 * FoodStats model FLOORS starvation at 1.0 HP (FoodStats.java:90-92), so starvation alone can never
 * reach 0 -- the only vanilla path to 0 HP under this model is damage (here: fall damage). The tape
 * below drives the player to death via BOTH a starvation-weakened death (food depleted, health
 * starved down, then finished) and a clean lethal fall, so a single run dies (>=2x) and respawns. */
#ifndef MC_PLAYER_DEATH_H
#define MC_PLAYER_DEATH_H

#include "player_vitals.h"

/* EntityLivingBase.deathTime reaches 20 (onDeathUpdate:423) before the player entity is removed and
 * the natural respawn happens. Model the respawn delay as that 20-tick death-animation window. */
#define PD_DEATH_TIME 20

/* Nominal held-item count at (re)spawn; the keepInventory observable (EntityPlayer.onDeath:
 * `if (!keepInventory) inventory.dropAllItems()`). Not emitted by the reference driver, so the
 * player_death golden is unaffected; the gamerule unit test reads it. */
#define PD_INIT_INV 36

typedef struct {
    PvStats pv;          /* the VERIFIED vitals (numbers unchanged from player_vitals.h) */
    i32     dead;        /* EntityLivingBase.dead: 1 while in the death window (set in onDeath, 1242) */
    i32     deaths;      /* deathCount / qrl obs 'deaths': incremented on each death */
    i32     death_time;  /* EntityLivingBase.deathTime: counts up while dead (onDeathUpdate, 421) */
    i32     inv_count;   /* held items; dropped (->0) on death unless keepInventory (onDeath) */
} PdState;

MC_HD static inline void pd_init(PdState *s) {
    pv_init(&s->pv);
    s->dead       = 0;
    s->deaths     = 0;
    s->death_time = 0;
    s->inv_count  = PD_INIT_INV;
}

/* Natural respawn (no bed, keepInventory=false): PlayerList.recreatePlayerEntity (552) builds a NEW
 * EntityPlayerMP; its ctor calls setHealth(getMaxHealth()) (EntityLivingBase.java:200) and it gets a
 * fresh FoodStats -> foodLevel=20 (FoodStats.java:14), saturation=5.0 (16), exhaustion=0 (18 default),
 * foodTimer=0 (20 default). Byte-identical to pv_init's vitals. deaths is preserved (a running count). */
MC_HD static inline void pd_respawn(PdState *s) {
    pv_init(&s->pv);      /* health=20, food=20, saturation=5, exhaustion=0, foodTimer=0 */
    s->dead       = 0;
    s->death_time = 0;
}

/* Death gate: EntityLivingBase.onUpdate (358) `if (getHealth() <= 0.0F) onDeathUpdate()`, and onDeath
 * (1224-1242) sets `this.dead = true`. Folded together: crossing to health<=0 -> dead=1, deaths++.
 * EntityPlayer.onDeath drops the inventory unless the keepInventory gamerule is set; with default
 * rules (keepInventory=0) items drop (inv_count -> 0), matching prior behavior for emitted state. */
MC_HD static inline void pd_check_death_gr(PdState *s, const McGameRules *gr) {
    if (!s->dead && s->pv.health <= 0.0f) {
        s->dead       = 1;
        s->deaths    += 1;    /* deathCount */
        s->death_time = 0;
        if (!gr->keepInventory) s->inv_count = 0;   /* inventory.dropAllItems() */
    }
}

/* Default-rules wrapper (keepInventory=0: drop on death). */
MC_HD static inline void pd_check_death(PdState *s) {
    McGameRules gr = mc_gamerules_default();
    pd_check_death_gr(s, &gr);
}

/* ---- deterministic death tape (drives the player to death, then lets the state machine respawn) ----
 * Faithful within the NORMAL / naturalRegeneration scope. Two scenarios, alternated by deaths parity
 * so a single run exercises both a starvation-weakened death and a clean lethal fall:
 *   scenario A (deaths even): pour exhaustion so saturation->0 then foodLevel->0 (FoodStats path),
 *       let starvation floor health toward 1.0, then once starved (food==0, health<=17) a 30-block
 *       fall (ceil(27)=27 dmg) delivers the finishing blow.  "died while starving."
 *   scenario B (deaths odd): from the fresh respawn state (full food/health) a single 30-block fall
 *       kills outright on the first alive tick.  "clean lethal fall."
 * Driven by pv_* helpers only; the tick argument is accepted for parity with pv_tape_tick. */
MC_HD static inline void pd_alive_tick(PdState *s, i32 tick) {
    (void)tick;
    if (s->deaths & 1) {
        /* scenario B: clean lethal fall at full health/food. */
        pv_fall_damage(&s->pv, 30.0f);
        pv_on_update(&s->pv);
        return;
    }
    /* scenario A: drain food fast (8.0F exhaustion/tick -> one rollover/tick), then finish. */
    pv_add_exhaustion(&s->pv, 8.0f);
    if (s->pv.foodLevel <= 0 && s->pv.health <= 17.0f)
        pv_fall_damage(&s->pv, 30.0f);
    pv_on_update(&s->pv);
}

/* One driven tick. seed is accepted for signature parity with pv_tape_tick (the schedule is a pure
 * function of state + tick, so seed is unused -- runs are reproducible across CPU/CUDA/Java). */
MC_HD static inline void pd_tape_tick(PdState *s, i64 seed, i32 tick) {
    (void)seed;
    if (!s->dead) {
        pd_alive_tick(s, tick);
        pd_check_death(s);
    } else {
        s->death_time += 1;                       /* onDeathUpdate: ++deathTime */
        if (s->death_time >= PD_DEATH_TIME)       /* deathTime == 20 -> entity removed -> respawn */
            pd_respawn(s);
    }
}

#endif /* MC_PLAYER_DEATH_H */
