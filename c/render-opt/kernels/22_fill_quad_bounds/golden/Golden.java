// GOLDEN: verbatim-logic port of MC 1.11.2 BlockModelRenderer.fillQuadBounds().
// Source: src/net/minecraft/client/renderer/BlockModelRenderer.java:168 fillQuadBounds().
// The method body is copied UNCHANGED. The only adaptations are removing the standalone-
// uncompilable dependencies (they do not change the computed output):
//   - IBlockState stateIn        -> a boolean `isFullCube` param (only stateIn.isFullCube() is read)
//   - net.minecraft.util.EnumFacing -> a local enum with the SAME index() values (D-U-N-S-W-E = 0..5)
//   - @Nullable annotation dropped
// Input  (per line): 28 hex ints (vertexData, raw int bits) + face(0-5) + isFullCube(0/1)  = 30 tokens
// Output (per line): 12 hex ints (raw float bits of quadBounds) + flag0 + flag1
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.BitSet;

public class Golden {
    // Local stand-in for net.minecraft.util.EnumFacing: same enum order + getIndex() (D-U-N-S-W-E).
    enum EnumFacing {
        DOWN(0), UP(1), NORTH(2), SOUTH(3), WEST(4), EAST(5);
        private final int index;
        EnumFacing(int i) { this.index = i; }
        public int getIndex() { return this.index; }
    }

    // --- verbatim from BlockModelRenderer.fillQuadBounds() (stateIn -> isFullCube, face param int->enum) ---
    private static void fillQuadBounds(boolean isFullCube, int[] vertexData, EnumFacing face, float[] quadBounds, BitSet boundsFlags) {
        float f = 32.0F;
        float f1 = 32.0F;
        float f2 = 32.0F;
        float f3 = -32.0F;
        float f4 = -32.0F;
        float f5 = -32.0F;

        for (int i = 0; i < 4; ++i) {
            float f6 = Float.intBitsToFloat(vertexData[i * 7]);
            float f7 = Float.intBitsToFloat(vertexData[i * 7 + 1]);
            float f8 = Float.intBitsToFloat(vertexData[i * 7 + 2]);
            f = Math.min(f, f6);
            f1 = Math.min(f1, f7);
            f2 = Math.min(f2, f8);
            f3 = Math.max(f3, f6);
            f4 = Math.max(f4, f7);
            f5 = Math.max(f5, f8);
        }

        if (quadBounds != null) {
            quadBounds[EnumFacing.WEST.getIndex()] = f;
            quadBounds[EnumFacing.EAST.getIndex()] = f3;
            quadBounds[EnumFacing.DOWN.getIndex()] = f1;
            quadBounds[EnumFacing.UP.getIndex()] = f4;
            quadBounds[EnumFacing.NORTH.getIndex()] = f2;
            quadBounds[EnumFacing.SOUTH.getIndex()] = f5;
            int j = EnumFacing.values().length;
            quadBounds[EnumFacing.WEST.getIndex() + j] = 1.0F - f;
            quadBounds[EnumFacing.EAST.getIndex() + j] = 1.0F - f3;
            quadBounds[EnumFacing.DOWN.getIndex() + j] = 1.0F - f1;
            quadBounds[EnumFacing.UP.getIndex() + j] = 1.0F - f4;
            quadBounds[EnumFacing.NORTH.getIndex() + j] = 1.0F - f2;
            quadBounds[EnumFacing.SOUTH.getIndex() + j] = 1.0F - f5;
        }

        float f9 = 1.0E-4F;
        float f10 = 0.9999F;

        switch (face) {
            case DOWN:
                boundsFlags.set(1, f >= 1.0E-4F || f2 >= 1.0E-4F || f3 <= 0.9999F || f5 <= 0.9999F);
                boundsFlags.set(0, (f1 < 1.0E-4F || isFullCube) && f1 == f4);
                break;
            case UP:
                boundsFlags.set(1, f >= 1.0E-4F || f2 >= 1.0E-4F || f3 <= 0.9999F || f5 <= 0.9999F);
                boundsFlags.set(0, (f4 > 0.9999F || isFullCube) && f1 == f4);
                break;
            case NORTH:
                boundsFlags.set(1, f >= 1.0E-4F || f1 >= 1.0E-4F || f3 <= 0.9999F || f4 <= 0.9999F);
                boundsFlags.set(0, (f2 < 1.0E-4F || isFullCube) && f2 == f5);
                break;
            case SOUTH:
                boundsFlags.set(1, f >= 1.0E-4F || f1 >= 1.0E-4F || f3 <= 0.9999F || f4 <= 0.9999F);
                boundsFlags.set(0, (f5 > 0.9999F || isFullCube) && f2 == f5);
                break;
            case WEST:
                boundsFlags.set(1, f1 >= 1.0E-4F || f2 >= 1.0E-4F || f4 <= 0.9999F || f5 <= 0.9999F);
                boundsFlags.set(0, (f < 1.0E-4F || isFullCube) && f == f3);
                break;
            case EAST:
                boundsFlags.set(1, f1 >= 1.0E-4F || f2 >= 1.0E-4F || f4 <= 0.9999F || f5 <= 0.9999F);
                boundsFlags.set(0, (f3 > 0.9999F || isFullCube) && f == f3);
        }
    }
    // --- /verbatim ---

    public static void main(String[] args) throws IOException {
        BufferedReader br = new BufferedReader(new InputStreamReader(System.in));
        StringBuilder sb = new StringBuilder();
        EnumFacing[] faces = EnumFacing.values();
        String line;
        while ((line = br.readLine()) != null) {
            line = line.trim();
            if (line.isEmpty()) continue;
            String[] tok = line.split("\\s+");
            if (tok.length != 30) continue;
            int[] vd = new int[28];
            for (int i = 0; i < 28; ++i) vd[i] = (int) Long.parseLong(tok[i], 16);
            int faceOrd = Integer.parseInt(tok[28]);
            boolean fullCube = Integer.parseInt(tok[29]) != 0;

            float[] quadBounds = new float[12];
            BitSet flags = new BitSet();
            fillQuadBounds(fullCube, vd, faces[faceOrd], quadBounds, flags);

            for (int i = 0; i < 12; ++i) {
                if (i > 0) sb.append(' ');
                sb.append(Long.toHexString(Float.floatToRawIntBits(quadBounds[i]) & 0xFFFFFFFFL));
            }
            sb.append(' ').append(flags.get(0) ? 1 : 0);
            sb.append(' ').append(flags.get(1) ? 1 : 0);
            sb.append('\n');
        }
        System.out.print(sb);
    }
}
