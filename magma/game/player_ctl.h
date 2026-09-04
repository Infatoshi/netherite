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
#include "mc_rng.h"
#include "player_control_state.h"

/* Runtime-owned controller. Initialize once before use. Simulation fields are
 * shared with Blaze; the remainder is Magma presentation/event state. */
typedef struct GmPlayerCtl {
    PlayerControlState sim;
    int   dig_particle_count; /* entity_pin dig_hit count; 0 = stage proxy */
    /* PlayerControllerMP hit audio every fourth damage tick. Excluded from
     * simulation snapshots; import discards queued sounds. */
    int   dig_sound_tick_counter;
    int   dig_sound_pending;
    int   dig_sound_wx, dig_sound_wy, dig_sound_wz;
    int   dig_sound_state;
    float fov_hand; /* EntityRenderer.fovModifierHand */
    int   bow_ticks; /* ItemBow active use for viewmodel */
    int   dig_face; /* EnumFacing of hit face while progressive dig */
    int   dig_swing; /* render-only swingArm signal, consumed in this tick */
    int   dig_brk; /* first dig_destroy event in this tick, world coordinates */
    int   dig_brk_wx, dig_brk_wy, dig_brk_wz;
    int   use_action;      /* 0 none, 1 eat/drink, 2 block */
    int   use_remaining;
    int   use_max;
    JavaRandom *world_rand; /* runtime-owned World.rand for food completion */
    ICStack pending_drop;
    int tnt_pending, tnt_x, tnt_y, tnt_z, tnt_fuse;
} GmPlayerCtl;

#ifdef __cplusplus
extern "C" {
#endif

void gm_player_ctl_init(GmPlayerCtl *ctl);

/* One player tick over the explicit controller and region-centered `window` of PSV_NCHUNKS raw
 * blaze Chunk structs. pl->ent position is in the window LOCAL frame (chunk 0 == region
 * center). `vitals` is the verified vanilla PvStats (mirrored into pl->health/food).
 * Emits up to max_edits GmBlockEdit in WORLD coords (local edit + (ox,oy,oz)).
 * *nedits is set to the count emitted (0..max_edits). */
void gm_player_tick(GmPlayerCtl *ctl, struct Chunk *window, const struct McSinTable *st,
                    struct PsvPlayer *pl, struct PvStats *vitals, GmAction act,
                    int ox, int oy, int oz,
                    GmBlockEdit *edits, int *nedits, int max_edits);
/* Runtime variant with the tape/live world's active GameRules. The legacy
 * entry point above remains the vanilla-default component-test API. */
void gm_player_tick_gr(GmPlayerCtl *ctl, struct Chunk *window, const struct McSinTable *st,
                       struct PsvPlayer *pl, struct PvStats *vitals,
                       const struct McGameRules *gamerules, GmAction act,
                       int ox, int oy, int oz,
                       GmBlockEdit *edits, int *nedits, int max_edits);

/* Fill a GmPlayerView (WORLD coords) from a PsvPlayer whose pos is in the LOCAL frame,
 * given the block offset (ox,oz) to convert local->world. */
void gm_player_view(const GmPlayerCtl *ctl, const struct PsvPlayer *pl, int ox, int oz, GmPlayerView *out);

/* Live inventory: Container.slotClick on hotbar slots 0..8 + cursor
 * (click_type: CC_CLICK_PICKUP / QUICK_MOVE / THROW from container_click.h). */
void gm_player_inv_click(GmPlayerCtl *ctl, struct PsvPlayer *pl, int slot_id, int button, int click_type);
void gm_player_bind_world_rand(GmPlayerCtl *ctl, JavaRandom *world_rand);
int  gm_player_take_drop(GmPlayerCtl *ctl, ICStack *out);
/* BlockTNT flint-and-steel ignite this tick: world-block pos + fuse 80. */
int  gm_player_take_tnt_ignite(GmPlayerCtl *ctl, int *x, int *y, int *z, int *fuse);
ICStack gm_player_cursor(const GmPlayerCtl *ctl);
void gm_player_cursor_set(GmPlayerCtl *ctl, ICStack s);
void gm_player_dig_reset(GmPlayerCtl *ctl);
/* Apply an authoritative SPacketEntityVelocity and supersede any locally
 * inferred damage-packet reset queued for this tick. */
void gm_player_set_packet_velocity(GmPlayerCtl *ctl, struct PsvPlayer *pl, double x, double y, double z);
/* A velocity-packet tape supplies EntityTracker's authoritative resend. Drop
 * a locally inferred pre-packet reset without changing current motion. */
void gm_player_clear_inferred_hurt_velocity(GmPlayerCtl *ctl);
/* Dig target (window-local coords) + damage 0..1; returns 0 when not digging.
 * face_out optional: EnumFacing D-U-N-S-W-E of the hit face when known, else -1. */
int  gm_player_dig_state(const GmPlayerCtl *ctl, int *lx, int *ly, int *lz, float *progress);
int  gm_player_dig_state_ex(const GmPlayerCtl *ctl, int *lx, int *ly, int *lz, float *progress, int *face_out);

/* Full snapshot of the controller fields that carry state
 * across ticks and can alter physics or dig timing: the progressive-dig
 * machine (curBlockDamageMP / currentBlock / isHittingBlock / blockHitDelay,
 * attack edge, leftClickCounter), the rightClickMouse timer + use edge, and
 * the hurt-velocity server-motion shadow. Excluded: fov_hand / bow_ticks
 * (render-only), cursor (serialized separately in the container trailer).
 * eat_ticks/eat_item are in the v10 snapshot trailer so a mid-eat
 * resume keeps the 32-tick use timer. dig_hx/hy/hz are window-LOCAL;
 * only valid against the same ox/oz origin they were exported with. */
typedef struct {
    float  dig_progress;
    int    dig_hx, dig_hy, dig_hz;   /* INT_MIN sentinel = no target */
    int    dig_face;                 /* EnumFacing 0..5, -1 unknown */
    int    dig_hitting;
    int    dig_delay;
    int    dig_particle_count;       /* entity_pin dig_hit freeze count; 0=use stage */
    int    atk_prev;
    int    left_click_counter;       /* Minecraft.leftClickCounter */
    int    rc_delay;
    int    use_prev;
    int    hurt_vel_reset;
    double server_motion_x, server_motion_z;
    int    eat_ticks;                /* ItemFood.onItemUseFinish countdown */
    int    eat_item;
} GmPlayerCtlSnap;

void gm_player_ctl_dig_export(const GmPlayerCtl *ctl, GmPlayerCtlSnap *out);
void gm_player_ctl_dig_import(GmPlayerCtl *ctl, const GmPlayerCtlSnap *in);
/* Preserve window-local controller targets across runtime floating-origin
 * recentering. dx/dz are the world-origin deltas subtracted from player pose. */
void gm_player_ctl_recenter(GmPlayerCtl *ctl, int dx, int dz);
/* Predict whether clickMouse / sendClickBlockToController run this tick given
 * the physical attack bit. Matches the leftClickCounter decrement that
 * gm_player_tick applies; does not mutate state. Runtime entity attacks use
 * this so damage shares the same post-decrement gate as dig. */
int  gm_player_left_click_allows(const GmPlayerCtl *ctl, int attack_held);
/* Minecraft displayGuiScreen: non-inventory container screens pin
 * leftClickCounter at 10000 while open; closing restores ordinary input. */
void gm_player_set_gui_blocked(GmPlayerCtl *ctl, int blocked);
/* Pinned dig_hit particle billboard count (0 = live stage proxy). */
int  gm_player_dig_particle_count(const GmPlayerCtl *ctl);
/* 1 when this tick's dig phase reached vanilla's swingArm call in
 * Minecraft.sendClickBlockToController (onPlayerDamageBlock returned true).
 * Valid only between gm_player_tick and the next tick's dig phase. */
int  gm_player_dig_swing(const GmPlayerCtl *ctl);
/* Consume this tick's PlayerControllerMP progressive-mining hit sound.
 * Coordinates are world-space and state_id is legacy id|(meta<<12). */
int  gm_player_take_dig_sound(GmPlayerCtl *ctl, int *wx, int *wy, int *wz, int *state_id);
/* dig_trace: 1 + WORLD coords if dig_destroy ran this tick (cleared next
 * gm_player_tick dig phase). Diagnostic only. */
int  gm_player_dig_break_event(const GmPlayerCtl *ctl, int *wx, int *wy, int *wz);
/* dig_trace: relative hardness for a window-local block without mutating dig
 * state. Uses the same tool/water/ground inputs as the dig tick. */
float gm_player_block_rel_hardness(const struct Chunk *window,
                                   const struct PsvPlayer *pl, int creative,
                                   int hx, int hy, int hz);
/* Convenience wrapper for the current progressive dig target. */
float gm_player_dig_rel_hardness(const GmPlayerCtl *ctl, const struct Chunk *window,
                                 const struct PsvPlayer *pl, int creative);

#ifdef __cplusplus
}
#endif
#endif /* MAGMA_GAME_PLAYER_CTL_H */
