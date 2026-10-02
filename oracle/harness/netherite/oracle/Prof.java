package netherite.oracle;

/**
 * --tick-profile: where the wall time of the tick loop goes, one line at the
 * end of the run (ORACLE PROFILE). The client thread laps through its phases
 * in the order a tick pair passes them; the server thread stamps its side of
 * the handoff. Off by default: every call site tests one static boolean.
 *
 *   frame      the rest of the client's frame (a render when one is asked for)
 *   cmd        the command source (script, bot, replay row), Oracle.commands
 *   client     preTick and Minecraft.runTick
 *   c2s, s2c   the packet drains around the server's tick
 *   ser        RowHash: the last row's entities serialized before the permit
 *   wait       permit to done: wakeS (the server's wake), stick (its tick),
 *              sjob (RowHash's job start), wakeC (the client's wake)
 *   flushprev  the held row completed and written at the capture
 *   capture    Rows.capture
 *   emit       the row emitted, and a reply when there is a reader
 *
 * tps from N is the rate after pair N, tps to N the rate up to it (the JIT's
 * warm-up). A second line, ORACLE PROFILE RUN, splits the whole run from the
 * JVM's start: to the first pair (JVM and client start, the world's launch
 * and join), the loop, and from the last pair to the report (a SAVEEND or a
 * save's snapshot and copy); the wait before the JVM started is
 * tests/headless.sh's ORACLE QUEUE line.
 *
 * A third, ORACLE PROFILE PHASES, is the run's timeline from its start (the
 * JVM's, or in a pool member the job's arrival; start= is that instant in
 * epoch ms, for a caller's own clock): jvm (to Main.main), client (the
 * client's startGame), pool (a member: from the job's arrival to its world
 * launch: the last world's teardown and the rewind, or a prelaunch of
 * another key finishing first; 0 when the job takes a prelaunched world),
 * save (the save deleted, a checkpoint installed, the Det reset), spawn (the
 * server thread's world load and spawn area, to its first tick wait), poll
 * (until the client saw it and connected), join (to the first pair), loop,
 * end. A phase a prelaunched world ran before the job arrived is marked pre.
 */
final class Prof
{
    private Prof() {}

    static boolean on;

    static final int FRAME = 0, CMD = 1, CLIENT = 2, C2S = 3, SER = 4, WAIT = 5, S2C = 6, FLUSHPREV = 7, CAPTURE = 8, EMIT = 9, N = 10;
    static final String[] NAMES = {"frame", "cmd", "client", "c2s", "ser", "wait", "s2c", "flushprev", "capture", "emit"};
    static final long[] sum = new long[N];
    static long mark, pairs, first, last;
    static final long[] AT = {100, 500, 1000, 2000, 4000};
    static final long[] at = new long[AT.length];

    // the handoff: permit released (client), tick started and ended, done released (server)
    static volatile long permitAt, sStart, sEnd, doneAt;
    static long wakeS, sTick, sJob, wakeC;

    // the run's timeline (System.nanoTime; 0 until reached): Main.main, the end of startGame, the world's
    // launch, the save ready, the server's first tick wait, the client's launchIntegratedServer returned
    static volatile long tMain, tStarted, tLaunch, tSave, tServerReady, tServerUp;
    /** A pool job's arrival (Pool.accept). */
    static long tJob;

    static void serverReady()
    {
        if (tServerReady == 0) tServerReady = System.nanoTime();
    }

    static void lap(int phase)
    {
        long now = System.nanoTime();
        if (mark != 0) sum[phase] += now - mark;
        mark = now;
    }

    /** Client, the done semaphore taken: the server side of this pair. */
    static void paired()
    {
        long now = System.nanoTime();
        wakeS += sStart - permitAt;
        sTick += sEnd - sStart;
        sJob += doneAt - sEnd;
        wakeC += now - doneAt;
        if (first == 0) first = now;
        last = now;
        for (int i = 0; i < AT.length; ++i) if (pairs == AT[i]) at[i] = now;
        ++pairs;
    }

    static String ms(long ns)
    {
        return String.format("%.3f", ns / 1e6 / pairs);
    }

    static void report()
    {
        if (!on || pairs == 0) return;
        double wall = (last - first) / 1e9;
        StringBuilder b = new StringBuilder(String.format("ORACLE PROFILE pairs=%d wall=%.2fs tps=%.1f ms/pair:", pairs, wall, (pairs - 1) / Math.max(1e-9, wall)));
        long tot = 0;
        for (int i = 0; i < N; ++i)
        {
            b.append(' ').append(NAMES[i]).append('=').append(ms(sum[i]));
            tot += sum[i];
        }
        b.append(" total=").append(ms(tot));
        b.append(" | wait: wakeS=").append(ms(wakeS)).append(" stick=").append(ms(sTick)).append(" sjob=").append(ms(sJob)).append(" wakeC=").append(ms(wakeC));
        b.append(" | tps from:");
        for (int i = 0; i < AT.length; ++i)
            if (at[i] != 0 && last > at[i]) b.append(' ').append(AT[i]).append('=').append(String.format("%.0f", (pairs - 1 - AT[i]) / ((last - at[i]) / 1e9)));
        b.append(" | tps to:");
        for (int i = 0; i < AT.length; ++i)
            if (at[i] != 0) b.append(' ').append(AT[i]).append('=').append(String.format("%.0f", AT[i] / ((at[i] - first) / 1e9)));
        System.out.println(b);
        // the whole run from the JVM's start (headless.sh's ORACLE QUEUE line has the wait before it)
        long now = System.nanoTime();
        double sinceJvm = (System.currentTimeMillis() - java.lang.management.ManagementFactory.getRuntimeMXBean().getStartTime()) / 1e3;
        double toFirst = sinceJvm - (now - first) / 1e9;
        System.out.println(String.format("ORACLE PROFILE RUN jvm-to-first-pair=%.2fs loop=%.2fs last-pair-to-end=%.2fs total=%.2fs", toFirst, wall, (now - last) / 1e9, sinceJvm));
        System.out.println(phases(now));
    }

    static String phases(long now)
    {
        long wallNow = System.currentTimeMillis();
        StringBuilder b = new StringBuilder("ORACLE PROFILE PHASES");
        long t0;
        if (tJob != 0)
        {
            t0 = tJob;
            b.append(String.format(" start=%d pool", wallNow - (now - tJob) / 1000000L));
        }
        else
        {
            long jvm = java.lang.management.ManagementFactory.getRuntimeMXBean().getStartTime();
            t0 = now - (wallNow - jvm) * 1000000L;
            b.append(String.format(" start=%d jvm=%s client=%s", jvm, sec(t0, tMain), sec(tMain, tStarted)));
            t0 = tStarted;
        }
        long[] at = {tLaunch, tSave, tServerReady, tServerUp, first, last, now};
        String[] names = {"save", "spawn", "poll", "join", "loop", "end"};
        if (tJob != 0) b.append(tLaunch < tJob ? "=0.000" : "=" + sec(tJob, tLaunch));
        for (int i = 0; i < names.length; ++i)
        {
            // a phase a prelaunch ran before the job arrived is its own; the next one starts at the arrival
            long a = at[i], e = at[i + 1];
            if (tJob != 0 && a != 0 && a < tJob && e != 0 && e >= tJob) a = tJob;
            b.append(' ').append(names[i]).append('=').append(sec(a, e));
            if (tJob != 0 && e != 0 && e < tJob) b.append("(pre)");
        }
        return b.toString();
    }

    static String sec(long a, long b)
    {
        return a == 0 || b == 0 ? "?" : String.format("%.3f", (b - a) / 1e9);
    }
}
