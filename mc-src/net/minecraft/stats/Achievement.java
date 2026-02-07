package net.minecraft.stats;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.block.Block;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.util.ChatComponentTranslation;
import net.minecraft.util.EnumChatFormatting;
import net.minecraft.util.IChatComponent;
import net.minecraft.util.StatCollector;

public class Achievement extends StatBase
{
    /** Is the column (related to center of achievement gui, in 24 pixels unit) that the achievement will be displayed. */
    public final int displayColumn;
    /** Is the row (related to center of achievement gui, in 24 pixels unit) that the achievement will be displayed. */
    public final int displayRow;
    /** Holds the parent achievement, that must be taken before this achievement is avaiable. */
    public final Achievement parentAchievement;
    /** Holds the description of the achievement, ready to be formatted and/or displayed. */
    private final String achievementDescription;
    /**
     * Holds a string formatter for the achievement, some of then needs extra dynamic info - like the key used to open
     * the inventory.
     */
    @SideOnly(Side.CLIENT)
    private IStatStringFormat statStringFormatter;
    /** Holds the ItemStack that will be used to draw the achievement into the GUI. */
    public final ItemStack theItemStack;
    /**
     * Special achievements have a 'spiked' (on normal texture pack) frame, special achievements are the hardest ones to
     * achieve.
     */
    private boolean isSpecial;
    private static final String __OBFID = "CL_00001466";

    public Achievement(String p_i45300_1_, String p_i45300_2_, int p_i45300_3_, int p_i45300_4_, Item p_i45300_5_, Achievement p_i45300_6_)
    {
        this(p_i45300_1_, p_i45300_2_, p_i45300_3_, p_i45300_4_, new ItemStack(p_i45300_5_), p_i45300_6_);
    }

    public Achievement(String p_i45301_1_, String p_i45301_2_, int p_i45301_3_, int p_i45301_4_, Block p_i45301_5_, Achievement p_i45301_6_)
    {
        this(p_i45301_1_, p_i45301_2_, p_i45301_3_, p_i45301_4_, new ItemStack(p_i45301_5_), p_i45301_6_);
    }

    public Achievement(String p_i45302_1_, String p_i45302_2_, int p_i45302_3_, int p_i45302_4_, ItemStack p_i45302_5_, Achievement p_i45302_6_)
    {
        super(p_i45302_1_, new ChatComponentTranslation("achievement." + p_i45302_2_, new Object[0]));
        this.theItemStack = p_i45302_5_;
        this.achievementDescription = "achievement." + p_i45302_2_ + ".desc";
        this.displayColumn = p_i45302_3_;
        this.displayRow = p_i45302_4_;

        if (p_i45302_3_ < AchievementList.minDisplayColumn)
        {
            AchievementList.minDisplayColumn = p_i45302_3_;
        }

        if (p_i45302_4_ < AchievementList.minDisplayRow)
        {
            AchievementList.minDisplayRow = p_i45302_4_;
        }

        if (p_i45302_3_ > AchievementList.maxDisplayColumn)
        {
            AchievementList.maxDisplayColumn = p_i45302_3_;
        }

        if (p_i45302_4_ > AchievementList.maxDisplayRow)
        {
            AchievementList.maxDisplayRow = p_i45302_4_;
        }

        this.parentAchievement = p_i45302_6_;
    }

    /**
     * Initializes the current stat as independent (i.e., lacking prerequisites for being updated) and returns the
     * current instance.
     */
    public Achievement initIndependentStat()
    {
        this.isIndependent = true;
        return this;
    }

    /**
     * Special achievements have a 'spiked' (on normal texture pack) frame, special achievements are the hardest ones to
     * achieve.
     */
    public Achievement setSpecial()
    {
        this.isSpecial = true;
        return this;
    }

    /**
     * Register the stat into StatList.
     */
    public Achievement registerStat()
    {
        super.registerStat();
        AchievementList.achievementList.add(this);
        return this;
    }

    /**
     * Returns whether or not the StatBase-derived class is a statistic (running counter) or an achievement (one-shot).
     */
    public boolean isAchievement()
    {
        return true;
    }

    public IChatComponent func_150951_e()
    {
        IChatComponent ichatcomponent = super.func_150951_e();
        ichatcomponent.getChatStyle().setColor(this.getSpecial() ? EnumChatFormatting.DARK_PURPLE : EnumChatFormatting.GREEN);
        return ichatcomponent;
    }

    public Achievement func_150953_b(Class p_150953_1_)
    {
        return (Achievement)super.func_150953_b(p_150953_1_);
    }

    /**
     * Returns the fully description of the achievement - ready to be displayed on screen.
     */
    @SideOnly(Side.CLIENT)
    public String getDescription()
    {
        return this.statStringFormatter != null ? this.statStringFormatter.formatString(StatCollector.translateToLocal(this.achievementDescription)) : StatCollector.translateToLocal(this.achievementDescription);
    }

    /**
     * Defines a string formatter for the achievement.
     */
    @SideOnly(Side.CLIENT)
    public Achievement setStatStringFormatter(IStatStringFormat p_75988_1_)
    {
        this.statStringFormatter = p_75988_1_;
        return this;
    }

    /**
     * Special achievements have a 'spiked' (on normal texture pack) frame, special achievements are the hardest ones to
     * achieve.
     */
    public boolean getSpecial()
    {
        return this.isSpecial;
    }
}