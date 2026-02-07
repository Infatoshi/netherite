package net.minecraft.entity.ai.attributes;

import com.google.common.collect.Multimap;
import java.util.Collection;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;
import java.util.Map.Entry;
import net.minecraft.server.management.LowerStringMap;

public abstract class BaseAttributeMap
{
    protected final Map attributes = new HashMap();
    protected final Map attributesByName = new LowerStringMap();
    private static final String __OBFID = "CL_00001566";

    public IAttributeInstance getAttributeInstance(IAttribute p_111151_1_)
    {
        return (IAttributeInstance)this.attributes.get(p_111151_1_);
    }

    public IAttributeInstance getAttributeInstanceByName(String p_111152_1_)
    {
        return (IAttributeInstance)this.attributesByName.get(p_111152_1_);
    }

    /**
     * Registers an attribute with this AttributeMap, returns a modifiable AttributeInstance associated with this map
     */
    public abstract IAttributeInstance registerAttribute(IAttribute p_111150_1_);

    public Collection getAllAttributes()
    {
        return this.attributesByName.values();
    }

    public void addAttributeInstance(ModifiableAttributeInstance p_111149_1_) {}

    public void removeAttributeModifiers(Multimap p_111148_1_)
    {
        Iterator iterator = p_111148_1_.entries().iterator();

        while (iterator.hasNext())
        {
            Entry entry = (Entry)iterator.next();
            IAttributeInstance iattributeinstance = this.getAttributeInstanceByName((String)entry.getKey());

            if (iattributeinstance != null)
            {
                iattributeinstance.removeModifier((AttributeModifier)entry.getValue());
            }
        }
    }

    public void applyAttributeModifiers(Multimap p_111147_1_)
    {
        Iterator iterator = p_111147_1_.entries().iterator();

        while (iterator.hasNext())
        {
            Entry entry = (Entry)iterator.next();
            IAttributeInstance iattributeinstance = this.getAttributeInstanceByName((String)entry.getKey());

            if (iattributeinstance != null)
            {
                iattributeinstance.removeModifier((AttributeModifier)entry.getValue());
                iattributeinstance.applyModifier((AttributeModifier)entry.getValue());
            }
        }
    }
}