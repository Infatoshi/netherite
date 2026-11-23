/* lane/lava (books only, read only; chain search --lava-log): what env id's macros could know of lava at its last
 * step's end, the tests the lava options and the strip's lava checks make, called between steps by the trace and by
 * nothing a macro or the policy reads. Every field is the macros' own sense (the client world through obsview, what
 * the player has been sent, and the view's rule: a cell whose centre the eye sees past every block, within 75 degrees
 * of the look); none reads the server. */

#define LV_VIEW_R 8             /* lava in view (OV_HOT, source or flowing) this far from the eye */

int neth_vec_lava(const struct neth_vec *v, int id, struct neth_vec_lava *out)
{
    if (!out || id < 0 || id >= v->n || !v->p) return -1;
    memset(out, 0, sizeof *out);
    const struct vec_env *e = &v->e[id];
    const struct vec_last *r = &e->last;
    double ey = r->y + 1.62;
    struct obsview_hit h[OV_KINDS];
    out->hot_d = -1.0f, out->water_d = -1.0f;
    if (obsview_scan(v->p, id, r->x, ey, r->z, r->yaw, r->pitch, LV_VIEW_R, 1 << OV_HOT | 1 << OV_WATER, NULL, NULL, h) == 0)
    {
        out->water_d = h[OV_WATER].d;
        out->hot_n = h[OV_HOT].n, out->hot_d = h[OV_HOT].d;
        out->hot_x = h[OV_HOT].x, out->hot_y = h[OV_HOT].y, out->hot_z = h[OV_HOT].z;
    }
    out->edge = surface_lava_edge(v, e, r);
    out->st_hot = st_hot(v, e, r, ey);
    {
        struct obsview_hit hn[OV_KINDS];
        struct st_ctx c = {e};
        out->st_near = obsview_scan(v->p, id, r->x, ey, r->z, r->yaw, r->pitch, ST_NEAR_R + ST_NEAR_UP + 1, 1 << OV_HOT,
                                    st_near_skip, &c, hn) == 0 && hn[OV_HOT].d > 0.0f;
    }
    out->ob_hot = (v->macros & NETH_VEC_OBSIDIAN) ? ob_hot(v, e) : 0;
    out->esc = e->esc;
    out->lava_t = e->lava_t;
    out->steps = e->steps;
    out->sdir = e->sdir;
    out->lsrc = e->lsrc;
    out->on_ground = r->on_ground;
    return 0;
}
