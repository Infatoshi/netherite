package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.PriorityQueue;
import java.util.Set;
import net.minecraft.block.Block;
import net.minecraft.block.material.Material;
import net.minecraft.client.Minecraft;
import net.minecraft.client.entity.EntityClientPlayerMP;
import net.minecraft.client.gui.inventory.GuiInventory;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.passive.EntityChicken;
import net.minecraft.entity.passive.EntityCow;
import net.minecraft.entity.passive.EntityPig;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.init.Blocks;
import net.minecraft.inventory.Container;
import net.minecraft.inventory.ContainerFurnace;
import net.minecraft.inventory.ContainerWorkbench;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.tileentity.TileEntityFurnace;
import net.minecraft.util.AxisAlignedBB;
import net.minecraft.util.MathHelper;
import net.minecraft.util.MovingObjectPosition;
import net.minecraft.util.Vec3;
import net.minecraft.world.ChunkPosition;
import net.minecraft.world.WorldServer;
import net.minecraft.world.chunk.Chunk;

/**
 * The golden-playthrough bot. A script calls
 *   {"cmd":"run","class":"GoldBot","seg":"G1"}
 * once; every call decides one tick of ordinary input from the live world,
 * queues it as a one-tick step and queues the next call, so the bot is a
 * closed loop at tick resolution. It reads the world with privileged
 * knowledge (server blocks, entities, tile entities) for planning, but the
 * only thing it emits is agent rows (look, hold, press, hotbar, gui clicks);
 * it never mutates the world, so its tapes are pure play (dev false) and a
 * replay needs nothing but the rows. When the segment's goal is met it
 * queues nothing and the script's next line (the save) runs. A plan that
 * cannot finish halts the run with rc 9 instead of saving a checkpoint.
 *
 * Every read is side-effect free for the game: block reads skip chunks that
 * are not loaded (a read there would load or generate one), tile entities
 * come from the chunk's map, and the crosshair test is the client's own
 * EntityLivingBase.rayTrace plus EntityRenderer.getMouseOver's entity pass,
 * evaluated between ticks with the position and world the next tick's
 * getMouseOver will see.
 */
public final class GoldBot
{
    private GoldBot() {}

    static final int LOG = 17, LOG2 = 162, PLANKS = 5, STICK = 280, TABLE = 58, COBBLE = 4, STONE = 1,
        FURNACE = 61, LIT_FURNACE = 62, COAL_ORE = 16, COAL = 263, TORCH = 50, WPICK = 270, SPICK = 274,
        SSWORD = 272, SAXE = 275, PORK = 319, CPORK = 320, BEEF = 363, CBEEF = 364, CHICKEN = 365, CCHICKEN = 366,
        IRON_ORE = 15, IRON = 265, GRAVEL = 13, SAND = 12, FLINT = 318, IPICK = 257, BUCKET = 325, FLINT_STEEL = 259,
        DIAMOND_ORE = 56, DIAMOND = 264, DPICK = 278, OBSIDIAN = 49, WATER_BUCKET = 326, LAVA_BUCKET = 327,
        ROD = 369, SPAWNER = 52, PORTAL = 90, ISWORD = 267, IHELMET = 306, ICHEST = 307, ILEGS = 308, IBOOTS = 309,
        PEARL = 368;
    static final int[] RAW_MEAT = {PORK, BEEF, CHICKEN};
    static final int APPLE = 260, BREAD = 297;
    /** When the last food hunt gave up. */
    static long huntGaveUp = -100000;
    static final int ROTTEN = 367;
    /** Where the eyes lead on seed 42 (D_playthrough 4.1): the stronghold. */
    static double HOME_X = -200.0, HOME_Z = -856.0;
    /** The chain segment's own name (S7 on seed 1; G7 on seed 42). */
    static String chain;
    /** Seed 1's chain: the switch for what its geography needs (seed 42's
     * decisions never read it, so G1..G30 re-record unchanged). */
    static boolean s1;

    /** The seed's geography. Seed 1's ring strongholds have their portal
     * rooms at (-76, 22, -715) with no eye in a frame, (1023, 26, 475) with
     * one and (-950, 37, 772) with three; the last is past 400 blocks of
     * ocean from the Nether portal, so seed 1 walks to (1023, 475), over
     * land, with eleven eyes. */
    static void seedGeo(long seed)
    {
        s1 = seed == 1L;
        s1k = 0;
        if (s1 && chain != null && chain.startsWith("S"))
            for (int i = 1; i < chain.length() && Character.isDigit(chain.charAt(i)); ++i) s1k = s1k * 10 + (chain.charAt(i) - '0');
        if (seed == 1L)
        {
            HOME_X = 1023.0;
            HOME_Z = 475.0;
            PEARLS_WANTED = 11;
        }
        else
        {
            HOME_X = -200.0;
            HOME_Z = -856.0;
            PEARLS_WANTED = 12;
        }
    }

    /** A grown farm animal within r of the player. */
    static boolean animalWithin(double r)
    {
        for (Object o : ws.loadedEntityList)
            if ((o instanceof EntityPig || o instanceof EntityCow || o instanceof EntityChicken) && !((EntityLivingBase)o).isChild()
                && !((Entity)o).isDead && ((Entity)o).getDistanceSqToEntity(sp) < r * r) return true;
        return false;
    }
    static final int[] COOKED_MEAT = {CPORK, CBEEF, CCHICKEN};

    static Minecraft mc;
    static EntityClientPlayerMP cp;
    static WorldServer ws;
    static EntityPlayerMP sp;
    static Brain brain;
    static String seg;
    static long segStart;
    static String lastTop = "";
    static float lastHp = 20.0F;
    static final HashSet<Integer> ignoredItems = new HashSet<Integer>();

    /** Wall time spent deciding in this segment, and the calls (printed at its end). */
    static long botNanos, botCalls;

    static JsonObject run(IntegratedServer server, JsonObject cmd)
    {
        long t0 = System.nanoTime();
        JsonObject out = decide(server, cmd);
        botNanos += System.nanoTime() - t0;
        ++botCalls;
        if (out.has("done")) System.out.println("GOLDBOT " + seg + " bot time " + botNanos / 1000000 + " ms over " + botCalls + " calls");
        return out;
    }

    static JsonObject decide(IntegratedServer server, JsonObject cmd)
    {
        mc = Minecraft.getMinecraft();
        cp = mc.thePlayer;
        sp = (EntityPlayerMP)server.getConfigurationManager().playerEntityList.get(0);
        // the world the player is in (the overworld until a portal)
        ws = server.worldServerForDimension(sp.dimension);
        newReads();
        String s = cmd.has("seg") ? cmd.get("seg").getAsString() : "G1";
        // a seed-1 chain segment S<k> plays as the seed-42 segment named by
        // "as" (every switch reads that name), on seed 1's geography
        if (cmd.has("as")) s = cmd.get("as").getAsString();
        JsonObject out = new JsonObject();
        if (brain == null || !s.equals(seg))
        {
            seg = s;
            chain = cmd.has("as") ? cmd.get("seg").getAsString() : s;
            huntNear = cmd.has("huntNear") ? cmd.get("huntNear").getAsDouble() : 10.0;
            huntMax = cmd.has("huntMax") ? cmd.get("huntMax").getAsDouble() : 14.0;
            huntDy = cmd.has("huntDy") ? cmd.get("huntDy").getAsInt() : 3;
            nightMode = cmd.has("night") ? cmd.get("night").getAsString() : "down";
            huntR = cmd.has("huntR") ? cmd.get("huntR").getAsDouble() : 0;
            huntT = cmd.has("huntT") ? cmd.get("huntT").getAsLong() : 0;
            roamHeading = cmd.has("heading") ? Math.toRadians(cmd.get("heading").getAsDouble()) : Double.NaN;
            segStart = Oracle.tick;
            seedGeo(server.worldServerForDimension(0).getSeed());
            farmX = false;
            fM = FX;
            fU0 = FZ0;
            fU1 = FZ1;
            fY = FY;
            lavaCardinalOnly = false;
            stepFloors = false;
            sneakMoves = false;
            digWeight = 3.0;
            farming = false;
            sheltering = false;
            noShelterUntil = 0;
            unreachable.clear();
            enderHunting = false;
            enderKills = 0;
            posted = false;
            trekPost = null;
            badSites.clear();
            brain = new Brain(segment(s));
            // seed 1: room in the inventory before each segment (the Nether
            // digging fills it with netherrack and brick)
            if (s1 && Tidy.free() < 8 && mc.currentScreen == null) brain.stack.push(new Tidy(GoldSeed1.JUNK));
            botNanos = 0;
            botCalls = 0;
            ignoredItems.clear();
            System.out.println("GOLDBOT " + seg + " start t=" + Oracle.tick + " at " + where() + " time=" + ws.getWorldTime() + " inv=" + invString());
        }
        JsonObject act;
        try
        {
            if (Oracle.tick - segStart > budget(seg)) throw new Stuck("over the tick budget " + budget(seg));
            if (GoldEnd.active() && sp.getHealth() <= 0.0F) throw new Stuck("dead");
            if ((trekNum() >= 16 || s1) && sp.getHealth() <= 0.0F) throw new Stuck("dead");
            // G18 on: at the segment's end the tasks over the trek give way
            // (not a fight, and not with a window open)
            // seed 1 from S29: 400 ticks past the end nothing interrupts the
            // trek's own end any more (a Recover and a Surface pushed again
            // every tick kept the segment from ending)
            boolean over = s1k >= 21 && Oracle.tick >= segStart + HUNT_TICKS + 400;
            // seed 1 from S21: the kit runs to its end (the cobwebs and the
            // flint took the kit past HUNT_TICKS with two arrows made)
            boolean kitRuns = s1k >= 21 && seg.equals("G28");
            if (over && kitRuns) over = false;
            if (trekNum() >= 18 && !kitRuns && Oracle.tick >= segStart + HUNT_TICKS && mc.currentScreen == null)
                while (brain.stack.size() > 1 && !(brain.stack.peek() instanceof Trek) && !(brain.stack.peek() instanceof SegTrek)
                    && (over || (!(brain.stack.peek() instanceof EnderFight) && !(brain.stack.peek() instanceof Brawl))))
                {
                    System.out.println("GOLDBOT t=" + Oracle.tick + " the segment's end: dropping " + brain.stack.peek().label());
                    brain.stack.pop();
                    if (brain.stack.peek() != null) brain.stack.peek().child = null;
                }
            if (!over) interrupt();
            if (s1) GoldSeed1.watchStuck();
            if (s1 && trekNum() > 0) GoldSeed1.watchBoxed();
            // seed 1: room before a craft or a pickup finds the inventory full
            if (s1 && Tidy.free() < 3 && mc.currentScreen == null && !(brain.stack.peek() instanceof Tidy) && !farming && !GoldEnd.active()
                && Oracle.tick - GoldSeed1.lastTidy > 600)
            {
                GoldSeed1.lastTidy = Oracle.tick;
                brain.stack.push(new Tidy(GoldSeed1.TREK_JUNK));
            }
            act = brain.tick();
        }
        catch (RuntimeException e)
        {
            System.out.println("GOLDBOT STUCK " + seg + " t=" + Oracle.tick + " at " + where() + ": " + e + " tasks=" + brain.describe());
            System.out.println("GOLDBOT STUCK inv=" + invString());
            dbgArea();
            e.printStackTrace(System.out);
            Oracle.finish(9);
            return out;
        }
        if (sp.getHealth() < lastHp)
            System.out.println("GOLDBOT t=" + Oracle.tick + " hurt " + lastHp + "->" + sp.getHealth() + " at " + where()
                + " by " + (sp.getAITarget() == null ? "-" : sp.getAITarget().getClass().getSimpleName()) + " fire=" + sp.isBurning()
                + " lava=" + sp.handleLavaMovement() + " fall=" + sp.fallDistance);
        lastHp = sp.getHealth();
        String top = brain.describe();
        if (!top.equals(lastTop))
        {
            System.out.println("GOLDBOT t=" + Oracle.tick + " " + where() + " hp=" + sp.getHealth() + " " + top);
            lastTop = top;
        }
        if (act == null)
        {
            System.out.println("GOLDBOT " + seg + " done t=" + Oracle.tick + " ticks=" + (Oracle.tick - segStart) + " at " + where()
                + " food=" + sp.getFoodStats().getFoodLevel() + " inv=" + invString());
            out.addProperty("done", seg);
            out.addProperty("ticks", Oracle.tick - segStart);
            brain = null;
            return out;
        }
        JsonObject step = new JsonObject();
        step.addProperty("cmd", "step");
        step.addProperty("n", 1);
        step.add("act", act);
        Control.queue.add(step);
        JsonObject again = new JsonObject();
        again.addProperty("cmd", "run");
        again.addProperty("class", "GoldBot");
        again.addProperty("seg", seg);
        Control.queue.add(again);
        out.addProperty("t", Oracle.tick);
        return out;
    }

    /** A trek segment's number (G16 to G39, not a stage G<n>SH or G<n>END),
     * else 0: the switch for the shared paths the trek added. */
    static int trekNum()
    {
        return seg != null && seg.startsWith("G") && !seg.endsWith("SH") && !seg.endsWith("END") && segNum() >= 16 && segNum() < 40 ? segNum() : 0;
    }

    /** Seed 1's own segment number (S52 is 52), else 0: the switch for a
     * seed-1 change made at that segment, so the segments before it
     * re-record unchanged. */
    static int s1Num()
    {
        return s1k;
    }

    static int s1k;
    /** The cave hunt's dig goal: a cell within huntNear (huntMax loose) of the
     * enderman's eyes and huntDy levels of its feet; a seed-1 segment's run
     * command may set them ("huntNear", "huntMax", "huntDy"). */
    static double huntNear = 10.0, huntMax = 14.0;
    static int huntDy = 3;
    /** The seed-1 trek's night: "down" (tunnels and cave hunts) or "pillar"
     * (the run command's "night"). */
    static String nightMode = "down";
    /** The cave hunt's search radius (0: 64 by day, 128 by night) and time
     * limit (the run command's "huntR", "huntT"). */
    static double huntR = 0;
    static long huntT = 0;
    /** Seed 1: the ender hunt's roam heading (the run command's "heading",
     * degrees of yaw), else NaN. */
    static double roamHeading = Double.NaN;

    /** A cave hunt's time limit: 2,400 ticks; 4,000 on seed 1 from S26
     * (fewer hunts cut off short of the enderman); the run command's. */
    static long huntLimit()
    {
        return huntT > 0 ? huntT : s1k >= 21 ? 4000 : 2400;
    }

    /** The segment's number: G10A is 10. */
    static int segNum()
    {
        return segNum(seg);
    }

    static int segNum(String s)
    {
        if (!s.startsWith("G")) return 99;
        int n = 0;
        for (int i = 1; i < s.length() && Character.isDigit(s.charAt(i)); ++i) n = n * 10 + (s.charAt(i) - '0');
        return n;
    }

    /** The ender hunt: pearls for the twelve frames (the seed-42 portal
     * room has no eye), in segments of HUNT_TICKS. */
    static final int HUNT_TICKS = 12000;
    /** Twelve on seed 42; eleven on seed 1, whose portal room holds one
     * eye already (seedGeo). */
    static int PEARLS_WANTED = 12;

    static int budget(String s)
    {
        if (s.startsWith("G7")) return 30000;
        if (s.equals("END") || s.startsWith("G") && s.endsWith("END")) return GoldEnd.BUDGET;
        if (s.equals("G9")) return 16000;
        if (s.startsWith("HUNT")) return Integer.parseInt(s.substring(4)) + 4000;
        if (s.equals("SH") || s.startsWith("G") && s.endsWith("SH")) return GoldStronghold.BUDGET;
        if (segNum(s) >= 10 && segNum(s) < 40) return HUNT_TICKS + 4000;
        return s.equals("G1") ? 4000 : s.startsWith("G4") || s.startsWith("G5") || s.equals("G6") ? 12000 : 7000;
    }

    static Task segment(String s)
    {
        // the stronghold stage: staged as SH, in the chain as G<n>SH
        if (s.equals("SH") || s.startsWith("G") && s.endsWith("SH")) return new GoldStronghold.SegStronghold();
        // the End: staged as END, in the chain as G<n>END
        if (s.equals("END") || s.startsWith("G") && s.endsWith("END")) return new GoldEnd.SegEnd();
        if (s.equals("G1")) return new SegG1();
        if (s.equals("G2")) return new SegG2();
        if (s.equals("G3")) return new SegG3();
        if (s.equals("G4")) return new SegG4();
        if (s.equals("G5")) return new SegG5(0, 99);
        if (s.equals("G5A")) return new SegG5(0, 9);
        if (s.equals("G5B")) return new SegG5(9, 99);
        if (s.equals("G6")) return new SegG6();
        if (s1 && s.equals("G7")) return new GoldSeed1.SegFortress();
        if (s1 && s.equals("G8")) return new GoldSeed1.SegHome();
        if (s.equals("G7")) return new SegG7(0, 99);
        if (s.equals("G7A")) return new SegG7(0, 3);
        if (s.equals("G7B")) return new SegG7(3, 99);
        if (s.equals("G8")) return new SegG8();
        if (s.equals("G9")) return new SegG9();
        if (s.startsWith("HUNT")) return new SegHunt(PEARLS_WANTED, Integer.parseInt(s.substring(4)));
        // G28: the kit for the End at the stronghold (bow, arrows, food, picks)
        if (s.equals("G28")) return new GoldKit.SegKit();
        if (segNum(s) >= 16 && segNum(s) < 40) return new SegTrek();
        if (segNum(s) >= 10 && segNum(s) < 20) return new SegHunt(PEARLS_WANTED, HUNT_TICKS);
        if (s.equals("SURVEY")) return new Survey();
        throw new Stuck("unknown segment " + s);
    }

    static String where()
    {
        return String.format("(%.2f,%.2f,%.2f)", cp.posX, cp.boundingBox.minY, cp.posZ);
    }

    static String invString()
    {
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < 36; ++i)
        {
            ItemStack s = cp.inventory.mainInventory[i];
            if (s != null) b.append(i).append(':').append(Item.getIdFromItem(s.getItem())).append('x').append(s.stackSize).append(' ');
        }
        return b.toString().trim();
    }

    static final class Stuck extends RuntimeException
    {
        Stuck(String m) { super(m); }
    }

    // ------------------------------------------------------------------ tasks

    static final Object DONE = "DONE", FAIL = "FAIL";

    /** A closed-loop behaviour. tick() returns this tick's act (a JsonObject),
     * a child Task to run first, DONE or FAIL; a parent reads its last child's
     * result in child. */
    abstract static class Task
    {
        Object child;
        abstract Object tick();
        String label() { return getClass().getSimpleName(); }
    }

    static final class Brain
    {
        final ArrayDeque<Task> stack = new ArrayDeque<Task>();

        Brain(Task root) { stack.push(root); }

        JsonObject tick()
        {
            for (int guard = 0; guard < 400; ++guard)
            {
                Task t = stack.peek();
                if (t == null) return null;
                Object r = t.tick();
                if (r instanceof JsonObject) return (JsonObject)r;
                if (r instanceof Task)
                {
                    t.child = null;
                    stack.push((Task)r);
                    continue;
                }
                stack.pop();
                if (t instanceof Interrupt)
                {
                    Task p = stack.peek();
                    if (p != null) p.child = ((Interrupt)t).saved;
                    continue;
                }
                if (r == FAIL) System.out.println("GOLDBOT t=" + Oracle.tick + " FAIL " + t.label());
                Task p = stack.peek();
                if (p == null && r == FAIL) throw new Stuck("segment failed");
                if (p != null) p.child = r;
            }
            throw new Stuck("no act after 400 task steps");
        }

        String describe()
        {
            StringBuilder b = new StringBuilder();
            java.util.Iterator<Task> it = stack.descendingIterator();
            while (it.hasNext())
            {
                if (b.length() > 0) b.append(" > ");
                b.append(it.next().label());
            }
            return b.toString();
        }
    }

    /** One tick of agent input. */
    static final class A
    {
        final JsonObject o = new JsonObject();

        A look(float[] yp)
        {
            JsonArray l = new JsonArray();
            l.add(new JsonPrimitive(yp[0]));
            l.add(new JsonPrimitive(yp[1]));
            o.add("look", l);
            return this;
        }

        A keys(String field, String... k)
        {
            JsonArray l = o.has(field) ? o.getAsJsonArray(field) : new JsonArray();
            for (String s : k) l.add(new JsonPrimitive(s));
            o.add(field, l);
            return this;
        }

        A hold(String... k) { return keys("hold", k); }
        A press(String... k) { return keys("press", k); }

        A hb(int slot)
        {
            if (slot >= 0 && slot != cp.inventory.currentItem) o.addProperty("hotbar", slot);
            return this;
        }

        A gui(Object... op)
        {
            JsonArray g = o.has("gui") ? o.getAsJsonArray("gui") : new JsonArray();
            JsonArray a = new JsonArray();
            for (Object x : op) a.add(x instanceof String ? new JsonPrimitive((String)x) : new JsonPrimitive((Number)x));
            g.add(a);
            o.add("gui", g);
            return this;
        }

        JsonObject j() { return o; }
    }

    static JsonObject idle() { return new A().j(); }

    static final class Idle extends Task
    {
        int n;
        Idle(int n) { this.n = n; }
        Object tick() { return n-- > 0 ? idle() : DONE; }
    }

    // ------------------------------------------------------------------ world reads

    // The bot runs on the client thread between ticks, when no server block
    // or chunk changes, so each run() reads every chunk through a small
    // direct-mapped cache (a loaded chunk, or none for an unloaded one) and
    // remembers stand and safeCell per cell. Each is exactly what the
    // world's own getters return (World.getBlock, Chunk.func_150810_a,
    // chunkExists then getChunkFromChunkCoords); nothing loads a chunk.
    static int readGen = 1;
    static final int CC = 64;
    static final int[] ccGen = new int[CC * CC], ccX = new int[CC * CC], ccZ = new int[CC * CC];
    static final Chunk[] ccChunk = new Chunk[CC * CC];

    /** Forget everything the reads remembered (the world may have changed). */
    static void newReads()
    {
        if (++readGen == 0)
        {
            java.util.Arrays.fill(ccGen, 0);
            java.util.Arrays.fill(memoGen, 0);
            readGen = 1;
        }
    }

    /** The loaded chunk (cx, cz), null when it is not loaded. */
    static Chunk chunk(int cx, int cz)
    {
        int i = (cx & (CC - 1)) * CC + (cz & (CC - 1));
        if (ccGen[i] == readGen && ccX[i] == cx && ccZ[i] == cz) return ccChunk[i];
        Chunk c = ws.getChunkProvider().chunkExists(cx, cz) ? ws.getChunkFromChunkCoords(cx, cz) : null;
        ccGen[i] = readGen;
        ccX[i] = cx;
        ccZ[i] = cz;
        ccChunk[i] = c;
        return c;
    }

    static boolean loaded(int x, int z)
    {
        return chunk(x >> 4, z >> 4) != null;
    }

    static Block blk(int x, int y, int z)
    {
        if (y < 0 || y > 255) return null;
        Chunk c = chunk(x >> 4, z >> 4);
        if (c == null) return null;
        if (x < -30000000 || z < -30000000 || x >= 30000000 || z >= 30000000) return Blocks.air;
        return c.func_150810_a(x & 15, y, z & 15);
    }

    /** ws.getBlockMetadata for a cell in a loaded chunk. */
    static int meta(int x, int y, int z)
    {
        Chunk c = chunk(x >> 4, z >> 4);
        if (c == null || y < 0 || y >= 256 || x < -30000000 || z < -30000000 || x >= 30000000 || z >= 30000000)
            return ws.getBlockMetadata(x, y, z);
        return c.getBlockMetadata(x & 15, y, z & 15);
    }

    // per-cell memo for this run: bits 0-1 stand, 2-3 safeCell (known,
    // value), 4-7 the same with lavaCardinalOnly set, 8-9 wetRoute
    static int memoMask = (1 << 16) - 1, memoUsed;
    static long[] memoKey = new long[1 << 16];
    static int[] memoVal = new int[1 << 16];
    static int[] memoGen = new int[1 << 16];

    static int memoSlot(long k)
    {
        long h = k * 0x9E3779B97F4A7C15L;
        int i = (int)(h >>> 40) & memoMask;
        while (memoGen[i] == readGen && memoKey[i] != k) i = (i + 1) & memoMask;
        return i;
    }

    static int memoGet(long k)
    {
        int i = memoSlot(k);
        return memoGen[i] == readGen ? memoVal[i] : 0;
    }

    static void memoPut(long k, int bits)
    {
        int i = memoSlot(k);
        if (memoGen[i] != readGen)
        {
            if (++memoUsed * 2 > memoMask)
            {
                memoGrow();
                i = memoSlot(k);
            }
            memoGen[i] = readGen;
            memoKey[i] = k;
            memoVal[i] = 0;
        }
        memoVal[i] |= bits;
    }

    static void memoGrow()
    {
        long[] ok = memoKey;
        int[] ov = memoVal;
        int[] og = memoGen;
        int n = (memoMask + 1) * 2;
        memoKey = new long[n];
        memoVal = new int[n];
        memoGen = new int[n];
        memoMask = n - 1;
        memoUsed = 0;
        for (int j = 0; j < ok.length; ++j)
        {
            if (og[j] != readGen) continue;
            int i = memoSlot(ok[j]);
            memoGen[i] = readGen;
            memoKey[i] = ok[j];
            memoVal[i] = ov[j];
            ++memoUsed;
        }
    }

    static int id(int x, int y, int z)
    {
        Block b = blk(x, y, z);
        return b == null ? -1 : Block.getIdFromBlock(b);
    }

    static boolean liquid(Block b)
    {
        return b != null && b.getMaterial().isLiquid();
    }

    /** No collision box: the player's feet or head can be here. */
    static boolean pass(int x, int y, int z)
    {
        Block b = blk(x, y, z);
        if (b == null) return false;
        Material m = b.getMaterial();
        if (m == Material.air) return true;
        if (b == Blocks.cactus || b == Blocks.web || b == Blocks.fire || b == Blocks.waterlily || b == Blocks.cocoa) return false;
        if (m == Material.plants || m == Material.vine || m == Material.circuits) return true;
        if (b == Blocks.snow_layer) return (meta(x, y, z) & 7) == 0;
        return m == Material.water;
    }

    static boolean water(int x, int y, int z)
    {
        Block b = blk(x, y, z);
        return b != null && b.getMaterial() == Material.water;
    }

    /** A full block the player can stand on. */
    static boolean floor(int x, int y, int z)
    {
        Block b = blk(x, y, z);
        if (b == null) return false;
        if (b.getMaterial() == Material.leaves) return true;
        if (stepFloors && (b instanceof net.minecraft.block.BlockStairs || b instanceof net.minecraft.block.BlockSlab || b == Blocks.end_portal_frame))
            return true;
        return b.isOpaqueCube() && b.getMaterial().isSolid();
    }

    /** While set (the stronghold stage), stairs, slabs and end portal frames
     * are floors too, and the feet cell is the one above a half-high block
     * the player stands on (see feetY). */
    static boolean stepFloors;

    /** While set, lava only rules out a cell it can flow into or burn from:
     * beside the feet or head, above, or below (see lavaNear). */
    static boolean lavaCardinalOnly;

    static boolean hot(Block b)
    {
        return b != null && (b.getMaterial() == Material.lava || b == Blocks.fire || b == Blocks.cactus);
    }

    static boolean lavaNear(int x, int y, int z)
    {
        if (hot(blk(x, y - 1, z)) || hot(blk(x, y, z)) || hot(blk(x, y + 1, z)) || hot(blk(x, y + 2, z))) return true;
        for (int[] d : DIRS)
            if ((d[0] == 0 || d[1] == 0) && (hot(blk(x + d[0], y, z + d[1])) || hot(blk(x + d[0], y + 1, z + d[1])))) return true;
        return false;
    }

    static boolean stand(int x, int y, int z)
    {
        long k = key(x, y, z);
        int sh = lavaCardinalOnly ? 4 : 0;
        int m = memoGet(k) >> sh;
        if ((m & 1) != 0) return (m & 2) != 0;
        boolean r = standNow(x, y, z);
        memoPut(k, (r ? 3 : 1) << sh);
        return r;
    }

    static boolean standNow(int x, int y, int z)
    {
        if (!pass(x, y, z) || !pass(x, y + 1, z) || water(x, y + 1, z)) return false;
        if (!floor(x, y - 1, z) && !water(x, y, z)) return false;
        if (lavaCardinalOnly) return !lavaNear(x, y, z);
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                for (int dy = -1; dy <= 1; ++dy)
                {
                    Block b = blk(x + dx, y + dy, z + dz);
                    if (b != null && (b.getMaterial() == Material.lava || b == Blocks.fire || b == Blocks.cactus)) return false;
                }
        return true;
    }

    /** Stops the planning ray: anything but air, liquid and the thin plants
     * (the real crosshair check catches a plant that does stop it). */
    static boolean raySolid(int x, int y, int z)
    {
        Block b = blk(x, y, z);
        if (b == null) return true;
        Material m = b.getMaterial();
        return !(m == Material.air || m.isLiquid() || m == Material.plants || m == Material.vine || m == Material.circuits);
    }

    static int count(int... ids)
    {
        int n = 0;
        for (int i = 0; i < 36; ++i)
        {
            ItemStack s = cp.inventory.mainInventory[i];
            if (s == null) continue;
            int d = Item.getIdFromItem(s.getItem());
            for (int k : ids) if (d == k) n += s.stackSize;
        }
        return n;
    }

    /** The best sword on the hotbar: iron, else stone. */
    static int swordSlot()
    {
        int s = hotbarOf(ISWORD);
        return s >= 0 ? s : hotbarOf(SSWORD);
    }

    static int hotbarOf(int... ids)
    {
        for (int i = 0; i < 9; ++i)
        {
            ItemStack s = cp.inventory.mainInventory[i];
            if (s == null) continue;
            int d = Item.getIdFromItem(s.getItem());
            for (int k : ids) if (d == k) return i;
        }
        return -1;
    }

    /** The hotbar slot that breaks b fastest; a tie keeps tools unworn. */
    static int bestSlot(Block b)
    {
        int best = cp.inventory.currentItem;
        double bestScore = -1e9;
        for (int i = 0; i < 9; ++i)
        {
            ItemStack s = cp.inventory.mainInventory[i];
            float str = s == null ? 1.0F : s.func_150997_a(b);
            boolean harvest = b.getMaterial().isToolNotRequired() || (s != null && s.func_150998_b(b));
            double score = (harvest ? 1000 : 0) + str * 10 - (s != null && s.isItemStackDamageable() ? 1 : 0)
                - (s != null && Item.getIdFromItem(s.getItem()) == SSWORD ? 2 : 0) + (i == cp.inventory.currentItem ? 0.1 : 0);
            if (score > bestScore)
            {
                bestScore = score;
                best = i;
            }
        }
        return best;
    }

    static int feetY()
    {
        return MathHelper.floor_double(cp.boundingBox.minY + (stepFloors ? 0.51 : 0.01));
    }

    static int[] cell()
    {
        return new int[] {MathHelper.floor_double(cp.posX), feetY(), MathHelper.floor_double(cp.posZ)};
    }

    static TileEntity tile(int x, int y, int z)
    {
        if (!loaded(x, z)) return null;
        Chunk c = ws.getChunkFromChunkCoords(x >> 4, z >> 4);
        return (TileEntity)c.chunkTileEntityMap.get(new ChunkPosition(x & 15, y, z & 15));
    }

    // ------------------------------------------------------------------ aiming

    static float wrap(float d)
    {
        while (d >= 180.0F) d -= 360.0F;
        while (d < -180.0F) d += 360.0F;
        return d;
    }

    /** Yaw and pitch from the eye (the client player's posY) to a point, the
     * yaw kept next to the current one. */
    static float[] lookAt(double tx, double ty, double tz)
    {
        double dx = tx - cp.posX, dy = ty - cp.posY, dz = tz - cp.posZ;
        float yaw = (float)Math.toDegrees(Math.atan2(-dx, dz));
        float pitch = (float)Math.toDegrees(Math.atan2(-dy, Math.sqrt(dx * dx + dz * dz)));
        if (pitch > 90.0F) pitch = 90.0F;
        if (pitch < -90.0F) pitch = -90.0F;
        return new float[] {cp.rotationYaw + wrap(yaw - cp.rotationYaw), pitch};
    }

    /** What the next tick's getMouseOver block pass sees for this look. */
    static MovingObjectPosition ray(float[] yp)
    {
        float oy = cp.rotationYaw, op = cp.rotationPitch;
        cp.rotationYaw = yp[0];
        cp.rotationPitch = yp[1];
        try
        {
            return cp.rayTrace((double)mc.playerController.getBlockReachDistance(), 1.0F);
        }
        finally
        {
            cp.rotationYaw = oy;
            cp.rotationPitch = op;
        }
    }

    static boolean hits(MovingObjectPosition h, int x, int y, int z)
    {
        return h != null && h.typeOfHit == MovingObjectPosition.MovingObjectType.BLOCK && h.blockX == x && h.blockY == y && h.blockZ == z;
    }

    /** getMouseOver's entity pass for this look: the client entity the
     * crosshair points at, or null. */
    static Entity pointed(float[] yp)
    {
        MovingObjectPosition blockHit = ray(yp);
        Vec3 eye = Vec3.createVectorHelper(cp.posX, cp.posY, cp.posZ);
        double reach = 3.0D;
        double best = blockHit != null ? blockHit.hitVec.distanceTo(eye) : reach;
        float f1 = MathHelper.cos(-yp[0] * 0.017453292F - (float)Math.PI);
        float f2 = MathHelper.sin(-yp[0] * 0.017453292F - (float)Math.PI);
        float f3 = -MathHelper.cos(-yp[1] * 0.017453292F);
        float f4 = MathHelper.sin(-yp[1] * 0.017453292F);
        Vec3 look = Vec3.createVectorHelper((double)(f2 * f3), (double)f4, (double)(f1 * f3));
        Vec3 end = eye.addVector(look.xCoord * reach, look.yCoord * reach, look.zCoord * reach);
        List list = mc.theWorld.getEntitiesWithinAABBExcludingEntity(cp,
            cp.boundingBox.addCoord(look.xCoord * reach, look.yCoord * reach, look.zCoord * reach).expand(1.0D, 1.0D, 1.0D));
        Entity found = null;
        double d = best;
        for (Object o : list)
        {
            Entity e = (Entity)o;
            if (!e.canBeCollidedWith()) continue;
            float border = e.getCollisionBorderSize();
            AxisAlignedBB bb = e.boundingBox.expand((double)border, (double)border, (double)border);
            MovingObjectPosition m = bb.calculateIntercept(eye, end);
            if (bb.isVecInside(eye))
            {
                if (0.0D < d || d == 0.0D)
                {
                    found = e;
                    d = 0.0D;
                }
            }
            else if (m != null)
            {
                double dd = eye.distanceTo(m.hitVec);
                if (dd < d || d == 0.0D)
                {
                    found = e;
                    d = dd;
                }
            }
        }
        return found;
    }

    static final int[][] FACES = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
    static final double[][] OFFS = {{0, 0}, {0.3, 0}, {-0.3, 0}, {0, 0.3}, {0, -0.3}, {0.3, 0.3}, {-0.3, -0.3}, {0.3, -0.3}, {-0.3, 0.3}};

    /** Points on the open faces of block (x,y,z), nearest the eye first. */
    static List<double[]> facePoints(int x, int y, int z, double ex, double ey, double ez)
    {
        List<double[]> pts = new ArrayList<double[]>();
        for (int[] f : FACES)
        {
            if (raySolid(x + f[0], y + f[1], z + f[2])) continue;
            double cx = x + 0.5 + f[0] * 0.5, cy = y + 0.5 + f[1] * 0.5, cz = z + 0.5 + f[2] * 0.5;
            for (double[] o : OFFS)
            {
                double px = cx, py = cy, pz = cz;
                if (f[0] != 0) { py += o[0]; pz += o[1]; }
                else if (f[1] != 0) { px += o[0]; pz += o[1]; }
                else { px += o[0]; py += o[1]; }
                pts.add(new double[] {px, py, pz});
            }
        }
        final double fx = ex, fy = ey, fz = ez;
        pts.sort((a, b) -> Double.compare(d2(a, fx, fy, fz), d2(b, fx, fy, fz)));
        return pts;
    }

    static double d2(double[] p, double x, double y, double z)
    {
        return (p[0] - x) * (p[0] - x) + (p[1] - y) * (p[1] - y) + (p[2] - z) * (p[2] - z);
    }

    /** A look whose crosshair lands on (x,y,z) from where the player stands. */
    static float[] aimAt(int x, int y, int z)
    {
        for (double[] p : facePoints(x, y, z, cp.posX, cp.posY, cp.posZ))
        {
            if (d2(p, cp.posX, cp.posY, cp.posZ) > 4.4 * 4.4) break;
            float[] yp = lookAt(p[0], p[1], p[2]);
            if (hits(ray(yp), x, y, z) && pointed(yp) == null) return yp;
        }
        if (blk(x, y, z) == Blocks.snow_layer)
        {
            // a thin layer: points on its own box, low on the side faces
            for (int[] f : FACES)
            {
                if (f[1] != 0 || raySolid(x + f[0], y, z + f[2])) continue;
                for (double o = -0.3; o <= 0.31; o += 0.3)
                {
                    double px = x + 0.5 + f[0] * 0.5 + (f[0] == 0 ? o : 0), pz = z + 0.5 + f[2] * 0.5 + (f[2] == 0 ? o : 0);
                    float[] yp = lookAt(px, y + 0.06, pz);
                    if (hits(ray(yp), x, y, z) && pointed(yp) == null) return yp;
                }
            }
        }
        return null;
    }

    /** A soft block (leaves, plants, snow) in the way of an aim at (x,y,z). */
    static int[] obstacle(int x, int y, int z)
    {
        for (double[] p : facePoints(x, y, z, cp.posX, cp.posY, cp.posZ))
        {
            if (d2(p, cp.posX, cp.posY, cp.posZ) > 4.4 * 4.4) break;
            MovingObjectPosition h = ray(lookAt(p[0], p[1], p[2]));
            if (h == null || h.typeOfHit != MovingObjectPosition.MovingObjectType.BLOCK) continue;
            Block b = blk(h.blockX, h.blockY, h.blockZ);
            if (b == null) continue;
            Material m = b.getMaterial();
            if (m == Material.leaves || m == Material.plants || m == Material.vine || b == Blocks.snow_layer)
                return new int[] {h.blockX, h.blockY, h.blockZ};
        }
        return null;
    }

    /** Planning line of sight: the first ray-solid voxel from the eye to the
     * point must be the target. */
    static boolean los(double ex, double ey, double ez, double px, double py, double pz, int tx, int ty, int tz)
    {
        double dx = px - ex, dy = py - ey, dz = pz - ez;
        double len = Math.sqrt(dx * dx + dy * dy + dz * dz);
        int n = Math.max(1, (int)Math.ceil(len / 0.1));
        int lx = Integer.MIN_VALUE, ly = 0, lz = 0;
        for (int i = 0; i <= n; ++i)
        {
            double t = (double)i / n;
            int x = MathHelper.floor_double(ex + dx * t), y = MathHelper.floor_double(ey + dy * t), z = MathHelper.floor_double(ez + dz * t);
            if (x == lx && y == ly && z == lz) continue;
            lx = x; ly = y; lz = z;
            if (x == tx && y == ty && z == tz) return true;
            if (raySolid(x, y, z)) return false;
        }
        return true;
    }

    /** Can an eye at (ex,ey,ez) see and reach block t? */
    static boolean sees(double ex, double ey, double ez, int tx, int ty, int tz, double reach)
    {
        for (int[] f : FACES)
        {
            if (raySolid(tx + f[0], ty + f[1], tz + f[2])) continue;
            double px = tx + 0.5 + f[0] * 0.49, py = ty + 0.5 + f[1] * 0.49, pz = tz + 0.5 + f[2] * 0.49;
            double d = Math.sqrt((px - ex) * (px - ex) + (py - ey) * (py - ey) + (pz - ez) * (pz - ez));
            if (d > reach) continue;
            if (los(ex, ey, ez, px, py, pz, tx, ty, tz)) return true;
        }
        return false;
    }

    // ------------------------------------------------------------------ paths

    interface Goal
    {
        boolean ok(int x, int y, int z);
    }

    /** The planners' map from key() to node. A HashMap<Long, ...> folds
     * key()'s x, z and y onto each other in Long.hashCode, and its bins
     * degrade into trees on a large plan. */
    static final class Seen<V>
    {
        long[] keys = new long[1 << 10];
        Object[] vals = new Object[1 << 10];
        int size;

        int slot(long k)
        {
            int m = keys.length - 1;
            int i = (int)((k * 0x9E3779B97F4A7C15L) >>> 40) & m;
            while (vals[i] != null && keys[i] != k) i = (i + 1) & m;
            return i;
        }

        @SuppressWarnings("unchecked")
        V get(long k) { return (V)vals[slot(k)]; }

        void put(long k, V v)
        {
            int i = slot(k);
            if (vals[i] == null)
            {
                if (++size * 2 > keys.length)
                {
                    long[] ok = keys;
                    Object[] ov = vals;
                    keys = new long[ok.length * 2];
                    vals = new Object[ok.length * 2];
                    for (int j = 0; j < ok.length; ++j) if (ov[j] != null) { int s = slot(ok[j]); keys[s] = ok[j]; vals[s] = ov[j]; }
                    i = slot(k);
                }
                keys[i] = k;
            }
            vals[i] = v;
        }

        int size() { return size; }
    }

    static final class Node
    {
        final int x, y, z;
        double cost;
        Node prev;
        boolean closed;
        Node(int x, int y, int z) { this.x = x; this.y = y; this.z = z; }
    }

    static long key(int x, int y, int z)
    {
        return ((long)(x + 33554432) << 34) | ((long)(z + 33554432) << 8) | (long)(y & 255);
    }

    static final int[][] DIRS = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

    /** Dijkstra over stand cells from the player's cell to the cheapest cell
     * the goal accepts; null when none is found inside maxNodes. */
    static List<int[]> plan(Goal g, int maxNodes, Set<Long> avoid)
    {
        return plan(g, maxNodes, avoid, Double.NaN, 0);
    }

    /** Whether the last plan's path ends on a goal cell (false: it is a leg
     * toward the hint, the explored cell nearest it). */
    static boolean planReached;

    static List<int[]> path(Node n)
    {
        ArrayList<int[]> path = new ArrayList<int[]>();
        for (Node p = n; p != null; p = p.prev) path.add(0, new int[] {p.x, p.y, p.z});
        return path;
    }

    /** The same, and when no goal cell is found, a leg to the explored cell
     * nearest (hx, hz) if that is at least two blocks nearer than the start. */
    static List<int[]> plan(Goal g, int maxNodes, Set<Long> avoid, double hx, double hz)
    {
        planReached = false;
        Node near = null;
        double nearD = Double.MAX_VALUE;
        int[] c = cell();
        Seen<Node> seen = new Seen<Node>();
        PriorityQueue<Object[]> pq = new PriorityQueue<Object[]>(64, (a, b) -> Double.compare((Double)a[0], (Double)b[0]));
        Node start = new Node(c[0], c[1], c[2]);
        seen.put(key(c[0], c[1], c[2]), start);
        pq.add(new Object[] {0.0, start});
        if (!stand(c[0], c[1], c[2]))
        {
            // standing on a block edge or mid-air: the neighbors are starts too
            for (int[] d : DIRS)
                for (int dy = 0; dy >= -1; --dy)
                {
                    int x = c[0] + d[0], y = c[1] + dy, z = c[2] + d[1];
                    if (!stand(x, y, z)) continue;
                    Node n = new Node(x, y, z);
                    n.cost = 0.8;
                    n.prev = start;
                    seen.put(key(x, y, z), n);
                    pq.add(new Object[] {0.8, n});
                }
        }
        while (!pq.isEmpty())
        {
            Object[] e = pq.poll();
            Node n = (Node)e[1];
            if (n.closed || (Double)e[0] > n.cost) continue;
            n.closed = true;
            if ((n == start || stand(n.x, n.y, n.z)) && g.ok(n.x, n.y, n.z))
            {
                planReached = true;
                return path(n);
            }
            if (!Double.isNaN(hx) && stand(n.x, n.y, n.z))
            {
                double d = (n.x + 0.5 - hx) * (n.x + 0.5 - hx) + (n.z + 0.5 - hz) * (n.z + 0.5 - hz);
                if (d < nearD)
                {
                    nearD = d;
                    near = n;
                }
            }
            if (seen.size() > maxNodes) break;
            boolean inWater = water(n.x, n.y, n.z);
            for (int k = 0; k < DIRS.length; ++k)
            {
                int dx = DIRS[k][0], dz = DIRS[k][1];
                int x = n.x + dx, z = n.z + dz;
                boolean diag = dx != 0 && dz != 0;
                if (diag)
                {
                    if (!(pass(n.x + dx, n.y, n.z) && pass(n.x + dx, n.y + 1, n.z) && pass(n.x, n.y, n.z + dz) && pass(n.x, n.y + 1, n.z + dz))) continue;
                    if (stand(x, n.y, z)) relax(seen, pq, n, x, n.y, z, 1.414, avoid);
                    continue;
                }
                if (stand(x, n.y, z)) relax(seen, pq, n, x, n.y, z, 1.0, avoid);
                else if (pass(n.x, n.y + 2, n.z) && stand(x, n.y + 1, z)) relax(seen, pq, n, x, n.y + 1, z, 2.0, avoid);
                else if (pass(x, n.y, z) && pass(x, n.y + 1, z))
                {
                    for (int drop = 1; drop <= 3; ++drop)
                    {
                        if (stand(x, n.y - drop, z))
                        {
                            relax(seen, pq, n, x, n.y - drop, z, 1.0 + 0.5 * drop, avoid);
                            break;
                        }
                        if (!pass(x, n.y - drop, z)) break;
                    }
                }
                if (inWater && stand(x, n.y + 1, z)) relax(seen, pq, n, x, n.y + 1, z, 3.0, avoid);
            }
        }
        if (near != null)
        {
            double d0 = (c[0] + 0.5 - hx) * (c[0] + 0.5 - hx) + (c[2] + 0.5 - hz) * (c[2] + 0.5 - hz);
            if (Math.sqrt(nearD) < Math.sqrt(d0) - 2.0) return path(near);
        }
        return null;
    }

    /** While set, plan() only steps on cells it accepts. */
    static Goal planOnly;

    /** plan() kept to the surface (no cave below y 58). */
    static List<int[]> surfacePlan(Goal g, int maxNodes, double hx, double hz)
    {
        planOnly = (a, b, c) -> b >= 58;
        try
        {
            return plan(g, maxNodes, null, hx, hz);
        }
        finally
        {
            planOnly = null;
        }
    }

    static void relax(Seen<Node> seen, PriorityQueue<Object[]> pq, Node from, int x, int y, int z, double step, Set<Long> avoid)
    {
        long k = key(x, y, z);
        if (avoid != null && avoid.contains(k)) return;
        if (planOnly != null && !planOnly.ok(x, y, z)) return;
        if (water(x, y, z)) step += 4.0;
        double c = from.cost + step;
        Node n = seen.get(k);
        if (n == null)
        {
            n = new Node(x, y, z);
            seen.put(k, n);
        }
        else if (n.closed || c >= n.cost) return;
        n.cost = c;
        n.prev = from;
        pq.add(new Object[] {c, n});
    }

    static String pathString(List<int[]> p)
    {
        StringBuilder b = new StringBuilder();
        for (int[] c : p) b.append(c[0]).append(',').append(c[1]).append(',').append(c[2]).append(' ');
        return b.toString().trim();
    }

    static double hdist(int[] t)
    {
        double dx = t[0] + 0.5 - cp.posX, dz = t[2] + 0.5 - cp.posZ;
        return Math.sqrt(dx * dx + dz * dz);
    }

    /** While set, a followed path is walked sneaking (an edge holds the player). */
    static boolean sneakMoves;

    /** Follows a planned path one tick at a time. */
    static final class Follow
    {
        final List<int[]> path;
        int i = 1, best;
        final boolean sprint;
        final double tol;
        int sinceAdvance;

        Follow(List<int[]> p, double tol, boolean sprint)
        {
            path = p;
            this.tol = tol;
            this.sprint = sprint;
            if (p.size() == 1) i = 0;
        }

        static boolean same(int[] a, int[] b) { return a[0] == b[0] && a[1] == b[1] && a[2] == b[2]; }

        /** This tick's act, null on arrival, FAIL when the path is lost. */
        Object step()
        {
            int[] c = cell();
            int n = path.size();
            for (int j = Math.min(n - 1, i + 3); j >= Math.max(0, i - 1); --j)
            {
                if (same(c, path.get(j)))
                {
                    if (j > i) i = j;
                    break;
                }
            }
            if (same(c, path.get(i)) && i < n - 1)
            {
                int[] a = i > 0 ? path.get(i - 1) : path.get(i), b = path.get(i), d = path.get(i + 1);
                boolean straight = i > 0 && (b[0] - a[0]) == (d[0] - b[0]) && (b[2] - a[2]) == (d[2] - b[2]) && a[1] == b[1] && b[1] == d[1];
                if (straight || hdist(b) < 0.35) ++i;
            }
            if (i > best)
            {
                best = i;
                sinceAdvance = 0;
            }
            if (++sinceAdvance > 50) return FAIL;
            int[] t = path.get(i);
            double d = hdist(t);
            double v = Math.sqrt(cp.motionX * cp.motionX + cp.motionZ * cp.motionZ);
            boolean fin = i == n - 1;
            if (fin && same(c, t) && d < tol && v < 0.03) return null;
            A a = new A();
            if (d > 0.05)
            {
                float[] yp = lookAt(t[0] + 0.5, cp.posY, t[2] + 0.5);
                yp[1] = GoldEnd.active() ? GoldEnd.WALK_PITCH : 0.0F;
                a.look(yp);
            }
            boolean coast = fin && same(c, t) && d < 1.2 * v + 0.08;
            if (!coast) a.hold("forward");
            if (cp.isInWater()) a.hold("jump");
            else if (t[1] > c[1] && cp.onGround && d < 1.5) a.hold("jump");
            else if (cp.onGround && sinceAdvance > 15 && sinceAdvance % 10 == 0) a.hold("jump");
            if (sneakMoves) a.hold("sneak");
            else if (sprint && !coast && i + 3 < n)
            {
                boolean flat = true;
                for (int j = i; j < i + 3; ++j) if (path.get(j)[1] != path.get(j + 1)[1]) flat = false;
                if (flat && cp.getFoodStats().getFoodLevel() > 6) a.hold("sprint");
            }
            return a.j();
        }
    }

    /** Walk to the nearest cell the goal accepts; with a hint, far goals are
     * reached in legs toward the hint, each replanned where the last ends. */
    static final class Walk extends Task
    {
        final Goal goal;
        final String what;
        final double tol;
        double hx = Double.NaN, hz;
        boolean sprint;
        Follow f;
        boolean leg;
        int replans, legs;

        Walk(String what, Goal g, double tol) { this.what = what; goal = g; this.tol = tol; }

        Walk hint(double x, double z)
        {
            hx = x;
            hz = z;
            return this;
        }

        String label() { return "Walk(" + what + ")"; }

        Object tick()
        {
            if (f == null)
            {
                List<int[]> p = plan(goal, 60000, null, hx, hz);
                if (p == null) return FAIL;
                leg = !planReached;
                if (leg && ++legs > 40) return FAIL;
                f = new Follow(p, leg ? 0.6 : tol, sprint || p.size() > 12);
                System.out.println("GOLDBOT t=" + Oracle.tick + " path " + what + (leg ? " (leg)" : "") + " from " + where() + ": " + pathString(p));
            }
            Object r = f.step();
            if (r == null)
            {
                if (!leg) return DONE;
                f = null;
                return idle();
            }
            if (r == FAIL)
            {
                f = null;
                if (++replans > 4) return FAIL;
                return idle();
            }
            return r;
        }
    }

    static Walk walkToCell(final int x, final int y, final int z, double tol)
    {
        return new Walk(x + "," + y + "," + z, (a, b, c) -> a == x && b == y && c == z, tol);
    }

    /** Walk until the eye can see and reach block (x,y,z). */
    static Walk walkToSee(final int x, final int y, final int z, final double reach, final Set<Long> bad)
    {
        return new Walk("see " + x + "," + y + "," + z, (Goal)(a, b, c) ->
        {
            if (bad != null && bad.contains(key(a, b, c))) return false;
            if (a == x && c == z && (b - 1 == y || b == y || b + 1 == y)) return false; // on or in the target
            double ex = a + 0.5, ey = b + 1.62, ez = c + 0.5;
            double dx = x + 0.5 - ex, dy = y + 0.5 - ey, dz = z + 0.5 - ez;
            if (dx * dx + dy * dy + dz * dz > (reach + 0.5) * (reach + 0.5)) return false;
            return sees(ex, ey, ez, x, y, z, reach);
        }, 0.3).hint(x + 0.5, z + 0.5);
    }

    // ------------------------------------------------------------------ mining

    static final class Mine extends Task
    {
        final int x, y, z;
        final boolean stay;
        final Set<Long> bad = new HashSet<Long>();
        final int blockId;
        int phase, ticks, obstacles, settle;

        Mine(int x, int y, int z, boolean stay)
        {
            this.x = x;
            this.y = y;
            this.z = z;
            this.stay = stay;
            blockId = id(x, y, z);
        }

        String label() { return "Mine(" + blockId + "@" + x + "," + y + "," + z + ")"; }

        Object tick()
        {
            boolean clientAir = mc.theWorld.getBlock(x, y, z).getMaterial() == Material.air;
            Block b = blk(x, y, z);
            if (clientAir && b != null && b.getMaterial() == Material.air) return DONE;
            if (clientAir)
            {
                // the client broke it; the server has not agreed yet
                if (++settle > 10) return FAIL;
                return idle();
            }
            if (b == null || Block.getIdFromBlock(b) != blockId) return FAIL;
            if (child == FAIL) return FAIL;
            child = null;
            float[] yp = aimAt(x, y, z);
            if (yp == null)
            {
                int[] o = obstacle(x, y, z);
                if (o != null && obstacles < 6)
                {
                    ++obstacles;
                    return new Mine(o[0], o[1], o[2], true);
                }
                if (stay || phase >= 3) return FAIL;
                if (phase > 0) bad.add(key(cell()[0], cell()[1], cell()[2]));
                ++phase;
                return walkToSee(x, y, z, 4.2, bad);
            }
            if (++ticks > 400) return FAIL;
            int s = bestSlot(b);
            if (!b.getMaterial().isToolNotRequired())
            {
                ItemStack st = cp.inventory.mainInventory[s];
                if ((st == null || !st.func_150998_b(b)) && b.getBlockHardness(ws, x, y, z) > 0.5F) throw new Stuck("no tool for block " + blockId);
            }
            return new A().look(yp).hold("attack").hb(s).j();
        }
    }

    /** A block the bot may dig out: no liquid beside it, no falling block on
     * top, not under the player's feet, and the hole it leaves has a way out
     * (a neighbor to walk to at most one block up). */
    static boolean safe(int x, int y, int z)
    {
        for (int[] f : FACES)
        {
            Block b = blk(x + f[0], y + f[1], z + f[2]);
            if (b == null || liquid(b)) return false;
        }
        Block up = blk(x, y + 1, z);
        if (up == Blocks.sand || up == Blocks.gravel || y <= 20) return false;
        AxisAlignedBB feet = cp.boundingBox;
        if (y == feetY() - 1 && x + 1 > feet.minX && x < feet.maxX && z + 1 > feet.minZ && z < feet.maxZ) return false;
        if (!pass(x, y + 1, z)) return true;
        for (int[] d : DIRS)
        {
            if (d[0] != 0 && d[1] != 0) continue;
            int nx = x + d[0], nz = z + d[1];
            if (floor(nx, y - 1, nz) && pass(nx, y, nz) && pass(nx, y + 1, nz)) return true;
            if (floor(nx, y, nz) && pass(nx, y + 1, nz) && pass(nx, y + 2, nz)) return true;
        }
        return false;
    }

    /** Mine blocks of the given kinds, nearest by path first, until the drop
     * count is met. */
    static final class MineFor extends Task
    {
        final int[] blocks, drops;
        final int drop, want;
        final HashSet<Long> bad = new HashSet<Long>();
        int phase;
        int[] target;

        MineFor(int[] blocks, int drop, int want) { this(blocks, new int[] {drop}, want); }

        MineFor(int[] blocks, int[] drops, int want) { this.blocks = blocks; this.drops = drops; this.drop = drops[0]; this.want = want; }

        String label() { return "MineFor(" + drop + " " + count(drops) + "/" + want + ")"; }

        Object tick()
        {
            if (phase == 1)
            {
                phase = 2;
                if (child == FAIL) bad.add(key(target[0], target[1], target[2]));
                return new Collect(drops, 6.0);
            }
            phase = 0;
            if (count(drops) >= want) return DONE;
            target = pick();
            if (target == null) throw new Stuck("no reachable block " + java.util.Arrays.toString(blocks) + " for item " + drop);
            phase = 1;
            return new Mine(target[0], target[1], target[2], false);
        }

        boolean wanted(int x, int y, int z)
        {
            int b = id(x, y, z);
            for (int k : blocks) if (b == k) return !bad.contains(key(x, y, z)) && safe(x, y, z);
            return false;
        }

        /** The nearest target by path, from a cell level with or below it
         * first (a block dug below the feet leaves a hole drops fall into). */
        int[] pick()
        {
            int[] t = pick(0);
            return t != null ? t : pick(-1);
        }

        int[] pick(final int low)
        {
            final int[] found = new int[3];
            List<int[]> p = plan((a, b, c) ->
            {
                double ex = a + 0.5, ey = b + 1.62, ez = c + 0.5;
                for (int dy = low; dy <= 5; ++dy)
                    for (int dx = -4; dx <= 4; ++dx)
                        for (int dz = -4; dz <= 4; ++dz)
                        {
                            int x = a + dx, y = b + dy, z = c + dz;
                            if (dy == -1 && Math.abs(dx) <= 1 && Math.abs(dz) <= 1) continue;
                            if (!wanted(x, y, z)) continue;
                            if (!sees(ex, ey, ez, x, y, z, 4.0)) continue;
                            found[0] = x;
                            found[1] = y;
                            found[2] = z;
                            return true;
                        }
                return false;
            }, 60000, null);
            return p == null ? null : found;
        }
    }

    /** Pick up item entities of the given kinds (null: any) nearby. */
    static final class Collect extends Task
    {
        final int[] ids;
        final double radius;
        final HashSet<Integer> ignore = ignoredItems;
        int target = -1, wait, phase;

        Collect(int[] ids, double radius) { this.ids = ids; this.radius = radius; }

        Object tick()
        {
            if (phase == 1 && child == FAIL && target >= 0)
            {
                ignore.add(target);
                target = -1;
            }
            boolean arrived = phase == 1 && child == DONE;
            phase = 0;
            EntityItem e = target >= 0 && !ignore.contains(target) ? liveItem(target) : null;
            if (e == null)
            {
                wait = 0;
                e = nearest();
                if (e == null) return DONE;
                target = e.getEntityId();
            }
            AxisAlignedBB box = sp.boundingBox.expand(1.0D, 0.5D, 1.0D);
            if (arrived || box.intersectsWith(e.boundingBox) || wait >= 1000)
            {
                // in reach (or standing where it will land): pickup is the
                // server's, after the item's pickup delay
                if (wait < 1000) wait = 1000;
                if (++wait > 1040) ignore.add(target);
                return idle();
            }
            if (++wait > 200)
            {
                ignore.add(target);
                return idle();
            }
            // a cell whose pickup box (the player's box grown by 1, 0.5, 1)
            // takes the item in from the cell's center
            final double ix = e.posX, iz = e.posZ;
            final int iy = MathHelper.floor_double(e.posY + 0.05);
            phase = 1;
            return new Walk("item " + target, (a, b, c) -> Math.abs(ix - (a + 0.5)) < 1.2 && Math.abs(iz - (c + 0.5)) < 1.2 && b <= iy && b >= iy - 2, 0.4);
        }

        EntityItem liveItem(int id)
        {
            Entity e = ws.getEntityByID(id);
            return e instanceof EntityItem && !e.isDead ? (EntityItem)e : null;
        }

        EntityItem nearest()
        {
            EntityItem best = null;
            double bd = radius * radius;
            for (Object o : ws.loadedEntityList)
            {
                if (!(o instanceof EntityItem)) continue;
                EntityItem e = (EntityItem)o;
                if (e.isDead || ignore.contains(e.getEntityId())) continue;
                if (ids != null)
                {
                    int d = Item.getIdFromItem(e.getEntityItem().getItem());
                    boolean ok = false;
                    for (int k : ids) if (d == k) ok = true;
                    if (!ok) continue;
                }
                double dd = e.getDistanceSq(sp.posX, sp.posY, sp.posZ);
                if (dd < bd)
                {
                    bd = dd;
                    best = e;
                }
            }
            return best;
        }
    }

    // ------------------------------------------------------------------ trees

    /** Fell the nearest single-trunk tree: the two bottom logs from beside
     * it, then the rest from inside the trunk column looking up, so every
     * drop falls onto the player. */
    static final class Chop extends Task
    {
        final HashSet<Long> bad;
        int x, y0, z, phase;

        Chop(HashSet<Long> bad) { this.bad = bad; }

        String label() { return "Chop(" + x + "," + y0 + "," + z + ")"; }

        Object tick()
        {
            switch (phase)
            {
                case 0:
                    if (!pick()) throw new Stuck("no tree in reach");
                    phase = 1;
                    return new Mine(x, y0, z, false);
                case 1:
                    if (child == FAIL)
                    {
                        bad.add(key(x, y0, z));
                        return FAIL;
                    }
                    phase = 2;
                    if (isLog(x, y0 + 1, z)) return new Mine(x, y0 + 1, z, false);
                    // fall through
                case 2:
                    phase = 3;
                    return walkToCell(x, y0, z, 0.15);
                case 3:
                    if (child == FAIL)
                    {
                        bad.add(key(x, y0, z));
                        phase = 5;
                        return new Collect(new int[] {LOG, LOG2}, 6.0);
                    }
                    phase = 4;
                    // fall through
                case 4:
                    for (int y = y0 + 1; y <= y0 + 7; ++y)
                    {
                        if (isLog(x, y, z))
                        {
                            if (y - cp.posY > 4.4) break;
                            return new Mine(x, y, z, true);
                        }
                        if (!pass(x, y, z)) break;
                    }
                    phase = 5;
                    return new Idle(12);
                case 5:
                    phase = 6;
                    return new Collect(new int[] {LOG, LOG2}, 6.0);
                default:
                    return DONE;
            }
        }

        boolean isLog(int x, int y, int z)
        {
            int b = id(x, y, z);
            return b == LOG || b == LOG2;
        }

        /** A trunk base (a log on dirt or grass) whose logs are one column
         * at most six high with no branch logs beside it. */
        boolean trunk(int x, int y, int z)
        {
            if (!isLog(x, y, z) || bad.contains(key(x, y, z))) return false;
            int under = id(x, y - 1, z);
            if (under != 2 && under != 3) return false;
            int top = y;
            while (isLog(x, top + 1, z)) ++top;
            if (top - y > 5) return false;
            for (int yy = y; yy <= top + 1; ++yy)
                for (int[] d : DIRS)
                    if (isLog(x + d[0], yy, z + d[1])) return false;
            return true;
        }

        boolean pick()
        {
            final int[] found = new int[3];
            List<int[]> p = plan((a, b, c) ->
            {
                for (int[] d : DIRS)
                {
                    if (d[0] != 0 && d[1] != 0) continue;
                    if (trunk(a + d[0], b, c + d[1]))
                    {
                        found[0] = a + d[0];
                        found[1] = b;
                        found[2] = c + d[1];
                        return true;
                    }
                }
                return false;
            }, 80000, null);
            if (p == null) return false;
            x = found[0];
            y0 = found[1];
            z = found[2];
            return true;
        }
    }

    // ------------------------------------------------------------------ containers

    /** One crafting recipe as rows of a 3x3 grid; the 2x2 uses the top left. */
    static final class Recipe
    {
        final String name;
        final int result, per;
        final int[] grid = new int[9]; // item id per grid cell, row-major, 0 empty
        final boolean all;             // one craft per item of the whole ingredient stack

        Recipe(String name, int result, int per, boolean all, String[] rows, Object... key)
        {
            this.name = name;
            this.result = result;
            this.per = per;
            this.all = all;
            for (int r = 0; r < rows.length; ++r)
                for (int c = 0; c < rows[r].length(); ++c)
                {
                    char ch = rows[r].charAt(c);
                    if (ch == ' ') continue;
                    for (int k = 0; k < key.length; k += 2)
                        if ((Character)key[k] == ch) grid[r * 3 + c] = (Integer)key[k + 1];
                }
        }
    }

    static final Recipe PLANKS_ALL = new Recipe("planks", PLANKS, 4, true, new String[] {"L"}, 'L', LOG);
    static final Recipe STICKS = new Recipe("sticks", STICK, 4, false, new String[] {"P", "P"}, 'P', PLANKS);
    static final Recipe TABLE_R = new Recipe("table", TABLE, 1, false, new String[] {"PP", "PP"}, 'P', PLANKS);
    static final Recipe WPICK_R = new Recipe("wooden pickaxe", WPICK, 1, false, new String[] {"PPP", " S ", " S "}, 'P', PLANKS, 'S', STICK);
    static final Recipe SPICK_R = new Recipe("stone pickaxe", SPICK, 1, false, new String[] {"CCC", " S ", " S "}, 'C', COBBLE, 'S', STICK);
    static final Recipe SSWORD_R = new Recipe("stone sword", SSWORD, 1, false, new String[] {" C ", " C ", " S "}, 'C', COBBLE, 'S', STICK);
    static final Recipe SAXE_R = new Recipe("stone axe", SAXE, 1, false, new String[] {"CC ", "CS ", " S "}, 'C', COBBLE, 'S', STICK);
    static final Recipe FURNACE_R = new Recipe("furnace", FURNACE, 1, false, new String[] {"CCC", "C C", "CCC"}, 'C', COBBLE);
    static final Recipe TORCH_R = new Recipe("torches", TORCH, 4, false, new String[] {"K", "S"}, 'K', COAL, 'S', STICK);

    static int gridSlot(boolean table, int cell)
    {
        if (table) return 1 + cell;
        int r = cell / 3, c = cell % 3;
        if (r > 1 || c > 1) throw new Stuck("recipe does not fit the 2x2 grid");
        return 1 + r * 2 + c;
    }

    /** Main inventory index (0-35) to the open window's slot number. */
    static int invSlot(Container k, int main)
    {
        if (k instanceof ContainerWorkbench) return main < 9 ? 37 + main : 1 + main;
        if (k instanceof ContainerFurnace) return main < 9 ? 30 + main : main - 6;
        return main < 9 ? 36 + main : main;
    }

    static int maxStack(int id)
    {
        return new ItemStack(Item.getItemById(id)).getMaxStackSize();
    }

    /** Things the bot holds in its hand go to the hotbar. */
    static boolean heldKind(int id)
    {
        return id == TABLE || id == FURNACE || id == TORCH || id == WPICK || id == SPICK || id == SSWORD || id == SAXE
            || id == CPORK || id == CBEEF || id == CCHICKEN || id == IPICK || id == BUCKET || id == FLINT_STEEL || id == DPICK
            || id == WATER_BUCKET || id == LAVA_BUCKET || id == ISWORD;
    }

    /** The client's main inventory as the click planner changes it:
     * [id, count, max] per slot. */
    static final class Inv
    {
        final int[][] s = new int[36][];

        Inv()
        {
            for (int i = 0; i < 36; ++i)
            {
                ItemStack st = cp.inventory.mainInventory[i];
                if (st != null) s[i] = new int[] {Item.getIdFromItem(st.getItem()), st.stackSize, st.getMaxStackSize()};
            }
        }

        int largest(int id)
        {
            int best = -1;
            for (int i = 0; i < 36; ++i) if (s[i] != null && s[i][0] == id && (best < 0 || s[i][1] > s[best][1])) best = i;
            return best;
        }

        /** Where a stack of n goes: the same item with room, else an empty
         * slot, the hotbar first for things the bot holds. */
        int dest(int id, int n, int max, boolean hotbar)
        {
            for (int i = 0; i < 36; ++i) if (s[i] != null && s[i][0] == id && s[i][1] + n <= max) return i;
            if (hotbar) for (int i = 0; i < 9; ++i) if (s[i] == null) return i;
            for (int i = 9; i < 36; ++i) if (s[i] == null) return i;
            for (int i = 0; i < 9; ++i) if (s[i] == null) return i;
            throw new Stuck("inventory full");
        }

        /** Seed 1 from S16: a craft's result goes onto a stack of its own with
         * room, else to the last empty slot (a pickup fills the first empty
         * one, and an item picked up during the clicks took the hotbar slot
         * the result was meant for: the furnace was swapped out and lost). */
        int destLast(int id, int n, int max, boolean fresh)
        {
            if (!fresh) for (int i = 0; i < 36; ++i) if (s[i] != null && s[i][0] == id && s[i][1] + n <= max) return i;
            for (int i = 35; i >= 0; --i) if (s[i] == null) return i;
            throw new Stuck("inventory full");
        }

        /** An empty slot for a stack that must not merge (another wood's planks). */
        int destEmpty(boolean hotbar)
        {
            if (hotbar) for (int i = 0; i < 9; ++i) if (s[i] == null) return i;
            for (int i = 9; i < 36; ++i) if (s[i] == null) return i;
            for (int i = 0; i < 9; ++i) if (s[i] == null) return i;
            throw new Stuck("inventory full");
        }

        void add(int d, int id, int n, int max)
        {
            if (s[d] == null) s[d] = new int[] {id, n, max};
            else s[d][1] += n;
        }
    }

    /** Crafts a list of (recipe, times) in the inventory grid or at a table,
     * one window click per tick, then closes the window. */
    static final class Craft extends Task
    {
        final boolean table;
        final Object[] jobs;
        /** Results go to an empty slot, never onto a stack of the same id. */
        boolean fresh;
        final List<int[]> clicks = new ArrayList<int[]>(); // slot, button
        final HashMap<Integer, Integer> expect = new HashMap<Integer, Integer>();
        int phase, ci, wait, settle;
        boolean newTable;

        Craft(boolean table, Object... jobs) { this.table = table; this.jobs = jobs; }

        /** An item the player's pickup box (EntityPlayer.onLivingUpdate's
         * boundingBox.expand(1, 0.5, 1)) reaches, with room a step wider. */
        static boolean pickupNear()
        {
            net.minecraft.util.AxisAlignedBB box = sp.boundingBox.expand(1.5, 1.0, 1.5);
            for (Object o : ws.loadedEntityList)
                if (o instanceof EntityItem && !((Entity)o).isDead && ((Entity)o).boundingBox.intersectsWith(box)) return true;
            return false;
        }

        String label()
        {
            StringBuilder b = new StringBuilder("Craft(");
            for (int i = 0; i < jobs.length; i += 2) b.append(i > 0 ? "," : "").append(((Recipe)jobs[i]).name);
            return b.append(")").toString();
        }

        Object tick()
        {
            switch (phase)
            {
                case 0:
                    // seed 1 from S13: no table within reach of the open, one
                    // placed from the pack (or made from the planks) first
                    if (table && s1k >= 13 && !newTable && findBlock(new int[] {TABLE}, 48) == null)
                    {
                        newTable = true;
                        if (count(TABLE) > 0)
                        {
                            phase = 11;
                            return new ToHotbar(TABLE);
                        }
                        if (count(PLANKS) < 4) return FAIL;
                        phase = 10;
                        return new Craft(false, TABLE_R, 1);
                    }
                    phase = 1;
                    if (table) return new OpenBlock(new int[] {TABLE});
                    return new A().press("inventory").j();
                case 1:
                {
                    // seed 1: a table left out of reach (a drop into a cave
                    // the walker cannot climb back) is replaced from the planks
                    if (table && child == FAIL && s1 && !newTable && count(PLANKS) >= 4)
                    {
                        newTable = true;
                        phase = 10;
                        return new Craft(false, TABLE_R, 1);
                    }
                    if (table && child == FAIL) return FAIL;
                    boolean open = table ? cp.openContainer instanceof ContainerWorkbench : mc.currentScreen instanceof GuiInventory;
                    if (!open)
                    {
                        if (++wait > 10) return FAIL;
                        return idle();
                    }
                    if (cp.inventory.getItemStack() != null) throw new Stuck("cursor not empty");
                    // seed 1 from S16: no layout while an item lies within the
                    // pickup reach (a stack thrown by the tidy before came back
                    // into a slot the clicks meant for the result)
                    if (s1 && s1k >= 16 && ++settle < 80 && pickupNear()) return idle();
                    layout();
                    phase = 2;
                }
                // fall through
                case 2:
                    if (ci < clicks.size())
                    {
                        int[] k = clicks.get(ci++);
                        return new A().gui("click", -1, k[0], k[1], 0).j();
                    }
                    phase = 3;
                    return new Close();
                case 10:
                    if (child == FAIL) return FAIL;
                    phase = 11;
                    return new ToHotbar(TABLE);
                case 11:
                    phase = 12;
                    return new PlaceAny(TABLE, TABLE);
                case 12:
                    if (child == FAIL) return FAIL;
                    phase = 0;
                    return idle();
                default:
                    for (java.util.Map.Entry<Integer, Integer> e : expect.entrySet())
                        if (count(e.getKey()) < e.getValue())
                            throw new Stuck(label() + ": have " + count(e.getKey()) + " of item " + e.getKey() + ", expected " + e.getValue());
                    return DONE;
            }
        }

        void layout()
        {
            Container k = cp.openContainer;
            Inv inv = new Inv();
            HashMap<Integer, Integer> made = new HashMap<Integer, Integer>();
            for (int j = 0; j < jobs.length; j += 2)
            {
                Recipe r = (Recipe)jobs[j];
                int times = (Integer)jobs[j + 1];
                ArrayList<Integer> kinds = new ArrayList<Integer>();
                for (int g : r.grid) if (g != 0 && !kinds.contains(g)) kinds.add(g);
                for (int kind : kinds)
                {
                    int src = inv.largest(kind);
                    if (r.all && src >= 0) times = inv.s[src][1];
                    int cells = 0;
                    for (int g : r.grid) if (g == kind) ++cells;
                    if (src < 0 || inv.s[src][1] < cells * times)
                        throw new Stuck(r.name + " x" + times + ": not enough of item " + kind + " (" + count(kind) + ")");
                    int left = inv.s[src][1];
                    clicks.add(new int[] {invSlot(k, src), 0});
                    inv.s[src] = null;
                    for (int c = 0; c < 9; ++c)
                    {
                        if (r.grid[c] != kind) continue;
                        if (r.all)
                        {
                            clicks.add(new int[] {gridSlot(table, c), 0});
                            left = 0;
                            continue;
                        }
                        for (int t = 0; t < times; ++t) clicks.add(new int[] {gridSlot(table, c), 1});
                        left -= times;
                    }
                    if (left > 0)
                    {
                        clicks.add(new int[] {invSlot(k, src), 0});
                        inv.s[src] = new int[] {kind, left, maxStack(kind)};
                    }
                }
                int max = maxStack(r.result);
                int got = 0;
                // seed 1 from S12: planks from all the logs go to an empty
                // slot (another wood's planks on a stack of the same id
                // swapped onto the cursor, and the table after them was lost)
                boolean fresh = this.fresh || (s1 && s1k >= 12 && r == PLANKS_ALL);
                for (int t = 0; t < times; ++t)
                {
                    if (got + r.per > max)
                    {
                        int d = s1 && s1k >= 16 ? inv.destLast(r.result, got, max, fresh) : fresh ? inv.destEmpty(heldKind(r.result)) : inv.dest(r.result, got, max, heldKind(r.result));
                        clicks.add(new int[] {invSlot(k, d), 0});
                        inv.add(d, r.result, got, max);
                        got = 0;
                    }
                    clicks.add(new int[] {0, 0});
                    got += r.per;
                }
                int d = s1 && s1k >= 16 ? inv.destLast(r.result, got, max, fresh) : fresh ? inv.destEmpty(heldKind(r.result)) : inv.dest(r.result, got, max, heldKind(r.result));
                clicks.add(new int[] {invSlot(k, d), 0});
                inv.add(d, r.result, got, max);
                Integer before = made.get(r.result);
                made.put(r.result, (before == null ? 0 : before) + r.per * times);
            }
            for (java.util.Map.Entry<Integer, Integer> e : made.entrySet())
                expect.put(e.getKey(), count(e.getKey()) + e.getValue() - used(e.getKey()));
        }

        /** How much of an item later jobs in this window consume. */
        int used(int id)
        {
            int n = 0;
            for (int j = 0; j < jobs.length; j += 2)
            {
                Recipe r = (Recipe)jobs[j];
                for (int g : r.grid) if (g == id) n += (Integer)jobs[j + 1];
            }
            return n;
        }
    }

    static int[] findBlock(int[] ids, int r)
    {
        int[] c = cell();
        int[] best = null;
        double bd = 1e9;
        for (int x = c[0] - r; x <= c[0] + r; ++x)
            for (int z = c[2] - r; z <= c[2] + r; ++z)
                for (int y = c[1] - (s1 ? 20 : 10); y <= c[1] + (s1 ? 20 : 10); ++y)
                {
                    int b = id(x, y, z);
                    for (int k : ids)
                    {
                        if (b != k) continue;
                        double d = (x - c[0]) * (x - c[0]) + (z - c[2]) * (z - c[2]) + (y - c[1]) * (y - c[1]);
                        if (d < bd)
                        {
                            bd = d;
                            best = new int[] {x, y, z};
                        }
                    }
                }
        return best;
    }

    /** Right-click the nearest block of a kind until its window opens. */
    static final class OpenBlock extends Task
    {
        final int[] ids;
        int[] at;
        int phase, wait, tries;

        OpenBlock(int[] ids) { this.ids = ids; }

        OpenBlock(int[] ids, int[] at)
        {
            this.ids = ids;
            this.at = at;
        }

        String label() { return "Open(" + ids[0] + ")"; }

        Object tick()
        {
            if (open()) return DONE;
            if (at == null)
            {
                at = findBlock(ids, 48);
                if (at == null) throw new Stuck("no block " + ids[0] + " nearby");
            }
            if (phase == 1)
            {
                if (child == FAIL) return FAIL;
                phase = 2;
            }
            if (wait > 0)
            {
                if (++wait > 8)
                {
                    wait = 0;
                    if (++tries > 3) return FAIL;
                }
                return idle();
            }
            float[] yp = aimAt(at[0], at[1], at[2]);
            if (yp == null)
            {
                if (phase >= 2) return FAIL;
                phase = 1;
                return walkToSee(at[0], at[1], at[2], 3.8, null);
            }
            wait = 1;
            return new A().look(yp).press("use").j();
        }

        boolean open()
        {
            Container k = cp.openContainer;
            if (ids[0] == TABLE) return k instanceof ContainerWorkbench;
            return k instanceof ContainerFurnace;
        }
    }

    /** Close the open window, then a tick with attack up, which clears the
     * leftClickCounter the screen left at 10000. */
    static final class Close extends Task
    {
        int phase;

        Object tick()
        {
            ++phase;
            if (phase == 1) return new A().gui("close").j();
            if (phase == 2) return idle();
            return DONE;
        }
    }

    /** Put the raw food and fuel into the nearest furnace. */
    static final class FurnaceLoad extends Task
    {
        final int[] inputs;
        final int fuel, fuelN;
        final List<int[]> clicks = new ArrayList<int[]>();
        int phase, ci;

        final int[] at;

        FurnaceLoad(int[] at, int[] inputs, int fuel, int fuelN)
        {
            this.at = at;
            this.inputs = inputs;
            this.fuel = fuel;
            this.fuelN = fuelN;
        }

        Object tick()
        {
            switch (phase)
            {
                case 0:
                    phase = 1;
                    return new OpenBlock(new int[] {FURNACE, LIT_FURNACE}, at);
                case 1:
                {
                    if (child == FAIL) return FAIL;
                    Container k = cp.openContainer;
                    Inv inv = new Inv();
                    int src = -1;
                    for (int id : inputs) if (src < 0) src = inv.largest(id);
                    if (src < 0) throw new Stuck("nothing to smelt");
                    clicks.add(new int[] {invSlot(k, src), 0});
                    clicks.add(new int[] {0, 0});
                    int f = inv.largest(fuel);
                    if (f < 0 || inv.s[f][1] < fuelN) throw new Stuck("no fuel");
                    clicks.add(new int[] {invSlot(k, f), 0});
                    // the whole stack in one click: a second click on the
                    // fuel slot can land after the furnace took the first
                    // item, and a client/server mismatch refuses the rest
                    if (fuelN >= inv.s[f][1]) clicks.add(new int[] {1, 0});
                    else for (int i = 0; i < fuelN; ++i) clicks.add(new int[] {1, 1});
                    if (inv.s[f][1] > fuelN) clicks.add(new int[] {invSlot(k, f), 0});
                    phase = 2;
                }
                // fall through
                case 2:
                    if (ci < clicks.size())
                    {
                        int[] c = clicks.get(ci++);
                        return new A().gui("click", -1, c[0], c[1], 0).j();
                    }
                    phase = 3;
                    return new Close();
                default:
                    return DONE;
            }
        }
    }

    static TileEntityFurnace furnaceAt(int[] f)
    {
        TileEntity t = tile(f[0], f[1], f[2]);
        return t instanceof TileEntityFurnace ? (TileEntityFurnace)t : null;
    }

    /** Take the furnace output into the hotbar. */
    static final class FurnaceTake extends Task
    {
        final int[] at;
        int phase;

        FurnaceTake(int[] at) { this.at = at; }

        Object tick()
        {
            switch (phase)
            {
                case 0:
                    phase = 1;
                    return new OpenBlock(new int[] {FURNACE, LIT_FURNACE}, at);
                case 1:
                    if (child == FAIL) return FAIL;
                    if (cp.openContainer.getSlot(2).getStack() == null)
                    {
                        phase = 3;
                        return new Close();
                    }
                    phase = 2;
                    return new A().gui("click", -1, 2, 0, 0).j();
                case 2:
                {
                    ItemStack cur = cp.inventory.getItemStack();
                    phase = 3;
                    if (cur == null) return new Close();
                    int d = new Inv().dest(Item.getIdFromItem(cur.getItem()), cur.stackSize, cur.getMaxStackSize(), true);
                    return new A().gui("click", -1, invSlot(cp.openContainer, d), 0, 0).j();
                }
                case 3:
                    phase = 4;
                    return new Close();
                default:
                    return DONE;
            }
        }
    }

    /** Wait near the furnace until its output holds n items. */
    static final class WaitSmelt extends Task
    {
        final int[] at;
        final int n;
        int t;

        WaitSmelt(int[] at, int n)
        {
            this.at = at;
            this.n = n;
        }

        String label() { return "WaitSmelt(" + n + ")"; }

        Object tick()
        {
            TileEntityFurnace f = furnaceAt(at);
            if (f == null) throw new Stuck("furnace gone");
            ItemStack out = f.getStackInSlot(2);
            if (out != null && out.stackSize >= n) return DONE;
            if (f.getStackInSlot(0) == null && !f.func_145950_i()) return DONE;
            if (++t > 300 + 200 * n) throw new Stuck("smelting never finished: " + f.getStackInSlot(0) + " " + f.getStackInSlot(1) + " " + f.getStackInSlot(2) + " burn " + f.field_145956_a + " cook " + f.field_145961_j + " inv " + invString());
            return idle();
        }
    }

    // ------------------------------------------------------------------ placing

    /** Place a block item on the ground beside the player: aim at the top of
     * the floor block under a free cell nearby and press use once. */
    static final class Place extends Task
    {
        final int item, block;
        int phase, tries;
        int[] at;
        float[] yp;

        Place(int item, int block) { this.item = item; this.block = block; }

        String label() { return "Place(" + item + ")"; }

        Object tick()
        {
            if (at != null && id(at[0], at[1], at[2]) == block) return DONE;
            int hb = hotbarOf(item);
            if (hb < 0) throw new Stuck("item " + item + " not on the hotbar");
            if (phase == 0)
            {
                if (++tries > 4) return FAIL;
                if (!choose()) return FAIL;
                phase = 1;
                return new A().look(yp).hb(hb).j();
            }
            if (phase == 1)
            {
                phase = 2;
                if (!hits(ray(yp), at[0], at[1] - 1, at[2]) || pointed(yp) != null)
                {
                    phase = 0;
                    return idle();
                }
                return new A().look(yp).hb(hb).press("use").j();
            }
            if (phase++ < 5) return idle();
            phase = 0;
            return idle();
        }

        boolean choose()
        {
            int[] c = cell();
            for (int r = 1; r <= 2; ++r)
                for (int dx = -r; dx <= r; ++dx)
                    for (int dz = -r; dz <= r; ++dz)
                    {
                        if (Math.max(Math.abs(dx), Math.abs(dz)) != r) continue;
                        int x = c[0] + dx, y = c[1], z = c[2] + dz;
                        if (id(x, y, z) != 0 || !floor(x, y - 1, z)) continue;
                        int under = id(x, y - 1, z);
                        if (under == TABLE || under == FURNACE || under == LIT_FURNACE) continue;
                        AxisAlignedBB box = AxisAlignedBB.getBoundingBox(x - 0.1, y, z - 0.1, x + 1.1, y + 1, z + 1.1);
                        if (busy(box)) continue;
                        float[] look = lookAt(x + 0.5, y, z + 0.5);
                        MovingObjectPosition h = ray(look);
                        if (!hits(h, x, y - 1, z) || h.sideHit != 1) continue;
                        at = new int[] {x, y, z};
                        yp = look;
                        return true;
                    }
            return false;
        }

        boolean busy(AxisAlignedBB box)
        {
            if (cp.boundingBox.intersectsWith(box)) return true;
            for (Object o : ws.loadedEntityList)
            {
                Entity e = (Entity)o;
                if (!e.isDead && e.preventEntitySpawning && e.boundingBox.expand(0.5, 0.5, 0.5).intersectsWith(box)) return true;
            }
            for (Object o : mc.theWorld.loadedEntityList)
            {
                Entity e = (Entity)o;
                if (!e.isDead && e.preventEntitySpawning && e.boundingBox.expand(0.5, 0.5, 0.5).intersectsWith(box)) return true;
            }
            return false;
        }
    }

    // ------------------------------------------------------------------ animals and food

    /** Chase and kill farm animals until enough meat is held. */
    static final class Hunt extends Task
    {
        final int want, minKills;
        final HashSet<Integer> ignore = new HashSet<Integer>();
        int target = -1, kills, since;
        Follow f;
        double gx, gz;

        final long start = Oracle.tick;

        Hunt(int want, int minKills) { this.want = want; this.minKills = minKills; }

        String label() { return "Hunt(" + (count(RAW_MEAT) + count(COOKED_MEAT)) + "/" + want + " kills " + kills + ")"; }

        Object tick()
        {
            EntityLivingBase e = target >= 0 ? animal(target) : null;
            if (target >= 0 && e == null)
            {
                ++kills;
                target = -1;
                f = null;
                return new Collect(null, 8.0);
            }
            if (count(RAW_MEAT) + count(COOKED_MEAT) >= want && kills >= minKills) return DONE;
            if (segNum() >= 16 && Oracle.tick - start > (s1k >= 17 ? 1200 : 3000))
            {
                System.out.println("GOLDBOT t=" + Oracle.tick + " hunt for food gives up with " + (count(RAW_MEAT) + count(COOKED_MEAT)) + " meat");
                huntGaveUp = Oracle.tick;
                return DONE;
            }
            if (e == null)
            {
                e = nearest();
                if (e == null && segNum() >= 16) return DONE;
                if (e == null) throw new Stuck("no animals left to hunt");
                target = e.getEntityId();
                since = 0;
                f = null;
            }
            if (++since > 1500)
            {
                ignore.add(target);
                target = -1;
                return idle();
            }
            Entity ce = mc.theWorld.getEntityByID(target);
            int sword = swordSlot();
            if (ce != null)
            {
                double cx = ce.posX, cy = ce.posY + ce.height * 0.5, cz = ce.posZ;
                double dx = cx - cp.posX, dy = cy - cp.posY, dz = cz - cp.posZ;
                double d = Math.sqrt(dx * dx + dy * dy + dz * dz);
                if (d < 3.2)
                {
                    float[] yp = lookAt(cx, cy, cz);
                    A a = new A().look(yp).hb(sword);
                    if (pointed(yp) == ce && e.hurtResistantTime <= 10) a.press("attack");
                    else if (d > 2.0) a.hold("forward");
                    return a.j();
                }
            }
            // chase: replan when the animal has left the planned goal
            double ex = e.posX, ez = e.posZ;
            if (f == null || (gx - ex) * (gx - ex) + (gz - ez) * (gz - ez) > 2.25 || since % 20 == 0)
            {
                final int tx = MathHelper.floor_double(ex), ty = MathHelper.floor_double(e.posY + 0.05), tz = MathHelper.floor_double(ez);
                Goal g = (a, b, c) -> Math.abs(a - tx) <= 1 && Math.abs(c - tz) <= 1 && Math.abs(b - ty) <= 1;
                // from G16 the animals are chased over the surface only, not into caves
                List<int[]> p = segNum() >= 16 ? surfacePlan(g, 40000, ex, ez) : plan(g, 40000, null, ex, ez);
                if (p == null)
                {
                    ignore.add(target);
                    target = -1;
                    return idle();
                }
                f = new Follow(p, 0.6, true);
                if (!planReached) System.out.println("GOLDBOT t=" + Oracle.tick + " hunt leg toward " + target + " from " + where() + ": " + pathString(p));
                gx = ex;
                gz = ez;
            }
            Object r = f.step();
            if (r == null || r == FAIL)
            {
                f = null;
                return idle();
            }
            return r;
        }

        EntityLivingBase animal(int id)
        {
            Entity e = ws.getEntityByID(id);
            if (!(e instanceof EntityLivingBase) || e.isDead) return null;
            EntityLivingBase l = (EntityLivingBase)e;
            return l.getHealth() > 0 ? l : null;
        }

        EntityLivingBase nearest()
        {
            EntityLivingBase best = null;
            double bd = 160 * 160;
            for (Object o : ws.loadedEntityList)
            {
                if (!(o instanceof EntityPig || o instanceof EntityCow || o instanceof EntityChicken)) continue;
                EntityLivingBase e = (EntityLivingBase)o;
                if (e.isDead || e.getHealth() <= 0 || ignore.contains(e.getEntityId()) || e.isChild()) continue;
                double d = e.getDistanceSq(sp.posX, sp.posY, sp.posZ);
                if (d < bd)
                {
                    bd = d;
                    best = e;
                }
            }
            return best;
        }
    }

    /** Bring a stack of one of the items from the main inventory to the
     * hotbar through the inventory screen: pick it up, drop it on the
     * hotbar slot (an empty one, else the least useful item, which the
     * cursor takes), put what the cursor holds where the stack was. */
    static final class ToHotbar extends Task
    {
        final int[] ids;
        final List<int[]> clicks = new ArrayList<int[]>();
        int phase, ci, wait;

        ToHotbar(int... ids) { this.ids = ids; }

        Object tick()
        {
            switch (phase)
            {
                case 0:
                    if (hotbarOf(ids) >= 0) return DONE;
                    phase = 1;
                    return new A().press("inventory").j();
                case 1:
                {
                    if (!(mc.currentScreen instanceof GuiInventory))
                    {
                        if (++wait > 10) return FAIL;
                        return idle();
                    }
                    int from = -1;
                    for (int i = 9; i < 36 && from < 0; ++i)
                    {
                        ItemStack st = cp.inventory.mainInventory[i];
                        if (st == null) continue;
                        for (int k : ids) if (Item.getIdFromItem(st.getItem()) == k) from = i;
                    }
                    if (from < 0) throw new Stuck("nothing to bring to the hotbar");
                    int to = -1;
                    for (int i = 0; i < 9 && to < 0; ++i) if (cp.inventory.mainInventory[i] == null) to = i;
                    int[] junk = {6, LOG, LOG2, 3, 2, 12, 13, PLANKS, COBBLE, PORK, WPICK, SPICK, SAXE, IPICK, TORCH};
                    // G26 on: the trek's hotbar fills with things it never holds
                    if (trekNum() >= 26) junk = new int[] {295, 287, 265, 318, 334, 289, 375, 352, 6, LOG, LOG2, 3, 2, 12, 13, PLANKS,
                        COBBLE, 259, WATER_BUCKET, ROTTEN, PORK, WPICK, SAXE, TORCH};
                    // seed 1 from S17: the hunt's hotbar too (bones, seeds and
                    // netherrack left no slot for a new sword's table)
                    if (s1 && s1k >= 17) junk = new int[] {87, 352, 295, 287, 265, 318, 334, 289, 375, 6, 37, 38, 175, LOG, LOG2, 3, 2, 12, 13, PLANKS,
                        COBBLE, 259, WATER_BUCKET, ROTTEN, PORK, WPICK, SAXE, TORCH};
                    for (int j : junk)
                        for (int i = 0; i < 9 && to < 0; ++i)
                            if (Item.getIdFromItem(cp.inventory.mainInventory[i].getItem()) == j) to = i;
                    if (to < 0) throw new Stuck("no hotbar slot to free");
                    Container k = cp.openContainer;
                    clicks.add(new int[] {invSlot(k, from), 0});
                    clicks.add(new int[] {invSlot(k, to), 0});
                    if (cp.inventory.mainInventory[to] != null) clicks.add(new int[] {invSlot(k, from), 0});
                    phase = 2;
                }
                // fall through
                case 2:
                    if (ci < clicks.size())
                    {
                        int[] c = clicks.get(ci++);
                        return new A().gui("click", -1, c[0], c[1], 0).j();
                    }
                    phase = 3;
                    return new Close();
                default:
                    return hotbarOf(ids) >= 0 ? DONE : FAIL;
            }
        }
    }

    /** Eat cooked meat, looking at the sky, until the food bar is full. */
    static final class Eat extends Task
    {
        int t;

        Object tick()
        {
            if (child == FAIL) return FAIL;
            if (hotbarOf(COOKED_MEAT) < 0 && count(COOKED_MEAT) > 0 && sp.getFoodStats().getFoodLevel() < 20) return new ToHotbar(COOKED_MEAT);
            int slot = hotbarOf(COOKED_MEAT);
            if (sp.getFoodStats().getFoodLevel() >= 20 || slot < 0)
            {
                if (cp.isUsingItem() || t > 0)
                {
                    t = -1000;
                    return idle();
                }
                return DONE;
            }
            if (++t > 400) throw new Stuck("eating never filled the food bar");
            return new A().look(new float[] {cp.rotationYaw, -80.0F}).hb(slot).hold("use").j();
        }
    }

    // ------------------------------------------------------------------ segments

    abstract static class Seg extends Task
    {
        int phase;

        void need(boolean ok, String what)
        {
            if (!ok) throw new Stuck(label() + " phase " + phase + ": " + what);
        }

        String label() { return super.label() + "#" + phase; }
    }

    /** G1: logs, planks, sticks, table, wooden pickaxe, 3 cobblestone. */
    static final class SegG1 extends Seg
    {
        final HashSet<Long> badTrees = new HashSet<Long>();

        Object tick()
        {
            switch (phase)
            {
                case 0:
                    if (count(LOG, LOG2) >= 5)
                    {
                        phase = 1;
                        return idle();
                    }
                    return new Chop(badTrees);
                case 1:
                    phase = 2;
                    return new Craft(false, PLANKS_ALL, 1, STICKS, 1, TABLE_R, 1);
                case 2:
                    need(child != FAIL, "planks, sticks, table not crafted");
                    phase = 3;
                    return new Place(TABLE, TABLE);
                case 3:
                    need(child != FAIL, "table not placed");
                    phase = 4;
                    return new Craft(true, WPICK_R, 1);
                case 4:
                    need(child != FAIL, "no wooden pickaxe");
                    phase = 5;
                    return new MineFor(new int[] {STONE}, COBBLE, 3);
                case 5:
                    phase = 6;
                    return new Idle(3);
                default:
                    return DONE;
            }
        }
    }

    /** G2: stone pickaxe, cobblestone, surface coal, sword, axe, furnace, torches. */
    static final class SegG2 extends Seg
    {
        Object tick()
        {
            switch (phase)
            {
                case 0:
                    phase = 1;
                    return new Craft(true, SPICK_R, 1);
                case 1:
                    need(child != FAIL, "no stone pickaxe");
                    phase = 2;
                    return new MineFor(new int[] {STONE}, COBBLE, 13);
                case 2:
                    phase = 3;
                    return new MineFor(new int[] {COAL_ORE}, COAL, 3);
                case 3:
                    phase = 4;
                    return new Craft(true, STICKS, 1, SSWORD_R, 1, SAXE_R, 1, FURNACE_R, 1, TORCH_R, 1);
                case 4:
                    need(child != FAIL, "tools not crafted");
                    phase = 5;
                    return new Place(FURNACE, FURNACE);
                case 5:
                    need(child != FAIL, "furnace not placed");
                    phase = 6;
                    return new Place(TORCH, TORCH);
                case 6:
                    phase = 7;
                    return new Idle(3);
                default:
                    return DONE;
            }
        }
    }

    /** G3: hunt, cook the meat, eat to full. */
    static final class SegG3 extends Seg
    {
        int cooking;
        int[] furnace;

        Object tick()
        {
            switch (phase)
            {
                case 0:
                    furnace = findBlock(new int[] {FURNACE, LIT_FURNACE}, 16);
                    need(furnace != null, "no furnace near the start");
                    phase = 1;
                    return new Hunt(4, 3);
                case 1:
                    cooking = count(RAW_MEAT);
                    phase = 2;
                    return new FurnaceLoad(furnace, RAW_MEAT, COAL, 1);
                case 2:
                    need(child != FAIL, "furnace not loaded");
                    phase = 3;
                    return new WaitSmelt(furnace, Math.min(cooking, 8));
                case 3:
                    phase = 4;
                    return new FurnaceTake(furnace);
                case 4:
                    need(count(COOKED_MEAT) > 0, "no cooked meat");
                    phase = 5;
                    return new Eat();
                case 5:
                    need(child != FAIL && sp.getFoodStats().getFoodLevel() >= 20, "not fed to full");
                    phase = 6;
                    return new Idle(3);
                default:
                    return DONE;
            }
        }
    }

    /** Where the blocks of each kind are in the loaded chunks, nearest first. */
    static List<int[]> scan(int[] ids, int ymin, int ymax)
    {
        int[] c = cell();
        List<int[]> out = new ArrayList<int[]>();
        int cx = c[0] >> 4, cz = c[2] >> 4;
        for (int x = (cx - 10) * 16; x < (cx + 11) * 16; ++x)
            for (int z = (cz - 10) * 16; z < (cz + 11) * 16; ++z)
            {
                if (!loaded(x, z)) continue;
                for (int y = ymin; y <= ymax; ++y)
                {
                    int b = id(x, y, z);
                    for (int k : ids) if (b == k) out.add(new int[] {x, y, z, b});
                }
            }
        final int x0 = c[0], y0 = c[1], z0 = c[2];
        out.sort((a, b) -> Integer.compare((a[0] - x0) * (a[0] - x0) + (a[1] - y0) * (a[1] - y0) + (a[2] - z0) * (a[2] - z0),
            (b[0] - x0) * (b[0] - x0) + (b[1] - y0) * (b[1] - y0) + (b[2] - z0) * (b[2] - z0)));
        return out;
    }

    static final class Survey extends Task
    {
        Object tick()
        {
            System.out.println("SURVEY at " + where() + " inv=" + invString() + " time=" + ws.getWorldTime());
            for (int y = 38; y >= 34; --y)
            {
                System.out.println("SURVEY y=" + y);
                for (int z = -126; z <= -100; ++z)
                {
                    StringBuilder b = new StringBuilder("SURVEY ");
                    for (int x = -210; x <= -182; ++x)
                    {
                        int i = id(x, y, z);
                        b.append(i == 10 || i == 11 ? (meta(x, y, z) == 0 ? 'L' : 'l') : i == 8 || i == 9 ? 'w' : i == 0 ? '.' : i == 1 ? '#' : i == 49 ? 'O' : 'x');
                    }
                    System.out.println(b + " z=" + z);
                }
            }
            int[][] kinds = {{15}, {13}, {56}, {10, 11}, {8, 9}, {49}, {16}};
            for (int[] k : kinds)
            {
                List<int[]> l = scan(k, 1, 90);
                StringBuilder b = new StringBuilder("SURVEY " + k[0] + " n=" + l.size() + ":");
                for (int i = 0; i < Math.min(12, l.size()); ++i) b.append(' ').append(l.get(i)[0]).append(',').append(l.get(i)[1]).append(',').append(l.get(i)[2]);
                System.out.println(b);
            }
            return DONE;
        }
    }

    // ------------------------------------------------------------------ interrupts

    /** A task pushed over whatever runs; when it ends the task under it gets
     * back the child result it had. */
    abstract static class Interrupt extends Task
    {
        Object saved;
    }

    static boolean hostile(Entity e)
    {
        if (e instanceof net.minecraft.entity.monster.EntityPigZombie && ((net.minecraft.entity.monster.EntityPigZombie)e).getEntityToAttack() == null) return false;
        return e instanceof net.minecraft.entity.monster.IMob && !(e instanceof net.minecraft.entity.monster.EntityEnderman)
            && !e.isDead && ((EntityLivingBase)e).getHealth() > 0;
    }

    /** The nearest hostile within r of the player, by server entity. */
    static EntityLivingBase threat(double r)
    {
        EntityLivingBase best = null;
        double bd = r * r;
        for (Object o : ws.loadedEntityList)
        {
            if (!(o instanceof EntityLivingBase) || !hostile((Entity)o)) continue;
            Long since = unreachable.get(((Entity)o).getEntityId());
            if (since != null && Oracle.tick - since < 100) continue;
            EntityLivingBase e = (EntityLivingBase)o;
            double d = e.getDistanceSq(sp.posX, sp.posY + 1.0, sp.posZ);
            if (d < bd)
            {
                bd = d;
                best = e;
            }
        }
        return best;
    }

    /** Before the brain ticks: fight a hostile that came close, eat when
     * hungry. Never while a window is open. */
    static void interrupt()
    {
        if (GoldEnd.active())
        {
            GoldEnd.interrupt();
            return;
        }
        if (segNum() < 4 || mc.currentScreen != null || cp.openContainer != cp.inventoryContainer) return;
        if (farming || sheltering || (posted && trekPost != null && trekPost.in())) return;
        Task top = brain.stack.peek();
        if (top == null || top instanceof Interrupt) return;
        Interrupt t = null;
        // seed 1: under water with the air running out, up and to the shore
        // (a walk to an item sank the bot in a pond, and nothing planned from there)
        if (s1 && sp.getAir() < 200 && sp.isInsideOfMaterial(Material.water))
        {
            t = new GoldSeed1.Surface();
            t.saved = top.child;
            brain.stack.push(t);
            return;
        }
        EntityLivingBase h = threat(4.5);
        // seed 1: the trek's fights and meals in every segment (Defend gave
        // up on a zombie it could not aim at, and the zombie killed the bot)
        if (trekNum() >= 16 || s1)
        {
            EntityLivingBase b = brawlTarget();
            EntityLivingBase en = enderClose();
            // seed 1: a blaze is fought from the farm only (charged in the
            // open, blazes killed the bot by the Throne)
            if (s1 && b != null && (b instanceof net.minecraft.entity.monster.EntityBlaze || b instanceof net.minecraft.entity.monster.EntityGhast)) b = null;
            Interrupt esc = s1 ? GoldSeed1.caveSpiderEscape() : null;
            if (esc != null) t = esc;
            else if (en != null && !(top instanceof CaveHunt) && !(top instanceof EnderFight)) t = new EnderFight(en.getEntityId());
            else if (b != null) t = new Brawl(b.getEntityId());
            // seed 1 from S15: walled in to heal at 8 health or less (a witch's
            // poison took the bot to 1 while it dug on after an enderman)
            else if (s1k >= 15 && (sp.getHealth() <= 8.0F || (sp.getHealth() <= 12.0F && unreachableAttacker())) && sp.dimension != 1 && Oracle.tick - GoldSeed1.lastRecover > 600 && !GoldSeed1.recovering()) t = new GoldSeed1.Recover();
            else if (sp.getFoodStats().getFoodLevel() <= 14 && count(COOKED_MEAT) > 0 && hotbarOf(COOKED_MEAT) >= 0 && !(top instanceof Eat)) t = new EatNow();
        }
        else if (enderHunting && segNum() >= 15 && sp.getHealth() <= 12.0F && Oracle.tick >= noShelterUntil) t = new Shelter();
        else if (h != null) t = new Defend(h.getEntityId());
        else if (enderHunting && !(top instanceof EnderHunt) && enderClose() != null) t = new EnderFight(enderClose().getEntityId());
        else if (sp.getFoodStats().getFoodLevel() <= 14 && count(COOKED_MEAT) > 0 && hotbarOf(COOKED_MEAT) >= 0 && !(top instanceof Eat)) t = new EatNow();
        if (t == null) return;
        t.saved = top.child;
        brain.stack.push(t);
    }

    /** A hostile after the player within 6 that a brawl could not reach in
     * the last 200 ticks. */
    static boolean unreachableAttacker()
    {
        for (Object o : ws.loadedEntityList)
        {
            if (!(o instanceof EntityLivingBase) || !hostile((Entity)o)) continue;
            Long since = unreachable.get(((Entity)o).getEntityId());
            if (since != null && Oracle.tick - since < 200 && targets((Entity)o) && ((Entity)o).getDistanceSqToEntity(sp) < 36.0) return true;
        }
        return false;
    }

    /** A liquid on a face of (x, y, z). */
    static boolean liquidNear(int x, int y, int z)
    {
        for (int[] f : FACES)
        {
            Block n = blk(x + f[0], y + f[1], z + f[2]);
            if (n == null || n.getMaterial().isLiquid()) return true;
        }
        return false;
    }

    /** Hit a hostile with the sword until it is dead or gone. */
    /** Hostiles Defend could not hit, by entity id: left alone. */
    static final HashMap<Integer, Long> unreachable = new HashMap<Integer, Long>();

    static final class Defend extends Interrupt
    {
        final int target;
        int t, miss;

        Defend(int target) { this.target = target; }

        String label() { return "Defend(" + target + ")"; }

        Object tick()
        {
            Entity e = ws.getEntityByID(target);
            if (e == null || !hostile(e) || e.getDistanceSq(sp.posX, sp.posY + 1.0, sp.posZ) > 6.0 * 6.0 || ++t > 300) return DONE;
            Entity ce = mc.theWorld.getEntityByID(target);
            if (ce == null) return DONE;
            float[] yp = null;
            for (double fy : new double[] {0.5, 0.8, 0.25, 0.95, 0.1})
            {
                float[] l = lookAt(ce.posX, ce.boundingBox.minY + ce.height * fy, ce.posZ);
                if (yp == null) yp = l;
                if (pointed(l) == ce)
                {
                    yp = l;
                    break;
                }
            }
            A a = new A().look(yp).hb(swordSlot());
            if (pointed(yp) == ce)
            {
                miss = 0;
                if (((EntityLivingBase)e).hurtResistantTime <= 10) a.press("attack");
            }
            else if (++miss > 20)
            {
                // behind a wall or out of reach: leave it
                System.out.println("GOLDBOT t=" + Oracle.tick + " leaving " + e.getClass().getSimpleName() + " " + target);
                unreachable.put(target, (long)Oracle.tick);
                return DONE;
            }
            return a.j();
        }
    }

    /** G16 on: the hostile to fight: one after the player within 6 blocks,
     * or any within 3, in sight and not far above or below. */
    static EntityLivingBase brawlTarget()
    {
        EntityLivingBase best = null;
        double bd = 1e9;
        for (Object o : ws.loadedEntityList)
        {
            if (!(o instanceof EntityLivingBase) || !hostile((Entity)o)) continue;
            EntityLivingBase e = (EntityLivingBase)o;
            Long since = unreachable.get(e.getEntityId());
            if (since != null && Oracle.tick - since < 200) continue;
            double d = e.getDistanceSqToEntity(sp);
            // G23 on: an archer after the player is charged from 12 blocks
            double r2 = segNum() >= 23 && e instanceof net.minecraft.entity.monster.EntitySkeleton && targets(e) ? 144.0 : 36.0;
            if (d >= bd || d > r2 || Math.abs(e.posY - sp.posY) > 3.0) continue;
            if (d > 9.0 && !targets(e)) continue;
            if (!nearLoaded(e) || !sp.canEntityBeSeen(e)) continue;
            bd = d;
            best = e;
        }
        return best;
    }

    /** Fight a hostile: hit it when the crosshair is on it, else face it and
     * step toward it while the ground that way is safe. */
    static final class Brawl extends Interrupt
    {
        final int target;
        int t, miss;

        Brawl(int target) { this.target = target; }

        String label() { return "Brawl(" + target + ")"; }

        Object tick()
        {
            Entity e = ws.getEntityByID(target);
            double lim = segNum() >= 23 && e instanceof net.minecraft.entity.monster.EntitySkeleton ? 14.0 : 8.0;
            if (e == null || !hostile(e) || e.getDistanceSqToEntity(sp) > lim * lim || ++t > 400) return DONE;
            Entity ce = mc.theWorld.getEntityByID(target);
            if (ce == null) return DONE;
            // seed 1: back off a creeper whose fuse is lit, until it is out
            // of its blast (one took 16 of 18 health)
            if (s1 && e instanceof net.minecraft.entity.monster.EntityCreeper && ((net.minecraft.entity.monster.EntityCreeper)e).getCreeperState() > 0
                && e.getDistanceSqToEntity(sp) < 36.0)
            {
                float[] l = lookAt(ce.posX, ce.boundingBox.minY + ce.height * 0.5, ce.posZ);
                l[1] = 0.0F;
                return new A().look(l).hold("back").j();
            }
            for (double fy : new double[] {0.5, 0.8, 0.25, 0.95, 0.1})
            {
                float[] l = lookAt(ce.posX, ce.boundingBox.minY + ce.height * fy, ce.posZ);
                if (pointed(l) == ce)
                {
                    miss = 0;
                    A a = new A().look(l).hb(swordSlot());
                    if (((EntityLivingBase)e).hurtResistantTime <= 10) a.press("attack");
                    return a.j();
                }
            }
            // seed 1 from S27: a block between the eye and a mob close by (a
            // tunnel's corner, a cave spider diagonal to the feet) is mined
            if (s1k >= 21 && child != FAIL && miss % 20 == 10 && e.getDistanceSqToEntity(sp) < 9.0)
            {
                float[] l0 = lookAt(ce.posX, ce.boundingBox.minY + ce.height * 0.5, ce.posZ);
                MovingObjectPosition h = ray(l0);
                if (h != null && h.typeOfHit == MovingObjectPosition.MovingObjectType.BLOCK && diggable(h.blockX, h.blockY, h.blockZ)
                    && !liquidNear(h.blockX, h.blockY, h.blockZ) && !falling(blk(h.blockX, h.blockY + 1, h.blockZ)))
                {
                    ++miss;
                    return new Mine(h.blockX, h.blockY, h.blockZ, true);
                }
            }
            if (++miss > 80)
            {
                System.out.println("GOLDBOT t=" + Oracle.tick + " brawl: cannot reach " + e.getClass().getSimpleName() + " " + target
                    + String.format(" at %.2f,%.2f,%.2f (client %.2f,%.2f,%.2f) from %s", e.posX, e.posY, e.posZ, ce.posX, ce.posY, ce.posZ, where()));
                unreachable.put(target, (long)Oracle.tick);
                return DONE;
            }
            float[] l = lookAt(ce.posX, ce.boundingBox.minY + ce.height * 0.5, ce.posZ);
            A a = new A().look(l).hb(swordSlot());
            double dx = ce.posX - cp.posX, dz = ce.posZ - cp.posZ, hd = Math.sqrt(dx * dx + dz * dz);
            if (hd > 1.6)
            {
                int[] c = cell();
                int nx = MathHelper.floor_double(cp.posX + dx / hd * 0.9), nz = MathHelper.floor_double(cp.posZ + dz / hd * 0.9);
                boolean up = !stand(nx, c[1], nz) && stand(nx, c[1] + 1, nz) && pass(c[0], c[1] + 2, c[2]);
                boolean ok = (nx == c[0] && nz == c[2]) || stand(nx, c[1], nz) || up || stand(nx, c[1] - 1, nz);
                if (ok)
                {
                    a.hold("forward");
                    if (up && cp.onGround) a.hold("jump");
                }
            }
            return a.j();
        }
    }

    static final class EatNow extends Interrupt
    {
        int t;

        Object tick()
        {
            int slot = hotbarOf(COOKED_MEAT);
            if (sp.getFoodStats().getFoodLevel() >= 20 || slot < 0 || t > 200)
            {
                if (cp.isUsingItem()) return idle();
                return DONE;
            }
            ++t;
            return new A().look(s1 && s1k >= 16 ? eatLook() : new float[] {cp.rotationYaw, cp.rotationPitch}).hb(slot).hold("use").j();
        }
    }

    /** Blocks a right click activates before the held food is eaten
     * (onBlockActivated answers true): the furnace, the table, chests,
     * doors and the rest. */
    static final int[] ACTIVATED = {23, 25, 26, 54, 58, 61, 62, 64, 69, 71, 77, 84, 93, 94, 96, 107, 116, 117, 118, 130, 138, 143, 145, 146, 149, 150, 154, 158};

    /** Seed 1 from S16: the look to eat with, the current one unless the
     * crosshair rests on a block a right click activates, else straight up
     * (eating at the furnace just cooked in reopened it, 200 ticks a try,
     * and the bot starved beside cooked meat). */
    static float[] eatLook()
    {
        net.minecraft.util.MovingObjectPosition m = mc.objectMouseOver;
        if (m != null && m.typeOfHit == net.minecraft.util.MovingObjectPosition.MovingObjectType.BLOCK)
        {
            int b = id(m.blockX, m.blockY, m.blockZ);
            for (int k : ACTIVATED) if (b == k) return new float[] {cp.rotationYaw, -90.0F};
        }
        return new float[] {cp.rotationYaw, cp.rotationPitch};
    }

    /** While set, no interrupt fires: the bot is in (or digging) its shelter. */
    static boolean sheltering;
    static long noShelterUntil;

    /** A shaft three blocks down from feet cell (x, y, z) that can be capped:
     * the three blocks diggable, a floor under them, solid walls beside the
     * two cells the player will fill, and a solid side at the cap. */
    static boolean shaftOk(int x, int y, int z)
    {
        for (int k = 1; k <= 3; ++k) if (!diggable(x, y - k, z)) return false;
        if (!floor(x, y - 4, z)) return false;
        boolean capSide = false;
        for (int[] d : DIRS)
        {
            if (d[0] != 0 && d[1] != 0) continue;
            if (!floor(x + d[0], y - 3, z + d[1]) || !floor(x + d[0], y - 2, z + d[1])) return false;
            if (floor(x + d[0], y - 1, z + d[1])) capSide = true;
        }
        return capSide && safeCell(x, y - 3, z);
    }

    /** Hurt while hunting: dig three blocks down, cap the shaft, eat and wait
     * there until healed. The hunt's way back to the surface climbs out. */
    static final class Shelter extends Interrupt
    {
        int phase, t, x, y, z, walks, waited, wi;
        final List<int[]> walls = new ArrayList<int[]>();

        String label() { return "Shelter(" + phase + ")"; }

        /** No shaft to dig: wall the player in where it stands, with cobble. */
        Object wallIn()
        {
            int[] c = cell();
            x = c[0];
            y = c[1];
            z = c[2];
            walls.clear();
            wi = 0;
            walks = 0;
            for (int dy = 0; dy <= 1; ++dy)
                for (int[] d : DIRS)
                    if (d[0] == 0 || d[1] == 0) walls.add(new int[] {x + d[0], y + dy, z + d[1]});
            // the roof: a block over a head-row wall first, the roof against its side
            int[] rs = null;
            for (int[] d : DIRS)
                if ((d[0] == 0 || d[1] == 0) && rs == null && !pass(x + d[0], y + 2, z + d[1])) rs = d;
            if (rs == null)
            {
                walls.add(new int[] {x + 1, y + 2, z});
            }
            walls.add(new int[] {x, y + 2, z});
            phase = 20;
            System.out.println("GOLDBOT t=" + Oracle.tick + " shelter walls in at " + x + "," + y + "," + z + " hp=" + sp.getHealth());
            return idle();
        }

        Object leave(String why)
        {
            System.out.println("GOLDBOT t=" + Oracle.tick + " shelter " + why + " at " + where() + " hp=" + sp.getHealth() + " food=" + sp.getFoodStats().getFoodLevel());
            sheltering = false;
            if (cp.isUsingItem()) return idle();
            return DONE;
        }

        Object tick()
        {
            sheltering = true;
            ++t;
            if (sp.getHealth() <= 0) return leave("dead");
            if (phase == 6) return leave("out");
            if (phase == 0)
            {
                if (child == FAIL) child = null;
                int[] c = cell();
                if (shaftOk(c[0], c[1], c[2]))
                {
                    x = c[0];
                    y = c[1];
                    z = c[2];
                    // the whole box over the one block, so the player falls
                    AxisAlignedBB bb = cp.boundingBox;
                    if (bb.minX < x + 0.02 || bb.maxX > x + 0.98 || bb.minZ < z + 0.02 || bb.maxZ > z + 0.98)
                    {
                        if (++walks > 6)
                        {
                            noShelterUntil = Oracle.tick + 200;
                            return leave("could not center");
                        }
                        return walkToCell(x, y, z, 0.12);
                    }
                    phase = 1;
                    System.out.println("GOLDBOT t=" + Oracle.tick + " shelter dig at " + x + "," + y + "," + z + " hp=" + sp.getHealth());
                    return new Mine(x, y - 1, z, true);
                }
                if (++walks > 3)
                {
                    if (segNum() >= 16) return wallIn();
                    noShelterUntil = Oracle.tick + 200;
                    return leave("found no shaft");
                }
                int[] best = null;
                double bd = 1e9;
                for (int dx = -4; dx <= 4; ++dx)
                    for (int dz = -4; dz <= 4; ++dz)
                        for (int dy = -1; dy <= 1; ++dy)
                        {
                            int nx = c[0] + dx, ny = c[1] + dy, nz = c[2] + dz;
                            if (!pass(nx, ny, nz) || !pass(nx, ny + 1, nz) || !floor(nx, ny - 1, nz) || !shaftOk(nx, ny, nz)) continue;
                            double d = dx * dx + dz * dz + dy * dy;
                            if (d < bd)
                            {
                                bd = d;
                                best = new int[] {nx, ny, nz};
                            }
                        }
                if (best == null)
                {
                    if (segNum() >= 16) return wallIn();
                    noShelterUntil = Oracle.tick + 200;
                    return leave("found no shaft");
                }
                return walkToCell(best[0], best[1], best[2], 0.3);
            }
            if (phase == 20)
            {
                // centred on the cell: the walls, feet row, head row, then the roof
                AxisAlignedBB bb = cp.boundingBox;
                if ((bb.minX < x + 0.02 || bb.maxX > x + 0.98 || bb.minZ < z + 0.02 || bb.maxZ > z + 0.98) && ++walks < 10)
                    return walkToCell(x, y, z, 0.12);
                if (child == FAIL) child = null;
                while (wi < walls.size())
                {
                    int[] w = walls.get(wi++);
                    if (pass(w[0], w[1], w[2]) && !water(w[0], w[1], w[2])) return new PlaceAt(COBBLE, COBBLE, w, null);
                }
                phase = 5;
                System.out.println("GOLDBOT t=" + Oracle.tick + " walled in at " + where() + " hp=" + sp.getHealth() + " food=" + sp.getFoodStats().getFoodLevel());
                return idle();
            }
            if (phase <= 3)
            {
                // each Mine: the block under the feet, then the fall
                if (child == FAIL)
                {
                    if (segNum() >= 16)
                    {
                        child = null;
                        return wallIn();
                    }
                    noShelterUntil = Oracle.tick + 200;
                    return leave("dig failed");
                }
                child = null;
                if (feetY() > y - phase)
                {
                    // knocked off the hole's column: back over it
                    AxisAlignedBB bb = cp.boundingBox;
                    if (feetY() == y - phase + 1 && (bb.minX < x + 0.02 || bb.maxX > x + 0.98 || bb.minZ < z + 0.02 || bb.maxZ > z + 0.98)
                        && ++walks < 12)
                        return walkToCell(x, feetY(), z, 0.12);
                    if (++waited > 40)
                    {
                        if (segNum() >= 16) return wallIn();
                        noShelterUntil = Oracle.tick + 200;
                        return leave("did not fall");
                    }
                    return idle();
                }
                waited = 0;
                if (phase == 3)
                {
                    phase = 4;
                    return new PlaceAt(COBBLE, COBBLE, new int[] {x, y - 1, z}, null);
                }
                ++phase;
                return new Mine(x, y - phase, z, true);
            }
            if (phase == 4)
            {
                phase = 5;
                if (child == FAIL) System.out.println("GOLDBOT t=" + Oracle.tick + " shelter cap failed");
                child = null;
                System.out.println("GOLDBOT t=" + Oracle.tick + " sheltered at " + where() + " hp=" + sp.getHealth() + " food=" + sp.getFoodStats().getFoodLevel());
            }
            // eat to a full bar (the regeneration needs 18), wait for the health
            int food = sp.getFoodStats().getFoodLevel();
            int slot = hotbarOf(COOKED_MEAT);
            if (slot < 0) slot = hotbarOf(PORK, BEEF, CHICKEN);
            if (slot < 0 && food < 18 && count(COOKED_MEAT) + count(PORK, BEEF) > 0)
            {
                for (int k : new int[] {CPORK, CBEEF, CCHICKEN, PORK, BEEF})
                    if (count(k) > 0) return new ToHotbar(k);
            }
            // nothing else: rotten flesh, whose hunger effect costs less than it gives
            if (slot < 0 && segNum() >= 16 && food <= 14 && count(ROTTEN) > 0)
            {
                slot = hotbarOf(ROTTEN);
                if (slot < 0) return new ToHotbar(ROTTEN);
            }
            if (slot >= 0 && food < 20) return new A().look(new float[] {cp.rotationYaw, 90.0F}).hb(slot).hold("use").j();
            if (cp.isUsingItem()) return idle();
            ++waited;
            // healed, and the mobs that waited above have gone (or given up on)
            boolean healed = sp.getHealth() >= 20.0F || (food < 18 && waited > 200);
            if (healed && food < 18 && segNum() >= 16) noShelterUntil = Oracle.tick + 1200;
            if ((healed && (threat(10.0) == null || waited > 2400)) || waited > 6000)
            {
                // dig back up to the sky, fighting what is still there
                System.out.println("GOLDBOT t=" + Oracle.tick + " shelter healed hp=" + sp.getHealth() + " food=" + food);
                phase = 6;
                sheltering = false;
                return new DigTo("the surface", (a, b, c) -> b >= ws.getHeightValue(a, c), sp.posX, 70, sp.posZ);
            }
            return idle();
        }
    }

    // ------------------------------------------------------------------ digging

    static boolean falling(Block b)
    {
        return b == Blocks.sand || b == Blocks.gravel;
    }

    /** A block the dig planner may break on the way: solid, not a tile or
     * a liquid's neighbor, not too hard. */
    static boolean diggable(int x, int y, int z)
    {
        Block b = blk(x, y, z);
        if (b == null || y < 6) return false;
        if (b.getMaterial().isLiquid() || b.hasTileEntity() || b == Blocks.bedrock || b == Blocks.obsidian) return false;
        // the native pick has no stair shape; and the portal is not a block to dig
        if (b instanceof net.minecraft.block.BlockStairs || b == Blocks.portal) return false;
        float h = b.getBlockHardness(ws, x, y, z);
        if (h < 0 || h > 3.5F) return false;
        if (!b.getMaterial().isToolNotRequired())
        {
            boolean ok = false;
            for (int i = 0; i < 9 && !ok; ++i)
            {
                ItemStack s = cp.inventory.mainInventory[i];
                if (s != null && s.func_150998_b(b)) ok = true;
            }
            if (!ok) return false;
        }
        for (int[] f : FACES)
        {
            Block n = blk(x + f[0], y + f[1], z + f[2]);
            if (n == null || n.getMaterial().isLiquid()) return false;
        }
        return true;
    }

    /** No lava within one block and not in a liquid. */
    static boolean safeCell(int x, int y, int z)
    {
        long k = key(x, y, z);
        int sh = lavaCardinalOnly ? 6 : 2;
        int m = memoGet(k) >> sh;
        if ((m & 1) != 0) return (m & 2) != 0;
        boolean r = safeCellNow(x, y, z);
        memoPut(k, (r ? 3 : 1) << sh);
        return r;
    }

    static boolean safeCellNow(int x, int y, int z)
    {
        if (lavaCardinalOnly) return !lavaNear(x, y, z) && !liquid(blk(x, y, z)) && !liquid(blk(x, y + 1, z));
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                for (int dy = -1; dy <= 2; ++dy)
                {
                    Block b = blk(x + dx, y + dy, z + dz);
                    if (b == null || b.getMaterial() == Material.lava || b == Blocks.fire) return false;
                }
        return !liquid(blk(x, y, z)) && !liquid(blk(x, y + 1, z));
    }

    static final class DNode
    {
        final int x, y, z;
        double cost;
        DNode prev;
        int[] dig; // blocks cleared on the way in from prev, top first
        boolean closed;
        DNode(int x, int y, int z) { this.x = x; this.y = y; this.z = z; }
    }

    /** The blocks a cardinal move from (x,y,z) by (dx,dy,dz) must clear, top
     * first; null when the move cannot be dug. */
    static int[] digMove(int x, int y, int z, int dx, int dy, int dz)
    {
        int mx = x + dx, my = y + dy, mz = z + dz;
        int[][] cells;
        if (dy == 0) cells = new int[][] {{mx, y + 1, mz}, {mx, y, mz}};
        else if (dy > 0) cells = new int[][] {{x, y + 2, z}, {mx, y + 2, mz}, {mx, y + 1, mz}};
        else cells = new int[][] {{mx, y + 1, mz}, {mx, y, mz}, {mx, y - 1, mz}};
        if (!floor(mx, my - 1, mz) || liquid(blk(mx, my - 1, mz)) || !safeCell(mx, my, mz)) return null;
        int n = 0;
        int[] out = new int[cells.length * 3];
        for (int[] c : cells)
        {
            if (pass(c[0], c[1], c[2]) && !liquid(blk(c[0], c[1], c[2]))) continue;
            if (!diggable(c[0], c[1], c[2])) return null;
            // what is above a cleared block must not fall into the tunnel
            Block up = blk(c[0], c[1] + 1, c[2]);
            boolean upCleared = false;
            for (int[] o : cells) if (o[0] == c[0] && o[1] == c[1] + 1 && o[2] == c[2]) upCleared = true;
            if (!upCleared && falling(up)) return null;
            out[n++] = c[0];
            out[n++] = c[1];
            out[n++] = c[2];
        }
        return java.util.Arrays.copyOf(out, n);
    }

    static double digCost(int[] dig)
    {
        double c = 0;
        for (int i = 0; i < dig.length; i += 3)
        {
            Block b = blk(dig[i], dig[i + 1], dig[i + 2]);
            c += 1.5 + b.getBlockHardness(ws, dig[i], dig[i + 1], dig[i + 2]) * 1.5;
        }
        return c;
    }

    /** The dig planner's heuristic weight (3: greedy toward the hint). */
    static double digWeight = 3.0;

    /** Weighted A* over cells, walking where the world allows and digging
     * staircase moves where it does not, to a cell the goal accepts. */
    static List<DNode> digPlan(Goal g, int maxNodes, final double hx, final double hy, final double hz, Set<Long> avoid)
    {
        int[] c = cell();
        Seen<DNode> seen = new Seen<DNode>();
        PriorityQueue<Object[]> pq = new PriorityQueue<Object[]>(64, (a, b) -> Double.compare((Double)a[0], (Double)b[0]));
        DNode start = new DNode(c[0], c[1], c[2]);
        seen.put(key(c[0], c[1], c[2]), start);
        pq.add(new Object[] {0.0, start});
        while (!pq.isEmpty())
        {
            Object[] e = pq.poll();
            DNode n = (DNode)e[1];
            if (n.closed) continue;
            n.closed = true;
            if (g.ok(n.x, n.y, n.z))
            {
                ArrayList<DNode> path = new ArrayList<DNode>();
                for (DNode p = n; p != null; p = p.prev) path.add(0, p);
                return path;
            }
            lastDigNodes = seen.size();
            if (seen.size() > maxNodes) break;
            for (int[] d : DIRS)
            {
                int dx = d[0], dz = d[1];
                int x = n.x + dx, z = n.z + dz;
                if (dx != 0 && dz != 0)
                {
                    if (!(pass(n.x + dx, n.y, n.z) && pass(n.x + dx, n.y + 1, n.z) && pass(n.x, n.y, n.z + dz) && pass(n.x, n.y + 1, n.z + dz))) continue;
                    if (stand(x, n.y, z)) drelax(seen, pq, n, x, n.y, z, 1.414, new int[0], hx, hy, hz, avoid);
                    continue;
                }
                for (int dy = -1; dy <= 1; ++dy)
                {
                    int[] dig = digMove(n.x, n.y, n.z, dx, dy, dz);
                    if (dig == null) continue;
                    drelax(seen, pq, n, x, n.y + dy, z, (dy > 0 ? 2.0 : 1.0) + digCost(dig), dig, hx, hy, hz, avoid);
                }
                // seed 1 from S5: the cell below the step is fallen through too
                // (a drop through the dirt under the feet trapped the bot in a cave)
                if (pass(x, n.y, z) && pass(x, n.y + 1, z) && !stand(x, n.y - 1, z) && (s1k < 5 || pass(x, n.y - 1, z)))
                    for (int drop = 2; drop <= 3; ++drop)
                    {
                        if (stand(x, n.y - drop, z))
                        {
                            drelax(seen, pq, n, x, n.y - drop, z, 1.0 + 0.5 * drop, new int[0], hx, hy, hz, avoid);
                            break;
                        }
                        if (!pass(x, n.y - drop, z)) break;
                    }
            }
        }
        return null;
    }

    /** No swimming on a dig route (the bot cannot climb out of every pool),
     * nor beside a flow that would push the player off it. */
    static boolean wetRoute(int x, int y, int z, long k)
    {
        int m = memoGet(k);
        if ((m & 0x100) != 0) return (m & 0x200) != 0;
        boolean r = water(x, y, z) || water(x, y + 1, z);
        for (int[] d : DIRS)
            if (!r && (d[0] == 0 || d[1] == 0) && (water(x + d[0], y, z + d[1]) || water(x + d[0], y + 1, z + d[1]))) r = true;
        memoPut(k, r ? 0x300 : 0x100);
        return r;
    }

    static void drelax(Seen<DNode> seen, PriorityQueue<Object[]> pq, DNode from, int x, int y, int z, double step, int[] dig,
        double hx, double hy, double hz, Set<Long> avoid)
    {
        long k = key(x, y, z);
        if (avoid != null && avoid.contains(k)) return;
        if (wetRoute(x, y, z, k)) return;
        double c = from.cost + step;
        DNode n = seen.get(k);
        if (n == null)
        {
            n = new DNode(x, y, z);
            seen.put(k, n);
        }
        else if (n.closed || c >= n.cost) return;
        n.cost = c;
        n.prev = from;
        n.dig = dig;
        double h = Math.abs(x + 0.5 - hx) + Math.abs(z + 0.5 - hz) + Math.abs(y - hy);
        pq.add(new Object[] {c + digWeight * h, n});
    }

    /** Nodes the last dig plan saw (a failed plan's log line). */
    static int lastDigNodes;

    /** Dig (and walk) along a planned route to a cell the goal accepts. */
    static final class DigTo extends Task
    {
        final Goal goal;
        final String what;
        final double hx, hy, hz;
        final HashSet<Long> avoid = new HashSet<Long>();
        List<DNode> path;
        DNode digFor;
        int i, replans, mined, bestI, totalReplans;
        int maxNodes = 150000;
        /** When set, the planner's weight on the distance left for this dig's plans. */
        double heur = Double.NaN;
        Follow f;
        /** When set and true between steps, the dig gives up (FAIL). */
        java.util.function.BooleanSupplier stop;

        DigTo(String what, Goal g, double hx, double hy, double hz)
        {
            this.what = what;
            goal = g;
            this.hx = hx;
            this.hy = hy;
            this.hz = hz;
        }

        String label() { return "DigTo(" + what + (path == null ? "" : " " + i + "/" + (path.size() - 1)) + ")"; }

        Object replan()
        {
            f = null;
            path = null;
            bestI = 0;
            if (++replans > 12 || ++totalReplans > 60) return FAIL;
            return idle();
        }

        Object tick()
        {
            if (stop != null && f == null && stop.getAsBoolean()) return FAIL;
            if (child == FAIL)
            {
                child = null;
                if (digFor != null) avoid.add(key(digFor.x, digFor.y, digFor.z));
                return replan();
            }
            child = null;
            digFor = null;
            if (path == null)
            {
                double w0 = digWeight;
                if (!Double.isNaN(heur)) digWeight = heur;
                try
                {
                    path = digPlan(goal, maxNodes, hx, hy, hz, avoid);
                }
                finally
                {
                    digWeight = w0;
                }
                if (path == null)
                {
                    System.out.println("GOLDBOT t=" + Oracle.tick + " no dig route to " + what + " from " + where() + " (" + lastDigNodes + " nodes)");
                    return FAIL;
                }
                i = 0;
                StringBuilder b = new StringBuilder();
                for (DNode n : path) b.append(n.x).append(',').append(n.y).append(',').append(n.z).append(n.dig != null && n.dig.length > 0 ? "d" + n.dig.length / 3 : "").append(' ');
                System.out.println("GOLDBOT t=" + Oracle.tick + " dig " + what + " from " + where() + ": " + b.toString().trim());
            }
            int[] c = cell();
            if (i == path.size() - 1)
            {
                DNode n = path.get(i);
                if (c[0] == n.x && c[1] == n.y && c[2] == n.z && hdist(new int[] {n.x, n.y, n.z}) < 0.35
                    && Math.sqrt(cp.motionX * cp.motionX + cp.motionZ * cp.motionZ) < 0.03) return DONE;
            }
            if (f == null)
            {
                DNode n = path.get(i);
                if (!(c[0] == n.x && c[1] == n.y && c[2] == n.z))
                {
                    // off the route: step back to the node when close, else replan
                    if (Math.abs(c[0] - n.x) + Math.abs(c[2] - n.z) <= 1 && Math.abs(c[1] - n.y) <= 1 && i > 0)
                    {
                        List<int[]> back = new ArrayList<int[]>();
                        back.add(c);
                        back.add(new int[] {n.x, n.y, n.z});
                        f = new Follow(back, 0.3, false);
                    }
                    else return replan();
                }
                else if (i + 1 < path.size())
                {
                    DNode m = path.get(i + 1);
                    int[] dig = m.dig == null ? new int[0] : m.dig;
                    for (int k = 0; k < dig.length; k += 3)
                    {
                        Block b = blk(dig[k], dig[k + 1], dig[k + 2]);
                        if (b == null) return replan();
                        if (pass(dig[k], dig[k + 1], dig[k + 2]) && !liquid(b)) continue;
                        if (!diggable(dig[k], dig[k + 1], dig[k + 2]))
                        {
                            avoid.add(key(m.x, m.y, m.z));
                            return replan();
                        }
                        ++mined;
                        digFor = m;
                        return new Mine(dig[k], dig[k + 1], dig[k + 2], true);
                    }
                    List<int[]> step = new ArrayList<int[]>();
                    step.add(new int[] {n.x, n.y, n.z});
                    step.add(new int[] {m.x, m.y, m.z});
                    f = new Follow(step, 0.3, false);
                    ++i;
                    if (i > bestI)
                    {
                        bestI = i;
                        replans = 0;
                    }
                }
                else
                {
                    List<int[]> step = new ArrayList<int[]>();
                    step.add(new int[] {n.x, n.y, n.z});
                    f = new Follow(step, 0.3, false);
                }
            }
            Object r = f.step();
            if (r == null)
            {
                f = null;
                return idle();
            }
            if (r == FAIL) return replan();
            return r;
        }
    }

    /** Mine ore blocks until count(drops) reaches want: a visible reachable
     * one by walking, else dig a staircase to the nearest. */
    static final class OreLoop extends Task
    {
        final int[] blocks, drops;
        final int want, ymin, ymax;
        final HashSet<Long> bad = new HashSet<Long>();
        int phase, digs;
        int[] target;
        boolean failIfNone;

        OreLoop(int[] blocks, int[] drops, int want, int ymin, int ymax)
        {
            this.blocks = blocks;
            this.drops = drops;
            this.want = want;
            this.ymin = ymin;
            this.ymax = ymax;
        }

        String label() { return "OreLoop(" + drops[0] + " " + count(drops) + "/" + want + ")"; }

        boolean ore(int x, int y, int z)
        {
            int b = id(x, y, z);
            for (int k : blocks) if (b == k) return !bad.contains(key(x, y, z));
            return false;
        }

        /** Safe to break: no liquid beside it, nothing falling on top. */
        boolean breakable(int x, int y, int z)
        {
            for (int[] f : FACES)
            {
                Block n = blk(x + f[0], y + f[1], z + f[2]);
                if (n == null || n.getMaterial().isLiquid()) return false;
            }
            return !falling(blk(x, y + 1, z));
        }

        Object tick()
        {
            if (phase == 1)
            {
                phase = 0;
                if (child == FAIL && target != null) bad.add(key(target[0], target[1], target[2]));
                return new Collect(drops, 6.0);
            }
            if (phase == 2)
            {
                phase = 0;
                if (child == FAIL)
                {
                    for (int[] t : lastCands) bad.add(key(t[0], t[1], t[2]));
                    if (++digs > 20) return FAIL;
                    return idle();
                }
                // at the ore: break the one in front
                target = adjacentOre(cell());
                if (target != null)
                {
                    phase = 1;
                    return new Mine(target[0], target[1], target[2], true);
                }
                return idle();
            }
            if (count(drops) >= want) return DONE;
            // one next to the cell we stand in
            target = adjacentOre(cell());
            if (target != null)
            {
                phase = 1;
                return new Mine(target[0], target[1], target[2], true);
            }
            MineFor mf = new MineFor(blocks, drops, want);
            mf.bad.addAll(bad);
            int[] t = mf.pick(-1);
            if (t != null && Math.abs(t[0] - cp.posX) + Math.abs(t[2] - cp.posZ) < 12)
            {
                target = t;
                phase = 1;
                return new Mine(t[0], t[1], t[2], false);
            }
            List<int[]> all = scan(blocks, ymin, ymax);
            lastCands.clear();
            for (int[] a : all)
            {
                if (bad.contains(key(a[0], a[1], a[2])) || !breakable(a[0], a[1], a[2])) continue;
                lastCands.add(a);
                if (lastCands.size() >= 24) break;
            }
            if (lastCands.isEmpty())
            {
                if (failIfNone) return FAIL;
                throw new Stuck("no ore " + blocks[0] + " left in the loaded chunks");
            }
            final HashSet<Long> cands = new HashSet<Long>();
            for (int[] a : lastCands) cands.add(key(a[0], a[1], a[2]));
            int[] n = lastCands.get(0);
            phase = 2;
            return new DigTo("ore " + blocks[0] + " near " + n[0] + "," + n[1] + "," + n[2],
                (a, b, c) -> adjacent(cands, a, b, c) != null, n[0] + 0.5, n[1], n[2] + 0.5);
        }

        final List<int[]> lastCands = new ArrayList<int[]>();

        int[] adjacentOre(int[] c)
        {
            int[][] offs = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {1, 1, 0}, {-1, 1, 0}, {0, 1, 1}, {0, 1, -1}, {0, 2, 0}};
            for (int[] o : offs)
            {
                int x = c[0] + o[0], y = c[1] + o[1], z = c[2] + o[2];
                if (ore(x, y, z) && breakable(x, y, z) && aimAt(x, y, z) != null) return new int[] {x, y, z};
            }
            return null;
        }

        static int[] adjacent(Set<Long> cands, int a, int b, int c)
        {
            int[][] offs = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {1, 1, 0}, {-1, 1, 0}, {0, 1, 1}, {0, 1, -1}, {0, 2, 0}};
            for (int[] o : offs)
                if (cands.contains(key(a + o[0], b + o[1], c + o[2]))) return new int[] {a + o[0], b + o[1], c + o[2]};
            return null;
        }
    }

    /** Flint: mine gravel, and while the drop is gravel, place it and mine
     * it again. */
    static final class GetFlint extends Task
    {
        final int want, maxLoops;
        int phase, loops;
        int[] placed;
        PlaceAny placer;

        GetFlint() { this(1, 40); }

        /** G28 on: flint for arrows, want of it in at most maxLoops gravel. */
        GetFlint(int want, int maxLoops)
        {
            this.want = want;
            this.maxLoops = maxLoops;
        }

        String label() { return "GetFlint(" + loops + (want > 1 ? " " + count(FLINT) + "/" + want : "") + ")"; }

        Object tick()
        {
            switch (phase)
            {
                case 1: // placed
                    phase = 2;
                    if (child == FAIL) throw new Stuck("gravel not placed");
                    placed = placer.at;
                    if (placed == null || id(placed[0], placed[1], placed[2]) != GRAVEL) throw new Stuck("placed gravel gone");
                    return new Mine(placed[0], placed[1], placed[2], true);
                case 2:
                    phase = 0;
                    // G28 on: a drop that settled out of reach is walked to
                    if (want > 1) return new GoldKit.Pickup(new int[] {GRAVEL, FLINT});
                    return new Collect(new int[] {GRAVEL, FLINT}, 5.0);
                default:
                    break;
            }
            if (count(FLINT) >= want) return DONE;
            if (++loops > maxLoops) throw new Stuck("no flint after " + maxLoops + " gravel");
            if (count(GRAVEL) == 0)
            {
                phase = 0;
                return new OreLoop(new int[] {GRAVEL}, want > 1 ? new int[] {GRAVEL} : new int[] {GRAVEL, FLINT}, 1, 40, 90);
            }
            if (hotbarOf(GRAVEL) < 0) return new ToHotbar(GRAVEL);
            phase = 1;
            placer = new PlaceAny(GRAVEL, GRAVEL);
            return placer;
        }
    }

    /** Place a block item into an air cell with a floor near the player,
     * clicking whichever solid face of a neighbor the crosshair reaches. */
    static final class PlaceAny extends Task
    {
        final int item, block;
        int phase, tries;
        int[] at;
        float[] yp;
        int side;
        int[] on;

        PlaceAny(int item, int block) { this.item = item; this.block = block; }

        String label() { return "PlaceAny(" + item + ")"; }

        Object tick()
        {
            if (at != null && id(at[0], at[1], at[2]) == block) return DONE;
            int hb = hotbarOf(item);
            if (hb < 0) throw new Stuck("item " + item + " not on the hotbar");
            if (phase == 0)
            {
                if (++tries > 4) return FAIL;
                if (!choose()) return FAIL;
                phase = 1;
                return new A().look(yp).hb(hb).j();
            }
            if (phase == 1)
            {
                phase = 2;
                MovingObjectPosition h = ray(yp);
                if (!hits(h, on[0], on[1], on[2]) || h.sideHit != side || pointed(yp) != null)
                {
                    phase = 0;
                    return idle();
                }
                return new A().look(yp).hb(hb).press("use").j();
            }
            if (phase++ < 5) return idle();
            phase = 0;
            return idle();
        }

        boolean choose()
        {
            int[] c = cell();
            // side numbers of the clicked face, by the offset from the clicked block to the cell
            for (int r = 1; r <= 2; ++r)
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -r; dx <= r; ++dx)
                        for (int dz = -r; dz <= r; ++dz)
                        {
                            if (Math.max(Math.abs(dx), Math.abs(dz)) != r) continue;
                            int x = c[0] + dx, y = c[1] + dy, z = c[2] + dz;
                            if (id(x, y, z) != 0 || !floor(x, y - 1, z)) continue;
                            AxisAlignedBB box = AxisAlignedBB.getBoundingBox(x - 0.1, y, z - 0.1, x + 1.1, y + 1, z + 1.1);
                            if (cp.boundingBox.intersectsWith(box)) continue;
                            int[][] faces = {{0, -1, 0, 1}, {1, 0, 0, 4}, {-1, 0, 0, 5}, {0, 0, 1, 2}, {0, 0, -1, 3}, {0, 1, 0, 0}};
                            for (int[] f : faces)
                            {
                                int ox = x + f[0], oy = y + f[1], oz = z + f[2];
                                if (!floor(ox, oy, oz) || id(ox, oy, oz) == TABLE || id(ox, oy, oz) == FURNACE || id(ox, oy, oz) == LIT_FURNACE) continue;
                                double px = x + 0.5 + f[0] * 0.5, py = y + 0.5 + f[1] * 0.5, pz = z + 0.5 + f[2] * 0.5;
                                float[] look = lookAt(px, py, pz);
                                MovingObjectPosition h = ray(look);
                                if (!hits(h, ox, oy, oz) || h.sideHit != f[3] || pointed(look) != null) continue;
                                at = new int[] {x, y, z};
                                on = new int[] {ox, oy, oz};
                                side = f[3];
                                yp = look;
                                return true;
                            }
                        }
            return false;
        }
    }

    static final Recipe IPICK_R = new Recipe("iron pickaxe", IPICK, 1, false, new String[] {"III", " S ", " S "}, 'I', IRON, 'S', STICK);
    static final Recipe BUCKET_R = new Recipe("bucket", BUCKET, 1, false, new String[] {"I I", " I "}, 'I', IRON);
    static final Recipe FLINT_STEEL_R = new Recipe("flint and steel", FLINT_STEEL, 1, false, new String[] {"IF"}, 'I', IRON, 'F', FLINT);


    /** Item.getMovingObjectPositionFromPlayer for this look: the ray a
     * bucket casts (5 blocks; liquid sources stop it when empty). */
    static MovingObjectPosition bucketRay(float[] yp, boolean liquids)
    {
        Vec3 eye = Vec3.createVectorHelper(cp.posX, cp.posY, cp.posZ);
        float c = MathHelper.cos(-yp[0] * 0.017453292F - (float)Math.PI);
        float sn = MathHelper.sin(-yp[0] * 0.017453292F - (float)Math.PI);
        float cp2 = -MathHelper.cos(-yp[1] * 0.017453292F);
        float sp2 = MathHelper.sin(-yp[1] * 0.017453292F);
        Vec3 end = eye.addVector((double)(sn * cp2) * 5.0D, (double)sp2 * 5.0D, (double)(c * cp2) * 5.0D);
        return mc.theWorld.func_147447_a(eye, end, liquids, !liquids, false);
    }

    static boolean waterSource(int x, int y, int z)
    {
        Block b = blk(x, y, z);
        return b != null && b.getMaterial() == Material.water && meta(x, y, z) == 0;
    }

    /** A look whose empty-bucket ray lands on the water source (x,y,z). */
    static float[] aimWater(int x, int y, int z)
    {
        double[][] pts = {{0.5, 0.9, 0.5}, {0.5, 0.5, 0.5}, {0.3, 0.9, 0.3}, {0.7, 0.9, 0.7}, {0.3, 0.9, 0.7}, {0.7, 0.9, 0.3}};
        for (double[] p : pts)
        {
            float[] yp = lookAt(x + p[0], y + p[1], z + p[2]);
            MovingObjectPosition h = bucketRay(yp, true);
            if (hits(h, x, y, z) && waterSource(x, y, z) && pointed(yp) == null) return yp;
        }
        return null;
    }

    /** Fill the empty bucket at the nearest water source with air above,
     * standing on its shore. */
    static final class FillWater extends Task
    {
        int phase, wait, tries;
        int[] w, dugTo;
        float[] yp;
        final HashSet<Long> bad = new HashSet<Long>();

        Object tick()
        {
            if (count(WATER_BUCKET) > 0) return DONE;
            int hb = hotbarOf(BUCKET);
            if (hb < 0) return new ToHotbar(BUCKET);
            if (phase == 1 && dugTo != null)
            {
                if (child != FAIL) w = dugTo.clone();
                dugTo = null;
                if (w == null) phase = 0;
            }
            if (phase == 1 && child == FAIL) { if (w != null) bad.add(key(w[0], w[1], w[2])); phase = 0; }
            if (phase == 3)
            {
                if (++wait > 6) phase = 0;
                return idle();
            }
            if (phase == 0 || phase == 1)
            {
                if (phase == 1)
                {
                    phase = 2;
                    yp = null;
                }
                else
                {
                    if (++tries > 6) return FAIL;
                    final HashSet<Long> ws0 = new HashSet<Long>();
                    List<int[]> all = scan(new int[] {8, 9}, 40, 80);
                    for (int[] a : all)
                        if (waterSource(a[0], a[1], a[2]) && id(a[0], a[1] + 1, a[2]) == 0 && !bad.contains(key(a[0], a[1], a[2])))
                        {
                            ws0.add(key(a[0], a[1], a[2]));
                            if (ws0.size() > 60) break;
                        }
                    if (ws0.isEmpty()) throw new Stuck("no open water source");
                    final int[] found = new int[3];
                    Goal g = (a, b, c) ->
                    {
                        for (int[] d : DIRS)
                        {
                            if (d[0] != 0 && d[1] != 0) continue;
                            if (ws0.contains(key(a + d[0], b - 1, c + d[1])))
                            {
                                found[0] = a + d[0];
                                found[1] = b - 1;
                                found[2] = c + d[1];
                                return true;
                            }
                        }
                        return false;
                    };
                    List<int[]> p = plan(g, 60000, null);
                    if (p == null && s1)
                    {
                        // seed 1: no walk reaches a shore, so dig to the nearest
                        int[] n0 = null;
                        for (int[] a : all) if (ws0.contains(key(a[0], a[1], a[2]))) { n0 = a; break; }
                        dugTo = found;
                        phase = 1;
                        return new DigTo("water " + n0[0] + "," + n0[1] + "," + n0[2], g, n0[0] + 0.5, n0[1] + 1, n0[2] + 0.5);
                    }
                    if (p == null) throw new Stuck("no shore to fill the bucket from");
                    w = found.clone();
                    phase = 1;
                    return new Walk("water " + w[0] + "," + w[1] + "," + w[2], g, 0.3);
                }
            }
            // phase 2: aim, then use
            if (yp == null)
            {
                yp = aimWater(w[0], w[1], w[2]);
                if (yp == null)
                {
                    bad.add(key(w[0], w[1], w[2]));
                    phase = 0;
                    return idle();
                }
                return new A().look(yp).hb(hb).j();
            }
            phase = 3;
            wait = 0;
            return new A().look(yp).hb(hb).press("use").j();
        }
    }

    /** Pour the water bucket on a lava lake's shore so the water runs over
     * the lake and its surface sources harden to obsidian, then scoop the
     * water back up. */
    static final class CastObsidian extends Task
    {
        final int lx, ly, lz; // a lava source of the lake
        int phase, wait;
        int[] shore, water;
        float[] yp;
        List<int[]> shores;

        CastObsidian(int lx, int ly, int lz) { this.lx = lx; this.ly = ly; this.lz = lz; }

        String label() { return "CastObsidian(" + phase + ")"; }

        static boolean lavaSource(int x, int y, int z)
        {
            Block b = blk(x, y, z);
            return b != null && b.getMaterial() == Material.lava && meta(x, y, z) == 0;
        }

        Object tick()
        {
            switch (phase)
            {
                case 0:
                {
                    if (count(WATER_BUCKET) == 0) throw new Stuck("no water to cast with");
                    // shore blocks: solid, air above, beside an open lava source of this lake's level
                    shores = new ArrayList<int[]>();
                    final HashSet<Long> sk = new HashSet<Long>();
                    for (int x = lx - 16; x <= lx + 16; ++x)
                        for (int z = lz - 16; z <= lz + 16; ++z)
                        {
                            if (!floor(x, ly, z) || id(x, ly + 1, z) != 0) continue;
                            boolean near = false;
                            for (int[] d : DIRS)
                                if ((d[0] == 0 || d[1] == 0) && lavaSource(x + d[0], ly, z + d[1]) && id(x + d[0], ly + 1, z + d[1]) == 0) near = true;
                            if (!near) continue;
                            shores.add(new int[] {x, ly, z});
                            sk.add(key(x, ly, z));
                        }
                    if (shores.isEmpty()) throw new Stuck("no shore at the lava lake " + lx + "," + ly + "," + lz);
                    final List<int[]> sh = shores;
                    phase = 1;
                    DigTo d = new DigTo("lava shore near " + lx + "," + ly + "," + lz, (a, b, c) -> pourFrom(sh, a, b, c) != null, lx + 0.5, ly + 1, lz + 0.5);
                    // seed 1: the lake is 50 blocks off and 32 up from the diamonds
                    if (s1) d.maxNodes = 600000;
                    return d;
                }
                case 1:
                {
                    // seed 1: SegG5 tries another lake
                    if (child == FAIL && s1) return FAIL;
                    if (child == FAIL) throw new Stuck("lava shore not reached");
                    int[] c = cell();
                    shore = pourFrom(shores, c[0], c[1], c[2]);
                    if (shore == null) throw new Stuck("no shore in sight after the walk");
                    int hb = hotbarOf(WATER_BUCKET);
                    if (hb < 0) return new ToHotbar(WATER_BUCKET);
                    yp = aimShore(shore);
                    if (yp == null) throw new Stuck("cannot aim at the shore " + shore[0] + "," + shore[1] + "," + shore[2]);
                    phase = 2;
                    return new A().look(yp).hb(hb).j();
                }
                case 2:
                    phase = 3;
                    wait = 0;
                    water = new int[] {shore[0], shore[1] + 1, shore[2]};
                    return new A().look(yp).hb(hotbarOf(WATER_BUCKET)).press("use").j();
                case 3:
                    // let the water run over the lake
                    if (++wait < 60) return idle();
                    if (!waterSource(water[0], water[1], water[2])) throw new Stuck("the poured water is not a source");
                    phase = 4;
                    wait = 0;
                    return idle();
                case 4:
                {
                    int hb = hotbarOf(BUCKET);
                    if (hb < 0) throw new Stuck("no empty bucket to scoop the water");
                    float[] a = aimWater(water[0], water[1], water[2]);
                    if (a == null)
                    {
                        if (++wait > 3) throw new Stuck("cannot aim at the poured water");
                        return new Walk("see water", (x, y, z) -> Math.abs(x - water[0]) + Math.abs(z - water[2]) == 2 && y == water[1] || y == water[1] + 1 && Math.abs(x - water[0]) + Math.abs(z - water[2]) == 2, 0.3);
                    }
                    yp = a;
                    phase = 5;
                    return new A().look(yp).hb(hb).j();
                }
                case 5:
                    phase = 6;
                    wait = 0;
                    return new A().look(yp).hb(hotbarOf(BUCKET)).press("use").j();
                case 6:
                    // the flowing water drains
                    if (++wait < 50) return idle();
                    if (count(WATER_BUCKET) == 0) throw new Stuck("the water was not scooped");
                    return DONE;
                default:
                    return DONE;
            }
        }

        /** A shore block whose top the bucket can reach from standing cell
         * (a,b,c): one or two blocks above it, two or three away. */
        static int[] pourFrom(List<int[]> shores, int a, int b, int c)
        {
            double ex = a + 0.5, ey = b + 1.62, ez = c + 0.5;
            for (int[] s : shores)
            {
                int dy = b - s[1];
                if (dy < 1 || dy > 2) continue;
                int dh = Math.abs(a - s[0]) + Math.abs(c - s[2]);
                if (dh < 2 || dh > 3) continue;
                if (los(ex, ey, ez, s[0] + 0.5, s[1] + 0.99, s[2] + 0.5, s[0], s[1], s[2])) return s;
            }
            return null;
        }

        static float[] aimShore(int[] s)
        {
            double[][] pts = {{0.5, 0.5}, {0.3, 0.3}, {0.7, 0.7}, {0.3, 0.7}, {0.7, 0.3}};
            for (double[] p : pts)
            {
                float[] yp = lookAt(s[0] + p[0], s[1] + 1.0, s[2] + p[1]);
                MovingObjectPosition h = bucketRay(yp, false);
                if (hits(h, s[0], s[1], s[2]) && h.sideHit == 1 && pointed(yp) == null) return yp;
            }
            return null;
        }
    }

    static final Recipe DPICK_R = new Recipe("diamond pickaxe", DPICK, 1, false, new String[] {"DDD", " S ", " S "}, 'D', DIAMOND, 'S', STICK);

    /** G5: water in the bucket, three diamonds with the iron pickaxe, a
     * table on the spot and a diamond pickaxe, then obsidian cast on a lava
     * lake with the water and mined. */
    static final class SegG5 extends Seg
    {
        final int end;
        int casts, climbs;
        int[] lake;
        final List<int[]> badLakes = new ArrayList<int[]>();
        boolean lakeTrip;

        SegG5(int from, int end)
        {
            phase = from;
            this.end = end;
        }

        Object tick()
        {
            if (phase >= end) return DONE;
            switch (phase)
            {
                case 0:
                    phase = 1;
                    return new ToHotbar(IPICK);
                case 1:
                    need(hotbarOf(IPICK) >= 0, "iron pickaxe not on the hotbar");
                    phase = 2;
                    return new ToHotbar(BUCKET);
                case 2:
                    need(hotbarOf(BUCKET) >= 0, "bucket not on the hotbar");
                    phase = 3;
                    return new FillWater();
                case 3:
                    need(child != FAIL && count(WATER_BUCKET) == 1, "no water");
                    phase = 4;
                    return new OreLoop(new int[] {DIAMOND_ORE}, new int[] {DIAMOND}, 3, 6, 20);
                case 4:
                    need(child != FAIL && count(DIAMOND) >= 3, "no diamonds");
                    phase = 5;
                    return new Craft(false, TABLE_R, 1);
                case 5:
                    need(child != FAIL, "no table");
                    phase = 6;
                    return new ToHotbar(TABLE);
                case 6:
                    phase = 7;
                    return new PlaceAny(TABLE, TABLE);
                case 7:
                    need(child != FAIL, "table not placed");
                    phase = 8;
                    return new Craft(true, DPICK_R, 1);
                case 8:
                    need(child != FAIL && count(DPICK) == 1, "no diamond pickaxe");
                    phase = 9;
                    return new ToHotbar(DPICK);
                case 9:
                {
                    need(hotbarOf(DPICK) >= 0, "diamond pickaxe not on the hotbar");
                    // seed 1: out of the diamond level first (the dig planner
                    // finds no climb of 30 blocks to a lake's shore)
                    if (s1 && climbs < 2 && cell()[1] < 30)
                    {
                        ++climbs;
                        final int[] c0 = cell();
                        DigTo up = new DigTo("up from the diamonds", (a, b, c) -> b >= 40 && stand(a, b, c), c0[0] + 0.5, 44, c0[2] + 0.5);
                        up.maxNodes = 600000;
                        return up;
                    }
                    need(++casts <= 3, "no ten obsidian after three casts");
                    // the nearest open lava lake above the lava sea
                    List<int[]> lv = scan(new int[] {10, 11}, 30, 60);
                    int[] lake = null;
                    for (int[] a : lv)
                    {
                        if (!CastObsidian.lavaSource(a[0], a[1], a[2]) || id(a[0], a[1] + 1, a[2]) != 0 || !floor(a[0], a[1] - 1, a[2])) continue;
                        boolean bad = false;
                        for (int[] q : badLakes) if (Math.abs(q[0] - a[0]) + Math.abs(q[2] - a[2]) < 24 && Math.abs(q[1] - a[1]) < 4) bad = true;
                        if (bad) continue;
                        // a lake with at least 20 open sources one block deep within 6
                        int n = 0;
                        for (int dx = -6; dx <= 6; ++dx)
                            for (int dz = -6; dz <= 6; ++dz)
                                if (CastObsidian.lavaSource(a[0] + dx, a[1], a[2] + dz) && id(a[0] + dx, a[1] + 1, a[2] + dz) == 0
                                    && floor(a[0] + dx, a[1] - 1, a[2] + dz)) ++n;
                        // seed 1: no gravel or sand over the lake (a cave lake
                        // half buried by a gravel fall takes the poured water off)
                        if (n >= 20 && s1)
                            for (int dx = -6; dx <= 6 && n >= 20; ++dx)
                                for (int dz = -6; dz <= 6 && n >= 20; ++dz)
                                    for (int dy = 0; dy <= 5; ++dy)
                                        if (falling(blk(a[0] + dx, a[1] + dy, a[2] + dz))) { n = 0; break; }
                        if (n >= 20)
                        {
                            lake = a;
                            break;
                        }
                    }
                    // seed 1 from S6: none loaded, to the lake the first chain
                    // cast at (135, 51, 126), 130 blocks north of the diamonds
                    if (lake == null && s1k >= 6 && !lakeTrip)
                    {
                        lakeTrip = true;
                        --casts;
                        GoldSeed1.log("no open lava lake loaded: toward the one at 135,51,126 from " + where());
                        return new GoldSeed1.Journey(135.5, 52.0, 140.5, 10.0);
                    }
                    need(lake != null, "no open lava lake");
                    phase = 10;
                    this.lake = lake;
                    return new CastObsidian(lake[0], lake[1], lake[2]);
                }
                case 10:
                {
                    if (s1 && child == FAIL && lake != null)
                    {
                        // seed 1: that lake's shore is out of the dig planner's reach
                        badLakes.add(lake);
                        --casts;
                        need(badLakes.size() <= 4, "no reachable lava lake");
                        phase = 9;
                        return idle();
                    }
                    need(child != FAIL, "no obsidian cast");
                    phase = 11;
                    // the drops land in the holes the mining leaves in the lake's crust
                    lavaCardinalOnly = true;
                    OreLoop o = new OreLoop(new int[] {OBSIDIAN}, new int[] {OBSIDIAN}, 10, 30, 60);
                    o.failIfNone = true;
                    return o;
                }
                case 11:
                    lavaCardinalOnly = false;
                    if (count(OBSIDIAN) < 10)
                    {
                        // cast again where the lake is still lava
                        phase = 9;
                        return idle();
                    }
                    phase = 12;
                    return new Idle(3);
                default:
                    return DONE;
            }
        }
    }

    // ------------------------------------------------------------------ the portal

    /** A portal site: frame corner o, u along the frame, n from the frame
     * toward the standing rows. Cells are (i along u, j up, k along n):
     * the frame is k 0, i 0..3, j 0..4; the standing rows are k 1 and 2. */
    static final class Site
    {
        final int ox, oy, oz, ux, uz, nx, nz;

        Site(int ox, int oy, int oz, int ux, int uz, int nx, int nz)
        {
            this.ox = ox; this.oy = oy; this.oz = oz; this.ux = ux; this.uz = uz; this.nx = nx; this.nz = nz;
        }

        int[] at(int i, int j, int k) { return new int[] {ox + ux * i + nx * k, oy + j, oz + uz * i + nz * k}; }

        public String toString() { return ox + "," + oy + "," + oz + " u" + ux + "," + uz + " n" + nx + "," + nz; }
    }

    /** Blocks to dig for the site, or -1 when it cannot be made. */
    static int siteCost(Site st)
    {
        int cost = 0;
        for (int i = 0; i < 4; ++i)
            for (int k = 0; k <= 2; ++k)
            {
                int[] f = st.at(i, -1, k);
                if (!floor(f[0], f[1], f[2]) || liquid(blk(f[0], f[1], f[2]))) return -1;
                for (int j = 0; j <= 4; ++j)
                {
                    int[] c = st.at(i, j, k);
                    Block b = blk(c[0], c[1], c[2]);
                    if (b == null) return -1;
                    if (b.getMaterial() == Material.air) continue;
                    if (!diggable(c[0], c[1], c[2])) return -1;
                    ++cost;
                }
                int[] up = st.at(i, 5, k);
                if (falling(blk(up[0], up[1], up[2])) || liquid(blk(up[0], up[1], up[2]))) return -1;
                // a ceiling over the frame: the columns are built down from it
                if (k == 0 && !floor(up[0], up[1], up[2])) return -1;
            }
        // no liquid touching the room, no lava within three
        for (int i = -3; i <= 6; ++i)
            for (int k = -3; k <= 5; ++k)
                for (int j = -3; j <= 7; ++j)
                {
                    int[] c = st.at(i, j, k);
                    Block b = blk(c[0], c[1], c[2]);
                    if (b == null) return -1;
                    if (b.getMaterial() == Material.lava || b == Blocks.fire) return -1;
                    boolean touching = i >= -1 && i <= 4 && k >= -1 && k <= 3 && j >= -1 && j <= 5;
                    if (touching && b.getMaterial() == Material.water) return -1;
                }
        return cost;
    }

    static Site findSite()
    {
        int[] c = cell();
        Site best = null;
        double bestScore = 1e9;
        int[][] orient = {{1, 0, 0, 1}, {1, 0, 0, -1}, {0, 1, 1, 0}, {0, 1, -1, 0}};
        for (int dx = -14; dx <= 14; ++dx)
            for (int dz = -14; dz <= 14; ++dz)
                for (int dy = -6; dy <= 6; ++dy)
                    for (int[] o : orient)
                    {
                        Site st = new Site(c[0] + dx, c[1] + dy, c[2] + dz, o[0], o[1], o[2], o[3]);
                        int cost = siteCost(st);
                        if (cost < 0) continue;
                        double score = cost * 3.0 + Math.abs(dx) + Math.abs(dz) + Math.abs(dy) * 2;
                        if (score < bestScore)
                        {
                            bestScore = score;
                            best = st;
                        }
                    }
        if (best != null) System.out.println("GOLDBOT t=" + Oracle.tick + " portal site " + best + " dig " + siteCost(best));
        return best;
    }

    /** Clear the site's room, nearest exposed block first. */
    static final class Excavate extends Task
    {
        final Site st;
        final HashSet<Long> bad = new HashSet<Long>();
        int n;

        Excavate(Site st) { this.st = st; }

        String label() { return "Excavate(" + n + ")"; }

        Object tick()
        {
            if (child == FAIL && last != null) bad.add(key(last[0], last[1], last[2]));
            child = null;
            int[] bestC = null;
            double bd = 1e9;
            for (int i = 0; i < 4; ++i)
                for (int k = 0; k <= 2; ++k)
                    for (int j = 0; j <= 4; ++j)
                    {
                        int[] c = st.at(i, j, k);
                        Block b = blk(c[0], c[1], c[2]);
                        if (b == null || b.getMaterial() == Material.air || bad.contains(key(c[0], c[1], c[2]))) continue;
                        if (b.getMaterial().isLiquid()) continue;
                        double d = (c[0] + 0.5 - cp.posX) * (c[0] + 0.5 - cp.posX) + (c[1] + 0.5 - cp.posY) * (c[1] + 0.5 - cp.posY)
                            + (c[2] + 0.5 - cp.posZ) * (c[2] + 0.5 - cp.posZ) - j * 0.01;
                        boolean exposed = false;
                        for (int[] f : FACES) if (!raySolid(c[0] + f[0], c[1] + f[1], c[2] + f[2])) exposed = true;
                        if (!exposed) continue;
                        if (d < bd)
                        {
                            bd = d;
                            bestC = c;
                        }
                    }
            if (bestC == null)
            {
                for (int i = 0; i < 4; ++i)
                    for (int k = 0; k <= 2; ++k)
                        for (int j = 0; j <= 4; ++j)
                        {
                            int[] c = st.at(i, j, k);
                            Block b = blk(c[0], c[1], c[2]);
                            if (b != null && b.getMaterial() != Material.air && !b.getMaterial().isLiquid()) return FAIL;
                        }
                return DONE;
            }
            ++n;
            last = bestC;
            return new Mine(bestC[0], bestC[1], bestC[2], false);
        }

        int[] last;
    }

    /** Put a block item into cell t by clicking a face of one of its solid
     * neighbors, walking within the site's standing rows when no face is in
     * reach. */
    static final class PlaceAt extends Task
    {
        final int item, block;
        final int[] t;
        final Site st;
        int phase, tries, walks;
        float[] yp;
        int side;
        int[] on;

        PlaceAt(int item, int block, int[] t, Site st)
        {
            this.item = item;
            this.block = block;
            this.t = t;
            this.st = st;
        }

        String label() { return "PlaceAt(" + item + "@" + t[0] + "," + t[1] + "," + t[2] + ")"; }

        Object tick()
        {
            if (id(t[0], t[1], t[2]) == block) return DONE;
            if (hotbarOf(item) < 0) return new ToHotbar(item);
            int hb = hotbarOf(item);
            if (phase == 0)
            {
                if (++tries > 8) return FAIL;
                if (!choose(t, this))
                {
                    if (st == null || ++walks > 4) return FAIL;
                    // another cell of the standing rows
                    // the next cell of the standing rows, in turn
                    int[] here = cell();
                    int[] to = null;
                    for (int n = 0; n < 8 && to == null; ++n)
                    {
                        int m = (walks * 3 + n) % 8;
                        int[] s0 = st.at(m % 4, 0, 1 + m / 4);
                        if (!(s0[0] == here[0] && s0[1] == here[1] && s0[2] == here[2])) to = s0;
                    }
                    return walkToCell(to[0], to[1], to[2], 0.3);
                }
                phase = 1;
                return new A().look(yp).hb(hb).j();
            }
            if (phase == 1)
            {
                phase = 2;
                MovingObjectPosition h = ray(yp);
                if (!hits(h, on[0], on[1], on[2]) || h.sideHit != side || pointed(yp) != null)
                {
                    phase = 0;
                    return idle();
                }
                return new A().look(yp).hb(hb).press("use").j();
            }
            if (phase++ < 5) return idle();
            phase = 0;
            return idle();
        }

        /** A face of a solid neighbor of cell t the crosshair reaches now. */
        static boolean choose(int[] t, PlaceAt p)
        {
            AxisAlignedBB box = AxisAlignedBB.getBoundingBox(t[0], t[1], t[2], t[0] + 1, t[1] + 1, t[2] + 1);
            if (cp.boundingBox.intersectsWith(box)) return false;
            int[][] faces = {{0, -1, 0, 1}, {1, 0, 0, 4}, {-1, 0, 0, 5}, {0, 0, 1, 2}, {0, 0, -1, 3}, {0, 1, 0, 0}};
            double[][] offs = {{0, 0}, {0.3, 0.3}, {-0.3, 0.3}, {0.3, -0.3}, {-0.3, -0.3}, {0, 0.35}, {0, -0.35}, {0.35, 0}, {-0.35, 0}};
            for (int[] f : faces)
            {
                int ox = t[0] + f[0], oy = t[1] + f[1], oz = t[2] + f[2];
                Block b = blk(ox, oy, oz);
                if (b == null || !b.isOpaqueCube() || b.hasTileEntity()) continue;
                for (double[] o : offs)
                {
                    double px = t[0] + 0.5 + f[0] * 0.5, py = t[1] + 0.5 + f[1] * 0.5, pz = t[2] + 0.5 + f[2] * 0.5;
                    if (f[0] != 0) { py += o[0]; pz += o[1]; }
                    else if (f[1] != 0) { px += o[0]; pz += o[1]; }
                    else { px += o[0]; py += o[1]; }
                    float[] look = lookAt(px, py, pz);
                    MovingObjectPosition h = ray(look);
                    if (!hits(h, ox, oy, oz) || h.sideHit != f[3] || pointed(look) != null) continue;
                    p.yp = look;
                    p.on = new int[] {ox, oy, oz};
                    p.side = f[3];
                    return true;
                }
            }
            return false;
        }
    }

    /** Light the frame: flint and steel on the top of a bottom obsidian. */
    static final class Light extends Task
    {
        final Site st;
        int phase, tries;
        float[] yp;

        Light(Site st) { this.st = st; }

        Object tick()
        {
            int[] in = st.at(1, 1, 0);
            if (id(in[0], in[1], in[2]) == 90) return DONE;
            if (hotbarOf(FLINT_STEEL) < 0) return new ToHotbar(FLINT_STEEL);
            int hb = hotbarOf(FLINT_STEEL);
            if (phase == 0)
            {
                if (++tries > 6) return FAIL;
                for (int i = 1; i <= 2 && yp == null; ++i)
                {
                    int[] b = st.at(i, 0, 0);
                    for (double[] o : new double[][] {{0.5, 0.5}, {0.3, 0.3}, {0.7, 0.7}, {0.3, 0.7}, {0.7, 0.3}})
                    {
                        float[] l = lookAt(b[0] + o[0], b[1] + 1.0, b[2] + o[1]);
                        MovingObjectPosition h = ray(l);
                        if (hits(h, b[0], b[1], b[2]) && h.sideHit == 1 && pointed(l) == null)
                        {
                            yp = l;
                            break;
                        }
                    }
                }
                if (yp == null) return FAIL;
                phase = 1;
                return new A().look(yp).hb(hb).j();
            }
            if (phase == 1)
            {
                phase = 2;
                return new A().look(yp).hb(hb).press("use").j();
            }
            if (phase++ < 8) return idle();
            phase = 0;
            yp = null;
            return idle();
        }
    }

    /** Walk into the lit portal and stand in it until the server moves the
     * player to the Nether. */
    static final class EnterPortal extends Task
    {
        final Site st;
        int t, inside;

        EnterPortal(Site st) { this.st = st; }

        String label() { return "EnterPortal(" + t + ")"; }

        Object tick()
        {
            if (sp.dimension == -1) return DONE;
            if (++t > 400) return FAIL;
            int[] a = st.at(1, 1, 0), b = st.at(2, 1, 0);
            double tx = (a[0] + b[0]) / 2.0 + 0.5, tz = (a[2] + b[2]) / 2.0 + 0.5;
            AxisAlignedBB portal = AxisAlignedBB.getBoundingBox(Math.min(a[0], b[0]), a[1], Math.min(a[2], b[2]),
                Math.max(a[0], b[0]) + 1, a[1] + 3, Math.max(a[2], b[2]) + 1);
            boolean in = cp.boundingBox.intersectsWith(portal);
            float[] yp = lookAt(tx, cp.posY, tz);
            yp[1] = 0.0F;
            double d = Math.sqrt((tx - cp.posX) * (tx - cp.posX) + (tz - cp.posZ) * (tz - cp.posZ));
            if (in && d < 0.25) return new A().look(yp).j();
            A act = new A().look(yp);
            if (d > 0.2) act.hold("forward");
            if (!in && cp.onGround && cp.boundingBox.minY < a[1]) act.hold("jump");
            return act.j();
        }
    }

    /** Step off the arrival portal onto a Nether cell away from it. */
    static final class LeavePortal extends Task
    {
        int phase;

        static boolean off(int a, int b, int c)
        {
            for (int dx = -2; dx <= 2; ++dx)
                for (int dz = -2; dz <= 2; ++dz)
                    for (int dy = -1; dy <= 3; ++dy)
                        if (id(a + dx, b + dy, c + dz) == 90) return false;
            return true;
        }

        Object tick()
        {
            // seed 1: a portal room dug into a hillside has no cell off the
            // portal to walk to, so dig one
            if (phase == 1 && child == FAIL && s1)
            {
                phase = 2;
                return new DigTo("off the portal", LeavePortal::off, cp.posX, cp.posY, cp.posZ);
            }
            if (phase >= 1) return child == FAIL ? FAIL : DONE;
            phase = 1;
            return new Walk("off the portal", LeavePortal::off, 0.3);
        }
    }

    /** G6: a portal frame near the obsidian, lit, and through to the Nether. */
    static final class SegG6 extends Seg
    {
        Site st;
        int k, collects;
        boolean waited;
        // the bottom row on the floor, the top row under the ceiling, then
        // each column down from its top corner (i, j, obsidian)
        static final int[][] ORDER = {
            {0, 0, 0}, {3, 0, 0}, {1, 0, 1}, {2, 0, 1},
            {0, 4, 0}, {3, 4, 0}, {1, 4, 1}, {2, 4, 1},
            {0, 3, 1}, {0, 2, 1}, {0, 1, 1}, {3, 3, 1}, {3, 2, 1}, {3, 1, 1}};

        /** An item entity in or next to the frame's inside. */
        boolean itemsInFrame()
        {
            int[] a = st.at(1, 1, 0), b = st.at(2, 1, 0);
            AxisAlignedBB in = AxisAlignedBB.getBoundingBox(Math.min(a[0], b[0]), a[1], Math.min(a[2], b[2]),
                Math.max(a[0], b[0]) + 1, a[1] + 3, Math.max(a[2], b[2]) + 1).expand(1.0D, 1.0D, 1.0D);
            for (Object o : ws.loadedEntityList)
                if (o instanceof EntityItem && !((EntityItem)o).isDead && ((EntityItem)o).boundingBox.intersectsWith(in)) return true;
            return false;
        }

        Object tick()
        {
            switch (phase)
            {
                case 0:
                    need(count(OBSIDIAN) >= 10 && count(FLINT_STEEL) >= 1 && count(COBBLE) >= 4, "no portal materials");
                    // seed 1: not onto the surface at night without armour
                    if (s1 && !waited && (GoldSeed1.night() || sp.getHealth() < 20.0F))
                    {
                        waited = true;
                        return new GoldSeed1.NightWait();
                    }
                    st = findSite();
                    need(st != null, "no portal site");
                    phase = 1;
                    {
                        final int[] s0 = st.at(1, 0, 2);
                        final Site fs = st;
                        return new DigTo("portal site " + st, (a, b, c) ->
                        {
                            for (int i = 0; i < 4; ++i)
                                for (int kk = 0; kk <= 2; ++kk)
                                {
                                    int[] q = fs.at(i, 0, kk);
                                    if (a == q[0] && b == q[1] && c == q[2]) return true;
                                }
                            return false;
                        }, s0[0] + 0.5, s0[1], s0[2] + 0.5);
                    }
                case 1:
                    need(child != FAIL, "portal site not reached");
                    phase = 2;
                    return new Excavate(st);
                case 2:
                    need(child != FAIL, "portal room not dug");
                    phase = 21;
                    // the room's drops: one left in the frame would go
                    // through the portal on its own once it is lit
                    return new Collect(null, 6.0);
                case 21:
                    phase = 3;
                    return new Walk("portal front", (a, b, c) ->
                    {
                        int[] q = st.at(1, 0, 2), r = st.at(2, 0, 2);
                        return b == q[1] && (a == q[0] && c == q[2] || a == r[0] && c == r[2]);
                    }, 0.3);
                case 3:
                    need(child != FAIL, "portal front not reached");
                    phase = 4;
                    k = 0;
                    // fall through
                case 4:
                    if (child == FAIL) need(false, "frame block " + k + " not placed");
                    child = null;
                    if (k < ORDER.length)
                    {
                        int[] o = ORDER[k++];
                        int[] c = st.at(o[0], o[1], 0);
                        return o[2] == 1 ? new PlaceAt(OBSIDIAN, OBSIDIAN, c, st) : new PlaceAt(COBBLE, COBBLE, c, st);
                    }
                    phase = 5;
                    return new Walk("portal front", (a, b, c) ->
                    {
                        int[] q = st.at(1, 0, 2), r = st.at(2, 0, 2);
                        return b == q[1] && (a == q[0] && c == q[2] || a == r[0] && c == r[2]);
                    }, 0.3);
                case 5:
                    if (itemsInFrame() && ++collects <= 3)
                    {
                        // then back to the front (phase 4 walks there)
                        phase = 4;
                        return new Collect(null, 6.0);
                    }
                    phase = 6;
                    return new Light(st);
                case 6:
                    need(child != FAIL, "portal not lit");
                    phase = 7;
                    return new EnterPortal(st);
                case 7:
                    need(child != FAIL && sp.dimension == -1, "not in the Nether");
                    phase = 8;
                    return new Idle(5);
                case 8:
                    phase = 9;
                    return new LeavePortal();
                case 9:
                    need(child != FAIL, "could not step off the arrival portal");
                    phase = 10;
                    return new Idle(5);
                default:
                    return DONE;
            }
        }
    }

    // ------------------------------------------------------------------ the fortress

    /** The Throne's blaze spawner: the mob_spawner block nearest the given
     * point in the loaded chunks, or null. */
    static int[] findSpawner(int x0, int y0, int z0, int r)
    {
        int[] best = null;
        double bd = 1e18;
        for (int x = x0 - r; x <= x0 + r; ++x)
            for (int z = z0 - r; z <= z0 + r; ++z)
            {
                if (!loaded(x, z)) continue;
                for (int y = Math.max(1, y0 - 12); y <= Math.min(126, y0 + 12); ++y)
                {
                    if (id(x, y, z) != SPAWNER) continue;
                    double d = (x - x0) * (x - x0) + (y - y0) * (y - y0) + (z - z0) * (z - z0);
                    if (d < bd)
                    {
                        bd = d;
                        best = new int[] {x, y, z};
                    }
                }
            }
        return best;
    }

    static boolean blaze(Entity e)
    {
        return e instanceof net.minecraft.entity.monster.EntityBlaze && !e.isDead && ((EntityLivingBase)e).getHealth() > 0;
    }

    /** The blaze farm at the seed-42 Throne (spawner at -130,83,-99 on a
     * brick platform, its top at y 83, over x -132..-128, z -101..-97). The
     * bot tunnels in from the south along z -95 at y 80, seals the tunnel
     * behind it, digs a 2-high corridor (feet 80) under the platform's
     * middle column x -130 (z -101..-96; the platform block at y 82 stays as
     * its ceiling, and carries the spawner), then a 1-wide trench on each
     * side, x -129 and x -131, y 81..82 (the brick at y 80 is its floor).
     * Every line from a blaze's eye to the player's eye crosses the corridor
     * side above y 82, into the ceiling, or the wall columns x -128 and
     * x -132: no blaze can see the player, so none attacks (EntityCreature
     * attacks only what canEntityBeSeen), while the player reaches the legs
     * of a blaze in a trench through the opening at y 81. Blazes spawned
     * over the trenches sink into them, and a blaze that targets the player
     * paths to the nearest cell to it, which is a trench. */
    static final int FX = -130, FY = 80, FZ0 = -101, FZ1 = -97;

    /** The farm BuildFarm and Farm work, in corridor coordinates: u along
     * the corridor (U0..U1 under the platform, U1 + 1 the entry end), v
     * across it (the middle column at M, the trenches at M +- 1), feet at
     * Y. Seed 42's Throne runs along z (u is z, v is x); seed 1's along x
     * (GoldSeed1.useFarm). */
    static boolean farmX;
    static int fM = FX, fU0 = FZ0, fU1 = FZ1, fY = FY;

    static int fwx(int u, int v) { return farmX ? u : fM + v; }
    static int fwz(int u, int v) { return farmX ? fM + v : u; }
    static double along(Entity e) { return farmX ? e.posX : e.posZ; }
    static double across(Entity e) { return farmX ? e.posZ : e.posX; }
    static int cellU() { return farmX ? cell()[0] : cell()[2]; }

    static boolean inTrench(Entity e)
    {
        double a = across(e), l = along(e);
        boolean col = a > fM + 1 && a < fM + 2 || a > fM - 1 && a < fM;
        return col && e.boundingBox.minY < fY + 3 && l > fU0 && l < fU1 + 1;
    }

    /** Walk the corridor to the cell at u. */
    static Walk corridor(int u)
    {
        final int uc = Math.max(fU0, Math.min(fU1 + 1, u));
        return walkToCell(fwx(uc, 0), fY, fwz(uc, 0), 0.25);
    }

    /** Suppresses the Defend interrupt (nothing can reach the farm). */
    static boolean farming;

    /** Build the farm, one step per call: {kind, x, y, z, stand z}. */
    static final class BuildFarm extends Task
    {
        int k, phase;
        final List<int[]> steps = new ArrayList<int[]>();
        static final int MINE = 0, PLACE = 1;

        void step(int kind, int u, int v, int y, int at)
        {
            steps.add(new int[] {kind, fwx(u, v), y, fwz(u, v), at});
        }

        BuildFarm()
        {
            // in from the tunnel's end at (u U1+1, v +1), then sealed behind
            step(MINE, fU1 + 1, 0, fY + 1, -1);
            step(MINE, fU1 + 1, 0, fY, -1);
            step(PLACE, fU1 + 1, 1, fY, fU1 + 1);
            step(PLACE, fU1 + 1, 1, fY + 1, fU1 + 1);
            // seed 1: the platform ends over the corridor's last cell, so it
            // gets a ceiling, and the step beyond it a wall at head height
            if (s1)
            {
                step(PLACE, fU1 + 1, 0, fY + 2, fU1 + 1);
                step(PLACE, fU1 + 2, 0, fY + 1, fU1 + 1);
            }
            for (int u = fU1; u >= fU0; --u)
            {
                step(MINE, u, 0, fY + 1, u + 1);
                step(MINE, u, 0, fY, u + 1);
            }
            for (int u = fU1; u >= fU0; --u)
                for (int dv = 1; dv >= -1; dv -= 2)
                {
                    step(MINE, u, dv, fY + 1, u);
                    step(MINE, u, dv, fY + 2, u);
                }
        }

        String label() { return "BuildFarm(" + k + "/" + steps.size() + ")"; }

        Object tick()
        {
            if (child == FAIL) throw new Stuck("farm step " + k + " failed: " + java.util.Arrays.toString(steps.get(k)));
            child = null;
            while (k < steps.size())
            {
                int[] st = steps.get(k);
                int x = st[1], y = st[2], z = st[3], at = st[4];
                boolean done = st[0] == PLACE ? id(x, y, z) != 0 : id(x, y, z) == 0;
                if (done)
                {
                    ++k;
                    phase = 0;
                    continue;
                }
                int[] c = cell();
                if (phase == 0 && at != -1 && !(c[0] == fwx(at, 0) && c[1] == fY && c[2] == fwz(at, 0)))
                {
                    phase = 1;
                    return walkToCell(fwx(at, 0), fY, fwz(at, 0), 0.25);
                }
                phase = 0;
                return st[0] == PLACE ? new PlaceAt(COBBLE, COBBLE, new int[] {x, y, z}, null) : new Mine(x, y, z, true);
            }
            return DONE;
        }
    }

    /** Wait in the corridor; hit what falls into a trench; pick up the
     * rods; keep the food bar high enough to heal. */
    static final class Farm extends Task
    {
        final int want;
        int target = -1, kills, t;

        Farm(int want) { this.want = want; }

        String label() { return "Farm(" + count(ROD) + "/" + want + " kills " + kills + ")"; }

        Object tick()
        {
            farming = true;
            if (++t > 40000) throw new Stuck("blazes not farmed in time");
            if (t % 500 == 0)
            {
                StringBuilder b = new StringBuilder();
                int n = 0;
                for (Object o : ws.loadedEntityList)
                    if (blaze((Entity)o) && ((Entity)o).getDistanceSq(sp.posX, sp.posY, sp.posZ) < 400)
                    {
                        ++n;
                        if (inTrench((Entity)o)) b.append(String.format(" %d@%.1f,%.1f,%.1f", ((Entity)o).getEntityId(), ((Entity)o).posX, ((Entity)o).posY, ((Entity)o).posZ));
                    }
                System.out.println("GOLDBOT t=" + Oracle.tick + " farm " + where() + " hp=" + sp.getHealth() + " food=" + sp.getFoodStats().getFoodLevel() + " blazes " + n + " in trench" + b);
            }
            if (child == FAIL) child = null;
            Entity te = target >= 0 ? ws.getEntityByID(target) : null;
            EntityLivingBase e = te != null && blaze(te) && inTrench(te) ? (EntityLivingBase)te : null;
            if (target >= 0 && e == null)
            {
                if (te == null || te.isDead || !blaze(te))
                {
                    ++kills;
                    System.out.println("GOLDBOT t=" + Oracle.tick + " blaze " + target + " dead, rods " + count(ROD));
                }
                target = -1;
            }
            EntityItem rod = null;
            for (Object o : ws.loadedEntityList)
                if (o instanceof EntityItem && !((EntityItem)o).isDead && Item.getIdFromItem(((EntityItem)o).getEntityItem().getItem()) == ROD
                    && inTrench((Entity)o) && (rod == null || ((Entity)o).getDistanceSqToEntity(sp) < rod.getDistanceSqToEntity(sp))) rod = (EntityItem)o;
            if (count(ROD) >= want && rod == null)
            {
                farming = false;
                return DONE;
            }
            if (e == null)
            {
                double bd = 1e9;
                for (Object o : ws.loadedEntityList)
                {
                    Entity b = (Entity)o;
                    if (!blaze(b) || !inTrench(b)) continue;
                    double d = b.getDistanceSqToEntity(sp);
                    if (d < bd)
                    {
                        bd = d;
                        e = (EntityLivingBase)b;
                    }
                }
                if (e != null) target = e.getEntityId();
            }
            if (e != null)
            {
                Entity ce = mc.theWorld.getEntityByID(target);
                if (ce != null)
                    for (double fy : new double[] {0.3, 0.15, 0.45, 0.05, 0.6})
                    {
                        float[] l = lookAt(ce.posX, ce.boundingBox.minY + ce.height * fy, ce.posZ);
                        if (pointed(l) == ce)
                        {
                            A a = new A().look(l).hb(hotbarOf(SSWORD));
                            if (e.hurtResistantTime <= 10) a.press("attack");
                            return a.j();
                        }
                    }
                int zc = MathHelper.floor_double(along(e));
                if (cellU() != Math.max(fU0, Math.min(fU1, zc))) return corridor(zc);
                return ce == null ? idle() : new A().look(lookAt(ce.posX, ce.boundingBox.minY + ce.height * 0.3, ce.posZ)).j();
            }
            if (rod != null)
            {
                int zc = MathHelper.floor_double(along(rod));
                if (cellU() != Math.max(fU0, Math.min(fU1, zc))) return corridor(zc);
                return idle();
            }
            int slot = hotbarOf(COOKED_MEAT);
            if (sp.getFoodStats().getFoodLevel() <= 17 && slot >= 0 && sp.getHealth() < 20)
                return new A().look(new float[] {cp.rotationYaw, cp.rotationPitch}).hb(slot).hold("use").j();
            return idle();
        }
    }

    /** G7: from the arrival portal to the Throne's blaze spawner, then
     * blazes until six rods. */
    static final class SegG7 extends Seg
    {
        final int end;
        int wp;
        // legs short enough for the dig planner, along a route it finds
        static final int[][] WAY = {{-62, 53, -53}, {-80, 69, -59}, {-95, 71, -89}, {-113, 75, -97}};

        SegG7(int from, int end)
        {
            phase = from;
            this.end = end;
        }

        Object tick()
        {
            if (phase >= end) return DONE;
            switch (phase)
            {
                case 0:
                {
                    int[] s = findSpawner(-130, 83, -99, 12);
                    need(s != null && s[0] == -130 && s[1] == 83 && s[2] == -99, "no blaze spawner at the Throne");
                    phase = 1;
                    wp = 0;
                }
                // fall through
                case 1:
                {
                    if (wp > 0) need(child != FAIL, "waypoint " + (wp - 1) + " on the way to the Throne not reached");
                    if (wp < WAY.length)
                    {
                        final int[] w = WAY[wp++];
                        return new DigTo("waypoint " + (wp - 1), (a, b, c) -> Math.abs(a - w[0]) <= 2 && Math.abs(c - w[2]) <= 2 && Math.abs(b - w[1]) <= 2,
                            w[0] + 0.5, w[1], w[2] + 0.5);
                    }
                    phase = 2;
                    // the last leg stays out of the Throne and its channels
                    // (along z -96: z -95 has lava at -129,81,-94 beside it)
                    DigTo d = new DigTo("the farm", (a, b, c) -> a == FX + 1 && b == FY && c == FZ1 + 1, FX + 1.5, FY, FZ1 + 1.5);
                    for (int x = -140; x <= -127; ++x)
                        for (int y = 70; y <= 92; ++y)
                            for (int z = -125; z <= FZ1 + 1; ++z)
                                if (z <= FZ1 || y >= FY + 2) d.avoid.add(key(x, y, z));
                    return d;
                }
                case 2:
                    need(child != FAIL, "the farm was not reached");
                    phase = 3;
                    return new BuildFarm();
                case 3:
                    phase = 4;
                    return new Farm(6);
                case 4:
                    need(count(ROD) >= 6, "fewer than six rods");
                    phase = 5;
                    return new Idle(3);
                default:
                    return DONE;
            }
        }
    }

    /** The bottom portal block nearest (x0,y0,z0) within r in the loaded
     * chunks, or null. */
    static int[] findPortal(int x0, int y0, int z0, int r)
    {
        int[] best = null;
        double bd = 1e18;
        for (int x = x0 - r; x <= x0 + r; ++x)
            for (int z = z0 - r; z <= z0 + r; ++z)
            {
                if (!loaded(x, z)) continue;
                for (int y = Math.max(1, y0 - 8); y <= Math.min(250, y0 + 8); ++y)
                {
                    if (id(x, y, z) != PORTAL || id(x, y - 1, z) == PORTAL) continue;
                    double d = (x - x0) * (x - x0) + (y - y0) * (y - y0) + (z - z0) * (z - z0);
                    if (d < bd)
                    {
                        bd = d;
                        best = new int[] {x, y, z};
                    }
                }
            }
        return best;
    }

    /** Walk into the portal whose bottom block is p (a lit 2-wide frame)
     * and stand in it until the player is in dimension to. */
    static final class ThroughPortal extends Task
    {
        final int[] p;
        final int to;
        int t;

        ThroughPortal(int[] p, int to)
        {
            this.p = p;
            this.to = to;
        }

        String label() { return "ThroughPortal(" + t + ")"; }

        Object tick()
        {
            if (sp.dimension == to) return DONE;
            if (++t > 600) return FAIL;
            // the frame's other bottom portal block, along x or z
            int[] q = id(p[0] + 1, p[1], p[2]) == PORTAL ? new int[] {p[0] + 1, p[1], p[2]}
                : id(p[0] - 1, p[1], p[2]) == PORTAL ? new int[] {p[0] - 1, p[1], p[2]}
                : id(p[0], p[1], p[2] + 1) == PORTAL ? new int[] {p[0], p[1], p[2] + 1} : new int[] {p[0], p[1], p[2] - 1};
            double tx = (p[0] + q[0]) / 2.0 + 0.5, tz = (p[2] + q[2]) / 2.0 + 0.5;
            AxisAlignedBB portal = AxisAlignedBB.getBoundingBox(Math.min(p[0], q[0]), p[1], Math.min(p[2], q[2]),
                Math.max(p[0], q[0]) + 1, p[1] + 3, Math.max(p[2], q[2]) + 1);
            boolean in = cp.boundingBox.intersectsWith(portal);
            float[] yp = lookAt(tx, cp.posY, tz);
            yp[1] = 0.0F;
            double d = Math.sqrt((tx - cp.posX) * (tx - cp.posX) + (tz - cp.posZ) * (tz - cp.posZ));
            if (in && d < 0.25) return new A().look(yp).j();
            A act = new A().look(yp);
            if (d > 0.2) act.hold("forward");
            if (!in && cp.onGround && cp.boundingBox.minY < p[1]) act.hold("jump");
            return act.j();
        }
    }

    /** G8: out of the blaze farm, back along G7's waypoints to the arrival
     * portal (the G6 frame's Nether side, near -29,32,-5), and through it
     * home to the overworld. */
    static final class SegG8 extends Seg
    {
        int wp;
        int[] portal;
        static final int[][] WAY = {{-113, 75, -97}, {-95, 71, -89}, {-80, 69, -59}, {-62, 53, -53}};

        Object tick()
        {
            switch (phase)
            {
                case 0:
                    need(sp.dimension == -1 && count(ROD) >= 6, "not in the Nether with the rods");
                    phase = 1;
                    // to the corridor's south end, beside the seal
                    return walkToCell(FX, FY, FZ1 + 1, 0.25);
                case 1:
                    need(child != FAIL, "the corridor's end not reached");
                    phase = 2;
                    return new Mine(FX + 1, FY + 1, FZ1 + 1, true);
                case 2:
                    phase = 3;
                    return new Mine(FX + 1, FY, FZ1 + 1, true);
                case 3:
                    need(id(FX + 1, FY, FZ1 + 1) == 0 && id(FX + 1, FY + 1, FZ1 + 1) == 0, "the seal not mined");
                    phase = 4;
                    wp = 0;
                    // fall through
                case 4:
                {
                    if (wp > 0) need(child != FAIL, "waypoint " + (wp - 1) + " on the way back not reached");
                    if (wp < WAY.length)
                    {
                        final int[] w = WAY[wp++];
                        return new DigTo("back " + (wp - 1), (a, b, c) -> Math.abs(a - w[0]) <= 2 && Math.abs(c - w[2]) <= 2 && Math.abs(b - w[1]) <= 2,
                            w[0] + 0.5, w[1], w[2] + 0.5);
                    }
                    phase = 5;
                    return new DigTo("the portal", (a, b, c) -> Math.abs(a + 29) + Math.abs(c + 5) <= 3 && b == 32, -28.5, 32, -4.5);
                }
                case 5:
                    need(child != FAIL, "the arrival portal not reached");
                    portal = findPortal(-29, 32, -5, 8);
                    need(portal != null, "no arrival portal");
                    phase = 51;
                    // loose items would travel on their own (native has no
                    // item portal travel yet)
                    return new Collect(null, 6.0);
                case 51:
                    phase = 6;
                    return new ThroughPortal(portal, 0);
                case 6:
                    need(child != FAIL && sp.dimension == 0, "not back in the overworld");
                    phase = 7;
                    return new Idle(5);
                case 7:
                    phase = 8;
                    return new LeavePortal();
                case 8:
                    need(child != FAIL, "could not step off the overworld portal");
                    phase = 9;
                    return new Idle(5);
                default:
                    return DONE;
            }
        }
    }

    static final Recipe IHELMET_R = new Recipe("iron helmet", IHELMET, 1, false, new String[] {"III", "I I"}, 'I', IRON);
    static final Recipe ICHEST_R = new Recipe("iron chestplate", ICHEST, 1, false, new String[] {"I I", "III", "III"}, 'I', IRON);
    static final Recipe ILEGS_R = new Recipe("iron leggings", ILEGS, 1, false, new String[] {"III", "I I", "I I"}, 'I', IRON);
    static final Recipe IBOOTS_R = new Recipe("iron boots", IBOOTS, 1, false, new String[] {"I I", "I I"}, 'I', IRON);
    static final Recipe ISWORD_R = new Recipe("iron sword", ISWORD, 1, false, new String[] {"I", "I", "S"}, 'I', IRON, 'S', STICK);
    static final int POWDER = 377, EYE = 381;
    static final Recipe POWDER_R = new Recipe("blaze powder", POWDER, 2, true, new String[] {"R"}, 'R', ROD);
    static final Recipe EYE_R = new Recipe("eye of ender", EYE, 1, false, new String[] {"PB"}, 'P', PEARL, 'B', POWDER);

    static int armorWorn()
    {
        int n = 0;
        for (int i = 0; i < 4; ++i) if (cp.inventory.armorInventory[i] != null) ++n;
        return n;
    }

    /** Wear every armor piece in the main inventory: open the inventory
     * and shift-click each, which ContainerPlayer moves to its armor slot. */
    static final class Equip extends Task
    {
        int phase, wait;
        final List<Integer> slots = new ArrayList<Integer>();
        int ci;

        Object tick()
        {
            switch (phase)
            {
                case 0:
                    phase = 1;
                    return new A().press("inventory").j();
                case 1:
                    if (!(mc.currentScreen instanceof GuiInventory))
                    {
                        if (++wait > 10) return FAIL;
                        return idle();
                    }
                    for (int i = 0; i < 36; ++i)
                    {
                        ItemStack st = cp.inventory.mainInventory[i];
                        if (st != null && st.getItem() instanceof net.minecraft.item.ItemArmor) slots.add(invSlot(cp.openContainer, i));
                    }
                    phase = 2;
                    // fall through
                case 2:
                    if (ci < slots.size()) return new A().gui("click", -1, slots.get(ci++), 0, 1).j();
                    phase = 3;
                    return new Close();
                default:
                    return DONE;
            }
        }
    }

    /** G9 (the first day home): iron for armor and a sword, coal to smelt
     * it, a table and a furnace placed where the bot stands, the smelt,
     * the crafts, and the armor worn. */
    static final class SegG9 extends Seg
    {
        int[] furnace;
        static final int IRON_WANT = 26;

        Object tick()
        {
            switch (phase)
            {
                case 0:
                    need(sp.dimension == 0, "not in the overworld");
                    phase = 1;
                    return new OreLoop(new int[] {IRON_ORE}, new int[] {IRON_ORE}, IRON_WANT, 6, 90);
                case 1:
                    need(child != FAIL && count(IRON_ORE) >= IRON_WANT, "not enough iron ore");
                    phase = 2;
                    return new OreLoop(new int[] {COAL_ORE}, new int[] {COAL}, 4, 6, 120);
                case 2:
                    need(child != FAIL && count(COAL) >= 4, "not enough coal");
                    phase = 3;
                    return new Craft(false, PLANKS_ALL, 1, TABLE_R, 1, STICKS, 1);
                case 3:
                    need(child != FAIL && count(TABLE) >= 1 && count(STICK) >= 1, "no table or sticks");
                    phase = 4;
                    return new ToHotbar(TABLE);
                case 4:
                    phase = 5;
                    return new PlaceAny(TABLE, TABLE);
                case 5:
                    need(child != FAIL, "table not placed");
                    phase = 6;
                    return new Craft(true, FURNACE_R, 1);
                case 6:
                    need(child != FAIL && count(FURNACE) >= 1, "no furnace");
                    phase = 7;
                    return new ToHotbar(FURNACE);
                case 7:
                    phase = 8;
                    return new PlaceAny(FURNACE, FURNACE);
                case 8:
                    need(child != FAIL, "furnace not placed");
                    furnace = findBlock(new int[] {FURNACE}, 6);
                    need(furnace != null, "placed furnace not found");
                    phase = 9;
                    // every coal in one stack (4 smelt 32)
                    return new FurnaceLoad(furnace, new int[] {IRON_ORE}, COAL, count(COAL));
                case 9:
                    need(child != FAIL, "furnace not loaded");
                    phase = 10;
                    return new WaitSmelt(furnace, IRON_WANT);
                case 10:
                    phase = 11;
                    return new FurnaceTake(furnace);
                case 11:
                    need(count(IRON) >= IRON_WANT, "fewer than " + IRON_WANT + " iron ingots");
                    phase = 12;
                    return new Craft(true, IHELMET_R, 1, ICHEST_R, 1, ILEGS_R, 1, IBOOTS_R, 1, ISWORD_R, 1);
                case 12:
                    need(child != FAIL && count(ISWORD) == 1, "armor and sword not crafted");
                    phase = 13;
                    return new Equip();
                case 13:
                    need(child != FAIL && armorWorn() == 4, "armor not worn: " + armorWorn());
                    phase = 14;
                    return new Idle(3);
                default:
                    return DONE;
            }
        }
    }

    // ------------------------------------------------------------------ endermen

    static boolean enderman(Entity e)
    {
        return e instanceof net.minecraft.entity.monster.EntityEnderman && !e.isDead && ((EntityLivingBase)e).getHealth() > 0;
    }

    static boolean aggressive(Entity e)
    {
        return ((net.minecraft.entity.EntityCreature)e).getEntityToAttack() == sp;
    }

    static long worldTime()
    {
        return ws.getWorldTime() % 24000L;
    }

    /** EntityEnderman.shouldAttackPlayer for the server player looking along
     * (yaw, pitch): the look within the stare cone of the body's middle,
     * and the enderman in sight. */
    static boolean stare(float yaw, float pitch, Entity e)
    {
        float f1 = MathHelper.cos(-yaw * 0.017453292F - (float)Math.PI);
        float f2 = MathHelper.sin(-yaw * 0.017453292F - (float)Math.PI);
        float f3 = -MathHelper.cos(-pitch * 0.017453292F);
        float f4 = MathHelper.sin(-pitch * 0.017453292F);
        Vec3 look = Vec3.createVectorHelper((double)(f2 * f3), (double)f4, (double)(f1 * f3)).normalize();
        Vec3 to = Vec3.createVectorHelper(e.posX - sp.posX, e.boundingBox.minY + (double)(e.height / 2.0F) - (sp.posY + (double)sp.getEyeHeight()), e.posZ - sp.posZ);
        double d = to.lengthVector();
        to = to.normalize();
        return look.dotProduct(to) > 1.0D - 0.025D / d && sp.canEntityBeSeen(e);
    }

    /** Both ends in loaded chunks, so the sight ray reads no unloaded one. */
    static boolean nearLoaded(Entity e)
    {
        return loaded(MathHelper.floor_double(e.posX), MathHelper.floor_double(e.posZ)) && e.getDistanceSqToEntity(sp) < 56.0 * 56.0;
    }

    /** A mob whose AI has the server player as its target (the old AI's
     * entityToAttack or the new AI's attackTarget). */
    static boolean targets(Entity x)
    {
        if (x instanceof net.minecraft.entity.EntityCreature && ((net.minecraft.entity.EntityCreature)x).getEntityToAttack() == sp) return true;
        return x instanceof net.minecraft.entity.EntityLiving && ((net.minecraft.entity.EntityLiving)x).getAttackTarget() == sp;
    }

    /** Hit an enderman in reach on the legs, never with the crosshair on
     * its middle (a stare within four blocks teleports it). */
    static JsonObject legHit(EntityLivingBase e, Entity ce, double d)
    {
        float[] rest = null;
        for (double fy : new double[] {0.15, 0.25, 0.08, 0.35, 0.05})
        {
            float[] l = lookAt(ce.posX, ce.boundingBox.minY + ce.height * fy, ce.posZ);
            if (stare(l[0], l[1], e)) continue;
            if (rest == null) rest = l;
            if (pointed(l) == ce)
            {
                A a = new A().look(l).hb(swordSlot());
                if (e.hurtResistantTime <= 10) a.press("attack");
                return a.j();
            }
        }
        if (rest == null) rest = new float[] {lookAt(ce.posX, ce.posY, ce.posZ)[0], 60.0F};
        A a = new A().look(rest).hb(swordSlot());
        if (d > 2.3) a.hold("forward");
        return a.j();
    }

    /** While the ender hunt runs, an enderman after the player that comes
     * within five blocks is fought whatever the bot was doing (a dig). */
    static boolean enderHunting;
    static int enderKills;

    static EntityLivingBase enderClose()
    {
        for (Object o : ws.loadedEntityList)
        {
            Entity x = (Entity)o;
            // seed 1: never the one hitting the player now (a skipped one
            // killed the bot mid-dig)
            if (segNum() >= 18 && !(s1 && sp.getAITarget() == x))
            {
                Long since = unreachable.get(x.getEntityId());
                if (since != null && Oracle.tick - since < 600) continue;
            }
            if (enderman(x) && aggressive(x) && x.getDistanceSqToEntity(sp) < 25.0) return (EntityLivingBase)x;
        }
        return null;
    }

    static final class EnderFight extends Interrupt
    {
        final int target;
        int t, lastHit;
        boolean collected;

        EnderFight(int target) { this.target = target; }

        String label() { return "EnderFight(" + target + ")"; }

        Object tick()
        {
            if (collected) return DONE;
            Entity e = ws.getEntityByID(target);
            if (e == null || e.isDead || ((EntityLivingBase)e).getHealth() <= 0)
            {
                if (e != null && ((EntityLivingBase)e).getHealth() <= 0)
                {
                    ++enderKills;
                    System.out.println("GOLDBOT t=" + Oracle.tick + " enderman " + target + " dead (fight), pearls " + count(PEARL));
                    collected = true;
                    return new Collect(new int[] {PEARL}, 10.0);
                }
                return DONE;
            }
            double d = Math.sqrt(e.getDistanceSqToEntity(sp));
            Entity ce = mc.theWorld.getEntityByID(target);
            if (ce == null || d > 6.0 || ++t > 400) return DONE;
            if (segNum() >= 18 && ((EntityLivingBase)e).hurtTime > 0) lastHit = t;
            if (segNum() >= 18 && t - lastHit > 100)
            {
                System.out.println("GOLDBOT t=" + Oracle.tick + " enderman " + target + " out of reach of the fight");
                unreachable.put(target, (long)Oracle.tick);
                return DONE;
            }
            if (d < 3.6) return legHit((EntityLivingBase)e, ce, d);
            float[] l = lookAt(e.posX, e.posY, e.posZ);
            l[1] = Math.max(l[1], 35.0F);
            if (stare(l[0], l[1], e)) l[1] = 60.0F;
            return new A().look(l).hb(swordSlot()).j();
        }
    }

    /** No ray-solid block on the segment between two points. */
    static boolean clearLine(double ax, double ay, double az, double bx, double by, double bz)
    {
        double dx = bx - ax, dy = by - ay, dz = bz - az;
        int n = Math.max(1, (int)Math.ceil(Math.sqrt(dx * dx + dy * dy + dz * dz) / 0.25));
        for (int i = 1; i < n; ++i)
        {
            double t = (double)i / n;
            if (raySolid(MathHelper.floor_double(ax + dx * t), MathHelper.floor_double(ay + dy * t), MathHelper.floor_double(az + dz * t))) return false;
        }
        return true;
    }

    /** Endermen by night. An enderman after the player is waited for with
     * the eyes kept off its middle (a stare within four blocks teleports it
     * away, and one farther than 16 blocks only teleports toward the player
     * while not stared at) and hit on the legs; one stuck within 16 blocks
     * (in a cave below) is lured by walking away, so its teleport brings it
     * up. Else the nearest one in sight is stared at until it turns
     * (EntityEnderman.findPlayerToAttack), one out of sight but under the
     * open sky is walked toward, and else the bot roams the surface. Other
     * hostiles that turn on the player within 16 blocks are killed first.
     * Pearls are picked up after each kill. Until want pearls, or the dawn
     * with no enderman after the player. */
    static final class EnderHunt extends Task
    {
        final int want;
        final HashMap<Integer, Long> skip = new HashMap<Integer, Long>();
        final HashMap<Integer, Long> dug = new HashMap<Integer, Long>();
        int target = -1, chasing = -1, kills, others, t, walkingTo = -1, roamFails, stuckFor, digTries;
        long chaseStart, lastPlan = -100000, lastDig = -100000, stareStart, lastWalk = -100000, wall = System.nanoTime(), lastSword = -100000, lastFood = -100000, lastCook = -100000;
        boolean chasingEnder;
        double lastD = 1e9, heading = Math.PI * 0.5;
        Follow f, roamF, lureF;

        final long until;

        EnderHunt(int want, long until)
        {
            this.want = want;
            this.until = until;
        }

        String label() { return "EnderHunt(" + count(PEARL) + "/" + want + " kills " + enderKills + "+" + others + ")"; }

        boolean skipped(Entity x)
        {
            Long since = skip.get(x.getEntityId());
            return since != null && Oracle.tick - since < 400;
        }

        Object tick()
        {
            enderHunting = true;
            if (++t % 500 == 0)
            {
                StringBuilder b = new StringBuilder();
                for (Object o : ws.loadedEntityList)
                    if (enderman((Entity)o))
                        b.append(String.format(" %d@%.0f,%.0f,%.0f%s", ((Entity)o).getEntityId(), ((Entity)o).posX, ((Entity)o).posY, ((Entity)o).posZ, aggressive((Entity)o) ? "!" : ""));
                long now = System.nanoTime();
                System.out.println("GOLDBOT t=" + Oracle.tick + " hunt " + where() + " hp=" + sp.getHealth() + " food=" + sp.getFoodStats().getFoodLevel()
                    + " time=" + worldTime() + " ms=" + (now - wall) / 1000000 + " endermen" + b);
                wall = now;
            }
            if (child == FAIL) child = null;
            // the chased mob died
            if (chasing >= 0)
            {
                Entity ce = ws.getEntityByID(chasing);
                if (ce == null || ce.isDead || ((EntityLivingBase)ce).getHealth() <= 0)
                {
                    boolean killed = ce != null && ((EntityLivingBase)ce).getHealth() <= 0;
                    if (chasingEnder && killed) ++enderKills;
                    else if (killed) ++others;
                    System.out.println("GOLDBOT t=" + Oracle.tick + " " + (chasingEnder ? "enderman " : "mob ") + chasing + (killed ? " dead" : " gone") + ", pearls " + count(PEARL));
                    chasing = -1;
                    f = null;
                    lureF = null;
                    if (chasingEnder && killed) return new Collect(new int[] {PEARL}, 10.0);
                }
            }
            if (count(PEARL) >= want) return DONE;
            if (segNum() >= 15 && Oracle.tick >= until + 1200) return DONE;
            // the sword broke: a stone one from the pack
            if (segNum() >= 15 && swordSlot() < 0 && Oracle.tick - lastSword > 1200)
            {
                lastSword = Oracle.tick;
                System.out.println("GOLDBOT t=" + Oracle.tick + " no sword: crafting a stone one at " + where());
                return new ReSword(1);
            }
            // a hostile after the player
            EntityLivingBase th = null;
            double bd = 16.0 * 16.0;
            for (Object o : ws.loadedEntityList)
            {
                Entity x = (Entity)o;
                if (!(x instanceof EntityLivingBase) || !hostile(x)) continue;
                double d = x.getDistanceSqToEntity(sp);
                if (skipped(x) && !(targets(x) && d < 36.0)) continue;
                if (Math.abs(x.posY - sp.posY) > 4 || !(targets(x) || d < 25.0)) continue;
                if (d >= 25.0 && (!nearLoaded(x) || !sp.canEntityBeSeen(x))) continue;
                if (d < bd)
                {
                    bd = d;
                    th = (EntityLivingBase)x;
                }
            }
            if (th != null) return chase(th, false);
            // an enderman after the player
            EntityLivingBase e = null;
            bd = 1e18;
            for (Object o : ws.loadedEntityList)
            {
                Entity x = (Entity)o;
                if (!enderman(x) || !aggressive(x)) continue;
                double d = x.getDistanceSqToEntity(sp);
                if (skipped(x) && d >= 36.0) continue;
                if (d < bd)
                {
                    bd = d;
                    e = (EntityLivingBase)x;
                }
            }
            if (e != null && Oracle.tick < until + 1200) return fight(e);
            lureF = null;
            stuckFor = 0;
            lastD = 1e9;
            if (chasing >= 0 && chasingEnder) chasing = -1;
            if (Oracle.tick >= until) return DONE;
            f = null;
            // seed 1 from S16: meat on the way (an animal within 48 while
            // under four pieces are carried), a cook-up once raw meat piles
            // up, and meals by the trek's tiers (the G15 hunt ate only cooked
            // meat, and three of eight S15 tries died hungry at low health)
            if (s1 && s1k >= 16)
            {
                boolean sky = ws.canBlockSeeTheSky(MathHelper.floor_double(sp.posX), MathHelper.floor_double(sp.posY) + 1, MathHelper.floor_double(sp.posZ));
                int meat = count(COOKED_MEAT) + count(PORK, BEEF, CHICKEN);
                if (meat < 4 && Oracle.tick - lastFood > 1200 && animalWithin(48.0) && sky)
                {
                    lastFood = Oracle.tick;
                    System.out.println("GOLDBOT t=" + Oracle.tick + " meat " + meat + ": hunting an animal from " + where());
                    return new Hunt(count(RAW_MEAT) + count(COOKED_MEAT) + 4, 1);
                }
                if (count(COOKED_MEAT) < 2 && count(PORK, BEEF, CHICKEN) >= 4 && Oracle.tick - lastCook > 4000 && sky)
                {
                    lastCook = Oracle.tick;
                    System.out.println("GOLDBOT t=" + Oracle.tick + " cooking " + count(PORK, BEEF, CHICKEN) + " raw at " + where());
                    return new Provision();
                }
                Object eat = Trek.eat(sp.getHealth() < 16.0F ? 17 : 16, eatLook());
                if (eat != null) return eat;
                if (cp.isUsingItem()) return idle();
            }
            // low on meat: an animal close by first
            if (segNum() >= 16 && count(COOKED_MEAT) + count(PORK, BEEF) < 2 && Oracle.tick - lastFood > 1200 && animalWithin(48.0)
                && ws.canBlockSeeTheSky(MathHelper.floor_double(sp.posX), MathHelper.floor_double(sp.posY) + 1, MathHelper.floor_double(sp.posZ)))
            {
                lastFood = Oracle.tick;
                System.out.println("GOLDBOT t=" + Oracle.tick + " short of meat: hunting an animal from " + where());
                return new Hunt(count(RAW_MEAT) + count(COOKED_MEAT) + 3, 1);
            }
            // keep the food bar at 18 or more, which the regeneration needs
            int food = sp.getFoodStats().getFoodLevel();
            int slot = hotbarOf(COOKED_MEAT);
            if (slot < 0 && food <= 14) slot = hotbarOf(PORK, BEEF);
            if (slot < 0 && food <= 14 && segNum() >= 16 && count(COOKED_MEAT) + count(PORK, BEEF) == 0 && count(ROTTEN) > 0)
            {
                slot = hotbarOf(ROTTEN);
                if (slot < 0) return new ToHotbar(ROTTEN);
            }
            // raw meat off the hotbar when nothing cooked is left
            if (slot < 0 && food <= 14 && count(COOKED_MEAT) == 0)
                for (int k : new int[] {PORK, BEEF})
                    if (count(k) > 0) return new ToHotbar(k);
            if (slot >= 0 && food <= 17)
                return new A().look(new float[] {cp.rotationYaw, cp.rotationPitch}).hb(slot).hold("use").j();
            if (cp.isUsingItem()) return idle();
            if (sp.getHealth() < 12) return idle();
            // one to stare down: the nearest in sight
            Entity te = null;
            bd = 56.0 * 56.0;
            for (Object o : ws.loadedEntityList)
            {
                Entity x = (Entity)o;
                if (!enderman(x) || skipped(x) || !nearLoaded(x)) continue;
                double d = x.getDistanceSqToEntity(sp);
                if (d < bd && sp.canEntityBeSeen(x))
                {
                    bd = d;
                    te = x;
                }
            }
            if (te != null)
            {
                if (te.getEntityId() != target)
                {
                    target = te.getEntityId();
                    stareStart = Oracle.tick;
                }
                if (Oracle.tick - stareStart > 200) skip.put(target, (long)Oracle.tick);
                return new A().look(lookAt(te.posX, te.boundingBox.minY + te.height / 2.0F, te.posZ)).hb(swordSlot()).j();
            }
            target = -1;
            // one in a cave within 48 blocks: dig to a cell in sight of it
            // (not from G16: the caves are where the hunt died)
            if (Oracle.tick - lastDig > 100 && segNum() < 16)
            {
                Entity cv = null;
                double cr = 48.0;
                bd = cr * cr;
                for (Object o : ws.loadedEntityList)
                {
                    Entity x = (Entity)o;
                    int bx = MathHelper.floor_double(x.posX), by = MathHelper.floor_double(x.posY), bz = MathHelper.floor_double(x.posZ);
                    if (!enderman(x) || skipped(x) || !loaded(bx, bz) || ws.canBlockSeeTheSky(bx, by, bz)) continue;
                    Long tried = dug.get(x.getEntityId());
                    if (tried != null && Oracle.tick - tried < 1200) continue;
                    double d = x.getDistanceSqToEntity(sp);
                    if (d < bd)
                    {
                        bd = d;
                        cv = x;
                    }
                }
                if (cv != null)
                {
                    lastDig = Oracle.tick;
                    dug.put(cv.getEntityId(), (long)Oracle.tick);
                    final double tx = cv.posX, ty = cv.boundingBox.minY + cv.height / 2.0F, tz = cv.posZ;
                    DigTo d = new DigTo("enderman " + cv.getEntityId(), (a, b, c) -> (a + 0.5 - tx) * (a + 0.5 - tx) + (b + 1.62 - ty) * (b + 1.62 - ty)
                        + (c + 0.5 - tz) * (c + 0.5 - tz) < 10.0 * 10.0 && clearLine(a + 0.5, b + 1.62, c + 0.5, tx, ty, tz), tx, cv.posY, tz);
                    d.maxNodes = 30000;
                    return d;
                }
            }
            // underground (a chase or a fight led there): back up to the sky
            int[] c0 = cell();
            if (roamF == null && c0[1] < ws.getHeightValue(c0[0], c0[2]) - 2 && !ws.canBlockSeeTheSky(c0[0], c0[1] + 1, c0[2]))
            {
                if (Oracle.tick - lastWalk < 60) return idle();
                lastWalk = Oracle.tick;
                return new DigTo("the surface", (a, b, c) -> b >= ws.getHeightValue(a, c), sp.posX, 70, sp.posZ);
            }
            // toward the nearest enderman standing under open sky
            Entity far = null;
            bd = 160.0 * 160.0;
            for (Object o : ws.loadedEntityList)
            {
                Entity x = (Entity)o;
                int bx = MathHelper.floor_double(x.posX), by = MathHelper.floor_double(x.posY), bz = MathHelper.floor_double(x.posZ);
                if (!enderman(x) || !loaded(bx, bz) || skipped(x) || !ws.canBlockSeeTheSky(bx, by, bz)) continue;
                double d = x.getDistanceSqToEntity(sp);
                if (d < bd)
                {
                    bd = d;
                    far = x;
                }
            }
            if (far != null)
            {
                if (roamF != null && far.getEntityId() == walkingTo)
                {
                    Object r = roamF.step();
                    if (r != null && r != FAIL) return r;
                }
                roamF = null;
                walkingTo = far.getEntityId();
                final double tx = far.posX, tz = far.posZ;
                List<int[]> p = surfacePlan((a, b, c) -> (a + 0.5 - tx) * (a + 0.5 - tx) + (c + 0.5 - tz) * (c + 0.5 - tz) < 16.0 * 16.0
                    && b >= ws.getHeightValue(a, c), 20000, tx, tz);
                if (p == null || p.size() < 2)
                {
                    skip.put(walkingTo, (long)Oracle.tick);
                    return idle();
                }
                roamF = new Follow(p, 0.6, true);
                System.out.println("GOLDBOT t=" + Oracle.tick + " toward enderman " + walkingTo + " at " + Math.round(tx) + "," + Math.round(tz) + " from " + where() + " " + p.size() + " cells");
                return idle();
            }
            walkingTo = -1;
            return roam();
        }

        /** An enderman after the player: hit it on the legs in reach, else
         * wait for it with the eyes low, luring it out of a cave. */
        Object fight(EntityLivingBase e)
        {
            if (chasing != e.getEntityId())
            {
                chasing = e.getEntityId();
                chasingEnder = true;
                chaseStart = Oracle.tick;
                f = null;
                lureF = null;
                stuckFor = 0;
                digTries = 0;
                lastD = 1e9;
            }
            Entity ce = mc.theWorld.getEntityByID(chasing);
            double d = Math.sqrt(e.getDistanceSqToEntity(sp));
            if (ce != null && d < 3.6)
            {
                lureF = null;
                return legHit(e, ce, d);
            }
            // is it coming? (a path within 16 blocks, a teleport beyond)
            if (d < lastD - 0.5)
            {
                lastD = d;
                stuckFor = 0;
            }
            else ++stuckFor;
            if (lureF != null)
            {
                Object r = lureF.step();
                if (r != null && r != FAIL) return r;
                lureF = null;
                lastD = d;
                stuckFor = 0;
            }
            if (stuckFor > 100)
            {
                stuckFor = 0;
                lastD = 1e9;
                if (d < 20.0)
                {
                    // walk off along the surface, past its 16-block path range
                    final double ex = e.posX, ez = e.posZ;
                    List<int[]> p = surfacePlan((a, b, c) -> (a + 0.5 - ex) * (a + 0.5 - ex) + (c + 0.5 - ez) * (c + 0.5 - ez) > 24.0 * 24.0
                        && b >= ws.getHeightValue(a, c) && ws.canBlockSeeTheSky(a, b + 1, c), 8000, Double.NaN, 0);
                    if (p != null && p.size() > 1)
                    {
                        System.out.println("GOLDBOT t=" + Oracle.tick + " lure enderman " + chasing + " at " + Math.round(ex) + "," + Math.round(e.posY) + "," + Math.round(ez) + " from " + where() + " " + p.size() + " cells");
                        lureF = new Follow(p, 0.6, true);
                        return idle();
                    }
                }
                // farther: walk toward it (a short leg), else dig to it,
                // else leave it
                if (Oracle.tick - lastPlan > 40)
                {
                    lastPlan = Oracle.tick;
                    final double ex = e.posX, ez = e.posZ;
                    List<int[]> p = plan((a, b, c) -> (a + 0.5 - ex) * (a + 0.5 - ex) + (c + 0.5 - ez) * (c + 0.5 - ez) < 4.0, 8000, null, ex, ez);
                    if (p != null && p.size() > 1) lureF = new Follow(p, 0.6, false);
                    else if (++digTries <= 2)
                    {
                        final int ey = MathHelper.floor_double(e.posY + 0.05);
                        DigTo dt = new DigTo("to enderman " + chasing, (a, b, c) -> (a + 0.5 - ex) * (a + 0.5 - ex) + (c + 0.5 - ez) * (c + 0.5 - ez) < 6.25
                            && Math.abs(b - ey) <= 1, ex, ey, ez);
                        dt.maxNodes = 30000;
                        return dt;
                    }
                    else
                    {
                        System.out.println("GOLDBOT t=" + Oracle.tick + " leaving enderman " + chasing);
                        skip.put(chasing, (long)Oracle.tick);
                        chasing = -1;
                        digTries = 0;
                        return idle();
                    }
                }
            }
            // eyes low toward it: never on its middle
            float[] l = lookAt(e.posX, e.posY, e.posZ);
            l[1] = Math.max(l[1], 35.0F);
            if (stare(l[0], l[1], e)) l[1] = 60.0F;
            return new A().look(l).hb(swordSlot()).j();
        }

        /** Walk the surface in 40-block legs along a heading, turning when a
         * leg cannot be planned: far mobs despawn behind (EntityLiving's
         * 128-block rule) and the spawner fills the chunks ahead. */
        Object roam()
        {
            if (roamF != null)
            {
                Object r = roamF.step();
                if (r != null && r != FAIL) return r;
                roamF = null;
                if (r == FAIL) ++roamFails;
            }
            if (Oracle.tick - lastWalk < 20) return idle();
            lastWalk = Oracle.tick;
            // from G16 the roam leads back toward the stronghold's eyes target
            if (segNum() >= 16 && roamFails % 4 == 0) heading = Math.atan2(-(HOME_X - sp.posX), HOME_Z - sp.posZ);
            if (s1 && !Double.isNaN(roamHeading) && roamFails % 4 == 0) heading = roamHeading;
            for (int tries = 0; tries < 4; ++tries)
            {
                final double tx = sp.posX - Math.sin(heading) * 40.0, tz = sp.posZ + Math.cos(heading) * 40.0;
                List<int[]> p = surfacePlan((a, b, c) -> (a + 0.5 - tx) * (a + 0.5 - tx) + (c + 0.5 - tz) * (c + 0.5 - tz) < 36.0
                    && b >= ws.getHeightValue(a, c), 20000, tx, tz);
                if (p != null && p.size() > 3)
                {
                    roamF = new Follow(p, 0.6, false);
                    System.out.println("GOLDBOT t=" + Oracle.tick + " roam heading " + Math.round(Math.toDegrees(heading)) + " from " + where() + " " + p.size() + " cells");
                    return idle();
                }
                // turn a quarter, alternately left and right
                heading += (++roamFails % 2 == 0 ? 1 : -1) * Math.PI * 0.5 + 0.3;
            }
            // walled in (a pit, a dug stair): dig out toward the heading
            final double tx = sp.posX - Math.sin(heading) * 12.0, tz = sp.posZ + Math.cos(heading) * 12.0;
            return new DigTo("out", (a, b, c) -> (a + 0.5 - tx) * (a + 0.5 - tx) + (c + 0.5 - tz) * (c + 0.5 - tz) < 16.0
                && b >= ws.getHeightValue(a, c), tx, sp.posY, tz);
        }

        /** Close in on a hostile and hit it. */
        Object chase(EntityLivingBase e, boolean ender)
        {
            if (chasing != e.getEntityId())
            {
                chasing = e.getEntityId();
                chasingEnder = ender;
                chaseStart = Oracle.tick;
                f = null;
            }
            if (Oracle.tick - chaseStart > 600 && e.getDistanceSqToEntity(sp) > 16.0)
            {
                skip.put(chasing, (long)Oracle.tick);
                chasing = -1;
                f = null;
                return idle();
            }
            Entity ce = mc.theWorld.getEntityByID(chasing);
            if (ce != null)
            {
                double cx = ce.posX, cy = ce.boundingBox.minY + ce.height * 0.5, cz = ce.posZ;
                double dx = cx - cp.posX, dy = cy - cp.posY, dz = cz - cp.posZ;
                double d = Math.sqrt(dx * dx + dy * dy + dz * dz);
                if (d < 3.6)
                {
                    for (double fy : new double[] {0.5, 0.8, 0.25, 0.95, 0.1})
                    {
                        float[] l = lookAt(ce.posX, ce.boundingBox.minY + ce.height * fy, ce.posZ);
                        if (pointed(l) == ce)
                        {
                            A a = new A().look(l).hb(swordSlot());
                            if (e.hurtResistantTime <= 10) a.press("attack");
                            return a.j();
                        }
                    }
                    A a = new A().look(lookAt(cx, cy, cz)).hb(swordSlot());
                    if (d > 2.3) a.hold("forward");
                    return a.j();
                }
            }
            // path toward it; replan when it has moved off the goal
            double ex = e.posX, ez = e.posZ;
            if (f == null && Oracle.tick - lastPlan < 20) return idle();
            if (f == null || Oracle.tick - lastPlan >= 20)
            {
                lastPlan = Oracle.tick;
                final int tx = MathHelper.floor_double(ex), ty = MathHelper.floor_double(e.posY + 0.05), tz = MathHelper.floor_double(ez);
                List<int[]> p = plan((a, b, c) -> Math.abs(a - tx) <= 1 && Math.abs(c - tz) <= 1 && Math.abs(b - ty) <= 1, 8000, null);
                if (p == null)
                {
                    skip.put(chasing, (long)Oracle.tick);
                    chasing = -1;
                    f = null;
                    return idle();
                }
                f = new Follow(p, 0.6, false);
            }
            Object r = f.step();
            if (r == null || r == FAIL)
            {
                f = null;
                return idle();
            }
            return r;
        }
    }

    /** Wait (idle, the interrupts still fighting) until the world time of
     * day reaches from. */
    static final class WaitTime extends Task
    {
        final long from;
        int t;

        WaitTime(long from) { this.from = from; }

        String label() { return "WaitTime(" + from + ")"; }

        Object tick()
        {
            long wt = worldTime();
            if (wt >= from && wt < from + 6000) return DONE;
            if (++t > 30000) throw new Stuck("the time never came");
            return idle();
        }
    }

    /** Throw out the stacks the rest of the run has no use for (netherrack,
     * dirt, gravel, sand, nether brick, rotten flesh, bones, string, flint,
     * seeds, cobblestone past two stacks), through the inventory screen's
     * throw click (mode 4, button 1: the whole stack), so crafting has room. */
    static final class Tidy extends Task
    {
        static final int[] JUNK = {87, 3, 13, 12, 112, 367, 352, 287, 318, 295};
        /** G16 on: rotten flesh stays (food of last resort), saplings, leather,
         * arrows, gunpowder and spider eyes go too. */
        static final int[] TREK_JUNK = {87, 3, 13, 12, 112, 352, 287, 318, 295, 6, 334, 262, 289, 375};
        int phase, wait, ci;
        final List<Integer> slots = new ArrayList<Integer>();
        final int[] junkIds;

        Tidy() { this(JUNK); }

        Tidy(int[] junkIds) { this.junkIds = junkIds; }

        static int free()
        {
            int n = 0;
            for (int i = 0; i < 36; ++i) if (cp.inventory.mainInventory[i] == null) ++n;
            return n;
        }

        Object tick()
        {
            switch (phase)
            {
                case 0:
                    phase = 1;
                    // seed 1 from S21: the throws go up and out (thrown at
                    // the feet after a dig, the junk came back in the pickup)
                    if (s1 && s1k >= 21) return new A().look(new float[] {cp.rotationYaw, -30.0F}).press("inventory").j();
                    return new A().press("inventory").j();
                case 1:
                    if (!(mc.currentScreen instanceof GuiInventory))
                    {
                        if (++wait > 10) return FAIL;
                        return idle();
                    }
                    {
                        int cobble = 0;
                        for (int i = 9; i < 36; ++i)
                        {
                            ItemStack st = cp.inventory.mainInventory[i];
                            if (st == null) continue;
                            int id = Item.getIdFromItem(st.getItem());
                            boolean junk = false;
                            for (int k : junkIds) if (id == k) junk = true;
                            if (id == COBBLE && ++cobble > 2) junk = true;
                            if (junk) slots.add(invSlot(cp.openContainer, i));
                        }
                    }
                    phase = 2;
                    // fall through
                case 2:
                    if (ci < slots.size()) return new A().gui("click", -1, slots.get(ci++), 1, 4).j();
                    phase = 3;
                    return new Close();
                default:
                    return DONE;
            }
        }
    }

    /** Food for the hunt: meat from the nearest animals, then a table and a
     * furnace from a felled tree and the carried cobblestone, cooked with
     * coal (planks when no coal ore is found). Skipped parts: no animals
     * loaded, no tree the walk planner reaches. */
    static final class Provision extends Task
    {
        final HashSet<Long> badTrees = new HashSet<Long>();
        int phase, tries, waited, rounds, fuel, cooking;
        boolean tidied;
        int[] furnace;

        String label() { return "Provision#" + phase; }

        int animals()
        {
            int n = 0;
            for (Object o : ws.loadedEntityList)
                if ((o instanceof EntityPig || o instanceof EntityCow || o instanceof EntityChicken) && !((EntityLivingBase)o).isChild()
                    && ((Entity)o).getDistanceSqToEntity(sp) < 96.0 * 96.0) ++n;
            return n;
        }

        Object tick()
        {
            switch (phase)
            {
                case 0:
                    // room first: a full inventory picks nothing up
                    if (!tidied && Tidy.free() < 4)
                    {
                        tidied = true;
                        return s1 && s1k >= 21 ? new Tidy(GoldSeed1.TREK_JUNK) : new Tidy();
                    }
                    phase = 1;
                    if (animals() < 3) return idle();
                    return new Hunt(count(RAW_MEAT) + count(COOKED_MEAT) + 8, 3);
                case 1:
                    if (count(RAW_MEAT) == 0) return DONE;
                    if (count(LOG, LOG2) >= 2)
                    {
                        phase = 2;
                        return idle();
                    }
                    // a felled tree's logs reach the inventory after their
                    // pickup delay: wait for them before the next tree
                    if (waited < tries)
                    {
                        waited = tries;
                        return new Idle(30);
                    }
                    if (++tries > 4) return DONE;
                    {
                        Chop c = new Chop(badTrees);
                        if (!c.pick()) return DONE;
                        return c;
                    }
                case 2:
                    phase = 3;
                    if (count(COAL) >= 2) return idle();
                    {
                        OreLoop o = new OreLoop(new int[] {COAL_ORE}, new int[] {COAL}, 2, 6, 120);
                        return o;
                    }
                case 3:
                    phase = 4;
                    // seed 1 from S21: the trek's junk (string and flint stay:
                    // the kit's bow and arrows)
                    if (Tidy.free() < 2) return s1 && s1k >= 21 ? new Tidy(GoldSeed1.TREK_JUNK) : new Tidy();
                    return idle();
                case 4:
                    phase = 5;
                    return new Craft(false, PLANKS_ALL, 1, TABLE_R, 1);
                case 5:
                    if (child == FAIL || count(TABLE) < 1) return DONE;
                    phase = 6;
                    return new ToHotbar(TABLE);
                case 6:
                    phase = 7;
                    return new PlaceAny(TABLE, TABLE);
                case 7:
                    if (child == FAIL) return DONE;
                    phase = 8;
                    return new Craft(true, FURNACE_R, 1);
                case 8:
                    if (child == FAIL || count(FURNACE) < 1) return DONE;
                    phase = 9;
                    return new ToHotbar(FURNACE);
                case 9:
                    phase = 10;
                    return new PlaceAny(FURNACE, FURNACE);
                case 10:
                    if (child == FAIL) return DONE;
                    furnace = findBlock(new int[] {FURNACE}, 6);
                    if (furnace == null) return DONE;
                    phase = 11;
                    // fall through
                case 11:
                {
                    // one raw kind per load, the largest stack first
                    int kind = -1, most = 0;
                    for (int k : new int[] {PORK, BEEF, CHICKEN})
                        if (count(k) > most)
                        {
                            most = count(k);
                            kind = k;
                        }
                    if (kind < 0 || ++rounds > 3) return DONE;
                    cooking = Math.min(most, 64);
                    int coalN = (cooking + 7) / 8, plankN = (cooking * 2 + 2) / 3;
                    if (count(COAL) >= coalN) fuel = COAL;
                    else if (count(PLANKS) >= plankN) fuel = PLANKS;
                    else
                    {
                        // what the fuel cooks
                        if (count(COAL) > 0) { fuel = COAL; cooking = Math.min(cooking, count(COAL) * 8); coalN = count(COAL); }
                        else if (count(PLANKS) > 0) { fuel = PLANKS; cooking = Math.min(cooking, count(PLANKS) * 3 / 2); plankN = count(PLANKS); }
                        else return DONE;
                    }
                    phase = 12;
                    return new FurnaceLoad(furnace, new int[] {kind}, fuel, fuel == COAL ? coalN : plankN);
                }
                case 12:
                    if (child == FAIL) return DONE;
                    phase = 13;
                    return new WaitSmelt(furnace, cooking);
                case 13:
                    phase = 14;
                    return new FurnaceTake(furnace);
                case 14:
                    phase = 11;
                    if (hotbarOf(COOKED_MEAT) < 0)
                        for (int k : COOKED_MEAT) if (count(k) > 0) return new ToHotbar(k);
                    return idle();
                default:
                    return DONE;
            }
        }
    }

    /** G10 on: the ender hunt, from wherever the last segment ended, until
     * want pearls or the tick limit (with no enderman after the player). */
    /** A stone sword: sticks and a table from the planks, cobble from the pack. */
    static final class ReSword extends Task
    {
        final int n;
        int phase;

        ReSword(int n) { this.n = n; }

        Object tick()
        {
            switch (phase)
            {
                case 0:
                {
                    phase = 1;
                    if (count(PLANKS) >= 6 || count(LOG, LOG2) == 0) return idle();
                    Craft c = new Craft(false, PLANKS_ALL, 1);
                    c.fresh = trekNum() >= 23;
                    return c;
                }
                case 1:
                    phase = 2;
                    if (count(STICK) >= n) return idle();
                    if (count(PLANKS) < 2) return FAIL;
                    return new Craft(false, STICKS, 1);
                case 2:
                    if (count(STICK) < n || count(COBBLE) < 2 * n) return FAIL;
                    if (findBlock(new int[] {TABLE}, 5) != null)
                    {
                        phase = 5;
                        return idle();
                    }
                    phase = 3;
                    if (count(TABLE) >= 1) return idle();
                    if (count(PLANKS) < 4) return FAIL;
                    return new Craft(false, TABLE_R, 1);
                case 3:
                    if (count(TABLE) < 1) return FAIL;
                    phase = 4;
                    return new ToHotbar(TABLE);
                case 4:
                    phase = 5;
                    return new PlaceAny(TABLE, TABLE);
                case 5:
                    if (child == FAIL) return FAIL;
                    phase = 6;
                    return new Craft(true, SSWORD_R, n);
                case 6:
                    if (count(SSWORD) < 1) return FAIL;
                    phase = 7;
                    if (hotbarOf(SSWORD) >= 0) return idle();
                    return new ToHotbar(SSWORD);
                default:
                    System.out.println("GOLDBOT t=" + Oracle.tick + " stone sword in slot " + swordSlot());
                    return DONE;
            }
        }
    }

    // ------------------------------------------------------------------ G16 on: the trek

    /** While set, no interrupt fires: the trek fights from its post itself. */
    static boolean posted;

    static final int[][] CARD = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

    /** Solid ground to dig into or wall with (no leaves). */
    static boolean ground(int x, int y, int z)
    {
        Block b = blk(x, y, z);
        return floor(x, y, z) && b.getMaterial() != Material.leaves;
    }

    /** The night post: the player's feet cell (x, y, z) sunk one block into
     * open ground, walled at head height on three sides and roofed; the
     * fourth side (mx, mz) opens at head height onto the surface. An
     * enderman (2.9 tall) cannot come in; at the mouth its eyes cannot see
     * the player's past the roof (EntityCreature attacks only an entity in
     * sight, and a look at it is no stare), while the player hits its legs.
     * One block in the opening seals it. */
    static final class Post
    {
        final int x, y, z, mx, mz;

        Post(int x, int y, int z, int mx, int mz)
        {
            this.x = x;
            this.y = y;
            this.z = z;
            this.mx = mx;
            this.mz = mz;
        }

        int[] mouth() { return new int[] {x + mx, y + 1, z + mz}; }

        boolean in()
        {
            int[] c = cell();
            return c[0] == x && c[1] == y && c[2] == z;
        }

        boolean sealed() { return !pass(x + mx, y + 1, z + mz); }

        /** Roof, the three head-height walls and the four feet-height sides stand. */
        boolean intact()
        {
            if (!floor(x, y + 2, z)) return false;
            for (int[] m : CARD)
            {
                if (!floor(x + m[0], y, z + m[1])) return false;
                if ((m[0] != mx || m[1] != mz) && !floor(x + m[0], y + 1, z + m[1])) return false;
            }
            return true;
        }

        /** The look out of the opening, low over the ground. */
        float[] rest()
        {
            return new float[] {lookAt(x + mx * 8 + 0.5, cp.posY, z + mz * 8 + 0.5)[0], 20.0F};
        }

        public String toString() { return x + "," + y + "," + z + " opening " + mx + "," + mz; }
    }

    static Post trekPost;

    /** Rock or earth over the head within 12 blocks (not a tree's canopy). */
    static boolean buried(int x, int y, int z)
    {
        return buried(x, y, z, 12);
    }

    static boolean buried(int x, int y, int z, int reach)
    {
        for (int k = 2; k <= reach; ++k)
        {
            Block b = blk(x, y + k, z);
            if (b == null) return false;
            Material m = b.getMaterial();
            if (m == Material.rock || m == Material.ground || m == Material.grass || m == Material.sand || m == Material.field_151571_B) return true;
        }
        return false;
    }

    /** A post the player stands in already (the last segment ended there). */
    static Post findPost()
    {
        int[] c = cell();
        int x = c[0], y = c[1], z = c[2];
        if (!floor(x, y + 2, z) || !pass(x, y, z) || !pass(x, y + 1, z)) return null;
        int open = -1, n = 0;
        for (int k = 0; k < 4; ++k)
        {
            if (!floor(x + CARD[k][0], y, z + CARD[k][1])) return null;
            if (pass(x + CARD[k][0], y + 1, z + CARD[k][1]))
            {
                open = k;
                ++n;
            }
        }
        if (n > 1) return null;
        if (n == 0)
        {
            // sealed: the side whose cobble has open air above, as the opening had
            double best = -1;
            for (int k = 0; k < 4; ++k)
            {
                int ox = x + CARD[k][0], oz = z + CARD[k][1];
                if (id(ox, y + 1, oz) != COBBLE || !pass(ox, y + 2, oz) || !pass(ox, y + 3, oz)) continue;
                double o = openness(x, y + 1, z, CARD[k][0], CARD[k][1]);
                if (o > best)
                {
                    best = o;
                    open = k;
                }
            }
            if (open < 0) return null;
        }
        return new Post(x, y, z, CARD[open][0], CARD[open][1]);
    }

    /** How much open ground a post's opening looks over from feet level b
     * (the surface): cells of a quarter circle 4 to 40 blocks out whose top
     * is no higher than the opening nor far below it, and not water. */
    static double openness(int a, int b, int c, int mx, int mz)
    {
        int n = 0;
        for (int d = 4; d <= 40; d += 2)
            for (int w = -d; w <= d; w += 2)
            {
                int x = a + mx * d + mz * w, z = c + mz * d + mx * w;
                if (!loaded(x, z)) continue;
                int h = ws.getHeightValue(x, z);
                if (h <= b + 1 && h >= b - 6 && !liquid(blk(x, h - 1, z))) ++n;
            }
        return n / 10.0;
    }

    /** A post site within r: {x, surface feet y, z, mx, mz}, the most open
     * one that needs the fewest blocks, nearest first on a tie. */
    static final HashSet<Long> badSites = new HashSet<Long>();

    static int[] postSite(int r)
    {
        return postSite(r, null);
    }

    /** Can the eye of a player whose feet are at (x, y, z) see the entity's
     * eyes (EntityLivingBase.canEntityBeSeen's ray)? */
    static boolean seesFrom(double x, double y, double z, Entity e)
    {
        return ws.rayTraceBlocks(Vec3.createVectorHelper(x, y + 1.62, z), Vec3.createVectorHelper(e.posX, e.posY + (double)e.getEyeHeight(), e.posZ)) == null;
    }

    /** The same with the opening toward entity e and e in sight of the
     * post's eye through it. */
    static int[] postSite(int r, Entity e)
    {
        int[] c0 = cell();
        int[] best = null;
        double bs = -1e9;
        for (int dx = -r; dx <= r; ++dx)
            for (int dz = -r; dz <= r; ++dz)
            {
                int a = c0[0] + dx, c = c0[2] + dz;
                if (!loaded(a, c)) continue;
                int b = ws.getHeightValue(a, c);
                if (badSites.contains(key(a, b, c))) continue;
                if (b < 4 || b > 250 || !stand(a, b, c) || !ground(a, b - 1, c) || !diggable(a, b - 1, c) || falling(blk(a, b - 1, c))
                    || !floor(a, b - 2, c) || !safeCell(a, b - 1, c)) continue;
                // seed 1: not with a plant in the feet cell, which takes the
                // ray down to the block to dig (tall grass failed every post)
                if (s1 && id(a, b, c) != 0) continue;
                for (int[] m : CARD)
                {
                    int mx = m[0], mz = m[1], sx = mz, sz = mx;
                    // the opening: a cell an enderman stands in, on natural ground
                    if (!stand(a + mx, b, c + mz) || !pass(a + mx, b + 2, c + mz) || water(a + mx, b, c + mz) || !ground(a + mx, b - 1, c + mz)) continue;
                    if (e != null)
                    {
                        double ex = e.posX - (a + 0.5), ez = e.posZ - (c + 0.5), el = Math.sqrt(ex * ex + ez * ez);
                        if (el < 4.0 || (ex * mx + ez * mz) / el < 0.77 || !seesFrom(a + 0.5, b - 1, c + 0.5, e)) continue;
                    }
                    if (!ground(a - mx, b - 1, c - mz) || !ground(a + sx, b - 1, c + sz) || !ground(a - sx, b - 1, c - sz)) continue;
                    int place = 1;
                    boolean bad = false;
                    for (int[] w : new int[][] {{a - mx, c - mz}, {a + sx, c + sz}, {a - sx, c - sz}})
                    {
                        if (floor(w[0], b, w[1])) continue;
                        if (!pass(w[0], b, w[1]) || liquid(blk(w[0], b, w[1]))) bad = true;
                        ++place;
                    }
                    if (bad) continue;
                    if (!postSupport(a, b, c, mx, mz))
                    {
                        if (!pass(a - mx, b + 1, c - mz) || liquid(blk(a - mx, b + 1, c - mz))) continue;
                        ++place;
                    }
                    double score = (e != null ? 0 : openness(a, b, c, mx, mz)) - 2.0 * place - 0.5 * Math.sqrt(dx * dx + dz * dz);
                    if (score > bs)
                    {
                        bs = score;
                        best = new int[] {a, b, c, mx, mz};
                    }
                }
            }
        return best;
    }

    /** A block beside the roof cell (x, b + 1, z) to place the roof against. */
    static boolean postSupport(int a, int b, int c, int mx, int mz)
    {
        return floor(a - mx, b + 1, c - mz) || floor(a + mz, b + 1, c + mx) || floor(a - mz, b + 1, c - mx) || floor(a, b + 2, c);
    }

    /** Build a post at site s (postSite): stand on the cell, dig it out and
     * drop in, wall the three sides at head height with cobble, a support
     * block over the back wall (placed at the top of a jump, its top face
     * being above the eye), and the roof against the support's side. */
    static final class BuildPost extends Task
    {
        final int a, b, c, mx, mz;
        int phase, walks, waited, wi;
        final List<int[]> walls = new ArrayList<int[]>();

        BuildPost(int[] s)
        {
            a = s[0];
            b = s[1];
            c = s[2];
            mx = s[3];
            mz = s[4];
        }

        String label() { return "BuildPost(" + a + "," + b + "," + c + " " + mx + "," + mz + " #" + phase + ")"; }

        Object tick()
        {
            if (child == FAIL)
            {
                System.out.println("GOLDBOT t=" + Oracle.tick + " post at " + a + "," + b + "," + c + " failed in phase " + phase);
                badSites.add(key(a, b, c));
                return FAIL;
            }
            child = null;
            switch (phase)
            {
                case 0:
                {
                    int[] cc = cell();
                    AxisAlignedBB bb = cp.boundingBox;
                    if (cc[0] == a && cc[1] == b && cc[2] == c && bb.minX >= a + 0.02 && bb.maxX <= a + 0.98 && bb.minZ >= c + 0.02 && bb.maxZ <= c + 0.98)
                    {
                        phase = 1;
                        System.out.println("GOLDBOT t=" + Oracle.tick + " post dig at " + a + "," + b + "," + c + " opening " + mx + "," + mz);
                        return new Mine(a, b - 1, c, true);
                    }
                    if (++walks > 8)
                    {
                        badSites.add(key(a, b, c));
                        return FAIL;
                    }
                    return walkToCell(a, b, c, 0.12);
                }
                case 1:
                    if (feetY() != b - 1)
                    {
                        if (++waited > 40) return FAIL;
                        return idle();
                    }
                    for (int[] w : new int[][] {{a - mx, c - mz}, {a + mz, c + mx}, {a - mz, c - mx}})
                        if (!floor(w[0], b, w[1])) walls.add(new int[] {w[0], b, w[1]});
                    phase = 2;
                    // fall through
                case 2:
                    while (wi < walls.size())
                    {
                        int[] w = walls.get(wi++);
                        if (!floor(w[0], w[1], w[2])) return new PlaceAt(COBBLE, COBBLE, w, null);
                    }
                    for (int[] w : walls) if (!floor(w[0], w[1], w[2])) return FAIL;
                    phase = 3;
                    if (!postSupport(a, b, c, mx, mz)) return new JumpPlace(COBBLE, new int[] {a - mx, b + 1, c - mz}, new int[] {a - mx, b, c - mz});
                    // fall through
                case 3:
                    if (!postSupport(a, b, c, mx, mz)) return FAIL;
                    if (!cp.onGround) return idle();
                    phase = 4;
                    return new PlaceAt(COBBLE, COBBLE, new int[] {a, b + 1, c}, null);
                case 4:
                    if (!floor(a, b + 1, c)) return FAIL;
                    trekPost = new Post(a, b - 1, c, mx, mz);
                    System.out.println("GOLDBOT t=" + Oracle.tick + " post built at " + trekPost + " time=" + worldTime());
                    return DONE;
                default:
                    return DONE;
            }
        }
    }

    /** Place a block on the top face of block `on`, which is above the eye
     * standing: jump, and click while the crosshair reaches that face. */
    static final class JumpPlace extends Task
    {
        final int item;
        final int[] t, on;
        int ticks, cool;

        JumpPlace(int item, int[] t, int[] on)
        {
            this.item = item;
            this.t = t;
            this.on = on;
        }

        String label() { return "JumpPlace(" + t[0] + "," + t[1] + "," + t[2] + ")"; }

        Object tick()
        {
            if (floor(t[0], t[1], t[2])) return DONE;
            if (hotbarOf(item) < 0) return new ToHotbar(item);
            if (++ticks > 160) return FAIL;
            int hb = hotbarOf(item);
            A a = new A().hb(hb);
            if (cool > 0) --cool;
            else if (!cp.onGround && hb == cp.inventory.currentItem)
                for (double[] o : OFFS)
                {
                    float[] yp = lookAt(on[0] + 0.5 + o[0], on[1] + 1.0, on[2] + 0.5 + o[1]);
                    MovingObjectPosition h = ray(yp);
                    if (hits(h, on[0], on[1], on[2]) && h.sideHit == 1 && pointed(yp) == null)
                    {
                        cool = 4;
                        return a.look(yp).press("use").j();
                    }
                }
            if (cp.onGround) a.hold("jump");
            return a.look(lookAt(on[0] + 0.5, on[1] + 1.0, on[2] + 0.5)).j();
        }
    }

    /** Eyes at (x, y, z) see the point, from the cell centre and (strict)
     * from four points a quarter block off it, so an off-centre stand sees
     * it too. */
    static boolean sightFrom(double x, double y, double z, double tx, double ty, double tz, boolean strict)
    {
        if (ws.rayTraceBlocks(Vec3.createVectorHelper(x, y, z), Vec3.createVectorHelper(tx, ty, tz)) != null) return false;
        if (!strict) return true;
        for (double[] o : new double[][] {{0.25, 0.25}, {-0.25, 0.25}, {0.25, -0.25}, {-0.25, -0.25}})
            if (ws.rayTraceBlocks(Vec3.createVectorHelper(x + o[0], y, z + o[1]), Vec3.createVectorHelper(tx, ty, tz)) != null) return false;
        return true;
    }

    /** A cave enderman, from a tunnel: dig to a roofed cell within 14 blocks
     * with its eyes in sight, stare it into turning, step one cell back into
     * the tunnel (an enderman at the tunnel mouth cannot see the player's
     * eyes there, nor come into a two-high tunnel) and hit its legs as it
     * comes; then its pearl. */
    static final class CaveHunt extends Task
    {
        int target;
        double lx, ly, lz;
        int phase, t, replans, blind, digUps;
        long until = Long.MAX_VALUE, born = Oracle.tick, lostAt = -1;
        DigTo dig;
        double px, py, pz;
        long since;
        int[] back, front;
        boolean hit;

        CaveHunt(Entity e)
        {
            target = e.getEntityId();
            px = lx = e.posX;
            py = ly = e.posY;
            pz = lz = e.posZ;
        }

        String label() { return "CaveHunt(" + target + " #" + phase + ")"; }

        Entity live()
        {
            Entity x = ws.getEntityByID(target);
            return x != null && !x.isDead && x instanceof net.minecraft.entity.monster.EntityEnderman ? x : null;
        }

        Object tick()
        {
            Entity e = live();
            if (phase == 9) return DONE;
            if (e == null && !hit && segNum() >= 18)
            {
                // its chunk was saved and loaded again (a new entity id): the
                // enderman nearest where it was
                double bd = 6.0 * 6.0;
                for (Object o : ws.loadedEntityList)
                {
                    Entity x = (Entity)o;
                    if (!enderman(x)) continue;
                    double d = (x.posX - lx) * (x.posX - lx) + (x.posY - ly) * (x.posY - ly) + (x.posZ - lz) * (x.posZ - lz);
                    if (d < bd)
                    {
                        bd = d;
                        e = x;
                    }
                }
                if (e != null)
                {
                    System.out.println("GOLDBOT t=" + Oracle.tick + " cave enderman " + target + " is now " + e.getEntityId());
                    target = e.getEntityId();
                    lostAt = -1;
                }
                else if (segNum() >= 19)
                {
                    // not loaded again yet: wait a little for it
                    if (lostAt < 0) lostAt = Oracle.tick;
                    if (Oracle.tick - lostAt < 80) return idle();
                }
            }
            if (e != null)
            {
                lx = e.posX;
                ly = e.posY;
                lz = e.posZ;
            }
            if (e == null || ((EntityLivingBase)e).getHealth() <= 0)
            {
                boolean killed = e != null || hit;
                System.out.println("GOLDBOT t=" + Oracle.tick + " cave enderman " + target + (e != null ? " dead" : " gone") + ", pearls " + count(PEARL));
                if (e != null) ++enderKills;
                phase = 9;
                if (killed) return new Collect(new int[] {PEARL}, 8.0);
                return DONE;
            }
            if (++t > 3000) return FAIL;
            // a hunt's own limit (G18 on), a fight in reach excepted
            if (segNum() >= 18 && Oracle.tick - born > huntLimit() && phase < 3)
            {
                System.out.println("GOLDBOT t=" + Oracle.tick + " cave enderman " + target + ": the hunt took too long");
                return FAIL;
            }
            // the segment's end (G17 on): a fight in reach finishes first
            if (Oracle.tick >= until && (phase < 4 || Oracle.tick >= until + 600 || e.getDistanceSqToEntity(sp) > 16.0)) return FAIL;
            switch (phase)
            {
                case 0:
                {
                    if (++replans > 4) return FAIL;
                    px = e.posX;
                    py = e.posY;
                    pz = e.posZ;
                    final double ex = e.posX, ey = e.posY + e.getEyeHeight(), ez = e.posZ;
                    // G21 on: a first plan that finds no near cell tries the loose goal
                    final boolean loose = segNum() >= 21 && replans >= 2 && dig != null && dig.path == null;
                    final boolean strict = segNum() >= 17 && !loose, near = segNum() >= 18 && !loose;
                    final int efeet = MathHelper.floor_double(e.posY + 0.05);
                    final long digStart = Oracle.tick;
                    dig = new DigTo("enderman " + target, (a, b, c) ->
                    {
                        double dx = a + 0.5 - ex, dy = b + 1.62 - ey, dz = c + 0.5 - ez, d2 = dx * dx + dy * dy + dz * dz;
                        if (d2 > huntMax * huntMax || d2 < 9.0 || !floor(a, b + 2, c)) return false;
                        // G18 on: near its own level, where it can walk up to the tunnel
                        if (near && (d2 > huntNear * huntNear || Math.abs(b - efeet) > huntDy)) return false;
                        return sightFrom(a + 0.5, b + 1.62, c + 0.5, ex, ey, ez, strict);
                    }, ex, e.posY, ez);
                    dig.maxNodes = 40000;
                    if (segNum() >= 17)
                    {
                        dig.maxNodes = 60000;
                        dig.heur = 6.0;
                    }
                    dig.stop = () ->
                    {
                        Entity x = live();
                        return x == null || (x.posX - px) * (x.posX - px) + (x.posY - py) * (x.posY - py) + (x.posZ - pz) * (x.posZ - pz) > 36.0
                            || Oracle.tick >= until || (strict && Oracle.tick - digStart > huntLimit() * 5 / 8) || (segNum() >= 18 && Oracle.tick - born > huntLimit());
                    };
                    phase = 1;
                    return dig;
                }
                case 1:
                    if (child == FAIL)
                    {
                        child = null;
                        if (dig.path == null && replans > (segNum() >= 21 ? 2 : 1)) return FAIL;
                        phase = 0;
                        return idle();
                    }
                    child = null;
                    back = front = null;
                    if (dig.path != null && dig.path.size() >= 2)
                    {
                        DNode a = dig.path.get(dig.path.size() - 1), b = dig.path.get(dig.path.size() - 2);
                        front = new int[] {a.x, a.y, a.z};
                        if (b.y == a.y && floor(b.x, b.y + 2, b.z) && Math.abs(b.x - a.x) + Math.abs(b.z - a.z) == 1) back = new int[] {b.x, b.y, b.z};
                    }
                    phase = 2;
                    since = Oracle.tick;
                    blind = 0;
                    System.out.println("GOLDBOT t=" + Oracle.tick + " cave enderman " + target + String.format(" at %.0f,%.0f,%.0f", e.posX, e.posY, e.posZ) + ": staring from " + where());
                    // fall through
                case 2:
                {
                    if (aggressive(e))
                    {
                        phase = 3;
                        since = Oracle.tick;
                        System.out.println("GOLDBOT t=" + Oracle.tick + " cave enderman " + target + " turned; back " + (back == null ? "none" : back[0] + "," + back[1] + "," + back[2]));
                        return idle();
                    }
                    float[] l = lookAt(e.posX, e.boundingBox.minY + e.height / 2.0F, e.posZ);
                    if (!stare(l[0], l[1], e) && ++blind > 30)
                    {
                        phase = 0;
                        return idle();
                    }
                    if (Oracle.tick - since > 200) return FAIL;
                    return new A().look(l).hb(swordSlot()).j();
                }
                case 3:
                {
                    // one step back into the tunnel
                    int[] c = cell();
                    if (back != null && !(c[0] == back[0] && c[1] == back[1] && c[2] == back[2]) && Oracle.tick - since < 40)
                    {
                        List<int[]> step = new ArrayList<int[]>();
                        step.add(c);
                        step.add(back);
                        Object r = new Follow(step, 0.3, false).step();
                        if (r instanceof JsonObject) return r;
                    }
                    phase = 4;
                    since = Oracle.tick;
                }
                // fall through
                case 4:
                {
                    double d = Math.sqrt(e.getDistanceSqToEntity(sp));
                    Entity ce = mc.theWorld.getEntityByID(target);
                    if (ce != null && d < 4.5)
                    {
                        for (double fy : new double[] {0.15, 0.25, 0.08, 0.35, 0.05, 0.45})
                        {
                            float[] l = lookAt(ce.posX, ce.boundingBox.minY + ce.height * fy, ce.posZ);
                            if (stare(l[0], l[1], e) || pointed(l) != ce) continue;
                            hit = true;
                            since = Oracle.tick;
                            A a = new A().look(l).hb(swordSlot());
                            if (((EntityLivingBase)e).hurtResistantTime <= 10) a.press("attack");
                            return a.j();
                        }
                    }
                    if (!aggressive(e) || t > 2500)
                    {
                        System.out.println("GOLDBOT t=" + Oracle.tick + " cave enderman " + target + (aggressive(e) ? " never came" : " calmed"));
                        return FAIL;
                    }
                    if (Oracle.tick - since > 200 && child != FAIL && segNum() >= 17 && digUps >= 2)
                    {
                        System.out.println("GOLDBOT t=" + Oracle.tick + " cave enderman " + target + " out of reach");
                        return FAIL;
                    }
                    if (Oracle.tick - since > 200 && child != FAIL)
                    {
                        ++digUps;
                        // it does not come: a roofed cell beside where it stands
                        since = Oracle.tick;
                        final double ex = e.posX, ey = e.posY + e.getEyeHeight(), ez = e.posZ;
                        final int fy = MathHelper.floor_double(e.posY + 0.05);
                        System.out.println("GOLDBOT t=" + Oracle.tick + " cave enderman " + target + String.format(" stays at %.1f,%.1f,%.1f", e.posX, e.posY, e.posZ) + ": digging up to it");
                        final boolean strict = segNum() >= 17;
                        final long upStart = Oracle.tick;
                        DigTo up = new DigTo("up to enderman " + target, (a, b, c) ->
                        {
                            double dx = a + 0.5 - ex, dz = c + 0.5 - ez;
                            if (dx * dx + dz * dz > 2.6 * 2.6 || Math.abs(b - fy) > 1 || !floor(a, b + 2, c)) return false;
                            return sightFrom(a + 0.5, b + 1.62, c + 0.5, ex, ey, ez, strict);
                        }, ex, e.posY, ez);
                        up.maxNodes = 20000;
                        if (strict) up.stop = () -> Oracle.tick >= until || Oracle.tick - upStart > 800;
                        return up;
                    }
                    if (child == FAIL) child = null;
                    // wait toward where it comes from, the eyes low
                    float[] l = front != null && back != null ? lookAt(front[0] + 0.5, cp.posY, front[2] + 0.5) : lookAt(e.posX, e.posY, e.posZ);
                    l[1] = Math.max(l[1], 30.0F);
                    if (stare(l[0], l[1], e)) l[1] = 70.0F;
                    return new A().look(l).hb(swordSlot()).j();
                }
                default:
                    return DONE;
            }
        }
    }

    /** Hurt underground: cobble on every open side of the feet and head
     * cells and over the head, then eat and wait until healed. */
    static final class Bunker extends Task
    {
        int phase, wi, waited;
        final List<int[]> walls = new ArrayList<int[]>();

        String label() { return "Bunker(" + phase + ")"; }

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
                System.out.println("GOLDBOT t=" + Oracle.tick + " bunker at " + where() + " hp=" + sp.getHealth());
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
            int slot = hotbarOf(COOKED_MEAT);
            if (slot < 0 && count(COOKED_MEAT) > 0 && food < 20) return new ToHotbar(COOKED_MEAT);
            if (slot < 0 && food <= 14) slot = hotbarOf(PORK, BEEF);
            if (slot < 0 && food <= 10) slot = hotbarOf(ROTTEN);
            if (slot >= 0 && food < 20) return new A().look(new float[] {cp.rotationYaw, 60.0F}).hb(slot).hold("use").j();
            if (cp.isUsingItem()) return idle();
            ++waited;
            boolean starving = segNum() >= 19 && food < 18 && count(COOKED_MEAT) + count(PORK, BEEF) == 0 && waited > 100;
            if (sp.getHealth() >= 20.0F || (sp.getHealth() >= 14.0F && food < 18) || waited > 3000 || starving)
            {
                System.out.println("GOLDBOT t=" + Oracle.tick + " bunker " + (starving ? "left, nothing to eat" : "healed") + " hp=" + sp.getHealth() + " food=" + food);
                return DONE;
            }
            return idle();
        }
    }

    /** G16 on: the trek. */
    static final class SegTrek extends Seg
    {
        Object tick()
        {
            switch (phase)
            {
                case 0:
                {
                    phase = 1;
                    // seed 1: no posts (the pillar), so none to go back to
                    trekPost = s1 ? null : findPost();
                    if (trekPost != null)
                    {
                        System.out.println("GOLDBOT t=" + Oracle.tick + " in the post at " + trekPost);
                        return idle();
                    }
                    int[] c0 = cell();
                    // G27 on: the trek handles a start underground itself (a
                    // night tunnels on; this dig had wandered 85 blocks off)
                    if (segNum() < 27 && c0[1] < ws.getHeightValue(c0[0], c0[2]) - 2 && !ws.canBlockSeeTheSky(c0[0], c0[1] + 1, c0[2]))
                        return new DigTo("the surface", (a, b, c) -> b >= ws.getHeightValue(a, c), sp.posX, 70, sp.posZ);
                    return idle();
                }
                case 1:
                    phase = 2;
                    if (trekPost == null && Tidy.free() < 6) return new Tidy(s1 ? GoldSeed1.TREK_JUNK : Tidy.TREK_JUNK);
                    return idle();
                case 2:
                    phase = 3;
                    return new Trek(segStart + HUNT_TICKS);
                case 3:
                    phase = 4;
                    return new Idle(3);
                default:
                    return DONE;
            }
        }
    }

    /** Toward the stronghold over the surface by day, with meat from the
     * animals on the way; from dusk a post (BuildPost) on open ground, and
     * through the night the endermen in sight of its opening are stared at
     * until they turn (EntityEnderman.findPlayerToAttack), come to the
     * opening (the only cell beside the player they can stand in) and are
     * hit on the legs. Hurt, the opening is sealed until healed. At dawn the
     * roof is dug out and the walk goes on. */
    static final class Trek extends Task
    {
        final long until;
        final HashMap<Integer, Long> skip = new HashMap<Integer, Long>();
        int target = -1, t, chasing = -1, roamFails, others, digFails;
        long stareStart, lastWalk = -100000, lastFood = -100000, lastSite = -100000, lastDigFail = -100000, wall = System.nanoTime();
        long lastPearls = -100000;
        boolean chasingEnder, healing;
        int lastCave = -1;

        /** The nearest enderman out of the sky within r, not skipped. */
        Entity caveEnder(double r)
        {
            Entity best = null;
            double bd = r * r;
            for (Object o : ws.loadedEntityList)
            {
                Entity x = (Entity)o;
                if (!enderman(x) || skipped(x)) continue;
                int bx = MathHelper.floor_double(x.posX), bz = MathHelper.floor_double(x.posZ);
                if (!loaded(bx, bz) || MathHelper.floor_double(x.posY + 0.05) >= ws.getHeightValue(bx, bz) - 1 || x.posY < (segNum() >= 18 ? 20 : 12)) continue;
                double d = x.getDistanceSqToEntity(sp);
                if (d < bd)
                {
                    bd = d;
                    best = x;
                }
            }
            return best;
        }

        static boolean pearlWithin(double r)
        {
            for (Object o : ws.loadedEntityList)
                if (o instanceof EntityItem && !((Entity)o).isDead && Item.getIdFromItem(((EntityItem)o).getEntityItem().getItem()) == PEARL
                    && ((Entity)o).getDistanceSqToEntity(sp) < r * r && !ignoredItems.contains(((Entity)o).getEntityId())) return true;
            return false;
        }
        Follow roamF;
        Task lastTask;

        Object start(Task k)
        {
            lastTask = k;
            return k;
        }

        Trek(long until) { this.until = until; }

        String label() { return "Trek(" + count(PEARL) + " pearls, kills " + enderKills + "+" + others + (trekPost != null && trekPost.in() ? " posted" : "") + ")"; }

        final HashMap<Long, Long> skipPos = new HashMap<Long, Long>();

        static long posKey(double x, double y, double z)
        {
            return key(MathHelper.floor_double(x) >> 2, MathHelper.floor_double(y) >> 2, MathHelper.floor_double(z) >> 2);
        }

        boolean skipped(Entity x)
        {
            Long since = skip.get(x.getEntityId());
            if (since != null && Oracle.tick - since < 400) return true;
            since = skipPos.get(posKey(x.posX, x.posY, x.posZ));
            return since != null && Oracle.tick - since < 3000;
        }

        Object tick()
        {
            if (++t % 500 == 0)
            {
                StringBuilder b = new StringBuilder();
                for (Object o : ws.loadedEntityList)
                    if (enderman((Entity)o))
                        b.append(String.format(" %d@%.0f,%.0f,%.0f%s", ((Entity)o).getEntityId(), ((Entity)o).posX, ((Entity)o).posY, ((Entity)o).posZ, aggressive((Entity)o) ? "!" : ""));
                long now = System.nanoTime();
                int mobs = 0, sky = 0;
                for (Object o : ws.loadedEntityList)
                {
                    if (!(o instanceof net.minecraft.entity.monster.IMob)) continue;
                    ++mobs;
                    Entity x = (Entity)o;
                    int bx = MathHelper.floor_double(x.posX), bz = MathHelper.floor_double(x.posZ);
                    if (loaded(bx, bz) && MathHelper.floor_double(x.posY) >= ws.getHeightValue(bx, bz) - 1) ++sky;
                }
                System.out.println("GOLDBOT t=" + Oracle.tick + " trek " + where() + " hp=" + sp.getHealth() + " food=" + sp.getFoodStats().getFoodLevel()
                    + " meat=" + count(COOKED_MEAT) + "+" + count(RAW_MEAT) + " time=" + worldTime() + " ms=" + (now - wall) / 1000000
                    + " mobs=" + mobs + " sky=" + sky + " endermen" + b);
                wall = now;
            }
            if (child == FAIL && lastTask instanceof DigTo)
            {
                if (((DigTo)lastTask).what.equals("tunnel")) tunnelFail = Oracle.tick;
                else
                {
                    ++digFails;
                    lastDigFail = Oracle.tick;
                }
            }
            if (child != null && lastTask instanceof CaveHunt)
            {
                CaveHunt h = (CaveHunt)lastTask;
                skip.put(h.target, (long)Oracle.tick + (child == FAIL ? 800 : 0));
                // an enderman in an unwatched chunk comes back with a new id at each load
                if (child == FAIL) skipPos.put(posKey(h.px, h.py, h.pz), (long)Oracle.tick);
            }
            if (child != null) lastTask = null;
            if (child == FAIL) child = null;
            if (chasing >= 0)
            {
                Entity ce = ws.getEntityByID(chasing);
                if (ce == null || ce.isDead || ((EntityLivingBase)ce).getHealth() <= 0)
                {
                    boolean killed = ce != null && ((EntityLivingBase)ce).getHealth() <= 0;
                    if (killed && chasingEnder) ++enderKills;
                    else if (killed) ++others;
                    System.out.println("GOLDBOT t=" + Oracle.tick + " " + (chasingEnder ? "enderman " : "mob ") + chasing + (killed ? " dead" : " gone") + ", pearls " + count(PEARL));
                    chasing = -1;
                }
            }
            long wt = worldTime();
            boolean night = wt >= 11000;
            Post p = trekPost;
            if (p != null && !p.intact())
            {
                System.out.println("GOLDBOT t=" + Oracle.tick + " post at " + p + " broken");
                p = trekPost = null;
            }
            boolean in = p != null && p.in();
            posted = in;
            // seed 1: in water too, or 400 ticks late (legs that failed in
            // a river never set the bot down)
            if (Oracle.tick >= until && !cp.isUsingItem() && (cp.onGround || (s1 && (cp.isInWater() || Oracle.tick >= until + 400))) && mc.currentScreen == null)
            {
                posted = false;
                return DONE;
            }
            if (segNum() >= 23 && mc.currentScreen == null && !cp.isUsingItem())
            {
                // the twelve eyes, once the twelfth pearl is in: six rods make
                // twelve blaze powder, each eye a pearl and a powder
                if (count(PEARL) >= PEARLS_WANTED && count(EYE) < PEARLS_WANTED)
                {
                    if (Tidy.free() < 3) return new Tidy(s1 ? GoldSeed1.TREK_JUNK : Tidy.TREK_JUNK);
                    if (count(POWDER) < count(PEARL) && count(ROD) > 0)
                    {
                        System.out.println("GOLDBOT t=" + Oracle.tick + " blaze powder from " + count(ROD) + " rods at " + where());
                        return new Craft(false, POWDER_R, 1);
                    }
                    int n = Math.min(count(PEARL), count(POWDER));
                    if (n > 0)
                    {
                        System.out.println("GOLDBOT t=" + Oracle.tick + " " + n + " eyes of ender at " + where());
                        return new Craft(false, EYE_R, n);
                    }
                }
                // at the stronghold with the eyes: the walk is over
                double hdx = HOME_X - sp.posX, hdz = HOME_Z - sp.posZ;
                if (count(EYE) >= PEARLS_WANTED && hdx * hdx + hdz * hdz < 24.0 * 24.0 && cp.onGround)
                {
                    System.out.println("GOLDBOT t=" + Oracle.tick + " at the stronghold with " + count(EYE) + " eyes of ender: " + where());
                    posted = false;
                    return DONE;
                }
            }
            // eat (cooked first, raw brought up from the pack)
            int food = sp.getFoodStats().getFoodLevel();
            if (!in || p.sealed())
            {
                Object eat = eat(food <= 14 ? food : 99, new float[] {cp.rotationYaw, cp.rotationPitch});
                if (eat != null) return eat;
            }
            if (in) return postTick(p, night);
            int[] c0 = cell();
            // G25 on: rock or earth within 40 blocks overhead (a tall tree's
            // leaves put the heightmap far above the head too)
            boolean under = !ws.canBlockSeeTheSky(c0[0], c0[1] + 1, c0[2])
                && (segNum() >= 25 ? buried(c0[0], c0[1], c0[2], 40)
                    : buried(c0[0], c0[1], c0[2]) || (segNum() >= 23 && c0[1] < ws.getHeightValue(c0[0], c0[2]) - 8));
            // G27 on: underground only after 60 ticks of it (a walk under an
            // overhang had the bot digging up and walking back for 1,700 ticks)
            underTicks = under ? underTicks + 1 : 0;
            if (segNum() >= 27 && underTicks <= 60) under = false;
            int meat = count(COOKED_MEAT) + count(PORK, BEEF, CHICKEN) + (s1 ? count(GoldSeed1.STEW) : 0);
            // seed 1: animals are scarce around the Nether portal, and the
            // day's hunts for them left no time for the walk
            boolean hungry = segNum() >= 19 && meat < (s1 ? 2 : 4) && !night;
            // seed 1: mushroom stew when the meat has run out (the animals
            // near the stronghold were all eaten and the bot starved; S5 on)
            // (S13 on: only with no animal within 64 and the food bar under
            // 16, and mushrooms of both kinds within 32)
            if (s1k >= 5 && meat < 2 && Oracle.tick - lastStew > 1500 && hotbarOf(DPICK, IPICK, SPICK) >= 0
                && (s1k < 13 ? GoldSeed1.mushrooms() : food < 16 && !animalWithin(64.0) && GoldSeed1.mushroomsNear(32.0)))
            {
                lastStew = Oracle.tick;
                return start(new GoldSeed1.Stew(4));
            }
            // hurt underground: a bunker (G19 on: only with food for the regeneration)
            if (under && sp.getHealth() <= 12.0F && (segNum() < 19 || food >= 18 || count(COOKED_MEAT) + count(PORK, BEEF) > 0)) return new Bunker();
            // G22 on: a pickaxe and a sword on the hotbar (the diamond pickaxe
            // wore out in G21 and stone stopped being diggable)
            if (segNum() >= 22 && Oracle.tick - lastRetool > 600)
            {
                if (hotbarOf(DPICK, IPICK, SPICK) < 0)
                {
                    lastRetool = Oracle.tick;
                    if (count(IPICK) > 0) return new ToHotbar(IPICK);
                    if (count(SPICK) > 0) return new ToHotbar(SPICK);
                    System.out.println("GOLDBOT t=" + Oracle.tick + " no pickaxe: crafting a stone one at " + where());
                    return new RePick();
                }
                if (swordSlot() < 0)
                {
                    lastRetool = Oracle.tick;
                    if (count(ISWORD, SSWORD) > 0) return new ToHotbar(ISWORD, SSWORD);
                    System.out.println("GOLDBOT t=" + Oracle.tick + " no sword: crafting a stone one at " + where());
                    return new ReSword(1);
                }
            }
            // G23 on: wood for sticks (the pickaxes wear out), from a tree by day
            if (segNum() >= 23 && !night && !under && count(PLANKS) + 4 * count(LOG, LOG2) < 8 && Oracle.tick - lastChop > 1200)
            {
                lastChop = Oracle.tick;
                Chop c = new Chop(badTrees);
                if (c.pick())
                {
                    System.out.println("GOLDBOT t=" + Oracle.tick + " wood: a tree at " + c.x + "," + c.y0 + "," + c.z + " from " + where());
                    return c;
                }
            }
            // G19 on: short of meat by day, the surface and its animals first
            if (hungry)
            {
                if (under)
                {
                    if (Oracle.tick - lastWalk < 40) return idle();
                    lastWalk = Oracle.tick;
                    System.out.println("GOLDBOT t=" + Oracle.tick + " meat " + meat + ": up to the animals from " + where());
                    return new DigTo("the surface", (a, b, c) -> b >= ws.getHeightValue(a, c) && stand(a, b, c)
                        && ws.getHeightValue(a + 1, c) <= b + 1 && ws.getHeightValue(a - 1, c) <= b + 1
                        && ws.getHeightValue(a, c + 1) <= b + 1 && ws.getHeightValue(a, c - 1) <= b + 1, sp.posX, 70, sp.posZ);
                }
                // seed 1 from S15: not again for 2,400 ticks after a hunt gave
                // up (three hunts at one unreachable herd took a whole day)
                if (Oracle.tick - lastFood > 300 && animalWithin(64.0) && (s1k < 15 || Oracle.tick - huntGaveUp > 2400))
                {
                    lastFood = Oracle.tick;
                    System.out.println("GOLDBOT t=" + Oracle.tick + " meat " + meat + ": hunting from " + where());
                    return new Hunt(count(RAW_MEAT) + count(COOKED_MEAT) + 6, 1);
                }
            }
            // a cave enderman within reach of a tunnel (on the open surface by day only)
            if (!hungry && (under || !night) && count(PEARL) + count(EYE) < PEARLS_WANTED && sp.getHealth() >= 14.0F && Oracle.tick < until - 600)
            {
                Object h = caveHunt(huntR > 0 ? huntR : night || (segNum() >= 17 && segNum() < 19) ? 128.0 : 64.0);
                if (h != null) return h;
            }
            if (p != null)
            {
                // out of it (the roof keeps a player out): another one
                System.out.println("GOLDBOT t=" + Oracle.tick + " left the post at " + p + ", at " + where());
                trekPost = null;
            }
            // pearls left behind
            if (Oracle.tick - lastPearls > 200 && pearlWithin(8.0))
            {
                lastPearls = Oracle.tick;
                return new Collect(new int[] {PEARL}, 8.0);
            }
            if (under && night && segNum() >= 20 && sp.getHealth() >= 14.0F)
            {
                Object tl = tunnelLeg();
                if (tl != null) return tl;
            }
            if (under)
            {
                if (Oracle.tick - lastWalk < 40) return idle();
                lastWalk = Oracle.tick;
                return new DigTo("the surface", (a, b, c) -> b >= ws.getHeightValue(a, c) && stand(a, b, c)
                    && ws.getHeightValue(a + 1, c) <= b + 1 && ws.getHeightValue(a - 1, c) <= b + 1
                    && ws.getHeightValue(a, c + 1) <= b + 1 && ws.getHeightValue(a, c - 1) <= b + 1, sp.posX, 70, sp.posZ);
            }
            if (night)
            {
                // a segment's run command may ask for the night on a pillar
                // (staring surface endermen into coming) instead
                if (s1 && nightMode.equals("pillar") && Oracle.tick - lastPillar > 300 && Oracle.tick < until - 600 && count(COBBLE) >= 6)
                {
                    lastPillar = Oracle.tick;
                    System.out.println("GOLDBOT t=" + Oracle.tick + " the night on a pillar from " + where());
                    return start(new GoldSeed1.PillarNight(until));
                }
                // seed 1: the night underground, where the tunnel legs and
                // the cave hunts go on (a pillar's census saw one enderman
                // within 64 all night, in a cave)
                if (s1 && Oracle.tick - lastPillar > 300 && Oracle.tick < until - 600 && hotbarOf(DPICK, IPICK, SPICK) >= 0)
                {
                    lastPillar = Oracle.tick;
                    final int[] c1 = cell();
                    System.out.println("GOLDBOT t=" + Oracle.tick + " down for the night from " + where());
                    DigTo down = new DigTo("down for the night", (a, b, c) -> b <= ws.getHeightValue(a, c) - 7 && !ws.canBlockSeeTheSky(a, b + 1, c)
                        && buried(a, b, c, 40), sp.posX + 3, c1[1] - 9, sp.posZ + 3);
                    down.maxNodes = 30000;
                    return start(down);
                }
                // seed 1: no posts (their seal looped), walled in instead
                if (s1)
                {
                    // (S30 on: not again at once when hurt; a wait that ended
                    // before its own night began was pushed every tick)
                    if ((Oracle.tick - lastPillar > 400 || (s1k < 21 && sp.getHealth() < 12.0F)) && worldTime() < 23000L)
                    {
                        lastPillar = Oracle.tick;
                        return start(new GoldSeed1.NightWait());
                    }
                    return idle();
                }
                // the night in a post; hurt, sealed in it
                if (Oracle.tick - lastSite > 60)
                {
                    lastSite = Oracle.tick;
                    int[] s = postSite(10);
                    if (s != null)
                    {
                        healing = sp.getHealth() <= 12.0F;
                        return new BuildPost(s);
                    }
                    System.out.println("GOLDBOT t=" + Oracle.tick + " no post site near " + where());
                }
                return travel(false);
            }
            if (cp.isUsingItem()) return idle();
            // meat: an animal close by while fewer than 8 are carried
            if (count(COOKED_MEAT) + count(PORK, BEEF, CHICKEN) < (s1 ? 3 : 8) && Oracle.tick - lastFood > 600 && animalWithin(40.0) && openSky()
                && (s1k < 17 || Oracle.tick - huntGaveUp > 2400))
            {
                lastFood = Oracle.tick;
                System.out.println("GOLDBOT t=" + Oracle.tick + " meat " + count(COOKED_MEAT) + "+" + count(RAW_MEAT) + ": hunting from " + where());
                return new Hunt(count(RAW_MEAT) + count(COOKED_MEAT) + 3, 1);
            }
            return travel(true);
        }

        /** This tick's eating (cooked meat first, then raw pork or beef, then
         * rotten flesh at 10 or less), a task bringing it to the hotbar, or
         * null when the food bar is above limit or nothing is left. */
        static Object eat(int limit, float[] look)
        {
            int food = sp.getFoodStats().getFoodLevel();
            if (cp.isUsingItem() && food < 20) return new A().look(look).hold("use").j();
            if (food > limit || food >= 20) return null;
            // G25 on: raw chicken too (a hunt that found only chickens left
            // the bot starving beside eight of them)
            // seed 1 from S5: apples and bread after the stew
            int[][] tiers = s1 && s1k >= 5 ? new int[][] {COOKED_MEAT, {GoldSeed1.STEW}, {APPLE, BREAD}, {PORK, BEEF}, {CHICKEN}, {ROTTEN}}
                : s1 ? new int[][] {COOKED_MEAT, {GoldSeed1.STEW}, {PORK, BEEF}, {CHICKEN}, {ROTTEN}}
                : segNum() >= 25 ? new int[][] {COOKED_MEAT, {PORK, BEEF}, {CHICKEN}, {ROTTEN}} : new int[][] {COOKED_MEAT, {PORK, BEEF}, {ROTTEN}};
            for (int[] kinds : tiers)
            {
                if (kinds[0] == ROTTEN && food > 10) break;
                if (count(kinds) == 0) continue;
                int slot = hotbarOf(kinds);
                if (slot < 0) return new ToHotbar(kinds);
                return new A().look(look).hb(slot).hold("use").j();
            }
            return null;
        }

        long lastTunnel = -100000, lastRetool = -100000, lastChop = -100000, tunnelFail = -100000, lastPillar = -100000, lastStew = -100000;
        int underTicks;
        final HashSet<Long> badTrees = new HashSet<Long>();

        /** G20 on: a leg of tunnel toward the stronghold at cave depth, the
         * night's walk underground (and new cave endermen come in range). */
        Object tunnelLeg()
        {
            if (Oracle.tick - lastTunnel < 60 || Oracle.tick >= until - 300) return null;
            // G27 on: not without a pickaxe, nor soon after a tunnel found no route
            if (segNum() >= 27 && (hotbarOf(DPICK, IPICK, SPICK) < 0 || Oracle.tick - tunnelFail < 1200)) return null;
            lastTunnel = Oracle.tick;
            double[] goal = GoldSeed1.trekGoal();
            double dx = goal[0] - sp.posX, dz = goal[1] - sp.posZ, dist = Math.sqrt(dx * dx + dz * dz);
            if (dist < 40.0) return null;
            final double tx = sp.posX + dx / dist * 24.0, tz = sp.posZ + dz / dist * 24.0;
            int[] c0 = cell();
            final int ty = Math.max(28, Math.min(c0[1], ws.getHeightValue(c0[0], c0[2]) - 10));
            final long t0 = Oracle.tick;
            DigTo d = new DigTo("tunnel", (a, b, c) -> (a + 0.5 - tx) * (a + 0.5 - tx) + (c + 0.5 - tz) * (c + 0.5 - tz) < 16.0
                && Math.abs(b - ty) <= 4 && floor(a, b + 2, c) && buried(a, b, c), tx, ty, tz);
            d.maxNodes = 30000;
            d.heur = 6.0;
            d.stop = () -> Oracle.tick >= until || Oracle.tick - t0 > 900;
            System.out.println("GOLDBOT t=" + Oracle.tick + " tunnel toward the stronghold (" + Math.round(dist) + ") from " + where());
            return start(d);
        }

        /** A CaveHunt at the nearest cave enderman, or null. */
        Object caveHunt(double r)
        {
            Entity ce = caveEnder(r);
            if (ce == null) return null;
            lastCave = ce.getEntityId();
            System.out.println("GOLDBOT t=" + Oracle.tick + " cave enderman " + ce.getEntityId() + String.format(" at %.0f,%.0f,%.0f", ce.posX, ce.posY, ce.posZ) + " from " + where());
            CaveHunt h = new CaveHunt(ce);
            if (segNum() >= 17) h.until = until;
            return start(h);
        }

        boolean openSky()
        {
            return ws.canBlockSeeTheSky(MathHelper.floor_double(sp.posX), MathHelper.floor_double(sp.posY) + 1, MathHelper.floor_double(sp.posZ));
        }

        /** A surface leg of up to 40 blocks toward the stronghold. */
        Object travel(boolean day)
        {
            if (roamF != null)
            {
                Object r = roamF.step();
                if (r != null && r != FAIL) return r;
                roamF = null;
                if (r == FAIL) ++roamFails;
            }
            if (Oracle.tick - lastWalk < 20 || Oracle.tick - lastDigFail < 100) return idle();
            lastWalk = Oracle.tick;
            double[] goal = GoldSeed1.trekGoal();
            double dx = goal[0] - sp.posX, dz = goal[1] - sp.posZ, dist = Math.sqrt(dx * dx + dz * dz);
            if (dist < 16.0) return idle();
            double heading = Math.atan2(-dx, dz), len = Math.min(40.0, dist);
            for (int tries = 0; tries < 5; ++tries)
            {
                double h = heading + (tries == 0 ? 0 : (tries % 2 == 1 ? 1 : -1) * ((tries + 1) / 2) * 0.6);
                final double tx = sp.posX - Math.sin(h) * len, tz = sp.posZ + Math.cos(h) * len;
                List<int[]> p = surfacePlan((a, b, c) -> (a + 0.5 - tx) * (a + 0.5 - tx) + (c + 0.5 - tz) * (c + 0.5 - tz) < 36.0
                    && b >= ws.getHeightValue(a, c), 20000, tx, tz);
                if (p != null && p.size() > 3)
                {
                    boolean sprint = count(COOKED_MEAT) >= 8 && sp.getFoodStats().getFoodLevel() >= 18;
                    roamF = new Follow(p, 0.6, sprint);
                    System.out.println("GOLDBOT t=" + Oracle.tick + " trek leg " + Math.round(dist) + " from home, from " + where() + " " + p.size() + " cells");
                    return idle();
                }
            }
            // walled in (a pit, a dug stair): dig out, another way after each failure
            double h = heading + (digFails % 2 == 1 ? 1 : -1) * ((digFails + 1) / 2) * 1.2;
            final double tx = sp.posX - Math.sin(h) * 12.0, tz = sp.posZ + Math.cos(h) * 12.0;
            DigTo d = new DigTo("out", (a, b, c) -> (a + 0.5 - tx) * (a + 0.5 - tx) + (c + 0.5 - tz) * (c + 0.5 - tz) < 16.0
                && b >= ws.getHeightValue(a, c), tx, sp.posY, tz);
            d.maxNodes = 40000;
            return start(d);
        }

        /** In the post: fight at the opening, seal it when hurt, eat, and
         * stare endermen into coming; by day dig the roof out. */
        Object postTick(Post p, boolean night)
        {
            float hp = sp.getHealth();
            int food = sp.getFoodStats().getFoodLevel();
            if (p.sealed())
            {
                Object eat = eat(19, p.rest());
                if (eat != null) return eat;
                if (cp.isUsingItem()) return idle();
                boolean healed = hp >= 20.0F || (hp >= 14.0F && food < 18);
                if (healed || !night || Oracle.tick >= until - 200)
                {
                    int[] m = p.mouth();
                    System.out.println("GOLDBOT t=" + Oracle.tick + " post opened hp=" + hp + " food=" + food);
                    healing = false;
                    return new Mine(m[0], m[1], m[2], true);
                }
                return new A().look(p.rest()).j();
            }
            if (!night)
            {
                System.out.println("GOLDBOT t=" + Oracle.tick + " dawn: out of the post at " + p);
                trekPost = null;
                posted = false;
                return new Mine(p.x, p.y + 2, p.z, true);
            }
            // something to hit at the opening
            Object hit = hitAtOpening(p);
            if (hit != null) return hit;
            if ((hp <= 10.0F || (healing && hp < 18.0F)) && Oracle.tick < until - 400)
            {
                System.out.println("GOLDBOT t=" + Oracle.tick + " post sealed hp=" + hp);
                return new PlaceAt(COBBLE, COBBLE, p.mouth(), null);
            }
            Object eat = eat(16, p.rest());
            if (eat != null) return eat;
            if (cp.isUsingItem()) return idle();
            // a cave enderman: a tunnel down from the post (nothing follows
            // through its one-high opening)
            if (count(PEARL) + count(EYE) < PEARLS_WANTED && hp >= 16.0F && Oracle.tick < until - 600)
            {
                Object h = caveHunt(128.0);
                if (h != null) return h;
            }
            // an enderman after the player: wait for it, the eyes off it
            float[] rest = p.rest();
            boolean after = false;
            for (Object o : ws.loadedEntityList)
            {
                Entity x = (Entity)o;
                if (!enderman(x) || !aggressive(x)) continue;
                after = true;
                if (stare(rest[0], rest[1], x)) rest[1] = 70.0F;
            }
            if (after) return new A().look(rest).hb(swordSlot()).j();
            // else the nearest one in sight through the opening: a stare turns it
            Entity te = null;
            float[] tl = null;
            double bd = 64.0 * 64.0;
            for (Object o : ws.loadedEntityList)
            {
                Entity x = (Entity)o;
                if (!enderman(x) || skipped(x) || !nearLoaded(x)) continue;
                double d = x.getDistanceSqToEntity(sp);
                if (d >= bd) continue;
                float[] l = lookAt(x.posX, x.boundingBox.minY + x.height / 2.0F, x.posZ);
                if (!stare(l[0], l[1], x)) continue;
                bd = d;
                te = x;
                tl = l;
            }
            if (te != null)
            {
                if (te.getEntityId() != target)
                {
                    target = te.getEntityId();
                    stareStart = Oracle.tick;
                    System.out.println("GOLDBOT t=" + Oracle.tick + " staring at enderman " + target + String.format(" %.1f blocks off", Math.sqrt(bd)));
                }
                if (Oracle.tick - stareStart > 120) skip.put(target, (long)Oracle.tick);
                return new A().look(tl).hb(swordSlot()).j();
            }
            target = -1;
            // G20 on: nothing to turn from here: the night's walk, underground
            if (segNum() >= 20 && night && hp >= 16.0F && (count(PEARL) + count(EYE) < PEARLS_WANTED || segNum() >= 23))
            {
                Object leg = tunnelLeg();
                if (leg != null)
                {
                    // G27 on: the post stays the trek's until the dig leaves it
                    if (segNum() < 27)
                    {
                        trekPost = null;
                        posted = false;
                    }
                    return leg;
                }
            }
            return new A().look(rest).hb(swordSlot()).j();
        }

        /** A hostile or an enderman the crosshair reaches from the post: hit
         * it (an enderman on the legs, never on its middle). */
        Object hitAtOpening(Post p)
        {
            Entity best = null;
            float[] bl = null;
            double bd = 1e9;
            for (Object o : ws.loadedEntityList)
            {
                Entity x = (Entity)o;
                boolean ender = enderman(x);
                if (!ender && !(x instanceof EntityLivingBase && hostile(x))) continue;
                double d = x.getDistanceSq(cp.posX, cp.posY, cp.posZ);
                if (d > 25.0 || d >= bd) continue;
                Entity ce = mc.theWorld.getEntityByID(x.getEntityId());
                if (ce == null) continue;
                double[] fys = ender ? new double[] {0.15, 0.25, 0.08, 0.35, 0.05} : new double[] {0.5, 0.8, 0.25, 0.95, 0.1};
                for (double fy : fys)
                {
                    float[] l = lookAt(ce.posX, ce.boundingBox.minY + ce.height * fy, ce.posZ);
                    if (ender && stare(l[0], l[1], x)) continue;
                    if (pointed(l) != ce) continue;
                    best = x;
                    bl = l;
                    bd = d;
                    break;
                }
            }
            if (best == null) return null;
            if (chasing != best.getEntityId())
            {
                chasing = best.getEntityId();
                chasingEnder = enderman(best);
            }
            A a = new A().look(bl).hb(swordSlot());
            if (((EntityLivingBase)best).hurtResistantTime <= 10) a.press("attack");
            return a.j();
        }
    }

    /** G22 on: a stone pickaxe (three cobble, two sticks) at a table, placed
     * from the planks when none stands near, as ReSword makes a sword. */
    static final class RePick extends Task
    {
        int phase;

        Object tick()
        {
            switch (phase)
            {
                case 0:
                {
                    phase = 1;
                    if (count(PLANKS) >= 6 || count(LOG, LOG2) == 0) return idle();
                    Craft c = new Craft(false, PLANKS_ALL, 1);
                    c.fresh = trekNum() >= 23;
                    return c;
                }
                case 1:
                    phase = 2;
                    if (count(STICK) >= 2) return idle();
                    if (count(PLANKS) < 2) return FAIL;
                    return new Craft(false, STICKS, 1);
                case 2:
                    if (count(STICK) < 2 || count(COBBLE) < 3) return FAIL;
                    if (findBlock(new int[] {TABLE}, 5) != null)
                    {
                        phase = 5;
                        return idle();
                    }
                    phase = 3;
                    if (count(TABLE) >= 1) return idle();
                    if (count(PLANKS) < 4) return FAIL;
                    return new Craft(false, TABLE_R, 1);
                case 3:
                    if (count(TABLE) < 1) return FAIL;
                    phase = 4;
                    return new ToHotbar(TABLE);
                case 4:
                    phase = 5;
                    return new PlaceAny(TABLE, TABLE);
                case 5:
                    if (child == FAIL) return FAIL;
                    phase = 6;
                    return new Craft(true, SPICK_R, 1);
                case 6:
                    if (count(SPICK) < 1) return FAIL;
                    phase = 7;
                    if (hotbarOf(SPICK) >= 0) return idle();
                    return new ToHotbar(SPICK);
                case 7:
                {
                    // G27 on: the table comes along (four planks a time ran the wood out)
                    int[] tb = segNum() >= 27 ? findBlock(new int[] {TABLE}, 5) : null;
                    if (tb == null)
                    {
                        System.out.println("GOLDBOT t=" + Oracle.tick + " stone pickaxe in slot " + hotbarOf(SPICK));
                        return DONE;
                    }
                    phase = 8;
                    return new Mine(tb[0], tb[1], tb[2], false);
                }
                case 8:
                    phase = 9;
                    return new Collect(new int[] {TABLE}, 6.0);
                default:
                    System.out.println("GOLDBOT t=" + Oracle.tick + " stone pickaxe in slot " + hotbarOf(SPICK));
                    return DONE;
            }
        }
    }

    static final class SegHunt extends Seg
    {
        final int want, ticks;

        SegHunt(int want, int ticks) { this.want = want; this.ticks = ticks; }

        Object tick()
        {
            switch (phase)
            {
                case 0:
                    if (segNum() < 15)
                    {
                        // G10..G14 as recorded
                        need(armorWorn() == 4 && swordSlot() >= 0, "no armor or sword");
                        phase = 1;
                        if (count(COOKED_MEAT) >= 3) return idle();
                        return new Provision();
                    }
                    if (armorWorn() < 4) System.out.println("GOLDBOT t=" + Oracle.tick + " hunting with " + armorWorn() + " armor pieces");
                    phase = 10;
                    if (segNum() >= 16)
                    {
                        // a hunt that ended in a cave: back to the sky first
                        int[] c0 = cell();
                        if (c0[1] < ws.getHeightValue(c0[0], c0[2]) - 2 && !ws.canBlockSeeTheSky(c0[0], c0[1] + 1, c0[2]))
                        {
                            phase = 11;
                            return new DigTo("the surface", (a, b, c) -> b >= ws.getHeightValue(a, c), sp.posX, 70, sp.posZ);
                        }
                    }
                    // a worn iron sword: stone ones in the pack to follow it
                    {
                        int hb = hotbarOf(ISWORD);
                        ItemStack is = hb >= 0 ? cp.inventory.mainInventory[hb] : null;
                        int left = is == null ? 0 : is.getMaxDamage() - is.getItemDamage();
                        if (count(SSWORD) < 2 && left < 80) return new ReSword(2 - count(SSWORD));
                    }
                    return idle();
                case 11:
                    phase = 10;
                    {
                        int hb = hotbarOf(ISWORD);
                        ItemStack is = hb >= 0 ? cp.inventory.mainInventory[hb] : null;
                        int left = is == null ? 0 : is.getMaxDamage() - is.getItemDamage();
                        if (count(SSWORD) < 2 && left < 80) return new ReSword(2 - count(SSWORD));
                    }
                    return idle();
                case 10:
                    need(swordSlot() >= 0, "no sword");
                    phase = 1;
                    if (count(COOKED_MEAT) >= 3) return idle();
                    return new Provision();
                case 1:
                    phase = 2;
                    return new EnderHunt(want, segStart + ticks);
                case 2:
                    phase = 3;
                    return new Idle(3);
                default:
                    return DONE;
            }
        }
    }

    /** G4: iron ore, flint from gravel, smelt, iron pickaxe, bucket, flint and steel. */
    static final class SegG4 extends Seg
    {
        int[] furnace;

        Object tick()
        {
            switch (phase)
            {
                case 0:
                    furnace = findBlock(new int[] {FURNACE, LIT_FURNACE}, 16);
                    need(furnace != null, "no furnace near the start");
                    phase = 1;
                    return new OreLoop(new int[] {IRON_ORE}, new int[] {IRON_ORE}, 7, 6, 90);
                case 1:
                    need(child != FAIL, "no iron");
                    phase = 2;
                    return new GetFlint();
                case 2:
                    need(child != FAIL, "no flint");
                    phase = 3;
                    return new Walk("furnace", (a, b, c) -> Math.abs(a - furnace[0]) + Math.abs(c - furnace[2]) <= 3 && Math.abs(b - furnace[1]) <= 1, 0.3)
                        .hint(furnace[0] + 0.5, furnace[2] + 0.5);
                case 3:
                    need(child != FAIL, "furnace not reached");
                    phase = 30;
                    // G3 left cooked meat in the output slot
                    return new FurnaceTake(furnace);
                case 30:
                    phase = 4;
                    return new FurnaceLoad(furnace, new int[] {IRON_ORE}, COAL, 1);
                case 4:
                    need(child != FAIL, "furnace not loaded");
                    phase = 5;
                    return new WaitSmelt(furnace, 7);
                case 5:
                    phase = 6;
                    return new FurnaceTake(furnace);
                case 6:
                    need(count(IRON) >= 7, "no iron ingots");
                    phase = 7;
                    return new Craft(true, STICKS, 1, IPICK_R, 1, BUCKET_R, 1, FLINT_STEEL_R, 1);
                case 7:
                    need(child != FAIL && count(IPICK) == 1 && count(BUCKET) == 1 && count(FLINT_STEEL) == 1, "iron kit not crafted");
                    phase = 8;
                    return new Idle(3);
                default:
                    return DONE;
            }
        }
    }

    static void dbgArea()
    {
        int[] c = cell();
        for (Object o : ws.loadedEntityList)
        {
            Entity e = (Entity)o;
            if (e.getDistanceSq(sp.posX, sp.posY, sp.posZ) < 100) System.out.println("DBG entity " + e.getClass().getSimpleName() + " " + e.getEntityId() + String.format(" %.2f,%.2f,%.2f", e.posX, e.posY, e.posZ));
        }
        for (int y = c[1] + 8; y >= c[1] - 4; --y)
        {
            System.out.println("DBG y=" + y);
            for (int z = c[2] - 6; z <= c[2] + 6; ++z)
            {
                StringBuilder b = new StringBuilder("DBG ");
                for (int x = c[0] - 6; x <= c[0] + 8; ++x)
                {
                    int i = id(x, y, z);
                    char ch = x == c[0] && y == c[1] && z == c[2] ? '@' : i == 8 || i == 9 ? 'w' : i == 10 || i == 11 ? 'L' : stand(x, y, z) ? 's' : pass(x, y, z) ? '.' : i == 1 ? '#' : i == 3 || i == 2 ? 'd' : i == 13 ? 'g' : i == 61 ? 'F' : 'x';
                    b.append(ch);
                }
                System.out.println(b);
            }
        }
    }
}
