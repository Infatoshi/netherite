package net.minecraft.block;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.block.material.Material;
import net.minecraft.creativetab.CreativeTabs;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.util.AxisAlignedBB;
import net.minecraft.util.MovingObjectPosition;
import net.minecraft.util.Vec3;
import net.minecraft.world.IBlockAccess;
import net.minecraft.world.World;
import net.minecraftforge.common.util.ForgeDirection;

public class BlockTrapDoor extends Block
{
    /** Set this to allow trapdoors to remain free-floating */
    public static boolean disableValidation = false;
    private static final String __OBFID = "CL_00000327";

    protected BlockTrapDoor(Material p_i45434_1_)
    {
        super(p_i45434_1_);
        float f = 0.5F;
        float f1 = 1.0F;
        this.setBlockBounds(0.5F - f, 0.0F, 0.5F - f, 0.5F + f, f1, 0.5F + f);
        this.setCreativeTab(CreativeTabs.tabRedstone);
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
        return !func_150118_d(p_149655_1_.getBlockMetadata(p_149655_2_, p_149655_3_, p_149655_4_));
    }

    /**
     * The type of render function that is called for this block
     */
    public int getRenderType()
    {
        return 0;
    }

    /**
     * Returns the bounding box of the wired rectangular prism to render.
     */
    @SideOnly(Side.CLIENT)
    public AxisAlignedBB getSelectedBoundingBoxFromPool(World p_149633_1_, int p_149633_2_, int p_149633_3_, int p_149633_4_)
    {
        this.setBlockBoundsBasedOnState(p_149633_1_, p_149633_2_, p_149633_3_, p_149633_4_);
        return super.getSelectedBoundingBoxFromPool(p_149633_1_, p_149633_2_, p_149633_3_, p_149633_4_);
    }

    /**
     * Returns a bounding box from the pool of bounding boxes (this means this box can change after the pool has been
     * cleared to be reused)
     */
    public AxisAlignedBB getCollisionBoundingBoxFromPool(World p_149668_1_, int p_149668_2_, int p_149668_3_, int p_149668_4_)
    {
        this.setBlockBoundsBasedOnState(p_149668_1_, p_149668_2_, p_149668_3_, p_149668_4_);
        return super.getCollisionBoundingBoxFromPool(p_149668_1_, p_149668_2_, p_149668_3_, p_149668_4_);
    }

    /**
     * Updates the blocks bounds based on its current state. Args: world, x, y, z
     */
    public void setBlockBoundsBasedOnState(IBlockAccess p_149719_1_, int p_149719_2_, int p_149719_3_, int p_149719_4_)
    {
        this.func_150117_b(p_149719_1_.getBlockMetadata(p_149719_2_, p_149719_3_, p_149719_4_));
    }

    /**
     * Sets the block's bounds for rendering it as an item
     */
    public void setBlockBoundsForItemRender()
    {
        float f = 0.1875F;
        this.setBlockBounds(0.0F, 0.5F - f / 2.0F, 0.0F, 1.0F, 0.5F + f / 2.0F, 1.0F);
    }

    public void func_150117_b(int p_150117_1_)
    {
        float f = 0.1875F;

        if ((p_150117_1_ & 8) != 0)
        {
            this.setBlockBounds(0.0F, 1.0F - f, 0.0F, 1.0F, 1.0F, 1.0F);
        }
        else
        {
            this.setBlockBounds(0.0F, 0.0F, 0.0F, 1.0F, f, 1.0F);
        }

        if (func_150118_d(p_150117_1_))
        {
            if ((p_150117_1_ & 3) == 0)
            {
                this.setBlockBounds(0.0F, 0.0F, 1.0F - f, 1.0F, 1.0F, 1.0F);
            }

            if ((p_150117_1_ & 3) == 1)
            {
                this.setBlockBounds(0.0F, 0.0F, 0.0F, 1.0F, 1.0F, f);
            }

            if ((p_150117_1_ & 3) == 2)
            {
                this.setBlockBounds(1.0F - f, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F);
            }

            if ((p_150117_1_ & 3) == 3)
            {
                this.setBlockBounds(0.0F, 0.0F, 0.0F, f, 1.0F, 1.0F);
            }
        }
    }

    /**
     * Called when a player hits the block. Args: world, x, y, z, player
     */
    public void onBlockClicked(World p_149699_1_, int p_149699_2_, int p_149699_3_, int p_149699_4_, EntityPlayer p_149699_5_) {}

    /**
     * Called upon block activation (right click on the block.)
     */
    public boolean onBlockActivated(World p_149727_1_, int p_149727_2_, int p_149727_3_, int p_149727_4_, EntityPlayer p_149727_5_, int p_149727_6_, float p_149727_7_, float p_149727_8_, float p_149727_9_)
    {
        if (this.blockMaterial == Material.iron)
        {
            return true;
        }
        else
        {
            int i1 = p_149727_1_.getBlockMetadata(p_149727_2_, p_149727_3_, p_149727_4_);
            p_149727_1_.setBlockMetadataWithNotify(p_149727_2_, p_149727_3_, p_149727_4_, i1 ^ 4, 2);
            p_149727_1_.playAuxSFXAtEntity(p_149727_5_, 1003, p_149727_2_, p_149727_3_, p_149727_4_, 0);
            return true;
        }
    }

    public void func_150120_a(World p_150120_1_, int p_150120_2_, int p_150120_3_, int p_150120_4_, boolean p_150120_5_)
    {
        int l = p_150120_1_.getBlockMetadata(p_150120_2_, p_150120_3_, p_150120_4_);
        boolean flag1 = (l & 4) > 0;

        if (flag1 != p_150120_5_)
        {
            p_150120_1_.setBlockMetadataWithNotify(p_150120_2_, p_150120_3_, p_150120_4_, l ^ 4, 2);
            p_150120_1_.playAuxSFXAtEntity((EntityPlayer)null, 1003, p_150120_2_, p_150120_3_, p_150120_4_, 0);
        }
    }

    /**
     * Lets the block know when one of its neighbor changes. Doesn't know which neighbor changed (coordinates passed are
     * their own) Args: x, y, z, neighbor Block
     */
    public void onNeighborBlockChange(World p_149695_1_, int p_149695_2_, int p_149695_3_, int p_149695_4_, Block p_149695_5_)
    {
        if (!p_149695_1_.isRemote)
        {
            int l = p_149695_1_.getBlockMetadata(p_149695_2_, p_149695_3_, p_149695_4_);
            int i1 = p_149695_2_;
            int j1 = p_149695_4_;

            if ((l & 3) == 0)
            {
                j1 = p_149695_4_ + 1;
            }

            if ((l & 3) == 1)
            {
                --j1;
            }

            if ((l & 3) == 2)
            {
                i1 = p_149695_2_ + 1;
            }

            if ((l & 3) == 3)
            {
                --i1;
            }

            if (!(func_150119_a(p_149695_1_.getBlock(i1, p_149695_3_, j1)) || p_149695_1_.isSideSolid(i1, p_149695_3_, j1, ForgeDirection.getOrientation((l & 3) + 2))))
            {
                p_149695_1_.setBlockToAir(p_149695_2_, p_149695_3_, p_149695_4_);
                this.dropBlockAsItem(p_149695_1_, p_149695_2_, p_149695_3_, p_149695_4_, l, 0);
            }

            boolean flag = p_149695_1_.isBlockIndirectlyGettingPowered(p_149695_2_, p_149695_3_, p_149695_4_);

            if (flag || p_149695_5_.canProvidePower())
            {
                this.func_150120_a(p_149695_1_, p_149695_2_, p_149695_3_, p_149695_4_, flag);
            }
        }
    }

    /**
     * Ray traces through the blocks collision from start vector to end vector returning a ray trace hit. Args: world,
     * x, y, z, startVec, endVec
     */
    public MovingObjectPosition collisionRayTrace(World p_149731_1_, int p_149731_2_, int p_149731_3_, int p_149731_4_, Vec3 p_149731_5_, Vec3 p_149731_6_)
    {
        this.setBlockBoundsBasedOnState(p_149731_1_, p_149731_2_, p_149731_3_, p_149731_4_);
        return super.collisionRayTrace(p_149731_1_, p_149731_2_, p_149731_3_, p_149731_4_, p_149731_5_, p_149731_6_);
    }

    /**
     * Called when a block is placed using its ItemBlock. Args: World, X, Y, Z, side, hitX, hitY, hitZ, block metadata
     */
    public int onBlockPlaced(World p_149660_1_, int p_149660_2_, int p_149660_3_, int p_149660_4_, int p_149660_5_, float p_149660_6_, float p_149660_7_, float p_149660_8_, int p_149660_9_)
    {
        int j1 = 0;

        if (p_149660_5_ == 2)
        {
            j1 = 0;
        }

        if (p_149660_5_ == 3)
        {
            j1 = 1;
        }

        if (p_149660_5_ == 4)
        {
            j1 = 2;
        }

        if (p_149660_5_ == 5)
        {
            j1 = 3;
        }

        if (p_149660_5_ != 1 && p_149660_5_ != 0 && p_149660_7_ > 0.5F)
        {
            j1 |= 8;
        }

        return j1;
    }

    /**
     * checks to see if you can place this block can be placed on that side of a block: BlockLever overrides
     */
    public boolean canPlaceBlockOnSide(World p_149707_1_, int p_149707_2_, int p_149707_3_, int p_149707_4_, int p_149707_5_)
    {
        if (disableValidation) return true;
        if (p_149707_5_ == 0)
        {
            return false;
        }
        else if (p_149707_5_ == 1)
        {
            return false;
        }
        else
        {
            if (p_149707_5_ == 2)
            {
                ++p_149707_4_;
            }

            if (p_149707_5_ == 3)
            {
                --p_149707_4_;
            }

            if (p_149707_5_ == 4)
            {
                ++p_149707_2_;
            }

            if (p_149707_5_ == 5)
            {
                --p_149707_2_;
            }

            return func_150119_a(p_149707_1_.getBlock(p_149707_2_, p_149707_3_, p_149707_4_)) || p_149707_1_.isSideSolid(p_149707_2_, p_149707_3_, p_149707_4_, ForgeDirection.UP);
        }
    }

    public static boolean func_150118_d(int p_150118_0_)
    {
        return (p_150118_0_ & 4) != 0;
    }

    private static boolean func_150119_a(Block p_150119_0_)
    {
        if (disableValidation) return true;
        return p_150119_0_.blockMaterial.isOpaque() && p_150119_0_.renderAsNormalBlock() || p_150119_0_ == Blocks.glowstone || p_150119_0_ instanceof BlockSlab || p_150119_0_ instanceof BlockStairs;
    }
}