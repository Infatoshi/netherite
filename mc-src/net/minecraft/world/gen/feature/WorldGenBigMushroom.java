package net.minecraft.world.gen.feature;

import java.util.Random;
import net.minecraft.block.Block;
import net.minecraft.block.material.Material;
import net.minecraft.init.Blocks;
import net.minecraft.world.World;

public class WorldGenBigMushroom extends WorldGenerator
{
    /** The mushroom type. 0 for brown, 1 for red. */
    private int mushroomType = -1;
    private static final String __OBFID = "CL_00000415";

    public WorldGenBigMushroom(int p_i2017_1_)
    {
        super(true);
        this.mushroomType = p_i2017_1_;
    }

    public WorldGenBigMushroom()
    {
        super(false);
    }

    public boolean generate(World p_76484_1_, Random p_76484_2_, int p_76484_3_, int p_76484_4_, int p_76484_5_)
    {
        int l = p_76484_2_.nextInt(2);

        if (this.mushroomType >= 0)
        {
            l = this.mushroomType;
        }

        int i1 = p_76484_2_.nextInt(3) + 4;
        boolean flag = true;

        if (p_76484_4_ >= 1 && p_76484_4_ + i1 + 1 < 256)
        {
            int k1;
            int l1;

            for (int j1 = p_76484_4_; j1 <= p_76484_4_ + 1 + i1; ++j1)
            {
                byte b0 = 3;

                if (j1 <= p_76484_4_ + 3)
                {
                    b0 = 0;
                }

                for (k1 = p_76484_3_ - b0; k1 <= p_76484_3_ + b0 && flag; ++k1)
                {
                    for (l1 = p_76484_5_ - b0; l1 <= p_76484_5_ + b0 && flag; ++l1)
                    {
                        if (j1 >= 0 && j1 < 256)
                        {
                            Block block = p_76484_1_.getBlock(k1, j1, l1);

                            if (!block.isAir(p_76484_1_, k1, j1, l1) && !block.isLeaves(p_76484_1_, k1, j1, l1))
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

                if (block1 != Blocks.dirt && block1 != Blocks.grass && block1 != Blocks.mycelium)
                {
                    return false;
                }
                else
                {
                    int k2 = p_76484_4_ + i1;

                    if (l == 1)
                    {
                        k2 = p_76484_4_ + i1 - 3;
                    }

                    for (k1 = k2; k1 <= p_76484_4_ + i1; ++k1)
                    {
                        l1 = 1;

                        if (k1 < p_76484_4_ + i1)
                        {
                            ++l1;
                        }

                        if (l == 0)
                        {
                            l1 = 3;
                        }

                        for (int l2 = p_76484_3_ - l1; l2 <= p_76484_3_ + l1; ++l2)
                        {
                            for (int i2 = p_76484_5_ - l1; i2 <= p_76484_5_ + l1; ++i2)
                            {
                                int j2 = 5;

                                if (l2 == p_76484_3_ - l1)
                                {
                                    --j2;
                                }

                                if (l2 == p_76484_3_ + l1)
                                {
                                    ++j2;
                                }

                                if (i2 == p_76484_5_ - l1)
                                {
                                    j2 -= 3;
                                }

                                if (i2 == p_76484_5_ + l1)
                                {
                                    j2 += 3;
                                }

                                if (l == 0 || k1 < p_76484_4_ + i1)
                                {
                                    if ((l2 == p_76484_3_ - l1 || l2 == p_76484_3_ + l1) && (i2 == p_76484_5_ - l1 || i2 == p_76484_5_ + l1))
                                    {
                                        continue;
                                    }

                                    if (l2 == p_76484_3_ - (l1 - 1) && i2 == p_76484_5_ - l1)
                                    {
                                        j2 = 1;
                                    }

                                    if (l2 == p_76484_3_ - l1 && i2 == p_76484_5_ - (l1 - 1))
                                    {
                                        j2 = 1;
                                    }

                                    if (l2 == p_76484_3_ + (l1 - 1) && i2 == p_76484_5_ - l1)
                                    {
                                        j2 = 3;
                                    }

                                    if (l2 == p_76484_3_ + l1 && i2 == p_76484_5_ - (l1 - 1))
                                    {
                                        j2 = 3;
                                    }

                                    if (l2 == p_76484_3_ - (l1 - 1) && i2 == p_76484_5_ + l1)
                                    {
                                        j2 = 7;
                                    }

                                    if (l2 == p_76484_3_ - l1 && i2 == p_76484_5_ + (l1 - 1))
                                    {
                                        j2 = 7;
                                    }

                                    if (l2 == p_76484_3_ + (l1 - 1) && i2 == p_76484_5_ + l1)
                                    {
                                        j2 = 9;
                                    }

                                    if (l2 == p_76484_3_ + l1 && i2 == p_76484_5_ + (l1 - 1))
                                    {
                                        j2 = 9;
                                    }
                                }

                                if (j2 == 5 && k1 < p_76484_4_ + i1)
                                {
                                    j2 = 0;
                                }

                                if ((j2 != 0 || p_76484_4_ >= p_76484_4_ + i1 - 1) && p_76484_1_.getBlock(l2, k1, i2).canBeReplacedByLeaves(p_76484_1_, l2, k1, i2))
                                {
                                    this.setBlockAndNotifyAdequately(p_76484_1_, l2, k1, i2, Block.getBlockById(Block.getIdFromBlock(Blocks.brown_mushroom_block) + l), j2);
                                }
                            }
                        }
                    }

                    for (k1 = 0; k1 < i1; ++k1)
                    {
                        Block block2 = p_76484_1_.getBlock(p_76484_3_, p_76484_4_ + k1, p_76484_5_);

                        if (block2.canBeReplacedByLeaves(p_76484_1_, p_76484_3_, p_76484_4_ + k1, p_76484_5_))
                        {
                            this.setBlockAndNotifyAdequately(p_76484_1_, p_76484_3_, p_76484_4_ + k1, p_76484_5_, Block.getBlockById(Block.getIdFromBlock(Blocks.brown_mushroom_block) + l), 10);
                        }
                    }

                    return true;
                }
            }
        }
        else
        {
            return false;
        }
    }
}