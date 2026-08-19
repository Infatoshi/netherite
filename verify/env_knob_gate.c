/*
 * env_knob_gate - reject project-prefixed runtime env-var knob reads.
 *
 * Law (AGENTS.md "Runtime knobs"): behavior toggles live in config registries,
 * init params, argparse, or java launch JSON/YAML. A getenv / os.environ read
 * of a project-prefixed key is a regression. Build-time make variables and
 * machine-environment pointers are exempt (they do not match the prefix set,
 * or live only under java/render-opt which is not scanned).
 *
 * Scans production trees: magma blaze verify java scripts.
 * Usage:
 *   env_knob_gate [ROOT]   scan ROOT (default: .)
 *   env_knob_gate --selftest
 */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Forbidden key prefixes (same set as the retired shell gate). */
static const char *const FORBIDDEN_PFX[] = {
    "MAGMA_",     "BLAZE_",      "NATIVE_",  "CGRAPH_",   "TRAIN_SEEDS",
    "REWARD_JSON","ZOOM_",       "MCW_",     "QRL_",      "EP_TICKS",
    "ENVMATCH",   "CHAIN_NET",   "SCENARIO_",
};
static const size_t FORBIDDEN_PFX_N =
    sizeof FORBIDDEN_PFX / sizeof FORBIDDEN_PFX[0];

/* Top-level production roots (relative to scan root). */
static const char *const SCAN_ROOTS[] = {
    "magma", "blaze", "verify", "java", "scripts",
};
static const size_t SCAN_ROOTS_N =
    sizeof SCAN_ROOTS / sizeof SCAN_ROOTS[0];

enum file_kind { KIND_NONE = 0, KIND_C, KIND_PY };

struct scan_result {
    int hits;
    int files;
};

/* ---- small helpers ------------------------------------------------------- */

static int starts_with(const char *s, const char *pfx) {
    size_t n = strlen(pfx);
    return strncmp(s, pfx, n) == 0;
}

static int is_dot_or_dotdot(const char *name) {
    return name[0] == '.' &&
           (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

static int is_skip_dir(const char *name) {
    if (name[0] == '.') return 1; /* .git, .cache, ... */
    if (strcmp(name, "out") == 0) return 1;
    if (strcmp(name, "docs") == 0) return 1;
    if (strcmp(name, "__pycache__") == 0) return 1;
    return 0;
}

/* Closed lab: java/render-opt only (QRL_SM machine pointer lives there). */
static int is_java_render_opt(const char *dir, const char *name) {
    const char *base;
    if (strcmp(name, "render-opt") != 0) return 0;
    base = strrchr(dir, '/');
    base = base ? base + 1 : dir;
    return strcmp(base, "java") == 0;
}

static enum file_kind kind_from_name(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name) return KIND_NONE;
    if (strcmp(dot, ".c") == 0 || strcmp(dot, ".h") == 0 ||
        strcmp(dot, ".cc") == 0 || strcmp(dot, ".cpp") == 0 ||
        strcmp(dot, ".cu") == 0 || strcmp(dot, ".cuh") == 0 ||
        strcmp(dot, ".m") == 0)
        return KIND_C;
    if (strcmp(dot, ".py") == 0) return KIND_PY;
    return KIND_NONE;
}

static int key_forbidden(const char *key) {
    size_t i;
    for (i = 0; i < FORBIDDEN_PFX_N; i++) {
        if (starts_with(key, FORBIDDEN_PFX[i])) return 1;
    }
    return 0;
}

/* Skip ASCII whitespace. */
static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\f' || *p == '\v')
        p++;
    return p;
}

/*
 * After a reader call opener, parse a string literal key and report if it is
 * a forbidden project prefix. quote is ' or ".
 * Returns 1 if forbidden, 0 if ok / not a string.
 */
static int key_at_quote_forbidden(const char *p) {
    char quote;
    char key[256];
    size_t n = 0;

    p = skip_ws(p);
    if (*p != '"' && *p != '\'') return 0;
    quote = *p++;
    while (*p && *p != quote && *p != '\n' && n + 1 < sizeof key) {
        if (*p == '\\' && p[1]) { /* keep escapes simple: store next byte */
            p++;
        }
        key[n++] = *p++;
    }
    key[n] = '\0';
    if (n == 0) return 0;
    return key_forbidden(key);
}

/* Match getenv( "KEY"  in C or Python. */
static int match_getenv(const char *line) {
    const char *p = line;
    for (;;) {
        const char *g = strstr(p, "getenv");
        if (!g) return 0;
        /* word boundary: not part of a larger identifier */
        if (g > line) {
            unsigned char c = (unsigned char)g[-1];
            if (isalnum(c) || c == '_') {
                p = g + 6;
                continue;
            }
        }
        p = g + 6;
        p = skip_ws(p);
        if (*p != '(') continue;
        p++;
        if (key_at_quote_forbidden(p)) return 1;
    }
}

/*
 * Match Python environ["KEY"] / environ('KEY') / environ.get("KEY") forms.
 * Mirrors the retired gate: (environ(\.get)?\s*[\[\(]|getenv\()\s*['\"]PREFIX
 * (getenv handled above).
 */
static int match_environ(const char *line) {
    const char *p = line;
    for (;;) {
        const char *e = strstr(p, "environ");
        if (!e) return 0;
        if (e > line) {
            unsigned char c = (unsigned char)e[-1];
            if (isalnum(c) || c == '_') {
                p = e + 7;
                continue;
            }
        }
        p = e + 7;
        /* optional .get */
        if (p[0] == '.' && p[1] == 'g' && p[2] == 'e' && p[3] == 't') {
            const char *q = p + 4;
            /* must be end of identifier: .get not .getattr */
            if (isalnum((unsigned char)*q) || *q == '_') {
                continue; /* p still at ".get..." - advance past "environ" only */
            }
            p = q;
        }
        p = skip_ws(p);
        if (*p != '[' && *p != '(') continue;
        p++;
        if (key_at_quote_forbidden(p)) return 1;
    }
}

static int line_hits(enum file_kind kind, const char *line) {
    if (kind == KIND_C) return match_getenv(line);
    if (kind == KIND_PY) return match_getenv(line) || match_environ(line);
    return 0;
}

/* ---- scan one file / tree ------------------------------------------------ */

static int scan_file(const char *path, enum file_kind kind,
                     struct scan_result *res) {
    FILE *fp;
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    int lineno = 0;
    int local = 0;

    fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "env_knob_gate: open %s: %s\n", path, strerror(errno));
        return -1;
    }
    res->files++;
    while ((n = getline(&line, &cap, fp)) != -1) {
        lineno++;
        if (line_hits(kind, line)) {
            /* trim trailing newline for display */
            if (n > 0 && line[n - 1] == '\n') line[n - 1] = '\0';
            fprintf(stderr, "%s:%d:%s\n", path, lineno, line);
            local++;
            res->hits++;
        }
    }
    free(line);
    fclose(fp);
    return local;
}

static int scan_dir(const char *path, struct scan_result *res);

static int scan_entry(const char *dir, const char *name,
                      struct scan_result *res) {
    char path[4096];
    struct stat st;
    enum file_kind kind;
    int n;

    if (is_dot_or_dotdot(name)) return 0;
    n = snprintf(path, sizeof path, "%s/%s", dir, name);
    if (n < 0 || (size_t)n >= sizeof path) {
        fprintf(stderr, "env_knob_gate: path too long under %s\n", dir);
        return -1;
    }
    if (lstat(path, &st) != 0) {
        /* race / dangling: ignore */
        return 0;
    }
    if (S_ISLNK(st.st_mode)) {
        /* do not follow symlinks (tapes/, ref trees, etc.) */
        return 0;
    }
    if (S_ISDIR(st.st_mode)) {
        if (is_skip_dir(name)) return 0;
        if (is_java_render_opt(dir, name)) return 0;
        return scan_dir(path, res);
    }
    if (!S_ISREG(st.st_mode)) return 0;
    kind = kind_from_name(name);
    if (kind == KIND_NONE) return 0;
    return scan_file(path, kind, res) < 0 ? -1 : 0;
}

static int scan_dir(const char *path, struct scan_result *res) {
    DIR *d;
    struct dirent *ent;

    d = opendir(path);
    if (!d) {
        fprintf(stderr, "env_knob_gate: opendir %s: %s\n", path,
                strerror(errno));
        return -1;
    }
    while ((ent = readdir(d)) != NULL) {
        if (scan_entry(path, ent->d_name, res) != 0) {
            closedir(d);
            return -1;
        }
    }
    closedir(d);
    return 0;
}

static int scan_repo(const char *root, struct scan_result *res) {
    size_t i;
    memset(res, 0, sizeof *res);
    for (i = 0; i < SCAN_ROOTS_N; i++) {
        char path[4096];
        struct stat st;
        int n = snprintf(path, sizeof path, "%s/%s", root, SCAN_ROOTS[i]);
        if (n < 0 || (size_t)n >= sizeof path) return -1;
        if (stat(path, &st) != 0) continue; /* root optional */
        if (!S_ISDIR(st.st_mode)) continue;
        if (scan_dir(path, res) != 0) return -1;
    }
    return 0;
}

/* ---- filesystem helpers for self-test ------------------------------------ */

static int mk_dir(const char *path) {
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "env_knob_gate: mkdir %s: %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

static int mk_dirs_under(const char *base, const char *rel) {
    /* create base/rel with intermediate components */
    char path[4096];
    char tmp[4096];
    char *p;
    int n = snprintf(path, sizeof path, "%s/%s", base, rel);
    if (n < 0 || (size_t)n >= sizeof path) return -1;
    snprintf(tmp, sizeof tmp, "%s", path);
    /* skip leading base length for progressive mkdir - walk full path */
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mk_dir(tmp) != 0) return -1;
            *p = '/';
        }
    }
    return mk_dir(tmp);
}

static int write_text(const char *path, const char *body) {
    FILE *fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "env_knob_gate: write %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (fputs(body, fp) < 0) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

static int write_under(const char *base, const char *rel, const char *body) {
    char path[4096];
    char dir[4096];
    char *slash;
    int n = snprintf(path, sizeof path, "%s/%s", base, rel);
    if (n < 0 || (size_t)n >= sizeof path) return -1;
    snprintf(dir, sizeof dir, "%s", path);
    slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        if (strlen(dir) > strlen(base)) {
            const char *rel_dir = dir + strlen(base) + 1;
            if (rel_dir[0] && mk_dirs_under(base, rel_dir) != 0) return -1;
        }
    }
    return write_text(path, body);
}

/* rm -rf for the self-test temp tree only (depth-first). */
static int rm_rf(const char *path) {
    struct stat st;
    DIR *d;
    struct dirent *ent;

    if (lstat(path, &st) != 0) return 0;
    if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
        d = opendir(path);
        if (!d) return -1;
        while ((ent = readdir(d)) != NULL) {
            char child[4096];
            if (is_dot_or_dotdot(ent->d_name)) continue;
            snprintf(child, sizeof child, "%s/%s", path, ent->d_name);
            if (rm_rf(child) != 0) {
                closedir(d);
                return -1;
            }
        }
        closedir(d);
        if (rmdir(path) != 0) return -1;
        return 0;
    }
    if (unlink(path) != 0) return -1;
    return 0;
}

/* ---- self-test ----------------------------------------------------------- */

/*
 * Build fixture bodies without embedding the contiguous forbidden reader
 * patterns in this translation unit (this file lives under verify/ and is
 * scanned by the gate).
 */
static int selftest(void) {
    char tmpl[] = "/tmp/env_knob_gate.XXXXXX";
    char *root;
    struct scan_result res;
    char body[512];
    int failed = 0;
    const char *pfx_magma;
    const char *pfx_qrl;
    const char *reader_getenv;
    const char *reader_environ;

    /* split so this source is not itself a hit */
    pfx_magma = "MAGMA_";
    pfx_qrl = "QRL_";
    reader_getenv = "getenv";
    reader_environ = "environ";

    root = mkdtemp(tmpl);
    if (!root) {
        fprintf(stderr, "env_knob_gate: mkdtemp: %s\n", strerror(errno));
        return 1;
    }

    /* --- accepted production-like file --- */
    if (write_under(root, "magma/ok.c",
                    "int main(void){ return 0; }\n") != 0)
        goto boom;

    /* --- rejected C getenv reader --- */
    snprintf(body, sizeof body,
             "void bad(void){ (void)%s(\"%sKNOB\"); }\n", reader_getenv,
             pfx_magma);
    if (write_under(root, "magma/bad_getenv.c", body) != 0) goto boom;

    /* --- rejected Python os.environ reader --- */
    snprintf(body, sizeof body,
             "import os\nx = os.%s.get(\"%sKNOB\")\n", reader_environ,
             pfx_magma);
    if (write_under(root, "magma/bad_environ.py", body) != 0) goto boom;

    /* --- exemptions: system env (not project-prefixed) --- */
    snprintf(body, sizeof body,
             "void sysenv(void){\n"
             "  (void)%s(\"DISPLAY\");\n"
             "  (void)%s(\"JAVA_HOME\");\n"
             "  (void)%s(\"CUDA_VISIBLE_DEVICES\");\n"
             "  (void)%s(\"TMPDIR\");\n"
             "  (void)%s(\"UV_CACHE_DIR\");\n"
             "}\n",
             reader_getenv, reader_getenv, reader_getenv, reader_getenv,
             reader_getenv);
    if (write_under(root, "magma/exempt_system.c", body) != 0) goto boom;

    /* --- exemptions: machine-environment pointers (MC_* not in prefix set) --- */
    snprintf(body, sizeof body,
             "import os\n"
             "a = os.%s.get(\"MC_JAR\")\n"
             "b = os.%s.get(\"MC_SM\")\n"
             "c = os.%s.get(\"MC_JAVA_DIR\")\n"
             "d = os.%s.get(\"MC_ASSET_INDEX\")\n",
             reader_environ, reader_environ, reader_environ, reader_environ);
    if (write_under(root, "blaze/exempt_machine.py", body) != 0) goto boom;

    /* --- exemption: QRL_SM only under java/render-opt (closed lab, skipped) --- */
    snprintf(body, sizeof body,
             "import os\narch = os.%s.get(\"%sSM\", \"sm_120\")\n",
             reader_environ, pfx_qrl);
    if (write_under(root, "java/render-opt/lab.py", body) != 0) goto boom;

    /* --- exemption: build-time make vars are not source getenv reads --- */
    if (write_under(root, "magma/Makefile",
                    "MAGMA_AUDIO_OPENAL=1\nBLAZE_SM=sm_120\n") != 0)
        goto boom;

    /* Also place a clean scripts tree so all five roots exist. */
    if (write_under(root, "scripts/noop.py", "pass\n") != 0) goto boom;
    if (write_under(root, "verify/noop.c", "void v(void){}\n") != 0) goto boom;
    if (write_under(root, "java/ok.py", "pass\n") != 0) goto boom;

    if (scan_repo(root, &res) != 0) goto boom;

    /* Expect exactly two hits: bad_getenv.c and bad_environ.py */
    if (res.hits != 2) {
        fprintf(stderr,
                "env_knob_gate selftest: expected 2 hits, got %d (files=%d)\n",
                res.hits, res.files);
        failed = 1;
    }

    /* Second pass checks: accepted + each exemption alone must be clean.
     * Rebuild a tree with only the good files. */
    {
        char tmpl2[] = "/tmp/env_knob_gate.XXXXXX";
        char *root2 = mkdtemp(tmpl2);
        struct scan_result res2;
        if (!root2) {
            fprintf(stderr, "env_knob_gate: mkdtemp2: %s\n", strerror(errno));
            failed = 1;
            goto done;
        }
        /* accepted */
        write_under(root2, "magma/ok.c", "int main(void){ return 0; }\n");
        /* system exemption */
        snprintf(body, sizeof body, "void f(void){ (void)%s(\"DISPLAY\"); }\n",
                 reader_getenv);
        write_under(root2, "magma/exempt_system.c", body);
        /* machine exemption */
        snprintf(body, sizeof body, "import os\nos.%s.get(\"MC_JAR\")\n",
                 reader_environ);
        write_under(root2, "blaze/exempt_machine.py", body);
        /* render-opt exemption */
        snprintf(body, sizeof body,
                 "import os\nos.%s.get(\"%sSM\")\n", reader_environ, pfx_qrl);
        write_under(root2, "java/render-opt/lab.py", body);
        /* build-time makefile exemption */
        write_under(root2, "magma/Makefile", "MAGMA_AUDIO_OPENAL=1\n");
        write_under(root2, "scripts/noop.py", "pass\n");
        write_under(root2, "verify/noop.c", "void v(void){}\n");
        write_under(root2, "java/ok.py", "pass\n");

        if (scan_repo(root2, &res2) != 0) {
            failed = 1;
        } else if (res2.hits != 0) {
            fprintf(stderr,
                    "env_knob_gate selftest: exemptions should pass, got %d "
                    "hits\n",
                    res2.hits);
            failed = 1;
        }
        rm_rf(root2);
    }

    /* Prove each reject individually. */
    {
        char tmpl3[] = "/tmp/env_knob_gate.XXXXXX";
        char *root3 = mkdtemp(tmpl3);
        struct scan_result res3;
        if (!root3) {
            failed = 1;
            goto done;
        }
        snprintf(body, sizeof body, "void bad(void){ (void)%s(\"%sK\"); }\n",
                 reader_getenv, pfx_magma);
        write_under(root3, "magma/only_bad.c", body);
        write_under(root3, "blaze/x.py", "pass\n");
        write_under(root3, "verify/x.c", "void v(void){}\n");
        write_under(root3, "java/x.py", "pass\n");
        write_under(root3, "scripts/x.py", "pass\n");
        scan_repo(root3, &res3);
        if (res3.hits != 1) {
            fprintf(stderr,
                    "env_knob_gate selftest: C reject expected 1 hit, got %d\n",
                    res3.hits);
            failed = 1;
        }
        rm_rf(root3);
    }
    {
        char tmpl4[] = "/tmp/env_knob_gate.XXXXXX";
        char *root4 = mkdtemp(tmpl4);
        struct scan_result res4;
        if (!root4) {
            failed = 1;
            goto done;
        }
        snprintf(body, sizeof body, "import os\nos.%s[\"%sK\"]\n",
                 reader_environ, pfx_magma);
        write_under(root4, "magma/only_bad.py", body);
        write_under(root4, "blaze/x.py", "pass\n");
        write_under(root4, "verify/x.c", "void v(void){}\n");
        write_under(root4, "java/x.py", "pass\n");
        write_under(root4, "scripts/x.py", "pass\n");
        scan_repo(root4, &res4);
        if (res4.hits != 1) {
            fprintf(stderr,
                    "env_knob_gate selftest: py reject expected 1 hit, got %d\n",
                    res4.hits);
            failed = 1;
        }
        rm_rf(root4);
    }

done:
    rm_rf(root);
    if (failed) {
        fprintf(stderr, "env_knob_gate: selftest FAIL\n");
        return 1;
    }
    printf("env_knob_gate: selftest PASS\n");
    return 0;

boom:
    rm_rf(root);
    fprintf(stderr, "env_knob_gate: selftest setup failed\n");
    return 1;
}

/* ---- main ---------------------------------------------------------------- */

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s [ROOT]\n"
            "       %s --selftest\n",
            argv0, argv0);
}

int main(int argc, char **argv) {
    const char *root = ".";
    struct scan_result res;

    if (argc >= 2) {
        if (strcmp(argv[1], "--selftest") == 0) {
            if (argc != 2) {
                usage(argv[0]);
                return 2;
            }
            return selftest();
        }
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
        root = argv[1];
    }
    if (argc > 2) {
        usage(argv[0]);
        return 2;
    }

    if (scan_repo(root, &res) != 0) return 2;
    if (res.hits != 0) {
        fprintf(stderr,
                "env_knob_gate: FAIL - runtime env-var knob read found.\n");
        fprintf(stderr,
                "Move it to the config registry / init params / argparse "
                "(AGENTS.md: Runtime knobs).\n");
        return 1;
    }
    printf("env_knob_gate: PASS\n");
    return 0;
}
