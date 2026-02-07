package net.minecraft.client.renderer.texture;

import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.IOException;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.concurrent.Callable;
import net.minecraft.client.resources.IResourceManager;
import net.minecraft.client.resources.IResourceManagerReloadListener;
import net.minecraft.crash.CrashReport;
import net.minecraft.crash.CrashReportCategory;
import net.minecraft.util.ReportedException;
import net.minecraft.util.ResourceLocation;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

@SideOnly(Side.CLIENT)
public class TextureManager implements ITickable, IResourceManagerReloadListener
{
    private static final Logger logger = LogManager.getLogger();
    private final Map mapTextureObjects = Maps.newHashMap();
    private final Map mapResourceLocations = Maps.newHashMap();
    private final List listTickables = Lists.newArrayList();
    private final Map mapTextureCounters = Maps.newHashMap();
    private IResourceManager theResourceManager;
    private static final String __OBFID = "CL_00001064";

    public TextureManager(IResourceManager p_i1284_1_)
    {
        this.theResourceManager = p_i1284_1_;
    }

    public void bindTexture(ResourceLocation p_110577_1_)
    {
        Object object = (ITextureObject)this.mapTextureObjects.get(p_110577_1_);

        if (object == null)
        {
            object = new SimpleTexture(p_110577_1_);
            this.loadTexture(p_110577_1_, (ITextureObject)object);
        }

        TextureUtil.bindTexture(((ITextureObject)object).getGlTextureId());
    }

    public ResourceLocation getResourceLocation(int p_130087_1_)
    {
        return (ResourceLocation)this.mapResourceLocations.get(Integer.valueOf(p_130087_1_));
    }

    public boolean loadTextureMap(ResourceLocation p_130088_1_, TextureMap p_130088_2_)
    {
        if (this.loadTickableTexture(p_130088_1_, p_130088_2_))
        {
            this.mapResourceLocations.put(Integer.valueOf(p_130088_2_.getTextureType()), p_130088_1_);
            return true;
        }
        else
        {
            return false;
        }
    }

    public boolean loadTickableTexture(ResourceLocation p_110580_1_, ITickableTextureObject p_110580_2_)
    {
        if (this.loadTexture(p_110580_1_, p_110580_2_))
        {
            this.listTickables.add(p_110580_2_);
            return true;
        }
        else
        {
            return false;
        }
    }

    public boolean loadTexture(ResourceLocation p_110579_1_, final ITextureObject p_110579_2_)
    {
        boolean flag = true;
        ITextureObject p_110579_2_2 = p_110579_2_;

        try
        {
            ((ITextureObject)p_110579_2_).loadTexture(this.theResourceManager);
        }
        catch (IOException ioexception)
        {
            logger.warn("Failed to load texture: " + p_110579_1_, ioexception);
            p_110579_2_2 = TextureUtil.missingTexture;
            this.mapTextureObjects.put(p_110579_1_, p_110579_2_2);
            flag = false;
        }
        catch (Throwable throwable)
        {
            CrashReport crashreport = CrashReport.makeCrashReport(throwable, "Registering texture");
            CrashReportCategory crashreportcategory = crashreport.makeCategory("Resource location being registered");
            crashreportcategory.addCrashSection("Resource location", p_110579_1_);
            crashreportcategory.addCrashSectionCallable("Texture object class", new Callable()
            {
                private static final String __OBFID = "CL_00001065";
                public String call()
                {
                    return p_110579_2_.getClass().getName();
                }
            });
            throw new ReportedException(crashreport);
        }

        this.mapTextureObjects.put(p_110579_1_, p_110579_2_2);
        return flag;
    }

    public ITextureObject getTexture(ResourceLocation p_110581_1_)
    {
        return (ITextureObject)this.mapTextureObjects.get(p_110581_1_);
    }

    public ResourceLocation getDynamicTextureLocation(String p_110578_1_, DynamicTexture p_110578_2_)
    {
        Integer integer = (Integer)this.mapTextureCounters.get(p_110578_1_);

        if (integer == null)
        {
            integer = Integer.valueOf(1);
        }
        else
        {
            integer = Integer.valueOf(integer.intValue() + 1);
        }

        this.mapTextureCounters.put(p_110578_1_, integer);
        ResourceLocation resourcelocation = new ResourceLocation(String.format("dynamic/%s_%d", new Object[] {p_110578_1_, integer}));
        this.loadTexture(resourcelocation, p_110578_2_);
        return resourcelocation;
    }

    public void tick()
    {
        Iterator iterator = this.listTickables.iterator();

        while (iterator.hasNext())
        {
            ITickable itickable = (ITickable)iterator.next();
            itickable.tick();
        }
    }

    public void deleteTexture(ResourceLocation p_147645_1_)
    {
        ITextureObject itextureobject = this.getTexture(p_147645_1_);

        if (itextureobject != null)
        {
            TextureUtil.deleteTexture(itextureobject.getGlTextureId());
        }
    }

    public void onResourceManagerReload(IResourceManager p_110549_1_)
    {
        cpw.mods.fml.common.ProgressManager.ProgressBar bar = cpw.mods.fml.common.ProgressManager.push("Reloading Texture Manager", this.mapTextureObjects.keySet().size(), true);
        Iterator iterator = this.mapTextureObjects.entrySet().iterator();

        while (iterator.hasNext())
        {
            Entry entry = (Entry)iterator.next();
            bar.step(entry.getKey().toString());
            this.loadTexture((ResourceLocation)entry.getKey(), (ITextureObject)entry.getValue());
        }
        cpw.mods.fml.common.ProgressManager.pop(bar);
    }
}