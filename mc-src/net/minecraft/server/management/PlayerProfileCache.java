package net.minecraft.server.management;

import com.google.common.base.Charsets;
import com.google.common.collect.Iterators;
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
import com.mojang.authlib.Agent;
import com.mojang.authlib.GameProfile;
import com.mojang.authlib.ProfileLookupCallback;
import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.lang.reflect.ParameterizedType;
import java.lang.reflect.Type;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.Date;
import java.util.Iterator;
import java.util.LinkedList;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.UUID;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.server.MinecraftServer;
import org.apache.commons.io.IOUtils;

public class PlayerProfileCache
{
    public static final SimpleDateFormat field_152659_a = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss Z");
    private final Map field_152661_c = Maps.newHashMap();
    private final Map field_152662_d = Maps.newHashMap();
    private final LinkedList field_152663_e = Lists.newLinkedList();
    private final MinecraftServer field_152664_f;
    protected final Gson field_152660_b;
    private final File field_152665_g;
    private static final ParameterizedType field_152666_h = new ParameterizedType()
    {
        private static final String __OBFID = "CL_00001886";
        public Type[] getActualTypeArguments()
        {
            return new Type[] {PlayerProfileCache.ProfileEntry.class};
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
    private static final String __OBFID = "CL_00001888";

    public PlayerProfileCache(MinecraftServer p_i1171_1_, File p_i1171_2_)
    {
        this.field_152664_f = p_i1171_1_;
        this.field_152665_g = p_i1171_2_;
        GsonBuilder gsonbuilder = new GsonBuilder();
        gsonbuilder.registerTypeHierarchyAdapter(PlayerProfileCache.ProfileEntry.class, new PlayerProfileCache.Serializer(null));
        this.field_152660_b = gsonbuilder.create();
        this.func_152657_b();
    }

    private static GameProfile func_152650_a(MinecraftServer p_152650_0_, String p_152650_1_)
    {
        final GameProfile[] agameprofile = new GameProfile[1];
        ProfileLookupCallback profilelookupcallback = new ProfileLookupCallback()
        {
            private static final String __OBFID = "CL_00001887";
            public void onProfileLookupSucceeded(GameProfile p_onProfileLookupSucceeded_1_)
            {
                agameprofile[0] = p_onProfileLookupSucceeded_1_;
            }
            public void onProfileLookupFailed(GameProfile p_onProfileLookupFailed_1_, Exception p_onProfileLookupFailed_2_)
            {
                agameprofile[0] = null;
            }
        };
        p_152650_0_.func_152359_aw().findProfilesByNames(new String[] {p_152650_1_}, Agent.MINECRAFT, profilelookupcallback);

        if (!p_152650_0_.isServerInOnlineMode() && agameprofile[0] == null)
        {
            UUID uuid = EntityPlayer.func_146094_a(new GameProfile((UUID)null, p_152650_1_));
            GameProfile gameprofile = new GameProfile(uuid, p_152650_1_);
            profilelookupcallback.onProfileLookupSucceeded(gameprofile);
        }

        return agameprofile[0];
    }

    public void func_152649_a(GameProfile p_152649_1_)
    {
        this.func_152651_a(p_152649_1_, (Date)null);
    }

    private void func_152651_a(GameProfile p_152651_1_, Date p_152651_2_)
    {
        UUID uuid = p_152651_1_.getId();

        if (p_152651_2_ == null)
        {
            Calendar calendar = Calendar.getInstance();
            calendar.setTime(new Date());
            calendar.add(2, 1);
            p_152651_2_ = calendar.getTime();
        }

        String s = p_152651_1_.getName().toLowerCase(Locale.ROOT);
        PlayerProfileCache.ProfileEntry profileentry = new PlayerProfileCache.ProfileEntry(p_152651_1_, p_152651_2_, null);
        LinkedList linkedlist = this.field_152663_e;

        synchronized (this.field_152663_e)
        {
            if (this.field_152662_d.containsKey(uuid))
            {
                PlayerProfileCache.ProfileEntry profileentry1 = (PlayerProfileCache.ProfileEntry)this.field_152662_d.get(uuid);
                this.field_152661_c.remove(profileentry1.func_152668_a().getName().toLowerCase(Locale.ROOT));
                this.field_152661_c.put(p_152651_1_.getName().toLowerCase(Locale.ROOT), profileentry);
                this.field_152663_e.remove(p_152651_1_);
            }
            else
            {
                this.field_152662_d.put(uuid, profileentry);
                this.field_152661_c.put(s, profileentry);
            }

            this.field_152663_e.addFirst(p_152651_1_);
        }
    }

    public GameProfile func_152655_a(String p_152655_1_)
    {
        String s1 = p_152655_1_.toLowerCase(Locale.ROOT);
        PlayerProfileCache.ProfileEntry profileentry = (PlayerProfileCache.ProfileEntry)this.field_152661_c.get(s1);

        if (profileentry != null && (new Date()).getTime() >= profileentry.field_152673_c.getTime())
        {
            this.field_152662_d.remove(profileentry.func_152668_a().getId());
            this.field_152661_c.remove(profileentry.func_152668_a().getName().toLowerCase(Locale.ROOT));
            LinkedList linkedlist = this.field_152663_e;

            synchronized (this.field_152663_e)
            {
                this.field_152663_e.remove(profileentry.func_152668_a());
            }

            profileentry = null;
        }

        GameProfile gameprofile;

        if (profileentry != null)
        {
            gameprofile = profileentry.func_152668_a();
            LinkedList linkedlist1 = this.field_152663_e;

            synchronized (this.field_152663_e)
            {
                this.field_152663_e.remove(gameprofile);
                this.field_152663_e.addFirst(gameprofile);
            }
        }
        else
        {
            gameprofile = func_152650_a(this.field_152664_f, s1);

            if (gameprofile != null)
            {
                this.func_152649_a(gameprofile);
                profileentry = (PlayerProfileCache.ProfileEntry)this.field_152661_c.get(s1);
            }
        }

        this.func_152658_c();
        return profileentry == null ? null : profileentry.func_152668_a();
    }

    public String[] func_152654_a()
    {
        ArrayList arraylist = Lists.newArrayList(this.field_152661_c.keySet());
        return (String[])arraylist.toArray(new String[arraylist.size()]);
    }

    public GameProfile func_152652_a(UUID p_152652_1_)
    {
        PlayerProfileCache.ProfileEntry profileentry = (PlayerProfileCache.ProfileEntry)this.field_152662_d.get(p_152652_1_);
        return profileentry == null ? null : profileentry.func_152668_a();
    }

    private PlayerProfileCache.ProfileEntry func_152653_b(UUID p_152653_1_)
    {
        PlayerProfileCache.ProfileEntry profileentry = (PlayerProfileCache.ProfileEntry)this.field_152662_d.get(p_152653_1_);

        if (profileentry != null)
        {
            GameProfile gameprofile = profileentry.func_152668_a();
            LinkedList linkedlist = this.field_152663_e;

            synchronized (this.field_152663_e)
            {
                this.field_152663_e.remove(gameprofile);
                this.field_152663_e.addFirst(gameprofile);
            }
        }

        return profileentry;
    }

    public void func_152657_b()
    {
        List list = null;
        BufferedReader bufferedreader = null;
        {
            try
            {
                bufferedreader = Files.newReader(this.field_152665_g, Charsets.UTF_8);
                list = (List)this.field_152660_b.fromJson(bufferedreader, field_152666_h);

        if (list != null)
        {
            this.field_152661_c.clear();
            this.field_152662_d.clear();
            LinkedList linkedlist = this.field_152663_e;

            synchronized (this.field_152663_e)
            {
                this.field_152663_e.clear();
            }

            list = Lists.reverse(list);
            Iterator iterator = list.iterator();

            while (iterator.hasNext())
            {
                PlayerProfileCache.ProfileEntry profileentry = (PlayerProfileCache.ProfileEntry)iterator.next();

                if (profileentry != null)
                {
                    this.func_152651_a(profileentry.func_152668_a(), profileentry.func_152670_b());
                }
            }
        }
            }
            catch (FileNotFoundException filenotfoundexception)
            {
                ;
            }
            catch (com.google.gson.JsonParseException parsefail)
            {
                // No op - the cache can quietly rebuild if it's junk
            }
            finally
            {
                IOUtils.closeQuietly(bufferedreader);
            }
        }
    }

    public void func_152658_c()
    {
        String s = this.field_152660_b.toJson(this.func_152656_a(1000));
        BufferedWriter bufferedwriter = null;

        try
        {
            bufferedwriter = Files.newWriter(this.field_152665_g, Charsets.UTF_8);
            bufferedwriter.write(s);
            return;
        }
        catch (FileNotFoundException filenotfoundexception)
        {
            ;
        }
        catch (IOException ioexception)
        {
            return;
        }
        finally
        {
            IOUtils.closeQuietly(bufferedwriter);
        }
    }

    private List func_152656_a(int p_152656_1_)
    {
        ArrayList arraylist = Lists.newArrayList();
        LinkedList linkedlist = this.field_152663_e;
        ArrayList arraylist1;

        synchronized (this.field_152663_e)
        {
            arraylist1 = Lists.newArrayList(Iterators.limit(this.field_152663_e.iterator(), p_152656_1_));
        }

        Iterator iterator = arraylist1.iterator();

        while (iterator.hasNext())
        {
            GameProfile gameprofile = (GameProfile)iterator.next();
            PlayerProfileCache.ProfileEntry profileentry = this.func_152653_b(gameprofile.getId());

            if (profileentry != null)
            {
                arraylist.add(profileentry);
            }
        }

        return arraylist;
    }

    class ProfileEntry
    {
        private final GameProfile field_152672_b;
        private final Date field_152673_c;
        private static final String __OBFID = "CL_00001885";

        private ProfileEntry(GameProfile p_i1165_2_, Date p_i1165_3_)
        {
            this.field_152672_b = p_i1165_2_;
            this.field_152673_c = p_i1165_3_;
        }

        public GameProfile func_152668_a()
        {
            return this.field_152672_b;
        }

        public Date func_152670_b()
        {
            return this.field_152673_c;
        }

        ProfileEntry(GameProfile p_i1166_2_, Date p_i1166_3_, Object p_i1166_4_)
        {
            this(p_i1166_2_, p_i1166_3_);
        }
    }

    class Serializer implements JsonDeserializer, JsonSerializer
    {
        private static final String __OBFID = "CL_00001884";

        private Serializer() {}

        public JsonElement func_152676_a(PlayerProfileCache.ProfileEntry p_152676_1_, Type p_152676_2_, JsonSerializationContext p_152676_3_)
        {
            JsonObject jsonobject = new JsonObject();
            jsonobject.addProperty("name", p_152676_1_.func_152668_a().getName());
            UUID uuid = p_152676_1_.func_152668_a().getId();
            jsonobject.addProperty("uuid", uuid == null ? "" : uuid.toString());
            jsonobject.addProperty("expiresOn", PlayerProfileCache.field_152659_a.format(p_152676_1_.func_152670_b()));
            return jsonobject;
        }

        public PlayerProfileCache.ProfileEntry func_152675_a(JsonElement p_152675_1_, Type p_152675_2_, JsonDeserializationContext p_152675_3_)
        {
            if (p_152675_1_.isJsonObject())
            {
                JsonObject jsonobject = p_152675_1_.getAsJsonObject();
                JsonElement jsonelement1 = jsonobject.get("name");
                JsonElement jsonelement2 = jsonobject.get("uuid");
                JsonElement jsonelement3 = jsonobject.get("expiresOn");

                if (jsonelement1 != null && jsonelement2 != null)
                {
                    String s = jsonelement2.getAsString();
                    String s1 = jsonelement1.getAsString();
                    Date date = null;

                    if (jsonelement3 != null)
                    {
                        try
                        {
                            date = PlayerProfileCache.field_152659_a.parse(jsonelement3.getAsString());
                        }
                        catch (ParseException parseexception)
                        {
                            date = null;
                        }
                    }

                    if (s1 != null && s != null)
                    {
                        UUID uuid;

                        try
                        {
                            uuid = UUID.fromString(s);
                        }
                        catch (Throwable throwable)
                        {
                            return null;
                        }

                        PlayerProfileCache.ProfileEntry profileentry = PlayerProfileCache.this.new ProfileEntry(new GameProfile(uuid, s1), date, null);
                        return profileentry;
                    }
                    else
                    {
                        return null;
                    }
                }
                else
                {
                    return null;
                }
            }
            else
            {
                return null;
            }
        }

        public JsonElement serialize(Object p_serialize_1_, Type p_serialize_2_, JsonSerializationContext p_serialize_3_)
        {
            return this.func_152676_a((PlayerProfileCache.ProfileEntry)p_serialize_1_, p_serialize_2_, p_serialize_3_);
        }

        public Object deserialize(JsonElement p_deserialize_1_, Type p_deserialize_2_, JsonDeserializationContext p_deserialize_3_)
        {
            return this.func_152675_a(p_deserialize_1_, p_deserialize_2_, p_deserialize_3_);
        }

        Serializer(Object p_i1163_2_)
        {
            this();
        }
    }
}