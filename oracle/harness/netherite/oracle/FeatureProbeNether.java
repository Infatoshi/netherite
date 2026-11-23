package netherite.oracle;

import com.google.gson.JsonObject;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import net.minecraft.entity.Entity;
import net.minecraft.entity.item.EntityEnderCrystal;
import net.minecraft.init.Blocks;
import net.minecraft.world.World;
import net.minecraft.world.gen.feature.WorldGenFire;
import net.minecraft.world.gen.feature.WorldGenFlowers;
import net.minecraft.world.gen.feature.WorldGenGlowStone1;
import net.minecraft.world.gen.feature.WorldGenGlowStone2;
import net.minecraft.world.gen.feature.WorldGenHellLava;
import net.minecraft.world.gen.feature.WorldGenMinable;
import net.minecraft.world.gen.feature.WorldGenSpikes;
import net.minecraft.world.gen.feature.WorldGenerator;
import netherite.oracle.FeatureProbe.Config;

/**
 * The Nether and End lane's rows for the population feature probe: the
 * generators ChunkProviderHell.populate and BiomeEndDecorator run, with the
 * dimension (-1 the Nether, 1 the End) recorded in the manifest.
 *
 * Every generator runs unchanged; the rows only carry where a case may place
 * it and what the manifest says. The bands copy the draws
 * ChunkProviderHell.populate makes from hellRNG (the probe draws them from its
 * own case Random instead, as every band row does): the open hell lava, fire
 * and glowstone 1 at nextInt(120) + 4, glowstone 2 and the mushrooms at
 * nextInt(128), the quartz vein and the hidden hell lava at nextInt(108) +
 * 10. The spikes take the top solid or liquid block, as
 * BiomeEndDecorator.getTopSolidOrLiquidBlock does. The margins are each
 * generator's own reach, so a case keeps its whole feature inside the loaded
 * region.
 *
 * WorldGenSpikes also spawns an ender crystal. Entities are not ported, so
 * {@link Spikes} reports the one it spawned (name, position, yaw) through
 * FeatureProbe.recordEntity, and the native test compares the reports.
 */
final class FeatureProbeNether
{
    /** The rows, in the native table's order. */
    static final Config[] ROWS = rows();

    private FeatureProbeNether() {}

    /** A generator whose generate spawns an entity: the record hook runs after
     * the vanilla call, which adds the crystal to the world's entity list. */
    static final class Spikes extends WorldGenSpikes
    {
        Spikes()
        {
            super(Blocks.end_stone);
        }

        public boolean generate(World w, Random r, int x, int y, int z)
        {
            boolean did = super.generate(w, r, x, y, z);

            if (did && FeatureProbe.entitySink != null) FeatureProbeNether.recordCrystal(w, x, y, z);

            return did;
        }
    }

    /** true when the table holds a row whose generator spawns an entity. */
    static boolean wantsEntities(Config[] table)
    {
        for (Config c : table) if (c.gen instanceof Spikes) return true;

        return false;
    }

    /** The crystal the case just spawned: the last ender crystal standing at
     * the case's column, with the position generate gave it and the yaw it
     * drew. */
    private static void recordCrystal(World w, int x, int y, int z)
    {
        double wantX = (double)((float)x + 0.5F), wantZ = (double)((float)z + 0.5F);

        for (int i = w.loadedEntityList.size() - 1; i >= 0; --i)
        {
            Entity e = (Entity)w.loadedEntityList.get(i);

            if (e instanceof EntityEnderCrystal && e.posX == wantX && e.posZ == wantZ && e.posY >= (double)y)
            {
                try
                {
                    FeatureProbe.recordEntity(e);
                }
                catch (IOException ex)
                {
                    throw new RuntimeException("entity record", ex);
                }

                return;
            }
        }

        throw new IllegalStateException("generate returned true but no ender crystal at (" + x + "," + z + ")");
    }

    private static Config[] rows()
    {
        List<Config> r = new ArrayList<Config>();

        // ChunkProviderHell.populate, in its own order: eight open lava
        // springs, then fire, glowstone 1, glowstone 2, the two mushrooms, the
        // quartz veins and the hidden lava springs
        r.add(band("helllava", 4, 124, 9, new WorldGenHellLava(Blocks.flowing_lava, false),
                   "WorldGenHellLava(Blocks.flowing_lava, false): the open spring, updateTick run under "
                   + "scheduledUpdatesAreImmediate"));
        r.add(band("fire", 4, 124, 8, new WorldGenFire(),
                   "WorldGenFire: 64 attempts; onBlockAdded tries a portal frame, checks its floor and parks a tick"));
        r.add(band("glowstone1", 4, 124, 7, new WorldGenGlowStone1(),
                   "WorldGenGlowStone1: the stalactite the populate band nextInt(120) + 4 reaches"));
        r.add(band("glowstone2", 0, 128, 7, new WorldGenGlowStone2(),
                   "WorldGenGlowStone2: the stalactite populate places at nextInt(128)"));
        r.add(band("nethermushrooms", 0, 128, 7, new WorldGenFlowers(Blocks.brown_mushroom),
                   "WorldGenFlowers(Blocks.brown_mushroom): 64 attempts, BlockMushroom.canBlockStay (netherrack is opaque, "
                   + "the full block light has to stay under 13)"));
        r.add(band("nethermushrooms", 0, 128, 7, new WorldGenFlowers(Blocks.red_mushroom),
                   "WorldGenFlowers(Blocks.red_mushroom): the red mushroom of the same two populate calls"));
        r.add(band("quartz", 10, 118, 11, new WorldGenMinable(Blocks.quartz_ore, 13, Blocks.netherrack),
                   "WorldGenMinable(Blocks.quartz_ore, 13, Blocks.netherrack): the vein of populate's nextInt(108) + 10"));
        r.add(band("helllavahidden", 10, 118, 9, new WorldGenHellLava(Blocks.flowing_lava, true),
                   "WorldGenHellLava(Blocks.flowing_lava, true): the hidden spring, five netherrack walls, settled at once"));
        r.add(new Config("spikes", 0, 0, 4, new Spikes(), Config.TOP, false,
                         "WorldGenSpikes(Blocks.end_stone): BiomeEndDecorator's placement, the top solid or liquid block; "
                         + "the obsidian column, the bedrock cap and the ender crystal it spawns are recorded"));

        return r.toArray(new Config[r.size()]);
    }

    /** A band row with the generator's own reach as the margin. */
    private static Config band(String feature, int y0, int y1, int margin, WorldGenerator gen, String note)
    {
        return new Config(feature, y0, y1, margin, gen, Config.BAND, false, note);
    }
}
