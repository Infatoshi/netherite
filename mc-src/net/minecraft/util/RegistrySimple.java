package net.minecraft.util;

import com.google.common.collect.Maps;
import java.util.Collections;
import java.util.Map;
import java.util.Set;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

public class RegistrySimple implements IRegistry
{
    private static final Logger logger = LogManager.getLogger();
    /** Objects registered on this registry. */
    protected final Map registryObjects = this.createUnderlyingMap();
    private static final String __OBFID = "CL_00001210";

    /**
     * Creates the Map we will use to map keys to their registered values.
     */
    protected Map createUnderlyingMap()
    {
        return Maps.newHashMap();
    }

    public Object getObject(Object p_82594_1_)
    {
        return this.registryObjects.get(p_82594_1_);
    }

    /**
     * Register an object on this registry.
     */
    public void putObject(Object p_82595_1_, Object p_82595_2_)
    {
        if (this.registryObjects.containsKey(p_82595_1_))
        {
            logger.debug("Adding duplicate key \'" + p_82595_1_ + "\' to registry");
        }

        this.registryObjects.put(p_82595_1_, p_82595_2_);
    }

    /**
     * Gets all the keys recognized by this registry.
     */
    public Set getKeys()
    {
        return Collections.unmodifiableSet(this.registryObjects.keySet());
    }

    /**
     * Does this registry contain an entry for the given key?
     */
    public boolean containsKey(Object p_148741_1_)
    {
        return this.registryObjects.containsKey(p_148741_1_);
    }
}