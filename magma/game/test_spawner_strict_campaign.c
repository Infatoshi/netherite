#include "game/runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int types[13] = {
    GM_MOB_PIG, GM_MOB_SHEEP, GM_MOB_RABBIT, GM_MOB_VILLAGER,
    GM_MOB_CREEPER, GM_MOB_PIGMAN, GM_MOB_BAT, GM_MOB_SNOWMAN,
    GM_MOB_ENDERMITE, GM_MOB_SLIME, GM_MOB_WOLF, GM_MOB_HORSE,
    GM_MOB_LLAMA,
};

static int files_equal(const char *a, const char *b) {
    FILE *left = fopen(a, "rb"), *right = fopen(b, "rb");
    int equal = left && right;
    while (equal) {
        unsigned char x[65536], y[65536];
        size_t nx = fread(x, 1, sizeof x, left);
        size_t ny = fread(y, 1, sizeof y, right);
        if (nx != ny || memcmp(x, y, nx)) equal = 0;
        if (nx < sizeof x || ny < sizeof y) {
            if (ferror(left) || ferror(right)) equal = 0;
            break;
        }
    }
    if (left) fclose(left);
    if (right) fclose(right);
    return equal;
}

static int make_payload(GmNbtBlob *blob, int index, int alternate) {
    GmNbtBlob effect = {0};
    if (!gm_nbt_blob_make_empty(blob)
            || !gm_nbt_blob_set_byte(blob, "NoAI", 1)
            || !gm_nbt_blob_set_byte(blob, "PersistenceRequired", 1)
            || !gm_nbt_blob_set_int(blob, "PortalCooldown", 20 + index)
            || !gm_nbt_blob_set_byte(blob, "LeftHanded", index & 1))
        return 0;
    switch (index) {
    case 0: return gm_nbt_blob_set_byte(blob, "Saddle", alternate);
    case 1:
        return gm_nbt_blob_set_byte(blob, "Color", alternate ? 14 : 5)
            && gm_nbt_blob_set_byte(blob, "Sheared", alternate);
    case 2:
        return gm_nbt_blob_set_int(blob, "RabbitType", alternate ? 99 : 4)
            && gm_nbt_blob_set_int(blob, "MoreCarrotTicks", 7 + alternate);
    case 3:
        return gm_nbt_blob_set_int(blob, "Profession", alternate ? 5 : 2)
            && gm_nbt_blob_set_byte(blob, "Willing", alternate);
    case 4:
        return gm_nbt_blob_set_byte(blob, "powered", alternate)
            && gm_nbt_blob_set_byte(blob, "ignited", 0);
    case 5: return gm_nbt_blob_set_int(blob, "Anger", 40 + alternate);
    case 6: return gm_nbt_blob_set_byte(blob, "BatFlags", alternate);
    case 7: return gm_nbt_blob_set_byte(blob, "Pumpkin", !alternate);
    case 8:
        return gm_nbt_blob_set_int(blob, "Lifetime", 30 + alternate)
            && gm_nbt_blob_set_byte(blob, "PlayerSpawned", alternate);
    case 9: return gm_nbt_blob_set_int(blob, "Size", 1 + alternate);
    case 10:
        return gm_nbt_blob_set_string(blob, "OwnerUUID",
                   "00000000-0000-0000-0000-000000000001")
            && gm_nbt_blob_set_byte(blob, "Sitting", alternate)
            && gm_nbt_blob_set_byte(blob, "Angry", !alternate)
            && gm_nbt_blob_set_byte(blob, "CollarColor", 5 + alternate);
    case 11:
        return gm_nbt_blob_set_byte(blob, "Tame", 1)
            && gm_nbt_blob_set_byte(blob, "EatingHaystack", alternate)
            && gm_nbt_blob_set_int(blob, "Temper", 70 + alternate)
            && gm_nbt_blob_set_int(blob, "Variant", 1284 + alternate);
    default:
        if (!gm_nbt_blob_set_int(blob, "Strength", 3 + alternate)
                || !gm_nbt_blob_set_int(blob, "Variant", alternate ? 3 : 1)
                || !gm_nbt_blob_make_empty(&effect)
                || !gm_nbt_blob_set_byte(&effect, "Id", 1)
                || !gm_nbt_blob_set_int(&effect, "Duration", 200)
                || !gm_nbt_blob_append_compound_list(
                    blob, "ActiveEffects", &effect)) {
            gm_nbt_blob_clear(&effect);
            return 0;
        }
        gm_nbt_blob_clear(&effect);
        return 1;
    }
}

static int fixture(GmRuntime *r) {
    GmConfig config;
    char error[256] = {0};
    gm_config_defaults(&config);
    config.seed = 12345;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 2;
    config.mobs = 1;
    config.weather = 0;
    config.daylight = 0;
    if (!gm_runtime_init(r, &config, error, sizeof error)) {
        fprintf(stderr, "init: %s\n", error);
        return 0;
    }
    r->gamerules.doMobSpawning = 0;
    gm_mobs_set_natural_spawning(&r->mobs, 0);
    gm_runtime_set_pose(r, 0.5, 79.0, 0.5, 0.0F, 0.0F);
    r->server_player = r->player;
    for (int x = -16; x <= 16; ++x)
        for (int z = -16; z <= 16; ++z)
            gm_world_set_block(r->world, x, 78, z, 2);
    for (int index = 0; index < 12; ++index) {
        int x = ((index & 3) - 2) * 4;
        int z = ((index >> 2) - 1) * 5;
        GmNbtBlob primary = {0}, alternate = {0};
        if (!gm_runtime_load_block(r, x, 79, z, 52, 0)
                || !make_payload(&primary, index, 0)
                || !make_payload(&alternate, index, 1)
                || !gm_runtime_spawner_set_state(
                    r, x, 79, z, types[index], index % 7,
                    13, 29, 1, 2, 16, 2,
                    primary.data, primary.len, 0)
                || !gm_runtime_spawner_add_potential(
                    r, x, 79, z, types[index], 1,
                    primary.data, primary.len, 0)
                || !gm_runtime_spawner_add_potential(
                    r, x, 79, z, types[index], 3,
                    alternate.data, alternate.len, 0)) {
            gm_nbt_blob_clear(&primary);
            gm_nbt_blob_clear(&alternate);
            return 0;
        }
        gm_nbt_blob_clear(&primary);
        gm_nbt_blob_clear(&alternate);
    }
    {
        GmNbtBlob primary = {0}, alternate = {0};
        gm_world_set_block_meta(r->world, 0, 79, 10, 66, 1);
        if (!make_payload(&primary, 12, 0)
                || !make_payload(&alternate, 12, 1)
                || !gm_runtime_spawn_minecart_fixture(
                    r, GM_MINECART_SPAWNER, 900000,
                    0.5, 79.0625, 10.5, 0.0, 0.0, 0.0, 0.0F)
                || !gm_runtime_minecart_set_spawner_nbt_state(
                    r, 900000, types[12], 3, 13, 29, 1, 2, 2, 16,
                    primary.data, primary.len, 0)
                || !gm_runtime_minecart_add_spawner_potential(
                    r, 900000, types[12], 3,
                    alternate.data, alternate.len, 0)) {
            gm_nbt_blob_clear(&primary);
            gm_nbt_blob_clear(&alternate);
            return 0;
        }
        gm_nbt_blob_clear(&primary);
        gm_nbt_blob_clear(&alternate);
    }
    return 1;
}

static unsigned int type_mask(const GmRuntime *r) {
    const EwStore *store = r->mobs.current ? &r->mobs.b : &r->mobs.a;
    unsigned int seen = 0;
    for (int slot = 1; slot < store->count; ++slot)
        if (store->alive[slot])
            for (int index = 0; index < 13; ++index)
                if (store->type[slot] == types[index])
                    seen |= 1u << index;
    return seen;
}

static int active_spawners(const GmRuntime *r) {
    int count = 0;
    for (int index = 0; index < r->mobs.spawners_cap; ++index)
        count += r->mobs.spawners[index].active != 0;
    return count;
}

static int run_path(const char *out, int reload) {
    static const int boundaries[] = {1, 7, 20, 199, 200, 220, 599};
    GmRuntime *r = calloc(1, sizeof *r);
    GmAction idle = {.hotbar_sel = -1};
    unsigned int seen = 0;
    if (!r || !fixture(r)) { free(r); return 0; }
    for (int tick = 0; tick < 600; ++tick) {
        if (tick == 200 && !gm_runtime_set_dimension(r, -1)) return 0;
        if (tick == 220 && !gm_runtime_set_dimension(r, 0)) return 0;
        gm_runtime_tick(r, idle);
        seen |= type_mask(r);
        if (reload) for (size_t i = 0;
                i < sizeof boundaries / sizeof boundaries[0]; ++i)
            if (tick == boundaries[i]
                    && (!gm_runtime_write_checkpoint(
                            r, ".world06-strict-mid.bin")
                        || !gm_runtime_load_checkpoint(
                            r, ".world06-strict-mid.bin"))) {
                fprintf(stderr, "reload boundary %d\n", tick);
                gm_runtime_destroy(r); free(r); return 0;
            }
    }
    if (seen != (1u << 13) - 1 || active_spawners(r) < 12
            || r->minecart_count < 1
            || !gm_runtime_write_checkpoint(r, out)) {
        fprintf(stderr, "final type_mask=%04x spawners=%d carts=%d\n",
                seen, active_spawners(r), r->minecart_count);
        gm_runtime_destroy(r); free(r); return 0;
    }
    gm_runtime_destroy(r); free(r); return 1;
}

int main(void) {
    const char *a = ".world06-strict-continuous.bin";
    const char *b = ".world06-strict-reloaded.bin";
    if (!run_path(a, 0) || !run_path(b, 1) || !files_equal(a, b)) {
        fprintf(stderr, "FAIL WORLD-06 strict continuation\n");
        return 1;
    }
    remove(".world06-strict-mid.bin");
    remove(".world06-strict-mid.bin.tmp");
    remove(a); remove(b);
    puts("spawner_strict_campaign: PASS 13 entity families, 12 block plus "
         "minecart spawner, weighted custom NBT, 600 ticks, dimension "
         "unload, 7 reload boundaries");
    return 0;
}
