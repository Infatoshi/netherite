package net.minecraft.server.management;

import com.google.common.base.Charsets;
import com.google.common.base.Predicate;
import com.google.common.collect.Iterators;
import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import com.google.common.io.Files;
import com.mojang.authlib.Agent;
import com.mojang.authlib.GameProfile;
import com.mojang.authlib.ProfileLookupCallback;
import com.mojang.authlib.yggdrasil.ProfileNotFoundException;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FilenameFilter;
import java.io.IOException;
import java.text.ParseException;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Date;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.UUID;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.dedicated.DedicatedServer;
import net.minecraft.server.dedicated.PropertyManager;
import net.minecraft.util.StringUtils;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

public class PreYggdrasilConverter
{
    private static final Logger field_152732_e = LogManager.getLogger();
    public static final File field_152728_a = new File("banned-ips.txt");
    public static final File field_152729_b = new File("banned-players.txt");
    public static final File field_152730_c = new File("ops.txt");
    public static final File field_152731_d = new File("white-list.txt");
    private static final String __OBFID = "CL_00001882";

    private static void func_152717_a(MinecraftServer p_152717_0_, Collection p_152717_1_, ProfileLookupCallback p_152717_2_)
    {
        String[] astring = (String[])Iterators.toArray(Iterators.filter(p_152717_1_.iterator(), new Predicate()
        {
            private static final String __OBFID = "CL_00001881";
            public boolean func_152733_a(String p_152733_1_)
            {
                return !StringUtils.isNullOrEmpty(p_152733_1_);
            }
            public boolean apply(Object p_apply_1_)
            {
                return this.func_152733_a((String)p_apply_1_);
            }
        }), String.class);

        if (p_152717_0_.isServerInOnlineMode())
        {
            p_152717_0_.func_152359_aw().findProfilesByNames(astring, Agent.MINECRAFT, p_152717_2_);
        }
        else
        {
            String[] astring1 = astring;
            int i = astring.length;

            for (int j = 0; j < i; ++j)
            {
                String s = astring1[j];
                UUID uuid = EntityPlayer.func_146094_a(new GameProfile((UUID)null, s));
                GameProfile gameprofile = new GameProfile(uuid, s);
                p_152717_2_.onProfileLookupSucceeded(gameprofile);
            }
        }
    }

    public static String func_152719_a(String p_152719_0_)
    {
        if (!StringUtils.isNullOrEmpty(p_152719_0_) && p_152719_0_.length() <= 16)
        {
            final MinecraftServer minecraftserver = MinecraftServer.getServer();
            GameProfile gameprofile = minecraftserver.func_152358_ax().func_152655_a(p_152719_0_);

            if (gameprofile != null && gameprofile.getId() != null)
            {
                return gameprofile.getId().toString();
            }
            else if (!minecraftserver.isSinglePlayer() && minecraftserver.isServerInOnlineMode())
            {
                final ArrayList arraylist = Lists.newArrayList();
                ProfileLookupCallback profilelookupcallback = new ProfileLookupCallback()
                {
                    private static final String __OBFID = "CL_00001880";
                    public void onProfileLookupSucceeded(GameProfile p_onProfileLookupSucceeded_1_)
                    {
                        minecraftserver.func_152358_ax().func_152649_a(p_onProfileLookupSucceeded_1_);
                        arraylist.add(p_onProfileLookupSucceeded_1_);
                    }
                    public void onProfileLookupFailed(GameProfile p_onProfileLookupFailed_1_, Exception p_onProfileLookupFailed_2_)
                    {
                        PreYggdrasilConverter.field_152732_e.warn("Could not lookup user whitelist entry for " + p_onProfileLookupFailed_1_.getName(), p_onProfileLookupFailed_2_);
                    }
                };
                func_152717_a(minecraftserver, Lists.newArrayList(new String[] {p_152719_0_}), profilelookupcallback);
                return arraylist.size() > 0 && ((GameProfile)arraylist.get(0)).getId() != null ? ((GameProfile)arraylist.get(0)).getId().toString() : "";
            }
            else
            {
                return EntityPlayer.func_146094_a(new GameProfile((UUID)null, p_152719_0_)).toString();
            }
        }
        else
        {
            return p_152719_0_;
        }
    }

    @SideOnly(Side.SERVER)
    static List func_152721_a(File p_152721_0_, Map p_152721_1_) throws IOException
    {
        List list = Files.readLines(p_152721_0_, Charsets.UTF_8);
        Iterator iterator = list.iterator();

        while (iterator.hasNext())
        {
            String s = (String)iterator.next();
            s = s.trim();

            if (!s.startsWith("#") && s.length() >= 1)
            {
                String[] astring = s.split("\\|");
                p_152721_1_.put(astring[0].toLowerCase(Locale.ROOT), astring);
            }
        }

        return list;
    }

    @SideOnly(Side.SERVER)
    public static boolean func_152724_a(final MinecraftServer p_152724_0_) throws IOException
    {
        final UserListBans userlistbans = new UserListBans(ServerConfigurationManager.field_152613_a);

        if (field_152729_b.exists() && field_152729_b.isFile())
        {
            if (userlistbans.func_152691_c().exists())
            {
                try
                {
                    userlistbans.func_152679_g();
                }
                catch (FileNotFoundException filenotfoundexception)
                {
                    field_152732_e.warn("Could not load existing file " + userlistbans.func_152691_c().getName(), filenotfoundexception);
                }
            }

            try
            {
                final HashMap hashmap = Maps.newHashMap();
                func_152721_a(field_152729_b, hashmap);
                ProfileLookupCallback profilelookupcallback = new ProfileLookupCallback()
                {
                    private static final String __OBFID = "CL_00001910";
                    public void onProfileLookupSucceeded(GameProfile p_onProfileLookupSucceeded_1_)
                    {
                        p_152724_0_.func_152358_ax().func_152649_a(p_onProfileLookupSucceeded_1_);
                        String[] astring = (String[])hashmap.get(p_onProfileLookupSucceeded_1_.getName().toLowerCase(Locale.ROOT));

                        if (astring == null)
                        {
                            PreYggdrasilConverter.field_152732_e.warn("Could not convert user banlist entry for " + p_onProfileLookupSucceeded_1_.getName());
                            throw new PreYggdrasilConverter.ConversionError("Profile not in the conversionlist", null);
                        }
                        else
                        {
                            Date date = astring.length > 1 ? PreYggdrasilConverter.func_152713_b(astring[1], (Date)null) : null;
                            String s = astring.length > 2 ? astring[2] : null;
                            Date date1 = astring.length > 3 ? PreYggdrasilConverter.func_152713_b(astring[3], (Date)null) : null;
                            String s1 = astring.length > 4 ? astring[4] : null;
                            userlistbans.func_152687_a(new UserListBansEntry(p_onProfileLookupSucceeded_1_, date, s, date1, s1));
                        }
                    }
                    public void onProfileLookupFailed(GameProfile p_onProfileLookupFailed_1_, Exception p_onProfileLookupFailed_2_)
                    {
                        PreYggdrasilConverter.field_152732_e.warn("Could not lookup user banlist entry for " + p_onProfileLookupFailed_1_.getName(), p_onProfileLookupFailed_2_);

                        if (!(p_onProfileLookupFailed_2_ instanceof ProfileNotFoundException))
                        {
                            throw new PreYggdrasilConverter.ConversionError("Could not request user " + p_onProfileLookupFailed_1_.getName() + " from backend systems", p_onProfileLookupFailed_2_, null);
                        }
                    }
                };
                func_152717_a(p_152724_0_, hashmap.keySet(), profilelookupcallback);
                userlistbans.func_152678_f();
                func_152727_c(field_152729_b);
                return true;
            }
            catch (IOException ioexception)
            {
                field_152732_e.warn("Could not read old user banlist to convert it!", ioexception);
                return false;
            }
            catch (PreYggdrasilConverter.ConversionError preyggdrasilconverterconversionerror)
            {
                field_152732_e.error("Conversion failed, please try again later", preyggdrasilconverterconversionerror);
                return false;
            }
        }
        else
        {
            return true;
        }
    }

    @SideOnly(Side.SERVER)
    public static boolean func_152722_b(MinecraftServer p_152722_0_) throws IOException
    {
        BanList banlist = new BanList(ServerConfigurationManager.field_152614_b);

        if (field_152728_a.exists() && field_152728_a.isFile())
        {
            if (banlist.func_152691_c().exists())
            {
                try
                {
                    banlist.func_152679_g();
                }
                catch (FileNotFoundException filenotfoundexception)
                {
                    field_152732_e.warn("Could not load existing file " + banlist.func_152691_c().getName(), filenotfoundexception);
                }
            }

            try
            {
                HashMap hashmap = Maps.newHashMap();
                func_152721_a(field_152728_a, hashmap);
                Iterator iterator = hashmap.keySet().iterator();

                while (iterator.hasNext())
                {
                    String s = (String)iterator.next();
                    String[] astring = (String[])hashmap.get(s);
                    Date date = astring.length > 1 ? func_152713_b(astring[1], (Date)null) : null;
                    String s1 = astring.length > 2 ? astring[2] : null;
                    Date date1 = astring.length > 3 ? func_152713_b(astring[3], (Date)null) : null;
                    String s2 = astring.length > 4 ? astring[4] : null;
                    banlist.func_152687_a(new IPBanEntry(s, date, s1, date1, s2));
                }

                banlist.func_152678_f();
                func_152727_c(field_152728_a);
                return true;
            }
            catch (IOException ioexception)
            {
                field_152732_e.warn("Could not parse old ip banlist to convert it!", ioexception);
                return false;
            }
        }
        else
        {
            return true;
        }
    }

    @SideOnly(Side.SERVER)
    public static boolean func_152718_c(final MinecraftServer p_152718_0_) throws IOException
    {
        final UserListOps userlistops = new UserListOps(ServerConfigurationManager.field_152615_c);

        if (field_152730_c.exists() && field_152730_c.isFile())
        {
            if (userlistops.func_152691_c().exists())
            {
                try
                {
                    userlistops.func_152679_g();
                }
                catch (FileNotFoundException filenotfoundexception)
                {
                    field_152732_e.warn("Could not load existing file " + userlistops.func_152691_c().getName(), filenotfoundexception);
                }
            }

            try
            {
                List list = Files.readLines(field_152730_c, Charsets.UTF_8);
                ProfileLookupCallback profilelookupcallback = new ProfileLookupCallback()
                {
                    private static final String __OBFID = "CL_00001909";
                    public void onProfileLookupSucceeded(GameProfile p_onProfileLookupSucceeded_1_)
                    {
                        p_152718_0_.func_152358_ax().func_152649_a(p_onProfileLookupSucceeded_1_);
                        userlistops.func_152687_a(new UserListOpsEntry(p_onProfileLookupSucceeded_1_, p_152718_0_.getOpPermissionLevel()));
                    }
                    public void onProfileLookupFailed(GameProfile p_onProfileLookupFailed_1_, Exception p_onProfileLookupFailed_2_)
                    {
                        PreYggdrasilConverter.field_152732_e.warn("Could not lookup oplist entry for " + p_onProfileLookupFailed_1_.getName(), p_onProfileLookupFailed_2_);

                        if (!(p_onProfileLookupFailed_2_ instanceof ProfileNotFoundException))
                        {
                            throw new PreYggdrasilConverter.ConversionError("Could not request user " + p_onProfileLookupFailed_1_.getName() + " from backend systems", p_onProfileLookupFailed_2_, null);
                        }
                    }
                };
                func_152717_a(p_152718_0_, list, profilelookupcallback);
                userlistops.func_152678_f();
                func_152727_c(field_152730_c);
                return true;
            }
            catch (IOException ioexception)
            {
                field_152732_e.warn("Could not read old oplist to convert it!", ioexception);
                return false;
            }
            catch (PreYggdrasilConverter.ConversionError preyggdrasilconverterconversionerror)
            {
                field_152732_e.error("Conversion failed, please try again later", preyggdrasilconverterconversionerror);
                return false;
            }
        }
        else
        {
            return true;
        }
    }

    @SideOnly(Side.SERVER)
    public static boolean func_152710_d(final MinecraftServer p_152710_0_) throws IOException
    {
        final UserListWhitelist userlistwhitelist = new UserListWhitelist(ServerConfigurationManager.field_152616_d);

        if (field_152731_d.exists() && field_152731_d.isFile())
        {
            if (userlistwhitelist.func_152691_c().exists())
            {
                try
                {
                    userlistwhitelist.func_152679_g();
                }
                catch (FileNotFoundException filenotfoundexception)
                {
                    field_152732_e.warn("Could not load existing file " + userlistwhitelist.func_152691_c().getName(), filenotfoundexception);
                }
            }

            try
            {
                List list = Files.readLines(field_152731_d, Charsets.UTF_8);
                ProfileLookupCallback profilelookupcallback = new ProfileLookupCallback()
                {
                    private static final String __OBFID = "CL_00001908";
                    public void onProfileLookupSucceeded(GameProfile p_onProfileLookupSucceeded_1_)
                    {
                        p_152710_0_.func_152358_ax().func_152649_a(p_onProfileLookupSucceeded_1_);
                        userlistwhitelist.func_152687_a(new UserListWhitelistEntry(p_onProfileLookupSucceeded_1_));
                    }
                    public void onProfileLookupFailed(GameProfile p_onProfileLookupFailed_1_, Exception p_onProfileLookupFailed_2_)
                    {
                        PreYggdrasilConverter.field_152732_e.warn("Could not lookup user whitelist entry for " + p_onProfileLookupFailed_1_.getName(), p_onProfileLookupFailed_2_);

                        if (!(p_onProfileLookupFailed_2_ instanceof ProfileNotFoundException))
                        {
                            throw new PreYggdrasilConverter.ConversionError("Could not request user " + p_onProfileLookupFailed_1_.getName() + " from backend systems", p_onProfileLookupFailed_2_, null);
                        }
                    }
                };
                func_152717_a(p_152710_0_, list, profilelookupcallback);
                userlistwhitelist.func_152678_f();
                func_152727_c(field_152731_d);
                return true;
            }
            catch (IOException ioexception)
            {
                field_152732_e.warn("Could not read old whitelist to convert it!", ioexception);
                return false;
            }
            catch (PreYggdrasilConverter.ConversionError preyggdrasilconverterconversionerror)
            {
                field_152732_e.error("Conversion failed, please try again later", preyggdrasilconverterconversionerror);
                return false;
            }
        }
        else
        {
            return true;
        }
    }

    @SideOnly(Side.SERVER)
    public static boolean func_152723_a(final DedicatedServer p_152723_0_, PropertyManager p_152723_1_)
    {
        final File file1 = func_152725_d(p_152723_1_);
        final File file2 = new File(file1.getParentFile(), "playerdata");
        final File file3 = new File(file1.getParentFile(), "unknownplayers");

        if (file1.exists() && file1.isDirectory())
        {
            File[] afile = file1.listFiles();
            ArrayList arraylist = Lists.newArrayList();
            File[] afile1 = afile;
            int i = afile.length;

            for (int j = 0; j < i; ++j)
            {
                File file4 = afile1[j];
                String s = file4.getName();

                if (s.toLowerCase(Locale.ROOT).endsWith(".dat"))
                {
                    String s1 = s.substring(0, s.length() - ".dat".length());

                    if (s1.length() > 0)
                    {
                        arraylist.add(s1);
                    }
                }
            }

            try
            {
                final String[] astring = (String[])arraylist.toArray(new String[arraylist.size()]);
                ProfileLookupCallback profilelookupcallback = new ProfileLookupCallback()
                {
                    private static final String __OBFID = "CL_00001907";
                    public void onProfileLookupSucceeded(GameProfile p_onProfileLookupSucceeded_1_)
                    {
                        p_152723_0_.func_152358_ax().func_152649_a(p_onProfileLookupSucceeded_1_);
                        UUID uuid = p_onProfileLookupSucceeded_1_.getId();

                        if (uuid == null)
                        {
                            throw new PreYggdrasilConverter.ConversionError("Missing UUID for user profile " + p_onProfileLookupSucceeded_1_.getName(), null);
                        }
                        else
                        {
                            this.func_152743_a(file2, this.func_152744_a(p_onProfileLookupSucceeded_1_), uuid.toString());
                        }
                    }
                    public void onProfileLookupFailed(GameProfile p_onProfileLookupFailed_1_, Exception p_onProfileLookupFailed_2_)
                    {
                        PreYggdrasilConverter.field_152732_e.warn("Could not lookup user uuid for " + p_onProfileLookupFailed_1_.getName(), p_onProfileLookupFailed_2_);

                        if (p_onProfileLookupFailed_2_ instanceof ProfileNotFoundException)
                        {
                            String s2 = this.func_152744_a(p_onProfileLookupFailed_1_);
                            this.func_152743_a(file3, s2, s2);
                        }
                        else
                        {
                            throw new PreYggdrasilConverter.ConversionError("Could not request user " + p_onProfileLookupFailed_1_.getName() + " from backend systems", p_onProfileLookupFailed_2_, null);
                        }
                    }
                    private void func_152743_a(File p_152743_1_, String p_152743_2_, String p_152743_3_)
                    {
                        File file7 = new File(file1, p_152743_2_ + ".dat");
                        File file6 = new File(p_152743_1_, p_152743_3_ + ".dat");
                        PreYggdrasilConverter.func_152711_b(p_152743_1_);

                        if (!file7.renameTo(file6))
                        {
                            throw new PreYggdrasilConverter.ConversionError("Could not convert file for " + p_152743_2_, null);
                        }
                    }
                    private String func_152744_a(GameProfile p_152744_1_)
                    {
                        String s2 = null;

                        for (int k = 0; k < astring.length; ++k)
                        {
                            if (astring[k] != null && astring[k].equalsIgnoreCase(p_152744_1_.getName()))
                            {
                                s2 = astring[k];
                                break;
                            }
                        }

                        if (s2 == null)
                        {
                            throw new PreYggdrasilConverter.ConversionError("Could not find the filename for " + p_152744_1_.getName() + " anymore", null);
                        }
                        else
                        {
                            return s2;
                        }
                    }
                };
                func_152717_a(p_152723_0_, Lists.newArrayList(astring), profilelookupcallback);
                return true;
            }
            catch (PreYggdrasilConverter.ConversionError preyggdrasilconverterconversionerror)
            {
                field_152732_e.error("Conversion failed, please try again later", preyggdrasilconverterconversionerror);
                return false;
            }
        }
        else
        {
            return true;
        }
    }

    @SideOnly(Side.SERVER)
    private static void func_152711_b(File p_152711_0_)
    {
        if (p_152711_0_.exists())
        {
            if (!p_152711_0_.isDirectory())
            {
                throw new PreYggdrasilConverter.ConversionError("Can\'t create directory " + p_152711_0_.getName() + " in world save directory.", null);
            }
        }
        else if (!p_152711_0_.mkdirs())
        {
            throw new PreYggdrasilConverter.ConversionError("Can\'t create directory " + p_152711_0_.getName() + " in world save directory.", null);
        }
    }

    @SideOnly(Side.SERVER)
    public static boolean func_152714_a(PropertyManager p_152714_0_)
    {
        boolean flag = func_152712_b(p_152714_0_);
        flag = flag && func_152715_c(p_152714_0_);
        return flag;
    }

    @SideOnly(Side.SERVER)
    private static boolean func_152712_b(PropertyManager p_152712_0_)
    {
        boolean flag = false;

        if (field_152729_b.exists() && field_152729_b.isFile())
        {
            flag = true;
        }

        boolean flag1 = false;

        if (field_152728_a.exists() && field_152728_a.isFile())
        {
            flag1 = true;
        }

        boolean flag2 = false;

        if (field_152730_c.exists() && field_152730_c.isFile())
        {
            flag2 = true;
        }

        boolean flag3 = false;

        if (field_152731_d.exists() && field_152731_d.isFile())
        {
            flag3 = true;
        }

        if (!flag && !flag1 && !flag2 && !flag3)
        {
            return true;
        }
        else
        {
            field_152732_e.warn("**** FAILED TO START THE SERVER AFTER ACCOUNT CONVERSION!");
            field_152732_e.warn("** please remove the following files and restart the server:");

            if (flag)
            {
                field_152732_e.warn("* " + field_152729_b.getName());
            }

            if (flag1)
            {
                field_152732_e.warn("* " + field_152728_a.getName());
            }

            if (flag2)
            {
                field_152732_e.warn("* " + field_152730_c.getName());
            }

            if (flag3)
            {
                field_152732_e.warn("* " + field_152731_d.getName());
            }

            return false;
        }
    }

    @SideOnly(Side.SERVER)
    private static boolean func_152715_c(PropertyManager p_152715_0_)
    {
        File file1 = func_152725_d(p_152715_0_);

        if (file1.exists() && file1.isDirectory())
        {
            String[] astring = file1.list(new FilenameFilter()
            {
                private static final String __OBFID = "CL_00001906";
                public boolean accept(File p_accept_1_, String p_accept_2_)
                {
                    return p_accept_2_.endsWith(".dat");
                }
            });

            if (astring.length > 0)
            {
                field_152732_e.warn("**** DETECTED OLD PLAYER FILES IN THE WORLD SAVE");
                field_152732_e.warn("**** THIS USUALLY HAPPENS WHEN THE AUTOMATIC CONVERSION FAILED IN SOME WAY");
                field_152732_e.warn("** please restart the server and if the problem persists, remove the directory \'{}\'", new Object[] {file1.getPath()});
                return false;
            }
        }

        return true;
    }

    @SideOnly(Side.SERVER)
    private static File func_152725_d(PropertyManager p_152725_0_)
    {
        String s = p_152725_0_.getStringProperty("level-name", "world");
        File file1 = new File(s);
        return new File(file1, "players");
    }

    @SideOnly(Side.SERVER)
    private static void func_152727_c(File p_152727_0_)
    {
        File file2 = new File(p_152727_0_.getName() + ".converted");
        p_152727_0_.renameTo(file2);
    }

    @SideOnly(Side.SERVER)
    private static Date func_152713_b(String p_152713_0_, Date p_152713_1_)
    {
        Date date1;

        try
        {
            date1 = BanEntry.dateFormat.parse(p_152713_0_);
        }
        catch (ParseException parseexception)
        {
            date1 = p_152713_1_;
        }

        return date1;
    }

    @SideOnly(Side.SERVER)
    static class ConversionError extends RuntimeException
        {
            private static final String __OBFID = "CL_00001905";

            private ConversionError(String p_i1206_1_, Throwable p_i1206_2_)
            {
                super(p_i1206_1_, p_i1206_2_);
            }

            private ConversionError(String p_i1207_1_)
            {
                super(p_i1207_1_);
            }

            ConversionError(String p_i1208_1_, Object p_i1208_2_)
            {
                this(p_i1208_1_);
            }

            ConversionError(String p_i1209_1_, Throwable p_i1209_2_, Object p_i1209_3_)
            {
                this(p_i1209_1_, p_i1209_2_);
            }
        }
}