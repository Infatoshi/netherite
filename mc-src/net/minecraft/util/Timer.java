package net.minecraft.util;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.client.Minecraft;

@SideOnly(Side.CLIENT)
public class Timer
{
    /** The number of timer ticks per second of real time */
    float ticksPerSecond;
    /** The time reported by the high-resolution clock at the last call of updateTimer(), in seconds */
    private double lastHRTime;
    /** How many full ticks have turned over since the last call to updateTimer(), capped at 10. */
    public int elapsedTicks;
    /**
     * How much time has elapsed since the last tick, in ticks, for use by display rendering routines (range: 0.0 -
     * 1.0).  This field is frozen if the display is paused to eliminate jitter.
     */
    public float renderPartialTicks;
    /**
     * A multiplier to make the timer (and therefore the game) go faster or slower.  0.5 makes the game run at half-
     * speed.
     */
    public float timerSpeed = 1.0F;
    /** How much time has elapsed since the last tick, in ticks (range: 0.0 - 1.0). */
    public float elapsedPartialTicks;
    /** The time reported by the system clock at the last sync, in milliseconds */
    private long lastSyncSysClock;
    /** The time reported by the high-resolution clock at the last sync, in milliseconds */
    private long lastSyncHRClock;
    private long field_74285_i;
    /** A ratio used to sync the high-resolution clock to the system clock, updated once per second */
    private double timeSyncAdjustment = 1.0D;
    private static final String __OBFID = "CL_00000658";

    public Timer(float p_i1018_1_)
    {
        this.ticksPerSecond = p_i1018_1_;
        this.lastSyncSysClock = Minecraft.getSystemTime();
        this.lastSyncHRClock = System.nanoTime() / 1000000L;
    }

    /**
     * Updates all fields of the Timer using the current time
     */
    public void updateTimer()
    {
        long i = Minecraft.getSystemTime();
        long j = i - this.lastSyncSysClock;
        long k = System.nanoTime() / 1000000L;
        double d0 = (double)k / 1000.0D;

        if (j <= 1000L && j >= 0L)
        {
            this.field_74285_i += j;

            if (this.field_74285_i > 1000L)
            {
                long l = k - this.lastSyncHRClock;
                double d1 = (double)this.field_74285_i / (double)l;
                this.timeSyncAdjustment += (d1 - this.timeSyncAdjustment) * 0.20000000298023224D;
                this.lastSyncHRClock = k;
                this.field_74285_i = 0L;
            }

            if (this.field_74285_i < 0L)
            {
                this.lastSyncHRClock = k;
            }
        }
        else
        {
            this.lastHRTime = d0;
        }

        this.lastSyncSysClock = i;
        double d2 = (d0 - this.lastHRTime) * this.timeSyncAdjustment;
        this.lastHRTime = d0;

        if (d2 < 0.0D)
        {
            d2 = 0.0D;
        }

        if (d2 > 1.0D)
        {
            d2 = 1.0D;
        }

        this.elapsedPartialTicks = (float)((double)this.elapsedPartialTicks + d2 * (double)this.timerSpeed * (double)this.ticksPerSecond);
        this.elapsedTicks = (int)this.elapsedPartialTicks;
        this.elapsedPartialTicks -= (float)this.elapsedTicks;

        if (this.elapsedTicks > 10)
        {
            this.elapsedTicks = 10;
        }

        this.renderPartialTicks = this.elapsedPartialTicks;
    }
}