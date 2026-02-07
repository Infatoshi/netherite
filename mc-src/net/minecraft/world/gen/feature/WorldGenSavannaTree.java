package net.minecraft.world.gen.feature;

import java.util.Random;
import net.minecraft.block.Block;
import net.minecraft.block.BlockSapling;
import net.minecraft.block.material.Material;
import net.minecraft.init.Blocks;
import net.minecraft.util.Direction;
import net.minecraft.world.World;
import net.minecraftforge.common.util.ForgeDirection;

public class WorldGenSavannaTree extends WorldGenAbstractTree
{
    private static final String __OBFID = "CL_00000432";

    public WorldGenSavannaTree(boolean p_i45463_1_)
    {
        super(p_i45463_1_);
    }

    public boolean generate(World p_76484_1_, Random p_76484_2_, int p_76484_3_, int p_76484_4_, int p_76484_5_)
    {
        int l = p_76484_2_.nextInt(3) + p_76484_2_.nextInt(3) + 5;
        boolean flag = true;

        if (p_76484_4_ >= 1 && p_76484_4_ + l + 1 <= 256)
        {
            int j1;
            int k1;

            for (int i1 = p_76484_4_; i1 <= p_76484_4_ + 1 + l; ++i1)
            {
                byte b0 = 1;

                if (i1 == p_76484_4_)
                {
                    b0 = 0;
                }

                if (i1 >= p_76484_4_ + 1 + l - 2)
                {
                    b0 = 2;
                }

                for (j1 = p_76484_3_ - b0; j1 <= p_76484_3_ + b0 && flag; ++j1)
                {
                    for (k1 = p_76484_5_ - b0; k1 <= p_76484_5_ + b0 && flag; ++k1)
                    {
                        if (i1 >= 0 && i1 < 256)
                        {
                            Block block = p_76484_1_.getBlock(j1, i1, k1);

                            if (!this.isReplaceable(p_76484_1_, j1, i1, k1))
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
                Block block3 = p_76484_1_.getBlock(p_76484_3_, p_76484_4_ - 1, p_76484_5_);

                boolean isSoil = block3.canSustainPlant(p_76484_1_, p_76484_3_, p_76484_4_ - 1, p_76484_5_, ForgeDirection.UP, (BlockSapling)Blocks.sapling);
                if (isSoil && p_76484_4_ < 256 - l - 1)
                {
                    block3.onPlantGrow(p_76484_1_, p_76484_3_, p_76484_4_ - 1, p_76484_5_, p_76484_3_, p_76484_4_, p_76484_5_);
                    int j3 = p_76484_2_.nextInt(4);
                    j1 = l - p_76484_2_.nextInt(4) - 1;
                    k1 = 3 - p_76484_2_.nextInt(3);
                    int k3 = p_76484_3_;
                    int l1 = p_76484_5_;
                    int i2 = 0;
                    int j2;
                    int k2;

                    for (j2 = 0; j2 < l; ++j2)
                    {
                        k2 = p_76484_4_ + j2;

                        if (j2 >= j1 && k1 > 0)
                        {
                            k3 += Direction.offsetX[j3];
                            l1 += Direction.offsetZ[j3];
                            --k1;
                        }

                        Block block1 = p_76484_1_.getBlock(k3, k2, l1);

                        if (block1.isAir(p_76484_1_, k3, k2, l1) || block1.isLeaves(p_76484_1_, k3, k2, l1))
                        {
                            this.setBlockAndNotifyAdequately(p_76484_1_, k3, k2, l1, Blocks.log2, 0);
                            i2 = k2;
                        }
                    }

                    for (j2 = -1; j2 <= 1; ++j2)
                    {
                        for (k2 = -1; k2 <= 1; ++k2)
                        {
                            this.func_150525_a(p_76484_1_, k3 + j2, i2 + 1, l1 + k2);
                        }
                    }

                    this.func_150525_a(p_76484_1_, k3 + 2, i2 + 1, l1);
                    this.func_150525_a(p_76484_1_, k3 - 2, i2 + 1, l1);
                    this.func_150525_a(p_76484_1_, k3, i2 + 1, l1 + 2);
                    this.func_150525_a(p_76484_1_, k3, i2 + 1, l1 - 2);

                    for (j2 = -3; j2 <= 3; ++j2)
                    {
                        for (k2 = -3; k2 <= 3; ++k2)
                        {
                            if (Math.abs(j2) != 3 || Math.abs(k2) != 3)
                            {
                                this.func_150525_a(p_76484_1_, k3 + j2, i2, l1 + k2);
                            }
                        }
                    }

                    k3 = p_76484_3_;
                    l1 = p_76484_5_;
                    j2 = p_76484_2_.nextInt(4);

                    if (j2 != j3)
                    {
                        k2 = j1 - p_76484_2_.nextInt(2) - 1;
                        int l3 = 1 + p_76484_2_.nextInt(3);
                        i2 = 0;
                        int l2;
                        int i3;

                        for (l2 = k2; l2 < l && l3 > 0; --l3)
                        {
                            if (l2 >= 1)
                            {
                                i3 = p_76484_4_ + l2;
                                k3 += Direction.offsetX[j2];
                                l1 += Direction.offsetZ[j2];
                                Block block2 = p_76484_1_.getBlock(k3, i3, l1);

                                if (block2.isAir(p_76484_1_, k3, i3, l1) || block2.isLeaves(p_76484_1_, k3, i3, l1))
                                {
                                    this.setBlockAndNotifyAdequately(p_76484_1_, k3, i3, l1, Blocks.log2, 0);
                                    i2 = i3;
                                }
                            }

                            ++l2;
                        }

                        if (i2 > 0)
                        {
                            for (l2 = -1; l2 <= 1; ++l2)
                            {
                                for (i3 = -1; i3 <= 1; ++i3)
                                {
                                    this.func_150525_a(p_76484_1_, k3 + l2, i2 + 1, l1 + i3);
                                }
                            }

                            for (l2 = -2; l2 <= 2; ++l2)
                            {
                                for (i3 = -2; i3 <= 2; ++i3)
                                {
                                    if (Math.abs(l2) != 2 || Math.abs(i3) != 2)
                                    {
                                        this.func_150525_a(p_76484_1_, k3 + l2, i2, l1 + i3);
                                    }
                                }
                            }
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

    private void func_150525_a(World p_150525_1_, int p_150525_2_, int p_150525_3_, int p_150525_4_)
    {
        Block block = p_150525_1_.getBlock(p_150525_2_, p_150525_3_, p_150525_4_);

        if (block.isAir(p_150525_1_, p_150525_2_, p_150525_3_, p_150525_4_) || block.isLeaves(p_150525_1_, p_150525_2_, p_150525_3_, p_150525_4_))
        {
            this.setBlockAndNotifyAdequately(p_150525_1_, p_150525_2_, p_150525_3_, p_150525_4_, Blocks.leaves2, 0);
        }
    }
}