package net.minecraft.scoreboard;

import java.util.List;

public class ScoreDummyCriteria implements IScoreObjectiveCriteria
{
    private final String field_96644_g;
    private static final String __OBFID = "CL_00000622";

    public ScoreDummyCriteria(String p_i2311_1_)
    {
        this.field_96644_g = p_i2311_1_;
        IScoreObjectiveCriteria.field_96643_a.put(p_i2311_1_, this);
    }

    public String func_96636_a()
    {
        return this.field_96644_g;
    }

    public int func_96635_a(List p_96635_1_)
    {
        return 0;
    }

    public boolean isReadOnly()
    {
        return false;
    }
}