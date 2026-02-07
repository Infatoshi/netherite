package net.minecraft.world.gen.feature;

import java.util.Random;
import net.minecraft.block.Block;
import net.minecraft.block.BlockSapling;
import net.minecraft.block.material.Material;
import net.minecraft.init.Blocks;
import net.minecraft.util.Direction;
import net.minecraft.world.World;
import net.minecraftforge.common.util.ForgeDirection;

public class WorldGenCanopyTree extends WorldGenAbstractTree
{
    private static final String __OBFID = "CL_00000430";

    public WorldGenCanopyTree(boolean p_i45461_1_)
    {
        super(p_i45461_1_);
    }

    public boolean generate(World p_76484_1_, Random p_76484_2_, int p_76484_3_, int p_76484_4_, int p_76484_5_)
    {
        int l = p_76484_2_.nextInt(3) + p_76484_2_.nextInt(2) + 6;
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
                Block block2 = p_76484_1_.getBlock(p_76484_3_, p_76484_4_ - 1, p_76484_5_);

                boolean isSoil = block2.canSustainPlant(p_76484_1_, p_76484_3_, p_76484_4_ - 1, p_76484_5_, ForgeDirection.UP, (BlockSapling)Blocks.sapling);
                if (isSoil && p_76484_4_ < 256 - l - 1)
                {
                    onPlantGrow(p_76484_1_, p_76484_3_,     p_76484_4_ - 1, p_76484_5_,     p_76484_3_, p_76484_4_, p_76484_5_);
                    onPlantGrow(p_76484_1_, p_76484_3_ + 1, p_76484_4_ - 1, p_76484_5_,     p_76484_3_, p_76484_4_, p_76484_5_);
                    onPlantGrow(p_76484_1_, p_76484_3_ + 1, p_76484_4_ - 1, p_76484_5_ + 1, p_76484_3_, p_76484_4_, p_76484_5_);
                    onPlantGrow(p_76484_1_, p_76484_3_,     p_76484_4_ - 1, p_76484_5_ + 1, p_76484_3_, p_76484_4_, p_76484_5_);
                    int j3 = p_76484_2_.nextInt(4);
                    j1 = l - p_76484_2_.nextInt(4);
                    k1 = 2 - p_76484_2_.nextInt(3);
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
                            this.setBlockAndNotifyAdequately(p_76484_1_, k3, k2, l1, Blocks.log2, 1);
                            this.setBlockAndNotifyAdequately(p_76484_1_, k3 + 1, k2, l1, Blocks.log2, 1);
                            this.setBlockAndNotifyAdequately(p_76484_1_, k3, k2, l1 + 1, Blocks.log2, 1);
                            this.setBlockAndNotifyAdequately(p_76484_1_, k3 + 1, k2, l1 + 1, Blocks.log2, 1);
                            i2 = k2;
                        }
                    }

                    for (j2 = -2; j2 <= 0; ++j2)
                    {
                        for (k2 = -2; k2 <= 0; ++k2)
                        {
                            byte b1 = -1;
                            this.func_150526_a(p_76484_1_, k3 + j2, i2 + b1, l1 + k2);
                            this.func_150526_a(p_76484_1_, 1 + k3 - j2, i2 + b1, l1 + k2);
                            this.func_150526_a(p_76484_1_, k3 + j2, i2 + b1, 1 + l1 - k2);
                            this.func_150526_a(p_76484_1_, 1 + k3 - j2, i2 + b1, 1 + l1 - k2);

                            if ((j2 > -2 || k2 > -1) && (j2 != -1 || k2 != -2))
                            {
                                byte b2 = 1;
                                this.func_150526_a(p_76484_1_, k3 + j2, i2 + b2, l1 + k2);
                                this.func_150526_a(p_76484_1_, 1 + k3 - j2, i2 + b2, l1 + k2);
                                this.func_150526_a(p_76484_1_, k3 + j2, i2 + b2, 1 + l1 - k2);
                                this.func_150526_a(p_76484_1_, 1 + k3 - j2, i2 + b2, 1 + l1 - k2);
                            }
                        }
                    }

                    if (p_76484_2_.nextBoolean())
                    {
                        this.func_150526_a(p_76484_1_, k3, i2 + 2, l1);
                        this.func_150526_a(p_76484_1_, k3 + 1, i2 + 2, l1);
                        this.func_150526_a(p_76484_1_, k3 + 1, i2 + 2, l1 + 1);
                        this.func_150526_a(p_76484_1_, k3, i2 + 2, l1 + 1);
                    }

                    for (j2 = -3; j2 <= 4; ++j2)
                    {
                        for (k2 = -3; k2 <= 4; ++k2)
                        {
                            if ((j2 != -3 || k2 != -3) && (j2 != -3 || k2 != 4) && (j2 != 4 || k2 != -3) && (j2 != 4 || k2 != 4) && (Math.abs(j2) < 3 || Math.abs(k2) < 3))
                            {
                                this.func_150526_a(p_76484_1_, k3 + j2, i2, l1 + k2);
                            }
                        }
                    }

                    for (j2 = -1; j2 <= 2; ++j2)
                    {
                        for (k2 = -1; k2 <= 2; ++k2)
                        {
                            if ((j2 < 0 || j2 > 1 || k2 < 0 || k2 > 1) && p_76484_2_.nextInt(3) <= 0)
                            {
                                int l3 = p_76484_2_.nextInt(3) + 2;
                                int l2;

                                for (l2 = 0; l2 < l3; ++l2)
                                {
                                    this.setBlockAndNotifyAdequately(p_76484_1_, p_76484_3_ + j2, i2 - l2 - 1, p_76484_5_ + k2, Blocks.log2, 1);
                                }

                                int i3;

                                for (l2 = -1; l2 <= 1; ++l2)
                                {
                                    for (i3 = -1; i3 <= 1; ++i3)
                                    {
                                        this.func_150526_a(p_76484_1_, k3 + j2 + l2, i2 - 0, l1 + k2 + i3);
                                    }
                                }

                                for (l2 = -2; l2 <= 2; ++l2)
                                {
                                    for (i3 = -2; i3 <= 2; ++i3)
                                    {
                                        if (Math.abs(l2) != 2 || Math.abs(i3) != 2)
                                        {
                                            this.func_150526_a(p_76484_1_, k3 + j2 + l2, i2 - 1, l1 + k2 + i3);
                                        }
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

    private void func_150526_a(World p_150526_1_, int p_150526_2_, int p_150526_3_, int p_150526_4_)
    {
        Block block = p_150526_1_.getBlock(p_150526_2_, p_150526_3_, p_150526_4_);

        if (block.isAir(p_150526_1_, p_150526_2_, p_150526_3_, p_150526_4_))
        {
            this.setBlockAndNotifyAdequately(p_150526_1_, p_150526_2_, p_150526_3_, p_150526_4_, Blocks.leaves2, 1);
        }
    }

    //Just a helper macro
    private void onPlantGrow(World world, int x, int y, int z, int sourceX, int sourceY, int sourceZ)
    {
        world.getBlock(x, y, z).onPlantGrow(world, x, y, z, sourceX, sourceY, sourceZ);
    }
}