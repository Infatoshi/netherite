package net.minecraft.entity.ai.attributes;

public abstract class BaseAttribute implements IAttribute
{
    private final String unlocalizedName;
    private final double defaultValue;
    private boolean shouldWatch;
    private static final String __OBFID = "CL_00001565";

    protected BaseAttribute(String p_i1607_1_, double p_i1607_2_)
    {
        this.unlocalizedName = p_i1607_1_;
        this.defaultValue = p_i1607_2_;

        if (p_i1607_1_ == null)
        {
            throw new IllegalArgumentException("Name cannot be null!");
        }
    }

    public String getAttributeUnlocalizedName()
    {
        return this.unlocalizedName;
    }

    public double getDefaultValue()
    {
        return this.defaultValue;
    }

    public boolean getShouldWatch()
    {
        return this.shouldWatch;
    }

    public BaseAttribute setShouldWatch(boolean p_111112_1_)
    {
        this.shouldWatch = p_111112_1_;
        return this;
    }

    public int hashCode()
    {
        return this.unlocalizedName.hashCode();
    }
}