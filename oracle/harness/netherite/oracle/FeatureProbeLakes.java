package netherite.oracle;

import net.minecraft.init.Blocks;
import net.minecraft.world.gen.feature.WorldGenBigMushroom;
import net.minecraft.world.gen.feature.WorldGenDesertWells;
import net.minecraft.world.gen.feature.WorldGenIcePath;
import net.minecraft.world.gen.feature.WorldGenIceSpike;
import net.minecraft.world.gen.feature.WorldGenLakes;
import net.minecraft.world.gen.feature.WorldGenerator;

/**
 * The lakes lane's features, wrapped so FeatureProbe's table can describe them
 * the way it describes a vein: the feature object, the spawn band, the margin,
 * and a note for the manifest.
 *
 * A wrapper adds nothing to generate; it only carries the note, because the
 * feature classes keep their parameters private. The two constructors that
 * matter to the note are WorldGenLakes (which block it fills with) and
 * WorldGenIcePath (its path radius); the offsets the probe needs to place a
 * feature on the surface (WorldGenIceSpike, WorldGenIcePath and
 * WorldGenDesertWells all walk down from the y they are handed, and
 * BiomeDecorator/BiomeGenSnow/BiomeGenDesert hand them World.getHeightValue,
 * plus one for the well) live in the probe's Config rows, not here.
 */
final class FeatureProbeLakes
{
    private FeatureProbeLakes() {}

    /** A WorldGenerator that can say what it is, for the manifest. */
    interface Noted
    {
        String note();
    }

    static final class WaterLake extends WorldGenLakes implements Noted
    {
        WaterLake()
        {
            super(Blocks.water);
        }

        public String note()
        {
            return "water lake (Blocks.water), y drawn over the full column as ChunkProviderGenerate.populate draws it";
        }
    }

    static final class LavaLake extends WorldGenLakes implements Noted
    {
        LavaLake()
        {
            super(Blocks.lava);
        }

        public String note()
        {
            return "lava lake (Blocks.lava), y drawn over the full column (vanilla draws a triangular 0..255 band)";
        }
    }

    /** One big mushroom. type -1 lets generate draw the colour itself, the way
     * the no-argument constructor BiomeDecorator builds does. */
    static final class BigMushroom extends WorldGenBigMushroom implements Noted
    {
        final int type;

        BigMushroom(int type)
        {
            super(type);
            this.type = type;
        }

        BigMushroom()
        {
            super();
            this.type = -1;
        }

        public String note()
        {
            return type < 0 ? "big mushroom, colour drawn by generate (BiomeDecorator's object; its cap writes carry flag 2)"
                            : "big mushroom, colour " + (type == 0 ? "brown" : "red") + " (cap writes carry flag 3)";
        }
    }

    static final class DesertWell extends WorldGenDesertWells implements Noted
    {
        public String note()
        {
            return "desert well (BiomeGenDesert.decorate, y = getHeightValue + 1)";
        }
    }

    static final class IceSpike extends WorldGenIceSpike implements Noted
    {
        public String note()
        {
            return "ice spike (BiomeGenSnow.decorate, y = getHeightValue)";
        }
    }

    static final class IcePath extends WorldGenIcePath implements Noted
    {
        final int radius;

        IcePath(int radius)
        {
            super(radius);
            this.radius = radius;
        }

        public String note()
        {
            return "ice path, radius " + radius + " (BiomeGenSnow.decorate, y = getHeightValue)";
        }
    }

    /** The manifest's note for any row of the feature table, so a lane can add
     * rows without touching how an existing feature reports itself. */
    static String noteOf(WorldGenerator gen)
    {
        if (gen instanceof Noted) return ((Noted)gen).note();
        if (gen instanceof FeatureProbe.Minable) return ((FeatureProbe.Minable)gen).note;
        return gen.getClass().getName();
    }
}