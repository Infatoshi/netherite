package net.minecraft.client.resources.data;

import com.google.common.collect.Sets;
import com.google.gson.JsonDeserializationContext;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParseException;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.lang.reflect.Type;
import java.util.HashSet;
import java.util.Iterator;
import java.util.Map.Entry;
import net.minecraft.client.resources.Language;
import net.minecraft.util.JsonUtils;

@SideOnly(Side.CLIENT)
public class LanguageMetadataSectionSerializer extends BaseMetadataSectionSerializer
{
    private static final String __OBFID = "CL_00001111";

    public LanguageMetadataSection deserialize(JsonElement p_deserialize_1_, Type p_deserialize_2_, JsonDeserializationContext p_deserialize_3_)
    {
        JsonObject jsonobject = p_deserialize_1_.getAsJsonObject();
        HashSet hashset = Sets.newHashSet();
        Iterator iterator = jsonobject.entrySet().iterator();
        String s;
        String s1;
        String s2;
        boolean flag;

        do
        {
            if (!iterator.hasNext())
            {
                return new LanguageMetadataSection(hashset);
            }

            Entry entry = (Entry)iterator.next();
            s = (String)entry.getKey();
            JsonObject jsonobject1 = JsonUtils.getJsonElementAsJsonObject((JsonElement)entry.getValue(), "language");
            s1 = JsonUtils.getJsonObjectStringFieldValue(jsonobject1, "region");
            s2 = JsonUtils.getJsonObjectStringFieldValue(jsonobject1, "name");
            flag = JsonUtils.getJsonObjectBooleanFieldValueOrDefault(jsonobject1, "bidirectional", false);

            if (s1.isEmpty())
            {
                throw new JsonParseException("Invalid language->\'" + s + "\'->region: empty value");
            }

            if (s2.isEmpty())
            {
                throw new JsonParseException("Invalid language->\'" + s + "\'->name: empty value");
            }
        }
        while (hashset.add(new Language(s, s1, s2, flag)));

        throw new JsonParseException("Duplicate language->\'" + s + "\' defined");
    }

    /**
     * The name of this section type as it appears in JSON.
     */
    public String getSectionName()
    {
        return "language";
    }
}