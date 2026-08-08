#include <stdio.h>

#include "items_tools_armor.h"

int main(int argc, char **argv) {
    FILE *file;
    char line[128];
    int h = 0, c = 0, d = 0;
    if (argc != 2 || !(file = fopen(argv[1], "r"))) {
        fprintf(stderr, "usage: %s JAVA_GOLDEN\n", argv[0]);
        return 2;
    }
    while (fgets(line, sizeof line, file)) {
        int item, a, b, e;
        if (line[0] == 'H') {
            if (sscanf(line, "H %d %d %d %d", &item, &a, &b, &e) != 4
                    || a != ita_tool_harvest_level_exact(item, 1)
                    || b != ita_tool_harvest_level_exact(item, 2)
                    || e != ita_tool_harvest_level_exact(item, 3)) {
                fprintf(stderr, "harvest-level mismatch: %s", line);
                return 1;
            }
            ++h;
        } else if (line[0] == 'C') {
            if (sscanf(line, "C %d %d %d", &item, &a, &b) != 3
                    || b != ita_can_harvest_block(item, a)) {
                fprintf(stderr, "can-harvest mismatch: %s", line);
                return 1;
            }
            ++c;
        } else if (line[0] == 'D') {
            if (sscanf(line, "D %d %d %d", &item, &a, &b) != 3
                    || a != ita_hit_entity_wear(item)
                    || b != ita_destroy_block_wear(item, 1)
                    || (item != 359
                        && ita_destroy_block_wear(item, 0) != 0)) {
                fprintf(stderr, "durability mismatch: %s", line);
                return 1;
            }
            ++d;
        }
    }
    fclose(file);
    if (h != 15 || c != 3776 || d != 26) {
        fprintf(stderr, "incomplete corpus H=%d C=%d D=%d\n", h, c, d);
        return 1;
    }
    printf("tool callback oracle: PASS (%d harvest, %d block, %d wear rows)\n",
        h, c, d);
    return 0;
}
