/* The block renderer's tables, one copy a process and asset directory
 * (lane/heapprof): render_blocks.h's rb_table (table.bin: the icon index,
 * the block bounds, the colour maps, about 4.6 MB) and rb_atlas
 * (atlas.json's sprites), read once and never changed or freed after. Each
 * pipeline env's view loaded both twice into its own heap (raster_live_new
 * and block_item_model.c), 9.3 MB an env. This file is the engine's, not
 * the views' (pipeline.mk's PL_RENDER), so every env's view copy shares one
 * table, as itemtab.h's. */
#ifndef NETHERITE_RBSHARE_H
#define NETHERITE_RBSHARE_H

struct rb_table;
struct rb_atlas;

/* DIR's table and atlas, loaded on the first call for that directory (the
 * env heap is off meanwhile: they are the process's); NULL when the load
 * fails. Thread-safe: a miss takes a lock, a hit reads a table that is
 * never changed or freed. */
const struct rb_table *rb_table_shared(const char *dir);
const struct rb_atlas *rb_atlas_shared(const char *dir);

#endif
