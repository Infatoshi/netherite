package net.minecraft.client.shader;

import com.google.common.base.Charsets;
import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.InputStream;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import net.minecraft.client.renderer.OpenGlHelper;
import net.minecraft.client.renderer.texture.ITextureObject;
import net.minecraft.client.resources.IResourceManager;
import net.minecraft.client.util.JsonBlendingMode;
import net.minecraft.client.util.JsonException;
import net.minecraft.util.JsonUtils;
import net.minecraft.util.ResourceLocation;
import org.apache.commons.io.IOUtils;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.ARBMultitexture;
import org.lwjgl.opengl.GL13;

@SideOnly(Side.CLIENT)
public class ShaderManager
{
    private static final Logger logger = LogManager.getLogger();
    private static final ShaderDefault defaultShaderUniform = new ShaderDefault();
    private static ShaderManager staticShaderManager = null;
    private static int field_147999_d = -1;
    private static boolean field_148000_e = true;
    private final Map field_147997_f = Maps.newHashMap();
    private final List field_147998_g = Lists.newArrayList();
    private final List field_148010_h = Lists.newArrayList();
    private final List field_148011_i = Lists.newArrayList();
    private final List field_148008_j = Lists.newArrayList();
    private final Map field_148009_k = Maps.newHashMap();
    private final int field_148006_l;
    private final String field_148007_m;
    private final boolean field_148004_n;
    private boolean field_148005_o;
    private final JsonBlendingMode field_148016_p;
    private final List field_148015_q;
    private final List field_148014_r;
    private final ShaderLoader field_148013_s;
    private final ShaderLoader field_148012_t;
    private static final String __OBFID = "CL_00001040";

    public ShaderManager(IResourceManager p_i45087_1_, String p_i45087_2_) throws JsonException
    {
        JsonParser jsonparser = new JsonParser();
        ResourceLocation resourcelocation = new ResourceLocation("shaders/program/" + p_i45087_2_ + ".json");
        this.field_148007_m = p_i45087_2_;
        InputStream inputstream = null;

        try
        {
            inputstream = p_i45087_1_.getResource(resourcelocation).getInputStream();
            JsonObject jsonobject = jsonparser.parse(IOUtils.toString(inputstream, Charsets.UTF_8)).getAsJsonObject();
            String s1 = JsonUtils.getJsonObjectStringFieldValue(jsonobject, "vertex");
            String s2 = JsonUtils.getJsonObjectStringFieldValue(jsonobject, "fragment");
            JsonArray jsonarray = JsonUtils.getJsonObjectJsonArrayFieldOrDefault(jsonobject, "samplers", (JsonArray)null);

            if (jsonarray != null)
            {
                int i = 0;

                for (Iterator iterator = jsonarray.iterator(); iterator.hasNext(); ++i)
                {
                    JsonElement jsonelement = (JsonElement)iterator.next();

                    try
                    {
                        this.func_147996_a(jsonelement);
                    }
                    catch (Exception exception2)
                    {
                        JsonException jsonexception1 = JsonException.func_151379_a(exception2);
                        jsonexception1.func_151380_a("samplers[" + i + "]");
                        throw jsonexception1;
                    }
                }
            }

            JsonArray jsonarray1 = JsonUtils.getJsonObjectJsonArrayFieldOrDefault(jsonobject, "attributes", (JsonArray)null);
            Iterator iterator1;

            if (jsonarray1 != null)
            {
                int j = 0;
                this.field_148015_q = Lists.newArrayListWithCapacity(jsonarray1.size());
                this.field_148014_r = Lists.newArrayListWithCapacity(jsonarray1.size());

                for (iterator1 = jsonarray1.iterator(); iterator1.hasNext(); ++j)
                {
                    JsonElement jsonelement1 = (JsonElement)iterator1.next();

                    try
                    {
                        this.field_148014_r.add(JsonUtils.getJsonElementStringValue(jsonelement1, "attribute"));
                    }
                    catch (Exception exception1)
                    {
                        JsonException jsonexception2 = JsonException.func_151379_a(exception1);
                        jsonexception2.func_151380_a("attributes[" + j + "]");
                        throw jsonexception2;
                    }
                }
            }
            else
            {
                this.field_148015_q = null;
                this.field_148014_r = null;
            }

            JsonArray jsonarray2 = JsonUtils.getJsonObjectJsonArrayFieldOrDefault(jsonobject, "uniforms", (JsonArray)null);

            if (jsonarray2 != null)
            {
                int k = 0;

                for (Iterator iterator2 = jsonarray2.iterator(); iterator2.hasNext(); ++k)
                {
                    JsonElement jsonelement2 = (JsonElement)iterator2.next();

                    try
                    {
                        this.func_147987_b(jsonelement2);
                    }
                    catch (Exception exception)
                    {
                        JsonException jsonexception3 = JsonException.func_151379_a(exception);
                        jsonexception3.func_151380_a("uniforms[" + k + "]");
                        throw jsonexception3;
                    }
                }
            }

            this.field_148016_p = JsonBlendingMode.func_148110_a(JsonUtils.getJsonObjectFieldOrDefault(jsonobject, "blend", (JsonObject)null));
            this.field_148004_n = JsonUtils.getJsonObjectBooleanFieldValueOrDefault(jsonobject, "cull", true);
            this.field_148013_s = ShaderLoader.func_148057_a(p_i45087_1_, ShaderLoader.ShaderType.VERTEX, s1);
            this.field_148012_t = ShaderLoader.func_148057_a(p_i45087_1_, ShaderLoader.ShaderType.FRAGMENT, s2);
            this.field_148006_l = ShaderLinkHelper.getStaticShaderLinkHelper().func_148078_c();
            ShaderLinkHelper.getStaticShaderLinkHelper().func_148075_b(this);
            this.func_147990_i();

            if (this.field_148014_r != null)
            {
                iterator1 = this.field_148014_r.iterator();

                while (iterator1.hasNext())
                {
                    String s3 = (String)iterator1.next();
                    int l = OpenGlHelper.func_153164_b(this.field_148006_l, s3);
                    this.field_148015_q.add(Integer.valueOf(l));
                }
            }
        }
        catch (Exception exception3)
        {
            JsonException jsonexception = JsonException.func_151379_a(exception3);
            jsonexception.func_151381_b(resourcelocation.getResourcePath());
            throw jsonexception;
        }
        finally
        {
            IOUtils.closeQuietly(inputstream);
        }

        this.func_147985_d();
    }

    public void func_147988_a()
    {
        ShaderLinkHelper.getStaticShaderLinkHelper().func_148077_a(this);
    }

    public void func_147993_b()
    {
        OpenGlHelper.func_153161_d(0);
        field_147999_d = -1;
        staticShaderManager = null;
        field_148000_e = true;

        for (int i = 0; i < this.field_148010_h.size(); ++i)
        {
            if (this.field_147997_f.get(this.field_147998_g.get(i)) != null)
            {
                GL13.glActiveTexture(ARBMultitexture.GL_TEXTURE0_ARB + i);
                GL11.glBindTexture(GL11.GL_TEXTURE_2D, 0);
            }
        }
    }

    public void func_147995_c()
    {
        this.field_148005_o = false;
        staticShaderManager = this;
        this.field_148016_p.func_148109_a();

        if (this.field_148006_l != field_147999_d)
        {
            OpenGlHelper.func_153161_d(this.field_148006_l);
            field_147999_d = this.field_148006_l;
        }

        if (field_148000_e != this.field_148004_n)
        {
            field_148000_e = this.field_148004_n;

            if (this.field_148004_n)
            {
                GL11.glEnable(GL11.GL_CULL_FACE);
            }
            else
            {
                GL11.glDisable(GL11.GL_CULL_FACE);
            }
        }

        for (int i = 0; i < this.field_148010_h.size(); ++i)
        {
            if (this.field_147997_f.get(this.field_147998_g.get(i)) != null)
            {
                GL13.glActiveTexture(ARBMultitexture.GL_TEXTURE0_ARB + i);
                GL11.glEnable(GL11.GL_TEXTURE_2D);
                Object object = this.field_147997_f.get(this.field_147998_g.get(i));
                int j = -1;

                if (object instanceof Framebuffer)
                {
                    j = ((Framebuffer)object).framebufferTexture;
                }
                else if (object instanceof ITextureObject)
                {
                    j = ((ITextureObject)object).getGlTextureId();
                }
                else if (object instanceof Integer)
                {
                    j = ((Integer)object).intValue();
                }

                if (j != -1)
                {
                    GL11.glBindTexture(GL11.GL_TEXTURE_2D, j);
                    OpenGlHelper.func_153163_f(OpenGlHelper.func_153194_a(this.field_148006_l, (CharSequence)this.field_147998_g.get(i)), i);
                }
            }
        }

        Iterator iterator = this.field_148011_i.iterator();

        while (iterator.hasNext())
        {
            ShaderUniform shaderuniform = (ShaderUniform)iterator.next();
            shaderuniform.func_148093_b();
        }
    }

    public void func_147985_d()
    {
        this.field_148005_o = true;
    }

    public ShaderUniform func_147991_a(String p_147991_1_)
    {
        return this.field_148009_k.containsKey(p_147991_1_) ? (ShaderUniform)this.field_148009_k.get(p_147991_1_) : null;
    }

    public ShaderUniform func_147984_b(String p_147984_1_)
    {
        return (ShaderUniform)(this.field_148009_k.containsKey(p_147984_1_) ? (ShaderUniform)this.field_148009_k.get(p_147984_1_) : defaultShaderUniform);
    }

    private void func_147990_i()
    {
        int i = 0;
        String s;
        int k;

        for (int j = 0; i < this.field_147998_g.size(); ++j)
        {
            s = (String)this.field_147998_g.get(i);
            k = OpenGlHelper.func_153194_a(this.field_148006_l, s);

            if (k == -1)
            {
                logger.warn("Shader " + this.field_148007_m + "could not find sampler named " + s + " in the specified shader program.");
                this.field_147997_f.remove(s);
                this.field_147998_g.remove(j);
                --j;
            }
            else
            {
                this.field_148010_h.add(Integer.valueOf(k));
            }

            ++i;
        }

        Iterator iterator = this.field_148011_i.iterator();

        while (iterator.hasNext())
        {
            ShaderUniform shaderuniform = (ShaderUniform)iterator.next();
            s = shaderuniform.func_148086_a();
            k = OpenGlHelper.func_153194_a(this.field_148006_l, s);

            if (k == -1)
            {
                logger.warn("Could not find uniform named " + s + " in the specified" + " shader program.");
            }
            else
            {
                this.field_148008_j.add(Integer.valueOf(k));
                shaderuniform.func_148084_b(k);
                this.field_148009_k.put(s, shaderuniform);
            }
        }
    }

    private void func_147996_a(JsonElement p_147996_1_)
    {
        JsonObject jsonobject = JsonUtils.getJsonElementAsJsonObject(p_147996_1_, "sampler");
        String s = JsonUtils.getJsonObjectStringFieldValue(jsonobject, "name");

        if (!JsonUtils.jsonObjectFieldTypeIsString(jsonobject, "file"))
        {
            this.field_147997_f.put(s, (Object)null);
            this.field_147998_g.add(s);
        }
        else
        {
            this.field_147998_g.add(s);
        }
    }

    public void func_147992_a(String p_147992_1_, Object p_147992_2_)
    {
        if (this.field_147997_f.containsKey(p_147992_1_))
        {
            this.field_147997_f.remove(p_147992_1_);
        }

        this.field_147997_f.put(p_147992_1_, p_147992_2_);
        this.func_147985_d();
    }

    private void func_147987_b(JsonElement p_147987_1_) throws JsonException
    {
        JsonObject jsonobject = JsonUtils.getJsonElementAsJsonObject(p_147987_1_, "uniform");
        String s = JsonUtils.getJsonObjectStringFieldValue(jsonobject, "name");
        int i = ShaderUniform.func_148085_a(JsonUtils.getJsonObjectStringFieldValue(jsonobject, "type"));
        int j = JsonUtils.getJsonObjectIntegerFieldValue(jsonobject, "count");
        float[] afloat = new float[Math.max(j, 16)];
        JsonArray jsonarray = JsonUtils.getJsonObjectJsonArrayField(jsonobject, "values");

        if (jsonarray.size() != j && jsonarray.size() > 1)
        {
            throw new JsonException("Invalid amount of values specified (expected " + j + ", found " + jsonarray.size() + ")");
        }
        else
        {
            int k = 0;

            for (Iterator iterator = jsonarray.iterator(); iterator.hasNext(); ++k)
            {
                JsonElement jsonelement1 = (JsonElement)iterator.next();

                try
                {
                    afloat[k] = JsonUtils.getJsonElementFloatValue(jsonelement1, "value");
                }
                catch (Exception exception)
                {
                    JsonException jsonexception = JsonException.func_151379_a(exception);
                    jsonexception.func_151380_a("values[" + k + "]");
                    throw jsonexception;
                }
            }

            if (j > 1 && jsonarray.size() == 1)
            {
                while (k < j)
                {
                    afloat[k] = afloat[0];
                    ++k;
                }
            }

            int l = j > 1 && j <= 4 && i < 8 ? j - 1 : 0;
            ShaderUniform shaderuniform = new ShaderUniform(s, i + l, j, this);

            if (i <= 3)
            {
                shaderuniform.func_148083_a((int)afloat[0], (int)afloat[1], (int)afloat[2], (int)afloat[3]);
            }
            else if (i <= 7)
            {
                shaderuniform.func_148092_b(afloat[0], afloat[1], afloat[2], afloat[3]);
            }
            else
            {
                shaderuniform.func_148097_a(afloat);
            }

            this.field_148011_i.add(shaderuniform);
        }
    }

    public ShaderLoader func_147989_e()
    {
        return this.field_148013_s;
    }

    public ShaderLoader func_147994_f()
    {
        return this.field_148012_t;
    }

    public int func_147986_h()
    {
        return this.field_148006_l;
    }
}