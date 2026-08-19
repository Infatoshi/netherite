package netherite.fetchassets;

import com.sun.net.httpserver.Headers;
import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpHandler;
import com.sun.net.httpserver.HttpServer;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.nio.charset.Charset;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Offline unit tests for FetchMcAssets. Uses only the JDK (local loopback
 * HTTP server). Does not require the public internet.
 */
public final class FetchMcAssetsTest {
    private static final Charset UTF8 = Charset.forName("UTF-8");
    private static int failures = 0;
    private static int passes = 0;

    public static void main(String[] args) throws Exception {
        testJsonEscapes();
        testObjectTraversal();
        testUniqueHashes();
        testSha1();
        testBadJson();
        testBadCli();
        testValidCacheReuseAndCorruptReplacement();
        testBadHash();

        System.out.println("FetchMcAssetsTest: " + passes + " passed, " + failures + " failed");
        if (failures > 0) {
            System.exit(1);
        }
    }

    static void testJsonEscapes() {
        Object v = Json.parse(
                "{\"a\":\"quote\\\"slash\\\\/nl\\ncr\\rtab\\tuni\\u0041\"}");
        Map<String, Object> m = Json.asObject(v);
        String a = Json.asString(m.get("a"));
        check("json escape content",
                "quote\"slash\\/nl\ncr\rtab\tuniA".equals(a),
                a);
        // unknown field retained
        Object v2 = Json.parse("{\"keep\":1,\"extra\":{\"x\":true},\"arr\":[null,false]}");
        Map<String, Object> m2 = Json.asObject(v2);
        check("unknown field keep", m2.containsKey("extra"), String.valueOf(m2.keySet()));
        check("nested object", m2.get("extra") instanceof Map, Json.typeName(m2.get("extra")));
        check("array present", m2.get("arr") instanceof List, Json.typeName(m2.get("arr")));
    }

    static void testObjectTraversal() {
        String json =
                "{\"versions\":[{\"id\":\"1.11.2\",\"url\":\"http://x/v.json\",\"unknown\":9},"
                        + "{\"id\":\"1.12\",\"url\":\"http://y\"}],"
                        + "\"latest\":{\"release\":\"1.12\"}}";
        Map<String, Object> man = Json.asObject(Json.parse(json));
        String url = FetchMcAssets.findVersionUrl(man, "1.11.2");
        check("findVersionUrl", "http://x/v.json".equals(url), url);
        try {
            FetchMcAssets.findVersionUrl(man, "nope");
            fail("findVersionUrl missing should throw");
        } catch (IllegalArgumentException e) {
            pass("findVersionUrl missing throws");
        }
    }

    static void testUniqueHashes() {
        Map<String, Object> objects = new LinkedHashMap<String, Object>();
        objects.put("a/b", mapOf("hash", "aaa", "size", Integer.valueOf(1), "extra", "x"));
        objects.put("c/d", mapOf("hash", "bbb", "size", Integer.valueOf(2)));
        objects.put("e/f", mapOf("hash", "aaa", "size", Integer.valueOf(3)));
        java.util.Set<String> u = FetchMcAssets.uniqueHashes(objects);
        check("unique count", u.size() == 2, String.valueOf(u));
        check("unique contains aaa", u.contains("aaa"), String.valueOf(u));
        check("unique contains bbb", u.contains("bbb"), String.valueOf(u));
    }

    static void testSha1() {
        // echo -n "hello" | sha1sum
        String h = FetchMcAssets.sha1Bytes("hello".getBytes(UTF8));
        check("sha1 hello",
                "aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d".equals(h),
                h);
    }

    static void testBadJson() {
        try {
            Json.parse("{not json");
            fail("bad json should throw");
        } catch (IllegalArgumentException e) {
            pass("bad json throws");
        }
        try {
            Json.parse("{\"a\":1} trailing");
            fail("trailing should throw");
        } catch (IllegalArgumentException e) {
            pass("trailing json throws");
        }
    }

    static void testBadCli() {
        // Capture exit by invoking logic mirror: argc check is in main.
        // Verify usage path by process-less unit of the condition:
        // Also exercise Config validation:
        try {
            FetchMcAssets.Config cfg = new FetchMcAssets.Config();
            FetchMcAssets.run(cfg);
            fail("null assetsDir should throw");
        } catch (Exception e) {
            pass("null assetsDir throws");
        }
        // Spawn self for real CLI argc if classes are on classpath
        try {
            String cp = System.getProperty("java.class.path");
            ProcessBuilder pb = new ProcessBuilder(
                    System.getProperty("java.home") + "/bin/java",
                    "-cp", cp,
                    "netherite.fetchassets.FetchMcAssets");
            pb.redirectErrorStream(true);
            Process p = pb.start();
            ByteArrayOutputStream bos = new ByteArrayOutputStream();
            InputStream in = p.getInputStream();
            byte[] buf = new byte[4096];
            int n;
            while ((n = in.read(buf)) >= 0) {
                bos.write(buf, 0, n);
            }
            int code = p.waitFor();
            String out = new String(bos.toByteArray(), UTF8);
            check("bad CLI exit code", code == 2, "code=" + code + " out=" + out);
            check("bad CLI usage text", out.contains("Usage:"), out);
        } catch (Exception e) {
            fail("bad CLI spawn: " + e.getMessage());
        }
    }

    static void testValidCacheReuseAndCorruptReplacement() throws Exception {
        final byte[] objA = "asset-a-bytes".getBytes(UTF8);
        final byte[] objB = "asset-b-payload".getBytes(UTF8);
        final String hashA = FetchMcAssets.sha1Bytes(objA);
        final String hashB = FetchMcAssets.sha1Bytes(objB);

        // Two logical paths, same hash for A twice to exercise unique set
        String indexJson =
                "{\"objects\":{"
                        + "\"minecraft/a.txt\":{\"hash\":\"" + hashA + "\",\"size\":" + objA.length + "},"
                        + "\"minecraft/a2.txt\":{\"hash\":\"" + hashA + "\",\"size\":" + objA.length + "},"
                        + "\"minecraft/b.txt\":{\"hash\":\"" + hashB + "\",\"size\":" + objB.length + ",\"meta\":1}"
                        + "},\"unknownIndexField\":true}";
        byte[] indexBytes = indexJson.getBytes(UTF8);
        final String indexSha1 = FetchMcAssets.sha1Bytes(indexBytes);
        final String indexId = "1.11";

        String versionJson =
                "{\"assetIndex\":{\"id\":\"" + indexId + "\",\"sha1\":\"" + indexSha1
                        + "\",\"url\":\"__INDEX__\",\"totalSize\":1,\"unknown\":0},"
                        + "\"id\":\"1.11.2\",\"type\":\"release\"}";

        final AtomicInteger objectHits = new AtomicInteger(0);
        final AtomicInteger indexHits = new AtomicInteger(0);

        FixtureServer server = FixtureServer.start(new HttpHandler() {
            @Override
            public void handle(HttpExchange ex) throws IOException {
                String path = ex.getRequestURI().getPath();
                byte[] body;
                if ("/manifest.json".equals(path)) {
                    String base = "http://127.0.0.1:" + ex.getLocalAddress().getPort();
                    String man =
                            "{\"versions\":[{\"id\":\"1.11.2\",\"url\":\"" + base
                                    + "/version.json\"}],\"latest\":{\"release\":\"1.11.2\"}}";
                    body = man.getBytes(UTF8);
                } else if ("/version.json".equals(path)) {
                    String base = "http://127.0.0.1:" + ex.getLocalAddress().getPort();
                    body = versionJson.replace("__INDEX__", base + "/indexes/" + indexId + ".json")
                            .getBytes(UTF8);
                } else if (("/indexes/" + indexId + ".json").equals(path)) {
                    indexHits.incrementAndGet();
                    body = indexBytes;
                } else if (path.startsWith("/objects/")) {
                    objectHits.incrementAndGet();
                    // /objects/ab/abcd...
                    String[] parts = path.split("/");
                    String hash = parts[parts.length - 1];
                    if (hash.equals(hashA)) {
                        body = objA;
                    } else if (hash.equals(hashB)) {
                        body = objB;
                    } else {
                        ex.sendResponseHeaders(404, -1);
                        ex.close();
                        return;
                    }
                } else {
                    ex.sendResponseHeaders(404, -1);
                    ex.close();
                    return;
                }
                Headers h = ex.getResponseHeaders();
                h.set("Content-Type", "application/json");
                ex.sendResponseHeaders(200, body.length);
                OutputStream os = ex.getResponseBody();
                os.write(body);
                os.close();
                ex.close();
            }
        });

        File assets = tempDir("assets-valid");
        try {
            FetchMcAssets.Config cfg = new FetchMcAssets.Config();
            cfg.manifestUrl = server.url("/manifest.json");
            cfg.resourcesBase = server.url("/objects");
            cfg.version = "1.11.2";
            cfg.workers = 4;
            cfg.assetsDir = assets;

            FetchMcAssets.Result r1 = FetchMcAssets.run(cfg);
            check("first run unique", r1.uniqueObjects == 2, String.valueOf(r1.uniqueObjects));
            check("first run downloaded both", r1.downloaded == 2, String.valueOf(r1.downloaded));
            check("first run reused zero", r1.reused == 0, String.valueOf(r1.reused));
            int hitsAfterFirst = objectHits.get();
            check("object hits after first >=2", hitsAfterFirst >= 2, String.valueOf(hitsAfterFirst));

            File objAPath = FetchMcAssets.objectPath(new File(assets, "objects"), hashA);
            check("obj A on disk", objAPath.isFile(), objAPath.getPath());
            check("obj A hash", hashA.equals(FetchMcAssets.sha1File(objAPath)), "bad");

            // Second run: valid cache reuse — no object re-download
            objectHits.set(0);
            indexHits.set(0);
            FetchMcAssets.Result r2 = FetchMcAssets.run(cfg);
            check("second run downloaded zero", r2.downloaded == 0, String.valueOf(r2.downloaded));
            check("second run reused both", r2.reused == 2, String.valueOf(r2.reused));
            check("second run object hits zero", objectHits.get() == 0, String.valueOf(objectHits.get()));
            check("second run index hits zero (valid index cache)",
                    indexHits.get() == 0, String.valueOf(indexHits.get()));

            // Corrupt one object; next run must replace it
            writeRaw(objAPath, "CORRUPT".getBytes(UTF8));
            check("corrupt on purpose",
                    !hashA.equals(FetchMcAssets.sha1File(objAPath)), "still valid?");
            objectHits.set(0);
            FetchMcAssets.Result r3 = FetchMcAssets.run(cfg);
            check("corrupt replace downloaded 1", r3.downloaded == 1, String.valueOf(r3.downloaded));
            check("corrupt replace reused 1", r3.reused == 1, String.valueOf(r3.reused));
            check("corrupt object re-fetched", objectHits.get() == 1, String.valueOf(objectHits.get()));
            check("obj A restored", hashA.equals(FetchMcAssets.sha1File(objAPath)), "not restored");

            // Corrupt index; must re-fetch index
            File idx = new File(new File(assets, "indexes"), indexId + ".json");
            writeRaw(idx, "{broken".getBytes(UTF8));
            indexHits.set(0);
            objectHits.set(0);
            FetchMcAssets.Result r4 = FetchMcAssets.run(cfg);
            check("corrupt index re-fetched", indexHits.get() == 1, String.valueOf(indexHits.get()));
            check("after index fix objects reused", r4.reused == 2, String.valueOf(r4.reused));
            check("index restored hash",
                    indexSha1.equals(FetchMcAssets.sha1File(idx)), "index hash");
        } finally {
            server.stop();
            deleteTree(assets);
        }
    }

    static void testBadHash() throws Exception {
        final byte[] real = "good-payload".getBytes(UTF8);
        // Claim a different hash in the index
        final String claimed = "0123456789abcdef0123456789abcdef01234567";

        String indexJson =
                "{\"objects\":{\"x\":{\"hash\":\"" + claimed + "\",\"size\":1}}}";
        byte[] indexBytes = indexJson.getBytes(UTF8);
        final String indexSha1 = FetchMcAssets.sha1Bytes(indexBytes);
        final String indexId = "bad";

        FixtureServer server = FixtureServer.start(new HttpHandler() {
            @Override
            public void handle(HttpExchange ex) throws IOException {
                String path = ex.getRequestURI().getPath();
                String base = "http://127.0.0.1:" + ex.getLocalAddress().getPort();
                byte[] body;
                if ("/manifest.json".equals(path)) {
                    body = ("{\"versions\":[{\"id\":\"1.11.2\",\"url\":\"" + base
                            + "/version.json\"}]}").getBytes(UTF8);
                } else if ("/version.json".equals(path)) {
                    body = ("{\"assetIndex\":{\"id\":\"" + indexId + "\",\"sha1\":\""
                            + indexSha1 + "\",\"url\":\"" + base + "/indexes/" + indexId
                            + ".json\"}}").getBytes(UTF8);
                } else if (path.startsWith("/indexes/")) {
                    body = indexBytes;
                } else if (path.startsWith("/objects/")) {
                    // Serve content that does not match claimed hash
                    body = real;
                } else {
                    ex.sendResponseHeaders(404, -1);
                    ex.close();
                    return;
                }
                ex.sendResponseHeaders(200, body.length);
                OutputStream os = ex.getResponseBody();
                os.write(body);
                os.close();
                ex.close();
            }
        });

        File assets = tempDir("assets-badhash");
        try {
            FetchMcAssets.Config cfg = new FetchMcAssets.Config();
            cfg.manifestUrl = server.url("/manifest.json");
            cfg.resourcesBase = server.url("/objects");
            cfg.assetsDir = assets;
            try {
                FetchMcAssets.run(cfg);
                fail("bad hash should throw");
            } catch (Exception e) {
                check("bad hash message",
                        e.getMessage() != null && e.getMessage().contains("hash mismatch"),
                        String.valueOf(e));
            }
            // Destination must not exist with claimed name as corrupt write
            File claimedPath = FetchMcAssets.objectPath(new File(assets, "objects"), claimed);
            check("no corrupt destination left",
                    !claimedPath.isFile()
                            || !Arrays.equals(FetchMcAssets.readAll(claimedPath), real)
                            || claimed.equals(FetchMcAssets.sha1File(claimedPath)),
                    "corrupt dest present");
            // Stronger: if file exists it must match claimed (impossible here) — so must not exist
            check("claimed object absent", !claimedPath.isFile(), claimedPath.getPath());
        } finally {
            server.stop();
            deleteTree(assets);
        }
    }

    // --- helpers ---

    static Map<String, Object> mapOf(Object... kv) {
        Map<String, Object> m = new LinkedHashMap<String, Object>();
        for (int i = 0; i < kv.length; i += 2) {
            m.put((String) kv[i], kv[i + 1]);
        }
        return m;
    }

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

    static void writeRaw(File f, byte[] data) throws IOException {
        File parent = f.getParentFile();
        if (parent != null && !parent.exists()) {
            parent.mkdirs();
        }
        FileOutputStream out = new FileOutputStream(f);
        try {
            out.write(data);
        } finally {
            out.close();
        }
    }

    static void deleteTree(File f) {
        if (f == null || !f.exists()) {
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

    static final class FixtureServer {
        final HttpServer server;
        final ExecutorService executor;
        final int port;

        FixtureServer(HttpServer server, ExecutorService executor, int port) {
            this.server = server;
            this.executor = executor;
            this.port = port;
        }

        String url(String path) {
            if (!path.startsWith("/")) {
                path = "/" + path;
            }
            return "http://127.0.0.1:" + port + path;
        }

        void stop() {
            server.stop(0);
            executor.shutdownNow();
        }

        static FixtureServer start(HttpHandler handler) throws IOException {
            HttpServer s = HttpServer.create(new InetSocketAddress("127.0.0.1", 0), 0);
            s.createContext("/", handler);
            ExecutorService ex = Executors.newCachedThreadPool(new ThreadFactory() {
                private final AtomicInteger n = new AtomicInteger();
                @Override
                public Thread newThread(Runnable r) {
                    Thread t = new Thread(r, "fixture-http-" + n.incrementAndGet());
                    t.setDaemon(true);
                    return t;
                }
            });
            s.setExecutor(ex);
            s.start();
            int port = s.getAddress().getPort();
            return new FixtureServer(s, ex, port);
        }
    }

    private FetchMcAssetsTest() {}
}
