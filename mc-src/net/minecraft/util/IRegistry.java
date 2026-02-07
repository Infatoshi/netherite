package net.minecraft.util;

public interface IRegistry
{
    Object getObject(Object p_82594_1_);

    /**
     * Register an object on this registry.
     */
    void putObject(Object p_82595_1_, Object p_82595_2_);
}