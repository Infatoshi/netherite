/* walkto (lane/pearlsee, 3rd pass): the player's path search, PRIVILEGED (walkto.h). Runs in the env's own context
 * (rlbind.c's step end: neth_env set), with the env's pathfinder arrays (arena.h pf_store); never inside a tick. */
#include "walkto.h"

#include <math.h>
#include <string.h>

#include "../engine/arena.h"
#include "../engine/env.h"
#include "../engine/pathfind.h"

#define WALKTO_SAFE_TRIES 3     /* Entity.getMaxSafePointTries' default */

void walkto_search(struct world *w, double px, double py, double pz, const struct walkto_ask *ask,
                   struct walkto_path *out)
{
    out->seq = ask->seq;
    out->n = out->len = -1;
    if (w == NULL) return;
    struct pf f;
    pf_init(&f, w);
    f.entity_w = w;
    struct pf_entity e;
    memset(&e, 0, sizeof e);
    e.width = 0.6f, e.height = 1.8f;
    e.max_safe_point_tries = WALKTO_SAFE_TRIES;
    pf_entity_set_position(&e, px, py, pz);
    int *pts = neth_arena()->pf.out;
    /* doors: open ones passed, closed ones not; water avoided where a dry way exists, swum where not */
    int x = ask->x, y = ask->y, z = ask->z;
    if (ask->rel) x += (int)floor(px), y += (int)floor(py), z += (int)floor(pz);
    int n = pf_get_entity_path_to_xyz(&f, &e, x, y, z, ask->range, 1, 0, 1, 1, pts, PF_MAX_POINTS);
    pf_free(&f);
    out->len = n;
    if (n < 0) return;
    int k = n < WALKTO_PTS ? n : WALKTO_PTS;
    for (int i = 0; i < k; ++i)
        for (int j = 0; j < 3; ++j) out->pts[i][j] = (int16_t)pts[i * 3 + j];
    out->n = k;
}
