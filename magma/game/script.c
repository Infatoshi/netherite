#include "game/script.h"
#include "game/runtime.h"
/* after runtime.h: entity_render.h's guarded GmEntityView redecl must see
 * game.h's full definition (MAGMA_GAME_H) or the types conflict. */
#include "core/config.h"
#include "game/entity_render.h"
#include "game/frame_capture.h"
#include "game/hand.h"
#include "game/particles_live.h"
#include "game/screen.h"
#include "game/sel_box.h"
#include "game/window_compose.h"
#include "tile_entity_brewing.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JL_FIELDS 32
#define JL_KEY 32
#define JL_VALUE 128

typedef struct { char key[JL_KEY]; char value[JL_VALUE]; int string; } JlField;
typedef struct { JlField f[JL_FIELDS]; int n; } JlObject;

static void write_hex(FILE *out, const uint8_t *data, size_t len) {
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        fputc(digits[data[i] >> 4], out);
        fputc(digits[data[i] & 15], out);
    }
}

static int read_capsule_nbt(
        const char *name, uint8_t **data_out, size_t *len_out) {
    const char *root = getenv("MAGMA_CAPSULE_DIR");
    char path[PATH_MAX];
    FILE *stream;
    long length;
    uint8_t *data;
    size_t name_len;
    if (!root || !*root || !name || !data_out || !len_out)
        return 0;
    name_len = strlen(name);
    if (name_len == 0 || name_len > 64 || name[0] == '.'
            || strstr(name, ".."))
        return 0;
    for (size_t i = 0; i < name_len; ++i)
        if (!isalnum((unsigned char)name[i]) && name[i] != '_'
                && name[i] != '-' && name[i] != '.')
            return 0;
    if (snprintf(path, sizeof path, "%s/%s", root, name)
            >= (int)sizeof path)
        return 0;
    stream = fopen(path, "rb");
    if (!stream) return 0;
    if (fseek(stream, 0, SEEK_END) != 0
            || (length = ftell(stream)) < 4
            || (unsigned long)length > GM_NBT_BLOB_MAX
            || fseek(stream, 0, SEEK_SET) != 0) {
        fclose(stream);
        return 0;
    }
    data = (uint8_t *)malloc((size_t)length);
    if (!data) {
        fclose(stream);
        return 0;
    }
    if (fread(data, 1, (size_t)length, stream) != (size_t)length
            || fclose(stream) != 0) {
        free(data);
        return 0;
    }
    *data_out = data;
    *len_out = (size_t)length;
    return 1;
}

static const char *skip_ws(const char *p) { while (*p && isspace((unsigned char)*p)) p++; return p; }

static int parse_string(const char **pp, char *out, int cap) {
    const char *p = *pp; int n = 0;
    if (*p++ != '"') return 0;
    while (*p && *p != '"') {
        if (*p == '\\' || (unsigned char)*p < 32 || n + 1 >= cap) return 0;
        out[n++] = *p++;
    }
    if (*p++ != '"') return 0;
    out[n] = 0; *pp = p; return 1;
}

static int parse_object(const char *line, JlObject *o, char *err, int cap) {
    const char *p = skip_ws(line); o->n = 0;
    if (*p++ != '{') { snprintf(err, cap, "expected JSON object"); return 0; }
    p = skip_ws(p);
    if (*p == '}') p++;
    else for (;;) {
        if (o->n >= JL_FIELDS) { snprintf(err, cap, "too many fields"); return 0; }
        JlField *f = &o->f[o->n++];
        if (!parse_string(&p, f->key, sizeof f->key)) { snprintf(err, cap, "invalid key"); return 0; }
        p = skip_ws(p);
        if (*p++ != ':') { snprintf(err, cap, "expected colon"); return 0; }
        p = skip_ws(p); f->string = *p == '"';
        if (f->string) {
            if (!parse_string(&p, f->value, sizeof f->value)) { snprintf(err, cap, "invalid string"); return 0; }
        } else {
            int n = 0;
            while (*p && *p != ',' && *p != '}' && !isspace((unsigned char)*p)) {
                if (n + 1 >= (int)sizeof f->value) { snprintf(err, cap, "value too long"); return 0; }
                f->value[n++] = *p++;
            }
            f->value[n] = 0;
            if (!n) { snprintf(err, cap, "missing value"); return 0; }
        }
        p = skip_ws(p);
        if (*p == '}') { p++; break; }
        if (*p++ != ',') { snprintf(err, cap, "expected comma"); return 0; }
        p = skip_ws(p);
    }
    p = skip_ws(p);
    if (*p) { snprintf(err, cap, "trailing JSON data"); return 0; }
    for (int i = 0; i < o->n; ++i)
        for (int j = i + 1; j < o->n; ++j)
            if (!strcmp(o->f[i].key, o->f[j].key)) {
                snprintf(err, cap, "duplicate field: %s", o->f[i].key); return 0;
            }
    return 1;
}

static const JlField *field(const JlObject *o, const char *key) {
    for (int i = 0; i < o->n; ++i) if (!strcmp(o->f[i].key, key)) return &o->f[i];
    return NULL;
}

/* Cold trace-harness export. Packed order matches qrl getblocks and the state
 * capsule: y, then z, then x, little-endian (block_id << 4 | meta). */
static int write_blocks_out(const GmRuntime *r) {
    const char *path = getenv("MAGMA_BLOCKS_OUT");
    const char *box = getenv("MAGMA_BLOCKS_BOX");
    int x0, y0, z0, x1, y1, z1;
    if (!path && !box) return 1;
    if (!path || !box
            || sscanf(box, "%d,%d,%d,%d,%d,%d",
                      &x0, &y0, &z0, &x1, &y1, &z1) != 6
            || x1 < x0 || y0 < 0 || y1 < y0 || y1 > 255 || z1 < z0) {
        fprintf(stderr, "blocks-out: invalid MAGMA_BLOCKS_OUT/BOX\n");
        return 0;
    }
    FILE *stream = fopen(path, "wb");
    if (!stream) {
        perror("blocks-out");
        return 0;
    }
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                unsigned value =
                    (unsigned)(gm_world_block(r->world, x, y, z) << 4)
                    | (unsigned)gm_world_meta(r->world, x, y, z);
                unsigned char packed[2] = {
                    (unsigned char)(value & 255u),
                    (unsigned char)((value >> 8) & 255u),
                };
                if (fwrite(packed, 1, sizeof packed, stream) != sizeof packed) {
                    fprintf(stderr, "blocks-out: short write\n");
                    fclose(stream);
                    return 0;
                }
            }
    if (fclose(stream) != 0) {
        perror("blocks-out");
        return 0;
    }
    return 1;
}

/* Cold parity-harness export. One raw block-light nibble per cell, promoted
 * to a byte, in the same y/z/x order as write_blocks_out. */
static int write_block_light_out(const GmRuntime *r) {
    const char *path = getenv("MAGMA_BLOCK_LIGHT_OUT");
    const char *box = getenv("MAGMA_BLOCKS_BOX");
    int x0, y0, z0, x1, y1, z1;
    if (!path) return 1;
    if (!box
            || sscanf(box, "%d,%d,%d,%d,%d,%d",
                      &x0, &y0, &z0, &x1, &y1, &z1) != 6
            || x1 < x0 || y0 < 0 || y1 < y0 || y1 > 255 || z1 < z0) {
        fprintf(stderr, "block-light-out: invalid MAGMA_BLOCK_LIGHT_OUT/BOX\n");
        return 0;
    }
    FILE *stream = fopen(path, "wb");
    if (!stream) {
        perror("block-light-out");
        return 0;
    }
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                unsigned char value =
                    (unsigned char)gm_world_block_light(r->world, x, y, z);
                if (fwrite(&value, 1, 1, stream) != 1) {
                    fprintf(stderr, "block-light-out: short write\n");
                    fclose(stream);
                    return 0;
                }
            }
    if (fclose(stream) != 0) {
        perror("block-light-out");
        return 0;
    }
    return 1;
}

/* Cold parity-harness skylight export, with the same one-byte y/z/x wire
 * format as write_block_light_out. */
static int write_sky_light_out(const GmRuntime *r) {
    const char *path = getenv("MAGMA_SKY_LIGHT_OUT");
    const char *box = getenv("MAGMA_BLOCKS_BOX");
    int x0, y0, z0, x1, y1, z1;
    if (!path) return 1;
    if (!box
            || sscanf(box, "%d,%d,%d,%d,%d,%d",
                      &x0, &y0, &z0, &x1, &y1, &z1) != 6
            || x1 < x0 || y0 < 0 || y1 < y0 || y1 > 255 || z1 < z0) {
        fprintf(stderr, "sky-light-out: invalid MAGMA_SKY_LIGHT_OUT/BOX\n");
        return 0;
    }
    FILE *stream = fopen(path, "wb");
    if (!stream) {
        perror("sky-light-out");
        return 0;
    }
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                unsigned char value =
                    (unsigned char)gm_world_sky_light(r->world, x, y, z);
                if (fwrite(&value, 1, 1, stream) != 1) {
                    fprintf(stderr, "sky-light-out: short write\n");
                    fclose(stream);
                    return 0;
                }
            }
    if (fclose(stream) != 0) {
        perror("sky-light-out");
        return 0;
    }
    return 1;
}

static int keys_only(const JlObject *o, const char *const *keys, int n,
                     char *err, int cap) {
    for (int i = 0; i < o->n; ++i) {
        int ok = 0;
        for (int j = 0; j < n; ++j) if (!strcmp(o->f[i].key,keys[j])) { ok=1; break; }
        if (!ok) { snprintf(err,cap,"unknown or forbidden field: %s",o->f[i].key); return 0; }
    }
    return 1;
}

static int as_i64(const JlField *f, long long *v) {
    char *e = NULL; errno = 0;
    if (!f || f->string || !f->value[0]) return 0;
    long long x = strtoll(f->value, &e, 10);
    if (errno || e == f->value || *e) return 0;
    *v = x; return 1;
}
static int as_double(const JlField *f, double *v) {
    char *e = NULL; errno = 0;
    if (!f || f->string || !f->value[0]) return 0;
    double x = strtod(f->value, &e);
    if (errno || e == f->value || *e || !isfinite(x)) return 0;
    *v = x; return 1;
}
static int as_string(const JlField *f, const char **v) {
    if (!f || !f->string) return 0;
    *v = f->value;
    return 1;
}

static int as_rule_bool(const JlField *f, int *v) {
    if (!f) return 1;
    if (!strcmp(f->value,"true")) { *v=1; return 1; }
    if (!strcmp(f->value,"false")) { *v=0; return 1; }
    return 0;
}

static int known_action_key(const char *k) {
    static const char *keys[] = {"tick","type","forward","strafe","dyaw","dpitch",
        "jump","sneak","sprint","attack","use","do_break","do_place","hotbar",
        "close_container","inv_slot","inv_button","inv_type"};
    for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; ++i) if (!strcmp(k, keys[i])) return 1;
    return 0;
}

static int parse_action(const JlObject *o, GmAction *a, char *err, int cap) {
    memset(a, 0, sizeof *a); a->hotbar_sel = -1;
    for (int i = 0; i < o->n; ++i)
        if (!known_action_key(o->f[i].key)) {
            snprintf(err, cap, "unknown or forbidden action field: %s", o->f[i].key); return 0;
        }
    double d; long long n;
#define NUM(K, DST) do { const JlField *q=field(o,K); if(q){if(!as_double(q,&d)){snprintf(err,cap,"invalid %s",K);return 0;} DST=(float)d;} }while(0)
#define INT(K, DST) do { const JlField *q=field(o,K); if(q){if(!as_i64(q,&n)){snprintf(err,cap,"invalid %s",K);return 0;} DST=(int)n;} }while(0)
    NUM("forward", a->forward); NUM("strafe", a->strafe); NUM("dyaw", a->dyaw); NUM("dpitch", a->dpitch);
    INT("jump", a->jump); INT("sneak", a->sneak); INT("sprint", a->sprint);
    INT("attack", a->attack); INT("use", a->use); INT("do_break", a->do_break);
    INT("do_place", a->do_place);
    INT("close_container", a->close_container);
    INT("hotbar", a->hotbar_sel);
#undef NUM
#undef INT
    /* Container.slotClick as a SURVIVAL action: inv_slot present -> one click of
     * (inv_slot, inv_button, inv_type) through gm_container_click this tick. */
    { const JlField *sf = field(o, "inv_slot");
      if (sf) {
          long long slot, button = 0, ctype = 0;
          if (!as_i64(sf, &slot) ||
              !(slot == GMC_OUTSIDE || (slot >= 0 && slot < GMC_SLOT_COUNT))) {
              snprintf(err, cap, "invalid inv_slot"); return 0;
          }
          const JlField *bf = field(o, "inv_button");
          const JlField *tf = field(o, "inv_type");
          if ((bf && (!as_i64(bf, &button) || (button != 0 && button != 1))) ||
              (tf && (!as_i64(tf, &ctype) ||
                      (ctype != 0 && ctype != 1 && ctype != 4)))) {
              snprintf(err, cap, "invalid inv_button/inv_type"); return 0;
          }
          a->inv_click = 1; a->inv_slot = (int)slot;
          a->inv_button = (int)button; a->inv_type = (int)ctype;
      } else if (field(o, "inv_button") || field(o, "inv_type")) {
          snprintf(err, cap, "inv_button/inv_type require inv_slot"); return 0;
      } }
    if (a->forward < -1 || a->forward > 1 || a->strafe < -1 || a->strafe > 1 ||
        a->hotbar_sel < -1 || a->hotbar_sel > 8) {
        snprintf(err, cap, "action value out of range"); return 0;
    }
    return 1;
}

static int parse_craft(const JlObject *o, int *width, int slots[9], char *err, int cap) {
    long long n;
    const JlField *wf = field(o, "width");
    if (!as_i64(wf, &n) || (n != 2 && n != 3)) { snprintf(err,cap,"craft width must be 2 or 3"); return 0; }
    *width = (int)n;
    for (int i = 0; i < 9; ++i) slots[i] = -1;
    for (int i = 0; i < o->n; ++i) {
        const char *k = o->f[i].key;
        if (!strcmp(k,"tick") || !strcmp(k,"type") || !strcmp(k,"width")) continue;
        if (strncmp(k,"grid",4) || strlen(k) != 5 || k[4] < '0' || k[4] > '8') {
            snprintf(err,cap,"unknown or forbidden craft field: %s",k); return 0;
        }
        if (!as_i64(&o->f[i],&n) || n < -1 || n >= ISR_MAIN_SLOTS) {
            snprintf(err,cap,"invalid %s inventory slot",k); return 0;
        }
        slots[k[4]-'0'] = (int)n;
    }
    return 1;
}

/* FNV-1a over the 9x9x9 id/meta volume around the player. Anchored at the
 * double-precision sim feet position (not the float render view): the Java
 * recorder computes the identical digest from floor(posX/Y/Z), and a float
 * round-trip can flip floor() at block boundaries. Java mirror:
 * Recorder.recordTick "wfnv". Iteration order and value packing must stay
 * bit-equal on both sides.
 * The basis below is NOT standard FNV-1a (last digit of ...6037 dropped,
 * historic); it only has to keep matching the Java mirror. */
static unsigned long long nearby_hash(const GmRuntime *r, int anchor[3]) {
    unsigned long long h = 1469598103934665603ULL;
    int cx = (int)floor(r->player.ent.posX + (double)r->ox);
    int cy = (int)floor(r->player.ent.posY);
    int cz = (int)floor(r->player.ent.posZ + (double)r->oz);
    anchor[0] = cx; anchor[1] = cy; anchor[2] = cz;
    for (int z = cz - 4; z <= cz + 4; ++z)
        for (int y = cy - 4; y <= cy + 4; ++y)
            for (int x = cx - 4; x <= cx + 4; ++x) {
                unsigned s = (unsigned)(gm_world_block(r->world,x,y,z) << 4 |
                                        gm_world_meta(r->world,x,y,z));
                h ^= s; h *= 1099511628211ULL;
            }
    return h;
}

static int nearby_blocks_every(void) {
    static int initialized;
    static int every;
    if (!initialized) {
        const char *value = getenv("MAGMA_STATE_NEARBY_BLOCKS_EVERY");
        long parsed = value && *value ? strtol(value, NULL, 10) : 0;
        every = parsed > 0 && parsed <= INT_MAX ? (int)parsed : 0;
        initialized = 1;
    }
    return every;
}

static long long nearby_blocks_offset(void) {
    static int initialized;
    static long long offset;
    if (!initialized) {
        const char *value = getenv("MAGMA_STATE_NEARBY_BLOCKS_OFFSET");
        offset = value && *value ? strtoll(value, NULL, 10) : 0;
        initialized = 1;
    }
    return offset;
}

static void write_nearby_blocks(
        FILE *out, const GmRuntime *r, const GmPlayerView *v) {
    int cx = (int)floor(v->x), cy = (int)floor(v->y), cz = (int)floor(v->z);
    int first = 1;
    fputs(",\"nearby_blocks\":[", out);
    for (int z = cz - 4; z <= cz + 4; ++z)
        for (int y = cy - 4; y <= cy + 4; ++y)
            for (int x = cx - 4; x <= cx + 4; ++x) {
                unsigned state = (unsigned)(gm_world_block(r->world, x, y, z) << 4 |
                                            gm_world_meta(r->world, x, y, z));
                fprintf(out, "%s%u", first ? "" : ",", state);
                first = 0;
            }
    fputc(']', out);
}

static void write_state(FILE *out, const GmRuntime *r) {
    GmPlayerView v; gm_runtime_view(r, &v);
    fprintf(out, "{\"version\":1,\"tick\":%lld,\"dim\":%d,\"world_time\":%lld,"
                 "\"total_time\":%lld,\"do_entity_drops\":%s,"
                 "\"entity_id_cursor\":%d,"
                 "\"world_rand_seed48\":%llu,\"math_rand_seed48\":%llu,"
                 "\"block_rand_seed48\":%llu,"
                 "\"world_rand_have_gaussian\":%s,"
                 "\"world_rand_gaussian\":%.17g,"
                 "\"world_update_lcg\":%d,",
            r->tick,r->dimension,(long long)r->clock.world_time,
            (long long)r->clock.total_time,
            r->do_entity_drops ? "true" : "false",r->next_entity_id,
            (unsigned long long)r->world_random_seed48,
            (unsigned long long)r->math_random_seed48,
            (unsigned long long)r->block_random_seed48,
            r->world_random_have_gaussian ? "true" : "false",
            r->world_random_gaussian,
            (int)r->world_update_lcg);
    if (r->controlled_input_valid
            && r->controlled_input_tick == r->tick) {
        fprintf(
            out,
            "\"controlled_input\":{\"before\":{"
            "\"world_rand_seed48\":%llu,\"math_rand_seed48\":%llu,"
            "\"block_rand_seed48\":%llu,\"world_update_lcg\":%d,"
            "\"next_entity_id\":%d},\"world_rand_seed48\":%llu,"
            "\"math_rand_seed48\":%llu,\"block_rand_seed48\":%llu,"
            "\"world_update_lcg\":%d,\"next_entity_id\":%d},",
            (unsigned long long)r->controlled_input_before_world_seed48,
            (unsigned long long)r->controlled_input_before_math_seed48,
            (unsigned long long)r->controlled_input_before_block_seed48,
            (int)r->controlled_input_before_update_lcg,
            r->controlled_input_before_entity_id,
            (unsigned long long)r->controlled_input_world_seed48,
            (unsigned long long)r->controlled_input_math_seed48,
            (unsigned long long)r->controlled_input_block_seed48,
            (int)r->controlled_input_update_lcg,
            r->controlled_input_entity_id);
    } else {
        fprintf(out, "\"controlled_input\":null,");
    }
    fprintf(out, "\"weather\":{\"raining\":%d,\"thundering\":%d,"
                 "\"rain_time\":%d,\"thunder_time\":%d,"
                 "\"clean_weather_time\":%d,\"weather_cycle\":%d,"
                 "\"daylight_cycle\":%d,\"prev_rain_strength\":%.9g,"
                 "\"rain_strength\":%.9g,\"prev_thunder_strength\":%.9g,"
                 "\"thunder_strength\":%.9g},",
            r->clock.raining,r->clock.thundering,
            r->clock.rain_time,r->clock.thunder_time,
            r->clock.clean_weather_time,r->clock.weather_cycle,
            !r->clock.freeze_daylight,
            (double)r->clock.prev_rain_strength,
            (double)r->clock.rain_strength,
            (double)r->clock.prev_thunder_strength,
            (double)r->clock.thunder_strength);
    fprintf(out, "\"scheduled_ticks\":[");
    for (int i = 0; i < gm_runtime_scheduled_tick_count(r); ++i) {
        GmRuntimeScheduledTick entry;
        if (!gm_runtime_scheduled_tick_get(r, i, &entry)) continue;
        if (i) fputc(',', out);
        fprintf(
            out,
            "{\"x\":%d,\"y\":%d,\"z\":%d,\"block\":%d,"
            "\"time\":%lld,\"priority\":%d,\"order\":%lld}",
            entry.x, entry.y, entry.z, entry.block, entry.time,
            entry.priority, entry.order);
    }
    fprintf(out, "],");
    fprintf(out, "\"moving_pistons\":[");
    int piston_written = 0;
    for (int i = 0; i < gm_runtime_moving_piston_count(r); ++i) {
        GmRuntimePiston piston;
        union { float f; unsigned u; } progress,last_progress;
        if (!gm_runtime_moving_piston_get(r, i, &piston)
                || piston.dimension != r->dimension)
            continue;
        if (piston_written++) fputc(',', out);
        progress.f = piston.progress;
        last_progress.f = piston.last_progress;
        fprintf(
            out,
            "{\"x\":%d,\"y\":%d,\"z\":%d,"
            "\"moved_block\":%d,\"moved_meta\":%d,\"facing\":%d,"
            "\"extending\":%s,\"source\":%s,"
            "\"progress_bits\":%u,\"last_progress_bits\":%u}",
            piston.x, piston.y, piston.z,
            piston.moved_block, piston.moved_meta, piston.facing,
            piston.extending ? "true" : "false",
            piston.source ? "true" : "false",
            progress.u, last_progress.u);
    }
    fprintf(out, "],");
    fprintf(out, "\"comparators\":[");
    int comparator_written = 0;
    for (int i = 0; i < gm_runtime_comparator_count(r); ++i) {
        GmRuntimeComparator comparator;
        if (!gm_runtime_comparator_get(r, i, &comparator)
                || comparator.dimension != r->dimension)
            continue;
        if (comparator_written++) fputc(',', out);
        fprintf(
            out,
            "{\"x\":%d,\"y\":%d,\"z\":%d,\"output_signal\":%d}",
            comparator.x, comparator.y, comparator.z,
            comparator.output_signal);
    }
    fprintf(out, "],");
    fprintf(out, "\"containers\":[");
    int container_written = 0;
    {
        int total = gm_runtime_chest_count(r);
        int have_previous = 0;
        int previous_x = 0, previous_y = 0, previous_z = 0;
        for (int written = 0; written < total; ++written) {
            GmRuntimeChest selected;
            int found = 0;
            for (int i = 0; i < total; ++i) {
                GmRuntimeChest candidate;
                if (!gm_runtime_chest_get(r, i, &candidate))
                    continue;
                if (have_previous
                        && (candidate.wx < previous_x
                            || (candidate.wx == previous_x
                                && candidate.wy < previous_y)
                            || (candidate.wx == previous_x
                                && candidate.wy == previous_y
                                && candidate.wz <= previous_z)))
                    continue;
                if (!found
                        || candidate.wx < selected.wx
                        || (candidate.wx == selected.wx
                            && candidate.wy < selected.wy)
                        || (candidate.wx == selected.wx
                            && candidate.wy == selected.wy
                            && candidate.wz < selected.wz)) {
                    selected = candidate;
                    found = 1;
                }
            }
            if (!found) break;
            if (container_written++) fputc(',', out);
            GmRuntimeChest pair;
            int pair_found = 0;
            int selected_block = gm_world_block(
                r->world, selected.wx, selected.wy, selected.wz);
            for (int i = 0; i < total; ++i) {
                GmRuntimeChest candidate;
                if (!gm_runtime_chest_get(r, i, &candidate)
                        || candidate.wy != selected.wy)
                    continue;
                if (gm_world_block(
                        r->world, candidate.wx, candidate.wy,
                        candidate.wz) != selected_block)
                    continue;
                int distance =
                    abs(candidate.wx - selected.wx)
                    + abs(candidate.wz - selected.wz);
                if (distance == 1) {
                    pair = candidate;
                    pair_found = 1;
                    break;
                }
            }
            if (selected_block == 146 && pair_found) {
                fprintf(
                    out,
                    "{\"type\":\"double_trapped_chest_half\","
                    "\"x\":%d,\"y\":%d,\"z\":%d,\"size\":%d,"
                    "\"pair_x\":%d,\"pair_y\":%d,\"pair_z\":%d,",
                    selected.wx, selected.wy, selected.wz,
                    CHEST_LIVE_SLOTS,
                    pair.wx, pair.wy, pair.wz);
            } else if (selected_block == 146) {
                fprintf(
                    out,
                    "{\"type\":\"single_trapped_chest\","
                    "\"x\":%d,\"y\":%d,\"z\":%d,\"size\":%d,",
                    selected.wx, selected.wy, selected.wz,
                    CHEST_LIVE_SLOTS);
            } else if (pair_found) {
                fprintf(
                    out,
                    "{\"type\":\"double_chest_half\","
                    "\"x\":%d,\"y\":%d,\"z\":%d,\"size\":%d,"
                    "\"pair_x\":%d,\"pair_y\":%d,\"pair_z\":%d,",
                    selected.wx, selected.wy, selected.wz,
                    CHEST_LIVE_SLOTS,
                    pair.wx, pair.wy, pair.wz);
            } else {
                fprintf(
                    out,
                    "{\"type\":\"single_chest\",\"x\":%d,\"y\":%d,"
                    "\"z\":%d,\"size\":%d,",
                    selected.wx, selected.wy, selected.wz,
                    CHEST_LIVE_SLOTS);
            }
            {
                union { float f; unsigned u; } lid, prev_lid;
                lid.f = selected.state.te.lid_angle;
                prev_lid.f = selected.state.te.prev_lid_angle;
                fprintf(
                    out,
                    "\"num_players_using\":%d,"
                    "\"lid_angle_bits\":%u,"
                    "\"prev_lid_angle_bits\":%u,\"items\":[",
                    selected.state.te.num_players_using,
                    lid.u, prev_lid.u);
            }
            int item_written = 0;
            for (int slot = 0; slot < CHEST_LIVE_SLOTS; ++slot) {
                ICStack stack = chest_live_get(&selected.state, slot);
                if (stack.item <= 0 || stack.count <= 0)
                    continue;
                if (item_written++) fputc(',', out);
                fprintf(
                    out,
                    "{\"slot\":%d,\"id\":%d,\"count\":%d,\"meta\":%d}",
                    slot, stack.item, stack.count, stack.meta);
            }
            fprintf(out, "]}");
            previous_x = selected.wx;
            previous_y = selected.wy;
            previous_z = selected.wz;
            have_previous = 1;
        }
    }
    for (int i = 0; i < gm_runtime_furnace_count(r); ++i) {
        GmRuntimeFurnace furnace;
        const SRStack *slots[FURNACE_LIVE_SLOT_COUNT];
        if (!gm_runtime_furnace_get(r, i, &furnace))
            continue;
        if (container_written++) fputc(',', out);
        fprintf(
            out,
            "{\"type\":\"furnace\",\"x\":%d,\"y\":%d,\"z\":%d,"
            "\"size\":%d,\"burn_time\":%d,\"current_burn_time\":%d,"
            "\"cook_time\":%d,\"total_cook_time\":%d,\"items\":[",
            furnace.wx, furnace.wy, furnace.wz,
            FURNACE_LIVE_SLOT_COUNT,
            furnace.state.burn_time, furnace.state.current_burn_time,
            furnace.state.cook_time, furnace.state.total_cook);
        slots[0] = &furnace.state.input;
        slots[1] = &furnace.state.fuel;
        slots[2] = &furnace.state.output;
        int item_written = 0;
        for (int slot = 0; slot < FURNACE_LIVE_SLOT_COUNT; ++slot) {
            if (sr_isEmpty(*slots[slot])) continue;
            if (item_written++) fputc(',', out);
            fprintf(
                out,
                "{\"slot\":%d,\"id\":%d,\"count\":%d,\"meta\":%d}",
                slot, slots[slot]->item, slots[slot]->count,
                slots[slot]->meta);
        }
        fprintf(out, "]}");
    }
    for (int i = 0; i < gm_runtime_static_container_count(r); ++i) {
        GmRuntimeStaticContainer container;
        if (!gm_runtime_static_container_get(r, i, &container))
            continue;
        if (container_written++) fputc(',', out);
        if (container.block >= 219 && container.block <= 234) {
            fprintf(
                out,
                "{\"type\":\"shulker_box\",\"x\":%d,\"y\":%d,"
                "\"z\":%d,\"size\":%d,\"block\":%d,"
                "\"facing\":%d,",
                container.wx, container.wy, container.wz, container.size,
                container.block,
                gm_world_meta(
                    r->world, container.wx, container.wy, container.wz));
            if (container.item_tag.data && container.item_tag.len > 0) {
                fprintf(out, "\"item_tag_nbt\":\"");
                write_hex(
                    out, container.item_tag.data, container.item_tag.len);
                fprintf(out, "\",");
            }
            fprintf(out, "\"items\":[");
        } else if (container.block == 117) {
            fprintf(
                out,
                "{\"type\":\"brewing_stand\",\"x\":%d,\"y\":%d,"
                "\"z\":%d,\"size\":%d,\"brew_time\":%d,"
                "\"fuel\":%d,\"items\":[",
                container.wx, container.wy, container.wz,
                container.size, container.brewing.brew_time,
                container.brewing.fuel);
        } else if (container.block == 154) {
            fprintf(
                out,
                "{\"type\":\"hopper\",\"x\":%d,\"y\":%d,"
                "\"z\":%d,\"size\":%d,\"transfer_cooldown\":%d,"
                "\"ticked_game_time\":%lld,\"items\":[",
                container.wx, container.wy, container.wz,
                container.size, container.transfer_cooldown,
                container.ticked_game_time);
        } else {
            fprintf(
                out,
                "{\"type\":\"%s\",\"x\":%d,\"y\":%d,\"z\":%d,"
                "\"size\":%d,\"items\":[",
                container.block == 23 ? "dispenser"
                    : container.block == 158 ? "dropper"
                    : container.block == 154 ? "hopper" : "jukebox",
                container.wx, container.wy, container.wz,
                container.size);
        }
        int item_written = 0;
        for (int slot = 0; slot < container.size; ++slot) {
            ICStack stack = container.slots[slot];
            if (stack.item <= 0 || stack.count <= 0)
                continue;
            if (item_written++) fputc(',', out);
            fprintf(
                out,
                "{\"slot\":%d,\"id\":%d,\"count\":%d,\"meta\":%d}",
                slot, stack.item, stack.count, stack.meta);
        }
        fprintf(out, "]}");
    }
    for (int i = 0; i < gm_runtime_command_block_count(r); ++i) {
        GmRuntimeCommandBlock command;
        if (!gm_runtime_command_block_get(r, i, &command))
            continue;
        if (container_written++) fputc(',', out);
        fprintf(
            out,
            "{\"type\":\"%s\",\"x\":%d,\"y\":%d,\"z\":%d,"
            "\"size\":0,\"success_count\":%d,\"items\":[]}",
            command.block == 210 ? "repeating_command_block"
                : command.block == 211 ? "chain_command_block"
                : "command_block",
            command.wx, command.wy, command.wz,
            command.success_count);
    }
    fprintf(out, "],");
    fprintf(out, "\"flower_pots\":[");
    for (int i = 0; i < gm_runtime_flower_pot_count(r); ++i) {
        GmRuntimeFlowerPot pot;
        if (!gm_runtime_flower_pot_get(r, i, &pot))
            continue;
        if (i) fputc(',', out);
        fprintf(
            out,
            "{\"x\":%d,\"y\":%d,\"z\":%d,\"item\":%d,\"meta\":%d}",
            pot.wx, pot.wy, pot.wz, pot.item, pot.meta);
    }
    fprintf(out, "],");
    fprintf(out, "\"skulls\":[");
    for (int i = 0; i < gm_runtime_skull_count(r); ++i) {
        GmRuntimeSkull skull;
        if (!gm_runtime_skull_get(r, i, &skull))
            continue;
        if (i) fputc(',', out);
        fprintf(
            out,
            "{\"x\":%d,\"y\":%d,\"z\":%d,\"type\":%d,"
            "\"rotation\":%d,\"has_owner\":%s",
            skull.wx, skull.wy, skull.wz, skull.type, skull.rotation,
            skull.owner_profile.data ? "true" : "false");
        if (skull.owner_profile.data) {
            fprintf(out, ",\"owner_nbt\":\"");
            write_hex(
                out, skull.owner_profile.data, skull.owner_profile.len);
            fputc('"', out);
        }
        fputc('}', out);
    }
    fprintf(out, "],");
    fprintf(out, "\"item_frames\":[");
    for (int i = 0; i < gm_runtime_item_frame_count(r); ++i) {
        GmRuntimeItemFrame frame;
        if (!gm_runtime_item_frame_get(r, i, &frame))
            continue;
        if (i) fputc(',', out);
        fprintf(
            out,
            "{\"eid\":%d,\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
            "\"hanging_x\":%d,\"hanging_y\":%d,\"hanging_z\":%d,"
            "\"facing\":%d,\"item\":%d,\"count\":%d,\"meta\":%d,"
            "\"rotation\":%d}",
            frame.eid, frame.x, frame.y, frame.z,
            frame.hanging_x, frame.hanging_y, frame.hanging_z,
            frame.facing, frame.item, frame.count, frame.meta,
            frame.rotation);
    }
    fprintf(out, "],");
    fprintf(out, "\"redstone_torch_toggles\":[");
    for (int i = 0; i < gm_runtime_redstone_torch_toggle_count(r); ++i) {
        GmRuntimeRedstoneTorchToggle toggle;
        if (!gm_runtime_redstone_torch_toggle_get(r, i, &toggle))
            continue;
        if (i) fputc(',', out);
        fprintf(
            out,
            "{\"x\":%d,\"y\":%d,\"z\":%d,\"time\":%lld}",
            toggle.x, toggle.y, toggle.z, toggle.time);
    }
    fprintf(out, "],");
    /* Position from the double-precision sim state, NOT the float render view:
     * MC positions are doubles and the tape differ works at 1e-9, so the float
     * round-trip alone showed up as a fake tick-0 divergence (3e-6). */
    fprintf(out, "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,",
            r->player.ent.posX + (double)r->ox,
            r->player.ent.posY,
            r->player.ent.posZ + (double)r->oz);
    fprintf(out, "\"yaw\":%.9g,\"pitch\":%.9g,", (double)v.yaw,(double)v.pitch);
    fprintf(out, "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,",
            r->player.ent.motionX,r->player.ent.motionY,r->player.ent.motionZ);
    fprintf(out,
            "\"server_x\":%.17g,\"server_y\":%.17g,\"server_z\":%.17g,"
            "\"server_vx\":%.17g,\"server_vy\":%.17g,"
            "\"server_vz\":%.17g,",
            r->server_player.ent.posX + (double)r->ox,
            r->server_player.ent.posY,
            r->server_player.ent.posZ + (double)r->oz,
            r->server_player.ent.motionX,
            r->server_player.ent.motionY,
            r->server_player.ent.motionZ);
    fprintf(out, "\"on_ground\":%d,\"health\":%.9g,"
                 "\"max_health\":%.9g,\"absorption\":%.9g,\"food\":%.9g,"
                 "\"saturation\":%.9g,\"food_exhaustion\":%.9g,"
                 "\"food_timer\":%d,\"air\":%d,\"fire\":%d,"
                 "\"xp_level\":%d,\"xp_frac\":%.9g,\"fall_distance\":%.9g,"
                 "\"sprinting\":%d,\"sneaking\":%d,\"jumping\":%d,",
            v.on_ground,(double)v.health,(double)v.max_health,
            (double)v.absorption,(double)v.food,
            (double)r->vitals.saturation,
            (double)r->vitals.exhaustion, r->vitals.foodTimer,
            r->player_air,
            r->player_fire_ticks,
            v.xp_level,(double)v.xp_frac,(double)r->player.fall_distance,
            r->player.sprinting,r->player.prev_sneak,r->player.prev_jump);
    {
        int sel = r->player.inv.current_item;
        ICStack held;
        if (sel < 0) sel = 0;
        if (sel > 8) sel = 8;
        held = isr_get_stack(&r->player.inv, sel);
        fprintf(out, "\"held_slot\":%d,\"held_id\":%d,\"held_count\":%d,"
                     "\"held_meta\":%d,\"attack_cooldown\":%.9g,"
                     "\"attack_ticks\":%d,\"hurt_time\":%d,"
                     "\"hurt_resistant_time\":%d,\"death_time\":%d,"
                     "\"xp_total\":%d,\"potions\":[",
                sel, held.item, held.count, held.meta,
                (double)v.attack_cooldown,
                r->mobs.player_ticks_since_last_swing,
                v.hurt_time, r->mobs.player_hurt_resistant,
                r->player_death_time,
                r->player_xp_level >= 0
                    ? r->player_xp_total : r->mobs.xp_total);
        for (int i = 0; i < v.potion_count; ++i) {
            if (i) fputc(',', out);
            fprintf(out, "{\"id\":%d,\"amp\":%d,\"dur\":%d}",
                    v.potions[i].id, v.potions[i].amplifier,
                    v.potions[i].duration);
        }
        fprintf(out, "],");
    }
    { int hx,hy,hz,ax,ay,az;
      int hit=gm_raycast_sel(r->window,&r->sin_table,&r->player,
                             &hx,&hy,&hz,&ax,&ay,&az);
      if(hit>=0) fprintf(out,"\"look\":{\"x\":%d,\"y\":%d,\"z\":%d,\"id\":%d},",
          hx+r->ox,hy,hz+r->oz,gm_world_block(r->world,hx+r->ox,hy,hz+r->oz));
      else fprintf(out,"\"look\":null,"); }
    fprintf(out, "\"dead\":%d,\"deaths\":%d,\"won\":%s,\"credits\":%d,\"container\":%d,",
            v.dead,v.deaths,r->won?"true":"false",r->credits,r->container);
    fprintf(out, "\"inventory\":[");
    {
        int first_inv = 1;
        for (int i = 0; i < ISR_MAIN_SLOTS; ++i) {
            ICStack s = isr_get_stack(&r->player.inv, i);
            fprintf(out, "%s{\"slot\":%d,\"item\":%d,\"count\":%d,\"meta\":%d,\"enchants\":[",
                    first_inv ? "" : ",", i, s.item, s.count, s.meta);
            for (int j = 0; j < s.n_enchants; ++j)
                fprintf(out, "%s[%d,%d]", j ? "," : "",
                        s.enchants[j].id, s.enchants[j].level);
            fputs("]}", out);
            first_inv = 0;
        }
        for (int i = 0; i < ISR_ARMOR_SLOTS; ++i) {
            ICStack s = isr_get_stack(&r->player.inv, ISR_ARMOR0 + i);
            fprintf(out, "%s{\"slot\":%d,\"item\":%d,\"count\":%d,\"meta\":%d,\"enchants\":[",
                    first_inv ? "" : ",", ISR_ARMOR0 + i, s.item, s.count, s.meta);
            for (int j = 0; j < s.n_enchants; ++j)
                fprintf(out, "%s[%d,%d]", j ? "," : "",
                        s.enchants[j].id, s.enchants[j].level);
            fputs("]}", out);
            first_inv = 0;
        }
        {
            ICStack oh = isr_get_stack(&r->player.inv, ISR_OFFHAND_SLOT);
            fprintf(out, "%s{\"slot\":%d,\"item\":%d,\"count\":%d,\"meta\":%d,\"enchants\":[",
                    first_inv ? "" : ",", ISR_OFFHAND_SLOT, oh.item, oh.count, oh.meta);
            for (int j = 0; j < oh.n_enchants; ++j)
                fprintf(out, "%s[%d,%d]", j ? "," : "",
                        oh.enchants[j].id, oh.enchants[j].level);
            fputs("]}", out);
        }
    }
    fprintf(out, "],");
    { ICStack c = gm_player_cursor();
      fprintf(out, "\"cursor\":[%d,%d,%d],\"grid\":[", c.item, c.count, c.meta);
      for (int i = 0; i < 9; ++i)
          fprintf(out, "%s[%d,%d,%d]", i ? "," : "",
                  r->craft_grid[i].item, r->craft_grid[i].count, r->craft_grid[i].meta);
      ICStack res = gm_container_result(r);
      fprintf(out, "],\"craft_result\":[%d,%d,%d],", res.item, res.count, res.meta); }
    fprintf(out, "\"entities\":[");
    int first = 1;
    for (int i = 0; i < GM_LIVE_MAX; ++i) if (r->entities.ents[i].active) {
        const GmLiveEnt *e = &r->entities.ents[i];
        fprintf(out, "%s{\"kind\":\"item\",\"eid\":%d,\"type\":%d,"
                     "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                     "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,"
                     "\"yaw\":%.9g,\"pitch\":0,\"health\":%d,"
                     "\"item\":%d,\"count\":%d,\"meta\":%d,"
                     "\"age\":%d,\"pickup_delay\":%d",
                first ? "" : ",", e->eid > 0 ? e->eid : 1000000 + i,
                e->type,e->x,e->y,e->z,e->mx,e->my,e->mz,
                (double)e->yaw,e->health,
                e->item,e->count,e->meta,e->age,e->pickup_delay);
        {
            GmRuntimeTaggedItem tagged;
            if (gm_runtime_tagged_item_get_by_eid(
                    r, e->eid, &tagged)) {
                if (tagged.tag.data && tagged.tag.len > 0) {
                    fprintf(
                        out,
                        ",\"stack_payload\":{\"kind\":\"item_tag\","
                        "\"nbt\":\"");
                    write_hex(out, tagged.tag.data, tagged.tag.len);
                    fprintf(out, "\"}");
                } else {
                    fprintf(
                        out,
                        ",\"stack_payload\":{\"kind\":\"shulker_box\","
                        "\"items\":[");
                    int item_written = 0;
                    for (int slot = 0; slot < tagged.size; ++slot) {
                        ICStack stack = tagged.slots[slot];
                        if (stack.item <= 0 || stack.count <= 0)
                            continue;
                        if (item_written++) fputc(',', out);
                        fprintf(
                            out,
                            "{\"slot\":%d,\"id\":%d,\"count\":%d,"
                            "\"meta\":%d}",
                            slot, stack.item, stack.count, stack.meta);
                    }
                    fprintf(out, "]}");
                }
            }
        }
        fputc('}', out);
        first = 0;
    }
    GmEntityView bosses[ED_NUM_CRYSTALS+1];
    int nboss=gm_dragon_fill_views(&r->dragon,bosses,ED_NUM_CRYSTALS+1);
    for(int i=0;i<nboss;++i){
        const GmEntityView *e=&bosses[i];
        fprintf(out,"%s{\"kind\":\"boss\",\"eid\":%d,\"type\":%d,"
                    "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                    "\"vx\":0,\"vy\":0,\"vz\":0,\"yaw\":%.9g,\"pitch\":0,"
                    "\"health\":%.9g}",
                first?"":",",2000000+i,e->type,(double)e->x,(double)e->y,
                (double)e->z,(double)e->yaw,(double)e->health);
        first=0;
    }
    {
        const EwStore *mobs = r->mobs.current ? &r->mobs.b : &r->mobs.a;
        for (int i = 1; i < EW_MAX_ENTITIES; ++i) {
            if (!mobs->alive[i] ||
                r->mobs.entity_dimension[i] != r->mobs.active_dimension)
                continue;
            fprintf(out,"%s{\"kind\":\"mob\",\"eid\":%d,\"type\":%d,"
                        "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                        "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,"
                        "\"yaw\":%.9g,\"pitch\":0,\"health\":%.9g",
                    first?"":",",mobs->id[i],mobs->type[i],
                    mobs->x[i],mobs->y[i],mobs->z[i],
                    mobs->vx[i],mobs->vy[i],mobs->vz[i],
                    (double)mobs->yaw[i],
                    mobs->type[i]==EW_TYPE_BOAT?-1.0:(double)mobs->health[i]);
            if (mobs->type[i] == EW_TYPE_BOAT) {
                fputs(",\"hurt_time\":null,\"death_time\":null,"
                      "\"hurt_resistant_time\":null}", out);
            } else {
                fprintf(out,",\"max_health\":%.9g,\"absorption\":%.9g,"
                            "\"air\":%d,"
                            "\"hurt_time\":%d,\"death_time\":%d,"
                            "\"hurt_resistant_time\":%d,\"potions\":[",
                        (double)gm_mobs_max_health(&r->mobs, i),
                        (double)gm_mobs_absorption(&r->mobs, i),
                        gm_mobs_air(&r->mobs, i),
                        r->mobs.entity_hurt_time[i],
                        r->mobs.entity_death_time[i],
                        r->mobs.entity_hurt_resistant[i]);
                for (int effect_index = 0;
                        effect_index < gm_mobs_potion_effect_count(
                            &r->mobs, i); ++effect_index) {
                    PtMobEffect effect;
                    if (!gm_mobs_potion_effect_get(
                            &r->mobs, i, effect_index, &effect))
                        continue;
                    fprintf(out,
                            "%s{\"id\":%d,\"amp\":%d,\"dur\":%d}",
                            effect_index ? "," : "", effect.id,
                            effect.amplifier, effect.duration);
                }
                fputc(']', out);
                if (mobs->type[i] == EW_TYPE_VILLAGER) {
                    const GmRuntimeVillageResident *resident = NULL;
                    for (int resident_index = 0;
                            resident_index < r->village_resident_count;
                            ++resident_index)
                        if (r->village_residents[resident_index].eid
                                == mobs->id[i]) {
                            resident = &r->village_residents[resident_index];
                            break;
                        }
                    const JavaGaussianRandom *random =
                        &r->mobs.entity_random[i];
                    fprintf(
                        out,
                        ",\"profession\":%d,\"growing_age\":%d,"
                        "\"career\":%d,\"career_level\":%d,"
                        "\"living_sound_time\":%d,"
                        "\"offers_initialized\":%s,"
                        "\"entity_seed48\":%llu,"
                        "\"entity_have_gaussian\":%s,"
                        "\"entity_gaussian\":%.17g",
                        (int)r->mobs.villager_profession[i],
                        r->mobs.growing_age[i],
                        resident && resident->trade.initialized
                            ? resident->trade.career : 0,
                        resident && resident->trade.initialized
                            ? resident->trade.career_level : 0,
                        r->mobs.entity_living_sound_time[i],
                        resident && resident->trade.initialized
                            ? "true" : "false",
                        (unsigned long long)random->random.seed,
                        random->have_next_next_gaussian
                            ? "true" : "false",
                        random->next_next_gaussian);
                }
                fputc('}', out);
            }
            first=0;
        }
    }
    for (int i = 0; i < GM_XP_ORBS; ++i) {
        const McOrb *e = &r->mobs.xp_orbs[i];
        if (e->dead || e->xpValue <= 0 ||
            r->mobs.orb_dimension[i] != r->mobs.active_dimension)
            continue;
        fprintf(out,"%s{\"kind\":\"xp_orb\",\"eid\":%d,\"type\":21,"
                    "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                    "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,"
                    "\"yaw\":0,\"pitch\":0,\"health\":%d,\"value\":%d,"
                    "\"age\":%d,\"pickup_delay\":%d,\"color\":%d,"
                    "\"target_color\":%d}",
                first?"":",",e->eid,e->posX,e->posY,e->posZ,
                e->motionX,e->motionY,e->motionZ,e->health,e->xpValue,
                e->xpOrbAge,e->delayBeforeCanPickup,e->xpColor,
                e->xpTargetColor);
        first=0;
    }
    if (r->fish_hook.active && r->fish_hook.dimension == r->dimension) {
        const GmRuntimeFishHook *e = &r->fish_hook;
        int state = e->state == GM_FISH_STATE_HOOKED ? 1
            : e->state == GM_FISH_STATE_BOBBING ? 2 : 0;
        fprintf(out,
                "%s{\"kind\":\"fish_hook\",\"eid\":%d,\"type\":90,"
                "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,"
                "\"yaw\":%.9g,\"pitch\":%.9g,\"health\":-1,"
                "\"fish_state\":%d,\"in_ground\":%d,"
                "\"ticks_in_ground\":%d,\"ticks_in_air\":%d,"
                "\"ticks_catchable\":%d,\"ticks_caught_delay\":%d,"
                "\"ticks_catchable_delay\":%d,"
                "\"fish_approach_angle\":%.9g,\"lure\":%d,"
                "\"luck\":%d,\"caught_eid\":%d,"
                "\"entity_seed48\":%llu,"
                "\"entity_have_gaussian\":%d,"
                "\"entity_gaussian\":%.17g}",
                first ? "" : ",", e->eid,
                e->x, e->y, e->z, e->vx, e->vy, e->vz,
                (double)e->yaw, (double)e->pitch, state, e->in_ground,
                e->ticks_in_ground, e->ticks_in_air,
                e->catch_state.ticks_catchable,
                e->catch_state.ticks_caught_delay,
                e->catch_state.ticks_catchable_delay,
                (double)e->catch_state.approach_angle,
                e->catch_state.lure, e->catch_state.luck, e->caught_eid,
                (unsigned long long)e->random.random.seed,
                e->random.have_next_next_gaussian,
                e->random.next_next_gaussian);
        first = 0;
    }
    for(int i=0;i<GM_RUNTIME_PROJECTILES;++i){
        const GmRuntimeProjectile *p = &r->projectiles[i];
        double yaw, pitch, horiz;
        if (!p->active) continue;
        if (p->type == 3) {
            yaw = p->yaw;
            pitch = p->pitch;
        } else if (p->controlled_stationary) {
            /* The saved arrow updates before a same-tick explosion, so its
             * fresh external motion has not changed rotation yet. */
            yaw = pitch = 0.0;
        } else {
            horiz = sqrt(p->vx*p->vx + p->vz*p->vz);
            yaw = atan2(p->vx,p->vz) * 180.0 / MC_PI;
            pitch = atan2(p->vy,horiz) * 180.0 / MC_PI;
        }
        fprintf(out,"%s{\"kind\":\"projectile\",\"eid\":%d,\"type\":%d,"
                    "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                    "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g",
                first?"":",",p->eid > 0 ? p->eid : 3000000+i,
                p->type,p->x,p->y,p->z,
                p->vx,p->vy,p->vz);
        if (p->type == 3)
            fprintf(out,",\"ax\":%.17g,\"ay\":%.17g,\"az\":%.17g",
                    p->ax,p->ay,p->az);
        if (p->type == 6)
            fprintf(out,
                    ",\"potion_item\":%d,\"potion_type\":%d,\"age\":%d",
                    p->potion_item,p->potion_type,p->age);
        fprintf(out,",\"yaw\":%.9g,\"pitch\":%.9g,\"health\":-1}",
                yaw,pitch);
        first=0;
    }
    for (int i = 0; i < GM_RUNTIME_MINECARTS; ++i) {
        const GmRuntimeMinecart *e = &r->minecarts[i];
        int size;
        int item_written = 0;
        if (!e->active || e->dimension != r->dimension) continue;
        size = e->kind == GM_MINECART_CHEST ? 27
            : e->kind == GM_MINECART_HOPPER ? 5 : 0;
        fprintf(out,
                "%s{\"kind\":\"minecart\",\"eid\":%d,\"type\":28,"
                "\"minecart_kind\":%d,\"x\":%.17g,\"y\":%.17g,"
                "\"z\":%.17g,\"vx\":%.17g,\"vy\":%.17g,"
                "\"vz\":%.17g,\"yaw\":%.9g,\"pitch\":%.9g,"
                "\"health\":-1,\"reverse\":%d,"
                "\"rolling_amplitude\":%d,\"rolling_direction\":%d,"
                "\"damage\":%.9g,"
                "\"fuel\":%d,\"push_x\":%.17g,\"push_z\":%.17g,"
                "\"tnt_fuse\":%d,\"hopper_enabled\":%d,"
                "\"transfer_cooldown\":%d,\"entity_seed48\":%llu,"
                "\"entity_have_gaussian\":%d,"
                "\"entity_gaussian\":%.17g,\"items\":[",
                first ? "" : ",", e->eid, e->kind,
                e->x, e->y, e->z, e->vx, e->vy, e->vz,
                (double)e->yaw, (double)e->pitch, e->reverse,
                e->rolling_amplitude, e->rolling_direction,
                (double)e->damage,
                e->fuel, e->push_x, e->push_z, e->tnt_fuse,
                e->hopper_enabled, e->transfer_cooldown,
                (unsigned long long)e->random_seed48,
                e->random_have_gaussian, e->random_gaussian);
        for (int slot = 0; slot < size; ++slot) {
            ICStack stack = e->slots[slot];
            if (isr_is_empty(&stack)) continue;
            fprintf(out,
                    "%s{\"slot\":%d,\"id\":%d,\"count\":%d,"
                    "\"meta\":%d}",
                    item_written++ ? "," : "", slot,
                    stack.item, stack.count, stack.meta);
        }
        fputs("]}", out);
        first = 0;
    }
    for (int i = 0; i < GM_RUNTIME_AREA_EFFECT_CLOUDS; ++i) {
        const GmRuntimeAreaEffectCloud *e = &r->area_effect_clouds[i];
        if (!e->state.active) continue;
        fprintf(out,
                "%s{\"kind\":\"area_effect_cloud\",\"eid\":%d,"
                "\"type\":41,\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                "\"vx\":0,\"vy\":0,\"vz\":0,\"yaw\":0,\"pitch\":0,"
                "\"health\":-1,\"potion_type\":%d,\"age\":%d,"
                "\"duration\":%d,\"wait_time\":%d,"
                "\"reapplication_delay\":%d,\"radius\":%.9g,"
                "\"radius_on_use\":%.9g,\"radius_per_tick\":%.9g,"
                "\"next_application\":%d}",
                first ? "" : ",", e->eid, e->x, e->y, e->z,
                e->potion_type, e->state.age, e->state.duration,
                e->state.wait_time, e->state.reapplication_delay,
                (double)e->state.radius, (double)e->state.radius_on_use,
                (double)e->state.radius_per_tick,
                e->state.next_application);
        first = 0;
    }
    for(int i=0;i<GM_RUNTIME_FALLING_BLOCKS;++i){
        const GmRuntimeFallingBlock *e = &r->falling_blocks[i];
        if (!e->active) continue;
        fprintf(out,"%s{\"kind\":\"falling_block\",\"eid\":%d,\"type\":38,"
                    "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                    "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,"
                    "\"yaw\":0,\"pitch\":0,\"health\":-1,"
                    "\"block\":%d,\"meta\":%d,\"fall_time\":%d,"
                    "\"origin_x\":%d,\"origin_y\":%d,\"origin_z\":%d}",
                first?"":",",e->eid,e->x,e->y,e->z,e->vx,e->vy,e->vz,
                e->block,e->meta,e->fall_time,
                e->origin_x,e->origin_y,e->origin_z);
        first=0;
    }
    for (int i = 0; i < GM_RUNTIME_PRIMED_TNT; ++i) {
        const GmRuntimePrimedTnt *e = &r->primed_tnt[i];
        if (!e->active || e->dimension != r->dimension) continue;
        fprintf(out,
                "%s{\"kind\":\"primed_tnt\",\"eid\":%d,\"type\":39,"
                "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,"
                "\"yaw\":0,\"pitch\":0,\"health\":-1,\"fuse\":%d}",
                first ? "" : ",", e->eid, e->x, e->y, e->z,
                e->vx, e->vy, e->vz, e->fuse);
        first = 0;
    }
    for (int i = 0; i < GM_RUNTIME_END_CRYSTALS; ++i) {
        const GmRuntimeEndCrystal *e = &r->end_crystals[i];
        if (!e->active || e->dimension != r->dimension) continue;
        fprintf(out,
                "%s{\"kind\":\"end_crystal\",\"eid\":%d,\"type\":40,"
                "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                "\"vx\":0,\"vy\":0,\"vz\":0,"
                "\"yaw\":0,\"pitch\":0,\"health\":-1,"
                "\"inner_rotation\":%d,\"show_bottom\":%d,"
                "\"has_beam\":%d,\"beam_x\":%d,\"beam_y\":%d,"
                "\"beam_z\":%d}",
                first ? "" : ",", e->eid, e->x, e->y, e->z,
                e->inner_rotation, e->show_bottom, e->has_beam,
                e->beam_x, e->beam_y, e->beam_z);
        first = 0;
    }
    fprintf(out, "],\"furnace\":");
    if (r->active_furnace >= 0) {
        const FurnaceLive *f=&r->furnaces[r->active_furnace].state;
        fprintf(out,"{\"input\":[%d,%d,%d],\"fuel\":[%d,%d,%d],"
                    "\"output\":[%d,%d,%d],\"burn\":%d,\"cook\":%d,\"cook_total\":%d}",
                f->input.item,f->input.count,f->input.meta,
                f->fuel.item,f->fuel.count,f->fuel.meta,
                f->output.item,f->output.count,f->output.meta,
                f->burn_time,f->cook_time,f->total_cook);
    } else fprintf(out,"null");
    /* Tape-driven render ghosts, exactly as ingested: the Java-truth entity
     * rows come back out so replay can assert the tape->magma pipeline did
     * not drop, cap, or corrupt them (positions are float32 of the taped
     * doubles). */
    fprintf(out, ",\"ghost_views\":[");
    {
        GmEntityView ghosts[GM_RUNTIME_GHOST_VIEWS];
        int ng = gm_runtime_ghost_views(r, ghosts, GM_RUNTIME_GHOST_VIEWS);
        for (int i = 0; i < ng; ++i)
            fprintf(out, "%s{\"type\":%d,\"x\":%.9g,\"y\":%.9g,\"z\":%.9g}",
                    i ? "," : "", ghosts[i].type, (double)ghosts[i].x,
                    (double)ghosts[i].y, (double)ghosts[i].z);
    }
    fprintf(out, "]");
    int anchor[3];
    unsigned long long nh = nearby_hash(r, anchor);
    fprintf(out, ",\"nearby_anchor\":[%d,%d,%d]", anchor[0],anchor[1],anchor[2]);
    {
        int every = nearby_blocks_every();
        long long offset = nearby_blocks_offset();
        if (every > 0 && r->tick >= offset
                && (r->tick - offset) % every == 0)
            write_nearby_blocks(out, r, &v);
    }
    fprintf(out, ",\"nearby_hash\":\"%016llx\",\"terminal\":%s}\n",
            nh, v.dead ? "\"death\"" : (r->won?"\"won\"":"null"));
}

/* MAGMA_STATE_PROF=1: batched-env sizing census, printed to stderr at run
 * end. Distinct packed states (id<<4|meta, air included) per chunk and per
 * 3x3-chunk window bound the u8-palette budget; non-air 16^3 sections per
 * chunk bound section elision. Scans the 9x9 chunks around the player (must
 * be inside the generated radius). */
static void prof_scan(GmRuntime *r) {
    int pcx = (int)floor((r->player.ent.posX + (double)r->ox) / 16.0);
    int pcz = (int)floor((r->player.ent.posZ + (double)r->oz) / 16.0);
    /* render-off runs only generate the physics window; a census over
     * ungenerated (all-air) chunks would undercount everything. */
    gm_world_ensure(r->world, pcx, pcz, 5);
    static unsigned char seen[65536];
    int worst_chunk = 0, sec_tot = 0, sec_max = 0, nchunks = 0;
    for (int dz = -4; dz <= 4; ++dz) for (int dx = -4; dx <= 4; ++dx) {
        int cx = pcx + dx, cz = pcz + dz, nsec = 0, ndist = 0;
        memset(seen, 0, sizeof seen);
        for (int s = 0; s < 16; ++s) {
            int has = 0;
            for (int y = s * 16; y < s * 16 + 16; ++y)
                for (int lz = 0; lz < 16; ++lz)
                    for (int lx = 0; lx < 16; ++lx) {
                        int wx = cx * 16 + lx, wz = cz * 16 + lz;
                        unsigned st = (unsigned)((gm_world_block(r->world, wx, y, wz) << 4) |
                                                 gm_world_meta(r->world, wx, y, wz));
                        if (st) has = 1;
                        if (!seen[st]) { seen[st] = 1; ndist++; }
                    }
            nsec += has;
        }
        nchunks++; sec_tot += nsec;
        if (nsec > sec_max) sec_max = nsec;
        if (ndist > worst_chunk) worst_chunk = ndist;
    }
    int worst_win = 0;
    for (int wz = -1; wz <= 1; ++wz) for (int wx = -1; wx <= 1; ++wx) {
        int ndist = 0;
        memset(seen, 0, sizeof seen);
        for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) {
            int cx = pcx + wx + dx, cz = pcz + wz + dz;
            for (int y = 0; y < 256; ++y)
                for (int lz = 0; lz < 16; ++lz)
                    for (int lx = 0; lx < 16; ++lx) {
                        unsigned st = (unsigned)((gm_world_block(r->world, cx * 16 + lx, y, cz * 16 + lz) << 4) |
                                                 gm_world_meta(r->world, cx * 16 + lx, y, cz * 16 + lz));
                        if (!seen[st]) { seen[st] = 1; ndist++; }
                    }
        }
        if (ndist > worst_win) worst_win = ndist;
    }
    fprintf(stderr, "[state_prof] census 9x9@(%d,%d): distinct states max %d/chunk, "
            "max %d/3x3-window; non-air sections mean %.1f max %d of 16\n",
            pcx, pcz, worst_chunk, worst_win, (double)sec_tot / nchunks, sec_max);
}

int gm_script_run(const GmConfig *cfg) {
    FILE *in = NULL, *out = stdout;
    if (cfg->script_path) { in = fopen(cfg->script_path, "r"); if (!in) { perror("script"); return 1; } }
    if (cfg->state_out_path) { out = fopen(cfg->state_out_path, "w"); if (!out) { perror("state-out"); if(in)fclose(in); return 1; } }
    GmRuntime r; char err[256];
    if (!gm_runtime_init(&r, cfg, err, sizeof err)) { fprintf(stderr,"runtime: %s\n",err); return 1; }
    /* Tape replay: never run live random-tick engine (oracle world RNG is
     * unseedable; terrain evolution is carried by snapshots, not re-simulated). */
    r.randtick_enabled = 0;
    GmFrameCapture *frames=NULL;
    GmWindowCompose *window_frames=NULL;
    GmParticlesLive replay_particles;
    uint64_t replay_particle_seed =
        (uint64_t)cfg->seed ^ UINT64_C(0x7061727469636c65);
    gm_particles_live_init(&replay_particles, replay_particle_seed);
    if(cfg->frames_out_dir){
        /* The oracle's frames carry the survival HUD (hearts/hunger/hotbar/
         * crosshair); headless frames must draw it too or every whole-frame
         * diff eats the missing overlay. gm_hud_draw silently no-ops until
         * gm_hud_init has run - the interactive path inits it, this script
         * path never did (that WAS the largest pixel cluster on the 12k-tape
         * poses: 2.1/ch of pose A's 3.42, 3.8/ch of pose B's 9.01). */
        gm_hud_init();
        if(cfg->compose==GM_COMPOSE_WINDOW){
            window_frames=gm_window_compose_open(cfg,err,sizeof err);
            if(window_frames)
                gm_window_compose_bind(window_frames,&r,&replay_particles);
        }else{
            frames=gm_frame_capture_open(cfg,err,sizeof err);
            if(frames)gm_frame_capture_bind_particles(frames,&replay_particles);
        }
        if(!frames&&!window_frames){fprintf(stderr,"frames-out: %s\n",err);gm_runtime_destroy(&r);if(in)fclose(in);if(out!=stdout)fclose(out);return 1;}
    }
    char line[2048] = {0}; long line_no = 0; JlObject pending; int have = 0;
    long long pending_tick = -1;
    /* Saturated FoodStats regeneration is server-side, but tape rows are
     * client ticks. Preserve an early local heal's hidden exhaustion/timer
     * effects while deferring its visible health until the recorded packet. */
    float held_regen = 0.0f;
    int continue_after_death = 0;
    /* MAGMA_STATE_PROF: per-tick world-edit rate. gm_world_block_gen counts
     * every block edit (set_block_meta + populate gen events), so its per-tick
     * delta = journal entries/tick for a dirty-edit journal. Baseline taken
     * here so worldgen's one-shot fill is excluded. */
    int prof_on = getenv("MAGMA_STATE_PROF") != NULL;
    long long prof_last = 0, prof_tot = 0, prof_max = 0, prof_maxt = -1, prof_nz = 0;
    long long prof_h[5] = {0};   /* buckets: 0, 1-8, 9-64, 65-512, 513+ */
    if (prof_on) prof_last = gm_world_block_gen(r.world);
    /* A tape can contain GuiGameOver followed by SPacketRespawn on the next
     * row. Keep consuming scripted events while dead; gm_runtime_tick itself
     * remains inert until an authoritative positive set_vitals revives it. */
    for (int tick = 0; tick < cfg->ticks && !r.won &&
         (!r.dead || continue_after_death); ++tick) {
        /* renderable ghost entities are per-tick state: last tick's recorded
         * entities must not linger into a tick whose tape row has none. */
        gm_runtime_ent_views_clear(&r);
        /* same for open GUI screen views (divergence #9). */
        gm_runtime_gui_view_clear(&r);
        GmAction action; memset(&action,0,sizeof action); action.hotbar_sel=-1;
        int have_look = 0; double look_yaw = 0.0, look_pitch = 0.0;
        int have_vitals_post = 0; double vitals_health = 20.0; long long vitals_food = 20;
        int have_regen_post = 0; double regen_health = 20.0, regen_exhaustion = 0.0;
        long long regen_food = 20;
        int have_hold_regen_post = 0;
        int clear_hurt_velocity_post = 0;
        int hold_fall_damage_post = 0;
        int have_food_stats_post = 0;
        double food_stats_saturation = 5.0, food_stats_exhaustion = 0.0;
        int have_pose_post = 0, pose_on_ground = 0;
        double pose_x = 0.0, pose_y = 0.0, pose_z = 0.0;
        double pose_yaw = 0.0, pose_pitch = 0.0;
        double pose_vx = 0.0, pose_vy = 0.0, pose_vz = 0.0, pose_fall = 0.0;
        for (;;) {
            if (!have && in && fgets(line,sizeof line,in)) {
                line_no++;
                err[0] = 0;
                if (!strchr(line,'\n') && !feof(in)) { fprintf(stderr,"script:%ld: line too long\n",line_no); goto bad; }
                char *nl=strchr(line,'\n'); if(nl)*nl=0;
                if (!parse_object(line,&pending,err,sizeof err) ||
                    !as_i64(field(&pending,"tick"),&pending_tick) || pending_tick < 0) {
                    fprintf(stderr,"script:%ld: %s\n",line_no,err[0]?err:"invalid tick"); goto bad;
                }
                have=1;
            }
            if (!have || pending_tick > tick) break;
            if (pending_tick < tick) { fprintf(stderr,"script:%ld: events must be tick-sorted\n",line_no); goto bad; }
            const char *type;
            if (!as_string(field(&pending,"type"),&type)) { fprintf(stderr,"script:%ld: missing string type\n",line_no); goto bad; }
            if (!strcmp(type,"continue_after_death")) {
                static const char *const keys[]={"tick","type"};
                if(!keys_only(&pending,keys,2,err,sizeof err)){
                    fprintf(stderr,"script:%ld: invalid continue_after_death\n",line_no);
                    goto bad;
                }
                continue_after_death=1;
            } else if (!strcmp(type,"action")) {
                if (!parse_action(&pending,&action,err,sizeof err)) { fprintf(stderr,"script:%ld: %s\n",line_no,err); goto bad; }
            } else if (!strcmp(type,"set_pose")) {
                double x,y,z,yaw,pitch;
                static const char *const keys[]={"tick","type","x","y","z","yaw","pitch"};
                if (!keys_only(&pending,keys,7,err,sizeof err)||
                    !as_double(field(&pending,"x"),&x)||!as_double(field(&pending,"y"),&y)||
                    !as_double(field(&pending,"z"),&z)||!as_double(field(&pending,"yaw"),&yaw)||
                    !as_double(field(&pending,"pitch"),&pitch)) {
                    fprintf(stderr,"script:%ld: %s\n",line_no,err[0]?err:"invalid set_pose"); goto bad;
                }
                gm_runtime_set_pose(&r,x,y,z,(float)yaw,(float)pitch);
            } else if (!strcmp(type,"set_pose_state")) {
                double x,y,z,yaw,pitch,vx,vy,vz,fall;
                long long on_ground;
                static const char *const keys[]={"tick","type","x","y","z","yaw",
                    "pitch","vx","vy","vz","on_ground","fall"};
                if (!keys_only(&pending,keys,12,err,sizeof err)||
                    !as_double(field(&pending,"x"),&x)||
                    !as_double(field(&pending,"y"),&y)||
                    !as_double(field(&pending,"z"),&z)||
                    !as_double(field(&pending,"yaw"),&yaw)||
                    !as_double(field(&pending,"pitch"),&pitch)||
                    !as_double(field(&pending,"vx"),&vx)||
                    !as_double(field(&pending,"vy"),&vy)||
                    !as_double(field(&pending,"vz"),&vz)||
                    !as_i64(field(&pending,"on_ground"),&on_ground)||
                    (on_ground!=0&&on_ground!=1)||
                    !as_double(field(&pending,"fall"),&fall)||fall<0) {
                    fprintf(stderr,"script:%ld: %s\n",line_no,
                            err[0]?err:"invalid set_pose_state");
                    goto bad;
                }
                gm_runtime_set_pose_state(&r,x,y,z,(float)yaw,(float)pitch,
                                          vx,vy,vz,(int)on_ground,(float)fall);
            } else if (!strcmp(type,"set_look")) {
                double yaw,pitch;
                static const char *const keys[]={"tick","type","yaw","pitch"};
                if (!keys_only(&pending,keys,4,err,sizeof err)||
                    !as_double(field(&pending,"yaw"),&yaw)||
                    !as_double(field(&pending,"pitch"),&pitch)) {
                    fprintf(stderr,"script:%ld: invalid set_look\n",line_no); goto bad;
                }
                /* DEFERRED to after gm_runtime_tick: the tape records yaw/pitch
                 * POST-tick (the qrl bridge applies the quantized turn after the
                 * tick's physics, before recordTick; mouse look likewise lands
                 * between ticks). Tick t's move must run with the PREVIOUS look;
                 * the new look takes effect for state/frame capture at t and for
                 * tick t+1's physics. Found at t371 of the fresh-world tape: a
                 * mid-walk 15-degree turn accelerated magma along the new yaw
                 * one tick early (accel fit: oracle 0.070711 = yaw 0, magma
                 * 0.086603 = yaw -15). */
                have_look = 1; look_yaw = yaw; look_pitch = pitch;
            } else if (!strcmp(type,"set_look_pre")) {
                double yaw,pitch;
                static const char *const keys[]={"tick","type","yaw","pitch"};
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !as_double(field(&pending,"yaw"),&yaw)||
                   !as_double(field(&pending,"pitch"),&pitch)){
                    fprintf(stderr,"script:%ld: invalid set_look_pre\n",line_no);goto bad;
                }
                gm_runtime_set_look(&r,(float)yaw,(float)pitch);
            } else if (!strcmp(type,"set_pose_post")) {
                long long og;
                static const char *const keys[]={"tick","type","x","y","z","yaw","pitch",
                    "vx","vy","vz","on_ground","fall"};
                if(!keys_only(&pending,keys,12,err,sizeof err)||
                   !as_double(field(&pending,"x"),&pose_x)||
                   !as_double(field(&pending,"y"),&pose_y)||
                   !as_double(field(&pending,"z"),&pose_z)||
                   !as_double(field(&pending,"yaw"),&pose_yaw)||
                   !as_double(field(&pending,"pitch"),&pose_pitch)||
                   !as_double(field(&pending,"vx"),&pose_vx)||
                   !as_double(field(&pending,"vy"),&pose_vy)||
                   !as_double(field(&pending,"vz"),&pose_vz)||
                   !as_i64(field(&pending,"on_ground"),&og)||(og!=0&&og!=1)||
                   !as_double(field(&pending,"fall"),&pose_fall)||pose_fall<0){
                    fprintf(stderr,"script:%ld: invalid set_pose_post\n",line_no);goto bad;
                }
                pose_on_ground=(int)og;have_pose_post=1;
            } else if (!strcmp(type,"set_vitals")) {
                double health; long long food;
                static const char *const keys[]={"tick","type","health","food"};
                if (!keys_only(&pending,keys,4,err,sizeof err)||
                    !as_double(field(&pending,"health"),&health)||
                    !as_i64(field(&pending,"food"),&food)||
                    health<0||health>1024||food<0||food>20
                    || health>(double)r.vitals.maxHealth+1e-6) {
                    fprintf(stderr,"script:%ld: invalid set_vitals\n",line_no); goto bad;
                }
                gm_runtime_set_vitals(&r,(float)health,(int)food);
                held_regen=0.0f;
            } else if (!strcmp(type,"set_vitals_post")) {
                static const char *const keys[]={"tick","type","health","food"};
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !as_double(field(&pending,"health"),&vitals_health)||
                   !as_i64(field(&pending,"food"),&vitals_food)||
                   vitals_health<0||vitals_health>20||vitals_food<0||vitals_food>20){
                    fprintf(stderr,"script:%ld: invalid set_vitals_post\n",line_no);goto bad;
                }
                have_vitals_post=1;
            } else if (!strcmp(type,"set_regen_post")) {
                static const char *const keys[]={"tick","type","health","food","exhaustion"};
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_double(field(&pending,"health"),&regen_health)||
                   !as_i64(field(&pending,"food"),&regen_food)||
                   !as_double(field(&pending,"exhaustion"),&regen_exhaustion)||
                   regen_health<0||regen_health>20||regen_food<0||regen_food>20||
                   regen_exhaustion<0||regen_exhaustion>6){
                    fprintf(stderr,"script:%ld: invalid set_regen_post\n",line_no);goto bad;
                }
                have_regen_post=1;
            } else if (!strcmp(type,"hold_regen_post")) {
                static const char *const keys[]={"tick","type"};
                if(!keys_only(&pending,keys,2,err,sizeof err)){
                    fprintf(stderr,"script:%ld: invalid hold_regen_post\n",line_no);goto bad;
                }
                have_hold_regen_post=1;
            } else if (!strcmp(type,"clear_hurt_velocity_post")) {
                static const char *const keys[]={"tick","type"};
                if(!keys_only(&pending,keys,2,err,sizeof err)){
                    fprintf(stderr,"script:%ld: invalid clear_hurt_velocity_post\n",line_no);goto bad;
                }
                clear_hurt_velocity_post=1;
            } else if (!strcmp(type,"hold_fall_damage_post")) {
                static const char *const keys[]={"tick","type"};
                if(!keys_only(&pending,keys,2,err,sizeof err)){
                    fprintf(stderr,"script:%ld: invalid hold_fall_damage_post\n",line_no);goto bad;
                }
                hold_fall_damage_post=1;
            } else if (!strcmp(type,"set_food_stats_post")) {
                static const char *const keys[]={"tick","type","saturation","exhaustion"};
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !as_double(field(&pending,"saturation"),&food_stats_saturation)||
                   !as_double(field(&pending,"exhaustion"),&food_stats_exhaustion)||
                   food_stats_saturation<0||food_stats_saturation>20||
                   food_stats_exhaustion<0||food_stats_exhaustion>40){
                    fprintf(stderr,"script:%ld: invalid set_food_stats_post\n",line_no);goto bad;
                }
                have_food_stats_post=1;
            } else if (!strcmp(type,"set_food_stats")) {
                double saturation,exhaustion;
                static const char *const keys[]={"tick","type","saturation","exhaustion"};
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !as_double(field(&pending,"saturation"),&saturation)||
                   !as_double(field(&pending,"exhaustion"),&exhaustion)||
                   saturation<0||saturation>20||exhaustion<0||exhaustion>40){
                    fprintf(stderr,"script:%ld: invalid set_food_stats\n",line_no);goto bad;
                }
                gm_runtime_set_food_stats(&r,(float)saturation,(float)exhaustion);
            } else if (!strcmp(type,"set_food_timer")) {
                long long timer;
                static const char *const keys[]={"tick","type","timer"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"timer"),&timer)||
                   !gm_runtime_set_food_timer(&r,(int)timer)) {
                    fprintf(stderr,"script:%ld: invalid set_food_timer\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_player_xp")) {
                long long level,total; double fraction;
                static const char *const keys[]={
                    "tick","type","level","fraction","total"
                };
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_i64(field(&pending,"level"),&level)||
                   !as_double(field(&pending,"fraction"),&fraction)||
                   !as_i64(field(&pending,"total"),&total)||
                   level>INT_MAX||total>INT_MAX||
                   !gm_runtime_set_player_xp(
                       &r,(int)level,(float)fraction,(int)total)) {
                    fprintf(stderr,"script:%ld: invalid set_player_xp\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_player_combat")) {
                long long attack,hurt,resistant,death,dead,deaths;
                static const char *const keys[]={
                    "tick","type","attack_ticks","hurt_time",
                    "hurt_resistant_time","death_time","dead","deaths"
                };
                if(!keys_only(&pending,keys,8,err,sizeof err)||
                   !as_i64(field(&pending,"attack_ticks"),&attack)||
                   !as_i64(field(&pending,"hurt_time"),&hurt)||
                   !as_i64(field(&pending,"hurt_resistant_time"),&resistant)||
                   !as_i64(field(&pending,"death_time"),&death)||
                   !as_i64(field(&pending,"dead"),&dead)||
                   !as_i64(field(&pending,"deaths"),&deaths)||
                   !gm_runtime_set_player_combat(
                       &r,(int)attack,(int)hurt,(int)resistant,
                       (int)death,(int)dead,(int)deaths)) {
                    fprintf(stderr,"script:%ld: invalid set_player_combat\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_player_absorption")) {
                double value;
                static const char *const keys[]={"tick","type","value"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_double(field(&pending,"value"),&value)||
                   !gm_runtime_set_player_absorption(&r,(float)value)) {
                    fprintf(stderr,"script:%ld: invalid set_player_absorption\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_position_update_ticks")) {
                long long value,queued;
                static const char *const keys[]={
                    "tick","type","value","pending"
                };
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !as_i64(field(&pending,"value"),&value)||
                   !as_i64(field(&pending,"pending"),&queued)||
                   !gm_runtime_set_position_update_ticks(
                       &r,(int)value,(int)queued)) {
                    fprintf(stderr,
                            "script:%ld: invalid set_position_update_ticks\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_dimension")) {
                long long dimension;
                static const char *const keys[]={"tick","type","dimension"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"dimension"),&dimension)||
                   !gm_runtime_set_dimension(&r,(int)dimension)){
                    fprintf(stderr,"script:%ld: invalid set_dimension\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"set_velocity")) {
                /* optional on_ground: a mid-session tape can start with
                 * residual motion while standing - first-tick friction is
                 * 0.546 on ground vs 0.91 airborne, and a fresh player
                 * defaults to airborne, so tick 0 diverges in vx without it. */
                double x,y,z; long long og=-1;
                static const char *const keys[]={"tick","type","x","y","z","on_ground"};
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_double(field(&pending,"x"),&x)||!as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   (field(&pending,"on_ground")&&
                    (!as_i64(field(&pending,"on_ground"),&og)||(og!=0&&og!=1)))){
                    fprintf(stderr,"script:%ld: invalid set_velocity\n",line_no);goto bad;
                }
                gm_runtime_set_velocity(&r,x,y,z);
                if(og>=0)r.player.ent.onGround=(int)og;
            } else if (!strcmp(type,"set_packet_velocity")) {
                double x,y,z;
                static const char *const keys[]={"tick","type","x","y","z"};
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_double(field(&pending,"x"),&x)||!as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)){
                    fprintf(stderr,"script:%ld: invalid set_packet_velocity\n",line_no);goto bad;
                }
                gm_runtime_set_packet_velocity(&r,x,y,z);
            } else if (!strcmp(type,"add_velocity")) {
                /* SPacketExplosion knockback: handleExplosion ADDS the
                 * packet motion to the local player, unlike pvel. */
                double x,y,z;
                static const char *const keys[]={"tick","type","x","y","z"};
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_double(field(&pending,"x"),&x)||!as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)){
                    fprintf(stderr,"script:%ld: invalid add_velocity\n",line_no);goto bad;
                }
                gm_runtime_add_velocity(&r,x,y,z);
            } else if (!strcmp(type,"ent_box")) {
                /* Tape replay ghost pusher: recorded oracle entity box (world
                 * coords, feet y, width, height) applied as a vanilla
                 * applyEntityCollision player push during this tick. */
                double x,y,z,w,h;
                static const char *const keys[]={"tick","type","x","y","z","w","h"};
                if(!keys_only(&pending,keys,7,err,sizeof err)||
                   !as_double(field(&pending,"x"),&x)||!as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||!as_double(field(&pending,"w"),&w)||
                   !as_double(field(&pending,"h"),&h)||w<=0||h<=0){
                    fprintf(stderr,"script:%ld: invalid ent_box\n",line_no);goto bad;
                }
                gm_runtime_ent_box(&r,x,y,z,w,h);
            } else if (!strcmp(type,"dragon_contact")) {
                /* Recorded EntityDragon part query. Wing boxes carry 5 damage
                 * (collideWithEntities); head/neck boxes carry 10
                 * (attackEntitiesInList). Damage lands before FoodStats.onUpdate. */
                double x0,y0,z0,x1,y1,z1,damage;
                static const char *const keys[]={"tick","type","min_x","min_y","min_z",
                    "max_x","max_y","max_z","damage"};
                if(!keys_only(&pending,keys,9,err,sizeof err)||
                   !as_double(field(&pending,"min_x"),&x0)||
                   !as_double(field(&pending,"min_y"),&y0)||
                   !as_double(field(&pending,"min_z"),&z0)||
                   !as_double(field(&pending,"max_x"),&x1)||
                   !as_double(field(&pending,"max_y"),&y1)||
                   !as_double(field(&pending,"max_z"),&z1)||
                   !as_double(field(&pending,"damage"),&damage)||damage<=0){
                    fprintf(stderr,"script:%ld: invalid dragon_contact\n",line_no);goto bad;
                }
                (void)gm_runtime_dragon_contact(&r,x0,y0,z0,x1,y1,z1,(float)damage);
            } else if (!strcmp(type,"mob_damage")) {
                double damage;
                static const char *const keys[]={"tick","type","damage"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_double(field(&pending,"damage"),&damage)||damage<=0){
                    fprintf(stderr,"script:%ld: invalid mob_damage\n",line_no);goto bad;
                }
                (void)gm_mobs_attack_player(&r.mobs,
                    (struct PvStats *)&r.vitals, &r.player.inv,
                    (float)damage, 0);
                r.player.health=r.vitals.health;
            } else if (!strcmp(type,"ent_view")) {
                /* Tape replay renderable ghost entity (divergence #10): the
                 * recorded oracle entity is drawn by frame capture through
                 * the live-entity model path. Render-only; the physics-push
                 * ghost stays the separate ent_box event above. Types with
                 * no magma model are skipped (logged once per type). */
                const char *ent;double x,y,z,yaw,hp=-1.0,d;
                long long eid=-1,n;
                static const char *const keys[]={"tick","type","ent","x","y","z","yaw","hp","id",
                    "tape_pose","head_yaw","pitch","swing","hurt","death","body_yaw","flags",
                    "sheared","fleece","graze_y","graze_x","item","item_meta","count","age",
                    "hover","has_hover","crystal_rot","show_bottom","has_beam","beam_x","beam_y","beam_z",
                    "anim_time","death_ticks","phase_id","stationary",
                    "ticks_existed","has_heal_beam","heal_x","heal_y","heal_z","heal_crystal_ticks",
                    /* EntityXPOrb: item=xpValue, item_meta=xpColor, age=xpOrbAge */
                    "xp_value","xp_color",
                    /* EntityArmorStand equipment + saved display flags. */
                    "armor_feet","armor_legs","armor_chest","armor_head","stand_flags"};
                if(!keys_only(&pending,keys,
                              (int)(sizeof keys / sizeof keys[0]),err,sizeof err)||
                   !as_string(field(&pending,"ent"),&ent)||
                   !as_double(field(&pending,"x"),&x)||!as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||!as_double(field(&pending,"yaw"),&yaw)||
                   (field(&pending,"hp")&&!as_double(field(&pending,"hp"),&hp))||
                   (field(&pending,"id")&&!as_i64(field(&pending,"id"),&eid))){
                    fprintf(stderr,"script:%ld: invalid ent_view\n",line_no);goto bad;
                }
                GmEntityView view;memset(&view,0,sizeof view);
                view.type=!strcmp(ent,"EntityItem")&&field(&pending,"item")
                    ?GM_VIEW_ITEM:gm_entity_type_for_name(ent);
                /* EntityFallingBlock: fallTile id/meta travel as item/item_meta
                 * (RenderFallingBlock). Legacy 7-field rows have no state; NBT
                 * load default is sand (EntityFallingBlock.java:328). */
                if(view.type==GM_VIEW_FALLING_BLOCK&&!field(&pending,"item")){
                    view.item_id=12; /* Blocks.SAND */
                    view.item_meta=0;
                }
                view.skin=gm_entity_skin_for_name(ent);
                if(view.type==GM_VIEW_BILLBOARD||
                   view.type==GM_VIEW_DRAGON_FIREBALL)
                    view.item_id=gm_entity_billboard_item(ent);
                if(!strcmp(ent,"EntityLargeFireball"))
                    view.item_meta=2; /* RenderFireball scale 2 + large fire layers */
                view.x=(float)x;view.y=(float)y;view.z=(float)z;view.yaw=(float)yaw;
                view.health=(float)hp;view.ent_id=(int)eid;
                if (view.type == EW_TYPE_BOAT)
                    gm_runtime_tape_boat_view(&r, (int)eid, x, y, z, yaw);
#define OPT_I64(K,DST) do{if(field(&pending,K)){if(!as_i64(field(&pending,K),&n)){fprintf(stderr,"script:%ld: invalid ent_view %s\n",line_no,K);goto bad;}DST=(int)n;}}while(0)
#define OPT_DBL(K,DST) do{if(field(&pending,K)){if(!as_double(field(&pending,K),&d)){fprintf(stderr,"script:%ld: invalid ent_view %s\n",line_no,K);goto bad;}DST=(float)d;}}while(0)
                OPT_I64("tape_pose",view.tape_pose);OPT_DBL("head_yaw",view.head_yaw);
                OPT_DBL("pitch",view.pitch);OPT_DBL("swing",view.swing_progress);
                OPT_I64("hurt",view.hurt_time);OPT_I64("death",view.death_time);
                OPT_DBL("body_yaw",view.yaw);OPT_I64("flags",view.flags);
                OPT_I64("sheared",view.sheared);OPT_I64("fleece",view.fleece_color);
                OPT_DBL("graze_y",view.graze_y);OPT_DBL("graze_x",view.graze_x);
                OPT_I64("item",view.item_id);OPT_I64("item_meta",view.item_meta);
                OPT_I64("count",view.item_count);OPT_I64("age",view.age);
                /* Explicit XP orb aliases (same fields as item/item_meta). */
                OPT_I64("xp_value",view.item_id);OPT_I64("xp_color",view.item_meta);
                OPT_DBL("hover",view.hover_start);OPT_I64("has_hover",view.has_hover_start);
                view.beam_x=view.beam_y=view.beam_z=-1;
                OPT_DBL("crystal_rot",view.crystal_rot);OPT_I64("show_bottom",view.show_bottom);
                OPT_I64("beam_x",view.beam_x);OPT_I64("beam_y",view.beam_y);
                OPT_I64("beam_z",view.beam_z);
                view.has_beam=!(view.beam_x==-1&&view.beam_y==-1&&view.beam_z==-1);
                OPT_I64("has_beam",view.has_beam);
                OPT_DBL("anim_time",view.anim_time);OPT_I64("death_ticks",view.death_ticks);
                OPT_I64("phase_id",view.phase_id);OPT_I64("stationary",view.stationary);
                OPT_I64("ticks_existed",view.ticks_existed);
                OPT_I64("armor_feet",view.armor_feet);OPT_I64("armor_legs",view.armor_legs);
                OPT_I64("armor_chest",view.armor_chest);OPT_I64("armor_head",view.armor_head);
                OPT_I64("stand_flags",view.stand_flags);
                OPT_I64("has_heal_beam",view.has_heal_beam);
                OPT_DBL("heal_x",view.heal_x);OPT_DBL("heal_y",view.heal_y);
                OPT_DBL("heal_z",view.heal_z);
                OPT_I64("heal_crystal_ticks",view.heal_crystal_ticks);
#undef OPT_I64
#undef OPT_DBL
                int vt=view.type;
                if(vt<0){
                    static char warned[16][JL_VALUE];static int nwarned=0;
                    int seen=0;
                    for(int i=0;i<nwarned;++i)if(!strcmp(warned[i],ent)){seen=1;break;}
                    if(!seen&&nwarned<16){
                        snprintf(warned[nwarned++],JL_VALUE,"%s",ent);
                        fprintf(stderr,"script: ent_view %s: no model, skipped\n",ent);
                    }
                }else if(view.item_id<0||
                         (view.item_id>4095&&
                          !(view.type==GM_VIEW_DRAGON_FIREBALL&&
                            view.item_id==9003))||view.item_meta<0||
                         view.item_meta>32767||view.item_count<0||view.item_count>64||
                         view.fleece_color<0||view.fleece_color>15||
                         (view.flags&~15)||view.hurt_time<0||view.death_time<0){
                    fprintf(stderr,"script:%ld: invalid ent_view state\n",line_no);goto bad;
                }else if(view.armor_feet<0||view.armor_feet>4095||
                         view.armor_legs<0||view.armor_legs>4095||
                         view.armor_chest<0||view.armor_chest>4095||
                         view.armor_head<0||view.armor_head>4095||
                         (view.stand_flags&~7)){
                    fprintf(stderr,"script:%ld: invalid armor stand state\n",line_no);goto bad;
                }else gm_runtime_ent_view(&r,&view);
            } else if (!strcmp(type,"gui_view")) {
                /* Tape replay open container GUI (divergence #9): draw-only.
                 * Maps vanilla GuiScreen simple name -> container kind; mx/my
                 * are ScaledResolution coords (converted to fb px at draw).
                 * Unmapped screens (pause, chat, ...) are logged once + skipped. */
                const char *gui; long long mx = -1, my = -1;
                static const char *const keys[]={"tick","type","gui","mx","my"};
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_string(field(&pending,"gui"),&gui)||
                   (field(&pending,"mx")&&!as_i64(field(&pending,"mx"),&mx))||
                   (field(&pending,"my")&&!as_i64(field(&pending,"my"),&my))){
                    fprintf(stderr,"script:%ld: invalid gui_view\n",line_no);goto bad;
                }
                int kind = gm_screen_kind_for_gui(gui);
                if(kind < 0){
                    static char warned[16][JL_VALUE]; static int nwarned = 0;
                    int seen = 0;
                    for(int i=0;i<nwarned;++i)if(!strcmp(warned[i],gui)){seen=1;break;}
                    if(!seen&&nwarned<16){
                        snprintf(warned[nwarned++],JL_VALUE,"%s",gui);
                        fprintf(stderr,"script: gui_view %s: no container screen, skipped\n",gui);
                    }
                }else{
                    /* default mouse to gui-space center when gmx/gmy absent */
                    if(mx < 0 || my < 0){
                        int s = gm_screen_gui_scale(cfg->height > 0 ? cfg->height : 480);
                        int gw = ((cfg->width > 0 ? cfg->width : 854) + s - 1) / s;
                        int gh = ((cfg->height > 0 ? cfg->height : 480) + s - 1) / s;
                        if(mx < 0) mx = gw / 2;
                        if(my < 0) my = gh / 2;
                    }
                    gm_runtime_gui_view(&r, kind, (int)mx, (int)my);
                }
            } else if (!strcmp(type,"gui_slot_view") || !strcmp(type,"gui_cursor_view")) {
                /* Optional StoredEnchantments subset: n_ench + e0..e7 packed as
                 * (id<<16)|level. Absent => n_enchants=0 (backward compatible). */
                long long slot=0,item,count,meta,n_ench=0;
                int is_slot = !strcmp(type,"gui_slot_view");
                static const char *const keys_slot[]={
                    "tick","type","slot","item","count","meta","n_ench",
                    "e0","e1","e2","e3","e4","e5","e6","e7"
                };
                static const char *const keys_cur[]={
                    "tick","type","item","count","meta","n_ench",
                    "e0","e1","e2","e3","e4","e5","e6","e7"
                };
                ICStack st;
                if (is_slot) {
                    if(!keys_only(&pending,keys_slot,15,err,sizeof err)||
                       !as_i64(field(&pending,"slot"),&slot)||
                       !as_i64(field(&pending,"item"),&item)||
                       !as_i64(field(&pending,"count"),&count)||
                       !as_i64(field(&pending,"meta"),&meta)){
                        fprintf(stderr,"script:%ld: invalid gui_slot_view\n",line_no);goto bad;
                    }
                } else {
                    if(!keys_only(&pending,keys_cur,14,err,sizeof err)||
                       !as_i64(field(&pending,"item"),&item)||
                       !as_i64(field(&pending,"count"),&count)||
                       !as_i64(field(&pending,"meta"),&meta)){
                        fprintf(stderr,"script:%ld: invalid gui_cursor_view\n",line_no);goto bad;
                    }
                }
                st = count == 0 ? ic_empty() : ic_mk((i32)item,(i32)count,(i32)meta);
                if (field(&pending,"n_ench")) {
                    char ek[4];
                    int ei;
                    if (!as_i64(field(&pending,"n_ench"),&n_ench) ||
                        n_ench < 0 || n_ench > IC_MAX_ENCHANTS) {
                        fprintf(stderr,"script:%ld: invalid n_ench\n",line_no);goto bad;
                    }
                    st.n_enchants = (i32)n_ench;
                    for (ei = 0; ei < (int)n_ench; ++ei) {
                        long long packed = 0;
                        snprintf(ek, sizeof ek, "e%d", ei);
                        if (!as_i64(field(&pending, ek), &packed)) {
                            fprintf(stderr,"script:%ld: missing %s\n",line_no,ek);goto bad;
                        }
                        st.enchants[ei].id = (i16)((packed >> 16) & 0xffff);
                        st.enchants[ei].level = (i16)(packed & 0xffff);
                    }
                }
                if (is_slot) {
                    if (!gm_runtime_tape_gui_slot_stack(&r,(int)slot,st)) {
                        fprintf(stderr,"script:%ld: invalid gui_slot_view\n",line_no);goto bad;
                    }
                } else if (!gm_runtime_tape_gui_cursor_stack(&r,st)) {
                    fprintf(stderr,"script:%ld: invalid gui_cursor_view\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"gui_furnace_view")) {
                long long burn,current,cook,total;
                static const char *const keys[]={"tick","type","burn","current_burn","cook","total_cook"};
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"burn"),&burn)||
                   !as_i64(field(&pending,"current_burn"),&current)||
                   !as_i64(field(&pending,"cook"),&cook)||
                   !as_i64(field(&pending,"total_cook"),&total)||
                   burn>2147483647LL||current>2147483647LL||
                   cook>2147483647LL||total>2147483647LL||
                   !gm_runtime_tape_furnace(&r,(int)burn,(int)current,(int)cook,(int)total)){
                    fprintf(stderr,"script:%ld: invalid gui_furnace_view\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"gui_brewing_view")) {
                long long brew,fuel;
                static const char *const keys[]={
                    "tick","type","brew","fuel"
                };
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !as_i64(field(&pending,"brew"),&brew)||
                   !as_i64(field(&pending,"fuel"),&fuel)||
                   !gm_runtime_tape_brewing(
                       &r,(int)brew,(int)fuel)){
                    fprintf(
                        stderr,"script:%ld: invalid gui_brewing_view\n",
                        line_no);goto bad;
                }
            } else if (!strcmp(type,"set_time")) {
                long long value;
                static const char *const keys[]={"tick","type","value"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"value"),&value)||value<0){
                    fprintf(stderr,"script:%ld: invalid set_time\n",line_no);goto bad;
                }
                gm_runtime_set_time(&r,value);
            } else if (!strcmp(type,"set_total_time")) {
                long long value;
                static const char *const keys[]={"tick","type","value"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"value"),&value)||value<0){
                    fprintf(stderr,"script:%ld: invalid set_total_time\n",line_no);goto bad;
                }
                gm_runtime_set_total_time(&r,value);
            } else if (!strcmp(type,"redstone_torch_toggle")) {
                long long x,y,z,time;
                static const char *const keys[]={
                    "tick","type","x","y","z","time"
                };
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"time"),&time)||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX||
                   !gm_runtime_redstone_torch_toggle_add(
                       &r,(int)x,(int)y,(int)z,time)){
                    fprintf(
                        stderr,
                        "script:%ld: invalid redstone_torch_toggle\n",
                        line_no);goto bad;
                }
            } else if (!strcmp(type,"set_comparator_output")) {
                long long dimension,x,y,z,output;
                static const char *const keys[]={
                    "tick","type","dim","x","y","z","output_signal"
                };
                if(!keys_only(&pending,keys,7,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"output_signal"),&output)||
                   dimension<-1||dimension>1||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX||output<0||output>15||
                   !gm_runtime_comparator_set_output(
                       &r,(int)dimension,(int)x,(int)y,(int)z,
                       (int)output)){
                    fprintf(
                        stderr,
                        "script:%ld: invalid set_comparator_output\n",
                        line_no);goto bad;
                }
            } else if (!strcmp(type,"load_moving_piston")) {
                long long dimension,x,y,z,moved_block,moved_meta,facing;
                long long extending,source,progress_bits,last_progress_bits;
                union { unsigned u; float f; } progress,last_progress;
                static const char *const keys[]={
                    "tick","type","dim","x","y","z",
                    "moved_block","moved_meta","facing",
                    "extending","source","progress_bits",
                    "last_progress_bits"
                };
                if(!keys_only(&pending,keys,13,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"moved_block"),&moved_block)||
                   !as_i64(field(&pending,"moved_meta"),&moved_meta)||
                   !as_i64(field(&pending,"facing"),&facing)||
                   !as_i64(field(&pending,"extending"),&extending)||
                   !as_i64(field(&pending,"source"),&source)||
                   !as_i64(field(&pending,"progress_bits"),&progress_bits)||
                   !as_i64(field(&pending,"last_progress_bits"),
                           &last_progress_bits)||
                   dimension<-1||dimension>1||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX||
                   moved_block<1||moved_block>4095||
                   moved_meta<0||moved_meta>15||facing<0||facing>5||
                   extending<0||extending>1||source<0||source>1||
                   progress_bits<0||progress_bits>0xffffffffLL||
                   last_progress_bits<0||last_progress_bits>0xffffffffLL){
                    fprintf(stderr,
                            "script:%ld: invalid load_moving_piston\n",
                            line_no);goto bad;
                }
                progress.u=(unsigned)progress_bits;
                last_progress.u=(unsigned)last_progress_bits;
                if(!gm_runtime_moving_piston_load(
                       &r,(int)dimension,(int)x,(int)y,(int)z,
                       (int)moved_block,(int)moved_meta,(int)facing,
                       (int)extending,(int)source,
                       progress.f,last_progress.f)){
                    fprintf(stderr,
                            "script:%ld: invalid load_moving_piston\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_chest_slot")) {
                long long dimension,x,y,z,slot,item,count,meta;
                static const char *const keys[]={
                    "tick","type","dim","x","y","z",
                    "slot","item","count","meta"
                };
                if(!keys_only(&pending,keys,10,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"slot"),&slot)||
                   !as_i64(field(&pending,"item"),&item)||
                   !as_i64(field(&pending,"count"),&count)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   dimension<-1||dimension>1||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX||
                   slot<0||slot>=CHEST_LIVE_SLOTS||
                   item<0||item>4095||count<0||count>64||
                   meta<0||meta>32767||
                   !gm_runtime_chest_set_slot(
                       &r,(int)dimension,(int)x,(int)y,(int)z,
                       (int)slot,(int)item,(int)count,(int)meta)){
                    fprintf(
                        stderr,
                        "script:%ld: invalid set_chest_slot\n",
                        line_no);goto bad;
                }
            } else if (!strcmp(type,"set_furnace_slot")) {
                long long dimension,x,y,z,slot,item,count,meta;
                long long burn,current_burn,cook,total_cook;
                static const char *const keys[]={
                    "tick","type","dim","x","y","z",
                    "slot","item","count","meta",
                    "burn_time","current_burn_time",
                    "cook_time","total_cook_time"
                };
                if(!keys_only(&pending,keys,14,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"slot"),&slot)||
                   !as_i64(field(&pending,"item"),&item)||
                   !as_i64(field(&pending,"count"),&count)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   !as_i64(field(&pending,"burn_time"),&burn)||
                   !as_i64(
                       field(&pending,"current_burn_time"),&current_burn)||
                   !as_i64(field(&pending,"cook_time"),&cook)||
                   !as_i64(field(&pending,"total_cook_time"),&total_cook)||
                   dimension<-1||dimension>1||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX||
                   slot<0||slot>=FURNACE_LIVE_SLOT_COUNT||
                   item<0||item>4095||count<0||count>64||
                   meta<0||meta>32767||
                   burn<0||burn>32767||
                   current_burn<0||current_burn>32767||
                   cook<0||cook>32767||
                   total_cook<0||total_cook>32767||
                   !gm_runtime_furnace_set_slot(
                       &r,(int)dimension,(int)x,(int)y,(int)z,
                       (int)slot,(int)item,(int)count,(int)meta,
                       (int)burn,(int)current_burn,
                       (int)cook,(int)total_cook)){
                    fprintf(
                        stderr,
                        "script:%ld: invalid set_furnace_slot\n",
                        line_no);goto bad;
                }
            } else if (!strcmp(type,"set_brewing_slot")) {
                long long dimension,x,y,z,slot,item,count,meta;
                long long brew_time,fuel;
                static const char *const keys[]={
                    "tick","type","dim","x","y","z",
                    "slot","item","count","meta","brew_time","fuel"
                };
                if(!keys_only(&pending,keys,12,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"slot"),&slot)||
                   !as_i64(field(&pending,"item"),&item)||
                   !as_i64(field(&pending,"count"),&count)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   !as_i64(field(&pending,"brew_time"),&brew_time)||
                   !as_i64(field(&pending,"fuel"),&fuel)||
                   dimension<-1||dimension>1||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX||
                   slot<0||slot>=BREWING_LIVE_SLOTS||
                   item<0||item>4095||count<0||count>64||
                   meta<0||meta>32767||
                   brew_time<0||brew_time>TB_BREW_TICKS||
                   fuel<0||fuel>TB_FUEL_CHARGE||
                   !gm_runtime_brewing_set_slot(
                       &r,(int)dimension,(int)x,(int)y,(int)z,
                       (int)slot,(int)item,(int)count,(int)meta,
                       (int)brew_time,(int)fuel)){
                    fprintf(
                        stderr,
                        "script:%ld: invalid set_brewing_slot\n",
                        line_no);goto bad;
                }
            } else if (!strcmp(type,"set_static_container_slot")) {
                long long dimension,x,y,z,slot,item,count,meta;
                const JlField *nbt_file=field(&pending,"nbt_file");
                uint8_t *item_tag_nbt=NULL;
                size_t item_tag_nbt_len=0;
                static const char *const keys[]={
                    "tick","type","dim","x","y","z",
                    "slot","item","count","meta","nbt_file"
                };
                if(!keys_only(&pending,keys,11,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"slot"),&slot)||
                   !as_i64(field(&pending,"item"),&item)||
                   !as_i64(field(&pending,"count"),&count)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   dimension<-1||dimension>1||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX||
                   slot<0||slot>=GM_RUNTIME_STATIC_CONTAINER_SLOTS||
                   item<0||item>4095||count<0||count>64||
                   meta<0||meta>32767||
                   (nbt_file&&(!nbt_file->string||
                       !read_capsule_nbt(
                           nbt_file->value,
                           &item_tag_nbt,&item_tag_nbt_len)))||
                   !gm_runtime_static_container_set_slot(
                       &r,(int)dimension,(int)x,(int)y,(int)z,
                       (int)slot,(int)item,(int)count,(int)meta)||
                   (nbt_file&&!gm_runtime_shulker_set_item_tag_nbt(
                       &r,(int)dimension,(int)x,(int)y,(int)z,
                       item_tag_nbt,item_tag_nbt_len))){
                    free(item_tag_nbt);
                    fprintf(
                        stderr,
                        "script:%ld: invalid set_static_container_slot\n",
                        line_no);goto bad;
                }
                free(item_tag_nbt);
            } else if (!strcmp(type,"set_hopper_transfer_state")) {
                long long dimension,x,y,z,cooldown,ticked_time;
                static const char *const keys[]={
                    "tick","type","dim","x","y","z",
                    "transfer_cooldown","ticked_game_time"
                };
                if(!keys_only(&pending,keys,8,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"transfer_cooldown"),&cooldown)||
                   !as_i64(field(&pending,"ticked_game_time"),&ticked_time)||
                   dimension<-1||dimension>1||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX||
                   cooldown<INT_MIN||cooldown>INT_MAX||
                   !gm_runtime_hopper_set_transfer_state(
                       &r,(int)dimension,(int)x,(int)y,(int)z,
                       (int)cooldown,ticked_time)){
                    fprintf(
                        stderr,
                        "script:%ld: invalid set_hopper_transfer_state\n",
                        line_no);goto bad;
                }
            } else if (!strcmp(type,"set_flower_pot")) {
                long long dimension,x,y,z,item,meta;
                static const char *const keys[]={
                    "tick","type","dim","x","y","z","item","meta"
                };
                if(!keys_only(&pending,keys,8,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"item"),&item)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   dimension<-1||dimension>1||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX||
                   item<0||item>4095||meta<0||meta>32767||
                   (item==0&&meta!=0)||
                   !gm_runtime_flower_pot_set(
                       &r,(int)dimension,(int)x,(int)y,(int)z,
                       (int)item,(int)meta)){
                    fprintf(
                        stderr,
                        "script:%ld: invalid set_flower_pot\n",
                        line_no);goto bad;
                }
            } else if (!strcmp(type,"set_skull")) {
                long long dimension,x,y,z,skull_type,rotation;
                const JlField *nbt_file=field(&pending,"nbt_file");
                uint8_t *profile_nbt=NULL;
                size_t profile_nbt_len=0;
                static const char *const keys[]={
                    "tick","type","dim","x","y","z",
                    "skull_type","rotation","nbt_file"
                };
                if(!keys_only(&pending,keys,9,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"skull_type"),&skull_type)||
                   !as_i64(field(&pending,"rotation"),&rotation)||
                   dimension<-1||dimension>1||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX||
                   skull_type<0||skull_type>5||
                   rotation<0||rotation>15||
                   (nbt_file&&(!nbt_file->string||skull_type!=3||
                       !read_capsule_nbt(
                           nbt_file->value,&profile_nbt,&profile_nbt_len)))||
                   !(nbt_file
                       ? gm_runtime_skull_set_profile_nbt(
                           &r,(int)dimension,(int)x,(int)y,(int)z,
                           (int)skull_type,(int)rotation,
                           profile_nbt,profile_nbt_len)
                       : gm_runtime_skull_set(
                           &r,(int)dimension,(int)x,(int)y,(int)z,
                           (int)skull_type,(int)rotation))){
                    free(profile_nbt);
                    fprintf(
                        stderr,
                        "script:%ld: invalid set_skull\n",
                        line_no);goto bad;
                }
                free(profile_nbt);
            } else if (!strcmp(type,"set_command_block_success")) {
                long long dimension,x,y,z,success;
                static const char *const keys[]={
                    "tick","type","dim","x","y","z","success_count"
                };
                if(!keys_only(&pending,keys,7,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"success_count"),&success)||
                   dimension<-1||dimension>1||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX||
                   success<0||success>15||
                   !gm_runtime_command_block_set_success(
                       &r,(int)dimension,(int)x,(int)y,(int)z,
                       (int)success)){
                    fprintf(
                        stderr,
                        "script:%ld: invalid set_command_block_success\n",
                        line_no);goto bad;
                }
            } else if (!strcmp(type,"set_item_frame_source")) {
                long long dimension,eid,hanging_x,hanging_y,hanging_z;
                long long facing,item,count,meta,rotation;
                double x,y,z;
                static const char *const keys[]={
                    "tick","type","dim","eid","x","y","z",
                    "hanging_x","hanging_y","hanging_z",
                    "facing","item","count","meta","rotation"
                };
                if(!keys_only(&pending,keys,15,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"hanging_x"),&hanging_x)||
                   !as_i64(field(&pending,"hanging_y"),&hanging_y)||
                   !as_i64(field(&pending,"hanging_z"),&hanging_z)||
                   !as_i64(field(&pending,"facing"),&facing)||
                   !as_i64(field(&pending,"item"),&item)||
                   !as_i64(field(&pending,"count"),&count)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   !as_i64(field(&pending,"rotation"),&rotation)||
                   dimension<-1||dimension>1||
                   eid<=0||eid>INT_MAX||
                   hanging_x<INT_MIN||hanging_x>INT_MAX||
                   hanging_y<0||hanging_y>255||
                   hanging_z<INT_MIN||hanging_z>INT_MAX||
                   facing<2||facing>5||
                   item<0||item>4095||count<0||count>64||
                   meta<0||meta>32767||rotation<0||rotation>7||
                   !gm_runtime_item_frame_set(
                       &r,(int)dimension,(int)eid,x,y,z,
                       (int)hanging_x,(int)hanging_y,(int)hanging_z,
                       (int)facing,(int)item,(int)count,(int)meta,
                       (int)rotation)){
                    fprintf(
                        stderr,
                        "script:%ld: invalid set_item_frame_source\n",
                        line_no);goto bad;
                }
            } else if (!strcmp(type,"set_entity_id_cursor")) {
                long long value;
                static const char *const keys[]={"tick","type","value"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"value"),&value)||
                   value<0||value>2147483647LL||
                   !gm_runtime_set_entity_id_cursor(&r,(int)value)){
                    fprintf(stderr,
                            "script:%ld: invalid set_entity_id_cursor\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_world_random_seed")) {
                long long value;
                static const char *const keys[]={"tick","type","value"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"value"),&value)||
                   value<0||value>((1LL<<48)-1)||
                   !gm_runtime_set_world_random_seed48(
                       &r,(uint64_t)value)){
                    fprintf(stderr,
                            "script:%ld: invalid set_world_random_seed\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_math_random_seed")) {
                long long value;
                static const char *const keys[]={"tick","type","value"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"value"),&value)||
                   value<0||value>((1LL<<48)-1)||
                   !gm_runtime_set_math_random_seed48(
                       &r,(uint64_t)value)){
                    fprintf(stderr,
                            "script:%ld: invalid set_math_random_seed\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_world_random_gaussian")) {
                long long have_next;
                double next;
                static const char *const keys[]={
                    "tick","type","have_next","next"
                };
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !as_i64(field(&pending,"have_next"),&have_next)||
                   !as_double(field(&pending,"next"),&next)||
                   (have_next!=0&&have_next!=1)||!isfinite(next)||
                   !gm_runtime_set_world_random_gaussian(
                       &r,(int)have_next,next)){
                    fprintf(stderr,
                            "script:%ld: invalid set_world_random_gaussian\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_block_random_seed")) {
                long long value;
                static const char *const keys[]={"tick","type","value"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"value"),&value)||
                   value<0||value>((1LL<<48)-1)||
                   !gm_runtime_set_block_random_seed48(
                       &r,(uint64_t)value)){
                    fprintf(stderr,
                            "script:%ld: invalid set_block_random_seed\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_world_update_lcg")) {
                long long value;
                static const char *const keys[]={"tick","type","value"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"value"),&value)||
                   value<(-2147483647LL-1)||value>2147483647LL||
                   !gm_runtime_set_world_update_lcg(
                       &r,(int32_t)value)){
                    fprintf(stderr,
                            "script:%ld: invalid set_world_update_lcg\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"random_tick_block")) {
                long long x,y,z,block;
                static const char *const keys[]={
                    "tick","type","x","y","z","block"
                };
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"block"),&block)||
                   x<-2147483647LL-1||x>2147483647LL||
                   z<-2147483647LL-1||z>2147483647LL||
                   y<0||y>255||block<1||block>4095||
                   !gm_runtime_random_tick_block(
                       &r,(int)x,(int)y,(int)z,(int)block)){
                    fprintf(stderr,
                            "script:%ld: invalid random_tick_block\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"random_tick_selection")) {
                long long x,y,z,block,advances;
                static const char *const keys[]={
                    "tick","type","x","y","z","block",
                    "lcg_advances_before"
                };
                if(!keys_only(&pending,keys,7,err,sizeof err)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"block"),&block)||
                   !as_i64(
                       field(&pending,"lcg_advances_before"),&advances)||
                   x<-2147483647LL-1||x>2147483647LL||
                   z<-2147483647LL-1||z>2147483647LL||
                   y<0||y>255||block<1||block>4095||
                   advances<0||advances>1000000||
                   !gm_runtime_random_tick_selection(
                       &r,(int)x,(int)y,(int)z,(int)block,
                       (int)advances)){
                    fprintf(stderr,
                            "script:%ld: invalid random_tick_selection\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"schedule_tick")) {
                long long x,y,z,block,time,priority,order;
                static const char *const keys[]={
                    "tick","type","x","y","z","block","time","priority","order"
                };
                if(!keys_only(&pending,keys,9,err,sizeof err)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"block"),&block)||
                   !as_i64(field(&pending,"time"),&time)||
                   !as_i64(field(&pending,"priority"),&priority)||
                   !as_i64(field(&pending,"order"),&order)||
                   x<-2147483647LL-1||x>2147483647LL||
                   z<-2147483647LL-1||z>2147483647LL||
                   !gm_runtime_schedule_tick(
                       &r,(int)x,(int)y,(int)z,(int)block,time,
                       (int)priority,order)){
                    fprintf(stderr,"script:%ld: invalid schedule_tick\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"set_elytra")) {
                long long equipped;
                static const char *const keys[]={"tick","type","equipped"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"equipped"),&equipped)||
                   (equipped!=0&&equipped!=1)){
                    fprintf(stderr,"script:%ld: invalid set_elytra\n",line_no);goto bad;
                }
                gm_runtime_set_elytra(&r,(int)equipped);
            } else if (!strcmp(type,"set_elytra_flag7")) {
                long long flying;
                static const char *const keys[]={"tick","type","flying"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"flying"),&flying)||
                   (flying!=0&&flying!=1)){
                    fprintf(stderr,"script:%ld: invalid set_elytra_flag7\n",line_no);goto bad;
                }
                gm_runtime_set_elytra_flag7(&r,(int)flying);
            } else if (!strcmp(type,"set_skin")) {
                /* first-person arm variant: offline players get steve or alex
                 * by username-UUID hash; the tape header records which. */
                const char *skin;
                static const char *const keys[]={"tick","type","skin"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_string(field(&pending,"skin"),&skin)||
                   (strcmp(skin,"default")&&strcmp(skin,"slim"))){
                    fprintf(stderr,"script:%ld: invalid set_skin\n",line_no);goto bad;
                }
                gm_hand_set_skin(!strcmp(skin,"slim"));
            } else if (!strcmp(type,"set_weather")) {
                long long raining,thundering,rain_time,thunder_time;
                long long clean_weather_time=0,weather_cycle=1;
                double prev_rain=0.0,rain=0.0,prev_thunder=0.0,thunder=0.0;
                static const char *const keys[]={
                    "tick","type","raining","thundering",
                    "rain_time","thunder_time","clean_weather_time",
                    "weather_cycle","prev_rain_strength","rain_strength",
                    "prev_thunder_strength","thunder_strength"};
                int full=field(&pending,"clean_weather_time")!=NULL||
                    field(&pending,"weather_cycle")!=NULL||
                    field(&pending,"prev_rain_strength")!=NULL||
                    field(&pending,"rain_strength")!=NULL||
                    field(&pending,"prev_thunder_strength")!=NULL||
                    field(&pending,"thunder_strength")!=NULL;
                if(!keys_only(&pending,keys,12,err,sizeof err)||
                   !as_i64(field(&pending,"raining"),&raining)||
                   !as_i64(field(&pending,"thundering"),&thundering)||
                   !as_i64(field(&pending,"rain_time"),&rain_time)||
                   !as_i64(field(&pending,"thunder_time"),&thunder_time)||
                   (raining!=0&&raining!=1)||(thundering!=0&&thundering!=1)||
                   rain_time<0||rain_time>2147483647LL||
                   thunder_time<0||thunder_time>2147483647LL) {
                    fprintf(stderr,"script:%ld: invalid set_weather\n",line_no);goto bad;
                }
                if(full){
                    if(!as_i64(field(&pending,"clean_weather_time"),&clean_weather_time)||
                       !as_i64(field(&pending,"weather_cycle"),&weather_cycle)||
                       !as_double(field(&pending,"prev_rain_strength"),&prev_rain)||
                       !as_double(field(&pending,"rain_strength"),&rain)||
                       !as_double(field(&pending,"prev_thunder_strength"),&prev_thunder)||
                       !as_double(field(&pending,"thunder_strength"),&thunder)||
                       clean_weather_time<0||clean_weather_time>2147483647LL||
                       (weather_cycle!=0&&weather_cycle!=1)||
                       prev_rain<0.0||prev_rain>1.0||rain<0.0||rain>1.0||
                       prev_thunder<0.0||prev_thunder>1.0||
                       thunder<0.0||thunder>1.0){
                        fprintf(stderr,"script:%ld: invalid full set_weather\n",line_no);goto bad;
                    }
                    gm_runtime_set_weather_full(
                        &r,(int)raining,(int)thundering,
                        (int)rain_time,(int)thunder_time,
                        (int)clean_weather_time,(int)weather_cycle,
                        (float)prev_rain,(float)rain,
                        (float)prev_thunder,(float)thunder);
                }else{
                    gm_runtime_set_weather(&r,(int)raining,(int)thundering,
                                           (int)rain_time,(int)thunder_time);
                }
            } else if (!strcmp(type,"set_gamerules")) {
                McGameRules gamerules=r.gamerules;
                /* The recorder emits all string-backed rules. These three
                 * currently have magma runtime mechanics; every other string
                 * field is intentionally consumed without effect. */
                for(int i=0;i<pending.n;++i){
                    const JlField *rf=&pending.f[i];
                    if(!strcmp(rf->key,"tick")||!strcmp(rf->key,"type"))continue;
                    if(!rf->string){
                        fprintf(stderr,"script:%ld: gamerule %s must be a string\n",
                                line_no,rf->key);goto bad;
                    }
                }
                if(!as_rule_bool(field(&pending,"naturalRegeneration"),
                                 &gamerules.naturalRegeneration)||
                   !as_rule_bool(field(&pending,"doDaylightCycle"),
                                 &gamerules.doDaylightCycle)||
                   !as_rule_bool(field(&pending,"doWeatherCycle"),
                                 &gamerules.doWeatherCycle)){
                    fprintf(stderr,"script:%ld: invalid honored gamerule\n",line_no);goto bad;
                }
                gm_runtime_set_gamerules(&r,&gamerules);
            } else if (!strcmp(type,"set_daylight_cycle")) {
                long long enabled;
                static const char *const keys[]={"tick","type","enabled"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"enabled"),&enabled)||
                   (enabled!=0&&enabled!=1)){
                    fprintf(stderr,"script:%ld: invalid set_daylight_cycle\n",line_no);goto bad;
                }
                gm_runtime_set_daylight_cycle(&r,(int)enabled);
            } else if (!strcmp(type,"set_fire_rain_context")) {
                long long x,y,z,can_die,raining_at_east;
                long long can_die_west_candidate;
                static const char *const keys[]={
                    "tick","type","x","y","z","can_die",
                    "raining_at_east","can_die_west_candidate"
                };
                if(!keys_only(&pending,keys,8,err,sizeof err)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"can_die"),&can_die)||
                   !as_i64(field(&pending,"raining_at_east"),
                           &raining_at_east)||
                   !as_i64(field(&pending,"can_die_west_candidate"),
                           &can_die_west_candidate)||
                   x<-2147483647LL-1||x>2147483647LL||
                   z<-2147483647LL-1||z>2147483647LL||
                   y<0||y>255||(can_die!=0&&can_die!=1)||
                   (raining_at_east!=0&&raining_at_east!=1)||
                   (can_die_west_candidate!=0&&
                    can_die_west_candidate!=1)||
                   !gm_runtime_set_fire_rain_context(
                       &r,(int)x,(int)y,(int)z,(int)can_die,
                       (int)raining_at_east,
                       (int)can_die_west_candidate)){
                    fprintf(stderr,
                            "script:%ld: invalid set_fire_rain_context\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_fire_humidity_context")) {
                long long x,y,z;
                static const char *const keys[]={
                    "tick","type","x","y","z"
                };
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   x<-2147483647LL-1||x>2147483647LL||
                   z<-2147483647LL-1||z>2147483647LL||y<0||y>255||
                   !gm_runtime_set_fire_humidity_context(
                       &r,(int)x,(int)y,(int)z)){
                    fprintf(stderr,
                            "script:%ld: invalid set_fire_humidity_context\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_block")) {
                long long x,y,z,id,meta;
                static const char *const keys[]={"tick","type","x","y","z","id","meta"};
                if(!keys_only(&pending,keys,7,err,sizeof err)||
                   !as_i64(field(&pending,"x"),&x)||!as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||!as_i64(field(&pending,"id"),&id)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   x<-2147483647LL-1||x>2147483647LL||z<-2147483647LL-1||z>2147483647LL||
                   y<0||y>255||id<0||id>4095||meta<0||meta>15||
                   !gm_runtime_set_block(&r,(int)x,(int)y,(int)z,(int)id,(int)meta)) {
                    fprintf(stderr,"script:%ld: invalid set_block\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"harvest_block")) {
                long long x,y,z;
                static const char *const keys[]={"tick","type","x","y","z"};
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   x<-2147483647LL-1||x>2147483647LL||
                   z<-2147483647LL-1||z>2147483647LL||y<0||y>255||
                   !gm_runtime_harvest_block(&r,(int)x,(int)y,(int)z)) {
                    fprintf(stderr,"script:%ld: invalid harvest_block\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"begin_controlled_input")) {
                static const char *const keys[]={"tick","type"};
                if(!keys_only(&pending,keys,2,err,sizeof err)){
                    fprintf(stderr,
                            "script:%ld: invalid begin_controlled_input\n",
                            line_no);goto bad;
                }
                gm_runtime_begin_controlled_input(&r);
            } else if (!strcmp(type,"capture_controlled_input")) {
                static const char *const keys[]={"tick","type"};
                if(!keys_only(&pending,keys,2,err,sizeof err)){
                    fprintf(stderr,
                            "script:%ld: invalid capture_controlled_input\n",
                            line_no);goto bad;
                }
                gm_runtime_capture_controlled_input(&r);
            } else if (!strcmp(type,"snapshot_block")) {
                long long x,y,z,id,meta,dimension=0;
                static const char *const keys[]={"tick","type","x","y","z","id","meta","dim"};
                if(!keys_only(&pending,keys,8,err,sizeof err)||
                   !as_i64(field(&pending,"x"),&x)||!as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||!as_i64(field(&pending,"id"),&id)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   (field(&pending,"dim")&&!as_i64(field(&pending,"dim"),&dimension))||
                   dimension < -1||dimension > 1||
                   x<-2147483647LL-1||x>2147483647LL||z<-2147483647LL-1||z>2147483647LL||
                   y<0||y>255||id<0||id>4095||meta<0||meta>15||
                   !gm_runtime_load_block_dim(&r,(int)dimension,(int)x,(int)y,(int)z,
                                              (int)id,(int)meta)){
                    fprintf(stderr,"script:%ld: invalid snapshot_block\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"snapshot_blocks_finalize")) {
                long long cx,cz,radius,dimension=0;
                static const char *const keys[]={
                    "tick","type","cx","cz","radius","dim"
                };
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"cx"),&cx)||
                   !as_i64(field(&pending,"cz"),&cz)||
                   !as_i64(field(&pending,"radius"),&radius)||
                   (field(&pending,"dim")&&
                    !as_i64(field(&pending,"dim"),&dimension))||
                   dimension < -1||dimension > 1||
                   cx<-134217728LL||cx>134217727LL||
                   cz<-134217728LL||cz>134217727LL||
                   !gm_runtime_finalize_block_snapshot_dim(
                       &r,(int)dimension,(int)cx,(int)cz,(int)radius)){
                    fprintf(stderr,
                            "script:%ld: invalid snapshot_blocks_finalize\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"snapshot_sky_light")) {
                long long x,y,z,value,dimension=0;
                static const char *const keys[]={
                    "tick","type","x","y","z","value","dim"
                };
                if(!keys_only(&pending,keys,7,err,sizeof err)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"value"),&value)||
                   (field(&pending,"dim")&&
                    !as_i64(field(&pending,"dim"),&dimension))||
                   dimension < -1||dimension > 1||
                   x<-2147483647LL-1||x>2147483647LL||
                   z<-2147483647LL-1||z>2147483647LL||
                   y<0||y>255||value<0||value>15||
                   !gm_runtime_load_sky_light_dim(
                       &r,(int)dimension,(int)x,(int)y,(int)z,(int)value)){
                    fprintf(stderr,
                            "script:%ld: invalid snapshot_sky_light\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"snapshot_sky_light_finalize")) {
                long long dimension=0;
                static const char *const keys[]={"tick","type","dim"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   (field(&pending,"dim")&&
                    !as_i64(field(&pending,"dim"),&dimension))||
                   dimension < -1||dimension > 1||
                   !gm_runtime_finalize_sky_light_snapshot_dim(
                       &r,(int)dimension)){
                    fprintf(stderr,
                            "script:%ld: invalid snapshot_sky_light_finalize\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"snapshot_region")) {
                long long cx,cz,radius,dimension=0;
                static const char *const keys[]={"tick","type","cx","cz","radius","dim"};
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"cx"),&cx)||!as_i64(field(&pending,"cz"),&cz)||
                   !as_i64(field(&pending,"radius"),&radius)||
                   (field(&pending,"dim")&&!as_i64(field(&pending,"dim"),&dimension))||
                   dimension < -1||dimension > 1||
                   cx<-134217728LL||cx>134217727LL||cz<-134217728LL||cz>134217727LL||
                   !gm_runtime_snapshot_region_dim(&r,(int)dimension,(int)cx,(int)cz,
                                                  (int)radius)){
                    fprintf(stderr,"script:%ld: invalid snapshot_region\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"set_inventory")) {
                long long slot,item,count,meta,n_ench=0;
                static const char *const keys[]={
                    "tick","type","slot","item","count","meta","n_ench",
                    "e0","e1","e2","e3","e4","e5","e6","e7"};
                ICStack st;
                if(!keys_only(&pending,keys,15,err,sizeof err)||
                   !as_i64(field(&pending,"slot"),&slot)||
                   !as_i64(field(&pending,"item"),&item)||
                   !as_i64(field(&pending,"count"),&count)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   !((slot>=0&&slot<ISR_MAIN_SLOTS)||
                     (slot>=ISR_ARMOR0&&slot<ISR_ARMOR0+ISR_ARMOR_SLOTS)||
                     slot==ISR_OFFHAND_SLOT)||item<0||item>4095||
                   count<0||count>64||meta<0||meta>32767) {
                    fprintf(stderr,"script:%ld: invalid set_inventory\n",line_no);goto bad;
                }
                st=count==0?ic_empty():ic_mk((int)item,(int)count,(int)meta);
                if(field(&pending,"n_ench")){
                    if(!as_i64(field(&pending,"n_ench"),&n_ench)||
                       n_ench<0||n_ench>IC_MAX_ENCHANTS){
                        fprintf(stderr,"script:%ld: invalid set_inventory n_ench\n",line_no);goto bad;
                    }
                    st.n_enchants=(int)n_ench;
                    for(int ei=0;ei<(int)n_ench;++ei){
                        char ek[4];long long packed;
                        snprintf(ek,sizeof ek,"e%d",ei);
                        if(!as_i64(field(&pending,ek),&packed)){
                            fprintf(stderr,"script:%ld: missing set_inventory %s\n",line_no,ek);goto bad;
                        }
                        st.enchants[ei].id=(i16)((packed>>16)&0xffff);
                        st.enchants[ei].level=(i16)(packed&0xffff);
                    }
                }
                if(!gm_runtime_set_inventory_stack(&r,(int)slot,st)){
                    fprintf(stderr,"script:%ld: invalid set_inventory stack\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"set_selected_slot")) {
                long long slot;
                static const char *const keys[]={"tick","type","slot"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"slot"),&slot)||
                   !gm_runtime_set_selected_slot(&r,(int)slot)) {
                    fprintf(stderr,"script:%ld: invalid set_selected_slot\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"set_air")) {
                long long air;
                static const char *const keys[]={"tick","type","air"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"air"),&air)||
                   !gm_runtime_set_air(&r,(int)air)) {
                    fprintf(stderr,"script:%ld: invalid set_air\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"set_fire")) {
                long long fire;
                static const char *const keys[]={"tick","type","fire"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"fire"),&fire)||
                   !gm_runtime_set_fire(&r,(int)fire)) {
                    fprintf(stderr,"script:%ld: invalid set_fire\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"set_do_fire_tick")) {
                long long enabled;
                static const char *const keys[]={"tick","type","enabled"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"enabled"),&enabled)||
                   !gm_runtime_set_do_fire_tick(&r,(int)enabled)) {
                    fprintf(stderr,"script:%ld: invalid set_do_fire_tick\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"set_do_entity_drops")) {
                long long enabled;
                static const char *const keys[]={"tick","type","enabled"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"enabled"),&enabled)||
                   !gm_runtime_set_do_entity_drops(&r,(int)enabled)) {
                    fprintf(stderr,"script:%ld: invalid set_do_entity_drops\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"inv_view")) {
                long long slot,item,count,meta;
                static const char *const keys[]={"tick","type","slot","item","count","meta"};
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"slot"),&slot)||
                   !as_i64(field(&pending,"item"),&item)||
                   !as_i64(field(&pending,"count"),&count)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   !gm_runtime_tape_inventory(&r,(int)slot,(int)item,(int)count,(int)meta)){
                    fprintf(stderr,"script:%ld: invalid inv_view\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"player_view")) {
                long long xp,air,portal_frame=-1,portal_phase=0,loading=0,pinned=0;
                long long fire=0,creative=0,hurt=0,max_hurt=10;
                double frac,portal=0.0,hurt_yaw=0.0,attack_cooldown=1.0;
                static const char *const keys[]={"tick","type","xp_level","xp_frac","air",
                    "portal","portal_frame","portal_phase","loading","texture_animations_pinned",
                    "fire","creative","hurt","max_hurt","hurt_yaw","attack_cooldown"};
                if(!keys_only(&pending,keys,16,err,sizeof err)||
                   !as_i64(field(&pending,"xp_level"),&xp)||
                   !as_double(field(&pending,"xp_frac"),&frac)||
                   !as_i64(field(&pending,"air"),&air)||
                   (field(&pending,"portal")&&!as_double(field(&pending,"portal"),&portal))||
                   (field(&pending,"portal_frame")&&!as_i64(field(&pending,"portal_frame"),&portal_frame))||
                   (field(&pending,"portal_phase")&&!as_i64(field(&pending,"portal_phase"),&portal_phase))||
                   (field(&pending,"loading")&&!as_i64(field(&pending,"loading"),&loading))||
                   (field(&pending,"texture_animations_pinned")&&
                    !as_i64(field(&pending,"texture_animations_pinned"),&pinned))||
                   (field(&pending,"fire")&&!as_i64(field(&pending,"fire"),&fire))||
                   (field(&pending,"creative")&&!as_i64(field(&pending,"creative"),&creative))||
                   (field(&pending,"hurt")&&!as_i64(field(&pending,"hurt"),&hurt))||
                   (field(&pending,"max_hurt")&&!as_i64(field(&pending,"max_hurt"),&max_hurt))||
                   (field(&pending,"hurt_yaw")&&!as_double(field(&pending,"hurt_yaw"),&hurt_yaw))||
                   (field(&pending,"attack_cooldown")&&
                    !as_double(field(&pending,"attack_cooldown"),&attack_cooldown))||
                   /* vanilla drowning runs air down to -20 (damage pulse then
                    * resets it to 0), so negative values are legitimate tape data */
                   xp<0||xp>21863||frac<0||frac>1||air<-20||air>300||
                   portal<0||portal>1||portal_frame < -1||
                   portal_phase<0||loading<0||loading>2||pinned<0||pinned>1||
                   fire<0||fire>1||creative<0||creative>1||hurt<0||hurt>20||
                   max_hurt<0||max_hurt>20||attack_cooldown<0||attack_cooldown>1){
                    fprintf(stderr,"script:%ld: invalid player_view\n",line_no);goto bad;
                }
                gm_runtime_tape_player_view(&r,(int)xp,(float)frac,(int)air,
                    (float)portal,(int)portal_frame,(int)portal_phase,(int)loading,
                    (int)pinned,(int)fire,(int)creative,(int)hurt,
                    (int)max_hurt,(float)hurt_yaw,(float)attack_cooldown);
            } else if (!strcmp(type,"potion_fixture")) {
                long long id=0,amplifier=0,duration=0;
                const JlField *clear=field(&pending,"clear");
                static const char *const keys[]={
                    "tick","type","id","amplifier","duration","clear"
                };
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"id"),&id)||
                   !as_i64(field(&pending,"amplifier"),&amplifier)||
                   !as_i64(field(&pending,"duration"),&duration)||
                   !clear||clear->string||strcmp(clear->value,"true")||
                   id<1||id>255||amplifier<0||amplifier>255||
                   duration<=0||duration>INT_MAX){
                    fprintf(stderr,"script:%ld: invalid potion_fixture\n",line_no);goto bad;
                }
                gm_runtime_potions_clear(&r);
                if(!gm_runtime_potion_add(&r,(int)id,(int)amplifier,(int)duration)){
                    fprintf(stderr,"script:%ld: potion_fixture capacity exceeded\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"player_potions_clear")) {
                static const char *const keys[]={"tick","type"};
                if(!keys_only(&pending,keys,2,err,sizeof err)){
                    fprintf(stderr,"script:%ld: invalid player_potions_clear\n",line_no);goto bad;
                }
                gm_runtime_potions_clear(&r);
            } else if (!strcmp(type,"player_potion_add")) {
                long long id,amplifier,duration;
                static const char *const keys[]={
                    "tick","type","id","amplifier","duration"
                };
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_i64(field(&pending,"id"),&id)||
                   !as_i64(field(&pending,"amplifier"),&amplifier)||
                   !as_i64(field(&pending,"duration"),&duration)||
                   !gm_runtime_potion_add(
                       &r,(int)id,(int)amplifier,(int)duration)){
                    fprintf(stderr,"script:%ld: invalid player_potion_add\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"potion_clear")) {
                static const char *const keys[]={"tick","type"};
                if(!keys_only(&pending,keys,2,err,sizeof err)){
                    fprintf(stderr,"script:%ld: invalid potion_clear\n",line_no);goto bad;
                }
                gm_runtime_tape_potions_clear(&r);
            } else if (!strcmp(type,"potion_view")) {
                long long id,amplifier,duration,show=1;
                /* show_particles is optional: tapes recorded before the flag
                 * existed carry visible effects only by ASSUMPTION, so the
                 * legacy default stays 1 (the PotionEffect ctor default). */
                static const char *const keys[]={"tick","type","id","amplifier",
                                                 "duration","show_particles"};
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"id"),&id)||
                   !as_i64(field(&pending,"amplifier"),&amplifier)||
                   !as_i64(field(&pending,"duration"),&duration)||
                   (field(&pending,"show_particles")&&
                    !as_i64(field(&pending,"show_particles"),&show))||
                   !gm_runtime_tape_potion(&r,(int)id,(int)amplifier,(int)duration,
                                           (int)show)){
                    fprintf(stderr,"script:%ld: invalid potion_view\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"armor_view")) {
                /* Recorded ForgeHooks.getTotalArmorValue; -1 clears. */
                long long points;
                static const char *const keys[]={"tick","type","points"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"points"),&points)||points<-1||points>20){
                    fprintf(stderr,"script:%ld: invalid armor_view\n",line_no);goto bad;
                }
                gm_runtime_tape_armor(&r,(int)points);
            } else if (!strcmp(type,"spawn_entity")) {
                long long entity;double x,y,z;
                static const char *const keys[]={"tick","type","entity","x","y","z"};
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"entity"),&entity)||
                   !as_double(field(&pending,"x"),&x)||!as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   gm_mobs_spawn(&r.mobs,(int)entity,x,y,z)<0){
                    fprintf(stderr,"script:%ld: invalid spawn_entity\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_particle")) {
                long long id;double x,y,z,vx,vy,vz;
                static const char *const keys[]={"tick","type","id","x","y","z",
                                                 "vx","vy","vz"};
                if(!keys_only(&pending,keys,9,err,sizeof err)||
                   !as_i64(field(&pending,"id"),&id)||id<0||id>2||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !gm_particles_live_spawn_recorded(&replay_particles,(int)id,
                       x,y,z,vx,vy,vz,
                       gm_world_sky_light(r.world,(int)floor(x),(int)floor(y),
                                          (int)floor(z)),
                       gm_world_block_light(r.world,(int)floor(x),(int)floor(y),
                                            (int)floor(z)))){
                    fprintf(stderr,"script:%ld: invalid spawn_particle\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_xp_fixture")) {
                double x,y,z,vx,vy,vz;
                long long value,eid,age,delay,color,target_color;
                static const char *const keys[]={
                    "tick","type","x","y","z","vx","vy","vz",
                    "value","eid","age","pickup_delay","color","target_color"
                };
                if(!keys_only(&pending,keys,14,err,sizeof err)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_i64(field(&pending,"value"),&value)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"age"),&age)||
                   !as_i64(field(&pending,"pickup_delay"),&delay)||
                   !as_i64(field(&pending,"color"),&color)||
                   !as_i64(field(&pending,"target_color"),&target_color)||
                   !gm_runtime_spawn_xp_fixture(
                       &r,x,y,z,vx,vy,vz,(int)value,(int)eid,
                       (int)age,(int)delay,(int)color,(int)target_color)){
                    fprintf(stderr,"script:%ld: invalid spawn_xp_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_item_fixture")) {
                double x,y,z,vx,vy,vz;
                long long eid,item,count,meta,age,delay,stationary;
                static const char *const keys[]={
                    "tick","type","eid","x","y","z","vx","vy","vz",
                    "item","count","meta","age","pickup_delay",
                    "controlled_stationary"
                };
                if(!keys_only(&pending,keys,15,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_i64(field(&pending,"item"),&item)||
                   !as_i64(field(&pending,"count"),&count)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   !as_i64(field(&pending,"age"),&age)||
                   !as_i64(field(&pending,"pickup_delay"),&delay)||
                   !as_i64(field(&pending,"controlled_stationary"),&stationary)||
                   !gm_runtime_spawn_item_fixture(
                       &r,(int)eid,x,y,z,vx,vy,vz,(int)item,(int)count,
                       (int)meta,(int)age,(int)delay,(int)stationary)){
                    fprintf(stderr,"script:%ld: invalid spawn_item_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_falling_fixture")) {
                double x,y,z,vx,vy,vz;
                long long eid,block,meta,fall_time,no_gravity,no_ground;
                static const char *const keys[]={
                    "tick","type","eid","block","meta","fall_time",
                    "x","y","z","vx","vy","vz",
                    "no_gravity","no_ground"
                };
                if(!keys_only(&pending,keys,14,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"block"),&block)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   !as_i64(field(&pending,"fall_time"),&fall_time)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_i64(field(&pending,"no_gravity"),&no_gravity)||
                   !as_i64(field(&pending,"no_ground"),&no_ground)||
                   !gm_runtime_spawn_falling_fixture(
                       &r,(int)eid,(int)block,(int)meta,(int)fall_time,
                       x,y,z,vx,vy,vz,(int)no_gravity,(int)no_ground)){
                    fprintf(stderr,"script:%ld: invalid spawn_falling_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_arrow_fixture")) {
                double x,y,z,vx,vy,vz,yaw,pitch;
                long long eid,stationary,fire_ticks=0;
                static const char *const keys[]={
                    "tick","type","eid","x","y","z","vx","vy","vz",
                    "yaw","pitch","controlled_stationary","fire_ticks"
                };
                if(!keys_only(&pending,keys,13,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_double(field(&pending,"yaw"),&yaw)||
                   !as_double(field(&pending,"pitch"),&pitch)||
                   !as_i64(field(&pending,"controlled_stationary"),
                           &stationary)||
                   (field(&pending,"fire_ticks")&&
                    !as_i64(field(&pending,"fire_ticks"),&fire_ticks))||
                   yaw != 0.0||pitch != 0.0||
                   !gm_runtime_spawn_arrow_fixture(
                       &r,(int)eid,x,y,z,vx,vy,vz,(int)stationary,
                       (int)fire_ticks)){
                    fprintf(stderr,"script:%ld: invalid spawn_arrow_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_primed_tnt_fixture")) {
                double x,y,z,vx,vy,vz;
                long long eid,fuse;
                static const char *const keys[]={
                    "tick","type","eid","x","y","z","vx","vy","vz",
                    "fuse"
                };
                if(!keys_only(&pending,keys,10,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_i64(field(&pending,"fuse"),&fuse)||
                   !gm_runtime_spawn_primed_tnt_fixture(
                       &r,(int)eid,x,y,z,vx,vy,vz,(int)fuse)){
                    fprintf(stderr,
                            "script:%ld: invalid spawn_primed_tnt_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_end_crystal_fixture")) {
                double x,y,z;
                long long eid,inner_rotation,show_bottom,has_beam;
                long long beam_x,beam_y,beam_z;
                static const char *const keys[]={
                    "tick","type","eid","x","y","z","inner_rotation",
                    "show_bottom","has_beam","beam_x","beam_y","beam_z"
                };
                if(!keys_only(&pending,keys,12,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"inner_rotation"),&inner_rotation)||
                   !as_i64(field(&pending,"show_bottom"),&show_bottom)||
                   !as_i64(field(&pending,"has_beam"),&has_beam)||
                   !as_i64(field(&pending,"beam_x"),&beam_x)||
                   !as_i64(field(&pending,"beam_y"),&beam_y)||
                   !as_i64(field(&pending,"beam_z"),&beam_z)||
                   !gm_runtime_spawn_end_crystal_fixture(
                       &r,(int)eid,x,y,z,(int)inner_rotation,
                       (int)show_bottom,(int)has_beam,
                       (int)beam_x,(int)beam_y,(int)beam_z)){
                    fprintf(stderr,
                            "script:%ld: invalid spawn_end_crystal_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_small_fireball_fixture")) {
                double x,y,z,vx,vy,vz,ax,ay,az;
                long long eid;
                static const char *const keys[]={
                    "tick","type","eid","x","y","z","vx","vy","vz",
                    "ax","ay","az"
                };
                if(!keys_only(&pending,keys,12,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_double(field(&pending,"ax"),&ax)||
                   !as_double(field(&pending,"ay"),&ay)||
                   !as_double(field(&pending,"az"),&az)||
                   !gm_runtime_spawn_small_fireball_fixture(
                       &r,(int)eid,x,y,z,vx,vy,vz,ax,ay,az)){
                    fprintf(stderr,
                            "script:%ld: invalid spawn_small_fireball_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_potion_fixture")) {
                double x,y,z,vx,vy,vz;
                long long eid,potion_item,potion_type,age;
                static const char *const keys[]={
                    "tick","type","eid","potion_item","potion_type",
                    "x","y","z","vx","vy","vz","age"
                };
                if(!keys_only(&pending,keys,12,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"potion_item"),&potion_item)||
                   !as_i64(field(&pending,"potion_type"),&potion_type)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_i64(field(&pending,"age"),&age)||
                   !gm_runtime_spawn_potion_fixture(
                       &r,(int)eid,(int)potion_item,(int)potion_type,
                       x,y,z,vx,vy,vz,(int)age)){
                    fprintf(stderr,"script:%ld: invalid spawn_potion_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_area_effect_cloud_fixture")) {
                double x,y,z,radius,radius_on_use,radius_per_tick;
                long long eid,potion_type,age,duration,wait_time;
                long long reapplication_delay,next_application;
                static const char *const keys[]={
                    "tick","type","eid","potion_type","x","y","z",
                    "age","duration","wait_time","reapplication_delay",
                    "radius","radius_on_use","radius_per_tick",
                    "next_application"
                };
                if(!keys_only(&pending,keys,15,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"potion_type"),&potion_type)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"age"),&age)||
                   !as_i64(field(&pending,"duration"),&duration)||
                   !as_i64(field(&pending,"wait_time"),&wait_time)||
                   !as_i64(field(&pending,"reapplication_delay"),
                           &reapplication_delay)||
                   !as_double(field(&pending,"radius"),&radius)||
                   !as_double(field(&pending,"radius_on_use"),&radius_on_use)||
                   !as_double(field(&pending,"radius_per_tick"),
                              &radius_per_tick)||
                   !as_i64(field(&pending,"next_application"),
                           &next_application)||
                   !gm_runtime_spawn_area_effect_cloud_fixture(
                       &r,(int)eid,(int)potion_type,x,y,z,
                       (int)age,(int)duration,(int)wait_time,
                       (int)reapplication_delay,(float)radius,
                       (float)radius_on_use,(float)radius_per_tick,
                       (int)next_application)){
                    fprintf(stderr,
                            "script:%ld: invalid spawn_area_effect_cloud_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_fish_hook_fixture")) {
                long long eid,state,in_ground,ticks_in_ground,ticks_in_air;
                long long ticks_catchable,ticks_caught_delay;
                long long ticks_catchable_delay,lure,luck,caught_eid;
                long long entity_seed48,entity_have_gaussian;
                double x,y,z,vx,vy,vz,yaw,pitch,approach_angle;
                double entity_gaussian;
                static const char *const keys[]={
                    "tick","type","eid","x","y","z","vx","vy","vz",
                    "yaw","pitch","fish_state","in_ground",
                    "ticks_in_ground","ticks_in_air","ticks_catchable",
                    "ticks_caught_delay","ticks_catchable_delay",
                    "fish_approach_angle","lure","luck","caught_eid",
                    "entity_seed48","entity_have_gaussian",
                    "entity_gaussian"
                };
                if(!keys_only(&pending,keys,25,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_double(field(&pending,"yaw"),&yaw)||
                   !as_double(field(&pending,"pitch"),&pitch)||
                   !as_i64(field(&pending,"fish_state"),&state)||
                   !as_i64(field(&pending,"in_ground"),&in_ground)||
                   !as_i64(field(&pending,"ticks_in_ground"),
                           &ticks_in_ground)||
                   !as_i64(field(&pending,"ticks_in_air"),&ticks_in_air)||
                   !as_i64(field(&pending,"ticks_catchable"),
                           &ticks_catchable)||
                   !as_i64(field(&pending,"ticks_caught_delay"),
                           &ticks_caught_delay)||
                   !as_i64(field(&pending,"ticks_catchable_delay"),
                           &ticks_catchable_delay)||
                   !as_double(field(&pending,"fish_approach_angle"),
                              &approach_angle)||
                   !as_i64(field(&pending,"lure"),&lure)||
                   !as_i64(field(&pending,"luck"),&luck)||
                   !as_i64(field(&pending,"caught_eid"),&caught_eid)||
                   !as_i64(field(&pending,"entity_seed48"),&entity_seed48)||
                   !as_i64(field(&pending,"entity_have_gaussian"),
                           &entity_have_gaussian)||
                   !as_double(field(&pending,"entity_gaussian"),
                              &entity_gaussian)||
                   eid<=0||eid>INT_MAX||state<0||state>2||
                   in_ground<0||in_ground>1||ticks_in_ground<0||
                   ticks_in_ground>INT_MAX||ticks_in_air<0||
                   ticks_in_air>INT_MAX||ticks_catchable<0||
                   ticks_catchable>INT_MAX||
                   ticks_caught_delay<INT_MIN||ticks_caught_delay>INT_MAX||
                   ticks_catchable_delay<INT_MIN||
                   ticks_catchable_delay>INT_MAX||lure<0||lure>3||
                   luck<0||luck>3||caught_eid<0||caught_eid>INT_MAX||
                   entity_seed48<0||entity_seed48>((1LL<<48)-1)||
                   entity_have_gaussian<0||entity_have_gaussian>1||
                   !isfinite(x)||!isfinite(y)||!isfinite(z)||
                   !isfinite(vx)||!isfinite(vy)||!isfinite(vz)||
                   !isfinite(yaw)||!isfinite(pitch)||
                   !isfinite(approach_angle)||!isfinite(entity_gaussian)||
                   !gm_runtime_spawn_fish_hook_fixture(
                       &r,(int)eid,x,y,z,vx,vy,vz,(float)yaw,(float)pitch,
                       (int)state,(int)in_ground,(int)ticks_in_ground,
                       (int)ticks_in_air,(int)ticks_catchable,
                       (int)ticks_caught_delay,(int)ticks_catchable_delay,
                       (float)approach_angle,(int)lure,(int)luck,
                       (int)caught_eid,(uint64_t)entity_seed48,
                       (int)entity_have_gaussian,entity_gaussian)){
                    fprintf(stderr,
                            "script:%ld: invalid spawn_fish_hook_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_minecart_fixture")) {
                long long kind,eid,reverse,rolling_amplitude;
                long long rolling_direction,fuel,tnt_fuse;
                long long hopper_enabled,transfer_cooldown;
                long long entity_seed48,entity_have_gaussian;
                double x,y,z,vx,vy,vz,yaw,pitch,damage,push_x,push_z;
                double entity_gaussian;
                static const char *const keys[]={
                    "tick","type","kind","eid","x","y","z",
                    "vx","vy","vz","yaw","pitch","reverse",
                    "rolling_amplitude","rolling_direction","damage",
                    "fuel","push_x","push_z","tnt_fuse",
                    "hopper_enabled","transfer_cooldown","entity_seed48",
                    "entity_have_gaussian","entity_gaussian"
                };
                if(!keys_only(&pending,keys,25,err,sizeof err)||
                   !as_i64(field(&pending,"kind"),&kind)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_double(field(&pending,"yaw"),&yaw)||
                   !as_double(field(&pending,"pitch"),&pitch)||
                   !as_i64(field(&pending,"reverse"),&reverse)||
                   !as_i64(field(&pending,"rolling_amplitude"),
                           &rolling_amplitude)||
                   !as_i64(field(&pending,"rolling_direction"),
                           &rolling_direction)||
                   !as_double(field(&pending,"damage"),&damage)||
                   !as_i64(field(&pending,"fuel"),&fuel)||
                   !as_double(field(&pending,"push_x"),&push_x)||
                   !as_double(field(&pending,"push_z"),&push_z)||
                   !as_i64(field(&pending,"tnt_fuse"),&tnt_fuse)||
                   !as_i64(field(&pending,"hopper_enabled"),&hopper_enabled)||
                   !as_i64(field(&pending,"transfer_cooldown"),
                           &transfer_cooldown)||
                   !as_i64(field(&pending,"entity_seed48"),&entity_seed48)||
                   !as_i64(field(&pending,"entity_have_gaussian"),
                           &entity_have_gaussian)||
                   !as_double(field(&pending,"entity_gaussian"),
                              &entity_gaussian)||
                   kind<0||kind>6||kind==4||kind==6||eid<=0||eid>INT_MAX||
                   reverse<0||reverse>1||rolling_amplitude<0||
                   rolling_amplitude>INT_MAX||rolling_direction==0||
                   rolling_direction<INT_MIN||rolling_direction>INT_MAX||
                   fuel<-32768||fuel>32767||tnt_fuse<-1||
                   tnt_fuse>INT_MAX||hopper_enabled<0||hopper_enabled>1||
                   transfer_cooldown<INT_MIN||transfer_cooldown>INT_MAX||
                   entity_seed48<0||entity_seed48>((1LL<<48)-1)||
                   entity_have_gaussian<0||entity_have_gaussian>1||
                   !isfinite(x)||!isfinite(y)||!isfinite(z)||
                   !isfinite(vx)||!isfinite(vy)||!isfinite(vz)||
                   !isfinite(yaw)||!isfinite(pitch)||!isfinite(damage)||
                   !isfinite(push_x)||!isfinite(push_z)||
                   !isfinite(entity_gaussian)||
                   !gm_runtime_spawn_minecart_fixture(
                       &r,(int)kind,(int)eid,x,y,z,vx,vy,vz,(float)yaw)||
                   !gm_runtime_minecart_set_base_state(
                       &r,(int)eid,(int)reverse,(int)rolling_amplitude,
                       (int)rolling_direction,(float)damage,(float)pitch)||
                   !gm_runtime_minecart_set_state(
                       &r,(int)eid,(int)fuel,push_x,push_z,(int)tnt_fuse,
                       (int)hopper_enabled,(int)transfer_cooldown)||
                   !gm_runtime_minecart_set_random_state(
                       &r,(int)eid,(uint64_t)entity_seed48,
                       (int)entity_have_gaussian,entity_gaussian)){
                    fprintf(stderr,
                            "script:%ld: invalid spawn_minecart_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_minecart_slot")) {
                long long eid,slot,item,count,meta;
                static const char *const keys[]={
                    "tick","type","eid","slot","item","count","meta"
                };
                if(!keys_only(&pending,keys,7,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"slot"),&slot)||
                   !as_i64(field(&pending,"item"),&item)||
                   !as_i64(field(&pending,"count"),&count)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   eid<=0||eid>INT_MAX||slot<0||slot>=27||
                   item<1||item>4095||count<1||count>64||
                   meta<0||meta>32767||
                   !gm_runtime_minecart_set_slot(
                       &r,(int)eid,(int)slot,(int)item,
                       (int)count,(int)meta)){
                    fprintf(stderr,
                            "script:%ld: invalid set_minecart_slot\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_boat_fixture")) {
                double x,y,z,vx,vy,vz,yaw,pitch;
                long long eid,stationary;
                static const char *const keys[]={
                    "tick","type","eid","x","y","z","vx","vy","vz",
                    "yaw","pitch","controlled_stationary"
                };
                if(!keys_only(&pending,keys,12,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_double(field(&pending,"yaw"),&yaw)||
                   !as_double(field(&pending,"pitch"),&pitch)||
                   !as_i64(field(&pending,"controlled_stationary"),
                           &stationary)||
                   vx!=0.0||vy!=0.0||vz!=0.0||pitch!=0.0||stationary!=1||
                   !gm_runtime_spawn_boat_fixture(
                       &r,(int)eid,x,y,z,(float)yaw)){
                    fprintf(stderr,"script:%ld: invalid spawn_boat_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_mob_fixture")) {
                double x,y,z,vx,vy,vz,yaw,health;
                long long entity,eid,no_ai,hurt_time,death_time,hurt_resistant;
                static const char *const keys[]={
                    "tick","type","entity","eid","x","y","z","vx","vy","vz",
                    "yaw","health","no_ai","hurt_time","death_time",
                    "hurt_resistant_time"
                };
                if(!keys_only(&pending,keys,16,err,sizeof err)||
                   !as_i64(field(&pending,"entity"),&entity)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_double(field(&pending,"yaw"),&yaw)||
                   !as_double(field(&pending,"health"),&health)||
                   !as_i64(field(&pending,"no_ai"),&no_ai)||
                   !as_i64(field(&pending,"hurt_time"),&hurt_time)||
                   !as_i64(field(&pending,"death_time"),&death_time)||
                   !as_i64(field(&pending,"hurt_resistant_time"),&hurt_resistant)||
                   !gm_runtime_spawn_mob_fixture(
                       &r,(int)entity,(int)eid,x,y,z,vx,vy,vz,
                       (float)yaw,(float)health,(int)no_ai,(int)hurt_time,
                       (int)death_time,(int)hurt_resistant)){
                    fprintf(stderr,"script:%ld: invalid spawn_mob_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_villager_fixture")) {
                double x,y,z,vx,vy,vz,yaw,health,gaussian;
                long long eid,hurt_time,death_time,hurt_resistant;
                long long profession,living_sound_time,seed48,have_gaussian;
                static const char *const keys[]={
                    "tick","type","eid","x","y","z","vx","vy","vz",
                    "yaw","health","hurt_time","death_time",
                    "hurt_resistant_time","profession","living_sound_time",
                    "entity_seed48",
                    "entity_have_gaussian","entity_gaussian"
                };
                if(!keys_only(&pending,keys,19,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_double(field(&pending,"yaw"),&yaw)||
                   !as_double(field(&pending,"health"),&health)||
                   !as_i64(field(&pending,"hurt_time"),&hurt_time)||
                   !as_i64(field(&pending,"death_time"),&death_time)||
                   !as_i64(field(&pending,"hurt_resistant_time"),
                           &hurt_resistant)||
                   !as_i64(field(&pending,"profession"),&profession)||
                   !as_i64(field(&pending,"living_sound_time"),
                           &living_sound_time)||
                   !as_i64(field(&pending,"entity_seed48"),&seed48)||
                   !as_i64(field(&pending,"entity_have_gaussian"),
                           &have_gaussian)||
                   !as_double(field(&pending,"entity_gaussian"),&gaussian)||
                   !gm_runtime_spawn_villager_fixture(
                       &r,(int)eid,x,y,z,vx,vy,vz,(float)yaw,(float)health,
                       (int)hurt_time,(int)death_time,(int)hurt_resistant,
                       (int)profession,(int)living_sound_time,
                       (uint64_t)seed48,(int)have_gaussian,
                       gaussian)){
                    fprintf(stderr,
                            "script:%ld: invalid spawn_villager_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_mob_air")) {
                long long eid,air;
                static const char *const keys[]={"tick","type","eid","air"};
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"air"),&air)||
                   !gm_runtime_set_mob_air(&r,(int)eid,(int)air)){
                    fprintf(stderr,"script:%ld: invalid set_mob_air\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"craft")) {
                int width, slots[9];
                if (!parse_craft(&pending,&width,slots,err,sizeof err) ||
                    !gm_runtime_craft(&r,width,slots)) {
                    fprintf(stderr,"script:%ld: %s\n",line_no,err[0]?err:"craft failed"); goto bad;
                }
            } else if (!strcmp(type,"use_block")) {
                long long x,y,z;
                static const char *const keys[]={"tick","type","x","y","z"};
                if (!keys_only(&pending,keys,5,err,sizeof err)||
                    !as_i64(field(&pending,"x"),&x)||!as_i64(field(&pending,"y"),&y)||
                    !as_i64(field(&pending,"z"),&z)||
                    !gm_runtime_use_block(&r,(int)x,(int)y,(int)z)) {
                    fprintf(stderr,"script:%ld: use_block failed\n",line_no); goto bad;
                }
            } else if (!strcmp(type,"furnace_insert")) {
                static const char *const keys[]={"tick","type","slot","inventory","count"};
                long long slot,inv,count;
                if (!keys_only(&pending,keys,5,err,sizeof err)||
                    !as_i64(field(&pending,"slot"),&slot)||
                    !as_i64(field(&pending,"inventory"),&inv)||
                    !as_i64(field(&pending,"count"),&count)||
                    gm_runtime_furnace_insert(&r,(int)slot,(int)inv,(int)count)<=0) {
                    fprintf(stderr,"script:%ld: %s\n",line_no,err[0]?err:"furnace insert failed"); goto bad;
                }
            } else if (!strcmp(type,"furnace_extract")) {
                static const char *const keys[]={"tick","type","slot","count"};
                long long slot,count;
                if (!keys_only(&pending,keys,4,err,sizeof err)||
                    !as_i64(field(&pending,"slot"),&slot)||
                    !as_i64(field(&pending,"count"),&count)||
                    gm_runtime_furnace_extract(&r,(int)slot,(int)count)<=0) {
                    fprintf(stderr,"script:%ld: %s\n",line_no,err[0]?err:"furnace extract failed"); goto bad;
                }
            } else { fprintf(stderr,"script:%ld: unknown or forbidden type: %s\n",line_no,type); goto bad; }
            have=0;
        }
        float health_before_tick=r.vitals.health;
        int food_before_tick=r.vitals.foodLevel;
        gm_runtime_tick(&r,action);
        if(clear_hurt_velocity_post)gm_player_clear_inferred_hurt_velocity();
        if (prof_on) {
            long long bg = gm_world_block_gen(r.world), d = bg - prof_last;
            if (d < 0) d = 0;   /* dimension switch swapped in a fresh world */
            prof_last = bg; prof_tot += d;
            if (d > prof_max) { prof_max = d; prof_maxt = tick; }
            if (d) prof_nz++;
            prof_h[d == 0 ? 0 : d <= 8 ? 1 : d <= 64 ? 2 : d <= 512 ? 3 : 4]++;
        }
        if (have_pose_post)
            gm_runtime_set_pose_state(&r,pose_x,pose_y,pose_z,
                (float)pose_yaw,(float)pose_pitch,pose_vx,pose_vy,pose_vz,
                pose_on_ground,(float)pose_fall);
        if (have_look) gm_runtime_set_look(&r,(float)look_yaw,(float)look_pitch);
        if(hold_fall_damage_post &&
           r.vitals.health < health_before_tick-1e-6f)
            gm_runtime_set_vitals(&r,health_before_tick,r.vitals.foodLevel);
        if (have_hold_regen_post &&
            r.vitals.health > health_before_tick + 1e-6f) {
            held_regen += r.vitals.health-health_before_tick;
            gm_runtime_set_vitals(&r,health_before_tick,r.vitals.foodLevel);
        }
        if(have_hold_regen_post&&held_regen>0.0f&&
           r.vitals.foodLevel<food_before_tick)
            gm_runtime_set_vitals(&r,r.vitals.health,food_before_tick);
        if (have_vitals_post) {
            if(r.vitals.health+1e-6f<(float)vitals_health&&held_regen>0.0f){
                float visible=(float)vitals_health-r.vitals.health;
                held_regen=held_regen>visible?held_regen-visible:0.0f;
            }
            gm_runtime_set_vitals(&r,(float)vitals_health,(int)vitals_food);
        }
        if (have_regen_post && r.gamerules.naturalRegeneration) {
            if (r.vitals.health + 1e-6f < (float)regen_health) {
                float visible=(float)regen_health-r.vitals.health;
                if (held_regen + 1e-6f >= visible) {
                    held_regen-=visible;
                    if(held_regen<1e-6f)held_regen=0.0f;
                } else {
                    held_regen=0.0f;
                    pv_add_exhaustion(&r.vitals,(float)regen_exhaustion);
                    r.vitals.foodTimer=0;
                }
            }
            gm_runtime_set_vitals(&r,(float)regen_health,(int)regen_food);
        }
        if (have_food_stats_post) {
            r.vitals.saturation=(float)food_stats_saturation;
            r.vitals.exhaustion=(float)food_stats_exhaustion;
        }
        /* Flywheel probe: dump="tick,x0,x1,y0,y1,z0,z1" dumps id/meta of
         * a world region to stderr at that tick - the way to see magma's LIVE
         * world state mid-replay (fluid CA etc.), which no state-out field has. */
        {
            const char *dbg = cr_cfg()->dump;
            if (dbg[0]) {
                int dt,dx0,dx1,dy0,dy1,dz0,dz1;
                if (sscanf(dbg,"%d,%d,%d,%d,%d,%d,%d",&dt,&dx0,&dx1,&dy0,&dy1,&dz0,&dz1)==7 &&
                    (long long)dt==r.tick) {
                    for (int y=dy1;y>=dy0;--y){
                        fprintf(stderr,"[dump t%d y=%d]",dt,y);
                        for (int z=dz0;z<=dz1;++z){
                            fprintf(stderr," z%d:",z);
                            for (int x=dx0;x<=dx1;++x)
                                fprintf(stderr," %d/%d",
                                    gm_world_block(r.world,x,y,z),
                                    gm_world_meta(r.world,x,y,z));
                        }
                        fprintf(stderr,"\n");
                    }
                }
            }
        }
        /* Flywheel probe: worlddump="tick,cx0,cz0,ncx,ncz,path[;...]" writes
         * the LIVE world's canonical vanilla states for a chunk range in exactly
         * the trace/world_dump --states "CRWS" layout, so snapshot_patch.py can
         * diff the save against THE GAME'S OWN generation instead of world_dump's.
         * The two do not agree by construction: populate windows seed each other
         * with their neighbours' out-of-bounds spill (world/populate_mc.c
         * build_window), so decoration depends on the order windows were built,
         * and the game builds them around a walking player while world_dump
         * sweeps. Semicolon-separated specs let one run dump several ticks - the
         * dimension is whichever one the replay is in at that tick, and a range
         * wider than the resident pool needs several player-centred rectangles.
         * Non-resident chunks dump as all-zero and are counted on stderr: a
         * silent all-zero tile would read as "the game generated air here" and
         * would patch a whole real chunk away. */
        {
            const char *dbg = cr_cfg()->worlddump;
            for (const char *spec = dbg; spec && *spec; ) {
                int dt,cx0,cz0,ncx,ncz; char path[512];
                const char *next = strchr(spec, ';');
                if (sscanf(spec,"%d,%d,%d,%d,%d,%511[^;]",&dt,&cx0,&cz0,&ncx,&ncz,path)==6 &&
                    (long long)dt==r.tick && ncx>0 && ncz>0) {
                    FILE *wf=fopen(path,"wb");
                    if (!wf) { fprintf(stderr,"worlddump: cannot open %s\n",path); }
                    else {
                        long long zero=0; int32_t hdr[4]={cx0,cz0,ncx,ncz};
                        fwrite("CRWS",1,4,wf); fwrite(&zero,8,1,wf);
                        fwrite(hdr,sizeof(int32_t),4,wf);
                        static unsigned short blk[16*256*16];
                        static int32_t bio[16*16];
                        int missing=0;
                        for (int ix=0;ix<ncx;++ix) for (int iz=0;iz<ncz;++iz) {
                            int cx=cx0+ix, cz=cz0+iz, any=0;
                            for (int lx=0;lx<16;++lx) for (int lz=0;lz<16;++lz) {
                                bio[lx*16+lz]=gm_world_biome(r.world,cx*16+lx,cz*16+lz);
                                for (int y=0;y<256;++y) {
                                    int id=gm_world_block(r.world,cx*16+lx,y,cz*16+lz);
                                    int mt=gm_world_meta(r.world,cx*16+lx,y,cz*16+lz);
                                    blk[lx*4096+lz*256+y]=(unsigned short)((id<<4)|(mt&15));
                                    if (id) any=1;
                                }
                            }
                            if (!any) ++missing;
                            fwrite(blk,sizeof(unsigned short),16*256*16,wf);
                            fwrite(bio,sizeof(int32_t),16*16,wf);
                        }
                        fclose(wf);
                        fprintf(stderr,"[worlddump t%d] %d chunks (%d,%d)+%dx%d -> %s "
                                "(%d empty/non-resident)\n",
                                dt,ncx*ncz,cx0,cz0,ncx,ncz,path,missing);
                    }
                }
                spec = next ? next + 1 : NULL;
            }
        }
        /* Same, for light: dump_light="tick,x0,x1,y0,y1,z0,z1" dumps
         * "wx wy wz sky blk" lines (matches the qrl sample_light CSV columns)
         * so live-game light can be diffed cell-for-cell against magma's. */
        {
            const char *dbg = cr_cfg()->dump_light;
            if (dbg[0]) {
                int dt,dx0,dx1,dy0,dy1,dz0,dz1;
                if (sscanf(dbg,"%d,%d,%d,%d,%d,%d,%d",&dt,&dx0,&dx1,&dy0,&dy1,&dz0,&dz1)==7 &&
                    (long long)dt==r.tick) {
                    fprintf(stderr,"[dumplight t%d] wx wy wz sky blk\n",dt);
                    for (int y=dy0;y<=dy1;++y)
                        for (int z=dz0;z<=dz1;++z)
                            for (int x=dx0;x<=dx1;++x)
                                fprintf(stderr,"%d %d %d %d %d\n",x,y,z,
                                    gm_world_sky_light(r.world,x,y,z),
                                    gm_world_block_light(r.world,x,y,z));
                }
            }
        }
        write_state(out,&r);
        gm_particles_live_tick(&replay_particles,r.window,r.ox,r.oz);
        if(window_frames){
            int render = tick >= cfg->frame_offset &&
                         (tick - cfg->frame_offset) % cfg->frame_every == 0;
            GmPlayerView view;
            gm_runtime_view(&r,&view);
            gm_runtime_apply_tape_view(&r,&view);
            gm_window_compose_advance(window_frames,&view,&action,1);
            if(render){
                GmWindowComposeFrame wf={
                    .view=&view,.camera_view=&view,.partial_ticks=1.0f,
                    .interactive=1,.screen_open=0,
                    .mouse_x=cfg->width/2,.mouse_y=cfg->height/2,.stamp=NULL
                };
                if(!gm_window_compose_draw(window_frames,&wf,NULL,err,sizeof err)||
                   !gm_window_compose_emit_frame(window_frames,tick,err,sizeof err)){
                    fprintf(stderr,"frames-out: %s\n",err);goto bad;
                }
            }
        }else if(frames){
            int render = tick >= cfg->frame_offset &&
                         (tick - cfg->frame_offset) % cfg->frame_every == 0;
            if(!gm_frame_capture_write(frames,&r,&action,render,err,sizeof err)){
                fprintf(stderr,"frames-out: %s\n",err);goto bad;
            }
        }
    }
    if (have || (in && fgets(line,sizeof line,in))) { fprintf(stderr,"script: event lies beyond --ticks\n"); goto bad; }
    if (prof_on && r.tick > 0) {
        fprintf(stderr, "[state_prof] edits: total %lld over %lld ticks (mean %.2f/tick), "
                "max %lld @t%lld, nonzero ticks %lld (%.1f%%), "
                "hist 0|1-8|9-64|65-512|513+ = %lld|%lld|%lld|%lld|%lld\n",
                prof_tot, r.tick, (double)prof_tot / (double)r.tick,
                prof_max, prof_maxt, prof_nz, 100.0 * (double)prof_nz / (double)r.tick,
                prof_h[0], prof_h[1], prof_h[2], prof_h[3], prof_h[4]);
        prof_scan(&r);
    }
    if (!write_blocks_out(&r)) goto bad;
    if (!write_block_light_out(&r)) goto bad;
    if (!write_sky_light_out(&r)) goto bad;
    gm_frame_capture_close(frames);gm_window_compose_close(window_frames);gm_runtime_destroy(&r); if(in)fclose(in); if(out!=stdout)fclose(out); return 0;
bad:
    gm_frame_capture_close(frames);gm_window_compose_close(window_frames);gm_runtime_destroy(&r); if(in)fclose(in); if(out!=stdout)fclose(out); return 2;
}
