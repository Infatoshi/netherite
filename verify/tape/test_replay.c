#define _XOPEN_SOURCE 700
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* This executable doubles as a deterministic --magma child. Every assertion
 * invokes the real replay CLI, including fork/exec, generated config, parsing,
 * row comparison and process exit status. No game assets or GPU are needed. */
static void die(const char *what)
{
    perror(what);
    exit(2);
}

static void path_join(char *out, size_t cap, const char *dir, const char *name)
{
    if (snprintf(out, cap, "%s/%s", dir, name) >= (int)cap) {
        fprintf(stderr, "test path too long\n");
        exit(2);
    }
}

static int fake_magma(const char *conf)
{
    char line[8192], state[4096] = "", mode[128] = "";
    FILE *f = fopen(conf, "r");
    int ticks = 0;
    if (!f) die("read config");
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "state_out = ", 12)) {
            size_t n = strcspn(line + 12, "\r\n");
            if (n >= sizeof state) return 93;
            memcpy(state, line + 12, n);
            state[n] = 0;
        }
        if (!strncmp(line, "ticks = ", 8)) ticks = atoi(line + 8);
    }
    fclose(f);
    if (!*state || ticks != 3) return 90;
    {
        char *end = strrchr(state, '/'), *start;
        if (!end) return 91;
        *end = 0;
        start = strrchr(state, '/');
        if (!start) return 92;
        snprintf(mode, sizeof mode, "%s", start + 1);
        *end = '/';
    }
    f = fopen(state, "w");
    if (!f) die("write state");
    if (!strcmp(mode, "empty")) ticks = 0;
    if (!strcmp(mode, "short") || !strcmp(mode, "child-partial")) ticks--;
    if (!strcmp(mode, "extra")) ticks++;
    for (int i = 0; i < ticks; i++) {
        int tick = i + 1;
        double x = 10 + i;
        const char *hp = "20", *food = "20", *xs = NULL;
        if (!strcmp(mode, "tick-gap") && i == 1) tick = 4;
        if (!strcmp(mode, "tick-duplicate") && i == 1) tick = 0;
        if (!strcmp(mode, "tick-reorder")) tick = 2 - i;
        if (!strcmp(mode, "tick-zero-based")) tick = i;
        if (!strcmp(mode, "mismatch") && i == 1) x += 1;
        if (!strcmp(mode, "within-tolerance")) x += 1e-10;
        if (!strcmp(mode, "hp-lag")) hp = i == 2 ? "18" : "20";
        if (!strcmp(mode, "food-lag")) food = i == 2 ? "19" : "20";
        if (!strcmp(mode, "hp-mismatch")) hp = "17";
        if (!strcmp(mode, "nan")) xs = "NaN";
        if (!strcmp(mode, "infinite")) xs = "1e999";
        if (!strcmp(mode, "invalid-number")) xs = "10oops";
        if (!strcmp(mode, "fractional-int")) food = "20.5";
        if (!strcmp(mode, "overflow-int")) food = "4294967316";
        if (!strcmp(mode, "nan-lag")) hp = i == 1 ? "NaN" : "20";
        if (!strcmp(mode, "malformed") && i == 1) {
            fputs("this is not JSON\n", f);
            continue;
        }
        fprintf(f, "{\"tick\":%d,", tick);
        if (!strcmp(mode, "nested-field"))
            fprintf(f, "\"nested\":{\"x\":%.17g},", x);
        else if (xs) fprintf(f, "\"x\":%s,", xs);
        else fprintf(f, "\"x\":%.17g,", x);
        if (!strcmp(mode, "duplicate-field")) fprintf(f, "\"x\":%.17g,", x);
        fprintf(f, "\"y\":64,\"z\":0,");
        if (strcmp(mode, "missing-field")) fprintf(f, "\"vx\":0,");
        fprintf(f, "\"vy\":0,\"vz\":0,\"health\":%s,"
                   "\"on_ground\":1,\"food\":%s,\"dim\":0%s\n",
                hp, food, !strcmp(mode, "unclosed") ? "" : "}");
    }
    if (fclose(f)) die("close state");
    if (!strcmp(mode, "child-signal")) raise(SIGTERM);
    return !strcmp(mode, "child-fail") || !strcmp(mode, "child-partial") ? 7 : 0;
}

static void write_tape(const char *path, const char *mode)
{
    FILE *f = fopen(path, "w");
    if (!f) die("write tape");
    fputs("{\"header\":1,\"seed\":0,\"x\":10,\"y\":64,\"z\":0,"
          "\"yaw\":0,\"pitch\":0,\"hp\":20,\"food\":20,\"og\":1,\"dim\":0}\n", f);
    for (int i = 0; i < 3; i++) {
        int tick = !strcmp(mode, "tape-tick-gap") && i == 1 ? 4 : i;
        int hp = !strcmp(mode, "hp-lag") && i > 0 ? 18 : 20;
        int food = !strcmp(mode, "food-lag") && i > 0 ? 19 : 20;
        fputc('{', f);
        if (strcmp(mode, "tape-missing-tick")) fprintf(f, "\"t\":%d,", tick);
        if (!strcmp(mode, "tape-duplicate-tick")) fprintf(f, "\"t\":%d,", tick);
        fprintf(f, "\"x\":%d,\"y\":64,\"z\":0,\"yaw\":0,\"pitch\":0,", 10 + i);
        if (strcmp(mode, "tape-missing-field")) fputs("\"vx\":0,", f);
        fprintf(f, "\"vy\":%s,\"vz\":0,\"hp\":%d,\"food\":%d,\"og\":1,\"dim\":0}\n",
                !strcmp(mode, "tape-nonfinite") ? "1e999" : "0", hp, food);
    }
    if (fclose(f)) die("close tape");
}

int main(int argc, char **argv)
{
    static const struct { const char *name; int ok; const char *diagnostic; } cases[] = {
        {"valid", 1, "first_div none"},
        {"within-tolerance", 1, "first_div none"},
        {"hp-lag", 1, "first_div none"},
        {"food-lag", 1, "first_div none"},
        {"child-fail", 0, "magma_rc 7"},
        {"child-partial", 0, "magma_rc 7"},
        {"child-signal", 0, "magma failed with status"},
        {"short", 0, "state row count mismatch"},
        {"empty", 0, "magma produced no state rows"},
        {"extra", 0, "state row count mismatch"},
        {"tick-gap", 0, "field tick"},
        {"tick-duplicate", 0, "field tick"},
        {"tick-reorder", 0, "field tick"},
        {"tick-zero-based", 0, "field tick"},
        {"mismatch", 0, "field x"},
        {"hp-mismatch", 0, "field hp"},
        {"missing-field", 0, "bad state row"},
        {"nested-field", 0, "bad state row"},
        {"duplicate-field", 0, "bad state row"},
        {"nan", 0, "bad state row"},
        {"infinite", 0, "bad state row"},
        {"nan-lag", 0, "bad state row"},
        {"invalid-number", 0, "bad state row"},
        {"fractional-int", 0, "bad state row"},
        {"overflow-int", 0, "bad state row"},
        {"malformed", 0, "bad state row"},
        {"unclosed", 0, "bad state row"},
        {"tape-tick-gap", 0, "bad tick row"},
        {"tape-missing-tick", 0, "bad tick row"},
        {"tape-duplicate-tick", 0, "bad tick row"},
        {"tape-missing-field", 0, "bad tick row"},
        {"tape-nonfinite", 0, "bad tick row"},
    };
    char tmp[] = "/tmp/netherite-replay-XXXXXX";
    char self[PATH_MAX], replay[PATH_MAX];
    int failures = 0;
    if (argc == 3 && !strcmp(argv[1], "--conf")) return fake_magma(argv[2]);
    if (argc != 2) {
        fprintf(stderr, "usage: %s REPLAY_BINARY\n", argv[0]);
        return 2;
    }
    if (!realpath(argv[0], self) || !realpath(argv[1], replay)) die("realpath");
    if (!mkdtemp(tmp)) die("mkdtemp");
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        char dir[4096], tape[4096], log[4096], buf[16384];
        FILE *f;
        int status;
        pid_t pid;
        path_join(dir, sizeof dir, tmp, cases[i].name);
        if (mkdir(dir, 0700)) die("mkdir");
        path_join(tape, sizeof tape, dir, "input.jsonl");
        path_join(log, sizeof log, dir, "output.log");
        write_tape(tape, cases[i].name);
        fflush(NULL);
        pid = fork();
        if (pid < 0) die("fork");
        if (!pid) {
            if (!freopen(log, "w", stdout) || dup2(STDOUT_FILENO, STDERR_FILENO) < 0) _exit(126);
            execl(replay, replay, "--tape", tape, "--ticks", "3", "--out", dir,
                  "--magma", self, (char *)NULL);
            _exit(127);
        }
        while (waitpid(pid, &status, 0) < 0) if (errno != EINTR) die("waitpid");
        f = fopen(log, "r");
        if (!f) die("read log");
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        buf[n] = 0;
        fclose(f);
        int ok = WIFEXITED(status) && (WEXITSTATUS(status) == 0) == cases[i].ok &&
                 strstr(buf, cases[i].diagnostic);
        printf("replay %s: %s\n", cases[i].name, ok ? "PASS" : "FAIL");
        if (!ok) {
            fprintf(stderr, "status=%d expected_success=%d diagnostic=%s\n%s",
                    status, cases[i].ok, cases[i].diagnostic, buf);
            failures++;
        }
        const char *files[] = {"input.jsonl", "output.log", "magma_script.jsonl",
                               "magma_state.jsonl", "magma_run.conf"};
        for (size_t j = 0; j < sizeof files / sizeof files[0]; j++) {
            char path[4096];
            path_join(path, sizeof path, dir, files[j]);
            if (unlink(path) && errno != ENOENT) die("unlink");
        }
        if (rmdir(dir)) die("rmdir case");
    }
    if (rmdir(tmp)) die("rmdir temp");
    printf("replay contract: %zu cases, %d failures\n", sizeof cases / sizeof cases[0], failures);
    return failures ? 1 : 0;
}
