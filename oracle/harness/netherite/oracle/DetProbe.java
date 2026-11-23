package netherite.oracle;

import com.google.gson.JsonObject;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.util.Random;
import java.util.UUID;
import java.util.zip.GZIPOutputStream;
import net.minecraft.server.integrated.IntegratedServer;

/**
 * Reference for the native port of the determinism layer (Det.java): the
 * per-role seeders and Math.random streams, newRandom, the name-seeded split
 * Randoms, the per-role entity IDs and UUIDs, reset(seed) and the three state
 * digests Rows.java writes (d.sseed, d.smath, d.sstat and their client twins).
 *
 * The probe drives the live Det state through Det's own entry points, one step
 * at a time, with Thread.currentThread() bound in Det's static thread fields to
 * the step's role, so role() selects that role inside the very code under test.
 * Every step draws its role, its operation and its arguments from one
 * java.util.Random(opseed), but the chosen values are written out, so the native
 * replay reads what to do instead of reproducing the chooser.
 *
 * Order inside one run:
 *   1. the five role() bindings, read without drawing from anything
 *   2. Det.reset(1), Det.reset(2), Det.reset(42): the state after each
 *   3. Det.reset(world seed): the state every step starts from
 *   4. StrictMath.log and StrictMath.sqrt on 4,106 inputs (nextGaussian reads log)
 *   5. the steps
 *   6. the state after the last step
 *
 * reset() is called for real: it is the function under test and it is not safe
 * to call twice on a live world (it rewinds every role's seeder, Math stream,
 * entity-ID counter and split Random). The probe is the last thing its JVM
 * does, the integrated server is parked while run() executes (Oracle handles
 * the command between frames, the server waits for its permit), and nothing
 * after step 6 reads the streams, so the rewound state is never observed. The
 * price is that a second run command in the same JVM would start from the
 * post-step state, which the native replay would then have to be given too.
 *
 * The live split list is the vanilla static Randoms Det.splitRandom() has
 * already replaced plus the ones the steps create, so the reference covers the
 * real Item.itemRand and friends, not only probe names.
 *
 * Output DIR/manifest.json plus DIR/steps.txt.gz, start.txt, end.txt,
 * reset-<seed>.txt, math.txt and dispatch.txt.
 */
final class DetProbe
{
    static final int OP_NEW_RANDOM = 0, OP_MATH = 1, OP_UUID = 2, OP_NEXT_ID = 3,
                     OP_SPLIT_CREATE = 4, OP_SPLIT_DRAW = 5, OP_SPLIT_SETSEED = 6, OPS = 7;
    static final int DRAW_BITS = 0, DRAW_INT = 1, DRAW_INT_N = 2, DRAW_LONG = 3,
                     DRAW_DOUBLE = 4, DRAW_FLOAT = 5, DRAW_BOOL = 6, DRAW_GAUSSIAN = 7, DRAWS = 8;

    /** Names for the split Randoms the steps create. They look like the static
     * names determinize.sh bakes in ("./net/minecraft/item/Item.java:itemRand")
     * and are seeded the same way, by name.hashCode(), but never collide with a
     * name the game itself creates. */
    static final String[] SPLIT_NAMES = {
        "./detprobe/Splits.java:a", "./detprobe/Splits.java:b", "./detprobe/Splits.java:c",
        "./detprobe/Splits.java:d", "./detprobe/Splits.java:e", "./detprobe/Splits.java:f",
        "./detprobe/Splits.java:g", "./detprobe/Splits.java:h",
    };

    /** A bound on the splits the steps create: splitState() folds every split that
     * has been drawn from, so an unbounded list would make the digests quadratic. */
    static final int MAX_PROBE_SPLITS = 24;

    static final long[] RESET_SEEDS = {1L, 2L, 42L};
    static final int MATH_INPUTS = 4096;

    static Thread serverAtStart;

    private DetProbe() {}

    static JsonObject run(IntegratedServer server, JsonObject cmd) throws Exception
    {
        final long seed = server.worldServers[0].getSeed();
        final File dir = new File(cmd.get("out").getAsString());
        final int steps = cmd.has("steps") ? cmd.get("steps").getAsInt() : 100000;
        final long opseed = cmd.has("opseed") ? cmd.get("opseed").getAsLong() : 20260922L + seed;
        serverAtStart = Det.serverThread;
        dir.mkdirs();

        dispatch(new File(dir, "dispatch.txt"));
        for (long s : RESET_SEEDS)
        {
            Det.reset(s);
            snapshot(new File(dir, "reset-" + s + ".txt"), s);
        }
        Det.reset(seed);
        snapshot(new File(dir, "start.txt"), seed);
        mathTable(new File(dir, "math.txt"));
        drive(new File(dir, "steps.txt.gz"), steps, opseed);
        snapshot(new File(dir, "end.txt"), seed);

        JsonObject m = new JsonObject();
        m.addProperty("area", "det");
        m.addProperty("seed", seed);
        m.addProperty("steps", steps);
        m.addProperty("opseed", opseed);
        m.addProperty("roles", "CLIENT 0, SERVER 1, OTHER 2, RENDER 3");
        m.addProperty("stepsFile", "steps.txt.gz");
        m.addProperty("format", "steps.txt.gz: one line per step, whitespace separated: <i> <op> <role> "
            + "<args...> <values...> <seeder> <math> <split>. i, op, role and the args are decimal; every value and the "
            + "three trailing digests are 0x + 16 hex digits of the raw 64-bit pattern (an int or a float-bit int is "
            + "sign extended to 64 bits, a double is its raw bits). The digests are Det.seederState(role), "
            + "Det.mathState(role) and Det.splitState(role) after the step. Ops: "
            + "0 newRandom(args n; values: state, nextInt, nextInt(n), nextLong, nextGaussian bits, nextDouble bits, "
            + "nextFloat bits, nextBoolean), 1 mathRandom(1 double bits), 2 uuid(2 longs), 3 nextEntityId(1 int), "
            + "4 splitRandom(args name index, split index; 4 states), 5 split draw(args split index, kind, k; 1 value; "
            + "kind 0 next(k), 1 nextInt, 2 nextInt(k), 3 nextLong, 4 nextDouble bits, 5 nextFloat bits, 6 nextBoolean, "
            + "7 nextGaussian bits), 6 split setSeed(args split index, seed; value: state after). "
            + "start.txt, end.txt and reset-<seed>.txt: \"resetSeed <n>\" (the seed Det.reset was last called "
            + "with: the world seed for start.txt and end.txt, 1, 2 or 42 for the reset files), \"worldSeed <n>\", "
            + "\"nextId <4 ints>\", "
            + "\"digest <role> <seeder> <math> <split>\" per role, then \"split <name> <4 states> <4 used flags>\" "
            + "per registered split in list order. math.txt: \"<x> <log> <sqrt>\" per input, all 0x hex, the "
            + "StrictMath values. dispatch.txt: \"<role> <binding>\" per Det.role() binding, in the order "
            + "client, client rendering, server, server rendering, other.");
        m.addProperty("resetFiles", "reset-1.txt reset-2.txt reset-42.txt");
        m.addProperty("manifestNote", "reset(seed) was called on the live state for 1, 2, 42, then for the world seed; "
            + "the steps start from that last state. The JVM's streams end at the state in end.txt.");

        PrintWriter out = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        out.println(m.toString());
        out.close();
        return m;
    }

    /** Det.role() under each thread binding, read without drawing from anything. */
    static void dispatch(File dir) throws Exception
    {
        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(dir), "UTF-8"));
        w.println(roleOf("client", true, false, false));
        w.println(roleOf("client-render", true, false, true));
        w.println(roleOf("server", false, true, false));
        w.println(roleOf("server-render", false, true, true));
        w.println(roleOf("other", false, false, false));
        w.close();
    }

    static String roleOf(String what, boolean client, boolean server, boolean render)
    {
        Thread t = Thread.currentThread();
        Thread oc = Det.clientThread, os = Det.serverThread;
        boolean or = Det.inRender;
        int r;
        try
        {
            Det.clientThread = client ? t : null;
            Det.serverThread = server ? t : null;
            Det.inRender = render;
            r = Det.role();
        }
        finally
        {
            Det.clientThread = oc;
            Det.serverThread = os;
            Det.inRender = or;
        }
        return r + " " + what;
    }

    /** Every value the native side has to load before the steps run. */
    static void snapshot(File f, long resetSeed) throws Exception
    {
        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(f), "UTF-8"));
        w.println("resetSeed " + resetSeed);
        w.println("worldSeed " + Det.worldSeed);
        StringBuilder b = new StringBuilder("nextId");
        for (int r = 0; r < Det.ROLES; ++r) b.append(' ').append(Det.nextId[r]);
        w.println(b);
        for (int r = 0; r < Det.ROLES; ++r)
            w.println("digest " + r + " " + hex(Det.seederState(r)) + " " + hex(Det.mathState(r)) + " " + hex(Det.splitState(r)));
        synchronized (Det.class)
        {
            for (Det.SplitRandom s : Det.splits)
            {
                b.setLength(0);
                b.append("split ").append(s.name);
                for (int r = 0; r < Det.ROLES; ++r) b.append(' ').append(hex(Det.state(s.d[r])));
                for (int r = 0; r < Det.ROLES; ++r) b.append(' ').append(s.used[r] ? 1 : 0);
                w.println(b);
            }
        }
        w.close();
    }

    /** Inputs with StrictMath.log and StrictMath.sqrt, bit for bit; nextGaussian is
     * sqrt(-2*log(s)/s) and C's log is not fdlibm's, so the native port carries its
     * own and is checked against these. */
    static void mathTable(File f) throws Exception
    {
        Random r = new Random(0x5eedL);
        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(f), "UTF-8"));
        double[] edge = {0.0, -0.0, 1.0, 1.0 - Math.ulp(1.0) / 2, -1.0, Double.POSITIVE_INFINITY,
                         Double.NEGATIVE_INFINITY, Double.MIN_VALUE, Double.MAX_VALUE, Double.MIN_NORMAL};
        for (int i = 0; i < MATH_INPUTS + edge.length; ++i)
        {
            double x;
            if (i < MATH_INPUTS)
            {
                int mode = i % 8;
                if (mode == 0) x = r.nextDouble();
                else if (mode == 1) x = r.nextDouble() * 2 + 0.5;
                else if (mode == 2)
                {
                    x = Double.longBitsToDouble(r.nextLong() & 0x7fffffffffffffffL);
                    if (x == 0 || Double.isNaN(x)) x = 1.0;
                }
                else if (mode == 3) x = 1.0 + (r.nextDouble() - 0.5) * 1e-15;
                else if (mode == 4) x = StrictMath.scalb(1.0, r.nextInt(120) - 60);
                else if (mode == 5) x = Double.longBitsToDouble(((long)(r.nextInt(2046) + 1) << 52) | (r.nextLong() & 0xfffffffffffffL));
                else if (mode == 6) x = Math.pow(2.0, r.nextInt(60) - 30) * (1 + r.nextDouble());
                else x = r.nextDouble() * r.nextDouble();
            }
            else x = edge[i - MATH_INPUTS];
            w.println(hex(Double.doubleToRawLongBits(x)) + " " + hex(Double.doubleToRawLongBits(StrictMath.log(x)))
                + " " + hex(Double.doubleToRawLongBits(StrictMath.sqrt(x))));
        }
        w.close();
    }

    static void drive(File f, int steps, long opseed) throws Exception
    {
        PrintWriter w = new PrintWriter(new BufferedWriter(new OutputStreamWriter(
            new GZIPOutputStream(new FileOutputStream(f), 1 << 16), "UTF-8"), 1 << 20));
        Random ops = new Random(opseed);
        int created = 0;
        StringBuilder line = new StringBuilder(256);
        long[] vals = new long[8];
        long[] args = new long[4];
        for (int i = 0; i < steps; ++i)
        {
            int role = ops.nextInt(Det.ROLES);
            int op = ops.nextInt(OPS);
            if (op == OP_SPLIT_CREATE && created >= MAX_PROBE_SPLITS) op = OP_SPLIT_DRAW;
            if (op == OP_SPLIT_DRAW && splitCount() == 0) op = OP_SPLIT_CREATE;
            int nargs = 0, nvals = 0;
            switch (op)
            {
                case OP_NEW_RANDOM:
                {
                    args[nargs++] = 1 + ops.nextInt(1000);
                    bind(role);
                    Random nr = Det.newRandom();
                    vals[nvals++] = Det.state(nr);
                    vals[nvals++] = nr.nextInt();
                    vals[nvals++] = nr.nextInt((int)args[0]);
                    vals[nvals++] = nr.nextLong();
                    vals[nvals++] = Double.doubleToRawLongBits(nr.nextGaussian());
                    vals[nvals++] = Double.doubleToRawLongBits(nr.nextDouble());
                    vals[nvals++] = Float.floatToRawIntBits(nr.nextFloat());
                    vals[nvals++] = nr.nextBoolean() ? 1L : 0L;
                    unbind();
                    break;
                }
                case OP_MATH:
                {
                    bind(role);
                    double d = Det.mathRandom();
                    unbind();
                    vals[nvals++] = Double.doubleToRawLongBits(d);
                    break;
                }
                case OP_UUID:
                {
                    bind(role);
                    UUID u = Det.uuid();
                    unbind();
                    vals[nvals++] = u.getMostSignificantBits();
                    vals[nvals++] = u.getLeastSignificantBits();
                    break;
                }
                case OP_NEXT_ID:
                {
                    bind(role);
                    int id = Det.nextEntityId();
                    unbind();
                    vals[nvals++] = id;
                    break;
                }
                case OP_SPLIT_CREATE:
                {
                    args[nargs++] = ops.nextInt(SPLIT_NAMES.length);
                    bind(role);
                    Det.SplitRandom sp = (Det.SplitRandom)Det.splitRandom(SPLIT_NAMES[(int)args[0]]);
                    args[nargs++] = splitIndex(sp);
                    for (int r = 0; r < Det.ROLES; ++r) vals[nvals++] = Det.state(sp.d[r]);
                    unbind();
                    ++created;
                    break;
                }
                case OP_SPLIT_DRAW:
                {
                    int size = splitCount();
                    args[nargs++] = ops.nextInt(size);
                    int kind = ops.nextInt(DRAWS);
                    args[nargs++] = kind;
                    args[nargs++] = kind == DRAW_BITS ? 1 + ops.nextInt(32) : kind == DRAW_INT_N ? 1 + ops.nextInt(1000) : 0;
                    bind(role);
                    Det.SplitRandom sp = splitAt((int)args[0]);
                    switch (kind)
                    {
                        case DRAW_BITS: vals[nvals++] = sp.next((int)args[2]); break;
                        case DRAW_INT: vals[nvals++] = sp.nextInt(); break;
                        case DRAW_INT_N: vals[nvals++] = sp.nextInt((int)args[2]); break;
                        case DRAW_LONG: vals[nvals++] = sp.nextLong(); break;
                        case DRAW_DOUBLE: vals[nvals++] = Double.doubleToRawLongBits(sp.nextDouble()); break;
                        case DRAW_FLOAT: vals[nvals++] = Float.floatToRawIntBits(sp.nextFloat()); break;
                        case DRAW_BOOL: vals[nvals++] = sp.nextBoolean() ? 1L : 0L; break;
                        default: vals[nvals++] = Double.doubleToRawLongBits(sp.nextGaussian()); break;
                    }
                    unbind();
                    break;
                }
                default:
                {
                    args[nargs++] = ops.nextInt(splitCount());
                    args[nargs++] = ops.nextLong() >>> 1; // non-negative, so the line stays a plain decimal
                    bind(role);
                    Det.SplitRandom sp = splitAt((int)args[0]);
                    sp.setSeed(args[1]);
                    vals[nvals++] = Det.state(sp.d[role]);
                    unbind();
                    break;
                }
            }
            line.setLength(0);
            line.append(i).append(' ').append(op).append(' ').append(role);
            for (int a = 0; a < nargs; ++a) line.append(' ').append(args[a]);
            for (int v = 0; v < nvals; ++v) line.append(' ').append(hex(vals[v]));
            line.append(' ').append(hex(Det.seederState(role)));
            line.append(' ').append(hex(Det.mathState(role)));
            line.append(' ').append(hex(Det.splitState(role)));
            w.println(line);
        }
        w.close();
    }

    static int splitCount()
    {
        synchronized (Det.class) { return Det.splits.size(); }
    }

    static int splitIndex(Det.SplitRandom sp)
    {
        synchronized (Det.class) { return Det.splits.indexOf(sp); }
    }

    static Det.SplitRandom splitAt(int idx)
    {
        synchronized (Det.class) { return Det.splits.get(idx); }
    }

    /** role() reads the thread, so the probe makes the current thread the role's
     * thread for the duration of one Det call and restores the binding after. */
    static void bind(int role)
    {
        Thread t = Thread.currentThread();
        Det.clientThread = (role == Det.CLIENT || role == Det.RENDER) ? t : null;
        Det.serverThread = role == Det.SERVER ? t : null;
        Det.inRender = role == Det.RENDER;
    }

    static void unbind()
    {
        Det.clientThread = Thread.currentThread();
        Det.serverThread = serverAtStart;
        Det.inRender = false;
    }

    static String hex(long v)
    {
        return String.format("0x%016x", v);
    }
}