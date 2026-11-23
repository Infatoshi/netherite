package netherite.oracle;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.lang.instrument.ClassFileTransformer;
import java.lang.instrument.Instrumentation;
import java.security.ProtectionDomain;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.BitSet;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import jdk.internal.org.objectweb.asm.ClassReader;
import jdk.internal.org.objectweb.asm.ClassVisitor;
import jdk.internal.org.objectweb.asm.ClassWriter;
import jdk.internal.org.objectweb.asm.MethodVisitor;
import jdk.internal.org.objectweb.asm.Opcodes;
import jdk.internal.org.objectweb.asm.Type;

/**
 * A coverage pool member's class initializers (Coverage, PoolAgent installs
 * this in a member that carries JaCoCo's agent; nothing else loads it).
 *
 * A fresh JVM runs a class's initializer when a run first uses the class; a
 * pool member ran every one in Pool.init. So a job's probes miss the
 * initializers (and what they call) that a fresh JVM runs during the job.
 * JLS 12.4.1: a class is initialized at a new, a getstatic or putstatic of
 * a non-constant field, an invokestatic, reflection, or the initialization
 * of a subclass. This transformer, after JaCoCo's (JaCoCo keeps its class
 * ids from the original bytes, as in a fresh JVM), makes every such site in
 * the vanilla and harness classes mark its target (touch: a member
 * reference, resolved to its declaring class when a job ends), and every
 * initializer mark its entry and exit. While Pool.init forces the
 * initializers, each one's own probes (the JaCoCo data between its entry
 * and exit, less the nested initializers') and the references it touched
 * are kept. When a job ends, the classes a fresh JVM would have initialized
 * are those the job touched or ran code of, their superclasses, and what
 * their initializers touched, to closure; their initializers' own probes
 * are the job's initializer data (Coverage.dump).
 */
public final class CovInit
{
    private CovInit() {}

    /** Every marked reference by id: kind (N new, F field, M static method, C an initializer), owner, name, descriptor. */
    static final List<String[]> refs = new ArrayList<String[]>();
    static final Map<String, Integer> refIds = new HashMap<String, Integer>();
    static final boolean[] touched = new boolean[1 << 17];

    static int id(String kind, String owner, String name, String desc)
    {
        String k = kind + " " + owner + " " + name + " " + desc;
        synchronized (refs)
        {
            Integer i = refIds.get(k);
            if (i != null) return i;
            if (refs.size() >= touched.length) throw new IllegalStateException("more than " + touched.length + " references");
            refIds.put(k, refs.size());
            refs.add(new String[] {kind, owner, name, desc});
            return refs.size() - 1;
        }
    }

    public static void touch(int i)
    {
        touched[i] = true;
        if (recording) recordTouch(i);
    }

    public static void enter(int i) { if (recording) boundary(i, true); }

    public static void exit(int i) { if (recording) boundary(i, false); }

    /** The transformer is on (PoolAgent.premain, JaCoCo's agent present). */
    static boolean installed;

    static void install(Instrumentation inst)
    {
        installed = true;
        inst.addTransformer(new ClassFileTransformer()
        {
            public byte[] transform(ClassLoader l, String name, Class<?> redefined, ProtectionDomain d, byte[] b)
            {
                if (name == null || redefined != null || !(name.startsWith("net/minecraft/") || name.startsWith("netherite/"))
                    || name.startsWith("netherite/oracle/CovInit") || name.startsWith("netherite/oracle/PoolAgent")) return null;
                try { return instrument(name, b); }
                catch (Throwable t)
                {
                    System.out.println("ORACLE COVERAGE agent: " + name + " not instrumented: " + t);
                    return null;
                }
            }
        });
    }

    static boolean ours(String owner) { return owner.startsWith("net/minecraft/") || owner.startsWith("netherite/"); }

    static byte[] instrument(final String cls, byte[] b)
    {
        ClassReader r = new ClassReader(b);
        // the inserted calls leave the stack as they found it and add no branch: the frames stand
        ClassWriter w = new ClassWriter(r, 0);
        r.accept(new ClassVisitor(Opcodes.ASM5, w)
        {
            public MethodVisitor visitMethod(int access, String mname, String desc, String sig, String[] ex)
            {
                MethodVisitor mv = super.visitMethod(access, mname, desc, sig, ex);
                if ((access & (Opcodes.ACC_ABSTRACT | Opcodes.ACC_NATIVE)) != 0 || mname.startsWith("$jacoco")) return mv;
                final boolean clinit = "<clinit>".equals(mname);
                final int self = clinit ? id("C", cls, "", "") : -1;
                return new MethodVisitor(Opcodes.ASM5, mv)
                {
                    void call(String m, int i)
                    {
                        super.visitLdcInsn(Integer.valueOf(i));
                        super.visitMethodInsn(Opcodes.INVOKESTATIC, "netherite/oracle/CovInit", m, "(I)V", false);
                    }

                    public void visitCode()
                    {
                        super.visitCode();
                        if (clinit) call("enter", self);
                    }

                    public void visitInsn(int op)
                    {
                        if (clinit && op == Opcodes.RETURN) call("exit", self);
                        super.visitInsn(op);
                    }

                    public void visitTypeInsn(int op, String type)
                    {
                        super.visitTypeInsn(op, type);
                        // after the new: a frame names an uninitialized object by the label of its new
                        if (op == Opcodes.NEW && ours(type) && !type.equals(cls)) call("touch", id("N", type, "", ""));
                    }

                    public void visitFieldInsn(int op, String owner, String name, String dsc)
                    {
                        if ((op == Opcodes.GETSTATIC || op == Opcodes.PUTSTATIC) && ours(owner) && !owner.equals(cls)) call("touch", id("F", owner, name, dsc));
                        super.visitFieldInsn(op, owner, name, dsc);
                    }

                    public void visitMethodInsn(int op, String owner, String name, String dsc, boolean itf)
                    {
                        if (op == Opcodes.INVOKESTATIC && ours(owner) && !owner.equals(cls)) call("touch", id("M", owner, name, dsc));
                        super.visitMethodInsn(op, owner, name, dsc, itf);
                    }

                    public void visitMaxs(int stack, int locals) { super.visitMaxs(stack + 1, locals); }
                };
            }
        }, 0);
        return w.toByteArray();
    }

    // ------------------------------------------------------------ Pool.init: the forced initializers

    static volatile boolean recording;
    static Thread initThread;
    static final ArrayDeque<String> stack = new ArrayDeque<String>();
    /** Each forced initializer's own probes (class id: name, probes) and the references it touched. */
    static final Map<String, Map<Long, Probes>> own = new HashMap<String, Map<Long, Probes>>();
    static final Map<String, BitSet> touches = new HashMap<String, BitSet>();
    static int initializers;

    static final class Probes
    {
        final String name;
        final boolean[] p;
        Probes(String name, boolean[] p) { this.name = name; this.p = p; }
    }

    /** Pool.init, before it forces the initializers: from here each initializer's probes are its own. */
    static void begin()
    {
        initThread = Thread.currentThread();
        Coverage.data(true);
        recording = true;
    }

    /** Pool.init, after: the probes since the last initializer ended are nobody's. */
    static void end()
    {
        recording = false;
        Coverage.data(true);
        stack.clear();
        System.out.println("ORACLE COVERAGE pool: " + initializers + " initializers kept, " + refs.size() + " references marked");
    }

    static void boundary(int i, boolean enter)
    {
        if (Thread.currentThread() != initThread) return;
        // the probes since the last boundary are the innermost running initializer's
        byte[] d = Coverage.data(true);
        String top = stack.peek();
        if (top != null) merge(read(d, null), ownOf(top));
        String c = refs.get(i)[1];
        if (enter) { stack.push(c); ++initializers; }
        else if (c.equals(stack.peek())) stack.pop();
    }

    static Map<Long, Probes> ownOf(String c)
    {
        Map<Long, Probes> m = own.get(c);
        if (m == null) own.put(c, m = new HashMap<Long, Probes>());
        return m;
    }

    static void recordTouch(int i)
    {
        if (Thread.currentThread() != initThread) return;
        String top = stack.peek();
        if (top == null) return;
        BitSet s = touches.get(top);
        if (s == null) touches.put(top, s = new BitSet());
        s.set(i);
    }

    /** Between jobs (Pool.clean, the end of Pool.init): no reference touched yet, the Gson caches as at the pristine point. */
    static void clear()
    {
        java.util.Arrays.fill(touched, false);
        for (Object[] g : gsons)
        {
            @SuppressWarnings("unchecked") Map<Object, Object> m = (Map<Object, Object>)g[0];
            synchronized (m) { m.clear(); m.putAll((Map<?, ?>)g[1]); }
        }
    }

    /**
     * Each static Gson's type adapter cache (Gson.typeTokenCache) and its
     * pristine contents. Rewind leaves a Gson opaque, so an adapter an
     * earlier job made would stay cached, and a later job would not run the
     * factory that made it (EnumTypeAdapterFactory.create, for the chat
     * component serializer) where a fresh JVM does.
     */
    static final List<Object[]> gsons = new ArrayList<Object[]>();

    /** Pool.init, at the pristine point: the static Gsons of these classes. */
    static void keepGsons(List<Class<?>> classes)
    {
        for (Class<?> c : classes)
        {
            java.lang.reflect.Field[] fs;
            try { fs = c.getDeclaredFields(); }
            catch (Throwable e) { continue; }
            for (java.lang.reflect.Field f : fs)
            {
                if (!java.lang.reflect.Modifier.isStatic(f.getModifiers()) || f.getType() != com.google.gson.Gson.class) continue;
                try
                {
                    f.setAccessible(true);
                    Object g = f.get(null);
                    if (g == null) continue;
                    java.lang.reflect.Field t = com.google.gson.Gson.class.getDeclaredField("typeTokenCache");
                    t.setAccessible(true);
                    Map<?, ?> m = (Map<?, ?>)t.get(g);
                    synchronized (m) { gsons.add(new Object[] {m, new HashMap<Object, Object>(m)}); }
                }
                catch (Exception e) { throw new IllegalStateException("coverage: the Gson of " + f, e); }
            }
        }
    }

    // ------------------------------------------------------------ a job's end

    static final Map<Integer, String> resolved = new HashMap<Integer, String>();

    /**
     * The initializers a fresh JVM would have run by now, as one JaCoCo dump
     * (header of the given data): what the job touched or ran code of, their
     * superclasses, and what those initializers touched, to closure.
     */
    static byte[] forJob(byte[] run) throws IOException
    {
        ArrayDeque<String> q = new ArrayDeque<String>();
        Map<Long, Probes> ran = read(run, null);
        for (Probes p : ran.values()) for (boolean h : p.p) if (h) { q.add(p.name); break; }
        for (int i = 0; i < touched.length && i < refs.size(); ++i) if (touched[i]) { String c = resolve(i); if (c != null) q.add(c); }
        Set<String> inited = new HashSet<String>();
        Map<Long, Probes> out = new HashMap<Long, Probes>();
        while (!q.isEmpty())
        {
            String c = q.poll();
            if (!inited.add(c)) continue;
            String s = superName(c);
            if (s != null) q.add(s);
            Map<Long, Probes> o = own.get(c);
            if (o != null) merge(o, out);
            BitSet t = touches.get(c);
            if (t != null) for (int i = t.nextSetBit(0); i >= 0; i = t.nextSetBit(i + 1)) { String r = resolve(i); if (r != null) q.add(r); }
        }
        return write(run, out);
    }

    static String superName(String c)
    {
        try
        {
            Class<?> s = Class.forName(c.replace('/', '.'), false, CovInit.class.getClassLoader()).getSuperclass();
            return s == null ? null : s.getName().replace('.', '/');
        }
        catch (Throwable e) { return null; }
    }

    /** The class a reference initializes: a new's type, a field's or static method's declaring class (JVMS 5.4.3.2, 5.4.3.3). */
    static String resolve(int i)
    {
        String c = resolved.get(i);
        if (c != null || resolved.containsKey(i)) return c;
        String[] r = refs.get(i);
        try
        {
            Class<?> k = Class.forName(r[1].replace('/', '.'), false, CovInit.class.getClassLoader());
            Class<?> d = "N".equals(r[0]) || "C".equals(r[0]) ? k : "F".equals(r[0]) ? fieldOwner(k, r[2]) : methodOwner(k, r[2], r[3]);
            c = d == null ? null : d.getName().replace('.', '/');
        }
        catch (Throwable e) { c = null; }
        resolved.put(i, c);
        return c;
    }

    static Class<?> fieldOwner(Class<?> k, String name)
    {
        if (k == null) return null;
        for (java.lang.reflect.Field f : k.getDeclaredFields()) if (f.getName().equals(name)) return k;
        for (Class<?> x : k.getInterfaces()) { Class<?> d = fieldOwner(x, name); if (d != null) return d; }
        return fieldOwner(k.getSuperclass(), name);
    }

    static Class<?> methodOwner(Class<?> k, String name, String desc)
    {
        for (; k != null; k = k.getSuperclass())
            for (java.lang.reflect.Method m : k.getDeclaredMethods())
                if (m.getName().equals(name) && Type.getMethodDescriptor(m).equals(desc)) return k;
        return null;
    }

    // ------------------------------------------------------------ JaCoCo's execution data format (0x1007)

    /** The class entries of a dump (or of several one after another), OR-merged into into (a new map if null). */
    static Map<Long, Probes> read(byte[] b, Map<Long, Probes> into)
    {
        if (into == null) into = new HashMap<Long, Probes>();
        try
        {
            DataInputStream in = new DataInputStream(new ByteArrayInputStream(b));
            for (int t; (t = in.read()) >= 0;)
            {
                if (t == 0x01) { in.readChar(); in.readChar(); }
                else if (t == 0x10) { in.readUTF(); in.readLong(); in.readLong(); }
                else if (t == 0x11)
                {
                    long id = in.readLong();
                    String name = in.readUTF();
                    int n = varInt(in);
                    boolean[] p = new boolean[n];
                    int buf = 0;
                    for (int i = 0; i < n; ++i)
                    {
                        if (i % 8 == 0) buf = in.readByte();
                        p[i] = (buf & 1) != 0;
                        buf >>>= 1;
                    }
                    Map<Long, Probes> one = new HashMap<Long, Probes>();
                    one.put(id, new Probes(name, p));
                    merge(one, into);
                }
                else throw new IOException("unknown block " + t);
            }
        }
        catch (IOException e) { throw new IllegalStateException("coverage: execution data", e); }
        return into;
    }

    static int varInt(DataInputStream in) throws IOException
    {
        int v = in.readByte() & 0xFF;
        return (v & 0x80) == 0 ? v : (v & 0x7F) | varInt(in) << 7;
    }

    static void merge(Map<Long, Probes> from, Map<Long, Probes> into)
    {
        for (Map.Entry<Long, Probes> e : from.entrySet())
        {
            Probes a = into.get(e.getKey()), b = e.getValue();
            if (a == null) { into.put(e.getKey(), new Probes(b.name, b.p.clone())); continue; }
            for (int i = 0; i < a.p.length && i < b.p.length; ++i) a.p[i] |= b.p[i];
        }
    }

    /** One dump of these entries: the header and session of like (the job's own data), then each class with a hit. */
    static byte[] write(byte[] like, Map<Long, Probes> m) throws IOException
    {
        ByteArrayOutputStream bo = new ByteArrayOutputStream();
        DataOutputStream o = new DataOutputStream(bo);
        DataInputStream in = new DataInputStream(new ByteArrayInputStream(like));
        if (in.read() != 0x01) throw new IOException("no header");
        char magic = in.readChar(), version = in.readChar();
        o.writeByte(0x01); o.writeChar(magic); o.writeChar(version);
        if (in.read() == 0x10)
        {
            o.writeByte(0x10); o.writeUTF(in.readUTF() + "-init"); o.writeLong(in.readLong()); o.writeLong(in.readLong());
        }
        List<Long> ids = new ArrayList<Long>(m.keySet());
        java.util.Collections.sort(ids);
        for (Long id : ids)
        {
            Probes p = m.get(id);
            boolean hit = false;
            for (boolean h : p.p) hit |= h;
            if (!hit) continue;
            o.writeByte(0x11); o.writeLong(id); o.writeUTF(p.name);
            writeVarInt(o, p.p.length);
            int buf = 0, n = 0;
            for (boolean h : p.p)
            {
                if (h) buf |= 1 << n;
                if (++n == 8) { o.writeByte(buf); buf = 0; n = 0; }
            }
            if (n > 0) o.writeByte(buf);
        }
        o.flush();
        return bo.toByteArray();
    }

    static void writeVarInt(DataOutputStream o, int v) throws IOException
    {
        if ((v & 0xFFFFFF80) == 0) o.writeByte(v);
        else { o.writeByte(0x80 | (v & 0x7F)); writeVarInt(o, v >>> 7); }
    }
}
