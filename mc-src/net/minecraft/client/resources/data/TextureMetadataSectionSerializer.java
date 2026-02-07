package net.minecraft.client.resources.data;

import com.google.common.collect.Lists;
import com.google.gson.JsonArray;
import com.google.gson.JsonDeserializationContext;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParseException;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.lang.reflect.Type;
import java.util.ArrayList;
import net.minecraft.util.JsonUtils;

@SideOnly(Side.CLIENT)
public class TextureMetadataSectionSerializer extends BaseMetadataSectionSerializer
{
    private static final String __OBFID = "CL_00001115";

    public TextureMetadataSection deserialize(JsonElement p_deserialize_1_, Type p_deserialize_2_, JsonDeserializationContext p_deserialize_3_)
    {
        JsonObject jsonobject = p_deserialize_1_.getAsJsonObject();
        boolean flag = JsonUtils.getJsonObjectBooleanFieldValueOrDefault(jsonobject, "blur", false);
        boolean flag1 = JsonUtils.getJsonObjectBooleanFieldValueOrDefault(jsonobject, "clamp", false);
        ArrayList arraylist = Lists.newArrayList();

        if (jsonobject.has("mipmaps"))
        {
            try
            {
                JsonArray jsonarray = jsonobject.getAsJsonArray("mipmaps");

                for (int i = 0; i < jsonarray.size(); ++i)
                {
                    JsonElement jsonelement1 = jsonarray.get(i);

                    if (jsonelement1.isJsonPrimitive())
                    {
                        try
                        {
                            arraylist.add(Integer.valueOf(jsonelement1.getAsInt()));
                        }
                        catch (NumberFormatException numberformatexception)
                        {
                            throw new JsonParseException("Invalid texture->mipmap->" + i + ": expected number, was " + jsonelement1, numberformatexception);
                        }
                    }
                    else if (jsonelement1.isJsonObject())
                    {
                        throw new JsonParseException("Invalid texture->mipmap->" + i + ": expected number, was " + jsonelement1);
                    }
                }
            }
            catch (ClassCastException classcastexception)
            {
                throw new JsonParseException("Invalid texture->mipmaps: expected array, was " + jsonobject.get("mipmaps"), classcastexception);
            }
        }

        return new TextureMetadataSection(flag, flag1, arraylist);
    }

    /**
     * The name of this section type as it appears in JSON.
     */
    public String getSectionName()
    {
        return "texture";
    }
}