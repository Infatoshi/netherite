package net.minecraft.client.renderer;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;
import java.nio.IntBuffer;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import org.lwjgl.opengl.GL11;

@SideOnly(Side.CLIENT)
public class GLAllocation
{
    private static final Map mapDisplayLists = new HashMap();
    private static final List listDummy = new ArrayList();
    private static final String __OBFID = "CL_00000630";

    /**
     * Generates the specified number of display lists and returns the first index.
     */
    public static synchronized int generateDisplayLists(int p_74526_0_)
    {
        int j = GL11.glGenLists(p_74526_0_);
        mapDisplayLists.put(Integer.valueOf(j), Integer.valueOf(p_74526_0_));
        return j;
    }

    public static synchronized void deleteDisplayLists(int p_74523_0_)
    {
        GL11.glDeleteLists(p_74523_0_, ((Integer)mapDisplayLists.remove(Integer.valueOf(p_74523_0_))).intValue());
    }

    /**
     * Deletes all textures and display lists. Called when Minecraft is shutdown to free up resources.
     */
    public static synchronized void deleteTexturesAndDisplayLists()
    {
        Iterator iterator = mapDisplayLists.entrySet().iterator();

        while (iterator.hasNext())
        {
            Entry entry = (Entry)iterator.next();
            GL11.glDeleteLists(((Integer)entry.getKey()).intValue(), ((Integer)entry.getValue()).intValue());
        }

        mapDisplayLists.clear();
    }

    /**
     * Creates and returns a direct byte buffer with the specified capacity. Applies native ordering to speed up access.
     */
    public static synchronized ByteBuffer createDirectByteBuffer(int p_74524_0_)
    {
        return ByteBuffer.allocateDirect(p_74524_0_).order(ByteOrder.nativeOrder());
    }

    /**
     * Creates and returns a direct int buffer with the specified capacity. Applies native ordering to speed up access.
     */
    public static IntBuffer createDirectIntBuffer(int p_74527_0_)
    {
        /**
         * Creates and returns a direct byte buffer with the specified capacity. Applies native ordering to speed up
         * access.
         */
        return createDirectByteBuffer(p_74527_0_ << 2).asIntBuffer();
    }

    /**
     * Creates and returns a direct float buffer with the specified capacity. Applies native ordering to speed up
     * access.
     */
    public static FloatBuffer createDirectFloatBuffer(int p_74529_0_)
    {
        /**
         * Creates and returns a direct byte buffer with the specified capacity. Applies native ordering to speed up
         * access.
         */
        return createDirectByteBuffer(p_74529_0_ << 2).asFloatBuffer();
    }
}