package net.minecraft.block;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.Random;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.item.Item;
import net.minecraft.world.IBlockAccess;
import net.minecraft.world.World;

public class BlockRedstoneRepeater extends BlockRedstoneDiode
{
    /** The offsets for the two torches in redstone repeater blocks. */
    public static final double[] repeaterTorchOffset = new double[] { -0.0625D, 0.0625D, 0.1875D, 0.3125D};
    /** The states in which the redstone repeater blocks can be. */
    private static final int[] repeaterState = new int[] {1, 2, 3, 4};
    private static final String __OBFID = "CL_00000301";

    protected BlockRedstoneRepeater(boolean p_i45424_1_)
    {
        super(p_i45424_1_);
    }

    /**
     * Called upon block activation (right click on the block.)
     */
    public boolean onBlockActivated(World p_149727_1_, int p_149727_2_, int p_149727_3_, int p_149727_4_, EntityPlayer p_149727_5_, int p_149727_6_, float p_149727_7_, float p_149727_8_, float p_149727_9_)
    {
        int i1 = p_149727_1_.getBlockMetadata(p_149727_2_, p_149727_3_, p_149727_4_);
        int j1 = (i1 & 12) >> 2;
        j1 = j1 + 1 << 2 & 12;
        p_149727_1_.setBlockMetadataWithNotify(p_149727_2_, p_149727_3_, p_149727_4_, j1 | i1 & 3, 3);
        return true;
    }

    protected int func_149901_b(int p_149901_1_)
    {
        return repeaterState[(p_149901_1_ & 12) >> 2] * 2;
    }

    protected BlockRedstoneDiode getBlockPowered()
    {
        return Blocks.powered_repeater;
    }

    protected BlockRedstoneDiode getBlockUnpowered()
    {
        return Blocks.unpowered_repeater;
    }

    public Item getItemDropped(int p_149650_1_, Random p_149650_2_, int p_149650_3_)
    {
        return Items.repeater;
    }

    /**
     * Gets an item for the block being called on. Args: world, x, y, z
     */
    @SideOnly(Side.CLIENT)
    public Item getItem(World p_149694_1_, int p_149694_2_, int p_149694_3_, int p_149694_4_)
    {
        return Items.repeater;
    }

    /**
     * The type of render function that is called for this block
     */
    public int getRenderType()
    {
        return 15;
    }

    public boolean func_149910_g(IBlockAccess p_149910_1_, int p_149910_2_, int p_149910_3_, int p_149910_4_, int p_149910_5_)
    {
        return this.func_149902_h(p_149910_1_, p_149910_2_, p_149910_3_, p_149910_4_, p_149910_5_) > 0;
    }

    protected boolean func_149908_a(Block p_149908_1_)
    {
        return isRedstoneRepeaterBlockID(p_149908_1_);
    }

    public void breakBlock(World p_149749_1_, int p_149749_2_, int p_149749_3_, int p_149749_4_, Block p_149749_5_, int p_149749_6_)
    {
        super.breakBlock(p_149749_1_, p_149749_2_, p_149749_3_, p_149749_4_, p_149749_5_, p_149749_6_);
        this.func_149911_e(p_149749_1_, p_149749_2_, p_149749_3_, p_149749_4_);
    }

    /**
     * A randomly called display update to be able to add particles or other items for display
     */
    @SideOnly(Side.CLIENT)
    public void randomDisplayTick(World p_149734_1_, int p_149734_2_, int p_149734_3_, int p_149734_4_, Random p_149734_5_)
    {
        if (this.isRepeaterPowered)
        {
            int l = p_149734_1_.getBlockMetadata(p_149734_2_, p_149734_3_, p_149734_4_);
            int i1 = getDirection(l);
            double d0 = (double)((float)p_149734_2_ + 0.5F) + (double)(p_149734_5_.nextFloat() - 0.5F) * 0.2D;
            double d1 = (double)((float)p_149734_3_ + 0.4F) + (double)(p_149734_5_.nextFloat() - 0.5F) * 0.2D;
            double d2 = (double)((float)p_149734_4_ + 0.5F) + (double)(p_149734_5_.nextFloat() - 0.5F) * 0.2D;
            double d3 = 0.0D;
            double d4 = 0.0D;

            if (p_149734_5_.nextInt(2) == 0)
            {
                switch (i1)
                {
                    case 0:
                        d4 = -0.3125D;
                        break;
                    case 1:
                        d3 = 0.3125D;
                        break;
                    case 2:
                        d4 = 0.3125D;
                        break;
                    case 3:
                        d3 = -0.3125D;
                }
            }
            else
            {
                int j1 = (l & 12) >> 2;

                switch (i1)
                {
                    case 0:
                        d4 = repeaterTorchOffset[j1];
                        break;
                    case 1:
                        d3 = -repeaterTorchOffset[j1];
                        break;
                    case 2:
                        d4 = -repeaterTorchOffset[j1];
                        break;
                    case 3:
                        d3 = repeaterTorchOffset[j1];
                }
            }

            p_149734_1_.spawnParticle("reddust", d0 + d3, d1, d2 + d4, 0.0D, 0.0D, 0.0D);
        }
    }
}