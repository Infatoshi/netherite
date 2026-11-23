package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.security.MessageDigest;
import java.util.Random;
import java.util.zip.GZIPOutputStream;
import net.minecraft.block.Block;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.world.WorldServer;
import net.minecraft.world.biome.BiomeGenBase;
import net.minecraft.world.chunk.Chunk;
import net.minecraft.world.chunk.storage.ExtendedBlockStorage;
import net.minecraft.world.gen.ChunkProviderGenerate;
import net.minecraft.world.gen.MapGenBase;

/**
 * Stage-by-stage dump of raw overworld chunk generation, the reference for the
 * native worldgen port. For each chunk it replays ChunkProviderGenerate.provideChunk
 * one step at a time on a fresh provider and records the block array after each
 * step:
 *
 *   genbiomes  10x10 biome ids the terrain noise reads (1/4 resolution)
 *   terrain    block ids after the density pass (stone, water, air)
 *   biomes     16x16 biome ids after the voronoi zoom
 *   surface    ids + metas after the per-biome surface pass
 *   caves      ids + metas after MapGenCaves
 *   ravines    ids + metas after MapGenRavine (the raw chunk)
 *   light      height maps and sky light after Chunk.generateSkylightMap
 *
 * The last stage is checked against provideChunk on a second fresh provider, so
 * the staging cannot drift from the real generator. Runs on its own thread (the
 * OTHER role) while the server is parked, so no CLIENT or SERVER RNG stream moves.
 *
 * Output DIR/manifest.json and DIR/STAGE.bin.gz. Chunks are written cx-major
 * (for cx in x0..x1, for cz in z0..z1). Block arrays are in Java index order,
 * index = x << 12 | z << 8 | y, ids as little-endian uint16 then metas as uint8.
 * The light stage, per chunk, is heightMap (256 int32 LE, index z << 4 | x),
 * heightMapMinimum (int32 LE), precipitationHeightMap (256 int32 LE), the
 * section mask (uint16 LE, bit s set when storageArrays[s] is not null) and sky
 * light (65536 bytes, index x << 12 | z << 8 | y, 0 where the section is absent).
 */
final class ChunkDump
{
    static final String[] STAGES = {"genbiomes", "terrain", "biomes", "surface", "caves", "ravines", "light"};
    private static final int LIGHT_BYTES = 256 * 4 + 4 + 256 * 4 + 2 + 65536;

    private ChunkDump() {}

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
        }, "Oracle ChunkDump");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        int x0 = cmd.get("x0").getAsInt(), z0 = cmd.get("z0").getAsInt();
        int x1 = cmd.get("x1").getAsInt(), z1 = cmd.get("z1").getAsInt();
        int step = cmd.has("step") ? cmd.get("step").getAsInt() : 1;
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();
        Trace.restart(); // drop the oracle's own spawn-area traces; this dump starts the file
        WorldServer ws = server.worldServers[0];
        long seed = ws.getSeed();

        ChunkProviderGenerate gen = new ChunkProviderGenerate(ws, seed, false);
        ChunkProviderGenerate ref = new ChunkProviderGenerate(ws, seed, false);
        Random rand = (Random)field(ChunkProviderGenerate.class, "rand").get(gen);
        Field genBiomes = field(ChunkProviderGenerate.class, "biomesForGeneration");
        MapGenBase caves = (MapGenBase)field(ChunkProviderGenerate.class, "caveGenerator").get(gen);
        MapGenBase ravines = (MapGenBase)field(ChunkProviderGenerate.class, "ravineGenerator").get(gen);

        DataOutputStream[] out = new DataOutputStream[STAGES.length];
        MessageDigest[] md = new MessageDigest[STAGES.length];
        JsonObject digests = new JsonObject();
        JsonArray[] lists = new JsonArray[STAGES.length];
        for (int s = 0; s < STAGES.length; ++s)
        {
            out[s] = new DataOutputStream(new GZIPOutputStream(new FileOutputStream(new File(dir, STAGES[s] + ".bin.gz")), 1 << 16));
            lists[s] = new JsonArray();
            digests.add(STAGES[s], lists[s]);
        }

        int mismatches = 0;
        StringBuilder where = new StringBuilder();
        for (int cx = x0; cx <= x1; cx += step)
        {
            for (int cz = z0; cz <= z1; cz += step)
            {
                Block[] blocks = new Block[65536];
                byte[] metas = new byte[65536];
                rand.setSeed((long)cx * 341873128712L + (long)cz * 132897987541L);
                gen.func_147424_a(cx, cz, blocks);
                BiomeGenBase[] coarse = (BiomeGenBase[])genBiomes.get(gen);
                emitBiomes(out[0], md(md, 0), coarse, 100);
                emitIds(out[1], md(md, 1), blocks);
                BiomeGenBase[] fine = ws.getWorldChunkManager().loadBlockGeneratorData(null, cx * 16, cz * 16, 16, 16);
                emitBiomes(out[2], md(md, 2), fine, 256);
                gen.func_147422_a(cx, cz, blocks, metas, fine);
                emitBlocks(out[3], md(md, 3), blocks, metas);
                caves.func_151539_a(gen, ws, cx, cz, blocks);
                emitBlocks(out[4], md(md, 4), blocks, metas);
                ravines.func_151539_a(gen, ws, cx, cz, blocks);
                emitBlocks(out[5], md(md, 5), blocks, metas);
                Chunk staged = new Chunk(ws, blocks, metas, cx, cz);
                staged.generateSkylightMap();
                emitLight(out[6], md(md, 6), staged);
                for (int s = 0; s < STAGES.length; ++s) lists[s].add(new JsonPrimitive(hex(md[s].digest())));

                // the self-check regenerates the chunk; trace each chunk once
                Trace.pause();
                Chunk c = ref.provideChunk(cx, cz);
                Trace.resume();
                for (int i = 0; i < 65536 && mismatches < 10; ++i)
                {
                    int x = i >> 12, z = (i >> 8) & 15, y = i & 255;
                    int want = Block.getIdFromBlock(c.func_150810_a(x, y, z)), got = blocks[i] == null ? 0 : Block.getIdFromBlock(blocks[i]);
                    // Chunk stores no meta for air; a cave can leave a stray meta byte under air in the raw array
                    if (want != got || (got != 0 && c.getBlockMetadata(x, y, z) != (metas[i] & 15)))
                    {
                        ++mismatches;
                        where.append(String.format(" chunk(%d,%d) block(%d,%d,%d) provideChunk %d:%d staged %d:%d;", cx, cz, x, y, z, want, c.getBlockMetadata(x, y, z), got, metas[i] & 15));
                    }
                }
                mismatches += checkLight(staged, c, cx, cz, where, 10 - mismatches);
            }
        }
        for (DataOutputStream o : out) o.close();
        if (mismatches != 0) throw new IllegalStateException("staged pipeline differs from provideChunk in " + mismatches + " fields:" + where);

        JsonObject m = new JsonObject();
        m.addProperty("seed", seed);
        m.addProperty("x0", x0);
        m.addProperty("z0", z0);
        m.addProperty("x1", x1);
        m.addProperty("z1", z1);
        m.addProperty("step", step);
        m.addProperty("order", "cx-major: for cx in x0..x1 by step, for cz in z0..z1 by step");
        m.addProperty("layout", "index = x << 12 | z << 8 | y; ids uint16 LE (65536) then metas uint8 (65536); terrain has ids only; biome stages are uint8; light is 256 int32 LE heightMap (z << 4 | x), int32 LE heightMapMinimum, 256 int32 LE precipitationHeightMap, uint16 LE section mask, 65536 sky light bytes (x << 12 | z << 8 | y)");
        m.add("sha1", digests);
        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        w.println(m.toString());
        w.close();

        JsonObject r = new JsonObject();
        r.addProperty("chunks", ((x1 - x0) / step + 1) * ((z1 - z0) / step + 1));
        r.addProperty("dir", dir.getPath());
        return r;
    }

    private static MessageDigest md(MessageDigest[] md, int s) throws Exception
    {
        md[s] = MessageDigest.getInstance("SHA-1");
        return md[s];
    }

    private static void emitBiomes(DataOutputStream o, MessageDigest md, BiomeGenBase[] b, int n) throws Exception
    {
        byte[] a = new byte[n];
        for (int i = 0; i < n; ++i) a[i] = (byte)b[i].biomeID;
        o.write(a);
        md.update(a);
    }

    private static void emitIds(DataOutputStream o, MessageDigest md, Block[] blocks) throws Exception
    {
        byte[] a = new byte[65536 * 2];
        for (int i = 0; i < 65536; ++i)
        {
            int id = blocks[i] == null ? 0 : Block.getIdFromBlock(blocks[i]);
            a[2 * i] = (byte)id;
            a[2 * i + 1] = (byte)(id >> 8);
        }
        o.write(a);
        md.update(a);
    }

    private static void emitBlocks(DataOutputStream o, MessageDigest md, Block[] blocks, byte[] metas) throws Exception
    {
        emitIds(o, md, blocks);
        o.write(metas);
        md.update(metas);
    }

    private static int le32(byte[] a, int k, int v)
    {
        a[k] = (byte)v;
        a[k + 1] = (byte)(v >> 8);
        a[k + 2] = (byte)(v >> 16);
        a[k + 3] = (byte)(v >> 24);
        return k + 4;
    }

    private static void emitLight(DataOutputStream o, MessageDigest md, Chunk c) throws Exception
    {
        byte[] a = new byte[LIGHT_BYTES];
        int k = 0;

        for (int i = 0; i < 256; ++i) k = le32(a, k, c.heightMap[i]);
        k = le32(a, k, c.heightMapMinimum);
        for (int i = 0; i < 256; ++i) k = le32(a, k, c.precipitationHeightMap[i]);
        ExtendedBlockStorage[] ss = c.getBlockStorageArray();
        int mask = 0;
        for (int s = 0; s < 16; ++s)
        {
            if (ss[s] != null) mask |= 1 << s;
        }
        a[k++] = (byte)mask;
        a[k++] = (byte)(mask >> 8);

        for (int x = 0; x < 16; ++x)
        {
            for (int z = 0; z < 16; ++z)
            {
                for (int y = 0; y < 256; ++y)
                {
                    a[k++] = (byte)skylight(ss, x, y, z);
                }
            }
        }
        o.write(a);
        md.update(a);
    }

    private static int skylight(ExtendedBlockStorage[] ss, int x, int y, int z)
    {
        ExtendedBlockStorage st = ss[y >> 4];
        return st == null ? 0 : st.getExtSkylightValue(x, y & 15, z);
    }

    /** Compares the staged chunk's light stage with provideChunk's chunk, field by field. */
    private static int checkLight(Chunk a, Chunk b, int cx, int cz, StringBuilder where, int limit)
    {
        if (limit <= 0) return 0;
        int bad = 0;
        for (int i = 0; i < 256 && bad < limit; ++i)
        {
            if (a.heightMap[i] != b.heightMap[i])
            {
                ++bad;
                where.append(String.format(" chunk(%d,%d) heightMap(%d,%d) provideChunk %d staged %d;", cx, cz, i & 15, i >> 4, b.heightMap[i], a.heightMap[i]));
            }
            if (bad < limit && a.precipitationHeightMap[i] != b.precipitationHeightMap[i])
            {
                ++bad;
                where.append(String.format(" chunk(%d,%d) precipitationHeightMap(%d,%d) provideChunk %d staged %d;", cx, cz, i & 15, i >> 4, b.precipitationHeightMap[i], a.precipitationHeightMap[i]));
            }
        }
        if (bad < limit && a.heightMapMinimum != b.heightMapMinimum)
        {
            ++bad;
            where.append(String.format(" chunk(%d,%d) heightMapMinimum provideChunk %d staged %d;", cx, cz, b.heightMapMinimum, a.heightMapMinimum));
        }
        ExtendedBlockStorage[] sa = a.getBlockStorageArray(), sb = b.getBlockStorageArray();
        for (int s = 0; s < 16 && bad < limit; ++s)
        {
            if ((sa[s] == null) != (sb[s] == null))
            {
                ++bad;
                where.append(String.format(" chunk(%d,%d) section %d provideChunk %s staged %s;", cx, cz, s, sb[s] == null ? "absent" : "present", sa[s] == null ? "absent" : "present"));
            }
        }
        for (int x = 0; x < 16 && bad < limit; ++x)
        {
            for (int z = 0; z < 16 && bad < limit; ++z)
            {
                for (int y = 0; y < 256 && bad < limit; ++y)
                {
                    int va = skylight(sa, x, y, z), vb = skylight(sb, x, y, z);
                    if (va != vb)
                    {
                        ++bad;
                        where.append(String.format(" chunk(%d,%d) sky(%d,%d,%d) provideChunk %d staged %d;", cx, cz, x, y, z, vb, va));
                    }
                }
            }
        }
        return bad;
    }

    private static Field field(Class<?> c, String name) throws Exception
    {
        Field f = c.getDeclaredField(name);
        f.setAccessible(true);
        return f;
    }

    static String hex(byte[] d)
    {
        StringBuilder b = new StringBuilder();
        for (byte x : d) b.append(String.format("%02x", x & 0xff));
        return b.toString();
    }
}
