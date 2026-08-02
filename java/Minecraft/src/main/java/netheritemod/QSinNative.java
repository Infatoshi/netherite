package netheritemod;

import java.nio.file.Files;
import java.nio.file.Paths;

/**
 * JNI bridge to the native MathHelper.sin() table lookup (render-opt/dropin/libqsin.so).
 * Mode is resolved ONCE at class-init (MathHelper.sin runs thousands of times/frame, so
 * we must not do IO per call). Single source: sidecar file
 * render-opt/dropin/qsin_mode.txt, written by the capture_*.sh drivers (a file, not an
 * env var: the gradle daemon serves a STALE env to runClient):
 *   "native"   -> MODE=1, load libqsin.so, route sin() through native nsin()
 *   "sabotage" -> MODE=2, sin() returns 0.0f (proves the inject has visible global effect)
 *   else/unset -> MODE=0, original Java SIN_TABLE runs (vanilla baseline)
 */
public final class QSinNative {
    public static volatile int MODE;  // volatile: runtime-switchable via the NetheriteMod "kmode" op
    private static boolean libLoaded = false;
    private static final String DIR = "../../render-opt/dropin";

    static {
        String m = null;
        try { m = new String(Files.readAllBytes(Paths.get(DIR, "qsin_mode.txt"))).trim(); }
        catch (Exception e) { m = null; }
        int mode = 0;
        if ("native".equals(m)) {
            mode = 1;
            String lib = DIR + "/libqsin.so";
            System.load(lib);
            libLoaded = true;
            System.err.println("[qsin] QSinNative: loaded native lib " + lib);
        } else if ("sabotage".equals(m)) {
            mode = 2;
        }
        MODE = mode;
        System.err.println("[qsin] QSinNative MODE=" + mode + " (resolved qsin_mode=" + m + ")");
    }

    public static native float nsin(float value);

    private QSinNative() {}

    /** Runtime kernel switch (NetheriteMod "kmode" op): off/vanilla -> 0, native -> 1, sabotage -> 2.
     *  Lazily loads the JNI lib on the first switch to native. Chunk-baked kernels
     *  (biome tint, AO) additionally need RenderGlobal.loadRenderers() after switching -
     *  the kmode op does that. */
    public static synchronized void setMode(String m) {
        int mode = 0;
        if ("native".equals(m)) {
            if (!libLoaded) { System.load(DIR + "/libqsin.so"); libLoaded = true; }
            mode = 1;
        } else if ("sabotage".equals(m)) {
            mode = 2;
        }
        MODE = mode;
        System.err.println("[qsin] MODE switched to " + mode + " (" + m + ")");
    }
}
