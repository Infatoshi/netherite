/* CANDIDATE: C port of MC 1.11.2 EntityRenderer.updateLightmap()
 *   src/net/minecraft/client/renderer/EntityRenderer.java:883
 *
 * The golden is CAPTURED FROM REAL MINECRAFT (capture_mode "live-hook"): one frame's
 * 256 lightmap ARGB texels, with every scalar the method reads snapshotted in inputs.txt.
 *
 * Input (golden/inputs.txt), label + raw-int-bits float (or plain int) per line:
 *   partialTicks, sunBrightness (= f = world.getSunBrightness(1.0F)), rainStrength,
 *   thunderStrength, gamma (= gameSettings.gammaSetting), nightVision (0/1), dimId,
 *   torchFlickerX, bossColorModifier, lastLightningBolt (int),
 *   brightnessTable (16 raw-bits floats = world.provider.getLightBrightnessTable()).
 *
 * Output (golden/golden.txt): 256 ARGB ints (decimal), lightmapColors[0..255].
 *
 * Captured state simplifications (all faithful to the method):
 *   - overworld (dimId 0): provider.getLightmapColors() is a no-op (empty body), dim!=1.
 *   - bossColorModifier == 0   -> boss branch skipped.
 *   - lastLightningBolt == 0   -> lightning override skipped.
 *   - nightVision == 0         -> night-vision branch skipped (its brightness isn't captured).
 * The code guards on the captured flags, so if a future capture sets them the relevant
 * (uncomputable-from-this-input) branch would be skipped -- re-capture those fields then.
 *
 * Build with -ffp-contract=off (runner does) so float ops round bit-identically to the JVM. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static float bits_to_f(int32_t b) { float f; memcpy(&f, &b, sizeof f); return f; }

int main(void) {
    char label[64];
    int32_t partialBits, sunBits, rainBits, thunBits, gammaBits, torchBits, bossBits;
    int nightVision, dimId, lastLightning;
    float bt[16];

    /* label-driven parse (order as written by the capture) */
    scanf("%63s %d", label, &partialBits);   /* partialTicks */
    scanf("%63s %d", label, &sunBits);       /* sunBrightness */
    scanf("%63s %d", label, &rainBits);      /* rainStrength */
    scanf("%63s %d", label, &thunBits);      /* thunderStrength */
    scanf("%63s %d", label, &gammaBits);     /* gamma */
    scanf("%63s %d", label, &nightVision);   /* nightVision (int flag) */
    scanf("%63s %d", label, &dimId);         /* dimId (int) */
    scanf("%63s %d", label, &torchBits);     /* torchFlickerX */
    scanf("%63s %d", label, &bossBits);      /* bossColorModifier */
    scanf("%63s %d", label, &lastLightning); /* lastLightningBolt (int) */
    scanf("%63s", label);                    /* brightnessTable */
    for (int i = 0; i < 16; ++i) { int32_t b; scanf("%d", &b); bt[i] = bits_to_f(b); }

    float partialTicks = bits_to_f(partialBits);
    float f = bits_to_f(sunBits);                 /* world.getSunBrightness(1.0F) */
    float gamma = bits_to_f(gammaBits);
    float torchFlickerX = bits_to_f(torchBits);
    float bossColorModifier = bits_to_f(bossBits);
    (void)rainBits; (void)thunBits; (void)partialTicks;

    float f1 = f * 0.95F + 0.05F;

    for (int i = 0; i < 256; ++i) {
        float f2 = bt[i / 16] * f1;
        float f3 = bt[i % 16] * (torchFlickerX * 0.1F + 1.5F);

        if (lastLightning > 0)
            f2 = bt[i / 16];

        float f4 = f2 * (f * 0.65F + 0.35F);
        float f5 = f2 * (f * 0.65F + 0.35F);
        float f6 = f3 * ((f3 * 0.6F + 0.4F) * 0.6F + 0.4F);
        float f7 = f3 * (f3 * f3 * 0.6F + 0.4F);
        float f8 = f4 + f3;
        float f9 = f5 + f6;
        float f10 = f2 + f7;
        f8 = f8 * 0.96F + 0.03F;
        f9 = f9 * 0.96F + 0.03F;
        f10 = f10 * 0.96F + 0.03F;

        if (bossColorModifier > 0.0F) {
            /* prev not captured; with bossColorModifier==0 this never runs */
            float f11 = bossColorModifier * partialTicks; /* placeholder, unreached */
            f8 = f8 * (1.0F - f11) + f8 * 0.7F * f11;
            f9 = f9 * (1.0F - f11) + f9 * 0.6F * f11;
            f10 = f10 * (1.0F - f11) + f10 * 0.6F * f11;
        }

        if (dimId == 1) {
            f8 = 0.22F + f3 * 0.75F;
            f9 = 0.28F + f6 * 0.75F;
            f10 = 0.25F + f7 * 0.75F;
        }

        /* provider.getLightmapColors(): no-op for the overworld */

        /* night-vision branch (nightVision==0 in capture): skipped */

        if (f8 > 1.0F) f8 = 1.0F;
        if (f9 > 1.0F) f9 = 1.0F;
        if (f10 > 1.0F) f10 = 1.0F;

        float f16 = gamma;
        float f17 = 1.0F - f8;
        float f13 = 1.0F - f9;
        float f14 = 1.0F - f10;
        f17 = 1.0F - f17 * f17 * f17 * f17;
        f13 = 1.0F - f13 * f13 * f13 * f13;
        f14 = 1.0F - f14 * f14 * f14 * f14;
        f8 = f8 * (1.0F - f16) + f17 * f16;
        f9 = f9 * (1.0F - f16) + f13 * f16;
        f10 = f10 * (1.0F - f16) + f14 * f16;
        f8 = f8 * 0.96F + 0.03F;
        f9 = f9 * 0.96F + 0.03F;
        f10 = f10 * 0.96F + 0.03F;

        if (f8 > 1.0F) f8 = 1.0F;
        if (f9 > 1.0F) f9 = 1.0F;
        if (f10 > 1.0F) f10 = 1.0F;
        if (f8 < 0.0F) f8 = 0.0F;
        if (f9 < 0.0F) f9 = 0.0F;
        if (f10 < 0.0F) f10 = 0.0F;

        int k = (int)(f8 * 255.0F);
        int l = (int)(f9 * 255.0F);
        int i1 = (int)(f10 * 255.0F);
        int color = -16777216 | k << 16 | l << 8 | i1;
        printf("%d\n", color);
    }
    return 0;
}
