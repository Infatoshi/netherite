/* A frame row with one piece of the client view state taken away, for the
 * negative controls of the C5 render scenes (out/java/render/cs_*): reads a
 * RenderStateProbe row, resets the named state (fov: fovModifierHand 1;
 * portal: timeInPortal 0; bolt: lastLightningBolt 0; hurt: hurtTime 0), runs
 * renderstate_compute and prints what the frame's camera, lightmap, sky and
 * fog become, as a JSON object in the probe's bit format for jq to splice
 * over the row's out and g blocks.
 *
 *   clientstate_row ROW.jsonl fov|portal|bolt|hurt */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/jmath.h"
#include "../engine/renderstate.h"
#include "../engine/tape.h"

static void farr(const char *k, const float *v, int n, int last)
{
    printf("\"%s\":[", k);
    for (int i = 0; i < n; ++i) {
        uint32_t b;
        memcpy(&b, &v[i], 4);
        printf("%s\"f:%08x\"", i ? "," : "", b);
    }
    printf("]%s", last ? "" : ",");
}

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: clientstate_row ROW.jsonl fov|portal|bolt|hurt\n"); return 2; }
    jmath_init();
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 1;
    char *line = NULL;
    size_t cap = 0;
    if (getline(&line, &cap, f) <= 0) return 1;
    fclose(f);
    struct jval *row = json_parse(line);
    struct rs_in in;
    if (!row || !renderstate_in_from_row(row, &in)) { fprintf(stderr, "clientstate_row: unreadable row\n"); return 1; }
    const char *m = argv[2];
    if (!strcmp(m, "fov")) in.fmh = in.fmhp = 1.0F;
    else if (!strcmp(m, "portal")) in.portal = in.pportal = 0.0F;
    else if (!strcmp(m, "bolt")) in.lbolt = 0;
    else if (!strcmp(m, "hurt")) in.hurt = 0;
    else { fprintf(stderr, "clientstate_row: unknown state %s\n", m); return 2; }
    struct rs_out o;
    renderstate_compute(&in, &o);
    printf("{");
    farr("proj", o.proj, 16, 0);
    farr("mv", o.mv, 16, 0);
    printf("\"lm\":[");
    for (int i = 0; i < 256; ++i) printf("%s%d", i ? "," : "", o.lm[i]);
    printf("],\"sky\":[");
    for (int i = 0; i < 3; ++i) {
        uint64_t b;
        memcpy(&b, &o.sky[i], 8);
        printf("%s\"d:%016llx\"", i ? "," : "", (unsigned long long)b);
    }
    printf("],");
    farr("fog", o.gfogc, 4, 1);
    printf("}\n");
    json_free(row);
    return 0;
}
