// GOLDEN: verbatim-logic port of MC 1.11.2 FaceBakery.getFacingFromVertexData() + applyFacing().
// Source: src/net/minecraft/client/renderer/block/model/FaceBakery.java:242 getFacingFromVertexData,
//         :283 applyFacing. As wired in makeBakedQuad (:65/:69): the chosen facing is computed from the
//         data; here applyFacing is fed a SEPARATE target facing input so both methods are exercised.
// Bodies copied UNCHANGED. Standalone-uncompilable deps reduced without changing the math:
//   - org.lwjgl.util.vector.Vector3f -> inlined `V3` with the EXACT LWJGL 2.9.2 sub/cross/dot semantics
//     (validated bitwise against the real lwjgl_util jar; see README).
//   - net.minecraft.util.EnumFacing -> a local enum with the same values() order (D-U-N-S-W-E),
//     getIndex() (0..5), and getDirectionVec().
//   - net.minecraft.client.renderer.EnumFaceDirection -> a 6x4x3 index table (xIndex,yIndex,zIndex per
//     vertex) transcribed verbatim from the enum; Constants indices D0 U1 N2 S3 W4 E5.
//   - MathHelper.epsilonEquals(a,b) = Math.abs(b-a) < 1.0E-5F (inlined verbatim).
// Input  (per line): 28 hex ints (vertexData) + targetFacing(0-5) = 29 tokens
// Output (per line): chosenFacingIndex + 28 hex ints (the reordered vertexData)
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;

public class Golden {
    // EnumFacing index constants D-U-N-S-W-E
    static final int DOWN = 0, UP = 1, NORTH = 2, SOUTH = 3, WEST = 4, EAST = 5;
    // values() order = declaration order = D,U,N,S,W,E
    static final int[][] DIR_VEC = {
        {0, -1, 0},  // DOWN
        {0, 1, 0},   // UP
        {0, 0, -1},  // NORTH
        {0, 0, 1},   // SOUTH
        {-1, 0, 0},  // WEST
        {1, 0, 0},   // EAST
    };

    // EnumFaceDirection: per facing (D-U-N-S-W-E), 4 vertices' (xIndex,yIndex,zIndex) into afloat.
    static final int[][][] FACE_DIR = {
        // DOWN
        {{WEST, DOWN, SOUTH}, {WEST, DOWN, NORTH}, {EAST, DOWN, NORTH}, {EAST, DOWN, SOUTH}},
        // UP
        {{WEST, UP, NORTH}, {WEST, UP, SOUTH}, {EAST, UP, SOUTH}, {EAST, UP, NORTH}},
        // NORTH
        {{EAST, UP, NORTH}, {EAST, DOWN, NORTH}, {WEST, DOWN, NORTH}, {WEST, UP, NORTH}},
        // SOUTH
        {{WEST, UP, SOUTH}, {WEST, DOWN, SOUTH}, {EAST, DOWN, SOUTH}, {EAST, UP, SOUTH}},
        // WEST
        {{WEST, UP, NORTH}, {WEST, DOWN, NORTH}, {WEST, DOWN, SOUTH}, {WEST, UP, SOUTH}},
        // EAST
        {{EAST, UP, SOUTH}, {EAST, DOWN, SOUTH}, {EAST, DOWN, NORTH}, {EAST, UP, NORTH}},
    };

    // --- inlined LWJGL 2.9.2 Vector3f (only the static ops used) ---
    static final class V3 {
        float x, y, z;
        V3() {}
        V3(float x, float y, float z) { this.x = x; this.y = y; this.z = z; }
        static V3 sub(V3 left, V3 right, V3 dest) {
            dest.x = left.x - right.x; dest.y = left.y - right.y; dest.z = left.z - right.z; return dest;
        }
        static V3 cross(V3 left, V3 right, V3 dest) {
            dest.x = left.y * right.z - left.z * right.y;
            dest.y = right.x * left.z - left.x * right.z;
            dest.z = left.x * right.y - left.y * right.x;
            return dest;
        }
        static float dot(V3 left, V3 right) {
            return left.x * right.x + left.y * right.y + left.z * right.z;
        }
    }

    // --- verbatim getFacingFromVertexData (Vector3f -> V3, EnumFacing -> int index) ---
    static int getFacingFromVertexData(int[] faceData) {
        V3 vector3f = new V3(Float.intBitsToFloat(faceData[0]), Float.intBitsToFloat(faceData[1]), Float.intBitsToFloat(faceData[2]));
        V3 vector3f1 = new V3(Float.intBitsToFloat(faceData[7]), Float.intBitsToFloat(faceData[8]), Float.intBitsToFloat(faceData[9]));
        V3 vector3f2 = new V3(Float.intBitsToFloat(faceData[14]), Float.intBitsToFloat(faceData[15]), Float.intBitsToFloat(faceData[16]));
        V3 vector3f3 = new V3();
        V3 vector3f4 = new V3();
        V3 vector3f5 = new V3();
        V3.sub(vector3f, vector3f1, vector3f3);
        V3.sub(vector3f2, vector3f1, vector3f4);
        V3.cross(vector3f4, vector3f3, vector3f5);
        float f = (float) Math.sqrt((double) (vector3f5.x * vector3f5.x + vector3f5.y * vector3f5.y + vector3f5.z * vector3f5.z));
        vector3f5.x /= f;
        vector3f5.y /= f;
        vector3f5.z /= f;
        int enumfacing = -1;
        float f1 = 0.0F;
        for (int ef = 0; ef < 6; ++ef) {  // EnumFacing.values() order D-U-N-S-W-E
            int[] vec3i = DIR_VEC[ef];
            V3 vector3f6 = new V3((float) vec3i[0], (float) vec3i[1], (float) vec3i[2]);
            float f2 = V3.dot(vector3f5, vector3f6);
            if (f2 >= 0.0F && f2 > f1) {
                f1 = f2;
                enumfacing = ef;
            }
        }
        if (enumfacing == -1) return UP;
        return enumfacing;
    }

    static boolean epsilonEquals(float a, float b) {
        return (Math.abs(b - a)) < 1.0E-5F;
    }

    // --- verbatim applyFacing (EnumFaceDirection -> FACE_DIR table) ---
    static void applyFacing(int[] p1, int targetFacing) {
        int[] aint = new int[p1.length];
        System.arraycopy(p1, 0, aint, 0, p1.length);
        float[] afloat = new float[6];
        afloat[WEST] = 999.0F;
        afloat[DOWN] = 999.0F;
        afloat[NORTH] = 999.0F;
        afloat[EAST] = -999.0F;
        afloat[UP] = -999.0F;
        afloat[SOUTH] = -999.0F;

        for (int i = 0; i < 4; ++i) {
            int j = 7 * i;
            float f = Float.intBitsToFloat(aint[j]);
            float f1 = Float.intBitsToFloat(aint[j + 1]);
            float f2 = Float.intBitsToFloat(aint[j + 2]);
            if (f < afloat[WEST]) afloat[WEST] = f;
            if (f1 < afloat[DOWN]) afloat[DOWN] = f1;
            if (f2 < afloat[NORTH]) afloat[NORTH] = f2;
            if (f > afloat[EAST]) afloat[EAST] = f;
            if (f1 > afloat[UP]) afloat[UP] = f1;
            if (f2 > afloat[SOUTH]) afloat[SOUTH] = f2;
        }

        int[][] enumfacedirection = FACE_DIR[targetFacing];

        for (int i1 = 0; i1 < 4; ++i1) {
            int j1 = 7 * i1;
            int[] vi = enumfacedirection[i1];
            float f8 = afloat[vi[0]];
            float f3 = afloat[vi[1]];
            float f4 = afloat[vi[2]];
            p1[j1] = Float.floatToRawIntBits(f8);
            p1[j1 + 1] = Float.floatToRawIntBits(f3);
            p1[j1 + 2] = Float.floatToRawIntBits(f4);
            for (int k = 0; k < 4; ++k) {
                int l = 7 * k;
                float f5 = Float.intBitsToFloat(aint[l]);
                float f6 = Float.intBitsToFloat(aint[l + 1]);
                float f7 = Float.intBitsToFloat(aint[l + 2]);
                if (epsilonEquals(f8, f5) && epsilonEquals(f3, f6) && epsilonEquals(f4, f7)) {
                    p1[j1 + 4] = aint[l + 4];
                    p1[j1 + 4 + 1] = aint[l + 4 + 1];
                }
            }
        }
    }
    // --- /verbatim ---

    public static void main(String[] args) throws IOException {
        BufferedReader br = new BufferedReader(new InputStreamReader(System.in));
        StringBuilder sb = new StringBuilder();
        String line;
        while ((line = br.readLine()) != null) {
            line = line.trim();
            if (line.isEmpty()) continue;
            String[] tok = line.split("\\s+");
            if (tok.length != 29) continue;
            int[] vd = new int[28];
            for (int i = 0; i < 28; ++i) vd[i] = (int) Long.parseLong(tok[i], 16);
            int target = Integer.parseInt(tok[28]);

            int chosen = getFacingFromVertexData(vd);
            applyFacing(vd, target);

            sb.append(chosen);
            for (int i = 0; i < 28; ++i)
                sb.append(' ').append(Long.toHexString(vd[i] & 0xFFFFFFFFL));
            sb.append('\n');
        }
        System.out.print(sb);
    }
}
