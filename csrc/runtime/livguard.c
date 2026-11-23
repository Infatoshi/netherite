/* livguard (lane/livfault): the living hook guard's check (engine/living.c living_hook_known).
 *
 *   out/runtime/livguard [SNAPSHOT]   (from the root; default out/java/snapshots/gold-g15-s42, a product start)
 *
 * On 2026-10-31 at 19:26 a trainer's worker took a general protection fault on `call *0x2f8(%rbx)`, a living's
 * on_living_update overwritten, and the host went down with it. The guard checks the pointer against the table of
 * hooks before every call and stops the program with a LIVFAULT report instead of jumping. This program holds it to
 * that: it runs itself as a child that resets one pool env to a recording's start (dozens of livings), steps it, then
 * writes a non-canonical address (the 19:26 kind) over every taken living's hook and steps again. The child must end
 * on SIGABRT with the guard's LIVFAULT report (its slot, kind and the nearest hook); without the guard (a library
 * built with -DLIVING_HOOK_GUARD=0) it dies on SIGSEGV at the jump and the check fails. The parent also runs a
 * child that corrupts nothing and must exit 0 after the same steps (the guard passes every real hook), its session
 * at 64 bytes (pool.c: the client player's _Alignas(64) tables, the 13:48 faults of a -march=znver5 build).
 *
 * One line a case, then LIVGUARD PASS or LIVGUARD FAIL; exit 0 on PASS. */
#define _GNU_SOURCE
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../engine/env.h"
#include "../engine/living.h"
#include "pool.h"

/* the value written over the hooks: bit 63 set, bits 47..62 not, so no canonical address (a #GP at the call) */
#define BAD_HOOK ((uintptr_t)0x8000123456789abcull)

static int step(struct pool *p, int ticks, uint64_t *seed)
{
    int id = 0;
    for (int t = 0; t < ticks; ++t)
    {
        struct pool_act a;
        memset(&a, 0, sizeof a);
        a.kind = PA_AGENT;
        a.hotbar = -1;
        *seed ^= *seed << 13, *seed ^= *seed >> 7, *seed ^= *seed << 17;
        if (*seed & 1) a.hold |= 1u << PK_FORWARD;
        a.look_mode = PL_DLOOK;
        a.look[0] = (float)((int)(*seed >> 8 & 63) - 32) * 0.1F;
        if (pool_step(p, &id, 1, &a, 1, 0)) return -1;
        const struct pool_result *r;
        while (pool_poll(p, 1, &r) == 0) {}
    }
    return 0;
}

/* the child: reset, 20 ticks, every taken living's hook overwritten (corrupt), 5 ticks more */
static int child(const char *snap, int corrupt)
{
    /* the stop it is meant to take writes no core (apport) */
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
    struct pool_config cfg = {0};
    cfg.n = 1;
    cfg.threads = 1;
    cfg.obs.ticks = 1;
    char err[512];
    struct pool *p = pool_make(&cfg, err, sizeof err);
    if (p == NULL) { fprintf(stderr, "livguard: pool: %s\n", err); return 3; }
    struct pool_start *s = pool_start_load(p, snap, err, sizeof err);
    if (s == NULL) { fprintf(stderr, "livguard: %s: %s\n", snap, err); return 3; }
    int id = 0;
    if (pool_reset(p, &id, 1, s, err, sizeof err)) { fprintf(stderr, "livguard: reset: %s\n", err); return 3; }
    /* the session at its type's alignment (pool.c; the 13:48 -march=znver5 faults: 16 bytes before) */
    if ((uintptr_t)pool_session(p, 0) & 63)
    {
        fprintf(stderr, "livguard: the session at %p is not 64-byte aligned\n", (void *)pool_session(p, 0));
        return 5;
    }
    uint64_t seed = 0x9e3779b97f4a7c15ull;
    if (step(p, 20, &seed)) { fprintf(stderr, "livguard: step failed\n"); return 3; }
    /* the env's livings pool: every slot under top not on the free chain and made (self set) */
    struct env *e = pool_env(p, 0);
    const struct slot_pool *lp = &e->arena.livings;
    uint8_t *is_free = calloc((size_t)lp->top + 1, 1);
    for (int32_t i = lp->free_head, n = 0; i >= 0 && n <= lp->cap; i = lp->next[i], ++n) is_free[i] = 1;
    int taken = 0;
    for (int32_t i = 0; i < lp->top; ++i)
    {
        struct living *l = pool_at(lp, i);
        if (is_free[i] || l->e.self != l) continue;
        ++taken;
        if (corrupt) memcpy(&l->on_living_update, &(uintptr_t){BAD_HOOK}, sizeof(uintptr_t));
    }
    free(is_free);
    fprintf(stderr, "livguard: %d livings taken, %s\n", taken, corrupt ? "every hook overwritten" : "none touched");
    if (taken == 0) return 4;
    if (step(p, 5, &seed)) { fprintf(stderr, "livguard: step failed\n"); return 3; }
    pool_free(p);
    pool_start_free(s);
    return 0;
}

/* run this program as the child; its stderr into buf; the wait status */
static int run_child(const char *self, const char *snap, int corrupt, char *buf, size_t cap)
{
    int fd[2];
    if (pipe(fd)) return -1;
    pid_t pid = fork();
    if (pid == 0)
    {
        dup2(fd[1], 2);
        close(fd[0]);
        close(fd[1]);
        execl(self, self, corrupt ? "--child-corrupt" : "--child-clean", snap, (char *)NULL);
        _exit(127);
    }
    close(fd[1]);
    size_t n = 0;
    for (ssize_t k; n + 1 < cap && (k = read(fd[0], buf + n, cap - 1 - n)) > 0;) n += (size_t)k;
    buf[n] = 0;
    for (char tmp[4096]; read(fd[0], tmp, sizeof tmp) > 0;) {}
    close(fd[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    return st;
}

static const char *line_of(const char *buf, const char *what)
{
    static char out[200];
    const char *s = strstr(buf, what);
    if (s == NULL) return "";
    size_t k = strcspn(s, "\n");
    snprintf(out, sizeof out, "%.*s", (int)(k < sizeof out - 1 ? k : sizeof out - 1), s);
    return out;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "--child-corrupt")) return child(argv[2], 1);
    if (argc >= 3 && !strcmp(argv[1], "--child-clean")) return child(argv[2], 0);
    const char *snap = argc >= 2 ? argv[1] : "out/java/snapshots/gold-g15-s42";
    static char buf[1 << 16];
    int fail = 0;

    int st = run_child("/proc/self/exe", snap, 0, buf, sizeof buf);
    int ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
    printf("livguard clean: %s (%s; %s)\n", ok ? "PASS" : "FAIL", line_of(buf, "livguard:"),
           WIFEXITED(st) ? "exited" : strsignal(WTERMSIG(st)));
    fail |= !ok;

    st = run_child("/proc/self/exe", snap, 1, buf, sizeof buf);
    ok = WIFSIGNALED(st) && WTERMSIG(st) == SIGABRT && strstr(buf, "LIVFAULT living") != NULL &&
         strstr(buf, "is no hook") != NULL;
    printf("livguard corrupt: %s (%s; %s)\n", ok ? "PASS" : "FAIL",
           WIFSIGNALED(st) ? strsignal(WTERMSIG(st)) : "exited, not stopped", line_of(buf, "LIVFAULT the nearest"));
    if (!ok && buf[0]) fprintf(stderr, "%s", buf);
    fail |= !ok;

    printf("LIVGUARD %s\n", fail ? "FAIL" : "PASS");
    return fail;
}
