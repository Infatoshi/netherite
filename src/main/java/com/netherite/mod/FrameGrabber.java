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
 */
public class FrameGrabber {
    public static final FrameGrabber INSTANCE = new FrameGrabber();

    private static final int HEADER_SIZE = 16;
    private static final int MAGIC = 0x4E455432; // "NET2"
    private static final int SHMEM_SIZE = 8 * 1024 * 1024; // 8MB per slot

    private int[] pbos = null;
    private ShmemBuffer shmemA;
    private ShmemBuffer shmemB;
    private int frameCount = 0;
    private int lastWidth = 0;
    private int lastHeight = 0;
    private boolean initialized = false;

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
        MinecraftClient mc = MinecraftClient.getInstance();
        if (mc.world == null || shmemA == null) return;

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

        // Kick async read into PBO[readPbo]
        GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER, pbos[readPbo]);
        GL11.glReadPixels(0, 0, width, height, GL11.GL_RGBA, GL11.GL_UNSIGNED_BYTE, 0L);

        // Map PBO[mapPbo] from previous frame and copy to shmem
        if (frameCount > 0) {
            GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER, pbos[mapPbo]);
            ByteBuffer mapped = GL15.glMapBuffer(GL21.GL_PIXEL_PACK_BUFFER, GL15.GL_READ_ONLY);
            if (mapped != null) {
                MappedByteBuffer shmem = (frameCount % 2 == 0 ? shmemA : shmemB).getBuffer();
                shmem.position(0);

                // Header: clear ready, write magic/frame/size, then pixels, then set ready
                shmem.putInt(MAGIC);          // offset 0: magic
                shmem.putInt(frameCount);     // offset 4: frame number
                shmem.putInt(dataSize);        // offset 8: data size
                shmem.putInt(0);              // offset 12: ready=0

                // Pixel data at offset 16
                shmem.position(HEADER_SIZE);
                int remaining = Math.min(mapped.remaining(), dataSize);
                mapped.limit(mapped.position() + remaining);
                shmem.put(mapped);

                // Set ready flag last
                shmem.putInt(12, 1);
                shmem.force();

                GL15.glUnmapBuffer(GL21.GL_PIXEL_PACK_BUFFER);
            }
        }

        GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER, 0);
        frameCount++;
    }
}
