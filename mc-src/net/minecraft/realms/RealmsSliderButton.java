package net.minecraft.realms;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.util.MathHelper;
import org.lwjgl.opengl.GL11;

@SideOnly(Side.CLIENT)
public class RealmsSliderButton extends RealmsButton
{
    public float value;
    public boolean sliding;
    private final float minValue;
    private final float maxValue;
    private int steps;
    private static final String __OBFID = "CL_00001834";

    public RealmsSliderButton(int p_i1056_1_, int p_i1056_2_, int p_i1056_3_, int p_i1056_4_, int p_i1056_5_, int p_i1056_6_)
    {
        this(p_i1056_1_, p_i1056_2_, p_i1056_3_, p_i1056_4_, p_i1056_6_, 0, 1.0F, (float)p_i1056_5_);
    }

    public RealmsSliderButton(int p_i1057_1_, int p_i1057_2_, int p_i1057_3_, int p_i1057_4_, int p_i1057_5_, int p_i1057_6_, float p_i1057_7_, float p_i1057_8_)
    {
        super(p_i1057_1_, p_i1057_2_, p_i1057_3_, p_i1057_4_, 20, "");
        this.value = 1.0F;
        this.minValue = p_i1057_7_;
        this.maxValue = p_i1057_8_;
        this.value = this.toPct((float)p_i1057_6_);
        this.getProxy().displayString = this.getMessage();
    }

    public String getMessage()
    {
        return "";
    }

    public float toPct(float p_toPct_1_)
    {
        return MathHelper.clamp_float((this.clamp(p_toPct_1_) - this.minValue) / (this.maxValue - this.minValue), 0.0F, 1.0F);
    }

    public float toValue(float p_toValue_1_)
    {
        return this.clamp(this.minValue + (this.maxValue - this.minValue) * MathHelper.clamp_float(p_toValue_1_, 0.0F, 1.0F));
    }

    public float clamp(float p_clamp_1_)
    {
        p_clamp_1_ = this.clampSteps(p_clamp_1_);
        return MathHelper.clamp_float(p_clamp_1_, this.minValue, this.maxValue);
    }

    protected float clampSteps(float p_clampSteps_1_)
    {
        if (this.steps > 0)
        {
            p_clampSteps_1_ = (float)(this.steps * Math.round(p_clampSteps_1_ / (float)this.steps));
        }

        return p_clampSteps_1_;
    }

    public int getYImage(boolean p_getYImage_1_)
    {
        return 0;
    }

    public void renderBg(int p_renderBg_1_, int p_renderBg_2_)
    {
        if (this.getProxy().visible)
        {
            if (this.sliding)
            {
                this.value = (float)(p_renderBg_1_ - (this.getProxy().xPosition + 4)) / (float)(this.getProxy().getButtonWidth() - 8);

                if (this.value < 0.0F)
                {
                    this.value = 0.0F;
                }

                if (this.value > 1.0F)
                {
                    this.value = 1.0F;
                }

                float f = this.toValue(this.value);
                this.clicked(f);
                this.value = this.toPct(f);
                this.getProxy().displayString = this.getMessage();
            }

            GL11.glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
            this.blit(this.getProxy().xPosition + (int)(this.value * (float)(this.getProxy().getButtonWidth() - 8)), this.getProxy().yPosition, 0, 66, 4, 20);
            this.blit(this.getProxy().xPosition + (int)(this.value * (float)(this.getProxy().getButtonWidth() - 8)) + 4, this.getProxy().yPosition, 196, 66, 4, 20);
        }
    }

    public void clicked(int p_clicked_1_, int p_clicked_2_)
    {
        this.value = (float)(p_clicked_1_ - (this.getProxy().xPosition + 4)) / (float)(this.getProxy().getButtonWidth() - 8);

        if (this.value < 0.0F)
        {
            this.value = 0.0F;
        }

        if (this.value > 1.0F)
        {
            this.value = 1.0F;
        }

        this.clicked(this.toValue(this.value));
        this.getProxy().displayString = this.getMessage();
        this.sliding = true;
    }

    public void clicked(float p_clicked_1_) {}

    public void released(int p_released_1_, int p_released_2_)
    {
        this.sliding = false;
    }
}