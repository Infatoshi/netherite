/* GOLDEN: verbatim decompiled MC World.getCombinedLight pure packing math.
 * Source: src/net/minecraft/world/World.java:955 getCombinedLight(BlockPos, int).
 * The real method reads sky/block light from neighbours; that world read is OUT OF SCOPE for this
 * pure kernel. We feed the two already-resolved light values (i = sky, j = block) and the override
 * (lightValue) directly, and port ONLY the pure tail of the method verbatim:
 *     if (j < lightValue) j = lightValue;
 *     return i << 20 | j << 4;
 * Driver: each input line = "skyLight blockLight override" (decimal int32).
 * Prints the packed combined-light int (decimal). */
import java.io.BufferedReader;
import java.io.InputStreamReader;

public class Golden {
    // verbatim pure tail of World.getCombinedLight (i = sky neighbour light, j = block neighbour light)
    private static int getCombinedLight(int i, int j, int lightValue) {
        if (j < lightValue) {
            j = lightValue;
        }

        return i << 20 | j << 4;
    }

    public static void main(String[] args) throws Exception {
        BufferedReader br = new BufferedReader(new InputStreamReader(System.in));
        StringBuilder sb = new StringBuilder();
        String line;
        while ((line = br.readLine()) != null) {
            if (line.isEmpty()) continue;
            String[] t = line.trim().split("\\s+");
            int sky = Integer.parseInt(t[0]);
            int block = Integer.parseInt(t[1]);
            int override = Integer.parseInt(t[2]);
            sb.append(getCombinedLight(sky, block, override)).append('\n');
        }
        System.out.print(sb);
    }
}
