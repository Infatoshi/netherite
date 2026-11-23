/* lane/proom: the stronghold latch's trace (vecenv.h neth_vec_sh_trace), included last by macros.c (it reads the snow
 * macro's constants). Read only: each hold is recomputed from the state the last step left (the chain peek's inventory,
 * the env's last result) without the clocks sn_holds and sh_food_holds advance as they test, so a trace on moves no
 * row. What it answers: on a step another macro or the policy drives with 12 eyes held, which test kept the stronghold
 * from latching (snow_first, stronghold_food, stronghold_armor, sword_stock), or, latched, what it wants and where. */

int neth_vec_prov_mode(const struct neth_vec *v, int id)
{
    if (v == NULL || id < 0 || id >= v->n || !v->chain) return -1;
    const struct vec_env *e = &v->e[id];
    return provision_mode(v, e, &e->last, &e->cpk.st);
}

int neth_vec_sh_trace(const struct neth_vec *v, int id, struct neth_vec_sh_trace *out)
{
    if (v == NULL || id < 0 || id >= v->n || out == NULL || !v->chain) return -1;
    const struct vec_env *e = &v->e[id];
    const struct sh_env *h = &e->sh;
    const struct arm_env *m = &e->arm;
    const struct vec_last *r = &e->last;
    const struct neth_rl_state *s = &e->cpk.st;
    memset(out, 0, sizeof *out);
    out->go = h->go, out->phase = h->phase, out->osub = h->osub, out->on = h->on, out->fwait = h->fwait;
    out->screen = r->screen;
    out->fw1 = h->fw1, out->sn_w0 = m->sn_w0, out->sn_quit = m->sn_quit, out->wtime = r->wtime;
    out->eyes = (int32_t)inv_count(s, EYE), out->snowballs = (int32_t)inv_count(s, SNOWBALL);
    int alive_ow = r->dim == 0 && !(r->flags & PF_DEAD);
    out->sn_hold = MOPT(MO_SNOW_FIRST) && MOPT(MO_SNOW_KIT) != SNK_OFF && MOPT(MO_SNOW_KIT) && alive_ow && !m->sn_quit &&
                   out->snowballs < SN_WANT;
    out->food_pts = (int32_t)food_points(s);
    int food_floor = MOPT(MO_SH_FOOD) == SHW_WAIT80 ? SH_FOOD_FLOOR_80 : SH_FOOD_FLOOR;
    out->food_wait = MOPT(MO_SH_FOOD) != SHW_OFF && out->food_pts < food_floor;
    const struct neth_rl_far *f = neth_rl_far(v->h, id);
    out->armor = f ? f->armor_worn : -1;
    out->armor_wait = MOPT(MO_SH_ARMOR) && (!f || f->armor_worn < SH_ARMOR_FLOOR);
    out->swords = sword_stock_fresh(s), out->sword_need = sword_stock_need(v);
    out->sword_wait = out->sword_need && out->swords < out->sword_need;
    out->want = (v->macros & NETH_VEC_STRONGHOLD) && alive_ow && h->go && h->phase != SH_DONE &&
                (out->eyes > 0 || h->phase == SH_ENTER || h->phase == SH_ROOM || h->phase == SH_LEAVE ||
                 (h->phase == SH_ORACLE && h->osub >= SO_FILL));
    out->room_ok = h->oroom.ok, out->room_d = -1;
    if (h->oroom.ok)
    {
        double mx = (h->oroom.box[0] + h->oroom.box[3]) / 2.0 + 0.5, mz = (h->oroom.box[2] + h->oroom.box[5]) / 2.0 + 0.5;
        out->room_d = hypot(mx - r->x, mz - r->z);
    }
    out->room = h->book.room, out->lit = h->book.lit, out->filled = h->book.filled, out->gave_up = h->book.gave_up;
    out->book_phase = h->book.phase;
    out->stand[0] = h->ostand_ok;
    for (int k = 0; k < 3; ++k) out->stand[k + 1] = h->ostand[k];
    out->nop = h->nop, out->opk = h->opk, out->ofail = h->ofail, out->ooff = h->ooff;
    out->dig[0] = h->odx, out->dig[1] = h->ody, out->dig[2] = h->odz;
    out->sp[0] = h->oroom.has_sp;
    for (int k = 0; k < 3; ++k) out->sp[k + 1] = h->oroom.sp[k];
    for (int k = 0; k < 6; ++k) out->box[k] = h->oroom.box[k];
    out->prov = provision_mode(v, e, r, s), out->fs_home = e->fs_home;
    out->wood[0] = (int32_t)e->fs_hx, out->wood[1] = (int32_t)e->fs_far, out->wood[2] = (int32_t)e->fs_hz;
    out->wood_stand[0] = (int32_t)e->fs_sx, out->wood_stand[1] = (int32_t)e->fs_det, out->wood_stand[2] = (int32_t)e->fs_sz;
    out->wood_blk = shoracle_block(v->p, id, out->wood[0], out->wood[1], out->wood[2], NULL);
    out->wood_logs = (int32_t)(inv_count(s, LOG) + inv_count(s, LOG2));
    out->wood_age = e->steps - e->fs_legt;
    out->sky = shoracle_sky_here(v->p, id, (int)floor(r->x), (int)floor(r->y + 1.62), (int)floor(r->z));
    out->pearl_on = e->pearl.on, out->pearl_phase = e->pearl.phase, out->pearls = (int32_t)pearl_held(s);
    out->hostiles = s->nvis;
    out->mo_entity = s->mo_entity, out->mo_ekind = s->mo_ekind, out->lq_hit = s->lq_hit, out->lq_block = s->lq_block;
    out->ptid = e->pearl.tid, out->pangry = e->pearl.tangry, out->phits = e->pearl.book.hits;
    out->ptdist = (float)e->pearl.tdist;
    out->rstart = (int32_t)h->rstart, out->frame_d = -1;
    struct shview o;
    if (r->dim == 0 && shview_scan(v->p, id, r->x, r->y + 1.62, r->z, r->yaw, r->pitch, SH_SCAN_R, SHV_BLOCKS, &o) == 0)
    {
        out->nframe = o.nframe;
        for (int i = 0; i < o.nframe; ++i)
        {
            double d = py_dist3(r->x, r->y + 1.62, r->z, o.frame[i].x + 0.5, o.frame[i].y + 0.5, o.frame[i].z + 0.5);
            if (out->frame_d < 0 || d < out->frame_d) out->frame_d = (float)d;
        }
    }
    return 0;
}
