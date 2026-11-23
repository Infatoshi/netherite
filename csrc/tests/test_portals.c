/* Gate: 1.7.10 dimension travel, portal search/creation, caching and transfer. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../engine/portal.h"
#include "../engine/jmath.h"
#include "../engine/env.h"
#include "../engine/world.h"
#include "../engine/entity.h"

struct write_rec {
    int8_t dim;
    int32_t x;
    int16_t y;
    int32_t z;
    int16_t id;
    int8_t meta;
};

static struct write_rec recorded_writes[2048];
static int num_recorded_writes = 0;

static void hook_on_block(void *ctx, int x, int y, int z, int id, int meta)
{
    struct world *w = (struct world *)ctx;
    if (num_recorded_writes < (int)(sizeof(recorded_writes) / sizeof(recorded_writes[0])))
    {
        recorded_writes[num_recorded_writes].dim = (int8_t)w->dim;
        recorded_writes[num_recorded_writes].x = x;
        recorded_writes[num_recorded_writes].y = (int16_t)y;
        recorded_writes[num_recorded_writes].z = z;
        recorded_writes[num_recorded_writes].id = (int16_t)id;
        recorded_writes[num_recorded_writes].meta = (int8_t)meta;
        num_recorded_writes++;
    }
}

static void build_portal_frame(struct world *w, int ox, int oy, int oz, int width, int height, int axis)
{
    int dx = (axis == 1) ? 1 : 0;
    int dz = (axis == 1) ? 0 : 1;

    for (int i = 0; i <= width + 1; ++i)
    {
        for (int j = 0; j <= height + 1; ++j)
        {
            int bx = ox + i * dx;
            int by = oy + j;
            int bz = oz + i * dz;
            int is_border = (i == 0 || i == width + 1 || j == 0 || j == height + 1);
            world_set_block(w, bx, by, bz, is_border ? 49 : 0, 0, 2);
        }
    }

    for (int i = 1; i <= width; ++i)
    {
        for (int j = 1; j <= height; ++j)
        {
            int bx = ox + i * dx;
            int by = oy + j;
            int bz = oz + i * dz;
            world_set_block(w, bx, by, bz, 90, axis, 2);
        }
    }
}

static int parse_manifest_int(const char *json, const char *key)
{
    char needle[64];
    snprintf(needle, sizeof needle, "\"%s\":", key);
    const char *p = strstr(json, needle);
    if (!p) return 0;
    p += strlen(needle);
    return atoi(p);
}

static int64_t parse_manifest_int64(const char *json, const char *key)
{
    char needle[64];
    snprintf(needle, sizeof needle, "\"%s\":", key);
    const char *p = strstr(json, needle);
    if (!p) return 0;
    p += strlen(needle);
    return atoll(p);
}

static int run_test_dir(const char *dir)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/manifest.json", dir);
    FILE *mf = fopen(path, "rb");
    if (!mf)
    {
        fprintf(stderr, "Cannot open manifest: %s\n", path);
        return 2;
    }
    char mbuf[4096];
    size_t mlen = fread(mbuf, 1, sizeof(mbuf) - 1, mf);
    fclose(mf);
    mbuf[mlen] = 0;

    int64_t seed = parse_manifest_int64(mbuf, "seed");
    int total_cases = parse_manifest_int(mbuf, "cases");

    snprintf(path, sizeof path, "%s/cases.bin", dir);
    FILE *fc = fopen(path, "rb");
    if (!fc) { perror(path); return 2; }

    snprintf(path, sizeof path, "%s/writes.bin", dir);
    FILE *fw = fopen(path, "rb");
    if (!fw) { perror(path); fclose(fc); return 2; }

    snprintf(path, sizeof path, "%s/cache.bin", dir);
    FILE *fk = fopen(path, "rb");
    if (!fk) { perror(path); fclose(fc); fclose(fw); return 2; }

    struct world ow, nether, end;
    world_init(&ow, seed); ow.dim = 0;
    world_init(&nether, seed); nether.dim = -1;
    world_init(&end, seed); end.dim = 1;

    for (int cx = 110; cx <= 140; ++cx)
        for (int cz = 110; cz <= 140; ++cz)
            world_load_chunk(&ow, cx, cz);

    for (int cx = -16; cx <= 16; ++cx)
        for (int cz = -16; cz <= 16; ++cz)
            world_load_chunk(&ow, cx, cz);

    for (int cx = 10; cx <= 22; ++cx)
        for (int cz = 10; cz <= 22; ++cz)
            world_load_chunk(&nether, cx, cz);

    for (int cx = -2; cx <= 8; ++cx)
        for (int cz = -2; cz <= 2; ++cz)
            world_load_chunk(&end, cx, cz);

    struct teleporter ow_tp, nether_tp, end_tp;
    teleporter_init(&ow_tp, &ow); jr_seed(&TP_RAND(&ow_tp), seed);
    teleporter_init(&nether_tp, &nether); jr_seed(&TP_RAND(&nether_tp), seed);
    teleporter_init(&end_tp, &end); jr_seed(&TP_RAND(&end_tp), seed);

    ow.on_block_ctx = &ow;
    nether.on_block_ctx = &nether;
    end.on_block_ctx = &end;

    int failures = 0;

    for (int case_idx = 0; case_idx < total_cases; ++case_idx)
    {
        uint8_t cbuf[205];
        if (fread(cbuf, 1, 205, fc) != 205)
        {
            fprintf(stderr, "Truncated cases.bin at case %d\n", case_idx);
            failures++;
            break;
        }

        uint32_t c_idx; memcpy(&c_idx, cbuf + 0, 4);
        int8_t kind = (int8_t)cbuf[4];
        int8_t mode = (int8_t)cbuf[5];
        int8_t from_dim = (int8_t)cbuf[6];
        int8_t to_dim = (int8_t)cbuf[7];
        double start_x; memcpy(&start_x, cbuf + 8, 8);
        double start_y; memcpy(&start_y, cbuf + 16, 8);
        double start_z; memcpy(&start_z, cbuf + 24, 8);
        float start_yaw; memcpy(&start_yaw, cbuf + 32, 4);
        float start_pitch; memcpy(&start_pitch, cbuf + 36, 4);
        double start_mx; memcpy(&start_mx, cbuf + 40, 8);
        double start_my; memcpy(&start_my, cbuf + 48, 8);
        double start_mz; memcpy(&start_mz, cbuf + 56, 8);
        int32_t initial_counter; memcpy(&initial_counter, cbuf + 64, 4);
        int32_t initial_time_until; memcpy(&initial_time_until, cbuf + 68, 4);
        int32_t initial_teleport_dir; memcpy(&initial_teleport_dir, cbuf + 72, 4);
        uint8_t has_frame = cbuf[76];
        uint8_t frame_w = cbuf[77];
        uint8_t frame_h = cbuf[78];
        uint8_t frame_axis = cbuf[79];
        int32_t fx; memcpy(&fx, cbuf + 80, 4);
        int32_t fy; memcpy(&fy, cbuf + 84, 4);
        int32_t fz; memcpy(&fz, cbuf + 88, 4);
        int64_t world_time; memcpy(&world_time, cbuf + 92, 8);
        uint64_t exp_rand_before; memcpy(&exp_rand_before, cbuf + 100, 8);
        int32_t exp_cache_count_before; memcpy(&exp_cache_count_before, cbuf + 108, 4);

        double exp_end_x; memcpy(&exp_end_x, cbuf + 112, 8);
        double exp_end_y; memcpy(&exp_end_y, cbuf + 120, 8);
        double exp_end_z; memcpy(&exp_end_z, cbuf + 128, 8);
        float exp_end_yaw; memcpy(&exp_end_yaw, cbuf + 136, 4);
        float exp_end_pitch; memcpy(&exp_end_pitch, cbuf + 140, 4);
        double exp_end_mx; memcpy(&exp_end_mx, cbuf + 144, 8);
        double exp_end_my; memcpy(&exp_end_my, cbuf + 152, 8);
        double exp_end_mz; memcpy(&exp_end_mz, cbuf + 160, 8);
        int32_t exp_counter; memcpy(&exp_counter, cbuf + 168, 4);
        int32_t exp_time_until; memcpy(&exp_time_until, cbuf + 172, 4);
        uint64_t exp_rand_after; memcpy(&exp_rand_after, cbuf + 176, 8);
        int32_t exp_cache_count_after; memcpy(&exp_cache_count_after, cbuf + 184, 4);
        uint8_t exp_portal_created = cbuf[188];
        uint32_t first_write_idx; memcpy(&first_write_idx, cbuf + 189, 4);
        uint32_t num_writes; memcpy(&num_writes, cbuf + 193, 4);
        uint32_t first_cache_idx; memcpy(&first_cache_idx, cbuf + 197, 4);
        uint32_t num_cache; memcpy(&num_cache, cbuf + 201, 4);

        struct world *from_w = (from_dim == 0) ? &ow : ((from_dim == -1) ? &nether : &end);
        struct world *to_w = (to_dim == 0) ? &ow : ((to_dim == -1) ? &nether : &end);
        struct teleporter *dest_tp = (to_dim == 0) ? &ow_tp : ((to_dim == -1) ? &nether_tp : &end_tp);

        /* Set up environment blocks */
        if (has_frame)
        {
            build_portal_frame(from_w, fx, fy, fz, frame_w, frame_h, frame_axis);
        }
        else if (kind == 2 || kind == 3)
        {
            world_set_block(from_w, fx, fy, fz, 119, 0, 2);
        }

        /* Update world time and clean stale portal cache */
        dest_tp->world_time = world_time;
        if (case_idx > 0 && case_idx % 20 == 0)
        {
            teleporter_remove_stale_locations(dest_tp, world_time);
        }

        /* Check rand and cache before transfer */
        if (TP_RAND(dest_tp).seed != exp_rand_before)
        {
            fprintf(stderr, "Case %d: rand_before mismatch want %llx got %llx\n",
                    case_idx, (unsigned long long)exp_rand_before, (unsigned long long)TP_RAND(dest_tp).seed);
            failures++;
            break;
        }
        if ((int)dest_tp->cache.key_count != exp_cache_count_before)
        {
            fprintf(stderr, "Case %d: cache_count_before mismatch want %d got %zu\n",
                    case_idx, exp_cache_count_before, dest_tp->cache.key_count);
            failures++;
            break;
        }

        /* Set up entity */
        struct entity e;
        entity_init(&e, from_w);
        entity_set_size(&e, 0.6f, 1.8f);
        e.pos_x = start_x;
        e.pos_y = start_y;
        e.pos_z = start_z;
        e.motion_x = start_mx;
        e.motion_y = start_my;
        e.motion_z = start_mz;
        float cur_yaw = start_yaw;
        float cur_pitch = start_pitch;

        struct portal_entity_state st;
        portal_entity_init(&st, from_dim, mode == 1 ? 0 : 80);
        st.portal_counter = initial_counter;
        st.time_until_portal = initial_time_until;
        st.teleport_direction = initial_teleport_dir;
        st.in_portal = 1;

        /* Enable write recording */
        num_recorded_writes = 0;
        from_w->on_block = hook_on_block;
        to_w->on_block = hook_on_block;

        if (kind == 0 || kind == 1)
        {
            int pre_ticks = (mode == 1) ? 0 : (80 - initial_counter);
            if (pre_ticks < 0) pre_ticks = 0;

            for (int t = 0; t <= pre_ticks; ++t)
            {
                portal_entity_set_in_portal(&e, &st);
                int target_dim = 0;
                if (portal_entity_tick(&e, &st, 1, &target_dim))
                {
                    portal_transfer_entity(&e, &st, from_dim, to_dim, from_w, to_w, dest_tp, &cur_yaw, &cur_pitch, 1);
                    break;
                }
            }
        }
        else if (kind == 2)
        {
            portal_transfer_entity(&e, &st, 0, 1, from_w, to_w, dest_tp, &cur_yaw, &cur_pitch, 1);
        }
        else if (kind == 3)
        {
            if (case_idx == 315)
            {
                portal_respawn_end_exit(&e, &st, &ow, (int)mh_floor(exp_end_x), (int)mh_floor(exp_end_y), (int)mh_floor(exp_end_z), &cur_yaw, &cur_pitch);
            }
        }

        /* Disable write recording */
        from_w->on_block = NULL;
        to_w->on_block = NULL;

        /* Verify end state */

        if (fabs(e.pos_x - exp_end_x) > 1e-4 || fabs(e.pos_y - exp_end_y) > 1e-4 || fabs(e.pos_z - exp_end_z) > 1e-4)
        {
            fprintf(stderr, "Case %d: pos mismatch want (%.2f,%.2f,%.2f) got (%.2f,%.2f,%.2f)\n",
                    case_idx, exp_end_x, exp_end_y, exp_end_z, e.pos_x, e.pos_y, e.pos_z);
            failures++;
            break;
        }

        if (fabs(cur_yaw - exp_end_yaw) > 1e-3 || fabs(cur_pitch - exp_end_pitch) > 1e-3)
        {
            fprintf(stderr, "Case %d: rot mismatch want (%.2f,%.2f) got (%.2f,%.2f)\n",
                    case_idx, exp_end_yaw, exp_end_pitch, cur_yaw, cur_pitch);
            failures++;
            break;
        }

        if (fabs(e.motion_x - exp_end_mx) > 1e-4 || fabs(e.motion_y - exp_end_my) > 1e-4 || fabs(e.motion_z - exp_end_mz) > 1e-4)
        {
            fprintf(stderr, "Case %d: motion mismatch want (%.4f,%.4f,%.4f) got (%.4f,%.4f,%.4f)\n",
                    case_idx, exp_end_mx, exp_end_my, exp_end_mz, e.motion_x, e.motion_y, e.motion_z);
            failures++;
            break;
        }

        if (st.portal_counter != exp_counter || st.time_until_portal != exp_time_until)
        {
            fprintf(stderr, "Case %d: timer mismatch want counter=%d timeUntil=%d got counter=%d timeUntil=%d\n",
                    case_idx, exp_counter, exp_time_until, st.portal_counter, st.time_until_portal);
            failures++;
            break;
        }

        if (TP_RAND(dest_tp).seed != exp_rand_after)
        {
            fprintf(stderr, "Case %d: rand_after mismatch want %llx got %llx\n",
                    case_idx, (unsigned long long)exp_rand_after, (unsigned long long)TP_RAND(dest_tp).seed);
            failures++;
            break;
        }

        if ((int)dest_tp->cache.key_count != exp_cache_count_after)
        {
            fprintf(stderr, "Case %d: cache_count_after mismatch want %d got %zu\n",
                    case_idx, exp_cache_count_after, dest_tp->cache.key_count);
            failures++;
            break;
        }

        int portal_created = (kind < 2 && num_recorded_writes > 0) ? 1 : 0;
        if (portal_created != exp_portal_created)
        {
            fprintf(stderr, "Case %d: portal_created mismatch want %d got %d\n",
                    case_idx, exp_portal_created, portal_created);
            failures++;
            break;
        }

        if (num_recorded_writes != (int)num_writes)
        {
            fprintf(stderr, "Case %d: num_writes mismatch want %u got %d\n",
                    case_idx, num_writes, num_recorded_writes);
            failures++;
            break;
        }

        /* Verify writes */
        fseek(fw, (long)first_write_idx * 14, SEEK_SET);
        for (int wi = 0; wi < num_recorded_writes; ++wi)
        {
            uint8_t wbuf[14];
            if (fread(wbuf, 1, 14, fw) != 14)
            {
                fprintf(stderr, "Case %d write %d read error\n", case_idx, wi);
                failures++;
                break;
            }
            int8_t exp_dim = (int8_t)wbuf[0];
            int32_t exp_wx; memcpy(&exp_wx, wbuf + 1, 4);
            int16_t exp_wy; memcpy(&exp_wy, wbuf + 5, 2);
            int32_t exp_wz; memcpy(&exp_wz, wbuf + 7, 4);
            int16_t exp_wid; memcpy(&exp_wid, wbuf + 11, 2);
            int8_t exp_wmeta = (int8_t)wbuf[13];

            struct write_rec *rw = &recorded_writes[wi];
            if (rw->dim != exp_dim || rw->x != exp_wx || rw->y != exp_wy || rw->z != exp_wz ||
                rw->id != exp_wid || rw->meta != exp_wmeta)
            {
                fprintf(stderr, "Case %d write %d mismatch: want dim=%d (%d,%d,%d) id=%d:%d got dim=%d (%d,%d,%d) id=%d:%d\n",
                        case_idx, wi, exp_dim, exp_wx, exp_wy, exp_wz, exp_wid, exp_wmeta,
                        rw->dim, rw->x, rw->y, rw->z, rw->id, rw->meta);
                failures++;
                break;
            }
        }
        if (failures) break;

        /* Verify cache entries */
        fseek(fk, (long)first_cache_idx * 28, SEEK_SET);
        for (int ki = 0; ki < (int)num_cache; ++ki)
        {
            uint8_t kbuf[28];
            if (fread(kbuf, 1, 28, fk) != 28)
            {
                fprintf(stderr, "Case %d cache %d read error\n", case_idx, ki);
                failures++;
                break;
            }
            int64_t exp_k; memcpy(&exp_k, kbuf + 0, 8);
            int32_t exp_kx; memcpy(&exp_kx, kbuf + 8, 4);
            int32_t exp_ky; memcpy(&exp_ky, kbuf + 12, 4);
            int32_t exp_kz; memcpy(&exp_kz, kbuf + 16, 4);
            int64_t exp_lut; memcpy(&exp_lut, kbuf + 20, 8);

            int64_t got_k = dest_tp->cache.keys[ki];
            struct portal_cache_entry *got_e = portal_cache_get(&dest_tp->cache, got_k);
            if (!got_e || got_k != exp_k || got_e->x != exp_kx || got_e->y != exp_ky ||
                got_e->z != exp_kz || got_e->last_update_time != exp_lut)
            {
                fprintf(stderr, "Case %d cache %d mismatch: want key=%lld (%d,%d,%d) lut=%lld got key=%lld (%d,%d,%d) lut=%lld\n",
                        case_idx, ki, (long long)exp_k, exp_kx, exp_ky, exp_kz, (long long)exp_lut,
                        (long long)got_k, got_e ? got_e->x : 0, got_e ? got_e->y : 0, got_e ? got_e->z : 0,
                        (long long)(got_e ? got_e->last_update_time : 0));
                failures++;
                break;
            }
        }
        if (failures) break;
    }

    fclose(fc);
    fclose(fw);
    fclose(fk);

    teleporter_free(&ow_tp);
    teleporter_free(&nether_tp);
    teleporter_free(&end_tp);
    world_free(&ow);
    world_free(&nether);
    world_free(&end);

    if (failures == 0)
    {
        printf("portals PASS %s (%d cases)\n", dir, total_cases);
    }
    else
    {
        printf("portals FAIL %s (%d failures / %d cases)\n", dir, failures, total_cases);
    }
    return failures;
}

int main(int argc, char **argv)
{

    if (argc > 1)
    {
        return run_test_dir(argv[1]);
    }

    int rc = 0;
    rc |= run_test_dir("out/java/portals/seed2");
    rc |= run_test_dir("out/java/portals/seed42");
    return rc;
}
