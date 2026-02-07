package net.minecraft.block;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import net.minecraft.creativetab.CreativeTabs;
import net.minecraft.init.Blocks;
import net.minecraft.item.Item;
import net.minecraft.world.IBlockAccess;
import net.minecraft.world.World;

public class BlockRedstoneTorch extends BlockTorch
{
    private boolean field_150113_a;
    private static Map field_150112_b = new HashMap();
    private static final String __OBFID = "CL_00000298";

    private boolean func_150111_a(World p_150111_1_, int p_150111_2_, int p_150111_3_, int p_150111_4_, boolean p_150111_5_)
    {
        if (!field_150112_b.containsKey(p_150111_1_))
        {
            field_150112_b.put(p_150111_1_, new ArrayList());
        }

        List list = (List)field_150112_b.get(p_150111_1_);

        if (p_150111_5_)
        {
            list.add(new BlockRedstoneTorch.Toggle(p_150111_2_, p_150111_3_, p_150111_4_, p_150111_1_.getTotalWorldTime()));
        }

        int l = 0;

        for (int i1 = 0; i1 < list.size(); ++i1)
        {
            BlockRedstoneTorch.Toggle toggle = (BlockRedstoneTorch.Toggle)list.get(i1);

            if (toggle.field_150847_a == p_150111_2_ && toggle.field_150845_b == p_150111_3_ && toggle.field_150846_c == p_150111_4_)
            {
                ++l;

                if (l >= 8)
                {
                    return true;
                }
            }
        }

        return false;
    }

    protected BlockRedstoneTorch(boolean p_i45423_1_)
    {
        this.field_150113_a = p_i45423_1_;
        this.setTickRandomly(true);
        this.setCreativeTab((CreativeTabs)null);
    }

    /**
     * How many world ticks before ticking
     */
    public int tickRate(World p_149738_1_)
    {
        return 2;
    }

    /**
     * Called whenever the block is added into the world. Args: world, x, y, z
     */
    public void onBlockAdded(World p_149726_1_, int p_149726_2_, int p_149726_3_, int p_149726_4_)
    {
        if (p_149726_1_.getBlockMetadata(p_149726_2_, p_149726_3_, p_149726_4_) == 0)
        {
            super.onBlockAdded(p_149726_1_, p_149726_2_, p_149726_3_, p_149726_4_);
        }

        if (this.field_150113_a)
        {
            p_149726_1_.notifyBlocksOfNeighborChange(p_149726_2_, p_149726_3_ - 1, p_149726_4_, this);
            p_149726_1_.notifyBlocksOfNeighborChange(p_149726_2_, p_149726_3_ + 1, p_149726_4_, this);
            p_149726_1_.notifyBlocksOfNeighborChange(p_149726_2_ - 1, p_149726_3_, p_149726_4_, this);
            p_149726_1_.notifyBlocksOfNeighborChange(p_149726_2_ + 1, p_149726_3_, p_149726_4_, this);
            p_149726_1_.notifyBlocksOfNeighborChange(p_149726_2_, p_149726_3_, p_149726_4_ - 1, this);
            p_149726_1_.notifyBlocksOfNeighborChange(p_149726_2_, p_149726_3_, p_149726_4_ + 1, this);
        }
    }

    public void breakBlock(World p_149749_1_, int p_149749_2_, int p_149749_3_, int p_149749_4_, Block p_149749_5_, int p_149749_6_)
    {
        if (this.field_150113_a)
        {
            p_149749_1_.notifyBlocksOfNeighborChange(p_149749_2_, p_149749_3_ - 1, p_149749_4_, this);
            p_149749_1_.notifyBlocksOfNeighborChange(p_149749_2_, p_149749_3_ + 1, p_149749_4_, this);
            p_149749_1_.notifyBlocksOfNeighborChange(p_149749_2_ - 1, p_149749_3_, p_149749_4_, this);
            p_149749_1_.notifyBlocksOfNeighborChange(p_149749_2_ + 1, p_149749_3_, p_149749_4_, this);
            p_149749_1_.notifyBlocksOfNeighborChange(p_149749_2_, p_149749_3_, p_149749_4_ - 1, this);
            p_149749_1_.notifyBlocksOfNeighborChange(p_149749_2_, p_149749_3_, p_149749_4_ + 1, this);
        }
    }

    public int isProvidingWeakPower(IBlockAccess p_149709_1_, int p_149709_2_, int p_149709_3_, int p_149709_4_, int p_149709_5_)
    {
        if (!this.field_150113_a)
        {
            return 0;
        }
        else
        {
            int i1 = p_149709_1_.getBlockMetadata(p_149709_2_, p_149709_3_, p_149709_4_);
            return i1 == 5 && p_149709_5_ == 1 ? 0 : (i1 == 3 && p_149709_5_ == 3 ? 0 : (i1 == 4 && p_149709_5_ == 2 ? 0 : (i1 == 1 && p_149709_5_ == 5 ? 0 : (i1 == 2 && p_149709_5_ == 4 ? 0 : 15))));
        }
    }

    private boolean func_150110_m(World p_150110_1_, int p_150110_2_, int p_150110_3_, int p_150110_4_)
    {
        int l = p_150110_1_.getBlockMetadata(p_150110_2_, p_150110_3_, p_150110_4_);
        return l == 5 && p_150110_1_.getIndirectPowerOutput(p_150110_2_, p_150110_3_ - 1, p_150110_4_, 0) ? true : (l == 3 && p_150110_1_.getIndirectPowerOutput(p_150110_2_, p_150110_3_, p_150110_4_ - 1, 2) ? true : (l == 4 && p_150110_1_.getIndirectPowerOutput(p_150110_2_, p_150110_3_, p_150110_4_ + 1, 3) ? true : (l == 1 && p_150110_1_.getIndirectPowerOutput(p_150110_2_ - 1, p_150110_3_, p_150110_4_, 4) ? true : l == 2 && p_150110_1_.getIndirectPowerOutput(p_150110_2_ + 1, p_150110_3_, p_150110_4_, 5))));
    }

    /**
     * Ticks the block if it's been scheduled
     */
    public void updateTick(World p_149674_1_, int p_149674_2_, int p_149674_3_, int p_149674_4_, Random p_149674_5_)
    {
        boolean flag = this.func_150110_m(p_149674_1_, p_149674_2_, p_149674_3_, p_149674_4_);
        List list = (List)field_150112_b.get(p_149674_1_);

        while (list != null && !list.isEmpty() && p_149674_1_.getTotalWorldTime() - ((BlockRedstoneTorch.Toggle)list.get(0)).field_150844_d > 60L)
        {
            list.remove(0);
        }

        if (this.field_150113_a)
        {
            if (flag)
            {
                p_149674_1_.setBlock(p_149674_2_, p_149674_3_, p_149674_4_, Blocks.unlit_redstone_torch, p_149674_1_.getBlockMetadata(p_149674_2_, p_149674_3_, p_149674_4_), 3);

                if (this.func_150111_a(p_149674_1_, p_149674_2_, p_149674_3_, p_149674_4_, true))
                {
                    p_149674_1_.playSoundEffect((double)((float)p_149674_2_ + 0.5F), (double)((float)p_149674_3_ + 0.5F), (double)((float)p_149674_4_ + 0.5F), "random.fizz", 0.5F, 2.6F + (p_149674_1_.rand.nextFloat() - p_149674_1_.rand.nextFloat()) * 0.8F);

                    for (int l = 0; l < 5; ++l)
                    {
                        double d0 = (double)p_149674_2_ + p_149674_5_.nextDouble() * 0.6D + 0.2D;
                        double d1 = (double)p_149674_3_ + p_149674_5_.nextDouble() * 0.6D + 0.2D;
                        double d2 = (double)p_149674_4_ + p_149674_5_.nextDouble() * 0.6D + 0.2D;
                        p_149674_1_.spawnParticle("smoke", d0, d1, d2, 0.0D, 0.0D, 0.0D);
                    }
                }
            }
        }
        else if (!flag && !this.func_150111_a(p_149674_1_, p_149674_2_, p_149674_3_, p_149674_4_, false))
        {
            p_149674_1_.setBlock(p_149674_2_, p_149674_3_, p_149674_4_, Blocks.redstone_torch, p_149674_1_.getBlockMetadata(p_149674_2_, p_149674_3_, p_149674_4_), 3);
        }
    }

    /**
     * Lets the block know when one of its neighbor changes. Doesn't know which neighbor changed (coordinates passed are
     * their own) Args: x, y, z, neighbor Block
     */
    public void onNeighborBlockChange(World p_149695_1_, int p_149695_2_, int p_149695_3_, int p_149695_4_, Block p_149695_5_)
    {
        if (!this.func_150108_b(p_149695_1_, p_149695_2_, p_149695_3_, p_149695_4_, p_149695_5_))
        {
            boolean flag = this.func_150110_m(p_149695_1_, p_149695_2_, p_149695_3_, p_149695_4_);

            if (this.field_150113_a && flag || !this.field_150113_a && !flag)
            {
                p_149695_1_.scheduleBlockUpdate(p_149695_2_, p_149695_3_, p_149695_4_, this, this.tickRate(p_149695_1_));
            }
        }
    }

    public int isProvidingStrongPower(IBlockAccess p_149748_1_, int p_149748_2_, int p_149748_3_, int p_149748_4_, int p_149748_5_)
    {
        return p_149748_5_ == 0 ? this.isProvidingWeakPower(p_149748_1_, p_149748_2_, p_149748_3_, p_149748_4_, p_149748_5_) : 0;
    }

    public Item getItemDropped(int p_149650_1_, Random p_149650_2_, int p_149650_3_)
    {
        return Item.getItemFromBlock(Blocks.redstone_torch);
    }

    /**
     * Can this block provide power. Only wire currently seems to have this change based on its state.
     */
    public boolean canProvidePower()
    {
        return true;
    }

    /**
     * A randomly called display update to be able to add particles or other items for display
     */
    @SideOnly(Side.CLIENT)
    public void randomDisplayTick(World p_149734_1_, int p_149734_2_, int p_149734_3_, int p_149734_4_, Random p_149734_5_)
    {
        if (this.field_150113_a)
        {
            int l = p_149734_1_.getBlockMetadata(p_149734_2_, p_149734_3_, p_149734_4_);
            double d0 = (double)((float)p_149734_2_ + 0.5F) + (double)(p_149734_5_.nextFloat() - 0.5F) * 0.2D;
            double d1 = (double)((float)p_149734_3_ + 0.7F) + (double)(p_149734_5_.nextFloat() - 0.5F) * 0.2D;
            double d2 = (double)((float)p_149734_4_ + 0.5F) + (double)(p_149734_5_.nextFloat() - 0.5F) * 0.2D;
            double d3 = 0.2199999988079071D;
            double d4 = 0.27000001072883606D;

            if (l == 1)
            {
                p_149734_1_.spawnParticle("reddust", d0 - d4, d1 + d3, d2, 0.0D, 0.0D, 0.0D);
            }
            else if (l == 2)
            {
                p_149734_1_.spawnParticle("reddust", d0 + d4, d1 + d3, d2, 0.0D, 0.0D, 0.0D);
            }
            else if (l == 3)
            {
                p_149734_1_.spawnParticle("reddust", d0, d1 + d3, d2 - d4, 0.0D, 0.0D, 0.0D);
            }
            else if (l == 4)
            {
                p_149734_1_.spawnParticle("reddust", d0, d1 + d3, d2 + d4, 0.0D, 0.0D, 0.0D);
            }
            else
            {
                p_149734_1_.spawnParticle("reddust", d0, d1, d2, 0.0D, 0.0D, 0.0D);
            }
        }
    }

    /**
     * Gets an item for the block being called on. Args: world, x, y, z
     */
    @SideOnly(Side.CLIENT)
    public Item getItem(World p_149694_1_, int p_149694_2_, int p_149694_3_, int p_149694_4_)
    {
        return Item.getItemFromBlock(Blocks.redstone_torch);
    }

    public boolean isAssociatedBlock(Block p_149667_1_)
    {
        return p_149667_1_ == Blocks.unlit_redstone_torch || p_149667_1_ == Blocks.redstone_torch;
    }

    static class Toggle
        {
            int field_150847_a;
            int field_150845_b;
            int field_150846_c;
            long field_150844_d;
            private static final String __OBFID = "CL_00000299";

            public Toggle(int p_i45422_1_, int p_i45422_2_, int p_i45422_3_, long p_i45422_4_)
            {
                this.field_150847_a = p_i45422_1_;
                this.field_150845_b = p_i45422_2_;
                this.field_150846_c = p_i45422_3_;
                this.field_150844_d = p_i45422_4_;
            }
        }
}