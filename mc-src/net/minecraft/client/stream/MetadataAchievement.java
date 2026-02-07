package net.minecraft.client.stream;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.stats.Achievement;

@SideOnly(Side.CLIENT)
public class MetadataAchievement extends Metadata
{
    private static final String __OBFID = "CL_00001824";

    public MetadataAchievement(Achievement p_i1032_1_)
    {
        super("achievement");
        this.func_152808_a("achievement_id", p_i1032_1_.statId);
        this.func_152808_a("achievement_name", p_i1032_1_.func_150951_e().getUnformattedText());
        this.func_152808_a("achievement_description", p_i1032_1_.getDescription());
        this.func_152807_a("Achievement \'" + p_i1032_1_.func_150951_e().getUnformattedText() + "\' obtained!");
    }
}