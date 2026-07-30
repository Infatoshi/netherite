// GOLDEN: verbatim-logic port of MC 1.11.2 ModelBakery.bakeModel() assembly/dispatch loop +
// SimpleBakedModel.Builder bucketing.
// Source: src/net/minecraft/client/renderer/block/model/ModelBakery.java:673 bakeModel
//         + SimpleBakedModel.java Builder (addGeneralQuad/addFaceQuad/makeBakedModel).
// The per-element/per-face dispatch is copied UNCHANGED:
//   if (blockpartface.cullFace == null || !TRSRTransformation.isInteger(modelRotationIn.getMatrix()))
//       builder.addGeneralQuad(quad);
//   else
//       builder.addFaceQuad(modelRotationIn.rotate(blockpartface.cullFace), quad);
// The builder keeps a general List + an EnumMap<EnumFacing,List> (6 buckets), appending in iteration
// order. makeBakedModel returns those buckets.
//
// Two upstream deps are reduced to INPUTS (this kernel is the ASSEMBLY, not quad baking or rotation):
//   - this.makeBakedQuad(...)        -> a quad id (the assembly never inspects the int[28]).
//   - modelRotationIn.rotate(cull)   -> a precomputed `rotatedCullFace` per face (ModelRotation's
//     matrix-based facing rotation is its own concern; see kernels 31-34 for the quad baking).
// So a "model" record = isIntegerMatrix flag + a face list, each face = (cullFace, rotatedCullFace,
// quadId). Faces are processed in input order (the deterministic iteration order of the record).
//
// Input  (per line = one model): isInteger nFaces  then nFaces triples: cull rot quad
//   cull = -1 for null (no cullface) else 0-5 (D-U-N-S-W-E); rot = 0-5; quad = int id.
// Output (7 lines per model): bucket label + space-separated quad ids, in order D U N S W E GEN.
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.List;

public class Golden {
    static final String[] LABELS = {"D", "U", "N", "S", "W", "E"};

    public static void main(String[] args) throws IOException {
        BufferedReader br = new BufferedReader(new InputStreamReader(System.in));
        StringBuilder sb = new StringBuilder();
        String line;
        while ((line = br.readLine()) != null) {
            line = line.trim();
            if (line.isEmpty()) continue;
            String[] tok = line.split("\\s+");
            int p = 0;
            boolean isInteger = Integer.parseInt(tok[p++]) != 0;
            int nFaces = Integer.parseInt(tok[p++]);

            // builderGeneralQuads + EnumMap<EnumFacing,List> (6 buckets in D-U-N-S-W-E index order)
            List<Integer> general = new ArrayList<Integer>();
            List<List<Integer>> faceQuads = new ArrayList<List<Integer>>();
            for (int i = 0; i < 6; ++i) faceQuads.add(new ArrayList<Integer>());

            for (int fi = 0; fi < nFaces; ++fi) {
                int cull = Integer.parseInt(tok[p++]);
                int rot = Integer.parseInt(tok[p++]);
                int quad = Integer.parseInt(tok[p++]);
                // --- verbatim dispatch ---
                if (cull == -1 || !isInteger) {
                    general.add(quad);                 // addGeneralQuad
                } else {
                    faceQuads.get(rot).add(quad);      // addFaceQuad(rotate(cullFace), quad)
                }
            }

            for (int i = 0; i < 6; ++i) {
                sb.append(LABELS[i]);
                for (int q : faceQuads.get(i)) sb.append(' ').append(q);
                sb.append('\n');
            }
            sb.append("GEN");
            for (int q : general) sb.append(' ').append(q);
            sb.append('\n');
        }
        System.out.print(sb);
    }
}
