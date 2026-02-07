package net.minecraft.world.gen.feature;

import java.util.Random;
import net.minecraft.block.Block;
import net.minecraft.init.Blocks;
import net.minecraft.world.World;

public class WorldGenBlockBlob extends WorldGenerator
{
    private Block field_150545_a;
    private int field_150544_b;
    private static final String __OBFID = "CL_00000402";

    public WorldGenBlockBlob(Block p_i45450_1_, int p_i45450_2_)
    {
        super(false);
        this.field_150545_a = p_i45450_1_;
        this.field_150544_b = p_i45450_2_;
    }

    public boolean generate(World p_76484_1_, Random p_76484_2_, int p_76484_3_, int p_76484_4_, int p_76484_5_)
    {
        while (true)
        {
            if (p_76484_4_ > 3)
            {
                label63:
                {
                    if (!p_76484_1_.isAirBlock(p_76484_3_, p_76484_4_ - 1, p_76484_5_))
                    {
                        Block block = p_76484_1_.getBlock(p_76484_3_, p_76484_4_ - 1, p_76484_5_);

                        if (block == Blocks.grass || block == Blocks.dirt || block == Blocks.stone)
                        {
                            break label63;
                        }
                    }

                    --p_76484_4_;
                    continue;
                }
            }

            if (p_76484_4_ <= 3)
            {
                return false;
            }

            int k2 = this.field_150544_b;

            for (int l = 0; k2 >= 0 && l < 3; ++l)
            {
                int i1 = k2 + p_76484_2_.nextInt(2);
                int j1 = k2 + p_76484_2_.nextInt(2);
                int k1 = k2 + p_76484_2_.nextInt(2);
                float f = (float)(i1 + j1 + k1) * 0.333F + 0.5F;

                for (int l1 = p_76484_3_ - i1; l1 <= p_76484_3_ + i1; ++l1)
                {
                    for (int i2 = p_76484_5_ - k1; i2 <= p_76484_5_ + k1; ++i2)
                    {
                        for (int j2 = p_76484_4_ - j1; j2 <= p_76484_4_ + j1; ++j2)
                        {
                            float f1 = (float)(l1 - p_76484_3_);
                            float f2 = (float)(i2 - p_76484_5_);
                            float f3 = (float)(j2 - p_76484_4_);

                            if (f1 * f1 + f2 * f2 + f3 * f3 <= f * f)
                            {
                                p_76484_1_.setBlock(l1, j2, i2, this.field_150545_a, 0, 4);
                            }
                        }
                    }
                }

                p_76484_3_ += -(k2 + 1) + p_76484_2_.nextInt(2 + k2 * 2);
                p_76484_5_ += -(k2 + 1) + p_76484_2_.nextInt(2 + k2 * 2);
                p_76484_4_ += 0 - p_76484_2_.nextInt(2);
            }

            return true;
        }
    }
}