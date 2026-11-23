/* walk_to (lane/pearlsee, 3rd pass): PRIVILEGED. The player walked along the engine's pathfinder's path (runtime
 * walkto.h: vanilla's PathFinder over the server world's blocks, for the player's size) to a cell, point by point with
 * the walk and jump keys: what a zombie's path to the player is. Only for options labelled privileged (pearl_oracle;
 * lane/rod's and lane/chain's walks), never a default.
 *
 *   int r = walk_to(v, e, r, s, a, &st, x, y, z);   1 there (the path walked), 0 walking, -1 no path within range
 *
 * The path is asked again when the cell moves more than a block, every WT_REASK steps, and after WT_STUCK steps with
 * no progress; until the first answer the player stands. The look's yaw is the walk's; its pitch is left to the caller
 * (a->look[1] is set to WT_PITCH, the caller may change it after). */
#define WT_RANGE 48.0f          /* the search's range (PathFinder's maxDistance: the cell must be within it) */
#define WT_REASK 60             /* steps between asks while walking */
#define WT_STUCK 25             /* steps with no progress: asked again (the jump held meanwhile) */
#define WT_NEAR 0.35            /* a point's middle this near (horizontally) with the feet at its height: the next */
#define WT_PITCH 10.0f
#define WT_MOVED 2              /* the cell moved more than this (blocks, across or up): asked again */

static int walk_to(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                   struct neth_rl_act *a, struct walkto_st *w, int x, int y, int z)
{
    int id = (int)(e - v->e);
    int64_t t = e->steps;
    const struct neth_rl_far *far = neth_rl_far(v->h, id);
    double wkd = hypot(x + 0.5 - r->x, z + 0.5 - r->z);
    int wky = y - (int)floor(r->y + 0.01);
    if (far == NULL) return WK_STAMP(e, WK_WT + 1, wkd, wky, 0), -1;
    if (hypot(r->x - w->px, r->z - w->pz) > 0.5) w->px = r->x, w->pz = r->z, w->prog_t = t;
    /* the newest answer first (an answer comes a step after its ask), then an ask only when due */
    if (far->wpath.seq > w->have && far->wpath.seq <= w->seq)
    {
        w->path = far->wpath;
        w->have = far->wpath.seq;
        w->k = 0;
    }
    int moved = abs(x - w->x) + abs(z - w->z) > WT_MOVED || abs(y - w->y) > WT_MOVED;
    if (w->seq == 0 || (w->have == w->seq && (moved || t - w->asked >= WT_REASK || t - w->prog_t > WT_STUCK)) ||
        t - w->asked >= 4 * WT_REASK)
    {
        neth_rl_walkto_ask(v->h, id, x, y, z, WT_RANGE);
        w->seq = far->wask.seq;
        w->x = x, w->y = y, w->z = z;
        w->asked = t;
        if (t - w->prog_t > WT_STUCK) w->prog_t = t;
    }
    still(a);
    a->look_mode = 1;
    a->look[0] = r->yaw;
    a->look[1] = WT_PITCH;
    if (w->have == 0) return WK_STAMP(e, WK_WT + 2, wkd, wky, 0), 0;   /* no answer yet */
    if (w->path.n < 0)                          /* an older cell's "no path": the new answer awaited */
        return w->have == w->seq ? (WK_STAMP(e, WK_WT + 3, wkd, wky, 0), -1) : (WK_STAMP(e, WK_WT + 4, wkd, wky, 0), 0);
    int fy = (int)floor(r->y + 0.01);
    while (w->k < w->path.n && hypot(w->path.pts[w->k][0] + 0.5 - r->x, w->path.pts[w->k][2] + 0.5 - r->z) < WT_NEAR &&
           abs(w->path.pts[w->k][1] - fy) <= 1)
        w->k += 1;
    if (w->k >= w->path.n)
    {
        if (w->path.len > w->path.n)            /* the kept part walked: the next ask brings the rest */
            return WK_STAMP(e, WK_WT + 5, wkd, wky, w->path.len), 0;
        return WK_STAMP(e, WK_WT + 6, wkd, wky, w->path.len), 1;
    }
    WK_STAMP(e, WK_WT + 7, wkd, wky, w->path.len);
    const int16_t *c = w->path.pts[w->k];
    double yaw, pitch;
    look_at(r->x, r->y + 1.62, r->z, c[0] + 0.5, r->y + 1.62, c[2] + 0.5, &yaw, &pitch);
    a->look[0] = (float)yaw;
    a->hold = 1u << PK_FORWARD;
    WK_FRONT(e, yaw);
    if (c[1] > fy || s->eye_water || t - w->prog_t > WT_STUCK / 2) a->hold |= 1u << PK_JUMP;
    return 0;
}
