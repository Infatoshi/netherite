// Verbatim MC 1.11.2 EntityBoat.controlBoat + updateMotion momentum subset.
// Eval-pure hand-port of decompiled oracle-src (not a live Mojang process).
//
// Logic from:
//   net/minecraft/entity/item/EntityBoat.java  controlBoat, updateMotion (momentum branch)
//   net/minecraft/util/math/MathHelper.java    sin/cos SIN_TABLE
//
// Composed step order matches client onUpdate when canPassengerSteer:
//   updateMotion() THEN controlBoat()  (EntityBoat.java ~314-318)
// controlBoat is client-only (world.isRemote). This battery models that client
// composed step from rest; it is not a full entity tick.
//
// From rest water forward: post-step |v| is 0.04 (not 0.04*0.9=0.036).
// Per-function constants are independent of composition (0.04 / 0.005 / deltaRot).
//
// Status is injected (IN_WATER=0, ON_LAND=1, IN_AIR=2) matching magma's simplified
// 3-way sample. OPEN: UNDER_WATER / UNDER_FLOWING_WATER, full multi-AABB waterLevel,
// 60-tick passenger eject, land boatGlide block-friction table, server path.
//
// Output: 12 scenarios * 6 fields (yaw,vx,vz,deltaRot,momentum bits + status) as %08x.

public class Golden {
    static final int N = 12, FIELDS = 6;
    static final int IN_WATER = 0, ON_LAND = 1, IN_AIR = 2;

    // ---- MathHelper SIN_TABLE (verbatim) ----
    static final float[] SIN_TABLE = new float[65536];
    static {
        for (int i = 0; i < 65536; ++i)
            SIN_TABLE[i] = (float)Math.sin((double)i * Math.PI * 2.0D / 65536.0D);
    }
    static float sin(float value) {
        return SIN_TABLE[(int)(value * 10430.378F) & 65535];
    }
    static float cos(float value) {
        return SIN_TABLE[(int)(value * 10430.378F + 16384.0F) & 65535];
    }

    static class State {
        float yaw, deltaRot, boatGlide = 0.8f;
        double vx, vz;
        int status, forward, strafe;
    }

    // EntityBoat.controlBoat VERBATIM (client/ridden)
    static void controlBoat(State s) {
        float f = 0.0F;
        boolean left = s.strafe < 0, right = s.strafe > 0;
        boolean fwd = s.forward > 0, back = s.forward < 0;
        if (left) s.deltaRot += -1.0F;
        if (right) s.deltaRot += 1.0F;
        if (left != right && !fwd && !back) f += 0.005F;
        s.yaw += s.deltaRot;
        if (fwd) f += 0.04F;
        if (back) f -= 0.005F;
        s.vx += (double)(sin(-s.yaw * 0.017453292F) * f);
        s.vz += (double)(cos(s.yaw * 0.017453292F) * f);
    }

    // updateMotion momentum subset (no gravity/buoyancy)
    static float updateMotion(State s) {
        float momentum = 0.05F;
        if (s.status == IN_WATER) {
            momentum = 0.9F;
        } else if (s.status == ON_LAND) {
            if (s.boatGlide <= 0.0F) s.boatGlide = 0.8F;
            momentum = s.boatGlide;
            s.boatGlide *= 0.5F;
        } else {
            momentum = 0.9F;
        }
        s.vx *= (double)momentum;
        s.vz *= (double)momentum;
        s.deltaRot *= momentum;
        return momentum;
    }

    static void emit(StringBuilder sb, State s, float mom) {
        u(sb, Float.floatToRawIntBits(s.yaw));
        u(sb, Float.floatToRawIntBits((float)s.vx));
        u(sb, Float.floatToRawIntBits((float)s.vz));
        u(sb, Float.floatToRawIntBits(s.deltaRot));
        u(sb, Float.floatToRawIntBits(mom));
        u(sb, s.status);
    }
    static void u(StringBuilder sb, int v) {
        sb.append(String.format("%08x", ((long)v) & 0xFFFFFFFFL)).append('\n');
    }

    static void runScenario(int idx, StringBuilder sb) {
        State s = new State();
        switch (idx) {
        case 0: s.status = IN_WATER; break;
        case 1: s.status = IN_WATER; s.forward = 1; s.yaw = 0.0f; break;
        case 2: s.status = IN_WATER; s.forward = 1; s.yaw = 90.0f; break;
        case 3: s.status = IN_WATER; s.strafe = -1; break;
        case 4: s.status = IN_WATER; s.strafe = 1; break;
        case 5: s.status = IN_WATER; s.forward = -1; break;
        case 6: s.status = ON_LAND; s.forward = 1; s.boatGlide = 0.8f; break;
        case 7: s.status = ON_LAND; s.vx = 0.2; s.vz = -0.1; s.boatGlide = 0.8f; break;
        case 8: s.status = IN_AIR; s.forward = 1; s.yaw = 45.0f; break;
        case 9: s.status = IN_WATER; s.forward = 1; s.strafe = -1; break;
        case 10: s.status = IN_WATER; s.strafe = -1; s.yaw = 180.0f; break;
        case 11: s.status = ON_LAND; s.forward = 1; s.boatGlide = 0.4f; s.yaw = -30.0f; break;
        default: break;
        }
        /* Client composed order: updateMotion then controlBoat. */
        float mom = updateMotion(s);
        controlBoat(s);
        emit(sb, s, mom);
    }

    public static void main(String[] args) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < N; ++i) runScenario(i, sb);
        System.out.print(sb);
    }
}
