#include "entityquery.h"
#include "jmath.h"

#include <stddef.h>

static int clamp_int(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static ie_chunk *chunk_at(struct ie_world *iew, int cx, int cz)
{
    for (int i = 0; i < iew->nchunks; ++i)
    {
        if (iew->chunks[i].used && iew->chunks[i].cx == cx && iew->chunks[i].cz == cz) return &iew->chunks[i];
    }

    return NULL;
}

int entityquery_in_box(struct ie_world *iew, struct aabb box, const void *exclude,
                       ie_ent **out, int cap)
{
    int cx0 = mh_floor((box.min_x - 2.0) / 16.0);
    int cx1 = mh_floor((box.max_x + 2.0) / 16.0);
    int cz0 = mh_floor((box.min_z - 2.0) / 16.0);
    int cz1 = mh_floor((box.max_z + 2.0) / 16.0);
    int n = 0;

    for (int cx = cx0; cx <= cx1; ++cx)
    {
        for (int cz = cz0; cz <= cz1; ++cz)
        {
            ie_chunk *c = chunk_at(iew, cx, cz);

            if (!c) continue; /* chunkExists */

            int y0 = clamp_int(mh_floor((box.min_y - 2.0) / 16.0), 0, IE_SECTIONS - 1);
            int y1 = clamp_int(mh_floor((box.max_y + 2.0) / 16.0), 0, IE_SECTIONS - 1);

            for (int s = y0; s <= y1; ++s)
            {
                for (int i = 0; i < c->sec[s].n; ++i)
                {
                    ie_ent *en = ie_ent_at(sec_items(&c->sec[s])[i]);

                    if ((const void *)en != exclude && aabb_intersects(&en->e.bounding_box, &box))
                    {
                        if (n < cap) out[n] = en;
                        ++n;
                    }
                }
            }
        }
    }

    return n;
}

int entityquery_section(struct ie_world *iew, int cx, int cz, int y, struct aabb box, const void *exclude,
                        ie_ent **out, int cap)
{
    ie_chunk *c = chunk_at(iew, cx, cz);
    int n = 0;

    if (!c || y < 0 || y >= IE_SECTIONS) return 0;

    for (int i = 0; i < c->sec[y].n; ++i)
    {
        ie_ent *en = ie_ent_at(sec_items(&c->sec[y])[i]);

        if ((const void *)en != exclude && aabb_intersects(&en->e.bounding_box, &box))
        {
            if (n < cap) out[n] = en;
            ++n;
        }
    }

    return n;
}
