package net.minecraft.client.audio;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.util.ResourceLocation;

@SideOnly(Side.CLIENT)
public class SoundPoolEntry
{
    private final ResourceLocation field_148656_a;
    private final boolean field_148654_b;
    private double field_148655_c;
    private double field_148653_d;
    private static final String __OBFID = "CL_00001140";

    public SoundPoolEntry(ResourceLocation p_i45113_1_, double p_i45113_2_, double p_i45113_4_, boolean p_i45113_6_)
    {
        this.field_148656_a = p_i45113_1_;
        this.field_148655_c = p_i45113_2_;
        this.field_148653_d = p_i45113_4_;
        this.field_148654_b = p_i45113_6_;
    }

    public SoundPoolEntry(SoundPoolEntry p_i45114_1_)
    {
        this.field_148656_a = p_i45114_1_.field_148656_a;
        this.field_148655_c = p_i45114_1_.field_148655_c;
        this.field_148653_d = p_i45114_1_.field_148653_d;
        this.field_148654_b = p_i45114_1_.field_148654_b;
    }

    public ResourceLocation getSoundPoolEntryLocation()
    {
        return this.field_148656_a;
    }

    public double getPitch()
    {
        return this.field_148655_c;
    }

    public void setPitch(double p_148651_1_)
    {
        this.field_148655_c = p_148651_1_;
    }

    public double getVolume()
    {
        return this.field_148653_d;
    }

    public void setVolume(double p_148647_1_)
    {
        this.field_148653_d = p_148647_1_;
    }

    public boolean func_148648_d()
    {
        return this.field_148654_b;
    }
}