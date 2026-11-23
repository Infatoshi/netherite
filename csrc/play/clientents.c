/* The live client's own copies of the entities it draws, as Java's
 * WorldClient has them (included by play.c; nothing here runs in the tick).
 *
 * The replay keeps the client copies the pick needs: the livings and the
 * fireballs in the combat client world (clientworld.c, fed by combat.c's
 * tracker entries), the dragon, the crystals, the falling blocks, the TNT
 * and the hanging entities in pickobj.c. Those give the positions the
 * tracker's cadence and quantization and the client's three-step
 * interpolation leave. What only the renderer reads is kept here, beside
 * them, one client tick per row the client ticked:
 *
 *  - a living's EntityLivingBase.onEntityUpdate and onUpdate on the client
 *    (hurtTime and deathTime, the prev angles, ticksExisted), the limb swing
 *    of moveEntityWithHeading over the copy's own movement, and the body yaw
 *    (func_110146_f: EntityBodyHelper for an AI mob, else the 0.3 ease),
 *    with the head yaw the S19 packets set;
 *  - the dragon's lastTickPos and animTime (EntityDragon.onLivingUpdate over
 *    the copy's packet motion) and its hurt clock;
 *  - the entity statuses the server sends as it happens (S19PacketEntity-
 *    Status 2, performHurtAnimation; 3, the death), read off the server's
 *    hurt and health after each row and handled at the next client tick's
 *    packets, as NetHandlerPlayClient does;
 *  - the dropped items and XP orbs: EntityTracker's entries for them
 *    (range 64 and 160, every 20 ticks, with velocity), the S0E and S11
 *    spawn at the tracker's 1/32 block, the S15-S18 moves and S12 velocities
 *    of EntityTrackerEntry.sendLocationToAllClients, the S13 when the entry
 *    drops the player or the entity leaves, and each copy's own
 *    EntityItem/EntityXPOrb.onUpdate on the client world's blocks.
 */

/* ------------------------------------------------------------ the livings */

/* one per living copy of the client world (clientworld.h CW_MAX_ENTITIES) */
enum { CENT_MAX = CW_MAX_ENTITIES };

struct cent_living {
    int id, used, seen;
    float prev_yaw, prev_pitch;       /* prevRotationYaw/Pitch: before this tick's interpolation */
    float head, prev_head, seen_head; /* rotationYawHead: the last S19's, then the body helper's */
    float body, prev_body;            /* renderYawOffset */
    int body_counter;                 /* EntityBodyHelper.field_75667_c */
    float body_yaw;                   /* EntityBodyHelper.field_75666_b */
    float limb, limb_amount, prev_limb_amount;
    int hurt, dying, death, dead, ticks;
    int status_hurt, status_death;    /* S19 statuses in flight to the next client tick */
    int srv_hurt;                     /* the server's hurtTime at the last row */
    float srv_health;
    float cli_health;                 /* the copy's own health (data watcher 6): the last row's, which its S1C brought */
    /* the copy's equipment: the tracker's S04s at the spawn, then each
     * changed slot EntityLivingBase.onUpdate's head sends (the server's
     * equip_sent), handled at the next client tick */
    struct equip_slot equip[5], pend_equip[5];
    int pend_equip_set;
    /* the copy's lead holder: the S1B type 1 the tracker sends at the spawn
     * and setLeashedToEntity or clearLeashed sends as it happens, handled at
     * the next client tick (the server's leash_holder, leash_knot) */
    int leash_holder, pend_leash_holder;
    uint64_t leash_knot, pend_leash_knot;
    uint32_t leash_sends;             /* the server's count this copy has taken */
    int creeper_since, creeper_last;  /* EntityCreeper.timeSinceIgnited, lastActiveTime */
    /* the render input of the last frame the copy's server entity drew:
     * the S13 that removes the copy lands a client tick after the server
     * removed the entity, and until then the copy draws from this, moved
     * and clocked as the copy is */
    struct mob_render_input last_m;
    int has_last_m, last_fuse;
};
static struct cent_living cents[CENT_MAX];
static int ncents;
/* the row's client tick ran (a paused row runs none) */
static int cents_client_ticked;

/* the dragon: lastTickPos, animTime, and its own hurt clock */
static struct cent_dragon {
    int id, used;
    double prev_x, prev_y, prev_z, x, y, z;
    float anim, prev_anim;
    int hurt, status_hurt, srv_hurt, ticks;
} cdragon;

/* MathHelper.wrapAngleTo180_float */
static float cent_wrap(float a)
{
    a = fmodf(a, 360.0F);
    if (a >= 180.0F) a -= 360.0F;
    if (a < -180.0F) a += 360.0F;
    return a;
}

/* Entity.moveEntity's fire test on the copy, what its isBurning reads
 * besides the server's flag: fire or lava in the box (contracted by 0.001)
 * where the copy's own move left it, unless it is wet. */
static int cent_touches_fire(const struct client_entity *ce, const struct entity *e)
{
    (void)e;
    struct world *w = view_world();
    double hw = (double)(ce->width / 2.0F);
    struct aabb b = {ce->x - hw + 0.001, ce->y + 0.001, ce->z - hw + 0.001,
                     ce->x + hw - 0.001, ce->y + (double)ce->height - 0.001, ce->z + hw - 0.001};
    return world_is_in_fire(w, b) && !entity_copy_is_wet(w, ce->in_water, ce->x, ce->y, ce->z, ce->height);
}

/* cents by id: open addressing over a power of two at least twice ncents,
 * each id's index (no two cents share one: a copy is added only when
 * cent_find has none), built on the first find after cents moved
 * (cfind_ok 0: the compaction, a new world) and kept by cent_add */
static struct { int32_t id, at; } cfind[2 * CENT_MAX];
static int cfind_mask, cfind_ok;

static unsigned cfind_hash(int id) { return (unsigned)id * 0x9E3779B1u; }

static void cfind_put(int id, int at)
{
    unsigned h = cfind_hash(id) & (unsigned)cfind_mask;
    while (cfind[h].at >= 0) h = (h + 1) & (unsigned)cfind_mask;
    cfind[h].id = id;
    cfind[h].at = at;
}

static void cfind_build(void)
{
    int size = 64;
    while (size < 2 * CENT_MAX && size < 4 * ncents) size *= 2;
    cfind_mask = size - 1;
    for (int i = 0; i < size; ++i) cfind[i].at = -1;
    for (int i = 0; i < ncents; ++i) cfind_put(cents[i].id, i);
    cfind_ok = 1;
}

static struct cent_living *cent_find(int id)
{
    if (!cfind_ok) cfind_build();
    for (unsigned h = cfind_hash(id) & (unsigned)cfind_mask; cfind[h].at >= 0; h = (h + 1) & (unsigned)cfind_mask)
        if (cfind[h].id == id) return &cents[cfind[h].at];
    return NULL;
}

/* a new copy at the list's end, found from now on */
static struct cent_living *cent_add(void)
{
    if (ncents == CENT_MAX) list_full("living copies in the play client", CENT_MAX);
    if (cfind_ok && 2 * (ncents + 1) > cfind_mask + 1) cfind_ok = 0;
    return &cents[ncents++];
}

/* The server's livings by entity id, for a stretch of the client's work in
 * which the server world does not change (cents_after_row, a frame's pass):
 * each id's first living in the list, as the scan below finds it. Open
 * addressing over a power of two at least twice the list; built by
 * cent_index_build, dropped by cent_index_drop. */
enum { CIDX_MAX = 2 * AN_MAX_ENTITIES };
static struct { int32_t id, at; } cidx[CIDX_MAX];
static int cidx_mask = -1;   /* the table's size less one, -1 none built */

static unsigned cidx_hash(int id) { return (unsigned)id * 0x9E3779B1u; }

static void cent_index_build(void)
{
    cidx_mask = -1;
    if (!SS.server_rows) return;
    int n = SR.d->anw.n, size = 64;
    while (size < 2 * n) size *= 2;
    for (int i = 0; i < size; ++i) cidx[i].at = -1;
    cidx_mask = size - 1;
    for (int i = 0; i < n; ++i)
    {
        const struct an_ent *a = an_ent_at(SR.d->anw.slot[i]);
        if (!(a && a->used && a->is_living && a->livh)) continue;
        int id = lv_get(a->livh)->entity_id;
        unsigned h = cidx_hash(id) & (unsigned)cidx_mask;
        while (cidx[h].at >= 0 && cidx[h].id != id) h = (h + 1) & (unsigned)cidx_mask;
        if (cidx[h].at < 0) cidx[h].id = id, cidx[h].at = i;
    }
}

static void cent_index_drop(void) { cidx_mask = -1; }

static const struct living *cent_server_living(int id)
{
    if (cidx_mask >= 0)
    {
        unsigned h = cidx_hash(id) & (unsigned)cidx_mask;
        while (cidx[h].at >= 0 && cidx[h].id != id) h = (h + 1) & (unsigned)cidx_mask;
        return cidx[h].at >= 0 ? lv_get(an_ent_at(SR.d->anw.slot[cidx[h].at])->livh) : NULL;
    }
    for (int i = 0; SS.server_rows && i < SR.d->anw.n; ++i)
    {
        const struct an_ent *a = an_ent_at(SR.d->anw.slot[i]);
        if (a && a->used && a->is_living && a->livh && lv_get(a->livh)->entity_id == id) return lv_get(a->livh);
    }
    return NULL;
}

static const struct pickobj *cent_dragon_copy(void)
{
    int n = 0;
    const struct pickobj *o = CP.pickobj ? pickobj_client_objs(&CP, &n) : NULL;
    for (int i = 0; i < n; ++i)
        if (o[i].kind == PK_DRAGON && !o[i].dead) return &o[i];
    return NULL;
}

/* Minecraft.runTick before the client's packets: the angles the copies had
 * after the previous tick are this tick's prev (the packets only aim a
 * living's interpolation, they do not turn it). */
static void cents_before_client_tick(int paused)
{
    cents_client_ticked = !paused;
    if (paused || !SS.server_rows || !SS.sr || !SR.combat) return;
    const struct clientworld *cw = &SR.combat->client;
    for (int i = 0; i < ncents; ++i)
    {
        cents[i].seen = 0;
        cents[i].cli_health = cents[i].srv_health;
    }
    for (int i = 0; i < cw->nents; ++i)
    {
        const struct client_entity *ce = &cw->ents[i];
        struct cent_living *c = ce->is_living ? cent_find(ce->id) : NULL;
        if (!c) continue;
        c->prev_yaw = ce->yaw;
        c->prev_pitch = ce->pitch;
        c->seen = 1;
    }
}

/* EntityLivingBase.onUpdate's tail on the client copy: func_110146_f. */
static void cent_body(struct cent_living *c, const struct client_entity *ce, const struct living *l)
{
    double dx = ce->x - ce->prev_x, dz = ce->z - ce->prev_z;
    float f = (float)(dx * dx + dz * dz);
    float target = c->body;
    if (f > 0.0025000002F) target = (float)fd_atan2(dz, dx) * 180.0F / 3.1415927F - 90.0F;
    if (l && l->swing_progress > 0.0F) target = ce->yaw;
    if (l && living_is_ai_enabled((struct living *)l))
    {
        /* EntityBodyHelper.func_75664_a */
        if (dx * dx + dz * dz > 2.500000277905201E-7)
        {
            c->body = ce->yaw;
            c->head = body_helper_a(c->body, c->head, 75.0F);
            c->body_yaw = c->head;
            c->body_counter = 0;
        }
        else
        {
            float lim = 75.0F;
            if (fabsf(c->head - c->body_yaw) > 15.0F)
            {
                c->body_counter = 0;
                c->body_yaw = c->head;
            }
            else if (++c->body_counter > 10)
            {
                float v = 1.0F - (float)(c->body_counter - 10) / 10.0F;
                lim = (v > 0.0F ? v : 0.0F) * 75.0F;
            }
            c->body = body_helper_a(c->head, c->body, lim);
        }
        return;
    }
    float d = cent_wrap(target - c->body);
    c->body += d * 0.3F;
    float w = cent_wrap(ce->yaw - c->body);
    if (w < -75.0F) w = -75.0F;
    if (w >= 75.0F) w = 75.0F;
    c->body = ce->yaw - w;
    if (w * w > 2500.0F) c->body += w * 0.2F;
}

static void cents_livings_tick(void)
{
    const struct clientworld *cw = &SR.combat->client;
    for (int i = 0; i < ncents; ++i) cents[i].used = 0;
    for (int i = 0; i < cw->nents; ++i)
    {
        const struct client_entity *ce = &cw->ents[i];
        if (!ce->is_living) continue;
        const struct living *l = cent_server_living(ce->id);
        struct cent_living *c = cent_find(ce->id);
        if (!c)
        {
            /* handleSpawnMob's copy: renderYawOffset 0, the packet's head,
             * setPositionAndRotation's prev angles */
            c = cent_add();
            memset(c, 0, sizeof *c);
            c->id = ce->id;
            if (cfind_ok) cfind_put(c->id, (int)(c - cents));
            c->head = c->seen_head = c->prev_head = ce->head_yaw;
            c->prev_yaw = ce->yaw;
            c->prev_pitch = ce->pitch;
            c->srv_hurt = l ? l->hurt_time : 0;
            c->creeper_since = ce->creeper_since;
            c->creeper_last = ce->creeper_last;
            c->srv_health = l ? l->health : 1.0F;
            c->cli_health = c->srv_health;
            /* tryStartWachingThis's S04s and S1B: the equipment and the lead
             * as the tracker saw them */
            if (l)
            {
                memcpy(c->equip, l->equip, sizeof c->equip);
                c->leash_holder = c->pend_leash_holder = l->leash_holder;
                c->leash_knot = c->pend_leash_knot = l->leash_knot;
                c->leash_sends = l->leash_sends;
            }
        }
        else if (!c->seen)
        {
            c->prev_yaw = ce->yaw;
            c->prev_pitch = ce->pitch;
        }
        c->used = 1;
        int chicken = l && l->kind == AK_CHICKEN;
        /* the packets: an S19 head look sets the head. The chicken's body
         * helper runs in the replay's copy (its rider sits by it), whose
         * head and body are read: a head the helper alone would not have
         * made is a new S19, which the helper leaves as it came unless it
         * is 75 off the body */
        if (chicken)
        {
            double mdx = ce->x - ce->prev_x, mdz = ce->z - ce->prev_z;
            float want = mdx * mdx + mdz * mdz > 2.500000277905201E-7 ? body_helper_a(ce->yaw, c->head, 75.0F) : c->head;
            if (want != ce->head_yaw) c->head = ce->head_yaw;
        }
        else if (ce->head_yaw != c->seen_head) c->head = c->seen_head = ce->head_yaw;
        if (c->status_hurt)
        {
            /* EntityLivingBase.handleHealthUpdate(2): performHurtAnimation */
            c->limb_amount = 1.5F;
            c->hurt = 10;
        }
        if (c->status_death) c->dying = 1;
        c->status_hurt = c->status_death = 0;
        if (c->pend_equip_set) memcpy(c->equip, c->pend_equip, sizeof c->equip);
        c->pend_equip_set = 0;
        c->leash_holder = c->pend_leash_holder;
        c->leash_knot = c->pend_leash_knot;
        if (c->dead) continue;
        /* EntityCreeper.onUpdate ahead of super: the copy's own clocks over
         * its watched state (the ignited flag sets it to 1 on the copy) */
        if (l && l->kind == HK_CREEPER && !c->dying)
        {
            c->creeper_last = c->creeper_since;
            int state = (ce->kindw >> 9) & 1 ? 1 : (int)(int8_t)(ce->kindw & 255);
            c->creeper_since += state;
            if (c->creeper_since < 0) c->creeper_since = 0;
            if (c->creeper_since >= l->creeper_fuse_time) c->creeper_since = l->creeper_fuse_time;
            /* EntityCreeper.fall from the copy's own move this tick */
            if (ce->landed_fall > 0.0F)
            {
                c->creeper_since = (int)((float)c->creeper_since + ce->landed_fall * 1.5F);
                if (c->creeper_since > l->creeper_fuse_time - 5) c->creeper_since = l->creeper_fuse_time - 5;
            }
        }
        /* World.updateEntityWithOptionalForce, then onEntityUpdate */
        ++c->ticks;
        if (c->hurt > 0) --c->hurt;
        if (c->dying && ++c->death == 20) c->dead = 1;
        /* EntitySilverfish.onUpdate turns the body to the yaw before super */
        if (l && l->kind == HK_SILVERFISH) c->body = c->prev_yaw;
        c->prev_body = c->body;
        c->prev_head = c->head;
        /* moveEntityWithHeading's limbs over the copy's own step
         * (EntitySquid's override moves without them) */
        if (!(l && l->kind == AK_SQUID))
        {
            c->prev_limb_amount = c->limb_amount;
            double dx = ce->x - ce->prev_x, dz = ce->z - ce->prev_z;
            float f = (float)sqrt(dx * dx + dz * dz) * 4.0F;
            if (f > 1.0F) f = 1.0F;
            c->limb_amount += (f - c->limb_amount) * 0.4F;
            c->limb += c->limb_amount;
        }
        if (chicken)
        {
            c->body = ce->render_yaw_offset;
            c->head = c->seen_head = ce->head_yaw;
        }
        else cent_body(c, ce, l);
        while (c->body - c->prev_body < -180.0F) c->prev_body -= 360.0F;
        while (c->body - c->prev_body >= 180.0F) c->prev_body += 360.0F;
        while (ce->pitch - c->prev_pitch < -180.0F) c->prev_pitch -= 360.0F;
        while (ce->pitch - c->prev_pitch >= 180.0F) c->prev_pitch += 360.0F;
        while (c->head - c->prev_head < -180.0F) c->prev_head -= 360.0F;
        while (c->head - c->prev_head >= 180.0F) c->prev_head += 360.0F;
    }
    /* the kept copies closed up, none copied onto itself (a copy is 2 KB:
     * the self copies were 2% of a village env-step's DRAM fills,
     * lane/cachefit) */
    int k = 0;
    for (int i = 0; i < ncents; ++i)
        if (cents[i].used)
        {
            if (k != i) cents[k] = cents[i];
            ++k;
        }
    ncents = k;
    cfind_ok = 0;
}

/* The statuses this row's server sent (attackEntityFrom's setEntityState 2
 * when the hurt clock restarts, onDeath's 3 when the health reaches 0): the
 * client handles them at its next tick. */
static void cents_server_statuses(void)
{
    for (int i = 0; i < ncents; ++i)
    {
        struct cent_living *c = &cents[i];
        const struct living *l = cent_server_living(c->id);
        if (!l) continue;
        /* the S1B type 1 this row sent (recreateLeash sends none); a lead
         * the client's own interactFirst took or let go this tick shows now */
        if (l->leash_sends != c->leash_sends)
        {
            c->leash_sends = l->leash_sends;
            c->pend_leash_holder = l->leash_holder;
            c->pend_leash_knot = l->leash_knot;
        }
        for (int k = 0; k < CP.nuse; ++k)
            if (CP.use_ids[k] == c->id)
            {
                c->leash_holder = c->pend_leash_holder = l->leash_holder;
                c->leash_knot = c->pend_leash_knot = l->leash_knot;
            }
        /* the S04s this row's update heads sent */
        if (l->equip_sent_valid)
        {
            memcpy(c->pend_equip, l->equip_sent, sizeof c->pend_equip);
            c->pend_equip_set = 1;
        }
        if (l->hurt_time > 0 && l->hurt_time >= c->srv_hurt) c->status_hurt = 1;
        if (l->health <= 0.0F && c->srv_health > 0.0F) c->status_death = 1;
        c->srv_hurt = l->hurt_time;
        c->srv_health = l->health;
    }
}

/* ------------------------------------------------------------- the dragon */

static void cents_dragon_tick(void)
{
    const struct pickobj *o = cent_dragon_copy();
    if (!o)
    {
        cdragon.used = 0;
        return;
    }
    const struct dragon_interp *ip = NULL;
    const struct dragon_state *d = pickobj_client_dragon(o, &ip);
    const struct dragon_state *sd = SR.end != NULL && SR.end->dragon_listed ? &SR.end->d : NULL;
    if (!cdragon.used || cdragon.id != o->id)
    {
        memset(&cdragon, 0, sizeof cdragon);
        cdragon.used = 1;
        cdragon.id = o->id;
        cdragon.x = d->x; cdragon.y = d->y; cdragon.z = d->z;
        cdragon.srv_hurt = sd ? sd->hurt_time : 0;
    }
    if (cdragon.status_hurt) cdragon.hurt = 10;
    cdragon.status_hurt = 0;
    ++cdragon.ticks;
    cdragon.prev_x = cdragon.x; cdragon.prev_y = cdragon.y; cdragon.prev_z = cdragon.z;
    cdragon.x = d->x; cdragon.y = d->y; cdragon.z = d->z;
    if (cdragon.hurt > 0) --cdragon.hurt;
    /* EntityDragon.onLivingUpdate: animTime over the copy's motion (the S0F
     * and S12 velocity; slowed is the server's alone) */
    cdragon.prev_anim = cdragon.anim;
    if (ip->health > 0.0F)
    {
        double mx = o->e.motion_x, my = o->e.motion_y, mz = o->e.motion_z;
        float f = 0.2F / ((float)sqrt(mx * mx + mz * mz) * 10.0F + 1.0F);
        f *= (float)fd_pow(2.0, my);
        cdragon.anim += f;
    }
}

static void cents_dragon_status(void)
{
    const struct dragon_state *sd = SR.end != NULL && SR.end->dragon_listed ? &SR.end->d : NULL;
    if (!cdragon.used || !sd || sd->entity_id != cdragon.id) return;
    if (sd->hurt_time > 0 && sd->hurt_time >= cdragon.srv_hurt) cdragon.status_hurt = 1;
    cdragon.srv_hurt = sd->hurt_time;
}

/* ------------------------------------------------------------------ a row */


/* ------------------------------------------------- the items and XP orbs */

/* one per item and orb of the replay's two item pools (IE_MAX_ENTITIES each);
 * a tick sends each at most a spawn or a destroy, a move, a velocity and a
 * metadata packet */
enum { CITEM_MAX = 2 * IE_MAX_ENTITIES, CPKT_MAX = 4 * CITEM_MAX };
/* EntityTracker's entityViewDistance, taken in WorldServer's constructor:
 * IntegratedPlayerList starts at view distance 10 and the integrated
 * server's tick only later sets the client's render distance, so every
 * world's tracker caps its ranges at getFurthestViewableBlock(10), not at
 * the player manager's current view */
enum { CITEM_TRACKER_VIEW = 10 * 16 - 16 };

/* One server item or orb as EntityTracker keeps it, and the client's copy. */
struct citem {
    int id, kind, seen, before_player;
    int destroy_wait;                 /* gone after the player's update: the S13 waits a row */
    int chunk_watched;                /* its chunk was watched at the last row (func_85172_a) */
    struct tracker_entry te;          /* EntityTrackerEntry for the one player */
    int sent_item, sent_damage, sent_count;   /* DataWatcher 10 as last sent */
    /* the client's EntityItem / EntityXPOrb */
    int has_copy, collected;
    int collect_on_spawn;             /* collected in the row that spawned it: the S0D follows the spawn */
    struct entity e;
    double prev_x, prev_y, prev_z;
    int server_x, server_y, server_z;
    float hover;
    int age, item, damage, count, tag;
    int xp_value, xp_color, xp_target_color, has_target;
};
static struct citem citems[CITEM_MAX];
static int ncitems;

/* the packets for these entities in flight to the next client tick */
enum { CP_SPAWN = 1, CP_MOVE, CP_TELEPORT, CP_VELOCITY, CP_META, CP_DESTROY };
static struct cpkt {
    int kind, id, has_pos, has_rot;
    int join;                         /* sent by the session's first tracker pass */
    int x, y, z, yaw, pitch, mx, my, mz;
    int item, damage, count, tag, xp;
    float hover;
} cpkts[CPKT_MAX];
static int ncpkts;
static struct tracker citem_tracker;  /* tracker_entry_tick's own flags */
static int citem_passes;              /* citems_server passes this session */
static int citem_player_watch;        /* the player's own entry has checked its position */
static double citem_player_x, citem_player_y, citem_player_z;

static struct citem *citem_find(int id)
{
    for (int i = 0; i < ncitems; ++i)
        if (citems[i].id == id) return &citems[i];
    return NULL;
}

static struct cpkt *cpkt_add(int kind, int id)
{
    if (ncpkts == CPKT_MAX) list_full("item packets in flight in the play client", CPKT_MAX);
    struct cpkt *p = &cpkts[ncpkts++];
    memset(p, 0, sizeof *p);
    p->kind = kind;
    p->id = id;
    return p;
}

static int clamp_velocity(double m)
{
    if (m < -3.9) m = -3.9;
    if (m > 3.9) m = 3.9;
    return (int)(m * 8000.0);
}

/* the item or orb behind an entity pass entry, NULL for any other; DEAD
 * also takes one that died in this row */
static const ie_ent *citem_of_any(const struct sr_ent *rec, int dead)
{
    const ie_ent *ie = NULL;
    if (rec->pool == 0) ie = sr_ent_p(&SR, rec);
    else if (rec->pool == 2)
    {
        const struct an_ent *ae = sr_ent_p(&SR, rec);
        if (ae == NULL || ae->is_living) return NULL;
        ie = ie_get(ae->ieh);
    }
    if (ie == NULL || (ie->is_dead && !dead) || (ie->kind != IE_ITEM && ie->kind != IE_ORB)) return NULL;
    return ie;
}

static const ie_ent *citem_of(const struct sr_ent *rec)
{
    return citem_of_any(rec, 0);
}

/* func_151260_c's packet (S0E type 2 data 1 for the item, S11 for the orb),
 * the S1C of the item's stack, the start-watching S12 */
static void citem_spawn_packets(struct citem *c, const ie_ent *ie, double x, double y, double z)
{
    struct cpkt *p = cpkt_add(CP_SPAWN, c->id);
    if (!p) return;
    p->join = citem_passes == 0;
    p->x = mh_floor(x * 32.0);
    p->y = mh_floor(y * 32.0);
    p->z = mh_floor(z * 32.0);
    p->yaw = (int8_t)mh_floor((double)(ie->rotation_yaw * 256.0F / 360.0F));
    p->pitch = (int8_t)mh_floor((double)(ie->rotation_pitch * 256.0F / 360.0F));
    p->mx = clamp_velocity(ie->e.motion_x);
    p->my = clamp_velocity(ie->e.motion_y);
    p->mz = clamp_velocity(ie->e.motion_z);
    p->item = ie->stack_item;
    p->damage = ie->stack_damage;
    p->count = ie->stack_count;
    p->tag = ie->stack_tag;
    p->xp = ie->xp_value;
    p->hover = ie->hover_start;
    c->sent_item = ie->stack_item;
    c->sent_damage = ie->stack_damage;
    c->sent_count = ie->stack_count;
    c->te.last_motion_x = ie->e.motion_x;
    c->te.last_motion_y = ie->e.motion_y;
    c->te.last_motion_z = ie->e.motion_z;
}

/* EntityTrackerEntry.tryStartWachingThis for the one player */
static void citem_try_watch(struct citem *c, const ie_ent *ie)
{
    const struct server_player *sp = SR.player;
    if (sp == NULL) return;
    double dx = sp->e.pos_x - (double)(c->te.last_scaled_x / 32);
    double dz = sp->e.pos_z - (double)(c->te.last_scaled_z / 32);
    double r = (double)c->te.tracking_range;
    if (dx >= -r && dx <= r && dz >= -r && dz <= r)
    {
        if (!c->te.watched && SR.d->cl != NULL && cl_player_watching(SR.d->cl, ie->chunk_x, ie->chunk_z))
        {
            c->te.watched = true;
            citem_spawn_packets(c, ie, ie->e.pos_x, ie->e.pos_y, ie->e.pos_z);
        }
    }
    else if (c->te.watched)
    {
        c->te.watched = false;
        cpkt_add(CP_DESTROY, c->id);
    }
}

/* EntityTracker for the items and orbs of the player's world. Its
 * updateTrackedEntities runs after the world's updateEntities and before the
 * network tick (MinecraftServer.updateTimeLightAndEntities), so it sees the
 * player where the last tick's C03 left it: trackEntity for the new ones
 * (the entry at the spawn position, its first watch), then each entry's
 * sendLocationToAllClients, the S13 of the ones gone, and the player's own
 * four-block re-check. NETWORK, after the row: only trackEntity for the ones
 * the network tick spawned (a dig's drops, a thrown item); their first
 * sendLocationToAllClients waits for the next tick's pass. */
static void citems_server(int network)
{
    const struct server_player *sp = SR.player;
    if (sp == NULL || SR.here != serverreplay_player_dim(&SR)) return;
    int player = -1;
    for (int k = 0; k < SR.d->nents; ++k)
        if (SR.d->ents[k].pool == 3) { player = k; break; }
    for (int i = 0; i < ncitems; ++i) citems[i].seen = 0;

    for (int k = 0; k < SR.d->nents; ++k)
    {
        /* one spawned and gone in this row (collected at once, an orb by a
         * player standing on it): trackEntity sent its spawn at
         * spawnEntityInWorld, the S13 follows at the next row */
        const ie_ent *ie = citem_of_any(&SR.d->ents[k], 1);
        if (!ie) continue;
        struct citem *c = citem_find(ie->entity_id);
        if (ie->is_dead && (c != NULL || ie->ticks_existed != 0)) continue;
        if (network && c != NULL) continue;
        if (!c)
        {
            if (ncitems == CITEM_MAX) list_full("item copies in the play client", CITEM_MAX);
            c = &citems[ncitems++];
            memset(c, 0, sizeof *c);
            c->id = ie->entity_id;
            c->kind = ie->kind;
            /* the entry at spawnEntityInWorld: the position before the
             * entity's first update, if it has had one */
            double sx = ie->ticks_existed > 0 ? ie->prev_x : ie->e.pos_x;
            double sy = ie->ticks_existed > 0 ? ie->prev_y : ie->e.pos_y;
            double sz = ie->ticks_existed > 0 ? ie->prev_z : ie->e.pos_z;
            struct tracker_entry *te = &c->te;
            te->id = c->id;
            te->last_scaled_x = mh_floor(sx * 32.0);
            te->last_scaled_y = mh_floor(sy * 32.0);
            te->last_scaled_z = mh_floor(sz * 32.0);
            te->last_yaw = mh_floor((double)(ie->rotation_yaw * 256.0F / 360.0F));
            te->last_pitch = mh_floor((double)(ie->rotation_pitch * 256.0F / 360.0F));
            /* EntityTracker.trackEntity: an item at 64, an orb at 160, both
             * every 20 ticks with velocity, the range capped at the
             * tracker's entityViewDistance */
            int range = ie->kind == IE_ORB ? 160 : 64;
            te->tracking_range = range > CITEM_TRACKER_VIEW ? CITEM_TRACKER_VIEW : range;
            te->update_frequency = 20;
            te->send_velocity_updates = true;
            te->size = tracker_entity_size(ie->e.width);
            c->chunk_watched = SR.d->cl != NULL && cl_player_watching(SR.d->cl, ie->chunk_x, ie->chunk_z);
            citem_try_watch(c, ie);
            if (c->te.watched)
            {
                /* the spawn went out with the spawn position */
                struct cpkt *p = &cpkts[ncpkts - 1];
                if (p->kind == CP_SPAWN && p->id == c->id)
                {
                    p->x = mh_floor(sx * 32.0);
                    p->y = mh_floor(sy * 32.0);
                    p->z = mh_floor(sz * 32.0);
                }
            }
        }
        c->seen = 1;
        c->before_player = player >= 0 && k < player;
        if (network || ie->is_dead) continue;

        struct tracker_entry *te = &c->te;
        /* func_85172_a: its chunk just went to the player */
        int cw = SR.d->cl != NULL && cl_player_watching(SR.d->cl, ie->chunk_x, ie->chunk_z);
        if (cw && !c->chunk_watched && !te->watched) citem_try_watch(c, ie);
        c->chunk_watched = cw;
        /* sendLocationToAllClients' head */
        double wx = ie->e.pos_x - te->watch_x, wy = ie->e.pos_y - te->watch_y, wz = ie->e.pos_z - te->watch_z;
        if (!te->watch_initialized || wx * wx + wy * wy + wz * wz > 16.0)
        {
            te->watch_initialized = true;
            te->watch_x = ie->e.pos_x;
            te->watch_y = ie->e.pos_y;
            te->watch_z = ie->e.pos_z;
            citem_try_watch(c, ie);
        }
        te->x = ie->e.pos_x;
        te->y = ie->e.pos_y;
        te->z = ie->e.pos_z;
        te->yaw = ie->rotation_yaw;
        te->pitch = ie->rotation_pitch;
        te->motion_x = ie->e.motion_x;
        te->motion_y = ie->e.motion_y;
        te->motion_z = ie->e.motion_z;
        /* DataWatcher 10: a merge changes the stack */
        int meta = ie->kind == IE_ITEM && (ie->stack_item != c->sent_item || ie->stack_damage != c->sent_damage ||
                                           ie->stack_count != c->sent_count);
        if (meta) te->dw_dirty = true;
        struct tracker_packet pk[4];
        int n = tracker_entry_tick(&citem_tracker, te, pk, 4);
        for (int i = 0; te->watched && i < n; ++i)
        {
            int kind = pk[i].kind == TRACKER_PKT_S18 ? CP_TELEPORT : pk[i].kind == TRACKER_PKT_S12 ? CP_VELOCITY :
                       pk[i].kind == TRACKER_PKT_S15 || pk[i].kind == TRACKER_PKT_S16 || pk[i].kind == TRACKER_PKT_S17 ? CP_MOVE : 0;
            struct cpkt *p = kind ? cpkt_add(kind, c->id) : NULL;
            if (!p) continue;
            p->has_pos = pk[i].kind != TRACKER_PKT_S16;
            p->has_rot = pk[i].kind != TRACKER_PKT_S15;
            p->x = pk[i].x; p->y = pk[i].y; p->z = pk[i].z;
            p->yaw = pk[i].yaw; p->pitch = pk[i].pitch;
            if (kind == CP_VELOCITY)
            {
                p->mx = clamp_velocity((double)pk[i].mx / 8000.0);
                p->my = clamp_velocity((double)pk[i].my / 8000.0);
                p->mz = clamp_velocity((double)pk[i].mz / 8000.0);
            }
        }
        if (meta)
        {
            c->sent_item = ie->stack_item;
            c->sent_damage = ie->stack_damage;
            c->sent_count = ie->stack_count;
            struct cpkt *p = te->watched ? cpkt_add(CP_META, c->id) : NULL;
            if (p) { p->item = ie->stack_item; p->damage = ie->stack_damage; p->count = ie->stack_count; p->tag = ie->stack_tag; }
        }
    }

    if (network) return;
    /* the ones gone from the world: the tracker's S13 */
    for (int i = 0; i < ncitems; )
    {
        struct citem *c = &citems[i];
        if (c->seen || c->has_copy) { ++i; continue; }
        citems[i] = citems[--ncitems];
    }
    /* the tracker's removal queues the id in the player's
     * destroyedItemsNetCache, which EntityPlayerMP.onUpdate sends: in this
     * row when the entity left the pass ahead of the player, else in the
     * next */
    for (int i = 0; i < ncitems; ++i)
    {
        struct citem *c = &citems[i];
        if (c->seen || !c->te.watched) continue;
        if (!c->before_player && !c->destroy_wait)
        {
            c->destroy_wait = 1;
            continue;
        }
        c->te.watched = false;
        cpkt_add(CP_DESTROY, c->id);
    }

    ++citem_passes;
    /* the player's own entry: four blocks since its last check, every
     * entry tries the player again */
    double px = sp->e.pos_x - citem_player_x, py = sp->e.pos_y - citem_player_y, pz = sp->e.pos_z - citem_player_z;
    if (!citem_player_watch || px * px + py * py + pz * pz > 16.0)
    {
        int first = !citem_player_watch;
        citem_player_watch = 1;
        citem_player_x = sp->e.pos_x;
        citem_player_y = sp->e.pos_y;
        citem_player_z = sp->e.pos_z;
        for (int k = 0; !first && k < SR.d->nents; ++k)
        {
            const ie_ent *ie = citem_of(&SR.d->ents[k]);
            struct citem *c = ie ? citem_find(ie->entity_id) : NULL;
            if (c) citem_try_watch(c, ie);
        }
    }
}

/* Entity.setPositionAndRotation2 for a non-living copy: the position, then
 * the lift out of any block box its box (0.03125 in on x and z) meets */
static void citem_set_position2(struct citem *c, double x, double y, double z)
{
    entity_set_position(&c->e, x, y, z);
    struct aabb q = c->e.bounding_box;
    q.min_x += 0.03125; q.max_x -= 0.03125;
    q.min_z += 0.03125; q.max_z -= 0.03125;
    struct collide_list *l COLLIDE_SCRATCH = collide_scratch_begin();
    collide_list_clear(l);
    world_get_colliding_bounding_boxes(c->e.world, q, l);
    if (l->n == 0) return;
    double top = 0.0;
    for (int i = 0; i < l->n; ++i)
        if (l->box[i].max_y > top) top = l->box[i].max_y;
    y += top - c->e.bounding_box.min_y;
    entity_set_position(&c->e, x, y, z);
}

/* NetHandlerPlayClient's handlers for the packets the last row sent */
static void citems_packets(void)
{
    for (int i = 0; i < ncpkts; ++i)
    {
        const struct cpkt *p = &cpkts[i];
        struct citem *c = citem_find(p->id);
        if (!c) continue;
        if (p->kind == CP_SPAWN)
        {
            if (c->has_copy) continue;
            /* handleSpawnObject's new EntityItem(world, x, y, z) (0.25 box,
             * yOffset half its height), handleSpawnExperienceOrb's
             * EntityXPOrb (0.5), the packet's angles and velocity */
            c->has_copy = 1;
            /* handleCollectItem right after the spawn: the pickup effect
             * takes the copy before it ever updates */
            c->collected = c->collect_on_spawn;
            entity_init(&c->e, CW);
            c->e.can_trigger_walking = 0;
            float s = c->kind == IE_ORB ? 0.5F : 0.25F;
            entity_set_size(&c->e, s, s);
            c->e.y_offset = c->e.height / 2.0F;
            /* handleSpawnExperienceOrb builds the orb at the S11's raw
             * 1/32-block fields, thirty-two times too far out: it stays out
             * there until the tracker's first move puts it at serverPos / 32 */
            double sc = c->kind == IE_ORB ? 1.0 : 1.0 / 32.0;
            entity_set_position(&c->e, (double)p->x * sc, (double)p->y * sc, (double)p->z * sc);
            c->prev_x = c->e.pos_x; c->prev_y = c->e.pos_y; c->prev_z = c->e.pos_z;
            c->server_x = p->x; c->server_y = p->y; c->server_z = p->z;
            c->e.motion_x = (double)p->mx / 8000.0;
            c->e.motion_y = (double)p->my / 8000.0;
            c->e.motion_z = (double)p->mz / 8000.0;
            c->hover = p->hover;
            if (c->kind == IE_ITEM)
            {
                /* the EntityItem constructor's four draws (hoverStart,
                 * rotationYaw, motionX, motionZ) on the item's pinned stream */
                /* the join's spawns went out before the snapshot's first
                 * row, and the client took them in that row's tick */
                det_rng *r = det_pin_at(&pin_item, p->join ? live_t - 1 : live_t);
                c->hover = (float)(det_rng_double(r) * 3.141592653589793 * 2.0);
                for (int k = 0; k < 3; ++k) (void)det_rng_double(r);
            }
            c->age = 0;
            c->item = p->item; c->damage = p->damage; c->count = p->count; c->tag = p->tag;
            c->xp_value = p->xp;
            c->xp_color = c->xp_target_color = c->has_target = 0;
            continue;
        }
        if (!c->has_copy) continue;
        switch (p->kind)
        {
        case CP_MOVE:
            c->server_x += p->x; c->server_y += p->y; c->server_z += p->z;
            citem_set_position2(c, (double)c->server_x / 32.0, (double)c->server_y / 32.0, (double)c->server_z / 32.0);
            break;
        case CP_TELEPORT:
            c->server_x = p->x; c->server_y = p->y; c->server_z = p->z;
            citem_set_position2(c, (double)c->server_x / 32.0, (double)c->server_y / 32.0 + 0.015625,
                                (double)c->server_z / 32.0);
            break;
        case CP_VELOCITY:
            c->e.motion_x = (double)p->mx / 8000.0;
            c->e.motion_y = (double)p->my / 8000.0;
            c->e.motion_z = (double)p->mz / 8000.0;
            break;
        case CP_META:
            c->item = p->item; c->damage = p->damage; c->count = p->count; c->tag = p->tag;
            break;
        case CP_DESTROY:
            c->has_copy = 0;
            break;
        }
    }
    ncpkts = 0;
}

/* World.func_147469_q: the cell's pool box averages a full block on an edge */
static int citem_solid(struct world *w, int x, int y, int z)
{
    struct aabb b;
    if (!collide_pool_box(w, x, y, z, &b)) return 0;
    return (b.max_x - b.min_x + b.max_y - b.min_y + b.max_z - b.min_z) / 3.0 >= 1.0;
}

/* Entity.pushOutOfBlocks (func_145771_j) at the box's centre: the nearest
 * open face's direction. The speed is the copy's own Random's (nextFloat *
 * 0.2 + 0.1), which no one can know; its mean stands in. */
static int citem_push_out(struct entity *e)
{
    double px = e->pos_x, py = (e->bounding_box.min_y + e->bounding_box.max_y) / 2.0, pz = e->pos_z;
    int x = mh_floor(px), y = mh_floor(py), z = mh_floor(pz);
    double fx = px - (double)x, fy = py - (double)y, fz = pz - (double)z;
    struct collide_list *l COLLIDE_SCRATCH = collide_scratch_begin();
    collide_list_clear(l);
    world_get_colliding_bounding_boxes(e->world, e->bounding_box, l);
    if (l->n == 0 && !citem_solid(e->world, x, y, z)) return 0;
    int dir = 3;
    double best = 9999.0;
    if (!citem_solid(e->world, x - 1, y, z) && fx < best) { best = fx; dir = 0; }
    if (!citem_solid(e->world, x + 1, y, z) && 1.0 - fx < best) { best = 1.0 - fx; dir = 1; }
    if (!citem_solid(e->world, x, y + 1, z) && 1.0 - fy < best) { best = 1.0 - fy; dir = 3; }
    if (!citem_solid(e->world, x, y, z - 1) && fz < best) { best = fz; dir = 4; }
    if (!citem_solid(e->world, x, y, z + 1) && 1.0 - fz < best) { best = 1.0 - fz; dir = 5; }
    float v = 0.2F;
    if (dir == 0) e->motion_x = (double)(-v);
    if (dir == 1) e->motion_x = (double)v;
    if (dir == 3) e->motion_y = (double)v;
    if (dir == 4) e->motion_z = (double)(-v);
    if (dir == 5) e->motion_z = (double)v;
    return 1;
}

/* EntityItem.onUpdate and EntityXPOrb.onUpdate on the client world: the
 * water's push, the fall, moveEntity over the client's blocks, the friction
 * and bounce, the age; the orb's pull toward the client player within 8
 * blocks. (The lava's random hop, whose draws are the copy's own Random,
 * is left out.) */
static void citem_update(struct citem *c)
{
    struct entity *e = &c->e;
    c->prev_x = e->pos_x; c->prev_y = e->pos_y; c->prev_z = e->pos_z;
    ie_water_accelerate(e->world, e, 1);
    e->motion_y -= c->kind == IE_ORB ? 0.029999999329447746 : 0.03999999910593033;
    /* the item keeps the push's answer as noClip, the orb drops it */
    int push = citem_push_out(e);
    if (c->kind == IE_ITEM) e->no_clip = (uint8_t)push;
    if (c->kind == IE_ORB)
    {
        const double d0 = 8.0;
        double ex = CP.e.pos_x, ey = CP.e.pos_y, ez = CP.e.pos_z;
        double qx = ex - e->pos_x, qy = ey - e->pos_y, qz = ez - e->pos_z;
        if (c->xp_target_color < c->xp_color - 20 + c->id % 100)
        {
            if (!c->has_target || qx * qx + qy * qy + qz * qz > d0 * d0)
                c->has_target = qx * qx + qy * qy + qz * qz < d0 * d0 && CP.sv.health > 0.0F;
            c->xp_target_color = c->xp_color;
        }
        if (c->has_target)
        {
            /* EntityPlayerSP.getEyeHeight is 0.12 over its eye-level posY */
            double d1 = (ex - e->pos_x) / d0;
            double d2 = (ey + (double)0.12F - e->pos_y) / d0;
            double d3 = (ez - e->pos_z) / d0;
            double d4 = sqrt(d1 * d1 + d2 * d2 + d3 * d3);
            double d5 = 1.0 - d4;
            if (d5 > 0.0)
            {
                d5 *= d5;
                e->motion_x += d1 / d4 * d5 * 0.1;
                e->motion_y += d2 / d4 * d5 * 0.1;
                e->motion_z += d3 / d4 * d5 * 0.1;
            }
        }
    }
    entity_move(e, e->motion_x, e->motion_y, e->motion_z);
    float f = 0.98F;
    if (e->on_ground)
    {
        int bx = mh_floor(e->pos_x), by = mh_floor(e->bounding_box.min_y) - 1, bz = mh_floor(e->pos_z);
        f = BLOCKS[world_get_block(e->world, bx, by, bz) & 4095].slipperiness * 0.98F;
    }
    e->motion_x *= (double)f;
    e->motion_y *= 0.9800000190734863;
    e->motion_z *= (double)f;
    if (e->on_ground) e->motion_y *= c->kind == IE_ORB ? -0.8999999761581421 : -0.5;
    if (c->kind == IE_ORB) ++c->xp_color;
    ++c->age;
}

static void citems_client_tick(void)
{
    citems_packets();
    for (int i = 0; i < ncitems; ++i)
    {
        struct citem *c = &citems[i];
        if (!c->has_copy) continue;
        if (c->collected)
        {
            /* handleCollectItem: the copy leaves the world (the pickup
             * effect took it) */
            c->has_copy = 0;
            continue;
        }
        citem_update(c);
    }
}


/* RenderItem's and RenderXPOrb's inputs from a copy at partial tick PT:
 * lastTickPos toward the position, the light at the position's
 * getBrightnessForRender cell (EntityXPOrb's block-light boost), the
 * client's own age and colour clock */
static void citem_fill(struct worldfx_entity *w, const struct citem *c, float pt)
{
    memset(w, 0, sizeof *w);
    const struct entity *e = &c->e;
    w->x = c->prev_x + (e->pos_x - c->prev_x) * pt;
    w->y = c->prev_y + (e->pos_y - c->prev_y) * pt;
    w->z = c->prev_z + (e->pos_z - c->prev_z) * pt;
    int bx = mh_floor(e->pos_x), bz = mh_floor(e->pos_z);
    int by = mh_floor(e->bounding_box.min_y + (e->bounding_box.max_y - e->bounding_box.min_y) * 0.66);
    w->light = (world_get_light(view_world(), LIGHT_SKY, bx, by, bz) << 20) |
               (world_get_light(view_world(), LIGHT_BLOCK, bx, by, bz) << 4);
    if (c->kind == IE_ORB)
    {
        w->orb = 1;
        w->count = c->xp_value;
        w->color = c->xp_color;
        int low = (w->light & 255) + 120;
        if (low > 240) low = 240;
        w->light = low | (w->light & ~255);
        return;
    }
    w->id = c->item;
    w->meta = c->damage;
    w->count = c->count;
    w->tag = c->tag;
    w->age = c->age;
    w->hover = c->hover;
}

/* ------------------------------------------------------------------ a row */

/* After a row: the livings' and the dragon's client tick it ran, then what
 * this row's server sent (the items' trackEntity for the network tick's
 * spawns; their tracker pass ran after the world tick). */
static void cents_after_row(void)
{
    if (!SS.server_rows || !SS.sr) return;
    cent_index_build();
    if (cents_client_ticked)
    {
        if (SR.combat) cents_livings_tick();
        cents_dragon_tick();
    }
    cents_client_ticked = 0;
    cents_server_statuses();
    cent_index_drop();
    cents_dragon_status();
    citems_server(1);
}

/* After the row's world tick, before its network tick: the items' and orbs'
 * copies take the client tick that ran (the packets the row before sent,
 * then each copy's update), then the world's EntityTracker pass. */
static void cents_after_world_tick(void)
{
    if (!SS.server_rows || !SS.sr) return;
    if (cents_client_ticked) citems_client_tick();
    citems_server(0);
}

/* A new WorldClient: none of the old copies survive, and every tracker
 * entry has lost its player. */
static void cents_world_changed(void)
{
    ncents = 0;
    cfind_ok = 0;
    memset(&cdragon, 0, sizeof cdragon);
    ncitems = 0;
    ncpkts = 0;
    citem_player_watch = 0;
}
