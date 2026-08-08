package com.microsoft.Malmo.Mixins;

import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiScreen;
import org.lwjgl.input.Keyboard;
import org.lwjgl.input.Mouse;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import qrl.Recorder;

/** Records the raw LWJGL events consumed by every open GuiScreen. */
@Mixin(GuiScreen.class)
public abstract class MixinRecordGuiInput {
    @Shadow public Minecraft mc;
    @Shadow public int width;
    @Shadow public int height;

    @Inject(method = "handleMouseInput", at = @At("HEAD"))
    private void qrl$recordMouse(CallbackInfo ci) {
        int x = Mouse.getEventX() * this.width / this.mc.displayWidth;
        int y = this.height
            - Mouse.getEventY() * this.height / this.mc.displayHeight - 1;
        Recorder.recordGuiMouse(
            ((Object)this).getClass().getSimpleName(), x, y,
            Mouse.getEventButton(), Mouse.getEventButtonState(),
            Mouse.getEventDWheel());
    }

    @Inject(method = "handleKeyboardInput", at = @At("HEAD"))
    private void qrl$recordKeyboard(CallbackInfo ci) {
        Recorder.recordGuiKey(
            ((Object)this).getClass().getSimpleName(),
            Keyboard.getEventKey(), (int)Keyboard.getEventCharacter(),
            Keyboard.getEventKeyState());
    }
}
