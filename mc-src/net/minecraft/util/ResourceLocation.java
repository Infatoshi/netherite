package net.minecraft.util;

import org.apache.commons.lang3.Validate;

public class ResourceLocation
{
    private final String resourceDomain;
    private final String resourcePath;
    private static final String __OBFID = "CL_00001082";

    public ResourceLocation(String p_i1292_1_, String p_i1292_2_)
    {
        Validate.notNull(p_i1292_2_);

        if (p_i1292_1_ != null && p_i1292_1_.length() != 0)
        {
            this.resourceDomain = p_i1292_1_;
        }
        else
        {
            this.resourceDomain = "minecraft";
        }

        this.resourcePath = p_i1292_2_;
    }

    public ResourceLocation(String p_i1293_1_)
    {
        String s1 = "minecraft";
        String s2 = p_i1293_1_;
        int i = p_i1293_1_.indexOf(58);

        if (i >= 0)
        {
            s2 = p_i1293_1_.substring(i + 1, p_i1293_1_.length());

            if (i > 1)
            {
                s1 = p_i1293_1_.substring(0, i);
            }
        }

        this.resourceDomain = s1.toLowerCase();
        this.resourcePath = s2;
    }

    public String getResourcePath()
    {
        return this.resourcePath;
    }

    public String getResourceDomain()
    {
        return this.resourceDomain;
    }

    public String toString()
    {
        return this.resourceDomain + ":" + this.resourcePath;
    }

    public boolean equals(Object p_equals_1_)
    {
        if (this == p_equals_1_)
        {
            return true;
        }
        else if (!(p_equals_1_ instanceof ResourceLocation))
        {
            return false;
        }
        else
        {
            ResourceLocation resourcelocation = (ResourceLocation)p_equals_1_;
            return this.resourceDomain.equals(resourcelocation.resourceDomain) && this.resourcePath.equals(resourcelocation.resourcePath);
        }
    }

    public int hashCode()
    {
        return 31 * this.resourceDomain.hashCode() + this.resourcePath.hashCode();
    }
}