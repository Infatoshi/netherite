/* Repeat frozen action lanes without changing their temporal sequence. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc != 4 || !strcmp(argv[1], argv[2])) {
        fprintf(stderr, "usage: action_rows INPUT OUTPUT TARGET_LANES\n");
        return 2;
    }
    char *end;
    long lanes = strtol(argv[3], &end, 10);
    if (*end || lanes < 1 || lanes > 4096) return 2;
    FILE *in = fopen(argv[1], "rb");
    uint32_t h[4];
    if (!in || fread(h, sizeof h, 1, in) != 1 ||
        h[0] != 0x41524f57 || h[1] != 1 || !h[2] || !h[3] ||
        h[3] > 4096 || lanes % h[3]) {
        fprintf(stderr, "invalid input or target is not a multiple of input lanes\n");
        if (in) fclose(in);
        return 2;
    }
    size_t row = (size_t)h[3] * 13 * sizeof(double);
    if (fseek(in, 0, SEEK_END) || ftell(in) != (long)(sizeof h + row*h[2]) ||
        fseek(in, sizeof h, SEEK_SET)) {
        fprintf(stderr, "invalid input length\n"); fclose(in); return 2;
    }
    void *data = malloc(row);
    FILE *out = fopen(argv[2], "wbx");
    if (!data || !out) { fprintf(stderr, "allocation or output creation failed\n"); free(data); fclose(in); return 2; }
    uint32_t source_lanes = h[3]; h[3] = (uint32_t)lanes;
    int failed = fwrite(h, sizeof h, 1, out) != 1;
    for (uint32_t t = 0; t < h[2] && !failed; ++t) {
        if (fread(data, row, 1, in) != 1) { failed = 1; break; }
        for (long i = 0; i < lanes / source_lanes; ++i)
            if (fwrite(data, row, 1, out) != 1) { failed = 1; break; }
    }
    if (fclose(out)) failed = 1;
    fclose(in); free(data);
    if (failed) { remove(argv[2]); return 2; }
    printf("steps=%u source_lanes=%u output_lanes=%ld\n", h[2], source_lanes, lanes);
    return 0;
}
