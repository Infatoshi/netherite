package qrl;

import java.nio.file.Files;
import java.nio.file.Paths;

/**
 * Dispatcher for the AO drop-in (render-opt kernel 13). The raw coremod transformer
 * (com.microsoft.Malmo.OverclockingClassTransformer) rewrites
 * BlockModelRenderer$AmbientOcclusionFace.getAoBrightness so that, when MODE != 0, it returns
 * QAOHook.aoBrightness(...) instead of running the vanilla body. This sidesteps Mixin, which
 * cannot attach to that package-private inner class in this Forge dev setup (the FML
 * deobfuscating remapper unmaps the inner-class target name to its notch form).
 *
 * MODE is resolved ONCE at class-init (getAoBrightness runs per smooth-lit vertex, so no
 * per-call IO). Single source: sidecar render-opt/dropin/ao/qao_mode.txt (a file, not an
 * env var: the gradle daemon serves a STALE env to runClient):
 *   "native"   -> MODE=1, load libqao.so, route the AO average through the C kernel
 *   "sabotage" -> MODE=2, AO brightness forced to 0 (smooth-lit geometry goes dark)
 *   else/unset -> MODE=0, transformer's MODE check falls through to the vanilla body
 */
public final class QAOHook {
    public static volatile int MODE;  // volatile: runtime-switchable via the qrl "kmode" op
    private static boolean libLoaded = false;
    private static final String DIR =
        "/home/infatoshi/dev/minecraft/mc-1.11.2-env/java/render-opt/dropin/ao";

    static {
        String m = null;
        try { m = new String(Files.readAllBytes(Paths.get(DIR, "qao_mode.txt"))).trim(); }
        catch (Exception e) { m = null; }
        int mode = 0;
        if ("native".equals(m)) {
            mode = 1;
            String lib = DIR + "/libqao.so";
            System.load(lib);
            libLoaded = true;
            System.err.println("[qao] QAOHook: loaded native lib " + lib);
        } else if ("sabotage".equals(m)) {
            mode = 2;
        }
        MODE = mode;
        System.err.println("[qao] QAOHook MODE=" + mode + " (resolved qao_mode=" + m + ")");
    }

    private static boolean firstSab = true;

    /** Called by the rewritten getAoBrightness only when MODE != 0. */
    public static int aoBrightness(int br1, int br2, int br3, int br4) {
        if (MODE == 2) {                               // sabotage: dark AO
            if (firstSab) { firstSab = false;
                System.err.println("[qao] sabotage aoBrightness()->0 INVOKED in live render path (proof)"); }
            return 0;
        }
        return naoBrightness(br1, br2, br3, br4);       // MODE == 1: native C kernel
    }

    /** native smooth-AO 4-way packed-brightness average (libqao.so). */
    public static native int naoBrightness(int br1, int br2, int br3, int br4);

    private QAOHook() {}

    /** Runtime kernel switch (qrl "kmode" op): off/vanilla -> 0, native -> 1, sabotage -> 2.
     *  Lazily loads the JNI lib on the first switch to native. Chunk-baked kernels
     *  (biome tint, AO) additionally need RenderGlobal.loadRenderers() after switching -
     *  the kmode op does that. */
    public static synchronized void setMode(String m) {
        int mode = 0;
        if ("native".equals(m)) {
            if (!libLoaded) { System.load(DIR + "/libqao.so"); libLoaded = true; }
            mode = 1;
        } else if ("sabotage".equals(m)) {
            mode = 2;
        }
        MODE = mode;
        System.err.println("[qao] MODE switched to " + mode + " (" + m + ")");
    }
}
