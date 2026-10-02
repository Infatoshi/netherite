package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.io.BufferedOutputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import net.minecraft.block.Block;
import net.minecraft.entity.Entity;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.item.EntityXPOrb;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTBase;
import net.minecraft.nbt.NBTTagByte;
import net.minecraft.nbt.NBTTagByteArray;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagDouble;
import net.minecraft.nbt.NBTTagFloat;
import net.minecraft.nbt.NBTTagInt;
import net.minecraft.nbt.NBTTagIntArray;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.nbt.NBTTagLong;
import net.minecraft.nbt.NBTTagShort;
import net.minecraft.nbt.NBTTagString;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.world.IWorldAccess;
import net.minecraft.world.World;
import net.minecraft.world.WorldServer;

/**
 * Every drop of every block, the reference for the native port of
 * Block.dropBlockAsItemWithChance and dropXpOnBlockBreak.
 *
 * One case: the probe places a block in a raw region far from spawn (flag 2,
 * no neighbor notification and no random tick), seeds the world Random, calls
 * dropBlockAsItemWithChance(meta, 1.0F, fortune) and records every entity that
 * appeared. The block goes back and every entity the case made is removed
 * before the next case, so cases cannot see each other; after the sweep the
 * region is compared against a snapshot taken before it and the run fails if a
 * single id or meta moved.
 *
 * Two Random streams feed a case. The world Random, seeded to the case's
 * opseed, holds quantityDroppedWithBonus and getItemDropped's draws and then
 * the three nextFloat() of every dropBlockAsItem_do offset, so each item of a
 * case lands on its own spot. Math.random holds the EntityItem/EntityXPOrb
 * constructor: hoverStart, rotationYaw, motionX and motionZ per EntityItem and
 * rotationYaw, motionX, motionY and motionZ per EntityXPOrb, so each entity
 * carries its own motion and yaw. Every entity of a case is recorded in full
 * (item, damage, count, canonical NBT, xp value, position, motion, yaw).
 *
 * Math.random is Det.mathRandom(), one java.util.Random per thread role. The
 * probe runs on its own thread (the OTHER role) and installs its own Random as
 * Det.math[OTHER], seeded to math_seed, for the length of the sweep, so the
 * native side reproduces the entity motion and yaw exactly by starting a
 * java.util.Random at math_seed and spending four nextDouble() per entity in
 * case order, not as a follow-up. spawn_draws is how many next(bits) calls
 * that Random handed out for one case (eight per entity on a JDK whose
 * nextDouble takes two), kept as a cross-check of the entity count.
 *
 * Output DIR/manifest.json, DIR/cases.bin, DIR/lines.jsonl. See the layout
 * strings in the manifest.
 */
public final class DropsProbe
{
    /**
     * Blocks whose drop path is not a function of the block, the meta and the
     * world Random, so a synthetic world cannot reproduce them:
     *   air               dropBlockAsItemWithChance is empty (recorded for shape only)
     *   monster_egg       spawns an EntitySilverfish and drops nothing
     *   piston_head       BlockPistonMoving needs its TileEntityPiston
     *   piston_extension  same: the block it moves is what drops
     */
    static final int[] EXTERIOR_MUTATOR = {0, 97, 34, 36};

    /** The metas every block of one id is probed with. */
    static final int METAS = 16;
    /** The fortune levels the sweep covers. 4 and up split into two stacks by
     * dropXpOnBlockBreak's getXPSplit loop and are left to DropsXp. */
    static final int FORTUNES = 5;

    /** One case record: 36 bytes. */
    static final int CASE_BYTES = 36;

    /** The drop location, in world coordinates. */
    static final int DX = 2002, DY = 100, DZ = 2002;

    /**
     * The world Random's seed per case. A spread, not a twin: leaves drop with
     * a 1-in-20 and a 1-in-200 draw, gravel flint with 1-in-10, so one seed
     * would leave the interesting branches unexercised. The first entry is the
     * OPSEED the lane was asked to run with.
     */
    static long[] opseeds(JsonObject cmd)
    {
        long s = cmd.has("opseed") ? cmd.get("opseed").getAsLong() : 3L;

        if (cmd.has("seedcount"))
        {
            int n = cmd.get("seedcount").getAsInt();
            long[] a = new long[n];

            for (int i = 0; i < n; ++i) a[i] = s + i;

            return a;
        }

        // The rare branches need a spread, not a twin: leaves roll 1-in-20 and
        // then 1-in-200, gravel flint 1-in-10, and the height-scaled crops roll
        // 1-in-15 per item. 16 consecutive seeds reach all of them.
        long[] a = new long[16];

        for (int i = 0; i < a.length; ++i) a[i] = s + i;

        return a;
    }

    private DropsProbe() {}

    static JsonObject run(final IntegratedServer server, final JsonObject cmd) throws Exception
    {
        final JsonObject[] result = new JsonObject[1];
        final Exception[] error = new Exception[1];
        Thread t = new Thread(new Runnable()
        {
            public void run()
            {
                try
                {
                    result[0] = dump(server, cmd);
                }
                catch (Exception e)
                {
                    error[0] = e;
                }
            }
        }, "Oracle Drops");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        if ("xp".equals(cmd.has("kind") ? cmd.get("kind").getAsString() : "drops"))
        {
            return xp(server, cmd);
        }

        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();
        WorldServer ws = server.worldServers[0];
        long seed = ws.getSeed();
        long[] opseeds = opseeds(cmd);
        Probe.rawChunks = true;

        // The 3x3 chunks the drop location reaches. The loads are raw
        // generation (the spawn chunks are already there) and draw nothing.
        load(ws, DX >> 4, DZ >> 4);

        // Snapshot: the sweep must leave the region exactly as it found it.
        int[] before = snapshot(ws, DX >> 4, DZ >> 4);

        Counted own = new Counted(MathStream.MATH_SEED);
        MATH = own;
        MathStream math = new MathStream(own);
        math.install();

        List<byte[]> cases = new ArrayList<byte[]>();
        List<JsonObject> lines = new ArrayList<JsonObject>();
        int[] blockIds = ids();

        try
        {
            for (int id : blockIds)
            {
                if (isExterior(id)) continue;

                for (int meta = 0; meta < METAS; ++meta)
                {
                    for (int fortune = 0; fortune < FORTUNES; ++fortune)
                    {
                        for (long opseed : opseeds)
                        {
                            one(ws, id, meta, fortune, opseed, cases, lines);
                        }
                    }
                }
            }
        }
        finally
        {
            math.uninstall(own);
        }

        int[] after = snapshot(ws, DX >> 4, DZ >> 4);

        if (!java.util.Arrays.equals(before, after))
        {
            throw new IllegalStateException("the sweep changed the region: " + where(before, after));
        }

        DataOutputStream cb = new DataOutputStream(new BufferedOutputStream(new FileOutputStream(new File(dir, "cases.bin")), 1 << 16));

        for (byte[] c : cases) cb.write(c);
        cb.close();

        PrintWriter lw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "lines.jsonl")), "UTF-8"));

        for (JsonObject l : lines) lw.println(l.toString());
        lw.close();

        JsonArray seeds = new JsonArray();

        for (long s : opseeds) seeds.add(new JsonPrimitive(s));

        JsonObject m = new JsonObject();
        m.addProperty("seed", seed);
        m.addProperty("kind", "drops");
        m.addProperty("x", DX);
        m.addProperty("y", DY);
        m.addProperty("z", DZ);
        m.addProperty("metas", METAS);
        m.addProperty("fortunes", FORTUNES);
        m.add("opseeds", seeds);
        m.addProperty("cases", cases.size());
        m.addProperty("lines", lines.size());
        m.addProperty("case_layout", "36 bytes per case: x int32 LE, y int32 LE, z int32 LE, block_id uint16 LE, meta uint8, fortune uint8, opseed int64 LE, item uint16 LE (0xffff when the case dropped no item, 0xfffe when an item was dropped but the game has no such item), flag uint8 (bit 0: an entity that is neither an item nor an orb was spawned, bit 2: an item was dropped but the game has no id for it, bit 3: the placement itself spawned something), items uint8 (item entities), damage uint16 LE (of the first item), spawn_draws uint16 LE, total uint16 LE (item entities plus the xp values), entities uint16 LE");
        m.addProperty("case_order", "block id ascending, then meta 0..15, then fortune 0..4, then opseed in the manifest's order");
        m.addProperty("blocks", blockIds.length - EXTERIOR_MUTATOR.length);
        m.addProperty("exterior_mutator", join(EXTERIOR_MUTATOR));
        m.addProperty("exterior_mutator_reason", "not a function of the block, the meta and the world Random: air's drop is empty (shape only), monster_egg spawns an EntitySilverfish, piston_head and piston_extension need the TileEntityPiston the place-and-remove cycle cannot make");
        m.addProperty("metas_probed", "every registered block is placed at metas 0..15 with World.setBlock; a case the world rejects (setBlock returned false, so the block did not change) is skipped and has no record");
        m.addProperty("placement", "each case places its block with World.setBlock(flag 2) and takes out whatever the placement itself spawned, then puts the Math.random stream back where it was; it does the same around the restoring setBlock to air, whose breakBlock lets a flower pot, a jukebox or a chest drop what it holds. Both draw from the shared stream, so a case's draws are exactly its own: the placement's entity count is the line's place_ents and flag bit 3, and spawn_draws is the case's own");
        m.addProperty("drop_draws", "per case, from the world Random seeded to opseed: quantityDroppedWithBonus first, then per item nextFloat() and, when the item is not null, getItemDropped's own draws, then damageDropped");
        m.addProperty("spawn_draws", "Math.random next(bits) calls the case's entities consumed, 0 when none: each EntityItem draws hoverStart, rotationYaw, motionX, motionZ and each EntityXPOrb draws rotationYaw, motionX, motionY, motionZ from math[OTHER], which the probe has replaced with a java.util.Random seeded to math_seed, so the native side starts that seed and spends four nextDouble() per entity in case order");
        m.addProperty("math_seed", MathStream.MATH_SEED);
        m.addProperty("position", "each entity of a case has its own position from the world Random (three nextFloat per dropBlockAsItem_do) and its own motion and yaw from the math stream; the line holds one entry per entity in spawn order");
        m.addProperty("nbt", "canonical NBT of ItemStack.writeToNBT of the dropped stack, the same text nbtjson.c parses");
        PrintWriter mw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        mw.println(m.toString());
        mw.close();

        JsonObject res = new JsonObject();
        res.addProperty("dir", dir.getPath());
        res.addProperty("cases", cases.size());
        res.addProperty("lines", lines.size());
        return res;
    }

    /**
     * The dropXpOnBlockBreak sweep: the same location, Block.dropXpOnBlockBreak
     * called with every amount from 1 to the cap, the world Random seeded the
     * same way. dropXpOnBlockBreak draws nothing from the world Random, so the
     * record is the split itself: the orbs, their values, their positions and
     * their motion. getXPSplit's thresholds (1, 3, 7, 17, 37, 73, 149, 307,
     * 617, 1237, 2477) are what the native side has to reproduce.
     *
     * Output is lines.jsonl only (one line per amount, 1..amount), with
     * case_layout "xp":
     *   {"amount":N,"n":<orbs>,"sum":<xp>,"values":[v1,v2,...],"math_seed":S,
     *    "ents":[{"kind":"xp","xp":v,"x":<bits>,...}, ...]}
     */
    static JsonObject xp(IntegratedServer server, JsonObject cmd) throws Exception
    {
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();
        WorldServer ws = server.worldServers[0];
        int amount = cmd.has("amount") ? cmd.get("amount").getAsInt() : 3000;
        Probe.rawChunks = true;
        load(ws, DX >> 4, DZ >> 4);

        int[] before = snapshot(ws, DX >> 4, DZ >> 4);
        Counted own = new Counted(MathStream.MATH_SEED);
        MATH = own;
        MathStream math = new MathStream(own);
        math.install();

        List<JsonObject> lines = new ArrayList<JsonObject>();

        try
        {
            for (int v = 1; v <= amount; ++v)
            {
                Capture capture = new Capture();
                ws.addWorldAccess(capture);
                int drawnBefore = mathState();

                try
                {
                    XP.breakXp(ws, DX, DY, DZ, v);
                }
                finally
                {
                    ws.removeWorldAccess(capture);
                }

                int spawnDraws = mathState() - drawnBefore;
                List<Entity> spawned = capture.spawned;

                for (Entity e : spawned) takeOut(ws, e);

                JsonArray ents = new JsonArray();
                JsonArray values = new JsonArray();
                int sum = 0;

                for (Entity e : spawned)
                {
                    if (!(e instanceof EntityXPOrb)) throw new IllegalStateException("amount " + v + " made a " + e.getClass().getSimpleName());

                    EntityXPOrb xo = (EntityXPOrb)e;
                    JsonObject o = new JsonObject();
                    o.addProperty("kind", "xp");
                    o.addProperty("xp", xo.getXpValue());
                    doubles(o, e);
                    ents.add(o);
                    values.add(new JsonPrimitive(xo.getXpValue()));
                    sum += xo.getXpValue();
                }

                JsonObject l = new JsonObject();
                l.addProperty("amount", v);
                l.addProperty("n", spawned.size());
                l.addProperty("sum", sum);
                l.addProperty("spawn_draws", spawnDraws);
                l.add("values", values);
                l.add("ents", ents);
                lines.add(l);
            }
        }
        finally
        {
            math.uninstall(own);
        }

        int[] after = snapshot(ws, DX >> 4, DZ >> 4);

        if (!java.util.Arrays.equals(before, after)) throw new IllegalStateException("the xp sweep changed the region: " + where(before, after));

        PrintWriter lw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "lines.jsonl")), "UTF-8"));

        for (JsonObject l : lines) lw.println(l.toString());
        lw.close();

        JsonObject m = new JsonObject();
        m.addProperty("seed", ws.getSeed());
        m.addProperty("kind", "xp");
        m.addProperty("x", DX);
        m.addProperty("y", DY);
        m.addProperty("z", DZ);
        m.addProperty("amount", amount);
        m.addProperty("lines", lines.size());
        m.addProperty("math_seed", MathStream.MATH_SEED);
        m.addProperty("layout", "lines.jsonl, one line per amount 1..amount: {\"amount\":N,\"n\":<orb count>,\"sum\":<total xp>,\"spawn_draws\":<Math.random draws>,\"values\":[v...],\"ents\":[{\"kind\":\"xp\",\"xp\":v,\"x\":<raw double bits>,...}]}; the values are getXPSplit's splits of N, in order, and an orb's position is 0.5 + the block coordinate on every axis");
        m.addProperty("math_seed_note", "each orb draws rotationYaw, motionX, motionY, motionZ from the math stream, which is a java.util.Random seeded to math_seed and spent in order across the whole sweep; the native side starts the same seed and spends spawn_draws doubles per line");
        PrintWriter mw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        mw.println(m.toString());
        mw.close();

        JsonObject res = new JsonObject();
        res.addProperty("dir", dir.getPath());
        res.addProperty("lines", lines.size());
        return res;
    }

    /**
     * One case: place, seed, drop, record, remove, restore. A case the world
     * rejects (World.setBlock returns false: the block did not change) is not
     * recorded at all.
     *
     * The placement is watched too. A rail, a torch, fire, redstone wire or a
     * lit redstone torch cannot stay on bare terrain, so the world drops one
     * and the drop's EntityItem draws from the shared Math.random stream. Those
     * draws belong to the world's own reaction, not to the case, so the probe
     * takes the placement's entities out and rewinds the stream to its state
     * before the placement: what a case spends is then exactly its own drops.
     * The placement's entity count is recorded, because it is evidence of the
     * world's behavior (and it is why a case's block may read back as air).
     */
    static void one(WorldServer ws, int id, int meta, int fortune, long opseed,
                    List<byte[]> cases, List<JsonObject> lines) throws Exception
    {
        Block b = Block.getBlockById(id);
        Capture capture = new Capture();
        ws.addWorldAccess(capture);

        long mathBefore = Det.state(MATH);
        int drawnBefore = mathState();
        boolean placed;

        try
        {
            placed = ws.setBlock(DX, DY, DZ, b, meta, 2);
        }
        finally
        {
            ws.removeWorldAccess(capture);
        }

        int placeEnts = capture.spawned.size();

        for (Entity e : capture.spawned) takeOut(ws, e);

        capture.spawned.clear();
        rewind(MATH, mathBefore);

        if (!placed) return;

        int placedId = Block.getIdFromBlock(ws.getBlock(DX, DY, DZ));
        int placedMeta = ws.getBlockMetadata(DX, DY, DZ);

        // Nothing may be left in the world between cases: the world's own
        // removeEntity only marks an entity dead and leaves the sweep to the
        // tick, so takeOut below does the removal itself. The capture happens
        // on the spawn call, through a world access, so it is exact.
        ws.addWorldAccess(capture);

        ws.rand.setSeed(opseed);
        Random savedRand = ws.rand;
        CountedRandom world = new CountedRandom(ws.rand);
        ws.rand = world;
        drawnBefore = mathState();

        try
        {
            b.dropBlockAsItemWithChance(ws, DX, DY, DZ, placedMeta, 1.0F, fortune);
        }
        finally
        {
            ws.removeWorldAccess(capture);
            ws.rand = savedRand;
        }

        int spawnDraws = mathState() - drawnBefore;
        int worldDraws = world.draws;

        List<Entity> spawned = capture.spawned;

        for (Entity e : spawned) takeOut(ws, e);

        // The restore must not reach the next case either: replacing the block
        // with air calls its breakBlock, and a flower pot, a jukebox or a
        // chest drops what it holds there, drawing from the same stream. The
        // stream goes back to where the case left it, and the restore's own
        // entities do not stay in the world.
        long afterDrop = Det.state(MATH);
        Capture restore = new Capture();
        ws.addWorldAccess(restore);

        try
        {
            ws.setBlock(DX, DY, DZ, Block.getBlockById(0), 0, 2);
        }
        finally
        {
            ws.removeWorldAccess(restore);
        }

        for (Entity e : restore.spawned) takeOut(ws, e);

        rewind(MATH, afterDrop);

        if (spawnDraws < 0 || spawnDraws > 65535)
        {
            throw new IllegalStateException("case " + id + ":" + meta + ":" + fortune + " drew " + spawnDraws + " Math.random values");
        }

        int itemId = 0xffff, flag = 0, total = 0, damage = 0;
        JsonArray ents = new JsonArray();
        int items = 0;

        for (Entity e : spawned)
        {
            JsonObject o = new JsonObject();

            if (e instanceof EntityItem)
            {
                EntityItem ei = (EntityItem)e;
                ItemStack st = ei.getEntityItem();
                Item it = st.getItem();
                int sid = it == null ? 0xffff : Item.getIdFromItem(it);

                if (it == null || Item.getItemById(sid) == null)
                {
                    // the block's own item was deleted (a block with no item
                    // registration): the drop exists, the game has no id for it
                    sid = it == null ? 0xffff : 0xfffe;
                    flag |= 4;
                }
                else if (items == 0)
                {
                    itemId = sid;
                    damage = st.getItemDamage();
                }

                ++items;
                ++total;
                o.addProperty("kind", "item");
                o.addProperty("id", sid);
                o.addProperty("damage", st.getItemDamage());
                o.addProperty("count", st.stackSize);
                o.add("nbt", canon(st.writeToNBT(new NBTTagCompound())));
                doubles(o, e);
            }
            else if (e instanceof EntityXPOrb)
            {
                EntityXPOrb xo = (EntityXPOrb)e;
                total += xo.getXpValue();
                o.addProperty("kind", "xp");
                o.addProperty("xp", xo.getXpValue());
                doubles(o, e);
            }
            else
            {
                flag |= 1;
                o.addProperty("kind", "other");
                o.addProperty("class", e.getClass().getSimpleName());
            }

            ents.add(o);
        }

        if (placeEnts != 0) flag |= 8;

        byte[] c = new byte[CASE_BYTES];
        le32(c, 0, DX);
        le32(c, 4, DY);
        le32(c, 8, DZ);
        c[12] = (byte)id;
        c[13] = (byte)(id >> 8);
        c[14] = (byte)meta;
        c[15] = (byte)fortune;
        le64(c, 16, opseed);
        c[24] = (byte)itemId;
        c[25] = (byte)(itemId >> 8);
        c[26] = (byte)flag;
        c[27] = (byte)items;
        le16(c, 28, damage);
        le16(c, 30, spawnDraws);
        le16(c, 32, total);
        le16(c, 34, spawned.size());
        cases.add(c);

        JsonObject l = new JsonObject();
        l.addProperty("case", cases.size() - 1);
        l.addProperty("block", id);
        l.addProperty("meta", meta);
        l.addProperty("fortune", fortune);
        l.addProperty("opseed", opseed);
        l.addProperty("placed", placedId);
        l.addProperty("placed_meta", placedMeta);
        l.addProperty("place_ents", placeEnts);
        l.addProperty("item", itemId == 0xffff ? -1 : itemId);
        l.addProperty("damage", damage);
        l.addProperty("items", items);
        l.addProperty("total", total);
        l.addProperty("spawn_draws", spawnDraws);
        l.addProperty("flag", flag);
        l.addProperty("world_draws", worldDraws);
        l.add("ents", ents);
        lines.add(l);
    }

    /** The position, motion and rotation of one spawned entity, as raw bits. */
    static void doubles(JsonObject o, Entity e)
    {
        o.addProperty("x", Double.doubleToRawLongBits(e.posX));
        o.addProperty("y", Double.doubleToRawLongBits(e.posY));
        o.addProperty("z", Double.doubleToRawLongBits(e.posZ));
        o.addProperty("mx", Double.doubleToRawLongBits(e.motionX));
        o.addProperty("my", Double.doubleToRawLongBits(e.motionY));
        o.addProperty("mz", Double.doubleToRawLongBits(e.motionZ));
        o.addProperty("yaw", Float.floatToRawIntBits(e.rotationYaw));
    }

    /** A java.util.Random that records how many values it handed out, so a case's
     * Math.random draw count is exact rather than a seed difference (the seed
     * wraps at 48 bits). */
    static final class Counted extends Random
    {
        int draws;

        Counted(long seed) { super(seed); }

        @Override protected int next(int bits)
        {
            ++draws;
            return super.next(bits);
        }

        @Override public synchronized double nextGaussian()
        {
            ++draws;
            return super.nextGaussian();
        }
    }

    /** The probe's own Math.random stream, installed as Det's OTHER stream for
     * the sweep. */
    static Counted MATH;

    static int mathState()
    {
        return MATH.draws;
    }

    /** {id} of every registered block, in id order. */
    static int[] ids()
    {
        List<Integer> out = new ArrayList<Integer>();

        for (int id = 0; id < 4096; ++id)
        {
            if (!Block.blockRegistry.containsID(id)) continue;
            out.add(Integer.valueOf(id));
        }

        int[] a = new int[out.size()];

        for (int i = 0; i < a.length; ++i) a[i] = out.get(i).intValue();

        return a;
    }

    static boolean isExterior(int id)
    {
        for (int x : EXTERIOR_MUTATOR)
        {
            if (x == id) return true;
        }

        return false;
    }

    static String join(int[] a)
    {
        StringBuilder b = new StringBuilder();

        for (int i = 0; i < a.length; ++i)
        {
            if (i != 0) b.append(',');
            b.append(a[i]);
        }

        return b.toString();
    }

    static void load(WorldServer ws, int cx, int cz)
    {
        for (int x = cx - 1; x <= cx + 1; ++x)
        {
            for (int z = cz - 1; z <= cz + 1; ++z)
            {
                if (ws.getChunkFromChunkCoords(x, z) == null) throw new IllegalStateException("chunk (" + x + "," + z + ") did not load");
            }
        }
    }

    /** ids * 16 + metas of the 3x3 chunks around (cx,cz), chunk-pair major. */
    static int[] snapshot(WorldServer ws, int cx, int cz)
    {
        int[] a = new int[9 * 65536];

        for (int x = -1; x <= 1; ++x)
        {
            for (int z = -1; z <= 1; ++z)
            {
                int base = ((x + 1) * 3 + (z + 1)) * 65536;

                for (int i = 0; i < 65536; ++i)
                {
                    int wx = (cx + x) * 16 + (i >> 12), wy = i & 255, wz = (cz + z) * 16 + ((i >> 8) & 15);
                    a[base + i] = Block.getIdFromBlock(ws.getBlock(wx, wy, wz)) * 16 + ws.getBlockMetadata(wx, wy, wz);
                }
            }
        }

        return a;
    }

    static String where(int[] a, int[] b)
    {
        for (int i = 0; i < a.length; ++i)
        {
            if (a[i] != b[i])
            {
                return "chunk " + ((i >> 16) / 3 - 1) + "," + ((i >> 16) % 3 - 1) + " cell " + (i & 65535)
                    + " (x " + ((i & 65535) >> 12) + ", y " + ((i & 65535) & 255) + ", z " + (((i & 65535) >> 8) & 15) + ")"
                    + " want " + (a[i] >> 4) + ":" + (a[i] & 15) + " got " + (b[i] >> 4) + ":" + (b[i] & 15);
            }
        }

        return "?";
    }

    /** Puts a java.util.Random back to a 48-bit state Det.state handed out, for
     * the placement draws a case must not inherit. */
    static void rewind(Random r, long state) throws Exception
    {
        if (seedField == null)
        {
            seedField = Random.class.getDeclaredField("seed");
            seedField.setAccessible(true);
        }

        ((java.util.concurrent.atomic.AtomicLong)seedField.get(r)).set(state);
    }

    static Field seedField;

    static void le16(byte[] a, int o, int v)
    {
        a[o] = (byte)v;
        a[o + 1] = (byte)(v >> 8);
    }

    static void le32(byte[] a, int o, int v)
    {
        for (int i = 0; i < 4; ++i) a[o + i] = (byte)(v >> (8 * i));
    }

    static void le64(byte[] a, int o, long v)
    {
        le32(a, o, (int)v);
        le32(a, o + 4, (int)(v >> 32));
    }

    static Field field(Class<?> c, String name) throws Exception
    {
        Field f = c.getDeclaredField(name);
        f.setAccessible(true);
        return f;
    }

    /**
     * Det's OTHER Math.random stream, replaced by the probe's own so the
     * artifact's entity motion is reproducible: the native side starts a
     * java.util.Random from MATH_SEED and spends four nextDouble() per entity
     * in case order. The field is picked by name: Det has two static
     * Random[ROLES] arrays (seeder and math) and reflection order is not a
     * contract, so a search by type would sometimes swap the seeder stream
     * instead, which is what the first run of this probe did.
     */
    static final class MathStream
    {
        static final long MATH_SEED = 0x6d6174684f7468L;

        final Random mine;
        Random saved;

        MathStream(Random mine)
        {
            this.mine = mine;
            mine.setSeed(MATH_SEED);
        }

        static Field mathField() throws Exception
        {
            Field f = Det.class.getDeclaredField("math");
            f.setAccessible(true);

            if (!Random[].class.isAssignableFrom(f.getType())) throw new IllegalStateException("Det.math is not a Random[]");

            return f;
        }

        void install() throws Exception
        {
            Random[] v = (Random[])mathField().get(null);

            if (v.length != Det.ROLES) throw new IllegalStateException("Det.math has " + v.length + " roles");
            saved = v[Det.OTHER];
            v[Det.OTHER] = mine;
        }

        void uninstall(Random own) throws Exception
        {
            Random[] v = (Random[])mathField().get(null);

            if (v[Det.OTHER] == mine) v[Det.OTHER] = saved;
        }
    }

    /** Keeps every entity a case spawns, in spawn order, straight from the world's
     * own onEntityCreate fan-out. Nothing else sees it: added for one call and
     * removed right after. */
    static final class Capture implements IWorldAccess
    {
        final List<Entity> spawned = new ArrayList<Entity>();

        public void onEntityCreate(Entity e) { spawned.add(e); }
        public void onEntityDestroy(Entity e) {}
        public void markBlockForUpdate(int x, int y, int z) {}
        public void markBlockForRenderUpdate(int x, int y, int z) {}
        public void markBlockRangeForRenderUpdate(int x0, int y0, int z0, int x1, int y1, int z1) {}
        public void playSound(String s, double x, double y, double z, float v, float p) {}
        public void playSoundToNearExcept(EntityPlayer p, String s, double x, double y, double z, float v, float q) {}
        public void spawnParticle(String s, double x, double y, double z, double a, double b, double c) {}
        public void playRecord(String s, int x, int y, int z) {}
        public void broadcastSound(int a, int b, int c, int d, int e) {}
        public void playAuxSFX(EntityPlayer p, int a, int x, int y, int z, int v) {}
        public void destroyBlockPartially(int a, int x, int y, int z, int v) {}
        public void onStaticEntitiesChanged() {}
    }

    /** Counts the values a Random hands out and forwards every draw to the stream
     * it wraps, so substituting it for World.rand changes nothing but the
     * count. Used to report how many world-Random values one case spent. */
    static final class CountedRandom extends Random
    {
        final Random inner;
        int draws;

        CountedRandom(Random inner)
        {
            super(0L);
            this.inner = inner;
        }

        @Override public int nextInt(int n) { ++draws; return inner.nextInt(n); }
        @Override public float nextFloat() { ++draws; return inner.nextFloat(); }
        @Override public double nextDouble() { ++draws; return inner.nextDouble(); }
        @Override public int nextInt() { ++draws; return inner.nextInt(); }
        @Override public long nextLong() { ++draws; return inner.nextLong(); }
        @Override public boolean nextBoolean() { ++draws; return inner.nextBoolean(); }
        @Override public void nextBytes(byte[] b) { ++draws; inner.nextBytes(b); }
        @Override public double nextGaussian() { ++draws; return inner.nextGaussian(); }

        /** Random's own constructor calls this before inner is assigned. */
        @Override public synchronized void setSeed(long s)
        {
            if (inner != null) inner.setSeed(s);
        }
    }

    /** Takes an entity out of the world completely: the chunk's list and the
     * loaded list both lose it now. World.removeEntity only marks it dead and
     * leaves the sweep to the tick; watchEntities on the tracker is the only
     * other holder, and nothing ticks while the server is parked. */
    static void takeOut(WorldServer ws, Entity e)
    {
        e.setDead();

        if (e.addedToChunk)
        {
            net.minecraft.world.chunk.Chunk c = ws.getChunkFromChunkCoords(e.chunkCoordX, e.chunkCoordZ);

            if (c != null) c.removeEntity(e);
        }

        ws.loadedEntityList.remove(e);
    }

    static final class XP extends Block
    {
        XP() { super(net.minecraft.block.material.Material.rock); }

        /** Block.dropXpOnBlockBreak is protected; this subclass is the caller. */
        static void breakXp(World w, int x, int y, int z, int amount)
        {
            new XP().dropXpOnBlockBreak(w, x, y, z, amount);
        }
    }

    /** Canonical JSON for one NBT tag, no whitespace and no list element type. */
    static JsonElement canon(NBTBase tag)
    {
        switch (tag.getId())
        {
            case 1: return new JsonPrimitive("b:" + ((NBTTagByte)tag).func_150290_f());
            case 2: return new JsonPrimitive("s:" + ((NBTTagShort)tag).func_150289_e());
            case 3: return new JsonPrimitive("i:" + ((NBTTagInt)tag).func_150287_d());
            case 4: return new JsonPrimitive("l:" + ((NBTTagLong)tag).func_150291_c());
            case 5: return new JsonPrimitive("f:" + hex(Float.floatToRawIntBits(((NBTTagFloat)tag).func_150288_h()) & 4294967295L, 8));
            case 6: return new JsonPrimitive("d:" + hex(Double.doubleToRawLongBits(((NBTTagDouble)tag).func_150286_g()), 16));
            case 7:
            {
                byte[] a = ((NBTTagByteArray)tag).func_150292_c();
                StringBuilder b = new StringBuilder("ba:");

                for (int i = 0; i < a.length; ++i)
                {
                    if (i != 0) b.append(',');
                    b.append(a[i]);
                }

                return new JsonPrimitive(b.toString());
            }
            case 8: return new JsonPrimitive("str:" + ((NBTTagString)tag).func_150285_a_());
            case 9:
            {
                JsonArray out = new JsonArray();

                for (Object o : list((NBTTagList)tag)) out.add(canon((NBTBase)o));

                return out;
            }
            case 10:
            {
                NBTTagCompound c = (NBTTagCompound)tag;
                String[] keys = (String[])c.func_150296_c().toArray(new String[0]);
                java.util.Arrays.sort(keys);
                JsonObject out = new JsonObject();

                for (String k : keys) out.add(k, canon(c.getTag(k)));

                return out;
            }
            case 11:
            {
                int[] a = ((NBTTagIntArray)tag).func_150302_c();
                StringBuilder b = new StringBuilder("ia:");

                for (int i = 0; i < a.length; ++i)
                {
                    if (i != 0) b.append(',');
                    b.append(a[i]);
                }

                return new JsonPrimitive(b.toString());
            }
            default: throw new IllegalStateException("cannot canonicalize NBT tag id " + tag.getId());
        }
    }

    static List list(NBTTagList tag)
    {
        try
        {
            return (List)field(NBTTagList.class, "tagList").get(tag);
        }
        catch (Exception e)
        {
            throw new IllegalStateException("NBTTagList.tagList", e);
        }
    }

    static String hex(long bits, int digits)
    {
        StringBuilder b = new StringBuilder();

        for (int i = digits - 1; i >= 0; --i) b.append("0123456789abcdef".charAt((int)((bits >>> (i * 4)) & 15L)));

        return b.toString();
    }
}