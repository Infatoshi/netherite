package netherite.oracle;

import java.lang.instrument.ClassFileTransformer;
import java.lang.instrument.Instrumentation;
import java.security.ProtectionDomain;
import java.util.ArrayList;
import java.util.List;
import jdk.internal.org.objectweb.asm.ClassReader;
import jdk.internal.org.objectweb.asm.ClassVisitor;
import jdk.internal.org.objectweb.asm.ClassWriter;
import jdk.internal.org.objectweb.asm.MethodVisitor;
import jdk.internal.org.objectweb.asm.Opcodes;

/**
 * The pool member's java agent (-javaagent, pool members only; a fresh JVM
 * never loads it): which classes that own a Det split Random a run touches.
 *
 * A fresh JVM registers a split Random (Det.splitRandom) when its owner class
 * is initialized, and a snapshot's det.nbt lists every registered split. A
 * pool member initialized every class at start, so it cannot see whether a
 * fresh run would have initialized one by now: EnchantmentHelper's comes
 * with a dungeon chest's enchanted book in a seed start's spawn chunks, or
 * the first damage an entity takes, and not at all in a short checkpoint
 * start. JLS 12.4.1: a class is initialized at its first instance creation,
 * static method call or non-constant static field access. Every owner's
 * static fields are private (the split owners: Item, EnchantmentHelper,
 * TileEntityEnchantmentTable, NetHandlerPlayServer, NetHandlerLoginServer;
 * a nested class reaches them through a static accessor), so a constructor
 * or static method running is the owner being touched: this agent makes
 * every constructor and static method (not the initializer) of each class
 * whose initializer calls Det.splitRandom mark the class first thing
 * (touch). Pool clears the marks when a run's world is launched; det.nbt
 * lists a split whose owner the pool had initialized for itself only once
 * the run has touched it (Pool.absentSplit).
 */
public final class PoolAgent
{
    private PoolAgent() {}

    /** Set by premain: the marks are kept (a member without the agent lists every split, as before). */
    static volatile boolean active;
    /** Each instrumented class (internal name) by its index, and whether the current run touched it. */
    static final List<String> owners = new ArrayList<String>();
    static final boolean[] touched = new boolean[64];

    public static void premain(String args, Instrumentation inst)
    {
        active = true;
        // a coverage member (JaCoCo's agent came first): its class initializers are kept per class
        try
        {
            Class.forName("org.jacoco.agent.rt.RT", false, ClassLoader.getSystemClassLoader());
            CovInit.install(inst);
        }
        catch (ClassNotFoundException e) {}
        inst.addTransformer(new ClassFileTransformer()
        {
            public byte[] transform(ClassLoader l, String name, Class<?> redefined, ProtectionDomain d, byte[] b)
            {
                if (name == null || redefined != null || !name.startsWith("net/minecraft/") || !mentions(b, "splitRandom")) return null;
                try { return instrument(name, b); }
                catch (Throwable t)
                {
                    System.out.println("ORACLE POOL agent: " + name + " not instrumented: " + t);
                    return null;
                }
            }
        });
    }

    /** Called first thing by every constructor and static method of an owner. */
    public static void touch(int i) { touched[i] = true; }

    static void clear() { java.util.Arrays.fill(touched, false); }

    /** Whether the current run touched the owner class (binary name), or null when the agent does not watch it. */
    static Boolean touched(String className)
    {
        if (!active) return null;
        String n = className.replace('.', '/');
        synchronized (owners)
        {
            int i = owners.indexOf(n);
            return i < 0 ? null : touched[i];
        }
    }

    static boolean mentions(byte[] b, String s)
    {
        byte[] p = s.getBytes();
        outer:
        for (int i = 0; i + p.length <= b.length; ++i)
        {
            for (int j = 0; j < p.length; ++j) if (b[i + j] != p[j]) continue outer;
            return true;
        }
        return false;
    }

    static byte[] instrument(final String name, byte[] b)
    {
        // an owner: its initializer calls Det.splitRandom
        final boolean[] owner = new boolean[1];
        new ClassReader(b).accept(new ClassVisitor(Opcodes.ASM5)
        {
            public MethodVisitor visitMethod(int access, String mname, String desc, String sig, String[] ex)
            {
                if (!"<clinit>".equals(mname)) return null;
                return new MethodVisitor(Opcodes.ASM5)
                {
                    public void visitMethodInsn(int op, String o, String n, String dsc, boolean itf)
                    {
                        if (op == Opcodes.INVOKESTATIC && "netherite/oracle/Det".equals(o) && "splitRandom".equals(n)) owner[0] = true;
                    }
                };
            }
        }, ClassReader.SKIP_DEBUG | ClassReader.SKIP_FRAMES);
        if (!owner[0]) return null;
        final int idx;
        synchronized (owners)
        {
            if (owners.size() >= touched.length) throw new IllegalStateException("more than " + touched.length + " split owners");
            idx = owners.size();
            owners.add(name);
        }
        ClassReader r = new ClassReader(b);
        // the inserted call needs no frames of its own: it is at offset 0, before any branch
        ClassWriter w = new ClassWriter(r, 0);
        r.accept(new ClassVisitor(Opcodes.ASM5, w)
        {
            public MethodVisitor visitMethod(int access, String mname, String desc, String sig, String[] ex)
            {
                MethodVisitor mv = super.visitMethod(access, mname, desc, sig, ex);
                boolean entry = "<init>".equals(mname) || ((access & Opcodes.ACC_STATIC) != 0 && !"<clinit>".equals(mname));
                if (!entry || (access & (Opcodes.ACC_ABSTRACT | Opcodes.ACC_NATIVE)) != 0) return mv;
                return new MethodVisitor(Opcodes.ASM5, mv)
                {
                    public void visitCode()
                    {
                        super.visitCode();
                        super.visitIntInsn(Opcodes.BIPUSH, idx);
                        super.visitMethodInsn(Opcodes.INVOKESTATIC, "netherite/oracle/PoolAgent", "touch", "(I)V", false);
                    }

                    public void visitMaxs(int stack, int locals) { super.visitMaxs(Math.max(stack, 1), locals); }
                };
            }
        }, 0);
        return w.toByteArray();
    }
}
