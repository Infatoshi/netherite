package com.microsoft.Malmo.Mixins;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;

import net.minecraft.client.renderer.OpenGlHelper;
import net.minecraft.client.shader.Framebuffer;

/**
 * macOS 26.x's deprecated GL stack (Rosetta x86) has a broken
 * glCheckFramebufferStatus: it returns 0 (with no GL error) even for
 * framebuffers that are complete and fully functional. Vanilla treats any
 * non-COMPLETE status as fatal ("glCheckFramebufferStatus returned unknown
 * status:0" at Minecraft.init). Treat a raw 0 as COMPLETE so FBO rendering
 * stays enabled; if the FBO were genuinely broken, later draws/reads would
 * fail visibly rather than silently.
 *
 * Enabled automatically on macOS; override with -Dqrl.fbostatus0ok=true/false.
 */
@Mixin(Framebuffer.class)
public abstract class MixinFramebufferStatusTolerant {
    private static final boolean STATUS0_OK = Boolean.parseBoolean(System.getProperty(
            // Frozen legacy system property name.
            "qrl.fbostatus0ok",
            String.valueOf(System.getProperty("os.name", "").toLowerCase().contains("mac"))));

    @Redirect(method = "checkFramebufferComplete",
              at = @At(value = "INVOKE",
                       target = "Lnet/minecraft/client/renderer/OpenGlHelper;glCheckFramebufferStatus(I)I"))
    private int qrl$tolerateStatus0(int target) {
        int s = OpenGlHelper.glCheckFramebufferStatus(target);
        return (s == 0 && STATUS0_OK) ? OpenGlHelper.GL_FRAMEBUFFER_COMPLETE : s;
    }
}
