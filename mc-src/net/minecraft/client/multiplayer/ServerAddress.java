package net.minecraft.client.multiplayer;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.Hashtable;
import javax.naming.directory.Attributes;
import javax.naming.directory.InitialDirContext;

@SideOnly(Side.CLIENT)
public class ServerAddress
{
    private final String ipAddress;
    private final int serverPort;
    private static final String __OBFID = "CL_00000889";

    protected ServerAddress(String p_i1192_1_, int p_i1192_2_)
    {
        this.ipAddress = p_i1192_1_;
        this.serverPort = p_i1192_2_;
    }

    public String getIP()
    {
        return this.ipAddress;
    }

    public int getPort()
    {
        return this.serverPort;
    }

    public static ServerAddress func_78860_a(String p_78860_0_)
    {
        if (p_78860_0_ == null)
        {
            return null;
        }
        else
        {
            String[] astring = p_78860_0_.split(":");

            if (p_78860_0_.startsWith("["))
            {
                int i = p_78860_0_.indexOf("]");

                if (i > 0)
                {
                    String s1 = p_78860_0_.substring(1, i);
                    String s2 = p_78860_0_.substring(i + 1).trim();

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
                astring = new String[] {p_78860_0_};
            }

            String s3 = astring[0];
            int j = astring.length > 1 ? parseIntWithDefault(astring[1], 25565) : 25565;

            if (j == 25565)
            {
                String[] astring1 = getServerAddress(s3);
                s3 = astring1[0];
                j = parseIntWithDefault(astring1[1], 25565);
            }

            return new ServerAddress(s3, j);
        }
    }

    /**
     * Returns a server's address and port for the specified hostname, looking up the SRV record if possible
     */
    private static String[] getServerAddress(String p_78863_0_)
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
            Attributes attributes = initialdircontext.getAttributes("_minecraft._tcp." + p_78863_0_, new String[] {"SRV"});
            String[] astring = attributes.get("srv").get().toString().split(" ", 4);
            return new String[] {astring[3], astring[2]};
        }
        catch (Throwable throwable)
        {
            return new String[] {p_78863_0_, Integer.toString(25565)};
        }
    }

    private static int parseIntWithDefault(String p_78862_0_, int p_78862_1_)
    {
        try
        {
            return Integer.parseInt(p_78862_0_.trim());
        }
        catch (Exception exception)
        {
            return p_78862_1_;
        }
    }
}