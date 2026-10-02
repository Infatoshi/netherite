/* See sleep.h. */
#include "sleep.h"
#include "env.h"

#include <math.h>
#include <string.h>

#include "blocks.h"
#include "living.h"
#include "player.h"
#include "riding.h"
#include "serverreplay.h"
#include "survival.h"
#include "tnt.h"
#include "chatmsg.h"

#define BLOCK_BED 26

/* BlockBed.field_149981_a: the foot-to-head offset per direction. */
static const int BED_OFFSET[4][2] = {{0, 1}, {-1, 0}, {0, -1}, {1, 0}};

/* The EntityPlayer.EnumStatus values sleepInBedAt answers. */
enum { SLEEP_OK, SLEEP_NOT_POSSIBLE_HERE, SLEEP_NOT_POSSIBLE_NOW, SLEEP_TOO_FAR_AWAY,
       SLEEP_OTHER_PROBLEM, SLEEP_NOT_SAFE };

static int block_at(struct world *w, int x, int y, int z)
{
    return world_get_block(w, x, y, z) & 4095;
}

/* EntityPlayerMP.updateFallState is empty: the handler's handleFalling runs
 * instead, and a resize's move is no fall. */
static void no_fall(struct entity *e, double dy, int on_ground)
{
    (void)e;
    (void)dy;
    (void)on_ground;
}

/* Entity.setSize: the box's max corner follows the new size, and a wider
 * server entity past its first update steps back by the growth. */
static void set_size(struct entity *e, float width, float height, int server, int sneaking)
{
    if (width == e->width && height == e->height) return;

    float var3 = e->width;
    e->width = width;
    e->height = height;
    e->bounding_box.max_x = e->bounding_box.min_x + (double)e->width;
    e->bounding_box.max_z = e->bounding_box.min_z + (double)e->width;
    e->bounding_box.max_y = e->bounding_box.min_y + (double)e->height;

    if (e->width > var3 && !e->first_update && server)
        entity_move_ex(e, (double)(var3 - e->width), 0.0, (double)(var3 - e->width), sneaking, no_fall);
}

/* World.isDaytime over the overworld's skylightSubtracted. */
static int server_is_daytime(const struct server_player *p)
{
    if (p->replay == NULL) return 1;
    return p->replay->w[0].st.skylight_subtracted < 4;
}

/* WorldServer.updateAllPlayersSleepingFlag: the one player decides it. */
static void update_sleeping_flag(struct server_player *p)
{
    if (p->replay != NULL) p->replay->w[0].st.all_players_sleeping = p->sv.sleeping != 0;
}

/* World.getEntitiesWithinAABB(EntityMob.class, box) is not empty. */
static int monsters_near(int x, int y, int z)
{
    if (surv.anw == NULL) return 0;

    struct aabb box = aabb_make((double)x - 8.0, (double)y - 5.0, (double)z - 8.0,
                                (double)x + 8.0, (double)y + 5.0, (double)z + 8.0);
    AN_QUERY_LIST(near);
    int n = an_entities_excluding(surv.anw, NULL, &box, near, AN_MAX_ENTITIES);

    for (int i = 0; i < n; ++i)
    {
        if (!near[i]->is_living) continue;

        switch (lv_get(near[i]->livh)->kind)
        {
            case HK_ZOMBIE: case HK_SKELETON: case HK_CREEPER: case HK_SPIDER: case HK_ENDERMAN:
            case HK_WITCH: case HK_SILVERFISH: case HK_PIGMAN: case HK_BLAZE: case HK_CAVE_SPIDER:
                return 1;
        }
    }

    return 0;
}

/* NetHandlerPlayServer.setPlayerLocation at the player's own position: the
 * S08 (posY + 1.62, onGround false) and the handler's move bookkeeping. */
static void set_player_location(struct server_player *p)
{
    struct entity *e = &p->e;

    p->has_moved = 0;
    p->last_pos_x = e->pos_x;
    p->last_pos_y = e->pos_y;
    p->last_pos_z = e->pos_z;
    entity_set_pos_rot(e, e->pos_x, e->pos_y, e->pos_z, &p->rotation_yaw, &p->rotation_pitch,
                       &p->prev_rotation_yaw, &p->prev_rotation_pitch, p->rotation_yaw, p->rotation_pitch);

    struct s2c_pkt *pkt = s2c_add(s2c_out());
    pkt->kind = PK_S08;
    pkt->f0 = e->pos_x;
    pkt->f1 = e->pos_y + 1.6200000047683716;
    pkt->f2 = e->pos_z;
    pkt->f3 = p->rotation_yaw;
    pkt->f4 = p->rotation_pitch;
}

/* EntityPlayer.sleepInBedAt's position: the head cell, pulled toward the
 * foot by the direction. */
static void bed_position(struct world *w, int x, int y, int z, double *px, double *py, double *pz)
{
    float var10 = 0.5F, var7 = 0.5F;

    if (world_chunk(w, x >> 4, z >> 4) != NULL)
    {
        switch (world_get_meta(w, x, y, z) & 3)
        {
            case 0: var7 = 0.9F; break;
            case 1: var10 = 0.1F; break;
            case 2: var7 = 0.1F; break;
            case 3: var10 = 0.9F; break;
        }
    }

    *px = (double)((float)x + var10);
    *py = (double)((float)y + 0.9375F);
    *pz = (double)((float)z + var7);
}

/* EntityPlayerMP.sleepInBedAt. */
static int sleep_in_bed_at(struct server_player *p, int x, int y, int z)
{
    struct surv_state *sv = &p->sv;
    struct entity *e = &p->e;

    if (sv->sleeping || sv->dead || sv->health <= 0.0F) return SLEEP_OTHER_PROBLEM;
    if (p->dimension != 0) return SLEEP_NOT_POSSIBLE_HERE;
    if (server_is_daytime(p)) return SLEEP_NOT_POSSIBLE_NOW;

    if (fabs(e->pos_x - (double)x) > 3.0 || fabs(e->pos_y - (double)y) > 2.0 || fabs(e->pos_z - (double)z) > 3.0)
        return SLEEP_TOO_FAR_AWAY;

    if (monsters_near(x, y, z)) return SLEEP_NOT_SAFE;

    /* isRiding: mountEntity(null) first (its S1B and S08) */
    if (p->ridingh != 0) ride_server_dismount(p);

    set_size(e, 0.2F, 0.2F, 1, p->sneaking);
    e->y_offset = 0.2F;

    double px, py, pz;
    bed_position(e->world, x, y, z, &px, &py, &pz);
    entity_set_position(e, px, py, pz);

    sv->sleeping = 1;
    sv->sleep_timer = 0;
    sv->has_bed = 1;
    sv->bed_x = x;
    sv->bed_y = y;
    sv->bed_z = z;
    e->motion_x = e->motion_z = e->motion_y = 0.0;
    update_sleeping_flag(p);

    /* the S0A to the trackers (no other player), setPlayerLocation's S08,
     * then the S0A to the player itself */
    set_player_location(p);

    struct s2c_pkt *pkt = s2c_add(s2c_out());
    pkt->kind = PK_S0A;
    pkt->i0 = x;
    pkt->i1 = y;
    pkt->i2 = z;

    return SLEEP_OK;
}

/* BlockBed.func_149979_a: the occupied bit, a flag-4 metadata write. */
static void set_occupied(struct world *w, int x, int y, int z, int occupied)
{
    int meta = world_get_meta(w, x, y, z);
    world_set_meta(w, x, y, z, occupied ? meta | 4 : meta & -5, 4);
}

int sleep_bed_activate(struct server_player *p, int x, int y, int z)
{
    struct world *w = p->e.world;
    int var10 = world_get_meta(w, x, y, z);

    if ((var10 & 8) == 0)
    {
        int dir = var10 & 3;
        x += BED_OFFSET[dir][0];
        z += BED_OFFSET[dir][1];

        if (block_at(w, x, y, z) != BLOCK_BED) return 1;

        var10 = world_get_meta(w, x, y, z);
    }

    /* provider.canRespawnHere() fails in the Nether and the End: the head
     * goes (the foot pops through its onNeighborBlockChange, with its bed
     * drop), then the cell one step past the head along the facing is
     * cleared if it is a bed, and newExplosion(null, 5.0F, flaming, smoking)
     * is centred on that cell, as the decompiled method writes it. The
     * overworld's hell-biome half of the test is never true. */
    if (p->dimension != 0)
    {
        world_set_block(w, x, y, z, 0, 0, 3);

        int dir = var10 & 3;
        x += BED_OFFSET[dir][0];
        z += BED_OFFSET[dir][1];

        if (block_at(w, x, y, z) == BLOCK_BED) world_set_block(w, x, y, z, 0, 0, 3);

        if (p->replay != NULL)
            tnt_replay_blast(p->replay, (double)((float)x + 0.5F), (double)((float)y + 0.5F),
                             (double)((float)z + 0.5F), 5.0F, 1, 1);
        return 1;
    }

    if ((var10 & 4) != 0)
    {
        const struct surv_state *sv = &p->sv;

        if (sv->sleeping && sv->has_bed && sv->bed_x == x && sv->bed_y == y && sv->bed_z == z)
        {
            chatmsg_bed(p, CM_BED_OCCUPIED);
            return 1;
        }

        set_occupied(w, x, y, z, 0);
    }

    /* the refusals change no state but their chat lines */
    int status = sleep_in_bed_at(p, x, y, z);
    if (status == SLEEP_OK) set_occupied(w, x, y, z, 1);
    else if (status == SLEEP_NOT_POSSIBLE_NOW) chatmsg_bed(p, CM_BED_NO_SLEEP);
    else if (status == SLEEP_NOT_SAFE) chatmsg_bed(p, CM_BED_NOT_SAFE);

    return 1;
}

void sleep_server_wake(struct server_player *p, int reset_timer, int update_flag, int set_spawn)
{
    struct surv_state *sv = &p->sv;
    struct entity *e = &p->e;

    /* a hit in the world's entity pass (a lightning bolt) wakes the player
     * ahead of the network tick that normally opens the row's packet queue:
     * open it here, the way a head-of-tick dev op does */
    if (!p->dev_prequeued && !p->net_phase)
    {
        s2c_clear();
        p->dev_prequeued = 1;
    }

    if (sv->sleeping)
    {
        struct s2c_pkt *pkt = s2c_add(s2c_out());
        pkt->kind = PK_S0B;
        pkt->i0 = 2;
    }

    /* EntityPlayer.wakeUpPlayer */
    set_size(e, 0.6F, 1.8F, 1, p->sneaking);
    e->y_offset = 0.0F;   /* EntityPlayerMP.resetHeight */

    if (sv->has_bed && block_at(e->world, sv->bed_x, sv->bed_y, sv->bed_z) == BLOCK_BED)
    {
        set_occupied(e->world, sv->bed_x, sv->bed_y, sv->bed_z, 0);

        int rx, rz, ry = sv->bed_y;

        if (!surv_bed_safe_spot(e->world, sv->bed_x, sv->bed_y, sv->bed_z, &rx, &rz))
        {
            rx = sv->bed_x;
            ry = sv->bed_y + 1;
            rz = sv->bed_z;
        }

        entity_set_position(e, (double)((float)rx + 0.5F), (double)((float)ry + e->y_offset + 0.1F),
                            (double)((float)rz + 0.5F));
    }

    sv->sleeping = 0;

    if (update_flag) update_sleeping_flag(p);

    sv->sleep_timer = reset_timer ? 0 : 100;

    if (set_spawn)
    {
        /* setSpawnChunk(playerLocation, false) */
        sv->has_spawn = sv->has_bed;
        sv->spawn_x = sv->bed_x;
        sv->spawn_y = sv->bed_y;
        sv->spawn_z = sv->bed_z;
        sv->spawn_forced = 0;
    }

    /* EntityPlayerMP's tail */
    set_player_location(p);
}

void sleep_server_update(struct server_player *p)
{
    struct surv_state *sv = &p->sv;

    if (sv->sleeping)
    {
        ++sv->sleep_timer;

        if (sv->sleep_timer > 100) sv->sleep_timer = 100;

        if (!sv->has_bed || block_at(p->e.world, sv->bed_x, sv->bed_y, sv->bed_z) != BLOCK_BED)
            sleep_server_wake(p, 1, 1, 0);
        else if (server_is_daytime(p))
            sleep_server_wake(p, 0, 1, 1);
    }
    else if (sv->sleep_timer > 0)
    {
        ++sv->sleep_timer;

        if (sv->sleep_timer >= 110) sv->sleep_timer = 0;
    }
}

int sleep_all_asleep(void *ctx)
{
    const struct serverreplay *sr = ctx;
    const struct server_player *p = sr->player;

    return p != NULL && p->sv.sleeping && p->sv.sleep_timer >= 100;
}

void sleep_wake_all(void *ctx, struct servertick *s)
{
    struct serverreplay *sr = ctx;
    struct server_player *p = sr->player;

    /* the Nether and the End read the overworld's clock (DerivedWorldInfo) */
    for (int i = 0; i < sr->nworlds; ++i)
        if (&sr->w[i].st != s) sr->w[i].st.world_time = s->world_time;

    if (p == NULL || !p->sv.sleeping) return;

    /* the world tick runs ahead of the network tick that normally opens the
     * row's packet queue: open it here, the way a head-of-tick dev op does */
    if (!p->dev_prequeued) s2c_clear();
    p->dev_prequeued = 1;

    sleep_server_wake(p, 0, 0, 1);

    /* the entity pass after this reads the player where it woke */
    serverreplay_player_resync(sr);
}

void sleep_client_use_bed(struct client_player *p, int x, int y, int z)
{
    struct surv_state *sv = &p->sv;
    struct entity *e = &p->e;

    set_size(e, 0.2F, 0.2F, 0, 0);
    e->y_offset = 0.2F;

    double px, py, pz;
    bed_position(e->world, x, y, z, &px, &py, &pz);
    entity_set_position(e, px, py, pz);

    sv->sleeping = 1;
    sv->sleep_timer = 0;
    sv->has_bed = 1;
    sv->bed_x = x;
    sv->bed_y = y;
    sv->bed_z = z;
    e->motion_x = e->motion_z = e->motion_y = 0.0;
}

void sleep_client_wake(struct client_player *p)
{
    struct surv_state *sv = &p->sv;
    struct entity *e = &p->e;

    set_size(e, 0.6F, 1.8F, 0, 0);
    e->y_offset = 1.62F;   /* EntityPlayer.resetHeight */

    if (sv->has_bed && block_at(e->world, sv->bed_x, sv->bed_y, sv->bed_z) == BLOCK_BED)
    {
        set_occupied(e->world, sv->bed_x, sv->bed_y, sv->bed_z, 0);

        int rx, rz, ry = sv->bed_y;

        if (!surv_bed_safe_spot(e->world, sv->bed_x, sv->bed_y, sv->bed_z, &rx, &rz))
        {
            rx = sv->bed_x;
            ry = sv->bed_y + 1;
            rz = sv->bed_z;
        }

        entity_set_position(e, (double)((float)rx + 0.5F), (double)((float)ry + e->y_offset + 0.1F),
                            (double)((float)rz + 0.5F));
    }

    sv->sleeping = 0;
    sv->sleep_timer = 100;
}

void sleep_client_update(struct client_player *p)
{
    struct surv_state *sv = &p->sv;

    if (sv->sleeping)
    {
        ++sv->sleep_timer;

        if (sv->sleep_timer > 100) sv->sleep_timer = 100;
    }
    else if (sv->sleep_timer > 0)
    {
        ++sv->sleep_timer;

        if (sv->sleep_timer >= 110) sv->sleep_timer = 0;
    }
}
