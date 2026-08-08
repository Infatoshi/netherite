/* Golden.java - verbatim-Minecraft-1.11.2 biome grass/foliage/water colours.
 *
 * Self-contained (no net.minecraft classpath): the colour math is copied
 * VERBATIM from the oracle:
 *   - ColorizerGrass.getGrassColor / ColorizerFoliage.getFoliageColor
 *   - Biome.getGrassColorAtPos / getFoliageColorAtPos (clamp temp/rainfall)
 *   - Biome.registerBiomes temperature/rainfall/waterColor
 *   - BiomeSwamp / BiomeForest(ROOFED) / BiomeMesa colour overrides
 * The 256x256 colormap buffers are read from the client jar the SAME way MC does
 * (BufferedImage.getRGB(0,0,w,h,aint,0,w) -> aint[y*w+x]) via ImageIO, an
 * independent PNG decode path from the Python extractor, so a match validates
 * both the extractor and the C index math.
 *
 * Emits lines: "<biomeId> <grass> <foliage> <water>" (decimal 0xRRGGBB ints).
 * Grass for swampland (6/134) is emitted as -1 (noise-dependent; asserted in C
 * to be one of the two swamp constants instead).
 *
 * Build/run: javac Golden.java && java Golden <path-to-minecraft-1.11.2.jar>
 */
import java.awt.image.BufferedImage;
import java.io.InputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import javax.imageio.ImageIO;

public class Golden {
    static int[] grassBuffer;    // 65536, ARGB row-major
    static int[] foliageBuffer;

    // VERBATIM net.minecraft.world.ColorizerGrass.getGrassColor
    static int getGrassColor(double temperature, double humidity) {
        humidity = humidity * temperature;
        int i = (int)((1.0D - temperature) * 255.0D);
        int j = (int)((1.0D - humidity) * 255.0D);
        int k = j << 8 | i;
        return k > grassBuffer.length ? -65281 : grassBuffer[k];
    }
    // VERBATIM net.minecraft.world.ColorizerFoliage.getFoliageColor
    static int getFoliageColor(double temperature, double humidity) {
        humidity = humidity * temperature;
        int i = (int)((1.0D - temperature) * 255.0D);
        int j = (int)((1.0D - humidity) * 255.0D);
        return foliageBuffer[j << 8 | i];
    }
    // MathHelper.clamp(float,float,float)
    static float clamp(float num, float min, float max) {
        return num < min ? min : (num > max ? max : num);
    }

    static int[] readImageData(ZipFile zf, String path) throws Exception {
        ZipEntry e = zf.getEntry(path);
        try (InputStream in = zf.getInputStream(e)) {
            BufferedImage img = ImageIO.read(in);
            int w = img.getWidth(), h = img.getHeight();
            int[] aint = new int[w * h];
            img.getRGB(0, 0, w, h, aint, 0, w);   // exactly TextureUtil.readImageData
            return aint;
        }
    }

    // Per-biome temperature (Biome.registerBiomes setTemperature; default 0.5).
    static float temperature(int id) {
        switch (id) {
            case 1: case 6: case 16: case 129: case 134: return 0.8f;
            case 2: case 8: case 17: case 37: case 38: case 39:
            case 130: case 165: case 166: case 167: return 2.0f;
            case 3: case 20: case 25: case 34: case 131: case 162: return 0.2f;
            case 4: case 18: case 29: case 132: case 157: return 0.7f;
            case 5: case 19: case 133: case 160: case 161: return 0.25f;
            case 14: case 15: return 0.9f;
            case 21: case 22: case 23: case 149: case 151: return 0.95f;
            case 27: case 28: case 155: case 156: return 0.6f;
            case 30: case 31: case 158: return -0.5f;
            case 32: case 33: return 0.3f;
            case 35: return 1.2f;
            case 36: case 164: return 1.0f;
            case 26: return 0.05f;
            case 163: return 1.1f;
            default: return 0.5f;  // ocean(0)/river(7)/ice/void/...
        }
    }
    // Per-biome rainfall (setRainfall; default 0.5).
    static float rainfall(int id) {
        switch (id) {
            case 1: case 16: case 129: return 0.4f;
            case 2: case 8: case 17: case 35: case 36: case 37: case 38: case 39:
            case 130: case 163: case 164: case 165: case 166: case 167: return 0.0f;
            case 3: case 20: case 25: case 26: case 34: case 131: case 162: return 0.3f;
            case 4: case 5: case 18: case 19: case 29: case 32: case 33:
            case 132: case 133: case 157: case 160: case 161: return 0.8f;
            case 6: case 21: case 22: case 134: case 149: return 0.9f;
            case 23: case 151: return 0.8f;
            case 14: case 15: return 1.0f;
            case 27: case 28: case 155: case 156: return 0.6f;
            case 30: case 31: case 158: return 0.4f;
            default: return 0.5f;
        }
    }
    static int waterColor(int id) {
        return (id == 6 || id == 134) ? 14745518 : 16777215;
    }
    static boolean isMesa(int id) {
        return id == 37 || id == 38 || id == 39 || id == 165 || id == 166 || id == 167;
    }

    // Biome.getGrassColorAtPos + subclass overrides (swamp grass -> -1 sentinel).
    static int grassColorAtPos(int id) {
        if (id == 6 || id == 134) return -1;        // BiomeSwamp: noise-dependent
        if (isMesa(id)) return 9470285;             // BiomeMesa
        double d0 = clamp(temperature(id), 0.0F, 1.0F);
        double d1 = clamp(rainfall(id), 0.0F, 1.0F);
        int i = getGrassColor(d0, d1) & 0xFFFFFF;
        if (id == 29 || id == 157) return ((i & 16711422) + 2634762) >> 1;  // ROOFED
        return i;
    }
    static int foliageColorAtPos(int id) {
        if (id == 6 || id == 134) return 6975545;   // BiomeSwamp
        if (isMesa(id)) return 10387789;            // BiomeMesa
        double d0 = clamp(temperature(id), 0.0F, 1.0F);
        double d1 = clamp(rainfall(id), 0.0F, 1.0F);
        return getFoliageColor(d0, d1) & 0xFFFFFF;
    }

    public static void main(String[] args) throws Exception {
        try (ZipFile zf = new ZipFile(args[0])) {
            grassBuffer   = readImageData(zf, "assets/minecraft/textures/colormap/grass.png");
            foliageBuffer = readImageData(zf, "assets/minecraft/textures/colormap/foliage.png");
        }
        int[] biomes = {0,1,2,3,4,5,6,14,21,27,29,30,35,37,131,133,134,160};
        for (int id : biomes) {
            System.out.println(id + " " + grassColorAtPos(id) + " " +
                               foliageColorAtPos(id) + " " + waterColor(id));
        }
    }
}
