package net.minecraft.entity.ai.attributes;

public interface IAttribute
{
    String getAttributeUnlocalizedName();

    double clampValue(double p_111109_1_);

    double getDefaultValue();

    boolean getShouldWatch();
}