package net.minecraft.potion;

import com.google.common.collect.Maps;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.Iterator;
import java.util.Map;
import java.util.UUID;
import java.util.Map.Entry;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.SharedMonsterAttributes;
import net.minecraft.entity.ai.attributes.AttributeModifier;
import net.minecraft.entity.ai.attributes.BaseAttributeMap;
import net.minecraft.entity.ai.attributes.IAttribute;
import net.minecraft.entity.ai.attributes.IAttributeInstance;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.util.DamageSource;
import net.minecraft.util.StringUtils;

public class Potion
{
    /** The array of potion types. */
    public static final Potion[] potionTypes = new Potion[32];
    public static final Potion field_76423_b = null;
    public static final Potion moveSpeed = (new Potion(1, false, 8171462)).setPotionName("potion.moveSpeed").setIconIndex(0, 0).func_111184_a(SharedMonsterAttributes.movementSpeed, "91AEAA56-376B-4498-935B-2F7F68070635", 0.20000000298023224D, 2);
    public static final Potion moveSlowdown = (new Potion(2, true, 5926017)).setPotionName("potion.moveSlowdown").setIconIndex(1, 0).func_111184_a(SharedMonsterAttributes.movementSpeed, "7107DE5E-7CE8-4030-940E-514C1F160890", -0.15000000596046448D, 2);
    public static final Potion digSpeed = (new Potion(3, false, 14270531)).setPotionName("potion.digSpeed").setIconIndex(2, 0).setEffectiveness(1.5D);
    public static final Potion digSlowdown = (new Potion(4, true, 4866583)).setPotionName("potion.digSlowDown").setIconIndex(3, 0);
    public static final Potion damageBoost = (new PotionAttackDamage(5, false, 9643043)).setPotionName("potion.damageBoost").setIconIndex(4, 0).func_111184_a(SharedMonsterAttributes.attackDamage, "648D7064-6A60-4F59-8ABE-C2C23A6DD7A9", 3.0D, 2);
    public static final Potion heal = (new PotionHealth(6, false, 16262179)).setPotionName("potion.heal");
    public static final Potion harm = (new PotionHealth(7, true, 4393481)).setPotionName("potion.harm");
    public static final Potion jump = (new Potion(8, false, 7889559)).setPotionName("potion.jump").setIconIndex(2, 1);
    public static final Potion confusion = (new Potion(9, true, 5578058)).setPotionName("potion.confusion").setIconIndex(3, 1).setEffectiveness(0.25D);
    /** The regeneration Potion object. */
    public static final Potion regeneration = (new Potion(10, false, 13458603)).setPotionName("potion.regeneration").setIconIndex(7, 0).setEffectiveness(0.25D);
    public static final Potion resistance = (new Potion(11, false, 10044730)).setPotionName("potion.resistance").setIconIndex(6, 1);
    /** The fire resistance Potion object. */
    public static final Potion fireResistance = (new Potion(12, false, 14981690)).setPotionName("potion.fireResistance").setIconIndex(7, 1);
    /** The water breathing Potion object. */
    public static final Potion waterBreathing = (new Potion(13, false, 3035801)).setPotionName("potion.waterBreathing").setIconIndex(0, 2);
    /** The invisibility Potion object. */
    public static final Potion invisibility = (new Potion(14, false, 8356754)).setPotionName("potion.invisibility").setIconIndex(0, 1);
    /** The blindness Potion object. */
    public static final Potion blindness = (new Potion(15, true, 2039587)).setPotionName("potion.blindness").setIconIndex(5, 1).setEffectiveness(0.25D);
    /** The night vision Potion object. */
    public static final Potion nightVision = (new Potion(16, false, 2039713)).setPotionName("potion.nightVision").setIconIndex(4, 1);
    /** The hunger Potion object. */
    public static final Potion hunger = (new Potion(17, true, 5797459)).setPotionName("potion.hunger").setIconIndex(1, 1);
    /** The weakness Potion object. */
    public static final Potion weakness = (new PotionAttackDamage(18, true, 4738376)).setPotionName("potion.weakness").setIconIndex(5, 0).func_111184_a(SharedMonsterAttributes.attackDamage, "22653B89-116E-49DC-9B6B-9971489B5BE5", 2.0D, 0);
    /** The poison Potion object. */
    public static final Potion poison = (new Potion(19, true, 5149489)).setPotionName("potion.poison").setIconIndex(6, 0).setEffectiveness(0.25D);
    /** The wither Potion object. */
    public static final Potion wither = (new Potion(20, true, 3484199)).setPotionName("potion.wither").setIconIndex(1, 2).setEffectiveness(0.25D);
    public static final Potion field_76434_w = (new PotionHealthBoost(21, false, 16284963)).setPotionName("potion.healthBoost").setIconIndex(2, 2).func_111184_a(SharedMonsterAttributes.maxHealth, "5D6F0BA2-1186-46AC-B896-C61C5CEE99CC", 4.0D, 0);
    public static final Potion field_76444_x = (new PotionAbsoption(22, false, 2445989)).setPotionName("potion.absorption").setIconIndex(2, 2);
    public static final Potion field_76443_y = (new PotionHealth(23, false, 16262179)).setPotionName("potion.saturation");
    public static final Potion field_76442_z = null;
    public static final Potion field_76409_A = null;
    public static final Potion field_76410_B = null;
    public static final Potion field_76411_C = null;
    public static final Potion field_76405_D = null;
    public static final Potion field_76406_E = null;
    public static final Potion field_76407_F = null;
    public static final Potion field_76408_G = null;
    /** The Id of a Potion object. */
    public final int id;
    private final Map field_111188_I = Maps.newHashMap();
    /** This field indicated if the effect is 'bad' - negative - for the entity. */
    private final boolean isBadEffect;
    /** Is the color of the liquid for this potion. */
    private final int liquidColor;
    /** The name of the Potion. */
    private String name = "";
    /** The index for the icon displayed when the potion effect is active. */
    private int statusIconIndex = -1;
    private double effectiveness;
    private boolean usable;
    private static final String __OBFID = "CL_00001528";

    protected Potion(int p_i1573_1_, boolean p_i1573_2_, int p_i1573_3_)
    {
        this.id = p_i1573_1_;
        potionTypes[p_i1573_1_] = this;
        this.isBadEffect = p_i1573_2_;

        if (p_i1573_2_)
        {
            this.effectiveness = 0.5D;
        }
        else
        {
            this.effectiveness = 1.0D;
        }

        this.liquidColor = p_i1573_3_;
    }

    /**
     * Sets the index for the icon displayed in the player's inventory when the status is active.
     */
    protected Potion setIconIndex(int p_76399_1_, int p_76399_2_)
    {
        this.statusIconIndex = p_76399_1_ + p_76399_2_ * 8;
        return this;
    }

    /**
     * returns the ID of the potion
     */
    public int getId()
    {
        return this.id;
    }

    public void performEffect(EntityLivingBase p_76394_1_, int p_76394_2_)
    {
        if (this.id == regeneration.id)
        {
            if (p_76394_1_.getHealth() < p_76394_1_.getMaxHealth())
            {
                p_76394_1_.heal(1.0F);
            }
        }
        else if (this.id == poison.id)
        {
            if (p_76394_1_.getHealth() > 1.0F)
            {
                p_76394_1_.attackEntityFrom(DamageSource.magic, 1.0F);
            }
        }
        else if (this.id == wither.id)
        {
            p_76394_1_.attackEntityFrom(DamageSource.wither, 1.0F);
        }
        else if (this.id == hunger.id && p_76394_1_ instanceof EntityPlayer)
        {
            ((EntityPlayer)p_76394_1_).addExhaustion(0.025F * (float)(p_76394_2_ + 1));
        }
        else if (this.id == field_76443_y.id && p_76394_1_ instanceof EntityPlayer)
        {
            if (!p_76394_1_.worldObj.isRemote)
            {
                ((EntityPlayer)p_76394_1_).getFoodStats().addStats(p_76394_2_ + 1, 1.0F);
            }
        }
        else if ((this.id != heal.id || p_76394_1_.isEntityUndead()) && (this.id != harm.id || !p_76394_1_.isEntityUndead()))
        {
            if (this.id == harm.id && !p_76394_1_.isEntityUndead() || this.id == heal.id && p_76394_1_.isEntityUndead())
            {
                p_76394_1_.attackEntityFrom(DamageSource.magic, (float)(6 << p_76394_2_));
            }
        }
        else
        {
            p_76394_1_.heal((float)Math.max(4 << p_76394_2_, 0));
        }
    }

    /**
     * Hits the provided entity with this potion's instant effect.
     */
    public void affectEntity(EntityLivingBase p_76402_1_, EntityLivingBase p_76402_2_, int p_76402_3_, double p_76402_4_)
    {
        int j;

        if ((this.id != heal.id || p_76402_2_.isEntityUndead()) && (this.id != harm.id || !p_76402_2_.isEntityUndead()))
        {
            if (this.id == harm.id && !p_76402_2_.isEntityUndead() || this.id == heal.id && p_76402_2_.isEntityUndead())
            {
                j = (int)(p_76402_4_ * (double)(6 << p_76402_3_) + 0.5D);

                if (p_76402_1_ == null)
                {
                    p_76402_2_.attackEntityFrom(DamageSource.magic, (float)j);
                }
                else
                {
                    p_76402_2_.attackEntityFrom(DamageSource.causeIndirectMagicDamage(p_76402_2_, p_76402_1_), (float)j);
                }
            }
        }
        else
        {
            j = (int)(p_76402_4_ * (double)(4 << p_76402_3_) + 0.5D);
            p_76402_2_.heal((float)j);
        }
    }

    /**
     * Returns true if the potion has an instant effect instead of a continuous one (eg Harming)
     */
    public boolean isInstant()
    {
        return false;
    }

    /**
     * checks if Potion effect is ready to be applied this tick.
     */
    public boolean isReady(int p_76397_1_, int p_76397_2_)
    {
        int k;

        if (this.id == regeneration.id)
        {
            k = 50 >> p_76397_2_;
            return k > 0 ? p_76397_1_ % k == 0 : true;
        }
        else if (this.id == poison.id)
        {
            k = 25 >> p_76397_2_;
            return k > 0 ? p_76397_1_ % k == 0 : true;
        }
        else if (this.id == wither.id)
        {
            k = 40 >> p_76397_2_;
            return k > 0 ? p_76397_1_ % k == 0 : true;
        }
        else
        {
            return this.id == hunger.id;
        }
    }

    /**
     * Set the potion name.
     */
    public Potion setPotionName(String p_76390_1_)
    {
        this.name = p_76390_1_;
        return this;
    }

    /**
     * returns the name of the potion
     */
    public String getName()
    {
        return this.name;
    }

    protected Potion setEffectiveness(double p_76404_1_)
    {
        this.effectiveness = p_76404_1_;
        return this;
    }

    /**
     * Returns true if the potion has a associated status icon to display in then inventory when active.
     */
    @SideOnly(Side.CLIENT)
    public boolean hasStatusIcon()
    {
        return this.statusIconIndex >= 0;
    }

    /**
     * Returns the index for the icon to display when the potion is active.
     */
    @SideOnly(Side.CLIENT)
    public int getStatusIconIndex()
    {
        return this.statusIconIndex;
    }

    /**
     * This method returns true if the potion effect is bad - negative - for the entity.
     */
    @SideOnly(Side.CLIENT)
    public boolean isBadEffect()
    {
        return this.isBadEffect;
    }

    @SideOnly(Side.CLIENT)
    public static String getDurationString(PotionEffect p_76389_0_)
    {
        if (p_76389_0_.getIsPotionDurationMax())
        {
            return "**:**";
        }
        else
        {
            int i = p_76389_0_.getDuration();
            return StringUtils.ticksToElapsedTime(i);
        }
    }

    public double getEffectiveness()
    {
        return this.effectiveness;
    }

    public boolean isUsable()
    {
        return this.usable;
    }

    /**
     * Returns the color of the potion liquid.
     */
    public int getLiquidColor()
    {
        return this.liquidColor;
    }

    public Potion func_111184_a(IAttribute p_111184_1_, String p_111184_2_, double p_111184_3_, int p_111184_5_)
    {
        AttributeModifier attributemodifier = new AttributeModifier(UUID.fromString(p_111184_2_), this.getName(), p_111184_3_, p_111184_5_);
        this.field_111188_I.put(p_111184_1_, attributemodifier);
        return this;
    }

    public void removeAttributesModifiersFromEntity(EntityLivingBase p_111187_1_, BaseAttributeMap p_111187_2_, int p_111187_3_)
    {
        Iterator iterator = this.field_111188_I.entrySet().iterator();

        while (iterator.hasNext())
        {
            Entry entry = (Entry)iterator.next();
            IAttributeInstance iattributeinstance = p_111187_2_.getAttributeInstance((IAttribute)entry.getKey());

            if (iattributeinstance != null)
            {
                iattributeinstance.removeModifier((AttributeModifier)entry.getValue());
            }
        }
    }

    @SideOnly(Side.CLIENT)
    public Map func_111186_k()
    {
        return this.field_111188_I;
    }

    public void applyAttributesModifiersToEntity(EntityLivingBase p_111185_1_, BaseAttributeMap p_111185_2_, int p_111185_3_)
    {
        Iterator iterator = this.field_111188_I.entrySet().iterator();

        while (iterator.hasNext())
        {
            Entry entry = (Entry)iterator.next();
            IAttributeInstance iattributeinstance = p_111185_2_.getAttributeInstance((IAttribute)entry.getKey());

            if (iattributeinstance != null)
            {
                AttributeModifier attributemodifier = (AttributeModifier)entry.getValue();
                iattributeinstance.removeModifier(attributemodifier);
                iattributeinstance.applyModifier(new AttributeModifier(attributemodifier.getID(), this.getName() + " " + p_111185_3_, this.func_111183_a(p_111185_3_, attributemodifier), attributemodifier.getOperation()));
            }
        }
    }

    public double func_111183_a(int p_111183_1_, AttributeModifier p_111183_2_)
    {
        return p_111183_2_.getAmount() * (double)(p_111183_1_ + 1);
    }
    
    /* ======================================== FORGE START =====================================*/
    
    /**
     * If the standard PotionEffect text (name and duration) should be drawn when this potion is active. 
     * @param effect the active PotionEffect
     * @return true to draw the standard text
     */
    public boolean shouldRenderInvText(PotionEffect effect)
    {
        return true;
    }
    
    /**
     * Called to draw the this Potion onto the player's inventory when it's active.
     * This can be used to e.g. render Potion icons from your own texture.
     * @param x the x coordinate
     * @param y the y coordinate
     * @param effect the active PotionEffect
     * @param mc the Minecraft instance, for convenience
     */
    @SideOnly(Side.CLIENT)
    public void renderInventoryEffect(int x, int y, PotionEffect effect, net.minecraft.client.Minecraft mc) { }
}