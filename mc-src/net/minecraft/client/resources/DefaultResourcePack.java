package net.minecraft.client.resources;

import com.google.common.collect.ImmutableSet;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.awt.image.BufferedImage;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.util.Map;
import java.util.Set;
import javax.imageio.ImageIO;
import net.minecraft.client.resources.data.IMetadataSection;
import net.minecraft.client.resources.data.IMetadataSerializer;
import net.minecraft.util.ResourceLocation;

@SideOnly(Side.CLIENT)
public class DefaultResourcePack implements IResourcePack
{
    public static final Set defaultResourceDomains = ImmutableSet.of("minecraft", "realms");
    private final Map field_152781_b;
    private static final String __OBFID = "CL_00001073";

    public DefaultResourcePack(Map p_i1046_1_)
    {
        this.field_152781_b = p_i1046_1_;
    }

    public InputStream getInputStream(ResourceLocation p_110590_1_) throws IOException
    {
        InputStream inputstream = this.getResourceStream(p_110590_1_);

        if (inputstream != null)
        {
            return inputstream;
        }
        else
        {
            InputStream inputstream1 = this.func_152780_c(p_110590_1_);

            if (inputstream1 != null)
            {
                return inputstream1;
            }
            else
            {
                throw new FileNotFoundException(p_110590_1_.getResourcePath());
            }
        }
    }

    public InputStream func_152780_c(ResourceLocation p_152780_1_) throws IOException
    {
        File file1 = (File)this.field_152781_b.get(p_152780_1_.toString());
        return file1 != null && file1.isFile() ? new FileInputStream(file1) : null;
    }

    private InputStream getResourceStream(ResourceLocation p_110605_1_)
    {
        return DefaultResourcePack.class.getResourceAsStream("/assets/" + p_110605_1_.getResourceDomain() + "/" + p_110605_1_.getResourcePath());
    }

    public boolean resourceExists(ResourceLocation p_110589_1_)
    {
        return this.getResourceStream(p_110589_1_) != null || this.field_152781_b.containsKey(p_110589_1_.toString());
    }

    public Set getResourceDomains()
    {
        return defaultResourceDomains;
    }

    public IMetadataSection getPackMetadata(IMetadataSerializer p_135058_1_, String p_135058_2_) throws IOException
    {
        try
        {
            FileInputStream fileinputstream = new FileInputStream((File)this.field_152781_b.get("pack.mcmeta"));
            return AbstractResourcePack.readMetadata(p_135058_1_, fileinputstream, p_135058_2_);
        }
        catch (RuntimeException runtimeexception)
        {
            return null;
        }
        catch (FileNotFoundException filenotfoundexception)
        {
            return null;
        }
    }

    public BufferedImage getPackImage() throws IOException
    {
        return ImageIO.read(DefaultResourcePack.class.getResourceAsStream("/" + (new ResourceLocation("pack.png")).getResourcePath()));
    }

    public String getPackName()
    {
        return "Default";
    }
}