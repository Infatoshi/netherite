/*
 * public_export - export a clean-history public tree of a netherite repo.
 *
 * Copies tracked worktree files to DEST, excluding Mojang-derived content
 * (oracle-src, texture-derived generated headers, media). Initializes a
 * fresh git repository at DEST (no source history). Does not stage, commit,
 * or push.
 *
 * Usage:
 *   public_export --repo ROOT --dest DEST
 *   public_export --selftest
 */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#elif defined(__linux__)
#define _DEFAULT_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Mojang-derived generated magma asset headers (exact relative paths). */
static const char *const EXCLUDED_ASSET_HEADERS[] = {
    "magma/assets/atlas_gen.h",
    "magma/assets/colormap_gen.h",
    "magma/assets/gui_atlas.h",
    "magma/assets/hand_atlas.h",
    "magma/assets/hud_atlas.h",
    "magma/assets/inventory_ui_atlas.h",
    "magma/assets/item_atlas.h",
    "magma/assets/loading_bg.h",
    "magma/assets/mob_atlas.h",
    "magma/assets/portal_tex.h",
    "magma/assets/sky_atlas.h",
    "magma/assets/underwater_tex.h",
    "magma/assets/water_frames.h",
};
static const size_t EXCLUDED_ASSET_HEADERS_N =
    sizeof EXCLUDED_ASSET_HEADERS / sizeof EXCLUDED_ASSET_HEADERS[0];

static const char *const EXCLUDED_MEDIA_EXT[] = {
    ".png", ".ppm", ".jpg", ".jpeg", ".gif", ".mp4",
};
static const size_t EXCLUDED_MEDIA_EXT_N =
    sizeof EXCLUDED_MEDIA_EXT / sizeof EXCLUDED_MEDIA_EXT[0];

/* Appended to exported .gitignore (same block as the retired shell export). */
static const char GITIGNORE_APPEND[] =
    "\n"
    "# ---- regenerated from YOUR Minecraft install (never committed) ----\n"
    "# make -C java bootstrap-oracle\n"
    "java/oracle-src/\n"
    "# make assets\n"
    "magma/assets/atlas_gen.h\n"
    "magma/assets/colormap_gen.h\n"
    "magma/assets/gui_atlas.h\n"
    "magma/assets/hand_atlas.h\n"
    "magma/assets/inventory_ui_atlas.h\n"
    "magma/assets/hud_atlas.h\n"
    "magma/assets/item_atlas.h\n"
    "magma/assets/loading_bg.h\n"
    "magma/assets/mob_atlas.h\n"
    "magma/assets/portal_tex.h\n"
    "magma/assets/sky_atlas.h\n"
    "magma/assets/underwater_tex.h\n"
    "magma/assets/water_frames.h\n";

/* ---- small helpers ------------------------------------------------------- */

static int starts_with(const char *s, const char *pfx) {
    size_t n = strlen(pfx);
    return strncmp(s, pfx, n) == 0;
}

static int is_dot_or_dotdot(const char *name) {
    return name[0] == '.' &&
           (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

static int str_eq_ci(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int path_is_absolute(const char *p) { return p && p[0] == '/'; }

/* true if path equals outer, or is outer/something */
static int path_is_within(const char *path, const char *outer) {
    size_t n;
    if (!path || !outer) return 0;
    n = strlen(outer);
    if (n == 0) return 0;
    if (strcmp(path, outer) == 0) return 1;
    if (strncmp(path, outer, n) == 0 && path[n] == '/') return 1;
    return 0;
}

static int path_has_dotdot(const char *rel) {
    const char *p = rel;
    while (*p) {
        if (p[0] == '.' && p[1] == '.' &&
            (p[2] == '/' || p[2] == '\0') &&
            (p == rel || p[-1] == '/'))
            return 1;
        p++;
    }
    return 0;
}

/* ---- exclusion predicates ------------------------------------------------ */

static int is_excluded_asset_header(const char *rel) {
    size_t i;
    for (i = 0; i < EXCLUDED_ASSET_HEADERS_N; i++) {
        if (strcmp(rel, EXCLUDED_ASSET_HEADERS[i]) == 0) return 1;
    }
    return 0;
}

static int is_excluded_media(const char *rel) {
    const char *dot = strrchr(rel, '.');
    size_t i;
    if (!dot || dot == rel) return 0;
    /* extension must be the final path component's extension */
    if (strchr(dot, '/')) return 0;
    for (i = 0; i < EXCLUDED_MEDIA_EXT_N; i++) {
        if (str_eq_ci(dot, EXCLUDED_MEDIA_EXT[i])) return 1;
    }
    return 0;
}

static int is_excluded_path(const char *rel) {
    if (starts_with(rel, "java/oracle-src/")) return 1;
    if (is_excluded_asset_header(rel)) return 1;
    if (is_excluded_media(rel)) return 1;
    return 0;
}

/*
 * Reject unsafe tracked relative paths. Returns 0 if ok, -1 if reject.
 * On ok, full_src is set to repo/rel. Symlink targets may point outside the
 * repo; only intermediate path resolution must stay under repo_real.
 */
static int validate_tracked_path(const char *repo, const char *repo_real,
                                 const char *rel, char *full_src,
                                 size_t full_src_sz) {
    char parent[PATH_MAX];
    char resolved_parent[PATH_MAX];
    char *slash;
    int n;

    if (!rel || rel[0] == '\0') {
        fprintf(stderr, "public_export: empty tracked path\n");
        return -1;
    }
    if (path_is_absolute(rel)) {
        fprintf(stderr, "public_export: absolute tracked path rejected: %s\n",
                rel);
        return -1;
    }
    if (path_has_dotdot(rel)) {
        fprintf(stderr, "public_export: '..' in tracked path rejected: %s\n",
                rel);
        return -1;
    }
    n = snprintf(full_src, full_src_sz, "%s/%s", repo, rel);
    if (n < 0 || (size_t)n >= full_src_sz) {
        fprintf(stderr, "public_export: path too long: %s\n", rel);
        return -1;
    }
    /* realpath the parent only so a final symlink is not followed. */
    n = snprintf(parent, sizeof parent, "%s", full_src);
    if (n < 0 || (size_t)n >= sizeof parent) return -1;
    slash = strrchr(parent, '/');
    if (!slash) {
        fprintf(stderr, "public_export: internal path error: %s\n", rel);
        return -1;
    }
    if (slash == parent) {
        /* parent is "/" - should not happen for repo/rel */
        if (strcmp(repo_real, "/") != 0) {
            fprintf(stderr,
                    "public_export: tracked path resolves outside repo: %s\n",
                    rel);
            return -1;
        }
        return 0;
    }
    *slash = '\0';
    if (!realpath(parent, resolved_parent)) {
        fprintf(stderr, "public_export: realpath %s: %s\n", parent,
                strerror(errno));
        return -1;
    }
    if (!path_is_within(resolved_parent, repo_real)) {
        fprintf(stderr,
                "public_export: tracked path resolves outside repo: %s\n", rel);
        return -1;
    }
    return 0;
}

/* ---- mkdir / copy -------------------------------------------------------- */

static int mk_dir(const char *path, mode_t mode) {
    if (mkdir(path, mode) != 0 && errno != EEXIST) {
        fprintf(stderr, "public_export: mkdir %s: %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

/* mkdir -p for an absolute or relative path (all components). */
static int mk_dirs_p(const char *path) {
    char tmp[PATH_MAX];
    char *p;
    size_t len;

    if (!path || path[0] == '\0') return -1;
    len = strlen(path);
    if (len + 1 > sizeof tmp) return -1;
    memcpy(tmp, path, len + 1);

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mk_dir(tmp, 0755) != 0) return -1;
            *p = '/';
        }
    }
    return mk_dir(tmp, 0755);
}

/* Ensure parent directory of path exists. */
static int mk_parent(const char *path) {
    char dir[PATH_MAX];
    char *slash;
    size_t len = strlen(path);
    if (len + 1 > sizeof dir) return -1;
    memcpy(dir, path, len + 1);
    slash = strrchr(dir, '/');
    if (!slash || slash == dir) return 0;
    *slash = '\0';
    return mk_dirs_p(dir);
}

static int copy_regular_file(const char *src, const char *dst, mode_t mode) {
    int in_fd = -1, out_fd = -1;
    char buf[64 * 1024];
    ssize_t n;
    int rc = -1;

    if (mk_parent(dst) != 0) return -1;

    in_fd = open(src, O_RDONLY);
    if (in_fd < 0) {
        fprintf(stderr, "public_export: open %s: %s\n", src, strerror(errno));
        return -1;
    }
    /* Never overwrite existing destination content. */
    out_fd = open(dst, O_WRONLY | O_CREAT | O_EXCL, mode & 0777);
    if (out_fd < 0) {
        fprintf(stderr, "public_export: create %s: %s\n", dst, strerror(errno));
        goto out;
    }
    for (;;) {
        n = read(in_fd, buf, sizeof buf);
        if (n < 0) {
            fprintf(stderr, "public_export: read %s: %s\n", src,
                    strerror(errno));
            goto out;
        }
        if (n == 0) break;
        {
            ssize_t off = 0;
            while (off < n) {
                ssize_t w = write(out_fd, buf + off, (size_t)(n - off));
                if (w < 0) {
                    fprintf(stderr, "public_export: write %s: %s\n", dst,
                            strerror(errno));
                    goto out;
                }
                off += w;
            }
        }
    }
    if (fchmod(out_fd, mode & 0777) != 0) {
        fprintf(stderr, "public_export: fchmod %s: %s\n", dst, strerror(errno));
        goto out;
    }
    rc = 0;
out:
    if (in_fd >= 0) close(in_fd);
    if (out_fd >= 0) {
        if (close(out_fd) != 0 && rc == 0) {
            fprintf(stderr, "public_export: close %s: %s\n", dst,
                    strerror(errno));
            rc = -1;
        }
        if (rc != 0) (void)unlink(dst);
    }
    return rc;
}

static int copy_symlink(const char *src, const char *dst) {
    char target[PATH_MAX];
    ssize_t n;

    if (mk_parent(dst) != 0) return -1;
    n = readlink(src, target, sizeof target - 1);
    if (n < 0) {
        fprintf(stderr, "public_export: readlink %s: %s\n", src,
                strerror(errno));
        return -1;
    }
    target[n] = '\0';
    if (symlink(target, dst) != 0) {
        fprintf(stderr, "public_export: symlink %s -> %s: %s\n", dst, target,
                strerror(errno));
        return -1;
    }
    return 0;
}

/* ---- git via fork/exec (no shell) ---------------------------------------- */

/*
 * Run git with argv-style args (argv[0] must be "git"). Capture stdout into
 * *out_buf (NUL-terminated, caller frees). Returns exit status, or -1 on
 * spawn/IO error.
 */
static int git_run_capture(char *const argv[], char **out_buf, size_t *out_len) {
    int pipefd[2];
    pid_t pid;
    char *buf = NULL;
    size_t cap = 0, len = 0;
    int status;
    ssize_t n;

    *out_buf = NULL;
    if (out_len) *out_len = 0;

    if (pipe(pipefd) != 0) {
        fprintf(stderr, "public_export: pipe: %s\n", strerror(errno));
        return -1;
    }
    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "public_export: fork: %s\n", strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) _exit(127);
        close(pipefd[1]);
        /* leave stderr shared for diagnostics */
        execvp("git", argv);
        _exit(127);
    }
    close(pipefd[1]);
    for (;;) {
        char chunk[8192];
        n = read(pipefd[0], chunk, sizeof chunk);
        if (n < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "public_export: read git stdout: %s\n",
                    strerror(errno));
            close(pipefd[0]);
            waitpid(pid, &status, 0);
            free(buf);
            return -1;
        }
        if (n == 0) break;
        if (len + (size_t)n + 1 > cap) {
            size_t ncap = cap ? cap * 2 : 8192;
            char *nb;
            while (ncap < len + (size_t)n + 1) ncap *= 2;
            nb = realloc(buf, ncap);
            if (!nb) {
                fprintf(stderr, "public_export: OOM capturing git output\n");
                close(pipefd[0]);
                waitpid(pid, &status, 0);
                free(buf);
                return -1;
            }
            buf = nb;
            cap = ncap;
        }
        memcpy(buf + len, chunk, (size_t)n);
        len += (size_t)n;
    }
    close(pipefd[0]);
    if (waitpid(pid, &status, 0) < 0) {
        free(buf);
        return -1;
    }
    if (!buf) {
        buf = malloc(1);
        if (!buf) return -1;
        buf[0] = '\0';
        len = 0;
    } else {
        buf[len] = '\0';
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        free(buf);
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
    *out_buf = buf;
    if (out_len) *out_len = len;
    return 0;
}

/* Run git; discard stdout. Returns 0 on success. */
static int git_run(char *const argv[]) {
    pid_t pid;
    int status;

    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "public_export: fork: %s\n", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            close(devnull);
        }
        execvp("git", argv);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0) return -1;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "public_export: git command failed (status %d)\n",
                WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        return -1;
    }
    return 0;
}

static int git_ls_files(const char *repo, char **out_buf, size_t *out_len) {
    char *argv[] = {"git", "-C", (char *)repo, "ls-files", "-z", NULL};
    return git_run_capture(argv, out_buf, out_len);
}

static int git_init_dest(const char *dest) {
    char *argv[] = {"git", "init", "-q", "--", (char *)dest, NULL};
    return git_run(argv);
}

/* ---- DEST validation ----------------------------------------------------- */

static int dir_is_empty(const char *path) {
    DIR *d;
    struct dirent *ent;
    int empty = 1;

    d = opendir(path);
    if (!d) return 0;
    while ((ent = readdir(d)) != NULL) {
        if (is_dot_or_dotdot(ent->d_name)) continue;
        empty = 0;
        break;
    }
    closedir(d);
    return empty;
}

/*
 * Resolve what DEST would be without creating it (must already be absolute).
 * out holds the normalized absolute path for within-repo checks.
 */
static int resolve_dest_for_check(const char *dest, char *out, size_t outsz) {
    char parent[PATH_MAX];
    char resolved_parent[PATH_MAX];
    char *slash;
    struct stat st;
    size_t len;

    if (!path_is_absolute(dest)) {
        fprintf(stderr, "public_export: DEST must be an absolute path\n");
        return -1;
    }
    if (strcmp(dest, "/") == 0) {
        fprintf(stderr, "public_export: DEST must not be /\n");
        return -1;
    }

    if (lstat(dest, &st) == 0) {
        char resolved[PATH_MAX];
        if (!realpath(dest, resolved)) {
            fprintf(stderr, "public_export: realpath DEST: %s\n",
                    strerror(errno));
            return -1;
        }
        if (strlen(resolved) + 1 > outsz) return -1;
        memcpy(out, resolved, strlen(resolved) + 1);
        return 0;
    }
    if (errno != ENOENT) {
        fprintf(stderr, "public_export: lstat DEST: %s\n", strerror(errno));
        return -1;
    }

    /* DEST does not exist: resolve parent and join basename. */
    len = strlen(dest);
    if (len + 1 > sizeof parent) return -1;
    memcpy(parent, dest, len + 1);
    slash = strrchr(parent, '/');
    if (!slash) return -1;
    if (slash == parent) {
        /* parent is "/" */
        if (strlen(dest) + 1 > outsz) return -1;
        memcpy(out, dest, strlen(dest) + 1);
        return 0;
    }
    *slash = '\0';
    if (!realpath(parent, resolved_parent)) {
        fprintf(stderr, "public_export: realpath DEST parent %s: %s\n", parent,
                strerror(errno));
        return -1;
    }
    {
        int n = snprintf(out, outsz, "%s/%s", resolved_parent, slash + 1);
        if (n < 0 || (size_t)n >= outsz) return -1;
    }
    return 0;
}

static int validate_dest(const char *dest, const char *repo_real,
                         char *dest_abs, size_t dest_abs_sz) {
    struct stat st;

    if (resolve_dest_for_check(dest, dest_abs, dest_abs_sz) != 0) return -1;

    if (path_is_within(dest_abs, repo_real)) {
        fprintf(stderr,
                "public_export: DEST must not be the repo root or inside the "
                "repo\n");
        return -1;
    }

    if (lstat(dest, &st) == 0) {
        /* Do not follow a DEST symlink; require a real empty directory. */
        if (S_ISLNK(st.st_mode) || !S_ISDIR(st.st_mode)) {
            fprintf(stderr,
                    "public_export: DEST exists and is not a directory\n");
            return -1;
        }
        if (!dir_is_empty(dest)) {
            fprintf(stderr,
                    "public_export: DEST exists and is not empty; refusing to "
                    "overwrite\n");
            return -1;
        }
    } else if (errno == ENOENT) {
        if (mk_dirs_p(dest) != 0) return -1;
    } else {
        fprintf(stderr, "public_export: lstat DEST: %s\n", strerror(errno));
        return -1;
    }

    /* Final realpath after creation. */
    if (!realpath(dest, dest_abs)) {
        fprintf(stderr, "public_export: realpath DEST after create: %s\n",
                strerror(errno));
        return -1;
    }
    if (path_is_within(dest_abs, repo_real)) {
        fprintf(stderr,
                "public_export: DEST must not be the repo root or inside the "
                "repo\n");
        return -1;
    }
    return 0;
}

/* ---- append generated-input ignore block --------------------------------- */

static int gitignore_has_generated_block(const char *path) {
    int fd;
    FILE *fp;
    char *line = NULL;
    size_t cap = 0;
    int has_marker = 0;
    int has_last_path = 0;
    int rc = -1;

    fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "public_export: open %s: %s\n", path, strerror(errno));
        return -1;
    }
    fp = fdopen(fd, "r");
    if (!fp) {
        fprintf(stderr, "public_export: fdopen %s: %s\n", path,
                strerror(errno));
        close(fd);
        return -1;
    }
    while (getline(&line, &cap, fp) >= 0) {
        if (strstr(line,
                   "# ---- regenerated from YOUR Minecraft install "))
            has_marker = 1;
        if (strstr(line, "magma/assets/water_frames.h")) has_last_path = 1;
    }
    if (ferror(fp)) {
        fprintf(stderr, "public_export: read %s: %s\n", path,
                strerror(errno));
    } else {
        rc = has_marker && has_last_path;
    }
    free(line);
    if (fclose(fp) != 0 && rc >= 0) {
        fprintf(stderr, "public_export: close %s: %s\n", path,
                strerror(errno));
        return -1;
    }
    return rc;
}

/*
 * Append the generated-input ignore block without following a symlink.
 * open(O_NOFOLLOW) refuses a symlink last component (ELOOP); a pre-lstat
 * gives a clear error for non-regular existing paths.
 */
static int append_gitignore(const char *dest) {
    char path[PATH_MAX];
    struct stat st;
    int fd = -1;
    size_t left;
    const char *p;
    int n = snprintf(path, sizeof path, "%s/.gitignore", dest);
    if (n < 0 || (size_t)n >= sizeof path) return -1;

    if (lstat(path, &st) == 0) {
        int has_block;
        if (S_ISLNK(st.st_mode)) {
            fprintf(stderr,
                    "public_export: .gitignore is a symlink; refusing to "
                    "follow\n");
            return -1;
        }
        if (!S_ISREG(st.st_mode)) {
            fprintf(stderr,
                    "public_export: .gitignore is not a regular file\n");
            return -1;
        }
        has_block = gitignore_has_generated_block(path);
        if (has_block < 0) return -1;
        if (has_block) return 0;
    } else if (errno != ENOENT) {
        fprintf(stderr, "public_export: lstat %s: %s\n", path, strerror(errno));
        return -1;
    }

    fd = open(path, O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC,
              0644);
    if (fd < 0) {
        if (errno == ELOOP) {
            fprintf(stderr,
                    "public_export: .gitignore is a symlink; refusing to "
                    "follow\n");
        } else {
            fprintf(stderr, "public_export: open %s: %s\n", path,
                    strerror(errno));
        }
        return -1;
    }

    p = GITIGNORE_APPEND;
    left = sizeof GITIGNORE_APPEND - 1;
    while (left > 0) {
        ssize_t w = write(fd, p, left);
        if (w < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "public_export: write %s: %s\n", path,
                    strerror(errno));
            close(fd);
            return -1;
        }
        p += w;
        left -= (size_t)w;
    }
    if (close(fd) != 0) {
        fprintf(stderr, "public_export: close %s: %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

/* ---- main export --------------------------------------------------------- */

static int do_export(const char *repo, const char *dest) {
    char repo_real[PATH_MAX];
    char dest_abs[PATH_MAX];
    char *list = NULL;
    size_t list_len = 0;
    size_t i;
    int n_all = 0, n_pub = 0, n_skip_missing = 0, n_excl = 0;

    if (!repo || !dest) {
        fprintf(stderr, "public_export: repo and dest required\n");
        return 2;
    }
    if (!realpath(repo, repo_real)) {
        fprintf(stderr, "public_export: realpath repo %s: %s\n", repo,
                strerror(errno));
        return 2;
    }

    if (validate_dest(dest, repo_real, dest_abs, sizeof dest_abs) != 0)
        return 2;

    if (git_ls_files(repo_real, &list, &list_len) != 0) {
        fprintf(stderr, "public_export: git ls-files failed in %s\n",
                repo_real);
        return 2;
    }

    for (i = 0; i < list_len;) {
        const char *rel = list + i;
        size_t rlen = strlen(rel);
        char full_src[PATH_MAX];
        char full_dst[PATH_MAX];
        struct stat st;
        int n;

        i += rlen + 1; /* skip NUL */
        if (rlen == 0) continue;
        n_all++;

        if (is_excluded_path(rel)) {
            n_excl++;
            continue;
        }

        n = snprintf(full_src, sizeof full_src, "%s/%s", repo_real, rel);
        if (n < 0 || (size_t)n >= sizeof full_src) {
            fprintf(stderr, "public_export: path too long: %s\n", rel);
            free(list);
            return 2;
        }

        if (lstat(full_src, &st) != 0) {
            /* tracked but missing from worktree: skip */
            n_skip_missing++;
            continue;
        }

        if (validate_tracked_path(repo_real, repo_real, rel, full_src,
                                  sizeof full_src) != 0) {
            free(list);
            return 2;
        }

        n = snprintf(full_dst, sizeof full_dst, "%s/%s", dest_abs, rel);
        if (n < 0 || (size_t)n >= sizeof full_dst) {
            fprintf(stderr, "public_export: dest path too long: %s\n", rel);
            free(list);
            return 2;
        }

        if (S_ISLNK(st.st_mode)) {
            if (copy_symlink(full_src, full_dst) != 0) {
                free(list);
                return 2;
            }
        } else if (S_ISREG(st.st_mode)) {
            if (copy_regular_file(full_src, full_dst, st.st_mode) != 0) {
                free(list);
                return 2;
            }
        } else {
            fprintf(stderr,
                    "public_export: skip non-regular non-symlink tracked "
                    "path: %s\n",
                    rel);
            continue;
        }
        n_pub++;
    }
    free(list);

    if (append_gitignore(dest_abs) != 0) return 2;

    if (git_init_dest(dest_abs) != 0) {
        fprintf(stderr, "public_export: git init failed in %s\n", dest_abs);
        return 2;
    }

    printf("public_export: exported %d/%d tracked files -> %s\n", n_pub, n_all,
           dest_abs);
    printf("public_export: excluded %d Mojang-derived/media; skipped %d "
           "missing\n",
           n_excl, n_skip_missing);
    fflush(stdout);
    return 0;
}

/* ---- self-test helpers --------------------------------------------------- */

/*
 * System temp root for fixtures: TMPDIR if set and non-empty, else /tmp.
 * This is the platform temp location, not a project runtime knob.
 */
static const char *st_temp_root(void) {
    const char *td = getenv("TMPDIR");
    if (td && td[0] != '\0') return td;
    return "/tmp";
}

/* Fill buf with a mkdtemp template under TMPDIR (or /tmp). Returns 0 on ok. */
static int st_temp_template(char *buf, size_t bufsz, const char *name) {
    const char *td = st_temp_root();
    size_t tlen = strlen(td);
    int n;
    /* Allow one trailing slash in TMPDIR without doubling awkwardly. */
    if (tlen > 0 && td[tlen - 1] == '/')
        n = snprintf(buf, bufsz, "%s%s.XXXXXX", td, name);
    else
        n = snprintf(buf, bufsz, "%s/%s.XXXXXX", td, name);
    if (n < 0 || (size_t)n >= bufsz) return -1;
    return 0;
}

static char *st_mkdtemp(char *tmpl) {
    char *p = mkdtemp(tmpl);
    if (!p) {
        fprintf(stderr, "public_export selftest: mkdtemp %s: %s\n", tmpl,
                strerror(errno));
    }
    return p;
}

static int st_join(char *out, size_t out_sz, const char *base,
                   const char *rel) {
    int n = snprintf(out, out_sz, "%s/%s", base, rel);
    if (n < 0 || (size_t)n >= out_sz) {
        fprintf(stderr, "public_export selftest: path too long: %s/%s\n",
                base, rel);
        return -1;
    }
    return 0;
}

static int st_mk_dir(const char *path) {
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "public_export selftest: mkdir %s: %s\n", path,
                strerror(errno));
        return -1;
    }
    return 0;
}

static int st_mk_dirs_under(const char *base, const char *rel) {
    char path[PATH_MAX];
    char tmp[PATH_MAX];
    char *p;
    int n = snprintf(path, sizeof path, "%s/%s", base, rel);
    if (n < 0 || (size_t)n >= sizeof path) return -1;
    snprintf(tmp, sizeof tmp, "%s", path);
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (st_mk_dir(tmp) != 0) return -1;
            *p = '/';
        }
    }
    return st_mk_dir(tmp);
}

static int st_write_text(const char *path, const char *body) {
    FILE *fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "public_export selftest: write %s: %s\n", path,
                strerror(errno));
        return -1;
    }
    if (fputs(body, fp) < 0) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

static int st_write_under(const char *base, const char *rel, const char *body) {
    char path[PATH_MAX];
    char dir[PATH_MAX];
    char *slash;
    int n = snprintf(path, sizeof path, "%s/%s", base, rel);
    if (n < 0 || (size_t)n >= sizeof path) return -1;
    snprintf(dir, sizeof dir, "%s", path);
    slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        if (strlen(dir) > strlen(base)) {
            const char *rel_dir = dir + strlen(base) + 1;
            if (rel_dir[0] && st_mk_dirs_under(base, rel_dir) != 0) return -1;
        }
    }
    return st_write_text(path, body);
}

static int st_rm_rf(const char *path) {
    struct stat st;
    DIR *d;
    struct dirent *ent;

    if (lstat(path, &st) != 0) return 0;
    if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
        d = opendir(path);
        if (!d) return -1;
        while ((ent = readdir(d)) != NULL) {
            char child[PATH_MAX];
            if (is_dot_or_dotdot(ent->d_name)) continue;
            snprintf(child, sizeof child, "%s/%s", path, ent->d_name);
            if (st_rm_rf(child) != 0) {
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

static int st_git(const char *repo, char *const extra_argv[]) {
    /* build argv: git -C repo <extra...> */
    char *argv[24];
    size_t n = 0;
    size_t i;
    argv[n++] = "git";
    argv[n++] = "-C";
    argv[n++] = (char *)repo;
    for (i = 0; extra_argv[i]; i++) {
        if (n + 1 >= sizeof argv / sizeof argv[0]) return -1;
        argv[n++] = extra_argv[i];
    }
    argv[n] = NULL;
    return git_run(argv);
}

/* Local identity only via argv -c; never env or global config. */
static int st_git_commit(const char *repo, const char *message) {
    char *argv[] = {
        "git",
        "-C",
        (char *)repo,
        "-c",
        "user.name=public_export-selftest",
        "-c",
        "user.email=public_export-selftest@invalid",
        "commit",
        "-m",
        (char *)message,
        NULL,
    };
    return git_run(argv);
}

static int st_file_equals(const char *path, const char *expect) {
    FILE *fp;
    char buf[4096];
    size_t elen = strlen(expect);
    size_t n;

    fp = fopen(path, "r");
    if (!fp) return 0;
    n = fread(buf, 1, sizeof buf - 1, fp);
    fclose(fp);
    buf[n] = '\0';
    return n == elen && memcmp(buf, expect, elen) == 0;
}

static int st_path_exists(const char *path) {
    struct stat st;
    return lstat(path, &st) == 0;
}

static int st_is_symlink_to(const char *path, const char *want) {
    char target[PATH_MAX];
    ssize_t n = readlink(path, target, sizeof target - 1);
    if (n < 0) return 0;
    target[n] = '\0';
    return strcmp(target, want) == 0;
}

static int st_is_executable(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (st.st_mode & 0111) != 0;
}

/* Commit count via git rev-list --all --count. Returns -1 on error. */
static int st_commit_count(const char *repo) {
    char *out = NULL;
    size_t out_len = 0;
    char *argv[] = {"git",   "-C", (char *)repo, "rev-list",
                    "--all", "--count", NULL};
    long v;
    char *end;
    int rc;

    if (git_run_capture(argv, &out, &out_len) != 0) {
        free(out);
        return -1;
    }
    if (!out) return -1;

    /* Parse and validate before free. */
    v = strtol(out, &end, 10);
    if (end == out) {
        free(out);
        return -1;
    }
    /* Allow optional trailing newline only. */
    if (*end == '\n')
        end++;
    if (*end != '\0') {
        free(out);
        return -1;
    }
    if (v < 0 || v > INT_MAX) {
        free(out);
        return -1;
    }
    rc = (int)v;
    free(out);
    return rc;
}

/* True if dest has a .git dir and zero commits. */
static int st_has_fresh_git(const char *dest) {
    char gitdir[PATH_MAX];
    struct stat st;
    int n;

    snprintf(gitdir, sizeof gitdir, "%s/.git", dest);
    if (lstat(gitdir, &st) != 0) return 0;
    n = st_commit_count(dest);
    return n == 0;
}

/* Prove validate_tracked_path rejects absolute and .. paths. */
static int st_test_path_rejects(const char *repo_real) {
    char full[PATH_MAX];
    if (validate_tracked_path(repo_real, repo_real, "/etc/passwd", full,
                              sizeof full) == 0) {
        fprintf(stderr, "selftest: absolute path should reject\n");
        return -1;
    }
    if (validate_tracked_path(repo_real, repo_real, "foo/../../etc/passwd",
                              full, sizeof full) == 0) {
        fprintf(stderr, "selftest: .. path should reject\n");
        return -1;
    }
    if (validate_tracked_path(repo_real, repo_real, "../outside", full,
                              sizeof full) == 0) {
        fprintf(stderr, "selftest: leading .. should reject\n");
        return -1;
    }
    return 0;
}

/* Stage a blob at index path (for paths git add will not take through a
 * directory symlink). */
static int st_git_index_blob(const char *repo, const char *abs_file,
                             const char *index_path) {
    char *hash = NULL;
    size_t hash_len = 0;
    char *ho_argv[] = {"git", "-C", (char *)repo, "hash-object", "-w",
                       "--", (char *)abs_file, NULL};
    char cacheinfo[PATH_MAX];
    char *ui_argv[8];
    int n;

    if (git_run_capture(ho_argv, &hash, &hash_len) != 0 || !hash) {
        free(hash);
        fprintf(stderr, "selftest: git hash-object failed\n");
        return -1;
    }
    /* strip trailing newline from hash */
    while (hash_len > 0 &&
           (hash[hash_len - 1] == '\n' || hash[hash_len - 1] == '\r')) {
        hash[--hash_len] = '\0';
    }
    n = snprintf(cacheinfo, sizeof cacheinfo, "100644,%s,%s", hash, index_path);
    free(hash);
    if (n < 0 || (size_t)n >= sizeof cacheinfo) return -1;

    ui_argv[0] = "git";
    ui_argv[1] = "-C";
    ui_argv[2] = (char *)repo;
    ui_argv[3] = "update-index";
    ui_argv[4] = "--add";
    ui_argv[5] = "--cacheinfo";
    ui_argv[6] = cacheinfo;
    ui_argv[7] = NULL;
    if (git_run(ui_argv) != 0) {
        fprintf(stderr, "selftest: git update-index failed\n");
        return -1;
    }
    return 0;
}

/*
 * Intermediate directory symlink resolves outside the repo: export must fail
 * containment. Outside payload must remain unmodified.
 */
static int st_test_intermediate_symlink_escape(void) {
    char base_tmpl[PATH_MAX];
    char *base = NULL;
    char repo[PATH_MAX];
    char outside[PATH_MAX];
    char escape[PATH_MAX];
    char dest[PATH_MAX];
    char path[PATH_MAX];
    char payload_abs[PATH_MAX];
    char *add_ok[] = {"add", "--", "ok.txt", NULL};
    char full[PATH_MAX];
    char repo_real[PATH_MAX];
    int rc = -1;

    if (st_temp_template(base_tmpl, sizeof base_tmpl, "public_export_esc") !=
        0)
        return -1;
    base = st_mkdtemp(base_tmpl);
    if (!base) return -1;

    snprintf(repo, sizeof repo, "%s/repo", base);
    snprintf(outside, sizeof outside, "%s/outside", base);
    snprintf(dest, sizeof dest, "%s/dest", base);
    if (st_mk_dir(repo) != 0) goto out;
    if (st_mk_dir(outside) != 0) goto out;
    if (st_write_under(outside, "payload.txt", "outside-payload\n") != 0)
        goto out;
    if (st_join(payload_abs, sizeof payload_abs, outside, "payload.txt") != 0)
        goto out;

    {
        char *init_argv[] = {"init", "-q", NULL};
        if (st_git(repo, init_argv) != 0) goto out;
    }
    if (st_write_under(repo, "ok.txt", "ok\n") != 0) goto out;
    if (st_git(repo, add_ok) != 0) goto out;

    /* escape -> ../outside (directory symlink leaving the repo). Not staged
     * as a symlink entry; only the path through it is indexed. */
    if (st_join(escape, sizeof escape, repo, "escape") != 0) goto out;
    if (symlink("../outside", escape) != 0) {
        fprintf(stderr, "selftest: escape symlink: %s\n", strerror(errno));
        goto out;
    }
    if (st_join(path, sizeof path, repo, "escape/payload.txt") != 0) goto out;
    if (!st_path_exists(path)) {
        fprintf(stderr, "selftest: escape/payload.txt missing after symlink\n");
        goto out;
    }
    if (st_git_index_blob(repo, payload_abs, "escape/payload.txt") != 0)
        goto out;
    if (st_git_commit(repo, "escape fixture") != 0) goto out;

    if (!realpath(repo, repo_real)) goto out;
    /* Unit containment: parent of escape/payload.txt resolves outside. */
    if (validate_tracked_path(repo_real, repo_real, "escape/payload.txt", full,
                              sizeof full) == 0) {
        fprintf(stderr,
                "selftest: validate should reject intermediate outside "
                "symlink\n");
        goto out;
    }
    if (do_export(repo, dest) == 0) {
        fprintf(stderr,
                "selftest: intermediate outside dir symlink should fail\n");
        goto out;
    }
    /* Outside payload must not have been modified. */
    if (!st_file_equals(payload_abs, "outside-payload\n")) {
        fprintf(stderr, "selftest: outside payload changed\n");
        goto out;
    }
    rc = 0;
out:
    if (base) st_rm_rf(base);
    return rc;
}

/*
 * Tracked .gitignore symlink must not cause append to write outside the export.
 */
static int st_test_gitignore_symlink_nofollow(void) {
    char base_tmpl[PATH_MAX];
    char *base = NULL;
    char repo[PATH_MAX];
    char outside[PATH_MAX];
    char dest[PATH_MAX];
    char path[PATH_MAX];
    char outside_real[PATH_MAX];
    char *add_argv[] = {"add", "-A", NULL};
    const char *keep = "OUTSIDE_KEEP\n";
    int rc = -1;

    if (st_temp_template(base_tmpl, sizeof base_tmpl, "public_export_gi") != 0)
        return -1;
    base = st_mkdtemp(base_tmpl);
    if (!base) return -1;

    snprintf(repo, sizeof repo, "%s/repo", base);
    snprintf(dest, sizeof dest, "%s/dest", base);
    snprintf(outside, sizeof outside, "%s/outside_gitignore", base);
    if (st_mk_dir(repo) != 0) goto out;
    if (st_write_text(outside, keep) != 0) goto out;
    if (!realpath(outside, outside_real)) goto out;

    {
        char *init_argv[] = {"init", "-q", NULL};
        if (st_git(repo, init_argv) != 0) goto out;
    }
    if (st_write_under(repo, "ok.txt", "ok\n") != 0) goto out;
    if (st_join(path, sizeof path, repo, ".gitignore") != 0) goto out;
    if (symlink(outside_real, path) != 0) {
        fprintf(stderr, "selftest: .gitignore symlink: %s\n", strerror(errno));
        goto out;
    }
    if (st_git(repo, add_argv) != 0) goto out;
    if (st_git_commit(repo, "gitignore symlink fixture") != 0) goto out;

    if (do_export(repo, dest) == 0) {
        fprintf(stderr, "selftest: symlink .gitignore export should fail\n");
        goto out;
    }
    if (!st_file_equals(outside, keep)) {
        fprintf(stderr, "selftest: outside .gitignore target was modified\n");
        goto out;
    }
    /* Also prove direct append_gitignore refuses a symlink path. */
    {
        char gi_dest_tmpl[PATH_MAX];
        char *gi_dest;
        char gi_path[PATH_MAX];
        if (st_temp_template(gi_dest_tmpl, sizeof gi_dest_tmpl,
                             "public_export_gi_dest") != 0)
            goto out;
        gi_dest = st_mkdtemp(gi_dest_tmpl);
        if (!gi_dest) goto out;
        if (st_join(gi_path, sizeof gi_path, gi_dest, ".gitignore") != 0) {
            st_rm_rf(gi_dest);
            goto out;
        }
        if (symlink(outside_real, gi_path) != 0) {
            st_rm_rf(gi_dest);
            goto out;
        }
        if (append_gitignore(gi_dest) == 0) {
            fprintf(stderr, "selftest: append_gitignore followed symlink\n");
            st_rm_rf(gi_dest);
            goto out;
        }
        if (!st_file_equals(outside, keep)) {
            fprintf(stderr,
                    "selftest: append_gitignore modified outside file\n");
            st_rm_rf(gi_dest);
            goto out;
        }
        st_rm_rf(gi_dest);
    }
    rc = 0;
out:
    if (base) st_rm_rf(base);
    return rc;
}

static int selftest(void) {
    char repo_tmpl[PATH_MAX];
    char dest_tmpl[PATH_MAX];
    char *repo = NULL;
    char *dest_base = NULL;
    char dest[PATH_MAX];
    char path[PATH_MAX];
    char repo_real[PATH_MAX];
    int failed = 0;
    int src_commits;
    char *add_argv[] = {"add", "-A", NULL};
    const char *included_body = "hello public\n";
    const char *exec_body = "#!/bin/sh\necho ok\n";
    const char *link_target = "included.txt";

    if (st_temp_template(repo_tmpl, sizeof repo_tmpl, "public_export_repo") !=
        0) {
        fprintf(stderr, "public_export selftest: temp template failed\n");
        return 1;
    }
    if (st_temp_template(dest_tmpl, sizeof dest_tmpl, "public_export_dest") !=
        0) {
        fprintf(stderr, "public_export selftest: temp template failed\n");
        return 1;
    }

    repo = st_mkdtemp(repo_tmpl);
    if (!repo) return 1;
    dest_base = st_mkdtemp(dest_tmpl);
    if (!dest_base) {
        st_rm_rf(repo);
        return 1;
    }
    /* Fresh child path as DEST (does not exist yet). */
    snprintf(dest, sizeof dest, "%s/export_out", dest_base);

    /* --- build temporary git repository with one local commit --- */
    {
        char *init_argv[] = {"init", "-q", NULL};
        if (st_git(repo, init_argv) != 0) goto boom;
    }

    if (st_write_under(repo, "included.txt", included_body) != 0) goto boom;
    if (st_write_under(repo, "bin/tool.sh", exec_body) != 0) goto boom;
    snprintf(path, sizeof path, "%s/bin/tool.sh", repo);
    if (chmod(path, 0755) != 0) goto boom;

    snprintf(path, sizeof path, "%s/link_to_included", repo);
    if (symlink(link_target, path) != 0) {
        fprintf(stderr, "public_export selftest: symlink: %s\n",
                strerror(errno));
        goto boom;
    }

    if (st_write_under(repo, "java/oracle-src/Secret.java",
                       "class Secret {}\n") != 0)
        goto boom;
    if (st_write_under(repo, "magma/assets/atlas_gen.h",
                       "/* generated */\n") != 0)
        goto boom;
    if (st_write_under(repo, "pic.PNG", "pngbytes") != 0) goto boom;
    if (st_write_under(repo, "shot.ppm", "ppmbytes") != 0) goto boom;
    if (st_write_under(repo, "a.JPG", "jpgbytes") != 0) goto boom;
    if (st_write_under(repo, "b.jpeg", "jpegbytes") != 0) goto boom;
    if (st_write_under(repo, "c.GIF", "gifbytes") != 0) goto boom;
    if (st_write_under(repo, "d.MP4", "mp4bytes") != 0) goto boom;
    if (st_write_under(repo, ".gitignore", "# root ignore\n") != 0) goto boom;

    if (st_git(repo, add_argv) != 0) goto boom;
    if (st_git_commit(repo, "public_export selftest fixture") != 0) goto boom;

    src_commits = st_commit_count(repo);
    if (src_commits < 1) {
        fprintf(stderr,
                "selftest: source repo should have >=1 commit, got %d\n",
                src_commits);
        goto boom;
    }

    if (!realpath(repo, repo_real)) goto boom;

    /* --- path traversal unit checks --- */
    if (st_test_path_rejects(repo_real) != 0) {
        failed = 1;
        goto done;
    }

    /* --- happy-path export --- */
    if (do_export(repo, dest) != 0) {
        fprintf(stderr, "public_export selftest: export failed\n");
        failed = 1;
        goto done;
    }
    if (append_gitignore(dest) != 0) {
        fprintf(stderr, "selftest: repeated gitignore append failed\n");
        failed = 1;
        goto done;
    }

    /* included regular file bytes */
    if (st_join(path, sizeof path, dest, "included.txt") != 0) goto boom;
    if (!st_file_equals(path, included_body)) {
        fprintf(stderr, "selftest: included.txt content mismatch\n");
        failed = 1;
    }

    /* executable mode */
    if (st_join(path, sizeof path, dest, "bin/tool.sh") != 0) goto boom;
    if (!st_path_exists(path) || !st_is_executable(path)) {
        fprintf(stderr, "selftest: executable not preserved\n");
        failed = 1;
    }
    if (!st_file_equals(path, exec_body)) {
        fprintf(stderr, "selftest: executable content mismatch\n");
        failed = 1;
    }

    /* symlink preserved as symlink with same target */
    if (st_join(path, sizeof path, dest, "link_to_included") != 0) goto boom;
    if (!st_is_symlink_to(path, link_target)) {
        fprintf(stderr, "selftest: symlink target mismatch\n");
        failed = 1;
    }

    /* exclusions */
    if (st_join(path, sizeof path, dest, "java/oracle-src/Secret.java") != 0)
        goto boom;
    if (st_path_exists(path)) {
        fprintf(stderr, "selftest: oracle-src should be excluded\n");
        failed = 1;
    }
    if (st_join(path, sizeof path, dest, "magma/assets/atlas_gen.h") != 0)
        goto boom;
    if (st_path_exists(path)) {
        fprintf(stderr, "selftest: atlas_gen.h should be excluded\n");
        failed = 1;
    }
    {
        const char *media[] = {"pic.PNG", "shot.ppm", "a.JPG",
                               "b.jpeg",  "c.GIF",    "d.MP4"};
        size_t mi;
        for (mi = 0; mi < sizeof media / sizeof media[0]; mi++) {
            if (st_join(path, sizeof path, dest, media[mi]) != 0) goto boom;
            if (st_path_exists(path)) {
                fprintf(stderr, "selftest: media should be excluded: %s\n",
                        media[mi]);
                failed = 1;
            }
        }
    }

    /* Source has history; export must be a fresh repo with zero commits. */
    if (st_commit_count(repo) < 1) {
        fprintf(stderr, "selftest: source commits vanished\n");
        failed = 1;
    }
    if (!st_has_fresh_git(dest)) {
        fprintf(stderr,
                "selftest: expected fresh git repo with 0 commits (source had "
                "%d)\n",
                src_commits);
        failed = 1;
    } else if (st_commit_count(dest) != 0) {
        fprintf(stderr, "selftest: exported git has %d commits, want 0\n",
                st_commit_count(dest));
        failed = 1;
    }

    /* gitignore append present */
    if (st_join(path, sizeof path, dest, ".gitignore") != 0) goto boom;
    {
        FILE *fp = fopen(path, "r");
        char buf[4096];
        size_t n;
        if (!fp) {
            fprintf(stderr, "selftest: .gitignore missing\n");
            failed = 1;
        } else {
            n = fread(buf, 1, sizeof buf - 1, fp);
            fclose(fp);
            buf[n] = '\0';
            if (!strstr(buf, "# root ignore\n")) {
                fprintf(stderr, "selftest: original .gitignore lost\n");
                failed = 1;
            }
            if (!strstr(buf, "java/oracle-src/")) {
                fprintf(stderr, "selftest: ignore block not appended\n");
                failed = 1;
            }
            if (!strstr(buf, "magma/assets/water_frames.h")) {
                fprintf(stderr, "selftest: ignore block incomplete\n");
                failed = 1;
            }
            {
                const char *marker =
                    "# ---- regenerated from YOUR Minecraft install";
                char *first = strstr(buf, marker);
                if (first && strstr(first + strlen(marker), marker)) {
                    fprintf(stderr, "selftest: ignore block duplicated\n");
                    failed = 1;
                }
            }
        }
    }

    /* --- reject non-empty DEST --- */
    {
        char dest2[PATH_MAX];
        char junk[PATH_MAX];
        snprintf(dest2, sizeof dest2, "%s/nonempty", dest_base);
        if (st_mk_dir(dest2) != 0) {
            failed = 1;
            goto done;
        }
        if (st_join(junk, sizeof junk, dest2, "x") != 0) goto boom;
        if (st_write_text(junk, "nope\n") != 0) {
            failed = 1;
            goto done;
        }
        if (do_export(repo, dest2) == 0) {
            fprintf(stderr, "selftest: non-empty DEST should fail\n");
            failed = 1;
        }
    }

    /* --- reject non-directory DEST --- */
    {
        char dest_file[PATH_MAX];
        snprintf(dest_file, sizeof dest_file, "%s/not_a_dir", dest_base);
        if (st_write_text(dest_file, "i am a file\n") != 0) {
            failed = 1;
            goto done;
        }
        if (do_export(repo, dest_file) == 0) {
            fprintf(stderr, "selftest: non-directory DEST should fail\n");
            failed = 1;
        }
    }

    /* --- reject DEST inside repo --- */
    {
        char dest3[PATH_MAX];
        snprintf(dest3, sizeof dest3, "%s/inside_export", repo);
        if (do_export(repo, dest3) == 0) {
            fprintf(stderr, "selftest: inside-repo DEST should fail\n");
            failed = 1;
        }
    }

    /* --- reject DEST == / --- */
    if (do_export(repo, "/") == 0) {
        fprintf(stderr, "selftest: DEST=/ should fail\n");
        failed = 1;
    }

    /* --- reject DEST == repo root --- */
    if (do_export(repo, repo_real) == 0) {
        fprintf(stderr, "selftest: DEST=repo root should fail\n");
        failed = 1;
    }

    /* --- reject relative DEST --- */
    if (do_export(repo, "relative/out") == 0) {
        fprintf(stderr, "selftest: relative DEST should fail\n");
        failed = 1;
    }

    /* --- intermediate directory symlink outside repo --- */
    if (st_test_intermediate_symlink_escape() != 0) {
        fprintf(stderr, "selftest: intermediate symlink containment failed\n");
        failed = 1;
    }

    /* --- .gitignore symlink must not write outside --- */
    if (st_test_gitignore_symlink_nofollow() != 0) {
        fprintf(stderr, "selftest: gitignore symlink nofollow failed\n");
        failed = 1;
    }

done:
    if (dest_base) st_rm_rf(dest_base);
    if (repo) st_rm_rf(repo);
    if (failed) {
        fprintf(stderr, "public_export: selftest FAIL\n");
        return 1;
    }
    printf("public_export: selftest PASS\n");
    return 0;

boom:
    if (dest_base) st_rm_rf(dest_base);
    if (repo) st_rm_rf(repo);
    fprintf(stderr, "public_export: selftest setup failed\n");
    return 1;
}

/* ---- main ---------------------------------------------------------------- */

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s --repo ROOT --dest DEST\n"
            "       %s --selftest\n",
            argv0, argv0);
}

int main(int argc, char **argv) {
    const char *repo = NULL;
    const char *dest = NULL;
    int i;

    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--selftest") == 0) {
            if (argc != 2) {
                usage(argv[0]);
                return 2;
            }
            return selftest();
        }
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--repo") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            repo = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--dest") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            dest = argv[++i];
            continue;
        }
        fprintf(stderr, "public_export: unknown argument: %s\n", argv[i]);
        usage(argv[0]);
        return 2;
    }

    if (!repo || !dest) {
        usage(argv[0]);
        return 2;
    }
    return do_export(repo, dest);
}
