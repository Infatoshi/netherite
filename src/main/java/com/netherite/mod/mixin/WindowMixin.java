package com.netherite.mod.mixin;

import net.minecraft.client.util.Window;
import net.minecraft.client.WindowEventHandler;
import net.minecraft.client.util.MonitorTracker;
import net.minecraft.client.WindowSettings;
import org.lwjgl.glfw.GLFW;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(Window.class)
public class WindowMixin {
    @Inject(method = "<init>", at = @At(
            value = "INVOKE",
            target = "Lorg/lwjgl/glfw/GLFW;glfwCreateWindow(IILjava/lang/CharSequence;JJ)J"
    ))
    private void beforeCreateWindow(WindowEventHandler handler, MonitorTracker tracker,
            WindowSettings settings, String videoMode, String title, CallbackInfo ci) {
        if (Boolean.getBoolean("netherite.headless")) {
            GLFW.glfwWindowHint(GLFW.GLFW_VISIBLE, GLFW.GLFW_FALSE);
        }
    }
}
