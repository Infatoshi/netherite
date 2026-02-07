package net.minecraft.nbt;

import com.mojang.authlib.GameProfile;
import com.mojang.authlib.properties.Property;
import java.util.Iterator;
import java.util.UUID;
import net.minecraft.util.StringUtils;

public final class NBTUtil
{
    private static final String __OBFID = "CL_00001901";

    public static GameProfile func_152459_a(NBTTagCompound p_152459_0_)
    {
        String s = null;
        String s1 = null;

        if (p_152459_0_.hasKey("Name", 8))
        {
            s = p_152459_0_.getString("Name");
        }

        if (p_152459_0_.hasKey("Id", 8))
        {
            s1 = p_152459_0_.getString("Id");
        }

        if (StringUtils.isNullOrEmpty(s) && StringUtils.isNullOrEmpty(s1))
        {
            return null;
        }
        else
        {
            UUID uuid;

            try
            {
                uuid = UUID.fromString(s1);
            }
            catch (Throwable throwable)
            {
                uuid = null;
            }

            GameProfile gameprofile = new GameProfile(uuid, s);

            if (p_152459_0_.hasKey("Properties", 10))
            {
                NBTTagCompound nbttagcompound1 = p_152459_0_.getCompoundTag("Properties");
                Iterator iterator = nbttagcompound1.func_150296_c().iterator();

                while (iterator.hasNext())
                {
                    String s2 = (String)iterator.next();
                    NBTTagList nbttaglist = nbttagcompound1.getTagList(s2, 10);

                    for (int i = 0; i < nbttaglist.tagCount(); ++i)
                    {
                        NBTTagCompound nbttagcompound2 = nbttaglist.getCompoundTagAt(i);
                        String s3 = nbttagcompound2.getString("Value");

                        if (nbttagcompound2.hasKey("Signature", 8))
                        {
                            gameprofile.getProperties().put(s2, new Property(s2, s3, nbttagcompound2.getString("Signature")));
                        }
                        else
                        {
                            gameprofile.getProperties().put(s2, new Property(s2, s3));
                        }
                    }
                }
            }

            return gameprofile;
        }
    }

    public static void func_152460_a(NBTTagCompound p_152460_0_, GameProfile p_152460_1_)
    {
        if (!StringUtils.isNullOrEmpty(p_152460_1_.getName()))
        {
            p_152460_0_.setString("Name", p_152460_1_.getName());
        }

        if (p_152460_1_.getId() != null)
        {
            p_152460_0_.setString("Id", p_152460_1_.getId().toString());
        }

        if (!p_152460_1_.getProperties().isEmpty())
        {
            NBTTagCompound nbttagcompound1 = new NBTTagCompound();
            Iterator iterator = p_152460_1_.getProperties().keySet().iterator();

            while (iterator.hasNext())
            {
                String s = (String)iterator.next();
                NBTTagList nbttaglist = new NBTTagList();
                NBTTagCompound nbttagcompound2;

                for (Iterator iterator1 = p_152460_1_.getProperties().get(s).iterator(); iterator1.hasNext(); nbttaglist.appendTag(nbttagcompound2))
                {
                    Property property = (Property)iterator1.next();
                    nbttagcompound2 = new NBTTagCompound();
                    nbttagcompound2.setString("Value", property.getValue());

                    if (property.hasSignature())
                    {
                        nbttagcompound2.setString("Signature", property.getSignature());
                    }
                }

                nbttagcompound1.setTag(s, nbttaglist);
            }

            p_152460_0_.setTag("Properties", nbttagcompound1);
        }
    }
}