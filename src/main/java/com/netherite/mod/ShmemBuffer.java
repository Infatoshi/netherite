package com.netherite.mod;

import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.MappedByteBuffer;
import java.nio.channels.FileChannel;

/**
 * Memory-mapped shared memory buffer. macOS: /tmp, Linux: /dev/shm.
 */
public class ShmemBuffer {
    private static final boolean FORCE_WRITES = Boolean.getBoolean("netherite.force_shmem");
    private final MappedByteBuffer buffer;
    private final RandomAccessFile raf;

    public ShmemBuffer(String name, int size) {
        String prefix = System.getProperty("os.name").toLowerCase().contains("mac")
                ? "/tmp/" : "/dev/shm/";
        String path = prefix + name;
        try {
            raf = new RandomAccessFile(path, "rw");
            raf.setLength(size);
            buffer = raf.getChannel().map(FileChannel.MapMode.READ_WRITE, 0, size);
            buffer.order(ByteOrder.LITTLE_ENDIAN);
        } catch (IOException e) {
            throw new RuntimeException("Failed to open shmem: " + path, e);
        }
    }

    public MappedByteBuffer getBuffer() {
        return buffer;
    }

    public static boolean forceWritesEnabled() {
        return FORCE_WRITES;
    }

    public static void forceIfEnabled(MappedByteBuffer buffer) {
        if (FORCE_WRITES) {
            buffer.force();
        }
    }

    public void close() {
        try {
            raf.close();
        } catch (IOException e) {
            // ignore
        }
    }
}
