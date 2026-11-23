package netherite.oracle;

import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import net.minecraft.block.Block;
import net.minecraft.init.Blocks;
import netherite.oracle.FeatureProbe.Config;
import net.minecraft.world.World;
import net.minecraft.world.gen.feature.WorldGenCactus;
import net.minecraft.world.gen.feature.WorldGenClay;
import net.minecraft.world.gen.feature.WorldGenDeadBush;
import net.minecraft.world.gen.feature.WorldGenDoublePlant;
import net.minecraft.world.gen.feature.WorldGenFlowers;
import net.minecraft.world.gen.feature.WorldGenMelon;
import net.minecraft.world.gen.feature.WorldGenPumpkin;
import net.minecraft.world.gen.feature.WorldGenReed;
import net.minecraft.world.gen.feature.WorldGenSand;
import net.minecraft.world.gen.feature.WorldGenTallGrass;
import net.minecraft.world.gen.feature.WorldGenVines;
import net.minecraft.world.gen.feature.WorldGenWaterlily;
import net.minecraft.world.gen.feature.WorldGenerator;

/**
 * The plants lane's rows for the population feature probe: the small
 * decoration features of 1.7.10 world population.
 *
 * The vanilla feature objects run unchanged; {@link Plant} only carries the
 * manifest note and the block and metadata the row places, so the probe's
 * per-config summary and the native table (csrc/engine/features_plants.c) can
 * name the same row. The rows are in the native table's order, and their
 * config indices are their position here, because a feature name's indices are
 * per feature (FeatureProbe.table collects only that feature's rows).
 *
 * Placement: every feature but the veins takes a surface or water spot, which
 * is where BiomeDecorator or the biome's own decorate() puts it. sand and clay
 * use the top solid or liquid block (BiomeDecorator's two sandGen calls and
 * clayGen call); waterlily uses the decorator's own nextInt(height * 2) then
 * walk down while the block below is air. The rest stand on the surface, so
 * the row uses that same walk (PLACE_LILY) rather than the decorator's
 * nextInt(height * 2), which would leave most of them buried. The mushrooms
 * take PLACE_UP2 and so land in caves, where their light rule can hold: a
 * surface mushroom in daylight reads getFullBlockLightValue 15 and is
 * refused. WorldGenVines is called by BiomeGenJungle with y = 128, which makes
 * its loop body (y < 128) never run, so the row gives it a real band instead.
 */
final class FeatureProbePlants
{
    /** The rows, in the native table's order. */
    static final Config[] ROWS = rows();

    private FeatureProbePlants() {}

    /** A vanilla feature under test, plus what the manifest should say about
     * the row. */
    static final class Plant extends WorldGenerator
    {
        final WorldGenerator gen;   /* the vanilla feature */
        final int block;            /* Block.getIdFromBlock of what it places */
        final int meta;             /* the metadata it places, where it has one */
        final String note;

        Plant(WorldGenerator gen, Block block, int meta, String note)
        {
            this.gen = gen;
            this.block = Block.getIdFromBlock(block);
            this.meta = meta;
            this.note = note;
        }

        public boolean generate(World w, Random r, int x, int y, int z)
        {
            return this.gen.generate(w, r, x, y, z);
        }
    }

    private static Config[] rows()
    {
        List<Config> r = new ArrayList<Config>();

        r.add(row("sand", Config.PLACE_TOP, 7, new WorldGenSand(Blocks.sand, 7), Blocks.sand, 0, "config 0: sand radius 7"));
        r.add(row("sand", Config.PLACE_TOP, 7, new WorldGenSand(Blocks.gravel, 6), Blocks.gravel, 0, "config 1: gravel radius 6"));

        r.add(row("clay", Config.PLACE_TOP, 4, new WorldGenClay(4), Blocks.clay, 0, "config 0: clay 4"));

        r.add(row("flowers", Config.PLACE_LILY, 8, new WorldGenFlowers(Blocks.yellow_flower), Blocks.yellow_flower, 0,
            "config 0: yellow_flower meta 0 (dandelion)"));
        r.add(row("flowers", Config.PLACE_LILY, 8, new WorldGenFlowers(Blocks.red_flower), Blocks.red_flower, 0,
            "config 1: red_flower meta 0 (poppy)"));

        WorldGenFlowers orchid = new WorldGenFlowers(Blocks.red_flower);
        orchid.func_150550_a(Blocks.red_flower, 1);
        r.add(row("flowers", Config.PLACE_LILY, 8, orchid, Blocks.red_flower, 1, "config 2: red_flower meta 1 (blue orchid)"));

        r.add(row("flowers", Config.PLACE_UP2, 8, new WorldGenFlowers(Blocks.brown_mushroom), Blocks.brown_mushroom, 0,
            "config 3: brown_mushroom (light rule)"));
        r.add(row("flowers", Config.PLACE_UP2, 8, new WorldGenFlowers(Blocks.red_mushroom), Blocks.red_mushroom, 0,
            "config 4: red_mushroom (light rule)"));

        r.add(row("tallgrass", Config.PLACE_LILY, 8, new WorldGenTallGrass(Blocks.tallgrass, 1), Blocks.tallgrass, 1,
            "config 0: tallgrass meta 1 (grass)"));
        r.add(row("tallgrass", Config.PLACE_LILY, 8, new WorldGenTallGrass(Blocks.tallgrass, 2), Blocks.tallgrass, 2,
            "config 1: tallgrass meta 2 (fern)"));

        r.add(row("deadbush", Config.PLACE_LILY, 8, new WorldGenDeadBush(Blocks.deadbush), Blocks.deadbush, 0, "config 0: deadbush"));

        r.add(row("reed", Config.PLACE_LILY, 4, new WorldGenReed(), Blocks.reeds, 0, "config 0: reeds"));

        r.add(row("cactus", Config.PLACE_LILY, 8, new WorldGenCactus(), Blocks.cactus, 0, "config 0: cactus"));

        r.add(row("pumpkin", Config.PLACE_LILY, 8, new WorldGenPumpkin(), Blocks.pumpkin, 0, "config 0: pumpkin"));

        r.add(row("waterlily", Config.PLACE_LILY, 8, new WorldGenWaterlily(), Blocks.waterlily, 0, "config 0: waterlily"));

        for (int meta = 0; meta < 6; ++meta)
        {
            WorldGenDoublePlant plant = new WorldGenDoublePlant();
            plant.func_150548_a(meta);
            r.add(row("doubleplant", Config.PLACE_LILY, 8, plant, Blocks.double_plant, meta,
                "config " + meta + ": double_plant meta " + meta + " (" + BlockDoublePlantNames.NAME[meta] + ")"));
        }

        r.add(row("melon", Config.PLACE_LILY, 8, new WorldGenMelon(), Blocks.melon_block, 0, "config 0: melon_block"));

        r.add(bandRow("vines", 4, 124, 4, new WorldGenVines(), Blocks.vine, 0, "config 0: vine, y band 4..123"));

        return r.toArray(new Config[r.size()]);
    }

    private static Config row(String feature, int place, int margin, WorldGenerator gen, Block block, int meta, String note)
    {
        return new Config(feature, place, margin, new Plant(gen, block, meta, note));
    }

    private static Config bandRow(String feature, int y0, int y1, int margin, WorldGenerator gen, Block block, int meta, String note)
    {
        return new Config(feature, y0, y1, margin, new Plant(gen, block, meta, note));
    }

    /** BlockDoublePlant.field_149892_a, for the note only. */
    static final class BlockDoublePlantNames
    {
        static final String[] NAME = {"sunflower", "syringa", "grass", "fern", "rose", "paeonia"};
    }
}