package net.minecraft.util;

import com.google.common.collect.BiMap;
import com.google.common.collect.HashBiMap;
import java.util.Iterator;
import java.util.Map;

public class RegistryNamespaced extends RegistrySimple implements IObjectIntIterable
{
    /** The backing store that maps Integers to objects. */
    protected ObjectIntIdentityMap underlyingIntegerMap = new ObjectIntIdentityMap();
    protected final Map field_148758_b;
    private static final String __OBFID = "CL_00001206";

    public RegistryNamespaced()
    {
        this.field_148758_b = ((BiMap)this.registryObjects).inverse();
    }

    /**
     * Adds a new object to this registry, keyed by both the given integer ID and the given string.
     */
    public void addObject(int p_148756_1_, String p_148756_2_, Object p_148756_3_)
    {
        this.underlyingIntegerMap.func_148746_a(p_148756_3_, p_148756_1_);
        this.putObject(ensureNamespaced(p_148756_2_), p_148756_3_);
    }

    /**
     * Creates the Map we will use to map keys to their registered values.
     */
    protected Map createUnderlyingMap()
    {
        return HashBiMap.create();
    }

    public Object getObject(String p_82594_1_)
    {
        return super.getObject(ensureNamespaced(p_82594_1_));
    }

    /**
     * Gets the name we use to identify the given object.
     */
    public String getNameForObject(Object p_148750_1_)
    {
        return (String)this.field_148758_b.get(p_148750_1_);
    }

    /**
     * Does this registry contain an entry for the given key?
     */
    public boolean containsKey(String p_148741_1_)
    {
        return super.containsKey(ensureNamespaced(p_148741_1_));
    }

    /**
     * Gets the integer ID we use to identify the given object.
     */
    public int getIDForObject(Object p_148757_1_)
    {
        return this.underlyingIntegerMap.func_148747_b(p_148757_1_);
    }

    /**
     * Gets the object identified by the given ID.
     */
    public Object getObjectById(int p_148754_1_)
    {
        return this.underlyingIntegerMap.func_148745_a(p_148754_1_);
    }

    public Iterator iterator()
    {
        return this.underlyingIntegerMap.iterator();
    }

    /**
     * Gets a value indicating whether this registry contains an object that can be identified by the given integer
     * value
     */
    public boolean containsId(int p_148753_1_)
    {
        return this.underlyingIntegerMap.func_148744_b(p_148753_1_);
    }

    /**
     * Ensures that the given name is indicated by a colon-delimited namespace, prepending "minecraft:" if it is not
     * already.
     */
    protected static String ensureNamespaced(String p_148755_0_)
    {
        return p_148755_0_.indexOf(58) == -1 ? "minecraft:" + p_148755_0_ : p_148755_0_;
    }

    /**
     * Does this registry contain an entry for the given key?
     */
    public boolean containsKey(Object p_148741_1_)
    {
        return this.containsKey((String)p_148741_1_);
    }

    public Object getObject(Object p_82594_1_)
    {
        return this.getObject((String)p_82594_1_);
    }
}