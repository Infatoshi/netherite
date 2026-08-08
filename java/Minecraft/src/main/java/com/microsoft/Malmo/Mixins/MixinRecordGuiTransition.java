package com.microsoft.Malmo.Mixins;

import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiScreen;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import qrl.Recorder;

/** Records screen open/close transitions, including the inventory-key edge
 * that occurs before any GuiScreen exists to receive raw keyboard input. */
@Mixin(Minecraft.class)
public abstract class MixinRecordGuiTransition {
    @Shadow public GuiScreen currentScreen;

    @Inject(method = "displayGuiScreen", at = @At("HEAD"))
    private void qrl$recordGuiTransition(GuiScreen next, CallbackInfo ci) {
        Recorder.recordGuiTransition(
            this.currentScreen == null ? "" :
                this.currentScreen.getClass().getSimpleName(),
            next == null ? "" : next.getClass().getSimpleName());
    }
}
