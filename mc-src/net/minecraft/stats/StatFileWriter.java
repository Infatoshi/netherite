package net.minecraft.stats;

import com.google.common.collect.Maps;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.Map;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.util.IJsonSerializable;
import net.minecraft.util.TupleIntJsonSerializable;

public class StatFileWriter
{
    protected final Map field_150875_a = Maps.newConcurrentMap();
    private static final String __OBFID = "CL_00001481";

    /**
     * Returns true if the achievement has been unlocked.
     */
    public boolean hasAchievementUnlocked(Achievement p_77443_1_)
    {
        return this.writeStat(p_77443_1_) > 0;
    }

    /**
     * Returns true if the parent has been unlocked, or there is no parent
     */
    public boolean canUnlockAchievement(Achievement p_77442_1_)
    {
        return p_77442_1_.parentAchievement == null || this.hasAchievementUnlocked(p_77442_1_.parentAchievement);
    }

    public void func_150871_b(EntityPlayer p_150871_1_, StatBase p_150871_2_, int p_150871_3_)
    {
        if (!p_150871_2_.isAchievement() || this.canUnlockAchievement((Achievement)p_150871_2_))
        {
            this.func_150873_a(p_150871_1_, p_150871_2_, this.writeStat(p_150871_2_) + p_150871_3_);
        }
    }

    @SideOnly(Side.CLIENT)
    public int func_150874_c(Achievement p_150874_1_)
    {
        if (this.hasAchievementUnlocked(p_150874_1_))
        {
            return 0;
        }
        else
        {
            int i = 0;

            for (Achievement achievement1 = p_150874_1_.parentAchievement; achievement1 != null && !this.hasAchievementUnlocked(achievement1); ++i)
            {
                achievement1 = achievement1.parentAchievement;
            }

            return i;
        }
    }

    public void func_150873_a(EntityPlayer p_150873_1_, StatBase p_150873_2_, int p_150873_3_)
    {
        TupleIntJsonSerializable tupleintjsonserializable = (TupleIntJsonSerializable)this.field_150875_a.get(p_150873_2_);

        if (tupleintjsonserializable == null)
        {
            tupleintjsonserializable = new TupleIntJsonSerializable();
            this.field_150875_a.put(p_150873_2_, tupleintjsonserializable);
        }

        tupleintjsonserializable.setIntegerValue(p_150873_3_);
    }

    public int writeStat(StatBase p_77444_1_)
    {
        TupleIntJsonSerializable tupleintjsonserializable = (TupleIntJsonSerializable)this.field_150875_a.get(p_77444_1_);
        return tupleintjsonserializable == null ? 0 : tupleintjsonserializable.getIntegerValue();
    }

    public IJsonSerializable func_150870_b(StatBase p_150870_1_)
    {
        TupleIntJsonSerializable tupleintjsonserializable = (TupleIntJsonSerializable)this.field_150875_a.get(p_150870_1_);
        return tupleintjsonserializable != null ? tupleintjsonserializable.getJsonSerializableValue() : null;
    }

    public IJsonSerializable func_150872_a(StatBase p_150872_1_, IJsonSerializable p_150872_2_)
    {
        TupleIntJsonSerializable tupleintjsonserializable = (TupleIntJsonSerializable)this.field_150875_a.get(p_150872_1_);

        if (tupleintjsonserializable == null)
        {
            tupleintjsonserializable = new TupleIntJsonSerializable();
            this.field_150875_a.put(p_150872_1_, tupleintjsonserializable);
        }

        tupleintjsonserializable.setJsonSerializableValue(p_150872_2_);
        return p_150872_2_;
    }
}