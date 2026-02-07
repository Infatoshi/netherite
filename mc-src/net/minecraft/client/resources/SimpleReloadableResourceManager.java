package net.minecraft.client.resources;

import com.google.common.base.Function;
import com.google.common.base.Joiner;
import com.google.common.collect.Iterables;
import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import com.google.common.collect.Sets;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Set;
import net.minecraft.client.resources.data.IMetadataSerializer;
import net.minecraft.util.ResourceLocation;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

@SideOnly(Side.CLIENT)
public class SimpleReloadableResourceManager implements IReloadableResourceManager
{
    private static final Logger logger = LogManager.getLogger();
    private static final Joiner joinerResourcePacks = Joiner.on(", ");
    private final Map domainResourceManagers = Maps.newHashMap();
    private final List reloadListeners = Lists.newArrayList();
    private final Set setResourceDomains = Sets.newLinkedHashSet();
    private final IMetadataSerializer rmMetadataSerializer;
    private static final String __OBFID = "CL_00001091";

    public SimpleReloadableResourceManager(IMetadataSerializer p_i1299_1_)
    {
        this.rmMetadataSerializer = p_i1299_1_;
    }

    public void reloadResourcePack(IResourcePack p_110545_1_)
    {
        FallbackResourceManager fallbackresourcemanager;

        for (Iterator iterator = p_110545_1_.getResourceDomains().iterator(); iterator.hasNext(); fallbackresourcemanager.addResourcePack(p_110545_1_))
        {
            String s = (String)iterator.next();
            this.setResourceDomains.add(s);
            fallbackresourcemanager = (FallbackResourceManager)this.domainResourceManagers.get(s);

            if (fallbackresourcemanager == null)
            {
                fallbackresourcemanager = new FallbackResourceManager(this.rmMetadataSerializer);
                this.domainResourceManagers.put(s, fallbackresourcemanager);
            }
        }
    }

    public Set getResourceDomains()
    {
        return this.setResourceDomains;
    }

    public IResource getResource(ResourceLocation p_110536_1_) throws IOException
    {
        IResourceManager iresourcemanager = (IResourceManager)this.domainResourceManagers.get(p_110536_1_.getResourceDomain());

        if (iresourcemanager != null)
        {
            return iresourcemanager.getResource(p_110536_1_);
        }
        else
        {
            throw new FileNotFoundException(p_110536_1_.toString());
        }
    }

    public List getAllResources(ResourceLocation p_135056_1_) throws IOException
    {
        IResourceManager iresourcemanager = (IResourceManager)this.domainResourceManagers.get(p_135056_1_.getResourceDomain());

        if (iresourcemanager != null)
        {
            return iresourcemanager.getAllResources(p_135056_1_);
        }
        else
        {
            throw new FileNotFoundException(p_135056_1_.toString());
        }
    }

    private void clearResources()
    {
        this.domainResourceManagers.clear();
        this.setResourceDomains.clear();
    }

    public void reloadResources(List p_110541_1_)
    {
        this.clearResources();
        cpw.mods.fml.common.ProgressManager.ProgressBar resReload = cpw.mods.fml.common.ProgressManager.push("Loading Resources", p_110541_1_.size()+1, true);
        logger.info("Reloading ResourceManager: " + joinerResourcePacks.join(Iterables.transform(p_110541_1_, new Function()
        {
            private static final String __OBFID = "CL_00001092";
            public String apply(IResourcePack p_apply_1_)
            {
                return p_apply_1_.getPackName();
            }
            public Object apply(Object p_apply_1_)
            {
                return this.apply((IResourcePack)p_apply_1_);
            }
        })));
        Iterator iterator = p_110541_1_.iterator();

        while (iterator.hasNext())
        {
            IResourcePack iresourcepack = (IResourcePack)iterator.next();
            resReload.step(iresourcepack.getPackName());
            this.reloadResourcePack(iresourcepack);
        }

        resReload.step("Reloading listeners");
        this.notifyReloadListeners();
        cpw.mods.fml.common.ProgressManager.pop(resReload);
    }

    public void registerReloadListener(IResourceManagerReloadListener p_110542_1_)
    {
        this.reloadListeners.add(p_110542_1_);
        cpw.mods.fml.common.ProgressManager.ProgressBar resReload = cpw.mods.fml.common.ProgressManager.push("Loading Resource", 1);
        resReload.step(p_110542_1_.getClass());
        p_110542_1_.onResourceManagerReload(this);
        cpw.mods.fml.common.ProgressManager.pop(resReload);
    }

    private void notifyReloadListeners()
    {
        Iterator iterator = this.reloadListeners.iterator();

        cpw.mods.fml.common.ProgressManager.ProgressBar resReload = cpw.mods.fml.common.ProgressManager.push("Reloading", this.reloadListeners.size());
        while (iterator.hasNext())
        {
            IResourceManagerReloadListener iresourcemanagerreloadlistener = (IResourceManagerReloadListener)iterator.next();
            resReload.step(iresourcemanagerreloadlistener.getClass());
            iresourcemanagerreloadlistener.onResourceManagerReload(this);
        }
        cpw.mods.fml.common.ProgressManager.pop(resReload);
    }
}