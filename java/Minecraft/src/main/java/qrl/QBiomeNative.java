package qrl;

import java.nio.file.Files;
import java.nio.file.Paths;

/**
 * JNI bridge to the native BiomeColorHelper 3x3 color blend
 * (render-opt/dropin/biome/libqbiome.so), reused from the verified kernel 18 port.
 *
 * Mode resolved ONCE at class-init. Single source: sidecar file
 * render-opt/dropin/biome/qbiome_mode.txt (a file, not an env var: the gradle daemon
 * serves a STALE env to runClient):
 *   "native"   -> MODE=1, load libqbiome.so, route the grass-color blend through C
 *   "sabotage" -> MODE=2, grass color forced to magenta (0xFF00FF) - visibly wrong
 *   else/unset -> MODE=0, original Java loop runs (vanilla baseline)
 */
public final class QBiomeNative {
    public static volatile int MODE;  // volatile: runtime-switchable via the qrl "kmode" op
    private static boolean libLoaded = false;
    private static final String DIR =
        "/home/infatoshi/dev/minecraft/mc-1.11.2-env/java/render-opt/dropin/biome";

    static {
        String m = null;
        try { m = new String(Files.readAllBytes(Paths.get(DIR, "qbiome_mode.txt"))).trim(); }
        catch (Exception e) { m = null; }
        int mode = 0;
        if ("native".equals(m)) {
            mode = 1;
            String lib = DIR + "/libqbiome.so";
            System.load(lib);
            libLoaded = true;
            System.err.println("[qbiome] QBiomeNative: loaded native lib " + lib);
        } else if ("sabotage".equals(m)) {
            mode = 2;
        }
        MODE = mode;
        System.err.println("[qbiome] QBiomeNative MODE=" + mode + " (resolved qbiome_mode=" + m + ")");
    }

    /** native 3x3 blend: 9 packed 0xRRGGBB colors in, blended color out. */
    public static native int nblend(int[] colors);

    private QBiomeNative() {}

    /** Runtime kernel switch (qrl "kmode" op): off/vanilla -> 0, native -> 1, sabotage -> 2.
     *  Lazily loads the JNI lib on the first switch to native. Chunk-baked kernels
     *  (biome tint, AO) additionally need RenderGlobal.loadRenderers() after switching -
     *  the kmode op does that. */
    public static synchronized void setMode(String m) {
        int mode = 0;
        if ("native".equals(m)) {
            if (!libLoaded) { System.load(DIR + "/libqbiome.so"); libLoaded = true; }
            mode = 1;
        } else if ("sabotage".equals(m)) {
            mode = 2;
        }
        MODE = mode;
        System.err.println("[qbiome] MODE switched to " + mode + " (" + m + ")");
    }
}
