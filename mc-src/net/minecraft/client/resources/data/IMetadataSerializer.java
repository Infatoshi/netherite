package net.minecraft.client.resources.data;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonObject;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.util.ChatStyle;
import net.minecraft.util.EnumTypeAdapterFactory;
import net.minecraft.util.IChatComponent;
import net.minecraft.util.IRegistry;
import net.minecraft.util.RegistrySimple;

@SideOnly(Side.CLIENT)
public class IMetadataSerializer
{
    private final IRegistry metadataSectionSerializerRegistry = new RegistrySimple();
    private final GsonBuilder gsonBuilder = new GsonBuilder();
    /** Cached Gson instance. Set to null when more sections are registered, and then re-created from the builder. */
    private Gson gson;
    private static final String __OBFID = "CL_00001101";

    public IMetadataSerializer()
    {
        this.gsonBuilder.registerTypeHierarchyAdapter(IChatComponent.class, new IChatComponent.Serializer());
        this.gsonBuilder.registerTypeHierarchyAdapter(ChatStyle.class, new ChatStyle.Serializer());
        this.gsonBuilder.registerTypeAdapterFactory(new EnumTypeAdapterFactory());
    }

    public void registerMetadataSectionType(IMetadataSectionSerializer p_110504_1_, Class p_110504_2_)
    {
        this.metadataSectionSerializerRegistry.putObject(p_110504_1_.getSectionName(), new IMetadataSerializer.Registration(p_110504_1_, p_110504_2_, null));
        this.gsonBuilder.registerTypeAdapter(p_110504_2_, p_110504_1_);
        this.gson = null;
    }

    public IMetadataSection parseMetadataSection(String p_110503_1_, JsonObject p_110503_2_)
    {
        if (p_110503_1_ == null)
        {
            throw new IllegalArgumentException("Metadata section name cannot be null");
        }
        else if (!p_110503_2_.has(p_110503_1_))
        {
            return null;
        }
        else if (!p_110503_2_.get(p_110503_1_).isJsonObject())
        {
            throw new IllegalArgumentException("Invalid metadata for \'" + p_110503_1_ + "\' - expected object, found " + p_110503_2_.get(p_110503_1_));
        }
        else
        {
            IMetadataSerializer.Registration registration = (IMetadataSerializer.Registration)this.metadataSectionSerializerRegistry.getObject(p_110503_1_);

            if (registration == null)
            {
                throw new IllegalArgumentException("Don\'t know how to handle metadata section \'" + p_110503_1_ + "\'");
            }
            else
            {
                return (IMetadataSection)this.getGson().fromJson(p_110503_2_.getAsJsonObject(p_110503_1_), registration.field_110500_b);
            }
        }
    }

    /**
     * Returns a Gson instance with type adapters registered for metadata sections.
     */
    private Gson getGson()
    {
        if (this.gson == null)
        {
            this.gson = this.gsonBuilder.create();
        }

        return this.gson;
    }

    @SideOnly(Side.CLIENT)
    class Registration
    {
        final IMetadataSectionSerializer field_110502_a;
        final Class field_110500_b;
        private static final String __OBFID = "CL_00001103";

        private Registration(IMetadataSectionSerializer p_i1305_2_, Class p_i1305_3_)
        {
            this.field_110502_a = p_i1305_2_;
            this.field_110500_b = p_i1305_3_;
        }

        Registration(IMetadataSectionSerializer p_i1306_2_, Class p_i1306_3_, Object p_i1306_4_)
        {
            this(p_i1306_2_, p_i1306_3_);
        }
    }
}