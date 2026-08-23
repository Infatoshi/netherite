/* item_overflow.h - bounded FIFO hold when the 48-slot EntityItem table is full.
 *
 * Magma magma/game/live_sim.c and blaze-CPU/CUDA compile this one source.
 * Magma wrappers stay thin; blaze compiles the same header.
 *
 * Java 1.11.2 (java/oracle-src):
 *   World.spawnEntity                 World.java:1268-1301
 *     no numeric entity cap; chunk-loaded / player / UUID only
 *   WorldServer.spawnEntity           WorldServer.java:1124-1127
 *     canAddEntity UUID duplicate     WorldServer.java:1141-1175
 *   EntityItem ctor                   EntityItem.java:51-68
 *     health 5 :54; setSize 0.25 :56; setPosition :57
 *
 * Magma extras (M1 is magma==blaze; not a Java cap):
 *   live table 48 (GM_LIVE_MAX / CU_MAX_ITEMS)
 *   overflow FIFO 32 (IL_OVERFLOW_MAX)
 *   spawn_fail_count only when overflow is also full
 *   hashed into BP_ITEMS so a silent skip is impossible
 */
#ifndef MC_ITEM_OVERFLOW_H
#define MC_ITEM_OVERFLOW_H

#include "mc.h"
#include "items_core.h"

#ifndef IL_OVERFLOW_MAX
#define IL_OVERFLOW_MAX 32
#endif

typedef struct {
    ICStack stack;
    double x, y, z;
    int delay;
} IlOverflow;

MC_HD static inline void il_overflow_shift(IlOverflow *ov, int n, int i) {
    int j;
    if (!ov || i < 0 || i >= n) return;
    for (j = i + 1; j < n; ++j)
        ov[j - 1] = ov[j];
}

MC_HD static inline int il_overflow_push(IlOverflow *ov, int *n, int cap,
                                         ICStack stack, double x, double y,
                                         double z, int delay) {
    IlOverflow *slot;
    if (!ov || !n || cap <= 0 || *n < 0 || *n >= cap) return 0;
    slot = &ov[*n];
    slot->stack = stack;
    slot->x = x;
    slot->y = y;
    slot->z = z;
    slot->delay = delay < 0 ? 0 : delay;
    (*n)++;
    return 1;
}

/* Table full: hold in overflow, else increment fail. Java has no cap. */
MC_HD static inline int il_overflow_or_fail(IlOverflow *ov, int *n, int cap,
                                            int *fail, ICStack stack,
                                            double x, double y, double z,
                                            int delay) {
    if (il_overflow_push(ov, n, cap, stack, x, y, z, delay))
        return 1;
    if (fail) (*fail)++;
    return 0;
}

#endif /* MC_ITEM_OVERFLOW_H */

#ifdef IL_OV_STORE
#ifndef il_ov_fill_free
#error "item_overflow.h IL_OV_STORE requires il_ov_fill_free(s,x,y,z,stack,delay)"
#endif
#ifndef IL_OV_CAP
#define IL_OV_CAP IL_OVERFLOW_MAX
#endif

MC_HD static inline void il_overflow_drain(IL_OV_STORE *s) {
    int i = 0;
    if (!s || s->n_overflow <= 0) return;
    while (i < s->n_overflow) {
        if (!il_ov_fill_free(s, s->overflow[i].x, s->overflow[i].y,
                             s->overflow[i].z, s->overflow[i].stack,
                             s->overflow[i].delay))
            break;
        il_overflow_shift(s->overflow, s->n_overflow, i);
        s->n_overflow--;
    }
}

MC_HD static inline int il_overflow_spawn(IL_OV_STORE *s, double x, double y,
                                          double z, ICStack stack,
                                          int pickup_delay) {
    if (!s || stack.item <= 0 || stack.count <= 0) return 0;
    il_overflow_drain(s);
    if (il_ov_fill_free(s, x, y, z, stack, pickup_delay)) return 1;
    return il_overflow_or_fail(s->overflow, &s->n_overflow, IL_OV_CAP,
                               &s->spawn_fail_count, stack, x, y, z,
                               pickup_delay);
}
#endif /* IL_OV_STORE */
