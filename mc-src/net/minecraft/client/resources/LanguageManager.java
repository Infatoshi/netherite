package net.minecraft.client.resources;

import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import com.google.common.collect.Sets;

import cpw.mods.fml.common.registry.LanguageRegistry;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.SortedSet;
import net.minecraft.client.resources.data.IMetadataSerializer;
import net.minecraft.client.resources.data.LanguageMetadataSection;
import net.minecraft.util.StringTranslate;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

@SideOnly(Side.CLIENT)
public class LanguageManager implements IResourceManagerReloadListener
{
    private static final Logger logger = LogManager.getLogger();
    private final IMetadataSerializer theMetadataSerializer;
    private String currentLanguage;
    protected static final Locale currentLocale = new Locale();
    private Map languageMap = Maps.newHashMap();
    private static final String __OBFID = "CL_00001096";

    public LanguageManager(IMetadataSerializer p_i1304_1_, String p_i1304_2_)
    {
        this.theMetadataSerializer = p_i1304_1_;
        this.currentLanguage = p_i1304_2_;
        I18n.setLocale(currentLocale);
    }

    public void parseLanguageMetadata(List p_135043_1_)
    {
        this.languageMap.clear();
        Iterator iterator = p_135043_1_.iterator();

        while (iterator.hasNext())
        {
            IResourcePack iresourcepack = (IResourcePack)iterator.next();

            try
            {
                LanguageMetadataSection languagemetadatasection = (LanguageMetadataSection)iresourcepack.getPackMetadata(this.theMetadataSerializer, "language");

                if (languagemetadatasection != null)
                {
                    Iterator iterator1 = languagemetadatasection.getLanguages().iterator();

                    while (iterator1.hasNext())
                    {
                        Language language = (Language)iterator1.next();

                        if (!this.languageMap.containsKey(language.getLanguageCode()))
                        {
                            this.languageMap.put(language.getLanguageCode(), language);
                        }
                    }
                }
            }
            catch (RuntimeException runtimeexception)
            {
                logger.warn("Unable to parse metadata section of resourcepack: " + iresourcepack.getPackName(), runtimeexception);
            }
            catch (IOException ioexception)
            {
                logger.warn("Unable to parse metadata section of resourcepack: " + iresourcepack.getPackName(), ioexception);
            }
        }
    }

    public void onResourceManagerReload(IResourceManager p_110549_1_)
    {
        ArrayList arraylist = Lists.newArrayList(new String[] {"en_US"});

        if (!"en_US".equals(this.currentLanguage))
        {
            arraylist.add(this.currentLanguage);
        }

        currentLocale.loadLocaleDataFiles(p_110549_1_, arraylist);
        LanguageRegistry.instance().mergeLanguageTable(currentLocale.field_135032_a, this.currentLanguage);
        StringTranslate.replaceWith(currentLocale.field_135032_a);
    }

    public boolean isCurrentLocaleUnicode()
    {
        return currentLocale.isUnicode();
    }

    public boolean isCurrentLanguageBidirectional()
    {
        return this.getCurrentLanguage() != null && this.getCurrentLanguage().isBidirectional();
    }

    public void setCurrentLanguage(Language p_135045_1_)
    {
        this.currentLanguage = p_135045_1_.getLanguageCode();
    }

    public Language getCurrentLanguage()
    {
        return this.languageMap.containsKey(this.currentLanguage) ? (Language)this.languageMap.get(this.currentLanguage) : (Language)this.languageMap.get("en_US");
    }

    public SortedSet getLanguages()
    {
        return Sets.newTreeSet(this.languageMap.values());
    }
}