/* nether CLI: processes, locks, the GPU guard and the hardware counters (cli_proc.h). */
#define _GNU_SOURCE
#include "cli_proc.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#if defined(__linux__)
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#endif

extern char **environ;

volatile int cli_interrupted;
static volatile pid_t g_pid, g_pgid;

static void on_signal(int sig)
{
    cli_interrupted = sig;
    if (g_pgid > 0) kill(-g_pgid, sig);
    else if (g_pid > 0) kill(g_pid, sig);
}

static int needs_quote(const char *s)
{
    if (!*s) return 1;
    for (const char *p = s; *p; ++p)
        if (!(*p == '_' || *p == '-' || *p == '.' || *p == '/' || *p == ':' || *p == '=' || *p == ',' || *p == '+' ||
              *p == '@' || *p == '%' || (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')))
            return 1;
    return 0;
}

void cli_argv_line(const char *const *argv, struct sb *b)
{
    for (int i = 0; argv[i]; ++i) {
        if (i) sb_putc(b, ' ');
        if (!needs_quote(argv[i])) {
            sb_puts(b, argv[i]);
            continue;
        }
        sb_putc(b, '\'');
        for (const char *p = argv[i]; *p; ++p)
            if (*p == '\'') sb_puts(b, "'\\''");
            else sb_putc(b, *p);
        sb_putc(b, '\'');
    }
    sb_str(b);
}

int cli_have(const char *prog)
{
    if (strchr(prog, '/')) return access(prog, X_OK) == 0;
    const char *path = getenv("PATH");
    if (!path) return 0;
    for (const char *p = path; *p;) {
        size_t n = strcspn(p, ":");
        char f[PATH_MAX];
        if (n && snprintf(f, sizeof f, "%.*s/%s", (int)n, p, prog) < (int)sizeof f && access(f, X_OK) == 0) return 1;
        p += n;
        if (*p == ':') ++p;
    }
    return 0;
}

/* the environment: ours, less unset names and the names env_set gives, then env_set */
static char **make_env(const struct cli_spawn_opts *o)
{
    struct sv e = {0};
    for (char **p = environ; *p; ++p) {
        size_t n = strcspn(*p, "=");
        int drop = 0;
        for (const char *const *u = o->env_unset; u && *u && !drop; ++u)
            if (strlen(*u) == n && !strncmp(*u, *p, n)) drop = 1;
        for (const char *const *s = o->env_set; s && *s && !drop; ++s)
            if (strcspn(*s, "=") == n && !strncmp(*s, *p, n)) drop = 1;
        if (!drop) sv_push(&e, *p);
    }
    for (const char *const *s = o->env_set; s && *s; ++s) sv_push(&e, *s);
    if (!e.v) sv_push(&e, "NETHERITE_CLI=1");
    return e.v;
}

static void free_env(char **v)
{
    for (char **p = v; p && *p; ++p) free(*p);
    free(v);
}

/* ---- hardware counters: two sets on ourselves, one inherited by every process we start and one not; a child's
 * count is their difference once it is reaped (the kernel folds an exited child's counts into the inherited one) */

struct perfset {
    int fd[4]; /* cycles, instructions: inherited, then ours alone */
    int ok;
};

#if defined(__linux__)
static int perf_open(uint64_t config, int inherit)
{
    struct perf_event_attr a;
    memset(&a, 0, sizeof a);
    a.size = sizeof a;
    a.type = PERF_TYPE_HARDWARE;
    a.config = config;
    a.inherit = (unsigned)inherit;
    a.exclude_kernel = 1;
    a.exclude_hv = 1;
    a.disabled = 1;
    return (int)syscall(SYS_perf_event_open, &a, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
}
#endif

static void perf_start(struct perfset *p, char *why, size_t wn)
{
    p->ok = 0;
    for (int i = 0; i < 4; ++i) p->fd[i] = -1;
#if defined(__linux__)
    static const uint64_t cfg[2] = {PERF_COUNT_HW_CPU_CYCLES, PERF_COUNT_HW_INSTRUCTIONS};
    for (int i = 0; i < 4; ++i) {
        p->fd[i] = perf_open(cfg[i & 1], i < 2);
        if (p->fd[i] < 0) {
            snprintf(why, wn, "perf_event_open: %s (kernel.perf_event_paranoid; rusage only)", strerror(errno));
            for (int k = 0; k < i; ++k) close(p->fd[k]), p->fd[k] = -1;
            p->fd[i] = -1;
            return;
        }
    }
    for (int i = 0; i < 4; ++i) ioctl(p->fd[i], PERF_EVENT_IOC_RESET, 0);
    for (int i = 0; i < 4; ++i) ioctl(p->fd[i], PERF_EVENT_IOC_ENABLE, 0);
    p->ok = 1;
#else
    snprintf(why, wn, "no perf_event_open on this system (rusage only)");
#endif
}

static void perf_stop(struct perfset *p, struct cli_spawn_res *r)
{
#if defined(__linux__)
    if (!p->ok) return;
    unsigned long long v[4] = {0};
    int ok = 1;
    for (int i = 0; i < 4; ++i) ioctl(p->fd[i], PERF_EVENT_IOC_DISABLE, 0);
    for (int i = 0; i < 4; ++i)
        if (read(p->fd[i], &v[i], sizeof v[i]) != (ssize_t)sizeof v[i]) ok = 0;
    for (int i = 0; i < 4; ++i) close(p->fd[i]);
    if (!ok) {
        snprintf(r->perf_why, sizeof r->perf_why, "perf counters unreadable");
        return;
    }
    r->cycles = v[0] > v[2] ? v[0] - v[2] : 0;
    r->instructions = v[1] > v[3] ? v[1] - v[3] : 0;
    r->ipc = r->cycles ? (double)r->instructions / (double)r->cycles : 0;
    r->perf_ok = 1;
#else
    (void)p;
    (void)r;
#endif
}

static void finish(struct cli_spawn_res *r, int st, const struct rusage *ru)
{
    if (WIFEXITED(st)) r->rc = WEXITSTATUS(st);
    else if (WIFSIGNALED(st)) r->rc = 128 + WTERMSIG(st), r->signaled = 1;
    else r->rc = 1;
    if (r->timed_out) r->rc = 124;
    r->utime = (double)ru->ru_utime.tv_sec + ru->ru_utime.tv_usec * 1e-6;
    r->stime = (double)ru->ru_stime.tv_sec + ru->ru_stime.tv_usec * 1e-6;
#if defined(__APPLE__)
    r->maxrss_kb = ru->ru_maxrss / 1024;
#else
    r->maxrss_kb = ru->ru_maxrss;
#endif
    r->minflt = ru->ru_minflt;
    r->majflt = ru->ru_majflt;
}

static void sink(const struct cli_spawn_opts *o, int fdout, const char *buf, size_t n)
{
    if (o->capture) sb_putn(o->capture, buf, n);
    if (o->tee) fwrite(buf, 1, n, stdout), fflush(stdout);
    if (fdout >= 0) {
        size_t off = 0;
        while (off < n) {
            ssize_t w = write(fdout, buf + off, n - off);
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) break;
            off += (size_t)w;
        }
    }
}

static int spawn_inner(const struct cli_spawn_opts *o, struct cli_spawn_res *r)
{
    memset(r, 0, sizeof *r);
    r->rc = 127;
    if (!o->quiet) {
        struct sb l = {0};
        cli_argv_line(o->argv, &l);
        if (o->cwd) printf("nether: $ (cd %s) %s\n", o->cwd, l.s);
        else printf("nether: $ %s\n", l.s);
        sb_free(&l);
        fflush(stdout);
    }
    int pgroup = o->new_pgroup || o->timeout_s > 0;
    int want_pipe = o->capture || (o->tee && o->out_path);
    int p[2] = {-1, -1};
    posix_spawn_file_actions_t fa;
    posix_spawnattr_t at;
    posix_spawn_file_actions_init(&fa);
    posix_spawnattr_init(&at);
    if (want_pipe) {
        if (pipe(p)) {
            fprintf(stderr, "nether: pipe: %s\n", strerror(errno));
            return -1;
        }
        fcntl(p[0], F_SETFD, FD_CLOEXEC);
        posix_spawn_file_actions_adddup2(&fa, p[1], 1);
        if (!o->null_stderr && !o->err_path) posix_spawn_file_actions_adddup2(&fa, p[1], 2);
        posix_spawn_file_actions_addclose(&fa, p[1]);
    } else if (o->out_path) {
        posix_spawn_file_actions_addopen(&fa, 1, o->out_path, O_WRONLY | O_CREAT | (o->append ? O_APPEND : O_TRUNC), 0644);
        if (!o->null_stderr && !o->err_path) posix_spawn_file_actions_adddup2(&fa, 1, 2);
    }
    if (o->null_stderr) posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    else if (o->err_path) posix_spawn_file_actions_addopen(&fa, 2, o->err_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (o->stdin_null) posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    if (o->cwd) posix_spawn_file_actions_addchdir_np(&fa, o->cwd);
    short flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
    sigset_t none, def;
    sigemptyset(&none);
    sigemptyset(&def);
    sigaddset(&def, SIGINT);
    sigaddset(&def, SIGTERM);
    sigaddset(&def, SIGHUP);
    sigaddset(&def, SIGPIPE);
    posix_spawnattr_setsigmask(&at, &none);
    posix_spawnattr_setsigdefault(&at, &def);
    if (pgroup) {
        flags |= POSIX_SPAWN_SETPGROUP;
        posix_spawnattr_setpgroup(&at, 0);
    }
    posix_spawnattr_setflags(&at, flags);
    char **envp = make_env(o);
    struct perfset ps = {{-1, -1, -1, -1}, 0};
    if (o->perf) perf_start(&ps, r->perf_why, sizeof r->perf_why);

    /* our handlers forward a signal to the child (its group) while it runs */
    struct sigaction sa, old[3];
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    static const int SIGS[3] = {SIGINT, SIGTERM, SIGHUP};
    for (int i = 0; i < 3; ++i) sigaction(SIGS[i], &sa, &old[i]);

    fflush(stdout);
    fflush(stderr);
    pid_t pid;
    double t0 = cli_now();
    int e = posix_spawnp(&pid, o->argv[0], &fa, &at, (char *const *)o->argv, envp);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);
    free_env(envp);
    if (p[1] >= 0) close(p[1]);
    if (e) {
        fprintf(stderr, "nether: cannot run %s: %s\n", o->argv[0], strerror(e));
        if (p[0] >= 0) close(p[0]);
        for (int i = 0; i < 3; ++i) sigaction(SIGS[i], &old[i], NULL);
        if (ps.ok)
            for (int i = 0; i < 4; ++i) close(ps.fd[i]);
        return -1;
    }
    g_pid = pid;
    g_pgid = pgroup ? pid : 0;

    int fdout = -1;
    if (want_pipe && o->out_path) {
        fdout = open(o->out_path, O_WRONLY | O_CREAT | (o->append ? O_APPEND : O_TRUNC), 0644);
        if (fdout < 0) fprintf(stderr, "nether: cannot write %s: %s\n", o->out_path, strerror(errno));
    }
    double grace = o->grace_s > 0 ? o->grace_s : 10;
    double deadline = o->timeout_s > 0 ? t0 + o->timeout_s : 0, kill_at = 0;
    int st = 0, reaped = 0, eof = !want_pipe;
    double drain_until = 0;
    struct rusage ru;
    memset(&ru, 0, sizeof ru);
    while (!reaped || !eof) {
        double now = cli_now();
        if (!reaped && deadline && now >= deadline && !r->timed_out) {
            r->timed_out = 1;
            fprintf(stderr, "nether: %s ran past %.0f s: SIGTERM to its group\n", o->argv[0], o->timeout_s);
            kill(pgroup ? -pid : pid, SIGTERM);
            kill_at = now + grace;
        }
        if (!reaped && kill_at && now >= kill_at) {
            kill(pgroup ? -pid : pid, SIGKILL);
            kill_at = 0;
        }
        if (!reaped) {
            pid_t w = wait4(pid, &st, (want_pipe || deadline || kill_at) ? WNOHANG : 0, &ru);
            if (w == pid) {
                reaped = 1;
                r->wall = cli_now() - t0;
                drain_until = cli_now() + 2.0; /* a grandchild that keeps the pipe gets 2 s, then is left behind */
                g_pid = 0;
                g_pgid = 0;
                continue;
            }
            if (w < 0 && errno != EINTR) break;
        }
        if (!eof) {
            struct pollfd pf = {p[0], POLLIN, 0};
            int k = poll(&pf, 1, reaped ? 100 : 50);
            if (k > 0) {
                char buf[65536];
                ssize_t n = read(p[0], buf, sizeof buf);
                if (n > 0) {
                    sink(o, fdout, buf, (size_t)n);
                    if (reaped) drain_until = cli_now() + 0.5;
                } else if (n == 0 || (errno != EINTR && errno != EAGAIN))
                    eof = 1;
            }
            if (reaped && cli_now() > drain_until) eof = 1;
        } else if (!reaped && (deadline || kill_at)) {
            struct timespec ts = {0, 20 * 1000 * 1000};
            nanosleep(&ts, NULL);
        }
    }
    if (p[0] >= 0) close(p[0]);
    if (fdout >= 0) close(fdout);
    for (int i = 0; i < 3; ++i) sigaction(SIGS[i], &old[i], NULL);
    g_pid = 0;
    g_pgid = 0;
    if (!reaped) {
        r->rc = 127;
        return -1;
    }
    if (o->capture) sb_str(o->capture);
    finish(r, st, &ru);
    perf_stop(&ps, r);
    return 0;
}

int cli_perf_stat(const char *const *argv, const char *cwd, struct cli_spawn_res *r)
{
    struct cli_spawn_opts o = {.argv = argv, .cwd = cwd, .perf = 1};
    return cli_spawn(&o, r);
}

/* ---- locks */

static int lock_inner(const char *path, double timeout_s)
{
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0666);
    if (fd < 0) {
        fprintf(stderr, "nether: cannot open the lock %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (flock(fd, LOCK_EX | LOCK_NB)) {
        if (errno != EWOULDBLOCK) {
            fprintf(stderr, "nether: flock %s: %s\n", path, strerror(errno));
            close(fd);
            return -1;
        }
        char who[128] = "";
        ssize_t n = pread(fd, who, sizeof who - 1, 0);
        if (n > 0) who[n] = 0, who[strcspn(who, "\n")] = 0;
        printf("nether: waiting for the lock %s (held by %s)\n", path, who[0] ? who : "another process");
        fflush(stdout);
        double t0 = cli_now();
        for (;;) {
            if (!flock(fd, LOCK_EX | LOCK_NB)) break;
            if (cli_interrupted || (timeout_s > 0 && cli_now() - t0 > timeout_s)) {
                close(fd);
                return cli_interrupted ? -1 : -2;
            }
            struct timespec ts = {0, 250 * 1000 * 1000};
            nanosleep(&ts, NULL);
        }
        printf("nether: got the lock %s after %.1f s\n", path, cli_now() - t0);
    }
    char stamp[64], line[160];
    cli_stamp(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%S");
    int l = snprintf(line, sizeof line, "pid %ld since %s\n", (long)getpid(), stamp);
    if (ftruncate(fd, 0) == 0 && pwrite(fd, line, (size_t)l, 0) < 0) { /* the note is advisory */
    }
    return fd;
}

void cli_lock_release(int fd)
{
    if (fd < 0) return;
    if (ftruncate(fd, 0)) { /* advisory */
    }
    flock(fd, LOCK_UN);
    close(fd);
}

/* ---- the GPU guard */

static int gpu_inner(int gpu, int max_mib, int max_util, double wait_s, int *mib, int *util)
{
    char g[16];
    snprintf(g, sizeof g, "%d", gpu);
    double t0 = cli_now();
    for (;;) {
        const char *av[] = {"nvidia-smi", "-i", g, "--query-gpu=memory.used,utilization.gpu", "--format=csv,noheader,nounits",
                            NULL};
        struct sb o = {0};
        struct cli_spawn_opts so = {.argv = av, .capture = &o, .quiet = 1, .null_stderr = 1, .timeout_s = 30};
        struct cli_spawn_res r;
        int m = -1, u = -1;
        if (cli_spawn(&so, &r) || r.rc || sscanf(sb_str(&o), " %d , %d", &m, &u) != 2) {
            fprintf(stderr, "nether: nvidia-smi could not read GPU %d (rc %d): %s\n", gpu, r.rc, sb_str(&o));
            sb_free(&o);
            return -1;
        }
        sb_free(&o);
        if (mib) *mib = m;
        if (util) *util = u;
        if (m <= max_mib && u <= max_util) return 0;
        double left = wait_s - (cli_now() - t0);
        if (wait_s <= 0 || left <= 0 || cli_interrupted) {
            fprintf(stderr, "nether: GPU %d is busy: %d MiB used (at most %d), %d%% utilization (at most %d)\n", gpu, m,
                    max_mib, u, max_util);
            return 1;
        }
        printf("nether: GPU %d busy (%d MiB, %d%%); polling again in 5 s (%.0f s left of --gpu-wait)\n", gpu, m, u, left);
        fflush(stdout);
        struct timespec ts = {5, 0};
        nanosleep(&ts, NULL);
    }
}

/* ---- the phases --profile sums (cli.h): a child's wall as spawn_exec, the lock and the GPU guard (its nvidia-smi
 * included) as lock_guard */

int cli_spawn(const struct cli_spawn_opts *o, struct cli_spawn_res *r)
{
    double t0 = cli_now();
    int rc = spawn_inner(o, r);
    cli_prof_add(CLI_P_SPAWN, t0);
    return rc;
}

int cli_lock_acquire(const char *path, double timeout_s)
{
    double t0 = cli_now();
    int rc = lock_inner(path, timeout_s);
    cli_prof_add(CLI_P_LOCK, t0);
    return rc;
}

int cli_gpu_check_idle(int gpu, int max_mib, int max_util, double wait_s, int *mib, int *util)
{
    double t0 = cli_now();
    ++cli_prof_nest; /* nvidia-smi's spawn counts as the guard's */
    int rc = gpu_inner(gpu, max_mib, max_util, wait_s, mib, util);
    --cli_prof_nest;
    cli_prof_add(CLI_P_LOCK, t0);
    return rc;
}
