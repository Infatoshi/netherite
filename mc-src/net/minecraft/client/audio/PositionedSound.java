package net.minecraft.client.audio;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.util.ResourceLocation;

@SideOnly(Side.CLIENT)
public abstract class PositionedSound implements ISound
{
    protected final ResourceLocation field_147664_a;
    protected float volume = 1.0F;
    protected float field_147663_c = 1.0F;
    protected float xPosF;
    protected float yPosF;
    protected float zPosF;
    protected boolean repeat = false;
    protected int field_147665_h = 0;
    protected ISound.AttenuationType field_147666_i;
    private static final String __OBFID = "CL_00001116";

    protected PositionedSound(ResourceLocation p_i45103_1_)
    {
        this.field_147666_i = ISound.AttenuationType.LINEAR;
        this.field_147664_a = p_i45103_1_;
    }

    public ResourceLocation getPositionedSoundLocation()
    {
        return this.field_147664_a;
    }

    public boolean canRepeat()
    {
        return this.repeat;
    }

    public int getRepeatDelay()
    {
        return this.field_147665_h;
    }

    public float getVolume()
    {
        return this.volume;
    }

    public float getPitch()
    {
        return this.field_147663_c;
    }

    public float getXPosF()
    {
        return this.xPosF;
    }

    public float getYPosF()
    {
        return this.yPosF;
    }

    public float getZPosF()
    {
        return this.zPosF;
    }

    public ISound.AttenuationType getAttenuationType()
    {
        return this.field_147666_i;
    }
}