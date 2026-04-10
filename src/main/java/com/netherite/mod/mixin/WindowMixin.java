package com.netherite.mod.mixin;

import com.netherite.mod.NetheriteConfig;
import com.netherite.mod.NetheriteMod;
import net.minecraft.client.util.Window;
import org.lwjgl.glfw.GLFW;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(Window.class)
public class WindowMixin {
    @Shadow
    private long handle;

    @Inject(
            method = "<init>",
            at = @At(
                    value = "INVOKE",
                    target = "Lorg/lwjgl/glfw/GLFW;glfwDefaultWindowHints()V",
                    shift = At.Shift.AFTER,
                    remap = false
            )
    )
    private void afterDefaultWindowHints(CallbackInfo ci) {
        if (Boolean.getBoolean("netherite.headless")) {
            GLFW.glfwWindowHint(GLFW.GLFW_VISIBLE, GLFW.GLFW_FALSE);
        }
        if (System.getProperty("os.name", "").toLowerCase().contains("mac")) {
            GLFW.glfwWindowHint(GLFW.GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW.GLFW_FALSE);
        }
    }

    @Inject(method = "<init>", at = @At("TAIL"))
    private void afterCreateWindow(CallbackInfo ci) {
        if (Boolean.getBoolean("netherite.headless")) {
            GLFW.glfwHideWindow(this.handle);
        }
        if (Boolean.getBoolean("netherite.uncapped")) {
            GLFW.glfwSwapInterval(0);
            NetheriteMod.LOGGER.info("WindowMixin: disabled VSync (swap interval = 0)");
        }
    }

    @Inject(method = "getFramebufferWidth", at = @At("HEAD"), cancellable = true)
    private void overrideFramebufferWidth(CallbackInfoReturnable<Integer> cir) {
        int w = NetheriteConfig.INSTANCE.width;
        if (w > 0) {
            cir.setReturnValue(w);
        }
    }

    @Inject(method = "getFramebufferHeight", at = @At("HEAD"), cancellable = true)
    private void overrideFramebufferHeight(CallbackInfoReturnable<Integer> cir) {
        int h = NetheriteConfig.INSTANCE.height;
        if (h > 0) {
            cir.setReturnValue(h);
        }
    }
}
