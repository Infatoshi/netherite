package netheritemod;

import net.minecraft.client.Minecraft;
import net.minecraft.client.shader.Framebuffer;
import net.minecraftforge.common.MinecraftForge;
import net.minecraftforge.fml.common.eventhandler.SubscribeEvent;
import net.minecraftforge.fml.common.gameevent.TickEvent;

import javax.imageio.IIOImage;
import javax.imageio.ImageIO;
import javax.imageio.ImageWriteParam;
import javax.imageio.ImageWriter;
import javax.imageio.stream.MemoryCacheImageOutputStream;
import java.awt.image.BufferedImage;
import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.IntBuffer;

/**
 * MineRL-style human-play frame stream: pushes every rendered frame (game +
 * GUI, straight from the MC framebuffer — no X11 capture anywhere) as JPEG
 * over a local TCP socket. Input comes in via XTEST on the X server, not here.
 *
 * Protocol (one-way, per frame): "QHF1" magic, then int32 width, int32 height,
 * int32 jpegLen, then jpegLen bytes (big-endian, DataOutputStream). One client;
 * a new connection replaces the old. Zero render-thread cost with no client.
 */
public final class HumanStream {
    static final int PORT = 25580;
    private static final float JPEG_QUALITY = 0.85f;

    private static volatile Socket client = null;

    // Render-thread capture buffers (alloc once, grow only on resize).
    private static IntBuffer texBuf = null;
    private static int[] latest = null;          // handoff slot, guarded by LOCK
    private static int latestW, latestH, latestStride;
    private static boolean latestFresh = false;
    private static final Object LOCK = new Object();

    // Encoder-thread state.
    private static BufferedImage img = null;
    private static int[] flipRow = null;

    // Mouse-look deltas from the viewer (pixels), applied on the render thread
    // with the vanilla sensitivity math. XTEST relative motion is useless for
    // look: LWJGL2's pointer grab is unreliable under GNOME on headless X, so
    // the viewer sends look deltas here instead and never moves the X pointer
    // while captured.
    private static float lookDx = 0, lookDy = 0;
    private static final Object LOOK_LOCK = new Object();

    /** True while a human viewer is connected (gates RL-only automation). */
    public static boolean hasViewer() {
        return client != null;
    }

    public static void install() {
        MinecraftForge.EVENT_BUS.register(new HumanStream());
        Thread acc = new Thread(HumanStream::acceptLoop, "qhs-accept");
        acc.setDaemon(true);
        acc.start();
        Thread enc = new Thread(HumanStream::encodeLoop, "qhs-encode");
        enc.setDaemon(true);
        enc.start();
    }

    private static void acceptLoop() {
        try (ServerSocket server = new ServerSocket(PORT, 1, InetAddress.getByName("127.0.0.1"))) {
            System.out.println("[qhs] human stream listening on 127.0.0.1:" + PORT);
            while (true) {
                Socket s = server.accept();
                s.setTcpNoDelay(true);
                Socket old = client;
                client = s;
                if (old != null) try { old.close(); } catch (IOException ig) {}
                System.out.println("[qhs] viewer connected from " + s.getRemoteSocketAddress());
                Thread rd = new Thread(() -> readLook(s), "qhs-look");
                rd.setDaemon(true);
                rd.start();
            }
        } catch (IOException e) {
            System.out.println("[qhs] accept loop dead: " + e);
        }
    }

    /** Incoming look lines: {"t":"look","dx":N,"dy":N} (dy positive = down). */
    private static void readLook(Socket s) {
        try {
            BufferedReader in = new BufferedReader(new InputStreamReader(s.getInputStream()));
            String line;
            while ((line = in.readLine()) != null) {
                try {
                    com.google.gson.JsonObject o =
                        new com.google.gson.JsonParser().parse(line).getAsJsonObject();
                    if ("look".equals(o.get("t").getAsString())) {
                        synchronized (LOOK_LOCK) {
                            lookDx += o.get("dx").getAsFloat();
                            lookDy += o.get("dy").getAsFloat();
                        }
                    }
                } catch (Throwable ig) { /* bad line: skip */ }
            }
        } catch (IOException ig) { /* viewer gone; encode loop handles cleanup */ }
    }

    @SubscribeEvent
    public void onRenderTick(TickEvent.RenderTickEvent e) {
        if (e.phase != TickEvent.Phase.END || client == null) return;
        Minecraft mc = Minecraft.getMinecraft();
        if (mc == null) return;
        // Apply pending look with the literal vanilla path (EntityRenderer
        // sensitivity curve -> Entity.turn's *0.15), once per frame like the
        // real mouse. Ignored while a GUI is open, same as vanilla.
        float dx, dy;
        synchronized (LOOK_LOCK) { dx = lookDx; dy = lookDy; lookDx = 0; lookDy = 0; }
        if ((dx != 0 || dy != 0) && mc.player != null && mc.currentScreen == null) {
            try {
                float f = mc.gameSettings.mouseSensitivity * 0.6F + 0.2F;
                float f1 = f * f * f * 8.0F;
                int i = mc.gameSettings.invertMouse ? -1 : 1;
                mc.player.turn(dx * f1, -dy * f1 * (float) i);
            } catch (Throwable ig) { /* never break rendering */ }
        }
        try {
            Framebuffer fb = mc.getFramebuffer();
            if (fb == null || fb.framebufferTexture <= 0) return;
            int w = fb.framebufferWidth, h = fb.framebufferHeight;
            int tw = fb.framebufferTextureWidth, th = fb.framebufferTextureHeight;
            if (w <= 0 || h <= 0) return;
            if (texBuf == null || texBuf.capacity() < tw * th) {
                texBuf = org.lwjgl.BufferUtils.createIntBuffer(tw * th);
            }
            texBuf.clear();
            org.lwjgl.opengl.GL11.glBindTexture(org.lwjgl.opengl.GL11.GL_TEXTURE_2D, fb.framebufferTexture);
            org.lwjgl.opengl.GL11.glGetTexImage(org.lwjgl.opengl.GL11.GL_TEXTURE_2D, 0,
                org.lwjgl.opengl.GL12.GL_BGRA,
                org.lwjgl.opengl.GL12.GL_UNSIGNED_INT_8_8_8_8_REV, texBuf);
            synchronized (LOCK) {
                if (latest == null || latest.length < tw * th) latest = new int[tw * th];
                texBuf.position(0);
                texBuf.get(latest, 0, tw * th);
                latestW = w; latestH = h; latestStride = tw;
                latestFresh = true;
                LOCK.notify();
            }
        } catch (Throwable t) { /* never break rendering */ }
    }

    private static void encodeLoop() {
        int[] px = null;
        ImageWriter writer = ImageIO.getImageWritersByFormatName("jpg").next();
        ImageWriteParam param = writer.getDefaultWriteParam();
        param.setCompressionMode(ImageWriteParam.MODE_EXPLICIT);
        param.setCompressionQuality(JPEG_QUALITY);
        ByteArrayOutputStream bos = new ByteArrayOutputStream(1 << 20);
        long frames = 0, windowStart = System.nanoTime();
        while (true) {
            int w, h, stride;
            synchronized (LOCK) {
                while (!latestFresh) {
                    try { LOCK.wait(500); } catch (InterruptedException ie) { return; }
                    if (!latestFresh) continue;
                }
                w = latestW; h = latestH; stride = latestStride;
                if (px == null || px.length < latest.length) px = new int[latest.length];
                System.arraycopy(latest, 0, px, 0, stride * h);
                latestFresh = false;
            }
            Socket s = client;
            if (s == null) continue;
            try {
                if (img == null || img.getWidth() != w || img.getHeight() != h) {
                    img = new BufferedImage(w, h, BufferedImage.TYPE_INT_RGB);
                    flipRow = new int[w];
                }
                // GL rows are bottom-up; write flipped into the image raster.
                int[] dst = ((java.awt.image.DataBufferInt) img.getRaster().getDataBuffer()).getData();
                for (int y = 0; y < h; y++) {
                    System.arraycopy(px, (h - 1 - y) * stride, dst, y * w, w);
                }
                bos.reset();
                MemoryCacheImageOutputStream ios = new MemoryCacheImageOutputStream(bos);
                writer.setOutput(ios);
                writer.write(null, new IIOImage(img, null, null), param);
                ios.flush();
                DataOutputStream out = new DataOutputStream(s.getOutputStream());
                out.writeInt(0x51484631); // "QHF1"
                out.writeInt(w);
                out.writeInt(h);
                out.writeInt(bos.size());
                bos.writeTo(out);
                out.flush();
                frames++;
                long now = System.nanoTime();
                if (now - windowStart > 5_000_000_000L) {
                    System.out.println(String.format("[qhs] %dx%d %.1f fps sent", w, h,
                        frames * 1e9 / (now - windowStart)));
                    frames = 0; windowStart = now;
                }
            } catch (IOException io) {
                System.out.println("[qhs] viewer gone: " + io.getMessage());
                if (client == s) client = null;
                try { s.close(); } catch (IOException ig) {}
            } catch (Throwable t) {
                System.out.println("[qhs] encode error: " + t);
            }
        }
    }
}
