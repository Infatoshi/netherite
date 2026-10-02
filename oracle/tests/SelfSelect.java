import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import java.util.zip.GZIPInputStream;

// ASM as jacococli.jar carries it (relocated)
import org.jacoco.cli.internal.asm.*;

/**
 * The selective self-check's selector (lane/selfselect; make selfcheck
 * SELECT=1 runs it through tests/selfcheck.sh).
 *
 *   java SelfSelect fingerprint CLASSES OUT.tsv
 *
 * Every class file under CLASSES/net/minecraft and CLASSES/netherite, as
 * behaviour sees it. Per class a header hash: version, access, name,
 * signature, super, interfaces, annotations, its own nesting attributes
 * and its instance fields in order (the object's layout, what Gson and
 * getDeclaredFields see). Per static field a hash of its access, type,
 * signature, constant and annotations. Per method a hash of its access,
 * signature, exceptions, annotations and code (every instruction with its
 * operands resolved to names and values, labels numbered in order,
 * try/catch blocks, max stack and locals). Debug attributes (source file,
 * line numbers, local variable names) and stack map frames (derived from
 * the code) are left out: the oracle never reads a line number (Det,
 * ParticleLog and ChunkLoadProbe read class and method names off the stack
 * only), so a comment or a moved line changes nothing.
 *   C <class> <header hash> <super> <interfaces,...> <access>
 *   F <class> <field> <hash> <static 0|1> <constant 0|1>
 *   M <class> <name><desc> <hash> <access>
 * make coverage writes the fingerprints of the classes it analysed beside
 * the coverage data (COVDIR/fingerprints.tsv).
 *
 *   java SelfSelect select COVDIR CLASSES RECS OUT [RACY]
 *
 * (CLASSES may be a fingerprints file instead of a build: the history proof.)
 *
 * Which of the recordings in RECS (one name or directory per line: what
 * the full self-check would replay on the tree built in CLASSES) can reach
 * code that differs between COVDIR's classes (its fingerprints.tsv) and
 * CLASSES. COVDIR is make coverage's output on some earlier tree: recs.tsv
 * (the status of each recording's coverage run) and recs/NAME.methods.tsv
 * (the methods it ran; .gz read too). A recording is selected when
 *   - COVDIR has no data for it (new since, or never covered);
 *   - its coverage run did not replay OK (the data stops at the divergence);
 *   - its data has classes of another build (recs/NAME.stale: a make coverage
 *     RECS=... run on a later tree re-analyses every recording, and an
 *     older run's data of a changed class names nothing in the new classes);
 *   - it ran a method whose hash differs, or that is gone;
 *   - it ran any method of a class touched as a whole, or of a subclass or
 *     implementer of one (an interface or a superclass with no code of its
 *     own shows in no recording's data, so the closure goes through the
 *     hierarchy of both trees). A class is touched as a whole when it is
 *     gone, its header differs, a static field changed or went, it gained
 *     a static field with a constant (reflection reads it: Oracle.mutates)
 *     or one that hides a supertype's field (unchanged code's getstatic
 *     resolves to it now), it gained a static initializer, or it gained a
 *     non-private method (or one became non-private or changed staticness)
 *     that overrides or hides a supertype's method of the same name and
 *     descriptor (a call that went up the hierarchy now stops at it). The
 *     supertypes are both trees' classes and, by reflection, the JDK's and
 *     the libraries' on the classpath; one that cannot be read counts as a
 *     match. Any other new member is reached only through changed code.
 * Otherwise every method it ran has the same code and header in a class
 * whose dispatch and fields its code sees unchanged, so its replay on
 * CLASSES runs the same instructions and makes the same rows as it did
 * when COVDIR recorded it, which replayed OK. A class new in CLASSES is
 * reached only from changed code (whose callers are selected) or by a
 * replay setup's class name (Oracle's Class.forName("netherite.oracle." +
 * cls)), which fails the coverage run on the older tree, so its recording
 * is selected as not OK.
 * Everything else falls back to every recording, loudly (FALLBACK lines):
 * a change to a class matching a line of RACY (tests/selfselect.racy: code
 * whose coverage follows thread timing, so one run's data may lack a
 * method another run takes; a line Class.name names one method), or to
 * the pool's own classes (Pool, Rewind, PoolAgent, PoolGl, PoolMouse: they run in every
 * pooled replay outside the job's coverage window), or COVDIR's
 * fingerprints missing, of the older format or made without netherite.*.
 * Writes the selected names to OUT, one per line, and prints
 *   SELECT NAME  why          (one line each)
 *   FALLBACK why              (every recording then)
 *   selfselect: S of N recordings selected (...)
 * rc 0; rc 2 on bad input.
 */
public class SelfSelect
{
    /** select ... RACY nofallback: the history proof's other column, what the mapping alone picks (FALLBACK lines still printed) */
    static boolean nofallback;

    public static void main(String[] a) throws Exception
    {
        if (a.length == 3 && "fingerprint".equals(a[0])) { fingerprint(new File(a[1]), new File(a[2])); return; }
        if (a.length >= 5 && a.length <= 7 && "select".equals(a[0]))
        {
            nofallback = a.length == 7 && "nofallback".equals(a[6]);
            System.exit(select(new File(a[1]), new File(a[2]), new File(a[3]), new File(a[4]), a.length >= 6 ? new File(a[5]) : null));
        }
        System.err.println("usage: SelfSelect fingerprint CLASSES OUT.tsv | select COVDIR CLASSES RECS OUT [RACY]");
        System.exit(2);
    }

    // ---- fingerprints

    static final class Fp
    {
        final Map<String, String> head = new TreeMap<String, String>();          // class -> header hash
        final Map<String, String> sup = new HashMap<String, String>();           // class -> super
        final Map<String, List<String>> ifs = new HashMap<String, List<String>>(); // class -> interfaces
        final Map<String, Integer> access = new HashMap<String, Integer>();
        final Map<String, Map<String, String[]>> fields = new HashMap<String, Map<String, String[]>>(); // class -> name -> hash, static, constant
        final Map<String, Map<String, String>> code = new HashMap<String, Map<String, String>>();       // class -> name+desc -> hash
        final Map<String, Map<String, Integer>> macc = new HashMap<String, Map<String, Integer>>();     // class -> name+desc -> access
        String header = "";
    }

    static void fingerprint(File classes, File out) throws IOException
    {
        Fp fp = fingerprints(classes);
        StringBuilder b = new StringBuilder("# SelfSelect fingerprints v2 of " + classes.getCanonicalPath() + ": net/minecraft netherite\n");
        for (String c : fp.head.keySet())
        {
            StringBuilder is = new StringBuilder();
            for (String i : fp.ifs.get(c)) is.append(is.length() > 0 ? "," : "").append(i);
            b.append("C\t").append(c).append('\t').append(fp.head.get(c)).append('\t').append(fp.sup.get(c)).append('\t')
             .append(is.length() == 0 ? "-" : is).append('\t').append(fp.access.get(c)).append('\n');
            for (Map.Entry<String, String[]> f : new TreeMap<String, String[]>(fp.fields.get(c)).entrySet())
                b.append("F\t").append(c).append('\t').append(f.getKey()).append('\t').append(f.getValue()[0]).append('\t')
                 .append(f.getValue()[1]).append('\t').append(f.getValue()[2]).append('\n');
            for (Map.Entry<String, String> m : new TreeMap<String, String>(fp.code.get(c)).entrySet())
                b.append("M\t").append(c).append('\t').append(m.getKey()).append('\t').append(m.getValue()).append('\t')
                 .append(fp.macc.get(c).get(m.getKey())).append('\n');
        }
        File t = new File(out.getPath() + ".part");
        Files.write(t.toPath(), b.toString().getBytes(StandardCharsets.UTF_8));
        Files.move(t.toPath(), out.toPath(), StandardCopyOption.REPLACE_EXISTING);
        System.err.println("selfselect: fingerprints of " + fp.head.size() + " classes: " + out);
    }

    static Fp fingerprints(File classes) throws IOException
    {
        Fp fp = new Fp();
        fp.header = "v2 net/minecraft netherite";
        List<File> files = new ArrayList<File>();
        collect(new File(classes, "net/minecraft"), files);
        collect(new File(classes, "netherite"), files);
        for (File f : files) one(Files.readAllBytes(f.toPath()), fp);
        return fp;
    }

    static Fp readFp(File f) throws IOException
    {
        Fp fp = new Fp();
        for (String l : Files.readAllLines(f.toPath(), StandardCharsets.UTF_8))
        {
            if (l.startsWith("#")) { fp.header += l; continue; }
            String[] p = l.split("\t");
            if (p[0].equals("C"))
            {
                fp.head.put(p[1], p[2]);
                fp.sup.put(p[1], p[3]);
                fp.ifs.put(p[1], p[4].equals("-") ? new ArrayList<String>() : Arrays.asList(p[4].split(",")));
                fp.access.put(p[1], p.length > 5 ? Integer.parseInt(p[5]) : 0);
                fp.fields.put(p[1], new HashMap<String, String[]>());
                fp.code.put(p[1], new HashMap<String, String>());
                fp.macc.put(p[1], new HashMap<String, Integer>());
            }
            else if (p[0].equals("F")) fp.fields.get(p[1]).put(p[2], new String[] { p[3], p[4], p[5] });
            else if (p[0].equals("M")) { fp.code.get(p[1]).put(p[2], p[3]); fp.macc.get(p[1]).put(p[2], p.length > 4 ? Integer.parseInt(p[4]) : 0); }
        }
        return fp;
    }

    /**
     * One class. Its header: version, access, name, signature, super,
     * interfaces, annotations, its own nesting (outer class, its own
     * InnerClasses entry: another member class's entry is that class's
     * business) and its instance fields in order (layout, and what Gson and
     * getDeclaredFields see). Each static field and each method on its own:
     * a static field's access, type, signature, constant and annotations; a
     * method's access, signature, exceptions, annotations and code.
     */
    static void one(byte[] bytes, final Fp fp)
    {
        final StringBuilder s = new StringBuilder();
        final String[] name = new String[1];
        final Map<String, String> code = new HashMap<String, String>();
        final Map<String, Integer> macc = new HashMap<String, Integer>();
        final Map<String, String[]> fields = new HashMap<String, String[]>();
        final StringBuilder inst = new StringBuilder();
        ClassReader r = new ClassReader(bytes);
        r.accept(new ClassVisitor(Opcodes.ASM9)
        {
            public void visit(int version, int access, String nm, String sig, String sup, String[] ifs)
            {
                name[0] = nm;
                s.append("class ").append(version).append(' ').append(access).append(' ').append(nm).append(' ').append(sig).append(' ').append(sup);
                List<String> il = ifs == null ? new ArrayList<String>() : Arrays.asList(ifs);
                s.append(' ').append(il).append('\n');
                fp.sup.put(nm, sup == null ? "-" : sup);
                fp.ifs.put(nm, new ArrayList<String>(il));
                fp.access.put(nm, access);
            }
            public void visitOuterClass(String owner, String nm, String desc) { s.append("outer ").append(owner).append(' ').append(nm).append(' ').append(desc).append('\n'); }
            public void visitInnerClass(String nm, String outer, String inner, int access)
            {
                if (nm.equals(name[0])) s.append("inner ").append(nm).append(' ').append(outer).append(' ').append(inner).append(' ').append(access).append('\n');
            }
            public AnnotationVisitor visitAnnotation(String desc, boolean vis) { s.append("ann ").append(desc).append(' ').append(vis).append('\n'); return new Ann(s); }
            public AnnotationVisitor visitTypeAnnotation(int ref, TypePath p, String desc, boolean vis) { s.append("tann ").append(ref).append(' ').append(p).append(' ').append(desc).append('\n'); return new Ann(s); }
            public void visitAttribute(Attribute at) { s.append("attr ").append(at.type).append('\n'); }
            public FieldVisitor visitField(final int access, final String nm, String desc, String sig, final Object value)
            {
                final StringBuilder f = new StringBuilder();
                f.append("field ").append(access).append(' ').append(nm).append(' ').append(desc).append(' ').append(sig).append(' ').append(val(value));
                return new FieldVisitor(Opcodes.ASM9)
                {
                    public AnnotationVisitor visitAnnotation(String d, boolean vis) { f.append(" ann ").append(d).append(' ').append(vis); return new Ann(f); }
                    public void visitEnd()
                    {
                        boolean st = (access & Opcodes.ACC_STATIC) != 0;
                        if (!st) inst.append(f).append('\n');
                        fields.put(nm, new String[] { sha(f.toString()), st ? "1" : "0", value != null ? "1" : "0" });
                    }
                };
            }
            public MethodVisitor visitMethod(final int access, String nm, final String desc, String sig, String[] exc)
            {
                final String key = nm + desc;
                final StringBuilder h = new StringBuilder();
                h.append("method ").append(access).append(' ').append(key).append(' ').append(sig).append(' ').append(exc == null ? "[]" : Arrays.asList(exc));
                return new Code(new StringBuilder())
                {
                    public AnnotationVisitor visitAnnotation(String d, boolean vis) { h.append(" ann ").append(d).append(' ').append(vis); return new Ann(h); }
                    public AnnotationVisitor visitParameterAnnotation(int p, String d, boolean vis) { h.append(" pann ").append(p).append(' ').append(d); return new Ann(h); }
                    public AnnotationVisitor visitAnnotationDefault() { h.append(" default"); return new Ann(h); }
                    public void visitParameter(String n, int acc) { h.append(" param ").append(acc); }
                    public void visitEnd()
                    {
                        code.put(key, sha(h + "\n" + t));
                        macc.put(key, access);
                    }
                };
            }
        }, ClassReader.SKIP_DEBUG | ClassReader.SKIP_FRAMES);
        s.append(inst);
        fp.head.put(name[0], sha(s.toString()));
        fp.code.put(name[0], code);
        fp.macc.put(name[0], macc);
        fp.fields.put(name[0], fields);
    }

    static String val(Object v)
    {
        if (v == null) return "null";
        if (v instanceof Double) return "D" + Long.toHexString(Double.doubleToRawLongBits((Double)v));
        if (v instanceof Float) return "F" + Integer.toHexString(Float.floatToRawIntBits((Float)v));
        if (v instanceof String) return "S" + v.toString().length() + ":" + v;
        if (v instanceof Type) return "T" + ((Type)v).getDescriptor();
        if (v instanceof Handle) { Handle x = (Handle)v; return "H" + x.getTag() + x.getOwner() + "." + x.getName() + x.getDesc() + x.isInterface(); }
        if (v instanceof ConstantDynamic) { ConstantDynamic c = (ConstantDynamic)v; StringBuilder b = new StringBuilder("CD" + c.getName() + c.getDescriptor() + val(c.getBootstrapMethod())); for (int i = 0; i < c.getBootstrapMethodArgumentCount(); ++i) b.append(',').append(val(c.getBootstrapMethodArgument(i))); return b.toString(); }
        return v.getClass().getSimpleName().charAt(0) + v.toString();
    }

    static final class Ann extends AnnotationVisitor
    {
        final StringBuilder s;
        Ann(StringBuilder s) { super(Opcodes.ASM9); this.s = s; }
        public void visit(String n, Object v) { s.append(" ").append(n).append('=').append(v != null && v.getClass().isArray() ? arr(v) : val(v)); }
        public void visitEnum(String n, String d, String v) { s.append(" ").append(n).append('=').append(d).append('.').append(v); }
        public AnnotationVisitor visitAnnotation(String n, String d) { s.append(" ").append(n).append("=@").append(d); return new Ann(s); }
        public AnnotationVisitor visitArray(String n) { s.append(" ").append(n).append("=["); return new Ann(s); }
        public void visitEnd() { s.append(" ;"); }
        static String arr(Object v)
        {
            StringBuilder b = new StringBuilder("[");
            int n = java.lang.reflect.Array.getLength(v);
            for (int i = 0; i < n; ++i) b.append(i > 0 ? "," : "").append(val(java.lang.reflect.Array.get(v, i)));
            return b.append(']').toString();
        }
    }

    /** A method's code as text: instructions with resolved operands, labels by order of first use. */
    static class Code extends MethodVisitor
    {
        final StringBuilder t;
        final IdentityHashMap<Label, Integer> labels = new IdentityHashMap<Label, Integer>();
        Code(StringBuilder t) { super(Opcodes.ASM9); this.t = t; }
        String l(Label x)
        {
            Integer i = labels.get(x);
            if (i == null) labels.put(x, i = labels.size());
            return "L" + i;
        }
        public void visitInsn(int op) { t.append(op).append('\n'); }
        public void visitIntInsn(int op, int v) { t.append(op).append(' ').append(v).append('\n'); }
        public void visitVarInsn(int op, int v) { t.append(op).append(' ').append(v).append('\n'); }
        public void visitTypeInsn(int op, String type) { t.append(op).append(' ').append(type).append('\n'); }
        public void visitFieldInsn(int op, String o, String n, String d) { t.append(op).append(' ').append(o).append('.').append(n).append(' ').append(d).append('\n'); }
        public void visitMethodInsn(int op, String o, String n, String d, boolean itf) { t.append(op).append(' ').append(o).append('.').append(n).append(d).append(' ').append(itf).append('\n'); }
        public void visitInvokeDynamicInsn(String n, String d, Handle bsm, Object... args)
        {
            t.append("indy ").append(n).append(d).append(' ').append(val(bsm));
            for (Object x : args) t.append(',').append(val(x));
            t.append('\n');
        }
        public void visitJumpInsn(int op, Label x) { t.append(op).append(' ').append(l(x)).append('\n'); }
        public void visitLabel(Label x) { t.append(l(x)).append(":\n"); }
        public void visitLdcInsn(Object v) { t.append("ldc ").append(val(v)).append('\n'); }
        public void visitIincInsn(int v, int inc) { t.append("iinc ").append(v).append(' ').append(inc).append('\n'); }
        public void visitTableSwitchInsn(int min, int max, Label dflt, Label... ls)
        {
            t.append("tswitch ").append(min).append(' ').append(max).append(' ').append(l(dflt));
            for (Label x : ls) t.append(',').append(l(x));
            t.append('\n');
        }
        public void visitLookupSwitchInsn(Label dflt, int[] keys, Label[] ls)
        {
            t.append("lswitch ").append(l(dflt));
            for (int i = 0; i < keys.length; ++i) t.append(',').append(keys[i]).append(':').append(l(ls[i]));
            t.append('\n');
        }
        public void visitMultiANewArrayInsn(String d, int dims) { t.append("multianew ").append(d).append(' ').append(dims).append('\n'); }
        public void visitTryCatchBlock(Label a, Label b, Label h, String type) { t.append("try ").append(l(a)).append(' ').append(l(b)).append(' ').append(l(h)).append(' ').append(type).append('\n'); }
        public void visitMaxs(int st, int lo) { t.append("maxs ").append(st).append(' ').append(lo).append('\n'); }
    }

    static String sha(String s)
    {
        try
        {
            byte[] d = MessageDigest.getInstance("SHA-1").digest(s.getBytes(StandardCharsets.UTF_8));
            StringBuilder b = new StringBuilder();
            for (int i = 0; i < 10; ++i) b.append(String.format("%02x", d[i] & 0xff));
            return b.toString();
        }
        catch (Exception e) { throw new IllegalStateException(e); }
    }

    static void collect(File d, List<File> out)
    {
        File[] fs = d.listFiles();
        if (fs == null) return;
        Arrays.sort(fs);
        for (File f : fs)
        {
            if (f.isDirectory()) collect(f, out);
            else if (f.getName().endsWith(".class")) out.add(f);
        }
    }

    // ---- selection

    /** The pool's own classes: every pooled replay runs them outside its job's coverage window. */
    static final String[] POOL = { "netherite/oracle/Pool", "netherite/oracle/Rewind", "netherite/oracle/PoolAgent", "netherite/oracle/PoolGl", "netherite/oracle/PoolMouse" };

    static boolean poolClass(String c)
    {
        for (String p : POOL) if (c.equals(p) || c.startsWith(p + "$")) return true;
        return false;
    }

    /** The supertypes of both trees' classes (the new tree's first), and of the JDK's and the libraries' by reflection. */
    static final class Hier
    {
        final Fp now, base;
        final Map<String, Object[]> ext = new HashMap<String, Object[]>();   // reflected: supertypes, field names, method keys -> access; null when it cannot be loaded
        Hier(Fp base, Fp now) { this.base = base; this.now = now; }

        Fp of(String c) { return now.head.containsKey(c) ? now : base.head.containsKey(c) ? base : null; }

        Object[] reflect(String c)
        {
            if (ext.containsKey(c)) return ext.get(c);
            Object[] r = null;
            try
            {
                Class<?> k = Class.forName(c.replace('/', '.'), false, SelfSelect.class.getClassLoader());
                List<String> sup = new ArrayList<String>();
                if (k.getSuperclass() != null) sup.add(k.getSuperclass().getName().replace('.', '/'));
                for (Class<?> i : k.getInterfaces()) sup.add(i.getName().replace('.', '/'));
                Set<String> fs = new HashSet<String>();
                for (java.lang.reflect.Field f : k.getDeclaredFields()) fs.add(f.getName());
                Map<String, Integer> ms = new HashMap<String, Integer>();
                for (java.lang.reflect.Method m : k.getDeclaredMethods()) ms.put(m.getName() + Type.getMethodDescriptor(m), m.getModifiers());
                r = new Object[] { sup, fs, ms };
            }
            catch (Throwable e) { r = null; }
            ext.put(c, r);
            return r;
        }

        /** Direct supertypes, or null when unknown. */
        @SuppressWarnings("unchecked")
        List<String> supers(String c)
        {
            Fp f = of(c);
            if (f != null)
            {
                List<String> l = new ArrayList<String>(f.ifs.get(c));
                if (!"-".equals(f.sup.get(c))) l.add(f.sup.get(c));
                return l;
            }
            Object[] r = reflect(c);
            return r == null ? null : (List<String>)r[0];
        }

        @SuppressWarnings("unchecked")
        Map<String, Integer> methods(String c)
        {
            Fp f = of(c);
            if (f != null) return f.macc.get(c);
            Object[] r = reflect(c);
            return r == null ? null : (Map<String, Integer>)r[2];
        }

        @SuppressWarnings("unchecked")
        Set<String> fieldNames(String c)
        {
            Fp f = of(c);
            if (f != null) return f.fields.get(c).keySet();
            Object[] r = reflect(c);
            return r == null ? null : (Set<String>)r[1];
        }

        /** Every proper supertype of C, or null when one cannot be read. */
        Set<String> above(String c)
        {
            Set<String> seen = new LinkedHashSet<String>();
            List<String> s0 = supers(c);
            if (s0 == null) return null;
            Deque<String> q = new ArrayDeque<String>(s0);
            while (!q.isEmpty())
            {
                String s = q.poll();
                if (!seen.add(s)) continue;
                List<String> l = supers(s);
                if (l == null) return null;
                q.addAll(l);
            }
            return seen;
        }

        /** A supertype of C declares a field NAME (a new static field of that name hides it from unchanged code's resolution). */
        boolean fieldAbove(String c, String name)
        {
            Set<String> up = above(c);
            if (up == null) return true;
            for (String s : up)
            {
                Set<String> fs = fieldNames(s);
                if (fs == null || fs.contains(name)) return true;
            }
            return false;
        }
    }

    /**
     * Whether a method KEY that C gained (or that became visible to dispatch)
     * can change what unchanged code does: it overrides or hides a
     * non-private method of the same name and descriptor in a supertype (a
     * call on C or a subclass that went up the hierarchy now stops at C).
     * A constructor or a private method never does; a supertype that cannot
     * be read counts as a match.
     */
    static boolean overrides(Hier h, String c, String key, Integer access)
    {
        if (key.startsWith("<init>") || key.startsWith("<clinit>")) return false;
        if (access != null && (access & Opcodes.ACC_PRIVATE) != 0) return false;
        Set<String> up = h.above(c);
        if (up == null) return true;
        for (String s : up)
        {
            Map<String, Integer> ms = h.methods(s);
            if (ms == null) return true;
            Integer a = ms.get(key);
            if (a != null && (a & Opcodes.ACC_PRIVATE) == 0) return true;
        }
        return false;
    }

    /** C's static fields and methods are the same in both trees (its header aside). */
    static boolean sameMembers(Fp base, Fp now, String c)
    {
        Map<String, String[]> bf = base.fields.get(c), nf = now.fields.get(c);
        if (!bf.keySet().equals(nf.keySet())) return false;
        for (String k : bf.keySet()) if (!bf.get(k)[0].equals(nf.get(k)[0])) return false;
        return base.code.get(c).equals(now.code.get(c));
    }

    static int select(File cov, File classes, File recsFile, File out, File racyFile) throws IOException
    {
        long t0 = System.currentTimeMillis();
        List<String> names = new ArrayList<String>();
        for (String l : Files.readAllLines(recsFile.toPath(), StandardCharsets.UTF_8))
        {
            l = l.trim();
            if (l.isEmpty() || l.startsWith("#")) continue;
            String n = l.split("\\s+")[0];
            while (n.endsWith("/")) n = n.substring(0, n.length() - 1);
            names.add(n.substring(n.lastIndexOf('/') + 1));
        }
        List<String> fallback = new ArrayList<String>();
        File fpf = new File(cov, "fingerprints.tsv");
        Fp base = null;
        if (!fpf.isFile()) fallback.add("the coverage data has no fingerprints of its classes (" + fpf + "): run make coverage");
        else
        {
            base = readFp(fpf);
            if (!base.header.contains("v2") || !base.header.contains("netherite"))
            {
                fallback.add("the coverage data's fingerprints are of an older format or do not cover netherite.*: run make coverage");
                base = null;
            }
        }
        Fp now = classes.isFile() ? readFp(classes) : fingerprints(classes);   // a fingerprints file (the history proof) or a build
        if (now.head.isEmpty()) { System.err.println("selfselect: no classes under " + classes); return 2; }
        List<String> racy = new ArrayList<String>();
        if (racyFile != null)
            for (String l : Files.readAllLines(racyFile.toPath(), StandardCharsets.UTF_8))
            {
                l = l.trim();
                if (!l.isEmpty() && !l.startsWith("#")) racy.add(l.split("\\s+")[0]);
            }

        // what changed
        Set<String> classTouch = new TreeSet<String>(), methodTouch = new TreeSet<String>(), added = new TreeSet<String>();
        Map<String, String> classWhy = new TreeMap<String, String>();
        Set<String> changed = new TreeSet<String>();   // every class with any difference
        int changedMethods = 0;
        if (base != null)
        {
            Hier h = new Hier(base, now);
            for (String c : base.head.keySet())
            {
                if (!now.head.containsKey(c)) { classWhy.put(c, "gone"); continue; }
                if (!base.head.get(c).equals(now.head.get(c))) { classWhy.put(c, "header or instance fields"); continue; }
                // static fields: a changed, gone or constant one, or one that hides a supertype's field
                Map<String, String[]> bf = base.fields.get(c), nf = now.fields.get(c);
                for (Map.Entry<String, String[]> e : bf.entrySet())
                {
                    String[] n = nf.get(e.getKey());
                    if (n == null || !n[0].equals(e.getValue()[0])) { classWhy.put(c, "static field " + e.getKey()); break; }
                }
                if (classWhy.containsKey(c)) continue;
                for (Map.Entry<String, String[]> e : nf.entrySet())
                {
                    if (bf.containsKey(e.getKey())) continue;
                    if ("1".equals(e.getValue()[2])) { classWhy.put(c, "new constant field " + e.getKey()); break; }
                    if (h.fieldAbove(c, e.getKey())) { classWhy.put(c, "new field " + e.getKey() + " hides a supertype's"); break; }
                }
                if (classWhy.containsKey(c)) continue;
                Map<String, String> bm = base.code.get(c), nm = now.code.get(c);
                Map<String, Integer> ba = base.macc.get(c), na = now.macc.get(c);
                for (Map.Entry<String, String> e : bm.entrySet())
                {
                    String k = e.getKey();
                    if (e.getValue().equals(nm.get(k))) continue;
                    methodTouch.add(c + "." + k);
                    ++changedMethods;
                    // a method now visible to dispatch (was private, or static changed) is as good as new
                    if (na.containsKey(k) && ((ba.get(k) ^ na.get(k)) & (Opcodes.ACC_PRIVATE | Opcodes.ACC_STATIC)) != 0 && overrides(h, c, k, na.get(k)))
                        classWhy.put(c, k + " now overrides or hides a supertype's");
                }
                for (Map.Entry<String, String> e : nm.entrySet())
                {
                    String k = e.getKey();
                    if (bm.containsKey(k)) continue;
                    if (k.startsWith("<clinit>")) { classWhy.put(c, "new static initializer"); break; }
                    if (overrides(h, c, k, na.get(k))) { classWhy.put(c, "new " + k + " overrides or hides a supertype's"); break; }
                }
            }
            classTouch.addAll(classWhy.keySet());
            for (String c : now.head.keySet())
            {
                if (!base.head.containsKey(c)) { added.add(c); continue; }
                if (!base.head.get(c).equals(now.head.get(c)) || !sameMembers(base, now, c)) changed.add(c);
            }
            changed.addAll(classTouch);
            for (String m : methodTouch)
                for (String r : racy) if (r.indexOf('.') >= 0 && (m.startsWith(r + "(") || m.equals(r))) { fallback.add(m + " changed and is " + r + " in the racy list"); break; }
            for (String c : changed)
            {
                if (poolClass(c)) fallback.add("the pool's own class " + c + " changed (every pooled replay runs it outside its coverage window)");
                for (String r : racy) if (r.indexOf('.') < 0 && c.startsWith(r)) { fallback.add(c + " changed and matches " + r + " in the racy list (its coverage follows thread timing)"); break; }
            }
        }
        // the class closure: every subclass or implementer, in either tree, of a class touched as a whole
        Set<String> closure = new TreeSet<String>(classTouch);
        if (!classTouch.isEmpty())
        {
            Map<String, Set<String>> kids = new HashMap<String, Set<String>>();
            for (Fp f : new Fp[] { base, now })
            {
                if (f == null) continue;
                for (String c : f.head.keySet())
                {
                    List<String> ps = new ArrayList<String>(f.ifs.get(c));
                    ps.add(f.sup.get(c));
                    for (String p : ps)
                    {
                        Set<String> k = kids.get(p);
                        if (k == null) kids.put(p, k = new HashSet<String>());
                        k.add(c);
                    }
                }
            }
            Deque<String> q = new ArrayDeque<String>(classTouch);
            while (!q.isEmpty())
            {
                Set<String> k = kids.get(q.poll());
                if (k != null) for (String c : k) if (closure.add(c)) q.add(c);
            }
        }

        // the coverage runs' statuses
        Map<String, String> status = new HashMap<String, String>();
        File rt = new File(cov, "recs.tsv");
        if (rt.isFile())
            for (String l : Files.readAllLines(rt.toPath(), StandardCharsets.UTF_8))
            {
                String[] p = l.split("\t");
                if (p.length >= 3) status.put(p[0], p[2]);
            }

        StringBuilder sel = new StringBuilder(), lines = new StringBuilder();
        int nsel = 0;
        Map<String, Integer> whyCount = new TreeMap<String, Integer>();
        boolean all = !fallback.isEmpty() && !nofallback;
        for (String n : names)
        {
            String why = null;
            if (all) why = "fallback";
            else if (!status.containsKey(n)) why = "no coverage data (new)";
            else if (!"OK".equals(status.get(n))) why = "its coverage run did not replay OK: " + status.get(n);
            else
            {
                File mf = new File(cov, "recs/" + n + ".methods.tsv"), gz = new File(mf.getPath() + ".gz");
                if (!mf.isFile() && !gz.isFile()) why = "no coverage data (no methods file)";
                else if (new File(cov, "recs/" + n + ".stale").isFile()) why = "its coverage data has classes of another build (recs/" + n + ".stale)";
                else
                {
                    BufferedReader br = new BufferedReader(new InputStreamReader(mf.isFile() ? new FileInputStream(mf) : new GZIPInputStream(new FileInputStream(gz)), StandardCharsets.UTF_8));
                    String l;
                    while ((l = br.readLine()) != null)
                    {
                        String[] p = l.split("\t");
                        if (p.length < 4) continue;
                        String m = p[1] + "." + p[2] + p[3];
                        if (methodTouch.contains(m)) { why = "runs " + m; break; }
                        if (closure.contains(p[1])) { why = "runs " + m + " of changed class " + (classTouch.contains(p[1]) ? p[1] : "(a subclass of) " + p[1]); break; }
                    }
                    br.close();
                }
            }
            if (why == null) continue;
            ++nsel;
            sel.append(n).append('\n');
            lines.append("SELECT ").append(n).append("  ").append(why).append('\n');
            String k = why.startsWith("runs ") ? "coverage" : why.startsWith("its coverage run") ? "not OK" : why.startsWith("its coverage data") ? "stale" : why.startsWith("no coverage") ? "new" : why;
            Integer c0 = whyCount.get(k);
            whyCount.put(k, c0 == null ? 1 : c0 + 1);
        }
        File t = new File(out.getPath() + ".part");
        Files.write(t.toPath(), sel.toString().getBytes(StandardCharsets.UTF_8));
        Files.move(t.toPath(), out.toPath(), StandardCopyOption.REPLACE_EXISTING);
        StringBuilder o = new StringBuilder();
        for (String f : new LinkedHashSet<String>(fallback)) o.append("FALLBACK ").append(f).append('\n');
        o.append("selfselect: changed ").append(classTouch.size()).append(" classes as a whole (").append(closure.size() - classTouch.size())
         .append(" more by the hierarchy), ").append(changedMethods).append(" methods, ").append(added.size()).append(" new classes\n");
        int k = 0;
        for (String c : classTouch) { if (k++ < 12) o.append("  class ").append(c).append(" (").append(classWhy.get(c)).append(")\n"); }
        if (classTouch.size() > 12) o.append("  ... ").append(classTouch.size() - 12).append(" more classes\n");
        k = 0;
        for (String m : methodTouch) { if (k++ < 20) o.append("  method ").append(m).append('\n'); }
        if (methodTouch.size() > 20) o.append("  ... ").append(methodTouch.size() - 20).append(" more methods\n");
        k = 0;
        for (String c : added) { if (k++ < 8) o.append("  new ").append(c).append('\n'); }
        if (added.size() > 8) o.append("  ... ").append(added.size() - 8).append(" more new classes\n");
        o.append(lines);
        o.append("selfselect: ").append(nsel).append(" of ").append(names.size()).append(" recordings selected ").append(whyCount)
         .append(all ? " (FALLBACK: every recording)" : "").append(" in ").append(System.currentTimeMillis() - t0).append(" ms\n");
        System.out.print(o);
        return 0;
    }
}
