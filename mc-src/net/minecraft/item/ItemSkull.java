package net.minecraft.item;

import com.mojang.authlib.GameProfile;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.List;
import java.util.UUID;
import net.minecraft.block.BlockSkull;
import net.minecraft.client.renderer.texture.IIconRegister;
import net.minecraft.creativetab.CreativeTabs;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTUtil;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.tileentity.TileEntitySkull;
import net.minecraft.util.IIcon;
import net.minecraft.util.MathHelper;
import net.minecraft.util.StatCollector;
import net.minecraft.world.World;

public class ItemSkull extends Item
{
    private static final String[] skullTypes = new String[] {"skeleton", "wither", "zombie", "char", "creeper"};
    public static final String[] field_94587_a = new String[] {"skeleton", "wither", "zombie", "steve", "creeper"};
    @SideOnly(Side.CLIENT)
    private IIcon[] field_94586_c;
    private static final String __OBFID = "CL_00000067";

    public ItemSkull()
    {
        this.setCreativeTab(CreativeTabs.tabDecorations);
        this.setMaxDamage(0);
        this.setHasSubtypes(true);
    }

    /**
     * Callback for item usage. If the item does something special on right clicking, he will have one of those. Return
     * True if something happen and false if it don't. This is for ITEMS, not BLOCKS
     */
    public boolean onItemUse(ItemStack p_77648_1_, EntityPlayer p_77648_2_, World p_77648_3_, int p_77648_4_, int p_77648_5_, int p_77648_6_, int p_77648_7_, float p_77648_8_, float p_77648_9_, float p_77648_10_)
    {
        if(p_77648_3_.getBlock(p_77648_4_, p_77648_5_, p_77648_6_).isReplaceable(p_77648_3_, p_77648_4_, p_77648_5_, p_77648_6_) && p_77648_7_ != 0)
        {
            p_77648_7_ = 1;
            p_77648_5_--;
        }
        if (p_77648_7_ == 0)
        {
            return false;
        }
        else if (!p_77648_3_.getBlock(p_77648_4_, p_77648_5_, p_77648_6_).getMaterial().isSolid() && !p_77648_3_.isSideSolid(p_77648_4_, p_77648_5_, p_77648_6_, net.minecraftforge.common.util.ForgeDirection.getOrientation(p_77648_7_)))
        {
            return false;
        }
        else
        {
            if (p_77648_7_ == 1)
            {
                ++p_77648_5_;
            }

            if (p_77648_7_ == 2)
            {
                --p_77648_6_;
            }

            if (p_77648_7_ == 3)
            {
                ++p_77648_6_;
            }

            if (p_77648_7_ == 4)
            {
                --p_77648_4_;
            }

            if (p_77648_7_ == 5)
            {
                ++p_77648_4_;
            }

        }
        {
            if (!p_77648_3_.isRemote)
            {
                if (!Blocks.skull.canPlaceBlockOnSide(p_77648_3_, p_77648_4_, p_77648_5_, p_77648_6_, p_77648_7_)) return false;
                p_77648_3_.setBlock(p_77648_4_, p_77648_5_, p_77648_6_, Blocks.skull, p_77648_7_, 2);
                int i1 = 0;

                if (p_77648_7_ == 1)
                {
                    i1 = MathHelper.floor_double((double)(p_77648_2_.rotationYaw * 16.0F / 360.0F) + 0.5D) & 15;
                }

                TileEntity tileentity = p_77648_3_.getTileEntity(p_77648_4_, p_77648_5_, p_77648_6_);

                if (tileentity != null && tileentity instanceof TileEntitySkull)
                {
                    if (p_77648_1_.getItemDamage() == 3)
                    {
                        GameProfile gameprofile = null;

                        if (p_77648_1_.hasTagCompound())
                        {
                            NBTTagCompound nbttagcompound = p_77648_1_.getTagCompound();

                            if (nbttagcompound.hasKey("SkullOwner", 10))
                            {
                                gameprofile = NBTUtil.func_152459_a(nbttagcompound.getCompoundTag("SkullOwner"));
                            }
                            else if (nbttagcompound.hasKey("SkullOwner", 8) && nbttagcompound.getString("SkullOwner").length() > 0)
                            {
                                gameprofile = new GameProfile((UUID)null, nbttagcompound.getString("SkullOwner"));
                            }
                        }

                        ((TileEntitySkull)tileentity).func_152106_a(gameprofile);
                    }
                    else
                    {
                        ((TileEntitySkull)tileentity).func_152107_a(p_77648_1_.getItemDamage());
                    }

                    ((TileEntitySkull)tileentity).func_145903_a(i1);
                    ((BlockSkull)Blocks.skull).func_149965_a(p_77648_3_, p_77648_4_, p_77648_5_, p_77648_6_, (TileEntitySkull)tileentity);
                }

                --p_77648_1_.stackSize;
            }

            return true;
        }
    }

    /**
     * returns a list of items with the same ID, but different meta (eg: dye returns 16 items)
     */
    @SideOnly(Side.CLIENT)
    public void getSubItems(Item p_150895_1_, CreativeTabs p_150895_2_, List p_150895_3_)
    {
        for (int i = 0; i < skullTypes.length; ++i)
        {
            p_150895_3_.add(new ItemStack(p_150895_1_, 1, i));
        }
    }

    /**
     * Returns the metadata of the block which this Item (ItemBlock) can place
     */
    public int getMetadata(int p_77647_1_)
    {
        return p_77647_1_;
    }

    /**
     * Returns the unlocalized name of this item. This version accepts an ItemStack so different stacks can have
     * different names based on their damage or NBT.
     */
    public String getUnlocalizedName(ItemStack p_77667_1_)
    {
        int i = p_77667_1_.getItemDamage();

        if (i < 0 || i >= skullTypes.length)
        {
            i = 0;
        }

        return super.getUnlocalizedName() + "." + skullTypes[i];
    }

    /**
     * Gets an icon index based on an item's damage value
     */
    @SideOnly(Side.CLIENT)
    public IIcon getIconFromDamage(int p_77617_1_)
    {
        if (p_77617_1_ < 0 || p_77617_1_ >= skullTypes.length)
        {
            p_77617_1_ = 0;
        }

        return this.field_94586_c[p_77617_1_];
    }

    public String getItemStackDisplayName(ItemStack p_77653_1_)
    {
        if (p_77653_1_.getItemDamage() == 3 && p_77653_1_.hasTagCompound())
        {
            if (p_77653_1_.getTagCompound().hasKey("SkullOwner", 10))
            {
                return StatCollector.translateToLocalFormatted("item.skull.player.name", new Object[] {NBTUtil.func_152459_a(p_77653_1_.getTagCompound().getCompoundTag("SkullOwner")).getName()});
            }

            if (p_77653_1_.getTagCompound().hasKey("SkullOwner", 8))
            {
                return StatCollector.translateToLocalFormatted("item.skull.player.name", new Object[] {p_77653_1_.getTagCompound().getString("SkullOwner")});
            }
        }

        return super.getItemStackDisplayName(p_77653_1_);
    }

    @SideOnly(Side.CLIENT)
    public void registerIcons(IIconRegister p_94581_1_)
    {
        this.field_94586_c = new IIcon[field_94587_a.length];

        for (int i = 0; i < field_94587_a.length; ++i)
        {
            this.field_94586_c[i] = p_94581_1_.registerIcon(this.getIconString() + "_" + field_94587_a[i]);
        }
    }
}