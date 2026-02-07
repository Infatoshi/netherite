package net.minecraft.block;

import java.util.Random;
import net.minecraft.init.Blocks;
import net.minecraft.world.World;
import net.minecraft.world.gen.feature.WorldGenBigMushroom;
import net.minecraftforge.common.util.ForgeDirection;

public class BlockMushroom extends BlockBush implements IGrowable
{
    private static final String __OBFID = "CL_00000272";

    protected BlockMushroom()
    {
        float f = 0.2F;
        this.setBlockBounds(0.5F - f, 0.0F, 0.5F - f, 0.5F + f, f * 2.0F, 0.5F + f);
        this.setTickRandomly(true);
    }

    /**
     * Ticks the block if it's been scheduled
     */
    public void updateTick(World p_149674_1_, int p_149674_2_, int p_149674_3_, int p_149674_4_, Random p_149674_5_)
    {
        if (p_149674_5_.nextInt(25) == 0)
        {
            byte b0 = 4;
            int l = 5;
            int i1;
            int j1;
            int k1;

            for (i1 = p_149674_2_ - b0; i1 <= p_149674_2_ + b0; ++i1)
            {
                for (j1 = p_149674_4_ - b0; j1 <= p_149674_4_ + b0; ++j1)
                {
                    for (k1 = p_149674_3_ - 1; k1 <= p_149674_3_ + 1; ++k1)
                    {
                        if (p_149674_1_.getBlock(i1, k1, j1) == this)
                        {
                            --l;

                            if (l <= 0)
                            {
                                return;
                            }
                        }
                    }
                }
            }

            i1 = p_149674_2_ + p_149674_5_.nextInt(3) - 1;
            j1 = p_149674_3_ + p_149674_5_.nextInt(2) - p_149674_5_.nextInt(2);
            k1 = p_149674_4_ + p_149674_5_.nextInt(3) - 1;

            for (int l1 = 0; l1 < 4; ++l1)
            {
                if (p_149674_1_.isAirBlock(i1, j1, k1) && this.canBlockStay(p_149674_1_, i1, j1, k1))
                {
                    p_149674_2_ = i1;
                    p_149674_3_ = j1;
                    p_149674_4_ = k1;
                }

                i1 = p_149674_2_ + p_149674_5_.nextInt(3) - 1;
                j1 = p_149674_3_ + p_149674_5_.nextInt(2) - p_149674_5_.nextInt(2);
                k1 = p_149674_4_ + p_149674_5_.nextInt(3) - 1;
            }

            if (p_149674_1_.isAirBlock(i1, j1, k1) && this.canBlockStay(p_149674_1_, i1, j1, k1))
            {
                p_149674_1_.setBlock(i1, j1, k1, this, 0, 2);
            }
        }
    }

    /**
     * Checks to see if its valid to put this block at the specified coordinates. Args: world, x, y, z
     */
    public boolean canPlaceBlockAt(World p_149742_1_, int p_149742_2_, int p_149742_3_, int p_149742_4_)
    {
        return super.canPlaceBlockAt(p_149742_1_, p_149742_2_, p_149742_3_, p_149742_4_) && this.canBlockStay(p_149742_1_, p_149742_2_, p_149742_3_, p_149742_4_);
    }

    /**
     * is the block grass, dirt or farmland
     */
    protected boolean canPlaceBlockOn(Block p_149854_1_)
    {
        return p_149854_1_.func_149730_j();
    }

    /**
     * Can this block stay at this position.  Similar to canPlaceBlockAt except gets checked often with plants.
     */
    public boolean canBlockStay(World p_149718_1_, int p_149718_2_, int p_149718_3_, int p_149718_4_)
    {
        if (p_149718_3_ >= 0 && p_149718_3_ < 256)
        {
            Block block = p_149718_1_.getBlock(p_149718_2_, p_149718_3_ - 1, p_149718_4_);
            return block == Blocks.mycelium || block == Blocks.dirt && p_149718_1_.getBlockMetadata(p_149718_2_, p_149718_3_ - 1, p_149718_4_) == 2 || p_149718_1_.getFullBlockLightValue(p_149718_2_, p_149718_3_, p_149718_4_) < 13 && block.canSustainPlant(p_149718_1_, p_149718_2_, p_149718_3_ - 1, p_149718_4_, ForgeDirection.UP, this);
        }
        else
        {
            return false;
        }
    }

    public boolean func_149884_c(World p_149884_1_, int p_149884_2_, int p_149884_3_, int p_149884_4_, Random p_149884_5_)
    {
        int l = p_149884_1_.getBlockMetadata(p_149884_2_, p_149884_3_, p_149884_4_);
        p_149884_1_.setBlockToAir(p_149884_2_, p_149884_3_, p_149884_4_);
        WorldGenBigMushroom worldgenbigmushroom = null;

        if (this == Blocks.brown_mushroom)
        {
            worldgenbigmushroom = new WorldGenBigMushroom(0);
        }
        else if (this == Blocks.red_mushroom)
        {
            worldgenbigmushroom = new WorldGenBigMushroom(1);
        }

        if (worldgenbigmushroom != null && worldgenbigmushroom.generate(p_149884_1_, p_149884_5_, p_149884_2_, p_149884_3_, p_149884_4_))
        {
            return true;
        }
        else
        {
            p_149884_1_.setBlock(p_149884_2_, p_149884_3_, p_149884_4_, this, l, 3);
            return false;
        }
    }

    public boolean func_149851_a(World p_149851_1_, int p_149851_2_, int p_149851_3_, int p_149851_4_, boolean p_149851_5_)
    {
        return true;
    }

    public boolean func_149852_a(World p_149852_1_, Random p_149852_2_, int p_149852_3_, int p_149852_4_, int p_149852_5_)
    {
        return (double)p_149852_2_.nextFloat() < 0.4D;
    }

    public void func_149853_b(World p_149853_1_, Random p_149853_2_, int p_149853_3_, int p_149853_4_, int p_149853_5_)
    {
        this.func_149884_c(p_149853_1_, p_149853_3_, p_149853_4_, p_149853_5_, p_149853_2_);
    }
}