#include "game/native_save.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static void save_error(char *out, size_t cap, const char *format, ...) {
    va_list args;
    if (!out || cap == 0) return;
    va_start(args, format);
    vsnprintf(out, cap, format, args);
    va_end(args);
}

static int save_slot_valid(const char *slot) {
    size_t len;
    if (!slot || !(len = strlen(slot)) || len > GM_NATIVE_SAVE_SLOT_MAX
            || slot[0] == '.')
        return 0;
    for (size_t index = 0; index < len; ++index) {
        unsigned char ch = (unsigned char)slot[index];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
                || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-'))
            return 0;
    }
    return 1;
}

static int save_path(
        char out[PATH_MAX], const char *format,
        const char *root, const char *slot, unsigned long long generation) {
    return snprintf(out, PATH_MAX, format, root, slot, generation) < PATH_MAX;
}

static int save_ensure_directory(const char *path) {
    struct stat value;
    if (mkdir(path, 0755) == 0) return 1;
    return errno == EEXIST && stat(path, &value) == 0
        && S_ISDIR(value.st_mode);
}

static int save_write_u32(FILE *stream, uint32_t value) {
    unsigned char raw[4] = {
        (unsigned char)value, (unsigned char)(value >> 8),
        (unsigned char)(value >> 16), (unsigned char)(value >> 24),
    };
    return fwrite(raw, 1, sizeof raw, stream) == sizeof raw;
}

static int save_write_u64(FILE *stream, uint64_t value) {
    unsigned char raw[8];
    for (int index = 0; index < 8; ++index) {
        raw[index] = (unsigned char)value;
        value >>= 8;
    }
    return fwrite(raw, 1, sizeof raw, stream) == sizeof raw;
}

static int save_read_u32(FILE *stream, uint32_t *value) {
    unsigned char raw[4];
    if (fread(raw, 1, sizeof raw, stream) != sizeof raw) return 0;
    *value = (uint32_t)raw[0] | (uint32_t)raw[1] << 8
        | (uint32_t)raw[2] << 16 | (uint32_t)raw[3] << 24;
    return 1;
}

static int save_read_u64(FILE *stream, uint64_t *value) {
    unsigned char raw[8];
    if (fread(raw, 1, sizeof raw, stream) != sizeof raw) return 0;
    *value = 0;
    for (int index = 7; index >= 0; --index)
        *value = (*value << 8) | raw[index];
    return 1;
}

static int save_write_manifest(
        const char *path, const GmRuntime *runtime,
        unsigned long long generation, unsigned mask) {
    static const unsigned char magic[8] = {
        'N','T','H','S','A','V','0','1'
    };
    FILE *stream = fopen(path, "wb");
    int ok = 0;
    if (!stream) return 0;
    if (fwrite(magic, 1, sizeof magic, stream) == sizeof magic
            && save_write_u32(stream, 1)
            && save_write_u32(stream, mask)
            && save_write_u64(stream, generation)
            && save_write_u64(stream, (uint64_t)runtime->seed)
            && save_write_u64(stream, (uint64_t)runtime->tick)
            && save_write_u32(stream, (uint32_t)runtime->world_type)
            && save_write_u32(stream, 0)
            && fflush(stream) == 0 && fsync(fileno(stream)) == 0)
        ok = 1;
    if (fclose(stream) != 0) ok = 0;
    return ok;
}

static int save_read_current(
        const char *root, const char *slot,
        unsigned long long *generation, int absent_ok) {
    char path[PATH_MAX], tail;
    FILE *stream;
    int ok;
    if (!save_path(path, "%s/%s/current", root, slot, 0)) return 0;
    stream = fopen(path, "rb");
    if (!stream) {
        if (absent_ok && errno == ENOENT) {
            *generation = 0;
            return 1;
        }
        return 0;
    }
    ok = fscanf(stream, "%16llx%c", generation, &tail) == 2
        && tail == '\n' && fgetc(stream) == EOF && *generation != 0;
    if (fclose(stream) != 0) ok = 0;
    return ok;
}

static int save_read_manifest(
        const char *root, const char *slot, unsigned long long generation,
        GmNativeSaveInfo *out) {
    static const unsigned char magic[8] = {
        'N','T','H','S','A','V','0','1'
    };
    char path[PATH_MAX];
    unsigned char found[8];
    uint32_t version, mask, world_type, reserved;
    uint64_t stored_generation, seed, tick;
    FILE *stream;
    int ok = 0;
    if (!save_path(path, "%s/%s/generation-%016llx/manifest.bin",
                   root, slot, generation)
            || !(stream = fopen(path, "rb")))
        return 0;
    if (fread(found, 1, sizeof found, stream) == sizeof found
            && memcmp(found, magic, sizeof magic) == 0
            && save_read_u32(stream, &version)
            && save_read_u32(stream, &mask)
            && save_read_u64(stream, &stored_generation)
            && save_read_u64(stream, &seed)
            && save_read_u64(stream, &tick)
            && save_read_u32(stream, &world_type)
            && save_read_u32(stream, &reserved)
            && fgetc(stream) == EOF && version == 1
            && mask != 0 && mask <= 7 && stored_generation == generation
            && world_type <= GM_WORLD_SUPERFLAT && reserved == 0)
        ok = 1;
    if (fclose(stream) != 0) ok = 0;
    if (!ok) return 0;
    memset(out, 0, sizeof *out);
    snprintf(out->slot, sizeof out->slot, "%s", slot);
    out->generation = generation;
    out->seed = (long long)seed;
    out->tick = (long long)tick;
    out->world_mask = mask;
    out->world_type = (GmWorldType)world_type;
    return 1;
}

int gm_native_save_info(
        const char *root, const char *slot, GmNativeSaveInfo *out,
        char *error, size_t error_cap) {
    unsigned long long generation;
    if (!root || !*root || !save_slot_valid(slot) || !out) {
        save_error(error, error_cap, "invalid native save slot");
        return 0;
    }
    if (!save_read_current(root, slot, &generation, 0)
            || !save_read_manifest(root, slot, generation, out)) {
        save_error(error, error_cap, "native save slot is incomplete: %s", slot);
        return 0;
    }
    return 1;
}

static void save_cleanup_generation(const char *directory) {
    char path[PATH_MAX];
    const char *files[] = {
        "runtime.bin", "player_statistics.json", "manifest.bin",
        "world_dim-1.bin", "world_dim0.bin", "world_dim1.bin",
    };
    for (size_t index = 0; index < sizeof files / sizeof files[0]; ++index) {
        if (snprintf(path, sizeof path, "%s/%s", directory, files[index])
                < (int)sizeof path)
            (void)remove(path);
    }
    (void)rmdir(directory);
}

int gm_native_save_write(
        GmRuntime *runtime, const char *root, const char *slot,
        char *error, size_t error_cap) {
    char slot_dir[PATH_MAX] = {0}, staging[PATH_MAX] = {0};
    char final[PATH_MAX] = {0}, path[PATH_MAX] = {0};
    char current_tmp[PATH_MAX] = {0}, current[PATH_MAX] = {0};
    char lock_path[PATH_MAX] = {0};
    unsigned long long prior = 0, generation;
    unsigned mask = 0;
    FILE *stream = NULL;
    int lock_fd = -1, renamed = 0, ok = 0;
    if (!runtime || !root || !*root || !save_slot_valid(slot)
            || !save_ensure_directory(root)
            || !save_path(slot_dir, "%s/%s", root, slot, 0)
            || !save_ensure_directory(slot_dir)
            || snprintf(lock_path, sizeof lock_path, "%s/write.lock", slot_dir)
                >= (int)sizeof lock_path
            || (lock_fd = open(lock_path, O_CREAT | O_RDWR, 0644)) < 0
            || flock(lock_fd, LOCK_EX) != 0) {
        if (lock_fd >= 0) close(lock_fd);
        save_error(error, error_cap, "could not lock native save slot");
        return 0;
    }
    if (!save_read_current(root, slot, &prior, 1)) {
        save_error(error, error_cap, "native save current pointer is corrupt");
        goto done;
    }
    generation = prior + 1;
    if (generation == 0) {
        save_error(error, error_cap, "native save generation overflow");
        goto done;
    }
    for (int index = 0; index < 3; ++index)
        if (runtime->worlds[index]) mask |= 1u << index;
    if (!mask) {
        save_error(error, error_cap, "native save has no worlds");
        goto done;
    }
    if (!runtime->player_statistics_present
            && !gm_runtime_restore_player_statistics(
                runtime, "{}", 2, runtime->tick, runtime->tick)) {
        save_error(error, error_cap, "could not initialize player statistics");
        goto done;
    }
    if (!save_path(final, "%s/%s/generation-%016llx",
                   root, slot, generation)
            || snprintf(staging, sizeof staging, "%s/.generation-%016llx-%ld",
                        slot_dir, generation, (long)getpid())
                >= (int)sizeof staging
            || mkdir(staging, 0755) != 0) {
        save_error(error, error_cap, "could not stage native save generation");
        goto done;
    }
    for (int index = 0; index < 3; ++index) {
        if (!(mask & (1u << index))) continue;
        if (snprintf(path, sizeof path, "%s/world_dim%d.bin",
                     staging, index - 1) >= (int)sizeof path
                || !gm_runtime_write_chunk_store_dim(
                    runtime, index - 1, path))
            goto write_failed;
    }
    if (snprintf(path, sizeof path, "%s/runtime.bin", staging)
                >= (int)sizeof path
            || !gm_runtime_write_checkpoint(runtime, path)
            || snprintf(path, sizeof path, "%s/player_statistics.json", staging)
                >= (int)sizeof path
            || !gm_runtime_write_player_statistics(runtime, path)
            || snprintf(path, sizeof path, "%s/manifest.bin", staging)
                >= (int)sizeof path
            || !save_write_manifest(path, runtime, generation, mask)
            || rename(staging, final) != 0)
        goto write_failed;
    renamed = 1;
    if (snprintf(current_tmp, sizeof current_tmp, "%s/current.tmp.%ld",
                 slot_dir, (long)getpid()) >= (int)sizeof current_tmp
            || snprintf(current, sizeof current, "%s/current", slot_dir)
                >= (int)sizeof current)
        goto write_failed;
    stream = fopen(current_tmp, "wb");
    if (!stream || fprintf(stream, "%016llx\n", generation) != 17
            || fflush(stream) != 0 || fsync(fileno(stream)) != 0) {
        if (stream) fclose(stream);
        stream = NULL;
        goto write_failed;
    }
    if (fclose(stream) != 0) {
        stream = NULL;
        goto write_failed;
    }
    stream = NULL;
    if (rename(current_tmp, current) != 0) goto write_failed;
    ok = 1;
    goto done;
write_failed:
    if (stream) fclose(stream);
    if (current_tmp[0]) (void)remove(current_tmp);
    if (renamed) save_cleanup_generation(final);
    else save_cleanup_generation(staging);
    save_error(error, error_cap, "native save write failed");
done:
    (void)flock(lock_fd, LOCK_UN);
    close(lock_fd);
    return ok;
}

int gm_native_save_load(
        GmRuntime *runtime, const GmConfig *template_config,
        const char *root, const char *slot,
        char *error, size_t error_cap) {
    GmNativeSaveInfo info;
    GmConfig config;
    GmRuntime *fresh;
    char generation_dir[PATH_MAX], path[PATH_MAX], init_error[256];
    if (!runtime || !template_config
            || !gm_native_save_info(root, slot, &info, error, error_cap))
        return 0;
    config = *template_config;
    config.seed = info.seed;
    config.world = info.world_type;
    fresh = (GmRuntime *)calloc(1, sizeof *fresh);
    if (!fresh) {
        save_error(error, error_cap, "native save load allocation failed");
        return 0;
    }
    if (!gm_runtime_init(fresh, &config, init_error, sizeof init_error)) {
        save_error(error, error_cap, "native save init: %s", init_error);
        free(fresh);
        return 0;
    }
    if (!save_path(generation_dir, "%s/%s/generation-%016llx",
                   root, slot, info.generation))
        goto fail;
    for (int index = 0; index < 3; ++index) {
        if (!(info.world_mask & (1u << index))) continue;
        if (snprintf(path, sizeof path, "%s/world_dim%d.bin",
                     generation_dir, index - 1) >= (int)sizeof path
                || !gm_runtime_attach_chunk_store_dim(fresh, index - 1, path))
            goto fail;
    }
    if (snprintf(path, sizeof path, "%s/runtime.bin", generation_dir)
                >= (int)sizeof path
            || !gm_runtime_load_checkpoint(fresh, path)
            || fresh->seed != info.seed || fresh->tick != info.tick
            || fresh->world_type != info.world_type)
        goto fail;
    gm_runtime_destroy(runtime);
    *runtime = *fresh;
    memset(fresh, 0, sizeof *fresh);
    free(fresh);
    return 1;
fail:
    gm_runtime_destroy(fresh);
    free(fresh);
    save_error(error, error_cap, "native save generation failed validation");
    return 0;
}

static int save_info_compare(const void *left, const void *right) {
    const GmNativeSaveInfo *a = (const GmNativeSaveInfo *)left;
    const GmNativeSaveInfo *b = (const GmNativeSaveInfo *)right;
    return strcmp(a->slot, b->slot);
}

int gm_native_save_list(
        const char *root, GmNativeSaveInfo *out, int max,
        char *error, size_t error_cap) {
    DIR *directory;
    struct dirent *entry;
    int count = 0;
    if (!root || !*root || !out || max < 0) {
        save_error(error, error_cap, "invalid native save list request");
        return -1;
    }
    directory = opendir(root);
    if (!directory) {
        if (errno == ENOENT) return 0;
        save_error(error, error_cap, "could not open native save root");
        return -1;
    }
    while ((entry = readdir(directory)) != NULL) {
        GmNativeSaveInfo info;
        if (!save_slot_valid(entry->d_name)) continue;
        if (!gm_native_save_info(root, entry->d_name, &info, NULL, 0))
            continue;
        if (count < max) out[count] = info;
        ++count;
    }
    closedir(directory);
    if (count > max) count = max;
    qsort(out, (size_t)count, sizeof *out, save_info_compare);
    return count;
}
