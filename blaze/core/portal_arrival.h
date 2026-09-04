/* Shared Magma portal lookup/placement order. This preserves the playable
 * runtime's ring-order lookup, not vanilla Teleporter's nearest-distance
 * selection (that Java/Magma difference remains a separate coverage item).
 * PORTAL_BLOCK returns -1 for unavailable world data; absence is never air.
 * The plan is read-only so an incomplete region cannot leave half a portal. */
#ifndef BLAZE_PORTAL_ARRIVAL_H
#define BLAZE_PORTAL_ARRIVAL_H

#ifndef PORTAL_HD
#define PORTAL_HD
#endif

typedef struct {
    double x, y, z;
    int create, bx, by, bz;
} PortalArrival;

PORTAL_HD static inline int portal_plan_arrival(
    const PORTAL_WORLD *w, int near_x, int near_z, PortalArrival *p) {
    int r, dx, dz, x, y, z, id;
    p->create = 0;
    /* magma/game/portal_live.c: full +/-128 square, ring order, y=4..123. */
    for (r = 0; r <= 128; ++r)
        for (dx = -r; dx <= r; ++dx)
            for (dz = -r; dz <= r; ++dz) {
                if (r && dx != -r && dx != r && dz != -r && dz != r) continue;
                x = near_x + dx; z = near_z + dz;
                for (y = 4; y < 124; ++y) {
                    id = PORTAL_BLOCK(w, x, y, z);
                    if (id < 0) return -1;
                    if (id == 90) {
                        p->x = x + 0.5; p->y = y; p->z = z + 0.5;
                        return 1;
                    }
                }
            }
    p->bx = near_x - 1; p->bz = near_z; p->by = -1;
    for (r = 0; r <= 16 && p->by < 0; ++r)
        for (dx = -r; dx <= r && p->by < 0; ++dx)
            for (dz = -r; dz <= r && p->by < 0; ++dz) {
                if (r && dx != -r && dx != r && dz != -r && dz != r) continue;
                x = near_x + dx; z = near_z + dz;
                for (y = 118; y >= 5; --y) {
                    int a = PORTAL_BLOCK(w, x, y, z);
                    int b = PORTAL_BLOCK(w, x, y + 1, z);
                    int c = PORTAL_BLOCK(w, x, y - 1, z);
                    if (a < 0 || b < 0 || c < 0) return -1;
                    if (a == 0 && b == 0 && c != 0 && c != 10 && c != 11) {
                        p->bx = x - 1; p->bz = z; p->by = y; break;
                    }
                }
            }
    if (p->by < 0) p->by = 70;
    for (x = p->bx; x < p->bx + 4; ++x)
        for (y = p->by - 1; y <= p->by + 3; ++y)
            if (PORTAL_BLOCK(w, x, y, p->bz) < 0) return -1;
    p->create = 1;
    p->x = p->bx + 1.5; p->y = p->by; p->z = p->bz + 0.5;
    return 1;
}

#endif
