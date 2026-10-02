package netherite.oracle;

import net.minecraft.world.gen.feature.WorldGenBigTree;
import net.minecraft.world.gen.feature.WorldGenCanopyTree;
import net.minecraft.world.gen.feature.WorldGenMegaJungle;
import net.minecraft.world.gen.feature.WorldGenMegaPineTree;
import net.minecraft.world.gen.feature.WorldGenSavannaTree;
import net.minecraft.world.gen.feature.WorldGenTaiga1;
import net.minecraft.world.gen.feature.WorldGenTaiga2;
import net.minecraft.world.gen.feature.WorldGenerator;

/**
 * The tree generators of population, one instance per FeatureProbe row, built
 * the way the biome that places them builds them:
 *
 *   bigtree    BiomeGenBase.worldGeneratorBigTree (a 1-in-10 pick in most
 *              biomes), BiomeGenForest's decorate and BlockSapling
 *   taiga1     BiomeGenTaiga.field_150639_aC
 *   taiga2     BiomeGenTaiga.field_150640_aD and BiomeGenHills.field_150634_aD
 *   megapine   BiomeGenTaiga.field_150641_aE and .field_150642_aF, the two
 *              WorldGenMegaPineTree variants (field_150542_e false and true)
 *   megajungle BiomeGenJungle.func_150567_a's mega tree
 *   savanna    BiomeGenSavanna.field_150627_aC
 *   canopy     BiomeGenForest.field_150631_aE
 *
 * The doBlockNotify flag of every one of these is false, so every write goes
 * out with flag 2, the same path WorldGenMinable uses. A biome keeps its
 * generator instance for the whole world and BiomeDecorator reuses it for every
 * tree it places, so a row keeps its instance across cases too: WorldGenBigTree
 * is the only one that carries state between calls (heightLimit), and it
 * carries it here exactly as it carries it in a biome.
 */
final class FeatureProbeTrees
{
    static final WorldGenerator BIG_TREE = new WorldGenBigTree(false);
    static final WorldGenerator TAIGA1 = new WorldGenTaiga1();
    static final WorldGenerator TAIGA2 = new WorldGenTaiga2(false);
    static final WorldGenerator MEGA_PINE = new WorldGenMegaPineTree(false, false);
    static final WorldGenerator MEGA_PINE_ALT = new WorldGenMegaPineTree(false, true);
    static final WorldGenerator MEGA_JUNGLE = new WorldGenMegaJungle(false, 10, 20, 3, 3);
    static final WorldGenerator SAVANNA = new WorldGenSavannaTree(false);
    static final WorldGenerator CANOPY = new WorldGenCanopyTree(false);

    private FeatureProbeTrees() {}
}