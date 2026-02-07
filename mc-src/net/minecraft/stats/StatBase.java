package net.minecraft.stats;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.text.DecimalFormat;
import java.text.NumberFormat;
import java.util.Locale;
import net.minecraft.event.HoverEvent;
import net.minecraft.scoreboard.IScoreObjectiveCriteria;
import net.minecraft.util.ChatComponentText;
import net.minecraft.util.EnumChatFormatting;
import net.minecraft.util.IChatComponent;

public class StatBase
{
    /** The Stat ID */
    public final String statId;
    /** The Stat name */
    private final IChatComponent statName;
    public boolean isIndependent;
    private final IStatType type;
    private final IScoreObjectiveCriteria field_150957_c;
    private Class field_150956_d;
    private static NumberFormat numberFormat = NumberFormat.getIntegerInstance(Locale.US);
    public static IStatType simpleStatType = new IStatType()
    {
        private static final String __OBFID = "CL_00001473";
        /**
         * Formats a given stat for human consumption.
         */
        @SideOnly(Side.CLIENT)
        public String format(int p_75843_1_)
        {
            return StatBase.numberFormat.format((long)p_75843_1_);
        }
    };
    private static DecimalFormat decimalFormat = new DecimalFormat("########0.00");
    public static IStatType timeStatType = new IStatType()
    {
        private static final String __OBFID = "CL_00001474";
        /**
         * Formats a given stat for human consumption.
         */
        @SideOnly(Side.CLIENT)
        public String format(int p_75843_1_)
        {
            double d0 = (double)p_75843_1_ / 20.0D;
            double d1 = d0 / 60.0D;
            double d2 = d1 / 60.0D;
            double d3 = d2 / 24.0D;
            double d4 = d3 / 365.0D;
            return d4 > 0.5D ? StatBase.decimalFormat.format(d4) + " y" : (d3 > 0.5D ? StatBase.decimalFormat.format(d3) + " d" : (d2 > 0.5D ? StatBase.decimalFormat.format(d2) + " h" : (d1 > 0.5D ? StatBase.decimalFormat.format(d1) + " m" : d0 + " s")));
        }
    };
    public static IStatType distanceStatType = new IStatType()
    {
        private static final String __OBFID = "CL_00001475";
        /**
         * Formats a given stat for human consumption.
         */
        @SideOnly(Side.CLIENT)
        public String format(int p_75843_1_)
        {
            double d0 = (double)p_75843_1_ / 100.0D;
            double d1 = d0 / 1000.0D;
            return d1 > 0.5D ? StatBase.decimalFormat.format(d1) + " km" : (d0 > 0.5D ? StatBase.decimalFormat.format(d0) + " m" : p_75843_1_ + " cm");
        }
    };
    public static IStatType field_111202_k = new IStatType()
    {
        private static final String __OBFID = "CL_00001476";
        /**
         * Formats a given stat for human consumption.
         */
        @SideOnly(Side.CLIENT)
        public String format(int p_75843_1_)
        {
            return StatBase.decimalFormat.format((double)p_75843_1_ * 0.1D);
        }
    };
    private static final String __OBFID = "CL_00001472";

    public StatBase(String p_i45307_1_, IChatComponent p_i45307_2_, IStatType p_i45307_3_)
    {
        this.statId = p_i45307_1_;
        this.statName = p_i45307_2_;
        this.type = p_i45307_3_;
        this.field_150957_c = new ObjectiveStat(this);
        IScoreObjectiveCriteria.field_96643_a.put(this.field_150957_c.func_96636_a(), this.field_150957_c);
    }

    public StatBase(String p_i45308_1_, IChatComponent p_i45308_2_)
    {
        this(p_i45308_1_, p_i45308_2_, simpleStatType);
    }

    /**
     * Initializes the current stat as independent (i.e., lacking prerequisites for being updated) and returns the
     * current instance.
     */
    public StatBase initIndependentStat()
    {
        this.isIndependent = true;
        return this;
    }

    /**
     * Register the stat into StatList.
     */
    public StatBase registerStat()
    {
        if (StatList.oneShotStats.containsKey(this.statId))
        {
            throw new RuntimeException("Duplicate stat id: \"" + ((StatBase)StatList.oneShotStats.get(this.statId)).statName + "\" and \"" + this.statName + "\" at id " + this.statId);
        }
        else
        {
            StatList.allStats.add(this);
            StatList.oneShotStats.put(this.statId, this);
            return this;
        }
    }

    /**
     * Returns whether or not the StatBase-derived class is a statistic (running counter) or an achievement (one-shot).
     */
    public boolean isAchievement()
    {
        return false;
    }

    @SideOnly(Side.CLIENT)
    public String func_75968_a(int p_75968_1_)
    {
        return this.type.format(p_75968_1_);
    }

    public IChatComponent func_150951_e()
    {
        IChatComponent ichatcomponent = this.statName.createCopy();
        ichatcomponent.getChatStyle().setColor(EnumChatFormatting.GRAY);
        ichatcomponent.getChatStyle().setChatHoverEvent(new HoverEvent(HoverEvent.Action.SHOW_ACHIEVEMENT, new ChatComponentText(this.statId)));
        return ichatcomponent;
    }

    public IChatComponent func_150955_j()
    {
        IChatComponent ichatcomponent = this.func_150951_e();
        IChatComponent ichatcomponent1 = (new ChatComponentText("[")).appendSibling(ichatcomponent).appendText("]");
        ichatcomponent1.setChatStyle(ichatcomponent.getChatStyle());
        return ichatcomponent1;
    }

    public boolean equals(Object p_equals_1_)
    {
        if (this == p_equals_1_)
        {
            return true;
        }
        else if (p_equals_1_ != null && this.getClass() == p_equals_1_.getClass())
        {
            StatBase statbase = (StatBase)p_equals_1_;
            return this.statId.equals(statbase.statId);
        }
        else
        {
            return false;
        }
    }

    public int hashCode()
    {
        return this.statId.hashCode();
    }

    public String toString()
    {
        return "Stat{id=" + this.statId + ", nameId=" + this.statName + ", awardLocallyOnly=" + this.isIndependent + ", formatter=" + this.type + ", objectiveCriteria=" + this.field_150957_c + '}';
    }

    public IScoreObjectiveCriteria func_150952_k()
    {
        return this.field_150957_c;
    }

    public Class func_150954_l()
    {
        return this.field_150956_d;
    }

    public StatBase func_150953_b(Class p_150953_1_)
    {
        this.field_150956_d = p_150953_1_;
        return this;
    }
}