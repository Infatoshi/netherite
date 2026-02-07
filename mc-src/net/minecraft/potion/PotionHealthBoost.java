package net.minecraft.potion;

import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.ai.attributes.BaseAttributeMap;

public class PotionHealthBoost extends Potion
{
    private static final String __OBFID = "CL_00001526";

    public PotionHealthBoost(int p_i1571_1_, boolean p_i1571_2_, int p_i1571_3_)
    {
        super(p_i1571_1_, p_i1571_2_, p_i1571_3_);
    }

    public void removeAttributesModifiersFromEntity(EntityLivingBase p_111187_1_, BaseAttributeMap p_111187_2_, int p_111187_3_)
    {
        super.removeAttributesModifiersFromEntity(p_111187_1_, p_111187_2_, p_111187_3_);

        if (p_111187_1_.getHealth() > p_111187_1_.getMaxHealth())
        {
            p_111187_1_.setHealth(p_111187_1_.getMaxHealth());
        }
    }
}