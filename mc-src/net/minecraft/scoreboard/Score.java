package net.minecraft.scoreboard;

import java.util.Comparator;
import java.util.List;

public class Score
{
    public static final Comparator field_96658_a = new Comparator()
    {
        private static final String __OBFID = "CL_00000618";
        public int compare(Score p_compare_1_, Score p_compare_2_)
        {
            return p_compare_1_.getScorePoints() > p_compare_2_.getScorePoints() ? 1 : (p_compare_1_.getScorePoints() < p_compare_2_.getScorePoints() ? -1 : 0);
        }
        public int compare(Object p_compare_1_, Object p_compare_2_)
        {
            return this.compare((Score)p_compare_1_, (Score)p_compare_2_);
        }
    };
    private final Scoreboard theScoreboard;
    private final ScoreObjective theScoreObjective;
    private final String scorePlayerName;
    private int field_96655_e;
    private static final String __OBFID = "CL_00000617";

    public Score(Scoreboard p_i2309_1_, ScoreObjective p_i2309_2_, String p_i2309_3_)
    {
        this.theScoreboard = p_i2309_1_;
        this.theScoreObjective = p_i2309_2_;
        this.scorePlayerName = p_i2309_3_;
    }

    public void increseScore(int p_96649_1_)
    {
        if (this.theScoreObjective.getCriteria().isReadOnly())
        {
            throw new IllegalStateException("Cannot modify read-only score");
        }
        else
        {
            this.setScorePoints(this.getScorePoints() + p_96649_1_);
        }
    }

    public void decreaseScore(int p_96646_1_)
    {
        if (this.theScoreObjective.getCriteria().isReadOnly())
        {
            throw new IllegalStateException("Cannot modify read-only score");
        }
        else
        {
            this.setScorePoints(this.getScorePoints() - p_96646_1_);
        }
    }

    public void func_96648_a()
    {
        if (this.theScoreObjective.getCriteria().isReadOnly())
        {
            throw new IllegalStateException("Cannot modify read-only score");
        }
        else
        {
            this.increseScore(1);
        }
    }

    public int getScorePoints()
    {
        return this.field_96655_e;
    }

    public void setScorePoints(int p_96647_1_)
    {
        int j = this.field_96655_e;
        this.field_96655_e = p_96647_1_;

        if (j != p_96647_1_)
        {
            this.getScoreScoreboard().func_96536_a(this);
        }
    }

    public ScoreObjective func_96645_d()
    {
        return this.theScoreObjective;
    }

    public String getPlayerName()
    {
        return this.scorePlayerName;
    }

    public Scoreboard getScoreScoreboard()
    {
        return this.theScoreboard;
    }

    public void func_96651_a(List p_96651_1_)
    {
        this.setScorePoints(this.theScoreObjective.getCriteria().func_96635_a(p_96651_1_));
    }
}