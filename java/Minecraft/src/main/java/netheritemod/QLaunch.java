package netheritemod;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import java.nio.charset.StandardCharsets;

/**
 * Static holder for the "strip" flags of qrl_launch.json (written by mc_cli.py from
 * fast.yaml/vanilla.yaml). Read once at class init so mixin hot paths pay a field load, not IO.
 * Everything defaults FALSE (full vanilla behavior) when the file or key is absent,
 * so an unconfigured launch remains a faithful oracle.
 *   strip.menus    - block pause menu, options screens, death screen (death goes to obs)
 *   strip.overlays - no vignette, no boss bar (hotbar/health/hunger/xp/crosshair stay)
 *   strip.sound    - never load the paulscode SoundSystem (threads + startup time)
 */
public final class QLaunch {
    public static final boolean STRIP_MENUS;
    public static final boolean STRIP_OVERLAYS;
    public static final boolean NO_SOUND;
    public static final boolean PIN_FLICKER;
    public static final boolean PIN_SKIN;
    public static final boolean PIN_TEXTURE_ANIMATIONS;

    static {
        boolean menus = false, overlays = false, sound = false, flicker = false, skin = false;
        boolean textureAnimations = false;
        // Frozen legacy filename: qrl_launch.json is the config sidecar name
        // (replay tooling + demo metadata reference it; do not rename).
        String[] paths = { "qrl_launch.json" };
        for (String p : paths) {
            if (p == null || p.isEmpty()) continue;
            try {
                byte[] b = java.nio.file.Files.readAllBytes(java.nio.file.Paths.get(p));
                JsonObject o = new JsonParser().parse(new String(b, StandardCharsets.UTF_8)).getAsJsonObject();
                if (o.has("strip")) {
                    JsonObject s = o.getAsJsonObject("strip");
                    menus = s.has("menus") && s.get("menus").getAsBoolean();
                    overlays = s.has("overlays") && s.get("overlays").getAsBoolean();
                    sound = s.has("sound") && s.get("sound").getAsBoolean();
                }
                if (o.has("determinism")) {
                    JsonObject d = o.getAsJsonObject("determinism");
                    flicker = d.has("pin_flicker") && d.get("pin_flicker").getAsBoolean();
                    skin = d.has("pin_skin") && d.get("pin_skin").getAsBoolean();
                    textureAnimations = d.has("pin_texture_animations")
                        && d.get("pin_texture_animations").getAsBoolean();
                }
                break;
            } catch (Exception ig) { /* try next path */ }
        }
        STRIP_MENUS = menus;
        STRIP_OVERLAYS = overlays;
        NO_SOUND = sound;
        PIN_FLICKER = flicker;
        PIN_SKIN = skin;
        PIN_TEXTURE_ANIMATIONS = textureAnimations;
        System.err.println("[qlaunch] strip: menus=" + menus + " overlays=" + overlays
            + " sound_off=" + sound + " pin_flicker=" + flicker + " pin_skin=" + skin
            + " pin_texture_animations=" + textureAnimations);
    }

    private QLaunch() {}
}
