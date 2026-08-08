package netheritemod;

import java.nio.file.Files;
import java.nio.file.Paths;

/**
 * JNI bridge to the native EntityRenderer.updateLightmap() heavy-buffer kernel
 * (render-opt/dropin/lightmap/libqlm.so). Unlike QSinNative (a scalar), nlightmap
 * fills the 256-int lightmapColors array (heavy-buffer marshaling spike).
 *
 * Mode resolved ONCE at class-init. Single source: sidecar file
 * render-opt/dropin/lightmap/qlm_mode.txt (a file, not an env var: the gradle daemon
 * serves a STALE env to runClient, same gotcha QSinNative hit):
 *   "native"   -> MODE=1, load libqlm.so, route updateLightmap through native nlightmap()
 *   "sabotage" -> MODE=2, fill lightmapColors with an obviously-wrong dark pattern
 *   else/unset -> MODE=0, original Java loop runs (vanilla baseline)
 */
public final class QLightmapNative {
    public static volatile int MODE;  // volatile: runtime-switchable via the NetheriteMod "kmode" op
    private static boolean libLoaded = false;
    private static final String DIR =
        "../../render-opt/dropin/lightmap";

    static {
        // Sidecar file is the ONLY source (freshly written per launch by capture_lm.sh).
        // The gradle daemon serves a STALE env to runClient, which is exactly why this
        // knob is a file and not an env var.
        String m = null;
        try { m = new String(Files.readAllBytes(Paths.get(DIR, "qlm_mode.txt"))).trim(); }
        catch (Exception e) { m = null; }
        int mode = 0;
        if ("native".equals(m)) {
            mode = 1;
            String lib = DIR + "/libqlm.so";
            System.load(lib);
            libLoaded = true;
            System.err.println("[qlm] QLightmapNative: loaded native lib " + lib);
        } else if ("sabotage".equals(m)) {
            mode = 2;
        }
        MODE = mode;
        System.err.println("[qlm] QLightmapNative MODE=" + mode + " (resolved qlm_mode=" + m + ")");
    }

    /** native heavy-buffer kernel: reads the 16-float table, fills out[256]. */
    public static native void nlightmap(float f, float gamma, float torchFlickerX,
                                        int lastLightning, int dimId,
                                        float[] brightnessTable, int[] out);

    private QLightmapNative() {}

    /** Runtime kernel switch (NetheriteMod "kmode" op): off/vanilla -> 0, native -> 1, sabotage -> 2.
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
