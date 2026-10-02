package netherite.oracle;

import static netherite.oracle.GoldBot.*;

import java.util.ArrayList;
import java.util.List;
import net.minecraft.block.Block;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.util.MathHelper;

/**
 * What seed 1's chain needs beyond the seed-42 segments GoldBot plays
 * (oracle/tests/s1chain-s*.jsonl run GoldBot segments S<k> "as" G<k>; every
 * change here is reached only through GoldBot.s1, so G1..G30 re-record
 * unchanged).
 */
final class GoldSeed1
{
    private GoldSeed1() {}

    /** What a seed-1 segment throws out when the inventory runs short:
     * netherrack, dirt, sand, nether brick, seeds, saplings, flowers,
     * gravel (string and flint stay for the bow and arrows). */
    static final int[] JUNK = {87, 3, 12, 112, 295, 6, 37, 38, 13};
    /** The trek's junk on seed 1: Tidy.TREK_JUNK but for string, flint and
     * arrows (the bow and arrows are made at the stronghold). */
    static long lastTidy = -100000;
    static final int[] TREK_JUNK = {87, 3, 13, 12, 112, 352, 295, 6, 334, 289, 375, 37, 38};

    static void log(String s)
    {
        System.out.println("GOLDBOT t=" + Oracle.tick + " " + s);
    }

    /** Between dusk and dawn: the hours mobs spawn on the surface. */
    static boolean night()
    {
        long t = worldTime();
        return t >= 12900L && t < 23200L;
    }

    /** Wall in where the player stands (cobble on every open side of the
     * feet and head cells and over the head), eat to a full bar and wait
     * for day and full health. The next dig mines out through the walls. */
    static final class NightWait extends Task
    {
        int phase, wi;
        long start;
        final List<int[]> walls = new ArrayList<int[]>();

        String label() { return "NightWait(" + phase + ")"; }

        Object tick()
        {
            if (child == FAIL) child = null;
            if (phase == 0)
            {
                int[] c = cell();
                for (int dy = 0; dy <= 1; ++dy)
                    for (int[] m : CARD) walls.add(new int[] {c[0] + m[0], c[1] + dy, c[2] + m[1]});
                walls.add(new int[] {c[0], c[1] + 2, c[2]});
                phase = 1;
                start = Oracle.tick;
                log("night wait at " + where() + " hp=" + sp.getHealth() + " time=" + worldTime());
            }
            if (phase == 1)
            {
                while (wi < walls.size())
                {
                    int[] w = walls.get(wi++);
                    if (pass(w[0], w[1], w[2])) return new PlaceAt(COBBLE, COBBLE, w, null);
                }
                phase = 2;
            }
            int food = sp.getFoodStats().getFoodLevel();
            int[] foods = {CPORK, CBEEF, CCHICKEN, STEW};
            int slot = hotbarOf(foods);
            if (slot < 0 && count(foods) > 0 && food < 20) return new ToHotbar(foods);
            if (slot >= 0 && food < 20) return new A().look(new float[] {cp.rotationYaw, 60.0F}).hb(slot).hold("use").j();
            if (cp.isUsingItem()) return idle();
            // day: out (health comes back once there is food, by day)
            boolean fed = count(COOKED_MEAT) + count(PORK, BEEF, CHICKEN) + count(STEW) > 0;
            if ((!night() && (sp.getHealth() >= 20.0F || !fed)) || Oracle.tick - start > 14000)
            {
                log("night wait over hp=" + sp.getHealth() + " food=" + food + " time=" + worldTime());
                return DONE;
            }
            return idle();
        }
    }

    static long lastRecover = -100000, lastSpiderEscape = -100000;

    /** From S27: a cave spider within 10 (a mineshaft's spawner): up to the
     * surface, at most once in 600 ticks. */
    static Interrupt caveSpiderEscape()
    {
        if (s1k < 21 || Oracle.tick - lastSpiderEscape < 600 || sp.dimension != 0) return null;
        boolean spider = false;
        for (Object o : ws.loadedEntityList)
            if (o instanceof net.minecraft.entity.monster.EntityCaveSpider && ((Entity)o).isEntityAlive() && ((Entity)o).getDistanceSqToEntity(sp) < 100.0)
                spider = true;
        int[] c = cell();
        if (!spider || c[1] >= ws.getHeightValue(c[0], c[2]) - 1) return null;
        lastSpiderEscape = Oracle.tick;
        log("cave spider near " + where() + ": up to the surface");
        DigTo up = new DigTo("up from the cave spiders", (a, b, cc) -> b >= ws.getHeightValue(a, cc) && stand(a, b, cc), sp.posX, 72, sp.posZ);
        up.maxNodes = 40000;
        return new Escape(up);
    }

    static boolean recovering()
    {
        for (Task x : brain.stack) if (x instanceof Recover) return true;
        return false;
    }

    /** Walled in to heal: two blocks down into the ground where it is
     * solid (a pit a witch's potion cannot reach), then cobble on every open
     * side of the feet and head cells and over the head; eat what there is
     * and wait for 16 health (the poison over first), 4,000 ticks at most,
     * 900 when there is nothing to eat and the food bar is too low to heal. */
    static final class Recover extends Interrupt
    {
        int phase, wi, downs, bi = -1, bstep, bx, by, bz, flees, drys;

        /** Water beside the feet or head cell. */
        boolean wetNear(int a, int b, int c)
        {
            for (int[] m : CARD)
                if (water(a + m[0], b, c + m[1]) || water(a + m[0], b + 1, c + m[1])) return true;
            return false;
        }
        long start;
        final List<int[]> walls = new ArrayList<int[]>();

        String label() { return "Recover(" + phase + ")"; }

        boolean rock(int x, int y, int z)
        {
            Block b = blk(x, y, z);
            return b != null && floor(x, y, z) && !falling(b) && !liquid(b);
        }

        /** Two cells toward d from c, feet and head, all diggable rock, and
         * rock on every side of the far one (and over it, and under it). */
        boolean burrow(int[] c, int[] d)
        {
            int ax = c[0] + d[0], az = c[2] + d[1], fx = c[0] + 2 * d[0], fz = c[2] + 2 * d[1], y = c[1];
            for (int dy = 0; dy <= 1; ++dy)
            {
                if (!rock(ax, y + dy, az) || !diggable(ax, y + dy, az) || !rock(fx, y + dy, fz) || !diggable(fx, y + dy, fz)) return false;
                if (!rock(fx + d[0], y + dy, fz + d[1]) || !rock(fx + d[1], y + dy, fz + d[0]) || !rock(fx - d[1], y + dy, fz - d[0])) return false;
                if (!rock(ax + d[1], y + dy, az + d[0]) || !rock(ax - d[1], y + dy, az - d[0])) return false;
            }
            return rock(fx, y + 2, fz) && rock(ax, y + 2, az) && floor(fx, y - 1, fz) && floor(ax, y - 1, az);
        }

        boolean solid(int x, int y, int z)
        {
            Block b = blk(x, y, z);
            return b != null && floor(x, y, z) && !falling(b) && diggable(x, y, z);
        }

        Object tick()
        {
            if (child == FAIL) child = null;
            if (phase == 0 && s1k >= 21 && drys < 2)
            {
                // S30 on: out of the water first (walled in standing in water,
                // the bot drowned)
                int[] c = cell();
                if (water(c[0], c[1], c[2]) || water(c[0], c[1] + 1, c[2]))
                {
                    ++drys;
                    log("recover: out of the water first from " + where());
                    return new Walk("dry ground", (a, b, cc) -> stand(a, b, cc) && !water(a, b, cc) && !water(a, b + 1, cc) && !wetNear(a, b, cc), 0.3);
                }
            }
            if (phase == 0 && s1k >= 21 && flees < 2)
            {
                // S27 on: away from the hostiles close by first (cave spiders
                // came through the walls and poisoned the bot to death)
                final List<Entity> near = new ArrayList<Entity>();
                for (Object o : ws.loadedEntityList)
                    if (o instanceof EntityLivingBase && hostile((Entity)o) && ((Entity)o).getDistanceSqToEntity(sp) < 64.0) near.add((Entity)o);
                if (!near.isEmpty())
                {
                    ++flees;
                    Entity h = near.get(0);
                    double dx = sp.posX - h.posX, dz = sp.posZ - h.posZ, d = Math.max(0.1, Math.sqrt(dx * dx + dz * dz));
                    final double tx = sp.posX + dx / d * 14.0, tz = sp.posZ + dz / d * 14.0;
                    log("recover: away from " + near.size() + " hostiles first, toward " + Math.round(tx) + "," + Math.round(tz));
                    DigTo away = new DigTo("away", (a, b, c) ->
                    {
                        for (Entity x : near)
                            if (x.isEntityAlive() && (x.posX - a - 0.5) * (x.posX - a - 0.5) + (x.posZ - c - 0.5) * (x.posZ - c - 0.5) < 144.0) return false;
                        return floor(a, b + 2, c);
                    }, tx, sp.posY, tz);
                    away.maxNodes = 20000;
                    return away;
                }
            }
            if (phase == 0)
            {
                start = Oracle.tick;
                StringBuilder b = new StringBuilder();
                for (Object o : ws.loadedEntityList)
                    if (o instanceof EntityLivingBase && hostile((Entity)o) && ((Entity)o).getDistanceSqToEntity(sp) < 400.0)
                        b.append(String.format(" %s@%.1f,%.1f,%.1f", o.getClass().getSimpleName().replace("Entity", ""), ((Entity)o).posX, ((Entity)o).posY, ((Entity)o).posZ));
                log("recover at " + where() + " hp=" + sp.getHealth() + " food=" + sp.getFoodStats().getFoodLevel() + " hostiles" + b);
                phase = 1;
            }
            if (phase == 1 && bi < 0)
            {
                // a burrow: two cells into solid rock beside the feet, the
                // entrance walled behind (a pit dug beside a cave was open to
                // a witch's potions and a zombie)
                int[] c = cell();
                for (int k = 0; k < CARD.length && bi < 0 && hotbarOf(DPICK, IPICK, SPICK) >= 0 && count(COBBLE) >= 2; ++k)
                    if (burrow(c, CARD[k])) bi = k;
                if (bi >= 0)
                {
                    bx = c[0];
                    by = c[1];
                    bz = c[2];
                    log("recover: a burrow toward " + CARD[bi][0] + "," + CARD[bi][1] + " from " + where());
                }
                else bi = 99;
            }
            if (phase == 1 && bi < CARD.length)
            {
                int dx = CARD[bi][0], dz = CARD[bi][1];
                int[][] digs = {{bx + dx, by + 1, bz + dz}, {bx + dx, by, bz + dz}, {bx + 2 * dx, by + 1, bz + 2 * dz}, {bx + 2 * dx, by, bz + 2 * dz}};
                while (bstep < digs.length)
                {
                    int[] m = digs[bstep++];
                    if (!pass(m[0], m[1], m[2])) return new Mine(m[0], m[1], m[2], true);
                }
                if (bstep == digs.length)
                {
                    ++bstep;
                    return walkToCell(bx + 2 * dx, by, bz + 2 * dz, 0.25);
                }
                walls.add(new int[] {bx + dx, by, bz + dz});
                walls.add(new int[] {bx + dx, by + 1, bz + dz});
                phase = 2;
            }
            if (phase == 1)
            {
                // down while the cell below and the one under it are solid
                int[] c = cell();
                if (downs < 2 && cp.onGround && solid(c[0], c[1] - 1, c[2]) && floor(c[0], c[1] - 2, c[2]) && !liquid(blk(c[0], c[1] - 2, c[2]))
                    && hotbarOf(DPICK, IPICK, SPICK) >= 0)
                {
                    ++downs;
                    return new Mine(c[0], c[1] - 1, c[2], true);
                }
                if (!cp.onGround) return idle();
                for (int dy = 0; dy <= 1; ++dy)
                    for (int[] m : CARD) walls.add(new int[] {c[0] + m[0], c[1] + dy, c[2] + m[1]});
                walls.add(new int[] {c[0], c[1] + 2, c[2]});
                phase = 2;
            }
            if (phase == 2)
            {
                while (wi < walls.size() && count(COBBLE) > 0)
                {
                    int[] w = walls.get(wi++);
                    if (pass(w[0], w[1], w[2]) || liquid(blk(w[0], w[1], w[2]))) return new PlaceAt(COBBLE, COBBLE, w, null);
                }
                phase = 3;
            }
            int food = sp.getFoodStats().getFoodLevel();
            int[][] tiers = {COOKED_MEAT, {STEW}, {APPLE, BREAD}, {PORK, BEEF}, {CHICKEN}, {ROTTEN}};
            if (cp.isUsingItem() && food < 20) return new A().look(new float[] {cp.rotationYaw, 60.0F}).hold("use").j();
            boolean fed = false;
            for (int[] kinds : tiers)
            {
                if (kinds[0] == ROTTEN && food > 10) break;
                if (count(kinds) == 0) continue;
                fed = true;
                if (food >= 20) break;
                int slot = hotbarOf(kinds);
                if (slot < 0) return new ToHotbar(kinds);
                return new A().look(new float[] {cp.rotationYaw, 60.0F}).hb(slot).hold("use").j();
            }
            long t = Oracle.tick - start;
            if (sp.getHealth() >= 16.0F || t > 4000 || (t > 900 && !fed && food < 18))
            {
                lastRecover = Oracle.tick;
                log("recovered to hp=" + sp.getHealth() + " food=" + food + " after " + t);
                return DONE;
            }
            return idle();
        }
    }

    static double stuckX, stuckY, stuckZ;
    static int stuckTicks;

    /** A walk that has not moved the player 0.1 in 400 ticks (hanging on a
     * vine in a jungle tree's leaves, every leg failing): Unstick. */
    static void watchStuck()
    {
        Task top = brain.stack.peek();
        boolean walking = top instanceof Walk || top instanceof DigTo || top instanceof Trek || (s1k >= 17 && top instanceof Hunt);
        double d = Math.abs(sp.posX - stuckX) + Math.abs(sp.posY - stuckY) + Math.abs(sp.posZ - stuckZ);
        if (!walking || posted || farming || sheltering || d > 0.1 || mc.currentScreen != null)
        {
            stuckX = sp.posX;
            stuckY = sp.posY;
            stuckZ = sp.posZ;
            stuckTicks = 0;
            return;
        }
        if (++stuckTicks < 400) return;
        stuckTicks = 0;
        log("stuck at " + where() + " under " + top.label() + ": unstick");
        Unstick u = new Unstick();
        u.saved = top.child;
        brain.stack.push(u);
    }

    static double boxX, boxZ;
    static long boxSince = -1;

    /** A trek that has made under 10 blocks from where it was 3,000 ticks
     * ago (legs and post sites that fail back and forth in a pit): a dig
     * 24 blocks toward the trek's goal, down there at the heightmap. */
    static void watchBoxed()
    {
        Task top = brain.stack.peek();
        if (boxSince < 0 || !(top instanceof Trek || top instanceof Walk || top instanceof BuildPost) || posted
            || Math.abs(sp.posX - boxX) + Math.abs(sp.posZ - boxZ) > 10.0)
        {
            boxX = sp.posX;
            boxZ = sp.posZ;
            boxSince = Oracle.tick;
            return;
        }
        if (Oracle.tick - boxSince < 3000 || mc.currentScreen != null) return;
        boxSince = Oracle.tick;
        double[] g = trekGoal();
        double dx = g[0] - sp.posX, dz = g[1] - sp.posZ, d = Math.sqrt(dx * dx + dz * dz);
        if (d < 1.0) return;
        final double tx = sp.posX + dx / d * 24.0, tz = sp.posZ + dz / d * 24.0;
        log("boxed in at " + where() + " under " + top.label() + ": a dig out toward " + Math.round(tx) + "," + Math.round(tz));
        DigTo out = new DigTo("out of the pit", (a, b, c) -> (a + 0.5 - tx) * (a + 0.5 - tx) + (c + 0.5 - tz) * (c + 0.5 - tz) < 25.0
            && b >= ws.getHeightValue(a, c) - 1, tx, sp.posY, tz);
        out.maxNodes = 100000;
        Escape e = new Escape(out);
        e.saved = top.child;
        brain.stack.push(e);
    }

    /** An interrupt around one task. */
    static final class Escape extends Interrupt
    {
        final Task t;
        boolean started;

        Escape(Task t) { this.t = t; }

        String label() { return "Escape"; }

        Object tick()
        {
            if (!started)
            {
                started = true;
                return t;
            }
            return DONE;
        }
    }

    /** Let go for 60 ticks (a vine lets the player slide down), then mine
     * the leaves and vines beside and over the feet and head cells. */
    static final class Unstick extends Interrupt
    {
        int t, k;
        final List<int[]> cells = new ArrayList<int[]>();

        String label() { return "Unstick(" + t + ")"; }

        Object tick()
        {
            if (child == FAIL) child = null;
            if (++t <= 60) return new A().look(new float[] {cp.rotationYaw, 0.0F}).j();
            if (t == 61)
            {
                int[] c = cell();
                for (int dy = -1; dy <= 2; ++dy)
                    for (int[] m : new int[][] {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}})
                    {
                        if (m[0] == 0 && m[1] == 0 && (dy == 0 || dy == 1)) continue;
                        int x = c[0] + m[0], y = c[1] + dy, z = c[2] + m[1];
                        int b = id(x, y, z);
                        if (b == 18 || b == 161 || b == 106 || b == 31 || b == 175) cells.add(new int[] {x, y, z});
                    }
            }
            while (k < cells.size())
            {
                int[] m = cells.get(k++);
                if (id(m[0], m[1], m[2]) != 0) return new Mine(m[0], m[1], m[2], true);
            }
            return DONE;
        }
    }

    // ------------------------------------------------------------ food

    static final int BOWL = 281, STEW = 282, BROWN = 39, RED = 40;
    static final Recipe BOWL_R = new Recipe("bowls", BOWL, 4, false, new String[] {"P P", " P "}, 'P', PLANKS);
    static final Recipe STEW_R = new Recipe("mushroom stew", STEW, 1, false, new String[] {"RB", "W "}, 'R', RED, 'B', BROWN, 'W', BOWL);

    /** A mushroom of each kind within the loaded chunks. */
    static boolean mushrooms()
    {
        return !scan(new int[] {BROWN}, 1, 120).isEmpty() && !scan(new int[] {RED}, 1, 120).isEmpty();
    }

    /** A mushroom of each kind within r blocks (horizontally). */
    static boolean mushroomsNear(double r)
    {
        return near(scan(new int[] {BROWN}, 1, 120), r) && near(scan(new int[] {RED}, 1, 120), r);
    }

    /** From S27: a mushroom on the surface (within 2 of the heightmap), not
     * one in a cave (the stew took the bot into a mineshaft's cave spiders). */
    static boolean surface(int[] a)
    {
        return s1k < 21 || a[1] >= ws.getHeightValue(a[0], a[2]) - 2;
    }

    static boolean near(List<int[]> l, double r)
    {
        for (int[] a : l)
            if (surface(a) && (a[0] + 0.5 - sp.posX) * (a[0] + 0.5 - sp.posX) + (a[2] + 0.5 - sp.posZ) * (a[2] + 0.5 - sp.posZ) < r * r) return true;
        return false;
    }

    /** Food where the animals are gone: mushroom stew (6 food), a brown and
     * a red mushroom and a bowl, the bowls from three planks at a table. */
    static final class Stew extends Task
    {
        final int want;
        int phase;

        Stew(int want) { this.want = want; }

        /** As many of kind as the loaded chunks hold, up to the want (a
         * loop after more than there are ends, not stuck). */
        OreLoop mushroomLoop(int kind)
        {
            List<int[]> all = scan(new int[] {kind}, 1, 120);
            int ok = 0;
            for (int[] a : all) if (surface(a)) ++ok;
            int n = Math.min(want - count(STEW), count(kind) + ok);
            OreLoop o = new OreLoop(new int[] {kind}, new int[] {kind}, n, 1, 120);
            o.failIfNone = true;
            for (int[] a : all) if (!surface(a)) o.bad.add(key(a[0], a[1], a[2]));
            return o;
        }

        String label() { return "Stew(" + phase + " " + count(STEW) + "/" + want + ")"; }

        Object tick()
        {
            switch (phase)
            {
                case 0:
                    phase = 1;
                    log("stew: " + count(STEW) + " of " + want + ", mushrooms " + count(BROWN) + "+" + count(RED) + " at " + where());
                    if (count(BROWN) < want - count(STEW)) return mushroomLoop(BROWN);
                    return idle();
                case 1:
                    phase = 2;
                    if (count(RED) < want - count(STEW)) return mushroomLoop(RED);
                    return idle();
                case 2:
                {
                    int m = Math.min(count(BROWN), count(RED));
                    if (m == 0) return FAIL;
                    phase = 3;
                    if (count(BOWL) >= m) return idle();
                    if (count(PLANKS) < 7 && count(LOG, LOG2) > 0) return new Craft(false, count(LOG) > 0 ? PLANKS_ALL : GoldKit.PLANKS2_ALL, 1);
                    return idle();
                }
                case 3:
                {
                    int m = Math.min(count(BROWN), count(RED));
                    phase = 4;
                    if (count(BOWL) >= m) return idle();
                    if (count(PLANKS) < 3) return FAIL;
                    return new Craft(true, BOWL_R, 1);
                }
                case 4:
                {
                    int m = Math.min(Math.min(count(BROWN), count(RED)), count(BOWL));
                    if (m == 0) return FAIL;
                    phase = 5;
                    return new Craft(false, STEW_R, 1);
                }
                case 5:
                    if (child == FAIL) return FAIL;
                    // one a craft (a stew does not stack): the next
                    if (count(STEW) < want && Math.min(Math.min(count(BROWN), count(RED)), count(BOWL)) > 0)
                    {
                        phase = 4;
                        return idle();
                    }
                    log("stew: " + count(STEW) + " made at " + where());
                    return DONE;
                default:
                    return DONE;
            }
        }
    }

    /** Head under water with the air running low: swim up (jump held)
     * toward the nearest dry cell the player can stand in, within 10. */
    static final class Surface extends Interrupt
    {
        int t;
        int[] shore;

        String label() { return "Surface(" + t + ")"; }

        Object tick()
        {
            if ((!sp.isInsideOfMaterial(net.minecraft.block.material.Material.water) && sp.getAir() >= 300) || ++t > 600) return DONE;
            if (shore == null)
            {
                int[] c = cell();
                double bd = 1e9;
                for (int dx = -10; dx <= 10; ++dx)
                    for (int dz = -10; dz <= 10; ++dz)
                        for (int dy = -2; dy <= 6; ++dy)
                        {
                            int x = c[0] + dx, y = c[1] + dy, z = c[2] + dz;
                            if (!stand(x, y, z) || water(x, y, z) || water(x, y + 1, z)) continue;
                            double d = dx * dx + dz * dz + dy * dy * 0.25;
                            if (d < bd)
                            {
                                bd = d;
                                shore = new int[] {x, y, z};
                            }
                        }
                if (shore != null) log("surfacing toward " + shore[0] + "," + shore[1] + "," + shore[2] + " air=" + sp.getAir());
            }
            A a = new A().hold("jump");
            if (shore != null)
            {
                float[] yp = lookAt(shore[0] + 0.5, sp.posY + 1.62, shore[2] + 0.5);
                yp[1] = 0.0F;
                a.look(yp);
                if (Math.abs(shore[0] + 0.5 - sp.posX) + Math.abs(shore[2] + 0.5 - sp.posZ) > 0.4) a.hold("forward");
            }
            else a.look(new float[] {cp.rotationYaw, -30.0F});
            return a.j();
        }
    }

    /** Around the stronghold, radius 600, all over land (the walk finds
     * the endermen: fresh chunks have fresh ones, and fresh animals; the
     * first loop at radius 320 had eaten its animals by the second). */
    static final double[][] CIRCUIT = {
        {1623, 475}, {1543, 775}, {1323, 995}, {1023, 1075}, {723, 995}, {503, 775},
        {423, 475}, {503, 175}, {723, -45}, {1023, -125}, {1323, -45}, {1543, 175}};

    /** Where the trek walks and tunnels: seed 42's stronghold; on seed 1
     * the stronghold once the pearls are in, before that the circuit point
     * after the one nearest the player. */
    /** From S15: a loop through the plains east of the stronghold (x 1070
     * to 1450, z 100 to 700; the radius-600 circuit ran through jungle, a
     * swamp and its witches), where animals come back every 400 ticks. */
    static final double[][] PLAINS = {
        {1400, 400}, {1356, 506}, {1250, 550}, {1144, 506}, {1100, 400}, {1144, 294}, {1250, 250}, {1356, 294}};

    static double[] trekGoal()
    {
        if (!s1 || count(PEARL) + count(EYE) >= PEARLS_WANTED) return new double[] {HOME_X, HOME_Z};
        double[][] ring = s1k >= 15 ? PLAINS : CIRCUIT;
        int n = 0;
        double bd = 1e18;
        for (int i = 0; i < ring.length; ++i)
        {
            double dx = ring[i][0] - sp.posX, dz = ring[i][1] - sp.posZ;
            if (dx * dx + dz * dz < bd)
            {
                bd = dx * dx + dz * dz;
                n = i;
            }
        }
        // far from the ring (more than 150 blocks from its nearest point): to that point first
        if (s1k >= 15 && bd > 150.0 * 150.0) return ring[n];
        return ring[(n + 1) % ring.length];
    }

    /** Seed 1's night: on a pillar two blocks high, where an enderman (or
     * a zombie) cannot reach the player (EntityMob.attackEntity wants the
     * feet within 2.0; EntityAIAttackOnCollide's reach is the same), and
     * the player reaches an enderman's legs from above with the entity
     * pick's 3.0. An enderman in sight within 48 is stared at until it
     * turns (EntityEnderman.findPlayerToAttack); an angry one comes to the
     * pillar and is hit on the legs, never looked at in the middle (a stare
     * within 16 teleports it). A spider that climbs is fought. After a kill
     * the bot drops to the ground for the pearl and builds again. At dawn
     * it gets down and collects what lies around. */
    static final class PillarNight extends Task
    {
        final long until;
        int phase, bx, by, bz, target = -1, builds;
        long stareStart, killAt = -1;

        PillarNight(long until) { this.until = until; }

        String label() { return "PillarNight(" + phase + ")"; }

        /** The trek's night is world time 11000 on, to the day's wrap. */
        boolean done()
        {
            return worldTime() < 11000L || Oracle.tick >= until - 100;
        }

        Object tick()
        {
            if (child == FAIL)
            {
                child = null;
                if (phase <= 2) { farming = false; return FAIL; }
            }
            child = null;
            switch (phase)
            {
                case 0:
                {
                    if (done()) return DONE;
                    int[] c = cell();
                    bx = c[0]; by = c[1]; bz = c[2];
                    if (++builds > 6 || !floor(bx, by - 1, bz) || !pass(bx, by + 2, bz) || !pass(bx, by + 3, bz)) return FAIL;
                    log("pillar at " + bx + "," + by + "," + bz + " time=" + worldTime());
                    phase = 1;
                    return new JumpPlace(COBBLE, new int[] {bx, by, bz}, new int[] {bx, by - 1, bz});
                }
                case 1:
                    phase = 2;
                    return new JumpPlace(COBBLE, new int[] {bx, by + 1, bz}, new int[] {bx, by, bz});
                case 2:
                    farming = true;
                    phase = 3;
                    // fall through
                case 3:
                {
                    if (done())
                    {
                        phase = 5;
                        return idle();
                    }
                    // a pearl below after a kill: down for it
                    if (killAt >= 0 && Oracle.tick - killAt > 20)
                    {
                        killAt = -1;
                        if (Trek.pearlWithin(6.0))
                        {
                            log("pearl below the pillar: down for it");
                            phase = 5;
                            return idle();
                        }
                    }
                    if (Oracle.tick % 400 == 0)
                    {
                        StringBuilder b = new StringBuilder();
                        for (Object o : ws.loadedEntityList)
                        {
                            Entity x = (Entity)o;
                            if (!enderman(x)) continue;
                            double d = Math.sqrt(x.getDistanceSqToEntity(sp));
                            if (d > 64) continue;
                            float[] l = lookAt(x.posX, x.boundingBox.minY + x.height / 2.0F, x.posZ);
                            b.append(String.format(" %d@%.0f%s%s%s", x.getEntityId(), d, sp.canEntityBeSeen(x) ? "s" : "", stare(l[0], l[1], x) ? "S" : "", targets(x) ? "!" : ""));
                        }
                        log("pillar " + where() + " hp=" + sp.getHealth() + " food=" + sp.getFoodStats().getFoodLevel() + " endermen" + b);
                    }
                    int food = sp.getFoodStats().getFoodLevel();
                    int slot = hotbarOf(COOKED_MEAT);
                    if (slot < 0) slot = hotbarOf(PORK, BEEF);
                    if (slot < 0 && food <= 10) slot = hotbarOf(CHICKEN);
                    if (cp.isUsingItem() && food < 20) return new A().look(new float[] {cp.rotationYaw, 80.0F}).hold("use").j();
                    // an angry enderman at the pillar: its legs
                    Entity near = null;
                    double nd = 1e9;
                    for (Object o : ws.loadedEntityList)
                    {
                        Entity x = (Entity)o;
                        boolean ender = enderman(x);
                        if (!(ender && targets(x)) && !(x instanceof net.minecraft.entity.monster.EntitySpider && hostile(x))) continue;
                        double d = x.getDistanceSqToEntity(sp);
                        if (d < nd) { nd = d; near = x; }
                    }
                    if (target >= 0)
                    {
                        Entity te = ws.getEntityByID(target);
                        if (te == null || te.isDead || ((EntityLivingBase)te).getHealth() <= 0)
                        {
                            if (te != null && ((EntityLivingBase)te).getHealth() <= 0 && enderman(te) == false && te instanceof net.minecraft.entity.monster.EntityEnderman)
                            {
                                ++enderKills;
                                killAt = Oracle.tick;
                                log("enderman " + target + " dead at the pillar, pearls " + count(PEARL));
                            }
                            target = -1;
                        }
                    }
                    if (near != null && nd < 4.5 * 4.5)
                    {
                        Entity ce = mc.theWorld.getEntityByID(near.getEntityId());
                        if (ce != null)
                        {
                            target = near.getEntityId();
                            if (enderman(near)) return legHit((EntityLivingBase)near, ce, 0.0);
                            for (double fy : new double[] {0.5, 0.8, 0.25})
                            {
                                float[] l = lookAt(ce.posX, ce.boundingBox.minY + ce.height * fy, ce.posZ);
                                if (pointed(l) == ce)
                                {
                                    A a = new A().look(l).hb(swordSlot());
                                    if (((EntityLivingBase)near).hurtResistantTime <= 10) a.press("attack");
                                    return a.j();
                                }
                            }
                        }
                    }
                    if (slot >= 0 && food <= 16) return new A().look(new float[] {cp.rotationYaw, 80.0F}).hb(slot).hold("use").j();
                    // an angry one on its way: the eyes down, off it
                    if (near != null && enderman(near)) return new A().look(new float[] {cp.rotationYaw, 80.0F}).hb(swordSlot()).j();
                    // else the nearest in sight is stared at until it turns
                    Entity best = null;
                    float[] bl = null;
                    double bd = 48.0 * 48.0;
                    for (Object o : ws.loadedEntityList)
                    {
                        Entity x = (Entity)o;
                        if (!enderman(x) || !nearLoaded(x)) continue;
                        double d = x.getDistanceSqToEntity(sp);
                        if (d >= bd || d < 16.0) continue;
                        float[] l = lookAt(x.posX, x.boundingBox.minY + x.height / 2.0F, x.posZ);
                        if (!stare(l[0], l[1], x)) continue;
                        bd = d;
                        best = x;
                        bl = l;
                    }
                    if (best != null) return new A().look(bl).hb(swordSlot()).j();
                    return new A().look(new float[] {cp.rotationYaw, 80.0F}).hb(swordSlot()).j();
                }
                case 5:
                    // down: a step off the pillar (a drop of two), then the pearls
                    farming = false;
                    phase = 6;
                    return new Walk("off the pillar", (a, b, c) -> b <= by && Math.abs(a - bx) + Math.abs(c - bz) >= 1 && Math.abs(a - bx) + Math.abs(c - bz) <= 3, 0.3);
                case 6:
                    phase = 7;
                    return new Collect(new int[] {PEARL}, 8.0);
                case 7:
                    if (!done() && Oracle.tick < until - 600 && count(PEARL) + count(EYE) < PEARLS_WANTED && count(COBBLE) >= 6)
                    {
                        phase = 0;
                        return idle();
                    }
                    return DONE;
                default:
                    return DONE;
            }
        }
    }

    // ------------------------------------------------------------ the Nether

    /** The Throne nearest seed 1's arrival portal (21, 102, 16): spawner
     * (42, 68, -72) on the 5x5 brick platform x 40..44, z -74..-70, top at
     * y 67, the Throne's long axis along x with its bridge running east
     * (deck top y 64, walkway feet y 65 from x 47). The farm is seed 42's
     * turned a quarter: the corridor feet y 65 under the middle row z -72,
     * x 41..44, entered from x 45 (the tunnel's end at (45, 65, -71)), the
     * trenches z -73 and -71 at y 66..67. */
    static final int SPAWNER_X = 42, SPAWNER_Y = 68, SPAWNER_Z = -72;
    static final int ARRIVAL_X = 21, ARRIVAL_Y = 102, ARRIVAL_Z = 16;
    /** Eleven eyes take eleven blaze powder. */
    static final int RODS = 6;

    /** The Throne's box, but for the bridge side below the platform's
     * top: what the way in and the way out stay out of (the blazes). */
    static java.util.Set<Long> throneAvoid()
    {
        java.util.Set<Long> a = new java.util.HashSet<Long>();
        for (int x = fU0 - 5; x <= fU1 + 4; ++x)
            for (int y = fY - 6; y <= fY + 14; ++y)
                for (int z = fM - 6; z <= fM + 6; ++z)
                    if (x <= fU1 || y >= fY + 2) a.add(key(x, y, z));
        return a;
    }

    static void useFarm()
    {
        farmX = true;
        fM = SPAWNER_Z;
        // x 41..44: the platform's x 40 column stays, closing the corridor
        // and its trenches on the cliff side (the cavern west of the Throne)
        fU0 = SPAWNER_X - 1;
        fU1 = SPAWNER_X + 2;
        fY = SPAWNER_Y - 3;
    }

    /** Dig legs toward (tx, ty, tz): each leg a DigTo to a cell within 2
     * (3 in y) of the point L blocks along the straight line, L 30, else 18,
     * else 10 when the planner finds no route. Done within near blocks
     * (horizontally) of the target. */
    static final class Journey extends Task
    {
        final double tx, ty, tz, near;
        int legs, tryI;
        /** Cells no leg enters or digs. */
        java.util.Set<Long> avoid;
        static final int[] LENS = {30, 18, 10};

        Journey(double tx, double ty, double tz, double near)
        {
            this.tx = tx;
            this.ty = ty;
            this.tz = tz;
            this.near = near;
        }

        String label() { return "Journey(" + legs + ")"; }

        Object tick()
        {
            if (child == FAIL)
            {
                if (++tryI >= LENS.length) return FAIL;
            }
            else if (child != null)
            {
                tryI = 0;
                ++legs;
            }
            child = null;
            if (legs > 40) return FAIL;
            double dx = tx - sp.posX, dy = ty - sp.posY, dz = tz - sp.posZ, d = Math.sqrt(dx * dx + dz * dz);
            if (d <= near) return DONE;
            double f = Math.min(LENS[tryI], d) / d;
            final int wx = MathHelper.floor_double(sp.posX + dx * f), wz = MathHelper.floor_double(sp.posZ + dz * f);
            final int wy = Math.max(36, Math.min(118, MathHelper.floor_double(sp.posY + dy * f)));
            DigTo leg = new DigTo("leg " + legs + " to " + wx + "," + wy + "," + wz, (a, b, c) -> Math.abs(a - wx) <= 2 && Math.abs(c - wz) <= 2 && Math.abs(b - wy) <= 3,
                wx + 0.5, wy, wz + 0.5);
            leg.maxNodes = 300000;
            if (avoid != null) leg.avoid.addAll(avoid);
            return leg;
        }
    }

    /** S8 (as G7): from the arrival portal to the Throne, the farm built
     * under its platform, blazes until RODS rods. */
    static final class SegFortress extends Seg
    {
        Object tick()
        {
            switch (phase)
            {
                case 0:
                    need(sp.dimension == -1, "not in the Nether");
                    useFarm();
                    phase = 1;
                    return new Journey(fU1 + 3.5, fY, fM + 0.5, 6.0);
                case 1:
                {
                    need(child != FAIL, "the Throne's bridge not reached");
                    phase = 2;
                    // the last leg stays out of the Throne, in by the bridge
                    final int ex = fwx(fU1 + 1, 1), ez = fwz(fU1 + 1, 1);
                    DigTo d = new DigTo("the farm", (a, b, c) -> a == ex && b == fY && c == ez, ex + 0.5, fY, ez + 0.5);
                    d.maxNodes = 300000;
                    for (int x = fU0 - 4; x <= fU1 + 4; ++x)
                        for (int y = fY - 6; y <= fY + 14; ++y)
                            for (int z = fM - 6; z <= fM + 6; ++z)
                                if (x <= fU1 || y >= fY + 2) d.avoid.add(key(x, y, z));
                    return d;

                }
                case 2:
                    need(child != FAIL, "the farm was not reached");
                    need(findSpawner(SPAWNER_X, SPAWNER_Y, SPAWNER_Z, 2) != null, "no spawner at the Throne");
                    phase = 3;
                    return new BuildFarm();
                case 3:
                    phase = 4;
                    return new Farm(RODS);
                case 4:
                    need(count(ROD) >= RODS, "too few rods");
                    phase = 5;
                    return new Idle(3);
                default:
                    return DONE;
            }
        }
    }

    /** S9 (as G8): out of the farm by its entry, the legs back to the
     * arrival portal and through it to the overworld. */
    static final class SegHome extends Seg
    {
        int[] portal;

        Object tick()
        {
            switch (phase)
            {
                case 0:
                    need(sp.dimension == -1 && count(ROD) >= RODS, "not in the Nether with the rods");
                    useFarm();
                    phase = 1;
                    return walkToCell(fwx(fU1 + 1, 0), fY, fwz(fU1 + 1, 0), 0.25);
                case 1:
                    need(child != FAIL, "the corridor's end not reached");
                    phase = 2;
                    return new Mine(fwx(fU1 + 1, 1), fY + 1, fwz(fU1 + 1, 1), true);
                case 2:
                    phase = 3;
                    return new Mine(fwx(fU1 + 1, 1), fY, fwz(fU1 + 1, 1), true);
                case 3:
                    need(id(fwx(fU1 + 1, 1), fY, fwz(fU1 + 1, 1)) == 0 && id(fwx(fU1 + 1, 1), fY + 1, fwz(fU1 + 1, 1)) == 0, "the seal not mined");
                    phase = 31;
                {
                    // out along the bridge first, clear of the Throne
                    final int bx = fU1 + 6;
                    DigTo d = new DigTo("the bridge", (a, b, c) -> a >= bx && a <= bx + 2 && b == fY && Math.abs(c - fM) <= 1, bx + 0.5, fY, fM + 0.5);
                    d.avoid.addAll(throneAvoid());
                    return d;
                }
                case 31:
                {
                    need(child != FAIL, "the bridge not reached");
                    phase = 4;
                    Journey j = new Journey(ARRIVAL_X + 0.5, ARRIVAL_Y, ARRIVAL_Z + 0.5, 6.0);
                    j.avoid = throneAvoid();
                    return j;
                }
                case 4:
                    need(child != FAIL, "the way back not found");
                    phase = 5;
                    return new DigTo("the portal", (a, b, c) -> Math.abs(a - ARRIVAL_X) + Math.abs(c - ARRIVAL_Z) <= 3 && b == ARRIVAL_Y,
                        ARRIVAL_X + 0.5, ARRIVAL_Y, ARRIVAL_Z + 0.5);
                case 5:
                    need(child != FAIL, "the arrival portal not reached");
                    portal = findPortal(ARRIVAL_X, ARRIVAL_Y, ARRIVAL_Z, 8);
                    need(portal != null, "no arrival portal");
                    phase = 6;
                    return new Collect(null, 6.0);
                case 6:
                    phase = 7;
                    return new ThroughPortal(portal, 0);
                case 7:
                    need(child != FAIL && sp.dimension == 0, "not back in the overworld");
                    phase = 8;
                    return new Idle(5);
                case 8:
                    phase = 9;
                    return new LeavePortal();
                case 9:
                    need(child != FAIL, "could not step off the overworld portal");
                    phase = 10;
                    return new Idle(5);
                default:
                    return DONE;
            }
        }
    }
}
