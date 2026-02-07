package net.minecraft.potion;

import java.util.ArrayList;
import java.util.List;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.init.Items;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;

public class PotionEffect
{
    /** ID value of the potion this effect matches. */
    private int potionID;
    /** The duration of the potion effect */
    private int duration;
    /** The amplifier of the potion effect */
    private int amplifier;
    /** Whether the potion is a splash potion */
    private boolean isSplashPotion;
    /** Whether the potion effect came from a beacon */
    private boolean isAmbient;
    /** True if potion effect duration is at maximum, false otherwise. */
    @SideOnly(Side.CLIENT)
    private boolean isPotionDurationMax;
    private static final String __OBFID = "CL_00001529";
    /** List of ItemStack that can cure the potion effect **/
    private List<ItemStack> curativeItems;

    public PotionEffect(int p_i1574_1_, int p_i1574_2_)
    {
        this(p_i1574_1_, p_i1574_2_, 0);
    }

    public PotionEffect(int p_i1575_1_, int p_i1575_2_, int p_i1575_3_)
    {
        this(p_i1575_1_, p_i1575_2_, p_i1575_3_, false);
    }

    public PotionEffect(int p_i1576_1_, int p_i1576_2_, int p_i1576_3_, boolean p_i1576_4_)
    {
        this.potionID = p_i1576_1_;
        this.duration = p_i1576_2_;
        this.amplifier = p_i1576_3_;
        this.isAmbient = p_i1576_4_;
        this.curativeItems = new ArrayList<ItemStack>();
        this.curativeItems.add(new ItemStack(Items.milk_bucket));
    }

    public PotionEffect(PotionEffect p_i1577_1_)
    {
        this.potionID = p_i1577_1_.potionID;
        this.duration = p_i1577_1_.duration;
        this.amplifier = p_i1577_1_.amplifier;
        this.curativeItems = p_i1577_1_.curativeItems;
    }

    /**
     * merges the input PotionEffect into this one if this.amplifier <= tomerge.amplifier. The duration in the supplied
     * potion effect is assumed to be greater.
     */
    public void combine(PotionEffect p_76452_1_)
    {
        if (this.potionID != p_76452_1_.potionID)
        {
            System.err.println("This method should only be called for matching effects!");
        }

        if (p_76452_1_.amplifier > this.amplifier)
        {
            this.amplifier = p_76452_1_.amplifier;
            this.duration = p_76452_1_.duration;
        }
        else if (p_76452_1_.amplifier == this.amplifier && this.duration < p_76452_1_.duration)
        {
            this.duration = p_76452_1_.duration;
        }
        else if (!p_76452_1_.isAmbient && this.isAmbient)
        {
            this.isAmbient = p_76452_1_.isAmbient;
        }
    }

    /**
     * Retrieve the ID of the potion this effect matches.
     */
    public int getPotionID()
    {
        return this.potionID;
    }

    public int getDuration()
    {
        return this.duration;
    }

    public int getAmplifier()
    {
        return this.amplifier;
    }

    /**
     * Set whether this potion is a splash potion.
     */
    public void setSplashPotion(boolean p_82721_1_)
    {
        this.isSplashPotion = p_82721_1_;
    }

    /**
     * Gets whether this potion effect originated from a beacon
     */
    public boolean getIsAmbient()
    {
        return this.isAmbient;
    }

    public boolean onUpdate(EntityLivingBase p_76455_1_)
    {
        if (this.duration > 0)
        {
            if (Potion.potionTypes[this.potionID].isReady(this.duration, this.amplifier))
            {
                this.performEffect(p_76455_1_);
            }

            this.deincrementDuration();
        }

        return this.duration > 0;
    }

    private int deincrementDuration()
    {
        return --this.duration;
    }

    public void performEffect(EntityLivingBase p_76457_1_)
    {
        if (this.duration > 0)
        {
            Potion.potionTypes[this.potionID].performEffect(p_76457_1_, this.amplifier);
        }
    }

    public String getEffectName()
    {
        return Potion.potionTypes[this.potionID].getName();
    }

    public int hashCode()
    {
        return this.potionID;
    }

    public String toString()
    {
        String s = "";

        if (this.getAmplifier() > 0)
        {
            s = this.getEffectName() + " x " + (this.getAmplifier() + 1) + ", Duration: " + this.getDuration();
        }
        else
        {
            s = this.getEffectName() + ", Duration: " + this.getDuration();
        }

        if (this.isSplashPotion)
        {
            s = s + ", Splash: true";
        }

        return Potion.potionTypes[this.potionID].isUsable() ? "(" + s + ")" : s;
    }

    public boolean equals(Object p_equals_1_)
    {
        if (!(p_equals_1_ instanceof PotionEffect))
        {
            return false;
        }
        else
        {
            PotionEffect potioneffect = (PotionEffect)p_equals_1_;
            return this.potionID == potioneffect.potionID && this.amplifier == potioneffect.amplifier && this.duration == potioneffect.duration && this.isSplashPotion == potioneffect.isSplashPotion && this.isAmbient == potioneffect.isAmbient;
        }
    }

    /**
     * Write a custom potion effect to a potion item's NBT data.
     */
    public NBTTagCompound writeCustomPotionEffectToNBT(NBTTagCompound p_82719_1_)
    {
        p_82719_1_.setByte("Id", (byte)this.getPotionID());
        p_82719_1_.setByte("Amplifier", (byte)this.getAmplifier());
        p_82719_1_.setInteger("Duration", this.getDuration());
        p_82719_1_.setBoolean("Ambient", this.getIsAmbient());
        return p_82719_1_;
    }

    /**
     * Read a custom potion effect from a potion item's NBT data.
     */
    public static PotionEffect readCustomPotionEffectFromNBT(NBTTagCompound p_82722_0_)
    {
        byte b0 = p_82722_0_.getByte("Id");

        if (b0 >= 0 && b0 < Potion.potionTypes.length && Potion.potionTypes[b0] != null)
        {
            byte b1 = p_82722_0_.getByte("Amplifier");
            int i = p_82722_0_.getInteger("Duration");
            boolean flag = p_82722_0_.getBoolean("Ambient");
            return new PotionEffect(b0, i, b1, flag);
        }
        else
        {
            return null;
        }
    }

    /**
     * Toggle the isPotionDurationMax field.
     */
    @SideOnly(Side.CLIENT)
    public void setPotionDurationMax(boolean p_100012_1_)
    {
        this.isPotionDurationMax = p_100012_1_;
    }

    @SideOnly(Side.CLIENT)
    public boolean getIsPotionDurationMax()
    {
        return this.isPotionDurationMax;
    }

    /* ======================================== FORGE START =====================================*/
    /***
     * Returns a list of curative items for the potion effect
     * @return The list (ItemStack) of curative items for the potion effect
     */
    public List<ItemStack> getCurativeItems()
    {
        return this.curativeItems;
    }

    /***
     * Checks the given ItemStack to see if it is in the list of curative items for the potion effect
     * @param stack The ItemStack being checked against the list of curative items for the potion effect
     * @return true if the given ItemStack is in the list of curative items for the potion effect, false otherwise
     */
    public boolean isCurativeItem(ItemStack stack)
    {
        boolean found = false;
        for (ItemStack curativeItem : this.curativeItems)
        {
            if (curativeItem.isItemEqual(stack))
            {
                found = true;
            }
        }

        return found;
    }

    /***
     * Sets the array of curative items for the potion effect
     * @param curativeItems The list of ItemStacks being set to the potion effect
     */
    public void setCurativeItems(List<ItemStack> curativeItems)
    {
        this.curativeItems = curativeItems;
    }

    /***
     * Adds the given stack to list of curative items for the potion effect
     * @param stack The ItemStack being added to the curative item list
     */
    public void addCurativeItem(ItemStack stack)
    {
        boolean found = false;
        for (ItemStack curativeItem : this.curativeItems)
        {
            if (curativeItem.isItemEqual(stack))
            {
                found = true;
            }
        }
        if (!found)
        {
            this.curativeItems.add(stack);
        }
    }
}