package net.minecraft.client.resources;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.File;
import java.io.FileNotFoundException;

@SideOnly(Side.CLIENT)
public class ResourcePackFileNotFoundException extends FileNotFoundException
{
    private static final String __OBFID = "CL_00001086";

    public ResourcePackFileNotFoundException(File p_i1294_1_, String p_i1294_2_)
    {
        super(String.format("\'%s\' in ResourcePack \'%s\'", new Object[] {p_i1294_2_, p_i1294_1_}));
    }
}