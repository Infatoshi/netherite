#define _POSIX_C_SOURCE 200809L
/* Gate: client world against the oracle's ClientWorldProbe recording.
 *
 *   test_clientworld RECORDING_DIR
 *
 * Checks:
 *   1. Snapshot loads into struct world, Det streams load from det.nbt;
 *   2. Clientworld initializes with initial cw_rand, cw_update_lcg, initial packets;
 *   3. Tick by tick:
 *      - Client entity interpolation (LivingBase onLivingUpdate);
 *      - Client world tick: setActivePlayerChunksAndCheckLight (4 draws) and
 *        doVoidFogParticles (1000 cells around player, Det.newRandom and block display ticks);
 *      - Verifies d.cw, d.cseed, d.cmath, d.cstat;
 *      - Verifies client entity list (ids, positions, serverPos, interpolation counters);
 *      - Verifies tracker packets (S0E, S0F, S11, S12, S15, S16, S17, S18, S19, S1C, S13);
 *      - Applies tracker packets to clientworld for the next tick;
 *   4. Negative checks:
 *      - Skip torch randomDisplayTick draw -> fails naming row and field;
 *      - Send S15 where code sends S17 -> fails naming row and field.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <zlib.h>

#include "../engine/clientworld.h"
#include "../engine/det.h"
#include "../engine/jrand.h"
#include "../engine/nbtjson.h"
#include "../engine/snapshot.h"
#include "../engine/tape.h"
#include "../engine/tracker.h"

static int fails;

static void fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("FAIL ");
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    ++fails;
}

static int64_t nbt_value_long(const nbt *v)
{
    if (v == NULL) return 0;
    char *t = nbt_render(v);
    if (t == NULL) return 0;
    int64_t n = 0;
    const char *p = t;
    if (strncmp(p, "\"l:", 3) == 0 || strncmp(p, "\"i:", 3) == 0 || strncmp(p, "\"s:", 3) == 0 || strncmp(p, "\"b:", 3) == 0)
    {
        n = strtoll(p + 3, NULL, 10);
    }
    else if (*p == '"')
    {
        n = strtoll(p + 1, NULL, 10);
    }
    else
    {
        n = strtoll(p, NULL, 10);
    }
    free(t);
    return n;
}

static char *nbt_value_str(const nbt *v)
{
    if (v == NULL) return NULL;
    char *t = nbt_render(v);
    if (t == NULL) return NULL;
    if (strncmp(t, "\"str:", 5) == 0)
    {
        size_t len = strlen(t + 5);
        if (len > 0 && (t + 5)[len - 1] == '"') (t + 5)[len - 1] = '\0';
        char *r = strdup(t + 5);
        free(t);
        return r;
    }
    free(t);
    return NULL;
}

static void load_det_from_nbt(det_state *det, const nbt *d)
{
    uint64_t seeder[DET_ROLES], math[DET_ROLES];
    int32_t next_id[DET_ROLES];

    for (int r = 0; r < DET_ROLES; ++r)
    {
        seeder[r] = (uint64_t)nbt_value_long(nbt_list_get(nbt_get(d, "seeder"), r));
        math[r] = (uint64_t)nbt_value_long(nbt_list_get(nbt_get(d, "math"), r));
        next_id[r] = (int32_t)nbt_value_long(nbt_list_get(nbt_get(d, "nextId"), r));
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
            st[r] = (uint64_t)nbt_value_long(nbt_list_get(nbt_get(e, "state"), r));
            used[r] = (uint8_t)nbt_value_long(nbt_list_get(nbt_get(e, "used"), r));
        }

        char *name = nbt_value_str(nbt_get(e, "name"));
        if (name != NULL)
        {
            det_split_add(det, name, st, used);
            free(name);
        }
    }
}

static void parse_packet_json(const struct jval *pj, struct tracker_packet *p)
{
    memset(p, 0, sizeof(*p));
    const char *pkt_name = json_str(json_get(pj, "pkt"));
    if (!pkt_name) return;

    if (strcmp(pkt_name, "S0F") == 0)
    {
        p->kind = TRACKER_PKT_S0F;
        int64_t v;
        if (json_int(json_get(pj, "id"), &v)) p->id = (int)v;
        if (json_int(json_get(pj, "type"), &v)) p->type = (int)v;
        if (json_int(json_get(pj, "x"), &v)) p->x = (int)v;
        if (json_int(json_get(pj, "y"), &v)) p->y = (int)v;
        if (json_int(json_get(pj, "z"), &v)) p->z = (int)v;
        if (json_int(json_get(pj, "yaw"), &v)) p->yaw = (int)v;
        if (json_int(json_get(pj, "pitch"), &v)) p->pitch = (int)v;
        if (json_int(json_get(pj, "head_yaw"), &v)) p->head_yaw = (int)v;
        if (json_int(json_get(pj, "mx"), &v)) p->mx = (int)v;
        if (json_int(json_get(pj, "my"), &v)) p->my = (int)v;
        if (json_int(json_get(pj, "mz"), &v)) p->mz = (int)v;
    }
    else if (strcmp(pkt_name, "S0D") == 0)
    {
        p->kind = TRACKER_PKT_S0D;
        int64_t v;
        if (json_int(json_get(pj, "id"), &v)) p->id = (int)v;
    }
    else if (strcmp(pkt_name, "S0E") == 0)
    {
        p->kind = TRACKER_PKT_S0E;
        int64_t v;
        if (json_int(json_get(pj, "id"), &v)) p->id = (int)v;
        if (json_int(json_get(pj, "type"), &v)) p->type = (int)v;
        if (json_int(json_get(pj, "data"), &v)) p->data = (int)v;
        if (json_int(json_get(pj, "x"), &v)) p->x = (int)v;
        if (json_int(json_get(pj, "y"), &v)) p->y = (int)v;
        if (json_int(json_get(pj, "z"), &v)) p->z = (int)v;
        if (json_int(json_get(pj, "pitch"), &v)) p->pitch = (int)v;
        if (json_int(json_get(pj, "yaw"), &v)) p->yaw = (int)v;
        if (json_int(json_get(pj, "mx"), &v)) p->mx = (int)v;
        if (json_int(json_get(pj, "my"), &v)) p->my = (int)v;
        if (json_int(json_get(pj, "mz"), &v)) p->mz = (int)v;
    }
    else if (strcmp(pkt_name, "S11") == 0)
    {
        p->kind = TRACKER_PKT_S11;
        int64_t v;
        if (json_int(json_get(pj, "id"), &v)) p->id = (int)v;
        if (json_int(json_get(pj, "x"), &v)) p->x = (int)v;
        if (json_int(json_get(pj, "y"), &v)) p->y = (int)v;
        if (json_int(json_get(pj, "z"), &v)) p->z = (int)v;
    }
    else if (strcmp(pkt_name, "S15") == 0)
    {
        p->kind = TRACKER_PKT_S15;
        int64_t v;
        if (json_int(json_get(pj, "id"), &v)) p->id = (int)v;
        if (json_int(json_get(pj, "dx"), &v)) p->x = (int8_t)v;
        if (json_int(json_get(pj, "dy"), &v)) p->y = (int8_t)v;
        if (json_int(json_get(pj, "dz"), &v)) p->z = (int8_t)v;
    }
    else if (strcmp(pkt_name, "S16") == 0)
    {
        p->kind = TRACKER_PKT_S16;
        int64_t v;
        if (json_int(json_get(pj, "id"), &v)) p->id = (int)v;
        if (json_int(json_get(pj, "yaw"), &v)) p->yaw = (int8_t)v;
        if (json_int(json_get(pj, "pitch"), &v)) p->pitch = (int8_t)v;
    }
    else if (strcmp(pkt_name, "S17") == 0)
    {
        p->kind = TRACKER_PKT_S17;
        int64_t v;
        if (json_int(json_get(pj, "id"), &v)) p->id = (int)v;
        if (json_int(json_get(pj, "dx"), &v)) p->x = (int8_t)v;
        if (json_int(json_get(pj, "dy"), &v)) p->y = (int8_t)v;
        if (json_int(json_get(pj, "dz"), &v)) p->z = (int8_t)v;
        if (json_int(json_get(pj, "yaw"), &v)) p->yaw = (int8_t)v;
        if (json_int(json_get(pj, "pitch"), &v)) p->pitch = (int8_t)v;
    }
    else if (strcmp(pkt_name, "S18") == 0)
    {
        p->kind = TRACKER_PKT_S18;
        int64_t v;
        if (json_int(json_get(pj, "id"), &v)) p->id = (int)v;
        if (json_int(json_get(pj, "x"), &v)) p->x = (int)v;
        if (json_int(json_get(pj, "y"), &v)) p->y = (int)v;
        if (json_int(json_get(pj, "z"), &v)) p->z = (int)v;
        if (json_int(json_get(pj, "yaw"), &v)) p->yaw = (int8_t)v;
        if (json_int(json_get(pj, "pitch"), &v)) p->pitch = (int8_t)v;
    }
    else if (strcmp(pkt_name, "S12") == 0)
    {
        p->kind = TRACKER_PKT_S12;
        int64_t v;
        if (json_int(json_get(pj, "id"), &v)) p->id = (int)v;
        if (json_int(json_get(pj, "mx"), &v)) p->mx = (int)v;
        if (json_int(json_get(pj, "my"), &v)) p->my = (int)v;
        if (json_int(json_get(pj, "mz"), &v)) p->mz = (int)v;
    }
    else if (strcmp(pkt_name, "S19") == 0)
    {
        p->kind = TRACKER_PKT_S19;
        int64_t v;
        if (json_int(json_get(pj, "id"), &v)) p->id = (int)v;
        if (json_int(json_get(pj, "head_yaw"), &v)) p->head_yaw = (int8_t)v;
    }
    else if (strcmp(pkt_name, "S1C") == 0)
    {
        p->kind = TRACKER_PKT_S1C;
        int64_t v;
        if (json_int(json_get(pj, "id"), &v)) p->id = (int)v;
    }
    else if (strcmp(pkt_name, "S13") == 0)
    {
        p->kind = TRACKER_PKT_S13;
        const struct jval *arr = json_get(pj, "ids");
        if (arr)
        {
            p->num_ids = json_len(arr);
            for (int i = 0; i < p->num_ids && i < 64; ++i)
            {
                int64_t idval;
                if (json_int(json_at(arr, i), &idval)) p->ids[i] = (int)idval;
            }
        }
    }
    else if (strcmp(pkt_name, "S1B") == 0)
    {
        p->kind = TRACKER_PKT_S1B;
        int64_t v;
        if (json_int(json_get(pj, "id"), &v)) p->id = (int)v;
        if (json_int(json_get(pj, "leash"), &v)) p->leash = (int)v;
        if (json_int(json_get(pj, "vehicle_id"), &v)) p->vehicle_id = (int)v;
    }
    else if (strcmp(pkt_name, "S04") == 0)
    {
        p->kind = TRACKER_PKT_S04;
        int64_t v;
        if (json_int(json_get(pj, "id"), &v)) p->id = (int)v;
        if (json_int(json_get(pj, "slot"), &v)) p->slot = (int)v;
    }
    else if (strcmp(pkt_name, "S20") == 0)
    {
        p->kind = TRACKER_PKT_S20;
        int64_t v;
        if (json_int(json_get(pj, "id"), &v)) p->id = (int)v;
    }
    else if (strcmp(pkt_name, "S23PacketBlockChange") == 0 || strcmp(pkt_name, "S23") == 0)
    {
        p->kind = TRACKER_PKT_S23;
        int64_t v;
        if (json_int(json_get(pj, "x"), &v)) p->x = (int)v;
        if (json_int(json_get(pj, "y"), &v)) p->y = (int)v;
        if (json_int(json_get(pj, "z"), &v)) p->z = (int)v;
        if (json_int(json_get(pj, "block"), &v)) p->block = (int)v;
        if (json_int(json_get(pj, "meta"), &v)) p->meta = (int)v;
    }
    else if (strcmp(pkt_name, "S22PacketMultiBlockChange") == 0 || strcmp(pkt_name, "S22") == 0)
    {
        p->kind = TRACKER_PKT_S22;
        const struct jval *records = json_get(pj, "records");
        if (records)
        {
            p->num_records = json_len(records);
            for (int i = 0; i < p->num_records && i < 64; ++i)
            {
                const struct jval *r = json_at(records, i);
                int64_t v;
                if (json_int(json_get(r, "x"), &v)) p->rx[i] = (int)v;
                if (json_int(json_get(r, "y"), &v)) p->ry[i] = (int)v;
                if (json_int(json_get(r, "z"), &v)) p->rz[i] = (int)v;
                if (json_int(json_get(r, "block"), &v)) p->rblock[i] = (int)v;
                if (json_int(json_get(r, "meta"), &v)) p->rmeta[i] = (int)v;
            }
        }
    }
    else if (strcmp(pkt_name, "S26PacketMapChunkBulk") == 0 || strcmp(pkt_name, "S26") == 0)
    {
        p->kind = TRACKER_PKT_S26;
        const struct jval *chunks = json_get(pj, "chunks");
        if (chunks)
        {
            p->num_chunks = json_len(chunks);
            for (int i = 0; i < p->num_chunks && i < 512; ++i)
            {
                const struct jval *c = json_at(chunks, i);
                int64_t cx = 0, cz = 0;
                if (c && json_len(c) == 2)
                {
                    json_int(json_at(c, 0), &cx);
                    json_int(json_at(c, 1), &cz);
                    p->chunk_x[i] = (int16_t)cx;
                    p->chunk_z[i] = (int16_t)cz;
                }
            }
        }
    }
    else if (strcmp(pkt_name, "S21PacketChunkData") == 0 || strcmp(pkt_name, "S21") == 0)
    {
        p->kind = TRACKER_PKT_S21;
        int64_t v;
        if (json_int(json_get(pj, "x"), &v)) p->x = (int)v;
        if (json_int(json_get(pj, "z"), &v)) p->z = (int)v;
    }
    else if (strcmp(pkt_name, "S28PacketEffect") == 0 || strcmp(pkt_name, "S28") == 0)
    {
        p->kind = TRACKER_PKT_S28;
        int64_t v;
        if (json_int(json_get(pj, "effect"), &v)) p->effect = (int)v;
        if (json_int(json_get(pj, "x"), &v)) p->x = (int)v;
        if (json_int(json_get(pj, "y"), &v)) p->y = (int)v;
        if (json_int(json_get(pj, "z"), &v)) p->z = (int)v;
        if (json_int(json_get(pj, "data"), &v)) p->data = (int)v;
    }
    else
    {
        p->kind = TRACKER_PKT_OTHER;
    }
}

/* The chunks the client holds (ChunkProviderClient): every chunk an S26 or
 * S21 brought, as the recorder writes no S21 unload. The test keeps them;
 * clientworld_tick's void fog reads them through loaded_has. */
static int32_t *loaded_c;
static int nloaded, cap_loaded;

static void loaded_add(int cx, int cz)
{
    for (int i = 0; i < nloaded; ++i)
        if (loaded_c[2 * i] == cx && loaded_c[2 * i + 1] == cz) return;
    if (nloaded == cap_loaded)
    {
        cap_loaded = cap_loaded ? 2 * cap_loaded : 256;
        loaded_c = realloc(loaded_c, (size_t)cap_loaded * 2 * sizeof *loaded_c);
        if (!loaded_c) abort();
    }
    loaded_c[2 * nloaded] = cx;
    loaded_c[2 * nloaded + 1] = cz;
    ++nloaded;
}

static bool loaded_has(int cx, int cz)
{
    for (int i = 0; i < nloaded; ++i)
        if (loaded_c[2 * i] == cx && loaded_c[2 * i + 1] == cz) return true;
    return false;
}

static void apply_packet(struct clientworld *cw, const struct tracker_packet *p)
{
    if (p->kind == TRACKER_PKT_S26)
        for (int i = 0; i < p->num_chunks && i < 512; ++i) loaded_add(p->chunk_x[i], p->chunk_z[i]);
    else if (p->kind == TRACKER_PKT_S21)
        loaded_add(p->x, p->z);
    clientworld_handle_packet(cw, p);
}

static int verify_torch_display_tick(struct clientworld *cw, char *fail_msg, size_t fail_msg_sz)
{
    det_state test_det;
    uint64_t seeder[DET_ROLES] = {0}, math[DET_ROLES] = {0};
    int32_t next_id[DET_ROLES] = {0};
    det_load(&test_det, 0, seeder, math, next_id);

    det_rng var5 = det_new_random_role(&test_det, DET_CLIENT);

    /* Compute expected math state for 13 math draws (7 for smoke, 6 for flame) */
    det_state exp_det;
    det_load(&exp_det, 0, seeder, math, next_id);
    det_new_random_role(&exp_det, DET_CLIENT);
    for (int i = 0; i < 13; ++i)
    {
        det_math_random_role(&exp_det, DET_CLIENT);
    }
    uint64_t exp_math = det_math_state(&exp_det, DET_CLIENT);

    det_state *orig_det = cw->det;
    cw->det = &test_det;
    clientworld_random_display_tick(cw, 50, 0, 64, 0, &var5, 0.0, 64.0, 0.0);
    uint64_t act_math = det_math_state(&test_det, DET_CLIENT);
    cw->det = orig_det;

    if (act_math != exp_math)
    {
        snprintf(fail_msg, fail_msg_sz, "row t=1 field d.cmath: exp=%016llx act=%016llx",
                 (unsigned long long)exp_math, (unsigned long long)act_math);
        return 1;
    }
    return 0;
}

static int verify_tracker_s17(struct tracker *tr, char *fail_msg, size_t fail_msg_sz)
{
    struct tracker_entry e;
    memset(&e, 0, sizeof(e));
    e.id = 100;
    e.update_frequency = 1;
    e.ticks = 1;
    e.last_scaled_x = 0;
    e.last_scaled_y = 0;
    e.last_scaled_z = 0;
    e.last_yaw = 0;
    e.last_pitch = 0;
    e.x = 10.0 / 32.0;
    e.y = 10.0 / 32.0;
    e.z = 10.0 / 32.0;
    e.yaw = 45.0f * 360.0f / 256.0f;
    e.pitch = 0.0f;

    struct tracker_packet out_p[4];
    int n = tracker_entry_tick(tr, &e, out_p, 4);
    if (n <= 0 || out_p[0].kind != TRACKER_PKT_S17)
    {
        snprintf(fail_msg, fail_msg_sz, "row t=1 field S17: expected S17 got %s",
                 (n > 0 && out_p[0].kind == TRACKER_PKT_S15) ? "S15" : "other");
        return 1;
    }
    return 0;
}

static int run_test(const char *dir, bool neg_torch, bool neg_tracker, char *fail_msg, size_t fail_msg_sz)
{
    char path[1024];

    /* Load snapshot */
    char dir_clean[1024];
    snprintf(dir_clean, sizeof(dir_clean), "%s", dir);
    size_t dlen = strlen(dir_clean);
    while (dlen > 0 && dir_clean[dlen - 1] == '/') dir_clean[--dlen] = '\0';
    const char *base = strrchr(dir_clean, '/');
    base = base ? base + 1 : dir_clean;

    char snap_dir[1024];
    snap_dir[0] = 0;
    snprintf(path, sizeof(path), "%s/manifest.json", dir_clean);
    FILE *f_man = fopen(path, "r");
    if (f_man)
    {
        fseek(f_man, 0, SEEK_END);
        long msz = ftell(f_man);
        fseek(f_man, 0, SEEK_SET);
        char *mtext = malloc(msz + 1);
        if (mtext && fread(mtext, 1, msz, f_man) == (size_t)msz)
        {
            mtext[msz] = 0;
            struct jval *mj = json_parse(mtext);
            if (mj)
            {
                const char *s = json_str(json_get(mj, "snapshot"));
                if (s && access(s, F_OK) == 0)
                {
                    snprintf(snap_dir, sizeof(snap_dir), "%s", s);
                }
                json_free(mj);
            }
            else
            {
                free(mtext);
            }
        }
        else
        {
            free(mtext);
        }
        fclose(f_man);
    }
    if (snap_dir[0] == 0)
    {
        snprintf(snap_dir, sizeof(snap_dir), "%s/../../snapshots/%s", dir_clean, base);
        if (access(snap_dir, F_OK) != 0)
        {
            snprintf(snap_dir, sizeof(snap_dir), "../out/java/snapshots/%s", base);
            if (access(snap_dir, F_OK) != 0)
            {
                snprintf(snap_dir, sizeof(snap_dir), "out/java/snapshots/%s", base);
                if (access(snap_dir, F_OK) != 0)
                    snprintf(snap_dir, sizeof(snap_dir), "%s", dir_clean);
            }
        }
    }

    struct snapshot snap;
    if (!snapshot_load(&snap, snap_dir))
    {
        snprintf(fail_msg, fail_msg_sz, "snapshot_load failed on %s", snap_dir);
        return 1;
    }

    /* Load initial.json */
    snprintf(path, sizeof(path), "%s/initial.json", dir);
    FILE *f_init = fopen(path, "r");
    if (!f_init)
    {
        snprintf(fail_msg, fail_msg_sz, "missing %.200s", path);
        snapshot_free(&snap);
        return 1;
    }
    fseek(f_init, 0, SEEK_END);
    long init_sz = ftell(f_init);
    fseek(f_init, 0, SEEK_SET);
    char *init_text = malloc(init_sz + 1);
    if (fread(init_text, 1, init_sz, f_init) != (size_t)init_sz) {}
    init_text[init_sz] = '\0';
    fclose(f_init);

    struct jval *init_j = json_parse(init_text);
    if (!init_j)
    {
        snprintf(fail_msg, fail_msg_sz, "malformed %.200s", path);
        snapshot_free(&snap);
        return 1;
    }

    const char *cw_rand_str = json_str(json_get(init_j, "cw_rand"));
    uint64_t init_rand = strtoull(cw_rand_str ? cw_rand_str : "0", NULL, 16);
    int64_t init_lcg_val = 0;
    json_int(json_get(init_j, "cw_update_lcg"), &init_lcg_val);
    int init_lcg = (int)init_lcg_val;

    /* Initialize Det */
    det_state det;
    load_det_from_nbt(&det, snap.det);

    /* Initialize clientworld */
    struct clientworld cw;
    clientworld_init(&cw, &snap.world, &det, init_rand, init_lcg, true);
    cw.negative_check_skip_torch_draw = neg_torch;
    if (verify_torch_display_tick(&cw, fail_msg, fail_msg_sz) != 0)
    {
        snapshot_free(&snap);
        return 1;
    }

    /* Load initial chunks from clientworld.nbt */
    nloaded = 0;
    const nbt *loaded_nbt = nbt_get(snap.clientworld, "loaded");
    if (loaded_nbt)
    {
        int n = nbt_list_size(loaded_nbt);
        for (int i = 0; i < n; ++i)
        {
            const nbt *c = nbt_list_get(loaded_nbt, i);
            if (c && nbt_list_size(c) == 2)
                loaded_add((int)nbt_value_long(nbt_list_get(c, 0)), (int)nbt_value_long(nbt_list_get(c, 1)));
        }
    }

    /* Apply initial join packets */
    const struct jval *init_pkts_j = json_get(init_j, "initial_packets");
    if (init_pkts_j)
    {
        int np = json_len(init_pkts_j);
        for (int i = 0; i < np; ++i)
        {
            struct tracker_packet p;
            parse_packet_json(json_at(init_pkts_j, i), &p);
            apply_packet(&cw, &p);
        }
    }
    json_free(init_j);

    /* Register local player entity in clientworld */
    int player_id = -1;
    for (int i = 0; i < snap.nents; ++i)
    {
        if (snap.ents[i].player)
        {
            player_id = snap.ents[i].id;
            break;
        }
    }
    if (player_id >= 0 && cw.nents < CW_MAX_ENTITIES)
    {
        struct client_entity *pe = &cw.ents[cw.nents++];
        memset(pe, 0, sizeof(*pe));
        pe->id = player_id;
        pe->is_living = true;
    }

    /* Initialize tracker */
    struct tracker tr;
    tracker_init(&tr);
    tr.negative_check_s15_for_s17 = neg_tracker;
    if (verify_tracker_s17(&tr, fail_msg, fail_msg_sz) != 0)
    {
        snapshot_free(&snap);
        return 1;
    }

    /* Populate initial tracked entities from snapshot */
    for (int i = 0; i < snap.nents; ++i)
    {
        struct snap_entity *se = &snap.ents[i];
        if (se->player) continue;

        double x = 0, y = 0, z = 0;
        float yaw = 0, pitch = 0;
        const nbt *pos_nbt = nbt_get(se->tag, "Pos");
        if (pos_nbt && nbt_list_size(pos_nbt) == 3)
        {
            uint64_t bx = (uint64_t)nbt_value_long(nbt_list_get(pos_nbt, 0));
            uint64_t by = (uint64_t)nbt_value_long(nbt_list_get(pos_nbt, 1));
            uint64_t bz = (uint64_t)nbt_value_long(nbt_list_get(pos_nbt, 2));
            memcpy(&x, &bx, 8);
            memcpy(&y, &by, 8);
            memcpy(&z, &bz, 8);
        }
        const nbt *rot_nbt = nbt_get(se->tag, "Rotation");
        if (rot_nbt && nbt_list_size(rot_nbt) == 2)
        {
            uint32_t byaw = (uint32_t)nbt_value_long(nbt_list_get(rot_nbt, 0));
            uint32_t bpitch = (uint32_t)nbt_value_long(nbt_list_get(rot_nbt, 1));
            memcpy(&yaw, &byaw, 4);
            memcpy(&pitch, &bpitch, 4);
        }
        tracker_add(&tr, se->id, tracker_class_of(se->cls), x, y, z, yaw, pitch, yaw);
    }

    /* Open clientrows.jsonl.gz */
    snprintf(path, sizeof(path), "%s/clientrows.jsonl.gz", dir);
    gzFile gz = gzopen(path, "rb");
    if (!gz)
    {
        snprintf(fail_msg, fail_msg_sz, "missing %.200s", path);
        snapshot_free(&snap);
        return 1;
    }

    struct lines lines;
    lines_init(&lines);
    lines_gz(&lines, gz);

    /* Open tape.jsonl to get player positions */
    snprintf(path, sizeof(path), "%s/tape.jsonl", dir);
    struct tape tape;
    if (!tape_open(&tape, path))
    {
        snprintf(fail_msg, fail_msg_sz, "failed to open %.200s", path);
        lines_free(&lines);
        snapshot_free(&snap);
        return 1;
    }

    const struct jval *tape_row = NULL;
    int64_t tape_t = -1;

    /* Advance tape until t == 2 */
    while (tape_next(&tape, &tape_row) > 0)
    {
        if (json_int(json_get(tape_row, "t"), &tape_t) && tape_t == 2)
            break;
    }

    double prev_px = 0, prev_py = 0, prev_pz = 0;
    for (int i = 0; i < snap.nents; ++i)
    {
        if (snap.ents[i].player)
        {
            const nbt *pos_nbt = nbt_get(snap.ents[i].tag, "Pos");
            if (pos_nbt && nbt_list_size(pos_nbt) == 3)
            {
                uint64_t bx = (uint64_t)nbt_value_long(nbt_list_get(pos_nbt, 0));
                uint64_t by = (uint64_t)nbt_value_long(nbt_list_get(pos_nbt, 1));
                uint64_t bz = (uint64_t)nbt_value_long(nbt_list_get(pos_nbt, 2));
                memcpy(&prev_px, &bx, 8);
                memcpy(&prev_py, &by, 8);
                memcpy(&prev_pz, &bz, 8);
            }
            break;
        }
    }

    const char *line = NULL;

    while ((line = lines_next(&lines)) != NULL)
    {

        struct jval *row_j = json_parse(strdup(line));
        if (!row_j)
        {
            snprintf(fail_msg, fail_msg_sz, "malformed row in clientrows.jsonl.gz");
            tape_close(&tape);
            lines_free(&lines);
            snapshot_free(&snap);
            return 1;
        }

        int64_t t = 0;
        json_int(json_get(row_j, "t"), &t);

        /* Ensure tape_row matches t */
        while (tape_t < t)
        {
            if (tape_next(&tape, &tape_row) <= 0) break;
            json_int(json_get(tape_row, "t"), &tape_t);
        }

        /* Player position from tape row cp */
        double px = 0, py = 0, pz = 0;
        float pyaw = 0, ppitch = 0;
        const struct jval *cp = json_get(tape_row, "cp");
        if (cp)
        {
            uint64_t bits;
            if (json_double(json_get(cp, "x"), &bits)) memcpy(&px, &bits, 8);
            if (json_double(json_get(cp, "y"), &bits)) memcpy(&py, &bits, 8);
            if (json_double(json_get(cp, "z"), &bits)) memcpy(&pz, &bits, 8);
            uint32_t fbits;
            if (json_float(json_get(cp, "yaw"), &fbits)) memcpy(&pyaw, &fbits, 4);
            if (json_float(json_get(cp, "pitch"), &fbits)) memcpy(&ppitch, &fbits, 4);
        }
        if (player_id >= 0)
        {
            struct client_entity *pe = clientworld_get_entity(&cw, player_id);
            if (pe)
            {
                pe->x = px;
                pe->y = py;
                pe->z = pz;
                pe->yaw = pyaw;
                pe->pitch = ppitch;
            }
        }

        /* 1. runTick up to updateRenderer: the textures, the dig particles and
         * the torch flicker */
        int64_t dig_count = 0;
        const struct jval *dig_j = json_get(row_j, "dig");
        if (dig_j) json_int(dig_j, &dig_count);
        clientworld_tick_input(&cw, (int)dig_count);

        /* 2. Client entity ticking */
        clientworld_tick_entities(&cw, px, py, pz);

        /* 2. Client world tick */
        bool is_sprinting = false;
        const struct jval *sprint_j = json_get(row_j, "sprint");
        if (sprint_j) {
            is_sprinting = (sprint_j->kind == 1 /* J_BOOL */) ? (sprint_j->boolean != 0) : ((sprint_j->kind == 2) ? (sprint_j->num != 0) : false);
        } else if (tape_row) {
            const struct jval *act = json_get(tape_row, "act");
            const struct jval *in = json_get(act, "in");
            const struct jval *keys = json_get(in, "keys");
            const struct jval *ksprint = json_get(keys, "key.sprint");
            if (ksprint && json_len(ksprint) > 0) {
                int64_t v = 0;
                json_int(json_at(ksprint, 0), &v);
                if (v == 1) is_sprinting = true;
            }
        }
        clientworld_tick(&cw, px, py, pz, prev_px, prev_py, prev_pz, is_sprinting, loaded_has);
        prev_px = px; prev_py = py; prev_pz = pz;
        /* Check d values */
        const struct jval *exp_d = json_get(row_j, "d");
        const char *exp_cw = json_str(json_get(exp_d, "cw"));
        const char *exp_cseed = json_str(json_get(exp_d, "cseed"));
        const char *exp_cmath = json_str(json_get(exp_d, "cmath"));
        const char *exp_cstat = json_str(json_get(exp_d, "cstat"));

        char act_cw[32], act_cseed[32], act_cmath[32], act_cstat[32];
        snprintf(act_cw, sizeof(act_cw), "%016llx", (unsigned long long)clientworld_cw(&cw));
        snprintf(act_cseed, sizeof(act_cseed), "%016llx", (unsigned long long)det_seeder_state(cw.det, DET_CLIENT));
        snprintf(act_cmath, sizeof(act_cmath), "%016llx", (unsigned long long)det_math_state(cw.det, DET_CLIENT));
        snprintf(act_cstat, sizeof(act_cstat), "%016llx", (unsigned long long)det_split_state(cw.det, DET_CLIENT));

        if (exp_cw && strcmp(act_cw, exp_cw) != 0)
        {
            snprintf(fail_msg, fail_msg_sz, "row t=%lld field d.cw: exp=%s act=%s", (long long)t, exp_cw, act_cw);
            json_free(row_j);
            tape_close(&tape);
            lines_free(&lines);
            snapshot_free(&snap);
            return 1;
        }

        if (exp_cseed && strcmp(act_cseed, exp_cseed) != 0)
        {
            snprintf(fail_msg, fail_msg_sz, "row t=%lld field d.cseed: exp=%s act=%s", (long long)t, exp_cseed, act_cseed);
            json_free(row_j);
            tape_close(&tape);
            lines_free(&lines);
            snapshot_free(&snap);
            return 1;
        }

        if (exp_cmath && strcmp(act_cmath, exp_cmath) != 0)
        {
            snprintf(fail_msg, fail_msg_sz, "row t=%lld field d.cmath: exp=%s act=%s", (long long)t, exp_cmath, act_cmath);
            printf("FAIL DETAILS t=%lld: cseed=%s (exp=%s) cmath=%s (exp=%s)\n", (long long)t, act_cseed, exp_cseed, act_cmath, exp_cmath);
            json_free(row_j);
            tape_close(&tape);
            lines_free(&lines);
            snapshot_free(&snap);
            return 1;
        }

        if (exp_cstat && strcmp(act_cstat, exp_cstat) != 0)
        {
            snprintf(fail_msg, fail_msg_sz, "row t=%lld field d.cstat: exp=%s act=%s", (long long)t, exp_cstat, act_cstat);
            json_free(row_j);
            tape_close(&tape);
            lines_free(&lines);
            snapshot_free(&snap);
            return 1;
        }

        /* Check client entity states */
        const struct jval *ents_j = json_get(row_j, "ents");
        if (ents_j)
        {
            int n_exp_ents = json_len(ents_j);
            for (int ei = 0; ei < n_exp_ents; ++ei)
            {
                const struct jval *ej = json_at(ents_j, ei);
                int64_t eid = 0;
                json_int(json_get(ej, "id"), &eid);
                struct client_entity *ce = clientworld_get_entity(&cw, (int)eid);
                if (!ce)
                {
                    snprintf(fail_msg, fail_msg_sz, "row t=%lld field client_entity %d: missing from clientworld", (long long)t, (int)eid);
                    json_free(row_j);
                    tape_close(&tape);
                    lines_free(&lines);
                    snapshot_free(&snap);
                    return 1;
                }

                int64_t exp_sx = 0, exp_sy = 0, exp_sz = 0, exp_inc = 0;
                json_int(json_get(ej, "sx"), &exp_sx);
                json_int(json_get(ej, "sy"), &exp_sy);
                json_int(json_get(ej, "sz"), &exp_sz);
                json_int(json_get(ej, "inc"), &exp_inc);

                if (ce->server_pos_x != (int)exp_sx || ce->server_pos_y != (int)exp_sy || ce->server_pos_z != (int)exp_sz)
                {
                    snprintf(fail_msg, fail_msg_sz, "row t=%lld field entity %d serverPos: exp=(%d,%d,%d) act=(%d,%d,%d)",
                             (long long)t, (int)eid, (int)exp_sx, (int)exp_sy, (int)exp_sz,
                             ce->server_pos_x, ce->server_pos_y, ce->server_pos_z);
                    json_free(row_j);
                    tape_close(&tape);
                    lines_free(&lines);
                    snapshot_free(&snap);
                    return 1;
                }

                if (ce->new_pos_rotation_increments != (int)exp_inc)
                {
                    snprintf(fail_msg, fail_msg_sz, "row t=%lld field entity %d inc: exp=%d act=%d",
                             (long long)t, (int)eid, (int)exp_inc, ce->new_pos_rotation_increments);
                    json_free(row_j);
                    tape_close(&tape);
                    lines_free(&lines);
                    snapshot_free(&snap);
                    return 1;
                }
            }
        }

        /* Apply tracker packets to clientworld */
        const struct jval *pkts_j = json_get(row_j, "packets");
        if (pkts_j)
        {
            int np = json_len(pkts_j);
            for (int pi = 0; pi < np; ++pi)
            {
                struct tracker_packet p;
                parse_packet_json(json_at(pkts_j, pi), &p);
                apply_packet(&cw, &p);
            }
        }

        json_free(row_j);
    }

    tape_close(&tape);
    lines_free(&lines);
    snapshot_free(&snap);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage: test_clientworld <clientworld_dir>\n");
        return 0;
    }

    bool neg_torch = false;
    bool neg_tracker = false;
    const char *dir = NULL;

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--neg-torch") == 0) neg_torch = true;
        else if (strcmp(argv[i], "--neg-tracker") == 0) neg_tracker = true;
        else dir = argv[i];
    }

    if (!dir)
    {
        printf("Usage: test_clientworld [--neg-torch] [--neg-tracker] <clientworld_dir>\n");
        return 0;
    }

    char fail_msg[2048];
    if (run_test(dir, neg_torch, neg_tracker, fail_msg, sizeof(fail_msg)) != 0)
    {
        fail("%s: %s", dir, fail_msg);
        return 1;
    }
    printf("PASS %s (exact matches on d.cw, d.cseed, d.cmath, d.cstat, entities, tracker)\n", dir);

    return 0;
}
