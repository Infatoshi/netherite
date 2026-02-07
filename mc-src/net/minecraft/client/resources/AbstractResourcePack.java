package net.minecraft.client.resources;

import com.google.common.base.Charsets;
import com.google.gson.JsonObject;
import com.google.gson.JsonParseException;
import com.google.gson.JsonParser;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.awt.image.BufferedImage;
import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import javax.imageio.ImageIO;
import net.minecraft.client.resources.data.IMetadataSection;
import net.minecraft.client.resources.data.IMetadataSerializer;
import net.minecraft.util.ResourceLocation;
import org.apache.commons.io.IOUtils;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

@SideOnly(Side.CLIENT)
public abstract class AbstractResourcePack implements IResourcePack
{
    private static final Logger resourceLog = LogManager.getLogger();
    protected final File resourcePackFile;
    private static final String __OBFID = "CL_00001072";

    public AbstractResourcePack(File p_i1287_1_)
    {
        this.resourcePackFile = p_i1287_1_;
    }

    private static String locationToName(ResourceLocation p_110592_0_)
    {
        return String.format("%s/%s/%s", new Object[] {"assets", p_110592_0_.getResourceDomain(), p_110592_0_.getResourcePath()});
    }

    protected static String getRelativeName(File p_110595_0_, File p_110595_1_)
    {
        return p_110595_0_.toURI().relativize(p_110595_1_.toURI()).getPath();
    }

    public InputStream getInputStream(ResourceLocation p_110590_1_) throws IOException
    {
        return this.getInputStreamByName(locationToName(p_110590_1_));
    }

    public boolean resourceExists(ResourceLocation p_110589_1_)
    {
        return this.hasResourceName(locationToName(p_110589_1_));
    }

    protected abstract InputStream getInputStreamByName(String p_110591_1_) throws IOException;

    protected abstract boolean hasResourceName(String p_110593_1_);

    protected void logNameNotLowercase(String p_110594_1_)
    {
        resourceLog.warn("ResourcePack: ignored non-lowercase namespace: {} in {}", new Object[] {p_110594_1_, this.resourcePackFile});
    }

    public IMetadataSection getPackMetadata(IMetadataSerializer p_135058_1_, String p_135058_2_) throws IOException
    {
        return readMetadata(p_135058_1_, this.getInputStreamByName("pack.mcmeta"), p_135058_2_);
    }

    static IMetadataSection readMetadata(IMetadataSerializer p_110596_0_, InputStream p_110596_1_, String p_110596_2_)
    {
        JsonObject jsonobject = null;
        BufferedReader bufferedreader = null;

        try
        {
            bufferedreader = new BufferedReader(new InputStreamReader(p_110596_1_, Charsets.UTF_8));
            jsonobject = (new JsonParser()).parse(bufferedreader).getAsJsonObject();
        }
        catch (RuntimeException runtimeexception)
        {
            throw new JsonParseException(runtimeexception);
        }
        finally
        {
            IOUtils.closeQuietly(bufferedreader);
        }

        return p_110596_0_.parseMetadataSection(p_110596_2_, jsonobject);
    }

    public BufferedImage getPackImage() throws IOException
    {
        return ImageIO.read(this.getInputStreamByName("pack.png"));
    }

    public String getPackName()
    {
        return this.resourcePackFile.getName();
    }
}