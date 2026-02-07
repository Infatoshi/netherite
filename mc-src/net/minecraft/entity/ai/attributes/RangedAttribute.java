package net.minecraft.entity.ai.attributes;

public class RangedAttribute extends BaseAttribute
{
    private final double minimumValue;
    private final double maximumValue;
    private String description;
    private static final String __OBFID = "CL_00001568";

    public RangedAttribute(String p_i1609_1_, double p_i1609_2_, double p_i1609_4_, double p_i1609_6_)
    {
        super(p_i1609_1_, p_i1609_2_);
        this.minimumValue = p_i1609_4_;
        this.maximumValue = p_i1609_6_;

        if (p_i1609_4_ > p_i1609_6_)
        {
            throw new IllegalArgumentException("Minimum value cannot be bigger than maximum value!");
        }
        else if (p_i1609_2_ < p_i1609_4_)
        {
            throw new IllegalArgumentException("Default value cannot be lower than minimum value!");
        }
        else if (p_i1609_2_ > p_i1609_6_)
        {
            throw new IllegalArgumentException("Default value cannot be bigger than maximum value!");
        }
    }

    public RangedAttribute setDescription(String p_111117_1_)
    {
        this.description = p_111117_1_;
        return this;
    }

    public String getDescription()
    {
        return this.description;
    }

    public double clampValue(double p_111109_1_)
    {
        if (p_111109_1_ < this.minimumValue)
        {
            p_111109_1_ = this.minimumValue;
        }

        if (p_111109_1_ > this.maximumValue)
        {
            p_111109_1_ = this.maximumValue;
        }

        return p_111109_1_;
    }
}