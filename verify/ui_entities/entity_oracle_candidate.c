/* entity_oracle_candidate.c - render one ui_entities state through the real
 * gm_frame_capture + CPU software-raster path (ghost views / dig_import).
 * Not a hand-painted stand-in. Reads goldens/meta/<id>.json for pose + entity.
 *
 * Usage:
 *   entity_oracle_candidate --state slime_size2 --meta goldens/meta/slime_size2.json \
 *       --ppm /tmp/c.ppm [--w 854 --h 480]
 */
#include "core/types.h"
#include "core/config.h"
#include "game/config.h"
#include "game/frame_capture.h"
#include "game/game.h"
#include "game/player_ctl.h"
#include "game/runtime.h"
#include "game/entity_render.h"
#include "game/particles_live.h"
#include "game/screen.h"
#include "game/hud.h"
#include "assets/mob_atlas.h"
#include "world/mesh_mc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *find_key(const char *j, const char *key) {
    char pat[128];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(j, pat);
    if (!p) return NULL;
    p = strchr(p + strlen(pat), ':');
    return p ? p + 1 : NULL;
}
static int j_int(const char *j, const char *key, int def) {
    const char *p = find_key(j, key);
    if (!p) return def;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    return (int)strtol(p, NULL, 10);
}
static int j_bool(const char *j, const char *key, int def) {
    const char *p = find_key(j, key);
    if (!p) return def;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    if (!strncmp(p, "true", 4)) return 1;
    if (!strncmp(p, "false", 5)) return 0;
    return (int)strtol(p, NULL, 10) != 0;
}
static float j_float(const char *j, const char *key, float def) {
    const char *p = find_key(j, key);
    if (!p) return def;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    return (float)strtod(p, NULL);
}
static int j_str_eq(const char *j, const char *key, const char *want) {
    const char *p = find_key(j, key);
    if (!p) return 0;
    while (*p && *p != '"') ++p;
    if (*p != '"') return 0;
    ++p;
    size_t n = strlen(want);
    return strncmp(p, want, n) == 0 && p[n] == '"';
}
static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || n > 1 << 20) { fclose(f); return NULL; }
    char *b = (char *)malloc((size_t)n + 1);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    b[n] = 0;
    fclose(f);
    return b;
}

typedef struct {
    int id, age, max_age, on_ground, tex, tex_base;
    double prev_x, prev_y, prev_z, x, y, z, vx, vy, vz;
    float scale, color_r, color_g, color_b;
} ParticleArg;

typedef struct {
    double x, y, z;
    float scale, color_r, color_g, color_b, jitter_x, jitter_y;
    float sky_light, block_light;
} DigParticleArg;

static int parse_particle_arg(const char *text, ParticleArg *out) {
    double values[19];
    const char *p = text;
    char *end = NULL;
    if (!text || !out) return 0;
    for (int i = 0; i < 19; ++i) {
        errno = 0;
        values[i] = strtod(p, &end);
        if (errno || end == p || !isfinite(values[i])
                || (i < 18 && *end != ',') || (i == 18 && *end != '\0'))
            return 0;
        p = end + (i < 18);
    }
    memset(out, 0, sizeof *out);
    out->id = (int)values[0];
    out->prev_x = values[1]; out->prev_y = values[2]; out->prev_z = values[3];
    out->x = values[4]; out->y = values[5]; out->z = values[6];
    out->vx = values[7]; out->vy = values[8]; out->vz = values[9];
    out->age = (int)values[10]; out->max_age = (int)values[11];
    out->on_ground = (int)values[12]; out->scale = (float)values[13];
    out->color_r = (float)values[14]; out->color_g = (float)values[15];
    out->color_b = (float)values[16];
    out->tex = (int)values[17]; out->tex_base = (int)values[18];
    return values[0] == out->id && values[10] == out->age
        && values[11] == out->max_age && values[12] == out->on_ground
        && values[17] == out->tex && values[18] == out->tex_base;
}

static int parse_dig_particle_arg(const char *text, DigParticleArg *out) {
    double values[11];
    const char *p = text;
    char *end = NULL;
    if (!text || !out) return 0;
    for (int i = 0; i < 11; ++i) {
        errno = 0;
        values[i] = strtod(p, &end);
        if (errno || end == p || !isfinite(values[i])
                || (i < 10 && *end != ',') || (i == 10 && *end != '\0'))
            return 0;
        p = end + (i < 10);
    }
    out->x = values[0]; out->y = values[1]; out->z = values[2];
    out->scale = (float)values[3];
    out->color_r = (float)values[4];
    out->color_g = (float)values[5];
    out->color_b = (float)values[6];
    out->jitter_x = (float)values[7];
    out->jitter_y = (float)values[8];
    out->sky_light = (float)values[9];
    out->block_light = (float)values[10];
    return out->scale > 0.0f
        && out->sky_light >= 0.0f && out->sky_light <= 15.0f
        && out->block_light >= 0.0f && out->block_light <= 15.0f;
}

static void place_pad(GmWorld *w, int y) {
    /* Keep this byte-for-byte aligned with capture_ui_entities_driver.py:
     * CX=8, CZ=8, x=[CX-6,CX+6], z=[CZ-2,CZ+10]. The older 16x16 native-only
     * pad made every complete-ROI comparison measure a different world. */
    for (int x = 2; x <= 14; ++x)
        for (int z = 6; z <= 18; ++z)
            gm_world_set_block(w, x, y, z, 1);
    gm_world_set_block(w, 10, y + 1, 11, 1); /* stone dig target */
    gm_world_set_block(w, 11, y + 1, 11, 2); /* grass dig target */
}

static void stage_hanging_support(GmRuntime *rt, const char *es) {
    int hx = j_int(es, "hanging_x", 8);
    int hy = j_int(es, "hanging_y", 7);
    int hz = j_int(es, "hanging_z", 12);
    int facing = j_int(es, "facing", 2);
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    if (j_str_eq(es, "support", "fence")) {
        gm_runtime_set_block(rt, hx, hy, hz, 85, 0);
        return;
    }
    int sx = hx - dx[facing], sz = hz - dz[facing];
    /* One common 7x7 wall supports every 1.11.2 painting art.  Stage the same
     * wall for the Java and native A/B fixtures so its occlusion and light are
     * part of the measured scene. */
    for (int across = -3; across <= 3; ++across)
        for (int up = -3; up <= 3; ++up) {
            int x = sx, z = sz;
            if (facing == 2 || facing == 3) x += across;
            else z += across;
            gm_runtime_set_block(rt, x, hy + up, z, 1, 0);
        }
}

static void fixture_map_colors(unsigned char colors[128 * 128]) {
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x) {
            int i = y * 128 + x;
            if (x < 16 && y < 16) colors[i] = 0;
            else {
                int palette = 1 + ((x / 16) + (y / 16) * 8) % 35;
                int shade = ((x / 4) + (y / 4)) & 3;
                colors[i] = (unsigned char)(palette * 4 + shade);
            }
        }
}

static void inject_from_meta(GmRuntime *rt, const char *state, const char *meta,
                             float entity_dx, float entity_dy,
                             float entity_dz) {
    gm_runtime_ent_views_clear(rt);
    gm_player_dig_reset();
    const char *es = strstr(meta, "\"entity\"");
    if (!es) es = meta;
    int hanging = j_str_eq(es, "type", "painting")
        || j_str_eq(es, "type", "item_frame")
        || j_str_eq(es, "type", "leash_knot")
        || j_str_eq(es, "type", "leashed_llama")
        || j_str_eq(es, "type", "hanging_background");
    if (hanging) {
        int hx = j_int(es, "hanging_x", 8);
        int hy = j_int(es, "hanging_y", 7);
        int hz = j_int(es, "hanging_z", 12);
        int facing = j_int(es, "facing", 2);
        stage_hanging_support(rt, es);
        if (j_str_eq(es, "type", "painting")) {
            gm_runtime_painting_set(
                rt, rt->dimension, 42, hx, hy, hz, facing,
                j_int(es, "art", 0), 0);
        } else if (j_str_eq(es, "type", "item_frame")) {
            static const int dx[6] = {0, 0, 0, 0, -1, 1};
            static const int dz[6] = {0, 0, -1, 1, 0, 0};
            double x = (double)hx + .5 - (double)dx[facing] * .46875;
            double y = (double)hy + .5;
            double z = (double)hz + .5 - (double)dz[facing] * .46875;
            gm_runtime_item_frame_set(
                rt, rt->dimension, 42, x, y, z, hx, hy, hz, facing,
                j_int(es, "item", 0), j_int(es, "item", 0) ? 1 : 0,
                j_int(es, "meta", 0), j_int(es, "rotation", 0));
            if (j_int(es, "item", 0) == 358) {
                unsigned char colors[128 * 128];
                int map_x_center = (int)floor(((double)hx + 64.0) / 128.0)
                    * 128;
                int map_z_center = (int)floor(((double)hz + 64.0) / 128.0)
                    * 128;
                fixture_map_colors(colors);
                gm_runtime_item_frame_set_map_state(
                    rt, 42, 0, 1, rt->dimension,
                    map_x_center, map_z_center, 0, 1, 0,
                    0, 0, 0, 0, 0);
                gm_runtime_item_frame_tracker_tick(rt, 42);
                gm_runtime_item_frame_set_map_colors(rt, 42, colors);
            }
        } else if (j_str_eq(es, "type", "leash_knot")
                || j_str_eq(es, "type", "leashed_llama")) {
            gm_runtime_leash_knot_set(rt, rt->dimension, 44, hx, hy, hz, 0);
            if (j_str_eq(es, "type", "leashed_llama")) {
                int eid = j_int(es, "eid", 42);
                GmLlamaState present;
                if (!gm_mobs_get_llama_state(&rt->mobs, eid, &present))
                    gm_runtime_spawn_llama_fixture(
                        rt, eid,
                        j_float(es, "x", 10.5f), j_float(es, "y", 5.0f),
                        j_float(es, "z", 10.5f), 0.0, 0.0, 0.0,
                        j_float(es, "yaw", 180.0f), 20.0f, 1,
                        20.0, 0.175, 0.5, 0, GM_HORSE_TAME,
                        20, j_int(es, "variant", 0), 3,
                        j_int(es, "decor", -1), 0, 0, 1,
                        0, 0, 0);
                gm_runtime_restore_living_leash_knot(rt, eid, 44);
            }
        }
        return;
    }
    if (j_str_eq(meta, "type", "empty") || !strcmp(state, "wither_empty"))
        return;
    if (!strncmp(state, "ender_chest_", 12)) return;
    if (!strncmp(state, "chest_", 6)) return;
    if (!strncmp(state, "shulker_box_", 12)) return;
    if (!strncmp(state, "beacon_world_", 13)) return;
    if (!strncmp(state, "spawner_", 8)) return;
    int is_dig = (strstr(state, "dig_") != NULL) || (strstr(meta, "\"dig\"") != NULL);
    if (is_dig) {
        /* Exact captured dust is a renderer fixture, not a player attack.
         * Keep the ordinary controller path for live reconstructed dust, but
         * do not invent hand swing and crack-overlay state for exact rows. */
        if (gm_recorded_dig_particles_count() > 0) return;
        const char *ds = strstr(meta, "\"dig\"");
        if (!ds) ds = meta;
        GmPlayerCtlSnap snap;
        memset(&snap, 0, sizeof snap);
        int stage = j_int(ds, "stage", 4);
        snap.dig_progress = (float)stage / 10.0f;
        if (snap.dig_progress < 0.1f) snap.dig_progress = 0.1f;
        snap.dig_hx = j_int(ds, "bx", 10);
        snap.dig_hy = j_int(ds, "by", 5);
        snap.dig_hz = j_int(ds, "bz", 11);
        snap.dig_face = j_int(ds, "face", 1);
        snap.dig_hitting = 1;
        /* entity_pin dig_hit freezes N ParticleDigging billboards (not stage). */
        snap.dig_particle_count = j_int(ds, "count", 0);
        gm_player_ctl_dig_import(&snap);
        return;
    }
    if (!strcmp(state, "skeleton_trap_group")) {
        static const float xs[4] = {5.8f, 7.6f, 9.4f, 11.2f};
        for (int i = 0; i < 4; ++i) {
            GmEntityView horse;
            memset(&horse, 0, sizeof horse);
            horse.type = 71;
            horse.x = xs[i] + entity_dx;
            horse.y = 5.0f + entity_dy;
            horse.z = 10.0f + entity_dz;
            horse.yaw = horse.head_yaw = 180.0f;
            horse.health = 15.0f;
            horse.ticks_existed = 40;
            horse.tape_pose = 1;
            horse.flags = 16384 | 32768; /* saddled and visibly ridden */
            horse.ent_id = 42 + i * 2;
            horse.lm_lit = 2;
            horse.lm_mul_r = horse.lm_mul_g = horse.lm_mul_b = 1.0f;
            gm_runtime_ent_view(rt, &horse);

            GmEntityView rider;
            memset(&rider, 0, sizeof rider);
            rider.type = 3;
            rider.x = xs[i] + entity_dx;
            rider.y = 6.2f + entity_dy;
            rider.z = 10.0f + entity_dz;
            rider.yaw = rider.head_yaw = 180.0f;
            rider.health = 20.0f;
            rider.ticks_existed = 40;
            rider.tape_pose = 1;
            rider.armor_head = 306;
            rider.flags = 131072; /* BOW_AND_ARROW */
            rider.ent_id = 43 + i * 2;
            rider.lm_lit = 2;
            rider.lm_mul_r = rider.lm_mul_g = rider.lm_mul_b = 1.0f;
            gm_runtime_ent_view(rt, &rider);
        }
        return;
    }
    GmEntityView ev;
    memset(&ev, 0, sizeof ev);
    const char *ss = strstr(es, "\"subject\"");
    const char *src = ss ? ss : es;
    ev.x = j_float(src, "x", 8.5f) + entity_dx;
    ev.y = j_float(src, "y", 5.0f) + entity_dy;
    ev.z = j_float(src, "z", 12.5f) + entity_dz;
    ev.yaw = j_float(src, "yaw", 180.0f);
    ev.pitch = j_float(src, "pitch", 0.0f);
    ev.head_yaw = ev.yaw;
    ev.health = 20.0f;
    /* RenderMinecart adds a deterministic sub-pixel offset derived from the
     * live entity id. Recorder metadata keeps that id in pin_reply_a; use it
     * so the strict subject lane compares the same raster registration. */
    ev.ent_id = j_int(meta, "eid", 42);
    ev.lm_lit = 2;
    ev.lm_mul_r = ev.lm_mul_g = ev.lm_mul_b = 1.0f;

    if (j_str_eq(es, "type", "slime") || (strstr(state, "slime") == state)) {
        ev.type = 35;
        ev.item_meta = j_int(es, "size", 2);
        if (ev.item_meta < 1) ev.item_meta = 1;
        ev.squish = j_float(es, "squish", 0.0f);
    } else if (j_str_eq(es, "type", "magma_cube") || (strstr(state, "magma") == state)) {
        ev.type = 27;
        ev.item_meta = j_int(es, "size", 2);
        if (ev.item_meta < 1) ev.item_meta = 1;
        ev.squish = j_float(es, "squish", 0.0f);
    } else if (j_str_eq(es, "type", "dragon") || strstr(state, "dragon_death")) {
        int packed = j_int(meta, "lightmap_packed", 15 * 16 << 16);
        ev.type = 9;
        ev.death_ticks = j_int(es, "death_ticks", 50);
        ev.lm_lit = 1;
        ev.lm_light = (float)((packed >> 16) & 65535) / 16.0f;
        ev.lm_blk = (float)(packed & 65535) / 16.0f;
        /* Match qrl render pin: keep health full so onDeathUpdate/explosion
         * particles do not run; only deathTicks drives dissolve + rays. */
        ev.health = 200.0f;
    } else if (j_str_eq(es, "type", "small_fireball") || strstr(state, "fireball_small")) {
        ev.type = 30;
        ev.item_id = 385;
        ev.item_meta = 0;
    } else if (j_str_eq(es, "type", "dragon_fireball") || strstr(state, "fireball_dragon")) {
        /* RenderDragonFireball: scale 2.0 + entity/enderdragon/dragon_fireball.png
         * (item atlas id 9003). Not fire_charge (385) and not on-fire layers. */
        ev.type = 33;
        ev.item_id = 9003;
        ev.item_meta = 0;
    } else if (j_str_eq(es, "type", "xp_orb") || strstr(state, "xp_orb")) {
        int packed = j_int(meta, "lightmap_packed", 15 * 16 << 16);
        ev.type = 21;
        ev.item_id = j_int(es, "value", 7);
        ev.item_meta = j_int(es, "color", 0);
        ev.age = j_int(es, "age", 0);
        ev.lm_lit = 1;
        ev.lm_light = (float)((packed >> 16) & 65535) / 16.0f;
        ev.lm_blk = (float)(packed & 65535) / 16.0f;
    } else if (j_str_eq(es, "type", "wither")
            || (strstr(state, "wither_") == state
                && !strstr(state, "wither_skull"))) {
        ev.type = GM_VIEW_WITHER;
        ev.health = j_float(es, "health", 300.0f);
        ev.ticks_existed = j_int(es, "ticks_existed", 40);
        ev.wither_invul_time = j_int(es, "invul", 0);
        ev.wither_head_yaw[0] = j_float(es, "head0_yaw", ev.yaw);
        ev.wither_head_pitch[0] = j_float(es, "head0_pitch", 0.0f);
        ev.wither_head_yaw[1] = j_float(es, "head1_yaw", ev.yaw);
        ev.wither_head_pitch[1] = j_float(es, "head1_pitch", 0.0f);
    } else if (j_str_eq(es, "type", "wither_skull")
            || strstr(state, "wither_skull")) {
        ev.type = GM_VIEW_WITHER_SKULL;
        ev.health = 1.0f;
        ev.ticks_existed = j_int(es, "ticks_existed", 20);
        ev.wither_skull_invulnerable = j_int(es, "invulnerable", 0);
    } else if (j_str_eq(es, "type", "skeleton")) {
        ev.type = 3;
        ev.tape_pose = 1;
        ev.limb_swing = j_float(es, "limb_swing", 0.0f);
        ev.limb_swing_amount = j_float(es, "limb_amount", 0.0f);
        ev.ticks_existed = j_int(es, "ticks_existed", 40);
        ev.armor_head = j_int(es, "armor_head", 0);
        if (j_bool(es, "swinging_arms", 0)) ev.flags |= 131072;
    } else if (j_str_eq(es, "type", "bat")) {
        ev.type = 24;
        ev.ticks_existed = j_int(es, "ticks_existed", 40);
        if (j_bool(es, "hanging", 0))
            ev.flags |= GM_ENTITY_FLAG_BAT_HANGING;
    } else if (j_str_eq(es, "type", "squid")) {
        ev.type = 14;
        ev.ticks_existed = j_int(es, "ticks_existed", 40);
        /* Type-local render handoff: yaw=renderYawOffset,
         * pitch=squidPitch, head_yaw=squidYaw, anim_time=tentacleAngle. */
        ev.pitch = j_float(es, "squid_pitch", 0.0f);
        ev.head_yaw = j_float(es, "squid_yaw", 0.0f);
        ev.anim_time = j_float(es, "tentacle_angle", 0.0f);
    } else if (j_str_eq(es, "type", "mooshroom")) {
        ev.type = 12; /* shared ModelCow */
        ev.skin = CR_MOB_MOOSHROOM + 1;
        ev.ticks_existed = j_int(es, "ticks_existed", 40);
        ev.head_yaw = j_float(es, "head_yaw", ev.yaw);
        ev.limb_swing = j_float(es, "limb_swing", 0.0f);
        ev.limb_swing_amount = j_float(es, "limb_amount", 0.0f);
        ev.tape_pose = 1;
        if (j_bool(es, "child", 0)) ev.flags |= 8;
    } else if (j_str_eq(es, "type", "llama")) {
        ev.type = 25;
        ev.item_id = j_int(es, "variant", 0);
        ev.item_meta = j_int(es, "decor", -1) + 1;
        ev.flags = (j_bool(es, "child", 0) ? 8 : 0)
                 | (j_bool(es, "chested", 0) ? 8192 : 0);
        ev.limb_swing = j_float(es, "limb_swing", 0.0f);
        ev.limb_swing_amount = j_float(es, "limb_amount", 0.0f);
        ev.ticks_existed = j_int(es, "ticks_existed", 40);
        ev.tape_pose = 1;
    } else if (j_str_eq(es, "type", "llama_spit")) {
        ev.type = 73;
        ev.ticks_existed = j_int(es, "ticks_existed", 20);
    } else if (j_str_eq(es, "type", "boat")) {
        ev.type = 37;
        ev.item_meta = j_int(es, "variant", 0);
        ev.health = -1.0f;
    } else if (j_str_eq(es, "type", "minecart_empty")
            || j_str_eq(es, "type", "minecart_chest")
            || j_str_eq(es, "type", "minecart_furnace")
            || j_str_eq(es, "type", "minecart_hopper")
            || j_str_eq(es, "type", "minecart_spawner")
            || j_str_eq(es, "type", "minecart_command")
            || j_str_eq(es, "type", "tnt_minecart")) {
        ev.type = j_str_eq(es, "type", "minecart_chest")
                ? GM_VIEW_MINECART_CHEST
                : j_str_eq(es, "type", "minecart_furnace")
                ? GM_VIEW_MINECART_FURNACE
                : j_str_eq(es, "type", "minecart_hopper")
                ? GM_VIEW_MINECART_HOPPER
                : j_str_eq(es, "type", "minecart_spawner")
                ? GM_VIEW_MINECART_SPAWNER
                : j_str_eq(es, "type", "minecart_command")
                ? GM_VIEW_MINECART_COMMAND
                : j_str_eq(es, "type", "tnt_minecart")
                ? GM_VIEW_MINECART_TNT
                : GM_VIEW_MINECART_EMPTY;
        ev.health = -1.0f;
        if (ev.type == GM_VIEW_MINECART_TNT) {
            ev.minecart_tnt_fuse = j_int(es, "fuse", -1);
            ev.minecart_tnt_fuse_valid = 1;
        }
    } else if (j_str_eq(es, "type", "horse")
            || j_str_eq(es, "type", "donkey")
            || j_str_eq(es, "type", "mule")
            || j_str_eq(es, "type", "skeleton_horse")
            || j_str_eq(es, "type", "zombie_horse")) {
        ev.type = j_str_eq(es, "type", "donkey") ? 69
                : j_str_eq(es, "type", "mule") ? 70
                : j_str_eq(es, "type", "skeleton_horse") ? 71
                : j_str_eq(es, "type", "zombie_horse") ? 72 : 68;
        ev.item_id = j_int(es, "variant", 0);
        ev.item_meta = j_int(es, "armor", 0);
        ev.flags = (j_bool(es, "child", 0) ? 8 : 0)
                 | (j_bool(es, "chested", 0) ? 8192 : 0)
                 | (j_bool(es, "saddled", 0) ? 16384 : 0)
                 | (j_bool(es, "ridden", 0) ? 32768 : 0)
                 | (j_int(es, "tail", 0) ? 65536 : 0);
        ev.graze_y = j_float(es, "head_lean", 0.0f);
        ev.swing_progress = j_float(es, "rearing", 0.0f);
        ev.squish = j_float(es, "mouth", 0.0f);
        ev.limb_swing = j_float(es, "limb_swing", 0.0f);
        ev.limb_swing_amount = j_float(es, "limb_amount", 0.0f);
        ev.ticks_existed = j_int(es, "ticks_existed", 40);
        ev.tape_pose = 1;
    } else {
        fprintf(stderr, "unknown entity for state %s\n", state);
        return;
    }
    if (j_bool(es, "glowing", 0)) ev.flags |= GM_ENTITY_FLAG_GLOWING;
    gm_runtime_ent_view(rt, &ev);
}

static int copy_file(const char *src, const char *dst) {
    FILE *sf = fopen(src, "rb");
    if (!sf) return 0;
    FILE *df = fopen(dst, "wb");
    if (!df) { fclose(sf); return 0; }
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, sf)) > 0) {
        if (fwrite(buf, 1, n, df) != n) { fclose(sf); fclose(df); return 0; }
    }
    fclose(sf); fclose(df);
    return 1;
}

static int write_framebuffer_ppm(const char *path, const CrFramebuffer *fb) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fprintf(f, "P6\n%d %d\n255\n", fb->w, fb->h);
    for (int i = 0; i < fb->w * fb->h; ++i) {
        unsigned char rgb[3] = {
            fb->color[i].r, fb->color[i].g, fb->color[i].b,
        };
        if (fwrite(rgb, 1, 3, f) != 3) {
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    return 1;
}

int main(int argc, char **argv) {
    const char *state = "unknown", *meta_path = NULL, *ppm = "/tmp/entity_oracle_c.ppm";
    int W = 854, H = 480;
    float pose_pitch_override = NAN;
    const char *raster_probe = NULL;
    float entity_dx = 0.0f, entity_dy = 0.0f, entity_dz = 0.0f;
    const char *set_args[16];
    int nset_args = 0;
    ParticleArg particle_args[32];
    int nparticle_args = 0;
    DigParticleArg dig_particle_args[32];
    int ndig_particle_args = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--state") && i + 1 < argc) state = argv[++i];
        else if (!strcmp(argv[i], "--meta") && i + 1 < argc) meta_path = argv[++i];
        else if (!strcmp(argv[i], "--ppm") && i + 1 < argc) ppm = argv[++i];
        else if (!strcmp(argv[i], "--w") && i + 1 < argc) W = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--h") && i + 1 < argc) H = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--pose-pitch") && i + 1 < argc)
            pose_pitch_override = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--raster-probe") && i + 1 < argc)
            raster_probe = argv[++i];
        else if (!strcmp(argv[i], "--entity-dx") && i + 1 < argc)
            entity_dx = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--entity-dy") && i + 1 < argc)
            entity_dy = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--entity-dz") && i + 1 < argc)
            entity_dz = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--set") && i + 1 < argc
                 && nset_args < (int)(sizeof set_args / sizeof set_args[0]))
            set_args[nset_args++] = argv[++i];
        else if (!strcmp(argv[i], "--particle") && i + 1 < argc
                 && nparticle_args <
                    (int)(sizeof particle_args / sizeof particle_args[0])) {
            if (!parse_particle_arg(argv[++i], &particle_args[nparticle_args])) {
                fprintf(stderr, "bad --particle: %s\n", argv[i]);
                return 2;
            }
            ++nparticle_args;
        }
        else if (!strcmp(argv[i], "--dig-particle") && i + 1 < argc
                 && ndig_particle_args <
                    (int)(sizeof dig_particle_args
                          / sizeof dig_particle_args[0])) {
            if (!parse_dig_particle_arg(
                    argv[++i], &dig_particle_args[ndig_particle_args])) {
                fprintf(stderr, "bad --dig-particle: %s\n", argv[i]);
                return 2;
            }
            ++ndig_particle_args;
        }
    }
    if (!meta_path) {
        fprintf(stderr, "usage: %s --meta PATH [--state ID] [--ppm PATH]\n", argv[0]);
        return 2;
    }
    char *meta = read_file(meta_path);
    if (!meta) { fprintf(stderr, "cannot read meta %s\n", meta_path); return 1; }

    gm_recorded_dig_particles_clear();
    if (ndig_particle_args > 0) {
        const char *ds = strstr(meta, "\"dig\"");
        gm_recorded_dig_particles_set_source(
            j_int(ds ? ds : meta, "block_id", 1));
    }
    for (int i = 0; i < ndig_particle_args; ++i) {
        const DigParticleArg *p = &dig_particle_args[i];
        if (!gm_recorded_dig_particle_add(
                p->x, p->y, p->z, p->scale,
                p->color_r, p->color_g, p->color_b,
                p->jitter_x, p->jitter_y,
                p->sky_light, p->block_light)) {
            fprintf(stderr, "dig particle %d rejected\n", i);
            free(meta);
            return 1;
        }
    }

    float px = 8.5f, py = 5.0f, pz = 8.5f, yaw = 0.0f, pitch = 10.0f;
    const char *pose_sec = strstr(meta, "\"pose\"");
    if (pose_sec) {
        px = j_float(pose_sec, "x", px);
        py = j_float(pose_sec, "y", py);
        pz = j_float(pose_sec, "z", pz);
        yaw = j_float(pose_sec, "yaw", yaw);
        pitch = j_float(pose_sec, "pitch", pitch);
    }
    if (!isnan(pose_pitch_override)) pitch = pose_pitch_override;

    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir || !tmpdir[0]) tmpdir = ".";
    char outdir[1024];
    snprintf(outdir, sizeof outdir, "%s/magma_entity_oracle_frames_%ld",
             tmpdir, (long)getpid());
    if (mkdir(outdir, 0775) != 0 && errno != EEXIST) {
        fprintf(stderr, "mkdir %s failed\n", outdir);
        free(meta);
        return 1;
    }

    if (raster_probe && cr_cfg_set("raster_probe", raster_probe) != 0) {
        fprintf(stderr, "invalid --raster-probe value\n");
        free(meta);
        return 1;
    }

    GmConfig cfg;
    gm_config_defaults(&cfg);
    cfg.seed = 0;
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.backend = GM_BACKEND_CPU;
    cfg.render = GM_RENDER_OFF;
    cfg.width = W;
    cfg.height = H;
    cfg.view_distance = 8;
    cfg.daylight = 0;
    cfg.weather = 0;
    cfg.brewing = 1;
    cfg.enchanting = 1;
    cfg.mobs = 0;
    cfg.headless = 1;
    cfg.frames_out_dir = outdir;
    cfg.frame_every = 1;
    cfg.frame_offset = 0;

    char err[256];
    GmRuntime rt;
    /* game_main enables shade-time lightmaps by default. The fixture bypasses
     * game_main, so opt in explicitly before any chunk is meshed. */
    worldmc_set_lightmap_mode(1);
    if (!gm_runtime_init(&rt, &cfg, err, (int)sizeof err)) {
        fprintf(stderr, "runtime: %s\n", err); free(meta); return 1;
    }
    place_pad(rt.world, 4);
    if (!strcmp(state, "skeleton_trap_group")
            || !strcmp(state, "skeleton_trap_group_background")) {
        gm_world_set_block(rt.world, 10, 5, 11, 0);
        gm_world_set_block(rt.world, 11, 5, 11, 0);
    }
    gm_world_ensure(rt.world, 0, 0, 4);
    /* Dragon states need a wider mesh window around z=-40 / y=70. */
    if (!strncmp(state, "dragon_death_", 13)
            || !strcmp(state, "dragon_background")) {
        for (int cx = -4; cx <= 2; ++cx)
            for (int cz = -4; cz <= 2; ++cz)
                gm_world_ensure(rt.world, cx, cz, 2);
        for (int x = -8; x <= 8; ++x)
            for (int z = -50; z <= 20; ++z)
                gm_world_set_block(rt.world, x, 60, z, 121); /* end_stone */
    }
    gm_runtime_set_time(&rt, 6000);
    rt.clock.freeze_daylight = 1;

    gm_runtime_set_pose(&rt, px, py, pz, yaw, pitch);
    rt.player.ent.onGround = 1;
    /* gm_runtime_set_pose rebases the finite native window and keeps the
     * player/entity view in absolute world coordinates through ox/oz.  Do not
     * erase that origin: the dragon fixture camera is at z=-40.5, outside the
     * origin chunk, and clearing oz silently moves its render to z=7.5. */
    /* Pin texture animations + creative (no hunger flash). */
    gm_runtime_tape_player_view(&rt, 0, 0.0f, 300, 0.0f, 0, 0, 0,
                                1 /* texture_animations_pinned */,
                                0, 1 /* creative */, 0, 0, 0.0f, 1.0f);

    /* The oracle pair records the complete composed frame with hideGUI=false.
     * Keep HUD, crosshair, and the pinned first-person hand in the candidate;
     * the hard gate owns the complete family ROI, not only entity-colored
     * pixels. */
    int clean_world_render = j_bool(meta, "clean_world_render", 0);
    cr_cfg_set("hide_gui", clean_world_render ? "1" : "0");
    cr_cfg_set("strip_overlays", clean_world_render ? "1" : "0");
    cr_cfg_set("no_hand", clean_world_render ? "1" : "0");
    for (int i = 0; i < nset_args; ++i) {
        const char *eq = strchr(set_args[i], '=');
        char key[64];
        if (!eq || eq == set_args[i]
                || (size_t)(eq - set_args[i]) >= sizeof key) {
            fprintf(stderr, "bad --set (expected key=value): %s\n", set_args[i]);
            free(meta); gm_runtime_destroy(&rt); return 2;
        }
        memcpy(key, set_args[i], (size_t)(eq - set_args[i]));
        key[eq - set_args[i]] = '\0';
        if (cr_cfg_set(key, eq + 1) != 0) {
            fprintf(stderr, "bad --set key/value: %s\n", set_args[i]);
            free(meta); gm_runtime_destroy(&rt); return 2;
        }
    }

    if (!strcmp(state, "gui_crafting_table")) {
        const int tile_x = 10, tile_y = 5, tile_z = 8;
        if (!gm_runtime_set_block(&rt, tile_x, tile_y, tile_z, 58, 0)
                || !gm_runtime_use_block(&rt, tile_x, tile_y, tile_z)
                || rt.container != 1) {
            fprintf(stderr, "Crafting Table GUI fixture setup failed\n");
            free(meta); gm_runtime_destroy(&rt); return 1;
        }
        rt.craft_grid[0] = (ICStack){.item=369, .count=3, .meta=0};
        ICStack result = gm_container_result(&rt);
        if (result.item != 377 || result.count != 2 || result.meta != 0
                || gm_hud_init() != 0) {
            fprintf(stderr, "Crafting Table real recipe result failed\n");
            free(meta); gm_runtime_destroy(&rt); return 1;
        }
        CrFramebuffer fb;
        cr_fb_alloc(&fb, W, H);
        cr_fb_clear(&fb, (CrRgba){40, 40, 40, 255});
        gm_screen_draw(&fb, &rt, 220, 200);
        if (!write_framebuffer_ppm(ppm, &fb)) {
            fprintf(stderr, "write %s failed\n", ppm);
            cr_fb_free(&fb); free(meta); gm_runtime_destroy(&rt);
            return 1;
        }
        cr_fb_free(&fb);
        free(meta);
        gm_runtime_destroy(&rt);
        return 0;
    }

    if (!strcmp(state, "gui_anvil")) {
        const int tile_x = 10, tile_y = 5, tile_z = 8;
        rt.tape_creative = 0;
        rt.player_xp_level = 30;
        if (!gm_runtime_set_block(&rt, tile_x, tile_y - 1, tile_z, 1, 0)
                || !gm_runtime_set_block(
                    &rt, tile_x, tile_y, tile_z, 145, 0)
                || !gm_runtime_use_block(&rt, tile_x, tile_y, tile_z)
                || rt.container != 6 || !rt.anvil.open) {
            fprintf(stderr, "Anvil GUI fixture setup failed\n");
            free(meta); gm_runtime_destroy(&rt); return 1;
        }
        rt.anvil.slots[0] = (ICStack){.item=369, .count=1, .meta=0};
        if (!gm_runtime_anvil_set_name(&rt, "Oracle")
                || rt.anvil.maximum_cost != 1
                || rt.anvil.material_cost != 0
                || rt.anvil.slots[2].item != 369
                || rt.anvil.slots[2].count != 1
                || gm_hud_init() != 0) {
            fprintf(stderr, "Anvil real rename result/cost failed\n");
            free(meta); gm_runtime_destroy(&rt); return 1;
        }
        CrFramebuffer fb;
        cr_fb_alloc(&fb, W, H);
        cr_fb_clear(&fb, (CrRgba){40, 40, 40, 255});
        gm_screen_draw(&fb, &rt, 220, 200);
        if (!write_framebuffer_ppm(ppm, &fb)) {
            fprintf(stderr, "write %s failed\n", ppm);
            cr_fb_free(&fb); free(meta); gm_runtime_destroy(&rt);
            return 1;
        }
        cr_fb_free(&fb);
        free(meta);
        gm_runtime_destroy(&rt);
        return 0;
    }

    if (!strcmp(state, "gui_merchant")) {
        const int eid = 42;
        int ok = gm_runtime_spawn_villager_fixture(
                &rt, eid, 8.5, 5.0, 9.5,
                0.0, 0.0, 0.0, 180.0f, 20.0f,
                0, 0, 0, 0, 0, 42, 0, 0.0)
            && gm_runtime_restore_villager_trade(
                &rt, eid, 1, 1, 0, 0, 2)
            && gm_runtime_restore_villager_offer(
                &rt, eid, 0, 0, 7, 1)
            && gm_runtime_restore_villager_offer_stack(
                &rt, eid, 0, 0, (ICStack){.item=388, .count=3})
            && gm_runtime_restore_villager_offer_stack(
                &rt, eid, 0, 1, (ICStack){.item=264, .count=1})
            && gm_runtime_restore_villager_offer_stack(
                &rt, eid, 0, 2, (ICStack){.item=369, .count=2})
            && gm_runtime_restore_villager_offer(
                &rt, eid, 1, 0, 7, 1)
            && gm_runtime_restore_villager_offer_stack(
                &rt, eid, 1, 0, (ICStack){.item=263, .count=5})
            && gm_runtime_restore_villager_offer_stack(
                &rt, eid, 1, 1, (ICStack){0})
            && gm_runtime_restore_villager_offer_stack(
                &rt, eid, 1, 2, (ICStack){.item=265, .count=1})
            && gm_runtime_open_villager(&rt, eid);
        if (ok) {
            rt.merchant_slots[0] = (ICStack){.item=388, .count=3};
            rt.merchant_slots[1] = (ICStack){.item=264, .count=1};
            gm_runtime_merchant_refresh(&rt);
            ok = rt.container == 7 && rt.merchant_selected == 0
                && rt.merchant_slots[2].item == 369
                && rt.merchant_slots[2].count == 2
                && gm_hud_init() == 0;
        }
        if (!ok) {
            fprintf(stderr, "Merchant selected trade/result setup failed\n");
            free(meta); gm_runtime_destroy(&rt); return 1;
        }
        CrFramebuffer fb;
        cr_fb_alloc(&fb, W, H);
        cr_fb_clear(&fb, (CrRgba){40, 40, 40, 255});
        gm_screen_draw(&fb, &rt, 220, 200);
        if (!write_framebuffer_ppm(ppm, &fb)) {
            fprintf(stderr, "write %s failed\n", ppm);
            cr_fb_free(&fb); free(meta); gm_runtime_destroy(&rt);
            return 1;
        }
        cr_fb_free(&fb);
        free(meta);
        gm_runtime_destroy(&rt);
        return 0;
    }

    if (!strcmp(state, "gui_enchanting")) {
        const int tile_x = 8, tile_y = 5, tile_z = 8;
        int ok = gm_runtime_set_block(
            &rt, tile_x, tile_y, tile_z, 116, 0);
        for (int dx = -2; ok && dx <= 2; ++dx) {
            ok = gm_runtime_set_block(
                    &rt, tile_x + dx, tile_y, tile_z - 2, 47, 0)
                && gm_runtime_set_block(
                    &rt, tile_x + dx, tile_y, tile_z + 2, 47, 0);
        }
        for (int dz = -1; ok && dz <= 1; ++dz)
            ok = gm_runtime_set_block(
                &rt, tile_x - 2, tile_y, tile_z + dz, 47, 0);
        ok = ok
            && gm_runtime_set_block(
                &rt, tile_x + 2, tile_y, tile_z - 1, 47, 0)
            && gm_runtime_set_block(
                &rt, tile_x + 2, tile_y, tile_z + 1, 47, 0);
        gm_runtime_set_pose(
            &rt, tile_x + 0.5, tile_y + 0.75, tile_z - 1.5,
            180.0f, 0.0f);
        rt.mobs.xp_total = 1395;
        ok = ok && gm_runtime_use_block(
                &rt, tile_x, tile_y, tile_z)
            && rt.container == 5 && rt.enchanting.open;
        if (ok) {
            enchanting_live_set_slot(
                &rt.enchanting, rt.world, 0,
                (ICStack){.item=276, .count=1, .meta=0});
            enchanting_live_set_slot(
                &rt.enchanting, rt.world, 1,
                (ICStack){.item=351, .count=12, .meta=4});
            rt.player_xp_level = 30;
            ok = rt.enchanting.power == 15
                && rt.enchanting.xp_seed == 0
                && rt.enchanting.offer.levels[0] == 8
                && rt.enchanting.offer.levels[1] == 13
                && rt.enchanting.offer.levels[2] == 30
                && rt.enchanting.offer.clue_id[0] == 34
                && rt.enchanting.offer.clue_id[1] == 17
                && rt.enchanting.offer.clue_id[2] == 21
                && rt.enchanting.offer.clue_lvl[0] == 1
                && rt.enchanting.offer.clue_lvl[1] == 2
                && rt.enchanting.offer.clue_lvl[2] == 2
                && gm_hud_init() == 0;
        }
        if (!ok) {
            fprintf(stderr, "Enchanting verified offer fixture failed\n");
            free(meta); gm_runtime_destroy(&rt); return 1;
        }
        CrFramebuffer fb;
        cr_fb_alloc(&fb, W, H);
        cr_fb_clear(&fb, (CrRgba){40, 40, 40, 255});
        gm_screen_draw(&fb, &rt, 220, 200);
        if (!write_framebuffer_ppm(ppm, &fb)) {
            fprintf(stderr, "write %s failed\n", ppm);
            cr_fb_free(&fb); free(meta); gm_runtime_destroy(&rt);
            return 1;
        }
        cr_fb_free(&fb);
        free(meta);
        gm_runtime_destroy(&rt);
        return 0;
    }

    if (!strcmp(state, "gui_dispenser")
            || !strcmp(state, "gui_dropper")
            || !strcmp(state, "gui_hopper")) {
        const int tile_x = 10, tile_y = 5, tile_z = 8;
        int hopper = !strcmp(state, "gui_hopper");
        int block = hopper ? 154
            : !strcmp(state, "gui_dropper") ? 158 : 23;
        int kind = hopper ? 14 : 13;
        if (!gm_runtime_set_block(&rt, tile_x, tile_y, tile_z, block, 2)
                || !gm_runtime_static_container_set_slot(
                    &rt, 0, tile_x, tile_y, tile_z,
                    0, 264, 5, 0)
                || !gm_runtime_static_container_set_slot(
                    &rt, 0, tile_x, tile_y, tile_z,
                    1, 297, 2, 0)
                || !gm_runtime_use_block(&rt, tile_x, tile_y, tile_z)
                || rt.container != kind
                || rt.active_static_container < 0
                || gm_hud_init() != 0) {
            fprintf(stderr, "static-container GUI fixture setup failed\n");
            free(meta); gm_runtime_destroy(&rt); return 1;
        }
        CrFramebuffer fb;
        cr_fb_alloc(&fb, W, H);
        cr_fb_clear(&fb, (CrRgba){40, 40, 40, 255});
        gm_screen_draw(&fb, &rt, 352, 124);
        if (!write_framebuffer_ppm(ppm, &fb)) {
            fprintf(stderr, "write %s failed\n", ppm);
            cr_fb_free(&fb); free(meta); gm_runtime_destroy(&rt);
            return 1;
        }
        cr_fb_free(&fb);
        free(meta);
        gm_runtime_destroy(&rt);
        return 0;
    }

    if (!strcmp(state, "gui_furnace")
            || !strcmp(state, "gui_brewing_stand")) {
        const int tile_x = 10, tile_y = 5, tile_z = 8;
        int brewing = !strcmp(state, "gui_brewing_stand");
        int ok = gm_runtime_set_block(
            &rt, tile_x, tile_y, tile_z, brewing ? 117 : 61, 2);
        if (ok && brewing) {
            ok = gm_runtime_brewing_set_slot(
                    &rt, 0, tile_x, tile_y, tile_z,
                    0, 373, 1, 0, 200, 10)
                && gm_runtime_brewing_set_slot(
                    &rt, 0, tile_x, tile_y, tile_z,
                    1, 373, 1, 0, 200, 10)
                && gm_runtime_brewing_set_slot(
                    &rt, 0, tile_x, tile_y, tile_z,
                    2, 373, 1, 0, 200, 10)
                && gm_runtime_brewing_set_slot(
                    &rt, 0, tile_x, tile_y, tile_z,
                    3, 372, 2, 0, 200, 10)
                && gm_runtime_brewing_set_slot(
                    &rt, 0, tile_x, tile_y, tile_z,
                    4, 377, 3, 0, 200, 10);
        } else if (ok) {
            ok = gm_runtime_furnace_set_slot(
                    &rt, 0, tile_x, tile_y, tile_z,
                    0, 15, 5, 0, 800, 1600, 100, 200)
                && gm_runtime_furnace_set_slot(
                    &rt, 0, tile_x, tile_y, tile_z,
                    1, 263, 2, 0, 800, 1600, 100, 200)
                && gm_runtime_furnace_set_slot(
                    &rt, 0, tile_x, tile_y, tile_z,
                    2, 265, 3, 0, 800, 1600, 100, 200);
        }
        ok = ok && gm_runtime_use_block(&rt, tile_x, tile_y, tile_z)
            && rt.container == (brewing ? 4 : 2)
            && gm_hud_init() == 0;
        if (!ok) {
            fprintf(stderr, "processing-container GUI fixture setup failed\n");
            free(meta); gm_runtime_destroy(&rt); return 1;
        }
        CrFramebuffer fb;
        cr_fb_alloc(&fb, W, H);
        cr_fb_clear(&fb, (CrRgba){40, 40, 40, 255});
        gm_screen_draw(&fb, &rt, 352, 124);
        if (!write_framebuffer_ppm(ppm, &fb)) {
            fprintf(stderr, "write %s failed\n", ppm);
            cr_fb_free(&fb); free(meta); gm_runtime_destroy(&rt);
            return 1;
        }
        cr_fb_free(&fb);
        free(meta);
        gm_runtime_destroy(&rt);
        return 0;
    }

    if (!strcmp(state, "gui_ender_chest")) {
        rt.container = 3;
        rt.active_chest = GM_ACTIVE_ENDER_CHEST;
        if (!gm_runtime_ender_chest_set_slot(
                    &rt, 0, (ICStack){.item=264, .count=5, .meta=0})
                || !gm_runtime_ender_chest_set_slot(
                    &rt, 1, (ICStack){.item=297, .count=2, .meta=0})
                || gm_hud_init() != 0) {
            fprintf(stderr, "Ender Chest GUI fixture setup failed\n");
            free(meta); gm_runtime_destroy(&rt); return 1;
        }
        CrFramebuffer fb;
        cr_fb_alloc(&fb, W, H);
        cr_fb_clear(&fb, (CrRgba){40, 40, 40, 255});
        gm_screen_draw(&fb, &rt, 352, 124);
        if (!write_framebuffer_ppm(ppm, &fb)) {
            fprintf(stderr, "write %s failed\n", ppm);
            cr_fb_free(&fb); free(meta); gm_runtime_destroy(&rt);
            return 1;
        }
        cr_fb_free(&fb);
        free(meta);
        gm_runtime_destroy(&rt);
        return 0;
    }

    if (!strcmp(state, "gui_large_chest")) {
        const int left_x = 10, chest_y = 5, chest_z = 8;
        if (!gm_runtime_set_block(&rt, left_x, chest_y, chest_z, 54, 2)
                || !gm_runtime_set_block(
                    &rt, left_x + 1, chest_y, chest_z, 54, 2)
                || !gm_runtime_chest_set_slot(
                    &rt, 0, left_x, chest_y, chest_z,
                    0, 264, 5, 0)
                || !gm_runtime_chest_set_slot(
                    &rt, 0, left_x + 1, chest_y, chest_z,
                    0, 297, 2, 0)
                /* Click the east half. The screen must still expose the
                 * west half first, matching InventoryLargeChest. */
                || !gm_runtime_use_block(
                    &rt, left_x + 1, chest_y, chest_z)
                || rt.container != 10
                || rt.active_chest < 0 || rt.active_chest_pair < 0
                || rt.chests[rt.active_chest].wx != left_x
                || rt.chests[rt.active_chest_pair].wx != left_x + 1
                || gm_hud_init() != 0) {
            fprintf(stderr, "Large Chest GUI fixture setup failed\n");
            free(meta); gm_runtime_destroy(&rt); return 1;
        }
        CrFramebuffer fb;
        cr_fb_alloc(&fb, W, H);
        cr_fb_clear(&fb, (CrRgba){40, 40, 40, 255});
        gm_screen_draw(&fb, &rt, 352, 124);
        if (!write_framebuffer_ppm(ppm, &fb)) {
            fprintf(stderr, "write %s failed\n", ppm);
            cr_fb_free(&fb); free(meta); gm_runtime_destroy(&rt);
            return 1;
        }
        cr_fb_free(&fb);
        free(meta);
        gm_runtime_destroy(&rt);
        return 0;
    }

    if (!strcmp(state, "gui_beacon")) {
        const int beacon_x = 10, beacon_y = 5, beacon_z = 8;
        if (!gm_runtime_set_block(
                    &rt, beacon_x, beacon_y, beacon_z, 138, 0)
                || !gm_runtime_beacon_set_state(
                    &rt, 0, beacon_x, beacon_y, beacon_z,
                    4, 5, 5, 1)
                || !gm_runtime_static_container_set_slot(
                    &rt, 0, beacon_x, beacon_y, beacon_z,
                    0, 264, 1, 0)
                || !gm_runtime_use_block(
                    &rt, beacon_x, beacon_y, beacon_z)
                || rt.container != 11
                || gm_hud_init() != 0) {
            fprintf(stderr, "Beacon GUI fixture setup failed\n");
            free(meta); gm_runtime_destroy(&rt); return 1;
        }
        CrFramebuffer fb;
        cr_fb_alloc(&fb, W, H);
        cr_fb_clear(&fb, (CrRgba){40, 40, 40, 255});
        gm_screen_draw(&fb, &rt, 220, 200);
        if (!write_framebuffer_ppm(ppm, &fb)) {
            fprintf(stderr, "write %s failed\n", ppm);
            cr_fb_free(&fb); free(meta); gm_runtime_destroy(&rt);
            return 1;
        }
        cr_fb_free(&fb);
        free(meta);
        gm_runtime_destroy(&rt);
        return 0;
    }

    if (!strcmp(state, "gui_shulker_box")) {
        const int box_x = 10, box_y = 5, box_z = 8;
        if (!gm_runtime_set_block(&rt, box_x, box_y, box_z, 229, 1)
                || !gm_runtime_static_container_set_slot(
                    &rt, 0, box_x, box_y, box_z,
                    0, 264, 5, 0)
                || !gm_runtime_static_container_set_slot(
                    &rt, 0, box_x, box_y, box_z,
                    1, 297, 2, 0)
                || !gm_runtime_use_block(&rt, box_x, box_y, box_z)
                || rt.container != 9
                || rt.active_chest != GM_ACTIVE_SHULKER_BOX
                || gm_hud_init() != 0) {
            fprintf(stderr, "Shulker Box GUI fixture setup failed\n");
            free(meta); gm_runtime_destroy(&rt); return 1;
        }
        CrFramebuffer fb;
        cr_fb_alloc(&fb, W, H);
        cr_fb_clear(&fb, (CrRgba){40, 40, 40, 255});
        gm_screen_draw(&fb, &rt, 352, 124);
        if (!write_framebuffer_ppm(ppm, &fb)) {
            fprintf(stderr, "write %s failed\n", ppm);
            cr_fb_free(&fb); free(meta); gm_runtime_destroy(&rt);
            return 1;
        }
        cr_fb_free(&fb);
        free(meta);
        gm_runtime_destroy(&rt);
        return 0;
    }

    if (!strncmp(state, "gui_horse_", 10)) {
        int donkey = !strcmp(state, "gui_horse_donkey_chest");
        int llama = !strcmp(state, "gui_horse_llama_chest");
        int type = donkey ? GM_MOB_DONKEY
                 : llama ? GM_MOB_LLAMA : GM_MOB_HORSE;
        int status = GM_HORSE_TAME | GM_HORSE_SADDLED;
        int eid = 42;
        int spawned = llama
            ? gm_runtime_spawn_llama_fixture(
                &rt, eid, 8.5, 5.0, 8.5, 0.0, 0.0, 0.0,
                0.0f, 20.0f, 1, 20.0, 0.175, 0.5, 0,
                GM_HORSE_TAME, 20, 2, 3, 4, 1, 0, 0, 0, 0, 0)
            : gm_runtime_spawn_horse_fixture(
                &rt, type, eid, 8.5, 5.0, 8.5, 0.0, 0.0, 0.0,
                0.0f, 20.0f, 1, 20.0, 0.225, 0.7, 0,
                status, 100, donkey ? 0 : (6 | (4 << 8)),
                donkey ? 0 : 3, donkey, 0, 0, 0, 0, 0);
        if (!spawned) {
            fprintf(stderr, "horse GUI fixture spawn failed\n");
            free(meta); gm_runtime_destroy(&rt); return 1;
        }
        if (!llama)
            gm_runtime_set_horse_inventory(
                &rt, eid, 0, (ICStack){.item=329, .count=1, .meta=0});
        if (llama) {
            gm_runtime_set_horse_inventory(
                &rt, eid, 1, (ICStack){.item=171, .count=1, .meta=4});
            gm_runtime_set_horse_inventory(
                &rt, eid, 2, (ICStack){.item=264, .count=3, .meta=0});
            gm_runtime_set_horse_inventory(
                &rt, eid, 3, (ICStack){.item=297, .count=5, .meta=0});
        } else if (!donkey) {
            gm_runtime_set_horse_inventory(
                &rt, eid, 1, (ICStack){.item=419, .count=1, .meta=0});
        } else {
            gm_runtime_set_horse_inventory(
                &rt, eid, 2, (ICStack){.item=264, .count=3, .meta=0});
            gm_runtime_set_horse_inventory(
                &rt, eid, 3, (ICStack){.item=297, .count=5, .meta=0});
        }
        if (!gm_runtime_open_horse_inventory(&rt, eid) || gm_hud_init() != 0) {
            fprintf(stderr, "horse-family GUI open/init failed\n");
            free(meta); gm_runtime_destroy(&rt); return 1;
        }
        CrFramebuffer fb;
        cr_fb_alloc(&fb, W, H);
        cr_fb_clear(&fb, (CrRgba){40, 40, 40, 255});
        gm_screen_draw(&fb, &rt, 352, 124);
        if (!write_framebuffer_ppm(ppm, &fb)) {
            fprintf(stderr, "horse-family GUI write failed: %s\n", ppm);
            cr_fb_free(&fb); free(meta); gm_runtime_destroy(&rt); return 1;
        }
        cr_fb_free(&fb);
        printf("wrote %s (state=%s fixed-gui-mouse=176,62)\n", ppm, state);
        free(meta);
        gm_runtime_destroy(&rt);
        return 0;
    }

    if (!strcmp(state, "ender_chest_closed")
            || !strcmp(state, "ender_chest_open")) {
        const int chest_x = 8, chest_y = 5, chest_z = 12;
        float lid = !strcmp(state, "ender_chest_open") ? 1.0f : 0.0f;
        if (!gm_runtime_set_block(
                &rt, chest_x, chest_y, chest_z, 130, 2)) {
            fprintf(stderr, "Ender Chest world fixture placement failed\n");
            free(meta); gm_runtime_destroy(&rt); return 1;
        }
        int found = 0;
        for (int i = 0; i < rt.ender_chests_cap; ++i) {
            GmRuntimeEnderChest *tile = &rt.ender_chests[i];
            if (!tile->active || tile->dimension != rt.dimension
                    || tile->wx != chest_x || tile->wy != chest_y
                    || tile->wz != chest_z)
                continue;
            tile->lid_angle = lid;
            tile->prev_lid_angle = lid;
            found = 1;
            break;
        }
        if (!found) {
            fprintf(stderr, "Ender Chest world fixture tile absent\n");
            free(meta); gm_runtime_destroy(&rt); return 1;
        }
    }

    if (!strcmp(state, "chest_normal_closed")
            || !strcmp(state, "chest_normal_open")
            || !strcmp(state, "chest_trapped_closed")
            || !strcmp(state, "chest_trapped_open")
            || !strcmp(state, "chest_normal_double_x_open")
            || !strcmp(state, "chest_trapped_double_z_open")) {
        const int chest_x = 8, chest_y = 5, chest_z = 12;
        int trapped = strstr(state, "trapped") != NULL;
        int double_x = strstr(state, "double_x") != NULL;
        int double_z = strstr(state, "double_z") != NULL;
        int block = trapped ? 146 : 54;
        int meta_value = double_z ? 5 : 2;
        float lid = strstr(state, "open") != NULL ? 1.0f : 0.0f;
        int positions[2][3] = {
            {chest_x, chest_y, chest_z},
            {chest_x + double_x, chest_y, chest_z + double_z},
        };
        int count = (double_x || double_z) ? 2 : 1;
        for (int p = 0; p < count; ++p)
            if (!gm_runtime_set_block(
                    &rt, positions[p][0], positions[p][1], positions[p][2],
                    block, meta_value)) {
                fprintf(stderr, "wooden Chest world fixture placement failed\n");
                free(meta); gm_runtime_destroy(&rt); return 1;
            }
        int found = 0;
        for (int i = 0; i < rt.chests_cap; ++i) {
            GmRuntimeChest *tile = &rt.chests[i];
            if (!tile->active) continue;
            for (int p = 0; p < count; ++p) {
                if (tile->wx != positions[p][0]
                        || tile->wy != positions[p][1]
                        || tile->wz != positions[p][2])
                    continue;
                tile->state.te.lid_angle = lid;
                tile->state.te.prev_lid_angle = lid;
                ++found;
            }
        }
        if (found != count) {
            fprintf(stderr,
                    "wooden Chest world fixture tiles absent (%d/%d)\n",
                    found, count);
            free(meta); gm_runtime_destroy(&rt); return 1;
        }
    }

    if (!strncmp(state, "shulker_box_", 12)
            && strcmp(state, "shulker_box_background")) {
        const int box_x = 8, box_y = 5, box_z = 12;
        int block = 219;
        int meta_value = 1;
        float progress = 1.0f;
        if (strstr(state, "orange_down")) {
            block = 220; meta_value = 0;
        } else if (strstr(state, "purple_north")) {
            block = 229; meta_value = 2; progress = 0.5f;
        } else if (strstr(state, "blue_south")) {
            block = 230; meta_value = 3;
        } else if (strstr(state, "red_west")) {
            block = 233; meta_value = 4;
        } else if (strstr(state, "black_east")) {
            block = 234; meta_value = 5;
        } else if (strstr(state, "closed")) {
            progress = 0.0f;
        }
        if (!gm_runtime_set_block(
                &rt, box_x, box_y, box_z, block, meta_value)
                || !gm_runtime_static_container_set_slot(
                    &rt, 0, box_x, box_y, box_z, 0, 0, 0, 0)) {
            fprintf(stderr, "Shulker Box world fixture placement failed\n");
            free(meta); gm_runtime_destroy(&rt); return 1;
        }
        int found = 0;
        for (int i = 0; i < rt.static_containers_cap; ++i) {
            GmRuntimeStaticContainer *tile = &rt.static_containers[i];
            if (!tile->active || tile->dimension != rt.dimension
                    || tile->wx != box_x || tile->wy != box_y
                    || tile->wz != box_z)
                continue;
            tile->shulker_progress = progress;
            tile->shulker_progress_old = progress;
            tile->shulker_animation_status = progress <= 0.0f
                ? GM_SHULKER_BOX_CLOSED : progress >= 1.0f
                    ? GM_SHULKER_BOX_OPENED : GM_SHULKER_BOX_OPENING;
            found = 1;
            break;
        }
        if (!found) {
            fprintf(stderr, "Shulker Box world fixture tile absent\n");
            free(meta); gm_runtime_destroy(&rt); return 1;
        }
    }

    if (!strcmp(state, "beacon_world_colored")) {
        const int beacon_x = 8, beacon_y = 6, beacon_z = 12;
        int placed = 1;
        for (int x = beacon_x - 1; x <= beacon_x + 1; ++x)
            for (int z = beacon_z - 1; z <= beacon_z + 1; ++z)
                placed = placed && gm_runtime_set_block(
                    &rt, x, beacon_y - 1, z, 42, 0);
        placed = placed
            && gm_runtime_set_block(
                &rt, beacon_x, beacon_y, beacon_z, 138, 0)
            && gm_runtime_set_block(
                &rt, beacon_x, beacon_y + 1, beacon_z, 95, 14)
            && gm_runtime_set_block(
                &rt, beacon_x, beacon_y + 2, beacon_z, 95, 11)
            && gm_runtime_beacon_set_state(
                &rt, 0, beacon_x, beacon_y, beacon_z, -1, 0, 0, 0)
            && gm_runtime_beacon_update(
                &rt, 0, beacon_x, beacon_y, beacon_z);
        if (!placed) {
            fprintf(stderr, "Beacon world fixture setup failed\n");
            free(meta); gm_runtime_destroy(&rt); return 1;
        }
        gm_runtime_set_total_time(
            &rt, (long long)j_int(meta, "total_time", 0));
        int found = 0;
        for (int i = 0; i < rt.static_containers_cap; ++i) {
            GmRuntimeStaticContainer *tile = &rt.static_containers[i];
            if (!tile->active || tile->dimension != rt.dimension
                    || tile->wx != beacon_x || tile->wy != beacon_y
                    || tile->wz != beacon_z || tile->block != 138)
                continue;
            /* Java frame_pair pins 0.975 immediately before each render;
             * shouldBeamRender adds the final 0.025 inside the real TESR. */
            tile->beacon_render_scale = 0.975F;
            tile->beacon_render_counter = rt.clock.total_time;
            found = 1;
            break;
        }
        if (!found) {
            fprintf(stderr, "Beacon world fixture tile absent\n");
            free(meta); gm_runtime_destroy(&rt); return 1;
        }
    }

    if (!strcmp(state, "spawner_pig_saved")
            || !strcmp(state, "spawner_zombie_noai_saved")) {
        static const unsigned char pig_nbt[] = {
            10, 0, 0,
            8, 0, 2, 'i', 'd', 0, 13,
            'm', 'i', 'n', 'e', 'c', 'r', 'a', 'f', 't', ':', 'p', 'i', 'g',
            0,
        };
        static const unsigned char zombie_noai_nbt[] = {
            10, 0, 0,
            8, 0, 2, 'i', 'd', 0, 16,
            'm', 'i', 'n', 'e', 'c', 'r', 'a', 'f', 't', ':',
            'z', 'o', 'm', 'b', 'i', 'e',
            1, 0, 4, 'N', 'o', 'A', 'I', 1,
            0,
        };
        const int spawner_x = 8, spawner_y = 5, spawner_z = 12;
        const int zombie = !strcmp(state, "spawner_zombie_noai_saved");
        const unsigned char *entity_nbt = zombie
            ? zombie_noai_nbt : pig_nbt;
        size_t entity_nbt_len = zombie
            ? sizeof zombie_noai_nbt : sizeof pig_nbt;
        if (!gm_runtime_set_block(
                &rt, spawner_x, spawner_y, spawner_z, 52, 0)
                || !gm_runtime_spawner_set_state(
                    &rt, spawner_x, spawner_y, spawner_z,
                    zombie ? 2 : 11,
                    200, 200, 800, 4, 6, 0, 4,
                    entity_nbt, entity_nbt_len, zombie ? 0 : 1)) {
            fprintf(stderr, "Spawner world fixture setup failed\n");
            free(meta); gm_runtime_destroy(&rt); return 1;
        }
        int found = 0;
        for (int i = 0; i < GM_SPAWNERS; ++i) {
            GmSpawnerTE *tile = &rt.mobs.spawners[i];
            if (!tile->active || tile->dimension != rt.dimension
                    || tile->x != spawner_x || tile->y != spawner_y
                    || tile->z != spawner_z)
                continue;
            tile->mob_rotation = 0.0;
            tile->prev_mob_rotation = 0.0;
            found = 1;
            break;
        }
        if (!found) {
            fprintf(stderr, "Spawner world fixture tile absent\n");
            free(meta); gm_runtime_destroy(&rt); return 1;
        }
    }

    /* Normal gallery frames are recorded with hideGUI=false. Initialize the
     * HUD atlas before frame_capture; without it gm_hud_draw silently no-ops
     * and the strict ROI reports the entire hotbar as missing. Clean subject
     * fixtures explicitly set hide_gui above and remain HUD-free. */
    if (gm_hud_init() != 0) {
        fprintf(stderr, "HUD fixture initialization failed\n");
        free(meta); gm_runtime_destroy(&rt); return 1;
    }

    inject_from_meta(&rt, state, meta, entity_dx, entity_dy, entity_dz);

    GmFrameCapture *fc = gm_frame_capture_open(&cfg, err, (int)sizeof err);
    if (!fc) {
        fprintf(stderr, "frame_capture_open: %s\n", err);
        free(meta); gm_runtime_destroy(&rt); return 1;
    }
    gm_frame_capture_set_boss_fog(fc, j_int(meta, "boss_fog", -1));
    if (strstr(state, "wither_") == state
            && !strstr(state, "wither_skull")
            && strcmp(state, "wither_empty")) {
        /* entity_pin changes the client model's health only. The server's
         * BossInfo packet remains full in all canonical Wither captures. */
        gm_frame_capture_set_boss_fraction(fc, 1.0f);
    }
    GmParticlesLive exact_particles;
    if (nparticle_args > 0) {
        gm_particles_live_init(&exact_particles, UINT64_C(0x686f72736570636c));
        for (int i = 0; i < nparticle_args; ++i) {
            const ParticleArg *p = &particle_args[i];
            int bx = (int)floor(p->x), by = (int)floor(p->y);
            int bz = (int)floor(p->z);
            if (!gm_particles_live_spawn_recorded_state(
                    &exact_particles, p->id,
                    p->prev_x, p->prev_y, p->prev_z,
                    p->x, p->y, p->z, p->vx, p->vy, p->vz,
                    p->age, p->max_age, p->on_ground, p->scale,
                    p->color_r, p->color_g, p->color_b,
                    p->tex, p->tex_base,
                    gm_world_sky_light(rt.world, bx, by, bz),
                    gm_world_block_light(rt.world, bx, by, bz))) {
                fprintf(stderr, "particle %d rejected\n", i);
                gm_frame_capture_close(fc); free(meta);
                gm_runtime_destroy(&rt); return 1;
            }
        }
        gm_frame_capture_bind_particles(fc, &exact_particles);
    }
    GmAction act;
    memset(&act, 0, sizeof act);
    /* The Java fixture pins EntityRenderer's Wither boss-color ramp at 1.0.
     * Advance the native per-tick scalar to the same defined phase without
     * rendering intermediary frames. */
    int prewarm = strstr(state, "wither_") == state
        && !strstr(state, "wither_skull")
        && strcmp(state, "wither_empty") ? 19 : 0;
    if (prewarm)
        for (int i = 0; i < 19; ++i)
            if (!gm_frame_capture_write(fc, &rt, &act, 0,
                                        err, (int)sizeof err)) {
                fprintf(stderr, "frame_capture prewarm: %s\n", err);
                gm_frame_capture_close(fc); free(meta);
                gm_runtime_destroy(&rt); return 1;
            }
    /* Re-inject ghosts immediately before write (no tick clears them). */
    inject_from_meta(&rt, state, meta, entity_dx, entity_dy, entity_dz);
    if (!gm_frame_capture_write(fc, &rt, &act, 1, err, (int)sizeof err)) {
        fprintf(stderr, "frame_capture_write: %s\n", err);
        gm_frame_capture_close(fc); free(meta); gm_runtime_destroy(&rt); return 1;
    }
    gm_frame_capture_close(fc);

    char src[1200];
    snprintf(src, sizeof src, "%s/frame_%06d.ppm", outdir, prewarm);
    if (!copy_file(src, ppm)) {
        fprintf(stderr, "no frame ppm at %s\n", src);
        free(meta); gm_runtime_destroy(&rt); return 1;
    }
    GmPlayerView rendered_view;
    gm_runtime_view(&rt, &rendered_view);
    printf("wrote %s (state=%s pose=%.2f,%.2f,%.2f yaw=%.1f pitch=%.1f)\n",
           ppm, state, rendered_view.x, rendered_view.y, rendered_view.z,
           rendered_view.yaw, rendered_view.pitch);
    unlink(src);
    rmdir(outdir);
    free(meta);
    gm_runtime_destroy(&rt);
    return 0;
}
