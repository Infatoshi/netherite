/* rk_14_light_query.c - compute core of render-opt kernel 14_light_query.
 * light_query copied VERBATIM from candidate.c; only main()/stdio removed. See rk.h. */
#include "rk.h"

int rk_light_query(int nb, int up, int east, int west, int south, int north, int own) {
    if (!nb) return own;
    int m = up;
    if (east  > m) m = east;
    if (west  > m) m = west;
    if (south > m) m = south;
    if (north > m) m = north;
    return m;
}
