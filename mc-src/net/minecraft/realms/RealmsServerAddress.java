package net.minecraft.realms;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.Hashtable;
import javax.naming.directory.Attributes;
import javax.naming.directory.InitialDirContext;

@SideOnly(Side.CLIENT)
public class RealmsServerAddress
{
    private final String host;
    private final int port;
    private static final String __OBFID = "CL_00001864";

    protected RealmsServerAddress(String p_i1121_1_, int p_i1121_2_)
    {
        this.host = p_i1121_1_;
        this.port = p_i1121_2_;
    }

    public String getHost()
    {
        return this.host;
    }

    public int getPort()
    {
        return this.port;
    }

    public static RealmsServerAddress parseString(String p_parseString_0_)
    {
        if (p_parseString_0_ == null)
        {
            return null;
        }
        else
        {
            String[] astring = p_parseString_0_.split(":");

            if (p_parseString_0_.startsWith("["))
            {
                int i = p_parseString_0_.indexOf("]");

                if (i > 0)
                {
                    String s1 = p_parseString_0_.substring(1, i);
                    String s2 = p_parseString_0_.substring(i + 1).trim();

                    if (s2.startsWith(":") && s2.length() > 0)
                    {
                        s2 = s2.substring(1);
                        astring = new String[] {s1, s2};
                    }
                    else
                    {
                        astring = new String[] {s1};
                    }
                }
            }

            if (astring.length > 2)
            {
                astring = new String[] {p_parseString_0_};
            }

            String s3 = astring[0];
            int j = astring.length > 1 ? parseInt(astring[1], 25565) : 25565;

            if (j == 25565)
            {
                String[] astring1 = lookupSrv(s3);
                s3 = astring1[0];
                j = parseInt(astring1[1], 25565);
            }

            return new RealmsServerAddress(s3, j);
        }
    }

    private static String[] lookupSrv(String p_lookupSrv_0_)
    {
        try
        {
            String s1 = "com.sun.jndi.dns.DnsContextFactory";
            Class.forName("com.sun.jndi.dns.DnsContextFactory");
            Hashtable hashtable = new Hashtable();
            hashtable.put("java.naming.factory.initial", "com.sun.jndi.dns.DnsContextFactory");
            hashtable.put("java.naming.provider.url", "dns:");
            hashtable.put("com.sun.jndi.dns.timeout.retries", "1");
            InitialDirContext initialdircontext = new InitialDirContext(hashtable);
            Attributes attributes = initialdircontext.getAttributes("_minecraft._tcp." + p_lookupSrv_0_, new String[] {"SRV"});
            String[] astring = attributes.get("srv").get().toString().split(" ", 4);
            return new String[] {astring[3], astring[2]};
        }
        catch (Throwable throwable)
        {
            return new String[] {p_lookupSrv_0_, Integer.toString(25565)};
        }
    }

    private static int parseInt(String p_parseInt_0_, int p_parseInt_1_)
    {
        try
        {
            return Integer.parseInt(p_parseInt_0_.trim());
        }
        catch (Exception exception)
        {
            return p_parseInt_1_;
        }
    }
}