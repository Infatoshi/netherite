#include "game/runtime.h"
#include "player_break.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_int(const char *text, int *out)
{
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (!text[0] || !end || *end || value < -32768 || value > 32767)
        return 0;
    *out = (int)value;
    return 1;
}

static int parse_seed(const char *text, uint64_t *out)
{
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 10);
    if (!text[0] || !end || *end || value >= (UINT64_C(1) << 48))
        return 0;
    *out = (uint64_t)value;
    return 1;
}

static int parse_float(const char *text, float *out)
{
    char *end = NULL;
    float value = strtof(text, &end);
    if (!text[0] || !end || *end || !isfinite(value)) return 0;
    *out = value;
    return 1;
}

static uint32_t float_bits(float value)
{
    union { float f; uint32_t u; } bits;
    bits.f = value;
    return bits.u;
}

static uint64_t double_bits(double value)
{
    union { double d; uint64_t u; } bits;
    bits.d = value;
    return bits.u;
}

static int run_respiration(int argc, char **argv)
{
    int level, air;
    uint64_t seed;
    JavaRandom random;
    if (argc != 5 || !parse_int(argv[2], &level)
            || !parse_int(argv[3], &air) || !parse_seed(argv[4], &seed)
            || level < 0 || level > 32 || air < -20 || air > 300)
        return 0;
    jrand_set_seed48(&random, seed);
    if (level <= 0 || jrand_int_bound(&random, level + 1) == 0)
        --air;
    printf("{\"ok\":true,\"mode\":\"respiration\",\"air\":%d,"
           "\"player_seed48\":%" PRIu64 "}\n", air, random.seed);
    return 1;
}

static int run_aqua(int argc, char **argv)
{
    int level, on_ground;
    PbInput input;
    if (argc != 4 || !parse_int(argv[2], &level)
            || !parse_int(argv[3], &on_ground)
            || level < 0 || level > 32
            || (on_ground != 0 && on_ground != 1))
        return 0;
    memset(&input, 0, sizeof input);
    input.block_id = BLK_STONE;
    input.tool_id = PB_IRON_PICKAXE;
    input.haste_amp = -1;
    input.fatigue_amp = -1;
    input.in_water = 1;
    input.aqua_affinity = level > 0;
    input.on_ground = on_ground;
    printf("{\"ok\":true,\"mode\":\"aqua\","
           "\"speed_bits\":\"%08" PRIx32 "\"}\n",
           float_bits(pb_get_dig_speed(&input)));
    return 1;
}

static int run_binding(int argc, char **argv)
{
    int level, creative;
    if (argc != 4 || !parse_int(argv[2], &level)
            || !parse_int(argv[3], &creative)
            || level < 0 || level > 32
            || (creative != 0 && creative != 1))
        return 0;
    printf("{\"ok\":true,\"mode\":\"binding\",\"can_take\":%s}\n",
           creative || level <= 0 ? "true" : "false");
    return 1;
}

static int run_thorns(int argc, char **argv)
{
    static const int items[6] = {276, 442, 313, 312, 311, 310};
    static const int slots[6] = {
        0, ISR_OFFHAND_SLOT, ISR_ARMOR_FEET,
        ISR_ARMOR_LEGS, ISR_ARMOR_CHEST, ISR_ARMOR_HEAD
    };
    int levels[6], unbreaking[4], damage[4];
    uint64_t seed;
    GmMobLive mobs;
    IsrInv inv;
    GmPlayerThornsOutcome outcome;
    if (argc != 17 || !parse_seed(argv[2], &seed)) return 0;
    for (int i = 0; i < 6; ++i)
        if (!parse_int(argv[3 + i], &levels[i])
                || levels[i] < 0 || levels[i] > 32)
            return 0;
    for (int i = 0; i < 4; ++i)
        if (!parse_int(argv[9 + i], &unbreaking[i])
                || unbreaking[i] < 0 || unbreaking[i] > 32)
            return 0;
    for (int i = 0; i < 4; ++i)
        if (!parse_int(argv[13 + i], &damage[i])
                || damage[i] < 0 || damage[i] > 4096)
            return 0;

    gm_mobs_init(&mobs, 42);
    jrand_set_seed48(&mobs.player_random, seed);
    isr_init(&inv);
    inv.current_item = 0;
    for (int i = 0; i < 6; ++i) {
        ICStack stack = ic_mk(items[i], 1, i < 2 ? 0 : damage[i - 2]);
        if (levels[i] > 0)
            stack.enchants[stack.n_enchants++] =
                (IcEnch){7, (i16)levels[i]};
        if (i >= 2 && unbreaking[i - 2] > 0)
            stack.enchants[stack.n_enchants++] =
                (IcEnch){34, (i16)unbreaking[i - 2]};
        isr_set_stack(&inv, slots[i], stack);
    }
    (void)gm_mobs_player_thorns_roll(&mobs, &inv, &outcome);
    printf("{\"ok\":true,\"mode\":\"thorns\",\"retaliation\":[");
    for (int i = 0; i < outcome.hit_count; ++i)
        printf("%s%d", i ? "," : "", outcome.damage[i]);
    printf("],\"equipment\":[");
    for (int i = 0; i < 6; ++i) {
        ICStack stack = isr_get_stack(&inv, slots[i]);
        printf("%s[%d,%d,%d]", i ? "," : "",
               stack.item, stack.count, stack.meta);
    }
    printf("],\"player_seed48\":%" PRIu64 "}\n",
           mobs.player_random.seed);
    return 1;
}

static int run_armor_damage(int argc, char **argv)
{
    static const int items[4] = {313, 312, 311, 310};
    int unbreaking[4], damage[4];
    uint64_t seed;
    float amount;
    GmMobLive mobs;
    IsrInv inv;
    PvStats vitals;
    if (argc != 12 || !parse_seed(argv[2], &seed)
            || !parse_float(argv[3], &amount)
            || amount <= 0.0F || amount > 2048.0F)
        return 0;
    for (int i = 0; i < 4; ++i)
        if (!parse_int(argv[4 + i], &unbreaking[i])
                || unbreaking[i] < 0 || unbreaking[i] > 32)
            return 0;
    for (int i = 0; i < 4; ++i)
        if (!parse_int(argv[8 + i], &damage[i])
                || damage[i] < 0 || damage[i] > 4096)
            return 0;
    gm_mobs_init(&mobs, 42);
    jrand_set_seed48(&mobs.player_random, seed);
    isr_init(&inv);
    pv_init(&vitals);
    for (int i = 0; i < 4; ++i) {
        ICStack stack = ic_mk(items[i], 1, damage[i]);
        if (unbreaking[i] > 0)
            stack.enchants[stack.n_enchants++] =
                (IcEnch){34, (i16)unbreaking[i]};
        isr_set_stack(&inv, ISR_ARMOR0 + i, stack);
    }
    (void)gm_mobs_attack_player(
        &mobs, (struct PvStats *)&vitals, &inv, amount, 0);
    printf("{\"ok\":true,\"mode\":\"armor_damage\","
           "\"equipment\":[");
    for (int i = 0; i < 4; ++i) {
        ICStack stack = isr_get_stack(&inv, ISR_ARMOR0 + i);
        printf("%s[%d,%d,%d]", i ? "," : "",
               stack.item, stack.count, stack.meta);
    }
    printf("],\"player_seed48\":%" PRIu64 "}\n",
           mobs.player_random.seed);
    return 1;
}

static int run_depth(int argc, char **argv)
{
    int level, on_ground;
    float forward, strafe, yaw, ai_speed;
    Chunk *window;
    McSinTable sin_table;
    PsvPlayer player;
    PsvAction action;
    McAABB blocks[PSV_MAX_BLOCKS];
    const double x0 = 8.5, y0 = 220.0, z0 = 8.5;
    if (argc != 8 || !parse_int(argv[2], &level)
            || !parse_int(argv[3], &on_ground)
            || !parse_float(argv[4], &forward)
            || !parse_float(argv[5], &strafe)
            || !parse_float(argv[6], &yaw)
            || !parse_float(argv[7], &ai_speed)
            || level < 0 || level > 32
            || (on_ground != 0 && on_ground != 1))
        return 0;
    window = (Chunk *)calloc(PSV_NCHUNKS, sizeof *window);
    if (!window) return 0;
    for (int z = 3; z <= 13; ++z)
        for (int y = 219; y <= 222; ++y)
            for (int x = 3; x <= 13; ++x)
                psv_set_block(window, x, y, z, 8);
    mc_sin_table_init(&sin_table);
    psv_player_init(&player);
    player.ent.posX = x0;
    player.ent.posY = y0;
    player.ent.posZ = z0;
    player.ent.box = psv_player_box(x0, y0, z0);
    player.ent.motionX = player.ent.motionY = player.ent.motionZ = 0.0;
    player.ent.onGround = on_ground;
    player.yaw = yaw;
    memset(&action, 0, sizeof action);
    action.forward = forward;
    action.strafe = strafe;
    action.yaw = yaw;
    action.depth_strider = level;
    action.water_ai_speed = ai_speed;
    psv_physics_tick(window, &sin_table, &player, &action, blocks);
    printf("{\"ok\":true,\"mode\":\"depth\","
           "\"x_bits\":\"%016" PRIx64 "\","
           "\"y_bits\":\"%016" PRIx64 "\","
           "\"z_bits\":\"%016" PRIx64 "\","
           "\"vx_bits\":\"%016" PRIx64 "\","
           "\"vy_bits\":\"%016" PRIx64 "\","
           "\"vz_bits\":\"%016" PRIx64 "\","
           "\"on_ground\":%s}\n",
           double_bits(player.ent.posX - x0),
           double_bits(player.ent.posY - y0),
           double_bits(player.ent.posZ - z0),
           double_bits(player.ent.motionX),
           double_bits(player.ent.motionY),
           double_bits(player.ent.motionZ),
           player.ent.onGround ? "true" : "false");
    free(window);
    return 1;
}

static int run_frost(int argc, char **argv)
{
    int level, on_ground;
    uint64_t seed;
    GmConfig config;
    GmRuntime runtime;
    char error[256];
    const int cx = 8, base_y = 220, cz = 8;
    if (argc != 5 || !parse_int(argv[2], &level)
            || !parse_int(argv[3], &on_ground)
            || !parse_seed(argv[4], &seed)
            || level < 0 || level > 32
            || (on_ground != 0 && on_ground != 1))
        return 0;
    gm_config_defaults(&config);
    config.seed = 42;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "%s\n", error);
        return 0;
    }
    gm_runtime_set_pose_state(
        &runtime, cx + 0.5, base_y + 1.0, cz + 0.5,
        0.0F, 0.0F, 0.0, 0.0, 0.0, on_ground, 0.0F);
    int radius = level + 2;
    if (radius > 16) radius = 16;
    for (int z = cz - radius; z <= cz + radius; ++z)
        for (int x = cx - radius; x <= cx + radius; ++x) {
            gm_world_set_block_meta(runtime.world, x, base_y, z, 9, 0);
            gm_world_set_block_meta(runtime.world, x, base_y + 1, z, 0, 0);
        }
    jrand_set_seed48(&runtime.mobs.player_random, seed);
    (void)gm_runtime_frost_walker_freeze(
        &runtime, cx + 0.5, base_y + 1.0, cz + 0.5,
        on_ground, level);
    printf("{\"ok\":true,\"mode\":\"frost\",\"changed\":[");
    int comma = 0;
    for (int z = cz - radius; z <= cz + radius; ++z)
        for (int x = cx - radius; x <= cx + radius; ++x)
            if (gm_world_block(runtime.world, x, base_y, z) == 212) {
                printf("%s[%d,-1,%d]", comma ? "," : "", x - cx, z - cz);
                comma = 1;
            }
    printf("],\"scheduled\":[");
    comma = 0;
    for (int i = 0; i < gm_runtime_scheduled_tick_count(&runtime); ++i) {
        GmRuntimeScheduledTick tick;
        if (!gm_runtime_scheduled_tick_get(&runtime, i, &tick)) continue;
        printf("%s[%d,%d,%d,%d,%lld,%d,%d]",
               comma ? "," : "", tick.x - cx,
               tick.y - (base_y + 1), tick.z - cz, tick.block,
               tick.time - runtime.clock.total_time - 1,
               tick.priority, i);
        comma = 1;
    }
    printf("],\"player_seed48\":%" PRIu64 "}\n",
           runtime.mobs.player_random.seed);
    gm_runtime_destroy(&runtime);
    return 1;
}

int main(int argc, char **argv)
{
    int ok = 0;
    if (argc >= 2 && strcmp(argv[1], "respiration") == 0)
        ok = run_respiration(argc, argv);
    else if (argc >= 2 && strcmp(argv[1], "aqua") == 0)
        ok = run_aqua(argc, argv);
    else if (argc >= 2 && strcmp(argv[1], "binding") == 0)
        ok = run_binding(argc, argv);
    else if (argc >= 2 && strcmp(argv[1], "thorns") == 0)
        ok = run_thorns(argc, argv);
    else if (argc >= 2 && strcmp(argv[1], "armor_damage") == 0)
        ok = run_armor_damage(argc, argv);
    else if (argc >= 2 && strcmp(argv[1], "depth") == 0)
        ok = run_depth(argc, argv);
    else if (argc >= 2 && strcmp(argv[1], "frost") == 0)
        ok = run_frost(argc, argv);
    if (!ok) {
        fprintf(stderr, "invalid equipment enchantment oracle arguments\n");
        return 2;
    }
    return 0;
}
