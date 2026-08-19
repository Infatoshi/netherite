package netherite.bootstraporacle;

import netherite.fetchassets.FetchMcAssets;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

/**
 * Regenerate {@code java/oracle-src} from a local ForgeGradle decompile of
 * Minecraft 1.11.2. Stdlib only. Invokes the Gradle wrapper main class with
 * Java (never {@code gradlew}). Retry and sleep live here.
 *
 * <p>Usage: {@code java netherite.bootstraporacle.BootstrapOracle <repo-root>}
 */
public final class BootstrapOracle {
    public static final int MIN_JAVA_FILES = 2600;
    public static final int MAX_ATTEMPTS = 5;

    /** Seconds to sleep after a failed attempt {@code attempt} (1-based). */
    public static int retryDelaySeconds(int attempt) {
        return attempt * 20;
    }

    public interface Sleeper {
        void sleepMillis(long ms) throws InterruptedException;
    }

    public interface ProcessRunner {
        int run(List<String> command, File workingDir, File javaHome) throws Exception;
    }

    public static final class Config {
        public File repoRoot;
        public File javaHome;
        public int minJavaFiles = MIN_JAVA_FILES;
        public int maxAttempts = MAX_ATTEMPTS;
        public Sleeper sleeper = new Sleeper() {
            @Override
            public void sleepMillis(long ms) throws InterruptedException {
                Thread.sleep(ms);
            }
        };
        public ProcessRunner processRunner = new ProcessRunner() {
            @Override
            public int run(List<String> command, File workingDir, File javaHome)
                    throws Exception {
                return runProcess(command, workingDir, javaHome);
            }
        };
        /** When non-null, used instead of invoking Gradle (tests). */
        public Boolean forceDecompPresent;
        /** When true, never invoke Gradle or asset fetch (tests / sync-only). */
        public boolean syncOnly;
    }

    public static final class Result {
        public final int javaFiles;
        public final boolean ranGradle;
        public final File destNet;

        Result(int javaFiles, boolean ranGradle, File destNet) {
            this.javaFiles = javaFiles;
            this.ranGradle = ranGradle;
            this.destNet = destNet;
        }
    }

    public static void main(String[] args) {
        if (args.length != 1) {
            System.err.println(
                    "Usage: java netherite.bootstraporacle.BootstrapOracle <repo-root>");
            System.exit(2);
        }
        Config cfg = new Config();
        cfg.repoRoot = new File(args[0]);
        String jh = System.getenv("JAVA_HOME");
        if (jh != null && jh.length() > 0) {
            cfg.javaHome = new File(jh);
        }
        try {
            Result r = run(cfg);
            System.out.println(
                    "oracle-src ready: " + r.javaFiles + " java files"
                            + (r.ranGradle ? " (decomp + sync)" : " (sync only)"));
        } catch (Exception e) {
            System.err.println("ERROR: " + e.getMessage());
            e.printStackTrace(System.err);
            System.exit(1);
        }
    }

    public static Result run(Config cfg) throws Exception {
        if (cfg == null || cfg.repoRoot == null) {
            throw new IllegalArgumentException("repoRoot required");
        }
        requireJava8();

        File repo = cfg.repoRoot.getCanonicalFile();
        if (!repo.isDirectory()) {
            throw new IOException("repo root is not a directory: " + repo);
        }

        File mcw = new File(repo, "java/Minecraft");
        File srcNet = new File(mcw, "build/tmp/recompileMc/sources/net");
        File dstRoot = new File(repo, "java/oracle-src");
        File dstNet = new File(dstRoot, "net");
        File wrapperJar = new File(mcw, "gradle/wrapper/gradle-wrapper.jar");
        File javaHome = cfg.javaHome != null
                ? cfg.javaHome
                : new File(System.getProperty("java.home"));

        boolean decompPresent;
        if (cfg.forceDecompPresent != null) {
            decompPresent = cfg.forceDecompPresent.booleanValue();
        } else {
            decompPresent = isDecompPresent(srcNet);
        }

        boolean ranGradle = false;
        if (!decompPresent) {
            if (cfg.syncOnly) {
                throw new IOException(
                        "decomp output missing at " + srcNet
                                + " and syncOnly is set");
            }
            if (!wrapperJar.isFile()) {
                throw new IOException("gradle wrapper jar missing: " + wrapperJar);
            }

            System.out.println(
                    "== pre-seeding MC asset cache (https; FG's http path is dead) ==");
            FetchMcAssets.Config fa = new FetchMcAssets.Config();
            fa.assetsDir = new File(mcw, "run/gradle/caches/minecraft/assets");
            FetchMcAssets.run(fa);

            System.out.println(
                    "== ForgeGradle setupDecompWorkspace (downloads + decompiles MC 1.11.2) ==");
            File javaBin = new File(javaHome, "bin/java");
            if (!javaBin.isFile()) {
                // Some JDKs use jre/ nested; fall back to current runtime.
                javaBin = new File(System.getProperty("java.home"), "bin/java");
            }
            List<String> cmd = gradleSetupCommand(javaBin, wrapperJar);
            boolean ok = false;
            for (int attempt = 1; attempt <= cfg.maxAttempts; attempt++) {
                System.out.println(
                        "== setupDecompWorkspace attempt " + attempt + "/"
                                + cfg.maxAttempts + " ==");
                int code = cfg.processRunner.run(cmd, mcw, javaHome);
                if (code == 0) {
                    ok = true;
                    break;
                }
                int delay = retryDelaySeconds(attempt);
                System.out.println(
                        "WARN: setupDecompWorkspace failed (often JitPack timeout)."
                                + " Retrying in " + delay + "s...");
                cfg.sleeper.sleepMillis(delay * 1000L);
            }
            if (!ok) {
                throw new IOException(
                        "setupDecompWorkspace failed after retries.\n"
                                + "  JitPack hosts com.github.brandonhoughton:"
                                + "ForgeGradle:FG_2.2_patched-SNAPSHOT.\n"
                                + "  If jitpack.io is down, retry later, or copy a"
                                + " known-good\n"
                                + "  java/Minecraft/run/gradle/caches/modules-2 tree"
                                + " from another machine.");
            }
            ranGradle = true;
        }

        if (!isDecompPresent(srcNet)) {
            throw new IOException("decomp output missing at " + srcNet);
        }

        File javaPath = new File(repo, "java");
        rejectSymbolicLink(javaPath, "java directory");
        File javaDir = javaPath.getCanonicalFile();
        if (!javaDir.isDirectory()) {
            throw new IOException("java directory missing: " + javaDir);
        }
        assertUnder(repo, javaDir);
        rejectSymbolicLink(dstRoot, "oracle destination");
        rejectSymbolicLink(dstNet, "oracle net destination");
        // Reject any other path whose canonical form escapes repo java/.
        assertUnder(javaDir, dstRoot.getCanonicalFile());
        assertUnder(javaDir, dstNet.getCanonicalFile());

        // Count source first: never copy/delete dest when the tree is too small.
        int srcCount = countJavaFiles(srcNet);
        if (srcCount < cfg.minJavaFiles) {
            throw new IOException(
                    "expected >=" + cfg.minJavaFiles + " files, got " + srcCount);
        }

        System.out.println("== populating java/oracle-src ==");
        if (!dstRoot.exists() && !dstRoot.mkdirs()) {
            throw new IOException("cannot create " + dstRoot);
        }
        // Re-check after create in case a race or odd FS layout appeared.
        rejectSymbolicLink(dstRoot, "oracle destination");
        rejectSymbolicLink(dstNet, "oracle net destination");
        assertUnder(javaDir, dstRoot.getCanonicalFile());
        assertUnder(javaDir, dstNet.getCanonicalFile());

        int n = mirrorNet(srcNet, dstNet);
        System.out.println("oracle-src ready: " + n + " java files");
        if (n < cfg.minJavaFiles) {
            throw new IOException(
                    "expected >=" + cfg.minJavaFiles + " files, got " + n);
        }
        return new Result(n, ranGradle, dstNet.getCanonicalFile());
    }

    /** Fail unless the running JVM is Java 1.8. */
    public static void requireJava8() {
        String v = System.getProperty("java.version");
        if (v == null || !v.startsWith("1.8")) {
            throw new IllegalStateException(
                    "need Java 1.8, got " + (v == null ? "(null)" : v));
        }
    }

    public static boolean isDecompPresent(File srcNet) {
        if (srcNet == null) {
            return false;
        }
        File mc = new File(srcNet, "minecraft");
        return mc.isDirectory();
    }

    /**
     * Build the exact Gradle setup command: Java + wrapper jar + main class.
     * Working directory must be {@code java/Minecraft}; {@code -g run/gradle}
     * is relative to that directory.
     */
    public static List<String> gradleSetupCommand(File javaBin, File wrapperJar) {
        if (javaBin == null || wrapperJar == null) {
            throw new IllegalArgumentException("javaBin and wrapperJar required");
        }
        List<String> cmd = new ArrayList<String>(8);
        cmd.add(javaBin.getPath());
        cmd.add("-classpath");
        cmd.add(wrapperJar.getPath());
        cmd.add("org.gradle.wrapper.GradleWrapperMain");
        cmd.add("-g");
        cmd.add("run/gradle");
        cmd.add("setupDecompWorkspace");
        cmd.add("--stacktrace");
        return Collections.unmodifiableList(cmd);
    }

    /**
     * Mirror {@code srcNet/} onto {@code dstNet/} (create dest as needed).
     * Delete destination entries that are not present in the source.
     * Never delete or write outside the destination tree.
     *
     * @return count of {@code *.java} files under dest after sync
     */
    public static int mirrorNet(File srcNet, File dstNet) throws IOException {
        if (srcNet == null || dstNet == null) {
            throw new IllegalArgumentException("srcNet and dstNet required");
        }
        File src = srcNet.getCanonicalFile();
        if (!src.isDirectory()) {
            throw new IOException("source net tree missing: " + src);
        }
        File dst = dstNet.getCanonicalFile();
        if (!dst.exists()) {
            if (!dst.mkdirs()) {
                throw new IOException("cannot create dest: " + dst);
            }
            dst = dst.getCanonicalFile();
        } else if (!dst.isDirectory()) {
            throw new IOException("dest is not a directory: " + dst);
        }

        copyTree(src, dst, dst);
        deleteStale(src, dst, dst);
        return countJavaFiles(dst);
    }

    /** Count {@code *.java} files under {@code root} (recursive). */
    public static int countJavaFiles(File root) throws IOException {
        if (root == null || !root.isDirectory()) {
            return 0;
        }
        return countJavaFilesRec(root.getCanonicalFile());
    }

    /**
     * Ensure {@code child} is the same as or strictly under {@code root}.
     * Used so delete/copy never touch paths outside the destination tree.
     */
    public static void assertUnder(File root, File child) throws IOException {
        if (root == null || child == null) {
            throw new IllegalArgumentException("root and child required");
        }
        String r = root.getCanonicalPath();
        String c = child.getCanonicalPath();
        if (c.equals(r)) {
            return;
        }
        String prefix = r.endsWith(File.separator) ? r : r + File.separator;
        if (!c.startsWith(prefix)) {
            throw new IOException(
                    "path escapes destination tree: " + child
                            + " (root=" + root + ")");
        }
    }

    static void rejectSymbolicLink(File path, String label) throws IOException {
        if (path != null && Files.isSymbolicLink(path.toPath())) {
            throw new IOException(label + " is a symbolic link: " + path);
        }
    }

    // --- internals ---

    private static int countJavaFilesRec(File dir) {
        int n = 0;
        File[] kids = dir.listFiles();
        if (kids == null) {
            return 0;
        }
        for (File k : kids) {
            if (k.isDirectory()) {
                n += countJavaFilesRec(k);
            } else if (k.isFile() && k.getName().toLowerCase(Locale.ROOT).endsWith(".java")) {
                n++;
            }
        }
        return n;
    }

    private static void copyTree(File srcDir, File dstDir, File dstRoot)
            throws IOException {
        assertUnder(dstRoot, dstDir);
        // Source directory must replace a destination file of the same name.
        if (dstDir.exists() && !dstDir.isDirectory()) {
            deleteTreeUnder(dstDir, dstRoot);
        }
        if (!dstDir.exists() && !dstDir.mkdirs()) {
            throw new IOException("cannot mkdir " + dstDir);
        }
        File[] kids = srcDir.listFiles();
        if (kids == null) {
            return;
        }
        for (File sk : kids) {
            File dk = new File(dstDir, sk.getName());
            assertUnder(dstRoot, dk);
            if (sk.isDirectory()) {
                copyTree(sk, dk, dstRoot);
            } else if (sk.isFile()) {
                // Source file must replace a destination directory of the same name.
                if (dk.exists() && dk.isDirectory()) {
                    deleteTreeUnder(dk, dstRoot);
                }
                copyFile(sk, dk);
            }
        }
    }

    private static void copyFile(File src, File dst) throws IOException {
        File parent = dst.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) {
            throw new IOException("cannot mkdir " + parent);
        }
        if (dst.exists() && dst.isDirectory()) {
            throw new IOException("dest is a directory, expected file: " + dst);
        }
        InputStream in = new FileInputStream(src);
        try {
            OutputStream out = new FileOutputStream(dst);
            try {
                byte[] buf = new byte[64 * 1024];
                int n;
                while ((n = in.read(buf)) >= 0) {
                    out.write(buf, 0, n);
                }
            } finally {
                out.close();
            }
        } finally {
            in.close();
        }
        long mt = src.lastModified();
        if (mt > 0L) {
            //noinspection ResultOfMethodCallIgnored
            dst.setLastModified(mt);
        }
    }

    /**
     * Delete files and directories under {@code dstDir} that have no counterpart
     * under {@code srcDir}. Never touches anything outside {@code dstRoot}.
     */
    static void deleteStale(File srcDir, File dstDir, File dstRoot)
            throws IOException {
        assertUnder(dstRoot, dstDir);
        File[] kids = dstDir.listFiles();
        if (kids == null) {
            return;
        }
        Set<String> srcNames = listNames(srcDir);
        for (File dk : kids) {
            assertUnder(dstRoot, dk);
            File sk = new File(srcDir, dk.getName());
            if (!srcNames.contains(dk.getName())) {
                deleteTreeUnder(dk, dstRoot);
                continue;
            }
            if (dk.isDirectory()) {
                if (sk.isDirectory()) {
                    deleteStale(sk, dk, dstRoot);
                } else {
                    // type mismatch: replace by removing dest entry
                    deleteTreeUnder(dk, dstRoot);
                }
            } else if (dk.isFile() && sk.isDirectory()) {
                deleteTreeUnder(dk, dstRoot);
            }
        }
    }

    private static Set<String> listNames(File dir) {
        Set<String> names = new LinkedHashSet<String>();
        if (dir == null || !dir.isDirectory()) {
            return names;
        }
        File[] kids = dir.listFiles();
        if (kids == null) {
            return names;
        }
        for (File k : kids) {
            names.add(k.getName());
        }
        return names;
    }

    static void deleteTreeUnder(File f, File dstRoot) throws IOException {
        assertUnder(dstRoot, f);
        if (f.isDirectory()) {
            File[] kids = f.listFiles();
            if (kids != null) {
                for (File k : kids) {
                    deleteTreeUnder(k, dstRoot);
                }
            }
        }
        assertUnder(dstRoot, f);
        if (!f.delete() && f.exists()) {
            throw new IOException("cannot delete " + f);
        }
    }

    static int runProcess(List<String> command, File workingDir, File javaHome)
            throws Exception {
        ProcessBuilder pb = new ProcessBuilder(command);
        pb.directory(workingDir);
        pb.redirectErrorStream(true);
        if (javaHome != null) {
            pb.environment().put("JAVA_HOME", javaHome.getPath());
        }
        Process p = pb.start();
        InputStream in = p.getInputStream();
        try {
            byte[] buf = new byte[4096];
            int n;
            while ((n = in.read(buf)) >= 0) {
                System.out.write(buf, 0, n);
            }
        } finally {
            in.close();
        }
        return p.waitFor();
    }

    private BootstrapOracle() {}
}
