package netheritemod;

import net.minecraft.block.Block;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.MathHelper;
import net.minecraft.world.World;

/**
 * Semantic camera obs for the chain-RL bridge: a 64x36 pinhole DDA voxel
 * raycast (Amanatides-Woo) from the player eye, mirroring the verified
 * blaze/core/obs_camera.h (oc_pixel) geometry exactly:
 *   - FOV 70 deg vertical: tan(35 deg) as the same double literal (OC_TANY),
 *     horizontal = vertical * (64/36) (OC_TANX)
 *   - 48-block reach (OC_FAR); depth = (int)(t*4) clamped 255; sky = id 0,
 *     depth 255, edge 0
 *   - edge = 1 where the hit point lies within 0.05 (world units) of the
 *     struck face's block border, tested on the two axes tangent to the
 *     entry face
 *   - trig via MathHelper's 65536-entry sin LUT - the exact table mc_sin
 *     replicates - so ray directions match the magma/blaze camera
 * Cells hold PLAIN BLOCK IDS (Block.getIdFromBlock), matching rl_camreg in
 * magma/game/rl_mode.c (ids, not packed states). Row 0 = top.
 */
final class SemanticCamera {
    static final int W = 64, H = 36, NPIX = W * H;
    static final double FAR = 48.0;
    static final double TANY = 0.7002075382097097;      // == OC_TANY literal
    static final double TANX = TANY * (64.0 / 36.0);
    static final double EDGE_W = 0.05;
    /** Fixed eye height (magma PSV_EYE_HEIGHT); rl_mode uses posY + 1.62
     * unconditionally, never the sneak-adjusted live eye height. */
    static final double EYE = 1.62;

    final int[] cam = new int[NPIX];
    final int[] depth = new int[NPIX];
    final int[] edge = new int[NPIX];

    private final BlockPos.MutableBlockPos mpos = new BlockPos.MutableBlockPos();

    private int blockId(World w, int wx, int wy, int wz) {
        if (wy < 0 || wy > 255) return 0;
        mpos.setPos(wx, wy, wz);
        return Block.getIdFromBlock(w.getBlockState(mpos).getBlock());
    }

    /** Render the full frame into cam/depth/edge (row-major, row 0 = top). */
    void render(World w, double ex, double ey, double ez,
                float yawDeg, float pitchDeg) {
        float yaw = yawDeg * (float) (Math.PI / 180.0);
        float pitch = pitchDeg * (float) (Math.PI / 180.0);
        double lx = (double) (-MathHelper.sin(yaw)) * (double) MathHelper.cos(pitch);
        double ly = (double) (-MathHelper.sin(pitch));
        double lz = (double) MathHelper.cos(yaw) * (double) MathHelper.cos(pitch);
        double hn = Math.sqrt(lx * lx + lz * lz + 1e-12);
        double rx = -lz / hn, rz = lx / hn;                       // right
        double ux = -rz * ly, uy = rz * lx - rx * lz, uz = rx * ly; // cross(r,l)
        for (int py = 0; py < H; ++py) {
            double ny = 1.0 - 2.0 * (py + 0.5) / H;
            for (int px = 0; px < W; ++px) {
                double nx = 2.0 * (px + 0.5) / W - 1.0;
                double ddx = lx + nx * TANX * rx + ny * TANY * ux;
                double ddy = ly + ny * TANY * uy;
                double ddz = lz + nx * TANX * rz + ny * TANY * uz;
                double n = Math.sqrt(ddx * ddx + ddy * ddy + ddz * ddz);
                raycast(w, ex, ey, ez, ddx / n, ddy / n, ddz / n, py * W + px);
            }
        }
    }

    private void raycast(World w, double ex, double ey, double ez,
                         double dx, double dy, double dz, int pix) {
        int vx = MathHelper.floor(ex), vy = MathHelper.floor(ey),
            vz = MathHelper.floor(ez);
        int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1, sz = dz > 0 ? 1 : -1;
        double tdx = dx != 0 ? Math.abs(1.0 / dx) : 1e30;
        double tdy = dy != 0 ? Math.abs(1.0 / dy) : 1e30;
        double tdz = dz != 0 ? Math.abs(1.0 / dz) : 1e30;
        double tmx = dx != 0 ? (dx > 0 ? (vx + 1 - ex) : (ex - vx)) * tdx : 1e30;
        double tmy = dy != 0 ? (dy > 0 ? (vy + 1 - ey) : (ey - vy)) * tdy : 1e30;
        double tmz = dz != 0 ? (dz > 0 ? (vz + 1 - ez) : (ez - vz)) * tdz : 1e30;
        double t = 0.0;
        int axis = -1, id = 0;
        while (t < FAR) {
            id = blockId(w, vx, vy, vz);
            if (id != 0) break;
            if (tmx < tmy && tmx < tmz) { t = tmx; vx += sx; tmx += tdx; axis = 0; }
            else if (tmy < tmz)         { t = tmy; vy += sy; tmy += tdy; axis = 1; }
            else                        { t = tmz; vz += sz; tmz += tdz; axis = 2; }
        }
        if (t >= FAR) id = 0;
        if (id == 0) { cam[pix] = 0; depth[pix] = 255; edge[pix] = 0; return; }
        cam[pix] = id;
        int d = (int) (t * 4.0);
        depth[pix] = d > 255 ? 255 : d;
        int eg = 0;
        if (axis >= 0) {
            for (int a = 0; a < 3; ++a) {
                if (a == axis) continue;
                double h = (a == 0 ? ex + t * dx : a == 1 ? ey + t * dy
                                                          : ez + t * dz);
                double f = h - (double) MathHelper.floor(h);
                if (f < EDGE_W || f > 1.0 - EDGE_W) { eg = 1; break; }
            }
        }
        edge[pix] = eg;
    }
}
