package net.minecraft.util;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.awt.image.BufferedImage;
import java.io.File;
import java.nio.IntBuffer;
import java.text.DateFormat;
import java.text.SimpleDateFormat;
import java.util.Date;
import javax.imageio.ImageIO;
import net.minecraft.client.renderer.OpenGlHelper;
import net.minecraft.client.renderer.texture.TextureUtil;
import net.minecraft.client.shader.Framebuffer;
import net.minecraft.event.ClickEvent;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;
import org.lwjgl.BufferUtils;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL12;

@SideOnly(Side.CLIENT)
public class ScreenShotHelper
{
    private static final Logger logger = LogManager.getLogger();
    private static final DateFormat dateFormat = new SimpleDateFormat("yyyy-MM-dd_HH.mm.ss");
    /** A buffer to hold pixel values returned by OpenGL. */
    private static IntBuffer pixelBuffer;
    /** The built-up array that contains all the pixel values returned by OpenGL. */
    private static int[] pixelValues;
    private static final String __OBFID = "CL_00000656";

    /**
     * Saves a screenshot in the game directory with a time-stamped filename.  Args: gameDirectory,
     * requestedWidthInPixels, requestedHeightInPixels, frameBuffer
     */
    public static IChatComponent saveScreenshot(File p_148260_0_, int p_148260_1_, int p_148260_2_, Framebuffer p_148260_3_)
    {
        /**
         * Saves a screenshot in the game directory with the given file name (or null to generate a time-stamped name).
         * Args: gameDirectory, fileName, requestedWidthInPixels, requestedHeightInPixels, frameBuffer
         */
        return saveScreenshot(p_148260_0_, (String)null, p_148260_1_, p_148260_2_, p_148260_3_);
    }

    /**
     * Saves a screenshot in the game directory with the given file name (or null to generate a time-stamped name).
     * Args: gameDirectory, fileName, requestedWidthInPixels, requestedHeightInPixels, frameBuffer
     */
    public static IChatComponent saveScreenshot(File p_148259_0_, String p_148259_1_, int p_148259_2_, int p_148259_3_, Framebuffer p_148259_4_)
    {
        try
        {
            File file2 = new File(p_148259_0_, "screenshots");
            file2.mkdir();

            if (OpenGlHelper.isFramebufferEnabled())
            {
                p_148259_2_ = p_148259_4_.framebufferTextureWidth;
                p_148259_3_ = p_148259_4_.framebufferTextureHeight;
            }

            int k = p_148259_2_ * p_148259_3_;

            if (pixelBuffer == null || pixelBuffer.capacity() < k)
            {
                pixelBuffer = BufferUtils.createIntBuffer(k);
                pixelValues = new int[k];
            }

            GL11.glPixelStorei(GL11.GL_PACK_ALIGNMENT, 1);
            GL11.glPixelStorei(GL11.GL_UNPACK_ALIGNMENT, 1);
            pixelBuffer.clear();

            if (OpenGlHelper.isFramebufferEnabled())
            {
                GL11.glBindTexture(GL11.GL_TEXTURE_2D, p_148259_4_.framebufferTexture);
                GL11.glGetTexImage(GL11.GL_TEXTURE_2D, 0, GL12.GL_BGRA, GL12.GL_UNSIGNED_INT_8_8_8_8_REV, pixelBuffer);
            }
            else
            {
                GL11.glReadPixels(0, 0, p_148259_2_, p_148259_3_, GL12.GL_BGRA, GL12.GL_UNSIGNED_INT_8_8_8_8_REV, pixelBuffer);
            }

            pixelBuffer.get(pixelValues);
            TextureUtil.func_147953_a(pixelValues, p_148259_2_, p_148259_3_);
            BufferedImage bufferedimage = null;

            if (OpenGlHelper.isFramebufferEnabled())
            {
                bufferedimage = new BufferedImage(p_148259_4_.framebufferWidth, p_148259_4_.framebufferHeight, 1);
                int l = p_148259_4_.framebufferTextureHeight - p_148259_4_.framebufferHeight;

                for (int i1 = l; i1 < p_148259_4_.framebufferTextureHeight; ++i1)
                {
                    for (int j1 = 0; j1 < p_148259_4_.framebufferWidth; ++j1)
                    {
                        bufferedimage.setRGB(j1, i1 - l, pixelValues[i1 * p_148259_4_.framebufferTextureWidth + j1]);
                    }
                }
            }
            else
            {
                bufferedimage = new BufferedImage(p_148259_2_, p_148259_3_, 1);
                bufferedimage.setRGB(0, 0, p_148259_2_, p_148259_3_, pixelValues, 0, p_148259_2_);
            }

            File file3;

            if (p_148259_1_ == null)
            {
                file3 = getTimestampedPNGFileForDirectory(file2);
            }
            else
            {
                file3 = new File(file2, p_148259_1_);
            }

            ImageIO.write(bufferedimage, "png", file3);
            ChatComponentText chatcomponenttext = new ChatComponentText(file3.getName());
            chatcomponenttext.getChatStyle().setChatClickEvent(new ClickEvent(ClickEvent.Action.OPEN_FILE, file3.getAbsolutePath()));
            chatcomponenttext.getChatStyle().setUnderlined(Boolean.valueOf(true));
            return new ChatComponentTranslation("screenshot.success", new Object[] {chatcomponenttext});
        }
        catch (Exception exception)
        {
            logger.warn("Couldn\'t save screenshot", exception);
            return new ChatComponentTranslation("screenshot.failure", new Object[] {exception.getMessage()});
        }
    }

    /**
     * Creates a unique PNG file in the given directory named by a timestamp.  Handles cases where the timestamp alone
     * is not enough to create a uniquely named file, though it still might suffer from an unlikely race condition where
     * the filename was unique when this method was called, but another process or thread created a file at the same
     * path immediately after this method returned.
     */
    private static File getTimestampedPNGFileForDirectory(File p_74290_0_)
    {
        String s = dateFormat.format(new Date()).toString();
        int i = 1;

        while (true)
        {
            File file2 = new File(p_74290_0_, s + (i == 1 ? "" : "_" + i) + ".png");

            if (!file2.exists())
            {
                return file2;
            }

            ++i;
        }
    }
}