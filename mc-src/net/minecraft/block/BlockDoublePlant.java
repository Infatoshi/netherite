package net.minecraft.block;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import net.minecraft.block.material.Material;
import net.minecraft.client.renderer.texture.IIconRegister;
import net.minecraft.creativetab.CreativeTabs;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.stats.StatList;
import net.minecraft.util.IIcon;
import net.minecraft.util.MathHelper;
import net.minecraft.world.IBlockAccess;
import net.minecraft.world.World;
import net.minecraftforge.common.IShearable;

public class BlockDoublePlant extends BlockBush implements IGrowable, IShearable
{
    public static final String[] field_149892_a = new String[] {"sunflower", "syringa", "grass", "fern", "rose", "paeonia"};
    @SideOnly(Side.CLIENT)
    private IIcon[] doublePlantBottomIcons;
    @SideOnly(Side.CLIENT)
    private IIcon[] doublePlantTopIcons;
    @SideOnly(Side.CLIENT)
    public IIcon[] sunflowerIcons;
    private static final String __OBFID = "CL_00000231";

    public BlockDoublePlant()
    {
        super(Material.plants);
        this.setHardness(0.0F);
        this.setStepSound(soundTypeGrass);
        this.setBlockName("doublePlant");
    }

    /**
     * The type of render function that is called for this block
     */
    public int getRenderType()
    {
        return 40;
    }

    /**
     * Updates the blocks bounds based on its current state. Args: world, x, y, z
     */
    public void setBlockBoundsBasedOnState(IBlockAccess p_149719_1_, int p_149719_2_, int p_149719_3_, int p_149719_4_)
    {
        this.setBlockBounds(0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F);
    }

    public int func_149885_e(IBlockAccess p_149885_1_, int p_149885_2_, int p_149885_3_, int p_149885_4_)
    {
        int l = p_149885_1_.getBlockMetadata(p_149885_2_, p_149885_3_, p_149885_4_);
        return !func_149887_c(l) ? l & 7 : p_149885_1_.getBlockMetadata(p_149885_2_, p_149885_3_ - 1, p_149885_4_) & 7;
    }

    /**
     * Checks to see if its valid to put this block at the specified coordinates. Args: world, x, y, z
     */
    public boolean canPlaceBlockAt(World p_149742_1_, int p_149742_2_, int p_149742_3_, int p_149742_4_)
    {
        return super.canPlaceBlockAt(p_149742_1_, p_149742_2_, p_149742_3_, p_149742_4_) && p_149742_1_.isAirBlock(p_149742_2_, p_149742_3_ + 1, p_149742_4_);
    }

    /**
     * checks if the block can stay, if not drop as item
     */
    protected void checkAndDropBlock(World p_149855_1_, int p_149855_2_, int p_149855_3_, int p_149855_4_)
    {
        if (!this.canBlockStay(p_149855_1_, p_149855_2_, p_149855_3_, p_149855_4_))
        {
            int l = p_149855_1_.getBlockMetadata(p_149855_2_, p_149855_3_, p_149855_4_);

            if (!func_149887_c(l))
            {
                this.dropBlockAsItem(p_149855_1_, p_149855_2_, p_149855_3_, p_149855_4_, l, 0);

                if (p_149855_1_.getBlock(p_149855_2_, p_149855_3_ + 1, p_149855_4_) == this)
                {
                    p_149855_1_.setBlock(p_149855_2_, p_149855_3_ + 1, p_149855_4_, Blocks.air, 0, 2);
                }
            }

            p_149855_1_.setBlock(p_149855_2_, p_149855_3_, p_149855_4_, Blocks.air, 0, 2);
        }
    }

    /**
     * Can this block stay at this position.  Similar to canPlaceBlockAt except gets checked often with plants.
     */
    public boolean canBlockStay(World p_149718_1_, int p_149718_2_, int p_149718_3_, int p_149718_4_)
    {
        if (p_149718_1_.getBlock(p_149718_2_, p_149718_3_, p_149718_4_) != this) return super.canBlockStay(p_149718_1_, p_149718_2_, p_149718_3_, p_149718_4_); //Forge: This function is called during world gen and placement, before this block is set, so if we are not 'here' then assume it's the pre-check.
        int l = p_149718_1_.getBlockMetadata(p_149718_2_, p_149718_3_, p_149718_4_);
        return func_149887_c(l) ? p_149718_1_.getBlock(p_149718_2_, p_149718_3_ - 1, p_149718_4_) == this : p_149718_1_.getBlock(p_149718_2_, p_149718_3_ + 1, p_149718_4_) == this && super.canBlockStay(p_149718_1_, p_149718_2_, p_149718_3_, p_149718_4_);
    }

    public Item getItemDropped(int p_149650_1_, Random p_149650_2_, int p_149650_3_)
    {
        if (func_149887_c(p_149650_1_))
        {
            return null;
        }
        else
        {
            int k = func_149890_d(p_149650_1_);
            return k != 3 && k != 2 ? Item.getItemFromBlock(this) : null;
        }
    }

    /**
     * Determines the damage on the item the block drops. Used in cloth and wood.
     */
    public int damageDropped(int p_149692_1_)
    {
        return func_149887_c(p_149692_1_) ? 0 : p_149692_1_ & 7;
    }

    public static boolean func_149887_c(int p_149887_0_)
    {
        return (p_149887_0_ & 8) != 0;
    }

    public static int func_149890_d(int p_149890_0_)
    {
        return p_149890_0_ & 7;
    }

    /**
     * Gets the block's texture. Args: side, meta
     */
    @SideOnly(Side.CLIENT)
    public IIcon getIcon(int p_149691_1_, int p_149691_2_)
    {
        return func_149887_c(p_149691_2_) ? this.doublePlantBottomIcons[0] : this.doublePlantBottomIcons[p_149691_2_ & 7];
    }

    @SideOnly(Side.CLIENT)
    public IIcon func_149888_a(boolean p_149888_1_, int p_149888_2_)
    {
        return p_149888_1_ ? this.doublePlantTopIcons[p_149888_2_] : this.doublePlantBottomIcons[p_149888_2_];
    }

    /**
     * Returns a integer with hex for 0xrrggbb with this color multiplied against the blocks color. Note only called
     * when first determining what to render.
     */
    @SideOnly(Side.CLIENT)
    public int colorMultiplier(IBlockAccess p_149720_1_, int p_149720_2_, int p_149720_3_, int p_149720_4_)
    {
        int l = this.func_149885_e(p_149720_1_, p_149720_2_, p_149720_3_, p_149720_4_);
        return l != 2 && l != 3 ? 16777215 : p_149720_1_.getBiomeGenForCoords(p_149720_2_, p_149720_4_).getBiomeGrassColor(p_149720_2_, p_149720_3_, p_149720_4_);
    }

    public void func_149889_c(World p_149889_1_, int p_149889_2_, int p_149889_3_, int p_149889_4_, int p_149889_5_, int p_149889_6_)
    {
        p_149889_1_.setBlock(p_149889_2_, p_149889_3_, p_149889_4_, this, p_149889_5_, p_149889_6_);
        p_149889_1_.setBlock(p_149889_2_, p_149889_3_ + 1, p_149889_4_, this, 8, p_149889_6_);
    }

    /**
     * Called when the block is placed in the world.
     */
    public void onBlockPlacedBy(World p_149689_1_, int p_149689_2_, int p_149689_3_, int p_149689_4_, EntityLivingBase p_149689_5_, ItemStack p_149689_6_)
    {
        int l = ((MathHelper.floor_double((double)(p_149689_5_.rotationYaw * 4.0F / 360.0F) + 0.5D) & 3) + 2) % 4;
        p_149689_1_.setBlock(p_149689_2_, p_149689_3_ + 1, p_149689_4_, this, 8 | l, 2);
    }

    /**
     * Called when the player destroys a block with an item that can harvest it. (i, j, k) are the coordinates of the
     * block and l is the block's subtype/damage.
     */
    public void harvestBlock(World p_149636_1_, EntityPlayer p_149636_2_, int p_149636_3_, int p_149636_4_, int p_149636_5_, int p_149636_6_)
    {
        if (p_149636_1_.isRemote || p_149636_2_.getCurrentEquippedItem() == null || p_149636_2_.getCurrentEquippedItem().getItem() != Items.shears || func_149887_c(p_149636_6_) || !this.func_149886_b(p_149636_1_, p_149636_3_, p_149636_4_, p_149636_5_, p_149636_6_, p_149636_2_))
        {
            super.harvestBlock(p_149636_1_, p_149636_2_, p_149636_3_, p_149636_4_, p_149636_5_, p_149636_6_);
        }
    }

    /**
     * Called when the block is attempted to be harvested
     */
    public void onBlockHarvested(World p_149681_1_, int p_149681_2_, int p_149681_3_, int p_149681_4_, int p_149681_5_, EntityPlayer p_149681_6_)
    {
        if (func_149887_c(p_149681_5_))
        {
            if (p_149681_1_.getBlock(p_149681_2_, p_149681_3_ - 1, p_149681_4_) == this)
            {
                if (!p_149681_6_.capabilities.isCreativeMode)
                {
                    int i1 = p_149681_1_.getBlockMetadata(p_149681_2_, p_149681_3_ - 1, p_149681_4_);
                    int j1 = func_149890_d(i1);

                    if (j1 != 3 && j1 != 2)
                    {
                        p_149681_1_.func_147480_a(p_149681_2_, p_149681_3_ - 1, p_149681_4_, true);
                    }
                    else
                    {
                        if (!p_149681_1_.isRemote && p_149681_6_.getCurrentEquippedItem() != null && p_149681_6_.getCurrentEquippedItem().getItem() == Items.shears)
                        {
                            this.func_149886_b(p_149681_1_, p_149681_2_, p_149681_3_, p_149681_4_, i1, p_149681_6_);
                        }

                        p_149681_1_.setBlockToAir(p_149681_2_, p_149681_3_ - 1, p_149681_4_);
                    }
                }
                else
                {
                    p_149681_1_.setBlockToAir(p_149681_2_, p_149681_3_ - 1, p_149681_4_);
                }
            }
        }
        else if (p_149681_6_.capabilities.isCreativeMode && p_149681_1_.getBlock(p_149681_2_, p_149681_3_ + 1, p_149681_4_) == this)
        {
            p_149681_1_.setBlock(p_149681_2_, p_149681_3_ + 1, p_149681_4_, Blocks.air, 0, 2);
        }

        super.onBlockHarvested(p_149681_1_, p_149681_2_, p_149681_3_, p_149681_4_, p_149681_5_, p_149681_6_);
    }

    private boolean func_149886_b(World p_149886_1_, int p_149886_2_, int p_149886_3_, int p_149886_4_, int p_149886_5_, EntityPlayer p_149886_6_)
    {
        int i1 = func_149890_d(p_149886_5_);

        if (i1 != 3 && i1 != 2)
        {
            return false;
        }
        else
        {
            p_149886_6_.addStat(StatList.mineBlockStatArray[Block.getIdFromBlock(this)], 1);
            byte b0 = 1;

            if (i1 == 3)
            {
                b0 = 2;
            }

            this.dropBlockAsItem(p_149886_1_, p_149886_2_, p_149886_3_, p_149886_4_, new ItemStack(Blocks.tallgrass, 2, b0));
            return true;
        }
    }

    @SideOnly(Side.CLIENT)
    public void registerBlockIcons(IIconRegister p_149651_1_)
    {
        this.doublePlantBottomIcons = new IIcon[field_149892_a.length];
        this.doublePlantTopIcons = new IIcon[field_149892_a.length];

        for (int i = 0; i < this.doublePlantBottomIcons.length; ++i)
        {
            this.doublePlantBottomIcons[i] = p_149651_1_.registerIcon("double_plant_" + field_149892_a[i] + "_bottom");
            this.doublePlantTopIcons[i] = p_149651_1_.registerIcon("double_plant_" + field_149892_a[i] + "_top");
        }

        this.sunflowerIcons = new IIcon[2];
        this.sunflowerIcons[0] = p_149651_1_.registerIcon("double_plant_sunflower_front");
        this.sunflowerIcons[1] = p_149651_1_.registerIcon("double_plant_sunflower_back");
    }

    /**
     * returns a list of blocks with the same ID, but different meta (eg: wood returns 4 blocks)
     */
    @SideOnly(Side.CLIENT)
    public void getSubBlocks(Item p_149666_1_, CreativeTabs p_149666_2_, List p_149666_3_)
    {
        for (int i = 0; i < this.doublePlantBottomIcons.length; ++i)
        {
            p_149666_3_.add(new ItemStack(p_149666_1_, 1, i));
        }
    }

    /**
     * Get the block's damage value (for use with pick block).
     */
    public int getDamageValue(World p_149643_1_, int p_149643_2_, int p_149643_3_, int p_149643_4_)
    {
        int l = p_149643_1_.getBlockMetadata(p_149643_2_, p_149643_3_, p_149643_4_);
        return func_149887_c(l) ? func_149890_d(p_149643_1_.getBlockMetadata(p_149643_2_, p_149643_3_ - 1, p_149643_4_)) : func_149890_d(l);
    }

    public boolean func_149851_a(World p_149851_1_, int p_149851_2_, int p_149851_3_, int p_149851_4_, boolean p_149851_5_)
    {
        int l = this.func_149885_e(p_149851_1_, p_149851_2_, p_149851_3_, p_149851_4_);
        return l != 2 && l != 3;
    }

    public boolean func_149852_a(World p_149852_1_, Random p_149852_2_, int p_149852_3_, int p_149852_4_, int p_149852_5_)
    {
        return true;
    }

    public void func_149853_b(World p_149853_1_, Random p_149853_2_, int p_149853_3_, int p_149853_4_, int p_149853_5_)
    {
        int l = this.func_149885_e(p_149853_1_, p_149853_3_, p_149853_4_, p_149853_5_);
        this.dropBlockAsItem(p_149853_1_, p_149853_3_, p_149853_4_, p_149853_5_, new ItemStack(this, 1, l));
    }

    @Override
    public boolean isShearable(ItemStack item, IBlockAccess world, int x, int y, int z)
    {
        int metadata = world.getBlockMetadata(x, y, z);
        int type = func_149890_d(metadata);
        return func_149887_c(metadata) && (type == 3 || type == 4);
    }

    @Override
    public ArrayList<ItemStack> onSheared(ItemStack item, IBlockAccess world, int x, int y, int z, int fortune)
    {
        ArrayList<ItemStack> ret = new ArrayList<ItemStack>();
        int type = func_149890_d(world.getBlockMetadata(x, y, z));
        if (type == 3 || type == 2)
            ret.add(new ItemStack(Blocks.tallgrass, 2, type == 3 ? 2 : 1));
        return ret;
    }
}