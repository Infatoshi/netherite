package net.minecraft.potion;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;

public class PotionHelper
{
    public static final String field_77924_a = null;
    public static final String sugarEffect;
    public static final String ghastTearEffect = "+0-1-2-3&4-4+13";
    public static final String spiderEyeEffect;
    public static final String fermentedSpiderEyeEffect;
    public static final String speckledMelonEffect;
    public static final String blazePowderEffect;
    public static final String magmaCreamEffect;
    public static final String redstoneEffect;
    public static final String glowstoneEffect;
    public static final String gunpowderEffect;
    public static final String goldenCarrotEffect;
    public static final String field_151423_m;
    private static final HashMap potionRequirements = new HashMap();
    /** Potion effect amplifier map */
    private static final HashMap potionAmplifiers = new HashMap();
    private static final HashMap field_77925_n;
    /** An array of possible potion prefix names, as translation IDs. */
    private static final String[] potionPrefixes;
    private static final String __OBFID = "CL_00000078";

    /**
     * Is the bit given set to 1?
     */
    public static boolean checkFlag(int p_77914_0_, int p_77914_1_)
    {
        return (p_77914_0_ & 1 << p_77914_1_) != 0;
    }

    /**
     * Returns 1 if the flag is set, 0 if it is not set.
     */
    private static int isFlagSet(int p_77910_0_, int p_77910_1_)
    {
        /**
         * Is the bit given set to 1?
         */
        return checkFlag(p_77910_0_, p_77910_1_) ? 1 : 0;
    }

    /**
     * Returns 0 if the flag is set, 1 if it is not set.
     */
    private static int isFlagUnset(int p_77916_0_, int p_77916_1_)
    {
        /**
         * Is the bit given set to 1?
         */
        return checkFlag(p_77916_0_, p_77916_1_) ? 0 : 1;
    }

    public static int func_77909_a(int p_77909_0_)
    {
        return func_77908_a(p_77909_0_, 5, 4, 3, 2, 1);
    }

    /**
     * Given a {@link Collection}<{@link PotionEffect}> will return an Integer color.
     */
    public static int calcPotionLiquidColor(Collection p_77911_0_)
    {
        int i = 3694022;

        if (p_77911_0_ != null && !p_77911_0_.isEmpty())
        {
            float f = 0.0F;
            float f1 = 0.0F;
            float f2 = 0.0F;
            float f3 = 0.0F;
            Iterator iterator = p_77911_0_.iterator();

            while (iterator.hasNext())
            {
                PotionEffect potioneffect = (PotionEffect)iterator.next();
                int j = Potion.potionTypes[potioneffect.getPotionID()].getLiquidColor();

                for (int k = 0; k <= potioneffect.getAmplifier(); ++k)
                {
                    f += (float)(j >> 16 & 255) / 255.0F;
                    f1 += (float)(j >> 8 & 255) / 255.0F;
                    f2 += (float)(j >> 0 & 255) / 255.0F;
                    ++f3;
                }
            }

            f = f / f3 * 255.0F;
            f1 = f1 / f3 * 255.0F;
            f2 = f2 / f3 * 255.0F;
            return (int)f << 16 | (int)f1 << 8 | (int)f2;
        }
        else
        {
            return i;
        }
    }

    public static boolean func_82817_b(Collection p_82817_0_)
    {
        Iterator iterator = p_82817_0_.iterator();
        PotionEffect potioneffect;

        do
        {
            if (!iterator.hasNext())
            {
                return true;
            }

            potioneffect = (PotionEffect)iterator.next();
        }
        while (potioneffect.getIsAmbient());

        return false;
    }

    @SideOnly(Side.CLIENT)
    public static int func_77915_a(int p_77915_0_, boolean p_77915_1_)
    {
        if (!p_77915_1_)
        {
            if (field_77925_n.containsKey(Integer.valueOf(p_77915_0_)))
            {
                return ((Integer)field_77925_n.get(Integer.valueOf(p_77915_0_))).intValue();
            }
            else
            {
                int j = calcPotionLiquidColor(getPotionEffects(p_77915_0_, false));
                field_77925_n.put(Integer.valueOf(p_77915_0_), Integer.valueOf(j));
                return j;
            }
        }
        else
        {
            /**
             * Given a {@link Collection}<{@link PotionEffect}> will return an Integer color.
             */
            return calcPotionLiquidColor(getPotionEffects(p_77915_0_, p_77915_1_));
        }
    }

    public static String func_77905_c(int p_77905_0_)
    {
        int j = func_77909_a(p_77905_0_);
        return potionPrefixes[j];
    }

    private static int func_77904_a(boolean p_77904_0_, boolean p_77904_1_, boolean p_77904_2_, int p_77904_3_, int p_77904_4_, int p_77904_5_, int p_77904_6_)
    {
        int i1 = 0;

        if (p_77904_0_)
        {
            i1 = isFlagUnset(p_77904_6_, p_77904_4_);
        }
        else if (p_77904_3_ != -1)
        {
            if (p_77904_3_ == 0 && countSetFlags(p_77904_6_) == p_77904_4_)
            {
                i1 = 1;
            }
            else if (p_77904_3_ == 1 && countSetFlags(p_77904_6_) > p_77904_4_)
            {
                i1 = 1;
            }
            else if (p_77904_3_ == 2 && countSetFlags(p_77904_6_) < p_77904_4_)
            {
                i1 = 1;
            }
        }
        else
        {
            i1 = isFlagSet(p_77904_6_, p_77904_4_);
        }

        if (p_77904_1_)
        {
            i1 *= p_77904_5_;
        }

        if (p_77904_2_)
        {
            i1 *= -1;
        }

        return i1;
    }

    /**
     * Count the number of bits in an integer set to ON.
     */
    private static int countSetFlags(int p_77907_0_)
    {
        int j;

        for (j = 0; p_77907_0_ > 0; ++j)
        {
            p_77907_0_ &= p_77907_0_ - 1;
        }

        return j;
    }

    private static int parsePotionEffects(String p_77912_0_, int p_77912_1_, int p_77912_2_, int p_77912_3_)
    {
        if (p_77912_1_ < p_77912_0_.length() && p_77912_2_ >= 0 && p_77912_1_ < p_77912_2_)
        {
            int l = p_77912_0_.indexOf(124, p_77912_1_);
            int i1;
            int j2;

            if (l >= 0 && l < p_77912_2_)
            {
                i1 = parsePotionEffects(p_77912_0_, p_77912_1_, l - 1, p_77912_3_);

                if (i1 > 0)
                {
                    return i1;
                }
                else
                {
                    j2 = parsePotionEffects(p_77912_0_, l + 1, p_77912_2_, p_77912_3_);
                    return j2 > 0 ? j2 : 0;
                }
            }
            else
            {
                i1 = p_77912_0_.indexOf(38, p_77912_1_);

                if (i1 >= 0 && i1 < p_77912_2_)
                {
                    j2 = parsePotionEffects(p_77912_0_, p_77912_1_, i1 - 1, p_77912_3_);

                    if (j2 <= 0)
                    {
                        return 0;
                    }
                    else
                    {
                        int k2 = parsePotionEffects(p_77912_0_, i1 + 1, p_77912_2_, p_77912_3_);
                        return k2 <= 0 ? 0 : (j2 > k2 ? j2 : k2);
                    }
                }
                else
                {
                    boolean flag = false;
                    boolean flag1 = false;
                    boolean flag2 = false;
                    boolean flag3 = false;
                    boolean flag4 = false;
                    byte b0 = -1;
                    int j1 = 0;
                    int k1 = 0;
                    int l1 = 0;

                    for (int i2 = p_77912_1_; i2 < p_77912_2_; ++i2)
                    {
                        char c0 = p_77912_0_.charAt(i2);

                        if (c0 >= 48 && c0 <= 57)
                        {
                            if (flag)
                            {
                                k1 = c0 - 48;
                                flag1 = true;
                            }
                            else
                            {
                                j1 *= 10;
                                j1 += c0 - 48;
                                flag2 = true;
                            }
                        }
                        else if (c0 == 42)
                        {
                            flag = true;
                        }
                        else if (c0 == 33)
                        {
                            if (flag2)
                            {
                                l1 += func_77904_a(flag3, flag1, flag4, b0, j1, k1, p_77912_3_);
                                flag3 = false;
                                flag4 = false;
                                flag = false;
                                flag1 = false;
                                flag2 = false;
                                k1 = 0;
                                j1 = 0;
                                b0 = -1;
                            }

                            flag3 = true;
                        }
                        else if (c0 == 45)
                        {
                            if (flag2)
                            {
                                l1 += func_77904_a(flag3, flag1, flag4, b0, j1, k1, p_77912_3_);
                                flag3 = false;
                                flag4 = false;
                                flag = false;
                                flag1 = false;
                                flag2 = false;
                                k1 = 0;
                                j1 = 0;
                                b0 = -1;
                            }

                            flag4 = true;
                        }
                        else if (c0 != 61 && c0 != 60 && c0 != 62)
                        {
                            if (c0 == 43 && flag2)
                            {
                                l1 += func_77904_a(flag3, flag1, flag4, b0, j1, k1, p_77912_3_);
                                flag3 = false;
                                flag4 = false;
                                flag = false;
                                flag1 = false;
                                flag2 = false;
                                k1 = 0;
                                j1 = 0;
                                b0 = -1;
                            }
                        }
                        else
                        {
                            if (flag2)
                            {
                                l1 += func_77904_a(flag3, flag1, flag4, b0, j1, k1, p_77912_3_);
                                flag3 = false;
                                flag4 = false;
                                flag = false;
                                flag1 = false;
                                flag2 = false;
                                k1 = 0;
                                j1 = 0;
                                b0 = -1;
                            }

                            if (c0 == 61)
                            {
                                b0 = 0;
                            }
                            else if (c0 == 60)
                            {
                                b0 = 2;
                            }
                            else if (c0 == 62)
                            {
                                b0 = 1;
                            }
                        }
                    }

                    if (flag2)
                    {
                        l1 += func_77904_a(flag3, flag1, flag4, b0, j1, k1, p_77912_3_);
                    }

                    return l1;
                }
            }
        }
        else
        {
            return 0;
        }
    }

    /**
     * Returns a list of effects for the specified potion damage value.
     */
    public static List getPotionEffects(int p_77917_0_, boolean p_77917_1_)
    {
        ArrayList arraylist = null;
        Potion[] apotion = Potion.potionTypes;
        int j = apotion.length;

        for (int k = 0; k < j; ++k)
        {
            Potion potion = apotion[k];

            if (potion != null && (!potion.isUsable() || p_77917_1_))
            {
                String s = (String)potionRequirements.get(Integer.valueOf(potion.getId()));

                if (s != null)
                {
                    int l = parsePotionEffects(s, 0, s.length(), p_77917_0_);

                    if (l > 0)
                    {
                        int i1 = 0;
                        String s1 = (String)potionAmplifiers.get(Integer.valueOf(potion.getId()));

                        if (s1 != null)
                        {
                            i1 = parsePotionEffects(s1, 0, s1.length(), p_77917_0_);

                            if (i1 < 0)
                            {
                                i1 = 0;
                            }
                        }

                        if (potion.isInstant())
                        {
                            l = 1;
                        }
                        else
                        {
                            l = 1200 * (l * 3 + (l - 1) * 2);
                            l >>= i1;
                            l = (int)Math.round((double)l * potion.getEffectiveness());

                            if ((p_77917_0_ & 16384) != 0)
                            {
                                l = (int)Math.round((double)l * 0.75D + 0.5D);
                            }
                        }

                        if (arraylist == null)
                        {
                            arraylist = new ArrayList();
                        }

                        PotionEffect potioneffect = new PotionEffect(potion.getId(), l, i1);

                        if ((p_77917_0_ & 16384) != 0)
                        {
                            potioneffect.setSplashPotion(true);
                        }

                        arraylist.add(potioneffect);
                    }
                }
            }
        }

        return arraylist;
    }

    /**
     * Does bit operations for brewPotionData, given data, the index of the bit being operated upon, whether the bit
     * will be removed, whether the bit will be toggled (NOT), or whether the data field will be set to 0 if the bit is
     * not present.
     */
    private static int brewBitOperations(int p_77906_0_, int p_77906_1_, boolean p_77906_2_, boolean p_77906_3_, boolean p_77906_4_)
    {
        if (p_77906_4_)
        {
            if (!checkFlag(p_77906_0_, p_77906_1_))
            {
                return 0;
            }
        }
        else if (p_77906_2_)
        {
            p_77906_0_ &= ~(1 << p_77906_1_);
        }
        else if (p_77906_3_)
        {
            if ((p_77906_0_ & 1 << p_77906_1_) == 0)
            {
                p_77906_0_ |= 1 << p_77906_1_;
            }
            else
            {
                p_77906_0_ &= ~(1 << p_77906_1_);
            }
        }
        else
        {
            p_77906_0_ |= 1 << p_77906_1_;
        }

        return p_77906_0_;
    }

    /**
     * Generate a data value for a potion, given its previous data value and the encoded string of new effects it will
     * receive
     */
    public static int applyIngredient(int p_77913_0_, String p_77913_1_)
    {
        byte b0 = 0;
        int j = p_77913_1_.length();
        boolean flag = false;
        boolean flag1 = false;
        boolean flag2 = false;
        boolean flag3 = false;
        int k = 0;

        for (int l = b0; l < j; ++l)
        {
            char c0 = p_77913_1_.charAt(l);

            if (c0 >= 48 && c0 <= 57)
            {
                k *= 10;
                k += c0 - 48;
                flag = true;
            }
            else if (c0 == 33)
            {
                if (flag)
                {
                    p_77913_0_ = brewBitOperations(p_77913_0_, k, flag2, flag1, flag3);
                    flag3 = false;
                    flag1 = false;
                    flag2 = false;
                    flag = false;
                    k = 0;
                }

                flag1 = true;
            }
            else if (c0 == 45)
            {
                if (flag)
                {
                    p_77913_0_ = brewBitOperations(p_77913_0_, k, flag2, flag1, flag3);
                    flag3 = false;
                    flag1 = false;
                    flag2 = false;
                    flag = false;
                    k = 0;
                }

                flag2 = true;
            }
            else if (c0 == 43)
            {
                if (flag)
                {
                    p_77913_0_ = brewBitOperations(p_77913_0_, k, flag2, flag1, flag3);
                    flag3 = false;
                    flag1 = false;
                    flag2 = false;
                    flag = false;
                    k = 0;
                }
            }
            else if (c0 == 38)
            {
                if (flag)
                {
                    p_77913_0_ = brewBitOperations(p_77913_0_, k, flag2, flag1, flag3);
                    flag3 = false;
                    flag1 = false;
                    flag2 = false;
                    flag = false;
                    k = 0;
                }

                flag3 = true;
            }
        }

        if (flag)
        {
            p_77913_0_ = brewBitOperations(p_77913_0_, k, flag2, flag1, flag3);
        }

        return p_77913_0_ & 32767;
    }

    public static int func_77908_a(int p_77908_0_, int p_77908_1_, int p_77908_2_, int p_77908_3_, int p_77908_4_, int p_77908_5_)
    {
        return (checkFlag(p_77908_0_, p_77908_1_) ? 16 : 0) | (checkFlag(p_77908_0_, p_77908_2_) ? 8 : 0) | (checkFlag(p_77908_0_, p_77908_3_) ? 4 : 0) | (checkFlag(p_77908_0_, p_77908_4_) ? 2 : 0) | (checkFlag(p_77908_0_, p_77908_5_) ? 1 : 0);
    }

    static
    {
        potionRequirements.put(Integer.valueOf(Potion.regeneration.getId()), "0 & !1 & !2 & !3 & 0+6");
        sugarEffect = "-0+1-2-3&4-4+13";
        potionRequirements.put(Integer.valueOf(Potion.moveSpeed.getId()), "!0 & 1 & !2 & !3 & 1+6");
        magmaCreamEffect = "+0+1-2-3&4-4+13";
        potionRequirements.put(Integer.valueOf(Potion.fireResistance.getId()), "0 & 1 & !2 & !3 & 0+6");
        speckledMelonEffect = "+0-1+2-3&4-4+13";
        potionRequirements.put(Integer.valueOf(Potion.heal.getId()), "0 & !1 & 2 & !3");
        spiderEyeEffect = "-0-1+2-3&4-4+13";
        potionRequirements.put(Integer.valueOf(Potion.poison.getId()), "!0 & !1 & 2 & !3 & 2+6");
        fermentedSpiderEyeEffect = "-0+3-4+13";
        potionRequirements.put(Integer.valueOf(Potion.weakness.getId()), "!0 & !1 & !2 & 3 & 3+6");
        potionRequirements.put(Integer.valueOf(Potion.harm.getId()), "!0 & !1 & 2 & 3");
        potionRequirements.put(Integer.valueOf(Potion.moveSlowdown.getId()), "!0 & 1 & !2 & 3 & 3+6");
        blazePowderEffect = "+0-1-2+3&4-4+13";
        potionRequirements.put(Integer.valueOf(Potion.damageBoost.getId()), "0 & !1 & !2 & 3 & 3+6");
        goldenCarrotEffect = "-0+1+2-3+13&4-4";
        potionRequirements.put(Integer.valueOf(Potion.nightVision.getId()), "!0 & 1 & 2 & !3 & 2+6");
        potionRequirements.put(Integer.valueOf(Potion.invisibility.getId()), "!0 & 1 & 2 & 3 & 2+6");
        field_151423_m = "+0-1+2+3+13&4-4";
        potionRequirements.put(Integer.valueOf(Potion.waterBreathing.getId()), "0 & !1 & 2 & 3 & 2+6");
        glowstoneEffect = "+5-6-7";
        potionAmplifiers.put(Integer.valueOf(Potion.moveSpeed.getId()), "5");
        potionAmplifiers.put(Integer.valueOf(Potion.digSpeed.getId()), "5");
        potionAmplifiers.put(Integer.valueOf(Potion.damageBoost.getId()), "5");
        potionAmplifiers.put(Integer.valueOf(Potion.regeneration.getId()), "5");
        potionAmplifiers.put(Integer.valueOf(Potion.harm.getId()), "5");
        potionAmplifiers.put(Integer.valueOf(Potion.heal.getId()), "5");
        potionAmplifiers.put(Integer.valueOf(Potion.resistance.getId()), "5");
        potionAmplifiers.put(Integer.valueOf(Potion.poison.getId()), "5");
        redstoneEffect = "-5+6-7";
        gunpowderEffect = "+14&13-13";
        field_77925_n = new HashMap();
        potionPrefixes = new String[] {"potion.prefix.mundane", "potion.prefix.uninteresting", "potion.prefix.bland", "potion.prefix.clear", "potion.prefix.milky", "potion.prefix.diffuse", "potion.prefix.artless", "potion.prefix.thin", "potion.prefix.awkward", "potion.prefix.flat", "potion.prefix.bulky", "potion.prefix.bungling", "potion.prefix.buttered", "potion.prefix.smooth", "potion.prefix.suave", "potion.prefix.debonair", "potion.prefix.thick", "potion.prefix.elegant", "potion.prefix.fancy", "potion.prefix.charming", "potion.prefix.dashing", "potion.prefix.refined", "potion.prefix.cordial", "potion.prefix.sparkling", "potion.prefix.potent", "potion.prefix.foul", "potion.prefix.odorless", "potion.prefix.rank", "potion.prefix.harsh", "potion.prefix.acrid", "potion.prefix.gross", "potion.prefix.stinky"};
    }
}