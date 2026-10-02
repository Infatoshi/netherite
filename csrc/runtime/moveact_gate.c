/* The action compiler's gate (lane/moveact, moveact.mk): a seeded stand-in
 * for the policy drives one env through libnwrl.so with a lever on (hold:
 * attack presses that hold, releases, the camera nudged; look: look
 * targets on a 16 x 9 grid and pixels; both, at any ticks a step), the
 * compiled per-tick acts written as a Java script (moveact.h
 * moveact_script). Recorded once (--script OUT: Snapshot, then the acts,
 * hold-only ticks merged), the script is a Java recording; the check runs
 * the same driver from that recording's start and holds the compiled script
 * to the stored one (--check-script) and every tick's row record to
 * test_snapshots' over the recording (--ref, rlgui.h nwrl_rows), so what the
 * compiler emits is what the oracle ran.
 *
 *   moveact_gate --config FILE --start DIR --lever hold|look|both [--tps T]
 *                [--steps N] [--seed S] [--look-ticks L]
 *                [--script OUT] [--check-script REF] [--ref ROWS --rows OUT]
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rlbind.h"
#include "rlgui.h"
#include "rlmove.h"

enum { PK_FORWARD, PK_BACK, PK_LEFT, PK_RIGHT, PK_JUMP, PK_SNEAK, PK_SPRINT, PK_ATTACK };

static uint64_t rng;
static double urand(void)
{
    rng = rng * 6364136223846793005ull + 1442695040888963407ull;
    return (double)(rng >> 11) * (1.0 / 9007199254740992.0);
}

static char *slurp(const char *path, long *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) n = 0;
    b[n] = 0;
    fclose(f);
    if (len) *len = n;
    return b;
}

/* a line whose act is holds only (or nothing): ticks of one multi-tick step */
static int hold_only_line(const char *l)
{
    return !strstr(l, "look") && !strstr(l, "press") && !strstr(l, "hotbar") && !strstr(l, "ctrl") && !strstr(l, "gui");
}

/* the raw per-tick script to the stored form: Snapshot first, equal
 * hold-only lines merged into one step of n ticks */
static int merge_script(const char *raw, const char *out)
{
    char *b = slurp(raw, NULL);
    if (!b) return -1;
    FILE *f = fopen(out, "w");
    if (!f) { free(b); return -1; }
    fputs("{\"cmd\":\"run\",\"class\":\"Snapshot\"}\n", f);
    const char *prev = NULL;
    size_t plen = 0;
    int n = 0;
    for (char *s = b; *s;)
    {
        char *e = strchr(s, '\n');
        if (!e) break;
        *e = 0;
        const char *act = strstr(s, "\"act\":");
        int mergeable = act && hold_only_line(act);
        if (prev && mergeable && strlen(act) == plen && !memcmp(prev, act, plen))
            ++n;
        else
        {
            if (prev) fprintf(f, "{\"n\":%d,%s\n", n, prev);
            prev = mergeable ? act : NULL;
            plen = prev ? strlen(act) : 0;
            n = 1;
            if (!mergeable) fprintf(f, "%s\n", s);
        }
        s = e + 1;
    }
    if (prev) fprintf(f, "{\"n\":%d,%s\n", n, prev);
    fclose(f);
    free(b);
    return 0;
}

static int rows_cmp(const char *ref, const char *got, long *nrows)
{
    char *a = slurp(ref, NULL), *b = slurp(got, NULL);
    if (!a || !b) { fprintf(stderr, "moveact_gate: cannot read %s or %s\n", ref, got); return -1; }
    /* index the reference by tick */
    int bad = 0;
    long n = 0;
    for (char *s = b; *s;)
    {
        char *e = strchr(s, '\n');
        if (!e) break;
        *e = 0;
        long t = strtol(s + 2, NULL, 10);
        char key[32];
        snprintf(key, sizeof key, "t=%ld ", t);
        char *r = strstr(a, key);
        while (r && r != a && r[-1] != '\n') r = strstr(r + 1, key);
        size_t len = strlen(s);
        if (!r || strncmp(r, s, len) || (r[len] != '\n' && r[len] != 0))
        {
            size_t j = 0;
            while (r && s[j] && r[j] == s[j]) ++j;
            fprintf(stderr, "FAIL row %ld: at column %zu: want %.60s got %.60s\n", t, j, r ? r + j : "(none)", s + j);
            bad = 1;
            break;
        }
        ++n;
        s = e + 1;
    }
    *nrows = n;
    free(a);
    free(b);
    return bad;
}

int main(int argc, char **argv)
{
    const char *config = NULL, *start = NULL, *lever = "hold", *script = NULL, *check = NULL, *ref = NULL,
               *rows = NULL;
    int tps = 1, steps = 600, look_ticks = 3, device = 0;
    uint64_t seed = 1;
    for (int i = 1; i < argc; ++i)
    {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--config") && v) config = argv[++i];
        else if (!strcmp(a, "--start") && v) start = argv[++i];
        else if (!strcmp(a, "--lever") && v) lever = argv[++i];
        else if (!strcmp(a, "--tps") && v) tps = atoi(argv[++i]);
        else if (!strcmp(a, "--steps") && v) steps = atoi(argv[++i]);
        else if (!strcmp(a, "--seed") && v) seed = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(a, "--look-ticks") && v) look_ticks = atoi(argv[++i]);
        else if (!strcmp(a, "--script") && v) script = argv[++i];
        else if (!strcmp(a, "--check-script") && v) check = argv[++i];
        else if (!strcmp(a, "--ref") && v) ref = argv[++i];
        else if (!strcmp(a, "--rows") && v) rows = argv[++i];
        else if (!strcmp(a, "--device") && v) device = atoi(argv[++i]);
        else { fprintf(stderr, "moveact_gate: unknown argument %s\n", a); return 2; }
    }
    if (!config || !start || (!script && !check)) { fprintf(stderr, "moveact_gate: --config, --start and --script or --check-script\n"); return 2; }
    int hold = !strcmp(lever, "hold") || !strcmp(lever, "both"), look = !strcmp(lever, "look") || !strcmp(lever, "both");
    if (!hold && !look) { fprintf(stderr, "moveact_gate: --lever hold, look or both\n"); return 2; }
    rng = seed * 0x9e3779b97f4a7c15ull + 1;

    char err[512];
    struct nwrl_opts o = {0};
    o.config = config;
    o.start = start;
    o.n = 1;
    o.device = device;
    o.device_mesh = -1;
    struct nwrl *h = nwrl_make_ex(&o, 0, err, sizeof err);
    if (!h) { fprintf(stderr, "moveact_gate: %s\n", err); return 1; }
    struct nwrl_move_cfg mc = {hold, look, look_ticks, 0.0F, 0.0F};
    nwrl_move_config(h, &mc);

    char raw[4096], tape[4096];
    snprintf(raw, sizeof raw, "%s.raw", script ? script : check);
    remove(raw);
    snprintf(tape, sizeof tape, "%s/tape.jsonl", start);
    int id = 0;
    struct nwrl_result *res = calloc(1, sizeof *res);
    /* the start's tick: a no-op poll is not possible before a step, so the
     * rows' first tick is the first result's t less its ticks */
    if (nwrl_move_script(h, id, raw)) { fprintf(stderr, "moveact_gate: cannot write %s\n", raw); return 1; }
    int64_t t0 = -1;
    if (rows)
    {
        /* the recording's own start: its tape's first row after the snapshot */
        char *m = NULL;
        char mpath[4096];
        snprintf(mpath, sizeof mpath, "%s/manifest.json", start);
        m = slurp(mpath, NULL);
        const char *tk = m ? strstr(m, "\"tick\":") : NULL;
        t0 = tk ? strtoll(tk + 7, NULL, 10) : 0;
        free(m);
        if (nwrl_rows(h, id, rows, nwrl_cw_from(tape, t0))) { fprintf(stderr, "moveact_gate: cannot write %s\n", rows); return 1; }
    }

    int fwd_left = 0, fwd = 0, sprint = 0, errs = 0, noframe = 0, mining = 0;
    uint32_t keys = 0;
    for (int s = 0; s < steps; ++s)
    {
        struct nwrl_act a;
        memset(&a, 0, sizeof a);
        a.hotbar = -1;
        /* movement in runs: forward (sometimes sprinting), standing, a strafe */
        if (fwd_left <= 0)
        {
            double r = urand();
            fwd = r < 0.45 ? 1 : r < 0.75 ? 0 : r < 0.87 ? 2 : 3;
            sprint = fwd == 1 && urand() < 0.4;
            fwd_left = 10 + (int)(urand() * 30);
        }
        --fwd_left;
        keys = 0;
        if (fwd == 1) keys |= 1u << PK_FORWARD;
        if (fwd == 2) keys |= 1u << PK_LEFT;
        if (fwd == 3) keys |= 1u << PK_RIGHT;
        if (sprint) keys |= 1u << PK_SPRINT;
        if (fwd && urand() < 0.08) keys |= 1u << PK_JUMP;
        a.hold = keys;
        /* the first step looks down at the ground ahead (blocks to break) */
        if (s == 0)
        {
            a.look_mode = 2;
            a.look[1] = 35.0F;
        }
        else if (!mining && urand() < (look ? 0.04 : 0.10))
        {
            /* the policy's own camera (the mu-law head's deltas) */
            a.look_mode = 2;
            a.look[0] = (float)((urand() - 0.5) * 12.0);
            a.look[1] = (float)((urand() - 0.5) * 6.0);
        }
        struct nwrl_move mv = {0, 0, 0.0F, 0.0F};
        if (hold)
        {
            /* a press, then mostly standing still while it digs (a fifth of
             * the time let go early) */
            double r = urand();
            if (mining > 0)
            {
                a.hold = 0;
                if (urand() < 0.05) mv.attack = 2, mining = 0;
                else --mining;
            }
            else if (r < 0.06)
            {
                mv.attack = 1;
                if (urand() < 0.6) mining = 6 + (int)(urand() * 30);
            }
            else if (r < 0.07) mv.attack = 2;
        }
        if (look && urand() < 0.12)
        {
            mv.look = 1;
            if (urand() < 0.7)
            {
                int cx = (int)(urand() * 16), cy = (int)(urand() * 9);
                mv.u = (cx + 0.5F) / 16.0F;
                mv.v = (cy + 0.5F) / 9.0F;
            }
            else
            {
                mv.u = (float)urand();
                mv.v = (float)urand();
            }
        }
        if (nwrl_move(h, &id, 1, &mv, err, sizeof err)) { fprintf(stderr, "moveact_gate: %s\n", err); return 1; }
        if (nwrl_step_ticks(h, &id, 1, &a, tps)) { fprintf(stderr, "moveact_gate: step %d failed\n", s); return 1; }
        if (nwrl_poll(h, 1, res) != 1) { fprintf(stderr, "moveact_gate: no result at step %d\n", s); return 1; }
        if (res->flags & (32 | 4)) { fprintf(stderr, "moveact_gate: step %d: %s\n", s, res->err); ++errs; break; }
        noframe += res->frame_rc != 0;
        /* dead: respawn as the macros do */
        if (res->screen == 8)
        {
            struct nwrl_act r;
            memset(&r, 0, sizeof r);
            r.hotbar = -1;
            r.nops = 1;
            r.ops[0].kind = 3;
            nwrl_step_ticks(h, &id, 1, &r, 1);
            nwrl_poll(h, 1, res);
        }
    }
    uint64_t st[NWRL_MOVE_STATS];
    nwrl_move_stats(h, id, st);
    nwrl_move_script(h, id, NULL);
    if (rows) nwrl_rows(h, id, NULL, 0);
    nwrl_free(h);
    printf("moveact_gate %s tps %d: %d steps; presses %llu (on a block %llu), holds ended: broken %llu, target %llu, "
           "release %llu, screen %llu; held ticks %llu; turns %llu over %llu ticks; frames missing %d\n",
           lever, tps, steps, (unsigned long long)st[0], (unsigned long long)st[1], (unsigned long long)st[2],
           (unsigned long long)st[3], (unsigned long long)st[4], (unsigned long long)st[5], (unsigned long long)st[6],
           (unsigned long long)st[7], (unsigned long long)st[8], noframe);
    int bad = errs || noframe;
    if (script)
    {
        if (merge_script(raw, script)) { fprintf(stderr, "moveact_gate: cannot write %s\n", script); return 1; }
        printf("script %s\n", script);
    }
    if (check)
    {
        char mine[4096];
        snprintf(mine, sizeof mine, "%s.got", check);
        merge_script(raw, mine);
        long la, lb;
        char *A = slurp(check, &la), *B = slurp(mine, &lb);
        int same = A && B && la == lb && !memcmp(A, B, (size_t)la);
        printf("script %s: %s\n", check, same ? "equal" : "DIFFERS");
        bad |= !same;
        free(A);
        free(B);
        if (same) remove(mine);
    }
    if (rows && ref)
    {
        long n = 0;
        int rb = rows_cmp(ref, rows, &n);
        printf("rows: %ld equal to %s%s\n", n, ref, rb ? " until the FAIL" : "");
        bad |= rb != 0 || n == 0;
    }
    remove(raw);
    printf("MOVEACT-GATE %s %s\n", lever, bad ? "FAIL" : "PASS");
    return bad;
}
