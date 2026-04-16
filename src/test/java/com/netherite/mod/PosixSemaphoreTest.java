package com.netherite.mod;

import org.junit.jupiter.api.Test;

import java.lang.foreign.MemorySegment;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class PosixSemaphoreTest {
    @Test
    void usesMacCreatFlagOnMacOs() {
        assertTrue(PosixSemaphore.oCreatFlagForOs("Mac OS X") == 0x200);
    }

    @Test
    void usesLinuxCreatFlagOnLinux() {
        assertTrue(PosixSemaphore.oCreatFlagForOs("Linux") == 0x40);
    }

    @Test
    void rejectsNullFailedAndZeroHandles() {
        assertTrue(PosixSemaphore.isInvalidHandle(null));
        assertTrue(PosixSemaphore.isInvalidHandle(MemorySegment.NULL));
        assertTrue(PosixSemaphore.isInvalidHandle(MemorySegment.ofAddress(-1L)));
        assertFalse(PosixSemaphore.isInvalidHandle(MemorySegment.ofAddress(1234L)));
    }
}
