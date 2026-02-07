package net.minecraft.block;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.ArrayList;
import java.util.Random;
import net.minecraft.block.material.Material;
import net.minecraft.creativetab.CreativeTabs;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.stats.StatList;
import net.minecraft.util.AxisAlignedBB;
import net.minecraft.util.Direction;
import net.minecraft.world.ColorizerFoliage;
import net.minecraft.world.IBlockAccess;
import net.minecraft.world.World;
import net.minecraftforge.common.ForgeHooks;
import net.minecraftforge.common.IShearable;

public class BlockVine extends Block implements IShearable
{
    private static final String __OBFID = "CL_00000330";

    public BlockVine()
    {
        super(Material.vine);
        this.setTickRandomly(true);
        this.setCreativeTab(CreativeTabs.tabDecorations);
    }

    /**
     * Sets the block's bounds for rendering it as an item
     */
    public void setBlockBoundsForItemRender()
    {
        this.setBlockBounds(0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F);
    }

    /**
     * The type of render function that is called for this block
     */
    public int getRenderType()
    {
        return 20;
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

    /**
     * Updates the blocks bounds based on its current state. Args: world, x, y, z
     */
    public void setBlockBoundsBasedOnState(IBlockAccess p_149719_1_, int p_149719_2_, int p_149719_3_, int p_149719_4_)
    {
        float f = 0.0625F;
        int l = p_149719_1_.getBlockMetadata(p_149719_2_, p_149719_3_, p_149719_4_);
        float f1 = 1.0F;
        float f2 = 1.0F;
        float f3 = 1.0F;
        float f4 = 0.0F;
        float f5 = 0.0F;
        float f6 = 0.0F;
        boolean flag = l > 0;

        if ((l & 2) != 0)
        {
            f4 = Math.max(f4, 0.0625F);
            f1 = 0.0F;
            f2 = 0.0F;
            f5 = 1.0F;
            f3 = 0.0F;
            f6 = 1.0F;
            flag = true;
        }

        if ((l & 8) != 0)
        {
            f1 = Math.min(f1, 0.9375F);
            f4 = 1.0F;
            f2 = 0.0F;
            f5 = 1.0F;
            f3 = 0.0F;
            f6 = 1.0F;
            flag = true;
        }

        if ((l & 4) != 0)
        {
            f6 = Math.max(f6, 0.0625F);
            f3 = 0.0F;
            f1 = 0.0F;
            f4 = 1.0F;
            f2 = 0.0F;
            f5 = 1.0F;
            flag = true;
        }

        if ((l & 1) != 0)
        {
            f3 = Math.min(f3, 0.9375F);
            f6 = 1.0F;
            f1 = 0.0F;
            f4 = 1.0F;
            f2 = 0.0F;
            f5 = 1.0F;
            flag = true;
        }

        if (!flag && this.func_150093_a(p_149719_1_.getBlock(p_149719_2_, p_149719_3_ + 1, p_149719_4_)))
        {
            f2 = Math.min(f2, 0.9375F);
            f5 = 1.0F;
            f1 = 0.0F;
            f4 = 1.0F;
            f3 = 0.0F;
            f6 = 1.0F;
        }

        this.setBlockBounds(f1, f2, f3, f4, f5, f6);
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
     * checks to see if you can place this block can be placed on that side of a block: BlockLever overrides
     */
    public boolean canPlaceBlockOnSide(World p_149707_1_, int p_149707_2_, int p_149707_3_, int p_149707_4_, int p_149707_5_)
    {
        switch (p_149707_5_)
        {
            case 1:
                return this.func_150093_a(p_149707_1_.getBlock(p_149707_2_, p_149707_3_ + 1, p_149707_4_));
            case 2:
                return this.func_150093_a(p_149707_1_.getBlock(p_149707_2_, p_149707_3_, p_149707_4_ + 1));
            case 3:
                return this.func_150093_a(p_149707_1_.getBlock(p_149707_2_, p_149707_3_, p_149707_4_ - 1));
            case 4:
                return this.func_150093_a(p_149707_1_.getBlock(p_149707_2_ + 1, p_149707_3_, p_149707_4_));
            case 5:
                return this.func_150093_a(p_149707_1_.getBlock(p_149707_2_ - 1, p_149707_3_, p_149707_4_));
            default:
                return false;
        }
    }

    private boolean func_150093_a(Block p_150093_1_)
    {
        return p_150093_1_.renderAsNormalBlock() && p_150093_1_.blockMaterial.blocksMovement();
    }

    private boolean func_150094_e(World p_150094_1_, int p_150094_2_, int p_150094_3_, int p_150094_4_)
    {
        int l = p_150094_1_.getBlockMetadata(p_150094_2_, p_150094_3_, p_150094_4_);
        int i1 = l;

        if (l > 0)
        {
            for (int j1 = 0; j1 <= 3; ++j1)
            {
                int k1 = 1 << j1;

                if ((l & k1) != 0 && !this.func_150093_a(p_150094_1_.getBlock(p_150094_2_ + Direction.offsetX[j1], p_150094_3_, p_150094_4_ + Direction.offsetZ[j1])) && (p_150094_1_.getBlock(p_150094_2_, p_150094_3_ + 1, p_150094_4_) != this || (p_150094_1_.getBlockMetadata(p_150094_2_, p_150094_3_ + 1, p_150094_4_) & k1) == 0))
                {
                    i1 &= ~k1;
                }
            }
        }

        if (i1 == 0 && !this.func_150093_a(p_150094_1_.getBlock(p_150094_2_, p_150094_3_ + 1, p_150094_4_)))
        {
            return false;
        }
        else
        {
            if (i1 != l)
            {
                p_150094_1_.setBlockMetadataWithNotify(p_150094_2_, p_150094_3_, p_150094_4_, i1, 2);
            }

            return true;
        }
    }

    @SideOnly(Side.CLIENT)
    public int getBlockColor()
    {
        return ColorizerFoliage.getFoliageColorBasic();
    }

    /**
     * Returns the color this block should be rendered. Used by leaves.
     */
    @SideOnly(Side.CLIENT)
    public int getRenderColor(int p_149741_1_)
    {
        return ColorizerFoliage.getFoliageColorBasic();
    }

    /**
     * Returns a integer with hex for 0xrrggbb with this color multiplied against the blocks color. Note only called
     * when first determining what to render.
     */
    @SideOnly(Side.CLIENT)
    public int colorMultiplier(IBlockAccess p_149720_1_, int p_149720_2_, int p_149720_3_, int p_149720_4_)
    {
        return p_149720_1_.getBiomeGenForCoords(p_149720_2_, p_149720_4_).getBiomeFoliageColor(p_149720_2_, p_149720_3_, p_149720_4_);
    }

    /**
     * Lets the block know when one of its neighbor changes. Doesn't know which neighbor changed (coordinates passed are
     * their own) Args: x, y, z, neighbor Block
     */
    public void onNeighborBlockChange(World p_149695_1_, int p_149695_2_, int p_149695_3_, int p_149695_4_, Block p_149695_5_)
    {
        if (!p_149695_1_.isRemote && !this.func_150094_e(p_149695_1_, p_149695_2_, p_149695_3_, p_149695_4_))
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
        if (!p_149674_1_.isRemote && p_149674_1_.rand.nextInt(4) == 0)
        {
            byte b0 = 4;
            int l = 5;
            boolean flag = false;
            int i1;
            int j1;
            int k1;
            label134:

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
                                flag = true;
                                break label134;
                            }
                        }
                    }
                }
            }

            i1 = p_149674_1_.getBlockMetadata(p_149674_2_, p_149674_3_, p_149674_4_);
            j1 = p_149674_1_.rand.nextInt(6);
            k1 = Direction.facingToDirection[j1];
            int l1;

            if (j1 == 1 && p_149674_3_ < 255 && p_149674_1_.isAirBlock(p_149674_2_, p_149674_3_ + 1, p_149674_4_))
            {
                if (flag)
                {
                    return;
                }

                int j2 = p_149674_1_.rand.nextInt(16) & i1;

                if (j2 > 0)
                {
                    for (l1 = 0; l1 <= 3; ++l1)
                    {
                        if (!this.func_150093_a(p_149674_1_.getBlock(p_149674_2_ + Direction.offsetX[l1], p_149674_3_ + 1, p_149674_4_ + Direction.offsetZ[l1])))
                        {
                            j2 &= ~(1 << l1);
                        }
                    }

                    if (j2 > 0)
                    {
                        p_149674_1_.setBlock(p_149674_2_, p_149674_3_ + 1, p_149674_4_, this, j2, 2);
                    }
                }
            }
            else
            {
                Block block;
                int i2;

                if (j1 >= 2 && j1 <= 5 && (i1 & 1 << k1) == 0)
                {
                    if (flag)
                    {
                        return;
                    }

                    block = p_149674_1_.getBlock(p_149674_2_ + Direction.offsetX[k1], p_149674_3_, p_149674_4_ + Direction.offsetZ[k1]);

                    if (block.blockMaterial == Material.air)
                    {
                        l1 = k1 + 1 & 3;
                        i2 = k1 + 3 & 3;

                        if ((i1 & 1 << l1) != 0 && this.func_150093_a(p_149674_1_.getBlock(p_149674_2_ + Direction.offsetX[k1] + Direction.offsetX[l1], p_149674_3_, p_149674_4_ + Direction.offsetZ[k1] + Direction.offsetZ[l1])))
                        {
                            p_149674_1_.setBlock(p_149674_2_ + Direction.offsetX[k1], p_149674_3_, p_149674_4_ + Direction.offsetZ[k1], this, 1 << l1, 2);
                        }
                        else if ((i1 & 1 << i2) != 0 && this.func_150093_a(p_149674_1_.getBlock(p_149674_2_ + Direction.offsetX[k1] + Direction.offsetX[i2], p_149674_3_, p_149674_4_ + Direction.offsetZ[k1] + Direction.offsetZ[i2])))
                        {
                            p_149674_1_.setBlock(p_149674_2_ + Direction.offsetX[k1], p_149674_3_, p_149674_4_ + Direction.offsetZ[k1], this, 1 << i2, 2);
                        }
                        else if ((i1 & 1 << l1) != 0 && p_149674_1_.isAirBlock(p_149674_2_ + Direction.offsetX[k1] + Direction.offsetX[l1], p_149674_3_, p_149674_4_ + Direction.offsetZ[k1] + Direction.offsetZ[l1]) && this.func_150093_a(p_149674_1_.getBlock(p_149674_2_ + Direction.offsetX[l1], p_149674_3_, p_149674_4_ + Direction.offsetZ[l1])))
                        {
                            p_149674_1_.setBlock(p_149674_2_ + Direction.offsetX[k1] + Direction.offsetX[l1], p_149674_3_, p_149674_4_ + Direction.offsetZ[k1] + Direction.offsetZ[l1], this, 1 << (k1 + 2 & 3), 2);
                        }
                        else if ((i1 & 1 << i2) != 0 && p_149674_1_.isAirBlock(p_149674_2_ + Direction.offsetX[k1] + Direction.offsetX[i2], p_149674_3_, p_149674_4_ + Direction.offsetZ[k1] + Direction.offsetZ[i2]) && this.func_150093_a(p_149674_1_.getBlock(p_149674_2_ + Direction.offsetX[i2], p_149674_3_, p_149674_4_ + Direction.offsetZ[i2])))
                        {
                            p_149674_1_.setBlock(p_149674_2_ + Direction.offsetX[k1] + Direction.offsetX[i2], p_149674_3_, p_149674_4_ + Direction.offsetZ[k1] + Direction.offsetZ[i2], this, 1 << (k1 + 2 & 3), 2);
                        }
                        else if (this.func_150093_a(p_149674_1_.getBlock(p_149674_2_ + Direction.offsetX[k1], p_149674_3_ + 1, p_149674_4_ + Direction.offsetZ[k1])))
                        {
                            p_149674_1_.setBlock(p_149674_2_ + Direction.offsetX[k1], p_149674_3_, p_149674_4_ + Direction.offsetZ[k1], this, 0, 2);
                        }
                    }
                    else if (block.blockMaterial.isOpaque() && block.renderAsNormalBlock())
                    {
                        p_149674_1_.setBlockMetadataWithNotify(p_149674_2_, p_149674_3_, p_149674_4_, i1 | 1 << k1, 2);
                    }
                }
                else if (p_149674_3_ > 1)
                {
                    block = p_149674_1_.getBlock(p_149674_2_, p_149674_3_ - 1, p_149674_4_);

                    if (block.blockMaterial == Material.air)
                    {
                        l1 = p_149674_1_.rand.nextInt(16) & i1;

                        if (l1 > 0)
                        {
                            p_149674_1_.setBlock(p_149674_2_, p_149674_3_ - 1, p_149674_4_, this, l1, 2);
                        }
                    }
                    else if (block == this)
                    {
                        l1 = p_149674_1_.rand.nextInt(16) & i1;
                        i2 = p_149674_1_.getBlockMetadata(p_149674_2_, p_149674_3_ - 1, p_149674_4_);

                        if (i2 != (i2 | l1))
                        {
                            p_149674_1_.setBlockMetadataWithNotify(p_149674_2_, p_149674_3_ - 1, p_149674_4_, i2 | l1, 2);
                        }
                    }
                }
            }
        }
    }

    /**
     * Called when a block is placed using its ItemBlock. Args: World, X, Y, Z, side, hitX, hitY, hitZ, block metadata
     */
    public int onBlockPlaced(World p_149660_1_, int p_149660_2_, int p_149660_3_, int p_149660_4_, int p_149660_5_, float p_149660_6_, float p_149660_7_, float p_149660_8_, int p_149660_9_)
    {
        byte b0 = 0;

        switch (p_149660_5_)
        {
            case 2:
                b0 = 1;
                break;
            case 3:
                b0 = 4;
                break;
            case 4:
                b0 = 8;
                break;
            case 5:
                b0 = 2;
        }

        return b0 != 0 ? b0 : p_149660_9_;
    }

    public Item getItemDropped(int p_149650_1_, Random p_149650_2_, int p_149650_3_)
    {
        return null;
    }

    /**
     * Returns the quantity of items to drop on block destruction.
     */
    public int quantityDropped(Random p_149745_1_)
    {
        return 0;
    }

    /**
     * Called when the player destroys a block with an item that can harvest it. (i, j, k) are the coordinates of the
     * block and l is the block's subtype/damage.
     */
    public void harvestBlock(World p_149636_1_, EntityPlayer p_149636_2_, int p_149636_3_, int p_149636_4_, int p_149636_5_, int p_149636_6_)
    {
        {
            super.harvestBlock(p_149636_1_, p_149636_2_, p_149636_3_, p_149636_4_, p_149636_5_, p_149636_6_);
        }
    }

    @Override
    public boolean isShearable(ItemStack item, IBlockAccess world, int x, int y, int z)
    {
        return true;
    }

    @Override
    public ArrayList<ItemStack> onSheared(ItemStack item, IBlockAccess world, int x, int y, int z, int fortune)
    {
        ArrayList<ItemStack> ret = new ArrayList<ItemStack>();
        ret.add(new ItemStack(this, 1));
        return ret;
    }

    @Override
    public boolean isLadder(IBlockAccess world, int x, int y, int z, EntityLivingBase entity)
    {
        return true;
    }
}