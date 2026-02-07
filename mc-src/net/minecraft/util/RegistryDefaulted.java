package net.minecraft.util;

public class RegistryDefaulted extends RegistrySimple
{
    /** Default object for this registry, returned when an object is not found. */
    private final Object defaultObject;
    private static final String __OBFID = "CL_00001198";

    public RegistryDefaulted(Object p_i1366_1_)
    {
        this.defaultObject = p_i1366_1_;
    }

    public Object getObject(Object p_82594_1_)
    {
        Object object1 = super.getObject(p_82594_1_);
        return object1 == null ? this.defaultObject : object1;
    }
}