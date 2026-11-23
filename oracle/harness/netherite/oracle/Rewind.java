package netherite.oracle;

import java.io.File;
import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import sun.misc.Unsafe;

/**
 * The warm pool's heap rewind. At the pristine point (the first world launch
 * of a pool JVM, before anything world-specific happened) every object
 * reachable from the static fields of the vanilla and harness classes is
 * recorded field by field; restore() writes every recorded field back, so the
 * whole reachable graph (block and item singletons and their Randoms, biome
 * decorators and their tree generators, registries, the client's renderers,
 * GUI counters and key bindings, the harness's own statics) is what a fresh
 * JVM has at the same point. Objects made after the pristine point drop out
 * of the graph because every reference to them is written back.
 *
 * What is walked: net.minecraft and netherite classes, java.util collections
 * and Randoms, atomics, guava collections, gson trees, arrays. Everything else
 * (threads, IO, NIO buffers, netty, LWJGL, locks, blocking queues, class
 * objects, strings and boxes) is opaque: the reference to it is restored, its
 * insides are not. Fields in KEEP are neither walked nor restored (the pool's
 * long-lived threads, the texture cache, netty's event loops).
 */
final class Rewind
{
    private Rewind() {}

    static final Unsafe U;
    static
    {
        try
        {
            Field f = Unsafe.class.getDeclaredField("theUnsafe");
            f.setAccessible(true);
            U = (Unsafe)f.get(null);
        }
        catch (Exception e) { throw new ExceptionInInitializerError(e); }
    }

    /** One class's instance (or static) field layout. */
    static final class Layout
    {
        final String name;
        final long[] refOff, primOff;
        final char[] primKind;
        final Field[] refField;
        Layout(String name, List<Field> refs, List<Field> prims, boolean statics)
        {
            this.name = name;
            refOff = new long[refs.size()];
            refField = refs.toArray(new Field[0]);
            for (int i = 0; i < refOff.length; ++i) refOff[i] = statics ? U.staticFieldOffset(refField[i]) : U.objectFieldOffset(refField[i]);
            primOff = new long[prims.size()];
            primKind = new char[prims.size()];
            for (int i = 0; i < primOff.length; ++i)
            {
                Field f = prims.get(i);
                primOff[i] = statics ? U.staticFieldOffset(f) : U.objectFieldOffset(f);
                primKind[i] = kind(f.getType());
            }
        }
    }

    static char kind(Class<?> t)
    {
        if (t == long.class) return 'J';
        if (t == int.class) return 'I';
        if (t == short.class) return 'S';
        if (t == byte.class) return 'B';
        if (t == boolean.class) return 'Z';
        if (t == char.class) return 'C';
        if (t == float.class) return 'F';
        return 'D';
    }

    static final Map<Class<?>, Layout> layouts = new HashMap<Class<?>, Layout>();
    static final Map<Class<?>, Boolean> walkable = new HashMap<Class<?>, Boolean>();

    /** Fields that are never walked or restored, "Class.field" by declaring class name. */
    static final Set<String> KEEP = new HashSet<String>();

    static void keep(String classField) { KEEP.add(classField); }

    /** Whether an object of this class has its fields walked and restored. */
    static boolean walk(Class<?> c)
    {
        Boolean b = walkable.get(c);
        if (b == null)
        {
            b = decide(c);
            walkable.put(c, b);
        }
        return b;
    }

    static boolean decide(Class<?> c)
    {
        if (c.isArray()) return true;
        String n = c.getName();
        if (n.startsWith("net.minecraft.") || n.startsWith("netherite.")) return !Thread.class.isAssignableFrom(c);
        if (n.startsWith("java.util.concurrent."))
            return n.startsWith("java.util.concurrent.atomic.") || n.startsWith("java.util.concurrent.ConcurrentHashMap")
                || n.startsWith("java.util.concurrent.CopyOnWrite") || n.startsWith("java.util.concurrent.ConcurrentLinked")
                || n.startsWith("java.util.concurrent.ConcurrentSkipList");
        if (n.startsWith("java.util.logging.") || n.startsWith("java.util.zip.") || n.startsWith("java.util.jar.")
            || n.startsWith("java.util.regex.") || n.equals("java.util.Timer") || n.startsWith("java.util.Timer$")
            || n.startsWith("java.util.stream.") || n.startsWith("java.util.function.")
            || n.equals("java.util.Locale") || n.equals("java.util.Currency") || n.startsWith("java.util.ResourceBundle")
            || n.equals("java.util.UUID"))
            return false;
        if (n.startsWith("java.util.")) return true;
        if (n.startsWith("com.google.common.collect.")) return true;
        if (n.startsWith("com.google.gson.")) return !n.startsWith("com.google.gson.internal.bind") && !n.equals("com.google.gson.Gson");
        return false;
    }

    static Layout layout(Class<?> c)
    {
        Layout l = layouts.get(c);
        if (l != null) return l;
        List<Field> refs = new ArrayList<Field>(), prims = new ArrayList<Field>();
        for (Class<?> k = c; k != null && k != Object.class; k = k.getSuperclass())
        {
            // a vanilla class over a library one (SoundSystemStarterThread over paulscode's SoundSystem,
            // NetworkManager over netty's handler): only the walked classes' own fields are restored
            if (!decide(k)) continue;
            for (Field f : k.getDeclaredFields())
            {
                if (Modifier.isStatic(f.getModifiers())) continue;
                if (KEEP.contains(k.getName() + "." + f.getName())) continue;
                if (f.getType().isPrimitive()) prims.add(f);
                else refs.add(f);
            }
        }
        l = new Layout(c.getName(), refs, prims, false);
        layouts.put(c, l);
        return l;
    }

    /** The static fields of a class worth restoring: not final primitives or strings. */
    static Layout staticLayout(Class<?> c)
    {
        List<Field> refs = new ArrayList<Field>(), prims = new ArrayList<Field>();
        for (Field f : c.getDeclaredFields())
        {
            int m = f.getModifiers();
            if (!Modifier.isStatic(m)) continue;
            if (KEEP.contains(c.getName() + "." + f.getName())) continue;
            // JaCoCo's probes (a coverage member): its runtime holds the same arrays, Pool resets them per job
            if (f.getName().startsWith("$jacoco")) continue;
            if (f.getType().isPrimitive())
            {
                if (!Modifier.isFinal(m)) prims.add(f);
            }
            else if (f.getType() != String.class || !Modifier.isFinal(m)) refs.add(f);
        }
        return new Layout(c.getName() + " (static)", refs, prims, true);
    }

    // ------------------------------------------------------------ the snapshot

    /** Every walked object, its layout (null for an array) and its saved values. */
    static Object[] objs = new Object[0];
    static Layout[] lays;
    static Object[][] refs;
    static long[][] prims;
    static Object[] arrays; // arrays: a copy of the array at the pristine point
    static int n;
    /** Static roots: the class's static base, its layout and its values. */
    static Object[] sBase;
    static Layout[] sLay;
    static Object[][] sRefs;
    static long[][] sPrims;
    static int sN;

    static IdentityHashMap<Object, Boolean> seen;
    static ArrayList<Object> stack;

    static void push(Object o)
    {
        if (o == null || seen.containsKey(o)) return;
        seen.put(o, Boolean.TRUE);
        Class<?> c = o.getClass();
        if (!walk(c)) return;
        stack.add(o);
    }

    /** Records the graph under every class's statics. */
    static void capture(List<Class<?>> roots)
    {
        long t0 = System.nanoTime();
        seen = new IdentityHashMap<Object, Boolean>(1 << 20);
        stack = new ArrayList<Object>();
        List<Object> sb = new ArrayList<Object>();
        List<Layout> sl = new ArrayList<Layout>();
        List<Object[]> sr = new ArrayList<Object[]>();
        List<long[]> sp = new ArrayList<long[]>();
        for (Class<?> c : roots)
        {
            Layout l = staticLayout(c);
            if (l.refOff.length == 0 && l.primOff.length == 0) continue;
            Object base = l.refOff.length > 0 ? U.staticFieldBase(l.refField[0]) : null;
            if (base == null)
            {
                for (Field f : c.getDeclaredFields()) if (Modifier.isStatic(f.getModifiers())) { base = U.staticFieldBase(f); break; }
            }
            Object[] r = new Object[l.refOff.length];
            for (int i = 0; i < r.length; ++i) { r[i] = U.getObject(base, l.refOff[i]); push(r[i]); }
            long[] p = savePrims(base, l);
            sb.add(base); sl.add(l); sr.add(r); sp.add(p);
        }
        sN = sb.size();
        sBase = sb.toArray();
        sLay = sl.toArray(new Layout[0]);
        sRefs = sr.toArray(new Object[0][]);
        sPrims = sp.toArray(new long[0][]);

        List<Object> os = new ArrayList<Object>();
        List<Layout> ls = new ArrayList<Layout>();
        List<Object[]> rs = new ArrayList<Object[]>();
        List<long[]> ps = new ArrayList<long[]>();
        List<Object> as = new ArrayList<Object>();
        while (!stack.isEmpty())
        {
            Object o = stack.remove(stack.size() - 1);
            Class<?> c = o.getClass();
            os.add(o);
            if (c.isArray())
            {
                ls.add(null); rs.add(null); ps.add(null);
                Object copy = copyArray(o);
                as.add(copy);
                if (o instanceof Object[]) for (Object e : (Object[])o) push(e);
                continue;
            }
            Layout l = layout(c);
            Object[] r = new Object[l.refOff.length];
            for (int i = 0; i < r.length; ++i) { r[i] = U.getObject(o, l.refOff[i]); push(r[i]); }
            ls.add(l); rs.add(r); ps.add(savePrims(o, l)); as.add(null);
        }
        n = os.size();
        objs = os.toArray();
        lays = ls.toArray(new Layout[0]);
        refs = rs.toArray(new Object[0][]);
        prims = ps.toArray(new long[0][]);
        arrays = as.toArray();
        seen = null;
        stack = null;
        System.out.println("ORACLE REWIND captured " + n + " objects under " + sN + " classes in " + (System.nanoTime() - t0) / 1000000 + " ms");
    }

    static Object copyArray(Object a)
    {
        if (a instanceof Object[]) return ((Object[])a).clone();
        if (a instanceof int[]) return ((int[])a).clone();
        if (a instanceof long[]) return ((long[])a).clone();
        if (a instanceof byte[]) return ((byte[])a).clone();
        if (a instanceof short[]) return ((short[])a).clone();
        if (a instanceof char[]) return ((char[])a).clone();
        if (a instanceof float[]) return ((float[])a).clone();
        if (a instanceof double[]) return ((double[])a).clone();
        return ((boolean[])a).clone();
    }

    static long[] savePrims(Object o, Layout l)
    {
        long[] p = new long[l.primOff.length];
        for (int i = 0; i < p.length; ++i) p[i] = getPrim(o, l.primOff[i], l.primKind[i]);
        return p;
    }

    static long getPrim(Object o, long off, char k)
    {
        switch (k)
        {
            case 'J': return U.getLong(o, off);
            case 'I': return U.getInt(o, off);
            case 'S': return U.getShort(o, off);
            case 'B': return U.getByte(o, off);
            case 'Z': return U.getBoolean(o, off) ? 1 : 0;
            case 'C': return U.getChar(o, off);
            case 'F': return Float.floatToRawIntBits(U.getFloat(o, off));
            default: return Double.doubleToRawLongBits(U.getDouble(o, off));
        }
    }

    static void putPrim(Object o, long off, char k, long v)
    {
        switch (k)
        {
            case 'J': U.putLong(o, off, v); break;
            case 'I': U.putInt(o, off, (int)v); break;
            case 'S': U.putShort(o, off, (short)v); break;
            case 'B': U.putByte(o, off, (byte)v); break;
            case 'Z': U.putBoolean(o, off, v != 0); break;
            case 'C': U.putChar(o, off, (char)v); break;
            case 'F': U.putFloat(o, off, Float.intBitsToFloat((int)v)); break;
            default: U.putDouble(o, off, Double.longBitsToDouble(v));
        }
    }

    /** Writes every recorded field and array element back; returns how many objects had a change. */
    static int restore()
    {
        long t0 = System.nanoTime();
        int changed = 0;
        for (int s = 0; s < sN; ++s) if (put(sBase[s], sLay[s], sRefs[s], sPrims[s])) ++changed;
        for (int i = 0; i < n; ++i)
        {
            Object o = objs[i];
            if (lays[i] == null)
            {
                if (putArray(o, arrays[i])) ++changed;
            }
            else if (put(o, lays[i], refs[i], prims[i])) ++changed;
        }
        U.fullFence();
        System.out.println("ORACLE REWIND restored " + changed + " of " + (n + sN) + " in " + (System.nanoTime() - t0) / 1000000 + " ms");
        return changed;
    }

    static boolean put(Object o, Layout l, Object[] r, long[] p)
    {
        boolean ch = false;
        for (int i = 0; i < r.length; ++i)
        {
            if (U.getObject(o, l.refOff[i]) != r[i]) { U.putObject(o, l.refOff[i], r[i]); ch = true; }
        }
        for (int i = 0; i < p.length; ++i)
        {
            if (getPrim(o, l.primOff[i], l.primKind[i]) != p[i]) { putPrim(o, l.primOff[i], l.primKind[i], p[i]); ch = true; }
        }
        return ch;
    }

    static boolean putArray(Object a, Object saved)
    {
        if (a instanceof Object[])
        {
            Object[] x = (Object[])a, y = (Object[])saved;
            boolean ch = false;
            for (int i = 0; i < x.length; ++i) if (x[i] != y[i]) { x[i] = y[i]; ch = true; }
            return ch;
        }
        if (a instanceof int[]) { if (java.util.Arrays.equals((int[])a, (int[])saved)) return false; System.arraycopy(saved, 0, a, 0, ((int[])a).length); return true; }
        if (a instanceof long[]) { if (java.util.Arrays.equals((long[])a, (long[])saved)) return false; System.arraycopy(saved, 0, a, 0, ((long[])a).length); return true; }
        if (a instanceof byte[]) { if (java.util.Arrays.equals((byte[])a, (byte[])saved)) return false; System.arraycopy(saved, 0, a, 0, ((byte[])a).length); return true; }
        if (a instanceof short[]) { if (java.util.Arrays.equals((short[])a, (short[])saved)) return false; System.arraycopy(saved, 0, a, 0, ((short[])a).length); return true; }
        if (a instanceof char[]) { if (java.util.Arrays.equals((char[])a, (char[])saved)) return false; System.arraycopy(saved, 0, a, 0, ((char[])a).length); return true; }
        if (a instanceof float[]) { if (java.util.Arrays.equals((float[])a, (float[])saved)) return false; System.arraycopy(saved, 0, a, 0, ((float[])a).length); return true; }
        if (a instanceof double[]) { if (java.util.Arrays.equals((double[])a, (double[])saved)) return false; System.arraycopy(saved, 0, a, 0, ((double[])a).length); return true; }
        if (java.util.Arrays.equals((boolean[])a, (boolean[])saved)) return false;
        System.arraycopy(saved, 0, a, 0, ((boolean[])a).length);
        return true;
    }

    /** Every class compiled into the oracle, by name, from the classes directory on the class path. */
    static List<String> classNames()
    {
        List<String> out = new ArrayList<String>();
        for (String e : System.getProperty("java.class.path").split(File.pathSeparator))
        {
            File d = new File(e);
            if (!d.isDirectory() || !new File(d, "netherite/oracle").isDirectory()) continue;
            walkDir(d, "", out);
        }
        return out;
    }

    static void walkDir(File d, String pkg, List<String> out)
    {
        File[] kids = d.listFiles();
        if (kids == null) return;
        java.util.Arrays.sort(kids);
        for (File k : kids)
        {
            String name = k.getName();
            if (k.isDirectory()) walkDir(k, pkg + name + ".", out);
            else if (name.endsWith(".class")) out.add(pkg + name.substring(0, name.length() - 6));
        }
    }

    /** The classes already initialized in this JVM, in name order. */
    static List<Class<?>> initialized(List<String> names)
    {
        List<Class<?>> out = new ArrayList<Class<?>>();
        ClassLoader cl = Rewind.class.getClassLoader();
        for (String nm : names)
        {
            try
            {
                Class<?> c = Class.forName(nm, false, cl);
                if (!U.shouldBeInitialized(c)) out.add(c);
            }
            catch (Throwable t) {}
        }
        return out;
    }
}
