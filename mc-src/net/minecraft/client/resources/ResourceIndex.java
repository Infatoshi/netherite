package net.minecraft.client.resources;

import com.google.common.base.Charsets;
import com.google.common.collect.Maps;
import com.google.common.io.Files;
import com.google.gson.JsonObject;
import com.google.gson.JsonParseException;
import com.google.gson.JsonParser;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileNotFoundException;
import java.util.Iterator;
import java.util.Map;
import java.util.Map.Entry;
import net.minecraft.util.JsonUtils;
import org.apache.commons.io.IOUtils;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

@SideOnly(Side.CLIENT)
public class ResourceIndex
{
    private static final Logger field_152783_a = LogManager.getLogger();
    private final Map field_152784_b = Maps.newHashMap();
    private static final String __OBFID = "CL_00001831";

    public ResourceIndex(File p_i1047_1_, String p_i1047_2_)
    {
        if (p_i1047_2_ != null)
        {
            File file2 = new File(p_i1047_1_, "objects");
            File file3 = new File(p_i1047_1_, "indexes/" + p_i1047_2_ + ".json");
            BufferedReader bufferedreader = null;

            try
            {
                bufferedreader = Files.newReader(file3, Charsets.UTF_8);
                JsonObject jsonobject = (new JsonParser()).parse(bufferedreader).getAsJsonObject();
                JsonObject jsonobject1 = JsonUtils.getJsonObjectFieldOrDefault(jsonobject, "objects", (JsonObject)null);

                if (jsonobject1 != null)
                {
                    Iterator iterator = jsonobject1.entrySet().iterator();

                    while (iterator.hasNext())
                    {
                        Entry entry = (Entry)iterator.next();
                        JsonObject jsonobject2 = (JsonObject)entry.getValue();
                        String s1 = (String)entry.getKey();
                        String[] astring = s1.split("/", 2);
                        String s2 = astring.length == 1 ? astring[0] : astring[0] + ":" + astring[1];
                        String s3 = JsonUtils.getJsonObjectStringFieldValue(jsonobject2, "hash");
                        File file4 = new File(file2, s3.substring(0, 2) + "/" + s3);
                        this.field_152784_b.put(s2, file4);
                    }
                }
            }
            catch (JsonParseException jsonparseexception)
            {
                field_152783_a.error("Unable to parse resource index file: " + file3);
            }
            catch (FileNotFoundException filenotfoundexception)
            {
                field_152783_a.error("Can\'t find the resource index file: " + file3);
            }
            finally
            {
                IOUtils.closeQuietly(bufferedreader);
            }
        }
    }

    public Map func_152782_a()
    {
        return this.field_152784_b;
    }
}