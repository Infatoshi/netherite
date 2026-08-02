package com.microsoft.Malmo.Mixins;

import java.util.List;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.Redirect;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import net.minecraft.client.model.ModelBox;
import net.minecraft.client.model.ModelRenderer;
import net.minecraft.client.renderer.GlStateManager;
import net.minecraft.client.renderer.Tessellator;

/**
 * macOS 26.x's deprecated OpenGL stack (under Rosetta x86) has broken display
 * lists: glGenLists returns 0 with no GL error, so vanilla entity-model
 * rendering (ModelRenderer.compileDisplayList) crashes the first time any
 * model part draws ("glGenLists returned an ID of 0"). When enabled, this
 * mixin skips list compilation entirely and tessellates the part's boxes
 * directly at every former glCallList site. Costs a re-tessellation per part
 * per frame; negligible next to the rest of the frame.
 *
 * Enabled automatically on macOS; override with -Dqrl.nolists=true/false.
 * Off (Linux/anvil) this is a pure pass-through.
 */
@Mixin(ModelRenderer.class)
public abstract class MixinModelRendererNoLists {
    private static final boolean NO_LISTS = Boolean.parseBoolean(System.getProperty(
            // Frozen legacy system property name.
            "qrl.nolists",
            String.valueOf(System.getProperty("os.name", "").toLowerCase().contains("mac"))));

    @Shadow public List<ModelBox> cubeList;
    @Shadow private boolean compiled;
    @Shadow private int displayList;

    @Inject(method = "compileDisplayList", at = @At("HEAD"), cancellable = true)
    private void qrl$skipCompile(float scale, CallbackInfo ci) {
        if (NO_LISTS) {
            this.compiled = true;
            this.displayList = 0;
            ci.cancel();
        }
    }

    @Redirect(method = {"render", "renderWithRotation"},
              at = @At(value = "INVOKE",
                       target = "Lnet/minecraft/client/renderer/GlStateManager;callList(I)V"))
    private void qrl$drawDirect(int list, float scale) {
        if (!NO_LISTS) {
            GlStateManager.callList(list);
            return;
        }
        net.minecraft.client.renderer.VertexBuffer vb = Tessellator.getInstance().getBuffer();
        for (int i = 0; i < this.cubeList.size(); ++i) {
            this.cubeList.get(i).render(vb, scale);
        }
    }
}
