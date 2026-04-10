"""POSIX semaphore-based synchronization for low-latency Python-Java IPC."""

import posix_ipc


class SemaphoreSync:
    """Replace polling with kernel-level semaphore signaling.

    Java calls sem_post() after writing state, Python blocks on sem.acquire().
    This eliminates the polling loop overhead (~1.5ms -> ~10-50us expected).
    """

    def __init__(self, instance_id: int = 0):
        self.instance_id = instance_id
        self._sem_name = f"/netherite_state_{instance_id}"
        self._sem = None

    def open(self):
        """Open or create the semaphore."""
        try:
            # Try to open existing semaphore (Java creates it)
            self._sem = posix_ipc.Semaphore(self._sem_name)
        except posix_ipc.ExistentialError:
            # Create it if Java hasn't yet (Python creates, Java opens)
            self._sem = posix_ipc.Semaphore(
                self._sem_name,
                flags=posix_ipc.O_CREAT,
                initial_value=0,
            )

    def wait(self, timeout: float = 1.0) -> bool:
        """Block until Java signals state is ready.

        Returns True if signaled, False if timed out.
        """
        if self._sem is None:
            self.open()
        try:
            self._sem.acquire(timeout=timeout)
            return True
        except posix_ipc.BusyError:
            return False

    def signal(self):
        """Signal that state is ready (called from Java side normally)."""
        if self._sem is None:
            self.open()
        self._sem.release()

    def close(self):
        """Close the semaphore."""
        if self._sem is not None:
            self._sem.close()
            self._sem = None

    def unlink(self):
        """Remove the semaphore from the system."""
        try:
            posix_ipc.unlink_semaphore(self._sem_name)
        except posix_ipc.ExistentialError:
            pass

    @staticmethod
    def cleanup(instance_id: int = 0):
        """Clean up semaphore for an instance."""
        try:
            posix_ipc.unlink_semaphore(f"/netherite_state_{instance_id}")
        except posix_ipc.ExistentialError:
            pass
