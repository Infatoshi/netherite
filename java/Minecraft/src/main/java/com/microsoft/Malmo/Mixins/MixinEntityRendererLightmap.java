package com.microsoft.Malmo.Mixins;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.EntityRenderer;
import net.minecraft.client.renderer.texture.DynamicTexture;
import net.minecraft.world.World;

import netheritemod.QLightmapNative;

/**
 * Heavy-buffer drop-in spike: routes EntityRenderer.updateLightmap (which fills the
 * 256-int lightmapColors array) through a native C kernel, or sabotages it, based on
 * the qlm_mode.txt sidecar (read once via netheritemod.QLightmapNative.MODE). @Inject HEAD + cancellable so
 * MODE=0 leaves the original Java loop untouched (one build, three modes).
 *
 * native (1):   gather the inputs updateLightmap reads (@Shadow access to mc/world/
 *               gameSettings), call native to fill this.lightmapColors[256], upload,
 *               mark not-needed, cancel().
 * sabotage (2): fill lightmapColors with an obviously-wrong dark pattern, upload, cancel().
 * off (0):      no-op (vanilla).
 */
@Mixin(EntityRenderer.class)
public abstract class MixinEntityRendererLightmap {
    @Shadow private Minecraft mc;
    @Shadow private int[] lightmapColors;
    @Shadow @org.spongepowered.asm.mixin.Final private DynamicTexture lightmapTexture;
    @Shadow private boolean lightmapUpdateNeeded;
    @Shadow private float torchFlickerX;

    @Inject(method = "updateLightmap", at = @At("HEAD"), cancellable = true)
    private void qlm$onUpdateLightmap(float partialTicks, CallbackInfo ci) {
        int mode = QLightmapNative.MODE;
        if (mode == 0) return;                       // vanilla
        if (!this.lightmapUpdateNeeded) return;      // mirror vanilla guard; let original no-op
        World world = this.mc.world;
        if (world == null) return;

        if (mode == 1) {
            float f = world.getSunBrightness(1.0F);
            float gamma = this.mc.gameSettings.gammaSetting;
            int lastLightning = world.getLastLightningBolt();
            int dimId = world.provider.getDimensionType().getId();
            float[] tbl = world.provider.getLightBrightnessTable();
            QLightmapNative.nlightmap(f, gamma, this.torchFlickerX,
                                      lastLightning, dimId, tbl, this.lightmapColors);
        } else { // sabotage: obviously-wrong global lighting (everything goes dark)
            for (int i = 0; i < this.lightmapColors.length; i++)
                this.lightmapColors[i] = 0xFF000000;
        }

        this.lightmapTexture.updateDynamicTexture();
        this.lightmapUpdateNeeded = false;
        ci.cancel();
    }
}
