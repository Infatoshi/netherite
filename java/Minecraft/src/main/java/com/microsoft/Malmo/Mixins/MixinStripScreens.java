package com.microsoft.Malmo.Mixins;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiGameOver;
import net.minecraft.client.gui.GuiIngameMenu;
import net.minecraft.client.gui.GuiOptions;
import net.minecraft.client.gui.GuiScreen;

import netheritemod.QLaunch;

/**
 * CLI-instance GUI strip (qrl_launch.json strip.menus): block the pause menu, the
 * options screen, and the death screen from ever opening. Death is surfaced in the
 * NetheriteMod observation ("dead"/"deaths") and auto-respawned by the NetheriteMod tick handler, so
 * no screen is needed. Inventory/chest/crafting GUIs pass through untouched.
 * Flag off (or no qrl_launch.json) = pure vanilla, so oracle captures are unaffected.
 */
@Mixin(Minecraft.class)
public abstract class MixinStripScreens {
    @Inject(method = "displayGuiScreen", at = @At("HEAD"), cancellable = true)
    private void qrl$filterScreens(GuiScreen screen, CallbackInfo ci) {
        if (!QLaunch.STRIP_MENUS || screen == null) return;
        if (screen instanceof GuiIngameMenu || screen instanceof GuiGameOver
                || screen instanceof GuiOptions) {
            ci.cancel();
        }
    }
}
