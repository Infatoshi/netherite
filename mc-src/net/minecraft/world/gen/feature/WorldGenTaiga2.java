package net.minecraft.world.gen.feature;

import java.util.Random;
import net.minecraft.block.Block;
import net.minecraft.block.BlockSapling;
import net.minecraft.block.material.Material;
import net.minecraft.init.Blocks;
import net.minecraft.world.World;
import net.minecraftforge.common.util.ForgeDirection;

public class WorldGenTaiga2 extends WorldGenAbstractTree
{
    private static final String __OBFID = "CL_00000435";

    public WorldGenTaiga2(boolean p_i2025_1_)
    {
        super(p_i2025_1_);
    }

    public boolean generate(World p_76484_1_, Random p_76484_2_, int p_76484_3_, int p_76484_4_, int p_76484_5_)
    {
        int l = p_76484_2_.nextInt(4) + 6;
        int i1 = 1 + p_76484_2_.nextInt(2);
        int j1 = l - i1;
        int k1 = 2 + p_76484_2_.nextInt(2);
        boolean flag = true;

        if (p_76484_4_ >= 1 && p_76484_4_ + l + 1 <= 256)
        {
            int i2;
            int l3;

            for (int l1 = p_76484_4_; l1 <= p_76484_4_ + 1 + l && flag; ++l1)
            {
                boolean flag1 = true;

                if (l1 - p_76484_4_ < i1)
                {
                    l3 = 0;
                }
                else
                {
                    l3 = k1;
                }

                for (i2 = p_76484_3_ - l3; i2 <= p_76484_3_ + l3 && flag; ++i2)
                {
                    for (int j2 = p_76484_5_ - l3; j2 <= p_76484_5_ + l3 && flag; ++j2)
                    {
                        if (l1 >= 0 && l1 < 256)
                        {
                            Block block = p_76484_1_.getBlock(i2, l1, j2);

                            if (!block.isAir(p_76484_1_, i2, l1, j2) && !block.isLeaves(p_76484_1_, i2, l1, j2))
                            {
                                flag = false;
                            }
                        }
                        else
                        {
                            flag = false;
                        }
                    }
                }
            }

            if (!flag)
            {
                return false;
            }
            else
            {
                Block block1 = p_76484_1_.getBlock(p_76484_3_, p_76484_4_ - 1, p_76484_5_);

                boolean isSoil = block1.canSustainPlant(p_76484_1_, p_76484_3_, p_76484_4_ - 1, p_76484_5_, ForgeDirection.UP, (BlockSapling)Blocks.sapling);
                if (isSoil && p_76484_4_ < 256 - l - 1)
                {
                    block1.onPlantGrow(p_76484_1_, p_76484_3_, p_76484_4_ - 1, p_76484_5_, p_76484_3_, p_76484_4_, p_76484_5_);
                    l3 = p_76484_2_.nextInt(2);
                    i2 = 1;
                    byte b0 = 0;
                    int k2;
                    int i4;

                    for (i4 = 0; i4 <= j1; ++i4)
                    {
                        k2 = p_76484_4_ + l - i4;

                        for (int l2 = p_76484_3_ - l3; l2 <= p_76484_3_ + l3; ++l2)
                        {
                            int i3 = l2 - p_76484_3_;

                            for (int j3 = p_76484_5_ - l3; j3 <= p_76484_5_ + l3; ++j3)
                            {
                                int k3 = j3 - p_76484_5_;

                                if ((Math.abs(i3) != l3 || Math.abs(k3) != l3 || l3 <= 0) && p_76484_1_.getBlock(l2, k2, j3).canBeReplacedByLeaves(p_76484_1_, l2, k2, j3))
                                {
                                    this.setBlockAndNotifyAdequately(p_76484_1_, l2, k2, j3, Blocks.leaves, 1);
                                }
                            }
                        }

                        if (l3 >= i2)
                        {
                            l3 = b0;
                            b0 = 1;
                            ++i2;

                            if (i2 > k1)
                            {
                                i2 = k1;
                            }
                        }
                        else
                        {
                            ++l3;
                        }
                    }

                    i4 = p_76484_2_.nextInt(3);

                    for (k2 = 0; k2 < l - i4; ++k2)
                    {
                        Block block2 = p_76484_1_.getBlock(p_76484_3_, p_76484_4_ + k2, p_76484_5_);

                        if (block2.isAir(p_76484_1_, p_76484_3_, p_76484_4_ + k2, p_76484_5_) || block2.isLeaves(p_76484_1_, p_76484_3_, p_76484_4_ + k2, p_76484_5_))
                        {
                            this.setBlockAndNotifyAdequately(p_76484_1_, p_76484_3_, p_76484_4_ + k2, p_76484_5_, Blocks.log, 1);
                        }
                    }

                    return true;
                }
                else
                {
                    return false;
                }
            }
        }
        else
        {
            return false;
        }
    }
}