package net.minecraft.client.audio;

import com.google.common.collect.Lists;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.Iterator;
import java.util.List;
import java.util.Random;
import net.minecraft.util.ResourceLocation;

@SideOnly(Side.CLIENT)
public class SoundEventAccessorComposite implements ISoundEventAccessor
{
    /** A composite (List) of ISoundEventAccessors */
    private final List soundPool = Lists.newArrayList();
    private final Random rnd = new Random();
    private final ResourceLocation field_148735_c;
    private final SoundCategory field_148732_d;
    private double eventPitch;
    private double eventVolume;
    private static final String __OBFID = "CL_00001146";

    public SoundEventAccessorComposite(ResourceLocation p_i45120_1_, double p_i45120_2_, double p_i45120_4_, SoundCategory p_i45120_6_)
    {
        this.field_148735_c = p_i45120_1_;
        this.eventVolume = p_i45120_4_;
        this.eventPitch = p_i45120_2_;
        this.field_148732_d = p_i45120_6_;
    }

    public int func_148721_a()
    {
        int i = 0;
        ISoundEventAccessor isoundeventaccessor;

        for (Iterator iterator = this.soundPool.iterator(); iterator.hasNext(); i += isoundeventaccessor.func_148721_a())
        {
            isoundeventaccessor = (ISoundEventAccessor)iterator.next();
        }

        return i;
    }

    public SoundPoolEntry func_148720_g()
    {
        int i = this.func_148721_a();

        if (!this.soundPool.isEmpty() && i != 0)
        {
            int j = this.rnd.nextInt(i);
            Iterator iterator = this.soundPool.iterator();
            ISoundEventAccessor isoundeventaccessor;

            do
            {
                if (!iterator.hasNext())
                {
                    return SoundHandler.missing_sound;
                }

                isoundeventaccessor = (ISoundEventAccessor)iterator.next();
                j -= isoundeventaccessor.func_148721_a();
            }
            while (j >= 0);

            SoundPoolEntry soundpoolentry = (SoundPoolEntry)isoundeventaccessor.func_148720_g();
            soundpoolentry.setPitch(soundpoolentry.getPitch() * this.eventPitch);
            soundpoolentry.setVolume(soundpoolentry.getVolume() * this.eventVolume);
            return soundpoolentry;
        }
        else
        {
            return SoundHandler.missing_sound;
        }
    }

    public void addSoundToEventPool(ISoundEventAccessor p_148727_1_)
    {
        this.soundPool.add(p_148727_1_);
    }

    public ResourceLocation getSoundEventLocation()
    {
        return this.field_148735_c;
    }

    public SoundCategory getSoundCategory()
    {
        return this.field_148732_d;
    }
}