package netherite.bootstraporacle;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.Charset;
import java.nio.file.Files;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Offline unit tests for BootstrapOracle. Uses temporary system directories.
 * Does not invoke ForgeGradle or the network.
 */
public final class BootstrapOracleTest {
    private static final Charset UTF8 = Charset.forName("UTF-8");
    private static int failures = 0;
    private static int passes = 0;

    public static void main(String[] args) throws Exception {
        testRetryDelay();
        testCommandConstruction();
        testMirrorCopy();
        testStaleFileDeletion();
        testCountFailurePreservesDest();
        testSymlinkDestEscape();
        testSymlinkDestInsideJava();
        testFileReplacesDirectory();
        testDirectoryReplacesFile();
        testPathSafety();
        testSyncOnlyWhenDecompPresent();
        testRequireJava8();

        System.out.println(
                "BootstrapOracleTest: " + passes + " passed, " + failures + " failed");
        if (failures > 0) {
            System.exit(1);
        }
    }

    static void testRetryDelay() {
        check("delay attempt 1", BootstrapOracle.retryDelaySeconds(1) == 20, "1");
        check("delay attempt 2", BootstrapOracle.retryDelaySeconds(2) == 40, "2");
        check("delay attempt 5", BootstrapOracle.retryDelaySeconds(5) == 100, "5");
    }

    static void testCommandConstruction() {
        File javaBin = new File("/usr/lib/jvm/java-8/bin/java");
        File wrapper = new File("/repo/java/Minecraft/gradle/wrapper/gradle-wrapper.jar");
        List<String> cmd = BootstrapOracle.gradleSetupCommand(javaBin, wrapper);
        check("cmd size", cmd.size() == 8, String.valueOf(cmd));
        check("cmd java", javaBin.getPath().equals(cmd.get(0)), cmd.get(0));
        check("cmd -classpath", "-classpath".equals(cmd.get(1)), cmd.get(1));
        check("cmd wrapper jar", wrapper.getPath().equals(cmd.get(2)), cmd.get(2));
        check("cmd main",
                "org.gradle.wrapper.GradleWrapperMain".equals(cmd.get(3)),
                cmd.get(3));
        check("cmd -g", "-g".equals(cmd.get(4)), cmd.get(4));
        check("cmd run/gradle", "run/gradle".equals(cmd.get(5)), cmd.get(5));
        check("cmd task", "setupDecompWorkspace".equals(cmd.get(6)), cmd.get(6));
        check("cmd stacktrace", "--stacktrace".equals(cmd.get(7)), cmd.get(7));
        // Must not reference gradlew
        for (String part : cmd) {
            if (part != null && part.contains("gradlew")) {
                fail("command must not invoke gradlew: " + cmd);
                return;
            }
        }
        pass("no gradlew in command");

        try {
            BootstrapOracle.gradleSetupCommand(null, wrapper);
            fail("null javaBin should throw");
        } catch (IllegalArgumentException e) {
            pass("null javaBin throws");
        }
    }

    static void testMirrorCopy() throws Exception {
        File tmp = tempDir("bo-mirror");
        try {
            File src = new File(tmp, "src/net");
            File dst = new File(tmp, "dst/net");
            writeText(new File(src, "minecraft/A.java"), "class A {}");
            writeText(new File(src, "minecraft/sub/B.java"), "class B {}");
            writeText(new File(src, "minecraftforge/C.java"), "class C {}");
            writeText(new File(src, "minecraft/readme.txt"), "not java");

            int n = BootstrapOracle.mirrorNet(src, dst);
            check("mirror java count", n == 3, String.valueOf(n));
            check("A copied",
                    new File(dst, "minecraft/A.java").isFile(), "missing A");
            check("B copied",
                    new File(dst, "minecraft/sub/B.java").isFile(), "missing B");
            check("C copied",
                    new File(dst, "minecraftforge/C.java").isFile(), "missing C");
            check("txt copied",
                    new File(dst, "minecraft/readme.txt").isFile(), "missing txt");
            check("content A",
                    "class A {}".equals(readText(new File(dst, "minecraft/A.java"))),
                    "bad content");
        } finally {
            deleteTree(tmp);
        }
    }

    static void testStaleFileDeletion() throws Exception {
        File tmp = tempDir("bo-stale");
        try {
            File src = new File(tmp, "src/net");
            File dst = new File(tmp, "dst/net");
            writeText(new File(src, "minecraft/Keep.java"), "keep");
            writeText(new File(dst, "minecraft/Keep.java"), "old");
            writeText(new File(dst, "minecraft/Stale.java"), "stale");
            writeText(new File(dst, "minecraft/old/Gone.java"), "gone");
            // Sibling outside dst must remain untouched
            File outside = new File(tmp, "outside/Important.java");
            writeText(outside, "do not delete");

            int n = BootstrapOracle.mirrorNet(src, dst);
            check("after stale java count", n == 1, String.valueOf(n));
            check("keep updated",
                    "keep".equals(readText(new File(dst, "minecraft/Keep.java"))),
                    "content");
            check("stale file gone",
                    !new File(dst, "minecraft/Stale.java").exists(), "Stale.java");
            check("stale dir gone",
                    !new File(dst, "minecraft/old").exists(), "old/");
            check("outside untouched",
                    outside.isFile()
                            && "do not delete".equals(readText(outside)),
                    outside.getPath());
        } finally {
            deleteTree(tmp);
        }
    }

    /**
     * Source has too few Java files: run() must fail before mirrorNet mutates
     * the destination. A pre-existing dest sentinel and its content stay put.
     */
    static void testCountFailurePreservesDest() throws Exception {
        File tmp = tempDir("bo-count");
        try {
            File repo = new File(tmp, "repo");
            File srcNet = new File(
                    repo, "java/Minecraft/build/tmp/recompileMc/sources/net");
            writeText(new File(srcNet, "minecraft/One.java"), "class One {}");

            File sentinel = new File(
                    repo, "java/oracle-src/net/minecraft/Sentinel.java");
            writeText(sentinel, "sentinel-content");

            BootstrapOracle.Config cfg = new BootstrapOracle.Config();
            cfg.repoRoot = repo;
            cfg.minJavaFiles = 2600;
            cfg.syncOnly = true;
            cfg.forceDecompPresent = Boolean.TRUE;
            try {
                BootstrapOracle.run(cfg);
                fail("low count should throw");
            } catch (IOException e) {
                check("count failure message",
                        e.getMessage() != null
                                && e.getMessage().contains("expected >="),
                        String.valueOf(e.getMessage()));
            }
            check("sentinel still exists after count fail",
                    sentinel.isFile(), sentinel.getPath());
            check("sentinel content intact",
                    "sentinel-content".equals(readText(sentinel)),
                    readText(sentinel));
            check("source file not copied into dest",
                    !new File(repo, "java/oracle-src/net/minecraft/One.java")
                            .isFile(),
                    "One.java should not appear");
        } finally {
            deleteTree(tmp);
        }
    }

    /**
     * A symlink at java/oracle-src pointing outside repo java/ must be
     * rejected; outside sentinel content must not be copied over or deleted.
     * Skips only when the platform cannot create symbolic links.
     */
    static void testSymlinkDestEscape() throws Exception {
        File tmp = tempDir("bo-symlink");
        try {
            File outside = new File(tmp, "outside-target");
            //noinspection ResultOfMethodCallIgnored
            outside.mkdirs();
            File outsideSentinel = new File(outside, "OUTSIDE_SENTINEL.txt");
            writeText(outsideSentinel, "outside-keep");
            File outsideNested = new File(outside, "net/minecraft/Stay.java");
            writeText(outsideNested, "must-not-delete");

            File repo = new File(tmp, "repo");
            File javaDir = new File(repo, "java");
            //noinspection ResultOfMethodCallIgnored
            javaDir.mkdirs();
            File srcNet = new File(
                    repo, "java/Minecraft/build/tmp/recompileMc/sources/net");
            writeText(new File(srcNet, "minecraft/One.java"), "class One {}");

            File link = new File(javaDir, "oracle-src");
            try {
                Files.createSymbolicLink(
                        link.toPath(), outside.getAbsoluteFile().toPath());
            } catch (UnsupportedOperationException e) {
                System.out.println(
                        "  SKIP  symlink dest escape (platform cannot create"
                                + " symlinks)");
                return;
            } catch (IOException e) {
                System.out.println(
                        "  SKIP  symlink dest escape (" + e.getMessage() + ")");
                return;
            }
            if (!Files.isSymbolicLink(link.toPath())) {
                System.out.println(
                        "  SKIP  symlink dest escape (link not created)");
                return;
            }
            pass("created oracle-src symlink to outside");

            BootstrapOracle.Config cfg = new BootstrapOracle.Config();
            cfg.repoRoot = repo;
            cfg.minJavaFiles = 1;
            cfg.syncOnly = true;
            cfg.forceDecompPresent = Boolean.TRUE;
            try {
                BootstrapOracle.run(cfg);
                fail("symlink escape should throw");
            } catch (IOException e) {
                check("symlink escape message",
                        e.getMessage() != null
                                && e.getMessage().contains("symbolic link"),
                        String.valueOf(e.getMessage()));
            }

            check("outside sentinel remains",
                    outsideSentinel.isFile()
                            && "outside-keep".equals(readText(outsideSentinel)),
                    outsideSentinel.getPath());
            check("outside nested remains",
                    outsideNested.isFile()
                            && "must-not-delete".equals(readText(outsideNested)),
                    outsideNested.getPath());
            check("source not written through symlink",
                    !new File(outside, "net/minecraft/One.java").isFile(),
                    "One.java under outside");
        } finally {
            deleteTree(tmp);
        }
    }

    /** A destination symlink is invalid even when it points inside java/. */
    static void testSymlinkDestInsideJava() throws Exception {
        File tmp = tempDir("bo-symlink-inside");
        try {
            File repo = new File(tmp, "repo");
            File javaDir = new File(repo, "java");
            File target = new File(javaDir, "other-oracle");
            File sentinel = new File(target, "net/minecraft/Stay.java");
            writeText(sentinel, "keep");
            File srcNet = new File(
                    repo, "java/Minecraft/build/tmp/recompileMc/sources/net");
            writeText(new File(srcNet, "minecraft/One.java"), "class One {}");

            File link = new File(javaDir, "oracle-src");
            try {
                Files.createSymbolicLink(link.toPath(), target.toPath());
            } catch (UnsupportedOperationException e) {
                System.out.println("  SKIP  inside-java symlink (unsupported)");
                return;
            } catch (IOException e) {
                System.out.println(
                        "  SKIP  inside-java symlink (" + e.getMessage() + ")");
                return;
            }

            BootstrapOracle.Config cfg = new BootstrapOracle.Config();
            cfg.repoRoot = repo;
            cfg.minJavaFiles = 1;
            cfg.syncOnly = true;
            cfg.forceDecompPresent = Boolean.TRUE;
            try {
                BootstrapOracle.run(cfg);
                fail("inside-java destination symlink should throw");
            } catch (IOException e) {
                check("inside-java symlink message",
                        e.getMessage() != null
                                && e.getMessage().contains("symbolic link"),
                        String.valueOf(e.getMessage()));
            }
            check("inside-java sentinel remains",
                    sentinel.isFile() && "keep".equals(readText(sentinel)),
                    sentinel.getPath());
            check("inside-java target not changed",
                    !new File(target, "net/minecraft/One.java").exists(),
                    "One.java under linked target");
        } finally {
            deleteTree(tmp);
        }
    }

    /** Source file replaces a destination directory of the same name. */
    static void testFileReplacesDirectory() throws Exception {
        File tmp = tempDir("bo-f2d");
        try {
            File src = new File(tmp, "src/net");
            File dst = new File(tmp, "dst/net");
            writeText(new File(src, "minecraft/X.java"), "class X {}");
            writeText(new File(dst, "minecraft/X.java/inner.txt"), "was-dir");

            int n = BootstrapOracle.mirrorNet(src, dst);
            check("file-replaces-dir java count", n == 1, String.valueOf(n));
            File x = new File(dst, "minecraft/X.java");
            check("X.java is a file", x.isFile(), "type=" + typeOf(x));
            check("X.java content",
                    "class X {}".equals(readText(x)), readText(x));
            check("inner entry gone",
                    !new File(dst, "minecraft/X.java/inner.txt").exists(),
                    "inner");
        } finally {
            deleteTree(tmp);
        }
    }

    /** Source directory replaces a destination file of the same name. */
    static void testDirectoryReplacesFile() throws Exception {
        File tmp = tempDir("bo-d2f");
        try {
            File src = new File(tmp, "src/net");
            File dst = new File(tmp, "dst/net");
            writeText(new File(src, "minecraft/pkg/Y.java"), "class Y {}");
            writeText(new File(dst, "minecraft/pkg"), "was-a-file");

            int n = BootstrapOracle.mirrorNet(src, dst);
            check("dir-replaces-file java count", n == 1, String.valueOf(n));
            File pkg = new File(dst, "minecraft/pkg");
            check("pkg is a directory", pkg.isDirectory(), "type=" + typeOf(pkg));
            File y = new File(pkg, "Y.java");
            check("Y.java present", y.isFile(), y.getPath());
            check("Y.java content",
                    "class Y {}".equals(readText(y)), readText(y));
        } finally {
            deleteTree(tmp);
        }
    }

    static void testPathSafety() throws Exception {
        File tmp = tempDir("bo-safe");
        try {
            File root = new File(tmp, "dst/net");
            //noinspection ResultOfMethodCallIgnored
            root.mkdirs();
            File rootCanon = root.getCanonicalFile();
            File child = new File(root, "minecraft");
            //noinspection ResultOfMethodCallIgnored
            child.mkdirs();
            BootstrapOracle.assertUnder(rootCanon, child);
            pass("assertUnder accepts child");

            File outside = new File(tmp, "other");
            //noinspection ResultOfMethodCallIgnored
            outside.mkdirs();
            try {
                BootstrapOracle.assertUnder(rootCanon, outside);
                fail("assertUnder should reject outside path");
            } catch (IOException e) {
                check("escape message",
                        e.getMessage() != null
                                && e.getMessage().contains("escapes"),
                        String.valueOf(e.getMessage()));
            }

            // deleteTreeUnder must refuse outside paths
            try {
                BootstrapOracle.deleteTreeUnder(outside, rootCanon);
                fail("deleteTreeUnder outside should throw");
            } catch (IOException e) {
                pass("deleteTreeUnder rejects outside");
            }
            check("outside still exists after rejected delete",
                    outside.isDirectory(), outside.getPath());
        } finally {
            deleteTree(tmp);
        }
    }

    static void testSyncOnlyWhenDecompPresent() throws Exception {
        File tmp = tempDir("bo-sync");
        try {
            File repo = new File(tmp, "repo");
            File srcNet = new File(
                    repo, "java/Minecraft/build/tmp/recompileMc/sources/net");
            // 2600+ files would be huge; lower min for this fixture
            for (int i = 0; i < 5; i++) {
                writeText(new File(srcNet, "minecraft/F" + i + ".java"),
                        "class F" + i + " {}");
            }

            final AtomicInteger gradleCalls = new AtomicInteger(0);
            BootstrapOracle.Config cfg = new BootstrapOracle.Config();
            cfg.repoRoot = repo;
            cfg.minJavaFiles = 5;
            cfg.syncOnly = false;
            cfg.forceDecompPresent = Boolean.TRUE;
            cfg.processRunner = new BootstrapOracle.ProcessRunner() {
                @Override
                public int run(List<String> command, File workingDir, File javaHome) {
                    gradleCalls.incrementAndGet();
                    return 0;
                }
            };
            BootstrapOracle.Result r = BootstrapOracle.run(cfg);
            check("sync java count", r.javaFiles == 5, String.valueOf(r.javaFiles));
            check("did not run gradle", !r.ranGradle, "ranGradle");
            check("gradle process never called",
                    gradleCalls.get() == 0, String.valueOf(gradleCalls.get()));
            check("dest present",
                    new File(repo, "java/oracle-src/net/minecraft/F0.java").isFile(),
                    "dest");
        } finally {
            deleteTree(tmp);
        }
    }

    static void testRequireJava8() {
        // Running under the Makefile's Java 8; should not throw.
        try {
            BootstrapOracle.requireJava8();
            pass("requireJava8 accepts 1.8");
        } catch (IllegalStateException e) {
            fail("requireJava8 rejected running JVM: " + e.getMessage());
        }
        String v = System.getProperty("java.version");
        check("java.version starts with 1.8",
                v != null && v.startsWith("1.8"),
                String.valueOf(v));
    }

    // --- helpers ---

    static void check(String name, boolean ok, String detail) {
        if (ok) {
            pass(name);
        } else {
            fail(name + " :: " + detail);
        }
    }

    static void pass(String name) {
        passes++;
        System.out.println("  PASS  " + name);
    }

    static void fail(String name) {
        failures++;
        System.out.println("  FAIL  " + name);
    }

    static File tempDir(String prefix) throws IOException {
        File d = File.createTempFile(prefix + "-", ".d");
        if (!d.delete() || !d.mkdir()) {
            throw new IOException("temp dir: " + d);
        }
        return d;
    }

    static void writeText(File f, String text) throws IOException {
        File parent = f.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) {
            throw new IOException("mkdir " + parent);
        }
        FileOutputStream out = new FileOutputStream(f);
        try {
            out.write(text.getBytes(UTF8));
        } finally {
            out.close();
        }
    }

    static String readText(File f) throws IOException {
        java.io.FileInputStream in = new java.io.FileInputStream(f);
        try {
            byte[] buf = new byte[(int) f.length()];
            int off = 0;
            while (off < buf.length) {
                int n = in.read(buf, off, buf.length - off);
                if (n < 0) {
                    break;
                }
                off += n;
            }
            return new String(buf, 0, off, UTF8);
        } finally {
            in.close();
        }
    }

    static String typeOf(File f) {
        if (f == null) {
            return "null";
        }
        if (f.isFile()) {
            return "file";
        }
        if (f.isDirectory()) {
            return "dir";
        }
        return "other";
    }

    /** Delete without following symbolic links (link itself only). */
    static void deleteTree(File f) {
        if (f == null) {
            return;
        }
        try {
            if (Files.isSymbolicLink(f.toPath())) {
                Files.deleteIfExists(f.toPath());
                return;
            }
        } catch (IOException ignored) {
            // fall through
        }
        if (!f.exists()) {
            return;
        }
        if (f.isDirectory()) {
            File[] kids = f.listFiles();
            if (kids != null) {
                for (File k : kids) {
                    deleteTree(k);
                }
            }
        }
        //noinspection ResultOfMethodCallIgnored
        f.delete();
    }

    private BootstrapOracleTest() {}
}
