package net.minecraft.scoreboard;

import java.util.Iterator;
import java.util.List;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.util.MathHelper;

public class ScoreHealthCriteria extends ScoreDummyCriteria
{
    private static final String __OBFID = "CL_00000623";

    public ScoreHealthCriteria(String p_i2312_1_)
    {
        super(p_i2312_1_);
    }

    public int func_96635_a(List p_96635_1_)
    {
        float f = 0.0F;
        EntityPlayer entityplayer;

        for (Iterator iterator = p_96635_1_.iterator(); iterator.hasNext(); f += entityplayer.getHealth() + entityplayer.getAbsorptionAmount())
        {
            entityplayer = (EntityPlayer)iterator.next();
        }

        if (p_96635_1_.size() > 0)
        {
            f /= (float)p_96635_1_.size();
        }

        return MathHelper.ceiling_float_int(f);
    }

    public boolean isReadOnly()
    {
        return true;
    }
}