package net.minecraft.client.resources;

import com.google.common.base.Splitter;
import com.google.common.collect.Iterables;
import com.google.common.collect.Maps;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.IOException;
import java.io.InputStream;
import java.util.IllegalFormatException;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.regex.Pattern;
import net.minecraft.util.ResourceLocation;
import org.apache.commons.io.Charsets;
import org.apache.commons.io.IOUtils;

@SideOnly(Side.CLIENT)
public class Locale
{
    /** Splits on "=" */
    private static final Splitter splitter = Splitter.on('=').limit(2);
    private static final Pattern field_135031_c = Pattern.compile("%(\\d+\\$)?[\\d\\.]*[df]");
    Map field_135032_a = Maps.newHashMap();
    private boolean field_135029_d;
    private static final String __OBFID = "CL_00001097";

    /**
     * par2 is a list of languages. For each language $L and domain $D, attempts to load the resource $D:lang/$L.lang
     */
    public synchronized void loadLocaleDataFiles(IResourceManager p_135022_1_, List p_135022_2_)
    {
        this.field_135032_a.clear();
        Iterator iterator = p_135022_2_.iterator();

        while (iterator.hasNext())
        {
            String s = (String)iterator.next();
            String s1 = String.format("lang/%s.lang", new Object[] {s});
            Iterator iterator1 = p_135022_1_.getResourceDomains().iterator();

            while (iterator1.hasNext())
            {
                String s2 = (String)iterator1.next();

                try
                {
                    this.loadLocaleData(p_135022_1_.getAllResources(new ResourceLocation(s2, s1)));
                }
                catch (IOException ioexception)
                {
                    ;
                }
            }
        }

        this.checkUnicode();
    }

    public boolean isUnicode()
    {
        return this.field_135029_d;
    }

    private void checkUnicode()
    {
        this.field_135029_d = false;
        int i = 0;
        int j = 0;
        Iterator iterator = this.field_135032_a.values().iterator();

        while (iterator.hasNext())
        {
            String s = (String)iterator.next();
            int k = s.length();
            j += k;

            for (int l = 0; l < k; ++l)
            {
                if (s.charAt(l) >= 256)
                {
                    ++i;
                }
            }
        }

        float f = (float)i / (float)j;
        this.field_135029_d = (double)f > 0.1D;
    }

    /**
     * par1 is a list of Resources
     */
    private void loadLocaleData(List p_135028_1_) throws IOException
    {
        Iterator iterator = p_135028_1_.iterator();

        while (iterator.hasNext())
        {
            IResource iresource = (IResource)iterator.next();
            this.loadLocaleData(iresource.getInputStream());
        }
    }

    private void loadLocaleData(InputStream p_135021_1_) throws IOException
    {
        Iterator iterator = IOUtils.readLines(p_135021_1_, Charsets.UTF_8).iterator();

        while (iterator.hasNext())
        {
            String s = (String)iterator.next();

            if (!s.isEmpty() && s.charAt(0) != 35)
            {
                String[] astring = (String[])Iterables.toArray(splitter.split(s), String.class);

                if (astring != null && astring.length == 2)
                {
                    String s1 = astring[0];
                    String s2 = field_135031_c.matcher(astring[1]).replaceAll("%$1s");
                    this.field_135032_a.put(s1, s2);
                }
            }
        }
    }

    /**
     * Returns the translation, or the key itself if the key could not be translated.
     */
    private String translateKeyPrivate(String p_135026_1_)
    {
        String s1 = (String)this.field_135032_a.get(p_135026_1_);
        return s1 == null ? p_135026_1_ : s1;
    }

    /**
     * Calls String.format(translateKey(key), params)
     */
    public String formatMessage(String p_135023_1_, Object[] p_135023_2_)
    {
        String s1 = this.translateKeyPrivate(p_135023_1_);

        try
        {
            return String.format(s1, p_135023_2_);
        }
        catch (IllegalFormatException illegalformatexception)
        {
            return "Format error: " + s1;
        }
    }
}