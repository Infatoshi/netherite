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
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Iterator;
import java.util.List;
import java.util.HashMap;
import java.util.Map;
import java.util.Random;
import java.util.zip.GZIPOutputStream;
import net.minecraft.block.Block;
import net.minecraft.entity.Entity;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.tileentity.TileEntityChest;
import net.minecraft.tileentity.TileEntityMobSpawner;
import net.minecraft.world.NextTickListEntry;
import net.minecraft.world.World;
import net.minecraft.world.WorldServer;
import net.minecraft.world.storage.WorldInfo;
import net.minecraft.world.biome.BiomeGenBase;
import net.minecraft.world.chunk.Chunk;
import net.minecraft.world.gen.ChunkProviderGenerate;
import net.minecraft.world.gen.ChunkProviderHell;
import net.minecraft.world.gen.ChunkProviderServer;
import net.minecraft.world.gen.structure.MapGenStructure;
import net.minecraft.world.gen.structure.StructureStart;

/**
 * The populate probe: ChunkProviderServer.populate for every chunk of one
 * region of raw terrain, recorded call by call, the reference for the native
 * populate driver (csrc/engine/populate.c, checked by csrc/tests/
 * test_populate.c).
 *
 * Raw means Probe.rawChunks is set for the whole run, so every chunk load (the
 * region's ring up front, and any a populate call reaches past it) is bare
 * terrain and its only worldgen side effect is the four structure maps
 * growing, which the native side must model in the same order.
 *
 * Region: a W x W square of chunks (W = 24, 576 populate calls) plus one chunk
 * of raw ring around it, loaded first cx-major, cz inner. Nothing a populate
 * call writes or reads reaches past that ring on its own; a write that still
 * does lands in a chunk the write itself generates (loadChunkOnProvideRequest
 * is true), and that load is recorded in the call's load list. Populate calls
 * run cx-major, cz inner, over the interior; one game dir per run, so every
 * run starts with empty structure maps and an unloaded world.
 *
 * Per call, in order:
 *   - chunk, the biome at (cx*16+16, cz*16+16)
 *   - per structure generator (mineshaft, village, stronghold, scattered): how
 *     many starts in its map are sizeable and intersect the populate box
 *     (cx*16+8 .. +15 in both axes) at decision time, and the map size before
 *     and after its generateStructuresInChunk
 *   - chunk loads during the call: the total and the cumulative count after
 *     each generator, from ChunkProviderServer.loadedChunks' tail in load
 *     order
 *   - stage markers: the populate Random's internal seed at each hook line
 *     (ChunkProviderGenerate.populate and BiomeDecorator.func_150513_a)
 *   - every changed block write in order: World.setBlock and
 *     setBlockMetadataWithNotify, with the id (-1 for the metadata path), the
 *     metadata and the flags the call passed
 *   - the tile entity deltas over five intervals: call start -> after each of
 *     the four structure generators -> after the whole call, taken over the
 *     loaded region in (x, z, y) order
 *   - the entities performWorldGenSpawning spawned this call: native skips
 *     spawning (nothing after it draws the populate Random, so no block
 *     depends on it); this record is for the later entity lane
 *
 * After all calls: the pending scheduled tick list (WorldServer's TreeSet, in
 * its own order) and the final region with light and height maps.
 *
 * Two commands. "scan" finds regions: the biomes of every 16x16 chunk block
 * over +-range, then greedy W x W regions that together cover every biome id
 * the overworld generator emits for the seed; the result prints as JSON, one
 * "populate" run per region afterwards (a fresh game dir each).
 *
 * The Random seed is the private AtomicLong java.util.Random keeps its state
 * in, read reflectively: exactly the 48-bit value jrand carries in C.
 */
public final class PopulateProbe
{
    /** The stages, in record order: 0..10 the populate driver, 11..29 the decorator. */
    static final String[] STAGES = {
        "seed", "mineshaft", "village", "stronghold", "scattered", "waterlake", "lavalake",
        "dungeons", "decorate", "spawning", "snow",
        "ores", "sand", "clay", "gravel", "trees", "bigmushrooms", "flowers", "grass",
        "deadbush", "waterlily", "mushrooms", "mushroom1", "mushroom2", "reeds", "reeds10",
        "pumpkin", "cactus", "springs", "springslava",
    };

    /** The Nether's stages, in record order: the fortress stage, then the eight
     * feature stages of ChunkProviderHell.populate, a mark after each. */
    static final String[] HELL_STAGES = {
        "fortress", "helllava", "fire", "glow1", "glow2", "brown", "red", "quartz", "hiddenlava",
    };

    /** The End's stages, in record order: BiomeEndDecorator's generateOres and
     * the spike attempt (the dragon's yaw draw comes after the last one, and
     * nothing after it draws the decorate Random). */
    static final String[] END_STAGES = {"ores", "spike"};

    /** While recording, the vanilla hook calls land in the sink. Vanilla calls
 * this with no throws clause, so the sink's IO errors surface as runtime ones. */
    public static void mark(String stage, Random r)
    {
        if (sink != null)
        {
            try
            {
                sink.mark(stage, seedOf(r));
            }
            catch (IOException e)
            {
                throw new RuntimeException(e);
            }
        }

        if (markSink != null) markSink.mark(stage, seedOf(r));
    }

    /** A stage listener for probes outside this one (the spawning probe's
     * populate kind): the stage name and the stage Random's state, in fire
     * order. */
    public interface MarkSink { void mark(String stage, long seed); }

    private static MarkSink markSink;

    public static void setMarkSink(MarkSink s) { markSink = s; }

    private static Sink sink;

    /** While recording, every chunk the world generates reaches this with its
     * raw content, before any populate write: the structure generators carve
     * during provideChunk, so the native port's raw chunks differ wherever a
     * mineshaft, village, stronghold or temple crosses one. The test replays
     * these bytes instead of generating natively (see the report). */
    private static OutputStream rawOut;

    /** The dump's own load order: the entries are keyed by (cx, cz). */
    private static final Map<Long, Boolean> rawSeen = new HashMap<Long, Boolean>();

    /** The structure maps the run's dimension holds, set by record(). dim 0
     * the four overworld maps, -1 MapGenNetherBridge's, 1 none. */
    private static java.util.Map[] dimMaps = new java.util.Map[0];

    /** The Random the dimension's populate draws from: dim -1 the provider's
     * hellRNG (reseeded by every provideChunk), dim 1 the End World's rand, and
     * dim 0 null (the populate rand's seed reaches the record through the
     * seed-stage marker). */
    private static Random dimRandom;

    /** The World.rand state at record start, for the manifest's worldRand
     * field: BlockFire's onBlockAdded and the immediate liquid updateTicks
     * draw the delays from it, and the native test installs the same stream.
     * Captured before the calls (the draws happen inside them). */
    private static long worldRandSeed;

    public static long seedOf(Random r)
    {
        try
        {
            if (randSeed == null)
            {
                randSeed = Random.class.getDeclaredField("seed");
                randSeed.setAccessible(true);
            }

            return ((java.util.concurrent.atomic.AtomicLong)randSeed.get(r)).get();
        }
        catch (Exception e)
        {
            throw new IllegalStateException(e);
        }
    }

    private static Field randSeed;

    /** The load hook ChunkProviderServer.loadChunk calls once per generated
 * chunk. No-op unless a record run is dumping raw chunks. */
    public static void onChunkLoaded(Chunk c)
    {
        if (rawOut == null)
        {
            return;
        }

        try
        {
            long key = (((long)c.xPosition) << 32) ^ (c.zPosition & 0xffffffffL);

            if (rawSeen.containsKey(key))
            {
                return;
            }

            rawSeen.put(key, Boolean.TRUE);

            byte[] h = new byte[8];
            le32(h, 0, c.xPosition);
            le32(h, 4, c.zPosition);
            rawOut.write(h);

            byte[] b = new byte[Probe.CHUNK_BYTES];
            Probe.fillChunkBytes(c, b);
            rawOut.write(b);

            byte[] cols = new byte[256];
            for (int k = 0; k < 256; ++k) cols[k] = (byte)(c.updateSkylightColumns[k] ? 1 : 0);
            rawOut.write(cols);
            rawOut.write(field(Chunk.class, "isGapLightingUpdated").getBoolean(c) ? 1 : 0);
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
    }

    private PopulateProbe() {}

    private static GZIPOutputStream rawStream;

    /** A probe outside this one that wants the same raw-chunk replay: the
     * dump opens before the chunks load (the hook fires once per generated
     * chunk, keyed by position, so a chunk loaded twice is recorded once) and
     * closes after the work. PopulateProbe.dump uses these for its own run. */
    public static void rawStart(File dir) throws IOException
    {
        rawStream = new GZIPOutputStream(new FileOutputStream(new File(dir, "raw.bin.gz")), 1 << 16);
        rawOut = rawStream;
        rawSeen.clear();
    }

    public static void rawClose() throws IOException
    {
        OutputStream o = rawOut;
        rawOut = null;

        if (o != null) o.close();
    }

    /** The Random the dimension's populate calls start from, for the call
     * header's startSeed field; dim 0 has none (null), so its header layout
     * stays what every existing recording holds. */
    static Random dimRand(int dim, ChunkProviderServer cps, WorldServer ws) throws Exception
    {
        if (dim == 0) return null;

        if (dim == -1)
        {
            ChunkProviderHell hell = (ChunkProviderHell)field(ChunkProviderServer.class, "currentChunkProvider").get(cps);
            return (Random)field(ChunkProviderHell.class, "hellRNG").get(hell);
        }

        return ws.rand;
    }

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
                    result[0] = "scan".equals(cmd.has("mode") ? cmd.get("mode").getAsString() : "populate")
                        ? scan(server, cmd) : record(server, cmd);
                }
                catch (Exception e)
                {
                    error[0] = e;
                }
            }
        }, "Oracle PopulateProbe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    // ------------------------------------------------------------------ scan

    static JsonObject scan(IntegratedServer server, JsonObject cmd) throws Exception
    {
        int range = cmd.has("range") ? cmd.get("range").getAsInt() : 1200;
        int width = cmd.has("width") ? cmd.get("width").getAsInt() : 24;
        WorldServer ws = server.worldServers[0];
        long seed = ws.getSeed();

        // the biome bitset of every 16x16 chunk block over the range, chunk coords
        int n = 2 * range / 16;
        long[][] tiles = new long[n * n][];
        long[] all = new long[4];
        int i = 0;

        for (int tx = 0; tx < n; ++tx)
        {
            for (int tz = 0; tz < n; ++tz)
            {
                long[] s = tileBitset(ws, -range + tx * 16, -range + tz * 16);
                tiles[i] = s;
                ++i;

                for (int k = 0; k < 4; ++k) all[k] |= s[k];
            }
        }

        int total = popcount(all);
        JsonArray found = new JsonArray();
        long[] left = all.clone();

        while (true)
        {
            int bx = 0, bz = 0, bgain = 0;

            // region origins every 8 chunks; the interior entirely at |c| >= 1000
            for (int x = -range; x + width <= range; x += 8)
            {
                if (!(x + width - 1 <= -1000 || x >= 1000)) continue;

                for (int z = -range; z + width <= range; z += 8)
                {
                    if (!(z + width - 1 <= -1000 || z >= 1000)) continue;

                    long[] cov = coverage(tiles, n, range, x, z, width);
                    int gain = popcountAnd(cov, left);

                    if (gain > bgain)
                    {
                        bgain = gain;
                        bx = x;
                        bz = z;
                    }
                }
            }

            if (bgain == 0) break;
            long[] cov = coverage(tiles, n, range, bx, bz, width);

            for (int k = 0; k < 4; ++k) left[k] &= ~cov[k];

            JsonArray r = new JsonArray();
            r.add(new JsonPrimitive(bx));
            r.add(new JsonPrimitive(bz));
            found.add(r);

            if (found.size() >= 16) break;
        }

        JsonObject m = new JsonObject();
        m.addProperty("seed", seed);
        m.addProperty("width", width);
        m.addProperty("biomes", total);
        JsonArray list = new JsonArray();

        for (int id = 0; id < 256; ++id)
        {
            if ((all[id >> 6] >> (id & 63) & 1) != 0) list.add(new JsonPrimitive(BiomeGenBase.func_150568_d(id).biomeName + "(" + id + ")"));
        }

        m.add("all", list);
        m.add("regions", found);

        if (cmd.has("locate"))
        {
            // tiles of the given ids whose 16x16 chunk block lies entirely at
            // |c| >= 1000 in both axes, in the order found
            JsonArray want = cmd.get("locate").getAsJsonArray();
            long[] mask = new long[4];

            for (int i2 = 0; i2 < want.size(); ++i2)
            {
                int id = want.get(i2).getAsInt();
                mask[id >> 6] |= 1L << (id & 63);
            }

            JsonArray spots = new JsonArray();

            for (int tx = 0; tx < n && spots.size() < 24; ++tx)
            {
                for (int tz = 0; tz < n && spots.size() < 24; ++tz)
                {
                    int cx = -range + tx * 16, cz = -range + tz * 16;

                    if (Math.min(Math.abs(cx), Math.abs(cx + 15)) < 1000) continue;
                    if (Math.min(Math.abs(cz), Math.abs(cz + 15)) < 1000) continue;

                    long[] s = tiles[tx * n + tz];

                    for (int i2 = 0; i2 < want.size(); ++i2)
                    {
                        int id = want.get(i2).getAsInt();

                        if ((s[id >> 6] >> (id & 63) & 1) != 0)
                        {
                            JsonArray sp = new JsonArray();
                            sp.add(new JsonPrimitive(BiomeGenBase.func_150568_d(id).biomeName + "(" + id + ")"));
                            sp.add(new JsonPrimitive(cx));
                            sp.add(new JsonPrimitive(cz));
                            spots.add(sp);
                        }
                    }
                }
            }

            m.add("locate", spots);
        }
        m.addProperty("covered", total - popcount(left));
        JsonArray missing = new JsonArray();

        for (int id = 0; id < 256; ++id)
        {
            if ((left[id >> 6] >> (id & 63) & 1) != 0)
            {
                JsonObject mm = new JsonObject();
                mm.addProperty("biome", BiomeGenBase.func_150568_d(id).biomeName + "(" + id + ")");
                JsonArray spots = new JsonArray();

                for (int tx = 0; tx < n && spots.size() < 5; ++tx)
                {
                    for (int tz = 0; tz < n && spots.size() < 5; ++tz)
                    {
                        if ((tiles[tx * n + tz][id >> 6] >> (id & 63) & 1) != 0)
                        {
                            JsonArray sp = new JsonArray();
                            sp.add(new JsonPrimitive(-range + tx * 16));
                            sp.add(new JsonPrimitive(-range + tz * 16));
                            spots.add(sp);
                        }
                    }
                }

                mm.add("tiles", spots);
                missing.add(mm);
            }
        }

        m.add("missing", missing);
        return m;
    }

    /** The biome bitset of one 16x16 chunk block, as a long[4]. */
    private static long[] tileBitset(WorldServer ws, int cx0, int cz0) throws Exception
    {
        BiomeGenBase[] b = ws.getWorldChunkManager().loadBlockGeneratorData(null, cx0 * 16, cz0 * 16, 16, 16);
        long[] set = new long[4];

        for (BiomeGenBase aB : b) set[aB.biomeID >> 6] |= 1L << (aB.biomeID & 63);

        return set;
    }

    /** The union of the 16x16 tiles the region [x, x+w) spans, as bitsets. */
    private static long[] coverage(long[][] tiles, int n, int range, int x, int z, int w)
    {
        long[] out = new long[4];
        int t0 = (x + range) / 16, t1 = (x + w - 1 + range) / 16;
        int u0 = (z + range) / 16, u1 = (z + w - 1 + range) / 16;

        for (int tx = t0; tx <= t1; ++tx)
        {
            for (int tz = u0; tz <= u1; ++tz)
            {
                long[] s = tiles[tx * n + tz];

                for (int k = 0; k < 4; ++k) out[k] |= s[k];
            }
        }

        return out;
    }

    private static int popcount(long[] a)
    {
        int c = 0;

        for (long x : a) c += Long.bitCount(x);

        return c;
    }

    private static int popcountAnd(long[] a, long[] b)
    {
        int c = 0;

        for (int k = 0; k < 4; ++k) c += Long.bitCount(a[k] & b[k]);

        return c;
    }

    // -------------------------------------------------------------- recording

    static JsonObject record(IntegratedServer server, JsonObject cmd) throws Exception
    {
        int x0 = cmd.get("x0").getAsInt(), z0 = cmd.get("z0").getAsInt();
        int width = cmd.has("width") ? cmd.get("width").getAsInt() : 24;
        /* dim 0 the overworld (the default, every existing run), -1 the Nether,
         * 1 the End */
        int dim = cmd.has("dim") ? cmd.get("dim").getAsInt() : 0;
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();
        Trace.restart();

        WorldServer ws = server.worldServerForDimension(dim);
        long seed = ws.getSeed();
        ChunkProviderServer cps = (ChunkProviderServer)field(World.class, "chunkProvider").get(ws);
        String[] stages;

        if (dim == 0)
        {
            ChunkProviderGenerate gen = (ChunkProviderGenerate)field(ChunkProviderServer.class, "currentChunkProvider").get(cps);
            MapGenStructure[] gens =
            {
                (MapGenStructure)field(ChunkProviderGenerate.class, "mineshaftGenerator").get(gen),
                (MapGenStructure)field(ChunkProviderGenerate.class, "villageGenerator").get(gen),
                (MapGenStructure)field(ChunkProviderGenerate.class, "strongholdGenerator").get(gen),
                (MapGenStructure)field(ChunkProviderGenerate.class, "scatteredFeatureGenerator").get(gen),
            };
            Field mapField = MapGenStructure.class.getDeclaredField("structureMap");
            mapField.setAccessible(true);
            java.util.Map[] maps = new java.util.Map[4];

            for (int g = 0; g < 4; ++g) maps[g] = (java.util.Map)mapField.get(gens[g]);

            dimMaps = maps;
            stages = STAGES;
        }
        else if (dim == -1)
        {
            /* the Nether's one structure map: MapGenNetherBridge */
            ChunkProviderHell hell = (ChunkProviderHell)field(ChunkProviderServer.class, "currentChunkProvider").get(cps);
            Field mapField = MapGenStructure.class.getDeclaredField("structureMap");
            mapField.setAccessible(true);
            dimMaps = new java.util.Map[] {(java.util.Map)mapField.get((MapGenStructure)hell.genNetherBridge)};
            stages = HELL_STAGES;
        }
        else
        {
            dimMaps = new java.util.Map[0];
            stages = END_STAGES;
        }

        java.util.Map[] maps = dimMaps;

        Probe.rawChunks = true;

        // the raw chunk dump must be open before the ring loads: the hook fires
        // once per generated chunk
        OutputStream rawStream = new GZIPOutputStream(new FileOutputStream(new File(dir, "raw.bin.gz")), 1 << 16);
        rawOut = rawStream;
        rawSeen.clear();

        int x1 = x0 + width - 1, z1 = z0 + width - 1;
        JsonArray loaded = new JsonArray();

        for (int lx = x0 - 1; lx <= x1 + 1; ++lx)
        {
            for (int lz = z0 - 1; lz <= z1 + 1; ++lz)
            {
                if (ws.getChunkFromChunkCoords(lx, lz) == null) throw new IllegalStateException("chunk (" + lx + "," + lz + ") did not load");
                JsonArray pair = new JsonArray();
                pair.add(new JsonPrimitive(lx));
                pair.add(new JsonPrimitive(lz));
                loaded.add(pair);
            }
        }

        JsonArray mapSizes = new JsonArray();

        for (int g = 0; g < maps.length; ++g) mapSizes.add(new JsonPrimitive(maps[g].size()));

        OutputStream callsOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "calls.bin")), 1 << 16);
        OutputStream writesOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "writes.bin")), 1 << 16);
        OutputStream teOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "tileentities.bin")), 1 << 16);
        OutputStream ticksOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "ticks.bin")), 1 << 16);
        Tiles tiles = new Tiles(teOut, ws, x0 - 1, x1 + 1, z0 - 1, z1 + 1);
        Sink s = new Sink(callsOut, writesOut, tiles, maps, cps, ws, dim, stages, dimRand(dim, cps, ws));
        sink = s;
        Rows.writeListener = s;

        int calls = 0;
        long totalWrites = 0;

        // the spawn-area generation left pending ticks; drop them and restart the
        // entry counter so the recorded list is what these populate calls left
        java.util.TreeSet pendBefore = (java.util.TreeSet)field(WorldServer.class, "pendingTickListEntriesTreeSet").get(ws);
        pendBefore.clear();
        java.lang.reflect.Field nextIdField = NextTickListEntry.class.getDeclaredField("nextTickEntryID");
        nextIdField.setAccessible(true);
        nextIdField.setLong(null, 0L);

        // scheduleBlockUpdate stamps getWorldTotalTime(); the server ran a few
        // ticks before the probe, so zero it and every scheduledTime is the
        // delay alone (the native world starts at time 0). The worlds beside
        // 0 wrap the shared WorldInfo in a DerivedWorldInfo, whose
        // incrementTotalWorldTime is a no-op, so reach the base one.
        java.lang.reflect.Field wi = field(World.class, "worldInfo");
        wi.setAccessible(true);
        WorldInfo info = (WorldInfo)wi.get(ws);

        while (info.getClass() != WorldInfo.class)
        {
            info = (WorldInfo)field(info.getClass(), "theWorldInfo").get(info);
        }

        info.incrementTotalWorldTime(0L);

        /* the World.rand state the populate calls draw from (the manifest's
         * worldRand): captured here, after the ring's raw loads, so any draw
         * they make is already in */
        if (dim != 0) worldRandSeed = seedOf(ws.rand);

        for (int cx = x0; cx <= x1; ++cx)
        {
            for (int cz = z0; cz <= z1; ++cz)
            {
                s.beginCall(cx, cz);
                cps.populate(cps, cx, cz);
                s.endCall();
                totalWrites += s.writes;
                ++calls;
            }
        }

        Rows.writeListener = null;
        sink = null;
        callsOut.close();
        writesOut.close();
        tiles.close();

        // pending ticks, in the TreeSet's own order (time, priority, tickEntryID)
        java.util.TreeSet pend = (java.util.TreeSet)field(WorldServer.class, "pendingTickListEntriesTreeSet").get(ws);
        Field tickId = NextTickListEntry.class.getDeclaredField("tickEntryID");
        tickId.setAccessible(true);
        byte[] th = new byte[4];
        le32(th, 0, pend.size());
        ticksOut.write(th);

        for (Object o : pend)
        {
            NextTickListEntry e = (NextTickListEntry)o;
            byte[] h = new byte[36];
            le32(h, 0, e.xCoord);
            le32(h, 4, e.yCoord);
            le32(h, 8, e.zCoord);
            le32(h, 12, Block.getIdFromBlock(e.func_151351_a()));
            le64(h, 16, e.scheduledTime);
            le32(h, 24, e.priority);
            le64(h, 28, (Long)tickId.get(e));
            ticksOut.write(h);
        }

        ticksOut.close();
        rawOut = null;
        rawStream.close();

        // the final region
        Field gap = field(Chunk.class, "isGapLightingUpdated");
        DataOutputStream out = new DataOutputStream(new GZIPOutputStream(new FileOutputStream(new File(dir, "final.bin.gz")), 1 << 16));
        byte[] buf = new byte[Probe.CHUNK_BYTES];
        byte[] cols = new byte[256];

        for (int lx = x0 - 1; lx <= x1 + 1; ++lx)
        {
            for (int lz = z0 - 1; lz <= z1 + 1; ++lz)
            {
                Chunk c = ws.getChunkFromChunkCoords(lx, lz);
                Probe.fillChunkBytes(c, buf);
                writeLe32(out, lx);
                writeLe32(out, lz);
                out.write(buf);

                for (int k = 0; k < 256; ++k) cols[k] = (byte)(c.updateSkylightColumns[k] ? 1 : 0);
                out.write(cols);
                out.write(gap.getBoolean(c) ? 1 : 0);
            }
        }

        out.close();

        JsonObject m = new JsonObject();
        m.addProperty("seed", seed);
        m.addProperty("kind", "populate");

        if (dim != 0)
        {
            m.addProperty("dim", dim);
            m.addProperty("worldRand", worldRandSeed);
            m.addProperty("call_layout", "calls.bin per call: cx int32, cz int32, biome int32, startSeed uint64 (the state "
                + "the dimension's populate Random carries at the call's start: dim -1 hellRNG, dim 1 the End World's rand; "
                + "the native model must derive it from the history, not read it), counts[4] int32 (dim -1: the sizeable "
                + "fortress starts whose box intersects the populate box at populate entry, other slots zero), "
                + "sizesBefore[4] int32, sizesAfter[4] int32 (the map size before the call and after the structure stage), "
                + "loadsTotal int32, loadsAfter[4] int32 (cumulative loads after each of the first four stages), "
                + "then loadsTotal pairs (cx int32, cz int32) in load order, markerCount uint8, markers "
                + "(stage uint8, seed uint64, writeIndex uint32, recordIndex uint32), writeCount uint32; the writes are in "
                + "writes.bin, one stream in call order; then 11 (dim -1) or 4 (dim 1) tile entity delta segments "
                + "(call start, after each stage, after the whole call, in tileentities.bin), then entityCount uint32 and "
                + "per entity nameLen uint8, name bytes, id int32, x double, y double, z double, yaw float");
            m.addProperty("te_layout", "tileentities.bin: one delta segment per interval (call start, after each stage, "
                + "after the whole call): count uint32, then per entry x int32, y int32, z int32, kind uint8 (0 gone, 1 chest, "
                + "2 mob spawner, 3 other), nameLen uint32, nbtLen uint32, name bytes, nbt bytes (canonical NBT, "
                + "StructuresProbe.canon); the entries are what the interval left different from the interval before, "
                + "sorted by x, then z, then y");
            m.addProperty("stages", String.join(",", stages));
        }

        m.addProperty("x0", x0);
        m.addProperty("z0", z0);
        m.addProperty("width", width);
        m.addProperty("calls", calls);
        m.addProperty("writes", totalWrites);
        m.add("mapSizes", mapSizes);
        m.add("loaded", loaded);
        m.addProperty("order", "ring cx-major for cx in x0-1..x1+1, cz inner; then populate calls cx-major: for cx in x0..x1, for cz in z0..z1");
        m.addProperty("call_layout", "calls.bin per call: cx int32, cz int32, biome int32, counts[4] int32 (sizeable starts in "
            + "the generator's map whose box intersects the populate box, at decision time: mineshaft, village, stronghold, "
            + "scattered), sizesBefore[4] int32 (map size before the call), sizesAfter[4] int32 (after each generator), "
            + "loadsTotal int32, loadsAfter[4] int32 (cumulative loads after each generator), then loadsTotal pairs (cx int32, cz int32) in load order, markerCount uint8, markers "
            + "(stage uint8, seed uint64, writeIndex uint32, recordIndex uint32), writeCount uint32; the writes are in writes.bin, one stream "
            + "in call order; then 6 tile entity delta segments (call start, after each of the four structure generators, "
            + "after the whole call, in tileentities.bin), then entityCount uint32 and per entity nameLen uint8, name "
            + "bytes, id int32, x double, y double, z double, yaw float");
        m.addProperty("write_layout", "writes.bin per write: x int32, y int32, z int32, id uint16 (0xffff is the metadata-only "
            + "path), meta uint8, flags uint8 whose high bit 0x80 marks World.setBlock attempts that changed nothing (they "
            + "still run the light pass, so the native replays them; they are not counted in the marker write indices)");
        m.addProperty("te_layout", "tileentities.bin: one delta segment per interval (call start, after each of the four "
            + "structure generators, after the whole call): count uint32, then per entry x int32, "
            + "y int32, z int32, kind uint8 (0 gone, 1 chest, 2 mob spawner, 3 other), nameLen uint32, nbtLen uint32, "
            + "name bytes, nbt bytes (canonical NBT, StructuresProbe.canon); the entries are what the interval left "
            + "different from the interval before, sorted by x, then z, then y");
        m.addProperty("raw_layout", "raw.bin.gz, one entry per generated chunk in load order: cx int32, cz int32, "
            + "chunk bytes (Probe.CHUNK_BYTES, the raw state right after provideChunk, with the structure "
            + "generators' carving baked in), updateSkylightColumns 256 bytes, isGapLightingUpdated 1 byte; "
            + "the native test replays these instead of generating the chunk natively, until the structure "
            + "lanes port the structure blocks");
        m.addProperty("tick_layout", "ticks.bin: count uint32, per entry x int32, y int32, z int32, block uint32, "
            + "scheduledTime int64, priority int32, tickEntryID int64; the record order is the TreeSet's "
            + "(scheduledTime, priority, tickEntryID), and tickEntryID follows insertion");
        m.addProperty("final_layout", "final.bin.gz, per loaded chunk in load order: cx int32 LE, cz int32 LE, chunk bytes "
            + "(Probe.CHUNK_BYTES: ids uint16 LE, metas, sky light, block light, heightMap 256 int32 LE, "
            + "precipitationHeightMap 256 int32 LE, heightMapMinimum int32 LE, section mask uint16 LE), "
            + "updateSkylightColumns 256 bytes (0/1), isGapLightingUpdated 1 byte");
        if (dim == 0) m.addProperty("stages", String.join(",", STAGES));
        m.addProperty("spawning_note", "the native port skips performWorldGenSpawning: its draws come after decorate and "
            + "nothing after it draws the populate Random, so the spawning and snow markers carry no constraint");
        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        w.println(m.toString());
        w.close();

        JsonObject r = new JsonObject();
        r.addProperty("dir", dir.getPath());
        r.addProperty("calls", calls);
        r.addProperty("writes", totalWrites);
        return r;
    }

    private static Field field(Class<?> c, String name) throws Exception
    {
        Field f = c.getDeclaredField(name);
        f.setAccessible(true);
        return f;
    }

    // ------------------------------------------------------------------ sink

    /** The write listener and mark hook: assembles one call's record. */
    static final class Sink implements Rows.WriteListener
    {
        private final OutputStream callsOut, writesOut;
        private final Tiles tiles;
        private final java.util.Map[] maps;
        private final ChunkProviderServer cps;
        private final WorldServer ws;

        int cx, cz, writes;

        /* the write records appended over the whole run, no-op attempts
         * included: the markers carry it so the native can slice the stream */
        private int recordsTotal;

        /* the call's write stream, buffered so an attempt's changed bit can be
         * patched when the attempt returns */
        private final java.io.ByteArrayOutputStream wbuf = new java.io.ByteArrayOutputStream(1 << 16);
        private int[] stack = new int[256];
        private boolean[] stackChanged = new boolean[256];
        private int depth;

        private final int[] counts = new int[4];
        private final int[] sizesBefore = new int[4];
        private final int[] sizesAfter = new int[4];
        private int loadsTotal;
        private final int[] loadsAfter = new int[4];
        private final List<long[]> markers = new ArrayList<long[]>();   /* stage, seed, writes */
        private int loaded0;
        private int entity0;

        /* the dimension, its stage names and the Random populate starts from
         * (null for dim 0, whose layout stays what every recording holds) */
        private final int dim;
        private final String[] stages;
        private final Random dimRand;
        private long startSeed;

        Sink(OutputStream calls, OutputStream writes, Tiles tiles, java.util.Map[] maps,
             ChunkProviderServer cps, WorldServer ws, int dim, String[] stages, Random dimRand)
        {
            this.callsOut = calls;
            this.writesOut = writes;
            this.tiles = tiles;
            this.maps = maps;
            this.cps = cps;
            this.ws = ws;
            this.dim = dim;
            this.stages = stages;
            this.dimRand = dimRand;
        }

        void beginCall(int cx, int cz) throws IOException
        {
            this.cx = cx;
            this.cz = cz;
            this.writes = 0;
            this.depth = 0;
            this.wbuf.reset();
            this.markers.clear();
            this.loaded0 = loadedChunks().size();
            this.entity0 = ws.loadedEntityList.size();

            /* dim -1: the fortress count and map size, at populate entry */
            if (dim == -1)
            {
                counts[0] = intersects(0);
                sizesBefore[0] = maps[0].size();
            }

            if (dimRand != null) startSeed = PopulateProbe.seedOf(dimRand);

            tiles.record();
        }

        void endCall() throws IOException
        {
            this.loadsTotal = loadedChunks().size() - this.loaded0;
            tiles.record();
            this.writes = changedRecords();
            this.recordsTotal += this.wbuf.size() / 16;
            this.wbuf.writeTo(this.writesOut);
            this.wbuf.reset();
            writeCall();
        }

        /** The high bit of the flags byte: the attempt changed nothing. */
        static final int NOOP = 0x80;

        private void bufWrite(World w, int x, int y, int z, int id, int meta, int flags)
        {
            byte[] b = new byte[16];
            le32(b, 0, x);
            le32(b, 4, y);
            le32(b, 8, z);
            b[12] = (byte)id;
            b[13] = (byte)(id >> 8);
            b[14] = (byte)meta;
            b[15] = (byte)flags;
            this.wbuf.write(b, 0, b.length);
        }

        /**
         * World.setBlock's attempt: recorded with the no-op bit set, cleared at
         * the matching return when the write reported a change. Structure
         * generators make many attempts that change nothing, and each still runs
         * the light pass.
         */
        public void onAttempt(World w, int x, int y, int z, int id, int meta, int flags)
        {
            if (this.depth == this.stack.length)
            {
                int[] bigger = new int[this.stack.length * 2];
                boolean[] bbigger = new boolean[this.stack.length * 2];
                System.arraycopy(this.stack, 0, bigger, 0, this.stack.length);
                System.arraycopy(this.stackChanged, 0, bbigger, 0, this.stack.length);
                this.stack = bigger;
                this.stackChanged = bbigger;
            }

            this.stack[this.depth] = this.wbuf.size();
            this.stackChanged[this.depth] = false;
            ++this.depth;
            bufWrite(w, x, y, z, id, meta, flags | NOOP);
        }

        public void onAttemptEnd(World w, int x, int y, int z, int id, int meta, int flags)
        {
            --this.depth;

            if (this.stackChanged[this.depth])
            {
                byte[] b = this.wbuf.toByteArray();
                int at = this.stack[this.depth];
                b[at + 15] = (byte)(b[at + 15] & ~NOOP);
                this.wbuf.reset();
                this.wbuf.write(b, 0, b.length);
                ++this.writes;
            }
        }

        public void onWrite(World w, int x, int y, int z, int id, int meta, int flags)
        {
            if (id == -1)
            {
                /* World.setBlockMetadataWithNotify: no attempt hook, and only a
                 * change of metadata is reported at all */
                bufWrite(w, x, y, z, -1, meta, flags);
                ++this.writes;
            }
            else
            {
                /* a change of the block the innermost open attempt is writing;
                 * its return hook clears the record's no-op bit. notifyBlockChange
                 * can open further attempts below this one, so the mark is per
                 * attempt and not a single flag. */
                this.stackChanged[this.depth - 1] = true;
            }
        }

        /**
         * The changed records the buffer holds: the marker's write index is a
         * count of them, so it always matches the flags the stream carries even
         * when an attempt is still open (its record is in the buffer, its
         * change is not reported yet).
         */
        private int changedRecords()
        {
            byte[] b = this.wbuf.toByteArray();
            int n = 0;

            for (int i = 0; i < b.length; i += 16)
                if ((b[i + 15] & NOOP) == 0) ++n;

            return n;
        }

        void mark(String stage, long seed) throws IOException
        {
            this.writes = changedRecords();

            List<Chunk> lc = loadedChunks();

            if (dim != 0)
            {
                /* a mark after one stage: the cumulative loads land in the
                 * stage's own slot, the Nether's fortress stage also closes
                 * the structure map's before/after pair */
                int si = stageIndex(stage);

                if (si < 0) throw new IllegalStateException("unknown stage " + stage);

                if (si < 4) loadsAfter[si] = lc.size() - loaded0;

                if (dim == -1 && si == 0) sizesAfter[0] = maps[0].size();

                tiles.record();
                markers.add(new long[] {si, seed, writes, this.recordsTotal + this.wbuf.size() / 16});
                return;
            }

            switch (stage)
            {
                case "seed":
                    counts[0] = intersects(0);

                    for (int g = 0; g < 4; ++g) sizesBefore[g] = maps[g].size();

                    break;
                case "mineshaft":
                    sizesAfter[0] = maps[0].size();
                    loadsAfter[0] = lc.size() - loaded0;
                    tiles.record();
                    counts[1] = intersects(1);
                    sizesBefore[1] = maps[1].size();
                    break;
                case "village":
                    sizesAfter[1] = maps[1].size();
                    loadsAfter[1] = lc.size() - loaded0;
                    tiles.record();
                    counts[2] = intersects(2);
                    sizesBefore[2] = maps[2].size();
                    break;
                case "stronghold":
                    sizesAfter[2] = maps[2].size();
                    loadsAfter[2] = lc.size() - loaded0;
                    tiles.record();
                    counts[3] = intersects(3);
                    sizesBefore[3] = maps[3].size();
                    break;
                case "scattered":
                    sizesAfter[3] = maps[3].size();
                    loadsAfter[3] = lc.size() - loaded0;
                    tiles.record();
                    break;
                default:
                    break;
            }

            int si = stageIndex(stage);

            if (si < 0) throw new IllegalStateException("unknown stage " + stage);

            markers.add(new long[] {si, seed, writes, this.recordsTotal + this.wbuf.size() / 16});
        }

        private int stageIndex(String stage)
        {
            for (int i = 0; i < stages.length; ++i)
            {
                if (stages[i].equals(stage)) return i;
            }

            return -1;
        }

        /** Sizeable starts in generator g's map whose box intersects the populate box. */
        private int intersects(int g)
        {
            int cx8 = cx * 16 + 8, cz8 = cz * 16 + 8;
            int n = 0;
            Iterator<?> it = maps[g].values().iterator();

            while (it.hasNext())
            {
                StructureStart s = (StructureStart)it.next();

                if (s.isSizeableStructure() && s.getBoundingBox().intersectsWith(cx8, cz8, cx8 + 15, cz8 + 15)) ++n;
            }

            return n;
        }

        @SuppressWarnings("unchecked")
        private List<Chunk> loadedChunks()
        {
            try
            {
                if (loadedChunksField == null)
                {
                    loadedChunksField = ChunkProviderServer.class.getDeclaredField("loadedChunks");
                    loadedChunksField.setAccessible(true);
                }

                return (List<Chunk>)loadedChunksField.get(cps);
            }
            catch (Exception e)
            {
                throw new IllegalStateException(e);
            }
        }

        private void writeCall() throws IOException
        {
            /* dim 0 keeps the 80-byte header every recording holds; the other
             * dimensions prepend the populate Random's state after the biome */
            byte[] h = new byte[dim == 0 ? 80 : 88];
            le32(h, 0, cx);
            le32(h, 4, cz);
            le32(h, 8, ws.getBiomeGenForCoords(cx * 16 + 16, cz * 16 + 16).biomeID);
            int p = 12;

            if (dim != 0)
            {
                le64(h, p, startSeed);
                p += 8;
            }

            for (int g = 0; g < 4; ++g, p += 4) le32(h, p, counts[g]);
            for (int g = 0; g < 4; ++g, p += 4) le32(h, p, sizesBefore[g]);
            for (int g = 0; g < 4; ++g, p += 4) le32(h, p, sizesAfter[g]);
            le32(h, p, loadsTotal);
            p += 4;
            for (int g = 0; g < 4; ++g, p += 4) le32(h, p, loadsAfter[g]);
            callsOut.write(h);

            List<Chunk> lc = loadedChunks();
            byte[] lb = new byte[8 * (lc.size() - this.loaded0)];

            for (int i = this.loaded0, q = 0; i < lc.size(); ++i, q += 8)
            {
                le32(lb, q, lc.get(i).xPosition);
                le32(lb, q + 4, lc.get(i).zPosition);
            }

            callsOut.write(lb);
            h = new byte[1];
            h[0] = (byte)markers.size();
            callsOut.write(h);

            for (long[] mk : markers)
            {
                byte[] b = new byte[17];
                b[0] = (byte)mk[0];
                le64(b, 1, mk[1]);
                le32(b, 9, (int)mk[2]);
                le32(b, 13, (int)mk[3]);
                callsOut.write(b);
            }

            h = new byte[4];
            le32(h, 0, writes);
            callsOut.write(h);
            writeEntities(callsOut);
        }

        private void writeEntities(OutputStream out) throws IOException
        {
            List<Entity> now = ws.loadedEntityList;
            List<Entity> fresh = new ArrayList<Entity>();

            for (int i = entity0; i < now.size(); ++i) fresh.add(now.get(i));

            byte[] h = new byte[4];
            le32(h, 0, fresh.size());
            out.write(h);

            for (Entity e : fresh)
            {
                String name = e.getClass().getSimpleName();
                byte[] nb = name.getBytes(StandardCharsets.UTF_8);
                byte[] b = new byte[1 + nb.length + 4 + 24 + 4];
                int p = 0;
                b[p++] = (byte)nb.length;
                System.arraycopy(nb, 0, b, p, nb.length);
                p += nb.length;
                le32(b, p, e.getEntityId());
                p += 4;
                le64(b, p, Double.doubleToRawLongBits(e.posX));
                p += 8;
                le64(b, p, Double.doubleToRawLongBits(e.posY));
                p += 8;
                le64(b, p, Double.doubleToRawLongBits(e.posZ));
                p += 8;
                le32(b, p, Float.floatToRawIntBits(e.rotationYaw));
                out.write(b);
            }
        }
    }

    private static Field loadedChunksField;

    private static void le32(byte[] a, int o, int v)
    {
        a[o] = (byte)v;
        a[o + 1] = (byte)(v >> 8);
        a[o + 2] = (byte)(v >> 16);
        a[o + 3] = (byte)(v >> 24);
    }

    private static void le64(byte[] a, int o, long v)
    {
        for (int i = 0; i < 8; ++i) a[o + i] = (byte)(v >> (8 * i));
    }

    private static void writeLe32(OutputStream o, int v) throws IOException
    {
        o.write(v & 255);
        o.write(v >> 8 & 255);
        o.write(v >> 16 & 255);
        o.write(v >> 24 & 255);
    }

    // ------------------------------------------------------------ tile entities

    /** The tile entity deltas over the loaded region, diffed per interval. */
    static final class Tiles
    {
        private final OutputStream out;
        private final WorldServer ws;
        private final int x0, x1, z0, z1;
        private List<Entry> prev = new ArrayList<Entry>();
        private int entries;

        Tiles(OutputStream out, WorldServer ws, int x0, int x1, int z0, int z1)
        {
            this.out = out;
            this.ws = ws;
            this.x0 = x0;
            this.x1 = x1;
            this.z0 = z0;
            this.z1 = z1;
        }

        /** One interval's record, taken right after it closed. */
        void record() throws IOException
        {
            List<Entry> cur = snapshot();
            List<Entry> delta = new ArrayList<Entry>();
            int i = 0, j = 0;

            while (i < this.prev.size() || j < cur.size())
            {
                Entry a = i < this.prev.size() ? this.prev.get(i) : null;
                Entry b = j < cur.size() ? cur.get(j) : null;
                int c = a == null ? 1 : b == null ? -1 : a.compareTo(b);

                if (c < 0)
                {
                    delta.add(new Entry(a.x, a.y, a.z, 0, "", ""));
                    ++i;
                }
                else if (c > 0)
                {
                    delta.add(b);
                    ++j;
                }
                else
                {
                    if (!a.text.equals(b.text)) delta.add(b);
                    ++i;
                    ++j;
                }
            }

            byte[] h = new byte[4];
            le32(h, 0, delta.size());
            out.write(h);

            for (Entry e : delta)
            {
                byte[] b = new byte[21];
                le32(b, 0, e.x);
                le32(b, 4, e.y);
                le32(b, 8, e.z);
                b[12] = (byte)e.kind;
                le32(b, 13, e.name.length());
                le32(b, 17, e.text.length());
                out.write(b);
                out.write(e.name.getBytes(StandardCharsets.UTF_8));
                out.write(e.text.getBytes(StandardCharsets.UTF_8));
                ++this.entries;
            }

            this.prev = cur;
        }

        int entries()
        {
            return this.entries;
        }

        void close() throws IOException
        {
            this.out.close();
        }

        /** Every tile entity the loaded region holds, in (x, z, y) order. */
        private List<Entry> snapshot()
        {
            List<Entry> out = new ArrayList<Entry>();

            for (int lx = x0; lx <= x1; ++lx)
            {
                for (int lz = z0; lz <= z1; ++lz)
                {
                    Chunk c = ws.getChunkFromChunkCoords(lx, lz);

                    for (Object o : c.chunkTileEntityMap.values())
                    {
                        TileEntity te = (TileEntity)o;
                        int kind = te instanceof TileEntityChest ? 1 : te instanceof TileEntityMobSpawner ? 2 : 3;
                        String name = te.getClass().getSimpleName();
                        NBTTagCompound t = new NBTTagCompound();
                        te.writeToNBT(t);
                        out.add(new Entry(te.field_145851_c, te.field_145848_d, te.field_145849_e, kind, name,
                            StructuresProbe.canon(t).toString()));
                    }
                }
            }

            Collections.sort(out);
            return out;
        }
    }

    /** One tile entity of the region, in the order the delta is written. */
    static final class Entry implements Comparable<Entry>
    {
        final int x, y, z, kind;
        final String name;
        final String text;

        Entry(int x, int y, int z, int kind, String name, String text)
        {
            this.x = x;
            this.y = y;
            this.z = z;
            this.kind = kind;
            this.name = name;
            this.text = text;
        }

        public int compareTo(Entry o)
        {
            if (this.x != o.x) return this.x < o.x ? -1 : 1;
            if (this.z != o.z) return this.z < o.z ? -1 : 1;
            if (this.y != o.y) return this.y < o.y ? -1 : 1;
            return 0;
        }
    }
}