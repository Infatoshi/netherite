package net.minecraft.profiler;

import com.google.common.collect.Maps;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.lang.management.ManagementFactory;
import java.lang.management.RuntimeMXBean;
import java.net.MalformedURLException;
import java.net.URL;
import java.util.HashMap;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Timer;
import java.util.TimerTask;
import java.util.UUID;
import java.util.Map.Entry;
import net.minecraft.util.HttpUtil;

public class PlayerUsageSnooper
{
    private final Map field_152773_a = Maps.newHashMap();
    private final Map field_152774_b = Maps.newHashMap();
    private final String uniqueID = UUID.randomUUID().toString();
    /** URL of the server to send the report to */
    private final URL serverUrl;
    private final IPlayerUsage playerStatsCollector;
    /** set to fire the snooperThread every 15 mins */
    private final Timer threadTrigger = new Timer("Snooper Timer", true);
    private final Object syncLock = new Object();
    private final long minecraftStartTimeMilis;
    private boolean isRunning;
    /** incremented on every getSelfCounterFor */
    private int selfCounter;
    private static final String __OBFID = "CL_00001515";

    public PlayerUsageSnooper(String p_i1563_1_, IPlayerUsage p_i1563_2_, long p_i1563_3_)
    {
        try
        {
            this.serverUrl = new URL("http://snoop.minecraft.net/" + p_i1563_1_ + "?version=" + 2);
        }
        catch (MalformedURLException malformedurlexception)
        {
            throw new IllegalArgumentException();
        }

        this.playerStatsCollector = p_i1563_2_;
        this.minecraftStartTimeMilis = p_i1563_3_;
    }

    /**
     * Note issuing start multiple times is not an error.
     */
    public void startSnooper()
    {
        if (!this.isRunning)
        {
            this.isRunning = true;
            this.func_152766_h();
            this.threadTrigger.schedule(new TimerTask()
            {
                private static final String __OBFID = "CL_00001516";
                public void run()
                {
                    if (PlayerUsageSnooper.this.playerStatsCollector.isSnooperEnabled())
                    {
                        HashMap hashmap;

                        synchronized (PlayerUsageSnooper.this.syncLock)
                        {
                            hashmap = new HashMap(PlayerUsageSnooper.this.field_152774_b);

                            if (PlayerUsageSnooper.this.selfCounter == 0)
                            {
                                hashmap.putAll(PlayerUsageSnooper.this.field_152773_a);
                            }

                            hashmap.put("snooper_count", Integer.valueOf(PlayerUsageSnooper.access$308(PlayerUsageSnooper.this)));
                            hashmap.put("snooper_token", PlayerUsageSnooper.this.uniqueID);
                        }

                        HttpUtil.func_151226_a(PlayerUsageSnooper.this.serverUrl, hashmap, true);
                    }
                }
            }, 0L, 900000L);
        }
    }

    private void func_152766_h()
    {
        this.addJvmArgsToSnooper();
        this.func_152768_a("snooper_token", this.uniqueID);
        this.func_152767_b("snooper_token", this.uniqueID);
        this.func_152767_b("os_name", System.getProperty("os.name"));
        this.func_152767_b("os_version", System.getProperty("os.version"));
        this.func_152767_b("os_architecture", System.getProperty("os.arch"));
        this.func_152767_b("java_version", System.getProperty("java.version"));
        this.func_152767_b("version", "1.7.10");
        this.playerStatsCollector.addServerTypeToSnooper(this);
    }

    private void addJvmArgsToSnooper()
    {
        RuntimeMXBean runtimemxbean = ManagementFactory.getRuntimeMXBean();
        List list = runtimemxbean.getInputArguments();
        int i = 0;
        Iterator iterator = list.iterator();

        while (iterator.hasNext())
        {
            String s = (String)iterator.next();

            if (s.startsWith("-X"))
            {
                this.func_152768_a("jvm_arg[" + i++ + "]", s);
            }
        }

        this.func_152768_a("jvm_args", Integer.valueOf(i));
    }

    public void addMemoryStatsToSnooper()
    {
        this.func_152767_b("memory_total", Long.valueOf(Runtime.getRuntime().totalMemory()));
        this.func_152767_b("memory_max", Long.valueOf(Runtime.getRuntime().maxMemory()));
        this.func_152767_b("memory_free", Long.valueOf(Runtime.getRuntime().freeMemory()));
        this.func_152767_b("cpu_cores", Integer.valueOf(Runtime.getRuntime().availableProcessors()));
        this.playerStatsCollector.addServerStatsToSnooper(this);
    }

    public void func_152768_a(String p_152768_1_, Object p_152768_2_)
    {
        Object object1 = this.syncLock;

        synchronized (this.syncLock)
        {
            this.field_152774_b.put(p_152768_1_, p_152768_2_);
        }
    }

    public void func_152767_b(String p_152767_1_, Object p_152767_2_)
    {
        Object object1 = this.syncLock;

        synchronized (this.syncLock)
        {
            this.field_152773_a.put(p_152767_1_, p_152767_2_);
        }
    }

    @SideOnly(Side.CLIENT)
    public Map getCurrentStats()
    {
        LinkedHashMap linkedhashmap = new LinkedHashMap();
        Object object = this.syncLock;

        synchronized (this.syncLock)
        {
            this.addMemoryStatsToSnooper();
            Iterator iterator = this.field_152773_a.entrySet().iterator();
            Entry entry;

            while (iterator.hasNext())
            {
                entry = (Entry)iterator.next();
                linkedhashmap.put(entry.getKey(), entry.getValue().toString());
            }

            iterator = this.field_152774_b.entrySet().iterator();

            while (iterator.hasNext())
            {
                entry = (Entry)iterator.next();
                linkedhashmap.put(entry.getKey(), entry.getValue().toString());
            }

            return linkedhashmap;
        }
    }

    public boolean isSnooperRunning()
    {
        return this.isRunning;
    }

    public void stopSnooper()
    {
        this.threadTrigger.cancel();
    }

    @SideOnly(Side.CLIENT)
    public String getUniqueID()
    {
        return this.uniqueID;
    }

    /**
     * Returns the saved value of System#currentTimeMillis when the game started
     */
    public long getMinecraftStartTimeMillis()
    {
        return this.minecraftStartTimeMilis;
    }

    static int access$308(PlayerUsageSnooper p_access$308_0_)
    {
        return p_access$308_0_.selfCounter++;
    }
}