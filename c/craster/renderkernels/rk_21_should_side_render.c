/* rk_21_should_side_render.c - compute core of render-opt kernel 21_should_side_render.
 * shouldSideBeRendered logic copied VERBATIM from candidate.c; only main()/stdio removed.
 * The caller supplies the exact doubles the method reads (reinterpret doubleToRawLongBits). */
#include "rk.h"

int rk_should_side_render(int side,
                          double minX, double minY, double minZ,
                          double maxX, double maxY, double maxZ, int nbr) {
    int early = 0;
    switch (side) {
        case 0: if (minY > 0.0) early = 1; break; /* DOWN  */
        case 1: if (maxY < 1.0) early = 1; break; /* UP    */
        case 2: if (minZ > 0.0) early = 1; break; /* NORTH */
        case 3: if (maxZ < 1.0) early = 1; break; /* SOUTH */
        case 4: if (minX > 0.0) early = 1; break; /* WEST  */
        case 5: if (maxX < 1.0) early = 1; break; /* EAST  */
    }
    return early ? 1 : (nbr ? 0 : 1);
}
