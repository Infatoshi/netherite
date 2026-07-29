/* CANDIDATE: C port of MC 1.11.2 ModelBakery.bakeModel() assembly/dispatch + SimpleBakedModel.Builder
 * bucketing. Must BITWISE-match golden/Golden.java. Pure integer bucketing; no FP.
 * Dispatch (verbatim): if (cull == -1 || !isInteger) general.add(quad)
 *                      else faceQuads[rot].add(quad)        // rot = modelRotation.rotate(cullFace)
 * makeBakedQuad -> quad id input; ModelRotation.rotate -> precomputed `rot` input (see README).
 * Input  (per line = one model): isInteger nFaces  then nFaces triples: cull rot quad
 * Output (7 lines per model): label + space-separated quad ids, order D U N S W E GEN. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *LABELS[6] = {"D", "U", "N", "S", "W", "E"};

int main(void) {
    size_t cap = 1 << 16;
    char *line = malloc(cap);
    while (getline(&line, &cap, stdin) != -1) {
        char *p = line;
        /* tokenizer over whitespace via strtol with endptr */
        char *end;
        long isInteger = strtol(p, &end, 10);
        if (end == p) continue;  /* blank line */
        p = end;
        long nFaces = strtol(p, &end, 10);
        p = end;

        /* buckets: 6 face lists + general; collect quad ids in order */
        int *face[6];
        int faceN[6];
        for (int i = 0; i < 6; ++i) { face[i] = malloc(sizeof(int) * (nFaces + 1)); faceN[i] = 0; }
        int *general = malloc(sizeof(int) * (nFaces + 1));
        int genN = 0;

        for (long fi = 0; fi < nFaces; ++fi) {
            long cull = strtol(p, &end, 10); p = end;
            long rot = strtol(p, &end, 10); p = end;
            long quad = strtol(p, &end, 10); p = end;
            if (cull == -1 || !isInteger) {
                general[genN++] = (int) quad;
            } else {
                face[rot][faceN[rot]++] = (int) quad;
            }
        }

        for (int i = 0; i < 6; ++i) {
            fputs(LABELS[i], stdout);
            for (int k = 0; k < faceN[i]; ++k) printf(" %d", face[i][k]);
            putchar('\n');
        }
        fputs("GEN", stdout);
        for (int k = 0; k < genN; ++k) printf(" %d", general[k]);
        putchar('\n');

        for (int i = 0; i < 6; ++i) free(face[i]);
        free(general);
    }
    free(line);
    return 0;
}
