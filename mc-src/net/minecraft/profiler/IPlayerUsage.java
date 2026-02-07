package net.minecraft.profiler;

public interface IPlayerUsage
{
    void addServerStatsToSnooper(PlayerUsageSnooper p_70000_1_);

    void addServerTypeToSnooper(PlayerUsageSnooper p_70001_1_);

    /**
     * Returns whether snooping is enabled or not.
     */
    boolean isSnooperEnabled();
}