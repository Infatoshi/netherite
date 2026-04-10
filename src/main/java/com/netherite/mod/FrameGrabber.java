package com.netherite.mod;

import net.minecraft.client.MinecraftClient;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL15;
import org.lwjgl.opengl.GL21;

import java.nio.ByteBuffer;
import java.nio.MappedByteBuffer;

/**
 * PBO double-buffered async frame readback to shared memory.
 * Called from GameRendererMixin at end of frame (render thread).
 *
 * Frame header (28 bytes):
 *   0: magic (4)
 *   4: frame number (4)
 *   8: data size (4)
 *  12: ready flag (4)
 *  16: width (4)
 *  20: height (4)
 *  24: state_tick at capture time (4) -- for sync verification
 */
public class FrameGrabber {
    public static final FrameGrabber INSTANCE = new FrameGrabber();

    private static final int HEADER_SIZE = 28;  // extended from 24
    private static final int MAGIC = 0x4E455432; // "NET2"
    private static final int SHMEM_SIZE = 8 * 1024 * 1024; // 8MB per slot
    private static final int PROFILE_INTERVAL = 500; // Log timing every N frames

    private int[] pbos = null;
    private ShmemBuffer shmemA;
    private ShmemBuffer shmemB;
    private int frameCount = 0;
    private int lastWidth = 0;
    private int lastHeight = 0;
    private boolean initialized = false;
    // Track which state tick each PBO was captured at (for sync verification)
    private final int[] captureStateTick = new int[2];

    // Profiling accumulators (nanoseconds)
    private long profileReadPixelsNs = 0;
    private long profileMapBufferNs = 0;
    private long profileShmemCopyNs = 0;
    private long profileTotalNs = 0;
    private int profileCount = 0;

    public void init(int instanceId) {
        shmemA = new ShmemBuffer("netherite_obs_" + instanceId + "_A", SHMEM_SIZE);
        shmemB = new ShmemBuffer("netherite_obs_" + instanceId + "_B", SHMEM_SIZE);
        NetheriteMod.LOGGER.info("FrameGrabber: shmem ready for instance {}", instanceId);
    }

    private void initPBOs(int width, int height) {
        if (pbos != null) {
            GL15.glDeleteBuffers(pbos[0]);
            GL15.glDeleteBuffers(pbos[1]);
        }
        int dataSize = width * height * 4; // RGBA
        pbos = new int[2];
        for (int i = 0; i < 2; i++) {
            pbos[i] = GL15.glGenBuffers();
            GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER, pbos[i]);
            GL15.glBufferData(GL21.GL_PIXEL_PACK_BUFFER, dataSize, GL15.GL_STREAM_READ);
        }
        GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER, 0);
        lastWidth = width;
        lastHeight = height;
        initialized = true;
        NetheriteMod.LOGGER.info("FrameGrabber: PBOs created {}x{}", width, height);
    }

    public void onFrameReady() {
        // Skip frame capture entirely in voxels-only mode
        if (!NetheriteConfig.INSTANCE.needsPixels()) {
            frameCount++;
            return;
        }

        MinecraftClient mc = MinecraftClient.getInstance();
        if (mc.world == null || shmemA == null) return;

        long frameStartNs = System.nanoTime();

        int width = mc.getWindow().getFramebufferWidth();
        int height = mc.getWindow().getFramebufferHeight();
        if (width <= 0 || height <= 0) return;

        if (!initialized || width != lastWidth || height != lastHeight) {
            initPBOs(width, height);
            frameCount = 0;
        }

        int dataSize = width * height * 4;
        int readPbo = frameCount % 2;
        int mapPbo = (frameCount + 1) % 2;

        // Record which state tick this frame corresponds to (matches state shmem)
        int currentStateTick = StateExporter.INSTANCE.getLastWrittenTick();
        captureStateTick[readPbo] = currentStateTick;

        // Profile: glReadPixels (async DMA kick)
        long t0 = System.nanoTime();
        GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER, pbos[readPbo]);
        GL11.glReadPixels(0, 0, width, height, GL11.GL_RGBA, GL11.GL_UNSIGNED_BYTE, 0L);
        long readPixelsNs = System.nanoTime() - t0;

        long mapBufferNs = 0;
        long shmemCopyNs = 0;

        // Map PBO[mapPbo] from previous frame and copy to shmem
        if (frameCount > 0) {
            // Profile: glMapBuffer (sync wait for DMA)
            long t1 = System.nanoTime();
            GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER, pbos[mapPbo]);
            ByteBuffer mapped = GL15.glMapBuffer(GL21.GL_PIXEL_PACK_BUFFER, GL15.GL_READ_ONLY);
            mapBufferNs = System.nanoTime() - t1;

            if (mapped != null) {
                // Profile: shmem copy
                long t2 = System.nanoTime();
                MappedByteBuffer shmem = (frameCount % 2 == 0 ? shmemA : shmemB).getBuffer();
                shmem.position(0);

                // Header: clear ready, write magic/frame/size, then pixels, then set ready
                shmem.putInt(MAGIC);                      // offset 0: magic
                shmem.putInt(frameCount);                 // offset 4: frame number
                shmem.putInt(dataSize);                   // offset 8: data size
                shmem.putInt(0);                          // offset 12: ready=0
                shmem.putInt(width);                      // offset 16: framebuffer width
                shmem.putInt(height);                     // offset 20: framebuffer height
                shmem.putInt(captureStateTick[mapPbo]);   // offset 24: state tick at capture

                // Pixel data at offset 28
                shmem.position(HEADER_SIZE);
                int remaining = Math.min(mapped.remaining(), dataSize);
                mapped.limit(mapped.position() + remaining);
                shmem.put(mapped);

                shmem.putInt(12, 1);
                ShmemBuffer.forceIfEnabled(shmem);
                shmemCopyNs = System.nanoTime() - t2;

                GL15.glUnmapBuffer(GL21.GL_PIXEL_PACK_BUFFER);
            }
        }

        GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER, 0);

        long totalNs = System.nanoTime() - frameStartNs;

        // Accumulate profiling stats
        profileReadPixelsNs += readPixelsNs;
        profileMapBufferNs += mapBufferNs;
        profileShmemCopyNs += shmemCopyNs;
        profileTotalNs += totalNs;
        profileCount++;

        if (profileCount >= PROFILE_INTERVAL) {
            double avgTotal = profileTotalNs / (double) profileCount / 1000.0;
            double avgReadPixels = profileReadPixelsNs / (double) profileCount / 1000.0;
            double avgMapBuffer = profileMapBufferNs / (double) profileCount / 1000.0;
            double avgShmemCopy = profileShmemCopyNs / (double) profileCount / 1000.0;
            NetheriteMod.LOGGER.info(
                "FrameGrabber profile (n={}, {}x{}): total={}us, glReadPixels={}us, glMapBuffer={}us, shmemCopy={}us",
                profileCount, width, height,
                String.format("%.1f", avgTotal),
                String.format("%.1f", avgReadPixels),
                String.format("%.1f", avgMapBuffer),
                String.format("%.1f", avgShmemCopy));
            profileReadPixelsNs = 0;
            profileMapBufferNs = 0;
            profileShmemCopyNs = 0;
            profileTotalNs = 0;
            profileCount = 0;
        }

        frameCount++;
    }
}
