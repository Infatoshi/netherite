package net.minecraft.world.gen.feature;

import java.util.Random;
import net.minecraft.block.material.Material;
import net.minecraft.init.Blocks;
import net.minecraft.world.World;

public class WorldGenReed extends WorldGenerator
{
    private static final String __OBFID = "CL_00000429";

    public boolean generate(World p_76484_1_, Random p_76484_2_, int p_76484_3_, int p_76484_4_, int p_76484_5_)
    {
        for (int l = 0; l < 20; ++l)
        {
            int i1 = p_76484_3_ + p_76484_2_.nextInt(4) - p_76484_2_.nextInt(4);
            int j1 = p_76484_4_;
            int k1 = p_76484_5_ + p_76484_2_.nextInt(4) - p_76484_2_.nextInt(4);

            if (p_76484_1_.isAirBlock(i1, p_76484_4_, k1) && (p_76484_1_.getBlock(i1 - 1, p_76484_4_ - 1, k1).getMaterial() == Material.water || p_76484_1_.getBlock(i1 + 1, p_76484_4_ - 1, k1).getMaterial() == Material.water || p_76484_1_.getBlock(i1, p_76484_4_ - 1, k1 - 1).getMaterial() == Material.water || p_76484_1_.getBlock(i1, p_76484_4_ - 1, k1 + 1).getMaterial() == Material.water))
            {
                int l1 = 2 + p_76484_2_.nextInt(p_76484_2_.nextInt(3) + 1);

                for (int i2 = 0; i2 < l1; ++i2)
                {
                    if (Blocks.reeds.canBlockStay(p_76484_1_, i1, j1 + i2, k1))
                    {
                        p_76484_1_.setBlock(i1, j1 + i2, k1, Blocks.reeds, 0, 2);
                    }
                }
            }
        }

        return true;
    }
}