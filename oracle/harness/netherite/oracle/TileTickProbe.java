package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.io.BufferedOutputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.IdentityHashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.zip.GZIPOutputStream;
import net.minecraft.block.Block;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
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
import net.minecraft.tileentity.MobSpawnerBaseLogic;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.tileentity.TileEntityBeacon;
import net.minecraft.tileentity.TileEntityBrewingStand;
import net.minecraft.tileentity.TileEntityChest;
import net.minecraft.tileentity.TileEntityEnderChest;
import net.minecraft.tileentity.TileEntityEnchantmentTable;
import net.minecraft.tileentity.TileEntityFurnace;
import net.minecraft.tileentity.TileEntityHopper;
import net.minecraft.tileentity.TileEntityMobSpawner;
import net.minecraft.tileentity.TileEntityNote;
import net.minecraft.tileentity.TileEntityPiston;
import net.minecraft.world.World;
import net.minecraft.world.WorldServer;
import net.minecraft.world.chunk.Chunk;

/**
 * Tile entity tick probe: the tile entity half of World.updateEntities, run
 * against a region of raw chunks (Probe.rawChunks, no population) scattered
 * with every tile entity class whose updateEntity changes server state in
 * 1.7.10 with this config.yaml.
 *
 * The setups are built with setBlock(flag 2) writes on a slot grid (stride
 * 13, y 80), so every tile entity the walk later sees was demand-created by a
 * recorded write and the world's loadedTileEntityList (field_147482_g) order
 * is the creation order. Furnaces hold every fuel against every other
 * smeltable with stack sizes that run out mid-way, daylight detectors sit
 * exposed and under roofs while the world's time runs through a sunset,
 * hoppers hold items (they only store: config.yaml redstone is off), chests
 * are single, double and trapped, and beacons sit on pyramids of each size.
 * One piston tile entity (a TileEntityPiston set over a BlockPistonMoving
 * block) finishes moving three ticks in, so the walk's removal of an invalid
 * tile entity runs. Mob spawners are placed but no player is ever within
 * their activation range, so updateSpawner returns before touching anything
 * (its countdown never runs, server side, without a player).
 *
 * Each tick advances the world the way WorldServer.tick does (the
 * skylightSubtracted recomputation from the current time, then both time
 * counters), then the tile entity pass of World.updateEntities runs verbatim
 * over the reflected World fields: updateEntity on every valid entity whose
 * block still exists, iterator removal of the invalid ones, the
 * addedTileEntityList merge into the tail of the list.
 *
 * Per tick the run records the Rows.onBlock write stream, every tile
 * entity's canonical NBT that changed (in walk order), the walk order
 * itself, and an FNV hash of the 3x3 chunks around every chunk a write
 * landed in. The two StrictMath.cos-derived values the pass reads,
 * skylightSubtracted and the celestial angle table, are recorded per tick:
 * glibc's cos is not fdlibm's, and they are the only inputs the native port
 * cannot recompute bit for bit. Nothing in the pass draws from a Random.
 *
 * Runs on its own thread (the OTHER role, like Probe) while the server is
 * parked, so no CLIENT or SERVER RNG stream moves.
 *
 * Output DIR/manifest.json, DIR/build.bin.gz, DIR/init.jsonl.gz,
 * DIR/ticks.bin, DIR/writes.bin.gz, DIR/nbt.jsonl.gz, DIR/order.bin.gz,
 * DIR/chunkhash.bin.gz, DIR/final.bin.gz. See the layout strings in the
 * manifest.
 */
public final class TileTickProbe
{
    /** One fuel: the item id, damage and burn value func_145952_a gives it. */
    static final class Fuel
    {
        final String name;
        final int item, damage;

        Fuel(String name, int item, int damage)
        {
            this.name = name;
            this.item = item;
            this.damage = damage;
        }
    }

    /** Every fuel func_145952_a names, with the burn times it returns. */
    static final Fuel[] FUELS = {
        new Fuel("coal", 263, 0),
        new Fuel("coal_block", 173, 0),
        new Fuel("planks", 5, 0),
        new Fuel("wooden_slab", 126, 0),
        new Fuel("log", 17, 0),
        new Fuel("lava_bucket", 327, 0),
        new Fuel("sapling", 6, 0),
        new Fuel("blaze_rod", 369, 0),
        new Fuel("stick", 280, 0),
        new Fuel("wooden_pickaxe", 270, 0),
        new Fuel("wooden_sword", 268, 0),
        new Fuel("wooden_hoe", 290, 0),
    };

    /** One input of every smeltable, damage 0 (entries 20 and 21, the fish, key on damage). */
    static final int[] IN = {15, 14, 12, 4, 392, 319, 363, 365, 337, 87, 81, 17, 82};

    private TileTickProbe() {}

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
        }, "Oracle Tile Tick Probe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    // ------------------------------------------------------------- reflect

    static Field fTicking, fAdded, fRemove;
    static Field fFurnaceSlots, fHopperSlots, fHopperCooldown, fChestSlots, fBrewSlots, fNote;
    static Field fChestPlayers, fChestLid, fChestPrev, fChestTicks;
    static Field fEnderPlayers, fEnderLid, fEnderPrev, fEnderTicks;

    static void reflect() throws Exception
    {
        fTicking = World.class.getDeclaredField("field_147481_N");
        fTicking.setAccessible(true);
        fAdded = World.class.getDeclaredField("field_147484_a");
        fAdded.setAccessible(true);
        fRemove = World.class.getDeclaredField("field_147483_b");
        fRemove.setAccessible(true);
        fFurnaceSlots = field(TileEntityFurnace.class, "field_145957_n");
        fHopperSlots = field(TileEntityHopper.class, "field_145900_a");
        fHopperCooldown = field(TileEntityHopper.class, "field_145901_j");
        fChestSlots = field(TileEntityChest.class, "field_145985_p");
        fBrewSlots = field(TileEntityBrewingStand.class, "field_145945_j");
        fNote = field(TileEntityNote.class, "field_145879_a");
        fChestPlayers = field(TileEntityChest.class, "field_145987_o");
        fChestLid = field(TileEntityChest.class, "field_145989_m");
        fChestPrev = field(TileEntityChest.class, "field_145986_n");
        fChestTicks = field(TileEntityChest.class, "field_145983_q");
        fEnderPlayers = field(TileEntityEnderChest.class, "field_145973_j");
        fEnderLid = field(TileEntityEnderChest.class, "field_145972_a");
        fEnderPrev = field(TileEntityEnderChest.class, "field_145975_i");
        fEnderTicks = field(TileEntityEnderChest.class, "field_145974_k");
    }

    static Field field(Class<?> c, String name) throws Exception
    {
        Field f = c.getDeclaredField(name);
        f.setAccessible(true);
        return f;
    }

    // ------------------------------------------------- the walk, verbatim

    /** The tile entity half of World.updateEntities, verbatim, over the
     * probe's own tile entities. The vanilla walk runs on
     * ws.field_147482_g, but that list also carries whatever the join's
     * populated spawn chunks left (a mineshaft or dungeon chest far outside
     * the region), which this run neither seeds nor checks; the pass runs on
     * this list instead, holding the probe's tile entities in creation
     * order, and the merge at the end lands in it too. The World fields
     * field_147481_N and field_147484_a are still driven through reflection,
     * so a write that lands during the pass sees the vanilla
     * getTileEntity behavior. */
    static List teWalk;

    static void tickTileEntities(WorldServer ws) throws Exception
    {
        List teList = teWalk;
        List added = (List)fAdded.get(ws);
        List remove = (List)fRemove.get(ws);
        fTicking.setBoolean(ws, true);
        Iterator it = teList.iterator();

        while (it.hasNext())
        {
            TileEntity te = (TileEntity)it.next();

            if (!te.isInvalid() && te.hasWorldObj() && ws.blockExists(te.field_145851_c, te.field_145848_d, te.field_145849_e))
            {
                te.updateEntity();
            }

            if (te.isInvalid())
            {
                it.remove();

                if (ws.theChunkProviderServer.chunkExists(te.field_145851_c >> 4, te.field_145849_e >> 4))
                {
                    ws.getChunkFromChunkCoords(te.field_145851_c >> 4, te.field_145849_e >> 4)
                        .removeTileEntity(te.field_145851_c & 15, te.field_145848_d, te.field_145849_e & 15);
                }
            }
        }

        fTicking.setBoolean(ws, false);

        if (!remove.isEmpty())
        {
            teList.removeAll(remove);
            remove.clear();
        }

        if (!added.isEmpty())
        {
            for (int i = 0; i < added.size(); ++i)
            {
                TileEntity te = (TileEntity)added.get(i);

                if (!te.isInvalid() && teSlotOf.containsKey(te))
                {
                    if (!teList.contains(te))
                    {
                        teList.add(te);
                    }

                    if (ws.theChunkProviderServer.chunkExists(te.field_145851_c >> 4, te.field_145849_e >> 4))
                    {
                        ws.getChunkFromChunkCoords(te.field_145851_c >> 4, te.field_145849_e >> 4)
                            .func_150812_a(te.field_145851_c & 15, te.field_145848_d, te.field_145849_e & 15, te);
                    }

                    ws.func_147471_g(te.field_145851_c, te.field_145848_d, te.field_145849_e);
                }
            }

            added.clear();
        }
    }

    // ------------------------------------------------------- canonical NBT

    static Field fTagList;

    @SuppressWarnings("unchecked")
    static List tagListGet(NBTTagList list)
    {
        try
        {
            if (fTagList == null)
            {
                fTagList = NBTTagList.class.getDeclaredField("tagList");
                fTagList.setAccessible(true);
            }

            return (List)fTagList.get(list);
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
    }

    static NBTBase tagAt(NBTTagList list, int i)
    {
        return (NBTBase)tagListGet(list).get(i);
    }

    /** Canonical text of one NBT tag, the form csrc/engine/nbtjson.c renders:
     * a compound is {"k":v,...} with keys in String.compareTo order, a list
     * is [...], and a scalar is its type-tagged string quoted. No
     * whitespace anywhere. */
    static String canonText(NBTBase tag)
    {
        switch (tag.getId())
        {
            case 1: return "\"b:" + ((NBTTagByte)tag).func_150290_f() + "\"";
            case 2: return "\"s:" + ((NBTTagShort)tag).func_150289_e() + "\"";
            case 3: return "\"i:" + ((NBTTagInt)tag).func_150287_d() + "\"";
            case 4: return "\"l:" + ((NBTTagLong)tag).func_150291_c() + "\"";
            case 5: return String.format("\"f:%08x\"", Float.floatToRawIntBits(((NBTTagFloat)tag).func_150288_h()) & 4294967295L);
            case 6: return String.format("\"d:%016x\"", Double.doubleToRawLongBits(((NBTTagDouble)tag).func_150286_g()));
            case 7:
            {
                byte[] a = ((NBTTagByteArray)tag).func_150292_c();
                StringBuilder b = new StringBuilder("\"ba:");

                for (int i = 0; i < a.length; ++i)
                {
                    if (i != 0) b.append(',');
                    b.append(a[i]);
                }

                return b.append('"').toString();
            }
            case 8: return "\"str:" + ((NBTTagString)tag).func_150285_a_() + "\"";
            case 9:
            {
                StringBuilder b = new StringBuilder("[");
                NBTTagList list = (NBTTagList)tag;

                for (int i = 0; i < list.tagCount(); ++i)
                {
                    if (i != 0) b.append(',');
                    b.append(canonText(tagAt(list, i)));
                }

                return b.append(']').toString();
            }
            case 10:
            {
                NBTTagCompound c = (NBTTagCompound)tag;
                String[] keys = (String[])c.func_150296_c().toArray(new String[0]);
                java.util.Arrays.sort(keys);
                StringBuilder out = new StringBuilder("{");

                for (int i = 0; i < keys.length; ++i)
                {
                    if (i != 0) out.append(',');
                    out.append('"').append(keys[i]).append("\":").append(canonText(c.getTag(keys[i])));
                }

                return out.append('}').toString();
            }
            case 11:
            {
                int[] a = ((NBTTagIntArray)tag).func_150302_c();
                StringBuilder b = new StringBuilder("\"ia:");

                for (int i = 0; i < a.length; ++i)
                {
                    if (i != 0) b.append(',');
                    b.append(a[i]);
                }

                return b.append('"').toString();
            }
            default: throw new IllegalStateException("cannot canonicalize NBT tag id " + tag.getId());
        }
    }

    /** A TE's writeToNBT as canonical text. */
    static String teCanon(TileEntity te)
    {
        NBTTagCompound tag = new NBTTagCompound();
        te.writeToNBT(tag);
        return canonText(tag);
    }

    /** The fields TileEntityChest and TileEntityEnderChest's updateEntity move
     * (numPlayersUsing, the lid angle and its previous value, and the tick
     * counter the 200/80-tick branches test) with the tick they hold after
     * it, as one canonical NBT line. writeToNBT carries none of them, so a
     * chest whose only state is the lid would be invisible in nbt.jsonl.gz.
     * The tick is in the line because one is written per chest per tick; the
     * key order is the canonical one. writeState's lines go to
     * lid.jsonl.gz, the setups' (the initial state, tick 0) to
     * lidinit.jsonl.gz. */
    static String lidCanon(TileEntity te, int tick) throws Exception
    {
        int players, counter;
        float lid, prev;
        String id;

        if (te instanceof TileEntityChest)
        {
            id = "Chest";
            players = fChestPlayers.getInt(te);
            lid = fChestLid.getFloat(te);
            prev = fChestPrev.getFloat(te);
            counter = fChestTicks.getInt(te);
        }
        else if (te instanceof TileEntityEnderChest)
        {
            id = "EnderChest";
            players = fEnderPlayers.getInt(te);
            lid = fEnderLid.getFloat(te);
            prev = fEnderPrev.getFloat(te);
            counter = fEnderTicks.getInt(te);
        }
        else
        {
            return null;
        }

        return "{\"counter\":\"i:" + counter + "\",\"id\":\"str:" + id + "\",\"lid\":\"" + floatText(lid)
            + "\",\"players\":\"i:" + players + "\",\"prev\":\"" + floatText(prev) + "\",\"tick\":\"i:" + tick
            + "\",\"x\":\"i:" + te.field_145851_c + "\",\"y\":\"i:" + te.field_145848_d + "\",\"z\":\"i:"
            + te.field_145849_e + "\"}";
    }

    /** A float as canonical text: the raw bits, like canonText's case 5. */
    static String floatText(float v)
    {
        return String.format("f:%08x", Float.floatToRawIntBits(v) & 4294967295L);
    }

    // ------------------------------------------------------------ recording

    static OutputStream buildOut;
    static OutputStream traceOut;
    static OutputStream writeOut;
    static PrintWriter nbtOut;
    static OutputStream orderOut;
    static OutputStream hashOut;
    static PrintWriter lidOut;
    static PrintWriter lidInitOut;
    static boolean recBuild;
    static final List<int[]> tickWrites = new ArrayList<int[]>();

    static void recordBuild(int x, int y, int z, int id, int meta) throws IOException
    {
        byte[] b = new byte[12];

        le32(b, 0, x);
        le32(b, 4, y);
        le32(b, 8, z);
        buildOut.write(b);
        buildOut.write(new byte[] {(byte)id, (byte)(id >> 8), (byte)meta, 0});
    }

    static void recordWrite(int x, int y, int z, int id, int meta) throws IOException
    {
        byte[] b = new byte[12];

        le32(b, 0, x);
        le32(b, 4, y);
        le32(b, 8, z);
        writeOut.write(b);
        writeOut.write(new byte[] {(byte)id, (byte)(id >> 8), (byte)meta, 0});
        tickWrites.add(new int[] {x >> 4, z >> 4});
    }

    static void le32(byte[] a, int o, int v)
    {
        for (int i = 0; i < 4; ++i) a[o + i] = (byte)(v >> (8 * i));
    }

    static void le64(byte[] a, int o, long v)
    {
        for (int i = 0; i < 8; ++i) a[o + i] = (byte)(v >> (8 * i));
    }

    static void writeLe32(OutputStream o, int v) throws IOException
    {
        o.write(v & 255);
        o.write(v >> 8 & 255);
        o.write(v >> 16 & 255);
        o.write(v >> 24 & 255);
    }

    /** One gzip stream the tick loop writes record-shaped data into. */
    static final class DataGZ
    {
        final OutputStream out;

        DataGZ(File f) throws IOException
        {
            out = new BufferedOutputStream(new GZIPOutputStream(new FileOutputStream(f), 1 << 16), 1 << 20);
        }

        /** A second constructor for the one plain (ungzipped) stream. */
        DataGZ(File f, boolean plain) throws IOException
        {
            if (!plain) throw new IllegalArgumentException("use the one-arg form for gzip");
            out = new BufferedOutputStream(new FileOutputStream(f), 1 << 16);
        }

        void raw(byte[] b) throws IOException
        {
            out.write(b);
        }

        void u16(int v) throws IOException
        {
            out.write(v & 255);
            out.write(v >> 8 & 255);
        }

        void u32(int v) throws IOException
        {
            out.write(v & 255);
            out.write(v >> 8 & 255);
            out.write(v >> 16 & 255);
            out.write(v >> 24 & 255);
        }

        void close() throws IOException
        {
            out.close();
        }
    }

    // ------------------------------------------------------------- the dump

    static final int SX = 8, SY = 9;
    static final int SLOT_STRIDE = 13, SLOT_Y = 80;
    static final int PISTON_SLOT = 47;

    static WorldServer ws;
    static TileEntity[][] tes;
    static TileEntity pistonTE;
    static Map<TileEntity,String> lastNbt = new IdentityHashMap<TileEntity,String>();
    static Map<TileEntity,Integer> teSlotOf = new IdentityHashMap<TileEntity,Integer>();

    static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        int cx = cmd.get("cx").getAsInt(), cz = cmd.get("cz").getAsInt();
        int radius = cmd.has("radius") ? cmd.get("radius").getAsInt() : 3;
        int ring = cmd.has("ring") ? cmd.get("ring").getAsInt() : 2;
        int ticks = cmd.has("ticks") ? cmd.get("ticks").getAsInt() : 3000;
        long timestart = cmd.has("timestart") ? cmd.get("timestart").getAsLong() : 10000L;
        boolean trace = cmd.has("trace") && cmd.get("trace").getAsBoolean();
        int slotLimit = cmd.has("slots") ? cmd.get("slots").getAsInt() : SX * SY;
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();

        ws = server.worldServers[0];
        long seed = ws.getSeed();
        Probe.rawChunks = true;

        int x0 = cx - radius - ring, x1 = cx + radius + ring;
        int z0 = cz - radius - ring, z1 = cz + radius + ring;
        JsonArray loaded = new JsonArray();

        for (int lx = x0; lx <= x1; ++lx)
        {
            for (int lz = z0; lz <= z1; ++lz)
            {
                if (ws.getChunkFromChunkCoords(lx, lz) == null) throw new IllegalStateException("chunk (" + lx + "," + lz + ") did not load");
                JsonArray pair = new JsonArray();
                pair.add(new JsonPrimitive(lx));
                pair.add(new JsonPrimitive(lz));
                loaded.add(pair);
            }
        }

        reflect();

        ws.skylightSubtracted = ws.calculateSkylightSubtracted(1.0F);
        ws.getWorldInfo().setWorldTime(timestart);
        ws.getWorldInfo().incrementTotalWorldTime(timestart);

        OutputStream bOut = new BufferedOutputStream(new GZIPOutputStream(new FileOutputStream(new File(dir, "build.bin.gz")), 1 << 16), 1 << 20);
        PrintWriter initOut = new PrintWriter(new OutputStreamWriter(new GZIPOutputStream(new FileOutputStream(new File(dir, "init.jsonl.gz")), 1 << 16), "UTF-8"));
        PrintWriter lidInitW = new PrintWriter(new OutputStreamWriter(new GZIPOutputStream(new FileOutputStream(new File(dir, "lidinit.jsonl.gz")), 1 << 16), "UTF-8"));
        lidInitOut = lidInitW;
        DataGZ ticksOut = new DataGZ(new File(dir, "ticks.bin"), true);
        DataGZ writesOut = new DataGZ(new File(dir, "writes.bin.gz"));
        PrintWriter nbtW = new PrintWriter(new OutputStreamWriter(new GZIPOutputStream(new FileOutputStream(new File(dir, "nbt.jsonl.gz")), 1 << 16), "UTF-8"));
        PrintWriter lidW = new PrintWriter(new OutputStreamWriter(new GZIPOutputStream(new FileOutputStream(new File(dir, "lid.jsonl.gz")), 1 << 16), "UTF-8"));
        DataGZ orderG = new DataGZ(new File(dir, "order.bin.gz"));
        DataGZ hashG = new DataGZ(new File(dir, "chunkhash.bin.gz"));
        buildOut = bOut;
        traceOut = trace ? new java.io.FileOutputStream(new File(dir, "trace.txt")) : null;
        writeOut = writesOut.out;
        nbtOut = nbtW;
        lidOut = lidW;
        orderOut = orderG.out;
        hashOut = hashG.out;
        Rows.writeListener = new Rows.WriteListener()
        {
            public void onWrite(World w, int x, int y, int z, int id, int meta)
            {
                try
                {
                    if (recBuild)
                    {
                        recordBuild(x, y, z, id, meta);

                        if (trace)
                        {
                            Chunk c = ws.getChunkFromChunkCoords(x >> 4, z >> 4);

                            try
                            {
                                StringBuilder sb = new StringBuilder("TTWRITE " + x + "," + y + "," + z + " id "
                                    + (id & 0xffff) + " meta " + meta + " hm "
                                    + c.heightMap[(z & 15) << 4 | (x & 15)]);

                                for (int yy = 60; yy <= 92; ++yy)
                                    sb.append(" ").append(c.getSavedLightValue(net.minecraft.world.EnumSkyBlock.Sky,
                                        x & 15, yy, z & 15));

                                if (x == 600 && z == 600) traceOut.write((sb.toString() + "\n").getBytes("UTF-8"));
                            }
                            catch (IOException e)
                            {
                                throw new RuntimeException(e);
                            }
                        }
                    }
                    else recordWrite(x, y, z, id, meta);
                }
                catch (IOException e)
                {
                    throw new RuntimeException(e);
                }
            }
        };
        recBuild = true;

        int slots = SX * SY;
        tes = new TileEntity[slots][];
        int[] p = new int[3];

        for (int s = 0; s < slotLimit; ++s)
        {
            slotAt(cx, cz, radius, s, p);
            setup(s, p[0], p[1], p[2], initOut);
        }

        // one TileEntityPiston over a BlockPistonMoving block, so the walk
        // removes an invalid tile entity once the move finishes
        slotAt(cx, cz, radius, PISTON_SLOT, p);
        ws.setBlock(p[0], p[1] - 1, p[2], Blocks.piston_extension, 0, 2);
        pistonTE = new TileEntityPiston(Blocks.stone, 0, 1, false, false);
        ws.setTileEntity(p[0], p[1] - 1, p[2], pistonTE);
        teSlotOf.put(pistonTE, Integer.valueOf(0xffff));
        initOut.println(teCanon(pistonTE));
        initOut.close();
        lidInitW.close();
        lidInitOut = null;
        recBuild = false;

        // the walk list: every tile entity the builds made, in creation
        // order (the piston entity last), which is the order the vanilla
        // field_147482_g would hold if the join had left nothing
        teWalk = new ArrayList<TileEntity>();

        for (int s = 0; s < slotLimit; ++s)
            for (TileEntity te : tes[s]) teWalk.add(te);

        teWalk.add(pistonTE);

        // the reference state before the first pass
        writeState(ticksOut, orderG, hashG, 0);

        for (int t = 1; t <= ticks; ++t)
        {
            tickWrites.clear();

            // WorldServer.tick's time half, minus everything the walk does not
            // see: the skylight recomputation from the current time, then both
            // time counters
            ws.skylightSubtracted = ws.calculateSkylightSubtracted(1.0F);
            ws.getWorldInfo().incrementTotalWorldTime(ws.getWorldInfo().getWorldTotalTime() + 1L);
            ws.getWorldInfo().setWorldTime(ws.getWorldInfo().getWorldTime() + 1L);

            tickTileEntities(ws);
            writeState(ticksOut, orderG, hashG, t);
        }

        Rows.writeListener = null;
        bOut.close();
        if (traceOut != null) traceOut.close();
        traceOut = null;
        ticksOut.close();
        writesOut.close();
        nbtW.close();
        lidW.close();
        orderG.close();
        hashG.close();
        buildOut = null;
        writeOut = null;
        nbtOut = null;
        lidOut = null;
        orderOut = null;
        hashOut = null;

        // final.bin.gz: the whole region after the last tick, the setblock
        // probe's chunk bytes
        Field gap = field(Chunk.class, "isGapLightingUpdated");
        DataOutputStream out = new DataOutputStream(new GZIPOutputStream(new FileOutputStream(new File(dir, "final.bin.gz")), 1 << 16));
        byte[] cols = new byte[256];
        byte[] buf = new byte[Probe.CHUNK_BYTES];

        for (int lx = x0; lx <= x1; ++lx)
        {
            for (int lz = z0; lz <= z1; ++lz)
            {
                Chunk c = ws.getChunkFromChunkCoords(lx, lz);
                Probe.fillChunkBytes(c, buf);
                writeLe32(out, lx);
                writeLe32(out, lz);
                out.write(buf);

                for (int i = 0; i < 256; ++i) cols[i] = (byte)(c.updateSkylightColumns[i] ? 1 : 0);
                out.write(cols);
                out.write(gap.getBoolean(c) ? 1 : 0);
            }
        }

        out.close();

        // the class census over the walk list, before the piston finished
        int[] census = new int[CENSUS_NAMES.length];

        for (int s = 0; s < slotLimit; ++s)
            for (TileEntity te : tes[s]) ++census[censusKey(te)];

        JsonObject m = new JsonObject();
        m.addProperty("seed", seed);
        m.addProperty("kind", "tileticks");
        m.addProperty("cx", cx);
        m.addProperty("cz", cz);
        m.addProperty("radius", radius);
        m.addProperty("ring", ring);
        m.addProperty("ticks", ticks);
        m.addProperty("timestart", timestart);
        m.addProperty("slots", slots);
        m.addProperty("piston_slot", PISTON_SLOT);
        m.addProperty("order", "cx-major: for lx in " + x0 + ".." + x1 + ", for lz in " + z0 + ".." + z1);
        m.add("loaded", loaded);
        m.addProperty("skylightSubtracted", ws.skylightSubtracted);
        m.addProperty("strictmath", "skylight_subtracted and cos_table are recorded per tick, not recomputed: "
            + "calculateSkylightSubtracted and getCelestialAngleRadians go through StrictMath.cos");
        m.addProperty("slot_grid", SX + " columns x " + SY + " rows of stride " + SLOT_STRIDE + " starting at "
            + "(cx-radius)*16 + 8, y " + SLOT_Y + "; slot s sits at (start + (s % " + SX + ") * " + SLOT_STRIDE
            + ", y, start + (s / " + SX + ") * " + SLOT_STRIDE + ")");
        m.add("census", censusJson(census));
        m.addProperty("build_layout", "build.bin.gz, gzip: every setBlock the setups make, in call order, 16 bytes per "
            + "write: x int32 LE, y int32 LE, z int32 LE, id uint16 LE, meta uint8, pad uint8; all writes are flag 2");
        m.addProperty("init_layout", "init.jsonl.gz, one line per tile entity the builds made plus the piston entity, "
            + "in creation order (the world list's order): the entity's writeToNBT as canonical NBT text");
        m.addProperty("tick_layout", "ticks.bin, " + (TICK_BYTES + 10) + " bytes per tick, tick 0 before the first pass "
            + "then one per tick: the " + TICK_BYTES + "-byte array (skylight_subtracted uint8, world_time int64 LE, "
            + "total_time int64 LE, 6 pad bytes, cos_table int32 LE x 24000, the celestial angle radians x 24000 at "
            + "that tick's world time), then the trailer: walk count uint16 LE, nbt count uint32 LE, write count "
            + "uint32 LE");
        m.addProperty("write_layout", "writes.bin.gz, 16 bytes per write, every tick's writes in order: x int32 LE, "
            + "y int32 LE, z int32 LE, id uint16 LE, meta uint8, pad uint8; a metadata-only write reports id -1, "
            + "stored as 0xffff");
        m.addProperty("nbt_layout", "nbt.jsonl.gz, one line per changed tile entity per tick in walk order: the "
            + "entity's canonical NBT text after the tick (its id and position included); tile entities not yet seen "
            + "count as changed at tick 0");
        m.addProperty("lidinit_layout", "lidinit.jsonl.gz, the same canonical NBT line as lid.jsonl.gz for every chest and "
            + "ender chest the setups made, in creation order: the initial state of the four fields writeToNBT does not "
            + "carry, which the native side cannot otherwise restore (the probe sets them directly); the tick in these "
            + "lines is always 0 and is not read");
        m.addProperty("lid_layout", "lid.jsonl.gz, one canonical NBT line per chest and ender chest in walk order, "
            + "one line per entity per tick (tick 0 included): id, x, y, z, tick and the three fields updateEntity "
            + "moves, none of which writeToNBT carries: players (numPlayersUsing), lid (lidAngle), prev "
            + "(prevLidAngle) and counter (the tick counter the 200 and 80 tick branches test)");
        m.addProperty("order_layout", "order.bin.gz, 2 bytes per walked tile entity, per tick in walk order: "
            + "slot uint16 LE; the piston entity is slot 65535");
        m.addProperty("chunkhash_layout", "chunkhash.bin.gz, 12 bytes per hash record: the tick uint32 LE and the "
            + "FNV-1a 64 hash of the 3x3 chunks around the chunk the tick's first write landed in; one record per "
            + "distinct chunk a tick wrote to, in first-write order, none for a tick with no writes");
        m.addProperty("final_layout", "final.bin.gz, per loaded chunk in load order: cx int32 LE, cz int32 LE, the "
            + "chunk bytes below, updateSkylightColumns 256 bytes (0/1), isGapLightingUpdated 1 byte");
        m.addProperty("chunk_bytes", "ids uint16 LE (65536, index x << 12 | z << 8 | y), metas uint8 (65536), sky light "
            + "uint8 (65536), block light uint8 (65536), heightMap 256 int32 LE, precipitationHeightMap 256 int32 LE "
            + "(both Java index order z << 4 | x), heightMapMinimum int32 LE, section mask uint16 LE; cells in absent "
            + "sections read as 0");
        m.addProperty("walk", "per tick: field_147481_N = true; every tile entity of field_147482_g with !isInvalid, "
            + "hasWorldObj and blockExists(x,y,z) runs updateEntity; an invalid one comes off the iterator and its "
            + "chunk's map; then field_147481_N = false, field_147483_b removed from the list, then field_147484_a "
            + "merged (not already present, appended) with func_150812_a and func_147471_g per entry");
        m.addProperty("tick_time", "per tick: skylightSubtracted = calculateSkylightSubtracted(1.0F) (recorded); "
            + "total_time = total_time + 1; world_time = world_time + 1; then the pass. world.rand is never drawn");
        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        w.println(m.toString());
        w.close();

        JsonObject res = new JsonObject();
        res.addProperty("dir", dir.getPath());
        res.addProperty("ticks", ticks);

        for (int k = 0; k < CENSUS_NAMES.length; ++k) res.addProperty(CENSUS_NAMES[k], census[k]);

        return res;
    }

    static JsonArray censusJson(int[] census)
    {
        JsonArray a = new JsonArray();

        for (int k = 0; k < CENSUS_NAMES.length; ++k)
        {
            JsonArray e = new JsonArray();
            e.add(new JsonPrimitive(CENSUS_NAMES[k]));
            e.add(new JsonPrimitive(census[k]));
            a.add(e);
        }

        return a;
    }

    // ------------------------------------------------- per-tick state rows

    static final int TICK_BYTES = 1 + 8 + 8 + 2 + 4 + 4 + 4 * 24000;  // cos table starts at 27

    /** The census kinds, in the order the walk's dispatch runs. */
    static final String[] CENSUS_NAMES = {"furnace", "detector", "hopper", "chest", "note", "spawner", "beacon",
        "brewing", "enderchest", "enchant", "piston", "other"};

    static int censusKey(TileEntity te)
    {
        if (te instanceof TileEntityFurnace) return 0;
        if (te instanceof net.minecraft.tileentity.TileEntityDaylightDetector) return 1;
        if (te instanceof TileEntityHopper) return 2;
        if (te instanceof TileEntityChest) return 3;
        if (te instanceof TileEntityNote) return 4;
        if (te instanceof TileEntityMobSpawner) return 5;
        if (te instanceof TileEntityBeacon) return 6;
        if (te instanceof TileEntityBrewingStand) return 7;
        if (te instanceof TileEntityEnderChest) return 8;
        if (te instanceof TileEntityEnchantmentTable) return 9;
        if (te instanceof TileEntityPiston) return 10;
        return 11;
    }

    /** One tick's rows: the tick record, the walk order, the changed NBTs,
     * the write stream's 3x3 hashes. lastNbt holds the previous canonical
     * NBT of every tile entity, so only changes are recorded. */
    static void writeState(DataGZ ticksOut, DataGZ orderG, DataGZ hashG, int t) throws Exception
    {
        List teList = teWalk;
        byte[] tick = new byte[TICK_BYTES];

        tick[0] = (byte)ws.skylightSubtracted;
        le64(tick, 1, ws.getWorldInfo().getWorldTime());
        le64(tick, 9, ws.getWorldInfo().getWorldTotalTime());

        // the celestial angle table, x 24000 so the int32 carries the float's
        // full precision at day scale (the value is always in [0, pi]); the
        // trailer (walk uint16, nbt count uint32, write count uint32) goes
        // out after the array, so the table starts at 27
        for (int i = 0; i < 24000; ++i)
        {
            float a = ws.getCelestialAngleRadians(i);
            le32(tick, 27 + 4 * i, Float.floatToRawIntBits(a));
        }

        int walk = teList.size();
        List<String> changed = new ArrayList<String>();

        for (int i = 0; i < walk; ++i)
        {
            TileEntity te = (TileEntity)teList.get(i);
            String now = teCanon(te);
            String before = lastNbt.get(te);

            if (before == null || !before.equals(now))
            {
                changed.add(now);
                lastNbt.put(te, now);
            }
        }

        ticksOut.raw(tick);
        ticksOut.u16(walk);
        ticksOut.u32(changed.size());
        ticksOut.u32(tickWrites.size());


        for (int i = 0; i < walk; ++i) orderG.u16(teSlot((TileEntity)teList.get(i)));

        for (String s : changed) nbtOut.println(s);

        // one line per chest and ender chest in walk order, every tick: the
        // lid fields nbt.jsonl.gz cannot carry
        for (int i = 0; i < walk; ++i)
        {
            String lid = lidCanon((TileEntity)teList.get(i), t);
            if (lid != null) lidOut.println(lid);
        }

        // one hash record per distinct chunk the tick wrote to, first-write
        // order
        byte[] scratch = new byte[Probe.CHUNK_BYTES];
        int n = tickWrites.size();

        for (int i = 0; i < n; ++i)
        {
            int[] c = tickWrites.get(i);

            if (i > 0 && c[0] == tickWrites.get(i - 1)[0] && c[1] == tickWrites.get(i - 1)[1]) continue;

            long h = Probe.hashAround(ws, c[0], c[1], scratch);
            byte[] hb = new byte[12];
            le32(hb, 0, t);
            le64(hb, 4, h);
            hashG.raw(hb);
        }
    }

    /** The slot a walked tile entity belongs to, or 65535 for the piston
     * entity (which sits outside the slot grid). */
    static int teSlot(TileEntity te)
    {
        if (te == pistonTE) return 0xffff;

        Integer s = teSlotOf.get(te);

        if (s != null) return s.intValue();

        StringBuilder known = new StringBuilder();

        for (Map.Entry<TileEntity,Integer> en : teSlotOf.entrySet())
        {
            TileEntity k = en.getKey();
            known.append(" (").append(k.field_145851_c).append(",").append(k.field_145848_d).append(",")
                .append(k.field_145849_e).append("=").append(en.getValue()).append(")");
        }

        throw new IllegalStateException("tile entity at " + te.field_145851_c + "," + te.field_145848_d + ","
            + te.field_145849_e + " is in no slot; cls " + te.getClass().getSimpleName() + " known:" + known);
    }

    // ------------------------------------------------------------ the builds

    static void slotAt(int cx, int cz, int radius, int s, int[] out)
    {
        out[0] = (cx - radius) * 16 + 8 + (s % SX) * SLOT_STRIDE;
        out[1] = SLOT_Y;
        out[2] = (cz - radius) * 16 + 8 + (s / SX) * SLOT_STRIDE;
    }

    /** One setup per slot. Furnaces (36): 12 fuels x 3 smeltables each, with
     * stack sizes that run out mid-way. Detectors (6): three exposed, one
     * walled, two roofed. Hoppers (6): one empty, one with stacks, one with
     * a set cooldown, one sideways over a chest, two plain. Chests (9):
     * single, double x, double z, trapped single, trapped double, one
     * holding a full 27 slots. Notes (3) at notes 0, 12, 24 on different
     * instruments. Spawners (3), default Pig then Cow and Sheep. Beacons
     * (6), pyramids of each size, the last two blocked. Brewing stand,
     * ender chest and enchant table (1 each). The remaining slots hold
     * nothing but their floor. */
    static void setup(int s, int x, int y, int z, PrintWriter initOut) throws Exception
    {
        Build b = new Build(x, y, z);
        List<TileEntity> out = new ArrayList<TileEntity>();

        if (s < 36)
        {
            furnace(b, s, out);
        }
        else if (s < 42)
        {
            detector(b, s, out);
        }
        else if (s < 48)
        {
            hopper(b, s, out);
        }
        else if (s < 57)
        {
            chest(b, s, out);
        }
        else if (s < 60)
        {
            note(b, out);
        }
        else if (s < 63)
        {
            spawner(b, out);
        }
        else if (s < 67)
        {
            beacon(b, s - 63, out);
        }
        else if (s == 67)
        {
            brewing(b, out);
        }
        else if (s == 68)
        {
            enderchest(b, out);
        }
        else if (s == 69)
        {
            enchant(b, out);
        }
        else
        {
            clear(b, 6, -3, 8);
        }

        for (TileEntity te : out)
        {
            if (te == null) throw new IllegalStateException("setup " + s + ": a null tile entity");
            initOut.println(teCanon(te));

            String lidInit = lidCanon(te, 0);

            if (lidInit != null) lidInitOut.println(lidInit);

            teSlotOf.put(te, Integer.valueOf(s));
        }

        tes[s] = out.toArray(new TileEntity[0]);
    }

    static void clear(Build b, int hx, int yBot, int yTop)
    {
        for (int dy = yBot; dy <= yTop; ++dy)
            for (int dz = -hx; dz <= hx; ++dz)
                for (int dx = -hx; dx <= hx; ++dx) b.set(ws, dx, dy, dz, Blocks.air, 0);
    }

    static void floor(Build b)
    {
        for (int dz = -6; dz <= 6; ++dz)
            for (int dx = -6; dx <= 6; ++dx) b.set(ws, dx, -2, dz, Blocks.stone, 0);
    }

    static void roof(Build b, int hx, int y)
    {
        for (int dz = -hx; dz <= hx; ++dz)
            for (int dx = -hx; dx <= hx; ++dx) b.set(ws, dx, y, dz, Blocks.stone, 0);
    }

    static void setSlot(Field slotsField, Object te, int i, ItemStack st) throws Exception
    {
        ItemStack[] a = (ItemStack[])slotsField.get(te);
        a[i] = st;
    }

    static void furnace(Build b, int s, List<TileEntity> out) throws Exception
    {
        clear(b, 6, -3, 8);
        floor(b);
        b.set(ws, 0, 0, 0, Blocks.furnace, 2);
        TileEntityFurnace f = (TileEntityFurnace)ws.getTileEntity(b.x, b.y, b.z);
        Random r = new Random(1000L + s);
        Fuel fuel = FUELS[r.nextInt(FUELS.length)];
        int in = IN[r.nextInt(IN.length)];
        int inCount = 1 + r.nextInt(8);
        int fuelCount = fuel.item == 327 || fuel.item == 173 || fuel.item == 270 || fuel.item == 268 || fuel.item == 290
            ? 1 : 1 + r.nextInt(3);

        setSlot(fFurnaceSlots, f, 0, new ItemStack(Item.getItemById(in), inCount, 0));
        setSlot(fFurnaceSlots, f, 1, new ItemStack(Item.getItemById(fuel.item), fuelCount, fuel.damage));
        out.add(f);
    }

    static void detector(Build b, int s, List<TileEntity> out)
    {
        clear(b, 6, -3, 8);
        floor(b);
        b.set(ws, 0, 0, 0, Blocks.daylight_detector, 0);

        if (s == 39) roof(b, 3, 5);
        if (s == 40) roof(b, 4, 6);

        if (s == 41) b.set(ws, 0, 1, 0, Blocks.stone, 0);

        out.add(ws.getTileEntity(b.x, b.y, b.z));
    }

    static void hopper(Build b, int s, List<TileEntity> out) throws Exception
    {
        clear(b, 6, -3, 8);
        floor(b);
        b.set(ws, 0, 0, 0, Blocks.hopper, s == 45 ? 2 : 0);
        TileEntityHopper h = (TileEntityHopper)ws.getTileEntity(b.x, b.y, b.z);
        Random r = new Random(2000L + s);
        int stacks = 1 + r.nextInt(3);

        for (int i = 0; i < stacks && i < 5; ++i)
        {
            setSlot(fHopperSlots, h, i, new ItemStack(Item.getItemById(IN[r.nextInt(IN.length)]), 1 + r.nextInt(16), 0));
        }

        if (s == 44) fHopperCooldown.setInt(h, 7);
        out.add(h);

        if (s == 43 || s == 45)
        {
            b.set(ws, 0, -1, 0, Blocks.chest, 2);
            out.add(ws.getTileEntity(b.x, b.y - 1, b.z));
        }
    }

    static void chest(Build b, int s, List<TileEntity> out) throws Exception
    {
        clear(b, 6, -3, 8);
        floor(b);
        Block block = s >= 55 ? Blocks.trapped_chest : Blocks.chest;
        b.set(ws, 0, 0, 0, block, s == 52 || s == 56 ? 3 : 2);
        TileEntityChest c = (TileEntityChest)ws.getTileEntity(b.x, b.y, b.z);

        // the lid fields, which no block or player interaction in this probe
        // would move: one chest reaches the 200-tick numPlayersUsing recount
        // on the first tick (players 2 falling to 0, then the lid closes),
        // one opens with a player count that never falls, one closes with the
        // count already at 0
        if (s == 48)
        {
            c.field_145987_o = 2;
            c.field_145989_m = 0.5F;
            fChestTicks.setInt(c, Math.floorMod(-(b.x + b.y + b.z) - 1, 200));
        }

        if (s == 49)
        {
            c.field_145987_o = 1;
            c.field_145989_m = 0.9F;
        }

        if (s == 50) c.field_145989_m = 0.3F;

        if (s == 51 || s == 56)
        {
            b.set(ws, 1, 0, 0, block, s == 51 ? 2 : 4);
            out.add(ws.getTileEntity(b.x + 1, b.y, b.z));
        }

        out.add(c);

        if (s == 54)
        {
            Random r = new Random(3000L);

            for (int i = 0; i < 27; ++i)
                if (r.nextInt(3) != 0)
                    setSlot(fChestSlots, c, i, new ItemStack(Item.getItemById(IN[r.nextInt(IN.length)]), 1 + r.nextInt(32), 0));
        }

    }

    static int noteIndex;

    static void note(Build b, List<TileEntity> out)
    {
        clear(b, 6, -3, 8);
        floor(b);
        b.set(ws, 0, 0, 0, Blocks.noteblock, 0);
        TileEntityNote n = (TileEntityNote)ws.getTileEntity(b.x, b.y, b.z);

        if (noteIndex == 1) n.field_145879_a = 12;
        if (noteIndex == 2) n.field_145879_a = 24;

        ++noteIndex;
        out.add(n);
    }

    static int spawnerIndex;

    static void spawner(Build b, List<TileEntity> out)
    {
        clear(b, 6, -3, 8);
        floor(b);
        b.set(ws, 0, 0, 0, Blocks.mob_spawner, 0);
        TileEntityMobSpawner m = (TileEntityMobSpawner)ws.getTileEntity(b.x, b.y, b.z);

        if (spawnerIndex == 1) m.func_145881_a().setMobID("Cow");
        if (spawnerIndex == 2) m.func_145881_a().setMobID("Sheep");

        ++spawnerIndex;
        out.add(m);
    }

    static void beacon(Build b, int v, List<TileEntity> out)
    {
        clear(b, 6, -3, 9);
        floor(b);

        for (int level = 0; level < 4; ++level)
        {
            int half = 3 - level;

            for (int dz = -half; dz <= half; ++dz)
                for (int dx = -half; dx <= half; ++dx)
                    if (level < v) b.set(ws, dx, -2 + level, dz, Blocks.iron_block, 0);
        }

        b.set(ws, 0, 0, 0, Blocks.beacon, 0);
        out.add(ws.getTileEntity(b.x, b.y, b.z));

        if (v == 3) roof(b, 5, 6);
        if (v == 4) b.set(ws, 0, 1, 0, Blocks.dirt, 0);
    }

    static void brewing(Build b, List<TileEntity> out) throws Exception
    {
        clear(b, 6, -3, 8);
        floor(b);
        b.set(ws, 0, 0, 0, Blocks.brewing_stand, 0);
        TileEntityBrewingStand st = (TileEntityBrewingStand)ws.getTileEntity(b.x, b.y, b.z);

        for (int i = 0; i < 3; ++i) setSlot(fBrewSlots, st, i, new ItemStack(Items.potionitem, 1, 0));

        setSlot(fBrewSlots, st, 3, new ItemStack(Items.nether_wart, 1, 0));
        out.add(st);
    }

    static void enderchest(Build b, List<TileEntity> out) throws Exception
    {
        clear(b, 6, -3, 8);
        floor(b);
        b.set(ws, 0, 0, 0, Blocks.ender_chest, 2);
        TileEntityEnderChest e = (TileEntityEnderChest)ws.getTileEntity(b.x, b.y, b.z);

        // a player count that never falls (nothing recounts it) and the lid
        // opening from a quarter; the counter is one short of the 80-tick
        // block event so that too runs on the first tick
        fEnderPlayers.setInt(e, 1);
        fEnderLid.setFloat(e, 0.25F);
        fEnderTicks.setInt(e, 79);
        out.add(e);
    }

    static void enchant(Build b, List<TileEntity> out)
    {
        clear(b, 6, -3, 8);
        floor(b);
        b.set(ws, 0, 0, 0, Blocks.enchanting_table, 0);
        out.add(ws.getTileEntity(b.x, b.y, b.z));
    }

    /** One build: the anchor position and the box a clear covers. */
    static final class Build
    {
        final int x, y, z;

        Build(int x, int y, int z)
        {
            this.x = x;
            this.y = y;
            this.z = z;
        }

        void set(WorldServer ws, int dx, int dy, int dz, Block block, int meta)
        {
            ws.setBlock(x + dx, y + dy, z + dz, block, meta, 2);
        }
    }
}
