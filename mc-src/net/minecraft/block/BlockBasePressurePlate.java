package net.minecraft.block;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.Random;
import net.minecraft.block.material.Material;
import net.minecraft.client.renderer.texture.IIconRegister;
import net.minecraft.creativetab.CreativeTabs;
import net.minecraft.entity.Entity;
import net.minecraft.util.AxisAlignedBB;
import net.minecraft.world.IBlockAccess;
import net.minecraft.world.World;

public abstract class BlockBasePressurePlate extends Block
{
    private String field_150067_a;
    private static final String __OBFID = "CL_00000194";

    protected BlockBasePressurePlate(String p_i45387_1_, Material p_i45387_2_)
    {
        super(p_i45387_2_);
        this.field_150067_a = p_i45387_1_;
        this.setCreativeTab(CreativeTabs.tabRedstone);
        this.setTickRandomly(true);
        this.func_150063_b(this.func_150066_d(15));
    }

    /**
     * Updates the blocks bounds based on its current state. Args: world, x, y, z
     */
    public void setBlockBoundsBasedOnState(IBlockAccess p_149719_1_, int p_149719_2_, int p_149719_3_, int p_149719_4_)
    {
        this.func_150063_b(p_149719_1_.getBlockMetadata(p_149719_2_, p_149719_3_, p_149719_4_));
    }

    protected void func_150063_b(int p_150063_1_)
    {
        boolean flag = this.func_150060_c(p_150063_1_) > 0;
        float f = 0.0625F;

        if (flag)
        {
            this.setBlockBounds(f, 0.0F, f, 1.0F - f, 0.03125F, 1.0F - f);
        }
        else
        {
            this.setBlockBounds(f, 0.0F, f, 1.0F - f, 0.0625F, 1.0F - f);
        }
    }

    /**
     * How many world ticks before ticking
     */
    public int tickRate(World p_149738_1_)
    {
        return 20;
    }

    /**
     * Returns a bounding box from the pool of bounding boxes (this means this box can change after the pool has been
     * cleared to be reused)
     */
    public AxisAlignedBB getCollisionBoundingBoxFromPool(World p_149668_1_, int p_149668_2_, int p_149668_3_, int p_149668_4_)
    {
        return null;
    }

    /**
     * Is this block (a) opaque and (b) a full 1m cube?  This determines whether or not to render the shared face of two
     * adjacent blocks and also whether the player can attach torches, redstone wire, etc to this block.
     */
    public boolean isOpaqueCube()
    {
        return false;
    }

    /**
     * If this block doesn't render as an ordinary block it will return False (examples: signs, buttons, stairs, etc)
     */
    public boolean renderAsNormalBlock()
    {
        return false;
    }

    public boolean getBlocksMovement(IBlockAccess p_149655_1_, int p_149655_2_, int p_149655_3_, int p_149655_4_)
    {
        return true;
    }

    /**
     * Checks to see if its valid to put this block at the specified coordinates. Args: world, x, y, z
     */
    public boolean canPlaceBlockAt(World p_149742_1_, int p_149742_2_, int p_149742_3_, int p_149742_4_)
    {
        return World.doesBlockHaveSolidTopSurface(p_149742_1_, p_149742_2_, p_149742_3_ - 1, p_149742_4_) || BlockFence.func_149825_a(p_149742_1_.getBlock(p_149742_2_, p_149742_3_ - 1, p_149742_4_));
    }

    /**
     * Lets the block know when one of its neighbor changes. Doesn't know which neighbor changed (coordinates passed are
     * their own) Args: x, y, z, neighbor Block
     */
    public void onNeighborBlockChange(World p_149695_1_, int p_149695_2_, int p_149695_3_, int p_149695_4_, Block p_149695_5_)
    {
        boolean flag = false;

        if (!World.doesBlockHaveSolidTopSurface(p_149695_1_, p_149695_2_, p_149695_3_ - 1, p_149695_4_) && !BlockFence.func_149825_a(p_149695_1_.getBlock(p_149695_2_, p_149695_3_ - 1, p_149695_4_)))
        {
            flag = true;
        }

        if (flag)
        {
            this.dropBlockAsItem(p_149695_1_, p_149695_2_, p_149695_3_, p_149695_4_, p_149695_1_.getBlockMetadata(p_149695_2_, p_149695_3_, p_149695_4_), 0);
            p_149695_1_.setBlockToAir(p_149695_2_, p_149695_3_, p_149695_4_);
        }
    }

    /**
     * Ticks the block if it's been scheduled
     */
    public void updateTick(World p_149674_1_, int p_149674_2_, int p_149674_3_, int p_149674_4_, Random p_149674_5_)
    {
        if (!p_149674_1_.isRemote)
        {
            int l = this.func_150060_c(p_149674_1_.getBlockMetadata(p_149674_2_, p_149674_3_, p_149674_4_));

            if (l > 0)
            {
                this.func_150062_a(p_149674_1_, p_149674_2_, p_149674_3_, p_149674_4_, l);
            }
        }
    }

    /**
     * Triggered whenever an entity collides with this block (enters into the block). Args: world, x, y, z, entity
     */
    public void onEntityCollidedWithBlock(World p_149670_1_, int p_149670_2_, int p_149670_3_, int p_149670_4_, Entity p_149670_5_)
    {
        if (!p_149670_1_.isRemote)
        {
            int l = this.func_150060_c(p_149670_1_.getBlockMetadata(p_149670_2_, p_149670_3_, p_149670_4_));

            if (l == 0)
            {
                this.func_150062_a(p_149670_1_, p_149670_2_, p_149670_3_, p_149670_4_, l);
            }
        }
    }

    protected void func_150062_a(World p_150062_1_, int p_150062_2_, int p_150062_3_, int p_150062_4_, int p_150062_5_)
    {
        int i1 = this.func_150065_e(p_150062_1_, p_150062_2_, p_150062_3_, p_150062_4_);
        boolean flag = p_150062_5_ > 0;
        boolean flag1 = i1 > 0;

        if (p_150062_5_ != i1)
        {
            p_150062_1_.setBlockMetadataWithNotify(p_150062_2_, p_150062_3_, p_150062_4_, this.func_150066_d(i1), 2);
            this.func_150064_a_(p_150062_1_, p_150062_2_, p_150062_3_, p_150062_4_);
            p_150062_1_.markBlockRangeForRenderUpdate(p_150062_2_, p_150062_3_, p_150062_4_, p_150062_2_, p_150062_3_, p_150062_4_);
        }

        if (!flag1 && flag)
        {
            p_150062_1_.playSoundEffect((double)p_150062_2_ + 0.5D, (double)p_150062_3_ + 0.1D, (double)p_150062_4_ + 0.5D, "random.click", 0.3F, 0.5F);
        }
        else if (flag1 && !flag)
        {
            p_150062_1_.playSoundEffect((double)p_150062_2_ + 0.5D, (double)p_150062_3_ + 0.1D, (double)p_150062_4_ + 0.5D, "random.click", 0.3F, 0.6F);
        }

        if (flag1)
        {
            p_150062_1_.scheduleBlockUpdate(p_150062_2_, p_150062_3_, p_150062_4_, this, this.tickRate(p_150062_1_));
        }
    }

    protected AxisAlignedBB func_150061_a(int p_150061_1_, int p_150061_2_, int p_150061_3_)
    {
        float f = 0.125F;
        return AxisAlignedBB.getBoundingBox((double)((float)p_150061_1_ + f), (double)p_150061_2_, (double)((float)p_150061_3_ + f), (double)((float)(p_150061_1_ + 1) - f), (double)p_150061_2_ + 0.25D, (double)((float)(p_150061_3_ + 1) - f));
    }

    public void breakBlock(World p_149749_1_, int p_149749_2_, int p_149749_3_, int p_149749_4_, Block p_149749_5_, int p_149749_6_)
    {
        if (this.func_150060_c(p_149749_6_) > 0)
        {
            this.func_150064_a_(p_149749_1_, p_149749_2_, p_149749_3_, p_149749_4_);
        }

        super.breakBlock(p_149749_1_, p_149749_2_, p_149749_3_, p_149749_4_, p_149749_5_, p_149749_6_);
    }

    protected void func_150064_a_(World p_150064_1_, int p_150064_2_, int p_150064_3_, int p_150064_4_)
    {
        p_150064_1_.notifyBlocksOfNeighborChange(p_150064_2_, p_150064_3_, p_150064_4_, this);
        p_150064_1_.notifyBlocksOfNeighborChange(p_150064_2_, p_150064_3_ - 1, p_150064_4_, this);
    }

    public int isProvidingWeakPower(IBlockAccess p_149709_1_, int p_149709_2_, int p_149709_3_, int p_149709_4_, int p_149709_5_)
    {
        return this.func_150060_c(p_149709_1_.getBlockMetadata(p_149709_2_, p_149709_3_, p_149709_4_));
    }

    public int isProvidingStrongPower(IBlockAccess p_149748_1_, int p_149748_2_, int p_149748_3_, int p_149748_4_, int p_149748_5_)
    {
        return p_149748_5_ == 1 ? this.func_150060_c(p_149748_1_.getBlockMetadata(p_149748_2_, p_149748_3_, p_149748_4_)) : 0;
    }

    /**
     * Can this block provide power. Only wire currently seems to have this change based on its state.
     */
    public boolean canProvidePower()
    {
        return true;
    }

    /**
     * Sets the block's bounds for rendering it as an item
     */
    public void setBlockBoundsForItemRender()
    {
        float f = 0.5F;
        float f1 = 0.125F;
        float f2 = 0.5F;
        this.setBlockBounds(0.5F - f, 0.5F - f1, 0.5F - f2, 0.5F + f, 0.5F + f1, 0.5F + f2);
    }

    /**
     * Returns the mobility information of the block, 0 = free, 1 = can't push but can move over, 2 = total immobility
     * and stop pistons
     */
    public int getMobilityFlag()
    {
        return 1;
    }

    protected abstract int func_150065_e(World p_150065_1_, int p_150065_2_, int p_150065_3_, int p_150065_4_);

    protected abstract int func_150060_c(int p_150060_1_);

    protected abstract int func_150066_d(int p_150066_1_);

    @SideOnly(Side.CLIENT)
    public void registerBlockIcons(IIconRegister p_149651_1_)
    {
        this.blockIcon = p_149651_1_.registerIcon(this.field_150067_a);
    }
}