/* The HUD's item records: a scene's state/item_table.json (RenderStateProbe's
 * records by item id, about 38,000 of them in 11 MB), read once a process and
 * scene and kept converted (lane/rlasync). raster_hand.c's lookup parsed the
 * file into each env's own heap the first time its player held an item, and
 * again after every reset (the view is loaded again): 2.5 million blocks,
 * about 120 MB an env, and a parse of 11 MB on that env's step. This file is
 * the engine's, not the views' (pipeline.mk's PL_RENDER), so every env's
 * view copy shares one table. */
#ifndef NETHERITE_ITEMTAB_H
#define NETHERITE_ITEMTAB_H

#include <stddef.h>

struct jval;
struct itemtab;

/* conv converts one record into out (size bytes) and sets *dmg to the key a
 * lookup matches against meta; its result is the lookup's */
typedef int (*itemtab_conv)(const struct jval *rec, void *out, int *dmg);

/* SCENE's table, read and converted on the first call for that scene (the
 * env heap is off meanwhile: the table is the process's); NULL when the file
 * is missing or not JSON. Thread-safe: a miss takes a lock, a hit reads a
 * table that is never changed or freed. */
const struct itemtab *itemtab_get(const char *scene, size_t size, itemtab_conv conv);

/* item ID's record whose dmg key is META, else its first one: conv's result
 * for it, and its converted bytes in out (left untouched when conv returned
 * 0); 0 for an id without records or a NULL table */
int itemtab_lookup(const struct itemtab *t, int id, int meta, void *out);

#endif
