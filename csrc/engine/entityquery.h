/* World.getEntitiesWithinAABBExcludingEntity over the native entity lists:
 * the chunk walk World does (cx outer, cz inner, only chunks that exist),
 * each chunk's y sections clamped the way Chunk.getEntitiesWithinAABBForEntity
 * clamps them, and within a section the insertion order the section list
 * carries. An entity's own box is the `this` of intersectsWith, the query box
 * the argument, exactly as Chunk passes them. */
#ifndef NETHERITE_ENTITYQUERY_H
#define NETHERITE_ENTITYQUERY_H

#include "aabb.h"
#include "item_entity.h"

/* Appends to out and returns the count; out must hold cap entries. exclude
 * (an ie_ent *) or NULL is left out, as p_72839_1_ is. */
int entityquery_in_box(struct ie_world *iew, struct aabb box, const void *exclude,
                       ie_ent **out, int cap);

/* One chunk section's part of the same walk: the entities of section y of
 * chunk (cx, cz) whose box meets box, in the section list's order. The
 * caller walks the chunks and sections (a query over two pools that share
 * the world merges them section by section, by chunk_stamp). */
int entityquery_section(struct ie_world *iew, int cx, int cz, int y, struct aabb box, const void *exclude,
                        ie_ent **out, int cap);

#endif
