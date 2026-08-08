/* CANDIDATE: pure-C port of MC 1.11.2 Chunk.generateSkylightMap()
 *   (src/net/minecraft/world/chunk/Chunk.java:238 generateSkylightMap).
 *
 * Golden CAPTURED FROM REAL MINECRAFT (capture_mode "live-hook", NetheriteMod 'capture_skylight'):
 * for the player's chunk, skylight in all non-null storages is zeroed, then the REAL
 * generateSkylightMap() runs, then heightMap + per-cell skylight nibbles are read back.
 * topFilledSegment (i) is per-chunk constant; nY = i + 16.
 *
 * Input record (one line per column, 256 columns), from golden/inputs.txt:
 *     i hasSky  op[0] nn[0] op[1] nn[1] ... op[nY-1] nn[nY-1]
 *   i      : topFilledSegment (chunk constant; nY = i+16)
 *   hasSky : world.provider.hasSkyLight() (0/1)
 *   op[y]  : Chunk.getBlockLightOpacity(j,y,k)
 *   nn[y]  : storageArrays[y>>4] != NULL_BLOCK_STORAGE (0/1)
 *
 * Logic (verbatim per-column):
 *   heightMap: for l=i+16; l>0; --l: if op[l-1]!=0 { heightMap=l; break; }
 *   skylight (only if hasSky): k1=15; i1=nY-1;
 *     loop: j1=op[i1]; if(j1==0 && k1!=15) j1=1; k1-=j1;
 *           if(k1>0 && nn[i1]) sky[i1]=k1;  --i1; if(i1<=0||k1<=0) break;
 *     all other cells (and all cells if !hasSky) remain 0.
 *
 * Output per column: heightMap (1 line) then sky[0..nY-1] (nY lines).
 * Must BITWISE-match golden/golden.txt. */
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    int i, hasSky;
    while (scanf("%d %d", &i, &hasSky) == 2) {
        int nY = i + 16;
        int *op = (int*)malloc(sizeof(int) * nY);
        int *nn = (int*)malloc(sizeof(int) * nY);
        int *sky = (int*)calloc(nY, sizeof(int));
        for (int y = 0; y < nY; y++) {
            if (scanf("%d %d", &op[y], &nn[y]) != 2) { return 1; }
        }
        /* heightMap */
        int heightMap = 0;
        for (int l = i + 16; l > 0; --l) {
            if (op[l - 1] != 0) { heightMap = l; break; }
        }
        /* skylight ladder */
        if (hasSky) {
            int k1 = 15, i1 = nY - 1;
            while (1) {
                int j1 = op[i1];
                if (j1 == 0 && k1 != 15) j1 = 1;
                k1 -= j1;
                if (k1 > 0 && nn[i1]) sky[i1] = k1;
                --i1;
                if (i1 <= 0 || k1 <= 0) break;
            }
        }
        printf("%d\n", heightMap);
        for (int y = 0; y < nY; y++) printf("%d\n", sky[y]);
        free(op); free(nn); free(sky);
    }
    return 0;
}
