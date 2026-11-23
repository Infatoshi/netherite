package netherite.oracle;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;

/**
 * Matched tracepoints. The same call site here and in csrc/engine/trace.h writes
 * the same line, so a diff of the two files names the first step where the
 * oracle and the port part.
 *
 *   Trace.t("cave.step", 3, 1.5, 0.25f)
 *     -> cave.step 3 d:3ff8000000000000 f:3e800000
 *
 * Doubles are raw bits (16 hex digits), floats raw bits (8), ints/longs/shorts/
 * bytes decimal, booleans 0/1, anything else String.valueOf. Off - t() returns
 * at once - unless Main got --trace PATH. Each line is flushed as it is
 * written, so a run that ends in Runtime.halt still leaves a whole file; the
 * writer is also closed at shutdown. pause()/resume() bracket work that would
 * trace the same steps twice; restart() starts the file over, which a chunk
 * dump uses to drop the oracle's own spawn-area generation.
 */
public final class Trace
{
    private static final Object LOCK = new Object();
    private static volatile PrintWriter out;
    private static volatile boolean paused;
    private static volatile String path;

    private Trace() {}

    /** Turn tracing on, appending to path. */
    static void open(String path) throws IOException
    {
        File f = new File(path).getAbsoluteFile();
        File dir = f.getParentFile();
        if (dir != null) dir.mkdirs();
        // autoflush: the oracle ends a run with Runtime.halt (Oracle.finish), which
        // skips shutdown hooks, so a line must not be sitting in a buffer at exit
        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(f), "UTF-8"), true);
        synchronized (LOCK)
        {
            Trace.path = f.getPath();
            out = w;
            paused = false;
        }
        Runtime.getRuntime().addShutdownHook(new Thread(new Runnable()
        {
            public void run()
            {
                close();
            }
        }, "Oracle Trace Close"));
    }

    /** Discard everything written so far and start the file over at the same
     * path. The oracle traces its own spawn-area generation before a script
     * command runs; a chunk dump calls this at its start so its trace holds the
     * dump and nothing else, equal to a native run over the same dump. */
    static void restart()
    {
        String p = path;
        if (p == null) return;
        synchronized (LOCK)
        {
            if (out != null)
            {
                out.flush();
                out.close();
            }
            try
            {
                out = new PrintWriter(new OutputStreamWriter(new FileOutputStream(p), "UTF-8"), true);
            }
            catch (IOException e)
            {
                throw new RuntimeException("oracle: cannot restart trace " + p, e);
            }
            paused = false;
        }
    }

    /** Append one line: the label, then each value after one space. */
    public static void t(String label, Object... values)
    {
        PrintWriter w = out;
        if (w == null || paused) return;
        StringBuilder b = new StringBuilder(label);
        for (Object v : values)
        {
            b.append(' ');
            if (v instanceof Double)
            {
                b.append("d:");
                hex(b, Double.doubleToRawLongBits(((Double)v).doubleValue()), 16);
            }
            else if (v instanceof Float)
            {
                b.append("f:");
                hex(b, Float.floatToRawIntBits(((Float)v).floatValue()), 8);
            }
            else if (v instanceof Integer || v instanceof Long || v instanceof Short || v instanceof Byte)
            {
                b.append(v.toString());
            }
            else if (v instanceof Boolean)
            {
                b.append(((Boolean)v).booleanValue() ? '1' : '0');
            }
            else
            {
                b.append(String.valueOf(v));
            }
        }
        String line = b.toString();
        synchronized (LOCK)
        {
            if (out != null) out.println(line);
        }
    }

    /** Drop tracepoints until resume(); used around a regeneration self-check. */
    static void pause()
    {
        synchronized (LOCK)
        {
            paused = true;
        }
    }

    static void resume()
    {
        synchronized (LOCK)
        {
            paused = false;
        }
    }

    static void close()
    {
        synchronized (LOCK)
        {
            if (out != null)
            {
                out.flush();
                out.close();
                out = null;
            }
        }
    }

    private static void hex(StringBuilder b, long bits, int digits)
    {
        for (int i = digits - 1; i >= 0; --i) b.append("0123456789abcdef".charAt((int)((bits >>> (i * 4)) & 15L)));
    }
}