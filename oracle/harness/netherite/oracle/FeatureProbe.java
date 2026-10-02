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
import net.minecraft.init.Blocks;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.world.World;
import net.minecraft.world.WorldServer;
import net.minecraft.world.chunk.Chunk;
import net.minecraft.world.gen.feature.WorldGenAbstractTree;
import net.minecraft.world.gen.feature.WorldGenForest;
import net.minecraft.world.gen.feature.WorldGenLiquids;
import net.minecraft.world.gen.feature.WorldGenMinable;
import net.minecraft.world.gen.feature.WorldGenShrub;
import net.minecraft.world.gen.feature.WorldGenSwamp;
import net.minecraft.world.gen.feature.WorldGenTrees;
import net.minecraft.world.gen.feature.WorldGenerator;

/**
 * Population feature probe: how one worldgen feature writes into the world.
 *
 * The region is loaded raw, exactly as the setblock probe loads it (no
 * populateChunk), so the feature starts from bare terrain and every block it
 * changes is its own. Each case draws a config index, a position, and a fresh
 * case seed from Random(opseed), then runs feature.generate with
 * new Random(caseSeed) over that position; the writes that generate performs
 * are recorded in order through Rows.onBlock, and the setblock probe's 3x3 FNV
 * hash is taken after the case, so a native port that writes the same blocks in
 * the same order also lands in the same light state.
 *
 * The native table in csrc/engine/features.h mirrors FEATURES one for one, and
 * the native test replays cases.bin against writes.bin case by case.
 *
 * Runs on its own thread (the OTHER role, like Probe and ChunkDump) while the
 * server is parked, so no CLIENT or SERVER RNG stream moves.
 *
 * Output DIR/manifest.json, DIR/cases.bin, DIR/writes.bin, DIR/final.bin.gz.
 * See the layout strings in the manifest for the byte layouts.
 */
public final class FeatureProbe
{

    /** The feature table: name -> how to build it, and where a case may place it. */
    static final Config[] FEATURES =
    {
        new Config("minable", 4, 124, 17, new Minable(Blocks.dirt, 32, "config 0: dirt 32")),
        new Config("minable", 4, 124, 17, new Minable(Blocks.gravel, 32, "config 1: gravel 32")),
        new Config("minable", 4, 124, 17, new Minable(Blocks.coal_ore, 16, "config 2: coal 16")),
        new Config("minable", 4, 60, 17, new Minable(Blocks.iron_ore, 8, "config 3: iron 8")),
        new Config("minable", 4, 28, 17, new Minable(Blocks.gold_ore, 8, "config 4: gold 8")),
        new Config("minable", 4, 12, 17, new Minable(Blocks.redstone_ore, 7, "config 5: redstone 7")),
        new Config("minable", 4, 12, 17, new Minable(Blocks.diamond_ore, 7, "config 6: diamond 7")),
        new Config("minable", 4, 12, 17, new Minable(Blocks.lapis_ore, 6, "config 7: lapis 6")),

        // lane/treesb: the large and conifer trees of population (WorldGenBigTree, the two taigas,
        // the mega taiga's two variants, the mega jungle, the acacia and the dark oak), one row
        // per variant the biomes build. Each row holds one generator instance for the whole run,
        // the way a biome holds one for the whole world, so WorldGenBigTree's heightLimit carries
        // from case to case exactly as it carries from tree to tree. A case puts its tree on the
        // surface (World.getHeightValue) after BiomeDecorator's setScale(1, 1, 1), and the mega
        // taiga row also runs func_150524_b, the hook BiomeDecorator calls after a generate that
        // returned true.
        new Config("bigtree", 0, 0, 24, FeatureProbeTrees.BIG_TREE, Config.SURFACE, false, "WorldGenBigTree(false) after setScale(1,1,1): heightLimitLimit 12, leafDistanceLimit 5"),
        new Config("taiga1", 0, 0, 24, FeatureProbeTrees.TAIGA1, Config.SURFACE, false, "WorldGenTaiga1(), the BiomeGenTaiga conifer"),
        new Config("taiga2", 0, 0, 24, FeatureProbeTrees.TAIGA2, Config.SURFACE, false, "WorldGenTaiga2(false), the tall BiomeGenTaiga and BiomeGenSnow conifer"),
        new Config("megapine", 0, 0, 24, FeatureProbeTrees.MEGA_PINE, Config.SURFACE, true, "WorldGenMegaPineTree(false, false), baseHeight 13, field_150538_d 15, with func_150524_b"),
        new Config("megapine", 0, 0, 24, FeatureProbeTrees.MEGA_PINE_ALT, Config.SURFACE, true, "WorldGenMegaPineTree(false, true), the taller field_150542_e variant, with func_150524_b"),
        new Config("megajungle", 0, 0, 24, FeatureProbeTrees.MEGA_JUNGLE, Config.SURFACE, false, "WorldGenMegaJungle(false, 10, 20, 3, 3), the BiomeGenJungle mega tree"),
        new Config("savanna", 0, 0, 24, FeatureProbeTrees.SAVANNA, Config.SURFACE, false, "WorldGenSavannaTree(false), the acacia of BiomeGenSavanna"),
        new Config("canopy", 0, 0, 24, FeatureProbeTrees.CANOPY, Config.SURFACE, false, "WorldGenCanopyTree(false), the dark oak of BiomeGenForest"),

        // the small tree generators (lane/treesa). BiomeDecorator's tree loop
        // builds them through BiomeGenBase.func_150567_a and calls them with
        // x, z from the chunk and y = World.getHeightValue(x, z), so every row
        // below is placed that way and never from a y band. The native table's
        // rows carry the same parameters in their block and count fields.
        new Config("trees", 0, 0, 8, new WorldGenTrees(false), Config.SURFACE, false,
                   "oak: WorldGenTrees(false), minTreeHeight 4, meta 0/0, no vines"),
        new Config("jungle", 0, 0, 8, new WorldGenTrees(false, 4, 3, 3, true), Config.SURFACE, false,
                   "jungle tree: minTreeHeight 4, meta 3/3, vines"),
        new Config("jungle", 0, 0, 8, new WorldGenTrees(false, 5, 3, 3, true), Config.SURFACE, false,
                   "jungle tree: minTreeHeight 5, meta 3/3, vines"),
        new Config("jungle", 0, 0, 8, new WorldGenTrees(false, 6, 3, 3, true), Config.SURFACE, false,
                   "jungle tree: minTreeHeight 6, meta 3/3, vines"),
        new Config("jungle", 0, 0, 8, new WorldGenTrees(false, 7, 3, 3, true), Config.SURFACE, false,
                   "jungle tree: minTreeHeight 7, meta 3/3, vines"),
        new Config("jungle", 0, 0, 8, new WorldGenTrees(false, 8, 3, 3, true), Config.SURFACE, false,
                   "jungle tree: minTreeHeight 8, meta 3/3, vines"),
        new Config("jungle", 0, 0, 8, new WorldGenTrees(false, 9, 3, 3, true), Config.SURFACE, false,
                   "jungle tree: minTreeHeight 9, meta 3/3, vines"),
        new Config("jungle", 0, 0, 8, new WorldGenTrees(false, 10, 3, 3, true), Config.SURFACE, false,
                   "jungle tree: minTreeHeight 10, meta 3/3, vines"),
        new Config("forest", 0, 0, 8, new WorldGenForest(false, false), Config.SURFACE, false,
                   "birch: WorldGenForest(false, false)"),
        new Config("forest", 0, 0, 8, new WorldGenForest(false, true), Config.SURFACE, false,
                   "tall birch: WorldGenForest(false, true)"),
        new Config("swamp", 0, 0, 8, new WorldGenSwamp(), Config.SURFACE, false,
                   "swamp oak: WorldGenSwamp(), vines"),
        new Config("shrub", 0, 0, 8, new WorldGenShrub(3, 0), Config.SURFACE, false,
                   "jungle bush: WorldGenShrub(3, 0), wood 3, leaves 0"),

        // the lakes lane's rows, appended so the minable rows keep their
        // indices and every existing probe keeps its bytes
        new Config("waterlake", 0, 256, 9, new FeatureProbeLakes.WaterLake()),
        new Config("lavalake", 0, 256, 9, new FeatureProbeLakes.LavaLake()),
        new Config("bigmushroom", 0, 0, 4, new FeatureProbeLakes.BigMushroom(0), Config.SURFACE, false, null, 0),
        new Config("bigmushroom", 0, 0, 4, new FeatureProbeLakes.BigMushroom(1), Config.SURFACE, false, null, 0),
        new Config("bigmushroom", 0, 0, 4, new FeatureProbeLakes.BigMushroom(), Config.SURFACE, false, null, 0),
        new Config("desertwell", 0, 0, 3, new FeatureProbeLakes.DesertWell(), Config.SURFACE, false, null, 1),
        new Config("icespike", 0, 0, 5, new FeatureProbeLakes.IceSpike(), Config.SURFACE, false, null, 0),
        new Config("icepath", 0, 0, 5, new FeatureProbeLakes.IcePath(4), Config.SURFACE, false, null, 0),
        // the same two lakes as one table, in the order populate builds them
        // (ChunkProviderGenerate.populate draws the water lake first, then the
        // lava lake): cases share a region, so a lava lake can land in a water
        // lake's water, which is the only way BlockLiquid.onBlockAdded's
        // obsidian/cobblestone branch can be reached
        new Config("lakes", 0, 256, 9, new FeatureProbeLakes.WaterLake()),
        new Config("lakes", 0, 256, 9, new FeatureProbeLakes.LavaLake()),

        // the dungeons lane's row (FeatureProbeDungeons), which also records tile entities
        FeatureProbeDungeons.row(),
        // the springs lane's rows: WorldGenLiquids, the rock-pocket spring of
        // BiomeDecorator's genDecorations (2 water, 20 lava). The generator is
        // built the way the decorator builds it and placed with the
        // decorator's own nested y draws, so the row carries the margin of
        // the flow's reach rather than the generator's one-block pocket: the
        // immediate update cascade can spread a spring across its whole cave.
        new Config("springwater", 0, 0, 9, new WorldGenLiquids(Blocks.flowing_water), Config.SPRING_WATER, false,
                   "WorldGenLiquids(Blocks.flowing_water): the BiomeDecorator water spring", 0),
        new Config("springlava", 0, 0, 9, new WorldGenLiquids(Blocks.flowing_lava), Config.SPRING_LAVA, false,
                   "WorldGenLiquids(Blocks.flowing_lava): the BiomeDecorator lava spring", 0),
    };

    /** One row of the feature table. margin is how far the feature can reach
     * from the position it is given, so a case keeps that much of the loaded
     * region between itself and the rim and the whole feature is recorded.
     *
     * mode is where a case may place the row. BAND draws y from [y0, y1), the
     * vein bands below; SURFACE takes the column's height as y, which is how
     * BiomeDecorator places a tree (y0 and y1 are then unused). post asks for
     * the generator's own func_150524_b call after a generate that returned
     * true, which is what BiomeDecorator does next for a tree. note is what the
     * manifest says about the row. */
    static final class Config
    {
        static final int BAND = 0, SURFACE = 1;
        /* The plants lane's modes, mirroring where BiomeDecorator stands each
         * feature: TOP is getTopSolidOrLiquidBlock (the sand and clay disks),
         * UP2 draws nextInt(getHeightValue(x, z) * 2), UP32 draws
         * nextInt(getHeightValue(x, z) + 32), LILY draws the same as UP2 then
         * walks down while the block below is air (the waterlily decorator's
         * spot). PLACE_HEIGHT is SURFACE. */
        static final int TOP = 2, UP2 = 3, UP32 = 4, LILY = 5;
        /* The springs lane's modes, mirroring where BiomeDecorator's
         * genDecorations stands each WorldGenLiquids: SPRING_WATER draws
         * nextInt(nextInt(248) + 8), SPRING_LAVA draws
         * nextInt(nextInt(nextInt(240) + 8) + 8), both from the same Random
         * that drew the case's x and z. */
        static final int SPRING_WATER = 6, SPRING_LAVA = 7;
        static final int PLACE_BAND = BAND, PLACE_TOP = TOP, PLACE_HEIGHT = SURFACE, PLACE_UP2 = UP2, PLACE_UP32 = UP32, PLACE_LILY = LILY;

        final String feature;
        final int y0, y1;
        final int margin;
        final WorldGenerator gen;
        final int mode;
        final int yoff;
        final boolean post;
        final String note;

        /** true when the feature leaves tile entities, so the probe records them
         * (FeatureProbeDungeons); false keeps every other probe's bytes. */
        final boolean tileEntities;

        /** A row that places itself on the surface or in water (the plants lane's rows). */
        Config(String feature, int place, int margin, WorldGenerator gen)
        {
            this(feature, 0, 0, margin, gen, place, false, null, 0);
        }

        Config(String feature, int y0, int y1, int margin, WorldGenerator gen)
        {
            this(feature, y0, y1, margin, gen, BAND, false, null, 0);
        }

        Config(String feature, int y0, int y1, int margin, WorldGenerator gen, int mode, boolean post, String note)
        {
            this(feature, y0, y1, margin, gen, mode, post, note, 0);
        }

        /** yoff: a SURFACE row's offset from World.getHeightValue(x, z) (the desert well's is 1). */
        Config(String feature, int y0, int y1, int margin, WorldGenerator gen, int mode, boolean post, String note, int yoff)
        {
            this(feature, y0, y1, margin, gen, mode, post, note, yoff, false);
        }

        /** A band row that leaves tile entities (the dungeon's). */
        Config(String feature, int y0, int y1, int margin, WorldGenerator gen, boolean tileEntities)
        {
            this(feature, y0, y1, margin, gen, BAND, false, null, 0, tileEntities);
        }

        Config(String feature, int y0, int y1, int margin, WorldGenerator gen, int mode, boolean post, String note, int yoff,
               boolean tileEntities)
        {
            this.feature = feature;
            this.y0 = y0;
            this.y1 = y1;
            this.margin = margin;
            this.gen = gen;
            this.mode = mode;
            this.yoff = yoff;
            this.post = post;
            this.note = note;
            this.tileEntities = tileEntities;
        }
    }

    /** The note one table row reports: a minable's own text, else the generator's
     * class name. */
    static String note(WorldGenerator gen)
    {
        return gen instanceof Minable ? ((Minable)gen).note : gen.getClass().getSimpleName();
    }

    /** WorldGenMinable over Blocks.stone, built the way BiomeDecorator builds it. */
    static final class Minable extends WorldGenMinable
    {
        Minable(Block block, int count, String note)
        {
            super(block, count);
            this.count = count;
            this.note = note;
        }

        final int count;
        final String note;
    }

    /** The feature table entries for one feature name. */
    static Config[] table(String feature)
    {
        java.util.List<Config> out = new java.util.ArrayList<Config>();

        for (Config c : FEATURES) if (c.feature.equals(feature)) out.add(c);
        for (Config c : FeatureProbePlants.ROWS) if (c.feature.equals(feature)) out.add(c);
        for (Config c : FeatureProbeNether.ROWS) if (c.feature.equals(feature)) out.add(c);

        if (out.isEmpty()) throw new IllegalArgumentException("unknown feature " + feature);
        return out.toArray(new Config[out.size()]);
    }

    private FeatureProbe() {}

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
        }, "Oracle Feature Probe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        int cx = cmd.get("cx").getAsInt(), cz = cmd.get("cz").getAsInt();
        int radius = cmd.has("radius") ? cmd.get("radius").getAsInt() : 2;
        int cases = cmd.has("cases") ? cmd.get("cases").getAsInt() : 600;
        long opseed = cmd.has("opseed") ? cmd.get("opseed").getAsLong() : 1L;
        int ring = cmd.has("ring") ? cmd.get("ring").getAsInt() : 1;
        String feature = cmd.has("feature") ? cmd.get("feature").getAsString() : "minable";
        /* the dimension the region loads in and the features run in: -1 the
         * Nether, 1 the End, 0 the overworld (the default, and what every
         * probe before the field ran in) */
        int dim = cmd.has("dim") ? cmd.get("dim").getAsInt() : 0;
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();

        Config[] table = table(feature);
        WorldServer ws = server.worldServerForDimension(dim);
        long seed = ws.getSeed();
        Probe.rawChunks = true;
        boolean wantsTiles = table[0].tileEntities;
        FeatureProbeDungeons.Tiles tiles = wantsTiles ? new FeatureProbeDungeons.Tiles(dir) : null;
        /* a row whose generator spawns an entity (the End's WorldGenSpikes)
         * records it beside its writes: entities are not ported, so the native
         * generator reports the one it would spawn and the test compares */
        boolean wantsEntities = FeatureProbeNether.wantsEntities(table);
        OutputStream entitiesOut = wantsEntities ? new BufferedOutputStream(new FileOutputStream(new File(dir, "entities.bin")), 1 << 16) : null;
        entitySink = entitiesOut;
        Rows.writeListener = new Rows.WriteListener()
        {
            public void onWrite(World w, int x, int y, int z, int id, int meta)
            {
                if (sink != null) FeatureProbe.record(x, y, z, id, meta);
            }
        };

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

        // a case writes through a feature, which may reach outside its own chunk:
        // the ops square the region was loaded for is what the 3x3 hash needs
        int width = (2 * radius + 1) * 16;
        Random r = new Random(opseed);
        byte[] buf = new byte[Probe.CHUNK_BYTES];
        byte[] cbuf = new byte[33];
        OutputStream casesOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "cases.bin")), 1 << 16);
        OutputStream writesOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "writes.bin")), 1 << 16);
        JsonArray perConfig = new JsonArray();
        int[] caseCount = new int[table.length], writeCount = new int[table.length];
        int totalWrites = 0, didTrue = 0, moved = 0;
        long prev = 0;

        for (int i = 0; i < cases; ++i)
        {
            int ci = r.nextInt(table.length);
            Config cfg = table[ci];
            int inset = cfg.margin;
            int x = (cx - radius) * 16 + inset + r.nextInt(width - 2 * inset);
            int z = (cz - radius) * 16 + inset + r.nextInt(width - 2 * inset);
            int y;

            switch (cfg.mode)
            {
                case Config.SURFACE:
                    y = ws.getHeightValue(x, z) + cfg.yoff;
                    break;
                case Config.TOP:
                    y = ws.getTopSolidOrLiquidBlock(x, z);
                    break;
                case Config.UP2:
                    y = r.nextInt(ws.getHeightValue(x, z) * 2);
                    break;
                case Config.UP32:
                    y = r.nextInt(ws.getHeightValue(x, z) + 32);
                    break;
                case Config.LILY:
                    y = r.nextInt(ws.getHeightValue(x, z) * 2);

                    while (y > 0 && ws.isAirBlock(x, y - 1, z)) --y;

                    break;
                case Config.SPRING_WATER:
                    y = r.nextInt(r.nextInt(248) + 8);
                    break;
                case Config.SPRING_LAVA:
                    y = r.nextInt(r.nextInt(r.nextInt(240) + 8) + 8);
                    break;
                default:
                    y = cfg.y0 + r.nextInt(cfg.y1 - cfg.y0);
                    break;
            }
            long fs = r.nextLong();

            FeatureProbe.sink = writesOut;
            FeatureProbe.caseIndex = i;
            FeatureProbe.writes = 0;
            Random caseRand = new Random(fs);
            boolean did;
            try
            {
                if (cfg.mode == Config.SURFACE) cfg.gen.setScale(1.0D, 1.0D, 1.0D); // BiomeDecorator, before every tree
                did = cfg.gen.generate(ws, caseRand, x, y, z);
                if (did && cfg.post) ((WorldGenAbstractTree)cfg.gen).func_150524_b(ws, caseRand, x, y, z);  // BiomeDecorator, after a tree that grew
            }
            finally
            {
                FeatureProbe.sink = null;
            }

            long hash = Probe.hashAround(ws, x >> 4, z >> 4, buf);

            if (tiles != null) tiles.record(ws, x0, x1, z0, z1);

            if (did) ++didTrue;
            if (i > 0 && hash != prev) ++moved;
            prev = hash;
            ++caseCount[ci];
            writeCount[ci] += FeatureProbe.writes;
            totalWrites += FeatureProbe.writes;

            cbuf[0] = (byte)ci;
            le32(cbuf, 1, x);
            le32(cbuf, 5, y);
            le32(cbuf, 9, z);
            le64(cbuf, 13, fs);
            le32(cbuf, 21, FeatureProbe.writes);
            le64(cbuf, 25, hash);
            casesOut.write(cbuf);
        }
        casesOut.close();
        writesOut.close();
        if (tiles != null) tiles.close();
        entitySink = null;
        if (entitiesOut != null) entitiesOut.close();
        Rows.writeListener = null;

        for (int i = 0; i < table.length; ++i)
        {
            JsonObject c = new JsonObject();
            c.addProperty("config", i);
            c.addProperty("note", table[i].note != null ? table[i].note : table[i].gen instanceof FeatureProbePlants.Plant ? ((FeatureProbePlants.Plant)table[i].gen).note : FeatureProbeLakes.noteOf(table[i].gen));
            c.addProperty("cases", caseCount[i]);
            c.addProperty("writes", writeCount[i]);
            perConfig.add(c);
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

                for (int i = 0; i < 256; ++i) cols[i] = (byte)(c.updateSkylightColumns[i] ? 1 : 0);
                out.write(cols);
                out.write(gap.getBoolean(c) ? 1 : 0);
            }
        }
        out.close();

        JsonObject m = new JsonObject();
        m.addProperty("seed", seed);
        m.addProperty("kind", "feature");
        m.addProperty("feature", feature);
        m.addProperty("dim", dim);
        m.addProperty("cx", cx);
        m.addProperty("cz", cz);
        m.addProperty("radius", radius);
        m.addProperty("ring", ring);
        m.addProperty("cases", cases);
        m.addProperty("opseed", opseed);
        m.addProperty("order", "cx-major: for cx in " + x0 + ".." + x1 + ", for cz in " + z0 + ".." + z1);
        m.add("loaded", loaded);
        m.add("configs", perConfig);
        m.addProperty("case_hash_moves", "cases after the first whose 3x3 hash differs from the one before it: how much of "
            + "the region the feature actually touched");
        m.addProperty("configs_note", "the feature table rows this run may pick; a new feature appends its rows after these, so an "
            + "existing probe keeps its config indices and byte-identical output");
        String yDraw = "y = y0 + nextInt(y1-y0) (the feature's own spawn band, kept so the "
            + "feature stays inside the world: minable uses the BiomeDecorator vein bands below and its y jitter of -2..0)";
        String yBands = "minable configs 0-2 y in 4..123, 3 in 4..59, 4 in 4..27, 5-7 in 4..11 (BiomeDecorator's 0..255, 0..127, "
            + "0..63, 0..31, 0..15 bands, shifted up by 4 for the vein's y jitter of -2 and floor rounding and down by 4 for its radius)";

        if (table[0].mode == Config.SURFACE)
        {
            yDraw = "y = ws.getHeightValue(x, z) + the row's yoff (a height case spends no draw on y, and the row keeps the "
                + "same y0/y1 for the record)";
            yBands = "the surface rows carry no band: a case takes y from the chunk's height map instead, the way the feature's "
                + "placement does (well +1 as BiomeGenDesert.decorate, ice spike/path +0 as BiomeGenSnow.decorate)";
        }

        m.addProperty("case_draws", "per case from Random(opseed): config index = nextInt(configs.length); x = (cx-radius)*16 + margin + "
            + "nextInt((2*radius+1)*16 - 2*margin); z the same with cz; " + yDraw + "; then case seed = "
            + "nextLong(); generate(ws, new Random(caseSeed), x, y, z)");
        m.addProperty("margin", "the feature table's per-row reach: a case stays this far from the loaded rim, so every write of the feature "
            + "lands in a loaded chunk and is recorded (minable: 17)");
        m.addProperty("place_modes", "0 band y = y0 + nextInt(y1-y0); 1 surface y = getHeightValue(x, z) + yoff; 2 top "
            + "World.getTopSolidOrLiquidBlock(x, z); 3 up2 nextInt(getHeightValue(x, z) * 2); 4 up32 nextInt(getHeightValue(x, z) + 32); "
            + "5 lily nextInt(getHeightValue(x, z) * 2) then walk down while isAirBlock(x, y - 1, z); "
            + "6 spring-water nextInt(nextInt(248) + 8); 7 spring-lava nextInt(nextInt(nextInt(240) + 8) + 8), both drawn from the "
            + "same Random as the case's x and z, as BiomeDecorator's genDecorations draws a spring's y");
        m.addProperty("placement", "a row's mode says where a case puts the feature. BAND (minable, the lakes) draws "
            + "y = y0 + nextInt(y1-y0); SURFACE (trees, big mushrooms, desert wells, ice spikes and paths) takes no draw and "
            + "uses y = World.getHeightValue(x, z) + yoff, as the biome that owns the feature places it (yoff is 1 for the "
            + "desert well, 0 otherwise); the case calls setScale(1, 1, 1) on the generator first and, for a row whose note "
            + "says so, func_150524_b after a generate that returned true, both as BiomeDecorator does.");
        m.addProperty("y_bands", yBands);
        m.addProperty("case_layout", "33 bytes per case: config index uint8, x int32 LE, y int32 LE, z int32 LE, case seed int64 LE, "
            + "number of writes uint32 LE, FNV-1a 64 hash of the 3x3 chunks around (x >> 4, z >> 4) after the case, uint64 LE");
        m.addProperty("write_layout", "16 bytes per write, every case's writes in order: x int32 LE, y int32 LE, z int32 LE, id uint16 LE, "
            + "meta uint8, pad uint8");
        m.addProperty("write_rule", "every call Rows.onBlock gets from World.setBlock that returned true (chunk.func_150807_a changed the "
            + "block), in call order; setBlockMetadataWithNotify reports id -1 the same way");
        m.addProperty("hash", "the setblock probe's: FNV-1a 64 (offset 0xcbf29ce484222325, prime 0x100000001b3) over the 3x3 chunks "
            + "around the case's chunk, dx outer -1..1, dz inner -1..1; a chunk that is not loaded contributes one 0 byte");
        m.addProperty("chunk_bytes", "the setblock probe's: ids uint16 LE (65536, index x << 12 | z << 8 | y), metas uint8 (65536), sky "
            + "light uint8 (65536), block light uint8 (65536), heightMap 256 int32 LE, precipitationHeightMap 256 int32 LE (both Java "
            + "index order z << 4 | x), heightMapMinimum int32 LE, section mask uint16 LE");
        m.addProperty("final_layout", "final.bin.gz, per loaded chunk in load order: cx int32 LE, cz int32 LE, the chunk bytes above, "
            + "updateSkylightColumns 256 bytes (0/1), isGapLightingUpdated 1 byte");

        if (tiles != null)
        {
            m.addProperty("tile_entities", "tileentities.bin");
            m.addProperty("tile_entity_layout", FeatureProbeDungeons.LAYOUT);
            m.addProperty("tile_entity_note", FeatureProbeDungeons.NOTE);
        }

        if (wantsEntities)
        {
            m.addProperty("entities", "entities.bin");
            m.addProperty("entity_layout", "one record per spawned entity, in case order: name length uint8, name UTF-8 bytes, "
                + "posX double LE, posY double LE, posZ double LE, rotationYaw float LE (4 bytes); the generator's own draw");
            m.addProperty("entity_note", "entities are not ported; the native generator reports the entity it would spawn "
                + "(WorldGenSpikes' ender crystal: name, position, nextFloat() * 360 yaw) and the test compares these records "
                + "in order");
        }

        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        w.println(m.toString());
        w.close();

        JsonObject res = new JsonObject();
        res.addProperty("dir", dir.getPath());
        res.addProperty("feature", feature);
        res.addProperty("chunks", loaded.size());
        res.addProperty("cases", cases);
        res.addProperty("writes", totalWrites);
        res.addProperty("generateTrue", didTrue);
        res.addProperty("moved", moved);
        if (tiles != null) res.addProperty("tileEntities", tiles.entries());
        res.add("configs", perConfig);
        return res;
    }



    // ------------------------------------------------------------ the write listener

    /** Non-null while a case runs: every write World.setBlock makes lands here. */
    static OutputStream sink;
    static int caseIndex;
    static int writes;

    /** Non-null while a probe that records spawned entities runs: one record
     * per entity a feature spawns, in case order (see the manifest's
     * entity_layout). */
    static OutputStream entitySink;

    /** One record of an entity a feature spawned: its name, position and
     * rotationYaw, the native side's reference for the entity it would spawn. */
    static void recordEntity(net.minecraft.entity.Entity e) throws IOException
    {
        if (entitySink == null) return;

        byte[] name = e.getClass().getSimpleName().getBytes("UTF-8");
        double[] d = new double[] {e.posX, e.posY, e.posZ};
        byte[] b = new byte[1 + name.length + 28];

        b[0] = (byte)name.length;
        System.arraycopy(name, 0, b, 1, name.length);

        for (int i = 0; i < 3; ++i) le64(b, 1 + name.length + 8 * i, Double.doubleToLongBits(d[i]));

        int bits = Float.floatToIntBits(e.rotationYaw);

        for (int i = 0; i < 4; ++i) b[1 + name.length + 24 + i] = (byte)(bits >> (8 * i));

        entitySink.write(b);
    }

    /** Called from Rows.onBlock with an already-changed block, in call order. */
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
            throw new RuntimeException("case " + caseIndex + " write " + writes, e);
        }

        ++writes;
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