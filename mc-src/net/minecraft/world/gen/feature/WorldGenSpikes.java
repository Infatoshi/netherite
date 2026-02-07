package net.minecraft.world.gen.feature;

import java.util.Random;
import net.minecraft.block.Block;
import net.minecraft.entity.item.EntityEnderCrystal;
import net.minecraft.init.Blocks;
import net.minecraft.world.World;

public class WorldGenSpikes extends WorldGenerator
{
    private Block field_150520_a;
    private static final String __OBFID = "CL_00000433";

    public WorldGenSpikes(Block p_i45464_1_)
    {
        this.field_150520_a = p_i45464_1_;
    }

    public boolean generate(World p_76484_1_, Random p_76484_2_, int p_76484_3_, int p_76484_4_, int p_76484_5_)
    {
        if (p_76484_1_.isAirBlock(p_76484_3_, p_76484_4_, p_76484_5_) && p_76484_1_.getBlock(p_76484_3_, p_76484_4_ - 1, p_76484_5_) == this.field_150520_a)
        {
            int l = p_76484_2_.nextInt(32) + 6;
            int i1 = p_76484_2_.nextInt(4) + 1;
            int j1;
            int k1;
            int l1;
            int i2;

            for (j1 = p_76484_3_ - i1; j1 <= p_76484_3_ + i1; ++j1)
            {
                for (k1 = p_76484_5_ - i1; k1 <= p_76484_5_ + i1; ++k1)
                {
                    l1 = j1 - p_76484_3_;
                    i2 = k1 - p_76484_5_;

                    if (l1 * l1 + i2 * i2 <= i1 * i1 + 1 && p_76484_1_.getBlock(j1, p_76484_4_ - 1, k1) != this.field_150520_a)
                    {
                        return false;
                    }
                }
            }

            for (j1 = p_76484_4_; j1 < p_76484_4_ + l && j1 < 256; ++j1)
            {
                for (k1 = p_76484_3_ - i1; k1 <= p_76484_3_ + i1; ++k1)
                {
                    for (l1 = p_76484_5_ - i1; l1 <= p_76484_5_ + i1; ++l1)
                    {
                        i2 = k1 - p_76484_3_;
                        int j2 = l1 - p_76484_5_;

                        if (i2 * i2 + j2 * j2 <= i1 * i1 + 1)
                        {
                            p_76484_1_.setBlock(k1, j1, l1, Blocks.obsidian, 0, 2);
                        }
                    }
                }
            }

            EntityEnderCrystal entityendercrystal = new EntityEnderCrystal(p_76484_1_);
            entityendercrystal.setLocationAndAngles((double)((float)p_76484_3_ + 0.5F), (double)(p_76484_4_ + l), (double)((float)p_76484_5_ + 0.5F), p_76484_2_.nextFloat() * 360.0F, 0.0F);
            p_76484_1_.spawnEntityInWorld(entityendercrystal);
            p_76484_1_.setBlock(p_76484_3_, p_76484_4_ + l, p_76484_5_, Blocks.bedrock, 0, 2);
            return true;
        }
        else
        {
            return false;
        }
    }
}