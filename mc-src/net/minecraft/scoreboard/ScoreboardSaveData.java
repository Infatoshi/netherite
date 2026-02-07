package net.minecraft.scoreboard;

import java.util.Collection;
import java.util.Iterator;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.nbt.NBTTagString;
import net.minecraft.world.WorldSavedData;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

public class ScoreboardSaveData extends WorldSavedData
{
    private static final Logger logger = LogManager.getLogger();
    private Scoreboard theScoreboard;
    private NBTTagCompound field_96506_b;
    private static final String __OBFID = "CL_00000620";

    public ScoreboardSaveData()
    {
        this("scoreboard");
    }

    public ScoreboardSaveData(String p_i2310_1_)
    {
        super(p_i2310_1_);
    }

    public void func_96499_a(Scoreboard p_96499_1_)
    {
        this.theScoreboard = p_96499_1_;

        if (this.field_96506_b != null)
        {
            this.readFromNBT(this.field_96506_b);
        }
    }

    /**
     * reads in data from the NBTTagCompound into this MapDataBase
     */
    public void readFromNBT(NBTTagCompound p_76184_1_)
    {
        if (this.theScoreboard == null)
        {
            this.field_96506_b = p_76184_1_;
        }
        else
        {
            this.func_96501_b(p_76184_1_.getTagList("Objectives", 10));
            this.func_96500_c(p_76184_1_.getTagList("PlayerScores", 10));

            if (p_76184_1_.hasKey("DisplaySlots", 10))
            {
                this.func_96504_c(p_76184_1_.getCompoundTag("DisplaySlots"));
            }

            if (p_76184_1_.hasKey("Teams", 9))
            {
                this.func_96498_a(p_76184_1_.getTagList("Teams", 10));
            }
        }
    }

    protected void func_96498_a(NBTTagList p_96498_1_)
    {
        for (int i = 0; i < p_96498_1_.tagCount(); ++i)
        {
            NBTTagCompound nbttagcompound = p_96498_1_.getCompoundTagAt(i);
            ScorePlayerTeam scoreplayerteam = this.theScoreboard.createTeam(nbttagcompound.getString("Name"));
            scoreplayerteam.setTeamName(nbttagcompound.getString("DisplayName"));
            scoreplayerteam.setNamePrefix(nbttagcompound.getString("Prefix"));
            scoreplayerteam.setNameSuffix(nbttagcompound.getString("Suffix"));

            if (nbttagcompound.hasKey("AllowFriendlyFire", 99))
            {
                scoreplayerteam.setAllowFriendlyFire(nbttagcompound.getBoolean("AllowFriendlyFire"));
            }

            if (nbttagcompound.hasKey("SeeFriendlyInvisibles", 99))
            {
                scoreplayerteam.setSeeFriendlyInvisiblesEnabled(nbttagcompound.getBoolean("SeeFriendlyInvisibles"));
            }

            this.func_96502_a(scoreplayerteam, nbttagcompound.getTagList("Players", 8));
        }
    }

    protected void func_96502_a(ScorePlayerTeam p_96502_1_, NBTTagList p_96502_2_)
    {
        for (int i = 0; i < p_96502_2_.tagCount(); ++i)
        {
            this.theScoreboard.func_151392_a(p_96502_2_.getStringTagAt(i), p_96502_1_.getRegisteredName());
        }
    }

    protected void func_96504_c(NBTTagCompound p_96504_1_)
    {
        for (int i = 0; i < 3; ++i)
        {
            if (p_96504_1_.hasKey("slot_" + i, 8))
            {
                String s = p_96504_1_.getString("slot_" + i);
                ScoreObjective scoreobjective = this.theScoreboard.getObjective(s);
                this.theScoreboard.func_96530_a(i, scoreobjective);
            }
        }
    }

    protected void func_96501_b(NBTTagList p_96501_1_)
    {
        for (int i = 0; i < p_96501_1_.tagCount(); ++i)
        {
            NBTTagCompound nbttagcompound = p_96501_1_.getCompoundTagAt(i);
            IScoreObjectiveCriteria iscoreobjectivecriteria = (IScoreObjectiveCriteria)IScoreObjectiveCriteria.field_96643_a.get(nbttagcompound.getString("CriteriaName"));
            ScoreObjective scoreobjective = this.theScoreboard.addScoreObjective(nbttagcompound.getString("Name"), iscoreobjectivecriteria);
            scoreobjective.setDisplayName(nbttagcompound.getString("DisplayName"));
        }
    }

    protected void func_96500_c(NBTTagList p_96500_1_)
    {
        for (int i = 0; i < p_96500_1_.tagCount(); ++i)
        {
            NBTTagCompound nbttagcompound = p_96500_1_.getCompoundTagAt(i);
            ScoreObjective scoreobjective = this.theScoreboard.getObjective(nbttagcompound.getString("Objective"));
            Score score = this.theScoreboard.func_96529_a(nbttagcompound.getString("Name"), scoreobjective);
            score.setScorePoints(nbttagcompound.getInteger("Score"));
        }
    }

    /**
     * write data to NBTTagCompound from this MapDataBase, similar to Entities and TileEntities
     */
    public void writeToNBT(NBTTagCompound p_76187_1_)
    {
        if (this.theScoreboard == null)
        {
            logger.warn("Tried to save scoreboard without having a scoreboard...");
        }
        else
        {
            p_76187_1_.setTag("Objectives", this.func_96505_b());
            p_76187_1_.setTag("PlayerScores", this.func_96503_e());
            p_76187_1_.setTag("Teams", this.func_96496_a());
            this.func_96497_d(p_76187_1_);
        }
    }

    protected NBTTagList func_96496_a()
    {
        NBTTagList nbttaglist = new NBTTagList();
        Collection collection = this.theScoreboard.getTeams();
        Iterator iterator = collection.iterator();

        while (iterator.hasNext())
        {
            ScorePlayerTeam scoreplayerteam = (ScorePlayerTeam)iterator.next();
            NBTTagCompound nbttagcompound = new NBTTagCompound();
            nbttagcompound.setString("Name", scoreplayerteam.getRegisteredName());
            nbttagcompound.setString("DisplayName", scoreplayerteam.func_96669_c());
            nbttagcompound.setString("Prefix", scoreplayerteam.getColorPrefix());
            nbttagcompound.setString("Suffix", scoreplayerteam.getColorSuffix());
            nbttagcompound.setBoolean("AllowFriendlyFire", scoreplayerteam.getAllowFriendlyFire());
            nbttagcompound.setBoolean("SeeFriendlyInvisibles", scoreplayerteam.func_98297_h());
            NBTTagList nbttaglist1 = new NBTTagList();
            Iterator iterator1 = scoreplayerteam.getMembershipCollection().iterator();

            while (iterator1.hasNext())
            {
                String s = (String)iterator1.next();
                nbttaglist1.appendTag(new NBTTagString(s));
            }

            nbttagcompound.setTag("Players", nbttaglist1);
            nbttaglist.appendTag(nbttagcompound);
        }

        return nbttaglist;
    }

    protected void func_96497_d(NBTTagCompound p_96497_1_)
    {
        NBTTagCompound nbttagcompound1 = new NBTTagCompound();
        boolean flag = false;

        for (int i = 0; i < 3; ++i)
        {
            ScoreObjective scoreobjective = this.theScoreboard.func_96539_a(i);

            if (scoreobjective != null)
            {
                nbttagcompound1.setString("slot_" + i, scoreobjective.getName());
                flag = true;
            }
        }

        if (flag)
        {
            p_96497_1_.setTag("DisplaySlots", nbttagcompound1);
        }
    }

    protected NBTTagList func_96505_b()
    {
        NBTTagList nbttaglist = new NBTTagList();
        Collection collection = this.theScoreboard.getScoreObjectives();
        Iterator iterator = collection.iterator();

        while (iterator.hasNext())
        {
            ScoreObjective scoreobjective = (ScoreObjective)iterator.next();
            NBTTagCompound nbttagcompound = new NBTTagCompound();
            nbttagcompound.setString("Name", scoreobjective.getName());
            nbttagcompound.setString("CriteriaName", scoreobjective.getCriteria().func_96636_a());
            nbttagcompound.setString("DisplayName", scoreobjective.getDisplayName());
            nbttaglist.appendTag(nbttagcompound);
        }

        return nbttaglist;
    }

    protected NBTTagList func_96503_e()
    {
        NBTTagList nbttaglist = new NBTTagList();
        Collection collection = this.theScoreboard.func_96528_e();
        Iterator iterator = collection.iterator();

        while (iterator.hasNext())
        {
            Score score = (Score)iterator.next();
            NBTTagCompound nbttagcompound = new NBTTagCompound();
            nbttagcompound.setString("Name", score.getPlayerName());
            nbttagcompound.setString("Objective", score.func_96645_d().getName());
            nbttagcompound.setInteger("Score", score.getScorePoints());
            nbttaglist.appendTag(nbttagcompound);
        }

        return nbttaglist;
    }
}