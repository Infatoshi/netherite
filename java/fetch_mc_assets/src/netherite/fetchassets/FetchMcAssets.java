package netherite.fetchassets;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Pre-seed the ForgeGradle 2 asset cache over HTTPS.
 *
 * <p>Usage: java netherite.fetchassets.FetchMcAssets &lt;assets-dir&gt;
 * where &lt;assets-dir&gt; is {@code <gradle-home>/caches/minecraft/assets}
 * (created if missing). Stdlib only; safe to rerun (validates hashes,
 * downloads only what is missing or corrupt).
 */
public final class FetchMcAssets {
    public static final String MANIFEST_URL =
            "https://launchermeta.mojang.com/mc/game/version_manifest.json";
    public static final String RESOURCES_BASE =
            "https://resources.download.minecraft.net";
    public static final String MC_VERSION = "1.11.2";
    /** Compiled default worker count for parallel object fetches. */
    public static final int DEFAULT_WORKERS = 8;

    private static final int CONNECT_TIMEOUT_MS = 60_000;
    private static final int READ_TIMEOUT_MS = 60_000;

    public static final class Config {
        public String manifestUrl = MANIFEST_URL;
        public String resourcesBase = RESOURCES_BASE;
        public String version = MC_VERSION;
        public int workers = DEFAULT_WORKERS;
        public File assetsDir;
    }

    public static final class Result {
        public final int uniqueObjects;
        public final int downloaded;
        public final int reused;
        public final File assetsDir;

        Result(int uniqueObjects, int downloaded, int reused, File assetsDir) {
            this.uniqueObjects = uniqueObjects;
            this.downloaded = downloaded;
            this.reused = reused;
            this.assetsDir = assetsDir;
        }
    }

    public static void main(String[] args) {
        if (args.length != 1) {
            System.err.println(
                    "Usage: java netherite.fetchassets.FetchMcAssets <assets-dir>\n"
                            + "where <assets-dir> is <gradle-home>/caches/minecraft/assets "
                            + "(created if missing). Stdlib only; safe to rerun "
                            + "(validates hashes, downloads only what is missing or corrupt).");
            System.exit(2);
        }
        Config cfg = new Config();
        cfg.assetsDir = new File(args[0]);
        try {
            Result r = run(cfg);
            System.out.println(
                    "assets ready: " + r.uniqueObjects + " objects ("
                            + r.downloaded + " downloaded, " + r.reused
                            + " already valid) in " + r.assetsDir.getPath());
        } catch (Exception e) {
            System.err.println("ERROR: " + e.getMessage());
            e.printStackTrace(System.err);
            System.exit(1);
        }
    }

    public static Result run(Config cfg) throws Exception {
        if (cfg == null || cfg.assetsDir == null) {
            throw new IllegalArgumentException("assetsDir required");
        }
        if (cfg.workers < 1) {
            throw new IllegalArgumentException("workers must be >= 1");
        }
        File assets = cfg.assetsDir;
        if (!assets.exists() && !assets.mkdirs()) {
            throw new IOException("cannot create assets dir: " + assets);
        }

        byte[] manifestBytes = httpGet(cfg.manifestUrl);
        Map<String, Object> manifest = Json.asObject(Json.parse(new String(manifestBytes, "UTF-8")));
        String versionUrl = findVersionUrl(manifest, cfg.version);

        byte[] versionBytes = httpGet(versionUrl);
        Map<String, Object> versionJson =
                Json.asObject(Json.parse(new String(versionBytes, "UTF-8")));
        Map<String, Object> assetIndex = Json.asObject(versionJson.get("assetIndex"));
        String indexId = Json.asString(assetIndex.get("id"));
        String indexSha1 = Json.asString(assetIndex.get("sha1"));
        String indexUrl = Json.asString(assetIndex.get("url"));

        File indexesDir = new File(assets, "indexes");
        File indexPath = new File(indexesDir, indexId + ".json");
        byte[] indexData = loadOrFetch(indexPath, indexSha1, indexUrl);

        Map<String, Object> indexJson =
                Json.asObject(Json.parse(new String(indexData, "UTF-8")));
        Map<String, Object> objects = Json.asObject(indexJson.get("objects"));
        Set<String> hashes = uniqueHashes(objects);

        File objectsDir = new File(assets, "objects");
        AtomicInteger downloaded = new AtomicInteger(0);
        AtomicInteger reused = new AtomicInteger(0);

        List<String> hashList = new ArrayList<String>(hashes);
        ExecutorService pool = Executors.newFixedThreadPool(cfg.workers);
        try {
            List<Future<Void>> futures = new ArrayList<Future<Void>>(hashList.size());
            final String resourcesBase = trimSlash(cfg.resourcesBase);
            for (final String hash : hashList) {
                futures.add(pool.submit(new Callable<Void>() {
                    @Override
                    public Void call() throws Exception {
                        if (getObject(objectsDir, resourcesBase, hash)) {
                            downloaded.incrementAndGet();
                        } else {
                            reused.incrementAndGet();
                        }
                        return null;
                    }
                }));
            }
            for (Future<Void> f : futures) {
                f.get();
            }
        } finally {
            pool.shutdown();
            try {
                if (!pool.awaitTermination(60, java.util.concurrent.TimeUnit.SECONDS)) {
                    pool.shutdownNow();
                }
            } catch (InterruptedException ie) {
                pool.shutdownNow();
                Thread.currentThread().interrupt();
            }
        }

        return new Result(hashes.size(), downloaded.get(), reused.get(), assets);
    }

    static String findVersionUrl(Map<String, Object> manifest, String versionId) {
        List<Object> versions = Json.asArray(manifest.get("versions"));
        for (Object item : versions) {
            Map<String, Object> v = Json.asObject(item);
            if (versionId.equals(v.get("id"))) {
                return Json.asString(v.get("url"));
            }
        }
        throw new IllegalArgumentException("version not found in manifest: " + versionId);
    }

    static Set<String> uniqueHashes(Map<String, Object> objects) {
        Set<String> out = new LinkedHashSet<String>();
        for (Object item : objects.values()) {
            Map<String, Object> obj = Json.asObject(item);
            out.add(Json.asString(obj.get("hash")));
        }
        return out;
    }

    /**
     * @return true if downloaded (or replaced), false if existing file was valid
     */
    static boolean getObject(File objectsDir, String resourcesBase, String hash)
            throws Exception {
        File dst = objectPath(objectsDir, hash);
        if (dst.isFile() && hash.equals(sha1File(dst))) {
            return false;
        }
        byte[] data = httpGet(resourcesBase + "/" + hash.substring(0, 2) + "/" + hash);
        String got = sha1Bytes(data);
        if (!hash.equals(got)) {
            throw new IOException("hash mismatch for " + hash + ": got " + got);
        }
        writeAtomically(dst, data);
        return true;
    }

    static File objectPath(File objectsDir, String hash) {
        return new File(new File(objectsDir, hash.substring(0, 2)), hash);
    }

    static byte[] loadOrFetch(File path, String expectedSha1, String url) throws Exception {
        if (path.isFile() && expectedSha1.equals(sha1File(path))) {
            return readAll(path);
        }
        byte[] data = httpGet(url);
        String got = sha1Bytes(data);
        if (!expectedSha1.equals(got)) {
            throw new IOException("asset index hash mismatch: got " + got);
        }
        writeAtomically(path, data);
        return data;
    }

    static void writeAtomically(File dest, byte[] data) throws IOException {
        File parent = dest.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) {
            throw new IOException("cannot create directory: " + parent);
        }
        File tmp = File.createTempFile(dest.getName() + ".", ".tmp", parent);
        try {
            FileOutputStream out = new FileOutputStream(tmp);
            try {
                out.write(data);
                out.flush();
                out.getFD().sync();
            } finally {
                out.close();
            }
            try {
                Files.move(
                        tmp.toPath(),
                        dest.toPath(),
                        StandardCopyOption.REPLACE_EXISTING,
                        StandardCopyOption.ATOMIC_MOVE);
            } catch (AtomicMoveNotSupportedException e) {
                Files.move(
                        tmp.toPath(),
                        dest.toPath(),
                        StandardCopyOption.REPLACE_EXISTING);
            }
            tmp = null;
        } finally {
            if (tmp != null && tmp.exists()) {
                //noinspection ResultOfMethodCallIgnored
                tmp.delete();
            }
        }
    }

    static byte[] httpGet(String urlStr) throws IOException {
        URL url = new URL(urlStr);
        HttpURLConnection conn = (HttpURLConnection) url.openConnection();
        conn.setConnectTimeout(CONNECT_TIMEOUT_MS);
        conn.setReadTimeout(READ_TIMEOUT_MS);
        conn.setInstanceFollowRedirects(true);
        conn.setRequestMethod("GET");
        conn.setRequestProperty("User-Agent", "netherite-fetch-mc-assets/1.0");
        int code = conn.getResponseCode();
        if (code < 200 || code >= 300) {
            conn.disconnect();
            throw new IOException("HTTP " + code + " for " + urlStr);
        }
        InputStream in = conn.getInputStream();
        try {
            return readStream(in);
        } finally {
            in.close();
            conn.disconnect();
        }
    }

    static byte[] readAll(File f) throws IOException {
        FileInputStream in = new FileInputStream(f);
        try {
            return readStream(in);
        } finally {
            in.close();
        }
    }

    static byte[] readStream(InputStream in) throws IOException {
        ByteArrayOutputStream bos = new ByteArrayOutputStream();
        byte[] buf = new byte[1 << 16];
        int n;
        while ((n = in.read(buf)) >= 0) {
            bos.write(buf, 0, n);
        }
        return bos.toByteArray();
    }

    static String sha1File(File f) throws IOException {
        FileInputStream in = new FileInputStream(f);
        try {
            MessageDigest md = sha1Digest();
            byte[] buf = new byte[1 << 20];
            int n;
            while ((n = in.read(buf)) >= 0) {
                md.update(buf, 0, n);
            }
            return toHex(md.digest());
        } finally {
            in.close();
        }
    }

    static String sha1Bytes(byte[] data) {
        MessageDigest md = sha1Digest();
        md.update(data);
        return toHex(md.digest());
    }

    static MessageDigest sha1Digest() {
        try {
            return MessageDigest.getInstance("SHA-1");
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException("SHA-1 not available", e);
        }
    }

    static String toHex(byte[] dig) {
        char[] hex = "0123456789abcdef".toCharArray();
        char[] out = new char[dig.length * 2];
        for (int i = 0; i < dig.length; i++) {
            int v = dig[i] & 0xff;
            out[i * 2] = hex[v >>> 4];
            out[i * 2 + 1] = hex[v & 0x0f];
        }
        return new String(out);
    }

    private static String trimSlash(String base) {
        if (base == null || base.isEmpty()) {
            throw new IllegalArgumentException("resourcesBase required");
        }
        if (base.endsWith("/")) {
            return base.substring(0, base.length() - 1);
        }
        return base;
    }

    private FetchMcAssets() {}
}
