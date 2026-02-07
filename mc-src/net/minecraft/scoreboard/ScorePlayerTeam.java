package net.minecraft.scoreboard;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.Collection;
import java.util.HashSet;
import java.util.Set;

public class ScorePlayerTeam extends Team
{
    private final Scoreboard theScoreboard;
    private final String field_96675_b;
    /** A set of all team member usernames. */
    private final Set membershipSet = new HashSet();
    private String teamNameSPT;
    private String namePrefixSPT = "";
    private String colorSuffix = "";
    private boolean allowFriendlyFire = true;
    private boolean canSeeFriendlyInvisibles = true;
    private static final String __OBFID = "CL_00000616";

    public ScorePlayerTeam(Scoreboard p_i2308_1_, String p_i2308_2_)
    {
        this.theScoreboard = p_i2308_1_;
        this.field_96675_b = p_i2308_2_;
        this.teamNameSPT = p_i2308_2_;
    }

    /**
     * Retrieve the name by which this team is registered in the scoreboard
     */
    public String getRegisteredName()
    {
        return this.field_96675_b;
    }

    public String func_96669_c()
    {
        return this.teamNameSPT;
    }

    public void setTeamName(String p_96664_1_)
    {
        if (p_96664_1_ == null)
        {
            throw new IllegalArgumentException("Name cannot be null");
        }
        else
        {
            this.teamNameSPT = p_96664_1_;
            this.theScoreboard.broadcastTeamRemoved(this);
        }
    }

    public Collection getMembershipCollection()
    {
        return this.membershipSet;
    }

    /**
     * Returns the color prefix for the player's team name
     */
    public String getColorPrefix()
    {
        return this.namePrefixSPT;
    }

    public void setNamePrefix(String p_96666_1_)
    {
        if (p_96666_1_ == null)
        {
            throw new IllegalArgumentException("Prefix cannot be null");
        }
        else
        {
            this.namePrefixSPT = p_96666_1_;
            this.theScoreboard.broadcastTeamRemoved(this);
        }
    }

    /**
     * Returns the color suffix for the player's team name
     */
    public String getColorSuffix()
    {
        return this.colorSuffix;
    }

    public void setNameSuffix(String p_96662_1_)
    {
        if (p_96662_1_ == null)
        {
            throw new IllegalArgumentException("Suffix cannot be null");
        }
        else
        {
            this.colorSuffix = p_96662_1_;
            this.theScoreboard.broadcastTeamRemoved(this);
        }
    }

    public String formatString(String p_142053_1_)
    {
        return this.getColorPrefix() + p_142053_1_ + this.getColorSuffix();
    }

    /**
     * Returns the player name including the color prefixes and suffixes
     */
    public static String formatPlayerName(Team p_96667_0_, String p_96667_1_)
    {
        return p_96667_0_ == null ? p_96667_1_ : p_96667_0_.formatString(p_96667_1_);
    }

    public boolean getAllowFriendlyFire()
    {
        return this.allowFriendlyFire;
    }

    public void setAllowFriendlyFire(boolean p_96660_1_)
    {
        this.allowFriendlyFire = p_96660_1_;
        this.theScoreboard.broadcastTeamRemoved(this);
    }

    public boolean func_98297_h()
    {
        return this.canSeeFriendlyInvisibles;
    }

    public void setSeeFriendlyInvisiblesEnabled(boolean p_98300_1_)
    {
        this.canSeeFriendlyInvisibles = p_98300_1_;
        this.theScoreboard.broadcastTeamRemoved(this);
    }

    public int func_98299_i()
    {
        int i = 0;

        if (this.getAllowFriendlyFire())
        {
            i |= 1;
        }

        if (this.func_98297_h())
        {
            i |= 2;
        }

        return i;
    }

    @SideOnly(Side.CLIENT)
    public void func_98298_a(int p_98298_1_)
    {
        this.setAllowFriendlyFire((p_98298_1_ & 1) > 0);
        this.setSeeFriendlyInvisiblesEnabled((p_98298_1_ & 2) > 0);
    }
}