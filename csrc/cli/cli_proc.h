/* nether CLI: processes, locks, the GPU guard and the hardware counters (csrc/cli/cli_proc.c).
 *
 * Every child is started by posix_spawn with an argv (never system() or sh -c), waited on by wait4 (its rusage),
 * optionally in its own process group with a timeout (SIGTERM to the group, SIGKILL after a grace), its output to a
 * file, to the terminal, into memory, or any of them at once. */
#ifndef NETH_CLI_PROC_H
#define NETH_CLI_PROC_H

#include "cli.h"

struct cli_spawn_opts {
    const char *const *argv;      /* argv[0] is looked up on PATH unless it holds a '/' */
    const char *cwd;              /* NULL: the current directory */
    const char *const *env_set;   /* "NAME=VALUE" each, over the inherited environment (NULL-terminated) */
    const char *const *env_unset; /* names dropped from it (NULL-terminated) */
    const char *out_path;         /* stdout and stderr into this file (append: add to it) */
    const char *err_path;         /* stderr into this file instead (out_path then holds stdout alone) */
    int append;
    int tee;                      /* the output also to our stdout (with out_path or capture) */
    int quiet;                    /* no "nether: $ CMD" line */
    int null_stderr;              /* stderr to /dev/null (not captured) */
    int stdin_null;               /* stdin from /dev/null */
    struct sb *capture;           /* the output kept in memory too */
    double timeout_s;             /* 0: none */
    double grace_s;               /* SIGTERM to SIGKILL (default 10 s) */
    int new_pgroup;               /* its own process group (any timeout implies it); our SIGINT/SIGTERM go to it */
    int perf;                     /* user cycles and instructions (perf_event_open), rusage always */
};

struct cli_spawn_res {
    int rc;                       /* the exit status, 128 + N for signal N, 124 on the timeout, 127 not started */
    int timed_out, signaled;
    double wall, utime, stime;    /* seconds */
    long maxrss_kb, minflt, majflt;
    int perf_ok;                  /* the counters below were read (else perf_why says why not) */
    unsigned long long cycles, instructions;
    double ipc;
    char perf_why[96];
};

/* 0 when the child ran (its result in *r, whatever its exit status), -1 when it could not be started (r->rc 127) */
int cli_spawn(const struct cli_spawn_opts *o, struct cli_spawn_res *r);
/* argv run once with the counters on (user-mode cycles, instructions and IPC from perf_event_open, falling back to
 * wait4's rusage alone when kernel.perf_event_paranoid forbids them): cli_spawn with perf set */
int cli_perf_stat(const char *const *argv, const char *cwd, struct cli_spawn_res *r);
/* the command as one printable line (quoted where a word needs it) */
void cli_argv_line(const char *const *argv, struct sb *b);
/* a program on PATH: 1 or 0 */
int cli_have(const char *prog);

/* a SIGINT/SIGTERM/SIGHUP received while a child ran (the child's group was sent it too) */
extern volatile int cli_interrupted;

/* an exclusive flock on path (created): the fd (>= 0), -1 on an error; prints that it waits when another holds it;
 * timeout_s <= 0 waits forever, else -2 after it */
int cli_lock_acquire(const char *path, double timeout_s);
void cli_lock_release(int fd);

/* nvidia-smi on gpu: 0 when memory.used <= max_mib and utilization.gpu <= max_util (*mib and *util as read), 1 busy,
 * -1 when it cannot be read. wait_s > 0 polls every 5 s up to it while busy. */
int cli_gpu_check_idle(int gpu, int max_mib, int max_util, double wait_s, int *mib, int *util);

#endif
