package netherite.oracle;

import java.util.concurrent.Semaphore;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;
import net.minecraft.client.Minecraft;
import net.minecraft.server.MinecraftServer;

/**
 * Pairs every client tick with exactly one integrated-server tick.
 *
 * Order per pair: client runTick, wait until every client packet has been
 * delivered to the server connection, permit one server tick, wait for it,
 * wait until every server packet has been delivered to the client connection.
 * The client thread never runs while the server ticks, so packet arrival and
 * shared-state interleaving are identical on every run.
 */
public final class Lockstep
{
    /** Set by the patched MinecraftServer.run before it reports being in its run loop. */
    public static volatile MinecraftServer server;

    // Local-channel packet counters, maintained by the patched NetworkManager.
    public static final AtomicLong c2sSent = new AtomicLong(), c2sRecv = new AtomicLong();
    public static final AtomicLong s2cSent = new AtomicLong(), s2cRecv = new AtomicLong();

    static final Semaphore permit = new Semaphore(0), done = new Semaphore(0);
    static final long TIMEOUT_NS = 120L * 1000000000L;

    private Lockstep() {}

    public static void onSend(boolean clientSide)
    {
        (clientSide ? c2sSent : s2cSent).incrementAndGet();
    }

    public static void onRecv(boolean clientSide)
    {
        (clientSide ? s2cRecv : c2sRecv).incrementAndGet();
    }

    /** A pool JVM is stopping the server: the permit it releases wakes the loop without a tick. */
    static volatile boolean stopping;

    /** Server thread: wait for one tick permit. False on timeout so the caller can re-check serverRunning. */
    public static boolean serverAwait() throws InterruptedException
    {
        Prof.serverReady();
        boolean go = permit.tryAcquire(100L, TimeUnit.MILLISECONDS) && !stopping;
        if (go && Prof.on) Prof.sStart = System.nanoTime();
        return go;
    }

    public static void serverDone()
    {
        if (Prof.on) Prof.sEnd = System.nanoTime();
        RowHash.Job j = RowHash.serverTickEnd(server);
        if (Prof.on) Prof.doneAt = System.nanoTime();
        done.release();
        if (j != null) j.serialize(); // idle until the next permit, which waits for this anyway
    }

    /** Client thread, after runTick. */
    static void pair(Minecraft mc)
    {
        MinecraftServer s = server;
        if (s == null || mc.getIntegratedServer() != s) return;
        drain(c2sSent, c2sRecv, "client->server");
        if (Prof.on) Prof.lap(Prof.C2S);
        RowHash.awaitSerialized(); // a row's entities are read before the server changes them
        if (Prof.on)
        {
            Prof.lap(Prof.SER);
            Prof.permitAt = System.nanoTime();
        }
        permit.release();
        long t0 = System.nanoTime();
        try
        {
            while (!done.tryAcquire(100L, TimeUnit.MILLISECONDS))
            {
                if (!s.isServerRunning()) return;
                if (System.nanoTime() - t0 > TIMEOUT_NS) throw new IllegalStateException("oracle: server tick did not finish");
            }
        }
        catch (InterruptedException e)
        {
            throw new RuntimeException(e);
        }
        if (Prof.on)
        {
            Prof.lap(Prof.WAIT);
            Prof.paired();
        }
        RowHash.onPaired();
        drain(s2cSent, s2cRecv, "server->client");
        if (Prof.on) Prof.lap(Prof.S2C);
    }

    /** Client thread, after a setup command: every packet it made the server
     * send has reached the client connection, so the next client tick reads
     * them whether the command ran in the recording or in its replay. */
    static void settleSetup()
    {
        if (server != null) drain(s2cSent, s2cRecv, "server->client");
    }

    static void drain(AtomicLong sent, AtomicLong recv, String dir)
    {
        long want = sent.get();
        long t0 = System.nanoTime();
        while (recv.get() < want)
        {
            if (System.nanoTime() - t0 > TIMEOUT_NS)
                throw new IllegalStateException("oracle: " + dir + " packets stuck at " + recv.get() + "/" + want);
            Thread.yield();
        }
    }
}
