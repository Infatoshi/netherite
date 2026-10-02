#include <stdio.h>
/* The server tick's head, see servertick.h: the weather timers and strengths,
 * the day and total time, and the skylight level. Everything here is a scalar
 * the recording carries per tick, and the order is WorldServer.tick's.
 *
 * World.rand is a plain java.util.Random whose 48-bit state the snapshot
 * carries (worldstate.nbt's "rand"), so the weather's nextInt draws are the
 * same statements Java evaluates: the timer rolls are one expression each,
 * split so nothing draws twice in one C expression. */
#include "comparator.h"
#include "servertick.h"
#include "arena.h"
#include "env.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "aabb.h"
#include "blockcb.h"
#include "fire.h"
#include "smath.h"
#include "jmath.h"
#include "lightning.h"
#include "nbtjson.h"
#include "trace.h"

/* ------------------------------------------------------------ nbt scalars */

/* The canonical scalars: "l:5" "i:-3" "f:"+8 hex "b:1". nbtjson carries no
 * typed getter, so the value is rendered back to the text the file holds and
 * the prefix comes off; absent keys read 0. */
static char *scalar_text(const nbt *comp, const char *key)
{
    const nbt *v = comp ? nbt_get(comp, key) : NULL;

    if (v == NULL) return NULL;

    char *raw = nbt_render(v);

    if (raw == NULL) return NULL;

    /* nbt_render renders a scalar the way it stands inside a compound, quoted
     * ("l:2"); the callers want the bare form */
    char *p = raw;

    if (*p == '"') ++p;

    size_t n = strlen(p);

    if (n > 0 && p[n - 1] == '"') --n;

    char *out = malloc(n + 1);

    memcpy(out, p, n);
    out[n] = 0;
    free(raw);
    return out;
}

static int64_t nbt_long(const nbt *comp, const char *key)
{
    char *t = scalar_text(comp, key);

    if (t == NULL) return 0;

    int64_t v = t[1] == ':' ? strtoll(t + 2, NULL, 10) : strtoll(t, NULL, 10);

    free(t);
    return v;
}

static int nbt_int(const nbt *comp, const char *key)
{
    return (int)nbt_long(comp, key);
}

static int nbt_byte(const nbt *comp, const char *key)
{
    return (int)nbt_long(comp, key);
}

/* A canonical float is "f:" plus the raw bits in hex. */
static float nbt_float(const nbt *comp, const char *key)
{
    char *t = scalar_text(comp, key);

    if (t == NULL) return 0.0F;

    uint32_t bits = (uint32_t)strtoul(t + 2, NULL, 16);
    float f;

    memcpy(&f, &bits, sizeof f);
    free(t);
    return f;
}

/* --------------------------------------------------------------- the load */

int servertick_load(struct servertick *s, struct snapshot *snap, const char *dir)
{
    char path[1200];

    memset(s, 0, sizeof *s);
    s->daylight_cycle = 1;
    s->w = &snap->world;
    s->det = NULL; /* the Det streams come with the block work, see the header */

    const nbt *wi = snap->worldinfo;
    const nbt *ws = snap->worldstate;

    if (wi == NULL || ws == NULL) return 0;

    s->total_time = nbt_long(wi, "Time");
    s->world_time = nbt_long(wi, "DayTime");
    /* VillageSiege is not saved. The oracle's live siege at any snapshot this
     * harness records is at 0: every world here has run daytime ticks before
     * the snapshot (a fresh play's join, or the pre-script's first rows), and
     * a daytime tick pins the phase at 0. The muster branch only differs from
     * phase 0 while the celestial gate is closed, and it draws nothing then. */
    s->rain_time = nbt_int(wi, "rainTime");
    s->thunder_time = nbt_int(wi, "thunderTime");
    s->raining = nbt_byte(wi, "raining");
    s->thundering = nbt_byte(wi, "thundering");
    const char *fire_rule = nbt_string_value(nbt_get(nbt_get(wi, "GameRules"), "doFireTick"));
    s->fire_rule_off = fire_rule != NULL && strcmp(fire_rule, "false") == 0;

    /* Det.load's shape: the 48-bit state goes in directly, not through
     * jr_seed, which scrambles its argument */
    ST_RAND(s).seed = (uint64_t)nbt_long(ws, "rand") & JR_MASK;
    ST_LCG(s) = nbt_int(ws, "updateLCG");
    s->skylight_subtracted = nbt_int(ws, "skylightSubtracted");
    s->prev_raining_strength = nbt_float(ws, "prevRainingStrength");
    s->raining_strength = nbt_float(ws, "rainingStrength");
    s->prev_thundering_strength = nbt_float(ws, "prevThunderingStrength");
    s->thundering_strength = nbt_float(ws, "thunderingStrength");
    s->all_players_sleeping = nbt_byte(ws, "allPlayersSleeping");
    s->next_tick_entry = nbt_long(ws, "nextTickEntryID");

    (void)path;
    return 1;
}

/* ------------------------------------------------------------- the weather */

static float clamp_float(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void servertick_update_weather(struct servertick *s)
{
    if (s->thunder_time <= 0)
    {
        if (s->thundering) s->thunder_time = jr_int_n(&ST_RAND(s), 12000) + 3600;
        else s->thunder_time = jr_int_n(&ST_RAND(s), 168000) + 12000;
    }
    else
    {
        --s->thunder_time;

        if (s->thunder_time <= 0) s->thundering = !s->thundering;
    }

    s->prev_thundering_strength = s->thundering_strength;

    if (s->thundering) s->thundering_strength = (float)((double)s->thundering_strength + 0.01);
    else s->thundering_strength = (float)((double)s->thundering_strength - 0.01);

    s->thundering_strength = clamp_float(s->thundering_strength, 0.0F, 1.0F);

    if (s->rain_time <= 0)
    {
        if (s->raining) s->rain_time = jr_int_n(&ST_RAND(s), 12000) + 12000;
        else s->rain_time = jr_int_n(&ST_RAND(s), 168000) + 12000;
    }
    else
    {
        --s->rain_time;

        if (s->rain_time <= 0) s->raining = !s->raining;
    }

    s->prev_raining_strength = s->raining_strength;

    if (s->raining) s->raining_strength = (float)((double)s->raining_strength + 0.01);
    else s->raining_strength = (float)((double)s->raining_strength - 0.01);

    s->raining_strength = clamp_float(s->raining_strength, 0.0F, 1.0F);
}

/* ------------------------------------------------------------- the skylight */

static float get_celestial_angle(struct servertick *s, float partial)
{
    int day = (int)(s->world_time % 24000L);
    float a = ((float)day + partial) / 24000.0F - 0.25F;

    if (a < 0.0F) ++a;
    if (a > 1.0F) --a;

    float b = a;
    a = 1.0F - (float)((cos((double)a * 3.141592653589793) + 1.0) / 2.0);

    return b + (a - b) / 3.0F;
}

/* World.calculateSkylightSubtracted also reads the celestial angle for the
 * tile-tick path (tileticks_set_time). */
float servertick_celestial_radians(struct servertick *s)
{
    /* WorldProviderHell and WorldProviderEnd: the constant angles 0.5, 0.0 */
    if (s->no_sky) return (s->w != NULL && s->w->dim == -1 ? 0.5F : 0.0F) * 3.1415927F * 2.0F;
    return get_celestial_angle(s, 1.0F) * 3.1415927F * 2.0F;
}

/* VillageSiege.tick. WorldServer.tick's tail runs the player manager, the
 * village collection and then the siege. Once night falls the first tick
 * whose celestial angle is in [0.5, 0.501] (midnight) draws
 * rand.nextInt(10) to decide whether a siege starts, and the phase then
 * parks at 1 (muster) or 2 (nothing tonight), so the roll happens once per
 * night. In the muster phase func_75529_b looks for a village near a player
 * every tick (siege_muster, its draws on World.rand). In 1.7.10 it never
 * succeeds: its func_75527_a builds the spawn point's Vec3 and drops it, so
 * it always returns null, field_75535_b never turns true, and the zombie
 * countdown (field_75534_e, field_75533_d) and spawnZombie are unreachable. */
static void village_siege_tick(struct servertick *s)
{
    if (s->skylight_subtracted < 4) /* World.isDaytime */
    {
        s->siege_phase = 0;
        return;
    }

    if (s->siege_phase == 2) return;

    if (s->siege_phase == 0)
    {
        float angle = get_celestial_angle(s, 0.0F);

        if ((double)angle < 0.5 || (double)angle > 0.501) return;

        s->siege_phase = jr_int_n(&ST_RAND(s), 10) == 0 ? 1 : 2;
        s->siege_mustering = 0;

        if (s->siege_phase == 2) return;
    }

    /* at -1 (a snapshot taken at night, which vanilla never moves to 0
     * before a daytime tick) the muster runs too, and the roll above is
     * skipped that night */
    if (!s->siege_mustering)
    {
        if (s->siege_muster == NULL || !s->siege_muster(s->siege_muster_ctx, &ST_RAND(s))) return;
        s->siege_mustering = 1;
    }
}

void servertick_village_siege(struct servertick *s)
{
    village_siege_tick(s);
}

static float rain_strength(const struct servertick *s, float partial)
{
    return s->prev_raining_strength + (s->raining_strength - s->prev_raining_strength) * partial;
}

static float weighted_thunder_strength(const struct servertick *s, float partial)
{
    return (s->prev_thundering_strength + (s->thundering_strength - s->prev_thundering_strength) * partial) *
           rain_strength(s, partial);
}

int servertick_skylight(struct servertick *s)
{
    float angle = get_celestial_angle(s, 1.0F);
    float v = 1.0F - (mh_cos(angle * 3.141592653589793F * 2.0F) * 2.0F + 0.5F);

    if (v < 0.0F) v = 0.0F;
    if (v > 1.0F) v = 1.0F;

    v = 1.0F - v;
    v = (float)((double)v * (1.0 - (double)(rain_strength(s, 1.0F) * 5.0F) / 16.0));
    v = (float)((double)v * (1.0 - (double)(weighted_thunder_strength(s, 1.0F) * 5.0F) / 16.0));
    v = 1.0F - v;
    return (int)(v * 11.0F);
}

/* ---------------------------------------------------------------- the head */

/* The Nether's and the End's calculateCelestialAngle are constants (0.5 and
 * 0.0), so calculateSkylightSubtracted is too: 11 and 0. */
static int skylight_no_sky(const struct servertick *s)
{
    return s->w != NULL && s->w->dim == -1 ? 11 : 0;
}

static void st_on_block(void *ctx, int x, int y, int z, int id, int meta);

/* S1: super.tick() (World.tick -> updateWeather, a no-op without a sky), the
 * derived clocks of a world without a sky, and areAllPlayersAsleep. */
static void st_weather_sleep(struct servertick *s)
{
    if (!s->no_sky) servertick_update_weather(s);

    /* the Nether's and the End's WorldInfo is a DerivedWorldInfo over the
     * overworld's, whose clocks the overworld's tick has already advanced:
     * every read in this tick, the spawner's included, sees the new time,
     * and the derived increments below are no-ops */
    if (s->no_sky)
    {
        ++s->total_time;
        if (s->daylight_cycle) ++s->world_time;
    }

    /* areAllPlayersAsleep: the skip to the next morning, then
     * wakeAllPlayers (its bed writes are the tick's own) and
     * resetRainAndThunder */
    if (s->all_players_sleeping && s->on_sleep_check != NULL && s->on_sleep_check(s->on_sleep_ctx))
    {
        if (s->daylight_cycle)
        {
            int64_t v = s->world_time + 24000;
            s->world_time = v - v % 24000;
        }

        s->all_players_sleeping = 0;
        void (*prev_on_block)(void *, int, int, int, int, int) = s->w->on_block;
        void *prev_ctx = s->w->on_block_ctx;
        s->w->on_block = st_on_block;
        s->w->on_block_ctx = s;
        if (s->on_wake_all != NULL) s->on_wake_all(s->on_sleep_ctx, s);
        s->w->on_block = prev_on_block;
        s->w->on_block_ctx = prev_ctx;
        s->rain_time = 0;
        s->raining = 0;
        s->thunder_time = 0;
        s->thundering = 0;
    }
}

/* S2: the mob spawner's slot, before the skylight level and the clocks */
static void st_spawner(struct servertick *s)
{
    if (s->on_spawner != NULL) s->on_spawner(s->on_spawner_ctx, s);
}

/* S1T: WorldServer.tick's skylight level, then the total time, then the day
 * time (doDaylightCycle is on) */
static void st_clocks(struct servertick *s)
{
    if (s->no_sky)
    {
        s->skylight_subtracted = skylight_no_sky(s);
        return;
    }

    s->skylight_subtracted = servertick_skylight(s);
    ++s->total_time;
    if (s->daylight_cycle) ++s->world_time;
}

void servertick_tick_head(struct servertick *s)
{
    st_weather_sleep(s);
    st_spawner(s);
    st_clocks(s);
}

/* ------------------------------------------------------------- the tick body */

#include <math.h>
#include "biomes.h"
#include "features_lakes.h"
#include "blocks.h"
#include "features_lakes.h"
#include "features_springs.h"
#include "randomtick.h"
#include "ticks.h"

#define ID_ICE 79
#define ID_SNOW_LAYER 78

/* The air material's index, looked up by name so a regenerated blocks.h cannot
 * change what it means. */
static int st_air_index = -1;

/* before main: the same for every environment */
__attribute__((constructor)) static void st_air_init(void)
{
    for (size_t i = 0; i < sizeof MATERIALS / sizeof MATERIALS[0]; ++i)
        if (MATERIALS[i].name != NULL && strcmp(MATERIALS[i].name, "air") == 0) st_air_index = (int)i;
}

static int st_air_material(void)
{
    return st_air_index;
}

/* ------------------------------------------------------------- the recorder */

/* The tick's write and spawn lists: fixed, made with the world (a probe that
 * never called this gets them at its first write). */
void servertick_reserve(struct servertick *s)
{
    if (s->writes == NULL)
    {
        s->writes = fixed_array(ST_MAX_WRITES, sizeof *s->writes);
        s->capwrites = ST_MAX_WRITES;
    }
    if (s->spawns == NULL)
    {
        s->spawns = fixed_array(ST_MAX_SPAWNS, sizeof *s->spawns);
        s->capspawns = ST_MAX_SPAWNS;
    }
}

static void write_push(struct servertick *s, int x, int y, int z, int id, int meta)
{
    if (s->writes == NULL) servertick_reserve(s);
    if (s->nwrites == ST_MAX_WRITES) abort();

    struct st_write *w = &s->writes[s->nwrites++];
    w->x = x;
    w->y = y;
    w->z = z;
    w->id = (int16_t)id;
    w->meta = (uint8_t)meta;
    w->pad = 0;
}

static void st_on_block(void *ctx, int x, int y, int z, int id, int meta)
{
    struct servertick *s = ctx;
    write_push(s, x, y, z, id, meta);
    if (s->w != NULL && !(s->w->write_flags & 2)) s->writes[s->nwrites - 1].pad |= ST_WRITE_UNSENT;
}

static struct st_spawn *spawn_new(struct servertick *s, const char *cls, int weather)
{
    if (s->spawns == NULL) servertick_reserve(s);
    if (s->nspawns == ST_MAX_SPAWNS) abort();

    struct st_spawn *sp = &s->spawns[s->nspawns++];
    memset(sp, 0, sizeof *sp);
    sp->t = s->tick;
    sp->dim = 0;
    sp->cls = cls;
    sp->weather = weather;
    sp->item = -1;
    sp->damage = -1;
    sp->count = -1;
    return sp;
}

/* The Entity constructor: the per-role id, the per-entity Random
 * (Det.newRandom, one seeder draw) and the UUID (two more). */
static void spawn_entity_base(struct servertick *s, struct st_spawn *sp, det_rng *rand_out)
{
    sp->id = det_next_entity_id_role(s->det, DET_SERVER);
    *rand_out = det_new_random_role(s->det, DET_SERVER);
    sp->rand_state = det_rng_state(rand_out);
    det_uuid_role(s->det, DET_SERVER, &sp->uuid_msb, &sp->uuid_lsb);
}

/* The tape replay's entity list takes the entity over: the record carries every
 * field its constructor set and the Det draws are already spent. */
static void spawn_notify(struct servertick *s, struct st_spawn *sp)
{
    if (s->on_spawn != NULL) s->on_spawn(s->on_spawn_ctx, sp);
}

/* The EntityItem(World, x, y, z) the drop's sink hands over: the constructor's
 * four Math.random draws and delayBeforeCanPickup = 10. */
static void st_drop_sink(void *ctx, double x, double y, double z, int item, int damage, int count)
{
    struct servertick *s = ctx;
    struct st_spawn *sp = spawn_new(s, "EntityItem", 0);
    det_rng r;
    spawn_entity_base(s, sp, &r);

    sp->hover = (float)(det_math_random_role(s->det, DET_SERVER) * 3.141592653589793 * 2.0);
    sp->x = x;
    sp->y = y;
    sp->z = z;
    sp->yaw = (float)(det_math_random_role(s->det, DET_SERVER) * 360.0);

    double m = det_math_random_role(s->det, DET_SERVER);
    sp->mx = (double)(float)(m * 0.20000000298023224 - 0.10000000149011612);
    sp->my = 0.20000000298023224;
    m = det_math_random_role(s->det, DET_SERVER);
    sp->mz = (double)(float)(m * 0.20000000298023224 - 0.10000000149011612);

    sp->item = item;
    sp->damage = damage;
    sp->count = count;
    sp->age = 0;
    sp->delay = 10;
    spawn_notify(s, sp);
}

/* BlockTNT.func_150114_a(world, x, y, z, 1, null) from the fire spread: the
 * EntityTNTPrimed constructor's Entity draws, then its fuse angle's
 * Math.random. */
static void st_tnt_prime(void *ctx, int x, int y, int z)
{
    struct servertick *s = ctx;
    struct st_spawn *sp = spawn_new(s, "EntityTNTPrimed", 0);
    det_rng r;
    spawn_entity_base(s, sp, &r);

    float var9 = (float)(det_math_random_role(s->det, DET_SERVER) * 3.141592653589793 * 2.0);
    sp->x = (double)((float)x + 0.5F);
    sp->y = (double)((float)y + 0.5F);
    sp->z = (double)((float)z + 0.5F);
    sp->mx = (double)(-((float)fd_sin((double)var9)) * 0.02F);
    sp->my = 0.20000000298023224;
    sp->mz = (double)(-((float)fd_cos((double)var9)) * 0.02F);
    sp->time = 80;   /* the fuse */
    spawn_notify(s, sp);
}

/* blockcb.c's own item_drop sink, for the blocks whose support test drops them
 * (the torch's and the rail's onNeighborBlockChange, BlockTorch.func_150109_e
 * on the scheduled tick): the World.rand chance float and position jitters
 * and the EntityItem's own draws are spent by blockcb.c, which hands the
 * finished entity over. The record takes them as they are. */
static void st_block_env_drop(void *ctx, int entity_id, uint64_t rand_state, int64_t uuid_msb, int64_t uuid_lsb,
                              double x, double y, double z, float yaw, float hover, double motion_x,
                              double motion_z, int item, int damage, int count)
{
    struct servertick *s = ctx;
    struct st_spawn *sp = spawn_new(s, "EntityItem", 0);

    sp->id = entity_id;
    sp->rand_state = rand_state;
    sp->uuid_msb = uuid_msb;
    sp->uuid_lsb = uuid_lsb;
    sp->x = x;
    sp->y = y;
    sp->z = z;
    sp->yaw = yaw;
    sp->hover = hover;
    sp->mx = motion_x;
    sp->my = 0.20000000298023224;
    sp->mz = motion_z;
    sp->item = item;
    sp->damage = damage;
    sp->count = count;
    sp->age = 0;
    sp->delay = 10;
    spawn_notify(s, sp);
}

/* BlockFalling.func_149830_m's spawn branch: the EntityFallingBlock constructor
 * draws the entity id, its Random and its UUID and nothing else. */
static void falling_spawn(struct servertick *s, int x, int y, int z, int block, int meta)
{
    struct st_spawn *sp = spawn_new(s, "EntityFallingBlock", 0);
    det_rng r;
    spawn_entity_base(s, sp, &r);

    sp->x = (double)((float)x + 0.5F);
    sp->y = (double)((float)y + 0.5F);
    sp->z = (double)((float)z + 0.5F);
    sp->tile = block;
    sp->tile_id = block;
    sp->data = meta;
    sp->time = 0;
    sp->air = 300;
    sp->drop_item = 1;
    sp->hurt_entities = block == 145;
    sp->fall_hurt_max = 40;
    sp->fall_hurt_amount = 2.0F;
    sp->on_ground = 0;
    spawn_notify(s, sp);
}

/* BlockFalling.func_149831_e. */
static int falling_can_fall(int below)
{
    int m = BLOCKS[below & 4095].material;

    if (m == st_air_material()) return 1;
    if ((below & 4095) == 51) return 1; /* fire */

    return MATERIALS[m].is_liquid;
}

/* ------------------------------------------------------- the scheduled ticks */

/* BlockButton.func_150046_n, the wooden button's arrow test (from its
 * updateTick while down, and from onEntityCollidedWithBlock while up):
 * func_150043_b's bounds for the metadata as it is (0.125 deep up, 0.0625
 * down), any EntityArrow inside them presses or keeps it down and
 * reschedules func_149738_a's 30; none releases a pressed one. */
void servertick_wood_button(struct world *w, int x, int y, int z,
                            int (*arrows_in)(void *ctx, const struct aabb *box), void *ctx)
{
    int meta = world_get_meta(w, x, y, z);
    int facing = meta & 7;
    int pressed = (meta & 8) != 0;
    float f4 = 0.375F, f5 = 0.625F, f6 = 0.1875F, f7 = pressed ? 0.0625F : 0.125F;
    float b0[6] = {0.5F - f6, f4, 0.5F - f6, 0.5F + f6, f5, 0.5F + f6};

    if (facing == 1) { b0[0] = 0.0F; b0[3] = f7; }
    else if (facing == 2) { b0[0] = 1.0F - f7; b0[3] = 1.0F; }
    else if (facing == 3) { b0[2] = 0.0F; b0[5] = f7; }
    else if (facing == 4) { b0[2] = 1.0F - f7; b0[5] = 1.0F; }

    struct aabb box = aabb_make((double)x + (double)b0[0], (double)y + (double)b0[1],
                                (double)z + (double)b0[2], (double)x + (double)b0[3],
                                (double)y + (double)b0[4], (double)z + (double)b0[5]);
    int arrows = arrows_in != NULL && arrows_in(ctx, &box);

    /* setBlockMetadataWithNotify(.., 3), func_150042_a; the render mark and
     * the click sound carry no state */
    if (arrows && !pressed)
    {
        world_set_meta(w, x, y, z, facing | 8, 3);
        blockcb_button_notify(w, x, y, z, facing, 143);
    }
    if (!arrows && pressed)
    {
        world_set_meta(w, x, y, z, facing, 3);
        blockcb_button_notify(w, x, y, z, facing, 143);
    }
    if (arrows) ticks_schedule_block_update(w, x, y, z, 143, 30);
}

static void update_tick(struct servertick *s, int x, int y, int z, int id)
{
    struct world *w = s->w;

    /* Block.updateTick is one virtual call, so the block callbacks it reaches
     * (a liquid's lava-water fizz, a flow destroying a plant) draw from
     * World.rand: publish the tick's stream for the whole dispatch. A nested
     * randomtick_update_tick clears it on its way out, which is why this is
     * not left to servertick_tick's one publication. */
    randomtick_tick_rand(&ST_RAND(s));
    int b = id & 4095;

    switch (b)
    {
    case 8:
    case 10:
        liquid_update_tick(w, x, y, z, b, &ST_RAND(s));
        break;

    case 9:
    case 11:
        liquid_static_update_tick(w, x, y, z, b, &ST_RAND(s));
        break;

    case 12:
    case 13:
    case 145: /* BlockAnvil extends BlockFalling. */
        /* BlockFalling.updateTick -> func_149830_m */
        if (falling_can_fall(world_get_block(w, x, y - 1, z) & 4095) && y >= 0)
        {
            if (world_check_chunks_exist(w, x - 32, y - 32, z - 32, x + 32, y + 32, z + 32))
            {
                falling_spawn(s, x, y, z, b, world_get_meta(w, x, y, z));
            }
            else
            {
                world_set_block(w, x, y, z, 0, 0, 3);

                while (falling_can_fall(world_get_block(w, x, y - 1, z) & 4095) && y > 0) --y;

                if (y > 0) world_set_block(w, x, y, z, b, 0, 3);
            }
        }

        break;

    case 122: /* BlockDragonEgg uses the same fall test and entity, but its
               * short path places with flag 2. */
        if (falling_can_fall(world_get_block(w, x, y - 1, z) & 4095) && y >= 0)
        {
            if (world_check_chunks_exist(w, x - 32, y - 32, z - 32, x + 32, y + 32, z + 32))
                falling_spawn(s, x, y, z, b, 0);
            else
            {
                world_set_block(w, x, y, z, 0, 0, 3);
                while (falling_can_fall(world_get_block(w, x, y - 1, z) & 4095) && y > 0) --y;
                if (y > 0) world_set_block(w, x, y, z, b, 0, 2);
            }
        }
        break;

    case 77:  /* BlockButton.updateTick: the stone button releases */
    case 143: /* the wooden one (field_150047_a) runs func_150046_n */
    {
        int meta = world_get_meta(w, x, y, z);
        int facing = meta & 7;

        if (!(meta & 8)) break;

        if (b == 143)
        {
            servertick_wood_button(w, x, y, z, s->arrows_in, s->arrows_ctx);
            break;
        }

        world_set_meta(w, x, y, z, facing, 3);
        blockcb_button_notify(w, x, y, z, facing, b);
        /* playSoundEffect, markBlockRangeForRenderUpdate: no state */
        break;
    }

    case FIRE_BLOCK:
        fire_update_tick(w, &ST_RAND(s), x, y, z);
        break;

    case 90:
        /* BlockPortal.updateTick: in a surface world with mob spawning on,
         * one nextInt(2000) against the difficulty; a hit walks down to the
         * first solid top surface and spawns a zombie pigman on it when the
         * block above is not a normal cube */
        if (w->dim == 0 && s->mob_spawning && jr_int_n(&ST_RAND(s), 2000) < s->difficulty)
        {
            int fy = y;

            while (!fire_solid_top_surface(w, x, fy, z) && fy > 0) --fy;

            if (fy > 0 && !BLOCKS[world_get_block(w, x, fy + 1, z) & 4095].normal_cube)
            {
                if (s->portal_spawn != NULL)
                    s->portal_spawn(s->portal_spawn_ctx, (double)x + 0.5, (double)fy + 1.1, (double)z + 0.5);
                else
                    fprintf(stderr, "servertick: a portal block spawned a zombie pigman at %d %d %d (not ported)\n",
                            x, fy, z);
            }
        }
        break;

    case 50: /* BlockTorch */
    case 76: /* BlockRedstoneTorch: super's updateTick, then its own state
              * machine, which writes nothing in a redstone-off world */
        block_torch_update_tick(w, x, y, z);
        break;

    case 70: /* BlockPressurePlate.updateTick: one virtual call, and the plate
              * body draws nothing (the recount is deterministic and
              * func_149738_a is a constant 20), so no rand publication. The
              * tick only acts while the plate is down. */
    case 72:
    case 147: /* BlockPressurePlateWeighted: the same base updateTick */
    case 148:
    case 132: /* BlockTripWire.updateTick: the entity re-check, the same
               * hook (no draws) */
        if (s->plate_tick) s->plate_tick(s->plate_tick_ctx, x, y, z, b);
        break;

    case 149: /* BlockRedstoneComparator.updateTick (comparator.c) */
    case 150:
        comparator_update_tick(w, x, y, z, b);
        break;

    case 131: /* BlockTripWireHook.updateTick: func_150136_a(false, meta,
               * true, -1, 0) */
        blockcb_tripwire_hook_tick(w, x, y, z);
        break;

    default:
        randomtick_update_tick(w, b, &ST_RAND(s), &ST_RAND(s), x, y, z);
        break;
    }
}

/* WorldServer.tickUpdates(false): the pending set's smallest entries with a
 * scheduled time at or before the total time, at most 1000 of them, each one
 * ticked or re-scheduled by a tick the chunks-exist check rejects. */
static void tick_updates(struct servertick *s)
{
    struct world *w = s->w;
    int n = ticks_pending_count();

    if (n > 1000) n = 1000;

    struct tick_entry *this_tick ENV_LOCAL = envstack_take(1000 * sizeof *this_tick);
    int nt = 0;

    for (int i = 0; i < n; ++i)
    {
        struct tick_entry e;

        if (!ticks_pop_due(&e, s->total_time)) break;
        this_tick[nt++] = e;
    }

    ticks_batch_begin(this_tick, nt);

    for (int i = 0; i < nt; ++i)
    {
        struct tick_entry e = this_tick[i];

        ticks_batch_at(i);

        /* checkChunksExist over the one cell and getBlock there: one
         * lookup of the cell's chunk (a server world's) */
        struct chunk *c = NULL;
        int exists;

        if (!w->is_remote && e.y >= 0 && e.y < 256)
            exists = (c = world_chunk(w, e.x >> 4, e.z >> 4)) != NULL;
        else exists = world_check_chunks_exist(w, e.x, e.y, e.z, e.x, e.y, e.z);
        if (exists)
        {
            int id = c != NULL && (unsigned)e.x + 30000000u < 60000000u && (unsigned)e.z + 30000000u < 60000000u
                         ? chunk_get_block(c, e.x & 15, e.y, e.z & 15) : world_get_block(w, e.x, e.y, e.z);

            if (BLOCKS[id & 4095].material != st_air_material() && (id & 4095) == e.block)
                update_tick(s, e.x, e.y, e.z, e.block);
        }
        else
        {
            ticks_schedule_block_update(w, e.x, e.y, e.z, e.block, 0);
        }
    }

    ticks_batch_end();
}

/* ------------------------------------------------------ func_147456_g's reads */

/* World.isRaining / World.isThundering: getRainStrength(1.0F) > 0.2, and the
 * thunder one as getWeightedThunderStrength(1.0F) > 0.9, which is the
 * interpolated thunder strength scaled by the rain strength. Neither reads the
 * worldInfo flags, which only drive updateWeather's own transitions. */
static int is_raining(const struct servertick *s)
{
    return (double)rain_strength(s, 1.0F) > 0.2;
}

static int is_thundering(const struct servertick *s)
{
    return (double)weighted_thunder_strength(s, 1.0F) > 0.9;
}

int servertick_is_thundering(const struct servertick *s)
{
    return is_thundering(s);
}

/* World.canLightningStrikeAt. */
static int can_lightning_strike_at(struct servertick *s, int x, int y, int z)
{
    struct world *w = s->w;

    if (!is_raining(s)) return 0;
    if (!world_can_block_see_the_sky(w, x, y, z)) return 0;
    if (world_get_precipitation_height(w, x, z) > y) return 0;

    int biome = biome_at(w, x, z);

    if (BIOMES[biome].snow) return 0;
    if (world_can_snow_at(w, x, y, z, 0)) return 0;

    /* BiomeGenBase.canSpawnLightningBolt, from the biome table (rain is off
     * for the desert, savanna and mesa biomes as well as hell and the end) */
    return BIOMES[biome].lightning;
}

/* World.getClosestPlayer(x, y, z, maxDistance) with one parked player: the
 * player answers when its distance is inside the box. */
static int player_near(struct servertick *s, double x, double y, double z, double maxd)
{
    double dx = s->player_x - x, dy = s->player_y - y, dz = s->player_z - z;
    double d = dx * dx + dy * dy + dz * dz;

    return maxd < 0.0 || d < maxd * maxd;
}

/* World.playSoundEffect's argument draws: the mood sound builds its pitch from
 * one nextFloat before the (empty) server-side call. */
static void mood_sound(struct servertick *s)
{
    jr_float(&ST_RAND(s));
    s->ambient_tick_countdown = jr_int_n(&ST_RAND(s), 12000) + 6000;
}

/* World.func_147467_a: the chunk's mood-sound draw and the relight enqueue. */
static void chunk_mood_sound(struct servertick *s, struct chunk *c, int ox, int oz)
{
    struct world *w = s->w;

    if (s->ambient_tick_countdown == 0)
    {
        ST_LCG(s) = ST_LCG(s) * 3 + 1013904223;
        int v = ST_LCG(s) >> 2;
        int lx = v & 15, lz = v >> 8 & 15, ly = v >> 16 & 255;
        int wx = lx + ox, wz = lz + oz;
        int id = world_get_block(w, wx, ly, wz);

        if (BLOCKS[id & 4095].material == st_air_material())
        {
            int light = world_get_full_block_light_value(w, wx, ly, wz, s->skylight_subtracted);
            int roll = jr_int_n(&ST_RAND(s), 8);

            if (light <= roll && world_get_light(w, LIGHT_SKY, wx, ly, wz) <= 0)
            {
                double cx = (double)wx + 0.5, cy = (double)ly + 0.5, cz = (double)wz + 0.5;
                double dx = s->player_x - cx, dy = s->player_y - cy, dz = s->player_z - cz;

                if (player_near(s, cx, cy, cz, 8.0) && dx * dx + dy * dy + dz * dz > 4.0)
                    mood_sound(s);
            }
        }
    }

    PHASE_SUB(PS_RELIGHT, chunk_enqueue_relight_checks(w, c));
}

/* WorldServer.func_147456_g over a chunk the provider answers with
 * ChunkProviderServer.defaultEmptyChunk (EmptyChunk: air everywhere, light 0,
 * no sections). Only the draws survive -- the mood sound's roll, the thunder
 * roll and the ice/snow roll -- and the writes the rolls produce land on the
 * empty chunk, reported but not stored. */
static void tick_empty_chunk(struct servertick *s, int cx, int cz)
{
    struct world *w = s->w;
    int ox = cx * 16, oz = cz * 16;

    if (s->ambient_tick_countdown == 0)
    {
        ST_LCG(s) = ST_LCG(s) * 3 + 1013904223;
        int v = ST_LCG(s) >> 2;
        int lx = v & 15, lz = v >> 8 & 15, ly = v >> 16 & 255;
        int wx = lx + ox, wz = lz + oz;

        /* the block is air and the full light and sky light are 0, so the
         * roll is taken and its comparison always holds */
        jr_int_n(&ST_RAND(s), 8);

        double px = (double)wx + 0.5, py = (double)ly + 0.5, pz = (double)wz + 0.5;
        double dx = s->player_x - px, dy = s->player_y - py, dz = s->player_z - pz;

        if (player_near(s, px, py, pz, 8.0) && dx * dx + dy * dy + dz * dz > 4.0) mood_sound(s);
    }

    /* Chunk.enqueueRelightChecks: the empty chunk's cursor is 4096, so it does
     * nothing */

    if (jr_int_n(&ST_RAND(s), 100000) == 0 && is_raining(s) && is_thundering(s))
    {
        /* the bolt's height is 0 and EmptyChunk.canBlockSeeTheSky is false, so
         * canLightningStrikeAt refuses; getPrecipitationHeight still memoizes
         * the empty chunk's column, which draws nothing */
        ST_LCG(s) = ST_LCG(s) * 3 + 1013904223;
    }

    if (jr_int_n(&ST_RAND(s), 16) == 0)
    {
        ST_LCG(s) = ST_LCG(s) * 3 + 1013904223;
        int v = ST_LCG(s) >> 2;
        int lx = v & 15, lz = v >> 8 & 15;
        /* the precipitation height is 0, so the freeze check is at y = -1 (air
         * reads as air, no water) and the snow check at y = 0 (the block below
         * is air, so nothing snows) */
        (void)lx;
        (void)lz;
        (void)ox;
        (void)oz;
    }

    (void)w;
}

/* WorldServer.func_147456_g. */
/* S5: World.func_147456_g -> setActivePlayerChunksAndCheckLight. The active
 * set's contents do not change (the player is parked), so the recorded
 * iteration order stands. */
static void tick_player_light(struct servertick *s)
{
    if (s->ambient_tick_countdown > 0) --s->ambient_tick_countdown;

    if (!s->no_player)
    {
        jr_int_n(&ST_RAND(s), 1); /* rand.nextInt(playerEntities.size()) */
        int px = mh_floor(s->player_x) + jr_int_n(&ST_RAND(s), 11) - 5;
        int py = mh_floor(s->player_y) + jr_int_n(&ST_RAND(s), 11) - 5;
        int pz = mh_floor(s->player_z) + jr_int_n(&ST_RAND(s), 11) - 5;
        world_check_light_at(s->w, px, py, pz);
    }
}

/* S6 (the audit's S6 to S8): func_147456_g's loop over the active chunks,
 * each chunk's light flags, lightning, ice and snow and random block ticks
 * in turn on one World.rand */
/* chunk c's rtick_mask for band sec (world.h), made from the band when it
 * is 0: every random-ticking cell's group */
static uint64_t rtick_mask(struct chunk *c, int sec)
{
    uint64_t m = c->rtick_mask[sec];

    if (m != 0) return m;

    const struct chunk_sec *band = (c->mask >> sec & 1) ? chunk_sec_at(c, sec) : NULL;

    if (band == NULL) return 0;
    for (int k = 0; k < SEC_CELLS; ++k)
        if (BLOCKS[chunk_sec_id(band, k)].tick_randomly) m |= 1ULL << RTICK_BIT(k >> 8, k, k >> 4);
    c->rtick_mask[sec] = m;
    return m;
}

/* The random ticks' cells of chunk c fetched ahead: their positions are the
 * update LCG's next values over the sections ticking now, so the loop's
 * reads, each in another band, wait on memory together rather than one
 * after another. Only a guess: a tick that changes which sections tick
 * leaves some fetches unused, and the loop decides as before. A cell whose
 * group holds no random-ticking block is not read (rtick_mask). */
static void rtick_prefetch(const struct servertick *s, struct chunk *c)
{
    uint32_t lcg = (uint32_t)ST_LCG(s);

    for (int sec = 0; sec < 16; ++sec)
    {
        if (c->sections_ticking[sec] == 0) continue;

        const struct chunk_sec *band = (c->mask >> sec & 1) ? chunk_sec_at(c, sec) : NULL;
        uint64_t m = rtick_mask(c, sec);

        for (int k = 0; k < 3; ++k)
        {
            lcg = lcg * 3 + 1013904223u;
            uint32_t v = lcg >> 2;
            int x = (int)(v & 15), y = (int)(v >> 16 & 15), z = (int)(v >> 8 & 15);

            if (band != NULL && (m >> RTICK_BIT(x, y, z) & 1)) NW_PREFETCH(&band->ids[SEC_XYZ(x, y, z)]);
        }
    }
}

/* the next active chunk's lines the loop reads first (the map's near cache
 * holds it or the fetch is skipped) */
static void chunk_prefetch(const struct servertick *s, int i)
{
    if (i >= s->nactive) return;

    const struct chunk *c = world_chunk_near(s->w, s->active[i * 2], s->active[i * 2 + 1]);

    if (c == NULL) return;
    NW_PREFETCH(&c->mask);
    NW_PREFETCH(&c->sections_ticking);
    NW_PREFETCH(&c->queued_light_checks);
}

static void tick_chunks(struct servertick *s)
{
    struct world *w = s->w;

    for (int i = 0; i < s->nactive; ++i)
    {
        int cx = s->active[i * 2], cz = s->active[i * 2 + 1];
        int ox = cx * 16, oz = cz * 16;
        struct chunk *c = world_chunk(w, cx, cz);

        chunk_prefetch(s, i + 1);

        /* getChunkFromChunkCoords on a chunk the active set holds but the
         * provider has not loaded: with loadChunkOnProvideRequest on (a world
         * that generates, whose provider hook loads through the replay) it
         * loads, populate rule and all (the active set's floor(pos / 16)
         * square reaches one chunk past the player manager's (int)pos >> 4
         * window); off, Java gets ChunkProviderServer.defaultEmptyChunk for
         * it and ticks that */
        if (c == NULL && !w->no_generate && w->provide != NULL) c = world_load_chunk(w, cx, cz);
        if (c == NULL) { tick_empty_chunk(s, cx, cz); continue; }

        /* every write this chunk's phase makes can reach a block callback that
         * draws from World.rand (a lava-water fizz under a neighbour change, a
         * lightning bolt's fire placement): publish the tick's stream for the
         * whole phase, since a nested randomtick_update_tick clears it again */
        randomtick_tick_rand(&ST_RAND(s));

        chunk_mood_sound(s, c, ox, oz);
        PHASE_SUB(PS_LFLAGS, chunk_tick_flags(w, c));

        if (jr_int_n(&ST_RAND(s), 100000) == 0 && is_raining(s) && is_thundering(s))
        {
            ST_LCG(s) = ST_LCG(s) * 3 + 1013904223;
            int v = ST_LCG(s) >> 2;
            int lx = ox + (v & 15), lz = oz + (v >> 8 & 15);
            int h = world_get_precipitation_height(w, lx, lz);

            if (can_lightning_strike_at(s, lx, h, lz))
            {
                struct st_spawn *sp = spawn_new(s, "EntityLightningBolt", 1);
                int bolt_spawn_index = s->nspawns - 1;
                det_rng r;
                spawn_entity_base(s, sp, &r);
                sp->x = (double)lx;
                sp->y = (double)h;
                sp->z = (double)lz;
                sp->bolt_vertex = jr_long(&r.r);
                sp->bolt_living_time = jr_int_n(&r.r, 3) + 1;

                lightning_constructor_fire(w, &r, sp->x, sp->y, sp->z, s->difficulty, !s->fire_rule_off);
                sp = &s->spawns[bolt_spawn_index];
                sp->rand_state = det_rng_state(&r);
                spawn_notify(s, sp);
            }
        }

        if (jr_int_n(&ST_RAND(s), 16) == 0)
        {
            ST_LCG(s) = ST_LCG(s) * 3 + 1013904223;
            int v = ST_LCG(s) >> 2;
            int lx = v & 15, lz = v >> 8 & 15;
            int h = world_get_precipitation_height(w, lx + ox, lz + oz);

            if (world_can_block_freeze(w, lx + ox, h - 1, lz + oz, 1))
                world_set_block(w, lx + ox, h - 1, lz + oz, ID_ICE, 0, 3);

            if (is_raining(s) && world_can_snow_at(w, lx + ox, h, lz + oz, 1))
                world_set_block(w, lx + ox, h, lz + oz, ID_SNOW_LAYER, 0, 3);

            if (is_raining(s))
            {
                int biome = biome_at(w, lx + ox, lz + oz);

                if (BIOMES[biome].lightning)
                    world_fill_with_rain(w, &ST_RAND(s), lx + ox, h - 1, lz + oz);
            }
        }

        if (PHASE_PROF_ON()) phase_sub_begin_(PS_RTICK);
        rtick_prefetch(s, c);
        for (int sec = 0; sec < 16; ++sec)
        {
            if (c->sections_ticking[sec] == 0) continue;

            for (int k = 0; k < 3; ++k)
            {
                ST_LCG(s) = ST_LCG(s) * 3 + 1013904223;
                int v = ST_LCG(s) >> 2;
                int lx = v & 15, lz = v >> 8 & 15, ly = v >> 16 & 15;
                int y = (sec << 4) + ly;
                /* no random-ticking block in the cell's group: the check
                 * below would fail (a tick of this loop may have put the
                 * band again: the mask is made again) */
                if (!(rtick_mask(c, sec) >> RTICK_BIT(lx, ly, lz) & 1)) continue;
                /* the cell is c's (world_get_block's answer, without the
                 * lookup: an update tick loads chunks but unloads none) */
                int id = chunk_get_block(c, lx, y, lz);

                if (BLOCKS[id & 4095].tick_randomly)
                {
                    /* Block.updateTick is one virtual call: the same dispatch
                     * the scheduled path uses, liquids included */
                    update_tick(s, lx + ox, y, lz + oz, id & 4095);
                }
            }
        }
        if (PHASE_PROF_ON()) phase_sub_end_(PS_RTICK);
    }
}

/* S6 by itself: the device build (cuda/tick/dev.c) runs it as a kernel */
void servertick_chunk_loop(struct servertick *s)
{
    tick_chunks(s);
}

static void st_publish(struct servertick *s);

/* one of the world tick's own phases by itself, as servertick_tick runs it
 * (phase.h's id; 0 for a phase this file does not run alone): the device
 * build's entry for S6's neighbours */
int servertick_phase(struct servertick *s, int id)
{
    switch (id)
    {
    case PH_S1: st_weather_sleep(s); return 1;
    case PH_S2: st_spawner(s); return 1;
    case PH_S1T: st_clocks(s); st_publish(s); return 1;
    case PH_S4: tick_updates(s); return 1;
    case PH_S5: tick_player_light(s); return 1;
    case PH_S6: tick_chunks(s); return 1;
    case PH_SG: village_siege_tick(s); return 1;
    default: return 0;
    }
}

/* ------------------------------------------------------------------ the tick */

/* S1T's tail: World.isRaining() and the difficulty the fire spread reads,
 * and the skylight level and the clock the head just advanced, published
 * for the block callbacks */
static void st_publish(struct servertick *s)
{
    fire_set_weather(is_raining(s), s->difficulty);
    randomtick_set_skylight(s->skylight_subtracted);
    ticks_set_total_time(s->total_time);
}

/* S3: chunkProvider.unloadQueuedChunks */
static void st_unload(struct servertick *s, int t)
{
    if (s->on_unload_step != NULL) s->on_unload_step(s->on_unload_step_ctx, t);
}

void servertick_tick(struct servertick *s, int t)
{
    struct world *w = s->w;

    s->tick = t;

    /* the write listener, World.rand's consumers and the drop sink, all
     * installed before the head: its spawner slot can load and populate a
     * chunk (SpawnerAnimals reads off the loaded edge), and that population
     * writes (Rows.onBlock counts them in call order) and runs the block
     * callbacks and immediate liquid ticks that draw World.rand */
    w->on_block = st_on_block;
    w->on_block_ctx = s;
    fire_tnt_prime = st_tnt_prime;
    fire_tnt_prime_ctx = s;
    randomtick_tick_rand(&ST_RAND(s));
    randomtick_tick_det(s->det, DET_SERVER);
    randomtick_tick_drop_sink(st_drop_sink, s);
    /* blockcb.c's env covers the callbacks whose own drop path runs before
     * randomtick's: the torch's and the rail's support test draws World.rand
     * (the tick's stream, the same one randomtick published) and Det's
     * entity-id/Random/UUID/Math.random streams, and its item goes to the
     * tick's own spawn recorder */
    struct blockcb_env prev_env = nw_env->blockcb.env;
    nw_env->blockcb.env.world_rand = &ST_RAND(s);
    nw_env->blockcb.env.det = s->det;
    nw_env->blockcb.env.role = DET_SERVER;
    nw_env->blockcb.env.item_drop = st_block_env_drop;
    nw_env->blockcb.env.ctx = s;
    nw_env->blockcb.env.item_spill = NULL;
    randomtick_set_skylight(s->skylight_subtracted);
    ticks_set_rand(&ST_RAND(s));
    ticks_set_total_time(s->total_time);

    /* WorldServer.tick's phases (phase.h): World.tick, the asleep shortcut,
     * the spawner, the skylight level and the two clocks; the unload queue;
     * the scheduled ticks; func_147456_g */
    PHASE_RUN(PH_S1, st_weather_sleep(s));
    PHASE_RUN(PH_S2, st_spawner(s));
    PHASE_RUN(PH_S1T, (st_clocks(s), st_publish(s)));
    PHASE_RUN(PH_S3, st_unload(s, t));
    PHASE_RUN(PH_S4, tick_updates(s));
    PHASE_RUN(PH_S5, tick_player_light(s));
    PHASE_RUN(PH_S6, servertick_chunk_loop(s));
    if (!s->siege_deferred) PHASE_RUN(PH_SG, village_siege_tick(s));

    /* chunkProvider.unloadQueuedChunks, thePlayerManager.updatePlayerInstances,
     * the village collection, the village siege, the portal forcer and
     * func_147488_Z: nothing observable for one parked player inside 2400
     * ticks, see out/report.md. */
    w->on_block = NULL;
    w->on_block_ctx = NULL;
    randomtick_tick_rand(NULL);
    randomtick_tick_drop_sink(NULL, NULL);
    nw_env->blockcb.env = prev_env;
    fire_tnt_prime = NULL;
    fire_tnt_prime_ctx = NULL;
}

/* WorldServer.tick for a world with no players and no loaded chunks, the
 * Nether and the End on a mob-free tape: the head (World.tick -> updateWeather,
 * the asleep shortcut, the skylight level and the two clocks) and the empty
 * chunk walk's one side effect, World.ambientTickCountdown's decrement. The
 * pending set, the active chunk set, the player light check, the mood sound,
 * the thunder, ice and snow draws and the random block ticks all need a chunk
 * or a player and do nothing there; the tick's tail (unloadQueuedChunks,
 * updatePlayerInstances, the villages, the portal forcer, the block event
 * queues) is empty for the same reason. It draws nothing from Det. */
void servertick_tick_empty(struct servertick *s, int has_sky)
{
    if (has_sky)
    {
        servertick_tick_head(s);
    }
    else
    {
        /* World.updateWeather is gated on !WorldProvider.hasNoSky: the Nether
         * and the End step no weather timer and draw nothing. The skylight
         * level and the two clocks still run. */
        s->skylight_subtracted = servertick_skylight(s);
        ++s->total_time;
        ++s->world_time;
    }

    if (s->ambient_tick_countdown > 0) --s->ambient_tick_countdown;
}

void servertick_clear(struct servertick *s)
{
    if (s->nwrites > s->hiwrites) s->hiwrites = s->nwrites;
    if (s->nspawns > s->hispawns) s->hispawns = s->nspawns;
    s->nwrites = 0;
    s->nspawns = 0;
}

void servertick_trim(struct servertick *s)
{
    if (s->writes) fixed_array_trim(s->writes, (size_t)s->nwrites, &s->hiwrites, sizeof *s->writes);
    if (s->spawns) fixed_array_trim(s->spawns, (size_t)s->nspawns, &s->hispawns, sizeof *s->spawns);
}

const struct st_write *servertick_writes(const struct servertick *s, int *n)
{
    *n = s->nwrites;
    return s->writes;
}

const struct st_spawn *servertick_spawns(const struct servertick *s, int *n)
{
    *n = s->nspawns;
    return s->spawns;
}

/* ------------------------------------------------- the state the tick needs */

void servertick_set_ambient(struct servertick *s, int ambient, int difficulty)
{
    s->ambient_tick_countdown = ambient;
    s->difficulty = difficulty;
}

void servertick_set_player(struct servertick *s, double x, double y, double z)
{
    s->player_x = x;
    s->player_y = y;
    s->player_z = z;
}

void servertick_set_active(struct servertick *s, const int *pairs, int nactive)
{
    if (nactive > ST_MAX_ACTIVE) abort();
    memcpy(s->active, pairs, (size_t)(nactive * 2) * sizeof *s->active);
    s->nactive = nactive;
}

/* -------------------------------------------------------------- Det's load */

/* One canonical scalar node's value ("l:5", "i:-3", "b:1", "str:name"). */
static int64_t st_value_long(const nbt *v)
{
    if (v == NULL) return 0;

    char *t = nbt_render(v);

    if (t == NULL) return 0;

    char *p = t;

    if (*p == '"') ++p;

    if (p[0] != 0 && p[1] == ':') p += 2;

    int64_t out = strtoll(p, NULL, 10);
    free(t);
    return out;
}

static char *st_value_str(const nbt *v)
{
    if (v == NULL) return NULL;

    char *t = nbt_render(v);
    char *p = t;

    if (*p == '"') ++p;

    size_t n = strlen(p);

    if (n > 0 && p[n - 1] == '"') --n;

    if (n > 4 && strncmp(p, "str:", 4) == 0) { p += 4; n -= 4; }

    char *out = malloc(n + 1);
    memcpy(out, p, n);
    out[n] = 0;
    free(t);
    return out;
}

void servertick_load_det(struct servertick *s, det_state *det, const nbt *d)
{
    uint64_t seeder[DET_ROLES], math[DET_ROLES];
    int32_t next_id[DET_ROLES];

    for (int r = 0; r < DET_ROLES; ++r)
    {
        seeder[r] = (uint64_t)st_value_long(nbt_list_get(nbt_get(d, "seeder"), r));
        math[r] = (uint64_t)st_value_long(nbt_list_get(nbt_get(d, "math"), r));
        next_id[r] = (int32_t)st_value_long(nbt_list_get(nbt_get(d, "nextId"), r));
    }

    det_load(det, 0, seeder, math, next_id);

    const nbt *splits = nbt_get(d, "splits");

    for (int i = 0; i < nbt_list_size(splits); ++i)
    {
        const nbt *e = nbt_list_get(splits, i);
        uint64_t st[DET_ROLES];
        uint8_t used[DET_ROLES];

        for (int r = 0; r < DET_ROLES; ++r)
        {
            st[r] = (uint64_t)st_value_long(nbt_list_get(nbt_get(e, "state"), r));
            used[r] = (uint8_t)st_value_long(nbt_list_get(nbt_get(e, "used"), r));
        }

        char *name = st_value_str(nbt_get(e, "name"));

        if (name != NULL)
        {
            det_split_add(det, name, st, used);
            free(name);
        }
    }

    det_set_role(det, DET_SERVER);
    s->det = det;
}
