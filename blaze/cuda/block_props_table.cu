/* CUDA driver for block_props_table - host-side loop (table is static const; same MC_HD core). */
#include <cstdio>
#include <cstdlib>
#include "../core/block_props_table.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    for (int i = 0; i < BPT_NKEEP; ++i) {
        int id = BPT_KEEP_IDS[i];
        BptProps p = mc_bpt_props(id);
        printf("%08x\n", (unsigned)id);
        printf("%08x\n", (unsigned)bpt_pack_hardness(p.hardness));
        printf("%04x\n", (unsigned)((p.light_emit << 8) | p.light_opacity));
        printf("%04x\n", (unsigned)p.flags);
    }
    return 0;
}
