package net.minecraft.client.audio;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.util.ResourceLocation;

@SideOnly(Side.CLIENT)
public interface ISound
{
    ResourceLocation getPositionedSoundLocation();

    boolean canRepeat();

    int getRepeatDelay();

    float getVolume();

    float getPitch();

    float getXPosF();

    float getYPosF();

    float getZPosF();

    ISound.AttenuationType getAttenuationType();

    @SideOnly(Side.CLIENT)
    public static enum AttenuationType
    {
        NONE(0),
        LINEAR(2);
        private final int field_148589_c;

        private static final String __OBFID = "CL_00001126";

        private AttenuationType(int p_i45110_3_)
        {
            this.field_148589_c = p_i45110_3_;
        }

        public int getTypeInt()
        {
            return this.field_148589_c;
        }
    }
}