#include "villagers.h"
#include "env.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ai.h"
#include "blocks.h"
#include "det.h"
#include "jmath.h"
#include "chunkscan.h"
#include "living.h"
#include "nbtjson.h"
#include "trades.h"
#include "world.h"

/* A vanilla list that would grow past its fixed capacity here: the
 * arena's rule (pool_take), a loud stop rather than a silent drop. */
static void village_full(int cap, const char *what)
{
    fprintf(stderr, "village: more than %d %s\n", cap, what);
    abort();
}

/* -------------------------------------------------------- VillageDoorInfo */

int vdi_dist_sq(const struct village_door_info *d, int x, int y, int z)
{
    int dx = x - d->pos_x, dy = y - d->pos_y, dz = z - d->pos_z;
    return dx * dx + dy * dy + dz * dz;
}

int vdi_inside_dist_sq(const struct village_door_info *d, int x, int y, int z)
{
    int dx = x - d->pos_x - d->inside_dir_x;
    int dy = y - d->pos_y;
    int dz = z - d->pos_z - d->inside_dir_z;
    return dx * dx + dy * dy + dz * dz;
}

int vdi_inside_x(const struct village_door_info *d)
{
    return d->pos_x + d->inside_dir_x;
}

int vdi_inside_y(const struct village_door_info *d)
{
    return d->pos_y;
}

int vdi_inside_z(const struct village_door_info *d)
{
    return d->pos_z + d->inside_dir_z;
}

int vdi_is_inside(const struct village_door_info *d, int x, int z)
{
    int dx = x - d->pos_x;
    int dz = z - d->pos_z;
    return dx * d->inside_dir_x + dz * d->inside_dir_z >= 0;
}

/* --------------------------------------------------------- Door Block Ops */

int door_calc_inside_dir(struct world *w, int x, int y, int z, int *inside_dir_x, int *inside_dir_z)
{
    int meta = world_get_meta(w, x, y, z);
    int is_top = (meta & 8) != 0;
    int bottom_meta = is_top ? world_get_meta(w, x, y - 1, z) : meta;
    int orientation = bottom_meta & 3;
    int var5 = 0;

    if (orientation != 0 && orientation != 2)
    {
        for (int dz = -5; dz < 0; ++dz)
        {
            if (world_can_block_see_the_sky(w, x, y, z + dz)) --var5;
        }
        for (int dz = 1; dz <= 5; ++dz)
        {
            if (world_can_block_see_the_sky(w, x, y, z + dz)) ++var5;
        }
        if (var5 != 0)
        {
            *inside_dir_x = 0;
            *inside_dir_z = (var5 > 0 ? -2 : 2);
            return 1;
        }
    }
    else
    {
        for (int dx = -5; dx < 0; ++dx)
        {
            if (world_can_block_see_the_sky(w, x + dx, y, z)) --var5;
        }
        for (int dx = 1; dx <= 5; ++dx)
        {
            if (world_can_block_see_the_sky(w, x + dx, y, z)) ++var5;
        }
        if (var5 != 0)
        {
            *inside_dir_x = (var5 > 0 ? -2 : 2);
            *inside_dir_z = 0;
            return 1;
        }
    }

    return 0;
}

void door_set_open(struct world *w, int x, int y, int z, int open,
                   void (*on_write)(int x, int y, int z, int meta, int flags, void *ctx), void *ctx)
{
    int meta = world_get_meta(w, x, y, z);
    int is_top = (meta & 8) != 0;
    int target_y = is_top ? y - 1 : y;
    int bottom_meta = is_top ? world_get_meta(w, x, target_y, z) : meta;
    int is_open = (bottom_meta & 4) != 0;

    if (is_open != (open ? 1 : 0))
    {
        int new_meta = (bottom_meta & 7) ^ 4;
        world_set_meta(w, x, target_y, z, new_meta, 2);
        if (on_write) on_write(x, target_y, z, new_meta, 2, ctx);
        /* func_150014_a's playAuxSFXAtEntity(null, 1003) at the called block */
        env_aux_sfx(w, 1003, x, y, z, 0);
    }
}

/* ---------------------------------------------------------------- Village */

void v_init(struct village *v, struct an_world *w)
{
    memset(v, 0, sizeof *v);
    v->world = w;
}

static void v_update_radius_and_center(struct village *v)
{
    int n = v->num_doors;
    if (n == 0)
    {
        v->center_x = 0;
        v->center_y = 0;
        v->center_z = 0;
        v->village_radius = 0;
        return;
    }

    v->center_x = v->center_helper_x / n;
    v->center_y = v->center_helper_y / n;
    v->center_z = v->center_helper_z / n;

    int max_dist = 0;
    for (int i = 0; i < n; ++i)
    {
        int d = vdi_dist_sq(door_at(v->doors[i]), v->center_x, v->center_y, v->center_z);
        if (d > max_dist) max_dist = d;
    }

    int r = (int)sqrt((double)max_dist) + 1;
    v->village_radius = r > 32 ? r : 32;
}

static void v_push_door(struct village *v, const struct village_door_info *door)
{
    v->doors = slab_table_room(v->doors, v->num_doors, &v->cap_doors, sizeof *v->doors);
    v->doors[v->num_doors++] = door_index(door);
}

static void v_push_agressor(struct village *v, lref agressor, int time, int id)
{
    v->agressors = slab_table_room(v->agressors, v->num_agressors, &v->cap_agressors, sizeof *v->agressors);
    v->agressors[v->num_agressors].agressor = agressor;
    v->agressors[v->num_agressors].agression_time = time;
    v->agressors[v->num_agressors].agressor_id = id;
    ++v->num_agressors;
}

/* the village's own tables back to the slab (it leaves the collection) */
static void v_release(struct village *v)
{
    slab_table_free(v->doors, v->cap_doors, sizeof *v->doors);
    slab_table_free(v->agressors, v->cap_agressors, sizeof *v->agressors);
    v->doors = NULL;
    v->agressors = NULL;
    v->cap_doors = v->cap_agressors = 0;
}

/* a new Village slot at the table's end, zeroed (the table may move: no
 * village address is held across this) */
static int vc_push_slot(struct village_collection *vc)
{
    vc->villages = slab_table_room(vc->villages, vc->num_slots, &vc->cap_slots, sizeof *vc->villages);
    memset(&vc->villages[vc->num_slots], 0, sizeof vc->villages[0]);
    return vc->num_slots++;
}

/* villageList.add: the slot at the list's end */
static void vc_list_push(struct village_collection *vc, int slot)
{
    vc->list = slab_table_room(vc->list, vc->num_villages, &vc->cap_list, sizeof *vc->list);
    vc->list[vc->num_villages++] = slot;
}

void v_add_door(struct village *v, const struct village_door_info *d)
{
    struct village_door_info *door = door_alloc();
    *door = *d;
    v_push_door(v, door);
    v->center_helper_x += d->pos_x;
    v->center_helper_y += d->pos_y;
    v->center_helper_z += d->pos_z;
    v_update_radius_and_center(v);
    v->last_add_door_timestamp = d->last_activity_timestamp;
}

static void v_remove_dead_and_out_of_range_doors(struct village *v)
{
    int changed = 0;
    int reset_restric = (det_rng_int_n(&v->world->iew.world_rand, 50) == 0);

    for (int i = 0; i < v->num_doors; ++i)
    {
        if (reset_restric) door_at(v->doors[i])->door_opening_restriction_counter = 0;

        int blk = world_get_block(v->world->w, door_at(v->doors[i])->pos_x, door_at(v->doors[i])->pos_y, door_at(v->doors[i])->pos_z) & 4095;
        int is_door = (blk == 64);

        if (!is_door || abs(v->tick_counter - door_at(v->doors[i])->last_activity_timestamp) > VILLAGE_DOOR_AGING_THRESHOLD)
        {
            v->center_helper_x -= door_at(v->doors[i])->pos_x;
            v->center_helper_y -= door_at(v->doors[i])->pos_y;
            v->center_helper_z -= door_at(v->doors[i])->pos_z;
            changed = 1;
            door_at(v->doors[i])->is_detached_from_village_flag = 1;
            memmove(&v->doors[i], &v->doors[i + 1], (size_t)(v->num_doors - i - 1) * sizeof v->doors[0]);
            --v->num_doors;
            --i;
        }
    }

    if (changed) v_update_radius_and_center(v);
}

static void v_remove_dead_and_old_agressors(struct village *v)
{
    for (int i = 0; i < v->num_agressors; ++i)
    {
        if (!v->agressors[i].agressor || !living_is_alive(lv_get(v->agressors[i].agressor)) ||
            abs(v->tick_counter - v->agressors[i].agression_time) > 300)
        {
            memmove(&v->agressors[i], &v->agressors[i + 1],
                    (size_t)(v->num_agressors - i - 1) * sizeof(v->agressors[0]));
            --v->num_agressors;
            --i;
        }
    }
}

static void v_update_num_villagers(struct village *v)
{
    struct aabb box;
    box.min_x = (double)(v->center_x - v->village_radius);
    box.max_x = (double)(v->center_x + v->village_radius);
    box.min_y = (double)(v->center_y - 4);
    box.max_y = (double)(v->center_y + 4);
    box.min_z = (double)(v->center_z - v->village_radius);
    box.max_z = (double)(v->center_z + v->village_radius);

    int count = 0;
    for (int i = 0; i < v->world->n; ++i)
    {
        struct an_ent *en = an_ent_at(v->world->slot[i]);
        if (en && en->is_living && en->livh && !lv_get(en->livh)->is_dead && lv_get(en->livh)->added_to_chunk && lv_get(en->livh)->kind == VK_VILLAGER)
        {
            if (aabb_intersects(&lv_get(en->livh)->e.bounding_box, &box)) ++count;
        }
    }
    v->num_villagers = count;
    if (v->num_villagers == 0) v->num_player_rep = 0;
}

static void v_update_num_iron_golems(struct village *v)
{
    struct aabb box;
    box.min_x = (double)(v->center_x - v->village_radius);
    box.max_x = (double)(v->center_x + v->village_radius);
    box.min_y = (double)(v->center_y - 4);
    box.max_y = (double)(v->center_y + 4);
    box.min_z = (double)(v->center_z - v->village_radius);
    box.max_z = (double)(v->center_z + v->village_radius);

    int count = 0;
    for (int i = 0; i < v->world->n; ++i)
    {
        struct an_ent *en = an_ent_at(v->world->slot[i]);
        if (en && en->is_living && en->livh && !lv_get(en->livh)->is_dead && lv_get(en->livh)->added_to_chunk && lv_get(en->livh)->kind == VK_IRON_GOLEM)
        {
            if (aabb_intersects(&lv_get(en->livh)->e.bounding_box, &box)) ++count;
        }
    }
    v->num_iron_golems = count;
}

static int class_is(int id, const char *name)
{
    const char *c = BLOCKS[id & 4095].class_name;
    return c != NULL && strcmp(c, name) == 0;
}

static int is_solid_top_surface(struct world *w, int x, int y, int z)
{
    int id = world_get_block(w, x, y, z) & 4095;
    int meta = world_get_meta(w, x, y, z);

    if (MATERIALS[BLOCKS[id].material].is_opaque && BLOCKS[id].normal_block) return 1;
    if (class_is(id, "BlockStairs")) return (meta & 4) == 4;
    if (class_is(id, "BlockSlab")) return (meta & 8) == 8;
    if (class_is(id, "BlockHopper")) return 1;
    if (class_is(id, "BlockSnow")) return (meta & 7) == 7;

    return 0;
}

static int is_normal_cube(struct world *w, int x, int y, int z)
{
    int id = world_get_block(w, x, y, z) & 4095;
    if (id == 0) return 0;
    return MATERIALS[BLOCKS[id].material].is_opaque && BLOCKS[id].normal_block;
}

static int is_valid_iron_golem_spawning_location(struct world *w, int x, int y, int z, int sx, int sy, int sz)
{
    if (!is_solid_top_surface(w, x, y - 1, z)) return 0;
    int x0 = x - sx / 2;
    int z0 = z - sz / 2;
    for (int vx = x0; vx < x0 + sx; ++vx)
    {
        for (int vy = y; vy < y + sy; ++vy)
        {
            for (int vz = z0; vz < z0 + sz; ++vz)
            {
                if (is_normal_cube(w, vx, vy, vz)) return 0;
            }
        }
    }
    return 1;
}

void v_tick(struct village *v, int tick_counter)
{
    v->tick_counter = tick_counter;
    v_remove_dead_and_out_of_range_doors(v);
    v_remove_dead_and_old_agressors(v);

    if (tick_counter % 20 == 0) v_update_num_villagers(v);
    if (tick_counter % 30 == 0) v_update_num_iron_golems(v);

    int var2 = v->num_villagers / 10;
    if (v->num_iron_golems < var2 && v->num_doors > 20 &&
        det_rng_int_n(&v->world->iew.world_rand, VILLAGE_GOLEM_SPAWN_CHANCE) == 0)
    {
        for (int var7 = 0; var7 < 10; ++var7)
        {
            int var8 = v->center_x + det_rng_int_n(&v->world->iew.world_rand, 16) - 8;
            int var9 = v->center_y + det_rng_int_n(&v->world->iew.world_rand, 6) - 3;
            int var10 = v->center_z + det_rng_int_n(&v->world->iew.world_rand, 16) - 8;

            if (v_is_in_range(v, var8, var9, var10) &&
                is_valid_iron_golem_spawning_location(v->world->w, var8, var9, var10, 2, 4, 2))
            {
                /* new EntityIronGolem(world) (the constructor's Det draws
                 * through the pass's constructor hook), setPosition,
                 * spawnEntityInWorld (the replay orders it after vc_tick) */
                struct an_world *an = v->world;
                if (an->constructor_hook) an->constructor_hook(an, 1, an->constructor_ctx);
                struct living *golem = living_alloc();
                living_init(golem, an->w, VK_IRON_GOLEM, an->det);
                golem->an = an;
                golem->dimension = an->dimension;
                iron_golem_construct(golem, an->det);
                if (an->constructor_hook) an->constructor_hook(an, 0, an->constructor_ctx);
                entity_set_position(&golem->e, (double)var8, (double)var9, (double)var10);
                an_add_living(an, golem, an->n);
                ++v->num_iron_golems;
                break;
            }
        }
    }
}

struct village_door_info *v_find_nearest_door(struct village *v, int x, int y, int z)
{
    struct village_door_info *best = NULL;
    int best_dist = 0x7fffffff;
    for (int i = 0; i < v->num_doors; ++i)
    {
        int d = vdi_dist_sq(door_at(v->doors[i]), x, y, z);
        if (d < best_dist)
        {
            best_dist = d;
            best = door_at(v->doors[i]);
        }
    }
    return best;
}

struct village_door_info *v_find_nearest_door_unrestricted(struct village *v, int x, int y, int z)
{
    struct village_door_info *best = NULL;
    int best_val = 0x7fffffff;
    for (int i = 0; i < v->num_doors; ++i)
    {
        int d = vdi_dist_sq(door_at(v->doors[i]), x, y, z);
        int val = (d > 256) ? d * 1000 : door_at(v->doors[i])->door_opening_restriction_counter;
        if (val < best_val)
        {
            best_val = val;
            best = door_at(v->doors[i]);
        }
    }
    return best;
}

struct village_door_info *v_get_door_at(struct village *v, int x, int y, int z)
{
    /* the centre distance greater than the radius squared: unlike isInRange,
     * a door exactly on the rim is still found */
    int dx = v->center_x - x, dy = v->center_y - y, dz = v->center_z - z;
    if ((float)(dx * dx + dy * dy + dz * dz) > (float)(v->village_radius * v->village_radius)) return NULL;
    for (int i = 0; i < v->num_doors; ++i)
    {
        if (door_at(v->doors[i])->pos_x == x && door_at(v->doors[i])->pos_z == z && abs(door_at(v->doors[i])->pos_y - y) <= 1)
            return door_at(v->doors[i]);
    }
    return NULL;
}

int v_is_in_range(const struct village *v, int x, int y, int z)
{
    int dx = x - v->center_x;
    int dy = y - v->center_y;
    int dz = z - v->center_z;
    return (float)(dx * dx + dy * dy + dz * dz) < (float)(v->village_radius * v->village_radius);
}

int v_get_reputation(struct village *v, const char *player_name)
{
    for (int i = 0; i < v->num_player_rep; ++i)
    {
        if (strcmp(v->player_rep[i].name, player_name) == 0)
            return v->player_rep[i].rep;
    }
    return 0;
}

int v_set_reputation(struct village *v, const char *player_name, int delta)
{
    for (int i = 0; i < v->num_player_rep; ++i)
    {
        if (strcmp(v->player_rep[i].name, player_name) == 0)
        {
            int r = v->player_rep[i].rep + delta;
            if (r < -30) r = -30;
            if (r > 10) r = 10;
            v->player_rep[i].rep = r;
            return r;
        }
    }
    /* playerReputation is a TreeMap: a new name takes its place in
     * String.compareTo order (the names are ASCII, so strcmp's), which is the
     * order setDefaultPlayerReputation and writeVillageDataToNBT walk */
    if (v->num_player_rep >= VILLAGE_MAX_PLAYERS) village_full(VILLAGE_MAX_PLAYERS, "player reputations in one village");
    int r = delta;
    if (r < -30) r = -30;
    if (r > 10) r = 10;
    int at = v->num_player_rep;
    while (at > 0 && strcmp(v->player_rep[at - 1].name, player_name) > 0)
    {
        v->player_rep[at] = v->player_rep[at - 1];
        --at;
    }
    memset(&v->player_rep[at], 0, sizeof v->player_rep[at]);
    strncpy(v->player_rep[at].name, player_name, sizeof v->player_rep[at].name - 1);
    v->player_rep[at].rep = r;
    ++v->num_player_rep;
    return r;
}

void v_set_default_player_reputation(struct village *v, int rep)
{
    for (int i = 0; i < v->num_player_rep; ++i)
    {
        v_set_reputation(v, v->player_rep[i].name, rep);
    }
}

/* Village.addOrRenewAgressor. */
void v_add_or_renew_agressor(struct village *v, struct living *agressor)
{
    for (int i = 0; i < v->num_agressors; ++i)
    {
        if (v->agressors[i].agressor == lv_ref(agressor))
        {
            v->agressors[i].agression_time = v->tick_counter;
            return;
        }
    }
    v_push_agressor(v, lv_ref(agressor), v->tick_counter, agressor->entity_id);
}

/* Village.findNearestVillageAggressor: the last of the nearest (<=). */
struct living *v_find_nearest_aggressor(struct village *v, struct living *l)
{
    double best = 1.7976931348623157E308;
    struct living *found = NULL;
    for (int i = 0; i < v->num_agressors; ++i)
    {
        struct living *a = lv_get(v->agressors[i].agressor);
        double dx = a->e.pos_x - l->e.pos_x, dy = a->e.pos_y - l->e.pos_y, dz = a->e.pos_z - l->e.pos_z;
        double d = dx * dx + dy * dy + dz * dz;
        if (d <= best)
        {
            found = a;
            best = d;
        }
    }
    return found;
}

/* Village.func_82685_c: the nearest player whose reputation is too low
 * (isPlayerReputationTooLow, <= -15). The world holds one player, the
 * an_world's living when it has one. */
struct living *v_find_low_reputation_player(struct village *v, struct living *l)
{
    (void)l;
    if (!v->world || !v->world->playerh) return NULL;
    /* each name with isPlayerReputationTooLow, in the TreeMap's order, found
     * by World.getPlayerEntityByName: the one player is VILLAGE_PLAYER_NAME,
     * and only when it is in this world (playerh); the distance test picks
     * among players, of which there is this one */
    for (int i = 0; i < v->num_player_rep; ++i)
    {
        if (v->player_rep[i].rep <= -15 && !strcmp(v->player_rep[i].name, VILLAGE_PLAYER_NAME))
            return lv_get(v->world->playerh);
    }
    return NULL;
}

int v_is_mating_season(const struct village *v)
{
    return v->no_breed_ticks == 0 || v->tick_counter - v->no_breed_ticks >= 3600;
}

void v_end_mating_season(struct village *v)
{
    v->no_breed_ticks = v->tick_counter;
}

int v_is_annihilated(const struct village *v)
{
    return v->num_doors == 0;
}

/* ------------------------------------------------------ VillageCollection */

static int vc_int(const nbt *comp, const char *key)
{
    return (int)nbt_int_value(nbt_get(comp, key));
}

int vc_load(struct village_collection *vc, const nbt *root)
{
    vc->tick_counter = vc_int(root, "Tick");
    int n = 0;
    const int *pos = nbt_int_array(nbt_get(root, "pos"), &n);
    vc->num_positions = 0;
    for (int i = 0; pos && i + 2 < n && vc->num_positions < VILLAGE_MAX_POSITIONS; i += 3)
        vc->positions[vc->num_positions++] = (struct chunk_coords){pos[i], pos[i + 1], pos[i + 2]};
    const nbt *list = nbt_get(root, "Villages");
    int nv = list ? nbt_list_size(list) : 0;
    for (int i = 0; i < vc->num_slots; ++i) v_release(&vc->villages[i]);
    vc->num_slots = 0;
    vc->num_villages = 0;
    for (int i = 0; i < nv; ++i)
    {
        const nbt *t = nbt_list_get(list, i);
        int slot = vc_push_slot(vc);
        vc_list_push(vc, slot);
        struct village *v = &vc->villages[slot];
        v_init(v, vc->world);
        /* Village.readVillageDataFromNBT */
        v->num_villagers = vc_int(t, "PopSize");
        v->village_radius = vc_int(t, "Radius");
        v->num_iron_golems = vc_int(t, "Golems");
        v->last_add_door_timestamp = vc_int(t, "Stable");
        v->tick_counter = vc_int(t, "Tick");
        v->no_breed_ticks = vc_int(t, "MTick");
        v->center_x = vc_int(t, "CX");
        v->center_y = vc_int(t, "CY");
        v->center_z = vc_int(t, "CZ");
        v->center_helper_x = vc_int(t, "ACX");
        v->center_helper_y = vc_int(t, "ACY");
        v->center_helper_z = vc_int(t, "ACZ");
        const nbt *doors = nbt_get(t, "Doors");
        int nrc = 0;
        const int *rc = nbt_int_array(nbt_get(t, "DoorsRC"), &nrc);
        int nd = doors ? nbt_list_size(doors) : 0;
        for (int k = 0; k < nd; ++k)
        {
            const nbt *d = nbt_list_get(doors, k);
            struct village_door_info *door = door_alloc();
            door->pos_x = vc_int(d, "X");
            door->pos_y = vc_int(d, "Y");
            door->pos_z = vc_int(d, "Z");
            door->inside_dir_x = vc_int(d, "IDX");
            door->inside_dir_z = vc_int(d, "IDZ");
            door->last_activity_timestamp = vc_int(d, "TS");
            door->door_opening_restriction_counter = rc && k < nrc ? rc[k] : 0;
            v_push_door(v, door);
        }
        const nbt *players = nbt_get(t, "Players");
        int np = players ? nbt_list_size(players) : 0;
        if (np > VILLAGE_MAX_PLAYERS) return 0;
        for (int k = 0; k < np; ++k)
        {
            const nbt *p = nbt_list_get(players, k);
            const char *name = nbt_string_value(nbt_get(p, "Name"));
            snprintf(v->player_rep[v->num_player_rep].name, sizeof v->player_rep[0].name, "%s", name ? name : "");
            v->player_rep[v->num_player_rep].rep = vc_int(p, "S");
            ++v->num_player_rep;
        }
        int na = 0;
        const int *agg = nbt_int_array(nbt_get(t, "Agg"), &na);
        for (int k = 0; agg && k + 1 < na; k += 2)
        {
            v_push_agressor(v, 0, agg[k + 1], agg[k]);
        }
    }
    return 1;
}

void vc_resolve_agressors(struct village_collection *vc, struct living *(*by_id)(void *ctx, int id), void *ctx)
{
    for (int i = 0; i < vc->num_villages; ++i)
        for (int k = 0; k < vc_list_at(vc, i)->num_agressors; ++k)
        {
            struct village_agressor *a = &vc_list_at(vc, i)->agressors[k];
            if (a->agressor == 0) a->agressor = lv_ref(by_id(ctx, a->agressor_id));
        }
}


void vc_init(struct village_collection *vc, struct an_world *w)
{
    memset(vc, 0, sizeof *vc);
    vc->world = w;
    vc->tick_counter = 2;
}

void vc_add_villager_position(struct village_collection *vc, int x, int y, int z)
{
    if (vc->num_positions <= 64)
    {
        for (int i = 0; i < vc->num_positions; ++i)
        {
            if (vc->positions[i].x == x && vc->positions[i].y == y && vc->positions[i].z == z)
                return;
        }
        if (vc->num_positions < VILLAGE_MAX_POSITIONS)
        {
            vc->positions[vc->num_positions++] = (struct chunk_coords){x, y, z};
        }
    }
}

struct village_door_info *vc_get_door_at(struct village_collection *vc, int x, int y, int z)
{
    for (int i = 0; i < vc->num_new_doors; ++i)
    {
        if (vc->new_doors[i].pos_x == x && vc->new_doors[i].pos_z == z &&
            abs(vc->new_doors[i].pos_y - y) <= 1)
            return &vc->new_doors[i];
    }
    for (int vi = 0; vi < vc->num_villages; ++vi)
    {
        struct village_door_info *d = v_get_door_at(vc_list_at(vc, vi), x, y, z);
        if (d != NULL) return d;
    }
    return NULL;
}

struct village *vc_find_nearest_village(struct village_collection *vc, int x, int y, int z, int extra_dist)
{
    if (!vc) return NULL;
    struct village *best = NULL;
    float best_dist = 1e30f;

    for (int i = 0; i < vc->num_villages; ++i)
    {
        struct village *v = vc_list_at(vc, i);
        int dx = v->center_x - x;
        int dy = v->center_y - y;
        int dz = v->center_z - z;
        float dist_sq = (float)(dx * dx + dy * dy + dz * dz);
        if (dist_sq < best_dist)
        {
            float max_d = (float)(extra_dist + v->village_radius);
            if (dist_sq <= max_d * max_d)
            {
                best = v;
                best_dist = dist_sq;
            }
        }
    }
    return best;
}

/* Whether anything in the collection's world still names slot s: a
 * living's village (EntityVillager.villageObj, EntityIronGolem's), an
 * EntityAIVillagerMate's villageObj, the siege's theVillage. */
static int vc_slot_named(struct village_collection *vc, int s)
{
    struct an_world *an = vc->world;
    int32_t ref = s + 1;

    if (an == NULL) return 0;
    if (an->village_siege != NULL && an->village_siege->the_village == ref) return 1;
    for (int k = 0; k < an->n; ++k)
    {
        struct an_ent *e = an_ent_at(an->slot[k]);
        if (e == NULL || !e->is_living) continue;
        struct living *l = lv_get(e->livh);
        if (l == NULL) continue;
        if (l->village == ref) return 1;
        struct living_ai *ai = lv_ai_peek(l);
        for (int t = 0; ai != NULL && t < ai->tasks.n; ++t)
            if (ai->tasks.entries[t].t.village_ptr == ref) return 1;
    }
    return 0;
}

/* The slot a new Village object takes: the first that is not listed and
 * that nothing names. */
static int vc_free_slot(struct village_collection *vc)
{
    for (int s = 0; s < vc->num_slots; ++s)
    {
        int listed = 0;
        for (int i = 0; i < vc->num_villages && !listed; ++i) listed = vc->list[i] == s;
        if (listed || vc_slot_named(vc, s)) continue;
        /* the old object is gone: its tables back to the slab */
        v_release(&vc->villages[s]);
        return s;
    }
    return vc_push_slot(vc);
}

void vc_tick(struct village_collection *vc)
{
    ++vc->tick_counter;
    for (int i = 0; i < vc->num_villages; ++i)
    {
        v_tick(vc_list_at(vc, i), vc->tick_counter);
    }

    /* removeAnnihilatedVillages: the iterator's remove; the object (its
     * slot) stays as it was for whoever still names it */
    for (int i = 0; i < vc->num_villages; ++i)
    {
        if (v_is_annihilated(vc_list_at(vc, i)))
        {
            memmove(&vc->list[i], &vc->list[i + 1], (size_t)(vc->num_villages - i - 1) * sizeof vc->list[0]);
            --vc->num_villages;
            --i;
        }
    }

    if (vc->num_positions > 0)
    {
        struct chunk_coords p = vc->positions[0];
        memmove(&vc->positions[0], &vc->positions[1],
                (size_t)(vc->num_positions - 1) * sizeof(vc->positions[0]));
        --vc->num_positions;

        /* the 32 x 32 columns: 3 x 3 chunks at most, each looked up once
         * while loaded (chunkscan.h) */
        struct chunkscan cs;
        chunkscan_init(&cs, vc->world->w, (p.x - 16) >> 4, (p.z - 16) >> 4);
        for (int x = p.x - 16; x < p.x + 16; ++x)
        {
            for (int y = p.y - 4; y < p.y + 4; ++y)
            {
                for (int z = p.z - 16; z < p.z + 16; ++z)
                {
                    int blk = chunkscan_block(&cs, x, y, z) & 4095;
                    if (blk == 64)
                    {
                        struct village_door_info *existing = vc_get_door_at(vc, x, y, z);
                        if (existing == NULL)
                        {
                            int idx = 0, idz = 0;
                            if (door_calc_inside_dir(vc->world->w, x, y, z, &idx, &idz))
                            {
                                {
                                    vc->new_doors = slab_table_room(vc->new_doors, vc->num_new_doors, &vc->cap_new_doors,
                                                                    sizeof *vc->new_doors);
                                    struct village_door_info *nd = &vc->new_doors[vc->num_new_doors++];
                                    nd->pos_x = x;
                                    nd->pos_y = y;
                                    nd->pos_z = z;
                                    nd->inside_dir_x = idx;
                                    nd->inside_dir_z = idz;
                                    nd->last_activity_timestamp = vc->tick_counter;
                                    nd->is_detached_from_village_flag = 0;
                                    nd->door_opening_restriction_counter = 0;
                                }
                            }
                        }
                        else
                        {
                            existing->last_activity_timestamp = vc->tick_counter;
                        }
                    }
                }
            }
        }
    }

    for (int i = 0; i < vc->num_new_doors; ++i)
    {
        struct village_door_info *door = &vc->new_doors[i];
        int added = 0;
        for (int vi = 0; vi < vc->num_villages; ++vi)
        {
            struct village *v = vc_list_at(vc, vi);
            int dx = v->center_x - door->pos_x;
            int dy = v->center_y - door->pos_y;
            int dz = v->center_z - door->pos_z;
            int dsq = dx * dx + dy * dy + dz * dz;
            int max_d = 32 + v->village_radius;
            if (dsq <= max_d * max_d)
            {
                v_add_door(v, door);
                added = 1;
                break;
            }
        }
        if (!added)
        {
            int slot = vc_free_slot(vc);
            vc_list_push(vc, slot);
            struct village *new_v = &vc->villages[slot];
            v_init(new_v, vc->world);
            v_add_door(new_v, door);
        }
    }
    vc->num_new_doors = 0;
}

/* ----------------------------------------------------------- VillageSiege */

void vs_init(struct village_siege *vs, struct an_world *w)
{
    memset(vs, 0, sizeof *vs);
    vs->world = w;
    vs->field_75536_c = -1;
}

static float calc_celestial_angle(int64_t world_time)
{
    int day = (int)(world_time % 24000L);
    float a = ((float)day + 0.0f) / 24000.0f - 0.25f;
    if (a < 0.0f) ++a;
    if (a > 1.0f) --a;
    float b = a;
    a = 1.0f - (float)((cos((double)a * 3.141592653589793) + 1.0) / 2.0);
    return b + (a - b) / 3.0f;
}

void vs_tick(struct village_siege *vs)
{
    if (vs->world->skylight < 4)
    {
        vs->field_75536_c = 0;
        return;
    }
    if (vs->field_75536_c == 2) return;

    if (vs->field_75536_c == 0)
    {
        float ang = calc_celestial_angle(vs->world->world_time);
        if ((double)ang < 0.5 || (double)ang > 0.501) return;

        vs->field_75536_c = (det_rng_int_n(&vs->world->iew.world_rand, 10) == 0) ? 1 : 2;
        vs->field_75535_b = 0;
        if (vs->field_75536_c == 2) return;
    }

    if (!vs->field_75535_b)
    {
        if (!vs->world->has_player ||
            !vs_muster(vs, vs->world->village_collection, &vs->world->iew.world_rand.r,
                       vs->world->player_x, vs->world->player_y, vs->world->player_z))
            return;
        vs->field_75535_b = 1;
    }
}

int vs_muster(struct village_siege *vs, struct village_collection *vc, jrand *rand,
              double px, double py, double pz)
{
    /* the player's position truncated toward zero, (int)posX */
    struct village *v = vc_find_nearest_village(vc, (int)px, (int)py, (int)pz, 1);
    vs->the_village = vc_village_ref(vc, v);
    if (v == NULL || v->num_doors < 10 || v->tick_counter - v->last_add_door_timestamp < 20 || v->num_villagers < 20)
        return 0;

    float rad = (float)v->village_radius;
    int in_other = 0;
    for (int attempt = 0; attempt < 10; ++attempt)
    {
        vs->field_75532_g = v->center_x + (int)((double)(mh_cos(jr_float(rand) * 3.1415927F * 2.0F) * rad) * 0.9);
        vs->field_75538_h = v->center_y;
        vs->field_75539_i = v->center_z + (int)((double)(mh_sin(jr_float(rand) * 3.1415927F * 2.0F) * rad) * 0.9);
        in_other = 0;
        for (int vi = 0; vi < vc->num_villages; ++vi)
        {
            if (vc_list_at(vc, vi) != v && v_is_in_range(vc_list_at(vc, vi), vs->field_75532_g, vs->field_75538_h, vs->field_75539_i))
            {
                in_other = 1;
                break;
            }
        }
        if (!in_other) break;
    }
    if (in_other) return 0;

    /* func_75527_a: ten tries around the point, whose Vec3 is built and
     * dropped, so it returns null (the spawn test reads blocks, no draws) */
    for (int k = 0; k < 10; ++k)
    {
        (void)jr_int_n(rand, 16);
        (void)jr_int_n(rand, 6);
        (void)jr_int_n(rand, 16);
    }
    return 0;
}

/* ---------------------------------------------------- Entity Constructors */

void villager_construct(struct living *l, det_state *det)
{
    (void)det;
    living_set_base_size(l, 0.6F, 1.8F);
    l->nav.can_pass_closed_doors = 1;
    l->nav.avoids_water = 1;
    l->maximum_home_distance = -1.0F;
    ai_setup_kind(l, det);
}

void villager_on_living_update(struct living *l, det_state *det)
{
    living_default_on_living_update(l, det);

    int v1 = l->growing_age;
    if (v1 < 0)
    {
        ++v1;
        living_set_growing_age(l, v1);
    }
    else if (v1 > 0)
    {
        --v1;
        living_set_growing_age(l, v1);
    }
}

void villager_on_spawn_with_egg(struct living *l, det_state *det)
{
    (void)det;
    l->profession = det_rng_int_n(&l->an->iew.world_rand, 5);
}

static int is_player_kind(const struct living *l)
{
    return l->kind == HK_PLAYER || l->kind == SK_PLAYER;
}

void villager_revenge_village(struct living *l, struct living *target)
{
    if (lv_village(l) == NULL || target == NULL) return;
    v_add_or_renew_agressor(lv_village(l), target);
    /* setEntityState(this, 13): the angry particles are the client's */
    if (is_player_kind(target))
        v_set_reputation(lv_village(l), VILLAGE_PLAYER_NAME, l->growing_age < 0 ? -3 : -1);
}

/* IMob: every hostile kind, the slimes, the ghast and the blaze */
static int is_imob(int kind)
{
    return (kind >= HK_ZOMBIE && kind < HK_KINDS && kind != HK_PLAYER && kind != AK_SQUID && kind != AK_BAT) ||
           kind == SK_SLIME || kind == SK_MAGMA_CUBE;
}

void villager_death_village(struct living *l, struct living *attacker)
{
    if (lv_village(l) == NULL) return;
    if (attacker != NULL)
    {
        if (is_player_kind(attacker)) v_set_reputation(lv_village(l), VILLAGE_PLAYER_NAME, -2);
        else if (is_imob(attacker->kind)) v_end_mating_season(lv_village(l));
        return;
    }
    /* World.getClosestPlayerToEntity(this, 16.0D) */
    if (l->an != NULL && l->an->has_player && !l->an->no_players)
    {
        double dx = l->an->player_x - l->e.pos_x, dy = l->an->player_y - l->e.pos_y, dz = l->an->player_z - l->e.pos_z;
        if (dx * dx + dy * dy + dz * dz < 256.0) v_end_mating_season(lv_village(l));
    }
}

void iron_golem_death_village(struct living *l)
{
    if (!l->is_player_created && lv_get(l->attacking_player) != NULL && lv_village(l) != NULL)
        v_set_reputation(lv_village(l), VILLAGE_PLAYER_NAME, -5);
}

void villager_update_ai_tick(struct living *l, det_state *det)
{
    (void)det;
    if (--l->random_tick_divider <= 0)
    {
        /* the replay's village collection never exists (no villages.dat in a
         * whole-server snapshot): Java's addVillagerPosition on a real one is
         * pure black-hole list work, invisible to the rows */
        if (l->an->village_collection)
            vc_add_villager_position(l->an->village_collection,
                                     mh_floor(l->e.pos_x),
                                     mh_floor(l->e.pos_y),
                                     mh_floor(l->e.pos_z));
        l->random_tick_divider = 70 + det_rng_int_n(&l->rand, 50);
        l->village = vc_village_ref(l->an->village_collection, vc_find_nearest_village(l->an->village_collection,
                                            mh_floor(l->e.pos_x),
                                            mh_floor(l->e.pos_y),
                                            mh_floor(l->e.pos_z), 32));

        if (!lv_village(l))
        {
            l->maximum_home_distance = -1.0F;
        }
        else
        {
            l->home_x = lv_village(l)->center_x;
            l->home_y = lv_village(l)->center_y;
            l->home_z = lv_village(l)->center_z;
            l->maximum_home_distance = (int)((float)lv_village(l)->village_radius * 0.6F);

            if (l->is_looking_for_home)
            {
                l->is_looking_for_home = 0;
                v_set_default_player_reputation(lv_village(l), 5);
            }
        }
    }

    /* !isTrading(): the countdown holds while a customer has the window */
    if (!l->buying_player && l->time_until_reset > 0)
    {
        --l->time_until_reset;

        if (l->time_until_reset <= 0)
        {
            if (l->needs_initialization)
            {
                if (lv_villager(l)->recipes.n > 1)
                {
                    trades_unlock_boost(&l->rand.r, &lv_villager(l)->recipes);
                }

                trades_add_default(&l->rand.r, l->an->shuf_rand, l->profession, 1, &lv_villager(l)->recipes);
                l->needs_initialization = 0;

                /* setEntityState(this, 14) is the client's hearts */
                if (lv_village(l) != NULL && l->has_last_buying_player)
                    v_set_reputation(lv_village(l), lv_villager(l)->last_buying_player, 1);
            }
                        struct potion_effect eff = {
                .id = POT_REGENERATION,
                .amplifier = 0,
                .duration = 200,
                .is_splash = 0,
                .is_ambient = 0,
            };
            living_add_potion_effect(l, &eff, det);
        }
    }
}

/* EntityVillager.getRecipes: the first trade builds the default offers. */
void villager_get_recipes(struct living *l, jrand *shuf_rand)
{
    if (!l->has_recipes)
    {
        trades_add_default(&l->rand.r, shuf_rand, l->profession, 1, &lv_villager(l)->recipes);
        l->has_recipes = 1;
    }
}

/* EntityVillager.useRecipe(MerchantRecipe): the offer's uses tick up, the yes
 * sound plays through getSoundPitch's two draws, and a trade of the LAST
 * recipe in the list starts the 40-tick unlock cycle. getTalkInterval is 80
 * for the villager. */
void villager_player_trade(struct living *l, int recipe_index)
{
    if (recipe_index < 0 || recipe_index >= lv_villager(l)->recipes.n) return;

    struct trade_recipe *rec = &lv_villager(l)->recipes.r[recipe_index];
    ++rec->uses;
    l->living_sound_time = -80;
    (void)det_rng_float(&l->rand);
    (void)det_rng_float(&l->rand);

    if (trades_same_ids(rec, &lv_villager(l)->recipes.r[lv_villager(l)->recipes.n - 1]))
    {
        l->time_until_reset = 40;
        l->needs_initialization = 1;

        /* lastBuyingPlayer = buyingPlayer != null ? name : null */
        if (l->buying_player)
        {
            snprintf(lv_villager(l)->last_buying_player, sizeof lv_villager(l)->last_buying_player, "%s",
                     lv_villager(l)->buying_player_name);
            l->has_last_buying_player = 1;
        }
        else
        {
            lv_villager(l)->last_buying_player[0] = 0;
            l->has_last_buying_player = 0;
        }
    }

    if (rec->buy.item == 388) l->wealth += rec->buy.count;
}

/* EntityVillager.func_110297_a_(ItemStack): the sell slot's yes/no sound,
 * gated on livingSoundTime > -80 + 20. */
void villager_func_110297_a_(struct living *l, int has_stack)
{
    /* !worldObj.isClient: living_is_client_world is EntityLivingBase's
     * misnamed isClientWorld, true on the server */
    if (living_is_client_world(l) && l->living_sound_time > -80 + 20)
    {
        l->living_sound_time = -80;
        (void)det_rng_float(&l->rand);
        (void)det_rng_float(&l->rand);
        /* the sound itself is client-only; the two pitch draws above are all
         * the server does */
        (void)has_stack;
    }
}

/* EntityVillager.createChild: new EntityVillager(world) (its constructor's
 * Det draws through the pass's constructor hook) and onSpawnWithEgg (the
 * follow-range bonus, then setProfession(World.rand.nextInt(5))). The caller
 * (EntityAIVillagerMate.giveBirth) sets the ages and the place; the list and
 * chunk entry are spawnEntityInWorld's, at the parent's place. */
struct living *villager_create_child(struct living *l, struct living *mate, det_state *det)
{
    (void)mate;
    (void)det;
    struct an_world *an = l->an;
    if (an->constructor_hook) an->constructor_hook(an, 1, an->constructor_ctx);
    struct living *child = an_spawn_living(an, VK_VILLAGER, an->n, l->e.pos_x, l->e.pos_y, l->e.pos_z,
                                           0.0F, 0.0F, 0, 0, 0, 0, 0, 1);
    if (an->constructor_hook) an->constructor_hook(an, 0, an->constructor_ctx);
    return child;
}

void iron_golem_construct(struct living *l, det_state *det)
{
    (void)det;
    living_set_base_size(l, 1.4F, 2.9F);
    l->nav.avoids_water = 1;
    l->maximum_home_distance = -1.0F;
    ai_setup_kind(l, det);
}

void iron_golem_on_living_update(struct living *l, det_state *det)
{
    living_default_on_living_update(l, det);
    if (l->attack_timer > 0) --l->attack_timer;
    if (l->hold_rose_tick > 0) --l->hold_rose_tick;

    if (l->e.motion_x * l->e.motion_x + l->e.motion_z * l->e.motion_z > 2.500000277905201E-7 &&
        det_rng_int_n(&l->rand, 5) == 0)
    {
        int var1 = mh_floor(l->e.pos_x);
        int var2 = mh_floor(l->e.pos_y - 0.20000000298023224 - (double)l->e.y_offset);
        int var3 = mh_floor(l->e.pos_z);
        int blk = world_get_block(l->world, var1, var2, var3) & 4095;

        if (blk != 0)
        {
            float r1 = det_rng_float(&l->rand);
            float r2 = det_rng_float(&l->rand);
            float r3 = det_rng_float(&l->rand);
            float r4 = det_rng_float(&l->rand);
            (void)r1; (void)r2; (void)r3; (void)r4;
        }
    }
}

void iron_golem_update_ai_tick(struct living *l, det_state *det)
{
    (void)det;
    if (--l->home_check_timer <= 0)
    {
        l->home_check_timer = 70 + det_rng_int_n(&l->rand, 50);
        l->village = vc_village_ref(l->an->village_collection, vc_find_nearest_village(l->an->village_collection,
                                            mh_floor(l->e.pos_x),
                                            mh_floor(l->e.pos_y),
                                            mh_floor(l->e.pos_z), 32));

        if (!lv_village(l))
        {
            l->maximum_home_distance = -1.0F;
        }
        else
        {
            l->home_x = lv_village(l)->center_x;
            l->home_y = lv_village(l)->center_y;
            l->home_z = lv_village(l)->center_z;
            l->maximum_home_distance = (int)((float)lv_village(l)->village_radius * 0.6F);
        }
    }
}

/* EntityIronGolem.attackEntityAsMob: the 10-tick arm swing, 7 + nextInt(15)
 * as mob damage and the 0.4 lift on a hit (the throw sound draws nothing). */
int iron_golem_attack_entity_as_mob(struct living *l, struct living *target, det_state *det)
{
    l->attack_timer = 10;
    float damage = (float)(7 + det_rng_int_n(&l->rand, 15));
    int hit = living_attack_entity_from_attacker(target, l, DMG_MOB, damage, det);
    if (hit) target->e.motion_y += 0.4000000059604645;
    return hit;
}

void iron_golem_set_holding_rose(struct living *l, int holding)
{
    l->hold_rose_tick = holding ? 400 : 0;
}

struct living *vil_spawn_living(struct an_world *an, int kind, int spawn_index, double x, double y, double z,
                                float yaw, float pitch, int growing_age, int profession, int wealth,
                                int player_created, int with_egg)
{
    struct living *l = living_alloc();
    living_init(l, an->w, kind, an->det);
    l->an = an;
    if (kind == VK_VILLAGER) villager_construct(l, an->det);
    else if (kind == VK_IRON_GOLEM) iron_golem_construct(l, an->det);
    living_set_location_and_angles(l, x, y, z, yaw, pitch);

    if (growing_age != 0) living_set_growing_age(l, growing_age);
    if (kind == VK_VILLAGER)
    {
        l->profession = profession;
        l->wealth = wealth;
    }
    else if (kind == VK_IRON_GOLEM)
    {
        l->is_player_created = player_created;
    }

    if (with_egg) living_on_spawn_with_egg(l, an->det);

    struct an_ent *en = an_ent_alloc();
    en->used = 1;
    en->is_living = 1;
    en->spawn_index = spawn_index;
    en->livh = lv_ref(l);
    en->ieh = 0;
    an_list_push(an, en);
    an_chunk_add(an, en, mh_floor(l->e.pos_x / 16.0), mh_floor(l->e.pos_y / 16.0),
                 mh_floor(l->e.pos_z / 16.0));
    return l;
}
