#define _POSIX_C_SOURCE 200809L
#include "obs_config.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fail(char *err, size_t cap, const char *why) {
    if (err && cap) snprintf(err, cap, "%s", why);
    return -1;
}
void policy_io_default(PolicyIoConfig *c) {
    if (!c) return;
    memset(c, 0, sizeof *c);
    c->obs_history = 2; c->obs_semantic_mask = 127;
    c->obs_depth = c->obs_edges = c->obs_base_scalars = 1;
    c->obs_inventory = c->obs_pose = c->obs_clock = 1;
    c->obs_pixel_stride = 1;
    c->action_yaw_degrees = 15; c->action_pitch_degrees = 10;
    c->action_heads = 511;
}
int policy_io_validate(const PolicyIoConfig *c, char *err, size_t cap) {
    if (!c) return fail(err, cap, "missing policy input/output configuration");
    if (c->obs_history != 1 && c->obs_history != 2)
        return fail(err, cap, "obs_history must be 1 or 2; recurrent policies are unsupported");
    if (c->obs_semantic_mask < 0 || c->obs_semantic_mask > 127)
        return fail(err, cap, "obs_semantic_mask must be a seven-bit mask (0..127)");
    const int toggles[] = {c->obs_depth,c->obs_edges,c->obs_base_scalars,
                           c->obs_inventory,c->obs_pose,c->obs_clock};
    for (size_t i = 0; i < sizeof toggles / sizeof toggles[0]; i++)
        if (toggles[i] != 0 && toggles[i] != 1)
            return fail(err, cap, "observation toggles must be 0 or 1");
    if (c->obs_pixel_stride != 1 && c->obs_pixel_stride != 2 && c->obs_pixel_stride != 4)
        return fail(err, cap, "obs_pixel_stride must be 1, 2 or 4; tensor size stays 64x36");
    if (!isfinite(c->action_yaw_degrees) || c->action_yaw_degrees <= 0 || c->action_yaw_degrees > 180)
        return fail(err, cap, "action_yaw_degrees must be finite and in (0,180]");
    if (!isfinite(c->action_pitch_degrees) || c->action_pitch_degrees <= 0 || c->action_pitch_degrees > 90)
        return fail(err, cap, "action_pitch_degrees must be finite and in (0,90]");
    if (c->action_heads < 0 || c->action_heads > 511)
        return fail(err, cap, "action_heads must be a nine-bit mask (0..511)");
    if (err && cap) err[0] = 0;
    return 0;
}
static const char *const keys[] = {
    "obs_history", "obs_semantic_mask", "obs_depth", "obs_edges",
    "obs_base_scalars", "obs_inventory", "obs_pose", "obs_clock",
    "obs_pixel_stride", "action_yaw_degrees", "action_pitch_degrees", "action_heads"
};
static int key_index(const char *key) {
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
        if (!strcmp(key, keys[i])) return (int)i;
    return -1;
}
int policy_io_set(PolicyIoConfig *c, const char *key, const char *value,
                  char *err, size_t cap) {
    PolicyIoConfig next;
    char *end;
    double d = 0;
    long v = 0;
    if (!c || !key || !value) return fail(err, cap, "missing policy configuration key/value");
    int k = key_index(key);
    if (k < 0) return 1;
    errno = 0;
    if (k == 9 || k == 10) d = strtod(value, &end);
    else v = strtol(value, &end, 0);
    if (end == value || errno || (k != 9 && k != 10 && (v < INT_MIN || v > INT_MAX)))
        return fail(err, cap, "invalid policy configuration number");
    while (isspace((unsigned char)*end)) end++;
    if (*end) return fail(err, cap, "trailing text in policy configuration number");
    next = *c;
    switch (k) {
        case 0: next.obs_history = (int)v; break;
        case 1: next.obs_semantic_mask = (int)v; break;
        case 2: next.obs_depth = (int)v; break;
        case 3: next.obs_edges = (int)v; break;
        case 4: next.obs_base_scalars = (int)v; break;
        case 5: next.obs_inventory = (int)v; break;
        case 6: next.obs_pose = (int)v; break;
        case 7: next.obs_clock = (int)v; break;
        case 8: next.obs_pixel_stride = (int)v; break;
        case 9: next.action_yaw_degrees = d; break;
        case 10: next.action_pitch_degrees = d; break;
        case 11: next.action_heads = (int)v; break;
    }
    if (policy_io_validate(&next, err, cap)) return -1;
    *c = next;
    return 0;
}
/* One canonical representation is used for dumps, fingerprints and metadata. */
static int format(const PolicyIoConfig *c, char *buf, size_t cap) {
    return snprintf(buf, cap,
        "obs_history = %d\nobs_semantic_mask = %d\nobs_depth = %d\nobs_edges = %d\n"
        "obs_base_scalars = %d\nobs_inventory = %d\nobs_pose = %d\nobs_clock = %d\n"
        "obs_pixel_stride = %d\naction_yaw_degrees = %.17g\naction_pitch_degrees = %.17g\n"
        "action_heads = %d\n",
        c->obs_history,c->obs_semantic_mask,c->obs_depth,c->obs_edges,
        c->obs_base_scalars,c->obs_inventory,c->obs_pose,c->obs_clock,
        c->obs_pixel_stride,c->action_yaw_degrees,c->action_pitch_degrees,c->action_heads);
}
void policy_io_dump(const PolicyIoConfig *c, FILE *out) {
    char buf[1024];
    if (c && out && format(c, buf, sizeof buf) < (int)sizeof buf) fputs(buf, out);
}
uint64_t policy_io_fingerprint(const PolicyIoConfig *c) {
    char buf[1024];
    const char schema[] = "blaze-policy-io-v1;planes=18x36x64;scalars=27;heads=9;nearest-top-left;\n";
    uint64_t h = UINT64_C(14695981039346656037);
    if (policy_io_validate(c, NULL, 0) || format(c, buf, sizeof buf) >= (int)sizeof buf) return 0;
    for (size_t i = 0; i < sizeof schema - 1; i++) h = (h ^ (unsigned char)schema[i]) * UINT64_C(1099511628211);
    for (size_t i = 0; buf[i]; i++) h = (h ^ (unsigned char)buf[i]) * UINT64_C(1099511628211);
    return h;
}
int policy_io_is_default(const PolicyIoConfig *c) {
    PolicyIoConfig d;
    char a[1024], b[1024];
    if (policy_io_validate(c, NULL, 0)) return 0;
    policy_io_default(&d); format(c, a, sizeof a); format(&d, b, sizeof b);
    return !strcmp(a, b);
}
static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = 0;
    return s;
}
static int side_path(const char *path, char *out, size_t cap, char *err, size_t ecap) {
    if (!path || !path[0] || snprintf(out, cap, "%s.policy.conf", path) >= (int)cap)
        return fail(err, ecap, "policy checkpoint path is empty or too long");
    return 0;
}
int policy_io_checkpoint_check(const char *path, const PolicyIoConfig *c,
                               char *err, size_t cap) {
    char side[4096], line[1024];
    PolicyIoConfig found;
    unsigned mask = 0;
    uint64_t saved = 0;
    int version = 0, have_fp = 0;
    if (policy_io_validate(c, err, cap) || side_path(path, side, sizeof side, err, cap)) return -1;
    FILE *f = fopen(side, "r");
    if (!f) {
        if (errno != ENOENT) return fail(err, cap, "cannot read checkpoint policy contract");
        if (!policy_io_is_default(c)) return fail(err, cap, "checkpoint lacks policy contract; nondefault observations/actions cannot load legacy weights");
        if (err && cap) snprintf(err, cap, "legacy checkpoint has no policy contract; assuming exact default observations/actions");
        return 1;
    }
    policy_io_default(&found);
    int bad = 0;
    while (fgets(line, sizeof line, f)) {
        if (!strchr(line, '\n') && !feof(f)) { bad = 1; break; }
        char *key = trim(line), *eq, *value;
        if (!*key || *key == '#') continue;
        eq = strchr(key, '=');
        if (!eq) { bad = 1; break; }
        *eq = 0; value = trim(eq + 1); key = trim(key);
        if (!strcmp(key, "policy_io_version")) {
            if (version || strcmp(value, "1")) { bad = 1; break; }
            version = 1;
        } else if (!strcmp(key, "policy_io_fingerprint")) {
            char *end;
            errno = 0; saved = strtoull(value, &end, 16);
            if (have_fp || errno || strlen(value) != 16 || *end) { bad = 1; break; }
            have_fp = 1;
        } else {
            int k = key_index(key);
            if (k < 0 || (mask & (1u << k)) || policy_io_set(&found, key, value, NULL, 0)) { bad = 1; break; }
            mask |= 1u << k;
        }
    }
    if (ferror(f)) bad = 1;
    fclose(f);
    if (bad || !version || !have_fp || mask != 4095 || saved != policy_io_fingerprint(&found))
        return fail(err, cap, "malformed or incomplete checkpoint policy contract");
    char a[1024], b[1024];
    format(&found, a, sizeof a); format(c, b, sizeof b);
    if (strcmp(a, b)) return fail(err, cap, "checkpoint observations/actions differ from requested policy configuration");
    if (err && cap) err[0] = 0;
    return 0;
}
int policy_io_checkpoint_can_save(const char *path, const PolicyIoConfig *c,
                                  char *err, size_t cap) {
    char side[4096];
    if (policy_io_validate(c, err, cap) || side_path(path, side, sizeof side, err, cap)) return -1;
    if (access(side, F_OK) == 0) return policy_io_checkpoint_check(path, c, err, cap) == 0 ? 0 : -1;
    if (errno != ENOENT) return fail(err, cap, "cannot inspect checkpoint policy contract");
    if (access(path, F_OK) == 0) {
        if (!policy_io_is_default(c)) return fail(err, cap, "refusing to overwrite legacy weights with a nondefault policy contract");
    } else if (errno != ENOENT) return fail(err, cap, "cannot inspect checkpoint weights");
    if (err && cap) err[0] = 0;
    return 0;
}
int policy_io_checkpoint_write(const char *path, const PolicyIoConfig *c,
                               char *err, size_t cap) {
    char side[4096], temp[4112], body[1024];
    if (policy_io_validate(c, err, cap) || side_path(path, side, sizeof side, err, cap)) return -1;
    if (access(side, F_OK) == 0) return policy_io_checkpoint_check(path, c, err, cap) == 0 ? 0 : -1;
    if (errno != ENOENT) return fail(err, cap, "cannot inspect checkpoint policy contract");
    if (snprintf(temp, sizeof temp, "%s.tmp.XXXXXX", side) >= (int)sizeof temp)
        return fail(err, cap, "temporary policy contract path too long");
    int fd = mkstemp(temp);
    if (fd < 0) return fail(err, cap, "cannot create temporary policy contract");
    FILE *f = fdopen(fd, "w");
    if (!f) { close(fd); unlink(temp); return fail(err, cap, "cannot open temporary policy contract"); }
    format(c, body, sizeof body);
    int bad = fprintf(f, "policy_io_version = 1\npolicy_io_fingerprint = %016llx\n%s",
                      (unsigned long long)policy_io_fingerprint(c), body) < 0;
    if (fflush(f) || fsync(fd)) bad = 1;
    if (fclose(f)) bad = 1;
    if (bad) { unlink(temp); return fail(err, cap, "cannot flush checkpoint policy contract"); }
    /* link is an atomic no-replace publish on the same filesystem. */
    if (link(temp, side)) {
        int exists = errno == EEXIST;
        unlink(temp);
        if (exists && policy_io_checkpoint_check(path, c, err, cap) == 0) return 0;
        return fail(err, cap, "cannot publish policy contract without replacing an existing file");
    }
    unlink(temp);
    if (err && cap) err[0] = 0;
    return 0;
}
