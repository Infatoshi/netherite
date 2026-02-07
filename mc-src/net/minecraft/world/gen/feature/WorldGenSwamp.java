package net.minecraft.world.gen.feature;

import java.util.Random;
import net.minecraft.block.Block;
import net.minecraft.block.BlockSapling;
import net.minecraft.block.material.Material;
import net.minecraft.init.Blocks;
import net.minecraft.world.World;
import net.minecraftforge.common.util.ForgeDirection;

public class WorldGenSwamp extends WorldGenAbstractTree
{
    private static final String __OBFID = "CL_00000436";

    public WorldGenSwamp()
    {
        super(false);
    }

    public boolean generate(World p_76484_1_, Random p_76484_2_, int p_76484_3_, int p_76484_4_, int p_76484_5_)
    {
        int l;

        for (l = p_76484_2_.nextInt(4) + 5; p_76484_1_.getBlock(p_76484_3_, p_76484_4_ - 1, p_76484_5_).getMaterial() == Material.water; --p_76484_4_)
        {
            ;
        }

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
                    b0 = 3;
                }

                for (j1 = p_76484_3_ - b0; j1 <= p_76484_3_ + b0 && flag; ++j1)
                {
                    for (k1 = p_76484_5_ - b0; k1 <= p_76484_5_ + b0 && flag; ++k1)
                    {
                        if (i1 >= 0 && i1 < 256)
                        {
                            Block block = p_76484_1_.getBlock(j1, i1, k1);

                            if (!(block.isAir(p_76484_1_, j1, i1, k1) || block.isLeaves(p_76484_1_, j1, i1, k1)))
                            {
                                if (block != Blocks.water && block != Blocks.flowing_water)
                                {
                                    flag = false;
                                }
                                else if (i1 > p_76484_4_)
                                {
                                    flag = false;
                                }
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
                    int l1;
                    int k2;
                    int l2;

                    for (k2 = p_76484_4_ - 3 + l; k2 <= p_76484_4_ + l; ++k2)
                    {
                        j1 = k2 - (p_76484_4_ + l);
                        k1 = 2 - j1 / 2;

                        for (l2 = p_76484_3_ - k1; l2 <= p_76484_3_ + k1; ++l2)
                        {
                            l1 = l2 - p_76484_3_;

                            for (int i2 = p_76484_5_ - k1; i2 <= p_76484_5_ + k1; ++i2)
                            {
                                int j2 = i2 - p_76484_5_;

                                if ((Math.abs(l1) != k1 || Math.abs(j2) != k1 || p_76484_2_.nextInt(2) != 0 && j1 != 0) && p_76484_1_.getBlock(l2, k2, i2).canBeReplacedByLeaves(p_76484_1_, l2, k2, i2))
                                {
                                    this.func_150515_a(p_76484_1_, l2, k2, i2, Blocks.leaves);
                                }
                            }
                        }
                    }

                    for (k2 = 0; k2 < l; ++k2)
                    {
                        Block block2 = p_76484_1_.getBlock(p_76484_3_, p_76484_4_ + k2, p_76484_5_);

                        if (block2.isAir(p_76484_1_, p_76484_3_, p_76484_4_ + k2, p_76484_5_) || block2.isLeaves(p_76484_1_, p_76484_3_, p_76484_4_ + k2, p_76484_5_) || block2 == Blocks.flowing_water || block2 == Blocks.water)
                        {
                            this.func_150515_a(p_76484_1_, p_76484_3_, p_76484_4_ + k2, p_76484_5_, Blocks.log);
                        }
                    }

                    for (k2 = p_76484_4_ - 3 + l; k2 <= p_76484_4_ + l; ++k2)
                    {
                        j1 = k2 - (p_76484_4_ + l);
                        k1 = 2 - j1 / 2;

                        for (l2 = p_76484_3_ - k1; l2 <= p_76484_3_ + k1; ++l2)
                        {
                            for (l1 = p_76484_5_ - k1; l1 <= p_76484_5_ + k1; ++l1)
                            {
                                if (p_76484_1_.getBlock(l2, k2, l1).isLeaves(p_76484_1_, l2, k2, l1))
                                {
                                    if (p_76484_2_.nextInt(4) == 0 && p_76484_1_.getBlock(l2 - 1, k2, l1).isAir(p_76484_1_, l2 - 1, k2, l1))
                                    {
                                        this.generateVines(p_76484_1_, l2 - 1, k2, l1, 8);
                                    }

                                    if (p_76484_2_.nextInt(4) == 0 && p_76484_1_.getBlock(l2 + 1, k2, l1).isAir(p_76484_1_, l2 + 1, k2, l1))
                                    {
                                        this.generateVines(p_76484_1_, l2 + 1, k2, l1, 2);
                                    }

                                    if (p_76484_2_.nextInt(4) == 0 && p_76484_1_.getBlock(l2, k2, l1 - 1).isAir(p_76484_1_, l2, k2, l1 - 1))
                                    {
                                        this.generateVines(p_76484_1_, l2, k2, l1 - 1, 1);
                                    }

                                    if (p_76484_2_.nextInt(4) == 0 && p_76484_1_.getBlock(l2, k2, l1 + 1).isAir(p_76484_1_, l2, k2, l1 + 1))
                                    {
                                        this.generateVines(p_76484_1_, l2, k2, l1 + 1, 4);
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

    /**
     * Generates vines at the given position until it hits a block.
     */
    private void generateVines(World p_76536_1_, int p_76536_2_, int p_76536_3_, int p_76536_4_, int p_76536_5_)
    {
        this.setBlockAndNotifyAdequately(p_76536_1_, p_76536_2_, p_76536_3_, p_76536_4_, Blocks.vine, p_76536_5_);
        int i1 = 4;

        while (true)
        {
            --p_76536_3_;

            if (!(p_76536_1_.getBlock(p_76536_2_, p_76536_3_, p_76536_4_).isAir(p_76536_1_, p_76536_2_, p_76536_3_, p_76536_4_)) || i1 <= 0)
            {
                return;
            }

            this.setBlockAndNotifyAdequately(p_76536_1_, p_76536_2_, p_76536_3_, p_76536_4_, Blocks.vine, p_76536_5_);
            --i1;
        }
    }
}