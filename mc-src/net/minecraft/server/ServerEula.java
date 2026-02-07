package net.minecraft.server;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.util.Properties;
import org.apache.commons.io.IOUtils;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

@SideOnly(Side.SERVER)
public class ServerEula
{
    private static final Logger field_154349_a = LogManager.getLogger();
    private final File field_154350_b;
    private final boolean field_154351_c;
    private static final String __OBFID = "CL_00001911";

    public ServerEula(File p_i1227_1_)
    {
        this.field_154350_b = p_i1227_1_;
        this.field_154351_c = this.func_154347_a(p_i1227_1_);
    }

    private boolean func_154347_a(File p_154347_1_)
    {
        FileInputStream fileinputstream = null;
        boolean flag = false;

        try
        {
            Properties properties = new Properties();
            fileinputstream = new FileInputStream(p_154347_1_);
            properties.load(fileinputstream);
            flag = Boolean.parseBoolean(properties.getProperty("eula", "false"));
        }
        catch (Exception exception)
        {
            field_154349_a.warn("Failed to load " + p_154347_1_);
            this.func_154348_b();
        }
        finally
        {
            IOUtils.closeQuietly(fileinputstream);
        }

        return flag;
    }

    public boolean func_154346_a()
    {
        return this.field_154351_c;
    }

    public void func_154348_b()
    {
        FileOutputStream fileoutputstream = null;

        try
        {
            Properties properties = new Properties();
            fileoutputstream = new FileOutputStream(this.field_154350_b);
            properties.setProperty("eula", "false");
            properties.store(fileoutputstream, "By changing the setting below to TRUE you are indicating your agreement to our EULA (https://account.mojang.com/documents/minecraft_eula).");
        }
        catch (Exception exception)
        {
            field_154349_a.warn("Failed to save " + this.field_154350_b, exception);
        }
        finally
        {
            IOUtils.closeQuietly(fileoutputstream);
        }
    }
}