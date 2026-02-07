package net.minecraft.enchantment;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.EnumCreatureAttribute;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Items;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.util.DamageSource;
import net.minecraft.util.WeightedRandom;

public class EnchantmentHelper
{
    /** Is the random seed of enchantment effects. */
    private static final Random enchantmentRand = new Random();
    /** Used to calculate the extra armor of enchantments on armors equipped on player. */
    private static final EnchantmentHelper.ModifierDamage enchantmentModifierDamage = new EnchantmentHelper.ModifierDamage(null);
    /** Used to calculate the (magic) extra damage done by enchantments on current equipped item of player. */
    private static final EnchantmentHelper.ModifierLiving enchantmentModifierLiving = new EnchantmentHelper.ModifierLiving(null);
    private static final EnchantmentHelper.HurtIterator field_151388_d = new EnchantmentHelper.HurtIterator(null);
    private static final EnchantmentHelper.DamageIterator field_151389_e = new EnchantmentHelper.DamageIterator(null);
    private static final String __OBFID = "CL_00000107";

    /**
     * Returns the level of enchantment on the ItemStack passed.
     */
    public static int getEnchantmentLevel(int p_77506_0_, ItemStack p_77506_1_)
    {
        if (p_77506_1_ == null)
        {
            return 0;
        }
        else
        {
            NBTTagList nbttaglist = p_77506_1_.getEnchantmentTagList();

            if (nbttaglist == null)
            {
                return 0;
            }
            else
            {
                for (int j = 0; j < nbttaglist.tagCount(); ++j)
                {
                    short short1 = nbttaglist.getCompoundTagAt(j).getShort("id");
                    short short2 = nbttaglist.getCompoundTagAt(j).getShort("lvl");

                    if (short1 == p_77506_0_)
                    {
                        return short2;
                    }
                }

                return 0;
            }
        }
    }

    /**
     * Return the enchantments for the specified stack.
     */
    public static Map getEnchantments(ItemStack p_82781_0_)
    {
        LinkedHashMap linkedhashmap = new LinkedHashMap();
        NBTTagList nbttaglist = p_82781_0_.getItem() == Items.enchanted_book ? Items.enchanted_book.func_92110_g(p_82781_0_) : p_82781_0_.getEnchantmentTagList();

        if (nbttaglist != null)
        {
            for (int i = 0; i < nbttaglist.tagCount(); ++i)
            {
                short short1 = nbttaglist.getCompoundTagAt(i).getShort("id");
                short short2 = nbttaglist.getCompoundTagAt(i).getShort("lvl");
                linkedhashmap.put(Integer.valueOf(short1), Integer.valueOf(short2));
            }
        }

        return linkedhashmap;
    }

    /**
     * Set the enchantments for the specified stack.
     */
    public static void setEnchantments(Map p_82782_0_, ItemStack p_82782_1_)
    {
        NBTTagList nbttaglist = new NBTTagList();
        Iterator iterator = p_82782_0_.keySet().iterator();

        while (iterator.hasNext())
        {
            int i = ((Integer)iterator.next()).intValue();
            NBTTagCompound nbttagcompound = new NBTTagCompound();
            nbttagcompound.setShort("id", (short)i);
            nbttagcompound.setShort("lvl", (short)((Integer)p_82782_0_.get(Integer.valueOf(i))).intValue());
            nbttaglist.appendTag(nbttagcompound);

            if (p_82782_1_.getItem() == Items.enchanted_book)
            {
                Items.enchanted_book.addEnchantment(p_82782_1_, new EnchantmentData(i, ((Integer)p_82782_0_.get(Integer.valueOf(i))).intValue()));
            }
        }

        if (nbttaglist.tagCount() > 0)
        {
            if (p_82782_1_.getItem() != Items.enchanted_book)
            {
                p_82782_1_.setTagInfo("ench", nbttaglist);
            }
        }
        else if (p_82782_1_.hasTagCompound())
        {
            p_82782_1_.getTagCompound().removeTag("ench");
        }
    }

    /**
     * Returns the biggest level of the enchantment on the array of ItemStack passed.
     */
    public static int getMaxEnchantmentLevel(int p_77511_0_, ItemStack[] p_77511_1_)
    {
        if (p_77511_1_ == null)
        {
            return 0;
        }
        else
        {
            int j = 0;
            ItemStack[] aitemstack1 = p_77511_1_;
            int k = p_77511_1_.length;

            for (int l = 0; l < k; ++l)
            {
                ItemStack itemstack = aitemstack1[l];
                int i1 = getEnchantmentLevel(p_77511_0_, itemstack);

                if (i1 > j)
                {
                    j = i1;
                }
            }

            return j;
        }
    }

    /**
     * Executes the enchantment modifier on the ItemStack passed.
     */
    private static void applyEnchantmentModifier(EnchantmentHelper.IModifier p_77518_0_, ItemStack p_77518_1_)
    {
        if (p_77518_1_ != null)
        {
            NBTTagList nbttaglist = p_77518_1_.getEnchantmentTagList();

            if (nbttaglist != null)
            {
                for (int i = 0; i < nbttaglist.tagCount(); ++i)
                {
                    short short1 = nbttaglist.getCompoundTagAt(i).getShort("id");
                    short short2 = nbttaglist.getCompoundTagAt(i).getShort("lvl");

                    if (Enchantment.enchantmentsList[short1] != null)
                    {
                        p_77518_0_.calculateModifier(Enchantment.enchantmentsList[short1], short2);
                    }
                }
            }
        }
    }

    /**
     * Executes the enchantment modifier on the array of ItemStack passed.
     */
    private static void applyEnchantmentModifierArray(EnchantmentHelper.IModifier p_77516_0_, ItemStack[] p_77516_1_)
    {
        ItemStack[] aitemstack1 = p_77516_1_;
        int i = p_77516_1_.length;

        for (int j = 0; j < i; ++j)
        {
            ItemStack itemstack = aitemstack1[j];
            applyEnchantmentModifier(p_77516_0_, itemstack);
        }
    }

    /**
     * Returns the modifier of protection enchantments on armors equipped on player.
     */
    public static int getEnchantmentModifierDamage(ItemStack[] p_77508_0_, DamageSource p_77508_1_)
    {
        enchantmentModifierDamage.damageModifier = 0;
        enchantmentModifierDamage.source = p_77508_1_;
        applyEnchantmentModifierArray(enchantmentModifierDamage, p_77508_0_);

        if (enchantmentModifierDamage.damageModifier > 25)
        {
            enchantmentModifierDamage.damageModifier = 25;
        }

        return (enchantmentModifierDamage.damageModifier + 1 >> 1) + enchantmentRand.nextInt((enchantmentModifierDamage.damageModifier >> 1) + 1);
    }

    /**
     * Return the (magic) extra damage of the enchantments on player equipped item.
     */
    public static float getEnchantmentModifierLiving(EntityLivingBase p_77512_0_, EntityLivingBase p_77512_1_)
    {
        return func_152377_a(p_77512_0_.getHeldItem(), p_77512_1_.getCreatureAttribute());
    }

    public static float func_152377_a(ItemStack p_152377_0_, EnumCreatureAttribute p_152377_1_)
    {
        enchantmentModifierLiving.livingModifier = 0.0F;
        enchantmentModifierLiving.entityLiving = p_152377_1_;
        applyEnchantmentModifier(enchantmentModifierLiving, p_152377_0_);
        return enchantmentModifierLiving.livingModifier;
    }

    public static void func_151384_a(EntityLivingBase p_151384_0_, Entity p_151384_1_)
    {
        field_151388_d.field_151363_b = p_151384_1_;
        field_151388_d.field_151364_a = p_151384_0_;
        applyEnchantmentModifierArray(field_151388_d, p_151384_0_.getLastActiveItems());

        if (p_151384_1_ instanceof EntityPlayer)
        {
            applyEnchantmentModifier(field_151388_d, p_151384_0_.getHeldItem());
        }
    }

    public static void func_151385_b(EntityLivingBase p_151385_0_, Entity p_151385_1_)
    {
        field_151389_e.field_151366_a = p_151385_0_;
        field_151389_e.field_151365_b = p_151385_1_;
        applyEnchantmentModifierArray(field_151389_e, p_151385_0_.getLastActiveItems());

        if (p_151385_0_ instanceof EntityPlayer)
        {
            applyEnchantmentModifier(field_151389_e, p_151385_0_.getHeldItem());
        }
    }

    /**
     * Returns the knockback value of enchantments on equipped player item.
     */
    public static int getKnockbackModifier(EntityLivingBase p_77507_0_, EntityLivingBase p_77507_1_)
    {
        /**
         * Returns the level of enchantment on the ItemStack passed.
         */
        return getEnchantmentLevel(Enchantment.knockback.effectId, p_77507_0_.getHeldItem());
    }

    public static int getFireAspectModifier(EntityLivingBase p_90036_0_)
    {
        /**
         * Returns the level of enchantment on the ItemStack passed.
         */
        return getEnchantmentLevel(Enchantment.fireAspect.effectId, p_90036_0_.getHeldItem());
    }

    /**
     * Returns the 'Water Breathing' modifier of enchantments on player equipped armors.
     */
    public static int getRespiration(EntityLivingBase p_77501_0_)
    {
        /**
         * Returns the biggest level of the enchantment on the array of ItemStack passed.
         */
        return getMaxEnchantmentLevel(Enchantment.respiration.effectId, p_77501_0_.getLastActiveItems());
    }

    /**
     * Return the extra efficiency of tools based on enchantments on equipped player item.
     */
    public static int getEfficiencyModifier(EntityLivingBase p_77509_0_)
    {
        /**
         * Returns the level of enchantment on the ItemStack passed.
         */
        return getEnchantmentLevel(Enchantment.efficiency.effectId, p_77509_0_.getHeldItem());
    }

    /**
     * Returns the silk touch status of enchantments on current equipped item of player.
     */
    public static boolean getSilkTouchModifier(EntityLivingBase p_77502_0_)
    {
        /**
         * Returns the level of enchantment on the ItemStack passed.
         */
        return getEnchantmentLevel(Enchantment.silkTouch.effectId, p_77502_0_.getHeldItem()) > 0;
    }

    /**
     * Returns the fortune enchantment modifier of the current equipped item of player.
     */
    public static int getFortuneModifier(EntityLivingBase p_77517_0_)
    {
        /**
         * Returns the level of enchantment on the ItemStack passed.
         */
        return getEnchantmentLevel(Enchantment.fortune.effectId, p_77517_0_.getHeldItem());
    }

    public static int func_151386_g(EntityLivingBase p_151386_0_)
    {
        /**
         * Returns the level of enchantment on the ItemStack passed.
         */
        return getEnchantmentLevel(Enchantment.field_151370_z.effectId, p_151386_0_.getHeldItem());
    }

    public static int func_151387_h(EntityLivingBase p_151387_0_)
    {
        /**
         * Returns the level of enchantment on the ItemStack passed.
         */
        return getEnchantmentLevel(Enchantment.field_151369_A.effectId, p_151387_0_.getHeldItem());
    }

    /**
     * Returns the looting enchantment modifier of the current equipped item of player.
     */
    public static int getLootingModifier(EntityLivingBase p_77519_0_)
    {
        /**
         * Returns the level of enchantment on the ItemStack passed.
         */
        return getEnchantmentLevel(Enchantment.looting.effectId, p_77519_0_.getHeldItem());
    }

    /**
     * Returns the aqua affinity status of enchantments on current equipped item of player.
     */
    public static boolean getAquaAffinityModifier(EntityLivingBase p_77510_0_)
    {
        /**
         * Returns the biggest level of the enchantment on the array of ItemStack passed.
         */
        return getMaxEnchantmentLevel(Enchantment.aquaAffinity.effectId, p_77510_0_.getLastActiveItems()) > 0;
    }

    public static ItemStack func_92099_a(Enchantment p_92099_0_, EntityLivingBase p_92099_1_)
    {
        ItemStack[] aitemstack = p_92099_1_.getLastActiveItems();
        int i = aitemstack.length;

        for (int j = 0; j < i; ++j)
        {
            ItemStack itemstack = aitemstack[j];

            if (itemstack != null && getEnchantmentLevel(p_92099_0_.effectId, itemstack) > 0)
            {
                return itemstack;
            }
        }

        return null;
    }

    /**
     * Returns the enchantability of itemstack, it's uses a singular formula for each index (2nd parameter: 0, 1 and 2),
     * cutting to the max enchantability power of the table (3rd parameter)
     */
    public static int calcItemStackEnchantability(Random p_77514_0_, int p_77514_1_, int p_77514_2_, ItemStack p_77514_3_)
    {
        Item item = p_77514_3_.getItem();
        int k = item.getItemEnchantability(p_77514_3_);

        if (k <= 0)
        {
            return 0;
        }
        else
        {
            if (p_77514_2_ > 15)
            {
                p_77514_2_ = 15;
            }

            int l = p_77514_0_.nextInt(8) + 1 + (p_77514_2_ >> 1) + p_77514_0_.nextInt(p_77514_2_ + 1);
            return p_77514_1_ == 0 ? Math.max(l / 3, 1) : (p_77514_1_ == 1 ? l * 2 / 3 + 1 : Math.max(l, p_77514_2_ * 2));
        }
    }

    /**
     * Adds a random enchantment to the specified item. Args: random, itemStack, enchantabilityLevel
     */
    public static ItemStack addRandomEnchantment(Random p_77504_0_, ItemStack p_77504_1_, int p_77504_2_)
    {
        List list = buildEnchantmentList(p_77504_0_, p_77504_1_, p_77504_2_);
        boolean flag = p_77504_1_.getItem() == Items.book;

        if (flag)
        {
            p_77504_1_.func_150996_a(Items.enchanted_book);
        }

        if (list != null)
        {
            Iterator iterator = list.iterator();

            while (iterator.hasNext())
            {
                EnchantmentData enchantmentdata = (EnchantmentData)iterator.next();

                if (flag)
                {
                    Items.enchanted_book.addEnchantment(p_77504_1_, enchantmentdata);
                }
                else
                {
                    p_77504_1_.addEnchantment(enchantmentdata.enchantmentobj, enchantmentdata.enchantmentLevel);
                }
            }
        }

        return p_77504_1_;
    }

    /**
     * Create a list of random EnchantmentData (enchantments) that can be added together to the ItemStack, the 3rd
     * parameter is the total enchantability level.
     */
    public static List buildEnchantmentList(Random p_77513_0_, ItemStack p_77513_1_, int p_77513_2_)
    {
        Item item = p_77513_1_.getItem();
        int j = item.getItemEnchantability(p_77513_1_);

        if (j <= 0)
        {
            return null;
        }
        else
        {
            j /= 2;
            j = 1 + p_77513_0_.nextInt((j >> 1) + 1) + p_77513_0_.nextInt((j >> 1) + 1);
            int k = j + p_77513_2_;
            float f = (p_77513_0_.nextFloat() + p_77513_0_.nextFloat() - 1.0F) * 0.15F;
            int l = (int)((float)k * (1.0F + f) + 0.5F);

            if (l < 1)
            {
                l = 1;
            }

            ArrayList arraylist = null;
            Map map = mapEnchantmentData(l, p_77513_1_);

            if (map != null && !map.isEmpty())
            {
                EnchantmentData enchantmentdata = (EnchantmentData)WeightedRandom.getRandomItem(p_77513_0_, map.values());

                if (enchantmentdata != null)
                {
                    arraylist = new ArrayList();
                    arraylist.add(enchantmentdata);

                    for (int i1 = l; p_77513_0_.nextInt(50) <= i1; i1 >>= 1)
                    {
                        Iterator iterator = map.keySet().iterator();

                        while (iterator.hasNext())
                        {
                            Integer integer = (Integer)iterator.next();
                            boolean flag = true;
                            Iterator iterator1 = arraylist.iterator();

                            while (true)
                            {
                                if (iterator1.hasNext())
                                {
                                    EnchantmentData enchantmentdata1 = (EnchantmentData)iterator1.next();

                                    Enchantment e1 = enchantmentdata1.enchantmentobj;
                                    Enchantment e2 = Enchantment.enchantmentsList[integer.intValue()];
                                    if (e1.canApplyTogether(e2) && e2.canApplyTogether(e1))  //Forge BugFix: Let Both enchantments veto being together
                                    {
                                        continue;
                                    }

                                    flag = false;
                                }

                                if (!flag)
                                {
                                    iterator.remove();
                                }

                                break;
                            }
                        }

                        if (!map.isEmpty())
                        {
                            EnchantmentData enchantmentdata2 = (EnchantmentData)WeightedRandom.getRandomItem(p_77513_0_, map.values());
                            arraylist.add(enchantmentdata2);
                        }
                    }
                }
            }

            return arraylist;
        }
    }

    /**
     * Creates a 'Map' of EnchantmentData (enchantments) possible to add on the ItemStack and the enchantability level
     * passed.
     */
    public static Map mapEnchantmentData(int p_77505_0_, ItemStack p_77505_1_)
    {
        Item item = p_77505_1_.getItem();
        HashMap hashmap = null;
        boolean flag = p_77505_1_.getItem() == Items.book;
        Enchantment[] aenchantment = Enchantment.enchantmentsList;
        int j = aenchantment.length;

        for (int k = 0; k < j; ++k)
        {
            Enchantment enchantment = aenchantment[k];

            if (enchantment == null) continue;
            if (enchantment.canApplyAtEnchantingTable(p_77505_1_) || ((item == Items.book) && enchantment.isAllowedOnBooks()))
            {
                for (int l = enchantment.getMinLevel(); l <= enchantment.getMaxLevel(); ++l)
                {
                    if (p_77505_0_ >= enchantment.getMinEnchantability(l) && p_77505_0_ <= enchantment.getMaxEnchantability(l))
                    {
                        if (hashmap == null)
                        {
                            hashmap = new HashMap();
                        }

                        hashmap.put(Integer.valueOf(enchantment.effectId), new EnchantmentData(enchantment, l));
                    }
                }
            }
        }

        return hashmap;
    }

    static final class DamageIterator implements EnchantmentHelper.IModifier
        {
            public EntityLivingBase field_151366_a;
            public Entity field_151365_b;
            private static final String __OBFID = "CL_00000109";

            private DamageIterator() {}

            /**
             * Generic method use to calculate modifiers of offensive or defensive enchantment values.
             */
            public void calculateModifier(Enchantment p_77493_1_, int p_77493_2_)
            {
                p_77493_1_.func_151368_a(this.field_151366_a, this.field_151365_b, p_77493_2_);
            }

            DamageIterator(Object p_i45359_1_)
            {
                this();
            }
        }

    static final class HurtIterator implements EnchantmentHelper.IModifier
        {
            public EntityLivingBase field_151364_a;
            public Entity field_151363_b;
            private static final String __OBFID = "CL_00000110";

            private HurtIterator() {}

            /**
             * Generic method use to calculate modifiers of offensive or defensive enchantment values.
             */
            public void calculateModifier(Enchantment p_77493_1_, int p_77493_2_)
            {
                p_77493_1_.func_151367_b(this.field_151364_a, this.field_151363_b, p_77493_2_);
            }

            HurtIterator(Object p_i45360_1_)
            {
                this();
            }
        }

    interface IModifier
    {
        /**
         * Generic method use to calculate modifiers of offensive or defensive enchantment values.
         */
        void calculateModifier(Enchantment p_77493_1_, int p_77493_2_);
    }

    static final class ModifierDamage implements EnchantmentHelper.IModifier
        {
            /**
             * Used to calculate the damage modifier (extra armor) on enchantments that the player have on equipped
             * armors.
             */
            public int damageModifier;
            /**
             * Used as parameter to calculate the damage modifier (extra armor) on enchantments that the player have on
             * equipped armors.
             */
            public DamageSource source;
            private static final String __OBFID = "CL_00000114";

            private ModifierDamage() {}

            /**
             * Generic method use to calculate modifiers of offensive or defensive enchantment values.
             */
            public void calculateModifier(Enchantment p_77493_1_, int p_77493_2_)
            {
                this.damageModifier += p_77493_1_.calcModifierDamage(p_77493_2_, this.source);
            }

            ModifierDamage(Object p_i1929_1_)
            {
                this();
            }
        }

    static final class ModifierLiving implements EnchantmentHelper.IModifier
        {
            /** Used to calculate the (magic) extra damage based on enchantments of current equipped player item. */
            public float livingModifier;
            /**
             * Used as parameter to calculate the (magic) extra damage based on enchantments of current equipped player
             * item.
             */
            public EnumCreatureAttribute entityLiving;
            private static final String __OBFID = "CL_00000112";

            private ModifierLiving() {}

            /**
             * Generic method use to calculate modifiers of offensive or defensive enchantment values.
             */
            public void calculateModifier(Enchantment p_77493_1_, int p_77493_2_)
            {
                this.livingModifier += p_77493_1_.func_152376_a(p_77493_2_, this.entityLiving);
            }

            ModifierLiving(Object p_i1928_1_)
            {
                this();
            }
        }
}