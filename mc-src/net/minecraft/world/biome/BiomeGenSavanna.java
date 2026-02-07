package net.minecraft.world.biome;

import java.util.Random;
import net.minecraft.block.Block;
import net.minecraft.entity.passive.EntityHorse;
import net.minecraft.init.Blocks;
import net.minecraft.world.World;
import net.minecraft.world.gen.feature.WorldGenAbstractTree;
import net.minecraft.world.gen.feature.WorldGenSavannaTree;

public class BiomeGenSavanna extends BiomeGenBase
{
    private static final WorldGenSavannaTree field_150627_aC = new WorldGenSavannaTree(false);
    private static final String __OBFID = "CL_00000182";

    public BiomeGenSavanna(int p_i45383_1_)
    {
        super(p_i45383_1_);
        this.spawnableCreatureList.add(new BiomeGenBase.SpawnListEntry(EntityHorse.class, 1, 2, 6));
        this.theBiomeDecorator.treesPerChunk = 1;
        this.theBiomeDecorator.flowersPerChunk = 4;
        this.theBiomeDecorator.grassPerChunk = 20;
    }

    public WorldGenAbstractTree func_150567_a(Random p_150567_1_)
    {
        return (WorldGenAbstractTree)(p_150567_1_.nextInt(5) > 0 ? field_150627_aC : this.worldGeneratorTrees);
    }

    /**
     * Creates a mutated version of the biome and places it into the biomeList with an index equal to the original plus
     * 128
     */
    public BiomeGenBase createMutation()
    {
        BiomeGenSavanna.Mutated mutated = new BiomeGenSavanna.Mutated(this.biomeID + 128, this);
        mutated.temperature = (this.temperature + 1.0F) * 0.5F;
        mutated.rootHeight = this.rootHeight * 0.5F + 0.3F;
        mutated.heightVariation = this.heightVariation * 0.5F + 1.2F;
        return mutated;
    }

    public void decorate(World p_76728_1_, Random p_76728_2_, int p_76728_3_, int p_76728_4_)
    {
        genTallFlowers.func_150548_a(2);

        for (int k = 0; k < 7; ++k)
        {
            int l = p_76728_3_ + p_76728_2_.nextInt(16) + 8;
            int i1 = p_76728_4_ + p_76728_2_.nextInt(16) + 8;
            int j1 = p_76728_2_.nextInt(p_76728_1_.getHeightValue(l, i1) + 32);
            genTallFlowers.generate(p_76728_1_, p_76728_2_, l, j1, i1);
        }

        super.decorate(p_76728_1_, p_76728_2_, p_76728_3_, p_76728_4_);
    }

    public static class Mutated extends BiomeGenMutated
        {
            private static final String __OBFID = "CL_00000183";

            public Mutated(int p_i45382_1_, BiomeGenBase p_i45382_2_)
            {
                super(p_i45382_1_, p_i45382_2_);
                this.theBiomeDecorator.treesPerChunk = 2;
                this.theBiomeDecorator.flowersPerChunk = 2;
                this.theBiomeDecorator.grassPerChunk = 5;
            }

            public void genTerrainBlocks(World p_150573_1_, Random p_150573_2_, Block[] p_150573_3_, byte[] p_150573_4_, int p_150573_5_, int p_150573_6_, double p_150573_7_)
            {
                this.topBlock = Blocks.grass;
                this.field_150604_aj = 0;
                this.fillerBlock = Blocks.dirt;

                if (p_150573_7_ > 1.75D)
                {
                    this.topBlock = Blocks.stone;
                    this.fillerBlock = Blocks.stone;
                }
                else if (p_150573_7_ > -0.5D)
                {
                    this.topBlock = Blocks.dirt;
                    this.field_150604_aj = 1;
                }

                this.genBiomeTerrain(p_150573_1_, p_150573_2_, p_150573_3_, p_150573_4_, p_150573_5_, p_150573_6_, p_150573_7_);
            }

            public void decorate(World p_76728_1_, Random p_76728_2_, int p_76728_3_, int p_76728_4_)
            {
                this.theBiomeDecorator.decorateChunk(p_76728_1_, p_76728_2_, this, p_76728_3_, p_76728_4_);
            }
        }
}