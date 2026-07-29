// Vanilla 1.11.2 biome property ground truth from Biome.registerBiomes + subclass ctor overrides.
// Output format matches cpu/biome_props_full.c (7 hex lines per biome id).
public class Golden {
    static class P {
        float baseHeight, heightVariation, temperature;
        int top, filler, genTerrainType;
        P(float bh, float hv, float t, int top, int filler, int gt) {
            baseHeight = bh; heightVariation = hv; temperature = t;
            this.top = top; this.filler = filler; genTerrainType = gt;
        }
    }
    static P[] T = new P[256];
    static void set(int id, float bh, float hv, float t, int top, int filler, int gt) {
        T[id] = new P(bh, hv, t, top, filler, gt);
    }
    static {
        for (int i = 0; i < 256; i++) set(i, 0.1f, 0.2f, 0.5f, 3, 4, 0);
        set(0, -1.0f, 0.1f, 0.5f, 3, 4, 0);
        set(1, 0.125f, 0.05f, 0.8f, 3, 4, 0);
        set(2, 0.125f, 0.05f, 2.0f, 7, 7, 0);
        set(3, 1.0f, 0.5f, 0.2f, 3, 4, 1);
        set(4, 0.1f, 0.2f, 0.7f, 3, 4, 0);
        set(5, 0.2f, 0.2f, 0.25f, 3, 4, 2);
        set(6, -0.2f, 0.1f, 0.8f, 3, 4, 3);
        set(7, -0.5f, 0.0f, 0.5f, 3, 4, 0);
        set(8, 0.1f, 0.2f, 2.0f, 3, 4, 0);
        set(9, 0.1f, 0.2f, 0.5f, 4, 4, 0);
        set(10, -1.0f, 0.1f, 0.0f, 3, 4, 0);
        set(11, -0.5f, 0.0f, 0.0f, 3, 4, 0);
        set(12, 0.125f, 0.05f, 0.0f, 3, 4, 0);
        set(13, 0.45f, 0.3f, 0.0f, 3, 4, 0);
        set(14, 0.2f, 0.3f, 0.9f, 15, 4, 0);
        set(15, 0.0f, 0.025f, 0.9f, 15, 4, 0);
        set(16, 0.0f, 0.025f, 0.8f, 7, 7, 0);
        set(17, 0.45f, 0.3f, 2.0f, 7, 7, 0);
        set(18, 0.45f, 0.3f, 0.7f, 3, 4, 0);
        set(19, 0.45f, 0.3f, 0.25f, 3, 4, 2);
        set(20, 0.8f, 0.3f, 0.2f, 3, 4, 257);
        set(21, 0.1f, 0.2f, 0.95f, 3, 4, 0);
        set(22, 0.45f, 0.3f, 0.95f, 3, 4, 0);
        set(23, 0.1f, 0.2f, 0.95f, 3, 4, 0);
        set(24, -1.8f, 0.1f, 0.5f, 3, 4, 0);
        set(25, 0.1f, 0.8f, 0.2f, 1, 1, 0);
        set(26, 0.0f, 0.025f, 0.05f, 7, 7, 0);
        set(27, 0.1f, 0.2f, 0.6f, 3, 4, 0);
        set(28, 0.45f, 0.3f, 0.6f, 3, 4, 0);
        set(29, 0.1f, 0.2f, 0.7f, 3, 4, 0);
        set(30, 0.2f, 0.2f, -0.5f, 3, 4, 2);
        set(31, 0.45f, 0.3f, -0.5f, 3, 4, 2);
        set(32, 0.2f, 0.2f, 0.3f, 3, 4, 258);
        set(33, 0.45f, 0.3f, 0.3f, 3, 4, 258);
        set(34, 1.0f, 0.5f, 0.2f, 3, 4, 257);
        set(35, 0.125f, 0.05f, 1.2f, 3, 4, 0);
        set(36, 1.5f, 0.025f, 1.0f, 3, 4, 0);
        set(37, 0.1f, 0.2f, 2.0f, 21, 18, 4);
        set(38, 1.5f, 0.025f, 2.0f, 21, 18, 4);
        set(39, 1.5f, 0.025f, 2.0f, 21, 18, 4);
        set(127, 0.1f, 0.2f, 0.5f, 3, 4, 0);
        set(129, 0.125f, 0.05f, 0.8f, 3, 4, 0);
        set(130, 0.225f, 0.25f, 2.0f, 7, 7, 0);
        set(131, 1.0f, 0.5f, 0.2f, 3, 4, 513);
        set(132, 0.1f, 0.4f, 0.7f, 3, 4, 0);
        set(133, 0.3f, 0.4f, 0.25f, 3, 4, 2);
        set(134, -0.1f, 0.3f, 0.8f, 3, 4, 3);
        set(140, 0.425f, 0.45000002f, 0.0f, 16, 4, 0);
        set(149, 0.2f, 0.4f, 0.95f, 3, 4, 0);
        set(151, 0.2f, 0.4f, 0.95f, 3, 4, 0);
        set(155, 0.2f, 0.4f, 0.6f, 3, 4, 0);
        set(156, 0.55f, 0.5f, 0.6f, 3, 4, 0);
        set(157, 0.2f, 0.4f, 0.7f, 3, 4, 0);
        set(158, 0.3f, 0.4f, -0.5f, 3, 4, 2);
        set(160, 0.2f, 0.2f, 0.25f, 3, 4, 514);
        set(161, 0.2f, 0.2f, 0.25f, 3, 4, 514);
        set(162, 1.0f, 0.5f, 0.2f, 3, 4, 513);
        set(163, 0.3625f, 1.225f, 1.1f, 3, 4, 5);
        set(164, 1.05f, 1.2125001f, 1.0f, 3, 4, 5);
        set(165, 0.1f, 0.2f, 2.0f, 21, 18, 4);
        set(166, 0.45f, 0.3f, 2.0f, 21, 18, 4);
        set(167, 0.45f, 0.3f, 2.0f, 21, 18, 4);
    }
    static final int[] ALL = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
        127, 129, 130, 131, 132, 133, 134, 140, 149, 151, 155, 156, 157, 158, 160, 161, 162, 163, 164, 165, 166, 167
    };
    public static void main(String[] args) {
        for (int id : ALL) {
            P p = T[id];
            System.out.printf("%08x\n", id);
            System.out.printf("%08x\n", Float.floatToRawIntBits(p.baseHeight));
            System.out.printf("%08x\n", Float.floatToRawIntBits(p.heightVariation));
            System.out.printf("%08x\n", Float.floatToRawIntBits(p.temperature));
            System.out.printf("%04x\n", p.top);
            System.out.printf("%04x\n", p.filler);
            System.out.printf("%04x\n", p.genTerrainType);
        }
    }
}
