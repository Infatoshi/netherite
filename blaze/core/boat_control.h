/* boat_control: EntityBoat.controlBoat + updateMotion momentum subset (MC 1.11.2).
 *
 * PORT: net/minecraft/entity/item/EntityBoat.java
 *   controlBoat() thrust/turn constants (client-only when world.isRemote)
 *   updateMotion() momentum by Status (IN_WATER / ON_LAND / IN_AIR subset)
 *   MathHelper.sin/cos for yaw thrust (table-based, via mc_math.h)
 *
 * Composed step order matches client onUpdate when canPassengerSteer:
 *   updateMotion() THEN controlBoat()  (EntityBoat.java ~314-318)
 * controlBoat is client-only; this battery models that client composed step.
 * It is NOT a full entity tick (no gravity/buoyancy/move/paddle packets).
 *
 * From rest (vx=vz=0) on water with forward=1: controlBoat adds 0.04 along look;
 * because updateMotion already applied 0.9 to the prior zero velocity, the post-
 * step horizontal speed is 0.04, not 0.04*0.9=0.036 (that would be the wrong
 * control-then-motion composition).
 *
 * Per-function constants (independent of composition):
 *   controlBoat: forward +0.04, back -0.005, turn-only +0.005, deltaRot +/-1
 *   updateMotion: IN_WATER 0.9, IN_AIR 0.9, ON_LAND boatGlide (halves under player)
 *
 * Eval-pure: no world, no multi-AABB waterLevel sampling. Status is injected,
 * matching the simplified 3-way sample used by magma mob_live (IN_WATER/ON_LAND/IN_AIR).
 *
 * OPEN (not in this battery):
 *   UNDER_WATER / UNDER_FLOWING_WATER status, full multi-AABB getWaterLevelAbove,
 *   60-tick underwater passenger ejection, land boatGlide block-friction table,
 *   server-side path where controlBoat does not run.
 *
 * Battery: scripted (status, forward, strafe, yaw) scenarios; emit float bits of
 * yaw/vx/vz/deltaRot/momentum after one updateMotion + one controlBoat step. */
#ifndef MC_BOAT_CONTROL_H
#define MC_BOAT_CONTROL_H

#include "mc.h"
#include "mc_math.h"

enum {
    BC_STATUS_IN_WATER = 0,
    BC_STATUS_ON_LAND  = 1,
    BC_STATUS_IN_AIR   = 2,
    /* UNDER_* intentionally absent from this subset battery. */
    BC_NUM_SCENARIOS   = 12,
    BC_FIELDS_PER      = 6, /* yaw, vx, vz, delta_rot, momentum, status_echo */
    BC_OUT             = (BC_NUM_SCENARIOS * BC_FIELDS_PER)
};

typedef struct {
    float yaw;
    float delta_rot;
    float boat_glide; /* ON_LAND only; halved each player-controlled tick */
    double vx, vz;
    int status;
    int forward; /* -1,0,1 */
    int strafe;  /* -1 left, +1 right */
} BcState;

MC_HD static inline u32 bc_fbits(float f) {
    union { float f; u32 u; } u;
    u.f = f;
    return u.u;
}

/* EntityBoat.controlBoat VERBATIM (ridden / client), using MathHelper sin/cos. */
MC_HD static inline void bc_control_boat(BcState *s, const McSinTable *st) {
    float f = 0.0f;
    int left = s->strafe < 0;
    int right = s->strafe > 0;
    int fwd = s->forward > 0;
    int back = s->forward < 0;
    if (left) s->delta_rot += -1.0f;
    if (right) s->delta_rot += 1.0f;
    if ((left != right) && !fwd && !back) f += 0.005f;
    s->yaw += s->delta_rot;
    if (fwd) f += 0.04f;
    if (back) f -= 0.005f;
    {
        float yaw_rad = s->yaw * 0.017453292f;
        float sn = mc_sin(st, -yaw_rad);
        float cs = mc_cos(st, yaw_rad);
        s->vx += (double)(sn * f);
        s->vz += (double)(cs * f);
    }
}

/* updateMotion momentum + deltaRotation damp (no gravity/buoyancy in battery). */
MC_HD static inline float bc_update_motion(BcState *s) {
    float momentum = 0.05f;
    if (s->status == BC_STATUS_IN_WATER) {
        momentum = 0.9f;
    } else if (s->status == BC_STATUS_ON_LAND) {
        if (s->boat_glide <= 0.0f) s->boat_glide = 0.8f;
        momentum = s->boat_glide;
        /* Player-controlled land glide halves each tick (EntityBoat.updateMotion). */
        s->boat_glide *= 0.5f;
    } else { /* IN_AIR */
        momentum = 0.9f;
    }
    s->vx *= (double)momentum;
    s->vz *= (double)momentum;
    s->delta_rot *= momentum;
    return momentum;
}

MC_HD static inline void bc_emit(const BcState *s, float momentum, u32 *out, int base) {
    out[base + 0] = bc_fbits(s->yaw);
    out[base + 1] = bc_fbits((float)s->vx);
    out[base + 2] = bc_fbits((float)s->vz);
    out[base + 3] = bc_fbits(s->delta_rot);
    out[base + 4] = bc_fbits(momentum);
    out[base + 5] = (u32)s->status;
}

MC_HD static inline void bc_run_scenario(int idx, const McSinTable *st, u32 *out) {
    BcState s;
    float mom;
    int base = idx * BC_FIELDS_PER;
    s.yaw = 0.0f;
    s.delta_rot = 0.0f;
    s.boat_glide = 0.8f;
    s.vx = 0.0;
    s.vz = 0.0;
    s.status = BC_STATUS_IN_WATER;
    s.forward = 0;
    s.strafe = 0;

    switch (idx) {
    case 0: /* water idle */
        s.status = BC_STATUS_IN_WATER;
        break;
    case 1: /* water forward from rest: expect |v|~0.04 not 0.036 */
        s.status = BC_STATUS_IN_WATER;
        s.forward = 1;
        s.yaw = 0.0f;
        break;
    case 2: /* water forward + yaw 90 */
        s.status = BC_STATUS_IN_WATER;
        s.forward = 1;
        s.yaw = 90.0f;
        break;
    case 3: /* water left turn */
        s.status = BC_STATUS_IN_WATER;
        s.strafe = -1;
        break;
    case 4: /* water right turn */
        s.status = BC_STATUS_IN_WATER;
        s.strafe = 1;
        break;
    case 5: /* water back */
        s.status = BC_STATUS_IN_WATER;
        s.forward = -1;
        break;
    case 6: /* land forward */
        s.status = BC_STATUS_ON_LAND;
        s.forward = 1;
        s.boat_glide = 0.8f;
        break;
    case 7: /* land residual velocity damp */
        s.status = BC_STATUS_ON_LAND;
        s.vx = 0.2;
        s.vz = -0.1;
        s.boat_glide = 0.8f;
        break;
    case 8: /* air forward */
        s.status = BC_STATUS_IN_AIR;
        s.forward = 1;
        s.yaw = 45.0f;
        break;
    case 9: /* water forward+left */
        s.status = BC_STATUS_IN_WATER;
        s.forward = 1;
        s.strafe = -1;
        break;
    case 10: /* water left only at yaw 180 */
        s.status = BC_STATUS_IN_WATER;
        s.strafe = -1;
        s.yaw = 180.0f;
        break;
    case 11: /* land second-tick glide 0.4 */
        s.status = BC_STATUS_ON_LAND;
        s.forward = 1;
        s.boat_glide = 0.4f;
        s.yaw = -30.0f;
        break;
    default:
        break;
    }

    /* Client canPassengerSteer order: updateMotion then controlBoat. */
    mom = bc_update_motion(&s);
    bc_control_boat(&s, st);
    bc_emit(&s, mom, out, base);
}

MC_HD static inline void bc_run_battery(const McSinTable *st, u32 *out) {
    int i;
    for (i = 0; i < BC_NUM_SCENARIOS; ++i)
        bc_run_scenario(i, st, out);
}

#endif /* MC_BOAT_CONTROL_H */
