package com.microsoft.Malmo.Mixins;

import net.minecraft.client.Minecraft;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;

import netheritemod.DigMinecraftAccess;

/**
 * Exposes Minecraft.leftClickCounter for dig-trace emission without reflection.
 * Source evidence: Minecraft.leftClickCounter (private int), decremented in
 * runTickMouse; clickMouse MISS arms 10 in survival.
 */
@Mixin(Minecraft.class)
public abstract class MixinMinecraftDigAccess implements DigMinecraftAccess {
    @Shadow private int leftClickCounter;

    @Override
    public int dig$leftClickCounter() {
        return this.leftClickCounter;
    }
}
