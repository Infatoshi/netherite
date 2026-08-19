#include "build.h"

#include <stdio.h>
#include <string.h>

static int is_world(const char *name)
{
    return strcmp(name, "atlas") == 0 ||
           strcmp(name, "colormap") == 0 ||
           strcmp(name, "loading") == 0 ||
           strcmp(name, "portal") == 0 ||
           strcmp(name, "sky") == 0 ||
           strcmp(name, "underwater") == 0 ||
           strcmp(name, "animations") == 0;
}

static int is_ui(const char *name)
{
    return strcmp(name, "gui") == 0 ||
           strcmp(name, "hand") == 0 ||
           strcmp(name, "hud") == 0 ||
           strcmp(name, "inventory") == 0 ||
           strcmp(name, "item") == 0 ||
           strcmp(name, "mob") == 0;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s --jar PATH [--out DIR] [--only NAME]\n"
            "NAME: atlas|colormap|loading|portal|sky|underwater|animations\n"
            "      gui|hand|hud|inventory|item|mob\n",
            argv0);
}

int main(int argc, char **argv)
{
    const char *jar_path = NULL;
    const char *out_dir = "assets";
    const char *only = NULL;
    AssetJar *jar;
    int i;
    int rc;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--jar") == 0 && i + 1 < argc) {
            jar_path = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_dir = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
            only = argv[++i];
            continue;
        }
        usage(argv[0]);
        return 2;
    }
    if (!jar_path) {
        usage(argv[0]);
        return 2;
    }

    jar = asset_jar_open(jar_path);
    if (!jar) {
        fprintf(stderr, "cannot open jar: %s\n", jar_path);
        return 1;
    }

    if (!only) {
        rc = asset_build_world(jar, out_dir, NULL);
        if (rc == 0)
            rc = asset_build_ui(jar, out_dir, NULL);
    } else if (is_world(only)) {
        rc = asset_build_world(jar, out_dir, only);
    } else if (is_ui(only)) {
        rc = asset_build_ui(jar, out_dir, only);
    } else {
        fprintf(stderr, "unknown --only name: %s\n", only);
        asset_jar_close(jar);
        return 1;
    }

    asset_jar_close(jar);
    if (rc != 0) {
        fprintf(stderr, "asset build failed\n");
        return 1;
    }
    return 0;
}
