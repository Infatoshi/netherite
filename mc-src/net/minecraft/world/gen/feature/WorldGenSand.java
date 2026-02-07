package net.minecraft.world.gen.feature;

import java.util.Random;
import net.minecraft.block.Block;
import net.minecraft.block.material.Material;
import net.minecraft.init.Blocks;
import net.minecraft.world.World;

public class WorldGenSand extends WorldGenerator
{
    private Block field_150517_a;
    /** The maximum radius used when generating a patch of blocks. */
    private int radius;
    private static final String __OBFID = "CL_00000431";

    public WorldGenSand(Block p_i45462_1_, int p_i45462_2_)
    {
        this.field_150517_a = p_i45462_1_;
        this.radius = p_i45462_2_;
    }

    public boolean generate(World p_76484_1_, Random p_76484_2_, int p_76484_3_, int p_76484_4_, int p_76484_5_)
    {
        if (p_76484_1_.getBlock(p_76484_3_, p_76484_4_, p_76484_5_).getMaterial() != Material.water)
        {
            return false;
        }
        else
        {
            int l = p_76484_2_.nextInt(this.radius - 2) + 2;
            byte b0 = 2;

            for (int i1 = p_76484_3_ - l; i1 <= p_76484_3_ + l; ++i1)
            {
                for (int j1 = p_76484_5_ - l; j1 <= p_76484_5_ + l; ++j1)
                {
                    int k1 = i1 - p_76484_3_;
                    int l1 = j1 - p_76484_5_;

                    if (k1 * k1 + l1 * l1 <= l * l)
                    {
                        for (int i2 = p_76484_4_ - b0; i2 <= p_76484_4_ + b0; ++i2)
                        {
                            Block block = p_76484_1_.getBlock(i1, i2, j1);

                            if (block == Blocks.dirt || block == Blocks.grass)
                            {
                                p_76484_1_.setBlock(i1, i2, j1, this.field_150517_a, 0, 2);
                            }
                        }
                    }
                }
            }

            return true;
        }
    }
}