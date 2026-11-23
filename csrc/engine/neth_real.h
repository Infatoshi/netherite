/* Opt-in storage-shrinking FP32 simulation real type (lane/neon-prec-sim-buf).
 * Default NETH_SIM_F32=0: neth_real is double and every struct layout, digest,
 * and Java replay is bit-for-bit identical to vanilla 1.7.10.
 * Opt-in -DNETH_SIM_F32=1: neth_real is float across hot simulation structs
 * (entity, aabb, cell_memo, living, living_ai/ai_task, attr_map, sp_rec,
 * sp_box, tracker_entry, client_entity, ie_ent, fh_ent, water_memo, pf_entity),
 * shrinking the L3 working set per env and per mob. Snapshot boundaries convert
 * between double and neth_real so starts made by either build load in either. */
#ifndef NETHERITE_NETH_REAL_H
#define NETHERITE_NETH_REAL_H

#ifndef NETH_SIM_F32
#define NETH_SIM_F32 0
#endif

#if NETH_SIM_F32
typedef float neth_real;
#else
typedef double neth_real;
#endif

#endif
