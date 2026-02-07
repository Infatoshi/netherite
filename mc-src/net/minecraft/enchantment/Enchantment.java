package net.minecraft.enchantment;

import java.util.ArrayList;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.EnumCreatureAttribute;
import net.minecraft.item.ItemStack;
import net.minecraft.util.DamageSource;
import net.minecraft.util.StatCollector;

public abstract class Enchantment
{
    public static final Enchantment[] enchantmentsList = new Enchantment[256];
    /** The list of enchantments applicable by the anvil from a book */
    public static final Enchantment[] enchantmentsBookList;
    /** Converts environmental damage to armour damage */
    public static final Enchantment protection = new EnchantmentProtection(0, 10, 0);
    /** Protection against fire */
    public static final Enchantment fireProtection = new EnchantmentProtection(1, 5, 1);
    /** Less fall damage */
    public static final Enchantment featherFalling = new EnchantmentProtection(2, 5, 2);
    /** Protection against explosions */
    public static final Enchantment blastProtection = new EnchantmentProtection(3, 2, 3);
    /** Protection against projectile entities (e.g. arrows) */
    public static final Enchantment projectileProtection = new EnchantmentProtection(4, 5, 4);
    /** Decreases the rate of air loss underwater; increases time between damage while suffocating */
    public static final Enchantment respiration = new EnchantmentOxygen(5, 2);
    /** Increases underwater mining rate */
    public static final Enchantment aquaAffinity = new EnchantmentWaterWorker(6, 2);
    public static final Enchantment thorns = new EnchantmentThorns(7, 1);
    /** Extra damage to mobs */
    public static final Enchantment sharpness = new EnchantmentDamage(16, 10, 0);
    /** Extra damage to zombies, zombie pigmen and skeletons */
    public static final Enchantment smite = new EnchantmentDamage(17, 5, 1);
    /** Extra damage to spiders, cave spiders and silverfish */
    public static final Enchantment baneOfArthropods = new EnchantmentDamage(18, 5, 2);
    /** Knocks mob and players backwards upon hit */
    public static final Enchantment knockback = new EnchantmentKnockback(19, 5);
    /** Lights mobs on fire */
    public static final Enchantment fireAspect = new EnchantmentFireAspect(20, 2);
    /** Mobs have a chance to drop more loot */
    public static final Enchantment looting = new EnchantmentLootBonus(21, 2, EnumEnchantmentType.weapon);
    /** Faster resource gathering while in use */
    public static final Enchantment efficiency = new EnchantmentDigging(32, 10);
    /**
     * Blocks mined will drop themselves, even if it should drop something else (e.g. stone will drop stone, not
     * cobblestone)
     */
    public static final Enchantment silkTouch = new EnchantmentUntouching(33, 1);
    /** Sometimes, the tool's durability will not be spent when the tool is used */
    public static final Enchantment unbreaking = new EnchantmentDurability(34, 5);
    /** Can multiply the drop rate of items from blocks */
    public static final Enchantment fortune = new EnchantmentLootBonus(35, 2, EnumEnchantmentType.digger);
    /** Power enchantment for bows, add's extra damage to arrows. */
    public static final Enchantment power = new EnchantmentArrowDamage(48, 10);
    /** Knockback enchantments for bows, the arrows will knockback the target when hit. */
    public static final Enchantment punch = new EnchantmentArrowKnockback(49, 2);
    /** Flame enchantment for bows. Arrows fired by the bow will be on fire. Any target hit will also set on fire. */
    public static final Enchantment flame = new EnchantmentArrowFire(50, 2);
    /**
     * Infinity enchantment for bows. The bow will not consume arrows anymore, but will still required at least one
     * arrow on inventory use the bow.
     */
    public static final Enchantment infinity = new EnchantmentArrowInfinite(51, 1);
    public static final Enchantment field_151370_z = new EnchantmentLootBonus(61, 2, EnumEnchantmentType.fishing_rod);
    public static final Enchantment field_151369_A = new EnchantmentFishingSpeed(62, 2, EnumEnchantmentType.fishing_rod);
    public final int effectId;
    private final int weight;
    /** The EnumEnchantmentType given to this Enchantment. */
    public EnumEnchantmentType type;
    /** Used in localisation and stats. */
    protected String name;
    private static final String __OBFID = "CL_00000105";

    protected Enchantment(int p_i1926_1_, int p_i1926_2_, EnumEnchantmentType p_i1926_3_)
    {
        this.effectId = p_i1926_1_;
        this.weight = p_i1926_2_;
        this.type = p_i1926_3_;

        if (enchantmentsList[p_i1926_1_] != null)
        {
            throw new IllegalArgumentException("Duplicate enchantment id! " + this.getClass() + " and " + enchantmentsList[p_i1926_1_].getClass() + " Enchantment ID:" + p_i1926_1_);
        }
        else
        {
            enchantmentsList[p_i1926_1_] = this;
        }
    }

    public int getWeight()
    {
        return this.weight;
    }

    /**
     * Returns the minimum level that the enchantment can have.
     */
    public int getMinLevel()
    {
        return 1;
    }

    /**
     * Returns the maximum level that the enchantment can have.
     */
    public int getMaxLevel()
    {
        return 1;
    }

    /**
     * Returns the minimal value of enchantability needed on the enchantment level passed.
     */
    public int getMinEnchantability(int p_77321_1_)
    {
        return 1 + p_77321_1_ * 10;
    }

    /**
     * Returns the maximum value of enchantability nedded on the enchantment level passed.
     */
    public int getMaxEnchantability(int p_77317_1_)
    {
        return this.getMinEnchantability(p_77317_1_) + 5;
    }

    /**
     * Calculates de damage protection of the enchantment based on level and damage source passed.
     */
    public int calcModifierDamage(int p_77318_1_, DamageSource p_77318_2_)
    {
        return 0;
    }

    public float func_152376_a(int p_152376_1_, EnumCreatureAttribute p_152376_2_)
    {
        return 0.0F;
    }

    /**
     * Determines if the enchantment passed can be applyied together with this enchantment.
     */
    public boolean canApplyTogether(Enchantment p_77326_1_)
    {
        return this != p_77326_1_;
    }

    /**
     * Sets the enchantment name
     */
    public Enchantment setName(String p_77322_1_)
    {
        this.name = p_77322_1_;
        return this;
    }

    /**
     * Return the name of key in translation table of this enchantment.
     */
    public String getName()
    {
        return "enchantment." + this.name;
    }

    /**
     * Returns the correct traslated name of the enchantment and the level in roman numbers.
     */
    public String getTranslatedName(int p_77316_1_)
    {
        String s = StatCollector.translateToLocal(this.getName());
        return s + " " + StatCollector.translateToLocal("enchantment.level." + p_77316_1_);
    }

    public boolean canApply(ItemStack p_92089_1_)
    {
        return this.type.canEnchantItem(p_92089_1_.getItem());
    }

    public void func_151368_a(EntityLivingBase p_151368_1_, Entity p_151368_2_, int p_151368_3_) {}

    public void func_151367_b(EntityLivingBase p_151367_1_, Entity p_151367_2_, int p_151367_3_) {}

    /**
     * This applies specifically to applying at the enchanting table. The other method {@link #canApply(ItemStack)}
     * applies for <i>all possible</i> enchantments.
     * @param stack
     * @return
     */
    public boolean canApplyAtEnchantingTable(ItemStack stack)
    {
        return canApply(stack);
    }

    private static final java.lang.reflect.Field bookSetter = Enchantment.class.getDeclaredFields()[1];
    /**
     * Add to the list of enchantments applicable by the anvil from a book
     *
     * @param enchantment
     */
    public static void addToBookList(Enchantment enchantment)
    {
        try
        {
            net.minecraftforge.common.util.EnumHelper.setFailsafeFieldValue(bookSetter, null,
                com.google.common.collect.ObjectArrays.concat(enchantmentsBookList, enchantment));
        }
        catch (Exception e)
        {
            throw new RuntimeException(e); //Rethrow see what happens
        }
    }

    /**
     * Is this enchantment allowed to be enchanted on books via Enchantment Table
     * @return false to disable the vanilla feature
     */
    public boolean isAllowedOnBooks()
    {
        return true;
    }

    static
    {
        ArrayList var0 = new ArrayList();
        Enchantment[] var1 = enchantmentsList;
        int var2 = var1.length;

        for (int var3 = 0; var3 < var2; ++var3)
        {
            Enchantment var4 = var1[var3];

            if (var4 != null)
            {
                var0.add(var4);
            }
        }

        enchantmentsBookList = (Enchantment[])var0.toArray(new Enchantment[0]);
    }
}