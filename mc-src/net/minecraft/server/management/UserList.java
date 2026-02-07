package net.minecraft.server.management;

import com.google.common.base.Charsets;
import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import com.google.common.io.Files;
import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonDeserializationContext;
import com.google.gson.JsonDeserializer;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonSerializationContext;
import com.google.gson.JsonSerializer;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.IOException;
import java.lang.reflect.ParameterizedType;
import java.lang.reflect.Type;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import org.apache.commons.io.IOUtils;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

public class UserList
{
    protected static final Logger field_152693_a = LogManager.getLogger();
    protected final Gson field_152694_b;
    private final File field_152695_c;
    private final Map field_152696_d = Maps.newHashMap();
    private boolean field_152697_e = true;
    private static final ParameterizedType field_152698_f = new ParameterizedType()
    {
        private static final String __OBFID = "CL_00001875";
        public Type[] getActualTypeArguments()
        {
            return new Type[] {UserListEntry.class};
        }
        public Type getRawType()
        {
            return List.class;
        }
        public Type getOwnerType()
        {
            return null;
        }
    };
    private static final String __OBFID = "CL_00001876";

    public UserList(File p_i1144_1_)
    {
        this.field_152695_c = p_i1144_1_;
        GsonBuilder gsonbuilder = (new GsonBuilder()).setPrettyPrinting();
        gsonbuilder.registerTypeHierarchyAdapter(UserListEntry.class, new UserList.Serializer(null));
        this.field_152694_b = gsonbuilder.create();
    }

    public boolean func_152689_b()
    {
        return this.field_152697_e;
    }

    public void func_152686_a(boolean p_152686_1_)
    {
        this.field_152697_e = p_152686_1_;
    }

    public void func_152687_a(UserListEntry p_152687_1_)
    {
        this.field_152696_d.put(this.func_152681_a(p_152687_1_.func_152640_f()), p_152687_1_);

        try
        {
            this.func_152678_f();
        }
        catch (IOException ioexception)
        {
            field_152693_a.warn("Could not save the list after adding a user.", ioexception);
        }
    }

    public UserListEntry func_152683_b(Object p_152683_1_)
    {
        this.func_152680_h();
        return (UserListEntry)this.field_152696_d.get(this.func_152681_a(p_152683_1_));
    }

    public void func_152684_c(Object p_152684_1_)
    {
        this.field_152696_d.remove(this.func_152681_a(p_152684_1_));

        try
        {
            this.func_152678_f();
        }
        catch (IOException ioexception)
        {
            field_152693_a.warn("Could not save the list after removing a user.", ioexception);
        }
    }

    @SideOnly(Side.SERVER)
    public File func_152691_c()
    {
        return this.field_152695_c;
    }

    public String[] func_152685_a()
    {
        return (String[])this.field_152696_d.keySet().toArray(new String[this.field_152696_d.size()]);
    }

    protected String func_152681_a(Object p_152681_1_)
    {
        return p_152681_1_.toString();
    }

    protected boolean func_152692_d(Object p_152692_1_)
    {
        return this.field_152696_d.containsKey(this.func_152681_a(p_152692_1_));
    }

    private void func_152680_h()
    {
        ArrayList arraylist = Lists.newArrayList();
        Iterator iterator = this.field_152696_d.values().iterator();

        while (iterator.hasNext())
        {
            UserListEntry userlistentry = (UserListEntry)iterator.next();

            if (userlistentry.hasBanExpired())
            {
                arraylist.add(userlistentry.func_152640_f());
            }
        }

        iterator = arraylist.iterator();

        while (iterator.hasNext())
        {
            Object object = iterator.next();
            this.field_152696_d.remove(object);
        }
    }

    protected UserListEntry func_152682_a(JsonObject p_152682_1_)
    {
        return new UserListEntry((Object)null, p_152682_1_);
    }

    protected Map func_152688_e()
    {
        return this.field_152696_d;
    }

    public void func_152678_f() throws IOException
    {
        Collection collection = this.field_152696_d.values();
        String s = this.field_152694_b.toJson(collection);
        BufferedWriter bufferedwriter = null;

        try
        {
            bufferedwriter = Files.newWriter(this.field_152695_c, Charsets.UTF_8);
            bufferedwriter.write(s);
        }
        finally
        {
            IOUtils.closeQuietly(bufferedwriter);
        }
    }

    @SideOnly(Side.SERVER)
    public boolean func_152690_d()
    {
        return this.field_152696_d.size() < 1;
    }

    @SideOnly(Side.SERVER)
    public void func_152679_g() throws IOException
    {
        Collection collection = null;
        BufferedReader bufferedreader = null;

        try
        {
            bufferedreader = Files.newReader(this.field_152695_c, Charsets.UTF_8);
            collection = (Collection)this.field_152694_b.fromJson(bufferedreader, field_152698_f);
        }
        finally
        {
            IOUtils.closeQuietly(bufferedreader);
        }

        if (collection != null)
        {
            this.field_152696_d.clear();
            Iterator iterator = collection.iterator();

            while (iterator.hasNext())
            {
                UserListEntry userlistentry = (UserListEntry)iterator.next();

                if (userlistentry.func_152640_f() != null)
                {
                    this.field_152696_d.put(this.func_152681_a(userlistentry.func_152640_f()), userlistentry);
                }
            }
        }
    }

    class Serializer implements JsonDeserializer, JsonSerializer
    {
        private static final String __OBFID = "CL_00001874";

        private Serializer() {}

        public JsonElement func_152751_a(UserListEntry p_152751_1_, Type p_152751_2_, JsonSerializationContext p_152751_3_)
        {
            JsonObject jsonobject = new JsonObject();
            p_152751_1_.func_152641_a(jsonobject);
            return jsonobject;
        }

        public UserListEntry func_152750_a(JsonElement p_152750_1_, Type p_152750_2_, JsonDeserializationContext p_152750_3_)
        {
            if (p_152750_1_.isJsonObject())
            {
                JsonObject jsonobject = p_152750_1_.getAsJsonObject();
                UserListEntry userlistentry = UserList.this.func_152682_a(jsonobject);
                return userlistentry;
            }
            else
            {
                return null;
            }
        }

        public JsonElement serialize(Object p_serialize_1_, Type p_serialize_2_, JsonSerializationContext p_serialize_3_)
        {
            return this.func_152751_a((UserListEntry)p_serialize_1_, p_serialize_2_, p_serialize_3_);
        }

        public Object deserialize(JsonElement p_deserialize_1_, Type p_deserialize_2_, JsonDeserializationContext p_deserialize_3_)
        {
            return this.func_152750_a(p_deserialize_1_, p_deserialize_2_, p_deserialize_3_);
        }

        Serializer(Object p_i1141_2_)
        {
            this();
        }
    }
}