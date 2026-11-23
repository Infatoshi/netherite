/* The privileged world scan (rlscan.h). */
#include "rlscan.h"

#include <math.h>
#include <string.h>

#include "../engine/player.h"
#include "../engine/session.h"
#include "../engine/world.h"

/* a column's chunk, kept while x and z stay in it */
struct col {
    struct world *w;
    int cx, cz;
    struct chunk *c;
};

static inline int cell(struct col *k, int x, int y, int z)
{
    int cx = x >> 4, cz = z >> 4;
    if (cx != k->cx || cz != k->cz || k->c == NULL)
    {
        k->cx = cx;
        k->cz = cz;
        k->c = world_chunk(k->w, cx, cz);
    }
    if (k->c == NULL) return -1;
    if (y < 0 || y > 255) return 0;
    int sy = y >> 4;
    if (!(k->c->mask >> sy & 1)) return 0;
    return chunk_sec_id(chunk_sec_at(k->c, sy), SEC_XYZ(x & 15, y, z & 15));
}

int rlscan_run(const struct rlscan_spec *sp, struct session *ss, struct rlscan_out *out)
{
    memset(out, 0, sizeof *out);
    struct world *w = ss->client_world;
    if (w == NULL) return -1;
    const struct client_player *cp = &ss->cp;
    /* the client player's posY is its eye (yOffset 1.62 standing: the camera's
     * yOffset - 1.62, semcam.c); its feet 1.62 under it */
    double eye = cp->e.pos_y + (double)(cp->e.y_offset - 1.62F), feet = eye - 1.62;
    int fx = (int)floor(cp->e.pos_x), fy = (int)floor(feet), fz = (int)floor(cp->e.pos_z);
    int ex = fx, ey = (int)floor(eye), ez = fz;
    out->fx = fx;
    out->fy = fy;
    out->fz = fz;
    out->x = (float)cp->e.pos_x;
    out->y = (float)feet;
    out->z = (float)cp->e.pos_z;
    out->yaw = cp->rotation_yaw;
    out->pitch = cp->rotation_pitch;
    struct col k = {w, 1 << 30, 1 << 30, NULL};
    for (int y = 0; y < RLSCAN_PY; ++y)
        for (int z = 0; z < RLSCAN_PX; ++z)
            for (int x = 0; x < RLSCAN_PX; ++x)
                out->patch[y][z][x] = (int16_t)cell(&k, fx - 2 + x, fy - 2 + y, fz - 2 + z);
    int best[RLSCAN_CLASSES];
    for (int c = 0; c < RLSCAN_CLASSES; ++c) best[c] = 1 << 30;
    int R = sp->radius, y0 = ey - sp->below, y1 = ey + sp->above;
    if (y0 < 0) y0 = 0;
    if (y1 > 255) y1 = 255;
    /* column by column: a column's chunk once, then its cells */
    for (int x = ex - R; x <= ex + R; ++x)
        for (int z = ez - R; z <= ez + R; ++z)
        {
            int cx = x >> 4, cz = z >> 4;
            if (cx != k.cx || cz != k.cz || k.c == NULL)
            {
                k.cx = cx;
                k.cz = cz;
                k.c = world_chunk(w, cx, cz);
            }
            if (k.c == NULL) continue;
            int dxz = (x - ex) * (x - ex) + (z - ez) * (z - ez);
            for (int y = y0; y <= y1; ++y)
            {
                int sy = y >> 4;
                if (!(k.c->mask >> sy & 1))
                {
                    y = (sy << 4) + 15;
                    continue;
                }
                int id = chunk_sec_id(chunk_sec_at(k.c, sy), SEC_XYZ(x & 15, y, z & 15));
                uint8_t m = sp->cls[id & 4095];
                if (!m) continue;
                int dy = y - ey, d = dxz + dy * dy * (dy > 1 ? sp->up_weight : 1);
                for (int c = 0; c < RLSCAN_CLASSES; ++c)
                    if ((m >> c & 1) && d < best[c])
                    {
                        best[c] = d;
                        out->near[c] = (struct rlscan_near){(int8_t)(x - ex), (int8_t)(y - ey), (int8_t)(z - ez), 1,
                                                            (int16_t)id, 0};
                    }
            }
        }
    return 0;
}
