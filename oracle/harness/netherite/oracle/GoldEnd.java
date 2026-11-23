package netherite.oracle;

import static netherite.oracle.GoldBot.*;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import net.minecraft.block.Block;
import net.minecraft.client.gui.GuiWinGame;
import net.minecraft.entity.Entity;
import net.minecraft.entity.boss.EntityDragon;
import net.minecraft.entity.boss.EntityDragonPart;
import net.minecraft.entity.item.EntityEnderCrystal;
import net.minecraft.util.AxisAlignedBB;
import net.minecraft.util.MathHelper;
import net.minecraft.util.MovingObjectPosition;
import net.minecraft.util.Vec3;

/**
 * GoldBot in the End (segment END, or any segment whose root is SegEnd):
 * off the arrival platform (bridging with cobble when it floats), every
 * end crystal shot with the bow from a spot the arrow's flight is simulated
 * from, the dragon fought with the sword when its head comes by and the
 * bow when it flies within range, the exit portal reached (pillaring up
 * to its rim when the ground is lower) and entered, the credits skipped,
 * and the respawn in the overworld. Only agent input, like the rest of
 * GoldBot: the same code runs in a pure-play chain segment.
 */
final class GoldEnd
{
    private GoldEnd() {}

    static final int BOW = 261, ARROW = 262, END_PORTAL = 119;
    /** The segment's tick budget (the seed-1 variant needs about 20,300). */
    static final int BUDGET = 40000;
    /** The pitch the End walks keep: endermen stare at eye level. */
    static final float WALK_PITCH = 35.0F;

    /** The brain's root is the End segment. */
    static boolean active()
    {
        return brain != null && brain.stack.peekLast() instanceof SegEnd;
    }

    static void need(boolean ok, String what)
    {
        if (!ok) throw new Stuck(what);
    }

    static void log(String s)
    {
        System.out.println("GOLDBOT t=" + Oracle.tick + " " + s);
    }

    // ------------------------------------------------------------------ reads

    /** The dragon while it is in the End's loaded entities. */
    static EntityDragon dragon()
    {
        for (Object o : ws.loadedEntityList)
            if (o instanceof EntityDragon) return (EntityDragon)o;
        return null;
    }

    static boolean alive(EntityDragon d)
    {
        return d != null && !d.isDead && d.getHealth() > 0.0F;
    }

    /** The crystals seen so far, by position; one is gone once its chunk is
     * loaded and holds no crystal there. */
    static final List<double[]> crystals = new ArrayList<double[]>();

    static void noteCrystals()
    {
        for (Object o : ws.loadedEntityList)
        {
            if (!(o instanceof EntityEnderCrystal) || ((Entity)o).isDead) continue;
            Entity e = (Entity)o;
            boolean known = false;
            for (double[] p : crystals) if (Math.abs(p[0] - e.posX) < 0.5 && Math.abs(p[1] - e.posY) < 0.5 && Math.abs(p[2] - e.posZ) < 0.5) known = true;
            if (!known)
            {
                crystals.add(new double[] {e.posX, e.posY, e.posZ});
                log(String.format("crystal at %.1f,%.1f,%.1f", e.posX, e.posY, e.posZ));
            }
        }
    }

    static EntityEnderCrystal crystalAt(double[] p)
    {
        for (Object o : ws.loadedEntityList)
        {
            if (!(o instanceof EntityEnderCrystal) || ((Entity)o).isDead) continue;
            Entity e = (Entity)o;
            if (Math.abs(p[0] - e.posX) < 0.5 && Math.abs(p[1] - e.posY) < 0.5 && Math.abs(p[2] - e.posZ) < 0.5) return (EntityEnderCrystal)e;
        }
        return null;
    }

    static boolean crystalGone(double[] p)
    {
        return loaded(MathHelper.floor_double(p[0]), MathHelper.floor_double(p[2])) && crystalAt(p) == null;
    }

    /** EntityEnderCrystal's box: 2 wide and 2 high about its position. */
    static AxisAlignedBB crystalBox(double[] p)
    {
        return AxisAlignedBB.getBoundingBox(p[0] - 1.0, p[1] - 1.0, p[2] - 1.0, p[0] + 1.0, p[1] + 1.0, p[2] + 1.0);
    }

    /** Solid ground (within four blocks below) all around a cell: a knock
     * back does not throw the player into the void from here. */
    static boolean inland(int x, int y, int z, int r)
    {
        for (int dx = -r; dx <= r; ++dx)
            for (int dz = -r; dz <= r; ++dz)
            {
                boolean g = false;
                for (int dy = 1; dy <= 5 && !g; ++dy)
                {
                    Block b = blk(x + dx, y - dy, z + dz);
                    if (b == null) return false;
                    if (b.getMaterial().blocksMovement()) g = true;
                }
                if (!g) return false;
            }
        return true;
    }

    /** Would the server player looking along (yaw, pitch) provoke an
     * enderman (EntityEnderman.shouldAttackPlayer)? */
    static boolean staresAny(float[] yp)
    {
        for (Object o : ws.loadedEntityList)
        {
            Entity e = (Entity)o;
            if (!enderman(e) || e.getDistanceSqToEntity(sp) > 64.0 * 64.0 || !nearLoaded(e)) continue;
            if (stare(yp[0], yp[1], e)) return true;
        }
        return false;
    }

    // ------------------------------------------------------------------ the bow

    static boolean arrowSolid(int x, int y, int z)
    {
        Block b = blk(x, y, z);
        return b == null || b.getMaterial().blocksMovement();
    }

    /** The arrow of a full draw from the server player's feet (px, py, pz)
     * along (yaw, pitch): EntityArrow(World, EntityLivingBase, 2.0F) without
     * setThrowableHeading's noise, then its flight (0.99 drag, 0.05 fall).
     * The number of moves before the one that enters box (grown by 0.3, the
     * arrow's entity test), -1 when a block stops it first or never. */
    static int arrowTicks(double px, double py, double pz, float yaw, float pitch, AxisAlignedBB box)
    {
        double[] s = arrowStart(px, py, pz, yaw, pitch);
        double x = s[0], y = s[1], z = s[2], mx = s[3], my = s[4], mz = s[5];
        AxisAlignedBB b = box.expand(0.3, 0.3, 0.3);
        for (int t = 0; t < 120; ++t)
        {
            Vec3 a = Vec3.createVectorHelper(x, y, z), e = Vec3.createVectorHelper(x + mx, y + my, z + mz);
            MovingObjectPosition h = b.calculateIntercept(a, e);
            double len = Math.sqrt(mx * mx + my * my + mz * mz);
            double lim = h != null ? a.distanceTo(h.hitVec) : len;
            int n = Math.max(1, (int)Math.ceil(lim / 0.1));
            for (int i = 1; i <= n; ++i)
            {
                double f = lim / len * i / n;
                if (arrowSolid(MathHelper.floor_double(x + mx * f), MathHelper.floor_double(y + my * f), MathHelper.floor_double(z + mz * f))) return -1;
            }
            if (h != null) return t;
            x += mx;
            y += my;
            z += mz;
            mx *= 0.99;
            my *= 0.99;
            mz *= 0.99;
            my -= 0.05;
            if (y < 0.0) return -1;
        }
        return -1;
    }

    static double[] arrowStart(double px, double py, double pz, float yaw, float pitch)
    {
        float yr = yaw / 180.0F * (float)Math.PI, pr = pitch / 180.0F * (float)Math.PI;
        double x = px - (double)(MathHelper.cos(yr) * 0.16F), y = py + 1.62 - 0.10000000149011612D, z = pz - (double)(MathHelper.sin(yr) * 0.16F);
        double mx = (double)(-MathHelper.sin(yr) * MathHelper.cos(pr)), mz = (double)(MathHelper.cos(yr) * MathHelper.cos(pr)), my = (double)(-MathHelper.sin(pr));
        double n = Math.sqrt(mx * mx + my * my + mz * mz);
        return new double[] {x, y, z, mx / n * 3.0, my / n * 3.0, mz / n * 3.0};
    }

    /** The arrow's height where it has flown d horizontally, NaN if it
     * falls away first. */
    static double heightAt(double px, double py, double pz, float yaw, float pitch, double d)
    {
        double[] s = arrowStart(px, py, pz, yaw, pitch);
        double h = 0, y = s[1], mx = s[3], my = s[4], mz = s[5];
        for (int t = 0; t < 120; ++t)
        {
            double step = Math.sqrt(mx * mx + mz * mz);
            if (h + step >= d) return y + my * ((d - h) / step);
            h += step;
            y += my;
            mx *= 0.99;
            my *= 0.99;
            mz *= 0.99;
            my -= 0.05;
            if (y < -64.0) break;
        }
        return Double.NaN;
    }

    /** A look (yaw, pitch, flight ticks) from feet (px, py, pz) whose
     * arrow meets box moving by (vx, vy, vz) a tick, lag ticks before the
     * arrow's first move; null when no clear shot exists. The flat arc
     * first, else the lob that falls onto the box (a crystal on a wide
     * pillar top cannot be reached rising). */
    static float[] solveShot(double px, double py, double pz, AxisAlignedBB box, double vx, double vy, double vz, int lag)
    {
        float[] r = solveArc(px, py, pz, box, vx, vy, vz, lag, false);
        return r != null ? r : solveArc(px, py, pz, box, vx, vy, vz, lag, true);
    }

    static float[] solveArc(double px, double py, double pz, AxisAlignedBB box, double vx, double vy, double vz, int lag, boolean lob)
    {
        double cx0 = (box.minX + box.maxX) / 2.0, cy0 = (box.minY + box.maxY) / 2.0, cz0 = (box.minZ + box.maxZ) / 2.0;
        int T = (int)(Math.sqrt((cx0 - px) * (cx0 - px) + (cz0 - pz) * (cz0 - pz)) / 2.8) + (lob ? 20 : 0);
        float[] best = null;
        for (int it = 0; it < 4; ++it)
        {
            double k = T + lag;
            double cx = cx0 + vx * k, cy = cy0 + vy * k, cz = cz0 + vz * k;
            float yaw = (float)Math.toDegrees(Math.atan2(-(cx - px), cz - pz));
            double d = Math.sqrt((cx - px) * (cx - px) + (cz - pz) * (cz - pz));
            // the pitch that carries highest at d splits the two arcs
            float a = -89.0F, b = 60.0F;
            for (int i = 0; i < 30; ++i)
            {
                float m1 = a + (b - a) / 3.0F, m2 = b - (b - a) / 3.0F;
                double h1 = heightAt(px, py, pz, yaw, m1, d), h2 = heightAt(px, py, pz, yaw, m2, d);
                if (Double.isNaN(h1) || (!Double.isNaN(h2) && h2 > h1)) a = m1;
                else b = m2;
            }
            float top = (a + b) / 2.0F;
            double ht = heightAt(px, py, pz, yaw, top, d);
            if (Double.isNaN(ht) || ht < cy) return null;
            // bisection on the chosen side: height at d falls away from top
            float lo = lob ? -89.0F : top, hi = lob ? top : 70.0F;
            for (int i = 0; i < 30; ++i)
            {
                float mid = (lo + hi) / 2.0F;
                double y = heightAt(px, py, pz, yaw, mid, d);
                boolean above = !Double.isNaN(y) && y > cy;
                if (lob == above) hi = mid;
                else lo = mid;
            }
            float pitch = (lo + hi) / 2.0F;
            int t = arrowTicks(px, py, pz, yaw, pitch, box.getOffsetBoundingBox(vx * k, vy * k, vz * k));
            if (t < 0) return null;
            best = new float[] {yaw, pitch, t};
            if (t == T || (vx == 0 && vy == 0 && vz == 0)) break;
            T = t;
        }
        return best;
    }

    /** yaw kept next to the client's own (the look act is absolute). */
    static float[] near(float[] s)
    {
        return new float[] {cp.rotationYaw + wrap(s[0] - cp.rotationYaw), s[1]};
    }

    /** Draw the bow and loose one arrow along the look aim() gives (null:
     * no clear shot now; the draw is held). The server reads the look of
     * the tick before the release (C07 comes before that tick's C03). */
    abstract static class Shoot extends Task
    {
        int t, state;
        float[] last;

        abstract float[] aim();

        Object tick()
        {
            if (state == 3) return DONE;
            if (count(ARROW) == 0) return FAIL;
            if (hotbarOf(BOW) < 0) return child == FAIL ? FAIL : new ToHotbar(BOW);
            int bow = hotbarOf(BOW);
            if (++t > 300) return FAIL;
            if (state == 2)
            {
                state = 3;
                return new A().look(last).hb(bow).j();
            }
            float[] a = aim();
            if (state == 0 || cp.inventory.currentItem != bow)
            {
                state = 1;
                last = a != null ? near(a) : new float[] {cp.rotationYaw, cp.rotationPitch};
                return new A().look(last).hb(bow).j();
            }
            if (a != null) last = near(a);
            A act = new A().look(last).hb(bow);
            if (!cp.isUsingItem()) return act.press("use").hold("use").j();
            act.hold("use");
            if (a != null && cp.getItemInUseDuration() >= 20 && !staresAny(last)) state = 2;
            return act.j();
        }
    }

    /** One arrow at a crystal. */
    static final class CrystalShot extends Shoot
    {
        final double[] p;

        CrystalShot(double[] p) { this.p = p; }

        String label() { return "CrystalShot(" + t + ")"; }

        float[] aim()
        {
            return solveShot(sp.posX, sp.posY, sp.posZ, crystalBox(p), 0, 0, 0, 0);
        }
    }

    /** Walk to a cell with a clear arrow to the crystal, far enough from its
     * blast and inland, and shoot until it is gone. */
    static final class ShootCrystal extends Task
    {
        final double[] p;
        final HashMap<Long, Boolean> spots = new HashMap<Long, Boolean>();
        final HashSet<Long> bad = new HashSet<Long>();
        int phase, walks, shots, wait;

        ShootCrystal(double[] p) { this.p = p; }

        String label() { return String.format("ShootCrystal(%.0f,%.0f,%.0f #%d)", p[0], p[1], p[2], shots); }

        boolean spot(int a, int b, int c)
        {
            long k = key(a, b, c);
            if (bad.contains(k)) return false;
            double dx = p[0] - (a + 0.5), dz = p[2] - (c + 0.5), dy = p[1] - (b + 1.62);
            double h = Math.sqrt(dx * dx + dz * dz);
            if (h < 8.0 || h > 40.0 || h * h + dy * dy < 15.0 * 15.0) return false;
            Boolean r = spots.get(k);
            if (r == null)
            {
                r = inland(a, b, c, 2) && solveShot(a + 0.5, b, c + 0.5, crystalBox(p), 0, 0, 0, 0) != null;
                spots.put(k, r);
            }
            return r;
        }

        Object tick()
        {
            if (crystalGone(p)) return DONE;
            switch (phase)
            {
                case 0:
                    if (++walks > 6) return FAIL;
                    phase = 1;
                    return new Walk("a shot at the crystal", this::spot, 0.3).hint(p[0], p[2]);
                case 1:
                    if (child == FAIL)
                    {
                        phase = 0;
                        return idle();
                    }
                    if (solveShot(sp.posX, sp.posY, sp.posZ, crystalBox(p), 0, 0, 0, 0) == null)
                    {
                        int[] c = cell();
                        bad.add(key(c[0], c[1], c[2]));
                        phase = 0;
                        return idle();
                    }
                    phase = 2;
                    ++shots;
                    return new CrystalShot(p);
                case 2:
                    phase = 3;
                    wait = 0;
                    // fall through
                default:
                    if (++wait < 80) return new A().look(new float[] {cp.rotationYaw, cp.rotationPitch}).j();
                    if (shots % 3 == 0)
                    {
                        int[] c = cell();
                        bad.add(key(c[0], c[1], c[2]));
                        phase = 0;
                    }
                    else phase = 1;
                    return idle();
            }
        }
    }

    // ------------------------------------------------------------------ the dragon

    /** The client's part the crosshair can hit now, head first, with the
     * look that hits it; null when none is in reach. */
    static Object[] partInReach(EntityDragon d)
    {
        Entity ce = mc.theWorld.getEntityByID(d.getEntityId());
        if (!(ce instanceof EntityDragon)) return null;
        EntityDragon cd = (EntityDragon)ce;
        EntityDragonPart[] order = {cd.dragonPartHead, cd.dragonPartBody, cd.dragonPartWing1, cd.dragonPartWing2,
            cd.dragonPartTail1, cd.dragonPartTail2, cd.dragonPartTail3};
        double[][] offs = {{0.5, 0.5, 0.5}, {0.5, 0.2, 0.5}, {0.5, 0.8, 0.5}, {0.2, 0.5, 0.5}, {0.8, 0.5, 0.5}, {0.5, 0.5, 0.2}, {0.5, 0.5, 0.8}};
        for (EntityDragonPart part : order)
        {
            AxisAlignedBB bb = part.boundingBox;
            double cx = Math.max(bb.minX, Math.min(bb.maxX, cp.posX)), cy = Math.max(bb.minY, Math.min(bb.maxY, cp.posY)), cz = Math.max(bb.minZ, Math.min(bb.maxZ, cp.posZ));
            if ((cx - cp.posX) * (cx - cp.posX) + (cy - cp.posY) * (cy - cp.posY) + (cz - cp.posZ) * (cz - cp.posZ) > 3.2 * 3.2) continue;
            List<double[]> pts = new ArrayList<double[]>();
            pts.add(new double[] {cx, cy, cz});
            for (double[] o : offs)
                pts.add(new double[] {bb.minX + (bb.maxX - bb.minX) * o[0], bb.minY + (bb.maxY - bb.minY) * o[1], bb.minZ + (bb.maxZ - bb.minZ) * o[2]});
            for (double[] q : pts)
            {
                float[] l = lookAt(q[0], q[1], q[2]);
                if (pointed(l) == part) return new Object[] {part, l};
            }
        }
        return null;
    }

    /** A sword swing at the dragon when a part is in reach, else null. */
    static com.google.gson.JsonObject melee(EntityDragon d)
    {
        Object[] r = partInReach(d);
        if (r == null) return null;
        A a = new A().look((float[])r[1]).hb(swordSlot());
        // a block (or a draw) ends this tick; the swing waits for the next
        if (d.hurtResistantTime <= 10 && cp.inventory.currentItem == swordSlot() && !cp.isUsingItem()) a.press("attack");
        return a.j();
    }

    /** The head part's distance from the eye. */
    static double headDist(EntityDragon d)
    {
        AxisAlignedBB bb = d.dragonPartHead.boundingBox;
        double ex = sp.posX, ey = sp.posY + 1.62, ez = sp.posZ;
        double cx = Math.max(bb.minX, Math.min(bb.maxX, ex)), cy = Math.max(bb.minY, Math.min(bb.maxY, ey)), cz = Math.max(bb.minZ, Math.min(bb.maxZ, ez));
        return Math.sqrt((cx - ex) * (cx - ex) + (cy - ey) * (cy - ey) + (cz - ez) * (cz - ez));
    }

    /** Eat to a full bar (natural regeneration needs 18), stopping when
     * the dragon's head comes within 16 blocks. */
    static final class EndEat extends Interrupt
    {
        int t;

        String label() { return "EndEat(" + t + ")"; }

        Object tick()
        {
            int slot = hotbarOf(COOKED_MEAT);
            EntityDragon d = dragon();
            boolean close = alive(d) && headDist(d) < 16.0;
            if (sp.getFoodStats().getFoodLevel() >= 20 || slot < 0 || t > 200 || close) return DONE;
            ++t;
            return new A().look(new float[] {cp.rotationYaw, cp.rotationPitch}).hb(slot).hold("use").j();
        }
    }

    /** The dragon's head came close while the bot did something else. */
    static final class DragonHit extends Interrupt
    {
        int t;

        String label() { return "DragonHit(" + t + ")"; }

        Object tick()
        {
            EntityDragon d = dragon();
            if (!alive(d) || headDist(d) > 7.0 || ++t > 60) return DONE;
            com.google.gson.JsonObject m = melee(d);
            if (m != null) return m;
            AxisAlignedBB bb = d.dragonPartHead.boundingBox;
            return new A().look(lookAt((bb.minX + bb.maxX) / 2, (bb.minY + bb.maxY) / 2, (bb.minZ + bb.maxZ) / 2)).hb(swordSlot()).j();
        }
    }

    /** A walk kept on the island's top (no cell below y floor, and with
     * inland set none with the void beside it): a chase must not follow
     * the dragon off an edge. */
    static final class TopWalk extends Task
    {
        final Walk w;
        final int floor;
        final boolean inl;

        TopWalk(Walk w, int floor, boolean inl)
        {
            this.w = w;
            this.floor = floor;
            this.inl = inl;
        }

        String label() { return "Top" + w.label(); }

        Object tick()
        {
            planOnly = (a, b, c) -> b >= floor && (!inl || inland(a, b, c, 1));
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

    /** The lowest feet y a fight walk may use: six below the first spot. */
    static int topFloor = 0;

    // ------------------------------------------------------------------ loaded chunks

    /** PlayerManager's view radius (the server's view distance). */
    static int viewRadius()
    {
        return sp.mcServer.getConfigurationManager().getViewDistance();
    }

    /** The chunk PlayerManager centres the player's view on: its managed
     * position, moved only after 8 blocks, by (int) then >> 4. */
    static int[] viewChunk()
    {
        return new int[] {(int)sp.managedPosX >> 4, (int)sp.managedPosZ >> 4};
    }

    static int[] viewChunkAt(double x, double z)
    {
        return new int[] {(int)x >> 4, (int)z >> 4};
    }

    static boolean inView(int cx, int cz, int[] v)
    {
        int r = viewRadius();
        return Math.abs(cx - v[0]) <= r && Math.abs(cz - v[1]) <= r;
    }

    /** Would an entity at (x, z) update with the player's view on chunk v?
     * World.updateEntityWithOptionalForce needs every chunk within 32
     * blocks loaded; a chunk counts when v's view holds it, or when it is
     * loaded now and not only by the current view (that one unloads when
     * the view leaves it). */
    static boolean frees(double x, double z, int[] v)
    {
        int[] cur = viewChunk();
        int i = MathHelper.floor_double(x), k = MathHelper.floor_double(z);
        for (int cx = (i - 32) >> 4; cx <= (i + 32) >> 4; ++cx)
            for (int cz = (k - 32) >> 4; cz <= (k + 32) >> 4; ++cz)
            {
                if (inView(cx, cz, v)) continue;
                if (ws.getChunkProvider().chunkExists(cx, cz) && !inView(cx, cz, cur)) continue;
                return false;
            }
        return true;
    }

    /** How much of the dragon's range (its random targets lie within 60 of
     * the middle) it could fly with the view on chunk v. */
    static int freeScore(int[] v)
    {
        int n = 0;
        for (int gx = -64; gx <= 64; gx += 8)
            for (int gz = -64; gz <= 64; gz += 8)
                if (frees(gx, gz, v)) ++n;
        return n;
    }

    // ------------------------------------------------------------------ the fight

    /** A cell beside a chunk corner: getMouseOver's and an arrow's entity
     * queries reach the chunks within two blocks of their boxes, and a
     * part is found only through the chunk its dragon is filed in (by its
     * middle), so from a corner the four chunks round the player hold the
     * dragon whenever a part is in reach. */
    static boolean corner(int a, int c)
    {
        return ((a & 15) == 0 || (a & 15) == 15) && ((c & 15) == 0 || (c & 15) == 15);
    }

    /** The fight spots near the middle, best first: corner cells on the top,
     * inland, ordered by how much of the dragon's range their view keeps
     * loaded, then by distance from the middle. */
    static List<int[]> fightSpots(HashSet<Long> bad)
    {
        List<int[]> out = new ArrayList<int[]>();
        final HashMap<Long, Integer> score = new HashMap<Long, Integer>();
        for (int i = -3; i <= 3; ++i)
            for (int j = -3; j <= 3; ++j)
                for (int di = -1; di <= 0; ++di)
                    for (int dj = -1; dj <= 0; ++dj)
                    {
                        int x = 16 * i + di, z = 16 * j + dj;
                        if (!loaded(x, z) || (double)x * x + (double)z * z > 48.0 * 48.0) continue;
                        for (int y = 100; y >= Math.max(topFloor, 40); --y)
                        {
                            if (!stand(x, y, z)) continue;
                            long k = key(x, y, z);
                            if (!bad.contains(k) && inland(x, y, z, 3))
                            {
                                out.add(new int[] {x, y, z});
                                score.put(k, freeScore(viewChunkAt(x + 0.5, z + 0.5)));
                            }
                            break;
                        }
                    }
        out.sort((p, q) ->
        {
            int a = score.get(key(p[0], p[1], p[2])), b = score.get(key(q[0], q[1], q[2]));
            if (a != b) return b - a;
            return Double.compare((double)p[0] * p[0] + (double)p[2] * p[2], (double)q[0] * q[0] + (double)q[2] * q[2]);
        });
        return out;
    }

    /** A dragon that stopped where the loaded chunks end (or was saved with
     * its chunk): bring the view to a chunk that frees it, walking on the
     * top, else from the nearest edge bridging out over the void; then wait
     * until it flies. */
    static final class Unfreeze extends Task
    {
        final double x, y, z;
        int phase, t;
        double lx = Double.NaN, ly, lz;

        Unfreeze(double x, double y, double z)
        {
            this.x = x;
            this.y = y;
            this.z = z;
        }

        String label() { return "Unfreeze#" + phase; }

        boolean free() { return frees(x, z, viewChunk()); }

        Object tick()
        {
            if (++t > 3000) return FAIL;
            switch (phase)
            {
                case 0:
                {
                    if (free())
                    {
                        phase = 4;
                        return idle();
                    }
                    phase = 1;
                    final double mx = sp.managedPosX, mz = sp.managedPosZ;
                    return new TopWalk(new Walk("a view that frees the dragon", (a, b, c) ->
                        ((a + 0.5 - mx) * (a + 0.5 - mx) + (c + 0.5 - mz) * (c + 0.5 - mz) >= 72.0) && frees(x, z, viewChunkAt(a + 0.5, c + 0.5))
                        && inland(a, b, c, 2), 0.5).hint(x, z), topFloor, true);
                }
                case 1:
                    if (child != FAIL)
                    {
                        phase = 4;
                        return idle();
                    }
                    // no such cell on the island: its edge nearest the dragon
                    phase = 2;
                    return new TopWalk(new Walk("the edge toward the dragon", (a, b, c) -> false, 0.3).hint(x, z), topFloor, false);
                case 2:
                {
                    phase = 3;
                    int[] c = cell();
                    double dx = x - (c[0] + 0.5), dz = z - (c[2] + 0.5);
                    int bx = Math.abs(dx) >= Math.abs(dz) ? (dx > 0 ? 1 : -1) : 0, bz = bx == 0 ? (dz > 0 ? 1 : -1) : 0;
                    log(String.format("bridging out %d,%d from %s toward the dragon at %.1f,%.1f,%.1f", bx, bz, where(), x, y, z));
                    Bridge b = new Bridge(bx, bz, c[1] - 1);
                    b.stop = this::free;
                    b.max = 24;
                    return b;
                }
                case 3:
                    phase = 4;
                    // fall through
                default:
                {
                    EntityDragon d = dragon();
                    boolean moved = d != null && !Double.isNaN(lx) && (d.posX != lx || d.posY != ly || d.posZ != lz);
                    if (d != null)
                    {
                        lx = d.posX;
                        ly = d.posY;
                        lz = d.posZ;
                    }
                    if (moved) return DONE;
                    if (++phase > 4 + 200) return FAIL;
                    return new A().hold("sneak").j();
                }
            }
        }
    }

    /** Fight the dragon from a chunk corner near the island's middle: the
     * sword when a part is in reach, the bow when it flies within range.
     * A dragon out of the loaded chunks (frozen at their edge, or saved
     * with its chunk) is freed by moving the view. */
    static final class DragonFight extends Task
    {
        int[] spot;
        int walks, still, unfreezes, middleDigs;
        boolean releasing, seeking;
        final HashSet<Long> badSpots = new HashSet<Long>();
        List<int[]> spots;
        int spotIndex;
        float[] last;
        double hx = Double.NaN, hy, hz, vx, vy, vz, lx = Double.NaN, ly, lz;
        float lastHealth = -1;
        long gone = -1;

        String label() { return "DragonFight(" + (int)lastHealth + ")"; }

        /** Walk to the best fight spot; the next one when a walk fails. */
        Object seek()
        {
            if (spots == null || spotIndex >= spots.size())
            {
                spots = fightSpots(badSpots);
                spotIndex = 0;
                // seed 1: none reachable from here (below the fight floor,
                // at the island's edge): a dig back toward the middle, then
                // every spot again
                if (spots.isEmpty() && s1 && ++middleDigs <= 3)
                {
                    badSpots.clear();
                    log("no fight spot reachable from " + where() + ": a dig toward the middle");
                    final int fl = topFloor;
                    return new DigTo("toward the middle", (a, b, c) -> (double)a * a + (double)c * c < 30.0 * 30.0 && b >= fl && inland(a, b, c, 2),
                        0.0, Math.max(fl, 60), 0.0);
                }
                need(!spots.isEmpty(), "no fight spot");
            }
            int[] p = spots.get(spotIndex++);
            seeking = true;
            spot = p;
            return new TopWalk(walkToCell(p[0], p[1], p[2], 0.4), topFloor == 0 ? p[1] - 6 : topFloor, false);
        }

        Object tick()
        {
            if (seeking)
            {
                seeking = false;
                if (child == FAIL)
                {
                    badSpots.add(key(spot[0], spot[1], spot[2]));
                    return seek();
                }
                spots = null;
                if (topFloor == 0) topFloor = spot[1] - 6;
                log("fight spot " + spot[0] + "," + spot[1] + "," + spot[2] + " view " + viewChunk()[0] + "," + viewChunk()[1]);
            }
            child = null;
            EntityDragon d = dragon();
            if (d == null)
            {
                // saved with its chunk where it was seen last
                if (gone < 0) gone = Oracle.tick;
                need(!Double.isNaN(lx), "no dragon");
                if (Oracle.tick - gone > 20)
                {
                    need(++unfreezes < 30, "the dragon stays away");
                    log(String.format("dragon unloaded, last at %.1f,%.1f,%.1f", lx, ly, lz));
                    gone = -1;
                    spot = null;
                    return new Unfreeze(lx, ly, lz);
                }
                return idle();
            }
            gone = -1;
            if (!alive(d)) return DONE;
            if (d.getHealth() != lastHealth)
            {
                log(String.format("dragon health %.1f at %.1f,%.1f,%.1f", d.getHealth(), d.posX, d.posY, d.posZ));
                lastHealth = d.getHealth();
            }
            still = d.posX == lx && d.posY == ly && d.posZ == lz ? still + 1 : 0;
            lx = d.posX;
            ly = d.posY;
            lz = d.posZ;
            if (spot == null) return seek();
            if (still > 20 && !cp.isUsingItem())
            {
                need(++unfreezes < 30, "the dragon stays away");
                log(String.format("dragon still at %.1f,%.1f,%.1f", lx, ly, lz));
                still = 0;
                spot = null;
                return new Unfreeze(lx, ly, lz);
            }
            AxisAlignedBB hb = d.dragonPartHead.boundingBox;
            double cx = (hb.minX + hb.maxX) / 2, cy = (hb.minY + hb.maxY) / 2, cz = (hb.minZ + hb.maxZ) / 2;
            if (!Double.isNaN(hx))
            {
                vx = cx - hx;
                vy = cy - hy;
                vz = cz - hz;
            }
            hx = cx;
            hy = cy;
            hz = cz;
            double dist = headDist(d);
            if (releasing)
            {
                releasing = false;
                return new A().look(last).hb(hotbarOf(BOW)).j();
            }
            com.google.gson.JsonObject m = melee(d);
            if (m != null) return m;
            int[] c = cell();
            if (cp.onGround && !cp.isUsingItem() && (Math.abs(c[0] - spot[0]) > 1 || Math.abs(c[2] - spot[2]) > 1) && dist > 16.0 && walks < 80)
            {
                ++walks;
                return new TopWalk(walkToCell(spot[0], spot[1], spot[2], 0.5), topFloor, false);
            }
            int bow = hotbarOf(BOW);
            if (dist > 7.0 && dist < 48.0 && bow >= 0 && count(ARROW) > 0)
            {
                float[] a = solveShot(sp.posX, sp.posY, sp.posZ, target(d), vx, vy, vz, 3);
                if (a != null) last = near(a);
                else if (last == null || cp.inventory.currentItem != bow) last = near(lookAt(cx, cy, cz));
                A act = new A().look(last).hb(bow);
                if (cp.inventory.currentItem != bow) return act.j();
                if (!cp.isUsingItem()) return act.press("use").hold("use").j();
                act.hold("use");
                if (a != null && cp.getItemInUseDuration() >= 20 && !staresAny(last)) releasing = true;
                return act.j();
            }
            // close or out of range: the sword ready, facing the head, and
            // blocking while it closes in (EntityPlayer.damageEntity halves
            // a blocked bite)
            float[] l = lookAt(cx, cy, cz);
            if (staresAny(l)) l[1] = Math.max(-90.0F, l[1] - 15.0F);
            A act = new A().look(l).hb(swordSlot());
            if (dist < 14.0 && swordSlot() >= 0 && cp.inventory.currentItem == swordSlot())
                act.hold("use");
            return act.j();
        }

        /** The head, when an arrow there finds the chunk the dragon is filed
         * in (its middle within the arrow query's reach), else the body. */
        AxisAlignedBB target(EntityDragon d)
        {
            AxisAlignedBB hb = d.dragonPartHead.boundingBox;
            double cx = (hb.minX + hb.maxX) / 2, cz = (hb.minZ + hb.maxZ) / 2;
            int dcx = MathHelper.floor_double(d.posX / 16.0), dcz = MathHelper.floor_double(d.posZ / 16.0);
            boolean inX = MathHelper.floor_double((cx - 4.0) / 16.0) <= dcx && dcx <= MathHelper.floor_double((cx + 4.0) / 16.0);
            boolean inZ = MathHelper.floor_double((cz - 4.0) / 16.0) <= dcz && dcz <= MathHelper.floor_double((cz + 4.0) / 16.0);
            return inX && inZ ? hb : d.dragonPartBody.boundingBox;
        }
    }

    // ------------------------------------------------------------------ building

    /** Extend the floor at level fy from the player's cell along (dx, dz)
     * with cobble until the next cell is solid ground: sneak backwards to
     * the edge, look down at the face of the block underfoot, place. */
    static final class Bridge extends Task
    {
        final int dx, dz, fy;
        int t, placed, max = 12;
        java.util.function.BooleanSupplier stop;

        Bridge(int dx, int dz, int fy)
        {
            this.dx = dx;
            this.dz = dz;
            this.fy = fy;
        }

        String label() { return "Bridge(" + dx + "," + dz + " " + placed + ")"; }

        Object tick()
        {
            if (++t > 600 || placed > max) return FAIL;
            if (stop != null && cp.onGround && stop.getAsBoolean()) return DONE;
            if (hotbarOf(COBBLE) < 0) return child == FAIL ? FAIL : new ToHotbar(COBBLE);
            int slot = hotbarOf(COBBLE);
            int[] c = cell();
            int nx = c[0], nz = c[2];
            while (floor(nx, fy, nz))
            {
                nx += dx;
                nz += dz;
            }
            // the first open cell; ground beyond it or the gap closed: done
            if (Math.abs(nx - c[0]) + Math.abs(nz - c[2]) > 1) return DONE;
            float yaw = (float)Math.toDegrees(Math.atan2(dx, -dz));
            float[] back = {cp.rotationYaw + wrap(yaw - cp.rotationYaw), 78.0F};
            PlaceAt p = new PlaceAt(COBBLE, COBBLE, new int[] {nx, fy, nz}, null);
            if (cp.onGround && PlaceAt.choose(p.t, p))
            {
                ++placed;
                return new A().look(p.yp).hb(slot).hold("sneak").press("use").j();
            }
            // keep centred across the line while backing to the edge
            double off = dx != 0 ? cp.posZ - (c[2] + 0.5) : cp.posX - (c[0] + 0.5);
            A a = new A().look(back).hb(slot).hold("sneak", "back");
            if (Math.abs(off) > 0.08)
            {
                // right of the facing (yaw toward -d): +x for dz -1 ...
                double side = dx != 0 ? off * -dx : off * dz;
                a.hold(side > 0 ? "left" : "right");
            }
            return a.j();
        }
    }

    /** Jump and place cobble underfoot until the feet are at y top. */
    static final class Pillar extends Task
    {
        final int top;
        int t, ground = Integer.MIN_VALUE;

        Pillar(int top) { this.top = top; }

        String label() { return "Pillar(" + top + ")"; }

        Object tick()
        {
            if (cp.onGround && feetY() >= top) return DONE;
            if (++t > 400) return FAIL;
            if (hotbarOf(COBBLE) < 0) return child == FAIL ? FAIL : new ToHotbar(COBBLE);
            int slot = hotbarOf(COBBLE);
            int[] c = cell();
            float[] down = {cp.rotationYaw, 90.0F};
            if (cp.onGround)
            {
                ground = feetY();
                return new A().look(down).hb(slot).hold("jump").j();
            }
            if (ground != Integer.MIN_VALUE && cp.boundingBox.minY >= ground + 1.0 && pass(c[0], ground, c[2]))
            {
                MovingObjectPosition h = ray(down);
                if (hits(h, c[0], ground - 1, c[2]) && h.sideHit == 1) return new A().look(down).hb(slot).press("use").j();
            }
            return new A().look(down).hb(slot).j();
        }
    }

    // ------------------------------------------------------------------ the exit

    /** An end portal block within r of (x, z) at y 64, or null. */
    static int[] portalNear(int x, int z, int r)
    {
        for (int dx = -r; dx <= r; ++dx)
            for (int dz = -r; dz <= r; ++dz)
                if (id(x + dx, 64, z + dz) == END_PORTAL) return new int[] {x + dx, 64, z + dz};
        return null;
    }

    /** Onto the portal's rim and into it; through the credits. */
    static final class Exit extends Task
    {
        final int px, pz;
        int phase, t, walks;
        int[] rim;

        Exit(int px, int pz)
        {
            this.px = px;
            this.pz = pz;
        }

        String label() { return "Exit#" + phase; }

        /** A feet cell on the rim (y 65) beside a portal block. */
        boolean onRim(int a, int b, int c)
        {
            if (b != 65) return false;
            for (int[] d : new int[][] {{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
                if (id(a + d[0], 64, c + d[1]) == END_PORTAL) return true;
            return false;
        }

        Object tick()
        {
            if (mc.currentScreen instanceof GuiWinGame || sp.dimension != 1) return DONE;
            if (++t > 6000) throw new Stuck("no way into the exit portal");
            int[] p = portalNear(px, pz, 6);
            if (p == null) return idle();
            switch (phase)
            {
                case 0:
                {
                    if (++walks > 8) throw new Stuck("the exit portal's rim is out of reach");
                    List<int[]> path = plan(this::onRim, 60000, null);
                    if (path != null)
                    {
                        phase = 3;
                        return new Walk("the portal rim", this::onRim, 0.3);
                    }
                    // the rim above the ground: a cell below and beside a rim cell
                    phase = 1;
                    return new Walk("under the portal rim", (a, b, c) ->
                    {
                        if (b > 65 || b < 58) return false;
                        for (int[] d : new int[][] {{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
                            if (id(a + d[0], 64, c + d[1]) == 7 && (a + d[0] - px) * (a + d[0] - px) + (c + d[1] - pz) * (c + d[1] - pz) > 4
                                && pass(a, 65, c) && pass(a, 66, c)) return true;
                        return false;
                    }, 0.3).hint(px + 0.5, pz + 0.5);
                }
                case 1:
                    if (child == FAIL)
                    {
                        phase = 0;
                        return idle();
                    }
                    phase = 2;
                    return new Pillar(65);
                case 2:
                    phase = 0;
                    return idle();
                case 3:
                    if (child == FAIL)
                    {
                        phase = 0;
                        return idle();
                    }
                    phase = 4;
                    // fall through
                default:
                {
                    int[] c = cell();
                    int[] q = null;
                    for (int[] d : new int[][] {{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
                        if (q == null && id(c[0] + d[0], 64, c[2] + d[1]) == END_PORTAL) q = new int[] {c[0] + d[0], c[2] + d[1]};
                    if (q == null) q = new int[] {p[0], p[2]};
                    float[] l = lookAt(q[0] + 0.5, cp.posY, q[1] + 0.5);
                    l[1] = WALK_PITCH;
                    return new A().look(l).hold("forward").j();
                }
            }
        }
    }

    /** The credits: skipped at once (GuiWinGame's escape), then the respawn
     * in the overworld. */
    static final class Credits extends Task
    {
        int t;

        Object tick()
        {
            if (++t > 2000) throw new Stuck("no respawn after the credits");
            if (mc.currentScreen instanceof GuiWinGame) return t > 5 ? new A().gui("respawn").j() : idle();
            if (sp.dimension == 0 && !sp.isDead && cp.onGround && t > 20) return DONE;
            return idle();
        }
    }

    // ------------------------------------------------------------------ the segment

    /** Off the arrival platform: a straight bridge at the platform's level
     * from one of its cells to the nearest ground, or null when the island
     * can be walked to already. */
    /** A walk from here makes headway toward the island's middle. */
    static boolean walkable(Goal island)
    {
        int[] c = cell();
        List<int[]> walk = plan(island, 60000, null, 0.0, 0.0);
        if (walk == null) return false;
        int[] e = walk.get(walk.size() - 1);
        return planReached || Math.abs(e[0] - c[0]) + Math.abs(e[2] - c[2]) > 8;
    }

    static int[] bridgePlan(Goal island)
    {
        int[] c = cell();
        int fy = c[1] - 1;
        int[] best = null;
        double bestScore = 1e18;
        for (int ax = -3; ax <= 3; ++ax)
            for (int az = -3; az <= 3; ++az)
            {
                int sx = c[0] + ax, sz = c[2] + az;
                if (!stand(sx, c[1], sz)) continue;
                for (int[] d : new int[][] {{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
                {
                    int k = 1;
                    for (; k <= 8; ++k)
                    {
                        int x = sx + d[0] * k, z = sz + d[1] * k;
                        if (!pass(x, c[1], z) || !pass(x, c[1] + 1, z)) { k = 99; break; }
                        if (floor(x, fy, z)) break;
                    }
                    if (k < 2 || k > 8) continue;
                    int lx = sx + d[0] * k, lz = sz + d[1] * k;
                    double score = k * 100.0 + Math.sqrt((double)lx * lx + (double)lz * lz);
                    if (score < bestScore)
                    {
                        bestScore = score;
                        best = new int[] {sx, c[1], sz, d[0], d[1], k - 1};
                    }
                }
            }
        return best;
    }

    static final class SegEnd extends Seg
    {
        double[] target;
        int[] portal;
        int bridges, fightTries, digs;
        boolean onIsland;
        final java.util.HashMap<double[], Integer> shotFails = new java.util.HashMap<double[], Integer>();

        SegEnd()
        {
            crystals.clear();
            topFloor = 0;
        }

        Object tick()
        {
            if (sp.dimension != 1 && phase < 7) need(false, "not in the End");
            noteCrystals();
            switch (phase)
            {
                case 0:
                    phase = 1;
                    return new Idle(10);
                case 1:
                {
                    // off the platform onto the island
                    Goal island = (a, b, c) -> (double)a * a + (double)c * c < 60.0 * 60.0 && inland(a, b, c, 1);
                    if (walkable(island))
                    {
                        onIsland = true;
                        phase = 2;
                        return new Walk("onto the island", island, 0.5).hint(0.0, 0.0);
                    }
                    // a bridge only off the arrival platform
                    int[] br = onIsland ? null : bridgePlan(island);
                    if (br == null)
                    {
                        // sealed in end stone (the platform, a pit the dragon
                        // threw the bot into): a staircase up to open sky, a
                        // little toward the middle, then dig on toward it
                        need(++digs <= 6, "no way onto the island");
                        int[] c = cell();
                        log("digging out from " + where());
                        if (!ws.canBlockSeeTheSky(c[0], c[1] + 1, c[2]))
                            return new DigTo("out to the sky", (a, b, d) -> ws.canBlockSeeTheSky(a, b, d) && ws.canBlockSeeTheSky(a, b + 1, d),
                                c[0] - 8 * Math.signum(c[0]), c[1] + 12, c[2] - 8 * Math.signum(c[2]));
                        return new DigTo("onto the island", island, 0.0, 62.0, 0.0);
                    }
                    need(++bridges <= 3, "no bridge off the platform");
                    log("bridge from " + br[0] + "," + br[1] + "," + br[2] + " dir " + br[3] + "," + br[4] + " blocks " + br[5]);
                    phase = 10;
                    return walkToCell(br[0], br[1], br[2], 0.2);
                }
                case 10:
                {
                    need(child != FAIL, "could not reach the bridge start");
                    int[] c = cell();
                    Goal island = (a, b, d) -> (double)a * a + (double)d * d < 60.0 * 60.0 && inland(a, b, d, 1);
                    int[] br = walkable(island) ? null : bridgePlan(island);
                    need(br != null, "bridge plan lost");
                    phase = 1;
                    return new Bridge(br[3], br[4], c[1] - 1);
                }
                case 2:
                    if (child == FAIL)
                    {
                        // knocked off the way: from wherever the bot is now
                        phase = 1;
                        return idle();
                    }
                    phase = 3;
                    // fall through
                case 3:
                {
                    // seed 1: a crystal with no shot is left for the others,
                    // tried again once, and else left standing for the fight
                    if (s1 && target != null && child == FAIL)
                    {
                        Integer f = shotFails.get(target);
                        shotFails.put(target, f == null ? 1 : f + 1);
                        log(String.format("no shot at the crystal %.1f,%.1f,%.1f (%d)", target[0], target[1], target[2], shotFails.get(target)));
                        target = null;
                        child = null;
                    }
                    // the crystals, nearest first
                    double[] next = null;
                    double bd = 1e18;
                    for (double[] p : crystals)
                    {
                        if (crystalGone(p)) continue;
                        if (s1 && shotFails.containsKey(p) && shotFails.get(p) >= 2) continue;
                        double d = (p[0] - sp.posX) * (p[0] - sp.posX) + (p[2] - sp.posZ) * (p[2] - sp.posZ);
                        // (one that failed once comes after the rest)
                        if (s1 && shotFails.containsKey(p)) d += 1e12;
                        if (d < bd)
                        {
                            bd = d;
                            next = p;
                        }
                    }
                    if (next == null)
                    {
                        log("every crystal is gone or out of reach (" + crystals.size() + ")");
                        phase = 4;
                        return idle();
                    }
                    if (target == next && child == FAIL) throw new Stuck(String.format("no shot at the crystal %.1f,%.1f,%.1f", next[0], next[1], next[2]));
                    target = next;
                    return new ShootCrystal(next);
                }
                case 4:
                case 5:
                    phase = 6;
                    return new DragonFight();
                case 6:
                {
                    EntityDragon d = dragon();
                    if (alive(d))
                    {
                        need(++fightTries < 5, "the fight ended with the dragon alive");
                        return new DragonFight();
                    }
                    if (d != null)
                    {
                        if (d.deathTicks % 20 == 0) log("dragon dying " + d.deathTicks + String.format(" at %.1f,%.1f,%.1f", d.posX, d.posY, d.posZ));
                        portal = new int[] {MathHelper.floor_double(d.posX), MathHelper.floor_double(d.posZ)};
                        return idle();
                    }
                    need(portal != null, "the dragon vanished");
                    log("exit portal at " + portal[0] + "," + portal[1]);
                    phase = 7;
                    return new Exit(portal[0], portal[1]);
                }
                case 7:
                    phase = 8;
                    return new Credits();
                default:
                    return DONE;
            }
        }
    }

    /** The End's interrupts, in place of GoldBot's: the dragon's head close,
     * an enderman after the player close, hunger. */
    static void interrupt()
    {
        if (mc.currentScreen != null || cp.openContainer != cp.inventoryContainer || sp.dimension != 1) return;
        Task top = brain.stack.peek();
        if (top == null || top instanceof Interrupt || top instanceof Credits) return;
        boolean fight = top instanceof DragonFight;
        Interrupt t = null;
        EntityDragon d = dragon();
        double hd = alive(d) ? headDist(d) : 1e9;
        if (!fight && hd < 5.0) t = new DragonHit();
        else if (enderClose() != null && hd > 12.0) t = new EnderFight(enderClose().getEntityId());
        else if (count(COOKED_MEAT) > 0 && hotbarOf(COOKED_MEAT) >= 0 && !(top instanceof Eat) && !cp.isUsingItem() && hd > 24.0
            && (sp.getFoodStats().getFoodLevel() <= 14 || (sp.getFoodStats().getFoodLevel() < 18 && sp.getHealth() < 20.0F))) t = new EndEat();
        if (t == null) return;
        t.saved = top.child;
        brain.stack.push(t);
    }
}
