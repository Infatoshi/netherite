package netherite.oracle;

import java.awt.image.BufferedImage;
import java.io.File;
import java.io.IOException;
import java.nio.IntBuffer;
import java.security.MessageDigest;
import javax.imageio.ImageIO;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.OpenGlHelper;
import net.minecraft.client.shader.Framebuffer;
import org.lwjgl.BufferUtils;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL12;

/** Framebuffer readback, same path as ScreenShotHelper. Returns the SHA-1 of the RGB pixels. */
final class Frames
{
    private static IntBuffer buf;
    private static int[] px;

    private Frames() {}

    static String capture(Minecraft mc, Framebuffer fb, File out) throws IOException
    {
        int tw, th, w, h;
        boolean useFb = OpenGlHelper.isFramebufferEnabled();
        if (useFb)
        {
            tw = fb.framebufferTextureWidth;
            th = fb.framebufferTextureHeight;
            w = fb.framebufferWidth;
            h = fb.framebufferHeight;
        }
        else
        {
            tw = w = mc.displayWidth;
            th = h = mc.displayHeight;
        }
        int n = tw * th;
        if (buf == null || buf.capacity() < n)
        {
            buf = BufferUtils.createIntBuffer(n);
            px = new int[n];
        }
        GL11.glPixelStorei(GL11.GL_PACK_ALIGNMENT, 1);
        GL11.glPixelStorei(GL11.GL_UNPACK_ALIGNMENT, 1);
        buf.clear();
        if (useFb)
        {
            GL11.glBindTexture(GL11.GL_TEXTURE_2D, fb.framebufferTexture);
            GL11.glGetTexImage(GL11.GL_TEXTURE_2D, 0, GL12.GL_BGRA, GL12.GL_UNSIGNED_INT_8_8_8_8_REV, buf);
        }
        else
        {
            GL11.glReadPixels(0, 0, tw, th, GL12.GL_BGRA, GL12.GL_UNSIGNED_INT_8_8_8_8_REV, buf);
        }
        buf.get(px, 0, n);
        BufferedImage img = new BufferedImage(w, h, BufferedImage.TYPE_INT_RGB);
        MessageDigest md;
        try
        {
            md = MessageDigest.getInstance("SHA-1");
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
        byte[] row = new byte[w * 3];
        // GL rows are bottom-up; image rows are top-down.
        for (int y = 0; y < h; ++y)
        {
            int src = (h - 1 - y) * tw;
            for (int x = 0; x < w; ++x)
            {
                int c = px[src + x] & 0xffffff;
                img.setRGB(x, y, c);
                row[3 * x] = (byte)(c >> 16);
                row[3 * x + 1] = (byte)(c >> 8);
                row[3 * x + 2] = (byte)c;
            }
            md.update(row);
        }
        if (out != null)
        {
            if (out.getParentFile() != null) out.getParentFile().mkdirs();
            ImageIO.write(img, "png", out);
        }
        byte[] d = md.digest();
        long v = 0;
        for (int i = 0; i < 8; ++i) v = (v << 8) | (d[i] & 0xff);
        return Rows.hex(v);
    }
}
