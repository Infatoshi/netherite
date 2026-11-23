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
import net.minecraft.world.gen.ChunkProviderEnd;
import net.minecraft.world.gen.ChunkProviderHell;
import net.minecraft.world.gen.MapGenBase;

/**
 * Stage-by-stage dump of raw chunk generation for the Nether and the End, the
 * reference for the native port of their providers. Like ChunkDump, but the
 * dimensions have different pipelines:
 *
 *   nether (dim -1): terrain  func_147419_a (density pass, lava ocean below y=32)
 *                    surface  func_147418_b (soul sand, gravel, lava ocean)
 *                    caves    MapGenCavesHell
 *   end    (dim  1): biomes   the 16x16 biome ids provideChunk loads
 *                    terrain  func_147420_a (density pass, end stone)
 *                    surface  func_147421_b
 *
 * The Nether has no biome noise (WorldChunkManagerHell returns one biome for
 * every position) and the Nether's stages never read a biome array, so its dump
 * has no biomes stage. The End's func_147420_a ignores its biome argument too,
 * but provideChunk loads the array and puts it in the chunk, so the End records
 * it: it costs one byte per block and pins what the native side fills in.
 *
 * Every block stage is ids then metas, as in ChunkDump. The last stage is
 * checked against provideChunk on a second fresh provider, so the staging
 * cannot drift from the real generator. Every chunk is compared: the Nether
 * fortress is built in populate (MapGenNetherBridge only records its starts
 * while generating) and is a separate lane, so it never reaches this array.
 *
 * Runs on its own thread (the OTHER role) while the server is parked, so no
 * CLIENT or SERVER RNG stream moves.
 *
 * Output DIR/manifest.json and DIR/STAGE.bin.gz. Chunks are written cx-major
 * (for cx in x0..x1, for cz in z0..z1). Both dimensions are 128 blocks high, so
 * the block arrays are in Java index order, index = x << 11 | z << 7 | y, 32768
 * cells, ids as little-endian uint16 then metas as uint8.
 */
final class DimDump
{
    static final String[] NETHER = {"terrain", "surface", "caves"};
    static final String[] END = {"biomes", "terrain", "surface"};

    private DimDump() {}

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
        }, "Oracle DimDump");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        int dim = cmd.get("dim").getAsInt();
        int x0 = cmd.get("x0").getAsInt(), z0 = cmd.get("z0").getAsInt();
        int x1 = cmd.get("x1").getAsInt(), z1 = cmd.get("z1").getAsInt();
        int step = cmd.has("step") ? cmd.get("step").getAsInt() : 1;
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();
        Trace.restart(); // drop the oracle's own spawn-area traces; this dump starts the file
        WorldServer ws = server.worldServerForDimension(dim);
        long seed = ws.getSeed();
        String[] stages = dim == -1 ? NETHER : END;

        DataOutputStream[] out = new DataOutputStream[stages.length];
        MessageDigest[] md = new MessageDigest[stages.length];
        JsonObject digests = new JsonObject();
        JsonArray[] lists = new JsonArray[stages.length];
        for (int s = 0; s < stages.length; ++s)
        {
            out[s] = new DataOutputStream(new GZIPOutputStream(new FileOutputStream(new File(dir, stages[s] + ".bin.gz")), 1 << 16));
            lists[s] = new JsonArray();
            digests.add(stages[s], lists[s]);
        }

        // The staged pipeline runs on its own providers; the self-check uses a
        // third so the staged provider's own rand and noise state never leaks in.
        Random rand;
        int mismatches = 0;
        StringBuilder where = new StringBuilder();
        if (dim == -1)
        {
            ChunkProviderHell gen = new ChunkProviderHell(ws, seed);
            ChunkProviderHell ref = new ChunkProviderHell(ws, seed);
            rand = (Random)field(ChunkProviderHell.class, "hellRNG").get(gen);
            MapGenBase caves = (MapGenBase)field(ChunkProviderHell.class, "netherCaveGenerator").get(gen);
            for (int cx = x0; cx <= x1; cx += step)
            {
                for (int cz = z0; cz <= z1; cz += step)
                {
                    Block[] blocks = new Block[32768];
                    byte[] metas = new byte[32768];
                    rand.setSeed((long)cx * 341873128712L + (long)cz * 132897987541L);
                    gen.func_147419_a(cx, cz, blocks);
                    emitBlocks(out[0], md(md, 0), blocks, metas);
                    gen.func_147418_b(cx, cz, blocks);
                    emitBlocks(out[1], md(md, 1), blocks, metas);
                    caves.func_151539_a(gen, ws, cx, cz, blocks);
                    emitBlocks(out[2], md(md, 2), blocks, metas);
                    for (int s = 0; s < stages.length; ++s) lists[s].add(new JsonPrimitive(hex(md[s].digest())));

                    Trace.pause();
                    Chunk c = ref.provideChunk(cx, cz);
                    Trace.resume();
                    // A fortress never lands in provideChunk's array: genNetherBridge
                    // is a MapGenStructure, so func_151539_a only records starts and the
                    // blocks are placed later, in populate. The raw chunk is terrain,
                    // surface and caves for every chunk, fortress near or not, so every
                    // one is compared here.
                    mismatches += checkBlocks(c, blocks, metas, cx, cz, where, 10 - mismatches);
                }
            }
        }
        else
        {
            ChunkProviderEnd gen = new ChunkProviderEnd(ws, seed);
            ChunkProviderEnd ref = new ChunkProviderEnd(ws, seed);
            rand = (Random)field(ChunkProviderEnd.class, "endRNG").get(gen);
            Field biomesForGeneration = field(ChunkProviderEnd.class, "biomesForGeneration");
            for (int cx = x0; cx <= x1; cx += step)
            {
                for (int cz = z0; cz <= z1; cz += step)
                {
                    Block[] blocks = new Block[32768];
                    byte[] metas = new byte[32768];
                    rand.setSeed((long)cx * 341873128712L + (long)cz * 132897987541L);
                    // provideChunk loads the biome array before the density pass; the staged
                    // run does the same so the noise sees the same provider state.
                    BiomeGenBase[] biomes = ws.getWorldChunkManager().loadBlockGeneratorData(null, cx * 16, cz * 16, 16, 16);
                    biomesForGeneration.set(gen, biomes);
                    emitBiomes(out[0], md(md, 0), biomes, 256);
                    gen.func_147420_a(cx, cz, blocks, biomes);
                    emitBlocks(out[1], md(md, 1), blocks, metas);
                    gen.func_147421_b(cx, cz, blocks, biomes);
                    emitBlocks(out[2], md(md, 2), blocks, metas);
                    for (int s = 0; s < stages.length; ++s) lists[s].add(new JsonPrimitive(hex(md[s].digest())));

                    Trace.pause();
                    Chunk c = ref.provideChunk(cx, cz);
                    Trace.resume();
                    mismatches += checkBlocks(c, blocks, metas, cx, cz, where, 10 - mismatches);
                }
            }
        }
        for (DataOutputStream o : out) o.close();
        if (mismatches != 0) throw new IllegalStateException("staged pipeline differs from provideChunk in " + mismatches + " fields:" + where);

        JsonObject m = new JsonObject();
        m.addProperty("dim", dim);
        m.addProperty("seed", seed);
        m.addProperty("x0", x0);
        m.addProperty("z0", z0);
        m.addProperty("x1", x1);
        m.addProperty("z1", z1);
        m.addProperty("step", step);
        m.addProperty("stages", String.join(" ", stages));
        m.addProperty("order", "cx-major: for cx in x0..x1 by step, for cz in z0..z1 by step");
        m.addProperty("layout", "index = x << 11 | z << 7 | y, 128 high, 32768 cells; ids uint16 LE (32768) then metas uint8 (32768); the biomes stage is 256 uint8, index x + z * 16");
        m.add("sha1", digests);
        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        w.println(m.toString());
        w.close();

        JsonObject r = new JsonObject();
        r.addProperty("dim", dim);
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

    private static void emitBlocks(DataOutputStream o, MessageDigest md, Block[] blocks, byte[] metas) throws Exception
    {
        byte[] a = new byte[32768 * 2];
        for (int i = 0; i < 32768; ++i)
        {
            int id = blocks[i] == null ? 0 : Block.getIdFromBlock(blocks[i]);
            a[2 * i] = (byte)id;
            a[2 * i + 1] = (byte)(id >> 8);
        }
        o.write(a);
        md.update(a);
        o.write(metas);
        md.update(metas);
    }

    /**
     * Compares the staged chunk with provideChunk's chunk. A Chunk stores no
     * meta for air, so a stray meta byte under air in the raw array is not a
     * difference (ChunkDump makes the same allowance).
     */
    private static int checkBlocks(Chunk c, Block[] blocks, byte[] metas, int cx, int cz, StringBuilder where, int limit)
    {
        int bad = 0;
        for (int i = 0; i < 32768 && bad < limit; ++i)
        {
            int x = i >> 11, z = (i >> 7) & 15, y = i & 127;
            int got = blocks[i] == null ? 0 : Block.getIdFromBlock(blocks[i]);
            int want = Block.getIdFromBlock(c.func_150810_a(x, y, z));
            if (want != got || (got != 0 && c.getBlockMetadata(x, y, z) != (metas[i] & 15)))
            {
                ++bad;
                where.append(String.format(" chunk(%d,%d) block(%d,%d,%d) provideChunk %d:%d staged %d:%d;", cx, cz, x, y, z, want, c.getBlockMetadata(x, y, z), got, metas[i] & 15));
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
