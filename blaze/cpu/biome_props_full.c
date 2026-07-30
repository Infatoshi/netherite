/* CPU reference: dump mc_bpf_* for every registered vanilla biome id (exhaustive list, no seed).
 * Seven hex lines per id: id, baseHeight, heightVariation, temperature, top, filler, genTerrainType. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/biome_props_full.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    for (int i = 0; i < BPF_N; ++i) {
        int id = BPF_ALL_IDS[i];
        printf("%08x\n", (unsigned)id);
        printf("%08x\n", (unsigned)bpf_pack_float(mc_bpf_baseHeight(id)));
        printf("%08x\n", (unsigned)bpf_pack_float(mc_bpf_heightVariation(id)));
        printf("%08x\n", (unsigned)bpf_pack_float(mc_bpf_temperature(id)));
        printf("%04x\n", (unsigned)mc_bpf_topBlock(id));
        printf("%04x\n", (unsigned)mc_bpf_fillerBlock(id));
        printf("%04x\n", (unsigned)mc_bpf_genTerrainTypePacked(id));
    }
    return 0;
}
