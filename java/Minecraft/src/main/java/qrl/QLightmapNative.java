package qrl;

import java.nio.file.Files;
import java.nio.file.Paths;

/**
 * JNI bridge to the native EntityRenderer.updateLightmap() heavy-buffer kernel
 * (render-opt/dropin/lightmap/libqlm.so). Unlike QSinNative (a scalar), nlightmap
 * fills the 256-int lightmapColors array (heavy-buffer marshaling spike).
 *
 * Mode resolved ONCE at class-init. Source order: env QLM_MODE, else sidecar file
 * render-opt/dropin/lightmap/qlm_mode.txt (file fallback dodges gradle-daemon env
 * staleness, same gotcha QSinNative hit):
 *   "native"   -> MODE=1, load libqlm.so, route updateLightmap through native nlightmap()
 *   "sabotage" -> MODE=2, fill lightmapColors with an obviously-wrong dark pattern
 *   else/unset -> MODE=0, original Java loop runs (vanilla baseline)
 */
public final class QLightmapNative {
    public static volatile int MODE;  // volatile: runtime-switchable via the qrl "kmode" op
    private static boolean libLoaded = false;
    private static final String DIR =
        "/home/infatoshi/dev/minecraft/mc-1.11.2-env/c/render-opt/dropin/lightmap";

    static {
        // Sidecar file is AUTHORITATIVE (freshly written per launch by capture_lm.sh).
        // The gradle daemon serves a STALE non-empty QLM_MODE to runClient, so an
        // env-first order would let the stale env shadow the sidecar. Sidecar wins.
        String m = null;
        try { m = new String(Files.readAllBytes(Paths.get(DIR, "qlm_mode.txt"))).trim(); }
        catch (Exception e) { m = null; }
        if (m == null || m.isEmpty()) m = System.getenv("QLM_MODE");
        int mode = 0;
        if ("native".equals(m)) {
            mode = 1;
            String lib = System.getenv("QLM_LIB");
            if (lib == null) lib = DIR + "/libqlm.so";
            System.load(lib);
            libLoaded = true;
            System.err.println("[qlm] QLightmapNative: loaded native lib " + lib);
        } else if ("sabotage".equals(m)) {
            mode = 2;
        }
        MODE = mode;
        System.err.println("[qlm] QLightmapNative MODE=" + mode + " (resolved QLM_MODE=" + m + ")");
    }

    /** native heavy-buffer kernel: reads the 16-float table, fills out[256]. */
    public static native void nlightmap(float f, float gamma, float torchFlickerX,
                                        int lastLightning, int dimId,
                                        float[] brightnessTable, int[] out);

    private QLightmapNative() {}

    /** Runtime kernel switch (qrl "kmode" op): off/vanilla -> 0, native -> 1, sabotage -> 2.
     *  Lazily loads the JNI lib on the first switch to native. Chunk-baked kernels
     *  (biome tint, AO) additionally need RenderGlobal.loadRenderers() after switching -
     *  the kmode op does that. */
    public static synchronized void setMode(String m) {
        int mode = 0;
        if ("native".equals(m)) {
            if (!libLoaded) { System.load(DIR + "/libqlm.so"); libLoaded = true; }
            mode = 1;
        } else if ("sabotage".equals(m)) {
            mode = 2;
        }
        MODE = mode;
        System.err.println("[qlm] MODE switched to " + mode + " (" + m + ")");
    }
}
