package net.minecraft.item;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.List;
import java.util.Random;
import net.minecraft.enchantment.Enchantment;
import net.minecraft.enchantment.EnchantmentData;
import net.minecraft.enchantment.EnchantmentHelper;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Items;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.util.WeightedRandomChestContent;

public class ItemEnchantedBook extends Item
{
    private static final String __OBFID = "CL_00000025";

    @SideOnly(Side.CLIENT)
    public boolean hasEffect(ItemStack p_77636_1_)
    {
        return true;
    }

    /**
     * Checks isDamagable and if it cannot be stacked
     */
    public boolean isItemTool(ItemStack p_77616_1_)
    {
        return false;
    }

    /**
     * Return an item rarity from EnumRarity
     */
    public EnumRarity getRarity(ItemStack p_77613_1_)
    {
        return this.func_92110_g(p_77613_1_).tagCount() > 0 ? EnumRarity.uncommon : super.getRarity(p_77613_1_);
    }

    public NBTTagList func_92110_g(ItemStack p_92110_1_)
    {
        return p_92110_1_.stackTagCompound != null && p_92110_1_.stackTagCompound.hasKey("StoredEnchantments", 9) ? (NBTTagList)p_92110_1_.stackTagCompound.getTag("StoredEnchantments") : new NBTTagList();
    }

    /**
     * allows items to add custom lines of information to the mouseover description
     */
    @SideOnly(Side.CLIENT)
    public void addInformation(ItemStack p_77624_1_, EntityPlayer p_77624_2_, List p_77624_3_, boolean p_77624_4_)
    {
        super.addInformation(p_77624_1_, p_77624_2_, p_77624_3_, p_77624_4_);
        NBTTagList nbttaglist = this.func_92110_g(p_77624_1_);

        if (nbttaglist != null)
        {
            for (int i = 0; i < nbttaglist.tagCount(); ++i)
            {
                short short1 = nbttaglist.getCompoundTagAt(i).getShort("id");
                short short2 = nbttaglist.getCompoundTagAt(i).getShort("lvl");

                if (Enchantment.enchantmentsList[short1] != null)
                {
                    p_77624_3_.add(Enchantment.enchantmentsList[short1].getTranslatedName(short2));
                }
            }
        }
    }

    /**
     * Adds an stored enchantment to an enchanted book ItemStack
     */
    public void addEnchantment(ItemStack p_92115_1_, EnchantmentData p_92115_2_)
    {
        NBTTagList nbttaglist = this.func_92110_g(p_92115_1_);
        boolean flag = true;

        for (int i = 0; i < nbttaglist.tagCount(); ++i)
        {
            NBTTagCompound nbttagcompound = nbttaglist.getCompoundTagAt(i);

            if (nbttagcompound.getShort("id") == p_92115_2_.enchantmentobj.effectId)
            {
                if (nbttagcompound.getShort("lvl") < p_92115_2_.enchantmentLevel)
                {
                    nbttagcompound.setShort("lvl", (short)p_92115_2_.enchantmentLevel);
                }

                flag = false;
                break;
            }
        }

        if (flag)
        {
            NBTTagCompound nbttagcompound1 = new NBTTagCompound();
            nbttagcompound1.setShort("id", (short)p_92115_2_.enchantmentobj.effectId);
            nbttagcompound1.setShort("lvl", (short)p_92115_2_.enchantmentLevel);
            nbttaglist.appendTag(nbttagcompound1);
        }

        if (!p_92115_1_.hasTagCompound())
        {
            p_92115_1_.setTagCompound(new NBTTagCompound());
        }

        p_92115_1_.getTagCompound().setTag("StoredEnchantments", nbttaglist);
    }

    /**
     * Returns the ItemStack of an enchanted version of this item.
     */
    public ItemStack getEnchantedItemStack(EnchantmentData p_92111_1_)
    {
        ItemStack itemstack = new ItemStack(this);
        this.addEnchantment(itemstack, p_92111_1_);
        return itemstack;
    }

    @SideOnly(Side.CLIENT)
    public void func_92113_a(Enchantment p_92113_1_, List p_92113_2_)
    {
        for (int i = p_92113_1_.getMinLevel(); i <= p_92113_1_.getMaxLevel(); ++i)
        {
            p_92113_2_.add(this.getEnchantedItemStack(new EnchantmentData(p_92113_1_, i)));
        }
    }

    public WeightedRandomChestContent func_92114_b(Random p_92114_1_)
    {
        return this.func_92112_a(p_92114_1_, 1, 1, 1);
    }

    public WeightedRandomChestContent func_92112_a(Random p_92112_1_, int p_92112_2_, int p_92112_3_, int p_92112_4_)
    {
        ItemStack itemstack = new ItemStack(Items.book, 1, 0);
        EnchantmentHelper.addRandomEnchantment(p_92112_1_, itemstack, 30);
        return new WeightedRandomChestContent(itemstack, p_92112_2_, p_92112_3_, p_92112_4_);
    }
}