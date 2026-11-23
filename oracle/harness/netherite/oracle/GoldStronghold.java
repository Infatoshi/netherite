package netherite.oracle;

import static netherite.oracle.GoldBot.*;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import net.minecraft.block.Block;
import net.minecraft.entity.Entity;
import net.minecraft.entity.item.EntityEnderEye;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.item.Item;
import net.minecraft.util.MovingObjectPosition;

/**
 * GoldBot's stronghold stage: from anywhere on the surface of the overworld
 * with eyes of ender, find the stronghold the way a player does and go
 * through its portal into the End.
 *
 * Eyes are thrown and watched (EntityEnderEye flies straight toward its
 * target: 12 blocks ahead and 8 up while the stronghold is farther than 12
 * blocks, else down to it). Each eye that rises gives a line; two lines give
 * the spot, which the bot walks to over the surface and checks with another
 * throw. An eye that goes down ends over the stronghold: the bot digs a
 * staircase down there until it stands in a stronghold room or corridor,
 * finds the portal frames in the loaded chunks, walks (and digs where it
 * must) up onto the frame ring, mines the silverfish spawner, fills every
 * empty frame and steps into the portal. Dropped eyes are picked up; eyes
 * are thrown only while more than KEEP are held (a player does not know
 * how many frames hold one before it sees the room).
 *
 * Everything here is ordinary input, as in GoldBot; the stage runs with
 * GoldBot.stepFloors set (stairs, slabs and frames are floors).
 */
final class GoldStronghold
{
    private GoldStronghold() {}

    static final int EYE = 381, FRAME = 120, END_PORTAL = 119, STONEBRICK = 98, MONSTER_EGG = 97;
    static final int BUDGET = 24000, KEEP = 12, MAX_THROWS = 12;

    /** This tick's act with the sneak key held as well. */
    static JsonObject sneak(JsonObject a)
    {
        JsonArray l = a.has("hold") ? a.getAsJsonArray("hold") : new JsonArray();
        boolean has = false;
        for (int i = 0; i < l.size(); ++i) if (l.get(i).getAsString().equals("sneak")) has = true;
        if (!has) l.add(new JsonPrimitive("sneak"));
        // sneaking and sprinting do not mix
        JsonArray k = new JsonArray();
        for (int i = 0; i < l.size(); ++i) if (!l.get(i).getAsString().equals("sprint")) k.add(l.get(i));
        a.add("hold", k);
        return a;
    }

    /** A Walk planned over the surface only (no cave below y 58). */
    static final class OverLand extends Task
    {
        final Walk w;

        OverLand(Walk w) { this.w = w; }

        String label() { return "OverLand(" + w.label() + ")"; }

        Object tick()
        {
            planOnly = (a, b, c) -> b >= 58;
            try
            {
                return w.tick();
            }
            finally
            {
                planOnly = null;
            }
        }
    }

    static Walk walkNear(final double tx, final double tz, final double r)
    {
        return new Walk(String.format("near %.1f,%.1f", tx, tz), (a, b, c) ->
        {
            double dx = a + 0.5 - tx, dz = c + 0.5 - tz;
            return dx * dx + dz * dz <= r * r;
        }, 0.4).hint(tx, tz);
    }

    /** Blocks a throw may be aimed through: none that a right click opens,
     * toggles or uses. */
    static boolean plainBlock(Block b)
    {
        int i = Block.getIdFromBlock(b);
        for (int k : new int[] {1, 2, 3, 4, 12, 13, 17, 18, 24, 31, 32, 37, 38, 78, 80, 81, 86, 106, 161, 162, 175})
            if (i == k) return true;
        return false;
    }

    /** One eye thrown the way a player does: the eye in hand, the look up
     * (into the sky where it can), use, then the eye watched until it
     * drops or shatters, and a dropped eye picked up. Leaves where it flew:
     * from (ox, oz) to (ex, ez), and whether it went down. */
    static final class ThrowEye extends Task
    {
        int phase, wait, eye = -1, item = -1;
        float[] yp;
        double ox, oy, oz, ex, ey, ez;
        boolean down, flew;
        final HashSet<Integer> before = new HashSet<Integer>();

        String label() { return "ThrowEye(" + phase + ")"; }

        Object tick()
        {
            switch (phase)
            {
                case 0:
                {
                    if (child == FAIL) return FAIL;
                    int slot = hotbarOf(EYE);
                    if (slot < 0)
                    {
                        if (count(EYE) == 0) return FAIL;
                        return new ToHotbar(EYE);
                    }
                    // stand still first: the eye starts where the player is
                    if (cp.motionX * cp.motionX + cp.motionZ * cp.motionZ > 1.0e-6 && ++wait < 20) return new A().hb(slot).j();
                    yp = null;
                    for (float p : new float[] {-40.0F, -60.0F, -80.0F, -89.0F, -20.0F})
                    {
                        float[] l = {cp.rotationYaw, p};
                        MovingObjectPosition h = ray(l);
                        if (pointed(l) != null) continue;
                        if (h == null || h.typeOfHit != MovingObjectPosition.MovingObjectType.BLOCK || plainBlock(blk(h.blockX, h.blockY, h.blockZ)))
                        {
                            yp = l;
                            break;
                        }
                    }
                    if (yp == null) return FAIL;
                    phase = 1;
                    return new A().look(yp).hb(slot).j();
                }
                case 1:
                    before.clear();
                    for (Object o : ws.loadedEntityList) if (o instanceof EntityEnderEye) before.add(((Entity)o).getEntityId());
                    phase = 2;
                    wait = 0;
                    return new A().look(yp).hb(hotbarOf(EYE)).press("use").j();
                case 2:
                {
                    Entity e = null;
                    if (eye < 0)
                    {
                        for (Object o : ws.loadedEntityList)
                            if (o instanceof EntityEnderEye && !before.contains(((Entity)o).getEntityId())) e = (Entity)o;
                        if (e == null)
                        {
                            if (++wait > 5) return FAIL;
                            return idle();
                        }
                        eye = e.getEntityId();
                        // the spawn point: the position before its first update
                        ox = e.ticksExisted > 0 ? e.lastTickPosX : e.posX;
                        oy = e.ticksExisted > 0 ? e.lastTickPosY : e.posY;
                        oz = e.ticksExisted > 0 ? e.lastTickPosZ : e.posZ;
                    }
                    else e = ws.getEntityByID(eye);
                    if (e != null && !e.isDead)
                    {
                        ex = e.posX;
                        ey = e.posY;
                        ez = e.posZ;
                        return idle();
                    }
                    flew = true;
                    down = ey < oy;
                    double dx = ex - ox, dz = ez - oz;
                    System.out.println(String.format("GOLDBOT t=%d eye from %.2f,%.2f,%.2f to %.2f,%.2f,%.2f (%s, heading %.1f) eyes %d",
                        Oracle.tick, ox, oy, oz, ex, ey, ez, down ? "down" : "up", Math.toDegrees(Math.atan2(-dx, dz)), count(EYE)));
                    phase = 3;
                    wait = 0;
                    // a dropped eye is an item where the eye was
                    for (Object o : ws.loadedEntityList)
                    {
                        if (!(o instanceof EntityItem) || ((Entity)o).isDead) continue;
                        EntityItem it = (EntityItem)o;
                        if (Item.getIdFromItem(it.getEntityItem().getItem()) == EYE && it.getDistanceSq(ex, ey, ez) < 4.0) item = it.getEntityId();
                    }
                    if (item < 0) return DONE;
                    return idle();
                }
                case 3:
                {
                    // let it fall first; one that ends inside the ground is lost
                    Entity it = ws.getEntityByID(item);
                    if (it == null || it.isDead) return DONE;
                    if (!it.onGround && ++wait < 80) return idle();
                    int ix = net.minecraft.util.MathHelper.floor_double(it.posX), iy = net.minecraft.util.MathHelper.floor_double(it.posY + 0.05),
                        iz = net.minecraft.util.MathHelper.floor_double(it.posZ);
                    phase = 4;
                    if (!pass(ix, iy, iz))
                    {
                        System.out.println("GOLDBOT t=" + Oracle.tick + " the dropped eye is inside the ground at " + ix + "," + iy + "," + iz);
                        return DONE;
                    }
                    return new Collect(new int[] {EYE}, 32.0);
                }
                default:
                    System.out.println("GOLDBOT t=" + Oracle.tick + " eyes " + count(EYE));
                    return DONE;
            }
        }
    }

    /** Frames of the portal ring in the loaded chunks within r blocks. */
    static List<int[]> scanFrames(int r)
    {
        int[] c = cell();
        List<int[]> out = new ArrayList<int[]>();
        for (int x = c[0] - r; x <= c[0] + r; ++x)
            for (int z = c[2] - r; z <= c[2] + r; ++z)
            {
                if (!loaded(x, z)) continue;
                for (int y = 1; y < 90; ++y) if (id(x, y, z) == FRAME) out.add(new int[] {x, y, z});
            }
        return out;
    }

    static boolean filled(int[] f)
    {
        return id(f[0], f[1], f[2]) == FRAME && (meta(f[0], f[1], f[2]) & 4) != 0;
    }

    static boolean strongholdFloor(int x, int y, int z)
    {
        int i = id(x, y, z);
        return i == STONEBRICK || i == MONSTER_EGG;
    }

    /** A look whose crosshair lands on frame f (its 13/16 high box), not an
     * entity, from where the player stands. */
    static float[] aimFrame(int[] f)
    {
        List<double[]> pts = new ArrayList<double[]>();
        double[] o = {0.5, 0.3, 0.7};
        for (double a : o)
            for (double b : o) pts.add(new double[] {f[0] + a, f[1] + 0.8125, f[2] + b});
        for (double a : o)
            for (double h : new double[] {0.6, 0.4, 0.75})
            {
                pts.add(new double[] {f[0], f[1] + h, f[2] + a});
                pts.add(new double[] {f[0] + 1.0, f[1] + h, f[2] + a});
                pts.add(new double[] {f[0] + a, f[1] + h, f[2]});
                pts.add(new double[] {f[0] + a, f[1] + h, f[2] + 1.0});
            }
        final double px = cp.posX, py = cp.posY, pz = cp.posZ;
        pts.sort((p, q) -> Double.compare(d2(p, px, py, pz), d2(q, px, py, pz)));
        for (double[] p : pts)
        {
            if (d2(p, px, py, pz) > 4.3 * 4.3) break;
            float[] yp = lookAt(p[0], p[1], p[2]);
            if (hits(ray(yp), f[0], f[1], f[2]) && pointed(yp) == null) return yp;
        }
        return null;
    }

    /** Put an eye into every empty frame: the nearest one the crosshair
     * reaches, else walk (sneaking, so the ring's edge holds the player)
     * to a cell that sees it. */
    static final class FillFrames extends Task
    {
        final List<int[]> frames;
        final HashSet<Long> bad = new HashSet<Long>();
        final java.util.HashMap<Long, Integer> tries = new java.util.HashMap<Long, Integer>();
        int phase, wait, walks, still;
        int[] f;
        float[] yp;
        Walk w;

        FillFrames(List<int[]> frames) { this.frames = frames; }

        String label() { return "FillFrames(" + left() + " empty)"; }

        int left()
        {
            int n = 0;
            for (int[] x : frames) if (!filled(x)) ++n;
            return n;
        }

        Object tick()
        {
            if (child == FAIL) return FAIL;
            child = null;
            if (w != null)
            {
                Object r = w.tick();
                if (r instanceof JsonObject) return sneak((JsonObject)r);
                w = null;
                if (r == FAIL)
                {
                    int[] c = cell();
                    bad.add(key(c[0], c[1], c[2]));
                }
                phase = 0;
                return sneak(idle());
            }
            if (phase == 2)
            {
                if (filled(f)) phase = 0;
                else if (++wait > 8) phase = 0;
                else return sneak(idle());
            }
            if (left() == 0) return DONE;
            int slot = hotbarOf(EYE);
            if (slot < 0)
            {
                if (count(EYE) == 0) throw new Stuck("no eye left for " + left() + " empty frames");
                return new ToHotbar(EYE);
            }
            // the nearest empty frame not given up on
            f = null;
            double best = 1e18;
            for (int[] x : frames)
            {
                if (filled(x)) continue;
                Integer n = tries.get(key(x[0], x[1], x[2]));
                if (n != null && n > 6) continue;
                double d = d2(new double[] {x[0] + 0.5, x[1] + 0.8, x[2] + 0.5}, cp.posX, cp.posY, cp.posZ);
                if (d < best)
                {
                    best = d;
                    f = x;
                }
            }
            if (f == null) return FAIL;
            // aim standing still
            if (cp.motionX * cp.motionX + cp.motionZ * cp.motionZ > 1.0e-5 && ++still < 20) return sneak(new A().hb(slot).j());
            still = 0;
            float[] l = aimFrame(f);
            if (l == null)
            {
                if (++walks > 30) return FAIL;
                phase = 0;
                w = walkToSee(f[0], f[1], f[2], 4.0, bad);
                return tick();
            }
            if (phase == 0 || yp == null || Math.abs(yp[0] - l[0]) > 0.5F || Math.abs(yp[1] - l[1]) > 0.5F
                || !hits(ray(yp), f[0], f[1], f[2]) || pointed(yp) != null)
            {
                yp = l;
                phase = 1;
                return sneak(new A().look(yp).hb(slot).j());
            }
            // the look set last tick still lands on the frame: use
            long k = key(f[0], f[1], f[2]);
            tries.put(k, tries.containsKey(k) ? tries.get(k) + 1 : 1);
            phase = 2;
            wait = 0;
            return sneak(new A().look(yp).hb(slot).press("use").j());
        }
    }

    /** Walk into the lit portal until the server moves the player to the End. */
    static final class EnterEnd extends Task
    {
        final int[] lo, hi;
        final int y;
        int t;

        EnterEnd(int[] lo, int[] hi, int y)
        {
            this.lo = lo;
            this.hi = hi;
            this.y = y;
        }

        String label() { return "EnterEnd(" + t + ")"; }

        Object tick()
        {
            if (sp.dimension == 1) return DONE;
            if (++t > 400) return FAIL;
            double tx = (lo[0] + hi[0] + 1) / 2.0, tz = (lo[2] + hi[2] + 1) / 2.0;
            float[] l = lookAt(tx, cp.posY, tz);
            l[1] = 30.0F;
            return new A().look(l).hold("forward").j();
        }
    }

    /** Stand while a hostile is within 10 blocks (Defend fights one that
     * comes within reach), at most max ticks. */
    static final class WaitClear extends Task
    {
        final int max;
        int t;

        WaitClear(int max) { this.max = max; }

        Object tick()
        {
            if (threat(10.0) == null || ++t > max) return DONE;
            return idle();
        }
    }

    /** The stage itself. */
    static final class SegStronghold extends Seg
    {
        final List<double[]> lines = new ArrayList<double[]>();
        double[] est;
        double nearX, nearZ, walkX, walkZ, walkFrom;
        int throwsDone;
        ThrowEye cur;
        List<int[]> frames;
        int[] ringLo, ringHi, spawner;
        int ringY;

        SegStronghold()
        {
            stepFloors = true;
        }

        /** The crossing of line (ox, oz, ux, uz) with the earlier line most
         * across it, when both point ahead to it. */
        double[] cross(double[] n)
        {
            double[] bestL = null;
            double bestS = 0.0;
            for (double[] m : lines)
            {
                double s = Math.abs(m[2] * n[3] - m[3] * n[2]);
                if (s > bestS)
                {
                    bestS = s;
                    bestL = m;
                }
            }
            if (bestL == null || bestS < Math.sin(Math.toRadians(4.0))) return null;
            double[] m = bestL;
            double c = m[2] * n[3] - m[3] * n[2];
            double wx = n[0] - m[0], wz = n[1] - m[1];
            double t1 = (wx * n[3] - wz * n[2]) / c, t2 = (wx * m[3] - wz * m[2]) / c;
            if (t1 < 0.0 || t2 < -12.0 || t2 > 3000.0) return null;
            return new double[] {m[0] + t1 * m[2], m[1] + t1 * m[3]};
        }

        Object tick()
        {
            switch (phase)
            {
                case 0:
                    need(sp.dimension == 0, "not in the overworld");
                    need(count(EYE) > 0, "no eyes of ender");
                    System.out.println("GOLDBOT t=" + Oracle.tick + " stronghold stage with " + count(EYE) + " eyes, armor " + armorWorn() + ", sword slot " + swordSlot());
                    phase = 10;
                    // G28 on in the chain: the trek walked here by the
                    // stronghold's position, and every eye goes into a frame
                    // (one breaks on a throw one time in five), so no throw:
                    // dig down to the ring when its frames are loaded here
                    if (seg.startsWith("G") && segNum() >= 28 && count(EYE) <= KEEP)
                    {
                        List<int[]> fr = scanFrames(64);
                        if (!fr.isEmpty())
                        {
                            double sx = 0.0, sz = 0.0;
                            for (int[] f : fr)
                            {
                                sx += f[0] + 0.5;
                                sz += f[2] + 0.5;
                            }
                            nearX = sx / fr.size();
                            nearZ = sz / fr.size();
                            System.out.println(String.format("GOLDBOT t=%d the stronghold is known: %d frames about %.1f,%.1f, no throw", Oracle.tick,
                                fr.size(), nearX, nearZ));
                            phase = 30;
                        }
                    }
                    return idle();
                case 10:
                    // at the reserve an estimate is dug at; with none yet a
                    // player has to throw anyway
                    if ((count(EYE) <= KEEP && est != null) || throwsDone >= MAX_THROWS)
                    {
                        need(est != null, "no eyes to spare and no estimate");
                        System.out.println(String.format("GOLDBOT t=%d no more throws: digging at the estimate %.1f,%.1f", Oracle.tick, est[0], est[1]));
                        nearX = est[0];
                        nearZ = est[1];
                        phase = 30;
                        return idle();
                    }
                    ++throwsDone;
                    cur = new ThrowEye();
                    phase = 11;
                    return cur;
                case 11:
                {
                    need(child != FAIL && cur.flew, "the eye was not thrown");
                    if (cur.down)
                    {
                        nearX = cur.ex;
                        nearZ = cur.ez;
                        phase = 30;
                        return idle();
                    }
                    double dx = cur.ex - cur.ox, dz = cur.ez - cur.oz, d = Math.sqrt(dx * dx + dz * dz);
                    need(d > 0.5, "the eye did not move");
                    double[] n = {cur.ox, cur.oz, dx / d, dz / d};
                    boolean restart = false;
                    if (est != null)
                    {
                        // the eye rose, so the stronghold is over 12 blocks off; an
                        // estimate nearer than that, or off this line, is stale (the
                        // eyes led to the start chunk until the stronghold was
                        // generated, and to its portal room since)
                        double ax = est[0] - n[0], az = est[1] - n[1];
                        double along = ax * n[2] + az * n[3], off = Math.abs(ax * n[3] - az * n[2]);
                        if (Math.sqrt(ax * ax + az * az) < 12.0 || off > 8.0 || along < 0.0)
                        {
                            System.out.println(String.format("GOLDBOT t=%d the estimate %.1f,%.1f is stale: new lines from here", Oracle.tick, est[0], est[1]));
                            lines.clear();
                            est = null;
                            restart = true;
                        }
                    }
                    double[] e = cross(n);
                    lines.add(n);
                    double tx, tz;
                    if (e != null)
                    {
                        est = e;
                        tx = e[0];
                        tz = e[1];
                    }
                    else if (lines.size() == 1)
                    {
                        // ahead and to the side, for a second line across the first
                        // (a short way when the stronghold is known to be near)
                        double ahead = restart ? 16.0 : 40.0, side = restart ? 16.0 : 32.0;
                        tx = n[0] + ahead * n[2] - side * n[3];
                        tz = n[1] + ahead * n[3] + side * n[2];
                    }
                    else
                    {
                        tx = n[0] + 64.0 * n[2];
                        tz = n[1] + 64.0 * n[3];
                    }
                    System.out.println(String.format("GOLDBOT t=%d line %d, %s: walking to %.1f,%.1f", Oracle.tick, lines.size(),
                        e == null ? "no crossing" : String.format("stronghold near %.1f,%.1f", e[0], e[1]), tx, tz));
                    phase = 12;
                    walkX = tx;
                    walkZ = tz;
                    walkFrom = Math.hypot(cp.posX - tx, cp.posZ - tz);
                    return new OverLand(walkNear(tx, tz, 4.0));
                }
                case 12:
                    if (child == FAIL)
                    {
                        // a long way (the walker gives up after 40 legs): on
                        // while it still gets nearer, else throw from here
                        double d = Math.hypot(cp.posX - walkX, cp.posZ - walkZ);
                        if (d < walkFrom - 16.0)
                        {
                            System.out.println(String.format("GOLDBOT t=%d walk ended %.0f blocks short of %.1f,%.1f: on", Oracle.tick, d, walkX, walkZ));
                            walkFrom = d;
                            return new OverLand(walkNear(walkX, walkZ, 4.0));
                        }
                        System.out.println("GOLDBOT t=" + Oracle.tick + " walk failed at " + where() + ": throwing from here");
                    }
                    phase = 10;
                    return idle();
                case 30:
                {
                    final double hx = nearX, hz = nearZ;
                    System.out.println(String.format("GOLDBOT t=%d digging down to the stronghold near %.1f,%.1f", Oracle.tick, hx, hz));
                    phase = 31;
                    DigTo d = new DigTo("the stronghold", (a, b, c) ->
                    {
                        if (b > 60 || !pass(a, b, c) || !pass(a, b + 1, c) || !strongholdFloor(a, b - 1, c)) return false;
                        double dx = a + 0.5 - hx, dz = c + 0.5 - hz;
                        return dx * dx + dz * dz < 48.0 * 48.0;
                    }, hx, 30.0, hz);
                    // seed 1: its stronghold lies under extreme hills (150,000
                    // nodes ran out from 35 blocks off)
                    if (s1)
                    {
                        d.maxNodes = 600000;
                        d.heur = 6.0;
                    }
                    return d;
                }
                case 31:
                {
                    need(child != FAIL, "no way down to the stronghold");
                    frames = scanFrames(64);
                    need(!frames.isEmpty(), "no portal frame in the loaded chunks");
                    ringLo = new int[] {Integer.MAX_VALUE, 0, Integer.MAX_VALUE};
                    ringHi = new int[] {Integer.MIN_VALUE, 0, Integer.MIN_VALUE};
                    int filledN = 0;
                    for (int[] f : frames)
                    {
                        ringLo[0] = Math.min(ringLo[0], f[0]);
                        ringLo[2] = Math.min(ringLo[2], f[2]);
                        ringHi[0] = Math.max(ringHi[0], f[0]);
                        ringHi[2] = Math.max(ringHi[2], f[2]);
                        ringY = f[1];
                        if (filled(f)) ++filledN;
                    }
                    spawner = null;
                    for (int x = ringLo[0] - 6; x <= ringHi[0] + 6 && spawner == null; ++x)
                        for (int z = ringLo[2] - 8; z <= ringHi[2] + 8 && spawner == null; ++z)
                            for (int y = ringY - 3; y <= ringY + 3; ++y)
                                if (id(x, y, z) == SPAWNER) spawner = new int[] {x, y, z};
                    System.out.println("GOLDBOT t=" + Oracle.tick + " in the stronghold at " + where() + ": " + frames.size() + " frames (" + filledN
                        + " with an eye) at y " + ringY + " x " + ringLo[0] + ".." + ringHi[0] + " z " + ringLo[2] + ".." + ringHi[2]
                        + (spawner == null ? ", no spawner" : ", spawner at " + spawner[0] + "," + spawner[1] + "," + spawner[2]));
                    phase = 32;
                    // along the corridors: the planner's heuristic barely weighted
                    digWeight = 1.2;
                    if (spawner != null)
                        return new DigTo("the spawner", spawnerView(), spawner[0] + 0.5, spawner[1], spawner[2] + 0.5);
                    return idle();
                }
                case 32:
                    digWeight = 3.0;
                    if (child == FAIL) System.out.println("GOLDBOT t=" + Oracle.tick + " no way to a cell seeing the spawner");
                    phase = 33;
                    // from the floor side, away from the ring (a silverfish's
                    // knockback on the platform can throw the player into it)
                    if (child != FAIL && spawner != null && id(spawner[0], spawner[1], spawner[2]) == SPAWNER)
                        return new Mine(spawner[0], spawner[1], spawner[2], true);
                    return idle();
                case 33:
                    if (child == FAIL) System.out.println("GOLDBOT t=" + Oracle.tick + " the spawner was not mined");
                    else if (spawner != null) System.out.println("GOLDBOT t=" + Oracle.tick + " the spawner is mined");
                    phase = 34;
                    return new WaitClear(300);
                case 34:
                    // the ring's middle drops into lava: walk sneaking from here
                    sneakMoves = true;
                    digWeight = 1.2;
                    phase = 340;
                    return new DigTo("the frames", ringTop(), (ringLo[0] + ringHi[0] + 1) / 2.0, ringY + 1, (ringLo[2] + ringHi[2] + 1) / 2.0);
                case 340:
                    digWeight = 3.0;
                    need(child != FAIL, "the frames not reached");
                    phase = 35;
                    return new FillFrames(frames);
                case 35:
                {
                    need(child != FAIL, "frames left empty");
                    int n = 0;
                    for (int x = ringLo[0]; x <= ringHi[0]; ++x)
                        for (int z = ringLo[2]; z <= ringHi[2]; ++z)
                            if (id(x, ringY, z) == END_PORTAL) ++n;
                    System.out.println("GOLDBOT t=" + Oracle.tick + " frames filled, " + n + " portal blocks, " + count(EYE) + " eyes left");
                    need(n > 0, "the portal did not light");
                    phase = 36;
                    if (feetY() != ringY + 1) return new DigTo("the frames", ringTop(), (ringLo[0] + ringHi[0] + 1) / 2.0, ringY + 1, (ringLo[2] + ringHi[2] + 1) / 2.0);
                    return idle();
                }
                case 36:
                    need(child != FAIL, "the frames not reached again");
                    sneakMoves = false;
                    phase = 37;
                    return new EnterEnd(new int[] {ringLo[0] + 1, ringY, ringLo[2] + 1}, new int[] {ringHi[0] - 1, ringY, ringHi[2] - 1}, ringY);
                case 37:
                    need(child != FAIL && sp.dimension == 1, "not in the End");
                    System.out.println("GOLDBOT t=" + Oracle.tick + " in the End at " + where());
                    phase = 38;
                    return new Idle(5);
                default:
                    return DONE;
            }
        }

        /** A cell from which the spawner is in reach, two or more blocks off
         * the ring and not above it. */
        Goal spawnerView()
        {
            final int x0 = ringLo[0] - 2, x1 = ringHi[0] + 2, z0 = ringLo[2] - 2, z1 = ringHi[2] + 2, y = ringY + 1;
            final int[] sv = spawner;
            return (a, b, c) -> b <= y && !(a >= x0 && a <= x1 && c >= z0 && c <= z1) && stand(a, b, c)
                && sees(a + 0.5, b + 1.62, c + 0.5, sv[0], sv[1], sv[2], 4.0);
        }

        /** A cell on top of the frame ring (on a frame, or level beside it). */
        Goal ringTop()
        {
            final int x0 = ringLo[0] - 1, x1 = ringHi[0] + 1, z0 = ringLo[2] - 1, z1 = ringHi[2] + 1, y = ringY + 1;
            return (a, b, c) -> b == y && a >= x0 && a <= x1 && c >= z0 && c <= z1 && stand(a, b, c);
        }
    }
}
