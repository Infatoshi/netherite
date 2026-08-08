#include <stdio.h>
#include <string.h>

#include "item_block_place.h"

int main(int argc, char **argv) {
    FILE *file;
    char line[1024], result[16], sound[512];
    int rows = 0, positives = 0, negatives = 0;
    if (argc != 2 || !(file = fopen(argv[1], "r"))) {
        fprintf(stderr, "usage: %s JAVA_GOLDEN\n", argv[0]);
        return 2;
    }
    while (fgets(line, sizeof line, file)) {
        int item, placed_block, stack_meta, face, yaw, high, geometry, allow;
        int state, count, final_meta, fields, entity_dir, native_meta;
        if (line[0] != 'I' || line[1] != ' ') continue;
        fields = sscanf(line,
            "I %d %d %d %d %d %d %d %d %15s %d %d %d %511s",
            &item, &placed_block, &stack_meta, &face, &yaw, &high,
            &geometry, &allow,
            result, &state, &count, &final_meta, sound);
        if (fields != 13) {
            fprintf(stderr, "malformed row %d: %s", rows + 1, line);
            return 1;
        }
        ++rows;
        if (strcmp(result, "SUCCESS")) {
            int intrinsic_face_fail = (item == 324 || item == 330
                || (item >= 427 && item <= 431) || item == 295
                || item == 361 || item == 362 || item == 372
                || item == 391 || item == 392 || item == 435
                || item == 331)
                ? face != IBP_UP
                : ((item >= 290 && item <= 294) || item == 256
                    || item == 269 || item == 273 || item == 277
                    || item == 284) ? face == IBP_DOWN : 0;
            int unchanged_state = ((item >= 290 && item <= 294)
                    || item == 256 || item == 269 || item == 273
                    || item == 277 || item == 284) ? 2 : 0;
            if ((!intrinsic_face_fail && allow) || state != unchanged_state
                    || count != 2
                    || final_meta != stack_meta || strcmp(sound, "-")) {
                fprintf(stderr, "negative row changed: %s", line);
                return 1;
            }
            ++negatives;
            continue;
        }
        if (!allow && !(item == 324 || item == 330
                || (item >= 427 && item <= 431)
                || item == 295 || item == 361 || item == 362
                || item == 372 || item == 391 || item == 392
                || item == 435 || (item >= 290 && item <= 294)
                || item == 256 || item == 269 || item == 273
                || item == 277 || item == 284)) {
            fprintf(stderr, "forced-negative row succeeded: %s", line);
            return 1;
        }
        entity_dir = geometry == 1 ? IBP_UP : geometry == 2 ? IBP_DOWN
            : ibp_opposite(ibp_horizontal_facing(yaw));
        native_meta = ibp_placed_meta_exact(
            placed_block, face, yaw, high, entity_dir, stack_meta) & 15;
        if (ibp_item_placed_block(item) != placed_block
                || strcmp(result, "SUCCESS")
                || (state & 4095) != placed_block
                || (state >> 12) != native_meta
                || (((item >= 290 && item <= 294) || item == 256
                        || item == 269 || item == 273 || item == 277
                        || item == 284)
                    ? (count != 2 || final_meta != stack_meta + 1)
                    : count != 1)
                || (!strcmp(sound, "-")
                    && !(item == 295 || item == 361 || item == 362
                        || item == 372 || item == 391 || item == 392
                        || item == 435 || item == 331))) {
            fprintf(stderr,
                "item block mismatch row=%d native_meta=%d: %s",
                rows, native_meta, line);
            return 1;
        }
        ++positives;
    }
    fclose(file);
    if (rows != 52635 || positives != 50256 || negatives != 2379) {
        fprintf(stderr, "incomplete corpus rows=%d pos=%d neg=%d\n",
            rows, positives, negatives);
        return 1;
    }
    printf("item block oracle: PASS (%d positive, %d negative rows)\n",
        positives, negatives);
    return 0;
}
