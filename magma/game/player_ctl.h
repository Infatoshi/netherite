/* game/player_ctl.h - PLAYER-CTL module public surface.
 *
 * One player tick over a region-centered raw-Chunk window using the VERIFIED
 * player_survival.h kernels (psv_physics_tick / psv_raycast / break / place / vitals).
 * The prototypes below are the exact ones declared in game/game.h; this header just
 * pulls in the blaze types (Chunk / McSinTable / PsvPlayer) so a translation unit
 * that includes player_ctl.h alone can call them. See game/game.h for the contract.
 */
#ifndef MAGMA_GAME_PLAYER_CTL_H
#define MAGMA_GAME_PLAYER_CTL_H

#include "player_survival.h"   /* Chunk, McSinTable, PsvPlayer, PsvAction + verified kernels */
#include "player_vitals.h"     /* PvStats + verified vanilla vitals */
#include "game/game.h"         /* GmAction, GmBlockEdit, GmPlayerView */

#ifdef __cplusplus
extern "C" {
#endif

/* One player tick, PURE over the supplied region-centered `window` of PSV_NCHUNKS raw
 * blaze Chunk structs. pl->ent position is in the window LOCAL frame (chunk 0 == region
 * center). `vitals` is the verified vanilla PvStats (mirrored into pl->health/food).
 * Emits up to max_edits GmBlockEdit in WORLD coords (local edit + (ox,oy,oz)).
 * *nedits is set to the count emitted (0..max_edits). */
void gm_player_tick(struct Chunk *window, const struct McSinTable *st,
                    struct PsvPlayer *pl, struct PvStats *vitals, GmAction act,
                    int ox, int oy, int oz,
                    GmBlockEdit *edits, int *nedits, int max_edits);
/* Runtime variant with the tape/live world's active GameRules. The legacy
 * entry point above remains the vanilla-default component-test API. */
void gm_player_tick_gr(struct Chunk *window, const struct McSinTable *st,
                       struct PsvPlayer *pl, struct PvStats *vitals,
                       const struct McGameRules *gamerules, GmAction act,
                       int ox, int oy, int oz,
                       GmBlockEdit *edits, int *nedits, int max_edits);
/* Runtime composition variant: performs movement and movement-derived vitals,
 * but leaves FoodStats.onUpdate to the caller so Entity.move contact damage
 * and exhaustion can be applied first, matching EntityPlayer.onLivingUpdate. */
void gm_player_tick_defer_food(
                    struct Chunk *window, const struct McSinTable *st,
                    struct PsvPlayer *pl, struct PvStats *vitals, GmAction act,
                    int ox, int oy, int oz,
                    GmBlockEdit *edits, int *nedits, int max_edits);
/* Integrated-client composition: client physics advances immediately, while
 * movement/jump exhaustion is charged by the delayed CPacketPlayer path. */
void gm_player_tick_network_client(
                    struct Chunk *window, const struct McSinTable *st,
                    struct PsvPlayer *pl, struct PvStats *vitals, GmAction act,
                    int ox, int oy, int oz,
                    GmBlockEdit *edits, int *nedits, int max_edits);
void gm_player_tick_network_client_effects(
                    struct Chunk *window, const struct McSinTable *st,
                    struct PsvPlayer *pl, struct PvStats *vitals, GmAction act,
                    int ox, int oy, int oz,
                    GmBlockEdit *edits, int *nedits, int max_edits,
                    int haste_amplifier, int fatigue_amplifier);

/* Fill a GmPlayerView (WORLD coords) from a PsvPlayer whose pos is in the LOCAL frame,
 * given the block offset (ox,oz) to convert local->world. */
void gm_player_view(const struct PsvPlayer *pl, int ox, int oz, GmPlayerView *out);

/* Live inventory: Container.slotClick on hotbar slots 0..8 + cursor
 * (click_type: CC_CLICK_PICKUP / QUICK_MOVE / THROW from container_click.h). */
void gm_player_inv_click(struct PsvPlayer *pl, int slot_id, int button, int click_type);
ICStack gm_player_cursor(void);
void gm_player_cursor_set(ICStack s);
/* Consume an item transform result that vanilla drops when InventoryPlayer is
 * full (currently ItemGlassBottle -> one water potion). */
ICStack gm_player_take_item_use_drop(void);
/* Consume one ItemPotion/ItemBucketMilk stack that finished its 32-tick DRINK
 * action; its container item has already replaced the selected stack. */
ICStack gm_player_take_finished_drink(void);
void gm_player_dig_reset(void);
/* Apply an authoritative SPacketEntityVelocity and supersede any locally
 * inferred damage-packet reset queued for this tick. */
void gm_player_set_packet_velocity(struct PsvPlayer *pl, double x, double y, double z);
/* A velocity-packet tape supplies EntityTracker's authoritative resend. Drop
 * a locally inferred pre-packet reset without changing current motion. */
void gm_player_clear_inferred_hurt_velocity(void);
/* Dig target (window-local coords) + damage 0..1; returns 0 when not digging.
 * face_out optional: EnumFacing D-U-N-S-W-E of the hit face when known, else -1. */
int  gm_player_dig_state(int *lx, int *ly, int *lz, float *progress);
int  gm_player_dig_state_ex(int *lx, int *ly, int *lz, float *progress, int *face_out);

/* Full snapshot of the player_ctl.c per-player statics that carry state
 * across ticks and can alter physics or dig timing: the progressive-dig
 * machine (curBlockDamageMP / currentBlock / isHittingBlock / blockHitDelay,
 * attack edge), the rightClickMouse timer + use edge, and the hurt-velocity
 * server-motion shadow. Excluded (documented): s_fov_hand / s_bow_ticks
 * (render-only), s_eat_* (cleared on any tick without use held), s_cursor
 * (empty outside container_click composition). dig_hx/hy/hz are window-LOCAL;
 * only valid against the same ox/oz origin they were exported with. */
typedef struct {
    float  dig_progress;
    int    dig_hx, dig_hy, dig_hz;   /* INT_MIN sentinel = no target */
    int    dig_face;                 /* EnumFacing 0..5, -1 unknown */
    int    dig_hitting;
    int    dig_delay;
    int    dig_particle_count;       /* entity_pin dig_hit freeze count; 0=use stage */
    int    atk_prev;
    int    rc_delay;
    int    use_prev;
    int    hurt_vel_reset;
    double server_motion_x, server_motion_z;
} GmPlayerCtlSnap;

void gm_player_ctl_dig_export(GmPlayerCtlSnap *out);
void gm_player_ctl_dig_import(const GmPlayerCtlSnap *in);
/* Pinned dig_hit particle billboard count (0 = live stage proxy). */
int  gm_player_dig_particle_count(void);
/* 1 when this tick's dig phase reached vanilla's swingArm call in
 * Minecraft.sendClickBlockToController (onPlayerDamageBlock returned true).
 * Valid only between gm_player_tick and the next tick's dig phase. */
int  gm_player_dig_swing(void);

#ifdef __cplusplus
}
#endif
#endif /* MAGMA_GAME_PLAYER_CTL_H */
