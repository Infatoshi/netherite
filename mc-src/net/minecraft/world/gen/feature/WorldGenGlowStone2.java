package net.minecraft.world.gen.feature;

import java.util.Random;
import net.minecraft.block.Block;
import net.minecraft.block.material.Material;
import net.minecraft.init.Blocks;
import net.minecraft.world.World;

public class WorldGenGlowStone2 extends WorldGenerator
{
    private static final String __OBFID = "CL_00000413";

    public boolean generate(World p_76484_1_, Random p_76484_2_, int p_76484_3_, int p_76484_4_, int p_76484_5_)
    {
        if (!p_76484_1_.isAirBlock(p_76484_3_, p_76484_4_, p_76484_5_))
        {
            return false;
        }
        else if (p_76484_1_.getBlock(p_76484_3_, p_76484_4_ + 1, p_76484_5_) != Blocks.netherrack)
        {
            return false;
        }
        else
        {
            p_76484_1_.setBlock(p_76484_3_, p_76484_4_, p_76484_5_, Blocks.glowstone, 0, 2);

            for (int l = 0; l < 1500; ++l)
            {
                int i1 = p_76484_3_ + p_76484_2_.nextInt(8) - p_76484_2_.nextInt(8);
                int j1 = p_76484_4_ - p_76484_2_.nextInt(12);
                int k1 = p_76484_5_ + p_76484_2_.nextInt(8) - p_76484_2_.nextInt(8);

                if (p_76484_1_.getBlock(i1, j1, k1).getMaterial() == Material.air)
                {
                    int l1 = 0;

                    for (int i2 = 0; i2 < 6; ++i2)
                    {
                        Block block = null;

                        if (i2 == 0)
                        {
                            block = p_76484_1_.getBlock(i1 - 1, j1, k1);
                        }

                        if (i2 == 1)
                        {
                            block = p_76484_1_.getBlock(i1 + 1, j1, k1);
                        }

                        if (i2 == 2)
                        {
                            block = p_76484_1_.getBlock(i1, j1 - 1, k1);
                        }

                        if (i2 == 3)
                        {
                            block = p_76484_1_.getBlock(i1, j1 + 1, k1);
                        }

                        if (i2 == 4)
                        {
                            block = p_76484_1_.getBlock(i1, j1, k1 - 1);
                        }

                        if (i2 == 5)
                        {
                            block = p_76484_1_.getBlock(i1, j1, k1 + 1);
                        }

                        if (block == Blocks.glowstone)
                        {
                            ++l1;
                        }
                    }

                    if (l1 == 1)
                    {
                        p_76484_1_.setBlock(i1, j1, k1, Blocks.glowstone, 0, 2);
                    }
                }
            }

            return true;
        }
    }
}