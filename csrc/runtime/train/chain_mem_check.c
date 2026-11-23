/* A controller cut through a disk sidecar every 500 ticks must give the same acts and state as one run. */
#define _POSIX_C_SOURCE 200809L
#include "../vecenv.h"
#include "../rlbind.h"
#include "../rlgui.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { CHAIN_TEST_MACROS = 29261823 }; /* configs/tests/chain.yaml's policy.actions.macros */

static uint64_t hash_bytes(uint64_t h, const void *data, size_t n)
{
    const unsigned char *p = data;
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

#define HASH(h, x) ((h) = hash_bytes((h), &(x), sizeof(x)))

static uint64_t step_hash(uint64_t h, const struct neth_vec_bufs *b, const struct neth_vec_chain_peek *p,
                          const struct neth_vec_chain_controller_probe *c, int cuts)
{
    HASH(h, p->x); HASH(h, p->y); HASH(h, p->z);
    HASH(h, p->yaw); HASH(h, p->pitch); HASH(h, p->health);
    HASH(h, p->dim); HASH(h, p->food); HASH(h, p->air); HASH(h, p->fire);
    int64_t logical_time = p->world_time - cuts; /* one deliberate world join tick at each restored seam */
    HASH(h, p->flags); HASH(h, logical_time); HASH(h, p->ms);
    HASH(h, b->rewards[0]); HASH(h, b->driver[0]); HASH(h, b->kind[0]); HASH(h, b->chosen[0]);
    const struct neth_rl_act *a = &b->acts[0];
    HASH(h, a->hold); HASH(h, a->hotbar); HASH(h, a->look_mode);
    for (int j = 0; j < 2; ++j) HASH(h, a->look[j]);
    HASH(h, a->nops);
    for (int j = 0; j < a->nops && j < NETH_RL_OPS; ++j) h = hash_bytes(h, &a->ops[j], sizeof a->ops[j]);
    for (int j = 0; j < 36; ++j)
    {
        HASH(h, p->st.inv[j].item);
        HASH(h, p->st.inv[j].count);
        HASH(h, p->st.inv[j].damage);
    }
    HASH(h, c->steps); HASH(h, c->stronghold_wait_at);
    HASH(h, c->portal_step); HASH(h, c->portal_x); HASH(h, c->portal_y); HASH(h, c->portal_z);
    HASH(h, c->pearl_phase); HASH(h, c->food_trip); HASH(h, c->strip_phase); HASH(h, c->dragon_phase);
    return h;
}

struct sample {
    uint64_t digest, state_hash, inventory_hash, act_hash, controller_hash;
    double x, y, z;
    float yaw, pitch, health;
    int64_t world_time;
    float reward;
    int dim, food, air, fire, flags, ms, kind, driver, chosen, hold, hotbar;
    struct neth_vec_chain_controller_probe controller;
    struct neth_rl_stack inv[36];
    struct neth_rl_act act;
};

/* Simulate a sidecar written before a controller gained eight zero-default tail bytes. The later pieces remain
 * byte-for-byte intact. A malformed fixed piece must be refused before the valid shorter file is restored. */
static int prefix_fixture(struct neth_vec *v, const char *path, char *err, int en)
{
    FILE *f = fopen(path, "rb");
    if (!f) return fprintf(stderr, "PREFIX: cannot open %s\n", path), -1;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return -1; }
    long length = ftell(f);
    if (length < (long)sizeof(struct neth_rl_chain_mem_header) || length > 1000000 || fseek(f, 0, SEEK_SET))
    { fclose(f); return fprintf(stderr, "PREFIX: invalid file size\n"), -1; }
    unsigned char *bytes = malloc((size_t)length);
    if (!bytes) { fclose(f); return -1; }
    int ok = fread(bytes, 1, (size_t)length, f) == (size_t)length;
    ok = fclose(f) == 0 && ok;
    struct neth_rl_chain_mem_header h = {0};
    memcpy(&h, bytes, sizeof h);
    if (!ok || h.magic != NETH_RL_CHAIN_MEM_MAGIC || h.version != NETH_RL_CHAIN_MEM_VERSION ||
        h.pieces != NETH_RL_CHAIN_MEM_PIECES || h.bytes != (uint32_t)length ||
        h.state_bytes + sizeof h != (uint32_t)length)
    { free(bytes); return fprintf(stderr, "PREFIX: invalid version-3 header\n"), -1; }
    static const int controllers[] = {NETH_RL_CHAIN_MEM_PEARL, 4, 2, 6, 10, 12};
    int chosen = -1;
    size_t end = 0;
    for (size_t k = 0; k < sizeof controllers / sizeof controllers[0]; ++k)
    {
        int i = controllers[k];
        size_t next = sizeof h;
        for (int j = 0; j <= i; ++j) next += h.piece_bytes[j];
        if (h.piece_bytes[i] < 16 || next > (size_t)length) continue;
        int zero = 1;
        for (size_t j = next - 8; j < next; ++j) zero &= bytes[j] == 0;
        if (zero) { chosen = i; end = next; break; }
    }
    if (chosen < 0) { free(bytes); return fprintf(stderr, "PREFIX: no zero-default controller tail\n"), -1; }
    char bad_path[320];
    if (snprintf(bad_path, sizeof bad_path, "%s.bad", path) >= (int)sizeof bad_path)
    { free(bytes); return -1; }
    struct neth_rl_chain_mem_header bad = h;
    bad.piece_bytes[0] += 1; /* a fixed prefix is not a controller extension */
    f = fopen(bad_path, "wb");
    if (!f) { free(bytes); return -1; }
    ok = fwrite(&bad, sizeof bad, 1, f) == 1 &&
         fwrite(bytes + sizeof h, 1, (size_t)length - sizeof h, f) == (size_t)length - sizeof h;
    ok = fclose(f) == 0 && ok;
    int rejected = ok && neth_vec_chain_memory_read(v, 0, bad_path, err, en) == -1;
    remove(bad_path);
    if (!rejected) { free(bytes); return fprintf(stderr, "PREFIX: non-prefix layout was accepted\n"), -1; }
    memmove(bytes + end - 8, bytes + end, (size_t)length - end);
    h.piece_bytes[chosen] -= 8, h.state_bytes -= 8, h.bytes -= 8;
    memcpy(bytes, &h, sizeof h);
    f = fopen(path, "wb");
    if (!f) { free(bytes); return -1; }
    ok = fwrite(bytes, 1, (size_t)length - 8, f) == (size_t)length - 8;
    ok = fclose(f) == 0 && ok;
    free(bytes);
    if (!ok) return fprintf(stderr, "PREFIX: cannot write shorter sidecar\n"), -1;
    printf("PREFIX FIXTURE piece=%d shortened=8 bytes; fixed-layout mutation rejected\n", chosen);
    return 0;
}

static int prefix_fixture_enabled;

static int one_run(const char *config, const char *start, const char *mopts, int macros,
                   int dev, int steps, int seam, int disk, int exact, int behaviour, uint64_t *digest,
                   struct sample *straight, const struct sample *want)
{
    struct neth_vec_opts o = {0};
    o.env.config = config;
    o.env.start = start;
    o.env.n = o.env.threads = o.groups = 1;
    o.env.device_mesh = -1;
    o.ep_len = steps + 10000;
    o.macros = macros;
    o.objective = NETH_VEC_FULL;
    o.macro_opts = mopts;
    o.chain = 1;
    if (dev) o.flags |= NETH_RL_DEV;
    char err[512] = {0};
    struct neth_vec *v = neth_vec_make(&o, err, sizeof err);
    if (!v) return fprintf(stderr, "make: %s\n", err), -1;
    struct neth_vec_bufs b = {0};
    neth_vec_buffers(v, &b);
    int aw = neth_vec_action_width(v), logical = 0, seen = 0, pending = 0, cuts = 0;
    int wait_steps = 0, max_portal_step = -1, macro_steps = 0;
    int wait_released = 0, portal_mid_cut = 0, entered_nether = 0;
    uint64_t h = 1469598103934665603ull;
    char path[256];
    snprintf(path, sizeof path, "out/chain/chain-mem-check-%d.mem", seam);
    while (logical < steps)
    {
        int g = neth_vec_recv(v, err, sizeof err);
        if (g != 0) { fprintf(stderr, "recv %d: %s\n", g, err); goto fail; }
        struct neth_vec_chain_peek p = {0};
        if (neth_vec_chain_peek(v, 0, &p)) { fprintf(stderr, "peek failed\n"); goto fail; }
        struct neth_vec_chain_controller_probe controller = {0};
        if (neth_vec_chain_controller_probe(v, 0, &controller)) { fprintf(stderr, "controller probe failed\n"); goto fail; }
        if (!seen) seen = 1;
        else if (pending)
        {
            if (!p.reset_step)
            {
                fprintf(stderr, "restore at %d: reset=%d %s\n", logical, p.reset_step, err);
                goto fail;
            }
            pending = 0;
        }
        else if (!p.reset_step)
        {
            h = step_hash(h, &b, &p, &controller, cuts);
            ++logical;
            wait_steps += controller.stronghold_wait_at != 0;
            if (controller.portal_step > max_portal_step) max_portal_step = controller.portal_step;
            macro_steps += b.kind[0] != 0;
            if (behaviour == 1 && controller.stronghold_wait_at > 0 &&
                controller.steps + 1 - controller.stronghold_wait_at > 36000 && b.kind[0])
                wait_released = 1;
            if (behaviour == 2 && logical == 500 && controller.portal_step > 0 && p.dim == 0)
                portal_mid_cut = 1;
            if (p.dim == -1) entered_nether = 1;
            uint64_t state_hash = 1469598103934665603ull, inventory_hash = state_hash, act_hash = state_hash;
            int64_t wt = p.world_time - cuts;
            HASH(state_hash, p.x); HASH(state_hash, p.y); HASH(state_hash, p.z);
            HASH(state_hash, p.yaw); HASH(state_hash, p.pitch); HASH(state_hash, p.health);
            HASH(state_hash, p.dim); HASH(state_hash, p.food); HASH(state_hash, p.air);
            HASH(state_hash, p.fire); HASH(state_hash, p.flags); HASH(state_hash, wt); HASH(state_hash, p.ms);
            HASH(state_hash, b.rewards[0]); HASH(state_hash, b.kind[0]); HASH(state_hash, b.driver[0]);
            HASH(state_hash, b.chosen[0]);
            for (int j = 0; j < 36; ++j)
            {
                HASH(inventory_hash, p.st.inv[j].item);
                HASH(inventory_hash, p.st.inv[j].count);
                HASH(inventory_hash, p.st.inv[j].damage);
            }
            const struct neth_rl_act *act = &b.acts[0];
            HASH(act_hash, act->hold); HASH(act_hash, act->hotbar); HASH(act_hash, act->look_mode);
            for (int j = 0; j < 2; ++j) HASH(act_hash, act->look[j]);
            HASH(act_hash, act->nops);
            for (int j = 0; j < act->nops && j < NETH_RL_OPS; ++j)
                act_hash = hash_bytes(act_hash, &act->ops[j], sizeof act->ops[j]);
            uint64_t controller_hash = hash_bytes(1469598103934665603ull, &controller, sizeof controller);
            struct sample row = {h, state_hash, inventory_hash, act_hash, controller_hash, p.x, p.y, p.z,
                                 p.yaw, p.pitch, p.health, wt, b.rewards[0], p.dim, p.food,
                                 p.air, p.fire, p.flags, (int)p.ms, b.kind[0], b.driver[0],
                                 b.chosen[0], (int)act->hold, act->hotbar, controller, {{0}}, {0}};
            memcpy(row.inv, p.st.inv, sizeof row.inv);
            row.act = *act;
            if (straight) straight[logical - 1] = row;
            if (want && h != want[logical - 1].digest)
            {
                const struct sample *a = &want[logical - 1];
                fprintf(stderr, "FIRST DIFF step %d straight=%016llx cut=%016llx\n"
                                "  state %016llx/%016llx inventory %016llx/%016llx act %016llx/%016llx controller %016llx/%016llx\n"
                                "  controller straight steps=%lld wait=%lld portal=%d at %d,%d,%d pearl=%d food_trip=%d strip=%d dragon=%d\n"
                                "  controller cut      steps=%lld wait=%lld portal=%d at %d,%d,%d pearl=%d food_trip=%d strip=%d dragon=%d\n"
                                "  straight wt=%lld pos=%.3f,%.3f,%.3f look=%.3f,%.3f hp=%.3f dim=%d food=%d air=%d fire=%d flags=%d ms=%d reward=%.6f kind=%d driver=%d chosen=%d hold=%d hotbar=%d\n"
                                "  cut      wt=%lld pos=%.3f,%.3f,%.3f look=%.3f,%.3f hp=%.3f dim=%d food=%d air=%d fire=%d flags=%d ms=%d reward=%.6f kind=%d driver=%d chosen=%d hold=%d hotbar=%d\n",
                        logical, (unsigned long long)a->digest, (unsigned long long)h,
                        (unsigned long long)a->state_hash, (unsigned long long)row.state_hash,
                        (unsigned long long)a->inventory_hash, (unsigned long long)row.inventory_hash,
                        (unsigned long long)a->act_hash, (unsigned long long)row.act_hash,
                        (unsigned long long)a->controller_hash, (unsigned long long)row.controller_hash,
                        (long long)a->controller.steps, (long long)a->controller.stronghold_wait_at,
                        a->controller.portal_step, a->controller.portal_x, a->controller.portal_y,
                        a->controller.portal_z, a->controller.pearl_phase, a->controller.food_trip,
                        a->controller.strip_phase, a->controller.dragon_phase,
                        (long long)row.controller.steps, (long long)row.controller.stronghold_wait_at,
                        row.controller.portal_step, row.controller.portal_x, row.controller.portal_y,
                        row.controller.portal_z, row.controller.pearl_phase, row.controller.food_trip,
                        row.controller.strip_phase, row.controller.dragon_phase,
                        (long long)a->world_time, a->x, a->y, a->z, a->yaw, a->pitch, a->health,
                        a->dim, a->food, a->air, a->fire, a->flags, a->ms, a->reward, a->kind,
                        a->driver, a->chosen, a->hold, a->hotbar,
                        (long long)row.world_time, row.x, row.y, row.z, row.yaw, row.pitch, row.health,
                        row.dim, row.food, row.air, row.fire, row.flags, row.ms, row.reward, row.kind,
                        row.driver, row.chosen, row.hold, row.hotbar);
                for (int j = 0; j < 36; ++j)
                    if (a->inv[j].item != row.inv[j].item || a->inv[j].count != row.inv[j].count ||
                        a->inv[j].damage != row.inv[j].damage)
                    {
                        fprintf(stderr, "  inventory slot %d straight=%d:%d/%d cut=%d:%d/%d\n", j,
                                a->inv[j].item, a->inv[j].count, a->inv[j].damage,
                                row.inv[j].item, row.inv[j].count, row.inv[j].damage);
                        break;
                    }
                fprintf(stderr, "  act straight lookmode=%d look=%.3f,%.3f nops=%d; cut lookmode=%d look=%.3f,%.3f nops=%d\n",
                        a->act.look_mode, a->act.look[0], a->act.look[1], a->act.nops,
                        row.act.look_mode, row.act.look[0], row.act.look[1], row.act.nops);
                if (a->act.nops || row.act.nops)
                    fprintf(stderr, "  first op straight=%d,%d,%d,%d,%d,%d cut=%d,%d,%d,%d,%d,%d\n",
                            a->act.ops[0].kind, a->act.ops[0].window, a->act.ops[0].slot,
                            a->act.ops[0].button, a->act.ops[0].mode, a->act.ops[0].index,
                            row.act.ops[0].kind, row.act.ops[0].window, row.act.ops[0].slot,
                            row.act.ops[0].button, row.act.ops[0].mode, row.act.ops[0].index);
                goto fail;
            }
            if (seam && logical < steps && logical % seam == 0)
            {
                size_t bytes = exact ? neth_vec_chain_controller_bytes(v, 0, NULL, 0) : 0;
                unsigned char *before = bytes ? malloc(bytes) : NULL, *after = bytes ? malloc(bytes) : NULL;
                if (exact && (!bytes || !before || !after ||
                              neth_vec_chain_controller_bytes(v, 0, before, bytes) != bytes))
                {
                    free(before); free(after);
                    fprintf(stderr, "controller bytes before cut %d unavailable\n", logical);
                    goto fail;
                }
                int st = neth_vec_chain_save(v, 0, err, sizeof err);
                if (st < 0 || neth_rl_start_wait(neth_vec_env(v), st, err, sizeof err))
                {
                    free(before); free(after);
                    fprintf(stderr, "cut at %d: %s\n", logical, err);
                    goto fail;
                }
                if (disk)
                {
                    if (neth_vec_chain_memory_write(v, st, path, err, sizeof err) ||
                        (prefix_fixture_enabled && logical == 500 && prefix_fixture(v, path, err, sizeof err)) ||
                        neth_vec_chain_memory_forget(v, st) ||
                        neth_vec_chain_memory_read(v, 0, path, err, sizeof err))
                    {
                        free(before); free(after);
                        fprintf(stderr, "disk at %d: %s\n", logical, err);
                        goto fail;
                    }
                    if (exact)
                    {
                        if (neth_vec_chain_controller_bytes(v, 0, after, bytes) != bytes ||
                            memcmp(before, after, bytes))
                        {
                            size_t at = 0;
                            while (at < bytes && before[at] == after[at]) ++at;
                            fprintf(stderr, "CONTROLLER-BYTES DIFF cut=%d offset=%zu of %zu before=%02x after=%02x\n",
                                    logical, at, bytes, at < bytes ? before[at] : 0, at < bytes ? after[at] : 0);
                            free(before); free(after);
                            goto fail;
                        }
                        printf("CONTROLLER-BYTES EQUAL cut=%d bytes=%zu\n", logical, bytes);
                    }
                    int restored = neth_vec_chain_save(v, 0, err, sizeof err);
                    if (restored < 0 || neth_rl_start_wait(neth_vec_env(v), restored, err, sizeof err))
                    {
                        free(before); free(after);
                        fprintf(stderr, "resave at %d: %s\n", logical, err);
                        goto fail;
                    }
                    neth_rl_start_free(neth_vec_env(v), st);
                    st = restored;
                }
                free(before); free(after);
                if (neth_vec_chain_next(v, 0, st, steps + 10000) || neth_vec_chain_end(v, 0))
                {
                    fprintf(stderr, "next at %d: %s\n", logical, err);
                    goto fail;
                }
                pending = 1;
                ++cuts;
            }
        }
        memset(b.actions, 0, (size_t)aw * sizeof *b.actions);
        if (neth_vec_send(v, 0)) { fprintf(stderr, "send failed\n"); goto fail; }
    }
    neth_vec_free(v);
    if (disk) remove(path);
    *digest = h;
    printf("%s %d steps %d cuts digest %016llx wait_steps %d max_portal_step %d macro_steps %d\n",
           !seam ? "straight" : disk ? "disk" : "memory", logical, cuts, (unsigned long long)h,
           wait_steps, max_portal_step, macro_steps);
    if (behaviour == 1 && (!wait_steps || !wait_released))
        return fprintf(stderr, "WAIT-BEHAVIOUR FAIL: wait_steps=%d released=%d\n", wait_steps, wait_released), -1;
    if (behaviour == 2 && (!portal_mid_cut || !entered_nether))
        return fprintf(stderr, "PORTAL-BEHAVIOUR FAIL: mid_cut=%d entered_nether=%d max_step=%d\n",
                       portal_mid_cut, entered_nether, max_portal_step), -1;
    return 0;
fail:
    neth_vec_free(v);
    if (disk) remove(path);
    return -1;
}

int main(int argc, char **argv)
{
    if (argc == 6 && !strcmp(argv[1], "scan-pearl"))
    {
        int first = atoi(argv[4]), last = atoi(argv[5]);
        if (first < 0 || last < first || last - first > 100000)
            return fprintf(stderr, "scan-pearl: invalid node range\n"), 2;
        struct neth_vec_opts o = {0};
        o.env.config = argv[2];
        o.env.n = o.env.threads = o.groups = 1;
        o.env.device_mesh = -1;
        o.ep_len = 2000;
        o.macros = NETH_VEC_ALL;
        o.chain = 1;
        char err[512] = {0};
        struct neth_vec *v = neth_vec_make(&o, err, sizeof err);
        if (!v) return fprintf(stderr, "scan-pearl make: %s\n", err), 1;
        if (neth_vec_recv(v, err, sizeof err) != 0)
            return fprintf(stderr, "scan-pearl recv: %s\n", err), 1;
        puts("node\titem_steps\titem_new\titem_walk\titem_full\titem_wet\twalk_fail\twalk_moving\twalk_there\twalk_timeover\tpearls\td_under1p5\td_1p5to6\td_6to16\td_over16\titem_below\titem_level\titem_above\thome_steps\thome_stand\thome_land\thome_d0\thome_d1\thome_noway\tphase\ton\tseen\tprovoked\tkilled\thunt_steps\thunt_open_sky\tender64_steps\tender64_sky\tbiome_top\tmem_version");
        for (int id = first; id <= last; ++id)
        {
            char path[2048];
            snprintf(path, sizeof path, "%s/seg/%d.mem", argv[3], id);
            FILE *f = fopen(path, "rb");
            if (!f) continue;
            uint32_t header[2] = {0};
            int valid = fread(header, sizeof header, 1, f) == 1 && header[0] == 0x43484d31u;
            if (fclose(f) || !valid)
                return fprintf(stderr, "scan-pearl invalid sidecar %d\n", id), 1;
            if (neth_vec_chain_memory_read(v, 0, path, err, sizeof err))
                return fprintf(stderr, "scan-pearl read %d: %s\n", id, err), 1;
            struct neth_vec_pearl p = {0};
            if (neth_vec_pearl(v, 0, 1, &p))
                return fprintf(stderr, "scan-pearl book %d unavailable\n", id), 1;
            int biome_top = -1;
            for (int b = 0; b < 64; ++b)
                if (p.hbiome[b] > 0 && (biome_top < 0 || p.hbiome[b] > p.hbiome[biome_top])) biome_top = b;
            printf("%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d",
                   id, p.item_steps, p.it_new, p.item_walk, p.it_full, p.it_wet,
                   p.it_wk[0], p.it_wk[1], p.it_wk[2], p.it_wk[3], p.pearls,
                   p.it_d[0], p.it_d[1], p.it_d[2], p.it_d[3],
                   p.it_dy[0], p.it_dy[1], p.it_dy[2],
                   p.home_steps, p.home_stand, p.home_land, p.home_d0, p.home_d1,
                   p.home_noway, p.phase, p.on, p.seen, p.provoked, p.killed);
            printf("\t%lld\t%d\t%lld\t%lld\t%d\t%u\n",
                   (long long)p.cen[4], p.hsky, (long long)p.cen[5], (long long)p.cen[6], biome_top, header[1]);
        }
        neth_vec_free(v);
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "wait"))
    {
        uint64_t digest;
        if (one_run("configs/tests/chain.yaml", "out/java/snapshots/gold-g26-s42",
                    "stronghold_food=wait,stronghold_oracle=locate", NETH_VEC_STRONGHOLD,
                    0, 38001, 2400, 1, 1, 1, &digest, NULL, NULL)) return 1;
        puts("WAIT-BEHAVIOUR PASS: the stronghold wait ended across 2400-step disk cuts");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "portal"))
    {
        uint64_t digest;
        if (one_run("configs/tests/chain.yaml", "out/java/snapshots/gold-g6-s42",
                    "portal_room=wide,portal_base=ground", CHAIN_TEST_MACROS,
                    0, 1501, 500, 1, 1, 2, &digest, NULL, NULL)) return 1;
        puts("PORTAL-BEHAVIOUR PASS: a mid-frame disk cut still entered the Nether");
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "legacy"))
    {
        struct neth_vec_opts o = {0};
        o.env.config = argv[2];
        o.env.n = o.env.threads = o.groups = 1;
        o.env.device_mesh = -1;
        o.ep_len = 2000;
        o.macros = NETH_VEC_ALL;
        o.chain = 1;
        char err[512] = {0};
        struct neth_vec *v = neth_vec_make(&o, err, sizeof err);
        if (!v) return fprintf(stderr, "make: %s\n", err), 1;
        int rc = neth_vec_recv(v, err, sizeof err) == 0 ?
                 neth_vec_chain_memory_read(v, 0, argv[3], err, sizeof err) : -1;
        neth_vec_free(v);
        if (rc) return fprintf(stderr, "LEGACY FAIL: %s\n", err), 1;
        puts("LEGACY PASS: saved sidecar read");
        return 0;
    }
    int paired = argc > 1 && !strcmp(argv[1], "paired");
    int byte_mode = argc > 1 && !strcmp(argv[1], "bytes");
    int prefix_mode = argc > 1 && !strcmp(argv[1], "prefix");
    int at = paired || byte_mode || prefix_mode ? 2 : 1;
    if (argc < at + 2 || argc > at + 6)
        return fprintf(stderr, "usage: chain_mem_check [paired|bytes|prefix] RUN.yaml STEPS [MOPTS [START [MACROS [DEV]]]]\n"), 2;
    int steps = atoi(argv[at + 1]);
    if (steps < 1001) return fprintf(stderr, "STEPS must exceed 1000\n"), 2;
    uint64_t a, b;
    struct sample *straight = calloc((size_t)steps, sizeof *straight);
    if (!straight) return 1;
    const char *mopts = argc > at + 2 && argv[at + 2][0] ? argv[at + 2] : NULL;
    const char *start = argc > at + 3 && argv[at + 3][0] ? argv[at + 3] : NULL;
    int macros = argc > at + 4 ? atoi(argv[at + 4]) : CHAIN_TEST_MACROS;
    int dev = argc > at + 5 ? atoi(argv[at + 5]) : 0;
    if (byte_mode)
    {
        int rc = one_run(argv[at], start, mopts, macros, dev, steps, 500, 1, 1, 0, &a, NULL, NULL);
        free(straight);
        if (rc) return 1;
        puts("CHAIN-MEMORY-BYTES PASS: every controller byte equal at each disk restore");
        return 0;
    }
    if (prefix_mode)
    {
        prefix_fixture_enabled = 1;
        int rc = one_run(argv[at], start, mopts, macros, dev, steps, 0, 0, 0, 0, &a, straight, NULL) ||
                 one_run(argv[at], start, mopts, macros, dev, steps, 500, 1, 1, 0, &b, NULL, straight);
        free(straight);
        if (rc || a != b) return fprintf(stderr, "CHAIN-MEMORY-PREFIX FAIL: trajectory differs\n"), 1;
        /* Frozen pre-version-3 library, same 1501-step pearl start and macro mask, options off. Its in-memory and
         * disk-cut runs both gave this digest; the new library's straight and shortened-sidecar runs must keep it. */
        if (!strcmp(argv[at], "configs/tests/pearlcase.yaml") && steps == 1501 && !mopts && !start &&
            macros == 33554431 && dev == 1 && a != 0x94951fc0cc47c274ull)
            return fprintf(stderr, "CHAIN-MEMORY-PREFIX FAIL: pre-version-3 digest changed, got %016llx\n",
                           (unsigned long long)a), 1;
        puts("CHAIN-MEMORY-PREFIX PASS: an older shorter controller restores every byte and trajectory; a fixed-layout change is refused");
        return 0;
    }
    int rc = one_run(argv[at], start, mopts, macros, dev, steps, paired ? 500 : 0, 0, 0, 0, &a, straight, NULL) ||
             one_run(argv[at], start, mopts, macros, dev, steps, 500, 1, 0, 0, &b, NULL, straight);
    free(straight);
    if (rc) return 1;
    if (a != b) return fprintf(stderr, "CHAIN-MEMORY FAIL: trajectory digests differ\n"), 1;
    puts(paired ? "CHAIN-MEMORY PASS: 500-step in-memory and disk restores are equal" :
                  "CHAIN-MEMORY PASS: straight and 500-step disk restores are equal");
    return 0;
}
