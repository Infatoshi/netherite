import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.zip.GZIPOutputStream;

import com.google.gson.*;

// JaCoCo 0.8.12's core, as jacococli.jar carries it (relocated with its ASM)
import org.jacoco.cli.internal.core.analysis.*;
import org.jacoco.cli.internal.core.data.*;
import org.jacoco.cli.internal.core.internal.data.CRC64;
import org.jacoco.cli.internal.core.tools.ExecFileLoader;

/**
 * make coverage's report (tests/coverage.sh runs it): the JaCoCo execution
 * data of each replayed recording, read against this tree's classes of
 * src/ and harness/ (out/java/classes/net/minecraft and netherite: harness/ only in
 * the per-recording data, for the self-check's selection) and the index's records.
 *
 *   java CovReport CLASSES INDEXDIR COVDIR [THREADS]
 *
 * COVDIR holds exec/NAME.exec (one per recording) and recs.tsv (NAME, dir,
 * status, rows, seconds; coverage.sh). Writes into COVDIR:
 *   recs/NAME.methods.tsv   the methods NAME ran: source, class, method, desc, line
 *   recs/NAME.lines.tsv     the lines NAME ran: source, line ranges
 *   recs/NAME.stale         NAME's data has classes this build does not have (its
 *                           methods file cannot name them: the self-check selects it)
 *   merged.exec             every recording's data in one file (jacococli reads it)
 *   lines.tsv               per source file: lines run by any recording / lines with code, the ranges run
 *   methods.tsv             every method of src/: source, class, method, desc, line, scope, recordings
 *                           running it (count, then the shortest few)
 *   unexecuted.tsv          the in-scope methods no recording runs (index scope in_scope),
 *                           grouped by package and class: first the classes none of whose
 *                           methods run (kind class), then the methods (kind method); each with
 *                           its record's status, its member's status (by name, "-" when the
 *                           record has none) and the recordings that run its class at all
 *   summary.txt             the counts by package, never-run classes first
 * A class whose data does not match the class file (another build's) counts
 * as not run and is reported on stderr. A recording whose exec file and
 * class files are those of the last report (recs/NAME.key: the exec file's
 * sha256 and a hash of every class id) takes its methods and lines from its
 * recs/ files instead of an analysis; recs/ files of recordings no longer in
 * recs.tsv are removed.
 *
 *   java CovReport compare CLASSES A.exec B.exec
 *
 * Two recordings' data (a pool member's and a fresh JVM's of one tape):
 * per class of src/, whether the probes are equal, and the methods one runs
 * and the other does not (all but the report's own filters: every method
 * JaCoCo counts). Prints "probes equal" or the differing classes, then
 * "methods equal: N" or the differing methods; rc 0 only when the covered
 * methods are equal.
 */
public class CovReport
{
    static final class Method
    {
        String src, cls, name, desc;
        int line, lines;
        BitSet recs = new BitSet();
    }

    static final class Klass
    {
        String name, src, pkg;
        boolean mods;   // netherite.* (oracle/harness): in the per-recording data, not the report
        byte[] bytes;
        long id;
        List<Method> methods = new ArrayList<Method>();
        BitSet recs = new BitSet();
    }

    static final class Rec
    {
        String name, dir, status;
        int rows;
    }

    public static void main(String[] a) throws Exception
    {
        if (a.length == 4 && "compare".equals(a[0])) { System.exit(compare(new File(a[1]), new File(a[2]), new File(a[3]))); }
        File classes = new File(a[0]), index = new File(a[1]), cov = new File(a[2]);
        int threads = a.length > 3 ? Integer.parseInt(a[3]) : 8;

        // the classes, their methods and lines (an analysis with no data)
        final List<Klass> ks = new ArrayList<Klass>();
        final Map<Long, Klass> byId = new HashMap<Long, Klass>();
        final Map<String, Method> byKey = new LinkedHashMap<String, Method>();
        final Map<String, BitSet> codeLines = new TreeMap<String, BitSet>();
        List<File> files = new ArrayList<File>();
        collect(new File(classes, "net/minecraft"), files);
        collect(new File(classes, "netherite"), files);
        Collections.sort(files);
        for (File f : files)
        {
            Klass k = new Klass();
            k.bytes = Files.readAllBytes(f.toPath());
            k.id = CRC64.classId(k.bytes);
            CoverageBuilder b = new CoverageBuilder();
            new Analyzer(new ExecutionDataStore(), b).analyzeClass(k.bytes, f.getPath());
            for (IClassCoverage c : b.getClasses())
            {
                k.name = c.getName();
                k.mods = k.name.startsWith("netherite/");
                String pkg = c.getPackageName();
                k.pkg = pkg.startsWith("net/minecraft/") ? pkg.substring(14) : pkg.equals("net/minecraft") ? "" : pkg;
                k.src = c.getSourceFileName() == null ? null : (k.pkg.isEmpty() ? "" : k.pkg + "/") + c.getSourceFileName();
                if (k.src == null) continue;
                BitSet cl = k.mods ? new BitSet() : codeLines.get(k.src);
                if (cl == null) codeLines.put(k.src, cl = new BitSet());
                for (IMethodCoverage m : c.getMethods())
                {
                    Method me = new Method();
                    me.src = k.src; me.cls = k.name; me.name = m.getName(); me.desc = m.getDesc();
                    me.line = m.getFirstLine();
                    me.lines = m.getLineCounter().getTotalCount();
                    for (int l = m.getFirstLine(); l >= 0 && l <= m.getLastLine(); ++l)
                        if (m.getLine(l).getStatus() != ICounter.EMPTY) cl.set(l);
                    k.methods.add(me);
                    byKey.put(k.name + "." + me.name + me.desc, me);
                }
            }
            if (k.src == null) continue;
            ks.add(k);
            byId.put(k.id, k);
        }
        final Map<String, Klass> byName = new HashMap<String, Klass>();
        for (Klass k : ks) byName.put(k.name, k);
        // what a recording's recs/ files were made from besides its data: these class files
        List<Long> allIds = new ArrayList<Long>(byId.keySet());
        Collections.sort(allIds);
        java.security.MessageDigest cd = java.security.MessageDigest.getInstance("SHA-256");
        for (Long id : allIds) cd.update(Long.toHexString(id).getBytes(StandardCharsets.UTF_8));
        final String classesHash = "v1 " + hex(cd.digest());
        final int[] reused = new int[1];

        // the recordings
        final List<Rec> recs = new ArrayList<Rec>();
        for (String l : Files.readAllLines(new File(cov, "recs.tsv").toPath(), StandardCharsets.UTF_8))
        {
            String[] p = l.split("\t");
            if (p.length < 4 || !new File(cov, "exec/" + p[0] + ".exec").isFile()) continue;
            Rec r = new Rec();
            r.name = p[0]; r.dir = p[1]; r.status = p[2];
            try { r.rows = Integer.parseInt(p[3]); } catch (NumberFormatException e) { r.rows = Integer.MAX_VALUE; }
            recs.add(r);
        }
        // one line per recording in recs.tsv: a later line (a RECS rerun) replaces an earlier one
        Map<String, Rec> last = new LinkedHashMap<String, Rec>();
        for (Rec r : recs) { last.remove(r.name); last.put(r.name, r); }
        recs.clear(); recs.addAll(last.values());
        Collections.sort(recs, new Comparator<Rec>() { public int compare(Rec x, Rec y) { return x.name.compareTo(y.name); } });

        final File recDir = new File(cov, "recs");
        recDir.mkdirs();
        Set<String> live = new HashSet<String>();
        for (Rec r : recs) live.add(r.name);
        File[] old = recDir.listFiles();
        if (old != null) for (File f : old)
        {
            String n = f.getName().replaceAll("\\.(methods\\.tsv|lines\\.tsv|key|stale)$", "");
            if (!live.contains(n)) f.delete();
        }
        final ExecFileLoader merged = new ExecFileLoader();
        final Map<String, BitSet> runLines = new TreeMap<String, BitSet>();
        final int[] mismatch = new int[1];
        ExecutorService ex = Executors.newFixedThreadPool(threads);
        List<Future<?>> fs = new ArrayList<Future<?>>();
        for (int ri = 0; ri < recs.size(); ++ri)
        {
            final int idx = ri;
            final Rec r = recs.get(ri);
            fs.add(ex.submit(new Callable<Void>() { public Void call() throws Exception
            {
                byte[] raw = Files.readAllBytes(new File(cov, "exec/" + r.name + ".exec").toPath());
                synchronized (merged) { merged.load(new ByteArrayInputStream(raw)); }
                String key = hex(java.security.MessageDigest.getInstance("SHA-256").digest(raw)) + " " + classesHash;
                File kf = new File(recDir, r.name + ".key"), mf = new File(recDir, r.name + ".methods.tsv"), lf = new File(recDir, r.name + ".lines.tsv");
                if (kf.isFile() && mf.isFile() && lf.isFile() && key.equals(new String(Files.readAllBytes(kf.toPath()), StandardCharsets.UTF_8).trim()))
                {
                    // the last report's analysis of these same bytes
                    for (String l : Files.readAllLines(mf.toPath(), StandardCharsets.UTF_8))
                    {
                        String[] p = l.split("\t");
                        if (p.length < 4) continue;
                        Method me = byKey.get(p[1] + "." + p[2] + p[3]);
                        Klass k = byName.get(p[1]);
                        if (me == null || k == null) throw new IllegalStateException(mf + ": no method " + p[1] + "." + p[2] + p[3]);
                        synchronized (me) { me.recs.set(idx); }
                        synchronized (k) { k.recs.set(idx); }
                    }
                    for (String l : Files.readAllLines(lf.toPath(), StandardCharsets.UTF_8))
                    {
                        String[] p = l.split("\t");
                        if (p.length < 2 || p[1].isEmpty()) continue;
                        BitSet b = new BitSet();
                        for (String rg : p[1].split(","))
                        {
                            int d = rg.indexOf('-');
                            if (d < 0) b.set(Integer.parseInt(rg));
                            else b.set(Integer.parseInt(rg.substring(0, d)), Integer.parseInt(rg.substring(d + 1)) + 1);
                        }
                        synchronized (runLines)
                        {
                            BitSet u = runLines.get(p[0]);
                            if (u == null) runLines.put(p[0], u = new BitSet());
                            u.or(b);
                        }
                    }
                    synchronized (reused) { reused[0]++; }
                    return null;
                }
                ExecFileLoader ld = new ExecFileLoader();
                ld.load(new ByteArrayInputStream(raw));
                ExecutionDataStore st = ld.getExecutionDataStore();
                CoverageBuilder b = new CoverageBuilder();
                Analyzer an = new Analyzer(st, b);
                int miss = 0;
                for (ExecutionData d : st.getContents())
                {
                    Klass k = byId.get(d.getId());
                    if (k == null) { if (d.getName().startsWith("net/minecraft/") || d.getName().startsWith("netherite/")) ++miss; continue; }
                    if (!d.hasHits()) continue;
                    an.analyzeClass(k.bytes, k.name);
                }
                Map<String, BitSet> lines = new TreeMap<String, BitSet>();
                StringBuilder ms = new StringBuilder();
                for (IClassCoverage c : b.getClasses())
                {
                    Klass k = byId.get(c.getId());
                    boolean any = false;
                    for (IMethodCoverage m : c.getMethods())
                    {
                        if (m.getMethodCounter().getCoveredCount() == 0) continue;
                        any = true;
                        Method me = byKey.get(c.getName() + "." + m.getName() + m.getDesc());
                        synchronized (me) { me.recs.set(idx); }
                        ms.append(k.src).append('\t').append(c.getName()).append('\t').append(m.getName()).append('\t')
                          .append(m.getDesc()).append('\t').append(m.getFirstLine()).append('\n');
                        BitSet bl = lines.get(k.src);
                        if (bl == null) lines.put(k.src, bl = new BitSet());
                        for (int l = m.getFirstLine(); l >= 0 && l <= m.getLastLine(); ++l)
                        {
                            int s = m.getLine(l).getStatus();
                            if (s == ICounter.PARTLY_COVERED || s == ICounter.FULLY_COVERED) bl.set(l);
                        }
                    }
                    if (any) synchronized (k) { k.recs.set(idx); }
                }
                write(new File(recDir, r.name + ".methods.tsv"), ms.toString());
                StringBuilder ls = new StringBuilder();
                for (Map.Entry<String, BitSet> e : lines.entrySet())
                {
                    ls.append(e.getKey()).append('\t').append(ranges(e.getValue())).append('\n');
                    synchronized (runLines)
                    {
                        BitSet u = runLines.get(e.getKey());
                        if (u == null) runLines.put(e.getKey(), u = new BitSet());
                        u.or(e.getValue());
                    }
                }
                write(new File(recDir, r.name + ".lines.tsv"), ls.toString());
                // data of classes this build does not have: the recording ran code its methods file cannot name
                File sf = new File(recDir, r.name + ".stale");
                if (miss > 0) write(sf, miss + " classes of another build\n"); else sf.delete();
                write(kf, key + "\n");
                if (miss > 0) synchronized (mismatch) { mismatch[0] += miss; System.err.println("coverage: " + r.name + ": " + miss + " classes of another build (not counted)"); }
                return null;
            }}));
        }
        for (Future<?> f : fs) f.get();
        ex.shutdown();
        System.err.println("coverage: " + reused[0] + " of " + recs.size() + " recordings' analyses reused (recs/NAME.key)");
        merged.save(new File(cov, "merged.exec"), false);

        // lines
        StringBuilder lt = new StringBuilder("# source\tlines run\tlines with code\tranges run\n");
        for (Map.Entry<String, BitSet> e : codeLines.entrySet())
        {
            BitSet run = runLines.containsKey(e.getKey()) ? runLines.get(e.getKey()) : new BitSet();
            lt.append(e.getKey()).append('\t').append(run.cardinality()).append('\t').append(e.getValue().cardinality())
              .append('\t').append(ranges(run)).append('\n');
        }
        write(new File(cov, "lines.tsv"), lt.toString());

        // the index: scope, status and member statuses per source file
        Map<String, JsonObject> recOf = new HashMap<String, JsonObject>();
        JsonParser jp = new JsonParser();
        List<File> jf = new ArrayList<File>();
        collect(new File(index, "java"), jf);
        for (File f : jf)
        {
            if (!f.getName().endsWith(".java.json")) continue;
            JsonObject o = jp.parse(new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8)).getAsJsonObject();
            String p = o.get("path").getAsString();
            if (p.startsWith("oracle/src/")) recOf.put(p.substring(9), o);
        }

        // methods.tsv, unexecuted.tsv, summary.txt
        // the report is oracle/src's: netherite.* is in the per-recording data only (the self-check's selection, tests/SelfSelect.java)
        for (Iterator<Klass> it = ks.iterator(); it.hasNext(); ) if (it.next().mods) it.remove();
        StringBuilder mt = new StringBuilder("# source\tclass\tmethod\tdesc\tline\tscope\trecordings\tshortest recordings running it\n");
        StringBuilder ct = new StringBuilder(), ut = new StringBuilder();
        TreeMap<String, int[]> byPkg = new TreeMap<String, int[]>();   // in-scope methods run, total; classes never run, in-scope classes
        List<String> deadClasses = new ArrayList<String>();
        TreeMap<String, Integer> byStatus = new TreeMap<String, Integer>();   // unexecuted in-scope methods by record and member status
        int noRecord = 0;
        Set<String> noRecordSrc = new TreeSet<String>();
        Map<String, BitSet> fileRecs = new HashMap<String, BitSet>();
        for (Klass k : ks)
        {
            BitSet u = fileRecs.get(k.src);
            if (u == null) fileRecs.put(k.src, u = new BitSet());
            u.or(k.recs);
        }
        Collections.sort(ks, new Comparator<Klass>() { public int compare(Klass x, Klass y)
        {
            int c = x.pkg.compareTo(y.pkg);
            if (c != 0) return c;
            c = x.src.compareTo(y.src);
            return c != 0 ? c : x.name.compareTo(y.name);
        }});
        for (Klass k : ks)
        {
            JsonObject o = recOf.get(k.src);
            String scope = o == null ? "no_record" : o.get("scope").getAsString();
            if (o == null) { ++noRecord; noRecordSrc.add(k.src); }
            boolean in = "in_scope".equals(scope);
            String status = o == null ? "-" : o.get("status").getAsString();
            int[] pc = null;
            if (in)
            {
                pc = byPkg.get(k.pkg.isEmpty() ? "(root)" : k.pkg);
                if (pc == null) byPkg.put(k.pkg.isEmpty() ? "(root)" : k.pkg, pc = new int[4]);
                pc[3]++;
            }
            Collections.sort(k.methods, new Comparator<Method>() { public int compare(Method x, Method y) { return x.line != y.line ? x.line - y.line : (x.name + x.desc).compareTo(y.name + y.desc); } });
            String simple = k.name.substring(k.name.lastIndexOf('/') + 1);
            for (Method m : k.methods)
            {
                mt.append(k.src).append('\t').append(simple).append('\t').append(m.name).append('\t').append(m.desc).append('\t')
                  .append(m.line).append('\t').append(scope).append('\t').append(m.recs.cardinality()).append('\t')
                  .append(shortest(recs, m.recs, 5)).append('\n');
                if (!in) continue;
                pc[1]++;
                if (!m.recs.isEmpty()) { pc[0]++; continue; }
                String ms = memberStatus(o, simple, m.name, m.desc);
                String sk = "record " + status + ", member " + ms;
                Integer c0 = byStatus.get(sk);
                byStatus.put(sk, c0 == null ? 1 : c0 + 1);
                if (k.recs.isEmpty()) continue;   // the whole class is listed as one row
                ut.append("method\t").append(k.pkg).append('\t').append(k.src).append('\t').append(simple).append('\t')
                  .append(m.name).append('\t').append(m.desc).append('\t').append(m.line).append('\t').append(status).append('\t')
                  .append(ms).append('\t').append(k.recs.cardinality()).append('\t')
                  .append(shortest(recs, k.recs, 6)).append('\n');
            }
            if (in && k.recs.isEmpty() && !k.methods.isEmpty())
            {
                pc[2]++;
                BitSet fr = fileRecs.get(k.src);
                deadClasses.add(k.pkg + "\t" + simple + "\t" + k.methods.size() + "\t" + status + "\t" + fr.cardinality());
                ct.append("class\t").append(k.pkg).append('\t').append(k.src).append('\t').append(simple).append('\t')
                  .append("*\t").append(k.methods.size()).append(" methods\t").append(k.methods.get(0).line).append('\t').append(status)
                  .append('\t').append(classStatus(o, simple)).append("\t0\t").append(fr.isEmpty() ? "(no class of its file runs)" : "file: " + fr.cardinality() + " " + shortest(recs, fr, 6)).append('\n');
            }
        }
        write(new File(cov, "methods.tsv"), mt.toString());
        write(new File(cov, "unexecuted.tsv"),
              "# kind\tpackage\tsource\tclass\tmethod\tdesc\tline\trecord status\tmember status\trecordings running the class\tshortest of them\n"
              + ct + ut);

        StringBuilder sm = new StringBuilder();
        int run = 0, tot = 0, dead = 0, inCls = 0;
        for (int[] c : byPkg.values()) { run += c[0]; tot += c[1]; dead += c[2]; inCls += c[3]; }
        int ok = 0;
        StringBuilder bad = new StringBuilder();
        for (Rec r : recs) { if ("OK".equals(r.status)) ++ok; else bad.append("  ").append(r.name).append(' ').append(r.status).append('\n'); }
        sm.append(String.format("java coverage over %d recordings (%d replayed OK)%n", recs.size(), ok));
        if (bad.length() > 0) sm.append("not OK (their data runs to the divergence):\n").append(bad);
        sm.append(String.format("in-scope methods run: %d / %d (%.1f%%); in-scope classes none of whose methods run: %d of %d%n",
                                run, tot, 100.0 * run / Math.max(1, tot), dead, inCls));
        int allRun = 0, all = 0;
        for (Klass k : ks) for (Method m : k.methods) { ++all; if (!m.recs.isEmpty()) ++allRun; }
        sm.append(String.format("all methods of src/ run: %d / %d%n", allRun, all));
        if (noRecord > 0) sm.append("classes whose source has no index record: " + noRecord + " " + noRecordSrc + "\n");
        if (mismatch[0] > 0) sm.append("classes whose data is another build's (not counted): " + mismatch[0] + "\n");
        sm.append("\nin-scope methods no recording runs, by their record's status and their member's (\"-\": the record names no such member):\n");
        for (Map.Entry<String, Integer> e : byStatus.entrySet()) sm.append(String.format("  %5d  %s%n", e.getValue(), e.getKey()));
        sm.append("\nin-scope classes that no recording runs (package, class, methods, record status, recordings running its file);\n"
                  + "anonymous classes (Minecraft$3: crash report sections, comparators and the like) are counted after them:\n");
        int anon = 0, anonM = 0;
        for (String d : deadClasses)
        {
            String c = d.split("\t")[1];
            if (c.matches(".*\\$[0-9]+")) { ++anon; anonM += Integer.parseInt(d.split("\t")[2]); continue; }
            sm.append("  ").append(d.replace('\t', ' ')).append('\n');
        }
        sm.append("  and " + anon + " anonymous classes (" + anonM + " methods): unexecuted.tsv\n");
        sm.append("\nin-scope methods by package: run / total, classes never run / in-scope classes\n");
        for (Map.Entry<String, int[]> e : byPkg.entrySet())
        {
            int[] c = e.getValue();
            sm.append(String.format("  %-40s %5d / %5d  %5.1f%%   %3d / %4d%n", e.getKey(), c[0], c[1], 100.0 * c[0] / Math.max(1, c[1]), c[2], c[3]));
        }
        write(new File(cov, "summary.txt"), sm.toString());
        System.out.print(sm);
    }

    static int compare(File classes, File fa, File fb) throws IOException
    {
        ExecFileLoader la = new ExecFileLoader(), lb = new ExecFileLoader();
        la.load(fa);
        lb.load(fb);
        ExecutionDataStore sa = la.getExecutionDataStore(), sb = lb.getExecutionDataStore();
        List<File> files = new ArrayList<File>();
        collect(new File(classes, "net/minecraft"), files);
        collect(new File(classes, "netherite"), files);
        Collections.sort(files);
        TreeSet<String> ma = new TreeSet<String>(), mb = new TreeSet<String>();
        int probeDiff = 0, classes0 = 0, other = 0;
        Set<Long> ids = new HashSet<Long>();
        for (File f : files)
        {
            byte[] b = Files.readAllBytes(f.toPath());
            long id = CRC64.classId(b);
            ids.add(id);
            ExecutionData da = sa.get(id), db = sb.get(id);
            boolean ha = da != null && da.hasHits(), hb = db != null && db.hasHits();
            if (!ha && !hb) continue;
            ++classes0;
            if (!(ha && hb && Arrays.equals(da.getProbes(), db.getProbes())))
            {
                ++probeDiff;
                if (probeDiff <= 20) System.out.println("probes differ: " + (ha ? da.getName() : db.getName()) + " (" + (ha ? da.getProbes().length : 0) + " probes)");
            }
            for (int side = 0; side < 2; ++side)
            {
                ExecutionDataStore st = new ExecutionDataStore();
                ExecutionData d = side == 0 ? da : db;
                if (d == null || !d.hasHits()) continue;
                st.put(d);
                CoverageBuilder cb = new CoverageBuilder();
                new Analyzer(st, cb).analyzeClass(b, f.getPath());
                for (IClassCoverage c : cb.getClasses())
                    for (IMethodCoverage m : c.getMethods())
                        if (m.getMethodCounter().getCoveredCount() > 0) (side == 0 ? ma : mb).add(c.getName() + "." + m.getName() + m.getDesc());
            }
        }
        for (ExecutionData d : sa.getContents()) if (d.hasHits() && !ids.contains(d.getId()) && d.getName().startsWith("net/minecraft/")) ++other;
        for (ExecutionData d : sb.getContents()) if (d.hasHits() && !ids.contains(d.getId()) && d.getName().startsWith("net/minecraft/")) ++other;
        System.out.println(probeDiff == 0 ? "probes equal: " + classes0 + " classes run" : "probes differ in " + probeDiff + " of " + classes0 + " classes run");
        if (other > 0) System.out.println("classes of another build: " + other);
        TreeSet<String> onlyA = new TreeSet<String>(ma), onlyB = new TreeSet<String>(mb);
        onlyA.removeAll(mb);
        onlyB.removeAll(ma);
        for (String s : onlyA) System.out.println("only " + fa.getName() + ": " + s);
        for (String s : onlyB) System.out.println("only " + fb.getName() + ": " + s);
        if (onlyA.isEmpty() && onlyB.isEmpty()) System.out.println("methods equal: " + ma.size() + " run");
        else System.out.println("methods differ: " + onlyA.size() + " only in " + fa.getName() + ", " + onlyB.size() + " only in " + fb.getName() + " (of " + ma.size() + ", " + mb.size() + ")");
        return onlyA.isEmpty() && onlyB.isEmpty() && other == 0 ? 0 : 1;
    }

    static String hex(byte[] b)
    {
        StringBuilder s = new StringBuilder();
        for (byte x : b) s.append(String.format("%02x", x & 0xff));
        return s.toString();
    }

    static byte[] bytes(ExecFileLoader ld) throws IOException
    {
        ByteArrayOutputStream o = new ByteArrayOutputStream();
        ld.save(o);
        return o.toByteArray();
    }

    /** The statuses of the record's members named NAME (a ctor for <init>: named after the class, <init>
     *  or ctor), distinct, joined by "/"; "-" when none. A name qualified with the class ("Outer$1.m",
     *  "Inner.m", "Inner.ctor") beats an unqualified one, which beats one qualified with another class.
     *  Overloads with different statuses are narrowed to the members whose sig has DESC's parameter
     *  types, else its parameter count, when any has. */
    static String memberStatus(JsonObject o, String cls, String name, String desc)
    {
        if (o == null || !o.has("members")) return "-";
        String inner = cls.substring(cls.lastIndexOf('$') + 1);
        List<String> hs = new ArrayList<String>();
        List<List<String>> hp = new ArrayList<List<String>>();
        List<Integer> hr = new ArrayList<Integer>();
        int best = -1;
        for (JsonElement e : o.getAsJsonArray("members"))
        {
            JsonObject m = e.getAsJsonObject();
            String kind = m.has("kind") ? m.get("kind").getAsString() : "";
            if ("field".equals(kind)) continue;
            String nm = m.has("name") ? m.get("name").getAsString() : "";
            int rank = -1;
            if ("<init>".equals(name))
            {
                if ("ctor".equals(kind) && (nm.startsWith(cls) || nm.startsWith("<init>") || nm.contains(inner) || "ctor".equals(nm)))
                {
                    String q = nm.endsWith(".ctor") ? nm.substring(0, nm.length() - 5) : nm;
                    rank = q.equals(cls) || q.equals(inner) || cls.endsWith("$" + q) ? 2 : 1;
                }
            }
            else if (!"ctor".equals(kind))
                for (String part : nm.split("\\s*[/,]\\s*"))
                {
                    String p = part.trim();
                    int sp = p.indexOf(' '), pa = p.indexOf('(');
                    int cut = sp < 0 ? pa : pa < 0 ? sp : Math.min(sp, pa);
                    if (cut >= 0) p = p.substring(0, cut);
                    int dot = p.lastIndexOf('.');
                    String q = dot >= 0 ? p.substring(0, dot) : null;
                    if (dot >= 0) p = p.substring(dot + 1);
                    if (p.equals(name)) rank = Math.max(rank, q == null ? 1 : q.equals(cls) || cls.endsWith("$" + q) ? 2 : 0);
                }
            if (rank < 0) continue;
            best = Math.max(best, rank);
            hs.add(m.has("status") ? m.get("status").getAsString() : "?");
            hp.add(m.has("sig") && !m.get("sig").isJsonNull() ? params(m.get("sig").getAsString()) : null);
            hr.add(rank);
        }
        if (hs.isEmpty()) return "-";
        List<String> want = params(desc);
        Set<String> keep = null;
        for (int pass = 0; pass < 3; ++pass)
        {
            Set<String> k = new LinkedHashSet<String>();
            for (int h = 0; h < hs.size(); ++h)
                if (hr.get(h) == best && (pass == 2 || want != null && hp.get(h) != null && (pass == 0 ? sameParams(hp.get(h), want) : sameCount(hp.get(h), want))))
                    k.add(hs.get(h));
            if (!k.isEmpty() && (keep == null || k.size() < keep.size())) keep = k;
            if (keep != null && keep.size() == 1) break;
        }
        StringBuilder b = new StringBuilder();
        for (String s : keep) b.append(b.length() > 0 ? "/" : "").append(s);
        return b.toString();
    }

    /** A record sig's parameters against a descriptor's; a trailing bare "..." (simpleType's "[]") stands for the rest. */
    static boolean sameParams(List<String> sig, List<String> want)
    {
        int n = sig.size();
        if (n > 0 && "[]".equals(sig.get(n - 1)))
            return want.size() >= n - 1 && sig.subList(0, n - 1).equals(want.subList(0, n - 1));
        return sig.equals(want);
    }

    static boolean sameCount(List<String> sig, List<String> want)
    {
        int n = sig.size();
        return n > 0 && "[]".equals(sig.get(n - 1)) ? want.size() >= n - 1 : n == want.size();
    }

    /** A never-run class's row: the statuses of the record's members named CLS or CLS.member, "-" when none. */
    static String classStatus(JsonObject o, String cls)
    {
        if (o == null || !o.has("members")) return "-";
        Set<String> st = new LinkedHashSet<String>();
        for (JsonElement e : o.getAsJsonArray("members"))
        {
            JsonObject m = e.getAsJsonObject();
            String nm = m.has("name") ? m.get("name").getAsString() : "";
            if (nm.equals(cls) || nm.startsWith(cls + ".")) st.add(m.has("status") ? m.get("status").getAsString() : "?");
        }
        if (st.isEmpty()) return "-";
        StringBuilder b = new StringBuilder();
        for (String s : st) b.append(b.length() > 0 ? "/" : "").append(s);
        return b.toString();
    }

    /** The parameter types, by lower-case simple name, of a JVM descriptor ("(IDLjava/lang/String;)V")
     *  or of an index sig ("(int,String)V", "(LDataInput;I)V", "(PacketBuffer)V"); null when it cannot tell. */
    static List<String> params(String sig)
    {
        // the parenthesis that closes the list: a sig may end in an annotation ("(Entity,...)V (bridge)")
        int a = sig.indexOf('('), z = a < 0 ? -1 : sig.indexOf(')', a);
        if (a < 0 || z < a) return null;
        String in = sig.substring(a + 1, z).trim();
        List<String> out = new ArrayList<String>();
        if (in.isEmpty()) return out;
        if (in.indexOf(',') >= 0)
        {
            for (String t : in.split(",")) out.add(simpleType(t.trim()));
            return out;
        }
        int i = 0;
        while (i < in.length())
        {
            int dims = 0;
            while (i < in.length() && in.charAt(i) == '[') { ++i; ++dims; }
            if (i >= in.length()) return null;
            char c = in.charAt(i);
            int semi = in.indexOf(';', i), k = "ZBCSIJFD".indexOf(c);
            String t;
            if (k >= 0) { t = new String[] { "boolean", "byte", "char", "short", "int", "long", "float", "double" }[k]; ++i; }
            else if (c == 'L' && semi > i) { t = simpleType(in.substring(i + 1, semi)); i = semi + 1; }
            else break;
            for (int d = 0; d < dims; ++d) t += "[]";
            out.add(t);
        }
        if (i >= in.length()) return out;
        if (!in.matches("[A-Za-z_$][\\w.$<>/]*(\\[\\]|\\.\\.\\.)*")) return null;
        out.clear();
        out.add(simpleType(in));   // one type by its simple name
        return out;
    }

    static String simpleType(String t)
    {
        t = t.replaceAll("<.*>", "").replace("...", "[]").trim();
        int cut = Math.max(t.lastIndexOf('/'), Math.max(t.lastIndexOf('.'), t.lastIndexOf('$')));
        return t.substring(cut + 1).toLowerCase(Locale.ROOT);
    }

    /** Up to N of the recordings in SET, the shortest (fewest rows) first. */
    static String shortest(List<Rec> recs, BitSet set, int n)
    {
        List<Rec> in = new ArrayList<Rec>();
        for (int i = set.nextSetBit(0); i >= 0; i = set.nextSetBit(i + 1)) in.add(recs.get(i));
        Collections.sort(in, new Comparator<Rec>() { public int compare(Rec x, Rec y) { return x.rows != y.rows ? Integer.compare(x.rows, y.rows) : x.name.compareTo(y.name); } });
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < in.size() && i < n; ++i) b.append(i > 0 ? " " : "").append(in.get(i).name);
        return b.length() == 0 ? "-" : b.toString();
    }

    static String ranges(BitSet b)
    {
        StringBuilder s = new StringBuilder();
        for (int i = b.nextSetBit(0); i >= 0; )
        {
            int j = b.nextClearBit(i) - 1;
            s.append(s.length() > 0 ? "," : "").append(i);
            if (j > i) s.append('-').append(j);
            i = b.nextSetBit(j + 1);
        }
        return s.toString();
    }

    static void collect(File d, List<File> out)
    {
        File[] fs = d.listFiles();
        if (fs == null) return;
        for (File f : fs)
        {
            if (f.isDirectory()) collect(f, out);
            else if (f.getName().endsWith(".class") || f.getName().endsWith(".json")) out.add(f);
        }
    }

    static void write(File f, String s) throws IOException
    {
        File t = new File(f.getPath() + ".part");
        Files.write(t.toPath(), s.getBytes(StandardCharsets.UTF_8));
        Files.move(t.toPath(), f.toPath(), StandardCopyOption.REPLACE_EXISTING);
    }
}
