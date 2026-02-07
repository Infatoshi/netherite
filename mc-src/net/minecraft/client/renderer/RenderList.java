package net.minecraft.client.renderer;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.nio.IntBuffer;
import org.lwjgl.opengl.GL11;

@SideOnly(Side.CLIENT)
public class RenderList
{
    /** The location of the 16x16x16 render chunk rendered by this RenderList. */
    public int renderChunkX;
    public int renderChunkY;
    public int renderChunkZ;
    /** The in-world location of the camera, used to translate the world into the proper position for rendering. */
    private double cameraX;
    private double cameraY;
    private double cameraZ;
    /** A list of OpenGL render list IDs rendered by this RenderList. */
    private IntBuffer glLists = GLAllocation.createDirectIntBuffer(65536);
    /** Does this RenderList contain properly-initialized and current data for rendering? */
    private boolean valid;
    /** Has glLists been flipped to make it ready for reading yet? */
    private boolean bufferFlipped;
    private static final String __OBFID = "CL_00000957";

    public void setupRenderList(int p_78422_1_, int p_78422_2_, int p_78422_3_, double p_78422_4_, double p_78422_6_, double p_78422_8_)
    {
        this.valid = true;
        this.glLists.clear();
        this.renderChunkX = p_78422_1_;
        this.renderChunkY = p_78422_2_;
        this.renderChunkZ = p_78422_3_;
        this.cameraX = p_78422_4_;
        this.cameraY = p_78422_6_;
        this.cameraZ = p_78422_8_;
    }

    public boolean rendersChunk(int p_78418_1_, int p_78418_2_, int p_78418_3_)
    {
        return !this.valid ? false : p_78418_1_ == this.renderChunkX && p_78418_2_ == this.renderChunkY && p_78418_3_ == this.renderChunkZ;
    }

    public void addGLRenderList(int p_78420_1_)
    {
        this.glLists.put(p_78420_1_);

        if (this.glLists.remaining() == 0)
        {
            this.callLists();
        }
    }

    public void callLists()
    {
        if (this.valid)
        {
            if (!this.bufferFlipped)
            {
                this.glLists.flip();
                this.bufferFlipped = true;
            }

            if (this.glLists.remaining() > 0)
            {
                GL11.glPushMatrix();
                GL11.glTranslatef((float)((double)this.renderChunkX - this.cameraX), (float)((double)this.renderChunkY - this.cameraY), (float)((double)this.renderChunkZ - this.cameraZ));
                GL11.glCallLists(this.glLists);
                GL11.glPopMatrix();
            }
        }
    }

    /**
     * Resets this RenderList to an uninitialized state.
     */
    public void resetList()
    {
        this.valid = false;
        this.bufferFlipped = false;
    }
}