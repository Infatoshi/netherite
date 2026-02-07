package net.minecraft.block;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.ArrayList;
import java.util.Random;
import net.minecraft.block.material.Material;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.item.Item;
import net.minecraft.item.ItemBlock;
import net.minecraft.item.ItemStack;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.tileentity.TileEntityFlowerPot;
import net.minecraft.world.World;

public class BlockFlowerPot extends BlockContainer
{
    private static final String __OBFID = "CL_00000247";

    public BlockFlowerPot()
    {
        super(Material.circuits);
        this.setBlockBoundsForItemRender();
    }

    /**
     * Sets the block's bounds for rendering it as an item
     */
    public void setBlockBoundsForItemRender()
    {
        float f = 0.375F;
        float f1 = f / 2.0F;
        this.setBlockBounds(0.5F - f1, 0.0F, 0.5F - f1, 0.5F + f1, f, 0.5F + f1);
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
     * The type of render function that is called for this block
     */
    public int getRenderType()
    {
        return 33;
    }

    /**
     * If this block doesn't render as an ordinary block it will return False (examples: signs, buttons, stairs, etc)
     */
    public boolean renderAsNormalBlock()
    {
        return false;
    }

    /**
     * Called upon block activation (right click on the block.)
     */
    public boolean onBlockActivated(World p_149727_1_, int p_149727_2_, int p_149727_3_, int p_149727_4_, EntityPlayer p_149727_5_, int p_149727_6_, float p_149727_7_, float p_149727_8_, float p_149727_9_)
    {
        ItemStack itemstack = p_149727_5_.inventory.getCurrentItem();

        if (itemstack != null && itemstack.getItem() instanceof ItemBlock)
        {
            TileEntityFlowerPot tileentityflowerpot = this.func_149929_e(p_149727_1_, p_149727_2_, p_149727_3_, p_149727_4_);

            if (tileentityflowerpot != null)
            {
                if (tileentityflowerpot.getFlowerPotItem() != null)
                {
                    return false;
                }
                else
                {
                    Block block = Block.getBlockFromItem(itemstack.getItem());

                    if (!this.func_149928_a(block, itemstack.getItemDamage()))
                    {
                        return false;
                    }
                    else
                    {
                        tileentityflowerpot.func_145964_a(itemstack.getItem(), itemstack.getItemDamage());
                        tileentityflowerpot.markDirty();

                        if (!p_149727_1_.setBlockMetadataWithNotify(p_149727_2_, p_149727_3_, p_149727_4_, itemstack.getItemDamage(), 2))
                        {
                            p_149727_1_.markBlockForUpdate(p_149727_2_, p_149727_3_, p_149727_4_);
                        }

                        if (!p_149727_5_.capabilities.isCreativeMode && --itemstack.stackSize <= 0)
                        {
                            p_149727_5_.inventory.setInventorySlotContents(p_149727_5_.inventory.currentItem, (ItemStack)null);
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
        else
        {
            return false;
        }
    }

    private boolean func_149928_a(Block p_149928_1_, int p_149928_2_)
    {
        return p_149928_1_ != Blocks.yellow_flower && p_149928_1_ != Blocks.red_flower && p_149928_1_ != Blocks.cactus && p_149928_1_ != Blocks.brown_mushroom && p_149928_1_ != Blocks.red_mushroom && p_149928_1_ != Blocks.sapling && p_149928_1_ != Blocks.deadbush ? p_149928_1_ == Blocks.tallgrass && p_149928_2_ == 2 : true;
    }

    /**
     * Gets an item for the block being called on. Args: world, x, y, z
     */
    @SideOnly(Side.CLIENT)
    public Item getItem(World p_149694_1_, int p_149694_2_, int p_149694_3_, int p_149694_4_)
    {
        TileEntityFlowerPot tileentityflowerpot = this.func_149929_e(p_149694_1_, p_149694_2_, p_149694_3_, p_149694_4_);
        return tileentityflowerpot != null && tileentityflowerpot.getFlowerPotItem() != null ? tileentityflowerpot.getFlowerPotItem() : Items.flower_pot;
    }

    /**
     * Get the block's damage value (for use with pick block).
     */
    public int getDamageValue(World p_149643_1_, int p_149643_2_, int p_149643_3_, int p_149643_4_)
    {
        TileEntityFlowerPot tileentityflowerpot = this.func_149929_e(p_149643_1_, p_149643_2_, p_149643_3_, p_149643_4_);
        return tileentityflowerpot != null && tileentityflowerpot.getFlowerPotItem() != null ? tileentityflowerpot.getFlowerPotData() : 0;
    }

    /**
     * Returns true only if block is flowerPot
     */
    @SideOnly(Side.CLIENT)
    public boolean isFlowerPot()
    {
        return true;
    }

    /**
     * Checks to see if its valid to put this block at the specified coordinates. Args: world, x, y, z
     */
    public boolean canPlaceBlockAt(World p_149742_1_, int p_149742_2_, int p_149742_3_, int p_149742_4_)
    {
        return super.canPlaceBlockAt(p_149742_1_, p_149742_2_, p_149742_3_, p_149742_4_) && World.doesBlockHaveSolidTopSurface(p_149742_1_, p_149742_2_, p_149742_3_ - 1, p_149742_4_);
    }

    /**
     * Lets the block know when one of its neighbor changes. Doesn't know which neighbor changed (coordinates passed are
     * their own) Args: x, y, z, neighbor Block
     */
    public void onNeighborBlockChange(World p_149695_1_, int p_149695_2_, int p_149695_3_, int p_149695_4_, Block p_149695_5_)
    {
        if (!World.doesBlockHaveSolidTopSurface(p_149695_1_, p_149695_2_, p_149695_3_ - 1, p_149695_4_))
        {
            this.dropBlockAsItem(p_149695_1_, p_149695_2_, p_149695_3_, p_149695_4_, p_149695_1_.getBlockMetadata(p_149695_2_, p_149695_3_, p_149695_4_), 0);
            p_149695_1_.setBlockToAir(p_149695_2_, p_149695_3_, p_149695_4_);
        }
    }

    public void breakBlock(World p_149749_1_, int p_149749_2_, int p_149749_3_, int p_149749_4_, Block p_149749_5_, int p_149749_6_)
    {
        super.breakBlock(p_149749_1_, p_149749_2_, p_149749_3_, p_149749_4_, p_149749_5_, p_149749_6_);
    }

    /**
     * Called when the block is attempted to be harvested
     */
    public void onBlockHarvested(World p_149681_1_, int p_149681_2_, int p_149681_3_, int p_149681_4_, int p_149681_5_, EntityPlayer p_149681_6_)
    {
        super.onBlockHarvested(p_149681_1_, p_149681_2_, p_149681_3_, p_149681_4_, p_149681_5_, p_149681_6_);

        if (p_149681_6_.capabilities.isCreativeMode)
        {
            TileEntityFlowerPot tileentityflowerpot = this.func_149929_e(p_149681_1_, p_149681_2_, p_149681_3_, p_149681_4_);

            if (tileentityflowerpot != null)
            {
                tileentityflowerpot.func_145964_a(Item.getItemById(0), 0);
            }
        }
    }

    public Item getItemDropped(int p_149650_1_, Random p_149650_2_, int p_149650_3_)
    {
        return Items.flower_pot;
    }

    private TileEntityFlowerPot func_149929_e(World p_149929_1_, int p_149929_2_, int p_149929_3_, int p_149929_4_)
    {
        TileEntity tileentity = p_149929_1_.getTileEntity(p_149929_2_, p_149929_3_, p_149929_4_);
        return tileentity != null && tileentity instanceof TileEntityFlowerPot ? (TileEntityFlowerPot)tileentity : null;
    }

    /**
     * Returns a new instance of a block's tile entity class. Called on placing the block.
     */
    public TileEntity createNewTileEntity(World p_149915_1_, int p_149915_2_)
    {
        Object object = null;
        byte b0 = 0;

        switch (p_149915_2_)
        {
            case 1:
                object = Blocks.red_flower;
                b0 = 0;
                break;
            case 2:
                object = Blocks.yellow_flower;
                break;
            case 3:
                object = Blocks.sapling;
                b0 = 0;
                break;
            case 4:
                object = Blocks.sapling;
                b0 = 1;
                break;
            case 5:
                object = Blocks.sapling;
                b0 = 2;
                break;
            case 6:
                object = Blocks.sapling;
                b0 = 3;
                break;
            case 7:
                object = Blocks.red_mushroom;
                break;
            case 8:
                object = Blocks.brown_mushroom;
                break;
            case 9:
                object = Blocks.cactus;
                break;
            case 10:
                object = Blocks.deadbush;
                break;
            case 11:
                object = Blocks.tallgrass;
                b0 = 2;
                break;
            case 12:
                object = Blocks.sapling;
                b0 = 4;
                break;
            case 13:
                object = Blocks.sapling;
                b0 = 5;
        }

        return new TileEntityFlowerPot(Item.getItemFromBlock((Block)object), b0);
    }

    /*============================FORGE START=====================================*/
    @Override
    public ArrayList<ItemStack> getDrops(World world, int x, int y, int z, int metadata, int fortune)
    {
        ArrayList<ItemStack> ret = super.getDrops(world, x, y, z, metadata, fortune);
        TileEntityFlowerPot te = this.func_149929_e(world, x, y, z);
        if (te != null && te.getFlowerPotItem() != null)
            ret.add(new ItemStack(te.getFlowerPotItem(), 1, te.getFlowerPotData()));
        return ret;
    }
    @Override
    public boolean removedByPlayer(World world, EntityPlayer player, int x, int y, int z, boolean willHarvest)
    {
        if (willHarvest) return true; //If it will harvest, delay deletion of the block until after getDrops
        return super.removedByPlayer(world, player, x, y, z, willHarvest);
    }
    /**
     * Called when the player destroys a block with an item that can harvest it. (i, j, k) are the coordinates of the
     * block and l is the block's subtype/damage.
     */
    @Override
    public void harvestBlock(World world, EntityPlayer player, int x, int y, int z, int meta)
    {
        super.harvestBlock(world, player, x, y, z, meta);
        world.setBlockToAir(x, y, z);
    }
    /*===========================FORGE END==========================================*/
}