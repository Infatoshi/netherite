/* CANDIDATE: C port of MC 1.11.2 VisGraph.computeVisibility() (BFS flood-fill over a 16^3 bitset).
 * Must BITWISE-match golden/Golden.java (line = packed 6x6 visibility bitset as a hex long).
 * Output is a SET, so BFS queue order is irrelevant - only connectivity + edge faces matter.
 * Index layout is deliberately NOT natural xyz: index = x<<0 | y<<8 | z<<4  (DX=1, DZ=16, DY=256).
 * The bitSet doubles as the visited-marker during floodFill (verbatim algorithm), so it must be
 * fully re-initialized per record.
 * Input  (per line): count then 64 hex words (16 hex digits); bit i = word[i>>6] bit (i&63).
 * Output (per line): packed visibility bitset as hex long, bit = a + b*6 (faces D-U-N-S-W-E). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define DX 1
#define DZ 16
#define DY 256

/* EnumFacing order D-U-N-S-W-E */
enum { DOWN = 0, UP = 1, NORTH = 2, SOUTH = 3, WEST = 4, EAST = 5 };

static uint8_t opaque[4096];   /* the VisGraph bitSet (opaque cubes + visited marks) */
static int INDEX_OF_EDGES[1352];

static int get_index(int x, int y, int z) { return x << 0 | y << 8 | z << 4; }

static void init_edges(void) {
    int k = 0;
    for (int l = 0; l < 16; ++l)
        for (int i1 = 0; i1 < 16; ++i1)
            for (int j1 = 0; j1 < 16; ++j1)
                if (l == 0 || l == 15 || i1 == 0 || i1 == 15 || j1 == 0 || j1 == 15)
                    INDEX_OF_EDGES[k++] = get_index(l, i1, j1);
}

static int neighbor_at_face(int idx, int facing) {
    switch (facing) {
        case DOWN:  if ((idx >> 8 & 15) == 0)  return -1; return idx - DY;
        case UP:    if ((idx >> 8 & 15) == 15) return -1; return idx + DY;
        case NORTH: if ((idx >> 4 & 15) == 0)  return -1; return idx - DZ;
        case SOUTH: if ((idx >> 4 & 15) == 15) return -1; return idx + DZ;
        case WEST:  if ((idx >> 0 & 15) == 0)  return -1; return idx - DX;
        case EAST:  if ((idx >> 0 & 15) == 15) return -1; return idx + DX;
        default:    return -1;
    }
}

static void add_edges(int idx, int *faceset) {
    int i = idx >> 0 & 15;
    if (i == 0) *faceset |= 1 << WEST;
    else if (i == 15) *faceset |= 1 << EAST;

    int j = idx >> 8 & 15;
    if (j == 0) *faceset |= 1 << DOWN;
    else if (j == 15) *faceset |= 1 << UP;

    int k = idx >> 4 & 15;
    if (k == 0) *faceset |= 1 << NORTH;
    else if (k == 15) *faceset |= 1 << SOUTH;
}

static int queue[4096];

/* returns the 6-bit face set reached from this seed */
static int flood_fill(int seed) {
    int faceset = 0;
    int head = 0, tail = 0;
    queue[tail++] = seed;
    opaque[seed] = 1;
    while (head < tail) {
        int i = queue[head++];
        add_edges(i, &faceset);
        for (int f = 0; f < 6; ++f) {
            int j = neighbor_at_face(i, f);
            if (j >= 0 && !opaque[j]) {
                opaque[j] = 1;
                queue[tail++] = j;
            }
        }
    }
    return faceset;
}

int main(void) {
    init_edges();
    char *line = NULL;
    size_t cap = 0;
    /* large line buffer: 64 words * 17 chars + count ~ 1.1KB */
    static char buf[4096];
    (void)line; (void)cap;
    while (fgets(buf, sizeof buf, stdin)) {
        unsigned long long count;
        unsigned long long w[64];
        int n = sscanf(buf,
            "%llu "
            "%llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx "
            "%llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx "
            "%llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx "
            "%llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx",
            &count,
            &w[0],&w[1],&w[2],&w[3],&w[4],&w[5],&w[6],&w[7],&w[8],&w[9],&w[10],&w[11],&w[12],&w[13],&w[14],&w[15],
            &w[16],&w[17],&w[18],&w[19],&w[20],&w[21],&w[22],&w[23],&w[24],&w[25],&w[26],&w[27],&w[28],&w[29],&w[30],&w[31],
            &w[32],&w[33],&w[34],&w[35],&w[36],&w[37],&w[38],&w[39],&w[40],&w[41],&w[42],&w[43],&w[44],&w[45],&w[46],&w[47],
            &w[48],&w[49],&w[50],&w[51],&w[52],&w[53],&w[54],&w[55],&w[56],&w[57],&w[58],&w[59],&w[60],&w[61],&w[62],&w[63]);
        if (n != 65) continue;

        memset(opaque, 0, sizeof opaque);
        int set_count = 0;
        for (int idx = 0; idx < 4096; ++idx) {
            if ((w[idx >> 6] >> (idx & 63)) & 1ULL) {
                opaque[idx] = 1;
                ++set_count;
            }
        }
        int empty = 4096 - set_count;

        uint64_t packed = 0;
        if (4096 - empty < 256) {
            /* setAllVisible(true): all 36 read bits set */
            for (int a = 0; a < 6; ++a)
                for (int b = 0; b < 6; ++b)
                    packed |= (uint64_t)1 << (a + b * 6);
        } else if (empty == 0) {
            packed = 0;
        } else {
            for (int e = 0; e < 1352; ++e) {
                int i = INDEX_OF_EDGES[e];
                if (!opaque[i]) {
                    int fs = flood_fill(i);
                    /* setManyVisible: for every pair (a,b) of reached faces, set bit a+b*6 */
                    for (int a = 0; a < 6; ++a) {
                        if (!(fs & (1 << a))) continue;
                        for (int b = 0; b < 6; ++b) {
                            if (fs & (1 << b))
                                packed |= (uint64_t)1 << (a + b * 6);
                        }
                    }
                }
            }
        }
        printf("%llx\n", (unsigned long long)packed);
    }
    return 0;
}
