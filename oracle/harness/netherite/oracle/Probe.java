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
import java.util.Random;
import java.util.zip.GZIPOutputStream;
import net.minecraft.block.Block;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.world.WorldServer;
import net.minecraft.world.chunk.Chunk;
import net.minecraft.world.chunk.NibbleArray;
import net.minecraft.world.chunk.storage.ExtendedBlockStorage;

/**
 * Fuzz probe for the native world core (height maps, sky light, block light):
 * a list of random World.setBlock calls on a region of raw chunks, plus what
 * each one did to the 3x3 chunks around it. The native port replays ops.bin and
 * is checked op by op against the recorded hashes; final.bin.gz is the whole
 * region after the last op, for a second check that does not depend on the
 * intermediate states.
 *
 * Raw means ChunkProviderServer.loadChunk skips populateChunk while
 * Probe.rawChunks is set, so the region is bare terrain (dirt, stone, water)
 * and every op is the only thing that touched those blocks. Chunks are loaded
 * cx-major with cz inner, in a square of radius+ring chunks around (cx,cz) so
 * that every op's 3x3 hash neighborhood and every light update inside it is
 * loaded; the load order is recorded because it decides which chunk generates
 * first and therefore the light state the ops start from.
 *
 * Runs on its own thread (the OTHER role, like ChunkDump) while the server is
 * parked, so no CLIENT or SERVER RNG stream moves.
 *
 * Output DIR/manifest.json, DIR/ops.bin, DIR/final.bin.gz. See the layout
 * strings in the manifest for the byte layout.
 *
 * The "move" kind ("kind":"move") is the reference for the native port of
 * Entity.moveEntity: the same raw-chunk region, scattered with the odd-shaped
 * blocks that have their own collision boxes, and a test entity (ProbeEntity)
 * driven through random moves. Each move's inputs and the whole entity state
 * after it are recorded, so the native engine can replay the moves and be
 * compared step by step. Output DIR/manifest.json, DIR/shapes.bin,
 * DIR/moves.bin.
 */
public final class Probe
{
    /**
     * While true, ChunkProviderServer.loadChunk does not populate a chunk, so
     * every chunk this run loads is raw terrain. Set for the rest of the run.
     */
    public static boolean rawChunks;

    /** Plain block set, in index order. */
    static final int[] PLAIN = {0, 1, 4, 5, 20, 35, 79, 95};
    /** Plain plus glowstone, so the block-light pass has a source to move. */
    static final int[] LIGHT = {0, 1, 4, 5, 20, 35, 79, 95, 89};

    static final long FNV_OFFSET = 0xcbf29ce484222325L;
    static final long FNV_PRIME = 0x100000001b3L;

    /**
     * Bytes per chunk in a hash: ids 2 * 65536, metas, sky light and block light
     * 65536 each, heightMap and precipitationHeightMap 256 * 4 each,
     * heightMapMinimum 4, section mask 2.
     */
    static final int CHUNK_BYTES = 2 * 65536 + 3 * 65536 + 2 * 256 * 4 + 4 + 2;

    private Probe() {}

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
        }, "Oracle Probe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        if ("move".equals(cmd.has("kind") ? cmd.get("kind").getAsString() : "setblock"))
        {
            return Move.dump(server, cmd);
        }
        if ("feature".equals(cmd.has("kind") ? cmd.get("kind").getAsString() : "setblock"))
            return FeatureProbe.dump(server, cmd);

        int cx = cmd.get("cx").getAsInt(), cz = cmd.get("cz").getAsInt();
        int radius = cmd.has("radius") ? cmd.get("radius").getAsInt() : 2;
        int ops = cmd.has("ops") ? cmd.get("ops").getAsInt() : 2000;
        long opseed = cmd.has("opseed") ? cmd.get("opseed").getAsLong() : 1L;
        String blocks = cmd.has("blocks") ? cmd.get("blocks").getAsString() : "plain";
        int ring = cmd.has("ring") ? cmd.get("ring").getAsInt() : 1;
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();

        WorldServer ws = server.worldServers[0];
        long seed = ws.getSeed();
        int[] set = "light".equals(blocks) ? LIGHT : PLAIN;
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

        int width = (2 * radius + 1) * 16;
        Random r = new Random(opseed);
        byte[] buf = new byte[CHUNK_BYTES];
        byte[] op = new byte[24];
        OutputStream opsOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "ops.bin")), 1 << 16);
        long prev = 0;
        int moved = 0, changed = 0, lightSkipped = 0;

        for (int i = 0; i < ops; ++i)
        {
            int x = (cx - radius) * 16 + r.nextInt(width);
            int z = (cz - radius) * 16 + r.nextInt(width);
            int y = r.nextInt(256);
            int id = set[r.nextInt(set.length)];
            int meta = id == 35 ? r.nextInt(16) : (id == 5 ? r.nextInt(6) : 0);

            // the guard the light pass inside setBlock will see
            boolean lit = ws.doChunksNearChunkExist(x, y, z, 17);
            boolean did = ws.setBlock(x, y, z, Block.getBlockById(id), meta, 2);
            long hash = hashAround(ws, x >> 4, z >> 4, buf);

            if (i > 0 && hash != prev) ++moved;
            prev = hash;
            if (did) ++changed;
            if (!lit) ++lightSkipped;

            le32(op, 0, x);
            le32(op, 4, y);
            le32(op, 8, z);
            op[12] = (byte)id;
            op[13] = (byte)(id >> 8);
            op[14] = (byte)meta;
            op[15] = (byte)((did ? 1 : 0) | (lit ? 0 : 2));
            le64(op, 16, hash);
            opsOut.write(op);
        }
        opsOut.close();

        Field gap = field(Chunk.class, "isGapLightingUpdated");
        DataOutputStream out = new DataOutputStream(new GZIPOutputStream(new FileOutputStream(new File(dir, "final.bin.gz")), 1 << 16));
        byte[] cols = new byte[256];

        for (int lx = x0; lx <= x1; ++lx)
        {
            for (int lz = z0; lz <= z1; ++lz)
            {
                Chunk c = ws.getChunkFromChunkCoords(lx, lz);
                fillChunkBytes(c, buf);
                writeLe32(out, lx);
                writeLe32(out, lz);
                out.write(buf);

                for (int i = 0; i < 256; ++i) cols[i] = (byte)(c.updateSkylightColumns[i] ? 1 : 0);
                out.write(cols);
                out.write(gap.getBoolean(c) ? 1 : 0);
            }
        }
        out.close();

        JsonObject m = new JsonObject();
        m.addProperty("seed", seed);
        m.addProperty("kind", "setblock");
        m.addProperty("cx", cx);
        m.addProperty("cz", cz);
        m.addProperty("radius", radius);
        m.addProperty("ring", ring);
        m.addProperty("ops", ops);
        m.addProperty("opseed", opseed);
        m.addProperty("blocks", blocks);
        m.addProperty("order", "cx-major: for cx in " + x0 + ".." + x1 + ", for cz in " + z0 + ".." + z1);
        m.add("loaded", loaded);
        m.addProperty("op_layout", "24 bytes per op: x int32 LE, y int32 LE, z int32 LE, id uint16 LE, meta uint8, flags uint8 (bit 0: setBlock changed the block; bit 1: the 17-block light guard was false, so no light pass ran), hash uint64 LE");
        m.addProperty("op_draws", "per op from Random(opseed): x = (cx-radius)*16 + nextInt((2*radius+1)*16); z the same with cz; y = nextInt(256); id = set[nextInt(n)]; meta = nextInt(16) if wool (35), nextInt(6) if planks (5), else 0; then setBlock(x, y, z, id, meta, 2)");
        m.addProperty("op_hash", "FNV-1a 64 (offset 0xcbf29ce484222325, prime 0x100000001b3) over the 3x3 chunks around the op's chunk, dx outer -1..1, dz inner -1..1, chunk bytes as below; a chunk that is not loaded contributes one 0 byte");
        m.addProperty("chunk_bytes", "ids uint16 LE (65536, index x << 12 | z << 8 | y), metas uint8 (65536), sky light uint8 (65536), block light uint8 (65536), heightMap 256 int32 LE, precipitationHeightMap 256 int32 LE (both Java index order z << 4 | x), heightMapMinimum int32 LE, section mask uint16 LE (bit s set when storageArrays[s] != null); cells in absent sections read as 0");
        m.addProperty("final_layout", "final.bin.gz, per loaded chunk in load order: cx int32 LE, cz int32 LE, the chunk bytes above, updateSkylightColumns 256 bytes (0/1), isGapLightingUpdated 1 byte");
        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        w.println(m.toString());
        w.close();

        JsonObject res = new JsonObject();
        res.addProperty("dir", dir.getPath());
        res.addProperty("chunks", loaded.size());
        res.addProperty("ops", ops);
        res.addProperty("changed", changed);
        res.addProperty("moved", moved);
        res.addProperty("lightSkipped", lightSkipped);
        return res;
    }

    /** FNV-1a 64 of the 3x3 chunks around (chx,cz) as an op's chunk. */
    static long hashAround(WorldServer ws, int chx, int chz, byte[] buf)
    {
        long h = FNV_OFFSET;

        for (int dx = -1; dx <= 1; ++dx)
        {
            for (int dz = -1; dz <= 1; ++dz)
            {
                int ax = chx + dx, az = chz + dz;

                if (!ws.getChunkProvider().chunkExists(ax, az))
                {
                    h *= FNV_PRIME; // one zero byte for a chunk that is not loaded
                    continue;
                }

                fillChunkBytes(ws.getChunkFromChunkCoords(ax, az), buf);

                for (int i = 0; i < buf.length; ++i) h = (h ^ (buf[i] & 255)) * FNV_PRIME;
            }
        }

        return h;
    }

    /**
     * The chunk bytes of the manifest's chunk_bytes layout, into out (CHUNK_BYTES long).
     */
    static void fillChunkBytes(Chunk c, byte[] out)
    {
        ExtendedBlockStorage[] sa = c.getBlockStorageArray();
        byte[][] lsb = new byte[16][];
        NibbleArray[] msb = new NibbleArray[16], meta = new NibbleArray[16], sky = new NibbleArray[16], blk = new NibbleArray[16];
        int mask = 0;

        for (int s = 0; s < 16; ++s)
        {
            ExtendedBlockStorage e = sa[s];

            if (e == null) continue;
            mask |= 1 << s;
            lsb[s] = e.getBlockLSBArray();
            msb[s] = e.getBlockMSBArray();
            meta[s] = e.getMetadataArray();
            sky[s] = e.getSkylightArray();
            blk[s] = e.getBlocklightArray();
        }

        int ip = 0, mp = 65536 * 2, sp = 65536 * 3, bp = 65536 * 4;

        for (int x = 0; x < 16; ++x)
        {
            for (int z = 0; z < 16; ++z)
            {
                for (int y = 0; y < 256; ++y)
                {
                    int s = y >> 4, yy = y & 15;
                    int id = lsb[s] == null ? 0 : lsb[s][yy << 8 | z << 4 | x] & 255;

                    if (msb[s] != null) id |= msb[s].get(x, yy, z) << 8;
                    out[ip++] = (byte)id;
                    out[ip++] = (byte)(id >> 8);
                    out[mp++] = (byte)(meta[s] == null ? 0 : meta[s].get(x, yy, z));
                    out[sp++] = (byte)(sky[s] == null ? 0 : sky[s].get(x, yy, z));
                    out[bp++] = (byte)(blk[s] == null ? 0 : blk[s].get(x, yy, z));
                }
            }
        }

        fillChunkTail(c, out);
    }

    /** The layout's part after the cells: the height maps, heightMapMinimum
     * and the section mask. */
    static void fillChunkTail(Chunk c, byte[] out)
    {
        ExtendedBlockStorage[] sa = c.getBlockStorageArray();
        int mask = 0;
        for (int s = 0; s < 16; ++s) if (sa[s] != null) mask |= 1 << s;

        int p = 65536 * 5; // end of block light: heightMap then precipitationHeightMap

        for (int i = 0; i < 256; ++i)
        {
            le32(out, p, c.heightMap[i]);
            le32(out, p + 1024, c.precipitationHeightMap[i]);
            p += 4;
        }

        le32(out, p + 1024, c.heightMapMinimum);
        out[p + 1028] = (byte)mask;
        out[p + 1029] = (byte)(mask >> 8);
    }

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

    private static Field field(Class<?> c, String name) throws Exception
    {
        Field f = c.getDeclaredField(name);
        f.setAccessible(true);
        return f;
    }
}