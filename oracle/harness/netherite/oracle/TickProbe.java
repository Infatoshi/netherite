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
import java.util.Random;
import java.util.zip.GZIPOutputStream;
import net.minecraft.block.Block;
import net.minecraft.entity.Entity;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.world.IWorldAccess;
import net.minecraft.world.World;
import net.minecraft.world.WorldServer;
import net.minecraft.world.chunk.Chunk;

/**
 * Random block tick probe: what one block's updateTick does to the world.
 *
 * The region is loaded raw (Probe.rawChunks, no population), like the feature
 * probe. A scene is a small block arrangement around an anchor; the ticked
 * block always sits at the anchor. Each case draws a scene, a variant of that
 * scene and two seeds from Random(opseed), takes an anchor from a fixed slot
 * table, rebuilds the scene there with setBlock(flag 2) writes, seeds the
 * world Random to the case seed and calls block.updateTick(ws, x, y, z, new
 * Random(tickSeed)). Every write the tick makes is recorded through
 * Rows.onBlock (the feature probe's write stream), the 3x3 hash is taken after
 * the case, and the world Random's 48-bit state is recorded so the native
 * port's draw count is checked, not just its writes.
 *
 * The scene builds are recorded once per (scene, variant) as anchor-relative
 * offsets (every op is setBlock flag 2), so the native replay rebuilds the
 * same world before each tick. Anchors come from a fixed slot table, and a
 * slot only ever holds one scene, so every case starts inside the box its own
 * scene's ops clear, not inside the case before it.
 *
 * Runs on its own thread (the OTHER role, like Probe and FeatureProbe) while
 * the server is parked, so no CLIENT or SERVER RNG stream moves; the shared
 * Math.random stream is replaced for the sweep the way DropsProbe replaces it.
 *
 * Output DIR/manifest.json, DIR/scenes.bin, DIR/cases.bin, DIR/writes.bin,
 * DIR/final.bin.gz. See the layout strings in the manifest.
 */
public final class TickProbe
{
    /** The ticked block and how many variants a scene has; the builder reads
     * the variant itself. */
    static final class SceneDef
    {
        final String name;
        final Block block;
        final int variants;

        SceneDef(String name, Block block, int variants)
        {
            this.name = name;
            this.block = block;
            this.variants = variants;
        }
    }

    /** The scene table, in dispatch order. */
    static final SceneDef[] SCENES = {
        new SceneDef("grass", Blocks.grass, 6),
        new SceneDef("mycelium", Blocks.mycelium, 6),
        new SceneDef("leaves", Blocks.leaves, 8),
        new SceneDef("leaves2", Blocks.leaves2, 3),
        new SceneDef("crops", Blocks.wheat, 8),
        new SceneDef("sapling", Blocks.sapling, 12),
        new SceneDef("cactus", Blocks.cactus, 5),
        new SceneDef("reed", Blocks.reeds, 6),
        new SceneDef("vine", Blocks.vine, 8),
        new SceneDef("stem_pumpkin", Blocks.pumpkin_stem, 4),
        new SceneDef("stem_melon", Blocks.melon_stem, 4),
        new SceneDef("cocoa", Blocks.cocoa, 6),
        new SceneDef("netherwart", Blocks.nether_wart, 4),
        new SceneDef("mushroom_brown", Blocks.brown_mushroom, 4),
        new SceneDef("mushroom_red", Blocks.red_mushroom, 4),
        new SceneDef("ice", Blocks.ice, 4),
        new SceneDef("snow", Blocks.snow_layer, 6),
        new SceneDef("farmland", Blocks.farmland, 6),
        new SceneDef("redstone_ore", Blocks.redstone_ore, 1),
        new SceneDef("lit_redstone_ore", Blocks.lit_redstone_ore, 1),
    };

    /** The anchor slots: 6 columns x 6 rows of stride 13, two y levels. */
    static final int SLOTS = 72;

    private TickProbe() {}

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
        }, "Oracle Tick Probe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    /** One (scene, variant)'s ops, recorded as anchor-relative offsets. */
    static final class SceneOps
    {
        final java.util.List<byte[]> ops = new ArrayList<byte[]>();
        boolean recorded;
    }

    static SceneOps[][] ops;
    static boolean recording;
    static OutputStream sink;
    static int writes;

    static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        int cx = cmd.get("cx").getAsInt(), cz = cmd.get("cz").getAsInt();
        int radius = cmd.has("radius") ? cmd.get("radius").getAsInt() : 2;
        int ring = cmd.has("ring") ? cmd.get("ring").getAsInt() : 2;
        int cases = cmd.has("cases") ? cmd.get("cases").getAsInt() : 20000;
        long opseed = cmd.has("opseed") ? cmd.get("opseed").getAsLong() : 2L;
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();

        WorldServer ws = server.worldServers[0];
        long seed = ws.getSeed();
        int skylightSubtracted = ws.skylightSubtracted;
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

        ops = new SceneOps[SCENES.length][];

        for (int s = 0; s < SCENES.length; ++s)
        {
            ops[s] = new SceneOps[SCENES[s].variants];

            for (int v = 0; v < SCENES[s].variants; ++v) ops[s][v] = new SceneOps();
        }

        Rows.writeListener = new Rows.WriteListener()
        {
            public void onWrite(World w, int x, int y, int z, int id, int meta)
            {
                if (sink != null) TickProbe.record(x, y, z, id, meta);
            }
        };

        MathStream math = new MathStream();
        math.install();

        OutputStream scenesOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "scenes.bin")), 1 << 16);
        OutputStream casesOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "cases.bin")), 1 << 16);
        OutputStream writesOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "writes.bin")), 1 << 16);
        Random r = new Random(opseed);
        byte[] buf = new byte[Probe.CHUNK_BYTES];
        byte[] cbuf = new byte[52];
        int[] sceneCases = new int[SCENES.length];
        int[] sceneWrites = new int[SCENES.length];
        int totalWrites = 0;
        long prev = 0;
        int moved = 0;

        try
        {
            for (int i = 0; i < cases; ++i)
            {
                int si = r.nextInt(SCENES.length);
                int variant = r.nextInt(SCENES[si].variants);
                long caseSeed = r.nextLong();
                long tickSeed = r.nextLong();

                int slot = slotFor(si, sceneCases[si]++);
                int ax = (cx - radius) * 16 + 8 + (slot % 6) * 13;
                int az = (cz - radius) * 16 + 8 + ((slot / 6) % 6) * 13;
                int ay = slot >= 36 ? 110 : 80;

                SceneOps so = ops[si][variant];
                Build b = new Build(ax, ay, az, so);

                if (!so.recorded)
                {
                    so.recorded = true;
                    recording = true;
                    scene(b, ws, si, variant);
                    recording = false;

                    byte[] head = new byte[8];
                    head[0] = (byte)si;
                    head[1] = (byte)(si >> 8);
                    head[2] = (byte)variant;
                    head[3] = (byte)(variant >> 8);
                    le32(head, 4, so.ops.size());
                    scenesOut.write(head);

                    for (byte[] o : so.ops) scenesOut.write(o);
                }
                else
                {
                    for (byte[] o : so.ops)
                        ws.setBlock(ax + o[0], ay + o[1], az + o[2], Block.getBlockById((o[3] & 255) | (o[4] << 8)), o[5], 2);
                }

                ws.rand.setSeed(caseSeed);
                Random tick = new Random(tickSeed);
                Capture capture = new Capture();
                ws.addWorldAccess(capture);
                sink = writesOut;
                writes = 0;

                try
                {
                    ws.getBlock(ax, ay, az).updateTick(ws, ax, ay, az, tick);
                }
                finally
                {
                    ws.removeWorldAccess(capture);
                    sink = null;
                }

                for (Entity e : capture.spawned) takeOut(ws, e);

                long hash = Probe.hashAround(ws, ax >> 4, az >> 4, buf);

                if (i > 0 && hash != prev) ++moved;
                prev = hash;
                sceneWrites[si] += writes;
                totalWrites += writes;

                cbuf[0] = (byte)si;
                cbuf[1] = (byte)(si >> 8);
                cbuf[2] = (byte)variant;
                cbuf[3] = (byte)(variant >> 8);
                le32(cbuf, 4, ax);
                le32(cbuf, 8, ay);
                le32(cbuf, 12, az);
                le64(cbuf, 16, caseSeed);
                le64(cbuf, 24, tickSeed);
                le32(cbuf, 32, writes);
                le64(cbuf, 36, hash);
                le64(cbuf, 44, worldRandState(ws.rand));
                casesOut.write(cbuf);
            }
        }
        finally
        {
            math.uninstall();
            Rows.writeListener = null;
            scenesOut.close();
            casesOut.close();
            writesOut.close();
        }

        Field gap = field(Chunk.class, "isGapLightingUpdated");
        DataOutputStream out = new DataOutputStream(new GZIPOutputStream(new FileOutputStream(new File(dir, "final.bin.gz")), 1 << 16));
        byte[] cols = new byte[256];

        for (int lx = x0; lx <= x1; ++lx)
        {
            for (int lz = z0; lz <= z1; ++lz)
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

        JsonArray sceneList = new JsonArray();

        for (int s = 0; s < SCENES.length; ++s)
        {
            JsonObject o = new JsonObject();
            o.addProperty("name", SCENES[s].name);
            o.addProperty("block", Block.getIdFromBlock(SCENES[s].block));
            o.addProperty("variants", SCENES[s].variants);
            o.addProperty("cases", sceneCases[s]);
            o.addProperty("writes", sceneWrites[s]);
            sceneList.add(o);
        }

        JsonObject m = new JsonObject();
        m.addProperty("seed", ws.getSeed());
        m.addProperty("kind", "ticks");
        m.addProperty("cx", cx);
        m.addProperty("cz", cz);
        m.addProperty("radius", radius);
        m.addProperty("ring", ring);
        m.addProperty("cases", cases);
        m.addProperty("opseed", opseed);
        m.addProperty("skylightSubtracted", skylightSubtracted);
        m.addProperty("order", "cx-major: for cx in " + x0 + ".." + x1 + ", for cz in " + z0 + ".." + z1);
        m.add("loaded", loaded);
        m.add("scenes", sceneList);
        m.addProperty("case_draws", "per case from Random(opseed): scene = nextInt(scenes.length); variant = nextInt(the "
            + "scene's variants); case seed = nextLong() (the world Random is set to it); tick seed = nextLong(); "
            + "block.updateTick(ws, x, y, z, new Random(tickSeed))");
        m.addProperty("anchors", "72 slots: 6 columns x 6 rows of stride 13 starting at (cx-radius)*16 + 8, y 80 for "
            + "slots 0..35 and y 110 for 36..71; scene s uses slots s, s+20, s+40, s+60 (< 72) in turn, so a slot only "
            + "ever holds one scene and every case starts inside the box its own scene's ops clear");
        m.addProperty("scenes_layout", "scenes.bin: per (scene, variant), scene order and variant ascending, first-build "
            + "order: scene uint16 LE, variant uint16 LE, op count uint32 LE, then count x 7 bytes: dx int8, dy int8, "
            + "dz int8, id uint16 LE, meta uint8; every op is World.setBlock(anchor + offset, id, meta, 2)");
        m.addProperty("case_layout", "52 bytes per case: scene uint16 LE, variant uint16 LE, x int32 LE, y int32 LE, "
            + "z int32 LE (the ticked block), case seed int64 LE, tick seed int64 LE, number of writes uint32 LE, "
            + "FNV-1a 64 hash of the 3x3 chunks around (x >> 4, z >> 4) after the case uint64 LE, the world Random's "
            + "48-bit state after the case uint64 LE");
        m.addProperty("write_layout", "16 bytes per write, every case's writes in order: x int32 LE, y int32 LE, z int32 LE, "
            + "id uint16 LE, meta uint8, pad uint8");
        m.addProperty("write_rule", "every call Rows.onBlock gets from World.setBlock and setBlockMetadataWithNotify that "
            + "changed something, in call order; a metadata-only write reports id -1, stored as 0xffff");
        m.addProperty("hash", "the setblock probe's: FNV-1a 64 (offset 0xcbf29ce484222325, prime 0x100000001b3) over the "
            + "3x3 chunks around the ticked block's chunk, dx outer -1..1, dz inner -1..1; a chunk that is not loaded "
            + "contributes one 0 byte");
        m.addProperty("chunk_bytes", "the setblock probe's: ids uint16 LE (65536, index x << 12 | z << 8 | y), metas uint8 "
            + "(65536), sky light uint8 (65536), block light uint8 (65536), heightMap 256 int32 LE, "
            + "precipitationHeightMap 256 int32 LE (both Java index order z << 4 | x), heightMapMinimum int32 LE, "
            + "section mask uint16 LE");
        m.addProperty("final_layout", "final.bin.gz, per loaded chunk in load order: cx int32 LE, cz int32 LE, the chunk "
            + "bytes above, updateSkylightColumns 256 bytes (0/1), isGapLightingUpdated 1 byte");
        m.addProperty("world_rand", "the recorded 48-bit state is java.util.Random's internal seed after the case: the "
            + "tick's world draws (vine and cocoa gates, every drop path) must spend exactly the same values. The "
            + "tick Random is seeded fresh per case and its draws are the native side's own");
        m.addProperty("math_rand", "the shared Math.random stream is Det.math[OTHER], replaced for the sweep by a Random "
            + "seeded to math_seed; every spawned EntityItem spends four nextDouble there. Entities are taken back "
            + "out and the draws are not part of the record: the native side reproduces no entity state, only the "
            + "world Random's draws");
        m.addProperty("math_seed", MathStream.MATH_SEED);
        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        w.println(m.toString());
        w.close();

        JsonObject res = new JsonObject();
        res.addProperty("dir", dir.getPath());
        res.addProperty("cases", cases);
        res.addProperty("writes", totalWrites);
        res.addProperty("moved", moved);
        res.add("scenes", sceneList);
        return res;
    }

    /** The write the case's tick made, in call order. */
    static void record(int x, int y, int z, int id, int meta)
    {
        byte[] b = new byte[12];

        le32(b, 0, x);
        le32(b, 4, y);
        le32(b, 8, z);

        try
        {
            sink.write(b);
            sink.write(new byte[] {(byte)id, (byte)(id >> 8), (byte)meta, 0});
        }
        catch (IOException e)
        {
            throw new RuntimeException("tick write", e);
        }

        ++writes;
    }

    /** Takes an entity out of the world completely (DropsProbe.takeOut). */
    static void takeOut(WorldServer ws, Entity e)
    {
        e.setDead();

        if (e.addedToChunk)
        {
            Chunk c = ws.getChunkFromChunkCoords(e.chunkCoordX, e.chunkCoordZ);

            if (c != null) c.removeEntity(e);
        }

        ws.loadedEntityList.remove(e);
    }

    /** One op of a scene build: applied now, recorded relative on the first
     * build of its (scene, variant). */
    static void set(WorldServer ws, Build b, int dx, int dy, int dz, Block block, int meta)
    {
        ws.setBlock(b.ax + dx, b.ay + dy, b.az + dz, block, meta, 2);

        if (recording)
        {
            int id = Block.getIdFromBlock(block);
            byte[] o = new byte[7];
            o[0] = (byte)dx;
            o[1] = (byte)dy;
            o[2] = (byte)dz;
            o[3] = (byte)id;
            o[4] = (byte)(id >> 8);
            o[5] = (byte)meta;
            o[6] = 0;
            b.so.ops.add(o);
        }
    }

    static final class Build
    {
        final int ax, ay, az;
        final SceneOps so;

        Build(int ax, int ay, int az, SceneOps so)
        {
            this.ax = ax;
            this.ay = ay;
            this.az = az;
            this.so = so;
        }

        void set(WorldServer ws, int dx, int dy, int dz, Block block, int meta)
        {
            TickProbe.set(ws, this, dx, dy, dz, block, meta);
        }
    }

    /** Clears the scene's box, bottom up. hx and the y range are the scene's
     * own reach, so a slot's residue from earlier cases of the same scene is
     * gone before the build starts. */
    static void clear(WorldServer ws, Build b, int hx, int yBot, int yTop)
    {
        for (int dy = yBot; dy <= yTop; ++dy)
            for (int dz = -hx; dz <= hx; ++dz)
                for (int dx = -hx; dx <= hx; ++dx) b.set(ws, dx, dy, dz, Blocks.air, 0);
    }

    /** Dirt in the 3x3 ring around the anchor and the two layers below it. */
    static void dirtRing(WorldServer ws, Build b, int meta)
    {
        for (int dy = -2; dy <= -1; ++dy)
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx) b.set(ws, dx, dy, dz, Blocks.dirt, meta);

        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                if (dx != 0 || dz != 0) b.set(ws, dx, 0, dz, Blocks.dirt, meta);
    }

    /** A stone roof one layer thick, so the sky light under it is gone. */
    static void roof(WorldServer ws, Build b, int hx, int y)
    {
        for (int dz = -hx; dz <= hx; ++dz)
            for (int dx = -hx; dx <= hx; ++dx) b.set(ws, dx, y, dz, Blocks.stone, 0);
    }

    /** The scenes. (0,0,0) relative to the anchor is the ticked block; the
     * builder writes it last so the tick sees the finished arrangement.
     * Variant counts must match the SCENES table. */
    static void scene(Build b, WorldServer ws, int scene, int variant)
    {
        switch (scene)
        {
        case 0: // grass: spread in the light, die under an opaque block
        case 1: // mycelium: the same shape, converting to itself
        {
            clear(ws, b, 6, -3, 7);
            Block self = scene == 0 ? Blocks.grass : Blocks.mycelium;

            switch (variant)
            {
            case 0:
                dirtRing(ws, b, 0);
                break;

            case 1:
                dirtRing(ws, b, 0);
                b.set(ws, 0, 1, 0, Blocks.stone, 0);
                break;

            case 2:
                dirtRing(ws, b, 0);
                roof(ws, b, 3, 6);
                break;

            case 3:
                dirtRing(ws, b, 0);
                b.set(ws, 0, 1, 0, Blocks.tallgrass, 1);
                b.set(ws, 1, 1, 1, Blocks.tallgrass, 1);
                b.set(ws, -1, 1, 0, Blocks.tallgrass, 1);
                b.set(ws, 0, 1, -1, Blocks.tallgrass, 1);
                break;

            case 4:
                dirtRing(ws, b, 1);
                b.set(ws, 3, 1, 3, Blocks.glowstone, 0);
                break;

            case 5:
                dirtRing(ws, b, 0);
                b.set(ws, 1, 1, 0, Blocks.stone, 0);
                b.set(ws, -1, 1, 0, Blocks.stone, 0);
                b.set(ws, 0, 1, 1, Blocks.stone, 0);
                b.set(ws, 0, 1, -1, Blocks.stone, 0);
                break;
            }

            b.set(ws, 0, 0, 0, self, 0);
            break;
        }

        case 2: // leaves (oak, spruce, birch, jungle)
        {
            clear(ws, b, 5, -2, 7);
            int meta = variant == 2 || variant == 3 ? 8 : variant == 7 ? 1 : 0;

            switch (variant)
            {
            case 1:
                b.set(ws, 3, 0, 0, Blocks.log, 0);
                break;

            case 3:
                b.set(ws, 2, 0, 0, Blocks.log, 0);
                break;

            case 4:
                b.set(ws, 0, 5, 0, Blocks.log, 0);
                break;

            case 5:
                b.set(ws, 0, 4, 0, Blocks.log, 0);
                break;

            case 6:
                b.set(ws, 1, 0, 0, Blocks.leaves, 0);
                break;
            }

            b.set(ws, 0, 0, 0, Blocks.leaves, meta);
            break;
        }

        case 3: // leaves2 (acacia, dark oak)
        {
            clear(ws, b, 5, -2, 7);

            if (variant != 0) b.set(ws, 2, 0, 0, Blocks.log2, 0);

            b.set(ws, 0, 0, 0, Blocks.leaves2, variant == 2 ? 8 : 0);
            break;
        }

        case 4: // crops
        {
            clear(ws, b, 4, -2, 6);
            int meta = variant == 1 ? 3 : variant == 2 ? 7 : variant == 6 ? 5 : 0;

            switch (variant)
            {
            case 3:
            case 6:
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dx = -1; dx <= 1; ++dx)
                        if (dx != 0 || dz != 0) b.set(ws, dx, -1, dz, Blocks.farmland, 7);

                b.set(ws, 1, 0, 0, Blocks.wheat, 7);
                b.set(ws, 0, 0, 1, Blocks.wheat, 7);

                if (variant == 6) b.set(ws, -1, 0, 1, Blocks.wheat, 7);

                break;

            case 4:
                b.set(ws, 0, 1, 0, Blocks.stone, 0);
                break;

            case 5:
                b.set(ws, 0, -1, 0, Blocks.dirt, 0);
                break;

            case 7:
                b.set(ws, 0, 2, 0, Blocks.glowstone, 0);
                meta = 2;
                break;
            }

            b.set(ws, 0, -1, 0, variant == 5 ? Blocks.dirt : Blocks.farmland, 0);
            b.set(ws, 0, 0, 0, Blocks.wheat, meta);
            break;
        }

        case 5: // sapling: each type, single and 2x2, room and without
        {
            clear(ws, b, 8, -1, 36);
            b.set(ws, 0, -1, 0, Blocks.dirt, 0);

            switch (variant)
            {
            case 0:
                b.set(ws, 0, 0, 0, Blocks.sapling, 0);
                break;

            case 1:
                b.set(ws, 0, 0, 0, Blocks.sapling, 8);
                break;

            case 2:
                b.set(ws, 0, 0, 0, Blocks.sapling, 9);
                break;

            case 3:
                b.set(ws, 0, 0, 0, Blocks.sapling, 9);
                b.set(ws, -1, 0, 0, Blocks.sapling, 9);
                b.set(ws, 0, 0, -1, Blocks.sapling, 9);
                b.set(ws, -1, 0, -1, Blocks.sapling, 9);
                break;

            case 4:
                b.set(ws, 0, 0, 0, Blocks.sapling, 10);
                break;

            case 5:
                b.set(ws, 0, 0, 0, Blocks.sapling, 11);
                break;

            case 6:
                b.set(ws, 0, 0, 0, Blocks.sapling, 11);
                b.set(ws, -1, 0, 0, Blocks.sapling, 11);
                b.set(ws, 0, 0, -1, Blocks.sapling, 11);
                b.set(ws, -1, 0, -1, Blocks.sapling, 11);
                break;

            case 7:
                b.set(ws, 0, 0, 0, Blocks.sapling, 12);
                break;

            case 8:
                b.set(ws, 0, 0, 0, Blocks.sapling, 13);
                break;

            case 9:
                b.set(ws, 0, 0, 0, Blocks.sapling, 13);
                b.set(ws, -1, 0, 0, Blocks.sapling, 13);
                b.set(ws, 0, 0, -1, Blocks.sapling, 13);
                b.set(ws, -1, 0, -1, Blocks.sapling, 13);
                break;

            case 10:
                b.set(ws, 0, 5, 0, Blocks.stone, 0);
                b.set(ws, 0, 0, 0, Blocks.sapling, 8);
                break;

            case 11:
                b.set(ws, 0, 0, 0, Blocks.sapling, 9);
                b.set(ws, -1, 0, 0, Blocks.sapling, 9);
                b.set(ws, 0, 0, -1, Blocks.sapling, 9);
                b.set(ws, -1, 0, -1, Blocks.sapling, 0);
                break;
            }

            break;
        }

        case 6: // cactus
        {
            clear(ws, b, 4, -2, 7);
            b.set(ws, 0, -1, 0, Blocks.sand, 0);

            switch (variant)
            {
            case 0:
                b.set(ws, 0, 0, 0, Blocks.cactus, 0);
                break;

            case 1:
                b.set(ws, 0, 0, 0, Blocks.cactus, 15);
                break;

            case 2:
                b.set(ws, 1, 0, 0, Blocks.stone, 0);
                b.set(ws, 0, 0, 0, Blocks.cactus, 0);
                break;

            case 3:
                b.set(ws, 1, 0, 0, Blocks.stone, 0);
                b.set(ws, 0, 0, 0, Blocks.cactus, 15);
                break;

            case 4:
                b.set(ws, 0, 0, 0, Blocks.cactus, 0);
                b.set(ws, 0, 1, 0, Blocks.cactus, 15);
                break;
            }

            break;
        }

        case 7: // reed
        {
            clear(ws, b, 4, -2, 7);
            b.set(ws, 0, -1, 0, Blocks.sand, 0);
            b.set(ws, 1, -1, 0, Blocks.water, 0);

            switch (variant)
            {
            case 0:
                b.set(ws, 0, 0, 0, Blocks.reeds, 0);
                break;

            case 1:
                b.set(ws, 0, 0, 0, Blocks.reeds, 15);
                break;

            case 2:
                b.set(ws, 0, 0, 0, Blocks.reeds, 15);
                b.set(ws, 0, 1, 0, Blocks.reeds, 15);
                break;

            case 3:
                b.set(ws, 0, 0, 0, Blocks.reeds, 15);
                b.set(ws, 0, 1, 0, Blocks.reeds, 15);
                b.set(ws, 0, 2, 0, Blocks.reeds, 15);
                break;

            case 4:
                b.set(ws, 0, -1, 0, Blocks.grass, 0);
                b.set(ws, 1, -1, 0, Blocks.air, 0);
                b.set(ws, 0, 0, 0, Blocks.reeds, 0);
                b.set(ws, 0, 1, 0, Blocks.reeds, 0);
                break;

            case 5:
                b.set(ws, 0, 0, 0, Blocks.reeds, 0);
                b.set(ws, 0, 1, 0, Blocks.reeds, 0);
                break;
            }

            break;
        }

        case 8: // vine
        {
            clear(ws, b, 4, -2, 7);
            int meta = 0;

            switch (variant)
            {
            case 0:
                b.set(ws, 0, 0, 1, Blocks.stone, 0);
                b.set(ws, 0, 0, 0, Blocks.vine, 1);
                break;

            case 1:
                b.set(ws, 0, 0, 1, Blocks.stone, 0);
                b.set(ws, 0, 1, 1, Blocks.stone, 0);
                b.set(ws, 0, 0, 0, Blocks.vine, 1);
                break;

            case 2:
                b.set(ws, 0, 0, 1, Blocks.stone, 0);
                b.set(ws, 1, 0, 0, Blocks.stone, 0);
                b.set(ws, 0, 0, 0, Blocks.vine, 1 | 8);
                break;

            case 3:
                b.set(ws, 0, 0, 1, Blocks.stone, 0);
                b.set(ws, 3, 0, 0, Blocks.stone, 0);
                b.set(ws, -3, 0, 0, Blocks.stone, 0);
                b.set(ws, 0, 0, -3, Blocks.stone, 0);
                b.set(ws, 0, 0, 0, Blocks.vine, 1);
                b.set(ws, 2, 0, 0, Blocks.vine, 8);
                b.set(ws, -2, 0, 0, Blocks.vine, 2);
                b.set(ws, 0, 0, -2, Blocks.vine, 4);
                b.set(ws, 1, 0, 2, Blocks.vine, 1);
                break;

            case 4:
                b.set(ws, 0, 0, 1, Blocks.stone, 0);
                b.set(ws, 0, 1, 1, Blocks.stone, 0);
                b.set(ws, 0, 1, 0, Blocks.vine, 1);
                b.set(ws, 0, 0, 0, Blocks.vine, 1);
                break;

            case 5:
                b.set(ws, 0, 0, 1, Blocks.stone, 0);
                b.set(ws, 0, -1, 1, Blocks.stone, 0);
                b.set(ws, 0, -1, 0, Blocks.vine, 1);
                b.set(ws, 0, 0, 0, Blocks.vine, 1);
                break;

            case 6:
                b.set(ws, -1, 0, 0, Blocks.stone, 0);
                b.set(ws, 0, 0, 0, Blocks.vine, 2);
                break;

            case 7:
                b.set(ws, 0, 0, 1, Blocks.stone, 0);
                b.set(ws, 1, 0, 0, Blocks.stone, 0);
                b.set(ws, 0, 0, 0, Blocks.vine, 1);
                break;
            }

            break;
        }

        case 9: // pumpkin stem
        case 10: // melon stem
        {
            clear(ws, b, 4, -2, 6);
            Block stem = scene == 9 ? Blocks.pumpkin_stem : Blocks.melon_stem;
            Block fruit = scene == 9 ? Blocks.pumpkin : Blocks.melon_block;
            b.set(ws, 0, -1, 0, Blocks.farmland, 0);
            int meta = 7;

            switch (variant)
            {
            case 0:
                b.set(ws, 1, -1, 0, Blocks.dirt, 0);
                break;

            case 1:
                b.set(ws, 1, 0, 0, fruit, 0);
                break;

            case 2:
                meta = 3;
                break;

            case 3:
                b.set(ws, 1, 0, 0, Blocks.stone, 0);
                b.set(ws, -1, 0, 0, Blocks.stone, 0);
                b.set(ws, 0, 0, 1, Blocks.stone, 0);
                b.set(ws, 0, 0, -1, Blocks.stone, 0);
                break;
            }

            b.set(ws, 0, 0, 0, stem, meta);
            break;
        }

        case 11: // cocoa
        {
            clear(ws, b, 4, -2, 6);
            int meta = variant == 0 ? 0 : variant == 1 ? 4 : variant == 2 ? 8
                     : variant == 3 ? 1 : variant == 4 ? 2 : 0;

            switch (variant)
            {
            case 0:
            case 1:
            case 2:
                b.set(ws, 0, 0, 1, Blocks.log, 3);
                break;

            case 3:
                b.set(ws, -1, 0, 0, Blocks.log, 3);
                break;

            case 4:
                b.set(ws, 0, 0, -1, Blocks.log, 3);
                break;
            }

            b.set(ws, 0, 0, 0, Blocks.cocoa, meta);
            break;
        }

        case 12: // nether wart
        {
            clear(ws, b, 3, -2, 5);
            b.set(ws, 0, -1, 0, variant == 3 ? Blocks.dirt : Blocks.soul_sand, 0);
            b.set(ws, 0, 0, 0, Blocks.nether_wart, variant == 1 ? 2 : variant == 2 ? 3 : 0);
            break;
        }

        case 13: // brown mushroom
        case 14: // red mushroom
        {
            clear(ws, b, 6, -2, 7);
            Block shroom = scene == 13 ? Blocks.brown_mushroom : Blocks.red_mushroom;

            switch (variant)
            {
            case 0:
                roof(ws, b, 2, 4);
                b.set(ws, 0, -1, 0, Blocks.dirt, 0);
                b.set(ws, 0, 0, 0, shroom, 0);
                break;

            case 1:
                roof(ws, b, 2, 4);
                b.set(ws, 0, -1, 0, Blocks.dirt, 0);
                b.set(ws, 2, 0, 0, shroom, 0);
                b.set(ws, -2, 0, 0, shroom, 0);
                b.set(ws, 0, 0, 2, shroom, 0);
                b.set(ws, 0, 0, -2, shroom, 0);
                b.set(ws, 1, 0, 1, shroom, 0);
                b.set(ws, 0, 0, 0, shroom, 0);
                break;

            case 2:
                b.set(ws, 0, -1, 0, Blocks.dirt, 0);
                b.set(ws, 0, 0, 0, shroom, 0);
                break;

            case 3:
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dx = -1; dx <= 1; ++dx) b.set(ws, dx, -1, dz, Blocks.mycelium, 0);

                b.set(ws, 0, 0, 0, shroom, 0);
                break;
            }

            break;
        }

        case 15: // ice
        {
            clear(ws, b, 4, -2, 7);

            switch (variant)
            {
            case 0:
                b.set(ws, 0, -1, 0, Blocks.water, 0);
                b.set(ws, 2, 1, 2, Blocks.glowstone, 0);
                b.set(ws, 0, 0, 0, Blocks.ice, 0);
                break;

            case 1:
                b.set(ws, 0, -1, 0, Blocks.water, 0);
                b.set(ws, 0, 0, 0, Blocks.ice, 0);
                break;

            case 2:
                b.set(ws, 4, 0, 4, Blocks.glowstone, 0);
                b.set(ws, 0, 0, 0, Blocks.ice, 0);
                break;

            case 3:
                b.set(ws, 1, 0, 0, Blocks.water, 0);
                b.set(ws, 1, 0, 1, Blocks.glowstone, 0);
                b.set(ws, 0, 0, 0, Blocks.ice, 0);
                break;
            }

            break;
        }

        case 16: // snow layer
        {
            clear(ws, b, 4, -2, 7);

            switch (variant)
            {
            case 0:
                b.set(ws, 0, 1, 0, Blocks.glowstone, 0);
                b.set(ws, 0, -1, 0, Blocks.stone, 0);
                b.set(ws, 0, 0, 0, Blocks.snow_layer, 3);
                break;

            case 1:
                b.set(ws, 2, 0, 2, Blocks.glowstone, 0);
                b.set(ws, 0, -1, 0, Blocks.stone, 0);
                b.set(ws, 0, 0, 0, Blocks.snow_layer, 0);
                break;

            case 2:
                b.set(ws, 3, 0, 3, Blocks.glowstone, 0);
                b.set(ws, 0, -1, 0, Blocks.stone, 0);
                b.set(ws, 0, 0, 0, Blocks.snow_layer, 0);
                break;

            case 3:
                b.set(ws, 0, -1, 0, Blocks.stone, 0);
                b.set(ws, 0, 0, 0, Blocks.snow_layer, 5);
                break;

            case 4:
                b.set(ws, 1, 0, 1, Blocks.glowstone, 0);
                b.set(ws, 0, -1, 0, Blocks.stone, 0);
                b.set(ws, 0, 0, 0, Blocks.snow_layer, 7);
                b.set(ws, 0, 1, 0, Blocks.snow_layer, 0);
                break;

            case 5:
                b.set(ws, 0, 1, 0, Blocks.glowstone, 0);
                b.set(ws, 0, -1, 0, Blocks.stone, 0);
                b.set(ws, 0, 0, 0, Blocks.snow_layer, 0);
                break;
            }

            break;
        }

        case 17: // farmland
        {
            clear(ws, b, 5, -2, 6);
            b.set(ws, 0, -1, 0, Blocks.dirt, 0);
            int meta = variant == 0 ? 7 : variant == 1 ? 4 : variant == 3 ? 2 : variant == 5 ? 3 : 0;

            switch (variant)
            {
            case 3:
                b.set(ws, 3, -1, 0, Blocks.water, 0);
                break;

            case 4:
                b.set(ws, 5, 0, 0, Blocks.water, 0);
                break;

            case 5:
                b.set(ws, 0, 1, 0, Blocks.wheat, 0);
                break;
            }

            b.set(ws, 0, 0, 0, Blocks.farmland, meta);
            break;
        }

        case 18: // redstone ore (the unlit block never does anything)
        case 19: // lit redstone ore
        {
            clear(ws, b, 2, -2, 3);
            b.set(ws, 0, 0, 0, scene == 18 ? Blocks.redstone_ore : Blocks.lit_redstone_ore, 0);
            break;
        }
        }
    }

    /** scene s's slot: s + 20k while under SLOTS, cycled per case of s. */
    static int slotFor(int scene, int nth)
    {
        int count = 0;

        for (int k = 0; k < 4; ++k) if (scene + 20 * k < SLOTS) ++count;

        return scene + 20 * (nth % count);
    }

    /** java.util.Random's 48-bit internal state. */
    static Field seedField;

    static long worldRandState(Random r)
    {
        try
        {
            if (seedField == null)
            {
                seedField = Random.class.getDeclaredField("seed");
                seedField.setAccessible(true);
            }

            return ((java.util.concurrent.atomic.AtomicLong)seedField.get(r)).get() & 0xffffffffffffL;
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
    }

    static Field field(Class<?> c, String name) throws Exception
    {
        Field f = c.getDeclaredField(name);
        f.setAccessible(true);
        return f;
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

    /** The probe's own Math.random stream, installed as Det's OTHER stream for
     * the sweep (DropsProbe.MathStream). */
    static final class MathStream
    {
        static final long MATH_SEED = 0x6d6174684f7468L;

        final Random mine = new Random(MATH_SEED);
        Random saved;

        void install() throws Exception
        {
            Field f = Det.class.getDeclaredField("math");
            f.setAccessible(true);
            Random[] v = (Random[])f.get(null);
            saved = v[Det.OTHER];
            v[Det.OTHER] = mine;
        }

        void uninstall() throws Exception
        {
            Field f = Det.class.getDeclaredField("math");
            f.setAccessible(true);
            Random[] v = (Random[])f.get(null);

            if (v[Det.OTHER] == mine) v[Det.OTHER] = saved;
        }
    }

    /** Keeps every entity a case spawns, in spawn order. */
    static final class Capture implements IWorldAccess
    {
        final java.util.List<Entity> spawned = new ArrayList<Entity>();

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
}
