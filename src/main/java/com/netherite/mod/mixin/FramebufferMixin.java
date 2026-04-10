package com.netherite.mod.mixin;

import com.netherite.mod.NetheriteConfig;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gl.Framebuffer;
import org.lwjgl.glfw.GLFW;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL30;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;

/**
 * When netherite.width/height are set, MC renders into a small FBO (e.g. 160x90).
 * This mixin replaces the framebuffer-to-screen blit with a direct GL blit that
 * stretches the small FBO to fill the actual window, producing a pixelated full-FOV view.
 * Uses GL_NEAREST filtering for blocky upscale (what the RL agent sees).
 */
@Mixin(MinecraftClient.class)
public class FramebufferMixin {

    @Redirect(
            method = "render(Z)V",
            at = @At(value = "INVOKE",
                    target = "Lnet/minecraft/client/gl/Framebuffer;draw(II)V")
    )
    private void stretchFramebufferDraw(Framebuffer framebuffer, int width, int height) {
        int targetW = NetheriteConfig.INSTANCE.width;
        int targetH = NetheriteConfig.INSTANCE.height;
        if (targetW > 0 && targetH > 0) {
            MinecraftClient mc = (MinecraftClient) (Object) this;
            long handle = mc.getWindow().getHandle();
            int[] winW = new int[1], winH = new int[1];
            GLFW.glfwGetFramebufferSize(handle, winW, winH);

            GL30.glBindFramebuffer(GL30.GL_READ_FRAMEBUFFER, framebuffer.fbo);
            GL30.glBindFramebuffer(GL30.GL_DRAW_FRAMEBUFFER, 0);
            GL30.glBlitFramebuffer(
                    0, 0, framebuffer.viewportWidth, framebuffer.viewportHeight,
                    0, 0, winW[0], winH[0],
                    GL11.GL_COLOR_BUFFER_BIT, GL11.GL_NEAREST);
            GL30.glBindFramebuffer(GL30.GL_FRAMEBUFFER, 0);
        } else {
            framebuffer.draw(width, height);
        }
    }
}
