package netherite.oracle;

import com.google.gson.JsonObject;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.concurrent.LinkedBlockingQueue;

/**
 * Command sources for agent mode: a JSONL script, a tape to replay, or the
 * TCP control port (the Malmo replacement). One JSON object per line in, one
 * per line out. Commands are consumed on the client thread only.
 */
final class Control
{
    static final LinkedBlockingQueue<JsonObject> queue = new LinkedBlockingQueue<JsonObject>();
    static BufferedReader script;
    /** The text of the line next() last parsed from the script or tape. */
    static String lastLine;
    static volatile PrintWriter out;

    private Control() {}

    /** A replay's rows, read and parsed ahead on their own thread: [line, JsonObject], END at the end. */
    static LinkedBlockingQueue<Object[]> ahead;
    static final Object[] END = new Object[0];

    static void prefetch()
    {
        final BufferedReader r = script;
        ahead = new LinkedBlockingQueue<Object[]>(4096);
        final LinkedBlockingQueue<Object[]> q = ahead;
        Thread t = reader = new Thread(new Runnable()
        {
            public void run()
            {
                try
                {
                    String line;
                    while ((line = r.readLine()) != null)
                    {
                        line = line.trim();
                        if (!line.isEmpty()) q.put(new Object[] {line, Rows.parse(line)});
                    }
                    q.put(END);
                }
                catch (InterruptedException e) {} // Control.stop: the run is over
                catch (Exception e)
                {
                    try { q.put(new Object[] {e}); }
                    catch (InterruptedException ie) {}
                }
            }
        }, "Oracle Replay Reader");
        t.setDaemon(true);
        t.start();
    }

    static void start() throws IOException
    {
        if (Oracle.replayPath != null)
        {
            script = Rows.openRef(Oracle.replayPath);
            String header = script.readLine(); // tape header
            if (header == null) throw new IOException("oracle: empty replay tape");
            Oracle.onReplayHeader(Rows.parse(header));
            prefetch();
        }
        else if (Oracle.scriptPath != null)
        {
            script = Rows.openRef(Oracle.scriptPath);
        }
        else if (Oracle.port > 0)
        {
            final ServerSocket ss = new ServerSocket(Oracle.port, 1, InetAddress.getByName("127.0.0.1"));
            Thread t = new Thread(new Runnable()
            {
                public void run()
                {
                    serve(ss);
                }
            }, "Oracle Control");
            t.setDaemon(true);
            t.start();
            System.out.println("ORACLE LISTEN 127.0.0.1:" + Oracle.port);
        }
    }

    static void serve(ServerSocket ss)
    {
        while (true)
        {
            try
            {
                Socket s = ss.accept();
                s.setTcpNoDelay(true);
                out = new PrintWriter(new OutputStreamWriter(s.getOutputStream(), "UTF-8"), true);
                BufferedReader in = new BufferedReader(new InputStreamReader(s.getInputStream(), "UTF-8"));
                String line;
                while ((line = in.readLine()) != null)
                {
                    line = line.trim();
                    if (line.isEmpty()) continue;
                    try
                    {
                        queue.put(Rows.parse(line));
                    }
                    catch (RuntimeException e)
                    {
                        reply(error("bad json: " + e.getMessage()));
                    }
                }
                out = null;
                s.close();
            }
            catch (Exception e)
            {
                System.out.println("ORACLE control: " + e);
            }
        }
    }

    /** Next command, blocking. Null means the source is exhausted. */
    static JsonObject next()
    {
        try
        {
            // bot run classes push steps; they must be consumed before the
            // script's next line
            if (!queue.isEmpty()) return queue.poll();
            if (ahead != null)
            {
                Object[] a = ahead.take();
                if (a == END)
                {
                    ahead.put(END);
                    return null;
                }
                if (a.length == 1) throw (Exception)a[0];
                lastLine = (String)a[0];
                return (JsonObject)a[1];
            }
            if (script != null)
            {
                String line;
                while ((line = script.readLine()) != null)
                {
                    line = line.trim();
                    if (!line.isEmpty())
                    {
                        lastLine = line;
                        return Rows.parse(line);
                    }
                }
                return null;
            }
            return queue.take();
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
    }

    /** The replay reader thread, while a replay runs. */
    static Thread reader;

    /**
     * A pool JVM between jobs: the replay reader stops (it may be blocked on a
     * full queue after an early divergence), the script closes, and commands a
     * run class queued and nobody took are dropped.
     */
    static void stop() throws InterruptedException
    {
        Thread t = reader;
        reader = null;
        if (t != null)
        {
            t.interrupt();
            t.join(10000L);
            if (t.isAlive()) throw new IllegalStateException("the replay reader did not stop");
        }
        if (script != null)
        {
            try { script.close(); }
            catch (IOException e) {}
        }
        queue.clear();
    }

    static void reply(JsonObject o)
    {
        PrintWriter w = out;
        if (w != null) w.println(o.toString());
    }

    static JsonObject error(String msg)
    {
        JsonObject o = new JsonObject();
        o.addProperty("ok", false);
        o.addProperty("error", msg);
        return o;
    }
}
