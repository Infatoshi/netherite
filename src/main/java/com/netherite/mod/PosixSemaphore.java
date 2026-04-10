package com.netherite.mod;

import java.lang.foreign.*;
import java.lang.invoke.MethodHandle;

/**
 * POSIX named semaphore wrapper using Panama FFM (Java 21+).
 * Used for low-latency Python-Java signaling.
 */
public class PosixSemaphore {
    private static final Linker LINKER = Linker.nativeLinker();
    private static final SymbolLookup LOOKUP = SymbolLookup.loaderLookup()
            .or(Linker.nativeLinker().defaultLookup());

    private static final int O_CREAT = 0x200;  // macOS value
    private static final long SEM_FAILED = -1L;  // (sem_t*)-1 as unsigned

    private static MethodHandle semOpen;
    private static MethodHandle semPost;
    private static MethodHandle semClose;
    private static MethodHandle semUnlink;

    private MemorySegment semaphore;
    private final String name;
    private boolean initialized = false;

    static {
        try {
            // sem_t* sem_open(const char *name, int oflag, ...) - variadic!
            // When O_CREAT is set, mode_t mode and unsigned int value are passed
            semOpen = LINKER.downcallHandle(
                    LOOKUP.find("sem_open").orElseThrow(),
                    FunctionDescriptor.of(
                            ValueLayout.ADDRESS,
                            ValueLayout.ADDRESS,
                            ValueLayout.JAVA_INT,
                            ValueLayout.JAVA_INT,
                            ValueLayout.JAVA_INT
                    ),
                    Linker.Option.firstVariadicArg(2)  // args after oflag are variadic
            );

            // int sem_post(sem_t *sem)
            semPost = LINKER.downcallHandle(
                    LOOKUP.find("sem_post").orElseThrow(),
                    FunctionDescriptor.of(
                            ValueLayout.JAVA_INT,
                            ValueLayout.ADDRESS
                    )
            );

            // int sem_close(sem_t *sem)
            semClose = LINKER.downcallHandle(
                    LOOKUP.find("sem_close").orElseThrow(),
                    FunctionDescriptor.of(
                            ValueLayout.JAVA_INT,
                            ValueLayout.ADDRESS
                    )
            );

            // int sem_unlink(const char *name)
            semUnlink = LINKER.downcallHandle(
                    LOOKUP.find("sem_unlink").orElseThrow(),
                    FunctionDescriptor.of(
                            ValueLayout.JAVA_INT,
                            ValueLayout.ADDRESS
                    )
            );
        } catch (Exception e) {
            NetheriteMod.LOGGER.error("Failed to initialize POSIX semaphore FFM bindings", e);
        }
    }

    public PosixSemaphore(String name) {
        this.name = name;
    }

    public boolean open() {
        if (initialized) return true;
        if (semOpen == null) {
            NetheriteMod.LOGGER.warn("PosixSemaphore: FFM not available");
            return false;
        }

        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nameSegment = arena.allocateUtf8String(name);
            // Open or create with initial value 0 (mode 0644 octal)
            semaphore = (MemorySegment) semOpen.invoke(nameSegment, O_CREAT, 0644, 0);
            if (semaphore.address() == SEM_FAILED) {
                NetheriteMod.LOGGER.error("PosixSemaphore: sem_open failed for {}", name);
                return false;
            }
            initialized = true;
            NetheriteMod.LOGGER.info("PosixSemaphore: opened {}", name);
            return true;
        } catch (Throwable e) {
            NetheriteMod.LOGGER.error("PosixSemaphore: failed to open {}", name, e);
            return false;
        }
    }

    public void post() {
        if (!initialized || semPost == null) return;
        try {
            semPost.invoke(semaphore);
        } catch (Throwable e) {
            // Ignore - best effort
        }
    }

    public void close() {
        if (!initialized || semClose == null) return;
        try {
            semClose.invoke(semaphore);
            initialized = false;
        } catch (Throwable e) {
            // Ignore
        }
    }

    public void unlink() {
        if (semUnlink == null) return;
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nameSegment = arena.allocateUtf8String(name);
            semUnlink.invoke(nameSegment);
        } catch (Throwable e) {
            // Ignore
        }
    }
}
