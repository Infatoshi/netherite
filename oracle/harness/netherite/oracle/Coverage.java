package netherite.oracle;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Method;

/**
 * --coverage FILE: at the end of the run, write the JaCoCo execution data of
 * this JVM to FILE (make coverage, tests/coverage.sh). The JVM must carry
 * JaCoCo's agent (make replay COVERAGE=FILE adds it, with output=none); the
 * agent's own dump runs in a shutdown hook, which Oracle.finish's
 * Runtime.halt skips, so the harness asks the agent for its data through
 * org.jacoco.agent.rt.RT, by reflection (the harness does not link JaCoCo).
 * Off by default: without the flag nothing here runs.
 *
 * A coverage pool member (tests/poolauto.sh, a member started with the
 * agent) takes only runs with the flag, and a member without the agent
 * none (Main.configurePool). A member's data would hold its start, every
 * earlier job and the class initializers Pool.init forces, so it is cut
 * per job: Pool.init keeps what the JVM start ran (startup, where a fresh
 * JVM launches its world) and what each forced initializer ran
 * (CovInit), and resets the probes after the pristine point and after
 * every job's teardown (Pool.clean). A job's file is then the start's data,
 * the initializers a fresh JVM would have run by the job's end (CovInit),
 * and the job's own probes: three JaCoCo dumps one after another, which
 * JaCoCo reads as one file (its agent appends a session the same way).
 */
final class Coverage
{
    private Coverage() {}

    static String out;

    /** The agent's IAgent and its getExecutionData(boolean), or null without the agent. */
    static Object agent;
    static Method getData;
    static boolean probed;

    /** Whether this JVM carries JaCoCo's agent. */
    static synchronized boolean present()
    {
        if (!probed)
        {
            probed = true;
            try
            {
                Class<?> rt = Class.forName("org.jacoco.agent.rt.RT");
                agent = rt.getMethod("getAgent").invoke(null);
                getData = Class.forName("org.jacoco.agent.rt.IAgent").getMethod("getExecutionData", boolean.class);
            }
            catch (Throwable e) { agent = null; }
        }
        return agent != null;
    }

    /** The execution data so far (a JaCoCo dump: header, session, the classes with hits); reset clears every probe after. */
    static byte[] data(boolean reset)
    {
        try { return (byte[])getData.invoke(agent, reset); }
        catch (Exception e) { throw new IllegalStateException("coverage: the JaCoCo agent", e); }
    }

    /** A coverage pool member: what its JVM start ran, written ahead of every job's data. */
    static byte[] startup;

    static void dump()
    {
        if (out == null) return;
        try
        {
            if (!present()) throw new IllegalStateException("no JaCoCo agent");
            ByteArrayOutputStream b = new ByteArrayOutputStream();
            byte[] run = data(false);
            if (startup != null)
            {
                b.write(startup);
                b.write(CovInit.forJob(run));
            }
            b.write(run);
            byte[] all = b.toByteArray();
            File f = new File(out), tmp = new File(out + ".part");
            FileOutputStream o = new FileOutputStream(tmp);
            try { o.write(all); }
            finally { o.close(); }
            if (!tmp.renameTo(f)) throw new java.io.IOException("rename " + tmp + " to " + f);
            System.out.println("ORACLE COVERAGE " + all.length + " bytes " + out);
            // a member: this job's file is written, nothing after the job may write it (the next job sets its own)
            if (startup != null) out = null;
        }
        catch (Throwable e)
        {
            System.out.println("ORACLE COVERAGE FAIL " + e + " (is the JaCoCo agent on the JVM?)");
        }
    }
}
