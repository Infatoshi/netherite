package net.minecraft.client.main;

import com.google.common.collect.HashMultimap;
import com.google.gson.Gson;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.File;
import java.lang.reflect.ParameterizedType;
import java.net.Authenticator;
import java.net.InetSocketAddress;
import java.net.PasswordAuthentication;
import java.net.Proxy;
import java.net.Proxy.Type;
import java.util.Collection;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import joptsimple.ArgumentAcceptingOptionSpec;
import joptsimple.NonOptionArgumentSpec;
import joptsimple.OptionParser;
import joptsimple.OptionSet;
import net.minecraft.client.Minecraft;
import net.minecraft.util.Session;

@SideOnly(Side.CLIENT)
public class Main
{
    private static final java.lang.reflect.Type field_152370_a = new ParameterizedType()
    {
        private static final String __OBFID = "CL_00000828";
        public java.lang.reflect.Type[] getActualTypeArguments()
        {
            return new java.lang.reflect.Type[] {String.class, new ParameterizedType()
            {
                private static final String __OBFID = "CL_00001836";
                public java.lang.reflect.Type[] getActualTypeArguments()
                {
                    return new java.lang.reflect.Type[] {String.class};
                }
                public java.lang.reflect.Type getRawType()
                {
                    return Collection.class;
                }
                public java.lang.reflect.Type getOwnerType()
                {
                    return null;
                }
            }
                                                };
        }
        public java.lang.reflect.Type getRawType()
        {
            return Map.class;
        }
        public java.lang.reflect.Type getOwnerType()
        {
            return null;
        }
    };
    private static final String __OBFID = "CL_00001461";

    public static void main(String[] p_main_0_)
    {
        System.setProperty("java.net.preferIPv4Stack", "true");
        OptionParser optionparser = new OptionParser();
        optionparser.allowsUnrecognizedOptions();
        optionparser.accepts("demo");
        optionparser.accepts("fullscreen");
        ArgumentAcceptingOptionSpec argumentacceptingoptionspec = optionparser.accepts("server").withRequiredArg();
        ArgumentAcceptingOptionSpec argumentacceptingoptionspec1 = optionparser.accepts("port").withRequiredArg().ofType(Integer.class).defaultsTo(Integer.valueOf(25565), new Integer[0]);
        ArgumentAcceptingOptionSpec argumentacceptingoptionspec2 = optionparser.accepts("gameDir").withRequiredArg().ofType(File.class).defaultsTo(new File("."), new File[0]);
        ArgumentAcceptingOptionSpec argumentacceptingoptionspec3 = optionparser.accepts("assetsDir").withRequiredArg().ofType(File.class);
        ArgumentAcceptingOptionSpec argumentacceptingoptionspec4 = optionparser.accepts("resourcePackDir").withRequiredArg().ofType(File.class);
        ArgumentAcceptingOptionSpec argumentacceptingoptionspec5 = optionparser.accepts("proxyHost").withRequiredArg();
        ArgumentAcceptingOptionSpec argumentacceptingoptionspec6 = optionparser.accepts("proxyPort").withRequiredArg().defaultsTo("8080", new String[0]).ofType(Integer.class);
        ArgumentAcceptingOptionSpec argumentacceptingoptionspec7 = optionparser.accepts("proxyUser").withRequiredArg();
        ArgumentAcceptingOptionSpec argumentacceptingoptionspec8 = optionparser.accepts("proxyPass").withRequiredArg();
        ArgumentAcceptingOptionSpec argumentacceptingoptionspec9 = optionparser.accepts("username").withRequiredArg().defaultsTo("Player" + Minecraft.getSystemTime() % 1000L, new String[0]);
        ArgumentAcceptingOptionSpec argumentacceptingoptionspec10 = optionparser.accepts("uuid").withRequiredArg();
        ArgumentAcceptingOptionSpec argumentacceptingoptionspec11 = optionparser.accepts("accessToken").withRequiredArg().required();
        ArgumentAcceptingOptionSpec argumentacceptingoptionspec12 = optionparser.accepts("version").withRequiredArg().required();
        ArgumentAcceptingOptionSpec argumentacceptingoptionspec13 = optionparser.accepts("width").withRequiredArg().ofType(Integer.class).defaultsTo(Integer.valueOf(854), new Integer[0]);
        ArgumentAcceptingOptionSpec argumentacceptingoptionspec14 = optionparser.accepts("height").withRequiredArg().ofType(Integer.class).defaultsTo(Integer.valueOf(480), new Integer[0]);
        ArgumentAcceptingOptionSpec argumentacceptingoptionspec15 = optionparser.accepts("userProperties").withRequiredArg().required();
        ArgumentAcceptingOptionSpec argumentacceptingoptionspec16 = optionparser.accepts("assetIndex").withRequiredArg();
        ArgumentAcceptingOptionSpec argumentacceptingoptionspec17 = optionparser.accepts("userType").withRequiredArg().defaultsTo("legacy", new String[0]);
        NonOptionArgumentSpec nonoptionargumentspec = optionparser.nonOptions();
        OptionSet optionset = optionparser.parse(p_main_0_);
        List list = optionset.valuesOf(nonoptionargumentspec);
        String s = (String)optionset.valueOf(argumentacceptingoptionspec5);
        Proxy proxy = Proxy.NO_PROXY;

        if (s != null)
        {
            try
            {
                proxy = new Proxy(Type.SOCKS, new InetSocketAddress(s, ((Integer)optionset.valueOf(argumentacceptingoptionspec6)).intValue()));
            }
            catch (Exception exception)
            {
                ;
            }
        }

        final String s1 = (String)optionset.valueOf(argumentacceptingoptionspec7);
        final String s2 = (String)optionset.valueOf(argumentacceptingoptionspec8);

        if (!proxy.equals(Proxy.NO_PROXY) && func_110121_a(s1) && func_110121_a(s2))
        {
            Authenticator.setDefault(new Authenticator()
            {
                private static final String __OBFID = "CL_00000829";
                protected PasswordAuthentication getPasswordAuthentication()
                {
                    return new PasswordAuthentication(s1, s2.toCharArray());
                }
            });
        }

        int i = ((Integer)optionset.valueOf(argumentacceptingoptionspec13)).intValue();
        int j = ((Integer)optionset.valueOf(argumentacceptingoptionspec14)).intValue();
        boolean flag = optionset.has("fullscreen");
        boolean flag1 = optionset.has("demo");
        String s3 = (String)optionset.valueOf(argumentacceptingoptionspec12);
        HashMultimap hashmultimap = HashMultimap.create();
        Iterator iterator = ((Map)(new Gson()).fromJson((String)optionset.valueOf(argumentacceptingoptionspec15), field_152370_a)).entrySet().iterator();

        while (iterator.hasNext())
        {
            Entry entry = (Entry)iterator.next();
            hashmultimap.putAll(entry.getKey(), (Iterable)entry.getValue());
        }

        File file2 = (File)optionset.valueOf(argumentacceptingoptionspec2);
        File file3 = optionset.has(argumentacceptingoptionspec3) ? (File)optionset.valueOf(argumentacceptingoptionspec3) : new File(file2, "assets/");
        File file1 = optionset.has(argumentacceptingoptionspec4) ? (File)optionset.valueOf(argumentacceptingoptionspec4) : new File(file2, "resourcepacks/");
        String s4 = optionset.has(argumentacceptingoptionspec10) ? (String)argumentacceptingoptionspec10.value(optionset) : (String)argumentacceptingoptionspec9.value(optionset);
        String s5 = optionset.has(argumentacceptingoptionspec16) ? (String)argumentacceptingoptionspec16.value(optionset) : null;
        Session session = new Session((String)argumentacceptingoptionspec9.value(optionset), s4, (String)argumentacceptingoptionspec11.value(optionset), (String)argumentacceptingoptionspec17.value(optionset));
        Minecraft minecraft = new Minecraft(session, i, j, flag, flag1, file2, file3, file1, proxy, s3, hashmultimap, s5);
        String s6 = (String)optionset.valueOf(argumentacceptingoptionspec);

        if (s6 != null)
        {
            minecraft.setServer(s6, ((Integer)optionset.valueOf(argumentacceptingoptionspec1)).intValue());
        }

        Runtime.getRuntime().addShutdownHook(new Thread("Client Shutdown Thread")
        {
            private static final String __OBFID = "CL_00001835";
            public void run()
            {
                Minecraft.stopIntegratedServer();
            }
        });

        if (!list.isEmpty())
        {
            System.out.println("Completely ignored arguments: " + list);
        }

        Thread.currentThread().setName("Client thread");
        minecraft.run();
    }

    private static boolean func_110121_a(String p_110121_0_)
    {
        return p_110121_0_ != null && !p_110121_0_.isEmpty();
    }
}